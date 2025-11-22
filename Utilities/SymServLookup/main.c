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

#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "utils.h"
#include "utilitiesLib.h"
#include "sysutil.h"
#include "MemoryMonitor.h"
#include "winutil.h"
#include "FolderCache.h"
#include "file.h"
#include "gimmeDLLWrapper.h"
#include "textparser.h"
#include "error.h"
#include "errornet.h"
#include "estring.h"
#include "timing.h"
#include "logging.h"
#include "ServerLib.h"
#include "net/net.h"
#include "timing_profiler_interface.h"

#include "ErrorTrackerLib.h"
#include "ETCommon/symstore.h"
#include "ETCommon/ETCommonStructs.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "autogen/callstack_h_ast.h"
#include "UTF8.h"

static NetComm *netComm = NULL;
static bool bSymDoneReceived = false;

#define TIMEOUT 3000

AUTO_COMMAND ACMD_CMDLINE;
void setLogFile(const char *file)
{
	setSymbolLogFile(file);
}

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_ERRORTRACKER);
}

static void ReceiveMsg(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	bSymDoneReceived = true;
}

// send update to ErrorTracker of what line we're processing now
void UpdateSymServStatus (int statuscount, SymServStatusStruct *data)
{
	if (data && linkConnected(data->link))
	{
		Packet *pak = pktCreate(data->link, TO_ERRORTRACKER_SYMSRV_STATUS_UPDATE);
		pktSendString(pak, data->hashString);	
		pktSendU32(pak, statuscount);
		pktSend(&pak);
	}
	else
	{
		// TODO some sort of failure, quit this thing right now?
		Errorf("Something bad happened.");
	}
}

// send update to ErrorTracker that one of the module lookups failed
void sendModuleLookupFailureToErrorTracker(SymServStatusStruct *data, char *module, char *guid)
{
	if (data && linkConnected(data->link))
	{
		Packet *pktResp = pktCreate(data->link, TO_ERRORTRACKER_SYMSRV_MODULE_FAILURE);
		pktSendString(pktResp, module);			
		pktSendString(pktResp, guid);
		pktSendString(pktResp, data->hashString);
		pktSend(&pktResp);
	}
	else
	{
		Errorf("Something bad happened.");
	}
	
}

static void symServLookup_SendRestart(NetLink *link)
{
	Packet *pkt = pktCreate(link, TO_ERRORTRACKER_SYMSRV_MEM_OVERLIMIT);
	pktSendF32(pkt, getProcessPageFileUsage() / MILLION);
	pktSendU32(pkt, SYMSERVLOOKUP_MEM_HARDCAP);
	pktSend(&pkt);
	linkFlush(link);
}

void symServLookup_MemCheck (NetLink *link)
{
	static bool sbCloseQueued = false;
	static int siCount = 0;
	static int siLastSoftcap = 0;
	size_t memUsage;
	
	if (sbCloseQueued)
		return;
	memUsage = (size_t) (getProcessPageFileUsage() / MILLION);
	// Force exit if memory usage is way too high
	if (memUsage > SYMSERVLOOKUP_MEM_HARDCAP)
	{
		printf("Mem usage too high - %dMB\n", memUsage);
		symServLookup_SendRestart(link);
		sbCloseQueued = true;
	}
	else if (memUsage > SYMSERVLOOKUP_MEM_SOFTCAP)
	{
		printf("Mem usage kinda high - %dMB\n", memUsage);
		if (siLastSoftcap == siCount)
		{
			symServLookup_SendRestart(link);
			sbCloseQueued = true;
		}
		else
		{
			symStore_TrimCache();
			siLastSoftcap = siCount+1;
		}
	}	
	siCount++;
}

static char *sStackTraceLinesFile = "stacktracelines.temp";
void IncomingHandleMsg(Packet *pak, int cmd, NetLink *link, void *data)
{
	symServLookup_MemCheck(link);
	loadstart_printf("Processing Packet...\n");
	switch (cmd)
	{
	case FROM_ERRORTRACKER_SYMLOOKUP:
		{
			char pHashString[MAX_PATH];
			char *extraErrorLogData = NULL;
			CallStack *pCallstack;
			char *pParseString;
			StackTraceLineList stackTraceLines = {0};
			char *pOutputParseText = 0;
			Packet *pktResp;

			pCallstack = StructCreate(parse_CallStack);
			pktGetString(pak, pHashString, MAX_PATH);
			pParseString = pktGetStringTemp(pak);
			printf("Hash: %s\n", pHashString);
			extraErrorLogData = pktGetStringTemp(pak);

			{
				FileWrapper * logfile = fopen(getSymbolLogFile(), "a");
				char datetime[128] = "";
				char startString[256];

				timeMakeLocalDateStringFromSecondsSince2000(datetime, timeSecondsSince2000());
				sprintf(startString, "** Stackwalk Start: %s **\n", datetime);

				fwrite(startString, 1, strlen(startString), logfile);
				fwrite(pParseString, 1, strlen(pParseString), logfile);
				fclose(logfile);
			}
			if(ParserReadText(pParseString, parse_CallStack, pCallstack, 0))
			{
				SymServStatusStruct symStruct;
				symStruct.link = link;
				symStruct.hashString = pHashString;
				symstoreLookupStackTrace(pCallstack, (NOCONST(StackTraceLine)***) &stackTraceLines.ppStackTraceLines, 
					UpdateSymServStatus, &symStruct, extraErrorLogData, sendModuleLookupFailureToErrorTracker);
			}
			ParserWriteText(&pOutputParseText, parse_StackTraceLineList, &stackTraceLines, 0, 0, 0);

			pktResp = pktCreate(link, TO_ERRORTRACKER_SYMSRV_DONE);
			pktSendString(pktResp, pHashString);			
			pktSendString(pktResp, pOutputParseText);
			pktSend(&pktResp);
			printf("Sent response.\n");

			estrDestroy(&pOutputParseText);

			EARRAY_FOREACH_REVERSE_BEGIN(stackTraceLines.ppStackTraceLines, i);
			{
				printf("%s (%d)\n", stackTraceLines.ppStackTraceLines[i]->pFunctionName, 
					stackTraceLines.ppStackTraceLines[i]->iLineNum);
			}
			EARRAY_FOREACH_END;

			StructDeInit(parse_StackTraceLineList, &stackTraceLines);
			StructDestroy(parse_CallStack, pCallstack);
			break;
		}
	}
	loadend_printf("Done Processing\n");
}

void IncomingClientConnect(NetLink *link, void *data)
{
}

void IncomingClientDisconnect(NetLink *link, void *data)
{
}

NetListen *gpListen = NULL;
NetComm *GetSymSrvComm()
{
	static NetComm	*comm;

	if (!comm)
		comm = commCreate(0,1);
	return comm;
}

void InitializeIncomingPort(void)
{
	for(;;)
	{
		gpListen = commListen(GetSymSrvComm(), LINKTYPE_UNSPEC, LINK_NO_COMPRESS, DEFAULT_SYMSRV_PORT,
			IncomingHandleMsg,IncomingClientConnect,IncomingClientDisconnect, sizeof(int));
		if (gpListen)
			break;
		Sleep(1);
	}
}

void KillOldSymSrvs(void)
{
	HANDLE hProcessSnap;
	HANDLE hProcess;
	PROCESSENTRY32 pe32;

	char *pTempFileName = NULL;

	// Take a snapshot of all processes in the system.
	hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if( hProcessSnap == INVALID_HANDLE_VALUE )
	{
		return;
	}

	// Retrieve information about the first process,
	// and exit if unsuccessful
	if( !Process32First( hProcessSnap, &pe32 ) )
	{
		CloseHandle( hProcessSnap );          // clean the snapshot object
		return;
	}

	// Now walk the snapshot of processes, and
	// display information about each process in turn
	do
	{
		
		if (pe32.th32ProcessID == GetCurrentProcessId())
			continue;

		UTF16ToEstring(pe32.szExeFile, 0, &pTempFileName);


		if (stricmp(pTempFileName, "SymServLookup.exe") == 0)
		{
			hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, pe32.th32ProcessID);
			if (hProcess)
			{
				TerminateProcess(hProcess, 0);
				CloseHandle(hProcess);
			}
		}
	} while( Process32Next( hProcessSnap, &pe32 ) );

	estrDestroy(&pTempFileName);
}

#define MAX_PROCESS_COUNT 200
int wmain(int argc, WCHAR** argv_wide)
{
	int frameTimer;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	setDefaultAssertMode();
	memMonitorInit();

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	serverLibStartup(argc, argv); 

	loadstart_printf("Killing old processes... ");
	{
		//DWORD aiProcessIDs[MAX_PROCESS_COUNT];
		//DWORD bytesCount = 0;
		//EnumProcesses(aiProcessIDs, MAX_PROCESS_COUNT * sizeof(DWORD), &bytesCount);

		KillOldSymSrvs();
	}
	loadend_printf("Done killing.");

	loadstart_printf("Opening incoming port...  ");
	InitializeIncomingPort();
	loadend_printf("Now accepting requests.");

	InitCOMAndDbgHelp();
	InitSymbolServer();

	frameTimer = timerAlloc();

	while (1)
	{
		F32 frametime;

		autoTimerThreadFrameBegin("SymServLookup");
		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);
		serverLibOncePerFrame();

		commMonitor(GetSymSrvComm());
		commMonitor(commDefault()); 
		FolderCacheDoCallbacks();
		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END
}
