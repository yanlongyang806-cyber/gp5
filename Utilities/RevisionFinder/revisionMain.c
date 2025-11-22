#include "sysutil.h"
#include "UtilitiesLib.h"
#include "FolderCache.h"
#include "ServerLib.h"
#include "BitStream.h"
#include "HttpLib.h"
#include "GenericHttpServing.h"
#include "RevisionFinder.h"
#include "ControllerLink.h"
#include "File.h"
#include "Alerts.h"
#include "StayUp.h"
#include "timing_profiler_interface.h"
#include "TimedCallback.h"
#include "ScratchStack.h"
#include "wininclude.h"
#include "winutil.h"
#include "UTF8.h"

//Print out framerate info each heartbeat?
int showFramerate = 0;
AUTO_CMD_INT(showFramerate, ShowFramerate);

//Port to monitor for requests
int giServerMonitorPort = DEFAULT_WEBMONITOR_REVISION_FINDER;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

// Number of ms a stall must last before issuing an ACCOUNTSERVER_SLOW_FRAME alert
static int giSlowFrameThreshold = 30000; // in ms
AUTO_CMD_INT(giSlowFrameThreshold, SlowFrameThreshold) ACMD_CMDLINE ACMD_CATEGORY(Revision_Finder);

int rfDisabled = RF_DO_EVERYTHING;
AUTO_CMD_INT(rfDisabled, rfMode) ACMD_CMDLINE ACMD_CATEGORY(Revision_Finder);

extern bool gbConnectToController;

//Indicate that Revision Finder has fully loaded
static bool gbFullyLoaded = false;

//Has exit been called?
static bool sbExitCalled = false;

//How many seconds between fetching data from Builders page
static int siHeartbeatInterval = 10;
AUTO_CMD_INT(siHeartbeatInterval, HeartbeatInterval) ACMD_COMMANDLINE;

SA_RET_NN_STR static const char * GetRevisionFinderConsoleTitle(void)
{
	static char *pTitle = NULL;

	if (!pTitle)
	{
		estrPrintf(&pTitle, "RF %s [pid:%d]", REVISION_FINDER_VERSION, GetCurrentProcessId());
		
		if (isProductionMode())
			estrConcatf(&pTitle, " (production mode)");
		else
			estrConcatf(&pTitle, " (non-production mode)");

		if(rfDisabled != 0)
		{
			if((rfDisabled & RF_DONT_COLLECT_DATA) && (rfDisabled & RF_DONT_RESPOND_REQS)) estrConcatf(&pTitle, " DO-NOTHING MODE");
			else if(rfDisabled & RF_DONT_COLLECT_DATA) estrConcatf(&pTitle, " REQUEST HANDLER ONLY");
			else if(rfDisabled & RF_DONT_RESPOND_REQS) estrConcatf(&pTitle, " DATA COLLECTION ONLY");
			
		}
	}

	return pTitle;
}

// Cleanup handling
static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
	case CTRL_CLOSE_EVENT: 
	case CTRL_LOGOFF_EVENT: 
	case CTRL_SHUTDOWN_EVENT: 
	case CTRL_BREAK_EVENT: 
	case CTRL_C_EVENT: 
		if (!gbFullyLoaded)
		{
			printf("Please wait for the Revision Finder to finish loading before killing it.\n");
			return TRUE;
		}
		printf("Revision Finder is shutting down...\n");
		sbExitCalled = true;
		while (1)
		{
			Sleep(DEFAULT_SERVER_SLEEP_TIME);
		}
		return FALSE; 

		// Pass other signals to the next handler.

	default: 
		return FALSE; 
	} 
}


// Get elapsed time since the program has been running or 
// since the last time this function was called
static F32 GetElapsedTime(void)
{
	static U32 elapsedTimerID = 0;
	static bool ranOnce = false;

	if (!ranOnce)
	{
		elapsedTimerID = timerAlloc();
		timerStart(elapsedTimerID);
		ranOnce = true;
	}

	return timerElapsedAndStart(elapsedTimerID);
}

// Get elapsed time since the program has been running
static F32 GetTotalElapsedTime(void)
{
	static U32 elapsedTimerID = 0;
	static bool ranOnce = false;

	if (!ranOnce)
	{
		elapsedTimerID = timerAlloc();
		timerStart(elapsedTimerID);
		ranOnce = true;
	}

	return timerElapsed(elapsedTimerID);
}

static void updateCoarseTimer(CoarseTimerManager * pFrameTimerManager, F32 fElapsed)
{
	PERFINFO_AUTO_START_FUNC();
	if (fElapsed > (giSlowFrameThreshold / 1000.0f))
	{
		char * pCounterData = NULL;
		coarseTimerPrint(pFrameTimerManager, &pCounterData, NULL);

		TriggerAlertf("REVISIONFINDER_SLOW_FRAME", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0,
			"Revision Finder frame was %f.\nProfile data:\n%s", fElapsed, pCounterData);

		estrDestroy(&pCounterData);
	}
	coarseTimerClear(pFrameTimerManager);
	PERFINFO_AUTO_STOP();
}

static bool StayUpTick(void *pUserData)
{
	return true;
}

// Return true if it is safe to start up.
static bool StayUpStartupSafe(void *pUserData)
{
	return true;
}

int wmain(int argc, WCHAR** argv_wide)
{
	CoarseTimerManager * pFrameTimerManager = NULL;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER
	// Make sure our random numbers are random
	srand(time(NULL));

	SetAppGlobalType(GLOBALTYPE_REVISIONFINDER);
	DO_AUTO_RUNS;

	// Run stay up code instead of normal Revision code if -StayUp was given
	if (StayUp(argc, argv, StayUpStartupSafe, NULL, StayUpTick, NULL)) return 0;

	// Print loading information.
	loadstart_printf("Starting Revision Finder %s...\n\n", REVISION_FINDER_VERSION);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'R', 0x3333cc);
	SetConsoleTitle(L"Revision Finder - Starting...");
	loadstart_report_unaccounted(true);

	// First, call the universal setup stuff
	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	preloadDLLs(0);

	// This ensures that we don't lose data when we close the app.
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheEnableCallbacks(1);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	bsAssertOnErrors(true);
	disableRtlHeapChecking(NULL);
	setDefaultAssertMode();

	serverLibStartup(argc, argv);

	// Set the console title
	SetConsoleTitle_UTF8(GetRevisionFinderConsoleTitle());

	//want to do this before AccountServerInit() so we don't incorrectly attempt to send errors
	if (!gbConnectToController)
	{
		ServerLibSetControllerHost("NONE");
	}
	
	//calls app-specific app-init
	if(!RevisionFinderInit())
		svrExit(-1);
	// Connect to a controller if appropriate
	if (gbConnectToController)
	{
		AttemptToConnectToController(false, NULL, false);
	}

	// Start the server monitor
		if (!devassertmsgf(GenericHttpServing_Begin(giServerMonitorPort, REVISION_FINDER_INTERNAL_NAME, DEFAULT_HTTP_CATEGORY_FILTER, 0),
			"Could not start server monitor HTTP page! This probably means you need to provide -serverMonitorPort x to the command line, "
			"where x is an unused port.  It is currently set to %d.", giServerMonitorPort))
		{
			svrExit(-1);
		}

		if (isProductionMode())
		{
			// Will send alerts instead of a stalling pop-up
			ErrorfPushCallback(serverErrorAlertCallbackProgrammer, NULL);
		}

		pFrameTimerManager = coarseTimerCreateManager(true);

		gbFullyLoaded = true;
		
		printf("\nEntering ready state...");
		RevisionFinderEachHeartbeat();
		loadend_printf("startup complete.");
		printf(" Done!\n");
		DirectlyInformControllerOfState("ready");

		if(!sbExitCalled)
		{
			U32 loopCount = 0;
			while (!sbExitCalled)
			{
				F32 elapsed = GetElapsedTime();

				if (siHeartbeatInterval)
				{
					static U32 siLastTime = 0;
					static U32 iFrameCount = 0;
					static U32 iLastFrameCount = 0;
					U32 iCurTime = timeSecondsSince2000();

					iFrameCount++;
					if (!siLastTime)
					{
						siLastTime = iCurTime;
					}
					else
					{
						if (siLastTime < iCurTime - siHeartbeatInterval)
						{
							siLastTime = iCurTime;
							if(showFramerate)
							{
								printf("%d frames in last %d seconds... %f fps\n", iFrameCount - iLastFrameCount, siHeartbeatInterval, (float)(iFrameCount - iLastFrameCount) / (float)siHeartbeatInterval);
							}
							iLastFrameCount = iFrameCount;
							//Every heartbeat
							RevisionFinderEachHeartbeat();
						}
					}
				}

				autoTimerThreadFrameBegin(__FUNCTION__);
				updateCoarseTimer(pFrameTimerManager, elapsed);
				coarseTimerAddInstance(NULL, "frame");

				ScratchVerifyNoOutstanding();

				utilitiesLibOncePerFrame(elapsed, 1);

				FolderCacheDoCallbacks();

				commMonitor(commDefault());

				{
					F32 totalElapsed = GetTotalElapsedTime();

					PERFINFO_AUTO_START("Revision Finder Activities", 1);
					//Every frame
					GenericHttpServing_Tick();
					TimedCallback_Tick(elapsed, 1);
					if (!StartedByStayUp())
						StayUpTick(NULL); // Go ahead and take over StayUp's tick if we got here and weren't started by StayUp.
					PERFINFO_AUTO_STOP();
				}

				UpdateControllerConnection();

				coarseTimerStopInstance(NULL, "frame");

				autoTimerThreadFrameEnd();
			}

			coarseTimerEnable(false);
		}
		ErrorfPopCallback();

		RevisionFinderShutdown();

	EXCEPTION_HANDLER_END
		
		return 0;
}
