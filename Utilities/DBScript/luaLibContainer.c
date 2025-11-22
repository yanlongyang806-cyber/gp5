#include "timing_profiler_interface.h"
#include "wininclude.h"
#include <shellapi.h>

#include "file.h"
#include "hoglib.h"
#include "timing.h"
#include "luaInternals.h"
#include "luaLibIntrinsics.h"
#include "luaLibContainer.h"
#include "luaTagContainer.h"
#include "luaCmdLine.h"
#include "objContainerIO.h"
#include "tokenstore.h"
#include "sharedLibContainer.h"
#include "WalkSnapshot.h"
#include "UTF8.h"

#include <math.h>

static Container  *spContainer  = NULL;
static U32 suContainerModifiedTime = 0;

static char *spScriptFilename   = NULL;
static char *spSnapshotFilename = NULL;
static char *spOfflineSnapshotFilename = NULL;

static int gNumThreads = 0;

extern ParseTable parse_ContainerRestoreState[];
#define TYPE_parse_ContainerRestoreState ContainerRestoreState
extern char gSnapshotFilename[MAX_PATH];
extern char gOfflineFilename[MAX_PATH];

// ---------------------------------------------------------------------------------------
// WalkSnapshot + Callback

static bool luaIsGlobalFunc(LuaContext *pContext, const char *funcName)
{
	bool ret;
	lua_State *L = pContext->luaState;
	lua_getglobal(L, funcName);
	ret = lua_isfunction(L, -1);
	lua_pop(L, 1);
	return ret;
}

static bool luaCallGlobalFuncWithNoArgs(LuaContext *pContext, const char *funcName)
{
	lua_State *L = pContext->luaState;

	lua_getglobal(L, funcName);
	if(!lua_isfunction(L, -1))
	{
		fprintf(fileGetStderr(), "%s() isn't a global function!\n", funcName);
		return false;
	}

	return luaContextCall(pContext, 0);
}
bool luaScriptIsSane(LuaContext * pLuaContext)
{
	bool ret = true;

	if(!luaIsGlobalFunc(pLuaContext, "Begin"))
	{
		fprintf(fileGetStderr(), "Warning: Begin() function absent.\n");
	}
	if(!luaIsGlobalFunc(pLuaContext, "Process"))
	{
		fprintf(fileGetStderr(), "ERROR: Process() function absent.\n"); // The program is effectively worthless without this!
		ret = false;
	}
	if(!luaIsGlobalFunc(pLuaContext, "End"))
	{
		fprintf(fileGetStderr(), "Warning: End() function absent.\n");
	}

	return ret;
}

void luaInit(LuaContext * pLuaContext, const char * scriptFilename, const char * snapshotFilename, const char * offlineSnapshotFilename, int numThreads)
{
	estrCopy2(&spScriptFilename,   scriptFilename);
	estrCopy2(&spSnapshotFilename, snapshotFilename);
	estrCopy2(&spOfflineSnapshotFilename, offlineSnapshotFilename);
	gNumThreads = numThreads;

	
}

void luaDeinit(LuaContext * pLuaContext)
{
}

void luaBegin(LuaContext *pLuaContext)
{
	PERFINFO_AUTO_START("luaCallGlobalFuncWithNoArgs(End)", 1);
	if(!luaCallGlobalFuncWithNoArgs(pLuaContext, "Begin"))
	{
		fprintf(fileGetStderr(), "Runtime Error during Begin(): %s\n", luaContextGetLastError(pLuaContext));
	}
	PERFINFO_AUTO_STOP();

	luaTagContainerCleanup();
}

bool luaProcessContainer(LuaContext *pLuaContext, Container *con, U32 uContainerModifiedTime)
{
	bool ret = true;

	spContainer             = con;
	suContainerModifiedTime = timeGetSecondsSince2000FromWindowsTime32(uContainerModifiedTime);

	PERFINFO_AUTO_START("luaCallGlobalFuncWithNoArgs(Process)", 1);
	if(!luaCallGlobalFuncWithNoArgs(pLuaContext, "Process"))
	{
		fprintf(fileGetStderr(), "Runtime Error during Process(): %s\n", luaContextGetLastError(pLuaContext));
		ret = false;
	}
	PERFINFO_AUTO_STOP();

	luaTagContainerCleanup();
	return ret;
}

void luaEnd(LuaContext *pLuaContext)
{
	PERFINFO_AUTO_START("luaCallGlobalFuncWithNoArgs(End)", 1);
	if(!luaCallGlobalFuncWithNoArgs(pLuaContext, "End"))
	{
		fprintf(fileGetStderr(), "Runtime Error during End(): %s\n", luaContextGetLastError(pLuaContext));
	}
	PERFINFO_AUTO_STOP();

	luaTagContainerCleanup();
}

// ---------------------------------------------------------------------------------------
// Lua Helpers

int xtype_helper(lua_State * L, const char * xpath, Container * con)
{
	VarType varType = xtype(xpath, con, NULL);
	if(varType != VARTYPE_UNKNOWN)
	{
		lua_pushinteger(L, (U32)varType);
		return 1;
	}
	return 0;
}

int xvalue_helper(lua_State * L, const char * xpath, Container * con)
{
	static char * result = NULL;

	estrCopy2(&result, "");
	if(objPathGetEString(xpath, con->containerSchema->classParse, con->containerData, &result))
	{
		lua_pushstring(L, result);
	}
	else
	{
		lua_pushstring(L, "");
	}
	return 1;
}

int xcount_helper(lua_State * L, const char * xpath, Container * con)
{
	int count = xcount(xpath, con);
	lua_pushinteger(L, count);
	return 1;
}

// This is the closure function that Lua's for loop uses internally
// for index in xindices(xpath) do ... end
static int xindices_aux(lua_State * L)
{
	XPathLookup l;
	int i, numelems, indexed, keyfield;
	ParseTable* subtable = NULL;
	char buf[MAX_TOKEN_LENGTH];

	l.tpi    = (void*)lua_topointer(L, lua_upvalueindex(1));
	l.ptr    = (void*)lua_topointer(L, lua_upvalueindex(2));
	l.column = lua_tointeger(L, lua_upvalueindex(3));
	numelems = lua_tointeger(L, lua_upvalueindex(4));
	indexed  = lua_tointeger(L, lua_upvalueindex(5));
	i        = lua_tointeger(L, lua_upvalueindex(6));

	if(!l.tpi)
		return 0;

	if(indexed)
	{
		if(i < numelems)
		{
			void* substruct = StructGetSubtable(l.tpi, l.column, l.ptr, i, &subtable, NULL);
			if (!substruct)
				return 0;
			keyfield = ParserGetTableKeyColumn(subtable);
			assertmsg(keyfield >= 0, "Some polymorph types of have a key field, but some do not?? BAD");
			if (TokenToSimpleString(subtable, keyfield, substruct, SAFESTR(buf), false))
			{			
				lua_pushinteger(L, ++i);
				lua_replace(L, lua_upvalueindex(6));

				lua_pushstring(L, buf);
				return 1;
			}
		}
	}
	else
	{
		if(i < numelems)
		{
			lua_pushinteger(L, ++i);
			lua_replace(L, lua_upvalueindex(6));

			lua_pushinteger(L, i-1);
			return 1;
		}
	}

	return 0;
}

int xindices_helper(lua_State * L, const char * xpath, Container * con)
{
	XPathLookup l = {0};
	int numelems = 0;
	int indexed = 0;

	if (xlookup(xpath, con, &l, false))
	{
		numelems = TokenStoreGetNumElems(l.tpi, l.column, l.ptr, NULL);

		indexed = ParserColumnIsIndexedEArray(l.tpi, l.column, NULL);

		// Setup all of our upvalues for the xmembers_aux() closure
		lua_pushlightuserdata(L, l.tpi);
		lua_pushlightuserdata(L, l.ptr);
		lua_pushinteger(L, l.column);
		lua_pushinteger(L, numelems);
		lua_pushinteger(L, indexed);
		lua_pushinteger(L, 0);

		lua_pushcclosure(L, xindices_aux, 6);
		return 1;
	}
	return 0;
}

// This is the closure function that Lua's for loop uses internally
// for member in xmembers(xpath) do ... end
static int xmembers_aux(lua_State * L)
{
	XPathLookup l;
	int i;
	l.tpi    = (void*)lua_topointer(L, lua_upvalueindex(1));
	l.ptr    = (void*)lua_topointer(L, lua_upvalueindex(2));
	i        =        lua_tointeger(L, lua_upvalueindex(3));

	if(!l.tpi)
		return 0;

	while(l.tpi[i].type || (l.tpi[i].name && l.tpi[i].name[0]))
	{
		int type = TOK_GET_TYPE(l.tpi[i].type);
		if (type == TOK_START)   { i++; continue; }
		if (type == TOK_END)     { i++; continue; }
		if (type == TOK_IGNORE)  { i++; continue; }
		if (type == TOK_COMMAND) { i++; continue; }

		lua_pushinteger(L, ++i);
		lua_replace(L, lua_upvalueindex(3));

		lua_pushstring(L, l.tpi[i-1].name);
		return 1;
	}

	return 0;
}

int xmembers_helper(lua_State * L, const char * xpath, Container * con)
{
	XPathLookup l = {0};
	xlookup(xpath, con, &l, true);

	// Setup all of our upvalues for the xmembers_aux() closure
	lua_pushlightuserdata(L, l.tpi);
	lua_pushlightuserdata(L, l.ptr);
	lua_pushinteger(L, 0);

	lua_pushcclosure(L, xmembers_aux, 3);
	return 1;
}

int freshness_helper(lua_State *L, U32 uModTime)
{
	// Time, in days (floating-point), since the container was last saved
	F32 freshness = (timeSecondsSince2000() - uModTime) / SECONDS_PER_DAY;
	lua_pushnumber(L, freshness);
	return 1;
}

int savecon_helper(lua_State * L, const char * filename, Container * con)
{
	int ret = ParserWriteTextFile(filename, con->containerSchema->classParse, con->containerData, 0, 0);
	lua_pushinteger(L, ret);
	return 1;
}

// ---------------------------------------------------------------------------------------
// Global Lua Function Calls

static int cl_xtype(lua_State * L)
{
	return xtype_helper(L, luaL_checkstring(L, 1), spContainer);
}

static int cl_xvalue(lua_State * L)
{
	return xvalue_helper(L, luaL_checkstring(L, 1), spContainer);
}

static int cl_xcount(lua_State * L)
{
	return xcount_helper(L, luaL_checkstring(L, 1), spContainer);
}

static int cl_xindices(lua_State * L)
{
	return xindices_helper(L, luaL_checkstring(L, 1), spContainer);
}

static int cl_xmembers(lua_State * L)
{
	XPathLookup l = {0};
	return xmembers_helper(L, luaL_checkstring(L, 1), spContainer);
}

static int cl_system(lua_State * L)
{
	system(luaL_checkstring(L, 1));
	return 0;
}

static int cl_shell(lua_State * L)
{
	ShellExecute_UTF8(NULL, NULL, luaL_checkstring(L, 1), NULL, NULL, SW_SHOW);
	return 0;
}

static int cl_modtime2000(lua_State * L)
{
	const char * hogFileName = NULL;
	const char * type = NULL;
	int id = 0;
	GlobalType globalType = GLOBALTYPE_NONE;
	ContainerRestoreState * pRS = NULL;
	HogFile * the_hog = NULL;
	int hogindex = HOG_INVALID_INDEX;
	int retcount = 0;

	if (lua_gettop(L) == 0)
	{
		lua_pushinteger(L, suContainerModifiedTime);
		return 1;
	}

	type = luaL_checkstring(L, 1);
	id = luaL_checkinteger(L, 2);
	globalType = StaticDefineIntGetInt(GlobalTypeEnum, type);
	hogFileName = gSnapshotFilename;

	pRS = objCheckRestoreState(hogFileName, globalType, id);
	if(!pRS)
	{
		if(!gOfflineFilename[0])
			goto cleanup;

		hogFileName = gOfflineFilename;
		pRS = objCheckRestoreState(hogFileName, globalType, id);
		if(!pRS)
			goto cleanup;
	}

	the_hog = hogFileRead(hogFileName, NULL, PIGERR_QUIET, NULL, HOG_READONLY);
	if(!the_hog)
		goto cleanup;

	hogindex = hogFileFind(the_hog, pRS->filename);
	if(hogindex == HOG_INVALID_INDEX)
		goto cleanup;

	lua_pushinteger(L, timeGetSecondsSince2000FromWindowsTime32(hogFileGetFileTimestamp(the_hog, hogindex)));
	retcount++;

cleanup:
	if(pRS)         StructDestroy(parse_ContainerRestoreState, pRS);
	if(the_hog)     hogFileDestroy(the_hog, true);
	return retcount;
}

static int cl_time2000(lua_State * L)
{
	lua_pushinteger(L, timeSecondsSince2000());
	return 1;
}

static int cl_time2000_explode(lua_State * L)
{
	unsigned int t = 0;
	struct tm timestruct = {0};
	int retcount = 0;

	t = luaL_checkinteger(L, 1);
	timeMakeLocalTimeStructFromSecondsSince2000(t, &timestruct);

	lua_pushinteger(L, timestruct.tm_year + 1900); retcount++;
	lua_pushinteger(L, timestruct.tm_mon  + 1); retcount++;
	lua_pushinteger(L, timestruct.tm_mday); retcount++;
	lua_pushinteger(L, timestruct.tm_hour); retcount++;
	lua_pushinteger(L, timestruct.tm_min); retcount++;
	lua_pushinteger(L, timestruct.tm_sec); retcount++;

	return retcount;
}

static int cl_freshness(lua_State * L)
{
	return freshness_helper(L, suContainerModifiedTime);
}

static int cl_savecon(lua_State * L)
{
	return savecon_helper(L, luaL_checkstring(L, 1), spContainer);
}

static int cl_snapshot_filename(lua_State * L)
{
	lua_pushstring(L, spSnapshotFilename);
	return 1;
}

static int cl_script_filename(lua_State * L)
{
	lua_pushstring(L, spScriptFilename);
	return 1;
}

#define CL_CONTAINER_ITER_TYPE "Cryptic.container_iter"
#define CL_CONTAINER_TYPE "Cryptic.new_container"
#define CL_CONTAINER_SUB_TYPE "Cryptic.new_container_sub"
#define CL_CONTAINER_ARRAY_TYPE "Cryptic.new_container_array"

typedef struct CLSubPath
{
	char * xpath;
	Container * con;
} CLSubPath;

static int cl_container_handle_xpath(lua_State * L, Container * con, const char * xpath)
{
	VarType type = VARTYPE_UNKNOWN;
	StructTokenType tokenType = TOK_IGNORE;

	type = xtype(xpath, con, &tokenType);

	switch (type)
	{
	case VARTYPE_NORMAL:
		{
			char * result = NULL;
			if (objPathGetEString(xpath, con->containerSchema->classParse, con->containerData, &result))
			{
				switch (tokenType)
				{
				case TOK_U8_X:
				case TOK_INT16_X:
				case TOK_INT_X:
				case TOK_INT64_X:
					lua_pushinteger(L, atoi(result));
					break;
				default:
					lua_pushstring(L, result);
					break;
				}
				estrDestroy(&result);
				return 1;
			}
		}
		break;
	case VARTYPE_STRUCT:
		{
			CLSubPath * pSubPath = lua_newuserdata(L, sizeof(*pSubPath));
			pSubPath->xpath = strdup(xpath);
			pSubPath->con = con;
			luaL_getmetatable(L, CL_CONTAINER_SUB_TYPE);
			lua_setmetatable(L, -2);
			return 1;
		}
		break;
	case VARTYPE_ARRAY:
		{
			CLSubPath * pSubPath = lua_newuserdata(L, sizeof(*pSubPath));
			pSubPath->xpath = strdup(xpath);
			pSubPath->con = con;
			luaL_getmetatable(L, CL_CONTAINER_ARRAY_TYPE);
			lua_setmetatable(L, -2);
			return 1;
		}
		break;
	}
	return 0;
}

static int cl_container_index(lua_State * L)
{
	Container ** ppCon = luaL_checkudata(L, 1, CL_CONTAINER_TYPE);
	char * xpath = NULL;
	int ret = 0;

	estrPrintf(&xpath, ".%s", luaL_checkstring(L, 2));
	ret = cl_container_handle_xpath(L, *ppCon, xpath);
	estrDestroy(&xpath);
	return ret;
}

static int cl_container_sub_index(lua_State * L)
{
	CLSubPath * pSubPath = luaL_checkudata(L, 1, CL_CONTAINER_SUB_TYPE);
	char * xpath = NULL;
	int ret = 0;

	estrPrintf(&xpath, "%s.%s", pSubPath->xpath, luaL_checkstring(L, 2));
	ret = cl_container_handle_xpath(L, pSubPath->con, xpath);
	estrDestroy(&xpath);
	return ret;
}

static int cl_container_sub_gc(lua_State *L)
{
	CLSubPath * pSubPath = luaL_checkudata(L, 1, CL_CONTAINER_SUB_TYPE);
	free(pSubPath->xpath);
	return 0;
}

static int cl_container_array_index(lua_State * L)
{
	CLSubPath * pSubPath = luaL_checkudata(L, 1, CL_CONTAINER_ARRAY_TYPE);
	char * xpath = NULL;
	int ret = 0;

	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		estrPrintf(&xpath, "%s[%u]", pSubPath->xpath, luaL_checkinteger(L, 2) - 1);
	}
	else
	{
		estrPrintf(&xpath, "%s[\"%s\"]", pSubPath->xpath, luaL_checkstring(L, 2));
	}

	ret = cl_container_handle_xpath(L, pSubPath->con, xpath);
	estrDestroy(&xpath);
	return ret;
}

static int cl_container_array_len(lua_State * L)
{
	CLSubPath * pSubPath = luaL_checkudata(L, 1, CL_CONTAINER_ARRAY_TYPE);
	return xcount_helper(L, pSubPath->xpath, pSubPath->con);
}

static int cl_container_array_gc(lua_State * L)
{
	CLSubPath * pSubPath = luaL_checkudata(L, 1, CL_CONTAINER_ARRAY_TYPE);
	free(pSubPath->xpath);
	return 0;
}

static int cl_containers_of_type_aux(lua_State * L)
{
	U32 uLastModified = 0;
	Container * con = NULL;
	ContainerLoadIterator * pIter = luaL_checkudata(L, lua_upvalueindex(1), CL_CONTAINER_ITER_TYPE);
	
	if (spContainer)
	{
		luaTagContainerCleanup();
		spContainer = NULL;
		suContainerModifiedTime = 0;
	}

	con = objGetNextContainerFromLoadIterator(pIter, &uLastModified);
	if (con)
	{
		Container ** ppCon = NULL;

		spContainer = con;
		suContainerModifiedTime = timeGetSecondsSince2000FromWindowsTime32(uLastModified);

		ppCon = lua_newuserdata(L, sizeof(*ppCon));
		*ppCon = con;

		luaL_getmetatable(L, CL_CONTAINER_TYPE);
		lua_setmetatable(L, -2);
		return 1;
	}

	return 0;
}

static bool gbCurrentlyIterating = false;

static int cl_containers_of_type(lua_State * L)
{
	GlobalType globalType = -1;
	ContainerLoadIterator * pIter = NULL;

	// Do garbage collection to clean up any previous iterators
	lua_gc(L, LUA_GCCOLLECT, 0);

	if (gbCurrentlyIterating)
	{
		return luaL_error(L, "Cannot nest calls to containers_of_type.");
	}
	gbCurrentlyIterating = true;

	globalType = luaL_checkinteger(L, 1);
	if (globalType < 0 || globalType >= GLOBALTYPE_MAX)
	{
		return luaL_error(L, "Invalid global type: %i", lua_tointeger(L, 1));
	}

	pIter = lua_newuserdata(L, sizeof(*pIter));
	luaL_getmetatable(L, CL_CONTAINER_ITER_TYPE);
	lua_setmetatable(L, -2);

	objContainerLoadIteratorInit(pIter, spSnapshotFilename, spOfflineSnapshotFilename, globalType, gNumThreads);

	lua_pushcclosure(L, cl_containers_of_type_aux, 1);

	return 1;
}

static int cl_containers_of_type_gc(lua_State * L)
{
	ContainerLoadIterator * pIter = luaL_checkudata(L, 1, CL_CONTAINER_ITER_TYPE);

	gbCurrentlyIterating = false;
	objClearContainerLoadIterator(pIter);
	luaTagContainerCleanup();
	return 0;
}

void luaLibContainerRegister(LuaContext * pLuaContext)
{
	lua_State * L = pLuaContext->luaState;
	int i = 0;

	static const struct luaL_Reg containerSubMetaTable [] = {
		{"__index", cl_container_sub_index},
		{"__gc", cl_container_sub_gc},
		{NULL, NULL}
	};

	static const struct luaL_Reg containerArrayMetaTable [] = {
		{"__index", cl_container_array_index},
		{"__gc", cl_container_array_gc},
		{"__len", cl_container_array_len},
		{NULL, NULL}
	};

	static const struct luaL_Reg containerMetaTable [] = {
		{"__index", cl_container_index},
		{NULL, NULL}
	};

	static const struct luaL_Reg containerIteratorMetaTable [] = {
		{"__gc", cl_containers_of_type_gc},
		{NULL, NULL}
	};

	static const struct luaL_Reg containerLib [] = {
		{"xtype", cl_xtype},
		{"xvalue", cl_xvalue},
		{"xcount", cl_xcount},
		{"xmembers", cl_xmembers},
		{"xindices", cl_xindices}, 
		{"system", cl_system},
		{"shell", cl_shell},
		{"modtime2000", cl_modtime2000},
		{"time2000", cl_time2000},
		{"time2000_explode", cl_time2000_explode},
		{"freshness", cl_freshness},
		{"savecon", cl_savecon},
		{"snapshot_filename", cl_snapshot_filename},
		{"script_filename", cl_script_filename},
		{"containers_of_type", cl_containers_of_type},
		{NULL, NULL}
	};

	luaL_newmetatable(L, CL_CONTAINER_ITER_TYPE);
	luaL_register(L, NULL, containerIteratorMetaTable);

	luaL_newmetatable(L, CL_CONTAINER_SUB_TYPE);
	luaL_register(L, NULL, containerSubMetaTable);

	luaL_newmetatable(L, CL_CONTAINER_ARRAY_TYPE);
	luaL_register(L, NULL, containerArrayMetaTable);

	luaL_newmetatable(L, CL_CONTAINER_TYPE);
	luaL_register(L, NULL, containerMetaTable);

	lua_pushvalue(L, LUA_GLOBALSINDEX);
	luaL_register(L, NULL, containerLib);
	lua_pop(L, 1);

	lua_pushnumber(L, VARTYPE_NORMAL);
	lua_setglobal(L, "NORMAL");

	lua_pushnumber(L, VARTYPE_STRUCT);
	lua_setglobal(L, "STRUCT");

	lua_pushnumber(L, VARTYPE_ARRAY);
	lua_setglobal(L, "ARRAY");

	for (i = 0; i < GLOBALTYPE_MAX; i++)
	{
		const char * pTypeName = StaticDefineIntRevLookup(GlobalTypeEnum, i);
		if (pTypeName)
		{
			char * pFullTypeName = NULL;
			estrPrintf(&pFullTypeName, "GLOBALTYPE_%s", pTypeName);
			lua_pushnumber(L, i);
			lua_setglobal(L, pFullTypeName);
			estrDestroy(&pFullTypeName);
		}
	}
}
