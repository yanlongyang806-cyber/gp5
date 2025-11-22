#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")

#include "sysutil.h"
#include "file.h"

void testLua();

int main(int argc,char **argv)
{
	WAIT_FOR_DEBUGGER
	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS

	testLua();

	EXCEPTION_HANDLER_END
}













// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
#include "luaScriptLib.h"
#include "luaInternals.h"

// -------------------------------------------------------------------------
// Object/Tag Example

static tag_int TAG_TESTOBJ;

static int obj1 = 1000;
static int obj2 = 2000;

GLUA_FUNC( get_obj1 )
{
	glua_pushUserdata_ptr(&obj1, TAG_TESTOBJ);
}
GLUA_END

GLUA_FUNC( get_obj2 )
{
	glua_pushUserdata_ptr(&obj2, TAG_TESTOBJ);
}
GLUA_END

GLUA_FUNC( testobj_gettable )
{
	int *pObj = glua_getUserdata(1, TAG_TESTOBJ);
	const char *memberName = glua_getString(2);

	if(pObj && memberName)
	{
		if(!strcmp(memberName, "name"))
		{
			// Obviously we'd be looking inside of the ref's member here,
			// or the strcmp would be a TPI lookup, etc. However you want.
			switch(*pObj)
			{
				xcase 1000: glua_pushString("One Thousand");
				xcase 2000: glua_pushString("Two Thousand");
				default:    glua_pushString("No Idea");
			}
		}
		else
		{
			// No such member variable ... handle however you want
			glua_pushNil();
		}
	}
	else
	{
		// Something terrible is going on!
		glua_pushNil();
	}
}
GLUA_END

void luaRegisterTestObj(LuaContext *pLuaContext)
{
	lua_State *_lua_ = pLuaContext->luaState;

	GLUA_DECL( 0 ) // global
	{
		glua_tag("testobj", &TAG_TESTOBJ),
		glua_tagmethod(TAG_TESTOBJ, "gettable", testobj_gettable),

		glua_func( get_obj1 ),
		glua_func( get_obj2 )
	}
	GLUA_DECL_END
}

// -------------------------------------------------------------------------
// Function Module Example

GLUA_FUNC( global_print )
{
	GLUA_ARG_COUNT_CHECK( print_some_text, 1, 1 );                      // Runtime Error if there isn't precisely one argument
	GLUA_ARG_CHECK( print_some_text, 1, GLUA_TYPE_STRING, false, "" );  // Runtime Error if the only argument isn't a string
	{
		const char *text = glua_getString(1);
		printf("%s", text);
	}
}
GLUA_END

GLUA_FUNC( test_print )
{
	GLUA_ARG_COUNT_CHECK( print_some_text, 1, 1 );                      // Runtime Error if there isn't precisely one argument
	GLUA_ARG_CHECK( print_some_text, 1, GLUA_TYPE_STRING, false, "" );  // Runtime Error if the only argument isn't a string
	{
		const char *text = glua_getString(1);
		printf("%s", text);
	}
}
GLUA_END

void RegisterGlobalFuncs(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = 0;

	GLUA_DECL( namespaceTbl )
	{
		glua_func( global_print )
	}
	GLUA_DECL_END
}

void RegisterTestFuncs(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "test", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( test_print )
	}
	GLUA_DECL_END
}

// -------------------------------------------------------------------------
// Variable manipulation

void setGlobalStringVar(LuaContext *pContext, const char *var, const char *val)
{
	lua_State *L = pContext->luaState;
	lua_pushstring(L, val);
	lua_setglobal(L, var);
}

void getGlobalStringVar(LuaContext *pContext, const char *var, char **estr)
{
	lua_State *L = pContext->luaState;
	lua_getglobal(L, var);

	if(!lua_isnil(L, -1))
		estrCopy2(estr, lua_tostring(L, -1));

	lua_pop(L, -1);
}

void pokeGlobalVars(LuaContext *pContext)
{
	char *temp = NULL;
	lua_State *L = pContext->luaState;

	setGlobalStringVar(pContext, "woovar", "wooval!");
	getGlobalStringVar(pContext, "woovar", &temp);

	if(temp)
		printf("Global variable 'woovar' is: %s\n", temp);
	else
		printf("Failed to query global variable 'woovar'.\n");

	estrDestroy(&temp);
}

void dumpGlobalVars(LuaContext *pContext)
{
	lua_State *L = pContext->luaState;

	printf("---   GLOBAL VARS   ---\n");

	// lua_next() walks a table one entry from whatever key is on the top (-1 index) of
	// the stack. If you start with "nil", it walks to the first element of the table. 
	// lua_next() pops the key on the top (-1), and replaces it with a key/value pair of
	// that table entry (at [-2, -1], respectively). This code finds out what type the 
	// key and value are and acts accordingly, and then ensures whatever key we just 
	// dumped is sitting on the top of the stack for the next call to lua_next().

	// To Alex: -- Printing out LUA_TSTRING/LUA_TNUMBER entries might be all you need.

	lua_pushnil(L);

	while (lua_next(L, LUA_GLOBALSINDEX))
	{
		if(lua_type(L, -2) == LUA_TSTRING)
		{
			int varType = lua_type(L, -1);
			printf("%s", lua_tostring(L, -2));
			switch(varType)
			{
			case LUA_TNIL:           printf(" = nil\n"); break;
			case LUA_TBOOLEAN:       printf(" = %s\n", lua_toboolean(L, -1) ? "true" : "false"); break;
			case LUA_TLIGHTUSERDATA: printf(" ~ [lightuserdata]\n"); break;
			case LUA_TNUMBER:        printf(" = %2.2f\n", lua_tonumber(L, -1)); break;
			case LUA_TSTRING:        printf(" = %s\n", lua_tostring(L, -1)); break;
			case LUA_TTABLE:         printf(" ~ [table]\n"); break;
			case LUA_TFUNCTION:      printf("()\n"); break;
			case LUA_TUSERDATA:      printf(" ~ [userdata]\n"); break;
			case LUA_TTHREAD:        printf(" ~ [thread]\n"); break;
			default:                 printf("???\n"); break;
			}
		}
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	printf("---  END GLOBAL VARS   ---\n");
}

// -------------------------------------------------------------------------
// Call Lua Function

bool CallGlobalFuncWithNoArgs(LuaContext *pContext, const char *funcName)
{
	lua_State *L = pContext->luaState;

	lua_getglobal(L, funcName); // Push the function (by name) onto the top of the stack
	if(!lua_isfunction(L, -1))        // If funcName isn't actually a global function ptr...
	{
		printf("%s() isn't a global function!\n", funcName);
		return false;
	}

	// If you wanted to have args, you would push them here, in order

	return luaContextCall(pContext, 0);
}

// -------------------------------------------------------------------------
// Basic load/execution

void testLua()
{
	int len;
	char *p;

	LuaContext *pLuaContext = luaContextCreate();
	luaRegisterTestObj(pLuaContext);
	RegisterGlobalFuncs(pLuaContext);
	RegisterTestFuncs(pLuaContext);

	pokeGlobalVars(pLuaContext);
	dumpGlobalVars(pLuaContext);

	p = fileAlloc("test.lua", &len);
	if(p && *p)
	{
		if(luaContextLoadScript(pLuaContext, p, len))
		{
			if(!CallGlobalFuncWithNoArgs(pLuaContext, "showTest"))
			{
				printf("Runtime Error: %s\n", luaContextGetLastError(pLuaContext));
			}
		}
		else
		{
			printf("Load Error: %s\n", luaContextGetLastError(pLuaContext));
		}

		free(p);
	}
	else
	{
		printf("ERROR: Couldn't load test.lua\n");
	}

	luaContextDestroy(&pLuaContext);
}
