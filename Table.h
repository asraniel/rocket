/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#ifndef ROCKETVM_TABLE_H
#define ROCKETVM_TABLE_H

#include "State.h"
#include "Gc.h"

struct TableNode
{
    Value          key;
    TableNode*     next;
    Value          value;
};

struct Table : public Gc_Object
{
    int             numNodes;
    TableNode*      nodes;
    Table*          metatable;
};

extern "C" Table* Table_Create(lua_State* L);
void   Table_Destroy(lua_State* L, Table* table);

/**
 * Updates the value for the key in the table. If the key does not exist
 * in the table, the function has no effect and returns false. If the value
 * is nil, 
 */
bool Table_Update(lua_State* L, Table* table, Value* key, Value* value);

/**
 * Inserts a new key, value pair into the table. The key is assumed to not
 * exist in the table already.
 */
void Table_Insert(lua_State* L, Table* table, Value* key, Value* value);

void Table_SetTable(lua_State* L, Table* table, int key, Value* value);
void Table_SetTable(lua_State* L, Table* table, const char* key, Value* value);
void Table_SetTable(lua_State* L, Table* table, Value* key, Value* value);

Value* Table_GetTable(lua_State* L, Table* table, const Value* key);
Value* Table_GetTable(lua_State* L, Table* table, int key);
Value* Table_GetTable(lua_State* L, Table* table, String* key);

int Table_GetSize(Table* table);

// The key will be updated to the next key.
const Value* Table_Next(Table* table, Value* key);

#endif