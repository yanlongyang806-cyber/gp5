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
#include "estring.h"
#include "trivia.h"
#include "utilitiesLib.h"
#include "foldercache.h"
#include "error.h"
#include "errornet.h"
#include "timing.h"
#include "cmdparse.h"
#include "HttpLib.h"
#include "Httpclient.h"
#include "net.h"
#include "systemspecs.h"
#include "netlink.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "errornet.h"
#include "../../../libs/UtilitiesLib/AutoGen/errornet_h_ast.h"
#include "timing_profiler_interface.h"
#include "main_c_ast.h"

void testCrashDLL();
void testCrashET();
void testErrors();
void test360();

DWORD id;

DWORD WINAPI CrashThread(LPVOID lpParameter)
{
	Sleep(2000);
	//assertmsg(0, "TEST CRASH IN THREAD\n");

	return 0;
}

void llama(char **dummy)
{
}

void jimbCrash()
{
	char *data = malloc_timed(1024, _NORMAL_BLOCK, "file.c", 1);
	llama(&data);
	data[1024+10] = 0x77;
	assertmsg(0, "Some other assert");
}

void badness();

typedef int (*fnPureDLLFunc)(void);

static void pureVirtualFuncWasCalled(void)
{
	assertmsg(0, "Pure virtual function called illegally!");
}

void errorMsg()
{
	ErrorDetailsf("These are the details");
	Errorf("This is the a test error!");
}

static NetComm *netComm = NULL;

void errorLoop(ErrorData *err, NetLink *pErrorTrackerLink, int i, int time)
{
	char *errMsg = NULL;
	char *pOutputParseText = NULL;
	Packet *pPak;

	PERFINFO_AUTO_START_FUNC();

	estrPrintf(&errMsg, "This is the test error #%d in %d seconds!", i, timeSecondsSince2000() - time);
	err->pErrorString = errMsg;

	estrCreate(&pOutputParseText);
	ParserWriteText(&pOutputParseText, parse_ErrorData, err, 0, 0, 0);

	pktCreateWithCachedTracker(pPak, pErrorTrackerLink, TO_ERRORTRACKER_ERROR);
	pktSendString(pPak, pOutputParseText);
	pktSendU32(pPak, i);
	pktSend(&pPak);

	if (i % 1000 == 0)
	{
		printf("Sent error #%d in %d seconds\n", i, timeSecondsSince2000() - time);
	}

	PERFINFO_AUTO_STOP();
}

void errorSpam()
{
	int i;
	int time = timeSecondsSince2000();
	NetLink *pErrorTrackerLink;
	ErrorData err = {0};
	int frameTimer = timerAlloc();

	if (!netComm)
		netComm = commCreate(0,0);

	pErrorTrackerLink = commConnect(netComm, LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,getErrorTracker(),
		DEFAULT_ERRORTRACKER_PUBLIC_PORT, 0,0,0,0);

	if (!linkConnectWait(&pErrorTrackerLink, 10.f))
	{
		return;
	}

	err.eType = ERRORDATATYPE_ERROR;
	err.pPlatformName = "Win32";
	err.pExecutableName = "C:/src/Utilities/Bin/CrashTest.exe";
	err.pProductName = "Core";
	err.pVersionString = "139106";
	err.pUserWhoGotIt = "W:jfotland";
	err.pSourceFile = "main.c";
	err.iSourceFileLine = 78;
	err.iProductionMode = 1;
	err.pSVNBranch = "http://code/svn/dev";
	err.pShardInfoString = "none";
	err.pAppGlobalType = "InvalidType";

	for(i = 0; 1; i++)
	{
		F32 frametime;
		autoTimerThreadFrameBegin("ErrorLoop");
		frametime = timerElapsedAndStart(frameTimer);

		utilitiesLibOncePerFrame(frametime, 1);

		//commMonitor(commDefault());
		commMonitor(netComm);
		FolderCacheDoCallbacks();
		errorLoop(&err, pErrorTrackerLink, i, time);
		autoTimerThreadFrameEnd();	
	}

	linkShutdown(&pErrorTrackerLink);
}

//extern volatile int g_duringDataLoading;
//extern char g_duringDataLoadingFilename[MAX_PATH];
void testDataValidation()
{
	//g_duringDataLoading = 1;
	//strcpy(g_duringDataLoadingFilename, "data/TestScripts/Quality_Week_Scripts/Monster_Island/WayfarerCoast/MON_WC_Utility.txt");
//	crashNow();
}

#pragma warning (disable: 4717) // Allow stack overflows ^_^
void stackOverflow(void)
{
	char stack_buf[1024] = "";
	alloca(rand() % 65536); // Want this to crash with a different number of recursions each time, should still bucket to the same ET entry
	stackOverflow();
}

void outOfMemoryTest(void)
{
	while (true)
	{
		char *bob = malloc(70000000);
	}
}

typedef void (*fptr) (void);

typedef void (*ffptr) (fptr);

typedef ffptr (*fptd) (void);

void assertTest(void)
{
	assertmsgf(0, "Assert test %d!", 1);
}

void callStackTest()
{
	HMODULE h = LoadLibrary(L"stackfunction.dll");
	ffptr ff = (ffptr)strtoul(fileAlloc("sketch.txt", NULL), NULL, 16);
	 
	ff(assertTest);
}

void hashBreaker(fptr f)
{
	f();
}

AUTO_STRUCT;
typedef struct InnerStruct
{
	int bob;
} InnerStruct;

AUTO_STRUCT;
typedef struct OutsideStruct
{
	int foo;
	int bar;
	InnerStruct *myBob;
} OutsideStruct;

void memoryStomp()
{
	OutsideStruct *myStruct = StructCreate(parse_OutsideStruct);
	InnerStruct *myInnerThing = NULL;
	OutsideStruct *myCloneStruct = NULL;

	myStruct->myBob = StructCreate(parse_InnerStruct);
	myInnerThing = myStruct->myBob;

	StructDestroy(parse_InnerStruct, myInnerThing);
	myCloneStruct = StructClone(parse_OutsideStruct, myStruct);
	StructDestroy(parse_OutsideStruct, myStruct);
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;
	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	FolderCacheChooseMode();

	cmdParseCommandLine(argc, argv);

	//setAssertMode((assertIsExecutableInToolsBin()? (ASSERTMODE_MINIDUMP|ASSERTMODE_TEMPORARYDUMPS) : 0));
	setDefaultAssertMode();
	//setProductionClientAssertMode();
	setDefaultProductionMode(1);

	utilitiesLibStartup();

	//OpenProcess(0, false, 0);

	//_set_purecall_handler(pureVirtualFuncWasCalled);

	//jimbCrash();

	//errorTrackerEnableSendErrorThreading(false);
	errorTrackerEnableErrorThreading(true);

	systemSpecsInit();

	//stackOverflow();

	//errorSpam();

	//errorMsg();

	//hashBreaker(outOfMemoryTest);

	//outOfMemoryTest();

	//callStackTest();

	memoryStomp();
	
	assertmsgf(0, "Crash test %d!", 1);

	if (0) {
		int *null_ptr = NULL;
		*null_ptr = 1;
	}

	assertmsg(0, "Crash test!"); 

	//ceSpawn("expression", 0, "error text", "stack dump", "callstack report", "filename", 13);

	EXCEPTION_HANDLER_END
}

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------

#include "main_c_ast.c"
#pragma warning (disable : 6011)

