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

#include "ErrorTrackerLib.h"
#include "ErrorTracker.h"
#include "ErrorTrackerDB.h"
#include "ETCommon/ETShared.h"

#include "error.h"
#include "file.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "GenericHttpServing.h"
#include "GlobalTypes.h"
#include "LogComm.h"
#include "logging.h"
#include "MemoryMonitor.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "ServerLib.h"
#include "StayUp.h"
#include "sysutil.h"
#include "textparser.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "UtilitiesLib.h"
#include "utils.h"
#include "winutil.h"

AUTO_CMD_INT(gServerLibState.iGenericHttpServingPort, ServerMonitorPort);

extern bool gbCreateSnapshotMode;
extern int giSearchIsRunning;

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_ERRORTRACKER);
}

static int siLastMergerPID = 0;
static U32 suLastSnapshotTime = 0;
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ErrorTracker);
void ET_CreateSnapshot(void)
{
	static int poke_count = 0;
	char *estr = NULL;

	if (siLastMergerPID)
	{
		if (system_poke(siLastMergerPID))
		{
			poke_count++;
			/*sprintf(string, "ErrorTracker merger [pid:%d] is still running! Please wait a few minutes before snapshotting again. If this message repeats, find Theo.\n", siLastMergerPID);
			if (poke_count > 2)
				ErrorOrAlert("ERRORTRACKER.MERGER_STILL_RUNNING",string);*/
			return;
		}
	}
	poke_count = 0;

	suLastSnapshotTime = timeSecondsSince2000();

	estrStackCreate(&estr);
	estrPrintf(&estr, "%s -CreateSnapshot -NeverConnectToController", getExecutableName());
	estrConcatf(&estr, " -SetErrorTracker localhost");

	siLastMergerPID = system_detach(estr,0,false);
	estrDestroy(&estr);
}


static bool ET_StayUpTick(void *pUserData)
{
	U32 currentTime;
	if (suLastSnapshotTime == 0)
		suLastSnapshotTime = timeSecondsSince2000();

	currentTime = timeSecondsSince2000();
	if (currentTime > suLastSnapshotTime + getSnapshotInterval()*60)
	{
		ET_CreateSnapshot();
		suLastSnapshotTime = currentTime;
	}
	return true;
}

static bool ET_StayUpStartupSafe(void *pUserData)
{
	if (!siLastMergerPID || !system_poke(siLastMergerPID))
		return true;
	return false;
}

void updateConsoleTitle(const char *pCurrentState)
{
	char buf[256];

	if(pCurrentState == NULL)
	{
		pCurrentState = "Running (Ctrl+C to safely close)";
	}

	sprintf_s(SAFESTR(buf), "ErrorTracker - %s", pCurrentState);
	setConsoleTitle(buf);
}

static bool sbExitCalled = false;
// Cleanup handling
static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			printf("Error Tracker gracefully shutting down ...");
			sbExitCalled = true;
			return TRUE; 

		// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

// Returns whether or not it handles the request
bool testHandlerFunc(NetLink *link, char **args, char **values, int count)
{
	if (stricmp(args[0],"/testhandler")==0)
	{
		errorTrackerLibSendWrappedString(link, "testHandlerFunc worked.");
		return true;
	}

	return false;
}

int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	char **argv;
	ErrorTrackerSettings tempErrorTrackerSettings = {0};
	int frameTimer;

	timerSetMaxTimers(4096);

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	setAssertMode(ASSERTMODE_FULLDUMP | ASSERTMODE_NOERRORTRACKER);
	memMonitorInit();

	if (StayUp(argc, argv, ET_StayUpStartupSafe, NULL, ET_StayUpTick, NULL)) return 0;

	//ErrorTracker needs Gimme for search lookups.
	gimmeDLLDisable(true);

	for(i = 0; i < argc; i++)
	{
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'E', 0x8080ff);
	if (gbCreateSnapshotMode)
		updateConsoleTitle("Merger - Initializing...");
	else
		updateConsoleTitle("Initializing...");


	FolderCacheChooseMode();
	FolderCacheSetManualCallbackMode(1);
	FolderCacheEnableCallbacks(1);
	preloadDLLs(0);
	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	setDefaultAssertMode();
	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	serverLibStartup(argc, argv); 
	ServerLibSetControllerHost("NONE");
	gServerLibState.iGenericHttpServingPort = 8084;

	// ---------------------------------------------------------------------
	// Fire these up as quickly as possible, to be capable of receiving 
	// errors again, and restore the web interface (after a restart/crash).

	if (gbCreateSnapshotMode)
		updateConsoleTitle("Merger - Reloading + Merging...");
	else
		updateConsoleTitle("Reloading...");
	StructInit(parse_ErrorTrackerSettings, &tempErrorTrackerSettings);
	if (fileExists("server/ErrorTracker/settings.txt"))
		ParserReadTextFile("server/ErrorTracker/settings.txt", parse_ErrorTrackerSettings, &tempErrorTrackerSettings, 0);
	FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, "server/ErrorTracker/settings.txt", errorTrackerLibReloadConfig, NULL);

	//errorTrackerLibSetRequestHandlerFunc(testHandlerFunc); // Just for testing purposes; not required

	errorTrackerLibInit(ERRORTRACKER_OPTION_LOG_TRIVIA | ERRORTRACKER_OPTION_RECORD_ALL_ERRORDATA_TO_DISK, 0, &tempErrorTrackerSettings);

	StructDeInit(parse_ErrorTrackerSettings, &tempErrorTrackerSettings);

	// This ensures that we don't lose data when we close the app.
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	printf("Load complete.\n\n");
	
	if (gbCreateSnapshotMode)
	{
		updateConsoleTitle("Merger shutting down...\n");
		objCloseAllWorkingFiles();
		objCloseContainerSource();
		logWaitForQueueToEmpty();
		return;
	}

	// Logging Init
	//logAutoRotateLogFiles(true);

	updateConsoleTitle(0);

	frameTimer = timerAlloc();
	GenericHttpServing_Begin(gServerLibState.iGenericHttpServingPort, "ErrorTracker", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	disableConsoleCloseButton();
	ErrorfPushCallback(NULL, NULL);
	// This thread currently handles the ErrorTracker Database work
	while (!sbExitCalled)
	{
		F32 frametime;
		autoTimerThreadFrameBegin("ET_MainThread");
		frametime = timerElapsedAndStart(frameTimer);

		utilitiesLibOncePerFrame(frametime, 1);
		serverLibOncePerFrame();
		errorTrackerLibOncePerFrame();

		if (giSearchIsRunning)
			commMonitorWithTimeout(commDefault(), 0);
		else
			commMonitor(commDefault());
		FolderCacheDoCallbacks();
		autoTimerThreadFrameEnd();
	}
	ErrorfPopCallback();
	updateConsoleTitle("Gracefully shutting down...\n");
	errorTrackerLibShutdown();

	EXCEPTION_HANDLER_END
}
