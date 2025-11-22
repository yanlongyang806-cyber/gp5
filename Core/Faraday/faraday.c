#include "BitStream.h"
#include "CmdParseJson.h"
#include "FolderCache.h"
#include "GenericHttpServing.h"
#include "HttpLib.h"
#include "ScratchStack.h"
#include "ServerLib.h"
#include "StayUp.h"
#include "file.h"
#include "logging.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "winutil.h"
#include "UTF8.h"

#include "faraday_websrv.h"

#define FARADAY_INTERNAL_NAME "Faraday"

#define FARADAY_VERSION "(dev)" // Change to X.X once branched

static int giServerMonitorPort = DEFAULT_WEBMONITOR_FARADAY;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

static bool gbFullyLoaded = false;
static bool gbExitCalled = false;


static bool StayUpTick(void * pUserData)
{
	return true;
}


static bool StayUpStartupSafe(void * pUserData)
{
	return true;
}


static BOOL ConsoleCtrlHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			if (!gbFullyLoaded)
			{
				printf("Please wait for Faraday to finish loading before killing it.\n");
				return TRUE;
			}
			printf("Faraday is shutting down...\n");
			log_printf(LOG_FARADAY, "Faraday is shutting down...");
			gbExitCalled = true;
			while (true) Sleep(DEFAULT_SERVER_SLEEP_TIME);
			return FALSE; 

		// Pass other signals to the next handler.
		default: 
			return FALSE; 
	} 
}


int wmain(int argc, WCHAR** argv_wide)
{
	U32 uElapsedTimerID = 0;
	U32 uLoopCount = 0;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER

	SetAppGlobalType(GLOBALTYPE_FARADAY);
	DO_AUTO_RUNS;

	// Run stay up code instead of normal account server code if -StayUp was given
	if (StayUp(argc, argv, StayUpStartupSafe, NULL, StayUpTick, NULL)) return 0;

	// Print loading information.
	loadstart_printf("Starting Faraday %s...\n\n", FARADAY_VERSION);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'F', 0xFFCC11);
	SetConsoleTitle(L"Faraday - Starting...");
	loadstart_report_unaccounted(true);

	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	preloadDLLs(0);

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleCtrlHandler, TRUE);
	if (isProductionMode()) disableConsoleCloseButton();

	FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheEnableCallbacks(1);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	bsAssertOnErrors(true);
	disableRtlHeapChecking(NULL);
	setDefaultAssertMode();
	
	serverLibStartup(argc, argv);

	ServerLibSetControllerHost("NONE");
	logSetDir(GlobalTypeToName(GetAppGlobalType()));

	// Start the server monitor
	if (!GenericHttpServing_Begin(giServerMonitorPort,
			FARADAY_INTERNAL_NAME, DEFAULT_HTTP_CATEGORY_FILTER, 0))
	{
		devassertmsgf(0,
			"Could not start server monitor HTTP page! This probably "
			"means you need to provide -serverMonitorPort x to the command "
			"line, where x is an unused port. It is currently set to %d.",
			giServerMonitorPort);
		svrExit(-1);
	}

	if (isProductionMode())
	{
		// Will send alerts instead of a stalling pop-up
		ErrorfPushCallback(serverErrorAlertCallbackProgrammer, NULL);
	}

	RegisterJsonRPCMissingCallback(FaradayMissingHandler);

	printf("\nEntering ready state...\n");

	HttpServing_RestrictIPGroupToJsonRpcCategories("PWE", "Faraday_RPC_PWE");

	uElapsedTimerID = timerAlloc();
	timerStart(uElapsedTimerID);
	while (!gbExitCalled)
	{
		F32 elapsed = timerElapsedAndStart(uElapsedTimerID);

		if (uLoopCount++ == 10 && !gbFullyLoaded)
		{
			gbFullyLoaded = true;
			loadend_printf("startup complete.");
			log_printf(LOG_FARADAY, "Server ready. (%s, %s)", FARADAY_VERSION,
				GetUsefulVersionString());
			SetConsoleTitle_UTF8(
				STACK_SPRINTF("Faraday %s [pid:%d]", FARADAY_VERSION, GetCurrentProcessId()));
		}

		autoTimerThreadFrameBegin(__FUNCTION__);
		utilitiesLibOncePerFrame(elapsed, 1);
		if (!StartedByStayUp()) StayUpTick(NULL);
		GenericHttpServing_Tick();
		ScratchVerifyNoOutstanding();
		FolderCacheDoCallbacks();
		commMonitor(commDefault());
		autoTimerThreadFrameEnd();
	}

	if (isProductionMode())
	{
		ErrorfPopCallback();
	}

	svrExit(0);
	EXCEPTION_HANDLER_END
	return 0;
}