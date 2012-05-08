/*
** $Id: liolib.c,v 2.73.1.3 2008/01/18 17:47:43 roberto Exp $
** Standard I/O (and system) library
** See Copyright Notice in lua.h
*/


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define liolib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#define IO_INPUT	1
#define IO_OUTPUT	2

#define IO_OPEN_CALLBACK    3
#define IO_CLOSE_CALLBACK   4
#define IO_READ_CALLBACK    5
#define IO_WRITE_CALLBACK   6
#define IO_SEEK_CALLBACK    7

typedef struct File File;

struct File
{
    void*   handle;
    char    buffer[LUAL_BUFFERSIZE];
    size_t  bufferLength;
    size_t  bufferPos;
};

static const char *const fnames[] = {"input", "output"};

static void* openfile(lua_State* L, const char* fileName, const char* mode)
{
    void* result;
    luaL_FileOpen open;
    lua_rawgeti(L, LUA_ENVIRONINDEX, IO_OPEN_CALLBACK);
    open = (luaL_FileOpen)lua_touserdata(L, -1);
    result = open(L, fileName, mode);
    lua_pop(L, 1);
    return result;
}

static int closefile(lua_State* L, void* handle)
{
    int result;
    lua_rawgeti(L, LUA_ENVIRONINDEX, IO_CLOSE_CALLBACK);
    result = ((luaL_FileClose)lua_touserdata(L, -1))(L, handle);
    lua_pop(L, 1);
    return result;
}

static long seekfile(lua_State* L, File* file, long offset, int origin)
{
    long result;
    luaL_FileSeek seek;
    lua_rawgeti(L, LUA_ENVIRONINDEX, IO_SEEK_CALLBACK);
    seek = (luaL_FileSeek)lua_touserdata(L, -1);
    result = seek(L, file, offset, origin);
    lua_pop(L, 1);
    file->bufferLength = 0;
    file->bufferPos = 0;
    return result;
}

/** This function makes sure that there is something in the file's buffer. */
static void ensurebuffer(lua_State* L, File* file)
{
    if (file->bufferLength == 0)
    {
        luaL_FileRead read;
        lua_rawgeti(L, LUA_ENVIRONINDEX, IO_READ_CALLBACK);
        read = (luaL_FileRead)lua_touserdata(L, -1);
        lua_pop(L, 1);
        file->bufferLength = read(L, file->handle, file->buffer, LUAL_BUFFERSIZE);
        file->bufferPos = 0;
    }
}

/** Fills the buffer with as much data as possible (discarding data already read) */
static void fillbuffer(lua_State* L, File* file)
{

    size_t remaining = LUAL_BUFFERSIZE - file->bufferLength;
    luaL_FileRead read;

    memcpy(file->buffer, file->buffer + file->bufferPos, file->bufferLength);
    file->bufferPos = 0;

    lua_rawgeti(L, LUA_ENVIRONINDEX, IO_READ_CALLBACK);
    read = (luaL_FileRead)lua_touserdata(L, -1);
    lua_pop(L, 1);
    
    file->bufferLength += read(L, file->handle, file->buffer + file->bufferLength, remaining);
    file->bufferPos = 0;

}


/** This is functionally equivalent to fread. */
static size_t readfile(lua_State* L, File* file, void* ptr, size_t size)
{
    size_t remaining = size;
    size_t result;
    do
    {
        size_t s = remaining;
        if (s > file->bufferLength)
        {
            s = file->bufferLength;
        }
        memcpy(ptr, file->buffer + file->bufferPos, s);
        file->bufferLength -= s;
        file->bufferPos += s;
        remaining -= s;
        ensurebuffer(L, file);
    }
    while (remaining > 0 && file->bufferLength != 0);
    result = size - remaining;
    return result;
}

static size_t writefile(lua_State* L, File* file, const void* src, size_t size)
{
    size_t result;
    lua_rawgeti(L, LUA_ENVIRONINDEX, IO_WRITE_CALLBACK);
    assert(!lua_isnil(L, -1));
    result = ((luaL_FileWrite)lua_touserdata(L, -1))(L, file->handle, src, size);
    lua_pop(L, 1);
    return result;
}

/** This is functionally equivalent to fgets.*/ 
char* readfileline(lua_State* L, File* file, char* dst, int num)
{

    char* p = dst; 

    while (num != 0)
    {

        size_t length;

        ensurebuffer(L, file);
        if (file->bufferLength == 0)
        {
            /* eof */
            if (p - dst == 0)
            {
                *p = 0;
                return NULL;
            }
            break;
        }
        
        length = file->bufferLength;
        while (file->bufferLength != 0 && num != 0)
        {
            char c = file->buffer[file->bufferPos];
            /* Skip carriage returns to handle different newline endings. */
            if (c != '\r')
            {
                *p = c;
                ++p;
                --num;
            }
            ++file->bufferPos;
            --file->bufferLength;
            if (c == '\n')
            {
                /* end of the line */
                *p = 0;
                return dst;
            }
        }
    
    }

    /* end of the buffer */
    *p = 0;
    return dst;

}

static int pushresult (lua_State *L, int i, const char *filename)
{
    int en = errno;  /* calls to Lua API may change this value */
    if (i)
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    if (filename)
    {
        lua_pushfstring(L, "%s: %s", filename, strerror(en));
    }
    else
    {
        lua_pushfstring(L, "%s", strerror(en));
    }
    lua_pushinteger(L, en);
    return 3;
}


static void fileerror (lua_State *L, int arg, const char *filename)
{
    lua_pushfstring(L, "%s: %s", filename, strerror(errno));
    luaL_argerror(L, arg, lua_tostring(L, -1));
}


#define tofilep(L)	((File*)luaL_checkudata(L, 1, LUA_FILEHANDLE))


static int io_type (lua_State *L)
{
    void *ud;
    luaL_checkany(L, 1);
    ud = lua_touserdata(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_FILEHANDLE);
    if (ud == NULL || !lua_getmetatable(L, 1) || !lua_rawequal(L, -2, -1))
        lua_pushnil(L);  /* not a file */
    else if (((File*)ud)->handle == NULL)
        lua_pushliteral(L, "closed file");
    else
        lua_pushliteral(L, "file");
    return 1;
}

static void* tofile (lua_State *L)
{
    File* f = tofilep(L);
    if (f->handle == NULL)
    {
        luaL_error(L, "attempt to use a closed file");
    }
    return f->handle;
}



/*
** When creating file handles, always creates a `closed' file handle
** before opening the actual file; so, if there is a memory error, the
** file is not left opened.
*/
static File* newfile (lua_State *L)
{
    File* pf = (File*)lua_newuserdata(L, sizeof(File));
    /* File handle is currently 'closed'. */
    pf->handle       = NULL;
    pf->bufferLength = 0;
    pf->bufferPos    = 0;
    luaL_getmetatable(L, LUA_FILEHANDLE);
    lua_setmetatable(L, -2);
    return pf;
}


/*
** function to (not) close the standard files stdin, stdout, and stderr
*/
static int io_noclose (lua_State *L) {
  lua_pushnil(L);
  lua_pushliteral(L, "cannot close standard file");
  return 2;
}


/*
** function to close 'popen' files
*/
static int io_pclose (lua_State *L)
{
    File* p = tofilep(L);
    int ok = lua_pclose(L, p->handle);
    p->handle = NULL;
    return pushresult(L, ok, NULL);
}


/*
** function to close regular files
*/
static int io_fclose (lua_State *L)
{
    File* p = tofilep(L);
    int ok = (closefile(L, p->handle) != 0);
    p->handle = NULL;
    return pushresult(L, ok, NULL);
}


static int aux_close (lua_State *L) {
  lua_getfenv(L, 1);
  lua_getfield(L, -1, "__close");
  return (lua_tocfunction(L, -1))(L);
}


static int io_close (lua_State *L) {
  if (lua_isnone(L, 1))
    lua_rawgeti(L, LUA_ENVIRONINDEX, IO_OUTPUT);
  tofile(L);  /* make sure argument is a file */
  return aux_close(L);
}


static int io_gc (lua_State *L)
{
    void* f = tofilep(L)->handle;
    /* ignore closed files */
    if (f != NULL)
    {
        aux_close(L);
    }
    return 0;
}


static int io_tostring (lua_State *L)
{
    void* f = tofilep(L)->handle;
    if (f == NULL)
    {
        lua_pushliteral(L, "file (closed)");
    }
    else
    {
        lua_pushfstring(L, "file (%p)", f);
    }
    return 1;
}


static int io_open (lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    const char *mode = luaL_optstring(L, 2, "r");
    File* pf = newfile(L);
    pf->handle = openfile(L, filename, mode);
    return (pf->handle == NULL) ? pushresult(L, 0, filename) : 1;
}


/*
** this function has a separated environment, which defines the
** correct __close for 'popen' files
*/
static int io_popen (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  File* pf = newfile(L);
  pf->handle = lua_popen(L, filename, mode);
  return (pf->handle == NULL) ? pushresult(L, 0, filename) : 1;
}


static int io_tmpfile (lua_State *L)
{
    File* pf = newfile(L);
    pf->handle = openfile(L, NULL, "wb+");
    return (pf->handle == NULL) ? pushresult(L, 0, NULL) : 1;
}


static File* getiofile (lua_State *L, int findex) {
  File *f;
  lua_rawgeti(L, LUA_ENVIRONINDEX, findex);
  f = (File*)lua_touserdata(L, -1);
  if (f == NULL)
    luaL_error(L, "standard %s file is closed", fnames[findex - 1]);
  return f;
}


static int g_iofile (lua_State *L, int f, const char *mode)
{
    if (!lua_isnoneornil(L, 1))
    {
        const char *filename = lua_tostring(L, 1);
        if (filename)
        {
            File* pf = newfile(L);
            pf->handle = openfile(L, filename, mode);
            if (pf->handle == NULL)
            {
                fileerror(L, 1, filename);
            }
        }
        else
        {
            tofile(L);  /* check that it's a valid file handle */
            lua_pushvalue(L, 1);
        }
        lua_rawseti(L, LUA_ENVIRONINDEX, f);
    }
    /* return current value */
    lua_rawgeti(L, LUA_ENVIRONINDEX, f);
    return 1;
}


static int io_input (lua_State *L) {
  return g_iofile(L, IO_INPUT, "r");
}


static int io_output (lua_State *L) {
  return g_iofile(L, IO_OUTPUT, "w");
}


static int io_readline (lua_State *L);


static void aux_lines (lua_State *L, int idx, int toclose) {
  lua_pushvalue(L, idx);
  lua_pushboolean(L, toclose);  /* close/not close file when finished */
  lua_pushcclosure(L, io_readline, 2);
}


static int f_lines (lua_State *L) {
  tofile(L);  /* check that it's a valid file handle */
  aux_lines(L, 1, 0);
  return 1;
}


static int io_lines (lua_State *L)
{
    /* no arguments? */
    if (lua_isnoneornil(L, 1))
    {  
        /* will iterate over default input */
        lua_rawgeti(L, LUA_ENVIRONINDEX, IO_INPUT);
        return f_lines(L);
    }
    else
    {
        const char *filename = luaL_checkstring(L, 1);
        File* pf = newfile(L);
        pf->handle = openfile(L, filename, "r");
        if (pf->handle == NULL)
        {
            fileerror(L, 1, filename);
        }
        aux_lines(L, lua_gettop(L), 1);
        return 1;
    }
}


/*
** {======================================================
** READ
** =======================================================
*/


static int read_number(lua_State *L, File* file)
{
    /** Maximum number of characters a number can take up. */
    const size_t maxNumberLength = 32;
    int n;
    lua_Number d;
    lua_assert(maxNumberLength < LUAL_BUFSIZE);
    if (file->bufferLength < maxNumberLength)
    {
        fillbuffer(L, file);
    }
    if (_snscanf(file->buffer + file->bufferPos, file->bufferLength, LUA_NUMBER_SCAN "%n", &d, &n) == 1)
    {
        file->bufferPos += n;
        file->bufferLength -= n;
        lua_pushnumber(L, d);
        return 1;
    }
    else return 0;  /* read fails */
}


static int test_eof (lua_State *L, File* f)
{
    ensurebuffer(L, f);
    return f->bufferLength == 0;
}


static int read_line (lua_State *L, File* f)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (;;)
    {
        size_t l;
        char *p = luaL_prepbuffer(&b);
        if (readfileline(L, f, p, LUAL_BUFFERSIZE) == NULL)
        {  /* eof? */
            luaL_pushresult(&b);  /* close buffer */
            return (lua_objlen(L, -1) > 0);  /* check whether read something */
        }
        l = strlen(p);
        if (l == 0 || p[l-1] != '\n')
        {
            luaL_addsize(&b, l);
        }
        else
        {
            luaL_addsize(&b, l - 1);  /* do not include `eol' */
            luaL_pushresult(&b);  /* close buffer */
            return 1;  /* read at least an `eol' */
        }
    }
}


static int read_chars (lua_State *L, File* f, size_t n)
{
    size_t rlen;  /* how much to read */
    size_t nr;  /* number of chars actually read */
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    rlen = LUAL_BUFFERSIZE;  /* try to read that much each time */
    do
    {
        char *p = luaL_prepbuffer(&b);
        if (rlen > n) rlen = n;  /* cannot read more than asked */
        nr = readfile(L, f, p, rlen);
        luaL_addsize(&b, nr);
        n -= nr;  /* still have to read `n' chars */
    }
    while (n > 0 && nr == rlen);  /* until end of count or eof */
    luaL_pushresult(&b);  /* close buffer */
    return (n == 0 || lua_objlen(L, -1) > 0);
}


static int g_read (lua_State *L, File* f, int first) {
  int nargs = lua_gettop(L) - 1;
  int success;
  int n;
  if (nargs == 0) {  /* no arguments? */
    success = read_line(L, f);
    n = first+1;  /* to return 1 result */
  }
  else {  /* ensure stack space for all results and for auxlib's buffer */
    luaL_checkstack(L, nargs+LUA_MINSTACK, "too many arguments");
    success = 1;
    for (n = first; nargs-- && success; n++) {
      if (lua_type(L, n) == LUA_TNUMBER) {
        size_t l = (size_t)lua_tointeger(L, n);
        success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
      }
      else {
        const char *p = lua_tostring(L, n);
        luaL_argcheck(L, p && p[0] == '*', n, "invalid option");
        switch (p[1]) {
          case 'n':  /* number */
            success = read_number(L, f);
            break;
          case 'l':  /* line */
            success = read_line(L, f);
            break;
          case 'a':  /* file */
            read_chars(L, f, ~((size_t)0));  /* read MAX_SIZE_T chars */
            success = 1; /* always success */
            break;
          default:
            return luaL_argerror(L, n, "invalid format");
        }
      }
    }
  }
  if (!success) {
    lua_pop(L, 1);  /* remove last result */
    lua_pushnil(L);  /* push nil instead */
  }
  return n - first;
}

static int io_read (lua_State *L)
{
    lua_assert(0);
    return 0;
    //return g_read(L, getiofile(L, IO_INPUT), 1);
}

static int f_read (lua_State *L)
{
    return g_read(L, tofilep(L), 2);
}

static int io_readline (lua_State *L)
{
    File* f = ((File*)lua_touserdata(L, lua_upvalueindex(1)));
    int sucess;
    if (f == NULL)
    {
        /* file is already closed? */
        luaL_error(L, "file is already closed");
    }
    sucess = read_line(L, f);
    if (sucess)
    {
        return 1;
    }
    else
    {
        /* EOF */
        if (lua_toboolean(L, lua_upvalueindex(2)))
        {  
            /* generator created file? */
            lua_settop(L, 0);
            lua_pushvalue(L, lua_upvalueindex(1));
            aux_close(L);  /* close it */
        }
        return 0;
    }
}

/* }====================================================== */


static int g_write (lua_State *L, File* file, int arg) {
  int nargs = lua_gettop(L) - 1;
  int status = 1;
  for (; nargs--; arg++) {
      size_t l;
      const char *s = luaL_checklstring(L, arg, &l);
      status = status && (writefile(L, file, s, l) == l);
  }
  return pushresult(L, status, NULL);
}


static int io_write (lua_State *L) {
    return g_write(L, getiofile(L, IO_OUTPUT), 1);
}


static int f_write (lua_State *L) {
  return g_write(L, tofilep(L), 2);
}


static int f_seek (lua_State *L)
{
    static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    static const char *const modenames[] = {"set", "cur", "end", NULL};
    File* file = tofilep(L);
    int op = luaL_checkoption(L, 2, "cur", modenames);
    long offset = luaL_optlong(L, 3, 0);
    op = seekfile(L, file, offset, mode[op]);
    if (op < 0)
    {
        return pushresult(L, 0, NULL);  /* error */
    }
    else
    {
        lua_pushinteger(L, op);
        return 1;
    }
}


static int f_setvbuf (lua_State *L)
{
    /*
    static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
    static const char *const modenames[] = {"no", "full", "line", NULL};
    FILE *f = tofile(L);
    int op = luaL_checkoption(L, 2, NULL, modenames);
    lua_Integer sz = luaL_optinteger(L, 3, LUAL_BUFFERSIZE);
    int res = setvbuf(f, NULL, mode[op], sz);
    return pushresult(L, res == 0, NULL);
    */
    return pushresult(L, 1, NULL);
}

static int io_flush (lua_State *L)
{
    /*return pushresult(L, fflush(getiofile(L, IO_OUTPUT)) == 0, NULL);*/
    return pushresult(L, 1, NULL);
}


static int f_flush (lua_State *L)
{
    /*return pushresult(L, fflush(tofile(L)) == 0, NULL);*/
    return pushresult(L, 1, NULL);
}


static const luaL_Reg iolib[] = {
  {"close", io_close},
  {"flush", io_flush},
  {"input", io_input},
  {"lines", io_lines},
  {"open", io_open},
  {"output", io_output},
  /*{"popen", io_popen},*/
  {"read", io_read},
  {"tmpfile", io_tmpfile},
  {"type", io_type},
  {"write", io_write},
  {NULL, NULL}
};


static const luaL_Reg flib[] = {
  {"close", io_close},
  {"flush", f_flush},
  {"lines", f_lines},
  {"read", f_read},
  {"seek", f_seek},
  {"setvbuf", f_setvbuf},
  {"write", f_write},
  {"__gc", io_gc},
  {"__tostring", io_tostring},
  {NULL, NULL}
};


static void createmeta (lua_State *L)
{
    luaL_newmetatable(L, LUA_FILEHANDLE);  /* create metatable for file handles */
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, flib);  /* file methods */
}


static void createstdfile (lua_State *L, FILE *f, int k, const char *fname)
{
    newfile(L)->handle = f;
    if (k > 0)
    {
        lua_pushvalue(L, -1);
        lua_rawseti(L, LUA_ENVIRONINDEX, k);
    }
    lua_pushvalue(L, -2);  /* copy environment */
    lua_setfenv(L, -2);  /* set it */
    lua_setfield(L, -3, fname);
}

static void setenvcallbacks(lua_State* L, luaL_FileCallbacks* callbacks)
{
    lua_pushlightuserdata(L, callbacks->open);
    lua_rawseti(L, LUA_ENVIRONINDEX, IO_OPEN_CALLBACK);
    lua_pushlightuserdata(L, callbacks->close);
    lua_rawseti(L, LUA_ENVIRONINDEX, IO_CLOSE_CALLBACK);
    lua_pushlightuserdata(L, callbacks->read);
    lua_rawseti(L, LUA_ENVIRONINDEX, IO_READ_CALLBACK);
    lua_pushlightuserdata(L, callbacks->write);
    lua_rawseti(L, LUA_ENVIRONINDEX, IO_WRITE_CALLBACK);
    lua_pushlightuserdata(L, callbacks->seek);
    lua_rawseti(L, LUA_ENVIRONINDEX, IO_SEEK_CALLBACK);
}

static void newfenv (lua_State *L, lua_CFunction cls)
{
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, cls);
    lua_setfield(L, -2, "__close");
}

static void* stdio_open(lua_State* L, const char* fileName, const char* mode)
{
    if (fileName != NULL)
    {
        return fopen(fileName, mode);
    }
    else
    {
        return tmpfile();
    }
}

static int stdio_close(lua_State* L, void* handle)
{
    return fclose(handle) == 0;
}

static size_t stdio_read(lua_State* L, void* handle, void* dst, size_t size)
{
    return fread(dst, 1, size, handle);
}

static size_t stdio_write(lua_State* L, void* handle, const void* src, size_t size)
{
    return fwrite(src, 1, size, handle);
}

static long stdio_seek(lua_State* L, void* handle, long offset, int origin)
{
    if (!fseek(handle, offset, origin))
    {
        return -1;
    }
    return ftell(handle);
}

LUALIB_API int luaopen_io (lua_State *L)
{
    luaL_FileCallbacks callbacks;
    callbacks.open  = stdio_open;
    callbacks.close = stdio_close;
    callbacks.read  = stdio_read;
    callbacks.write = stdio_write;
    callbacks.seek  = stdio_seek;
    return luaopen_iocallbacks(L, &callbacks);
}

LUALIB_API int luaopen_iocallbacks(lua_State *L, luaL_FileCallbacks* callbacks)
{
    /* create (private) environment with the callbacks */
    lua_newtable(L);
    lua_replace(L, LUA_ENVIRONINDEX);
    setenvcallbacks(L, callbacks);
    createmeta(L);
    /* create (private) environment (with fields IO_INPUT, IO_OUTPUT, __close) */
    newfenv(L, io_fclose);
    lua_replace(L, LUA_ENVIRONINDEX);
    setenvcallbacks(L, callbacks);
    /* open library */
    luaL_register(L, LUA_IOLIBNAME, iolib);
    /* create (and set) default files */
    newfenv(L, io_noclose);  /* close function for default files */
    createstdfile(L, stdin, IO_INPUT, "stdin");
    createstdfile(L, stdout, IO_OUTPUT, "stdout");
    createstdfile(L, stderr, 0, "stderr");
    setenvcallbacks(L, callbacks);
    lua_pop(L, 1);  /* pop environment for default files */
    lua_getfield(L, -1, "popen");
    newfenv(L, io_pclose);  /* create environment for 'popen' */
    lua_setfenv(L, -2);  /* set fenv for 'popen' */
    lua_pop(L, 1);  /* pop 'popen' */
    return 1;
}