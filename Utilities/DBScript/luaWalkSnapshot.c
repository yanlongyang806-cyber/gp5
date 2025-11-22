#include "wininclude.h"
#include <shellapi.h>

#include "luawalksnapshot.h"
#include "tokenstore.h"
#include "file.h"
#include "timing.h"
#include "WalkSnapshot.h"

#include "luaInternals.h"
#include "luaScriptLib.h"
#include "luaLibContainer.h"
#include "luaTagContainer.h"
#include "luaCmdLine.h"

static LuaContext *spLuaContext = NULL;

static bool OnLoadContainer(Container *con, U32 uContainerModifiedTime)
{
	return luaProcessContainer(spLuaContext, con, uContainerModifiedTime);
}

bool luaWalkSnapshot(GlobalType type, char *luaScriptFilename, char *snapshotFilename, char *offlineFilename, int numThreads)
{
	bool ret = true;
	int len = 0;
	char *p;

	luaTagContainerInit();

	spLuaContext = luaContextCreate();
	luaLibContainerRegister(spLuaContext);
	luaTagContainerRegister(spLuaContext);

	luaInit(spLuaContext, luaScriptFilename, snapshotFilename, offlineFilename, numThreads);

	p = fileAlloc(luaScriptFilename, &len);
	if(p && *p)
	{
		fprintf(fileGetStderr(), "Loading script [%s]...\n", luaScriptFilename);
		if(luaLoadScriptRaw(spLuaContext, luaScriptFilename, p, len, true))
		{
			luaCmdLineSetAllVars(spLuaContext);
			if (type != -1) {
				ret = luaScriptIsSane(spLuaContext);
			}
		}
		else
		{
			fprintf(fileGetStderr(), "Load Error: %s\n", luaContextGetLastError(spLuaContext));
			ret = false;
		}

		free(p);
	}
	else
	{
		fprintf(fileGetStderr(), "ERROR: Couldn't load [%s]\n", luaScriptFilename);
		ret = false;
	}

	if(ret && type != -1)
	{
		int startTime = timeSecondsSince2000();
		int totalTime;

		luaBegin(spLuaContext);

		WalkSnapshot(type, snapshotFilename, offlineFilename, numThreads, OnLoadContainer);

		luaEnd(spLuaContext);

		totalTime = timeSecondsSince2000() - startTime;
		fprintf(fileGetStderr(), "Completed script in %d seconds.\n", totalTime);
	}
	luaDeinit(spLuaContext);

	luaContextDestroy(&spLuaContext);
	return ret;
}
