#include "AppRegCache.h"
#include "cmdparse.h"
#include "crypt.h"
#include "error.h"
#include "file.h"
#include "FolderCache.h"
#include "gimme.h"
#include "GimmeDLL.h"
#include "GlobalTypes.h"
#include "hoglib.h"
#include "logging.h"
#include "MemAlloc.h"
#include "patchme.h"
#include "patchxfer.h"
#include "timing_profiler_interface.h"
#include "utils.h"
#include "utilitiesLib.h"
#include "wininclude.h"
#include "winutil.h"

#ifdef _MANAGED
#pragma managed(push, off)
#endif

AUTO_RUN_EARLY;
void gimmeDLLAutoRunEarly(void)
{
	dontLogErrors(true);
	cmdParsePrintCommandLine(false);
}

void gimmeDLLStartup(void)
{
	extern S32 createConsoleOnPrintf;
	static bool bDidStartup=false;
	if (bDidStartup)
		return;
	bDidStartup = true;

	SetAppGlobalType(GLOBALTYPE_GIMMEDLL);

	DO_AUTO_RUNS

	createConsoleOnPrintf = true;
	memCheckInit();
	//fileDisableAutoDataDir();
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	fileAllPathsAbsolute(true);
	cryptMD5Init();
	regSetAppName("Gimme");
	fileSetAsyncFopen(true);
	hogSetAllowUpgrade(true);
	{
		extern void gimmeDLLDisable(int enabled);
		gimmeDLLDisable(true); // Don't let the DLL try to call the DLL!
	}
	patchmeCheckCommandLine();
	utilitiesLibStartup();

	setCustomGimmeLog(GimmeDllLog);
	logSetDir("c:/");
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	winSetHInstance(hModule);
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		// Not calling gimmeDLLStartup() here because it does lots of things that might not be safe in this callback (e.g. can't call assert() and pop up a dialog/walk stacks/etc)
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
    return TRUE;
}

#ifdef _MANAGED
#pragma managed(pop)
#endif

GIMMEDLL_API int gimmeDLLTest(void)
{
	return 42;
}
