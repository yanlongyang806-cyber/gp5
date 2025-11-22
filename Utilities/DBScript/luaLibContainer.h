#pragma once

#include "GlobalTypeEnum.h"
#include "luaScriptLib.h"
#include "luaInternals.h"
#include "objContainer.h"

void luaLibContainerRegister(LuaContext *pLuaContext);

bool luaScriptIsSane(LuaContext *pLuaContext);
void luaInit(LuaContext *pLuaContext, const char *scriptFilename, const char *snapshotFilename, const char *offlineSnapshotFilename, int numThreads);
void luaDeinit(LuaContext *pLuaContext);
void luaBegin(LuaContext *pLuaContext);
bool luaProcessContainer(LuaContext *pLuaContext, Container *con, U32 uContainerModifiedTime);
void luaEnd(LuaContext *pLuaContext);

// Forward Declarations used by luaTagContainer
int xtype_helper(lua_State * L, const char * xpath, Container * con);
int xvalue_helper(lua_State * L, const char * xpath, Container * con);
int xcount_helper(lua_State * L, const char * xpath, Container * con);
int xindices_helper(lua_State * L, const char * xpath, Container * con);
int xmembers_helper(lua_State * L, const char * xpath, Container * con);
int freshness_helper(lua_State *L, U32 uModTime);
int savecon_helper(lua_State * L, const char * filename, Container * con);