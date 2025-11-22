#include "file.h"
#include "estring.h"
#include "earray.h"
#include "luaCmdLine.h"
#include "luaCmdLine_c_ast.h"

void luaInsertVar(LuaContext *pLuaContext, const char *var, const char *val);
void luaPokeVar(LuaContext *pLuaContext, const char *var, const char *val);

AUTO_STRUCT;
typedef struct LuaCmdLineVar
{
	bool bInsert;
	char *var; AST(ESTRING) 
	char *val; AST(ESTRING)
} LuaCmdLineVar;

static LuaCmdLineVar **sppVars = NULL;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(set) ACMD_CMDLINE;
void luaCmdLineSetVar(const char *var, const char *val)
{
	LuaCmdLineVar *pVar = StructAlloc(parse_LuaCmdLineVar);

	pVar->bInsert = false;
	estrCopy2(&pVar->var, var);
	estrCopy2(&pVar->val, val);
	eaPush(&sppVars, pVar);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(insert) ACMD_CMDLINE;
void luaCmdLineInsertVar(const char *var, const char *val)
{
	LuaCmdLineVar *pVar = StructAlloc(parse_LuaCmdLineVar);

	pVar->bInsert = true;
	estrCopy2(&pVar->var, var);
	estrCopy2(&pVar->val, val);
	eaPush(&sppVars, pVar);
}

void luaCmdLineSetAllVars(LuaContext *pLuaContext)
{
	FOR_EACH_IN_EARRAY_FORWARDS(sppVars, LuaCmdLineVar, pVar);
	{
		if(pVar->bInsert)
		{
			luaInsertVar(pLuaContext, pVar->var, pVar->val);
		}
		else
		{
			luaPokeVar(pLuaContext, pVar->var, pVar->val);
		}
	}
	FOR_EACH_END;
}

void luaInsertVar(LuaContext *pLuaContext, const char *var, const char *val)
{
	bool isNumber;
	lua_State *L = pLuaContext->luaState;
	int iVal = atoi(val);
	int n = 0;

	lua_getglobal(L, var);
	if(lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, var);
		lua_getglobal(L, var);
	}

	isNumber = iVal || (val[0] == '0' && val[1] == 0);
	
	for(;;)
	{
		lua_rawgeti(L, -1, ++n);
		if(lua_isnil(L, -1))
			break;
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	lua_pushnumber(L, n);
	lua_pushstring(L, val);
	if(isNumber)
	{
		lua_tonumber(L, -1);
	}
	lua_settable(L, -3);
	lua_pop(L, 1);

	fprintf(fileGetStderr(), "Inserting into Lua table '%s' (%s): %s\n", var, isNumber ? "number" : "string", val);
}

void luaPokeVar(LuaContext *pLuaContext, const char *var, const char *val)
{
	bool isNumber;
	lua_State *L = pLuaContext->luaState;

	lua_getglobal(L, var);
	isNumber = (lua_isnumber(L, -1) != 0);
	lua_pop(L, 1);

	lua_pushstring(L, val);
	if(isNumber)
	{
		lua_tonumber(L, -1);
	}
	lua_setglobal(L, var);

	fprintf(fileGetStderr(), "Setting Lua variable '%s' (%s): %s\n", var, isNumber ? "number" : "string", val);
}

#include "luaCmdLine_c_ast.c"
