#include "file.h"
#include "timing_profiler_interface.h"
#include "luaLibContainer.h"
#include "luaTagContainer.h"
#include "luaInternals.h"
#include "luaScriptLib.h"
#include "objContainerIO.h"
#include "timing.h"
#include "hoglib.h"

extern ParseTable parse_ContainerRestoreState[];
#define TYPE_parse_ContainerRestoreState ContainerRestoreState
extern char gSnapshotFilename[MAX_PATH];
extern char gOfflineFilename[MAX_PATH];

// --------------------------------------------------------------------

typedef struct LoadedContainer
{
	Container *con;

	int        lcindex;
	GlobalType type;
	U32        id;
	U32        modtime;
	bool       loaded;
}
LoadedContainer;

#define MAX_LOADED_CONTAINERS 16
LoadedContainer sLoadedContainers[MAX_LOADED_CONTAINERS] = {0};

// --------------------------------------------------------------------

void luaTagContainerInit(void)
{
	int i;
	for(i=0; i<MAX_LOADED_CONTAINERS; i++)
	{
		sLoadedContainers[i].lcindex = i;
	}
}

static void unloadContainer(LoadedContainer *lc)
{
	assert(lc && lc == &sLoadedContainers[lc->lcindex]);

	PERFINFO_AUTO_START_FUNC();
	objDestroyContainer(lc->con);
	lc->con     = NULL;

	// This block isn't necessary at all, just good for debugging
	lc->id      = 0;
	lc->type    = 0;
	lc->modtime = 0;
	lc->loaded  = false;

	PERFINFO_AUTO_STOP();
}

void luaTagContainerCleanup(void)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	for(i=0; i<MAX_LOADED_CONTAINERS; i++)
	{
		if(sLoadedContainers[i].loaded)
			unloadContainer(&sLoadedContainers[i]);
	}
	PERFINFO_AUTO_STOP();
}

static int findFreeConSlot()
{
	int i;
	for(i=0; i<MAX_LOADED_CONTAINERS; i++)
	{
		if(!sLoadedContainers[i].loaded)
			return i;
	}
	return -1;
}

#define TC_CONTAINER_TYPE "Cryptic.container"

LoadedContainer * getLoadedContainer(lua_State * L)
{
	LoadedContainer * lc = NULL;
	int * pconslot = luaL_checkudata(L, 1, TC_CONTAINER_TYPE);
	
	if (*pconslot >= 0 && *pconslot < MAX_LOADED_CONTAINERS)
	{
		lc = &sLoadedContainers[*pconslot];
	}

	if (!lc)
	{
		luaL_error(L, "Invalid container ID");
	}

	return lc;
}

// --------------------------------------------------------------------

static int tc_container_xtype(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return xtype_helper(L, luaL_checkstring(L, 2), lc->con);
}

static int tc_container_xvalue(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return xvalue_helper(L, luaL_checkstring(L, 2), lc->con);
}

static int tc_container_xcount(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return xcount_helper(L, luaL_checkstring(L, 2), lc->con);
}

static int tc_container_xindices(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return xindices_helper(L, luaL_checkstring(L, 2), lc->con);
}

static int tc_container_xmembers(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return xmembers_helper(L, luaL_checkstring(L, 2), lc->con);
}

static int tc_container_modtime2000(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	lua_pushinteger(L, lc->modtime);
	return 1;
}

static int tc_container_freshness(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return freshness_helper(L, lc->modtime);
}

static int tc_container_unload(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	unloadContainer(lc);
	return 0;
}

static int tc_container_savecon(lua_State * L)
{
	LoadedContainer * lc = getLoadedContainer(L);
	return savecon_helper(L, luaL_checkstring(L, 1), lc->con);
}

static int tc_loadcon(lua_State * L)
{
	const char *hogFileName = NULL;
	const char *type = luaL_checkstring(L, 1);
	int id = luaL_checkinteger(L, 2);
	int conslot = findFreeConSlot();
	GlobalType globalType = StaticDefineIntGetInt(GlobalTypeEnum, type);
	Container * pContainer = NULL;
	ContainerRestoreState * pRS = NULL;
	HogFile * the_hog = NULL;
	int hogindex = HOG_INVALID_INDEX;
	bool loadedFromSnapshot = true;
	int * cp = NULL;

	if (conslot == -1 || globalType == -1 || !id)
	{
		return luaL_error(L, "No container slot, invalid type, or invalid ID.");
	}

	PERFINFO_AUTO_START_FUNC();

	hogFileName = gSnapshotFilename;

	pRS = objCheckRestoreState(hogFileName, globalType, id);
	if(!pRS)
	{
		if(!gOfflineFilename[0])
			goto cleanup;

		loadedFromSnapshot = false;
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

	pContainer = objLoadTemporaryContainer(globalType, id, the_hog, hogindex, true);
	if(!pContainer)
		goto cleanup;

	sLoadedContainers[conslot].con     = pContainer;
	sLoadedContainers[conslot].id      = id;
	sLoadedContainers[conslot].type    = globalType;
	sLoadedContainers[conslot].modtime = timeGetSecondsSince2000FromWindowsTime32(hogFileGetFileTimestamp(the_hog, hogindex));
	sLoadedContainers[conslot].loaded  = true;

	cp = lua_newuserdata(L, sizeof(conslot));
	*cp = conslot;
	luaL_getmetatable(L, TC_CONTAINER_TYPE);
	lua_setmetatable(L, -2);

cleanup:
	if(pRS)         StructDestroy(parse_ContainerRestoreState, pRS);
	if(the_hog)     hogFileDestroy(the_hog, true);
	if (!cp) lua_pushnil(L);

	lua_pushboolean(L, loadedFromSnapshot);
	PERFINFO_AUTO_STOP();
	return 2;
}

static int tc_conexists(lua_State * L)
{
	const char *hogFileName = NULL;
	const char *type = luaL_checkstring(L, 1);
	int id = luaL_checkinteger(L, 2);
	GlobalType globalType = StaticDefineIntGetInt(GlobalTypeEnum, type);
	ContainerRestoreState *pRS = NULL;
	HogFile *the_hog = NULL;
	int hogindex = HOG_INVALID_INDEX;
	bool loadedFromSnapshot = true;
	bool result = false;

	if(globalType == -1 || !id)
	{
		return luaL_error(L, "Invalid type or invalid ID.");
	}

	PERFINFO_AUTO_START_FUNC();

	hogFileName = gSnapshotFilename;

	pRS = objCheckRestoreState(hogFileName, globalType, id);
	if(!pRS)
	{
		if(!gOfflineFilename[0])
			goto cleanup;

		loadedFromSnapshot = false;
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

	result = true;

cleanup:
	if(pRS)         StructDestroy(parse_ContainerRestoreState, pRS);
	if(the_hog)     hogFileDestroy(the_hog, true);
	lua_pushboolean(L, result);
	lua_pushboolean(L, loadedFromSnapshot);
	PERFINFO_AUTO_STOP();
	return 2;
}

void luaTagContainerRegister(LuaContext * pLuaContext)
{
	lua_State * L = pLuaContext->luaState;

	static const struct luaL_Reg containerTag [] = {
		{"xtype", tc_container_xtype},
		{"xvalue", tc_container_xvalue},
		{"xcount", tc_container_xcount},
		{"xindices", tc_container_xindices},
		{"xmembers", tc_container_xmembers},
		{"freshness", tc_container_freshness},
		{"modtime2000", tc_container_modtime2000},
		{"unload", tc_container_unload},
		{"savecon", tc_container_savecon},
		{NULL, NULL}
	};

	static const struct luaL_Reg containerTagLib [] = {
		{"loadcon", tc_loadcon},
		{"conexists", tc_conexists},
		{NULL, NULL}
	};

	luaL_newmetatable(L, TC_CONTAINER_TYPE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, containerTag);

	lua_pushvalue(L, LUA_GLOBALSINDEX);
	luaL_register(L, NULL, containerTagLib);
	lua_pop(L, 1);
}
