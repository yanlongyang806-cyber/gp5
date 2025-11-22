#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

#include "sysutil.h"
#include "file.h"
#include "logging.h"
#include "cmdparse.h"
#include "foldercache.h"
#include "structDefines.h"
#include "globalTypeEnum.h"
#include "utilitiesLib.h"
#include "gimmeDLLWrapper.h"
#include "hoglib.h"
#include "utils.h"

#include "luaWalkSnapshot.h"
#include "pyWalkSnapshot.h"

char gContainerType[MAX_PATH] = "";
AUTO_CMD_STRING(gContainerType, type);

char gScriptFilename[MAX_PATH] = "";
AUTO_CMD_STRING(gScriptFilename, script);

char gSnapshotFilename[MAX_PATH] = "";
AUTO_CMD_STRING(gSnapshotFilename, snapshot);

char gOfflineFilename[MAX_PATH] = "";
AUTO_CMD_STRING(gOfflineFilename, offlinehogg);

int gNumThreads = 0;
AUTO_CMD_INT(gNumThreads, threads);

int gListTypes = 0;
AUTO_CMD_INT(gListTypes, listtypes);

static bool gbRAMCacheMode = false;
AUTO_CMD_INT(gbRAMCacheMode, RAMCacheMode) ACMD_CMDLINE;

static bool gbRAMCacheModeOffline = false;
AUTO_CMD_INT(gbRAMCacheModeOffline, RAMCacheModeOffline) ACMD_CMDLINE;

extern StaticDefineInt GlobalTypeEnum[];
extern bool g_disableLastAuthor;

int wmain(int argc, WCHAR** argv_wide)
{
	GlobalType globalType = -1;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	setCavemanMode();
	logDisableLogging(true);
	utilitiesLibStartup();
	timeSecondsSince2000EnableCache(0);

	cmdParsePrintCommandLine(false);
	cmdParseCommandLine(argc, argv);

	if(gListTypes)
	{
		int index = 1;
		StaticDefineInt *pGlobalType = &GlobalTypeEnum[1];

		fprintf(fileGetStderr(), "Known Global Types: (not all are actually valid container types)\n\n");
		while(pGlobalType->key != U32_TO_PTR(DM_END))
		{
			fprintf(fileGetStderr(), "* %s\n", pGlobalType->key);
			pGlobalType = &GlobalTypeEnum[++index];
		}
	}
	else if(gScriptFilename[0] == 0 || gSnapshotFilename[0] == 0)
	{
		fprintf(fileGetStderr(), "Syntax  : DBScript -script [script filename] -snapshot [snapshot filename]\n");
		fprintf(fileGetStderr(), "Optional:			 -type [container type; ex. EntityPlayer]\n");
		fprintf(fileGetStderr(), "Optional:          -threads X\n");
		fprintf(fileGetStderr(), "Optional:          -listtypes          (prints out a list of known global type names and quits)\n");
		fprintf(fileGetStderr(), "Optional:          -set varname value  (sets global variable \"varname\" to value; can be used multiple times)\n");
		fprintf(fileGetStderr(), "Optional:          -offlinehogg [offline filename]\n");
	}
	else
	{
		HogFile *snapshot, *offline = NULL;
		static const int openMode = HOG_DEFAULT|HOG_NOCREATE|HOG_NO_STRING_CACHE;

		snapshot = hogFileRead(gSnapshotFilename, NULL, PIGERR_ASSERT, NULL, openMode | (gbRAMCacheMode ? HOG_RAM_CACHED : 0));
		if (gOfflineFilename[0])
			offline = hogFileRead(gOfflineFilename, NULL, PIGERR_ASSERT, NULL, openMode | (gbRAMCacheModeOffline ? HOG_RAM_CACHED : 0));

		if (gContainerType[0])
		{
			globalType = StaticDefineIntGetInt(GlobalTypeEnum, gContainerType);
			if(globalType == -1)
			{
				fprintf(fileGetStderr(), "ERROR: Unknown GlobalType [%s]\n", gContainerType);
				return 0;
			}

			fprintf(fileGetStderr(), "Walking GlobalType [%s] [%d]\n", gContainerType, globalType);
		}

		g_disableLastAuthor = true;
		fileDisableAutoDataDir();
		fileAllPathsAbsolute(true);

		if(strEndsWith(gScriptFilename, ".py"))
		{
			pyWalkSnapshot(globalType, gScriptFilename, gSnapshotFilename, gOfflineFilename, gNumThreads);
		}
		else
		{
			luaWalkSnapshot(globalType, gScriptFilename, gSnapshotFilename, gOfflineFilename, gNumThreads);
		}

		hogFileDestroy(offline, true);
		hogFileDestroy(snapshot, true);
	}

	EXCEPTION_HANDLER_END
}
