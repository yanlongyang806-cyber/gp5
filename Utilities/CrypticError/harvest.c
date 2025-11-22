#include "GlobalTypes.h"
#include "GlobalComm.h"
#include "CrypticPorts.h"
#include "sysutil.h"
#include "file.h"
#include "wininclude.h"
#include "dbghelp.h"
#include "cmdparse.h"
#include "stashtable.h"
#include "stdtypes.h"
#include "textparser.h"
#include "errornet.h"
#include "Autogen/errornet_h_ast.h"
#include "errorprogressdlg.h"
#include "harvest.h"
#include "throttle.h"
#include "ui.h"
#include "net/net.h"
#include "AppRegCache.h"
#include "dataValidation.h"
#include <psapi.h>
#include <tlhelp32.h>
#include "UTF8.h"

// Uncomment to debug
//#define HARVEST_WAIT_FOR_DEBUGGER

#define PERMANENT_DUMP_DIR_DEFAULT "c:\\Night\\dumps"

char sPermanentDumpDir[MAX_PATH] = PERMANENT_DUMP_DIR_DEFAULT;

#pragma warning ( disable : 4996 ) // getenv() warning

extern bool gbForceAutoClose;
extern bool gbForceStayUp;
extern bool gbIgnoreProgrammerMode;
extern bool gbSkipUserInput;

// -------------------------------------------------------------------------------------------------------
// externs from SuperAssert.c
extern void setErrorTracker(const char *pErrorTracker);

#ifndef _XBOX
typedef BOOL (__stdcall *MiniDumpWriter)(
	HANDLE hProcess,
	DWORD ProcessId,
	HANDLE hFile,
	MINIDUMP_TYPE DumpType,
	PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	PMINIDUMP_CALLBACK_INFORMATION CallbackParam
	);
#endif

// -------------------------------------------------------------------------------------------------------
// This makes our actual command line very lean and mean. 

int gCEProcessId = 0;
AUTO_CMD_INT(gCEProcessId, ceProcessId) ACMD_CMDLINE;
AUTO_CMD_INT(gCEProcessId, pid) ACMD_CMDLINE;
int gCEThreadId = 0;
AUTO_CMD_INT(gCEThreadId, ceThreadId) ACMD_CMDLINE;
char gCEPtrArgs[100] = "";
AUTO_CMD_STRING(gCEPtrArgs, ceArgs) ACMD_CMDLINE;

// For stalled processes
char gCEManualDumpDesc[512] = "";
AUTO_CMD_STRING(gCEManualDumpDesc, manualDump) ACMD_CMDLINE;

int gDisplayInfoOnly = 0;
AUTO_CMD_INT(gDisplayInfoOnly, info) ACMD_CMDLINE;

bool gCEXperfMode = false;
AUTO_CMD_INT(gCEXperfMode, xperfMode) ACMD_CMDLINE;

// -------------------------------------------------------------------------------------------------------
// Variables from the failed process

static StashTable sStringVars = NULL;
static StashTable sIntVars    = NULL;
static U64 sDebugMePtr = 0;
static PEXCEPTION_POINTERS gExceptionInfo = NULL;
static bool sbWroteCachedPackage = false;

// -------------------------------------------------------------------------------------------------------
// Global and other static vars

extern int gDeferredMode;
extern DWORD gChosenProcessID;
extern char gChosenProcessName[MAX_PATH+1];
extern char gChosenProcessDetails[2048];

CrypticErrorMode sCrypticErrorMode = CEM_CUSTOMER;
int gProcessProductionMode = 0;
int gProcessAssertMode = 0;

static bool sbProcessGone = false;
static bool sbRegKeyIsProgrammerMachine = false;
static bool sbRegKeyIsProductionServer  = false;
static bool sbWritingDumps = false;
static bool sbSendingDumps = false;
static bool sbWroteDumps = false;
static bool sbWaitingForProgrammerAction = false;

static HANDLE shProcess = INVALID_HANDLE_VALUE;
static char sFullDumpFilename[MAX_PATH] = {0};
static char sMiniDumpFilename[MAX_PATH] = {0};
static U32 dumpsRequired = 0;
static U32 dumpsWritten = 0;
static bool sbNotified = false;

extern char gChosenProcessName[];

enum TerminateFlags
{
	TF_SHUTDOWN = (1 << 0),
	TF_FORCE    = (1 << 1),
};

static bool TerminateCrashedProcess(int flags);

typedef enum DebugProcessCommand
{
	DPC_DEBUG = 0,
	DPC_IGNORE
} DebugProcessCommand;

void DebugProcess(DebugProcessCommand eCmd);

// -------------------------------------------------------------------------------------------------------
// A complete rip-off of DoEarlyCommandLineParsing()

static void ParseCrypticErrorArgs(const char *fakeCmdLine)
{
	char *pDupCmdLine = NULL;
	char **ppArgs; //NOT an earray.
	int iMaxArgs;
	int iNumArgs;

	estrCopy2(&pDupCmdLine, fakeCmdLine);
	iMaxArgs = estrCountChars(&pDupCmdLine, ' ') + 1;
	ppArgs = calloc(iMaxArgs * sizeof(char*), 1);
	iNumArgs = tokenize_line_quoted_safe(pDupCmdLine,ppArgs, iMaxArgs ,NULL);
	cmdParseCommandLine_internal(iNumArgs, ppArgs, false);
	estrDestroy(&pDupCmdLine);
	free(ppArgs);
}

// -------------------------------------------------------------------------------------------------------
// Simple Registry Query

void LookupRegistryKeys()
{
	MachineMode eMachineMode = regGetMachineMode();

	if (eMachineMode == MACHINEMODE_PROGRAMMER)
	{
		sbRegKeyIsProgrammerMachine = true;
	}
	else if (eMachineMode == MACHINEMODE_PRODUCTION)
	{
		sbRegKeyIsProductionServer = true;
	}

	// reuse temp
	regGetAppString("DumpDir", PERMANENT_DUMP_DIR_DEFAULT, sPermanentDumpDir, MAX_PATH);
}

// -------------------------------------------------------------------------------------------------------
// Takes a string in the form "x:y", where x is a raw pointer address, and y is a string length. 
// Populates and null-terminates outputBuffer with the contents of that address from inside of hProcess.

static bool ReadTruncatedStringFromProcess(HANDLE hProcess, U64 uPtrLoc, U32 uLenPtr, char *outputBuffer, U32 uBufferLen)
{
	LPCVOID ptr = 0;
	SIZE_T bytesToRead = 0;
	SIZE_T bytesRead = 0;

	if(!hProcess)
		return false;

	if(!uPtrLoc || !uLenPtr)
		return false;

	bytesToRead = uLenPtr+1; // Grabbing the NULL terminator
	if(bytesToRead > uBufferLen)
		bytesToRead = uBufferLen;

	ptr = (LPCVOID)uPtrLoc;

	if(!ReadProcessMemory(hProcess, ptr, outputBuffer, bytesToRead, &bytesRead))
		return false;

	if(bytesToRead != bytesRead)
		return false;

	outputBuffer[bytesToRead-1] = 0;
	return true;
}

static bool ReadTruncatedStringFromProcessEncoded(HANDLE hProcess, const char *pEncodedStrLoc, char *outputBuffer, U32 uBufferLen)
{
	LPCVOID ptr = 0;
	SIZE_T bytesToRead = 0;
	SIZE_T bytesRead = 0;
	U64 uPtrLoc = 0;
	U32 uLenPtr = 0;

	if(*pEncodedStrLoc == 0)
	{
		// Entry wasn't supplied, return an empty string
		outputBuffer[0] = 0;
		return true; 
	}

	if(sscanf_s(pEncodedStrLoc, "%"FORM_LL"d:%d", &uPtrLoc, &uLenPtr) != 2)
		return false;

	return ReadTruncatedStringFromProcess(hProcess, uPtrLoc, uLenPtr, outputBuffer, uBufferLen);
}

int harvestGetPid()
{
	return gCEProcessId;
}

char *harvestGetStringVar(const char *name, const char *defaultVal)
{
	char *ret = (char*)defaultVal;
	if(!stashFindPointer(sStringVars, name, (void**)&ret))
	{
		ret = (char*)defaultVal;
	}

	return ret;
}

int harvestGetIntVar(const char *name, int defaultVal)
{
	int ret = defaultVal;
	if(!stashFindInt(sIntVars, name, &ret))
	{
		ret = defaultVal;
	}

	return ret;
}

static bool PopulateVarsFromProcess(HANDLE hProcess)
{
	bool bRet = true;
	char crypticErrorArgs[4096];
	char *strtokContext = NULL;
	char *token;
	char *pTemp = NULL;

	LogNormal("Harvesting data from crashed process...");

	sStringVars = stashTableCreate(32, StashDeepCopyKeys_NeverRelease, StashKeyTypeStrings, 0);
	sIntVars    = stashTableCreate(32, StashDeepCopyKeys_NeverRelease, StashKeyTypeStrings, 0);

	if(GetModuleFileNameEx_UTF8(hProcess, NULL, &pTemp))
	{
		strcpy(gChosenProcessName, pTemp);
	}
	else
	{
		gChosenProcessName[0] = 0;
		LogError("Couldn't detect process name, using UNKNOWNEXE\n");
	}
	estrDestroy(&pTemp);


	if(!ReadTruncatedStringFromProcessEncoded(hProcess, gCEPtrArgs, SAFESTR(crypticErrorArgs)))
	{
		printf("Failed to read arguments!\n");
		return false;
	}
	
	token = strtok_s(crypticErrorArgs, "|", &strtokContext);
	if(!token)
	{
		return false;
	}

	if(strcmp(token, "CRYPTICERROR") != 0)
	{
		return false;
	}

	while(token = strtok_s(NULL, "|", &strtokContext))
	{
		switch(token[0])
		{
		case 'S':
			{
				char tempVar[1024];
				U64 ptrVal;
				int len;
				if(sscanf_s(token, "S:%"FORM_LL"d:%d:%s", &ptrVal, &len, tempVar, 1024) == 3)
				{
					char *tempVal = NULL;
					estrReserveCapacity(&tempVal, len+1);
					if(ReadTruncatedStringFromProcess(hProcess, ptrVal, len, tempVal, len+1))
					{
						tempVal[len] = 0;
						stashAddPointer(sStringVars, tempVar, tempVal, false);
					}
				}

				break;
			}

		case 'P':
			{
				char tempVar[1024];
				U64 ptrVal;
				tempVar[0] = 0;
				if(sscanf_s(token, "P:%"FORM_LL"d:%s", &ptrVal, tempVar, 1024) == 2)
				{
					if(!strcmp(tempVar, "debugme"))
					{
						sDebugMePtr = ptrVal;
					}
					else if(!strcmp(tempVar, "exceptioninfo"))
					{
						gExceptionInfo = (PEXCEPTION_POINTERS)ptrVal;
					}
				}

				break;
			}

		case 'I':
			{
				char tempVar[1024];
				int intVal;
				if(sscanf_s(token, "I:%d:%s", &intVal, tempVar, 1024) == 2)
				{
					stashAddInt(sIntVars, tempVar, intVal, false);
				}

				break;
			}
		}
	}

	// ----------------------------------------------------------------------------------
	// Setup some global variables based on the data we got
	gProcessProductionMode = harvestGetIntVar("productionmode", 0);
	gProcessAssertMode     = harvestGetIntVar("assertmode", 0);

	return bRet;
}

bool FreezeProcess(DWORD pid, bool bFreeze) 
{ 
    bool ret = false;
	HANDLE hSnapshot;
    THREADENTRY32 threadEntries = {0};

	if(bFreeze)
		LogNormal("Freezing Process ...");
	else
		LogNormal("Thawing Process ...");

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
		return (false);

	threadEntries.dwSize = sizeof(THREADENTRY32);
	if (Thread32First(hSnapshot, &threadEntries))
	{
		ret = true;
		do
		{
			if (threadEntries.th32OwnerProcessID == pid)
			{
				HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadEntries.th32ThreadID);
				if (hThread)
				{
					ANALYSIS_ASSUME(hThread);
					if (bFreeze)
						SuspendThread(hThread);
					else
						ResumeThread(hThread);
					CloseHandle(hThread);
				}
			}
		}
		while (Thread32Next(hSnapshot, &threadEntries));
	}

    CloseHandle (hSnapshot);
    return (ret);
} 

static void SwitchCrypticErrorMode()
{
	// ----------------------------------------------------------------------------------
	// Calculate our mode

	sCrypticErrorMode = CEM_CUSTOMER;

	if(gProcessAssertMode) // If this isn't set, we have to assume that it is an end user
	{
		if( !(gProcessAssertMode & ASSERTMODE_ISEXTERNALAPP) ) // Only GameClients have this set
		{
			sCrypticErrorMode = (gProcessProductionMode) ? CEM_PRODSERVER : CEM_DEVSERVER;
		}
		else
		{
			if(gProcessProductionMode)
			{
				// At this point, ISEXTERNALAPP is set and we're in production mode, so
				// this is definitely a production mode game client. The one last check
				// is whether or not the game client specifically considers itself a 
				// server, which happens in no-gfx test clients headshot servers.
				if(harvestGetIntVar("servermode", 0) != 0)
				{
					sCrypticErrorMode = CEM_PRODSERVER;
				}
			}
			else
			{
				// ASSERTMODE_ISEXTERNALAPP + no-production = dev client
				sCrypticErrorMode = CEM_DEVELOPER;
			}
		}
	}

	if(sbRegKeyIsProductionServer)
	{
		LogNote("Forced Mode: Prod Server");
		sCrypticErrorMode = CEM_PRODSERVER;
	}
	else if(sbRegKeyIsProgrammerMachine)
	{
		LogNote("Forced Mode: Prod Server [Programmer]");
		sCrypticErrorMode = CEM_PRODSERVER;
	}
	else
	{
		switch(sCrypticErrorMode)
		{
		xcase CEM_CUSTOMER:   LogNote("Mode: Customer");
		xcase CEM_DEVELOPER:  LogNote("Mode: Dev Client");
		xcase CEM_DEVSERVER:  LogNote("Mode: Dev Server");
		xcase CEM_PRODSERVER: LogNote("Mode: Prod Server");
		};
	}

	// ----------------------------------------------------------------------------------
	// ... then act on it

	if (!gbSkipUserInput)
		uiRequestSwitchMode();
}

void setProcessGone()
{
	if(!sbProcessGone)
	{
		sbProcessGone = true;
		uiNotifyProcessGone();
	}
}

bool isProcessGone()
{
	return sbProcessGone;
}

static void PopulateValidationErrorData(ErrorData *pErrorData, const char *dataDir, const char *pFilename)
{
	static char *pNewTrivia = NULL;
	bool bAppendFilename = false;
	if(!pFilename || strlen(pFilename) < 3 || strlen(pFilename) > MAX_PATH)
		return;

	LogNormal("Gathering Data Validation Info...");

	estrPrintf(&pNewTrivia, "%s", pErrorData->pTrivia);
	if(!strEndsWith(pNewTrivia, "\n"))
		estrConcatf(&pNewTrivia, "\n");
	estrConcatf(&pNewTrivia, "ValidationError:Filename %s\n", pFilename);
	appendDataValidationTrivia(shProcess, dataDir, pFilename, &pNewTrivia, &bAppendFilename);
	pErrorData->pTrivia = pNewTrivia;

	if (bAppendFilename)
	{
		static char *newErrorString = NULL;
		estrPrintf(&newErrorString, "%s %s", pErrorData->pErrorString, pFilename);
		pErrorData->pErrorString = newErrorString;
	}
}

static bool CalculateMemoryDumpName(char *outputFilename, int outputFilename_size);
static bool PopulateErrorData(ErrorData *pErrorData)
{
	pErrorData->eType           = harvestGetIntVar("errortype", ERRORDATATYPE_ASSERT);
	pErrorData->pPlatformName   = harvestGetStringVar("platformname", NULL);
	pErrorData->pExecutableName = harvestGetStringVar("executablename", NULL);
	pErrorData->pProductName    = harvestGetStringVar("productname", NULL);
	pErrorData->pVersionString  = harvestGetStringVar("versionstring", NULL);
	pErrorData->pSVNBranch      = harvestGetStringVar("svnbranch", NULL);
	pErrorData->pExpression     = harvestGetStringVar("expression", NULL);
	pErrorData->pUserWhoGotIt   = harvestGetStringVar("userwhogotit", NULL);
	pErrorData->pErrorString    = harvestGetStringVar("errortext", NULL);
	pErrorData->pStackData      = harvestGetStringVar("stackdata", NULL);
	pErrorData->pSourceFile     = harvestGetStringVar("filename", NULL);
	pErrorData->iSourceFileLine = harvestGetIntVar("lineno", 0);
	pErrorData->iClientCount    = harvestGetIntVar("clientcount", 0);
	pErrorData->pTrivia         = harvestGetStringVar("trivia", NULL);
	pErrorData->iProductionMode = harvestGetIntVar("productionmode", 0);
	pErrorData->pShardInfoString= harvestGetStringVar("shardinfostring", NULL);
	pErrorData->pAppGlobalType  = harvestGetStringVar("appglobaltype", NULL);
	pErrorData->uCEPId			= GetCurrentProcessId();
		
	if (harvestGetStringVar("memorydump", NULL))
	{
		char *lastItem;
		char *context = NULL;
		char *memDump = strdup(harvestGetStringVar("memorydump", NULL));
		char *currChar = strstri_safe(memDump, "TOTAL:"); //Skip to the summary at the end
		if (currChar)
		{
			*currChar = '\0';
			currChar = strrchr(memDump, '-'); //skip to the line of dashes
			if (currChar)
			{
				*currChar = '\0';
				currChar = strrchr(memDump, '\n'); //Skip past the line of dashes
				if (currChar)
				{
					*currChar = '\0';
					currChar = strrchr(memDump, '\n'); //Move to the front of the last memory line
					if (currChar)
					{
						lastItem = strtok_s(currChar, " \n", &context);
						pErrorData->lastMemItem = strdup(lastItem);
					}
				}
			}
		}
		{
			char memdump_fname[MAX_PATH];
			if (CalculateMemoryDumpName(SAFESTR(memdump_fname)))
			{
				FILE *memdump_file = NULL;
				size_t len = strlen(memDump);
				memdump_file = fopen(memdump_fname, "w");
				if (memdump_file)
				{
					fwrite(memDump, 1, len, memdump_file);
					fclose(memdump_file);
				}
			}
		}
		free(memDump);
	}

	if(harvestGetIntVar("isvalidationerror", 0))
		PopulateValidationErrorData(pErrorData, NULL, harvestGetStringVar("validationerrorfile", NULL));

	return true;
}

static bool NotifyErrorTracker()
{
	bool bRet = false;
	ErrorData errorData = {0};
	PopulateErrorData(&errorData);

	setErrorTracker(harvestGetStringVar("errortracker", getErrorTracker()));

	LogNormal("Communicating with Cryptic Error Tracker...");

	bRet = ( errorTrackerSendError(&errorData) && (errorTrackerGetUniqueID() != 0) );

	uiCheckErrorTrackerResponse();
	return bRet;
}

static void IgnoreLauncherPackets(Packet *pkt,int cmd,NetLink* link,void *user_data) {}

static void NotifyCriticalSystems(char *pMyName, char *pControllerTrackerName)
{
	static NetComm *netComm = NULL;
	NetLink *pLink;
	Packet *pPak;
	char *pStackReport;
	char *pColon;
	int iPort = CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT;

	if(harvestIsEndUserMode())
		return;

	LogNormal(STACK_SPRINTF("Notifying ControllerTracker %s...", pControllerTrackerName));

	if (!netComm)
		netComm = commCreate(0,0);

	commSetSendTimeout(netComm, 10.0f);

	if ((pColon = strchr(pControllerTrackerName, ':')))
	{
		*pColon = 0;
		iPort = atoi(pColon + 1);
		if (!iPort)
		{
			return;
		}
	}


	pLink = commConnect(netComm,LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, pControllerTrackerName,
			iPort, 0,0,0,0);

	if (!linkConnectWait(&pLink,2.f))
	{
		return;
	}

	pStackReport = harvestGetStringVar("stackdata", NULL);

	pPak = pktCreate(pLink, FROM_ERRORTRACKER_TO_CONTROLLERTRACKER_CRASH_REPORT);
	pktSendString(pPak, pMyName);
	pktSendString(pPak, pStackReport ? pStackReport : "");
	pktSendString(pPak, getErrorTracker());
	pktSendU32(pPak, errorTrackerGetUniqueID()); // Might be zero
	pktSend(&pPak);

	// Wait a bit for the launcher to get it
	commMonitor(netComm);
	Sleep(250); // Remove? localhost data should instantaneously arrive?
	commMonitor(netComm);
	linkRemove(&pLink);

	return;
}

static void NotifyAllCriticalSystems(char *pMyName, char *pAllControllerTrackerNames)
{
	char **ppCTNames = NULL;
	DivideString(pAllControllerTrackerNames, ",", &ppCTNames, 
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	FOR_EACH_IN_EARRAY(ppCTNames, char, pCTName)
	{
		NotifyCriticalSystems(pMyName, pCTName);
	}
	FOR_EACH_END;

	eaDestroyEx(&ppCTNames, NULL);
}


static bool NotifyLauncher(U32 uPacketType)
{
	static NetComm *netComm = NULL;
	NetLink *pLauncherLink;
	Packet *pPak;

	if(harvestIsEndUserMode())
		return true;

	LogNormal("Notifying Launcher...");

	if (!netComm)
		netComm = commCreate(0,0);

	commSetSendTimeout(netComm, 10.0f);

	pLauncherLink = commConnect(netComm,LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,"localhost",
		GetLauncherListenPort(), IgnoreLauncherPackets, 0,0,0);
	if (!linkConnectWait(&pLauncherLink,2.f))
	{
		return false;
	}

	pPak = pktCreate(pLauncherLink, uPacketType);
	pktSendU32(pPak, (U32)gCEProcessId);
	pktSendU32(pPak, errorTrackerGetUniqueID()); // Might be zero
	pktSend(&pPak);

	// Wait a bit for the launcher to get it
	commMonitor(netComm);
	Sleep(250); // Remove? localhost data should instantaneously arrive?
	commMonitor(netComm);
	linkRemove(&pLauncherLink);

	return true;

}

static bool NotifyErrorTrackerManualDump()
{
	bool bRet;
	ErrorData errorData = {0};

	errorData.eType           = ERRORDATATYPE_GAMEBUG;
	errorData.pPlatformName   = PLATFORM_NAME;
	errorData.pExecutableName = gChosenProcessName;
	errorData.pUserWhoGotIt   = (char *)getUserName();
	errorData.pExpression     = "Manual UserDumps";
	errorData.pErrorString    = gChosenProcessDetails;

	LogNormal("Communicating with Cryptic Error Tracker...");
	bRet = ( errorTrackerSendError(&errorData) && (errorTrackerGetUniqueID() != 0) );

	uiCheckErrorTrackerResponse();
	return bRet;
}

static bool WriteDump(HANDLE hProcess, bool bFullDump, char *filename)
{
	MiniDumpWriter pMiniDumpWriteDump = NULL;
	HMODULE debugDll;
	HANDLE hFile;
	MINIDUMP_TYPE dumpFlags = MiniDumpWithProcessThreadData|MiniDumpWithDataSegs; 
	MINIDUMP_EXCEPTION_INFORMATION mdei = {0};
	PMINIDUMP_EXCEPTION_INFORMATION pmdei = NULL;
	
	if(bFullDump)
		dumpFlags |= MiniDumpWithFullMemory; 

	// Try to load the debug help dll or imagehlp.dll
	debugDll = LoadLibrary( L"dbghelp.dll" );
	if(!debugDll)
	{
		debugDll = LoadLibrary( L"imagehlp.dll" );
		
		if(!debugDll)
		{
			return false;
		}
	}
	pMiniDumpWriteDump = (MiniDumpWriter) GetProcAddress(debugDll, "MiniDumpWriteDump");
	if(!pMiniDumpWriteDump)
	{
		FreeLibrary(debugDll);
		return false;
	}

	if(gExceptionInfo)
	{
		mdei.ExceptionPointers = gExceptionInfo;
		mdei.ThreadId = gCEThreadId;
		mdei.ClientPointers = (gExceptionInfo) ? TRUE : FALSE;
		pmdei = &mdei;
	}

	mkdirtree(filename);

	hFile = CreateFile_UTF8(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE != hFile)
	{
		if(!pMiniDumpWriteDump(hProcess, gCEProcessId, hFile, 
			dumpFlags, pmdei, NULL, NULL))
		{
			HRESULT err = GetLastError();
			char buffer[128];
			sprintf(buffer, "Error Writing Dump: 0x%08X", err);
			LogError(buffer);
			CloseHandle(hFile);
			return false;
		}

		CloseHandle(hFile);
	}

	if(sCrypticErrorMode == CEM_PRODSERVER)
	{
		char *temp = NULL;
		estrPrintf(&temp, "Wrote: %s", filename);
		LogNote(temp);
		estrDestroy(&temp);
	}
	dumpsWritten |= (bFullDump) ? DUMPFLAGS_FULLDUMP : DUMPFLAGS_MINIDUMP;
	return true;
}

static void CalculateDumpName(HANDLE hProcess, U32 whichdump, char *outputFilename, int outputFilename_size)
{
	char *dumpType = "mdmp";

	char *tempDir = getenv("temp");
	if(!tempDir)
		tempDir = ".";

	if(whichdump & DUMPFLAGS_FULLDUMP)
	{
		dumpType = "dmp";
	}

	if(sCrypticErrorMode == CEM_PRODSERVER)
	{
		sprintf_s(SAFESTR2(outputFilename), "%s\\%s.T%d.ET%d.%s", 
			sPermanentDumpDir,
			harvestGetFilename(), 
			timeSecondsSince2000(), 
			errorTrackerGetUniqueID(), 
			dumpType);
		return;
	}

	// Non-server dumps are temporary
	{
		char *pTemp = NULL;
		GetTempFileName_UTF8(tempDir, dumpType, 0, &pTemp);
		strcpy_s(outputFilename, outputFilename_size, pTemp);
		estrDestroy(&pTemp);
	}
}

static bool CalculateMemoryDumpName(char *outputFilename, int outputFilename_size)
{
	char *tempDir = getenv("temp");
	if(!tempDir)
		tempDir = ".";

	if(sCrypticErrorMode == CEM_PRODSERVER)
	{
		sprintf_s(SAFESTR2(outputFilename), "%s\\%s.T%d.ET%d.memdump.txt", 
			sPermanentDumpDir,
			harvestGetFilename(), 
			timeSecondsSince2000(), 
			errorTrackerGetUniqueID());
		return true;
	}
	else
		return false;
}

static bool WriteRequiredDumps(HANDLE hProcess, U32 uForceWrite)
{
	if(sbProcessGone)
	{
		LogNormal("Skipping dump writes, process is gone...");
		return false;
	}

	sbWritingDumps = true;

	dumpsRequired = uForceWrite;
	dumpsRequired |= (errorTrackerGetDumpFlags() & (DUMPFLAGS_MINIDUMP|DUMPFLAGS_FULLDUMP));

	if(sCrypticErrorMode == CEM_PRODSERVER)
	{
		// Its a production server, write a minidump and a full dump anyway.
		dumpsRequired |= DUMPFLAGS_MINIDUMP|DUMPFLAGS_FULLDUMP;
	}

	if(dumpsRequired & (DUMPFLAGS_MINIDUMP|DUMPFLAGS_FULLDUMP))
		LogBold("Preparing Diagnostic Information...");

	if(dumpsRequired & DUMPFLAGS_MINIDUMP)
	{
		LogNormal("Writing Mini Dump...");
		CalculateDumpName(hProcess, DUMPFLAGS_MINIDUMP, SAFESTR(sMiniDumpFilename));
		assertOverrideMiniDumpFilename(sMiniDumpFilename);
		WriteDump(hProcess, false, sMiniDumpFilename);
	}

	if(dumpsRequired & DUMPFLAGS_FULLDUMP)
	{
		LogNormal("Writing Full Dump...");
		CalculateDumpName(hProcess, DUMPFLAGS_FULLDUMP, SAFESTR(sFullDumpFilename));
		assertOverrideFullDumpFilename(sFullDumpFilename);

		WriteDump(hProcess, true, sFullDumpFilename);
	}

	sbWritingDumps = false;
	sbWroteDumps = true;
	return (dumpsRequired == dumpsWritten);
}

static void SendHarvestBeganNotifications(void)
{
	const char *beganEventName = NULL;
	HANDLE beganEvent = NULL;


	if (harvestGetStringVar("CriticalSystem_MyName", NULL) && harvestGetStringVar("CriticalSystem_CTName", NULL))
	{
		NotifyAllCriticalSystems(harvestGetStringVar("CriticalSystem_MyName", NULL), harvestGetStringVar("CriticalSystem_CTName", NULL));
	}

	// Notify launcher that the app began doing CrypticError processing
	NotifyLauncher(TO_LAUNCHER_PROCESS_BEGAN_CRASH_OR_ASSERT);
	


	// Set an event to notify a process that might be watching
	beganEventName = harvestGetStringVar("beganevent", NULL);

	if(!beganEventName)
	{
		return;
	}

	beganEvent = OpenEvent_UTF8(EVENT_ALL_ACCESS, false, beganEventName);

	if(beganEvent)
	{
		SetEvent(beganEvent);
		CloseHandle(beganEvent);
	}
}

static void SendHarvestCompletedNotifications(void)
{
	const char *completedEventName = NULL;
	HANDLE completedEvent = NULL;


	
	// Notify launcher that the app finished CrypticError processing
	NotifyLauncher(TO_LAUNCHER_PROCESS_COMPLETED_CRASH_OR_ASSERT);
	

	// Set an event to notify a process that might be watching
	completedEventName = harvestGetStringVar("completedevent", NULL);

	if(!completedEventName)
	{
		return;
	}

	completedEvent = OpenEvent_UTF8(EVENT_ALL_ACCESS, false, completedEventName);

	if(completedEvent)
	{
		SetEvent(completedEvent);
		CloseHandle(completedEvent);
	}
}

static void dumpSendingUpdate(size_t iSentBytes, size_t iTotalBytes)
{
	char temp[512];
	sprintf(temp, "Sending: %Iu / %Iu bytes", iSentBytes, iTotalBytes);
	LogStatusBar(temp);
}

CrypticErrorMode harvestGetMode()
{
	return sCrypticErrorMode;
}

bool harvestIsEndUserMode()
{
	return (sCrypticErrorMode == CEM_CUSTOMER);
}

bool harvestIsDeveloperMode()
{
	return ((sCrypticErrorMode == CEM_DEVELOPER)
	     || (sCrypticErrorMode == CEM_DEVSERVER));
}

bool harvestNeedsProgrammer()
{
	// "Needs a Programmer" doesn't make sense on a production server, as
	// there is no mechanism right now to notify programmers to go look at it.
	// DUMPFLAGS_AUTOCLOSE implies that the ET CE is talking to is actually a builder.
	if((sCrypticErrorMode == CEM_PRODSERVER) || (errorTrackerGetDumpFlags() & DUMPFLAGS_AUTOCLOSE))
		return (errorTrackerGetErrorResponse() == ERRORRESPONSE_PROGRAMMERREQUEST);

	return ((errorTrackerGetErrorResponse() == ERRORRESPONSE_NULLCALLSTACK)
	     || (errorTrackerGetErrorResponse() == ERRORRESPONSE_PROGRAMMERREQUEST));
}

const char *harvestGetFilename()
{
	if(gChosenProcessName[0] == 0)
		return "UNKNOWNEXE";

	return getFileName(gChosenProcessName);
}

static bool TerminateCrashedProcess(int flags)
{
	if(shProcess == INVALID_HANDLE_VALUE)
		return true;

	if(isProcessGone())
		return true;

	LogNormal("Terminating crashed process...");

	if(!(flags & TF_FORCE))
	{
		if(!(flags & TF_SHUTDOWN))
		{
			// Bailing out in here means you're expecting to offer the user a decision, which
			// is why it is protected by "you can't be shutting down". 

			if(harvestIsDeveloperMode())
			{
				// Give the developer the decision to close the process, unless CrypticError is going away
				return false;
			}

			if(!harvestIsEndUserMode())
			{
				if(harvestNeedsProgrammer())
				{
					LogImportant("Leaving process open; Programmer required");
					return false;
				}

				if(harvestGetIntVar("debugger", 0))
				{
					LogImportant("Leaving process open; Debugger attached");
					return false;
				}
			}
		}

		// Put protections against "automatic" types of termination in here,
		// such as CrypticError no longer "needing" the process. TF_FORCE
		// means the user manually pressed a button requesting a process kill,
		// and that should override automatic donotkill type features (cmdline
		// or file based).

		if (harvestGetIntVar("leaveCrashesUpForever", 0))
		{
			LogImportant("Leaving process open; LeaveCrashesUpForever set");
			return false;
		}

		if(fileExists("c:\\CrypticErrorDoNotKill.txt"))
		{
			LogImportant("Leaving process open; DoNotKill File Found");
			return false;
		}
	}

	TerminateProcess(shProcess, -1);
	CloseHandle(shProcess);
	shProcess = INVALID_HANDLE_VALUE;
	setProcessGone();
	return true;
}

static bool SendDumpAndReport(const char *pCrashInfo, const char *pDescription, int dump_type)
{
	if(!(errorTrackerGetDumpFlags() & dump_type))
	{
		// ET didn't want the dump, don't bother trying to send it.
		return true;
	}
	else
	{
		char *temp = NULL;
		DumpResult dumpResult = errorTrackerSendDump(pCrashInfo, pDescription, dump_type);

		if(dumpResult == DUMPRESULT_SUCCESS)
		{
			LogNormal("Dump send succeeded.");
			return true;
		}

		estrPrintf(&temp, "Dump send FAILED: %s", DumpResultToString(dumpResult));
		LogError(temp);
		estrDestroy(&temp);
	}
	return false;
}

static bool SendDumps()
{
	bool bSuccess = true;
	char *memoryDump = harvestGetStringVar("memorydump", NULL);

	if (gbDontSendDumps)
	{
		return bSuccess;
	}

	sbSendingDumps = true;

	LogBold("Sending Diagnostic Information...");

	if(memoryDump && memoryDump[0])
	{
		ANALYSIS_ASSUME(memoryDump);
		LogNormal("Sending Memory Dump...");
		errorTrackerSendMemoryDump(errorTrackerGetUniqueID(), memoryDump, (int)strlen(memoryDump));
	}

	errorProgressDlgSetUpdateCallback(dumpSendingUpdate);
	errorTrackerSetThrottleCallbacks(throttleReset, throttleProgress);

	if(dumpsWritten & DUMPFLAGS_MINIDUMP)
	{
		LogNormal("Sending Mini Dump...");
		if(!SendDumpAndReport(harvestGetStringVar("assertbuf", ""), "--", DUMPFLAGS_MINIDUMP))
			bSuccess = false;
		LogTransferProgress(0, 100, 1);
	}

	if(dumpsWritten & DUMPFLAGS_FULLDUMP)
	{
		LogNormal("Sending Full Dump...");
		if(!SendDumpAndReport(harvestGetStringVar("assertbuf", ""), "--", DUMPFLAGS_FULLDUMP))
			bSuccess = false;
		LogTransferProgress(0, 100, 1);
	}

	assertOverrideMiniDumpFilename(NULL);
	assertOverrideFullDumpFilename(NULL);

	LogStatusBar("");

	sbSendingDumps = false;

	return bSuccess;
}

static void WaitForDescription()
{
	if(!shouldForceClose() && !uiDescriptionDialogIsComplete())
	{
		LogNormal("Waiting for description...");
		while(!uiDescriptionDialogIsComplete() && !uiWorkIsComplete())
		{
			// TODO Add Timeout?
			Sleep(100);
		}
	}
}

static bool SendDescription()
{
	WaitForDescription();

	if(sbNotified && uiDescriptionDialogIsComplete())
	{
		const char *pDesc = uiGetDescription();
		if(pDesc && *pDesc)
		{
			LogNormal("Sending Description...");
			errorTrackerSendDumpDescriptionUpdate(pDesc);
			LogNormal("Description Sent.");
		}
		else
		{
			LogNormal("Description canceled.");
		}
	}

	return true;
}

static char * getCachedPackageFilename(void)
{
	static char fn[MAX_PATH] = {0};
	char *tempDir = getenv("TEMP");
	if(!tempDir)
		tempDir = "c:";
	sprintf_s(SAFESTR(fn), "%s\\__CrypticErrorPkg", tempDir);
	return fn;
}


static char * getCachedDumpFilename(void)
{
	static char fn[MAX_PATH] = {0};
	char *tempDir = getenv("TEMP");
	if(!tempDir)
		tempDir = "c:";
	sprintf_s(SAFESTR(fn), "%s\\__CrypticErrorDmp", tempDir);
	return fn;
}

static void writeCachedPackage(ErrorData *pErrorData, const char *description)
{
	char *pCacheFilename   = getCachedPackageFilename();
	ErrorDataCache *pCache = StructCreate(parse_ErrorDataCache);

	pCache->iUniqueID   = errorTrackerGetUniqueID();
	pCache->iDumpFlags  = errorTrackerGetDumpFlags();
	pCache->assertbuf   = harvestGetStringVar("assertbuf", NULL); // NULL this before destroying pCache!
	pCache->description = (char*)description;              // NULL this before destroying pCache!
	pCache->pErrorData  = pErrorData;                      // NULL this before destroying pCache!

	makeDirectoriesForFile(pCacheFilename);
	ParserWriteTextFile(pCacheFilename, parse_ErrorDataCache, pCache, 0, 0);

	pCache->assertbuf   = NULL; // We don't own this
	pCache->description = NULL; // We don't own this
	pCache->pErrorData  = NULL; // We don't own this
	StructDestroy(parse_ErrorDataCache, pCache);

	sbWroteCachedPackage = true;
}

static void GenerateErrorCache()
{
	ErrorData errorData = {0};

	LogBold("Caching Error Data...");

	if(!harvestLockDeferred())
	{
		LogNormal("Deferred process already running, bailing out.");
		return;
	}

	LogNormal("Writing cache...");

	PopulateErrorData(&errorData);
	writeCachedPackage(&errorData, uiGetDescription());

	CopyFile_UTF8(sMiniDumpFilename, getCachedDumpFilename(), FALSE);
}

static bool DeleteDumps()
{
	LogBold("Cleaning up...");

	if(sCrypticErrorMode != CEM_PRODSERVER) // Dumps are permanent on servers
	{
		if(sMiniDumpFilename[0])
		{
			LogNormal("Deleting Mini Dump...");
			DeleteFileA(sMiniDumpFilename);
		}

		if(sFullDumpFilename[0])
		{
			LogNormal("Deleting Full Dump...");
			DeleteFileA(sFullDumpFilename);
		}
	}

	return true;
}

void DebugProcess(DebugProcessCommand eCmd)
{
	int rawData = 1;

	if(eCmd == DPC_IGNORE)
		rawData = 2;

	if(shProcess == INVALID_HANDLE_VALUE)
		return;

	if(sDebugMePtr == 0)
		return;

	if(!WriteProcessMemory(shProcess, (LPVOID)sDebugMePtr, &rawData, 4, NULL))
	{
		LogNote("Failed to debug process!");
	}
}

void performProcessAction(ProcessAction eAction)
{
	// Programmers are allowed to do things prior to the dump writing phase (!sbWritingDumps && !sbWroteDumps)
	if(sbWritingDumps || (!sbRegKeyIsProgrammerMachine && !sbWroteDumps))
	{
		LogError("Waiting for dumps...");
		return;
	}

	// Every possible ProcessAction counts as a programmer action
	sbWaitingForProgrammerAction = false;

	switch(eAction)
	{
	xcase PA_REPORT:
		{
			// Only exists to stop "waiting", which causes dumps to start
			// writing and the error reporting system to kick in.
			// Effectively a no-op for if not waiting for a programmer action.
			return;
		}
	xcase PA_DEBUG:
		{
			DebugProcess(DPC_DEBUG);
		}
	xcase PA_IGNORE:
		{
			DebugProcess(DPC_IGNORE);
		}
	xcase PA_TERMINATE:
		{
			TerminateCrashedProcess(TF_FORCE);
		}
	};

	setProcessGone();
	uiShutdown();
}

bool harvestPerformFake()
{
	int i = 0;
	LogBold("Starting Process...");
	LogNormal("Normal Text");
	LogBold("More Bold Text");
	while(!errorProgressDlgIsCancelled())
	{
		Sleep(1000);
	}
	return true;
}

bool harvestLockDeferred()
{
	static HANDLE shDeferred = INVALID_HANDLE_VALUE;
	DWORD dwResult;
	if(shDeferred == INVALID_HANDLE_VALUE)
	{
		shDeferred = CreateMutex(NULL, FALSE, L"_Cryptic_DeferredDump_Lock_");
	}

	WaitForSingleObjectWithReturn(shDeferred, 1000, dwResult);
	return (dwResult == WAIT_OBJECT_0);
}

bool harvestCheckDeferred()
{
	// First, try to grab a system wide lock 
	if(!harvestLockDeferred())
		return false;

	// Check to see if we have cached data.
	{
		char *pCachedPackageFilename = getCachedPackageFilename();
		char *pCachedDumpFilename    = getCachedDumpFilename();
		bool  cachedPackageExists    = fileExists(pCachedPackageFilename);
		bool  cachedDumpExists       = fileExists(pCachedDumpFilename);

		return (cachedPackageExists && cachedDumpExists);
	}
}

bool harvestCheckManualUserDumpFromCmdLine()
{
	return (gCEManualDumpDesc[0] != 0 && gCEProcessId != 0);
}

bool harvestCheckManualUserDump()
{
	if(harvestCheckManualUserDumpFromCmdLine())
		return true;

	return (!gDeferredMode && (gCEPtrArgs[0] == 0));
}

bool harvestPerformDeferred()
{
	// Deferred mode disabled -- jdrago 01/25/10

	//char *pCachedPackageFilename = getCachedPackageFilename();
	//char *pCachedDumpFilename    = getCachedDumpFilename();
	//bool  cachedPackageExists    = fileExists(pCachedPackageFilename);
	//bool  cachedDumpExists       = fileExists(pCachedDumpFilename);

	//if(cachedPackageExists && cachedDumpExists)
	//{
	//	ErrorDataCache cache = {0};

	//	LogNormal("Reading Error Cache...");
	//	ParserReadTextFile(pCachedPackageFilename, parse_ErrorDataCache, &cache, 0);
	//	if (cache.pErrorData)
	//	{
	//		LogNormal("Communicating with Cryptic Error Tracker...");

	//		if(errorTrackerSendError(cache.pErrorData) && errorTrackerGetUniqueID())
	//		{
	//			errorProgressDlgSetUpdateCallback(dumpSendingUpdate);
	//			errorTrackerSetThrottleCallbacks(throttleReset, throttleProgress);

	//			assertOverrideMiniDumpFilename(pCachedDumpFilename);

	//			LogNormal("Sending Mini Dump...");
	//			SendDumpAndReport(cache.assertbuf, cache.description, DUMPFLAGS_MINIDUMP);
	//			LogTransferProgress(0, 100, 1);

	//			assertOverrideMiniDumpFilename(NULL);

	//			WriteLog(WRITELOGFLAG_ERRORTRACKER_PRESENT|WRITELOGFLAG_DEFERRED);

	//			// Cleanup the files so we aren't constantly sending the same dump over and over.
	//		}

	//		StructDeInit(parse_ErrorDataCache, &cache);
	//	}

	//	if(cachedPackageExists)
	//		DeleteFile(pCachedPackageFilename);

	//	if(cachedDumpExists)
	//		DeleteFile(pCachedDumpFilename);
	//}

	return true;
}

static void GetManualDumpEXENameFromPID(HANDLE hProcess)
{
	HMODULE hMod;
	DWORD cbIgnored;
	char errBuffer[128] = "";

	if (EnumProcessModules( hProcess, &hMod, sizeof(hMod), &cbIgnored))
	{
		char *pTemp = NULL;

		if (!GetModuleBaseName_UTF8( hProcess, hMod, &pTemp ))
		{
			DWORD err = GetLastError();

			sprintf(errBuffer, "GetModuleBaseName failed: %d", err);
			LogError(errBuffer);
		}
		else
		{
			strcpy(gChosenProcessName, pTemp);
		}

		estrDestroy(&pTemp);
	}
	else
	{
		DWORD err = GetLastError();
		sprintf(errBuffer, "EnumProcessModules failed: %d", err);
		LogError(errBuffer);
	}
}

static bool WaitForProgrammerAction()
{
	if(!gbSkipUserInput && !harvestIsEndUserMode() && sbRegKeyIsProgrammerMachine && !harvestGetIntVar("IsContinuousBuilder", 0) && !gbIgnoreProgrammerMode)
	{
		LogImportant("Programmer; Choose Process Action");

		sbWaitingForProgrammerAction = true;
		while(sbWaitingForProgrammerAction && !uiIsShuttingDown())
		{
			Sleep(50);
		}

		// If the process is gone after this while loop, it is because
		// performProcessAction() decided to bail out, which causes
		// the process to "go away" (be forgotten). We return true here
		// to avoid doing anything more for this harvest.
		return isProcessGone();
	}

	return false;
}

void harvestStartup(void)
{
	LookupRegistryKeys();
}

bool harvestDisplayInfo(void)
{
	if(gDisplayInfoOnly)
	{
		char *temp = NULL;
		estrPrintf(&temp, 
				   " * ProductionServer: %s\n"
				   " * ProgrammerMachine: %s\n"
				   " * DumpDir: %s\n"
				   ,
				   sbRegKeyIsProductionServer  ? "True" : "False",
				   sbRegKeyIsProgrammerMachine ? "True" : "False",
				   sPermanentDumpDir
      			  );
		MessageBox_UTF8(NULL, temp, "CrypticError Info", MB_OK);
		estrDestroy(&temp);
	}

	return gDisplayInfoOnly;
}

bool harvestPerformManualUserDump()
{
	bool bDumpSendSuccess = true;
	bool bFromCmdLine = harvestCheckManualUserDumpFromCmdLine();
	if(bFromCmdLine)
	{
		strcpy_trunc(gChosenProcessDetails, gCEManualDumpDesc);

		sCrypticErrorMode = CEM_PRODSERVER; // Permanent dumps
	}
	else
	{
		if(!uiChooseProcess())
			return false;

		sCrypticErrorMode = CEM_CUSTOMER;
	}

	uiRequestSwitchMode();

	if(bFromCmdLine)
	{
		LogImportant("Stalled Process mode.");
	}
	else
	{
		gCEProcessId = gChosenProcessID;
	}

	shProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, gCEProcessId);
	if(!shProcess)
	{
		LogError("Couldn't attach to selected process!");
		Sleep(5000);
		return false;
	}

	if(bFromCmdLine)
	{
		GetManualDumpEXENameFromPID(shProcess);
	}

	if(!NotifyErrorTrackerManualDump())
	{
		LogError("Couldn't talk to ErrorTracker!");
		Sleep(5000);
		return false;
	}

#ifdef HARVEST_WAIT_FOR_DEBUGGER
	printf("Waiting for someone to attach ...\n");
	while(!IsDebuggerPresent())
	{
		Sleep(100);
	}
	Sleep(4000);
#endif

	FreezeProcess(gChosenProcessID, true);
	WriteRequiredDumps(shProcess, DUMPFLAGS_FULLDUMP);
	FreezeProcess(gChosenProcessID, false);

	bDumpSendSuccess = SendDumps();
	DeleteDumps();

	LogBold("Complete!");
	if (gbForceStayUp || !gbForceAutoClose)
		uiPopErrorID();
	return bDumpSendSuccess;
}

static bool harvestStandard();
static bool harvestNoErrorTracker();
static bool harvestXperfFile();

static void clearProcessFPExceptionMask(void)
{
#ifndef _WIN64
	HANDLE hSnapshot;

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE)
	{
		THREADENTRY32 threadEntries = {0};
		threadEntries.dwSize = sizeof(threadEntries);
		if (Thread32First(hSnapshot, &threadEntries))
		{
			do
			{
				if (threadEntries.th32OwnerProcessID == (DWORD)gCEProcessId)
				{
					HANDLE hThread = OpenThread(THREAD_SET_CONTEXT|THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME, FALSE, threadEntries.th32ThreadID);
					if(hThread)
					{
						CONTEXT c = {0};
						
						SuspendThread(hThread);

						// Ask for the whole context.

						c.ContextFlags = ~0;
						GetThreadContext(hThread, &c);
						
						// Mask the x87 exceptions.
						
						c.FloatSave.ControlWord |= _MCW_EM;
						
						// Mask the SSE exceptions.
						
						c.ExtendedRegisters[24] |= BIT(7);
						c.ExtendedRegisters[25] |= BIT_RANGE(0, 4);
						
						SetThreadContext(hThread, &c);
						ResumeThread(hThread);
						CloseHandle(hThread);
					}
				}
			}
			while (Thread32Next(hSnapshot, &threadEntries));
		}

		CloseHandle (hSnapshot);
	}
#endif
}

// The main entry point of the CrypticError "meat"
bool harvestPerform()
{
#ifdef HARVEST_WAIT_FOR_DEBUGGER
	printf("Waiting for someone to attach ...\n");
	while(!IsDebuggerPresent())
	{
		Sleep(100);
	}
	Sleep(4000);
#endif

	if(gCEPtrArgs[0] == 0)
	{
		LogError("Did not receive argument address.");
		return false;
	}

	if(!gCEProcessId)
	{
		LogError("No process ID specified.");
		return false;
	}

	LogNormal("Attaching to crashed process...");
	shProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_TERMINATE, FALSE, gCEProcessId);
	if(!shProcess)
	{
		printf("Couldn't open process PID %d for read!", gCEProcessId);
		return false;
	}

	if(!PopulateVarsFromProcess(shProcess))
	{
		return false;
	}

	if (!gCEXperfMode)
		clearProcessFPExceptionMask();

	SwitchCrypticErrorMode();

	if(WaitForProgrammerAction())
		return true; // indicate "success"; programmer chose to directly manipulate process instead of reporting

	sbNotified = NotifyErrorTracker();
	if(!sbNotified)
	{
		return harvestNoErrorTracker();
	}

	if (gCEXperfMode)
		return harvestXperfFile();
	else
		return harvestStandard();
}

// Normal flow ... assumes a healthy connection to the ErrorTracker
static bool harvestStandard()
{
	bool bSuccess = true;
	SendHarvestBeganNotifications();
	WriteRequiredDumps(shProcess, 0);
	if(!TerminateCrashedProcess(0))
		bSuccess = false;
	SendHarvestCompletedNotifications();
	if(!SendDumps())
		bSuccess = false;
	DeleteDumps();

	LogStatusBar("Complete!");
	LogBold("Cleanup Complete!");
	LogDiskf("harvestSuccess: %s", bSuccess ? "True" : "False");
	
	if (!gbSkipUserInput)
		uiDumpSendComplete();

	if (!gbSkipUserInput)
		SendDescription();

	LogBold("All Error Reporting Complete!");

	if (!gbSkipUserInput)
		uiFinished(bSuccess);
	
	return bSuccess;
}

// Failed to talk to ErrorTracker, backup plan!
static bool harvestNoErrorTracker()
{
	LogError("Failed to talk to ErrorTracker.");
	if (!gbSkipUserInput)
		uiDisableDescription();

	if (!gCEXperfMode)
	{
		// Xperf mode doesn't send notifications or generate dumps
		SendHarvestBeganNotifications();
		WriteRequiredDumps(shProcess, DUMPFLAGS_MINIDUMP);
		TerminateCrashedProcess(0);
		SendHarvestCompletedNotifications();
		// GenerateErrorCache();
		DeleteDumps();
	}

	LogStatusBar("Complete (Failed)");
	
	return false;
}

// XPerf sending
static bool harvestXperfFile()
{
	bool bSuccess = true;
	const char *filename;
	char fullPath[MAX_PATH];
	ErrorXperfData xperfData = {0};

	filename = harvestGetStringVar("xperffile", NULL);
	if (!filename)
		return false;
	sprintf(fullPath, "%s%s", XPERFDUMP_DIRECTORY_PATH, filename);
	if (!fileExists(fullPath))
		return false;

	xperfData.pFilename = StructAllocString(filename);
	errorTrackerSendGenericFile(errorTrackerGetUniqueID(), ERRORFILETYPE_Xperf, fullPath, &xperfData, parse_ErrorXperfData);
	SAFE_FREE(xperfData.pFilename);
	return bSuccess;
}

void harvestWorkComplete(bool bHarvestSuccess)
{
	if (!gCEXperfMode)
		TerminateCrashedProcess(TF_SHUTDOWN);
}