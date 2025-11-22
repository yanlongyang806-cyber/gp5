/*
 * CrypticMon
 */

#include "CrypticMon_c_ast.h"
#include "estring.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "MemoryMonitor.h"
#include "ServerLib.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "utilitieslib.h"
#include "winutil.h"

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

// Method to use when monitoring a server
AUTO_ENUM;
typedef enum
{
	MonitorMethod_Tcp = 0,		// Plain TCP connection
	MonitorMethod_Http,			// HTTP request
	MonitorMethod_Accountnet	// Account login
} MonitorMethod;

// Entry for monitoring a specific server
AUTO_STRUCT;
typedef struct MonitorServer {
	char *host;										// Hostname of server to monitor
	int interval;				AST(DEFAULT(1))		// Period of monitor requests, in seconds
	int timeout;				AST(DEFAULT(60))	// Maximum time to wait for a response, in seconds

	MonitorMethod method;							// Method to use for monitoring
	int port;										// Monitor port, if required
	char *username;									// Username for service, if required
	char *password;									// Password for service, if required
} MonitorServer;

// CrypticMon configuration
AUTO_STRUCT;
typedef struct CrypticMonConfig {
	EARRAY_OF(MonitorServer) monitor;
} CrypticMonConfig;

// Global configuration state
static CrypticMonConfig config;

// Set to true when it is time to close.
static bool gbCloseCrypticMon = false;

// Shut down
static void consoleCloseHandler(DWORD fdwCtrlType)
{
	printf("Shutting down...\n");
	gbCloseCrypticMon = true;
}

int wmain(int argc, WCHAR** argv_wide)
{
	int frameTimer, i;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER

	SetAppGlobalType(GLOBALTYPE_CRYPTICMON);
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'M', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	preloadDLLs(0);

	log_printf(LOG_CRYPTICMON, "CrypticMon Version: %s\n", GetUsefulVersionString());

	setConsoleTitle("CrypticMon");

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	logEnableHighPerformance();
	logAutoRotateLogFiles(true);
	serverLibStartup(argc, argv);

	setSafeCloseAction(consoleCloseHandler);
	useSafeCloseHandler();
	disableConsoleCloseButton();

	frameTimer = timerAlloc();

	for(;;)
	{
		F32 frametime;
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);
		commMonitor(commDefault());
		serverLibOncePerFrame();

		// Exit if we've been asked to.
		if (gbCloseCrypticMon)
			break;

		autoTimerThreadFrameEnd();

	}
	EXCEPTION_HANDLER_END
}

#include "CrypticMon_c_ast.c"
