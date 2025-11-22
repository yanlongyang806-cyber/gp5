#include "continuousBuilder.h"
#include "superassert.h"
#include "sysutil.h"
#include "MemoryMonitor.h"
#include "cmdparse.h"
#include "foldercache.h"
#include "file.h"
#include "globaltypes.h"
#include "svnutils.h"
#include "earray.h"
#include "Stringcache.h"
#include "utils.h"
#include "textparser.h"
#include "resource.h"
#include "winutil.h"
#include "ThreadManager.h"
#include "Stringutil.h"
#include "net/net.h"
#include "GlobalComm.h"
#include "simpleParser.h"
#include "resource.h"
#include "suspectList.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "fileutil2.h"
#include "sock.h"
#include "fileutil.h"
#include "buildscripting.h"
#include "objContainer.h"
#include "filewatch.h"
#include "GimmeUtils.h"
#include "process_util.h"
#include "logging.h"
#include "netSMTP.h"
#include "CBZoomedInTextEditor.h"
#include "regex.h"

#include "continuousBuilder_h_ast.h"
#include "continuousBuilder_c_ast.h"
#include "autogen/SVNUtils_h_ast.h"
#include "UtilitiesLib.h"
#include "stringcache.h"
#include "alerts.h"
#include "CBStartup.h"
#include "CBConfig.h"
#include "statusreporting.h"
#include "xboxHostIO.h"
#include "ContinuousBuilderTiming.h"
#include "CBErrorProcessing.h"
#include "ETCommon/ETCommonStructs.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETWebCommon.h"
#include "GenericHttpServing.h"
#include "Organization.h"
#include "CBReportToCBMonitor.h"
#include "Continuousbuilder_pub_h_ast.h"
#include "StashTable.h"
#include "UTF8.h"

int sMaxToJabberAtOnce = 10;
AUTO_CMD_INT(sMaxToJabberAtOnce, MaxToJabberAtOnce);

int sMaxToEmailAtOnce = 15;
AUTO_CMD_INT(sMaxToEmailAtOnce, MaxToEmailAtOnce);

//if true, then skip all slow startup steps so you get into the running of the script as fast as possible
bool gbFastTestingMode = false;
AUTO_CMD_INT(gbFastTestingMode, FastTestingMode);

//gets incremented to 1 during the first INIT step, and then incremented in each further INIT
int giBuildRunCount = 0;

static bool sbDontPauseNextTime = false;
extern FolderCache *folder_cache; // from file.c
extern ParseTable parse_ErrorTrackerEntryUserData[];
#define TYPE_parse_ErrorTrackerEntryUserData ErrorTrackerEntryUserData
int giServerMonitorPort = 8084;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);
static DWORD siMainThreadID = 0;
ErrorEntry *FindErrorInList(ErrorEntry *pEntry, ErrorTrackerEntryList *pList);
static void CB_MergeAndCleanupErrors(void);
static bool sbEnableServerMonitor = false;
bool gbIsOnceADayBuild = false;

static bool sbForceStopWaiting = false;


//extern void BlameCacheDisable(void);

static void ContinuousBuilder_MainLoopEveryFrame(void)
{
	commMonitor(errorTrackerCommDefault());
	if (sbEnableServerMonitor) GenericHttpServing_Tick();
}

static int siSleepLength = 1;
AUTO_CMD_INT(siSleepLength, SleepLength);

#define SLEEP Sleep(siSleepLength)

#define CB_EMAIL_ALL_STRING "CBEmailAll:"

HWND ghCBDlg = 0;

#define GLOBAL_DUMP_PURGE_AGE  4

extern ParseTable parse_SMTPMessageRequest[];
#define TYPE_parse_SMTPMessageRequest SMTPMessageRequest

bool gbJustCompletedProcessOrJustStartedUp = false;

bool gbPauseBetweenRuns = false;
bool gbPauseBetweenSteps = false;
bool gbCurrentlyPausedBetweenSteps = false;

bool gbNeedFullCompile = false;

char *gpSharedSVNRoot = NULL;

static int siCompileRetryCount = 0;

static char sCurScriptingFileName[MAX_PATH] = "UNDEFINED";
static bool sScriptingFileNameChanged = true;

//the ContinuousBuilder listens for connections from controllers it spawns
NetListen *CBListen;

CRITICAL_SECTION gCBCriticalSection;
CRITICAL_SECTION gCBCommentsCriticalSection;

//ss2000 that we last started our test
U32 giLastTimeStarted = 0;

//this is the "key" that identifies individual builds for things like log dir names
char *gpLastTimeStartedString = NULL; //local stringified version of last time started with no spaces


char compileOutputFileName[MAX_PATH] = "";

U32 gTimeCBStartedUp;

bool gbNoSelfPatching = false;
AUTO_CMD_INT(gbNoSelfPatching, NoSelfPatching);

//xbox dump files after they were copied to the local dir
char **gppXboxDumpFiles = NULL;


void CheckForNewXboxDumps(void);

AUTO_ENUM;
typedef enum
{
	CBSTATE_INIT,
	CBSTATE_PAUSED,
	CBSTATE_GETTING,
	CBSTATE_COMPILING,
	CBSTATE_TESTING,
	CBSTATE_TESTING_NONFATAL,
	CBSTATE_CHECKIN,

	CBSTATE_DONE_SUCCEEDED,
	CBSTATE_DONE_SUCCEEDED_W_ERRORS,
	CBSTATE_DONE_FAILED,
	CBSTATE_DONE_ABORTED,

	CBSTATE_WAITING,

} enumCBState;



AUTO_STRUCT;
typedef struct CBDynamicState
{
	CBRevision lastGoodRev;
	CBRevision lastCheckedInRev;
	CBRevision lastRev;
	int iNextEmailNumber;
	SuspectList curSuspects;
	ErrorTrackerContext *pLastErrorContext; AST(LATEBIND)
	enumCBResult eLastResult;
	enumCBResult eLastNonAbortResult;
	U32 iLastSuccessfulCycleTime;

	//the CB has a feature where you can put $ONCE_A_DAY(foo) or $ALL_BUT_ONCE_A_DAY(foo) in the config
	//screens. If a build is starting between 1 and 6 a.m., and which is at least 23 hours since the last recorded once-
	//a-day build, will turn on all the ONCE_A_DAY things and turn off all the ALL_BUT_ONCE_A_DAY things. All other
	//builds will do the opposite
	U32 iLastOnceADayTime;

	//we specially maintain a list of precisely who we emailed last time the build fatally broke, so that we can email them and tell them
	//it's fixed
	char **ppLastFatalBreakageEmailRecipients;

} CBDynamicState;



enumCBState geState = CBSTATE_INIT;

char sCurTestTitle[128] = "";
U32 giTimeEnteredState = 0;
U32 giLastCheckPointTime = 0;
char gLastBuildBrokeFileName[MAX_PATH] = "";

char *gpSVNRepository = NULL; //estring

void SendEmailToList_Internal(enumEmailFlags eFlags, char ***pppList, char *pFileName, char *pSubjectLine);
#define SendEmailToList(eFlags, pppList, pFileName, pSubjectLine) SendEmailToList_Internal(eFlags, pppList, pFileName, pSubjectLine); 
void SendJabberToList_Internal(char ***pppList, char *pMessage, bool bNoDefaltRecipients);
#define SendJabberToList(pppList, pMessage, bNoDefaultRecipients) SendJabberToList_Internal(pppList, pMessage, bNoDefaultRecipients); 
void MoveAndUpdateDumpFiles(ErrorTrackerEntryList *pErrors);
void SendEmailToAdministrators(char *pFailureString);
bool IsFatal(ErrorEntry *pEntry);

char *pLastDescriptiveString = NULL;
char *pLastHTMLBugsString = NULL;

void SetSubStateString(const char *pNewString);
void SetSubStateStringf(FORMAT_STR const char *pFmt, ...);
const char *GetSubStateString(void);


void SetSubSubStateString(const char *pNewString);
void SetSubSubStateStringf(FORMAT_STR const char *pFmt, ...);
const char *GetSubSubStateString(void);

CheckinInfo **ppSVNCheckinsInSuccessfulBuild = NULL;
CheckinInfo **ppGimmeCheckinsInSuccessfulBuild = NULL;
CheckinInfo **ppSVNCheckinsBeingTested = NULL;
CheckinInfo **ppGimmeCheckinsBeingTested = NULL;

enumCBResult eCurResult = CBRESULT_NONE;

int gbEmailDefaultRecipientsOnly = 0;
AUTO_CMD_INT(gbEmailDefaultRecipientsOnly, EmailDefaultRecipientsOnly);

CBRevision gFirstBadRev = {0};
CBRevision gCurRev = {0};

CBDynamicState gDynamicState = {0};


//returns c:\continuousbuilder\productname\typename
char *GetCBDirectoryName(void);

//returns c:\continuousbuilder\productname\typename\logs\STARTTIME
//inserts "path name" from child context if there is one after STARTTIME, NULL means use the parent context
char *GetCBLogDirectoryName(BuildScriptingContext *pContext);

//returns c:\continuousbuilder\productname\typename\logs\STARTTIME\CBLog.txt
//inserts "path name" from child context if there is one after STARTTIME, NULL means use the parent context
char *GetCBLogFileName(BuildScriptingContext *pContext);

//returns c:\continuousbuilder\productname\typename\logs\STARTTIME\CBScriptingLog.txt
//inserts "path name" from child context if there is one after STARTTIME, NULL means use the parent context
char *GetCBScriptingLogFileName(BuildScriptingContext *pContext);

//returns c:\continuousbuilder\productname\typename\autoBuildHistory.txt
char *GetCBAutoHistoryFileName(void);

//passed the SHORT build type name and the NORMAL product name, returns c:\continuousbuilder\productname\typename
char *GetCBDirectoryNameWithBuildTypeAndProductName(char *pBuildTypeName, char *pProductName);
char *GetCBLogDirectoryNameWithBuildTypeAndProductName(char *pBuildTypeName, char *pProductName);

//used to make http links that will continue to work even when running a different build,
//returns productname=fightclub&buildtype=cont 
char *GetProductAndBuildTypeLinkString(void)
{
	static char retVal[256] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "productname=%s&buildtype=%s",gpCBProduct->pProductName, gpCBType->pShortTypeName);
	}

	return retVal;
}


AUTO_STRUCT;
typedef struct BuilderStatusStruct
{
	char *pCurStateString;
	char *pCurStateTime;
	U32 iSecondsInCurrentState;
	char *pTimeInBuild;
	U32 iSecondsInBuild;
	char *pStatusSubString;
	char *pStatusSubSubString;
	char* pLastBuildResult;
	char* pLastSuccessfullBuildTime;

	CBConfig *pConfig;

	CBDynamicState *DynamicState; // ????

	enumCBResult CurrentResult;
} BuilderStatusStruct;

void GetCurrentBuilderStatusStruct( BuilderStatusStruct* pBSS );


AUTO_STRUCT;
typedef struct CBComment
{
	char *pStr; AST(ESTRING)
	CommentCategory eCategory;
	U32 iTime;
} CBComment;

CBComment **sppComments = NULL;
bool sBCommentsChanged = false;

void AddComment(CommentCategory eCategory, char *pString, ...)
{
	CBComment *pNewComment = StructCreate(parse_CBComment);
	va_list ap;

	EnterCriticalSection(&gCBCommentsCriticalSection);

	va_start(ap, pString);
	estrConcatfv(&pNewComment->pStr, pString, ap);
	va_end(ap);

	pNewComment->iTime = timeSecondsSince2000();
	pNewComment->eCategory = eCategory;

	eaPush(&sppComments, pNewComment);
	sBCommentsChanged = true;

	LeaveCriticalSection(&gCBCommentsCriticalSection);
}

void OVERRIDE_LATELINK_BuildScriptingAddComment(const char *pStr)
{
	AddComment(COMMENT_COMMENT, "%s", pStr);
}

static BuildScriptingContext *spRootScriptingContext = NULL;
BuildScriptingContext *CBGetRootScriptingContext(void)
{
	return spRootScriptingContext;
}

void SendComments(NetLink *pLink)
{
	int i;
	char *pFullString = NULL;
	char *pTempString = NULL;
	EnterCriticalSection(&gCBCommentsCriticalSection);
	
	estrConcatf(&pFullString, "<pre>\n");


	for (i=0 ; i < eaSize(&sppComments); i++)
	{
		estrCopyWithHTMLEscapingSafe(&pTempString, sppComments[i]->pStr, false);
		estrConcatf(&pFullString, "%s: %s\n",
			timeGetLocalTimeStringFromSecondsSince2000(sppComments[i]->iTime), pTempString);
	}


	estrConcatf(&pFullString, "</pre>\n");

	errorTrackerLibSendWrappedString(pLink, pFullString);
	LeaveCriticalSection(&gCBCommentsCriticalSection);
	estrDestroy(&pFullString);
	estrDestroy(&pTempString);

}


void SendCBTiming(NetLink *pLink, U32 iStartTime, char *pStepString)
{
	char *pString = NULL;
	CBTiming_ToHTMLString(&pString, iStartTime, pStepString);
	errorTrackerLibSendWrappedString(pLink, pString);
	estrDestroy(&pString);
}

const char *GetLastResultStateString(void)
{
	return StaticDefineIntRevLookup(enumCBResultEnum,gDynamicState.eLastResult);
}

const char *GetLastNonAbortResultStateString(void)
{
	return StaticDefineIntRevLookup(enumCBResultEnum,gDynamicState.eLastNonAbortResult);
}

void ResetComments(void)
{
	EnterCriticalSection(&gCBCommentsCriticalSection);
	eaDestroyStruct(&sppComments, parse_CBComment);
	LeaveCriticalSection(&gCBCommentsCriticalSection);
}


#if 0
//makes the target directory, then copies all files to it, then erases them, then gets upset and emails administrators
//if some couldn't be deleted
void MoveDirectoryContents(char *pSrcName, char *pDestName)
{
	char fullDestPath[CRYPTIC_MAX_PATH];
	char systemString[1024];
	char fullSrcPath[CRYPTIC_MAX_PATH];

	strcpy(fullSrcPath, pSrcName);

	
	sprintf(fullDestPath, "%s\\%s", getDirectoryName(fullSrcPath), pDestName);
	

	strcpy(fullSrcPath, pSrcName);
	backSlashes(fullSrcPath);

	backSlashes(fullDestPath);


		
	mkdirtree_const(STACK_SPRINTF("%s\\foo.txt", fullDestPath));


	sprintf(systemString, "xcopy /E %s\\*.* %s", fullSrcPath, fullDestPath);
	system(systemString);

	sprintf(systemString, "del /F /S /Q %s\\*.*", fullSrcPath);
		system(systemString);


	if (CountFilesInDir(fullSrcPath) != 0)
	{
		SendEmailToAdministrators(STACK_SPRINTF("Couldn't erase all files from %s", fullSrcPath));
	}
}

#endif





void RelocateAbortedLogs(void)
{
	char systemString[1024];
	logWaitForQueueToEmpty();
	logFlush();
	sprintf(systemString, "rename %s %s_ABORTED", GetCBLogDirectoryName(NULL), gpLastTimeStartedString);
	system(systemString);
}



void StartBuildScripting(char *pFileName, int iIncludeDepth)
{
	char *pBuildID = NULL;
	char *pScriptDirSuffix = NULL;
	
	CBTiming_Step(STACK_SPRINTF("Scripting startup: %s", pFileName), iIncludeDepth);

	filelog_printf(GetCBLogFileName(NULL), "Starting build scripting in %s", GetCBScriptingLogFileName(NULL));

	AddComment(COMMENT_SCRIPTING, "New Script: %s", pFileName);

	sprintf(sCurScriptingFileName, "%s", pFileName);
	sScriptingFileNameChanged = true;

	BuildScripting_ResetResettableStartingVariables(CBGetRootScriptingContext());

	CB_SetScriptingVariablesFromConfig();


	BuildScripting_Begin(CBGetRootScriptingContext(), pFileName, iIncludeDepth, gConfig.ppScriptDirectories);
	
	filelog_printf(GetCBScriptingLogFileName(NULL), "");
	filelog_printf(GetCBScriptingLogFileName(NULL), "");
	filelog_printf(GetCBScriptingLogFileName(NULL), "");
}

void OVERRIDE_LATELINK_BuildScriptingLogging(BuildScriptingContext *pContext, bool bIsImportant, char *pString)
{
	if (bIsImportant)
	{
		filelog_printf(GetCBLogFileName(pContext), "%s", pString);
	}

	filelog_printf(GetCBScriptingLogFileName(pContext), "%s", pString);
}

void OVERRIDE_LATELINK_BuildScriptingFailureExtraStuff(BuildScriptingContext *pContext, char *pErrorString, char *pExtraString)
{
	//during compile step, we will always generate a more useful error message, so we ignore this to avoid redundant errors,
	//unless it's a timeout
	if (geState != CBSTATE_COMPILING || strstri(pErrorString, "Failure time overflow"))
	{
		ErrorData data = {0};									
		data.eType = ERRORDATATYPE_FATALERROR;						
		data.pErrorString = pErrorString;
		data.pUserDataStr = pExtraString;
		CB_ProcessErrorData(NULL, &data); 
	}

	if (!CBTypeIsCONT())
	{
		CBTiming_Fail();
	}
}

//callback to modify command lines of System commands
void OVERRIDE_LATELINK_BuildScriptingCommandLineOverride(BuildScriptCommand *pCommand, char **ppCmdLine)
{
	if (pCommand->eSubType == BSCSUBTYPE_CONTROLLER_STYLE && (strstri(*ppCmdLine, "controller.exe") || strstri(*ppCmdLine, "controllerFD.exe")))
	{
		static char *pDirName = NULL;
		estrCopy2(&pDirName, pCommand->outputFileName);
		estrTruncateAtLastOccurrence(&pDirName, '.');


		//propogate LeaveCrashesUpForever down to any spawned controllers
		if (gbLeaveCrashesUpForever)
		{
			estrConcatf(ppCmdLine, " -LeaveCrashesUpForever ");
		}

		estrConcatf(ppCmdLine, " -?PrintfForkingDir %s", pDirName);
		
	}

}

char *OVERRIDE_LATELINK_BuildScriptingGetLogFileLocation(BuildScriptingContext *pContext, const char *pFName)
{
	static char *pFullName = NULL;
	estrPrintf(&pFullName, "%s\\%s", GetCBLogDirectoryName(pContext), pFName);
	return pFullName;
}

char *OVERRIDE_LATELINK_BuildScriptingGetStatusFileLocation(const char *pFName)
{
	static char *pFullName = NULL;
	estrPrintf(&pFullName, "%s\\%s", GetCBDirectoryName(), pFName);
	return pFullName;
}

char *OVERRIDE_LATELINK_BuildScriptingGetLinkToLogFile(BuildScriptingContext *pContext, const char *pFName, const char *pLinkName)
{
	if (pFName)
	{
		static char *pRetString = NULL;

		estrPrintf(&pRetString, "<a href=\"http://%s/logfile?buildStartTime=%s&fname=%s&%s\">%s</a>",
			getHostName(), gpLastTimeStartedString, pFName, GetProductAndBuildTypeLinkString(), pLinkName);

		return pRetString;
	}
	else
	{
		static char *pRetString = NULL;

		estrPrintf(&pRetString, "<a href=\"http://%s/logs?buildStartTime=%s&%s\">%s</a>",
			getHostName(), gpLastTimeStartedString, GetProductAndBuildTypeLinkString(), pLinkName);

		return pRetString;



	}
}


char *OVERRIDE_LATELINK_BuildScriptingGetLogDir(BuildScriptingContext *pContext)
{
	return GetCBLogDirectoryName(pContext);
}

void DoOldFilePurging(void)
{
	PurgeDirectoryCriterion **ppCriteria = NULL;

	//everything with _ABORTED in its name we purge > 1 day old
	PurgeDirectoryCriterion aborted = { "_ABORTED", 24 * 60 * 60 };

	//everything with tempcount in its name we purge > 1 day old
	PurgeDirectoryCriterion tempcount = { "tempcount", 24 * 60 * 60 };

	//everything in our log folder we purge > 14 days old
	PurgeDirectoryCriterion logs;

	char logDirName[CRYPTIC_MAX_PATH];

	char *pPurgeTimeDays = GetConfigVar("LOG_PURGE_TIME_DAYS");

	printf("Purging old log files\n");

	sprintf(logDirName, "%s\\logs", GetCBDirectoryName());
	backSlashes(logDirName);

	logs.iSecondsOld = 24 * 60 * 60 * (pPurgeTimeDays ? atoi(pPurgeTimeDays) : 45);
	logs.pNameToMatch = logDirName;

	eaPush(&ppCriteria, &aborted);
	eaPush(&ppCriteria, &tempcount);
	eaPush(&ppCriteria, &logs);


	PurgeDirectoryOfOldFiles_MultipleCriteria("c:\\continuousbuilder", &ppCriteria);

	eaDestroy(&ppCriteria);

	if (!gConfig.bDev)
	{
		PurgeDirectoryOfOldFiles("c:\\Night\\dumps", 14, NULL);
	}

	if (gConfig.pGlobalDumpLocation)
	{
		PurgeDirectoryOfOldFiles(gConfig.pGlobalDumpLocation, GLOBAL_DUMP_PURGE_AGE, NULL);
	}


}


void CheckForNewerCBAndMaybeRestart(void)
{
	char *pMyFileName = getExecutableName();
	char *pOfficialVersionFileName = "n:\\ContinuousBuilder\\ContinuousBuilder.exe";

	char *pMyDirectory = NULL; //estring
	char *pMyFileNameNoDirectory = NULL; //estring



	char *pBackupFileName = NULL; //estring
	char *pSystemString = NULL; //estring

	U32 iMyTime, iOfficialVersionTime;

	char *pLastBackSlash;

	char *pRestartBatchFileName = NULL; //estring

	FILE *pRestartBatchFile;

	char originalWorkingDir[CRYPTIC_MAX_PATH];

	if (!fileExists(pOfficialVersionFileName))
	{
		return;
	}

	iMyTime = fileLastChangedSS2000(pMyFileName);
	iOfficialVersionTime = fileLastChangedSS2000(pOfficialVersionFileName);

	if (iMyTime >= iOfficialVersionTime)
	{
		return;
	}

	fileGetcwd(SAFESTR(originalWorkingDir));

	estrCopy2(&pMyDirectory, pMyFileName);
	backSlashes(pMyDirectory);
	pLastBackSlash = strrchr(pMyDirectory, '\\');

	if (!pLastBackSlash)
	{
		printf("WARNING: can't do CB patching because of directory weirdness\n");
		return;
	}
	

	estrCopy2(&pMyFileNameNoDirectory, pLastBackSlash + 1);
	estrSetSize(&pMyDirectory, pLastBackSlash - pMyDirectory + 1);


	printf("Detected newer continuousbuilder.exe... going to copy it and restart\n");

	estrCopy2(&pBackupFileName, pMyFileNameNoDirectory);
	estrReplaceOccurrences(&pBackupFileName, ".exe", ".bak");


	assert(chdir(pMyDirectory) == 0);


	estrPrintf(&pSystemString, "erase %s", pBackupFileName);
	backSlashes(pSystemString);
	system(pSystemString);

	estrPrintf(&pSystemString, "rename %s %s", pMyFileNameNoDirectory, pBackupFileName);
	backSlashes(pSystemString);
	system(pSystemString);

	

	estrPrintf(&pSystemString, "copy %s %s", pOfficialVersionFileName, pMyFileName);
	backSlashes(pSystemString);
	system(pSystemString);

	estrPrintf(&pRestartBatchFileName, "%sRestartCB.bat", pMyDirectory);

	pRestartBatchFile = fopen(pRestartBatchFileName, "wt");
	fprintf(pRestartBatchFile, "sleep 4\ncd %s\n%s%s\n", originalWorkingDir, GetCommandLine(),
		strstri(GetCommandLine(), "autostart") || giBuildRunCount == 0 ? "" : " -autostart ");
//the first time through, don't add autostart if it wasn't already there. Otherwise, incr and baselines builds
//don't see the options screen on CB restart


	fclose(pRestartBatchFile);

	estrPrintf(&pSystemString, "cmd.exe /c %s", pRestartBatchFileName);
	system_detach(pSystemString, false, false);

	exit(0);
}






char *GetCBName(void)
{
	static char buf[256] = "";

	if (!buf[0])
	{
		sprintf(buf, "%s %s", gpCBProduct->pProductName, gpCBType->pShortTypeName);
	}

	return buf;
}

char *GetCBFullName(void)
{
	static char buf[256] = "";

	if (!buf[0])
	{
		sprintf(buf, "%s %s", gpCBProduct->pProductName, gpCBType->pTypeName);
	}

	return buf;
}

void CB_SetState(enumCBState eState)
{
	U32 iCurTime = timeSecondsSince2000_ForceRecalc();
	bool bDontFileLog = false;

	//don't log switching into state INIT because the log falls between two builds and is really part of neither
	if (eState == CBSTATE_INIT)
	{
		bDontFileLog = true;
	}


	if (giTimeEnteredState)
	{
		char *pDurationString = NULL;

		timeSecondsDurationToPrettyEString(iCurTime - giTimeEnteredState, &pDurationString);

		if (!bDontFileLog)
		{
			filelog_printf(GetCBLogFileName(NULL), "Time in state %s: <%s> \n",
				StaticDefineIntRevLookup(enumCBStateEnum, geState), pDurationString);
		}

		AddComment(COMMENT_STATE, "Leaving state %s after %s", StaticDefineIntRevLookup(enumCBStateEnum, geState), pDurationString);


		if (!bDontFileLog)
		{
			filelog_printf(GetCBLogFileName(NULL), "RAW Time in state %s: <%d> \n",
				StaticDefineIntRevLookup(enumCBStateEnum, geState), iCurTime - giTimeEnteredState);
		}

		estrDestroy(&pDurationString);
	}

	
	if (!bDontFileLog)
	{
		filelog_printf(GetCBLogFileName(NULL), "Now in state %s\n", StaticDefineIntRevLookup(enumCBStateEnum, eState));
	}



	geState = eState;
	giTimeEnteredState = iCurTime;

	SetSubStateString("");
	SetSubSubStateString("");

	giLastCheckPointTime = iCurTime;

	AddComment(COMMENT_STATE, "New state: %s", StaticDefineIntRevLookup(enumCBStateEnum, geState));

	CBReportToCBMonitor_ReportState();
}

void CB_LogCheckPoint(char *pString)
{
	U32 iCurTime = timeSecondsSince2000_ForceRecalc();
	char *pDurationString = NULL;

	timeSecondsDurationToPrettyEString(iCurTime - giLastCheckPointTime, &pDurationString);


	filelog_printf(GetCBLogFileName(NULL),"%s. Duration: <%s>\n",
		pString, pDurationString);

	giLastCheckPointTime = iCurTime;

	estrDestroy(&pDurationString);
}


void UpdateAutoBuildHistory(enumCBResult eResult, char *pRestartString)
{
	static char *pNewLine = NULL;
	static char *pDuration = NULL;
	static char *pGimmeSVN = NULL;
	char logsLink[1024];

	if (!CBTypeIsCONT())
	{
		return;
	}

	estrPrintf(&pGimmeSVN, "(SVN %u Gimme %u(%s))", 
		gCurRev.iSVNRev, gCurRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gCurRev.iGimmeTime));

	timeSecondsDurationToPrettyEString(timeSecondsSince2000() - giLastTimeStarted, &pDuration);

	sprintf(logsLink, "<a href=\"/logs?buildStartTime=%s&%s\">(Logs)</a> <a href=\"/CBTiming?buildStartSS2000=%u\">(Timing)</a>",  gpLastTimeStartedString, GetProductAndBuildTypeLinkString(), giLastTimeStarted);

	if (pRestartString)
	{
		estrPrintf(&pNewLine, "%sBuild began at %s %s RESTARTED after %s because: %s", logsLink,
			gpLastTimeStartedString, pGimmeSVN, pDuration, pRestartString);
	}
	else
	{
		estrPrintf(&pNewLine, "%sBuild began at %s %s %s in %s.", logsLink, gpLastTimeStartedString, pGimmeSVN, StaticDefineIntRevLookup(enumCBResultEnum, eResult), pDuration);

		switch (eResult)
		{
		case CBRESULT_FAILED:
		case CBRESULT_SUCCEEDED_W_ERRS:
			estrConcatf(&pNewLine, "%s", BuildScriptingGetLinkToLogFile(CBGetRootScriptingContext(), "BreakageEmail.txt", "Breakage Email"));
			break;
		}
	}


	InsertLineAtBeginningOfFileAndTrunc(GetCBAutoHistoryFileName(), pNewLine, 5000);







}

void StartOver_Failed(void)
{
	//if OKAY_TO_PAUSE_BETWEEN_RUNS_ON_FAIL is set, then we do nothing and then if
	//PAUSE_BETWEEN_RUNS_MINUTES is also set, we'll pause next time. If it's not set,
	//then set sbDontPauseNextTime
	char *pOkayToPause = NULL;
	if (!BuildScripting_FindVarValue(CBGetRootScriptingContext(), "$OKAY_TO_PAUSE_BETWEEN_RUNS_ON_FAIL$", &pOkayToPause) 
		|| !atoi(pOkayToPause))
	{
		sbDontPauseNextTime = true;
	}

	estrDestroy(&pOkayToPause);

	CBReportToCBMonitor_BuildEnded(CBRESULT_FAILED);

	gbJustCompletedProcessOrJustStartedUp = true;

	UpdateAutoBuildHistory(CBRESULT_FAILED, NULL);

	// Write out the current state to a log file
	WriteCurrentStateToXML(STACK_SPRINTF("%s\\StateEnd.txt", GetCBLogDirectoryName(NULL)));

	RelocateLogs();

	gDynamicState.eLastResult = gDynamicState.eLastNonAbortResult = CBRESULT_FAILED; 
	eCurResult = CBRESULT_NONE;

	if (!CBTypeIsCONT())
	{
		CB_SetState(CBSTATE_DONE_FAILED); 
		CBTiming_End(true);
		return;
	}

	gDynamicState.lastRev.iSVNRev = gCurRev.iSVNRev; 
	gDynamicState.lastRev.iGimmeTime = gCurRev.iGimmeTime; 
	CB_SetState(CBSTATE_INIT); 
	ParserWriteTextFile(STACK_SPRINTF("%s\\CBDynStatus.txt", GetCBDirectoryName()), parse_CBDynamicState, &gDynamicState, 0, 0); 
	DumpCBSummary();
	CBTiming_End(true);

}


void StartOver_Aborted(void)
{
	CBReportToCBMonitor_BuildEnded(CBRESULT_ABORTED);


	system("taskkill /im sendjabber.exe");


	if (!CBTypeIsCONT())
	{
		gDynamicState.eLastResult = CBRESULT_ABORTED; 
		eCurResult = CBRESULT_NONE;
		CB_SetState(CBSTATE_DONE_ABORTED); 

		//if there are any log files hanging around in logs\currentbuild, move them to logs\aborted_curtime
		CBTiming_End(false);
		RelocateAbortedLogs();

		return;
	}

	// Write out the current state to a log file
	WriteCurrentStateToXML(STACK_SPRINTF("%s\\StateEnd.txt", GetCBLogDirectoryName(NULL)));

	gDynamicState.lastRev.iSVNRev = gCurRev.iSVNRev; 
	gDynamicState.lastRev.iGimmeTime = gCurRev.iGimmeTime; 
	gDynamicState.eLastResult = CBRESULT_ABORTED; 
	eCurResult = CBRESULT_NONE;
	CB_SetState(CBSTATE_INIT); 
	ParserWriteTextFile(STACK_SPRINTF("%s\\CBDynStatus.txt", GetCBDirectoryName()), parse_CBDynamicState, &gDynamicState, 0, 0); 
	DumpCBSummary();
	CBTiming_End(false);

	//if there are any log files hanging around in logs\currentbuild, move them to logs\aborted_curtime
	RelocateAbortedLogs();

}


void StartOver_Succeeded(void)
{ 
	CBReportToCBMonitor_BuildEnded(CBRESULT_SUCCEEDED);

	system("taskkill /im sendjabber.exe");
	
	UpdateAutoBuildHistory(CBRESULT_SUCCEEDED, NULL);

	if (gbIsOnceADayBuild)
	{
		gDynamicState.iLastOnceADayTime = giLastTimeStarted;
	}


	gbJustCompletedProcessOrJustStartedUp = true;
	if (!CBTypeIsCONT())
	{
		gDynamicState.eLastResult = gDynamicState.eLastNonAbortResult = CBRESULT_SUCCEEDED; 
		eCurResult = CBRESULT_NONE;
		CB_SetState(CBSTATE_DONE_SUCCEEDED); 
		CBTiming_End(false);
		return;
	}

	// Write out the current state to a log file
	WriteCurrentStateToXML(STACK_SPRINTF("%s\\StateEnd.txt", GetCBLogDirectoryName(NULL)));

	gDynamicState.lastGoodRev.iSVNRev = gDynamicState.lastRev.iSVNRev = gCurRev.iSVNRev; 
	gDynamicState.lastGoodRev.iGimmeTime = gDynamicState.lastRev.iGimmeTime = gCurRev.iGimmeTime; 
	gDynamicState.eLastResult = gDynamicState.eLastNonAbortResult =  CBRESULT_SUCCEEDED; 
	eCurResult = CBRESULT_NONE;
	CB_SetState(CBSTATE_INIT); 	
	gDynamicState.iLastSuccessfulCycleTime = timeSecondsSince2000_ForceRecalc() - giLastTimeStarted;
	ParserWriteTextFile(STACK_SPRINTF("%s\\CBDynStatus.txt", GetCBDirectoryName()), parse_CBDynamicState, &gDynamicState, 0, 0); 
	DumpCBSummary();
	CBTiming_End(false);



}

void StartOver_SucceededWithErrors(void)
{
	CBReportToCBMonitor_BuildEnded(CBRESULT_SUCCEEDED_W_ERRS);


	system("taskkill /im sendjabber.exe");
	UpdateAutoBuildHistory(CBRESULT_SUCCEEDED_W_ERRS, NULL);

	if (gbIsOnceADayBuild)
	{
		gDynamicState.iLastOnceADayTime = giLastTimeStarted;
	}


	gbJustCompletedProcessOrJustStartedUp = true;
	if (!CBTypeIsCONT())
	{
		gDynamicState.eLastResult = gDynamicState.eLastNonAbortResult =  CBRESULT_SUCCEEDED_W_ERRS; 
		eCurResult = CBRESULT_NONE;
		CB_SetState(CBSTATE_DONE_SUCCEEDED_W_ERRORS); 
		CBTiming_End(false);
		return;
	}

	// Write out the current state to a log file
	WriteCurrentStateToXML(STACK_SPRINTF("%s\\StateEnd.txt", GetCBLogDirectoryName(NULL)));

	gDynamicState.lastRev.iSVNRev = gCurRev.iSVNRev; 
	gDynamicState.lastRev.iGimmeTime = gCurRev.iGimmeTime; 
	gDynamicState.eLastResult = gDynamicState.eLastNonAbortResult = CBRESULT_SUCCEEDED_W_ERRS; 
	eCurResult = CBRESULT_NONE;
	CB_SetState(CBSTATE_INIT); 	
	gDynamicState.iLastSuccessfulCycleTime = timeSecondsSince2000_ForceRecalc() - giLastTimeStarted;
	ParserWriteTextFile(STACK_SPRINTF("%s\\CBDynStatus.txt", GetCBDirectoryName()), parse_CBDynamicState, &gDynamicState, 0, 0); 
	DumpCBSummary();
	CBTiming_End(false);
}

void DumpCBSummary(void)
{
	CBRunSummary *pSummary = StructCreate(parse_CBRunSummary);
	ErrorTrackerEntryList *pCurErrorList;
	char fileName[CRYPTIC_MAX_PATH];
	char *pTempStr = NULL;
	CBRunSummaryList *pSummaryList;

	pSummary->pType = gpCBType;
	pSummary->pProduct = gpCBProduct;
	pSummary->pConfig = &gConfig;

	pSummary->iBuildStartTime = giLastTimeStarted;
	pSummary->iBuildFinishTime = timeSecondsSince2000_ForceRecalc();
	pSummary->eResult = gDynamicState.eLastResult;
	StructCopy(parse_CBRevision, &gDynamicState.lastRev, &pSummary->revision, 0, 0, 0);

	pCurErrorList = CB_GetCurrentEntryList();

	if (objCountTotalContainersWithType(pCurErrorList->eContainerType))
	{
		ContainerIterator iter = {0};
		Container *currCon = NULL;

		objInitContainerIteratorFromType(pCurErrorList->eContainerType, &iter);
		currCon = objGetNextContainerFromIterator(&iter);
		while (currCon)
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
			eaPush(&pSummary->ppErrors, (ErrorEntry*)pEntry);
			currCon = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);
	}

	pSummary->pScriptingVariables = StructCreate(parse_NameValuePairList);
	BuildScripting_PutVariablesIntoList(CBGetRootScriptingContext(), pSummary->pScriptingVariables);

	estrPrintf(&pTempStr, "%s", timeGetLocalDateStringFromSecondsSince2000(pSummary->iBuildStartTime));
	estrMakeAllAlphaNumAndUnderscores(&pTempStr);
	

	sprintf(fileName, "c:\\continuousbuilder\\summaries\\%s.txt", pTempStr);
	estrDestroy(&pTempStr);

	pSummaryList = StructCreate(parse_CBRunSummaryList);
	eaPush(&pSummaryList->ppSummaries, pSummary);

	ParserWriteTextFile(fileName, parse_CBRunSummaryList, pSummaryList, 0, 0);

	eaDestroy(&pSummary->ppErrors);

	pSummary->pType = NULL;
	pSummary->pProduct = NULL;
	pSummary->pConfig = NULL;

	StructDestroy(parse_CBRunSummaryList, pSummaryList);
}


void CB_DumpEntryToString(char **estr, ErrorEntry *pEntry, GlobalType eErrorType)
{
	char hostName[256];
	gethostname(hostName, 255);

	if (pEntry->pUserData)
	{
		if (pEntry->pUserData->iEmailNumberWhenItFirstHappened != gDynamicState.iNextEmailNumber)
		{
			U32 iSecsAgo = timeSecondsSince2000_ForceRecalc() - pEntry->pUserData->iTimeWhenItFirstHappened;

			estrConcatf(estr, "First occurrence, ID %d (%d hours %d minutes ago)", 
				pEntry->pUserData->iEmailNumberWhenItFirstHappened, iSecsAgo / 3600, iSecsAgo / 60 % 60);
		}

		if (pEntry->pUserData->pDumpFileName)
		{
			estrConcatf(estr, "\nDump file: %s", pEntry->pUserData->pDumpFileName);
		}

		if (pEntry->pUserData->pMemoryDumpFileName)
		{
			estrConcatf(estr, "\nMemory dump file: %s", pEntry->pUserData->pMemoryDumpFileName);
		}

		if (pEntry->pUserData->pStepsWhereItHappened)
		{
			estrConcatf(estr, "\nEncountered during: %s", pEntry->pUserData->pStepsWhereItHappened);
		}
	}
	estrConcatf(estr, "\nhttp://%s/CBDetail?CBId=%d&Context=%d\n", hostName, pEntry->uID, eErrorType);
	if (pEntry->pUserDataStr)
	{
		estrConcatf(estr, "%s\n", pEntry->pUserDataStr);
	}
	ET_DumpEntryToString(estr, pEntry, DUMPENTRY_FLAG_NO_USERSCOUNT | DUMPENTRY_FLAG_NO_DAYS_AGO | DUMPENTRY_FLAG_ALLOW_NEWLINES_IN_ERRORSTRING |
		DUMPENTRY_FLAG_NO_USERS | DUMPENTRY_FLAG_NO_DUMPINFO | DUMPENTRY_FLAG_NO_HTTP | DUMPENTRY_FLAG_NO_DUMP_TOGGLES | DUMPENTRY_FLAG_NO_JIRA, true);
	
}

void CB_NewErrorCallback(ErrorEntry *pNewEntry, ErrorEntry *pMergedEntry)
{
	char *pCurStepString = NULL;
	bool bNonFatal = false;

	estrConcatf(&pCurStepString, "%s ", StaticDefineIntRevLookup(enumCBStateEnum, geState));

	//if we get an assert or a crash, then fail in 20 minutes (the 20 minute delay is so that
	//if the script is going to fail itself, we let it finish all its steps. In particular, crypticError takes
	//a long time to get a dump for gameservers)
	if (pNewEntry->eType == ERRORDATATYPE_ASSERT || pNewEntry->eType == ERRORDATATYPE_CRASH || pNewEntry->eType == ERRORDATATYPE_FATALERROR)
	{
		
		char *pNonFatalAssertExeNames = GetConfigVar("NON_FATAL_ASSERT_EXES");
		if (pNonFatalAssertExeNames && eaSize(&pNewEntry->ppExecutableNames))
		{
			char **ppNames = NULL;
			DivideString(pNonFatalAssertExeNames, " ,;", &ppNames, DIVIDESTRING_STANDARD);

			FOR_EACH_IN_EARRAY(ppNames, char, pName)
			{
				if (strstri(pNewEntry->ppExecutableNames[0], pName))
				{
					bNonFatal = true;
				}
			}
			FOR_EACH_END;

			eaDestroyEx(&ppNames, NULL);
		}

		if (bNonFatal)
		{
				filelog_printf(GetCBLogFileName(NULL), "Got an assert, crash or fatal error from %s, but it is non-fatal",
					pNewEntry->ppExecutableNames[0]);
		}
		else 
		{
			if (gbLeaveCrashesUpForever)
				BuildScripting_TotallyDisable(CBGetRootScriptingContext(), true);
			else
				BuildScripting_FailAfterDelay(CBGetRootScriptingContext(), 1200);
		}
	}

	if (GetSubStateString())
		estrConcatf(&pCurStepString, "(%s)", GetSubStateString());

	//the current step string might be ridiculously long, as the substate can include super-long variables during a setvar
	if (estrLength(&pCurStepString) > 200)
	{
		estrSetSize(&pCurStepString, 200);
		estrConcatf(&pCurStepString, "...)");
	}

	if (!pMergedEntry->pUserData)
	{
		pMergedEntry->pUserData = StructCreate(parse_ErrorTrackerEntryUserData);
		pMergedEntry->pUserData->iEmailNumberWhenItFirstHappened = gDynamicState.iNextEmailNumber;
		pMergedEntry->pUserData->iTimeWhenItFirstHappened = timeSecondsSince2000_ForceRecalc();
	}

	if (bNonFatal)
	{
		pMergedEntry->pUserData->bNonFatal = true;
	}

	if (pMergedEntry->pUserData->pStepsWhereItHappened && strstr(pMergedEntry->pUserData->pStepsWhereItHappened, pCurStepString))
	{
		estrDestroy(&pCurStepString);
		return;
	}
	else
	{
		estrConcatf(&pMergedEntry->pUserData->pStepsWhereItHappened, "%s", pCurStepString);
	}
	estrDestroy(&pCurStepString);
}


int SortCheckinsByDate(const CheckinInfo **pInfo1, const CheckinInfo **pInfo2)
{
	if ((*pInfo1)->iCheckinTimeSS2000 > (*pInfo2)->iCheckinTimeSS2000)
	{
		return -1;
	}
	else if ((*pInfo1)->iCheckinTimeSS2000 < (*pInfo2)->iCheckinTimeSS2000)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void SendCheckinRequestedEmails(char *pAddressString, CheckinInfo *pCheckin)
{
	char **ppWhoToSendTo = NULL;
	char *jirastr = NULL;
	FILE *pFile;
	int i;

	if (!CBTypeIsCONT())
	{
		filelog_printf(GetCBLogFileName(NULL), "Not sending email... CB is not cont");
		return;
	}

	filelog_printf(GetCBLogFileName(NULL), "Address string: %s", pAddressString);


	ExtractAlphaNumTokensFromString(pAddressString, &ppWhoToSendTo);

	for (i=0; i < eaSize(&ppWhoToSendTo); i++)
	{
		if (stricmp(ppWhoToSendTo[i], "me") == 0)
		{
			free(ppWhoToSendTo[i]);
			ppWhoToSendTo[i] = strdup(pCheckin->userName);
		}
	}
	
	pFile = fopen(STACK_SPRINTF("%s\\CBRequestedEmail.txt", GetCBLogDirectoryName(NULL)), "wt");

	fprintf(pFile, "%s wanted the %s continuous builder (svn %s) (gimme %s) to inform you that the following code checkin (%u) has now been tested, compiled, and checked in:\n\n%s\n",
		pCheckin->userName, GetCBName(), 
		CBGetPresumedSVNBranch(), CBGetPresumedGimmeProjectAndBranch(),
		pCheckin->iRevNum, pCheckin->checkinComment);

	fclose(pFile);

	estrStackCreate(&jirastr);
	if (pCheckin->checkinComment && strlen(pCheckin->checkinComment) && pCheckin->checkinComment[0] == '[')
	{	//This just looks for brackets. If you want to use a regex, feel free to do it yourself.
		char *end = NULL;
		estrPrintf(&jirastr, "%s", pCheckin->checkinComment);
		if ((end = strchr(jirastr, ']')) && (end - jirastr < 21))
		{
			estrSetSize(&jirastr, (1 + end - jirastr));
		}
		else
		{
			estrClear(&jirastr);
		}
	}
	else
	{
		estrPrintf(&jirastr, "");
	}

	SendEmailToList(EMAILFLAG_NO_DEFAULT_RECIPIENTS, &ppWhoToSendTo, STACK_SPRINTF("%s\\CBRequestedEmail.txt", GetCBLogDirectoryName(NULL)), STACK_SPRINTF("!!%s's change %s(%u) is in!!", pCheckin->userName, jirastr, pCheckin->iRevNum));

	estrDestroy(&jirastr);

	eaDestroyEx(&ppWhoToSendTo, NULL);
}

void SendEmailToAdministrators(char *pFailureString)
{
	static char *pLastString = NULL;


	FILE *pFile;
	char hostName[256];
	static U32 iLastTime = 0;
	char *pEmailFileName = NULL;

	//increment email number when sending an alert email in case there's something wonky going 
	//on with the compile so that we don't leave a wonky old error in the compile log and the
	//keep re-hitting it.
	gDynamicState.iNextEmailNumber++;

	if (pLastString && strcmp(pLastString, pFailureString) == 0 && timeSecondsSince2000_ForceRecalc() - iLastTime < 60 * 5)
	{
		return;
	}

	estrCopy2(&pLastString, pFailureString);
	iLastTime = timeSecondsSince2000_ForceRecalc();

	gethostname(hostName, 255);

	estrPrintf(&pEmailFileName, "%s\\CBAdminEmail.txt", GetCBLogDirectoryName(NULL));
	pFile = fopen(pEmailFileName, "wt");

	if (!pFile)
	{
		estrDestroy(&pEmailFileName);
		return;
	}

	fprintf(pFile, "This email was sent to you by the %s Continuous Builder, located at http://%s\n", GetCBName(), hostName);
	fprintf(pFile, "It thinks you are one of its administrators. Isn't that touching? Don't you feel special?\n");
	fprintf(pFile, "It has the following error message for you:\n\n");
	fprintf(pFile, "%s\n", pFailureString);
	fclose(pFile);


	SendEmailToList(EMAILFLAG_NO_DEFAULT_RECIPIENTS | EMAILFLAG_HIGH_PRIORITY, eaSize(&gConfig.ppAdministrator) ? &gConfig.ppAdministrator : &gConfig.ppDefaultEmailRecipient, pEmailFileName, "Continuous Builder Needs Attention");


	estrDestroy(&pEmailFileName);

}	

void RestartWithMessage(char *pMessage)
{
	CB_SetState(CBSTATE_INIT);
	SendEmailToAdministrators(pMessage);
	UpdateAutoBuildHistory(CBRESULT_NONE, pMessage);
}

void CheckinFailed(char *pFailureString)
{	
	char *pEmailFileName = NULL;
	FILE *pFile = NULL;
	char hostName[256];
	estrPrintf(&pEmailFileName, "%s\\CBCheckinFailedEmail.txt", GetCBLogDirectoryName(NULL));
	pFile = fopen(pEmailFileName, "wt");

	assertmsgf(pFile, "Couldn't create file %s... is the disk full or something?", pEmailFileName);

	gethostname(hostName, 255);

	fprintf(pFile, "This email was sent to you by the %s Continuous Builder, located at http://%s\n\n", GetCBName(), hostName);
	fprintf(pFile, "\nA checkin process failed with error message <<%s>>\n", pFailureString);
	fclose(pFile);

	SendEmailToList(EMAILFLAG_NO_DEFAULT_RECIPIENTS | EMAILFLAG_HIGH_PRIORITY, &gConfig.ppAdministrator, pEmailFileName, "Continuous Build Checkin Failed");

	estrDestroy(&pEmailFileName);

}

void PossiblyPauseBetweenSteps(void)
{
	while (gbPauseBetweenSteps)
	{
		gbCurrentlyPausedBetweenSteps = true;
		LeaveCriticalSection(&gCBCriticalSection);
		BuildScripting_Tick(CBGetRootScriptingContext());
		SLEEP;
		EnterCriticalSection(&gCBCriticalSection);
	}

	gbCurrentlyPausedBetweenSteps = false;
}

void DoBadCheckinStuff(void)
{
	if (!CheckConfigVar("DONT_CHECKIN") && CBTypeIsCONT())
	{
		U32 iCurTime = timeSecondsSince2000_ForceRecalc();


		filelog_printf(GetCBLogFileName(NULL), "Build failed... doing badBuildCheckin commands\n");
	
		CBTiming_Step("Bad Checkin script", 0);
		StartBuildScripting(gConfig.continuousConfig.pBadCheckinScript, 1);
			

		while (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_RUNNING)
		{
			LeaveCriticalSection(&gCBCriticalSection);
			BuildScripting_Tick(CBGetRootScriptingContext());
			SLEEP;
			EnterCriticalSection(&gCBCriticalSection);

			if (!BuildScripting_CurrentCommandSetsCBStringsByItself(CBGetRootScriptingContext()))
			{
				SetSubStateString(BuildScripting_GetCurStateString(CBGetRootScriptingContext()));
			}
		}

		if (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_SUCCEEDED)
		{
			filelog_printf(GetCBLogFileName(NULL), "Finished checking in\n");
		}
		else
		{
			filelog_printf(GetCBLogFileName(NULL), "Bad checkin failed\n");
			CheckinFailed(BuildScripting_GetCurFailureString(CBGetRootScriptingContext()));
		}
	}
}

bool DoesUserAlwaysWantToBeNotifiedOfHisCheckins(char *pUserName)
{
	int i;

	for (i=0; i < eaSize(&gConfig.continuousConfig.ppPeopleWhoAlwaysWantCheckinNotification); i++)
	{
		if (stricmp(pUserName, gConfig.continuousConfig.ppPeopleWhoAlwaysWantCheckinNotification[i]) == 0)
		{
			return true;
		}
	}

	return false;
}

void DoCheckinStuff(bool bThereWereErrors)
{
	int i;
	U32 iCurTime = timeSecondsSince2000_ForceRecalc();

	CheckinInfo **ppSVNCheckinsSinceLastCheckin = NULL;
	CheckinInfo **ppGimmeCheckinsSinceLastCheckin = NULL;

	if (!CBTypeIsCONT())
	{
		return;
	}

	if (!gDynamicState.lastCheckedInRev.iSVNRev)
	{
		filelog_printf(GetCBLogFileName(NULL), "During checkin, had no lastCheckedInRev, so not getting checkins this time");
	}
	else
	{
		if (!SVN_GetCheckins(gDynamicState.lastCheckedInRev.iSVNRev, gCurRev.iSVNRev, NULL, gConfig.ppSVNFolders, NULL, &ppSVNCheckinsSinceLastCheckin, 60, 0))
		{
			filelog_printf(GetCBLogFileName(NULL), "During checkin, couldn't get SVN checkins.");
		}

		if (!Gimme_GetCheckinsBetweenTimes(gDynamicState.lastCheckedInRev.iGimmeTime, gCurRev.iGimmeTime, NULL, gConfig.ppGimmeFolders, GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppGimmeCheckinsSinceLastCheckin, 600))
		{
			filelog_printf(GetCBLogFileName(NULL), "During checkin, couldn't get gimme checkins.");
		}

		filelog_printf(GetCBLogFileName(NULL), "During checkin, had %d gimme and %d svn checkins since last time.",
			eaSize(&ppSVNCheckinsSinceLastCheckin), eaSize(&ppGimmeCheckinsSinceLastCheckin));
	}

	
	if (bThereWereErrors)
	{
		filelog_printf(GetCBLogFileName(NULL), "Build succeeded with errors... Checking in\n");
	}
	else
	{
		filelog_printf(GetCBLogFileName(NULL), "Build succeeded... Checking in\n");
	}


	if (!CheckConfigVar("DONT_CHECKIN") && gConfig.continuousConfig.pCheckinScript && gConfig.continuousConfig.pCheckinScript[0])
	{


		CBTiming_Step("Checkin script", 0);
		StartBuildScripting(gConfig.continuousConfig.pCheckinScript, 1);
			

		while (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_RUNNING)
		{
			LeaveCriticalSection(&gCBCriticalSection);
			BuildScripting_Tick(CBGetRootScriptingContext());
			SLEEP;
			EnterCriticalSection(&gCBCriticalSection);

			if (!BuildScripting_CurrentCommandSetsCBStringsByItself(CBGetRootScriptingContext()))
			{
				SetSubStateString(BuildScripting_GetCurStateString(CBGetRootScriptingContext()));
			}
		}

		if (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_SUCCEEDED)
		{
			filelog_printf(GetCBLogFileName(NULL), "Finished checking in\n");
		}
		else
		{
			filelog_printf(GetCBLogFileName(NULL), "Checkin failed\n");
			CheckinFailed(BuildScripting_GetCurFailureString(CBGetRootScriptingContext()));
		}
	

		CBTiming_Step("Auto checkin emails", 0);
		filelog_printf(GetCBLogFileName(NULL), "Maybe about to check for magic email strings");
		if (gDynamicState.lastCheckedInRev.iSVNRev && gConfig.continuousConfig.pCheckinScript && gConfig.continuousConfig.pCheckinScript[0]
			&& gConfig.continuousConfig.pMagicSVNCheckinEmailString && gConfig.continuousConfig.pMagicSVNCheckinEmailString[0])
			{
			for (i=0; i < eaSize(&ppSVNCheckinsSinceLastCheckin); i++)
			{
				char *pCBEmailString;

				filelog_printf(GetCBLogFileName(NULL), "Looking at SVN checkin %d (%s)", ppSVNCheckinsSinceLastCheckin[i]->iRevNum,
					ppSVNCheckinsSinceLastCheckin[i]->userName);


				if (DoesUserAlwaysWantToBeNotifiedOfHisCheckins(ppSVNCheckinsSinceLastCheckin[i]->userName))
				{
					filelog_printf(GetCBLogFileName(NULL), "User always wants to be notified of checkins... sending email");
					SendCheckinRequestedEmails(ppSVNCheckinsSinceLastCheckin[i]->userName, ppSVNCheckinsSinceLastCheckin[i]);
				}


				if ((pCBEmailString = strstri(ppSVNCheckinsSinceLastCheckin[i]->checkinComment, CB_EMAIL_ALL_STRING)))
				{
					char *pFirstNewLine = strchr(pCBEmailString, '\n');
					char *pTemp = pCBEmailString;

					filelog_printf(GetCBLogFileName(NULL), "found %s... sending emails", CB_EMAIL_ALL_STRING);


					if (pFirstNewLine)
					{
						*pFirstNewLine = 0;
					}
					SendCheckinRequestedEmails(pCBEmailString + strlen(CB_EMAIL_ALL_STRING), ppSVNCheckinsSinceLastCheckin[i]);
					if (pFirstNewLine)
					{
						*pFirstNewLine = '\n';
					}
				}

				if ((pCBEmailString = strstri(ppSVNCheckinsSinceLastCheckin[i]->checkinComment, gConfig.continuousConfig.pMagicSVNCheckinEmailString)))
				{
					char *pFirstNewLine = strchr(pCBEmailString, '\n');
					char *pTemp = pCBEmailString;

					filelog_printf(GetCBLogFileName(NULL), "found %s... sending emails", gConfig.continuousConfig.pMagicSVNCheckinEmailString);

					if (pFirstNewLine)
					{
						*pFirstNewLine = 0;
					}
					SendCheckinRequestedEmails(pCBEmailString + strlen(gConfig.continuousConfig.pMagicSVNCheckinEmailString), ppSVNCheckinsSinceLastCheckin[i]);
					if (pFirstNewLine)
					{
						*pFirstNewLine = '\n';
					}
				}
			}
		}
		else
		{
			filelog_printf(GetCBLogFileName(NULL), "Decided not to check for magic email strings");
		}

	}		

	filelog_printf(GetCBLogFileName(NULL), "Finished checking in\n");

	gDynamicState.lastCheckedInRev.iGimmeTime = gCurRev.iGimmeTime;
	gDynamicState.lastCheckedInRev.iSVNRev = gCurRev.iSVNRev;

	eaDestroyStruct(&ppSVNCheckinsSinceLastCheckin, parse_CheckinInfo);
	eaDestroyStruct(&ppGimmeCheckinsSinceLastCheckin, parse_CheckinInfo);

	filelog_printf(GetCBLogFileName(NULL), "About to decide whether to write goodrevs file\n");

	if (gConfig.continuousConfig.bWriteMagicProdBuildFile && !gConfig.bDev)
	{
		char *pFileName1 = NULL;
		char *pFileName2 = NULL;
		
		
		static int iGimmeBranch = -1;

		filelog_printf(GetCBLogFileName(NULL), "Going to write goodrevs file\n");



//we want to use the non-core gimme folder if possible, core as a fallback
	//(this is because the getall.cb prod build script uses $GIMMEBRANCH$ in the filename, not $COREGIMMEBRANCH$)
		if (iGimmeBranch == -1)
		{
			int iFolderNumToCheck = 0;

			assert(gConfig.ppGimmeFolders);

			while (iFolderNumToCheck < eaSize(&gConfig.ppGimmeFolders) && strstri(gConfig.ppGimmeFolders[iFolderNumToCheck], "core"))
			{
				iFolderNumToCheck++;
			}

			if (iFolderNumToCheck == eaSize(&gConfig.ppGimmeFolders))
			{
				iFolderNumToCheck = 0;
			}


			iGimmeBranch = Gimme_GetBranchNum(gConfig.ppGimmeFolders[iFolderNumToCheck]);

		}

		filelog_printf(GetCBLogFileName(NULL), "Calculated gimme branch: %d\n", iGimmeBranch);


		if (iGimmeBranch != -1)
		{
			static char *pFileContents = NULL;
			FILE *pFile;
			char hostName[256];
					
			gethostname(hostName, 255);


			estrPrintf(&pFileName1, "GoodRevs_%s_%s_%d", 
				gpCBProduct->pProductName, gpSVNRepository, iGimmeBranch);

			estrMakeAllAlphaNumAndUnderscores(&pFileName1);

			estrPrintf(&pFileName2, "n:\\continuousbuilder\\%s.txt", pFileName1);

			estrPrintf(&pFileContents, "GIMMETIME = %s\nSVNREVNUM = %d\nREVS_ORIGIN = AutoGenerated by ContinuousBuilder %s (%s)",
					timeGetLocalGimmeStringFromSecondsSince2000(gCurRev.iGimmeTime),
					gCurRev.iSVNRev, GetCBName(), hostName);


			filelog_printf(GetCBLogFileName(NULL), "Filename to write: %s. Contents: %s", pFileName2, pFileContents);


			pFile = fopen_with_retries(pFileName2, "wt", 10, 1000);


			if (pFile)
			{

				fprintf(pFile, "%s", pFileContents);


				fclose(pFile);
				filelog_printf(GetCBLogFileName(NULL), "Written");
			}
			else
			{
				filelog_printf(GetCBLogFileName(NULL), "fopen failed");
				TriggerAlertf("CANT_WRITE_GOODREVS", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, "Couldn't write good revs info into %s. Is the N drive down? File should contain:\n%s\n", pFileName2, pFileContents);
			}
		}

		estrDestroy(&pFileName1);
		estrDestroy(&pFileName2);

	}
	else
	{
		filelog_printf(GetCBLogFileName(NULL), "Decided not to write goodrevs");
	}
}

void SendJabberToList_VeryInternal(char ***pppList, char *pMessage)
{
	char *pSystemString = NULL;
	int i;
	
	estrStackCreate(&pSystemString);

	for (i=0; i < eaSize(pppList); i++)
	{
		estrPrintf(&pSystemString, "sendJabber crypticam@sol crypticam ");

		estrConcatf(&pSystemString, "%s", Gimme_GetEmailNameFromGimmeName((*pppList)[i]));
			
		estrConcatf(&pSystemString, "@sol \"%s\"", pMessage);

		system_detach(pSystemString, true, true);
	}

	estrDestroy(&pSystemString);
}

void SendJabberToList_Internal(char ***pppList, char *pMessage, bool bNoDefaultRecipients)
{
	char *pSystemString = NULL;
	int i;

	if (!eaSize(pppList) && bNoDefaultRecipients)
	{
		return;
	}

	//never jabber more than 10 people at once, it's probably a mistake
	if (eaSize(pppList) > sMaxToJabberAtOnce)
	{
		filelog_printf(GetCBLogFileName(NULL), "Would send jabbers to more than %d people at once, probably a mistake, not sending", 
			sMaxToJabberAtOnce);
		return;
	}

	if (gbEmailDefaultRecipientsOnly)
	{
		eaDestroy(pppList);
	}

	if (!bNoDefaultRecipients)
	{
		for (i=0; i < eaSize(&gConfig.ppDefaultEmailRecipient); i++)
		{
			eaPushUnique(pppList, (char*)allocAddCaseSensitiveString(gConfig.ppDefaultEmailRecipient[i]));
		}
	}


	if (CheckConfigVar("ONLY_SEND_TO"))
	{
		char **ppOnlySendToNames = NULL;

		DivideString(GetConfigVar("ONLY_SEND_TO"), ",", &ppOnlySendToNames, 
			DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		SendJabberToList_VeryInternal(&ppOnlySendToNames, pMessage);

		eaDestroyEx(&ppOnlySendToNames, NULL);


	}
	else
	{
		SendJabberToList_VeryInternal(pppList, pMessage);

		
	}

	estrDestroy(&pSystemString);
}

void SendEmailToList_Internal(enumEmailFlags eFlags, char ***pppList, char *pFileName, char *pSubjectLine)
{
	SendEmailToList_WithAttachment_Internal(eFlags, pppList, pFileName, pSubjectLine, NULL, NULL, NULL);
}

AUTO_STRUCT;
typedef struct SendEmailUserData
{
	char *pSubjectLine;
	char *pLogFileForErrors;
} SendEmailUserData;



void ContinuousBuilderSendEmailResultCB(SendEmailUserData *pUserData, char *pErrorString)
{
	static U32 siLastAlertTime = 0;
	if (pErrorString)
	{
		filelog_printf(pUserData->pLogFileForErrors, "Tried to send email entitled %s, failed due to: %s",
			pUserData->pSubjectLine, pErrorString);
	}

	StructDestroy(parse_SendEmailUserData, pUserData);
}

void SendEmailToList_WithAttachment_Internal(enumEmailFlags eFlags, char ***pppList, char *pFileName, char *pSubjectLine, char* pAttachmentFile, char* pAttachmentName, char* pMimeType)
{
	char *pWhoToSendToString = NULL;
	char *pBodyAlloced = fileAlloc(pFileName, NULL);
	SMTPMessageRequest *pReq;
	char *pResultEStr = NULL;
	SendEmailUserData *pUserData;
	
	int i;


	Gimme_LoadEmailAliases();


	if (!eaSize(pppList) && (eFlags & EMAILFLAG_NO_DEFAULT_RECIPIENTS))
	{
		return;
	}

	if (gbEmailDefaultRecipientsOnly)
	{
		eaDestroy(pppList);
	}

	if (!(eFlags & EMAILFLAG_NO_DEFAULT_RECIPIENTS))
	{
		for (i=0; i < eaSize(&gConfig.ppDefaultEmailRecipient); i++)
		{
			eaPushUnique(pppList, (char*)allocAddCaseSensitiveString(gConfig.ppDefaultEmailRecipient[i]));
		}
	}

	pReq = StructCreate(parse_SMTPMessageRequest);
	estrPrintf(&pReq->body, "%s", pBodyAlloced ? pBodyAlloced : "(No body)");

	if (CheckConfigVar("ONLY_SEND_TO"))
	{
		DivideString(GetConfigVar("ONLY_SEND_TO"), ",", &pReq->to, 
			DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_ESTRINGS);
	}
	else
	{
		if (eaSize(pppList) > sMaxToEmailAtOnce)
		{
			filelog_printf(GetCBLogFileName(NULL), "Would be sending email to more than %d people at once, probably a mistake, just sending to default recipients", 
				sMaxToEmailAtOnce);

			estrInsertf(&pReq->body, 0, "NOTE NOTE NOTE NOTE:\n\n\n\nThis email would have been sent to more than %d recipients at once, which is spammy and probably a mistake.\n\nSo it's being send only to default recipients\n\n",
				sMaxToEmailAtOnce);

			for (i=0; i < eaSize(&gConfig.ppDefaultEmailRecipient); i++)
			{
				eaPush(&pReq->to, estrDup(gConfig.ppDefaultEmailRecipient[i]));
			}
		}
		else
		{
			for (i=0; i < eaSize(pppList); i++)
			{
				eaPush(&pReq->to, estrDup((*pppList)[i]));
			}
		}
	}

	if (eFlags & EMAILFLAG_HTML)
	{
		pReq->html = 1;
	}

	
	estrPrintf(&pReq->from, "%s", getHostName());
	estrConcatf(&pReq->from, "@"ORGANIZATION_DOMAIN);

	estrPrintf(&pReq->subject, "%s", pSubjectLine);

	if((pAttachmentFile != NULL) && (pAttachmentName != NULL))
	{
		estrPrintf(&pReq->attachfilename, "%s", pAttachmentFile);
		estrPrintf(&pReq->attachsuggestedname, "%s", pAttachmentName);
		estrPrintf(&pReq->attachmimetype, "%s", pMimeType);
	}

	{
		char *pLogString = NULL;
		estrPrintf(&pLogString, "About to attempt to send email in file %s entitled %s to: ", pFileName, pSubjectLine);
		for (i=0; i < eaSize(&pReq->to); i++)
		{
			estrConcatf(&pLogString, "%s%s", i == 0 ? "" : ", ", pReq->to[i]);
		}
		filelog_printf(GetCBLogFileName(NULL), "%s", pLogString);
		estrDestroy(&pLogString);
	}

	if (CheckConfigVarExistsAndTrue("HIGH_PRIORITY_EMAILS") || (eFlags & EMAILFLAG_HIGH_PRIORITY))
	{
		pReq->priority = 1;
	}

	pReq->timeout = 60;

	pUserData = StructCreate(parse_SendEmailUserData);
	pUserData->pLogFileForErrors = strdup(GetCBLogFileName(NULL));
	pUserData->pSubjectLine = strdup(pReq->subject);

	pReq->pResultCBFunc = ContinuousBuilderSendEmailResultCB;
	

	if (eFlags & EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES)
	{
		//store off these two pointers and then restore
		char **ppTo = pReq->to;
		char *pBody = pReq->body;

		pReq->to = NULL;
		pReq->body = NULL;
		

		FOR_EACH_IN_EARRAY(ppTo, char, pTo)
		{
			char *pWrapped = NULL;
			estrCopy2(&pReq->body, pBody);
			estrPrintf(&pWrapped, "<b style=\"font-size=1.2em; background-color: yellow\">%s</b>", pTo);
			estrReplaceOccurrences(&pReq->body, pTo, pWrapped);

			eaPush(&pReq->to, estrDup(pTo));

			pReq->pUserData = StructClone(parse_SendEmailUserData, pUserData);
			smtpMsgRequestSend_BgThread(pReq);

			eaDestroyEString(&pReq->to);
			estrDestroy(&pReq->body);
			estrDestroy(&pWrapped);
		}
		FOR_EACH_END;

		pReq->to = ppTo;
		pReq->body = pBody;
		StructDestroy(parse_SendEmailUserData, pUserData);


	}
	else
	{
		pReq->pUserData = pUserData;
		smtpMsgRequestSend_BgThread(pReq);
	}
	

	StructDestroy(parse_SMTPMessageRequest, pReq);
	if (pBodyAlloced)
	{
		free(pBodyAlloced);
	}
	estrDestroy(&pResultEStr);
}



void SendAllClearEmail(void)
{

	char **ppWhoToSendTo = NULL;
	FILE *pEmailFile;
	char hostName[255];

	if (!CBTypeIsCONT())
	{
		return;
	}

	pEmailFile = fopen(STACK_SPRINTF("%s\\CBAllClearEmail.txt", GetCBLogDirectoryName(NULL)), "wt");

	assert(pEmailFile);

	gethostname(hostName, 255);

	fprintf(pEmailFile, "The %s continuous build system on %s wants you to know that everything\nis now A-OK. You are being informed of this because\nyou were previously suspected of being involved in a bad checkin\n",
		GetCBName(), hostName);
	
	fclose(pEmailFile);

	PutSuspectsIntoSimpleList(&gDynamicState.curSuspects, &ppWhoToSendTo);

	SendEmailToList(0, &ppWhoToSendTo, STACK_SPRINTF("%s\\CBAllClearEmail.txt", GetCBLogDirectoryName(NULL)), STACK_SPRINTF("!!%s Continuous Build on %s - All Clear (fixed breakage ID %d)!!", GetCBName(), hostName, gDynamicState.iNextEmailNumber - 1));


	eaDestroy(&ppWhoToSendTo);
	ClearSuspectList(&gDynamicState.curSuspects);



}

bool ErrorListIsErrorsOnly(ErrorTrackerEntryList *pList)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(pList->eContainerType, &iter);
	
	while ((currCon = objGetNextContainerFromIterator(&iter)))
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		if(pEntry->eType != ERRORDATATYPE_ERROR)
		{
			if (pEntry->pUserData && pEntry->pUserData->bNonFatal)
			{
				continue;
			}

			objClearContainerIterator(&iter);
			return false;
		}
	}
	objClearContainerIterator(&iter);
	return true;
}



ErrorEntry *FindErrorInList(ErrorEntry *pEntry, ErrorTrackerEntryList *pList)
{
	ContainerIterator iter = {0};
	ErrorEntry *pConEntry;

	if (!pList)
		return NULL;

	objInitContainerIteratorFromType(pList->eContainerType, &iter);
	while (pConEntry = objGetNextObjectFromIterator(&iter))
	{
		if(hashMatches(pEntry, pConEntry))
		{
			objClearContainerIterator(&iter);
			return pConEntry;
		}
	}
	objClearContainerIterator(&iter);
	return NULL;
}

bool ErrorListsAreDifferent(ErrorTrackerEntryList *pList1, ErrorTrackerEntryList *pList2)
{
	int iSize1, iSize2;
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	iSize1 = pList1 ? objCountTotalContainersWithType(pList1->eContainerType) : 0;
	iSize2 = pList2 ? objCountTotalContainersWithType(pList2->eContainerType) : 0;

	if (iSize1 != iSize2)
	{
		return true;
	}

	if (iSize1 == 0)
	{
		return false;
	}

	if (pList1)
	{
		objInitContainerIteratorFromType(pList1->eContainerType, &iter);
		currCon = objGetNextContainerFromIterator(&iter);
		while (currCon)
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
			if (!FindErrorInList(pEntry, pList2))
			{
				objClearContainerIterator(&iter);
				return true;
			}
			currCon = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);
	}
	return false;
}


void CB_StartSummaryTable(char **estr)
{
		estrConcatf(estr,
			"<table width=\"100%%\" class=\"summarytable\" cellpadding=3 cellspacing=0>"
			"<tr>"
			"<td align=right class=\"summaryheadtd\">ID</td>"
			"<td align=right class=\"summaryheadtd\">Type</td>"
			"<td align=right class=\"summaryheadtd\">Description</td>"
			"<td align=right class=\"summaryheadtd\">Blamee</td>"
			"</tr>\n");
}

void CB_EndSummaryTable(char **estr)
{
	estrConcatf(estr, "</table>\n");
}



void CB_DumpSummaryToString(char **ppEString, ErrorEntry *pEntry, char *pClassString, bool bIncludeLink, GlobalType eErrorType)
{
	char *pShortDescription = NULL;
	char *pEscapedShortDescription = NULL;
	estrConcatf(ppEString, "<tr>\n");

	estrPrintf(&pShortDescription, "%s %s", pEntry->pErrorString ? pEntry->pErrorString : "", pEntry->pExpression ? pEntry->pExpression : "");

	if (estrLength(&pShortDescription) > 90)
	{
		estrSetSize(&pShortDescription, 90);
	}

	estrCopyWithHTMLEscapingSafe(&pEscapedShortDescription, pShortDescription, false);

	if (bIncludeLink)
	{	
		estrConcatf(ppEString, "<td class=\"%s\">[<a href=\"/CBdetail?CBid=%d&Context=%d\">%d</a>]</td>\n", 
			pClassString, pEntry->uID, eErrorType, pEntry->uID);
	}
	else
	{
		estrConcatf(ppEString, "<td class=\"%s\">[%d]</td>\n", pClassString, pEntry->uID);
	}
	estrConcatf(ppEString, "<td class=\"%s\">%s</td>\n", pClassString, ErrorDataTypeToString(pEntry->eType));
	estrConcatf(ppEString, "<td class=\"%s\">%s</td>\n", pClassString, pEscapedShortDescription);
	estrConcatf(ppEString, "<td class=\"%s\">%s</td>\n", pClassString, pEntry->pLastBlamedPerson ? pEntry->pLastBlamedPerson : "");
	

	estrConcatf(ppEString, "</tr>\n");
	estrDestroy(&pShortDescription);
	estrDestroy(&pEscapedShortDescription);
}


void GetAllBugsIntoHTML(char **ppEString, bool bIncludeFixedBugs)
{
	ErrorTrackerEntryList *pCurList = NULL;
	ErrorTrackerEntryList *pLastList = NULL;

	estrConcatf(ppEString, "</pre>\n\n");

	pCurList = CB_GetCurrentEntryList();
	pLastList = CB_GetLastEntryList();

	if (objCountTotalContainersWithType(pCurList->eContainerType))
	{
		char *pNewErrors = NULL;
		char *pOldErrors = NULL;
		char *pFixedErrors = NULL;
		ContainerIterator iter = {0};
		ErrorEntry *pEntry;

		objInitContainerIteratorFromType(pCurList->eContainerType, &iter);
		while (pEntry = objGetNextObjectFromIterator(&iter))
		{
			if (FindErrorInList(pEntry, pLastList))
				CB_DumpSummaryToString(&pOldErrors, pEntry, "OldBug", true, pCurList->eContainerType);
			else
				CB_DumpSummaryToString(&pNewErrors, pEntry, "NewBug", true, pCurList->eContainerType);
			
		}
		objClearContainerIterator(&iter);

		if (bIncludeFixedBugs)
		{
			if (pLastList && objCountTotalContainersWithType(pLastList->eContainerType))
			{
				objInitContainerIteratorFromType(pLastList->eContainerType, &iter);
				while (pEntry = objGetNextObjectFromIterator(&iter))
				{
					if (!FindErrorInList(pEntry, pCurList))
						CB_DumpSummaryToString(&pFixedErrors, pEntry, "FixedBug", false, 0);
				}
				objClearContainerIterator(&iter);
			}
		}

		CB_StartSummaryTable(ppEString);
		if (pNewErrors)
			estrConcatf(ppEString, "%s", pNewErrors);
		if (pOldErrors)
			estrConcatf(ppEString, "%s", pOldErrors);
		if (pFixedErrors)
			estrConcatf(ppEString, "%s", pFixedErrors);
		CB_EndSummaryTable(ppEString);

		estrDestroy(&pNewErrors);
		estrDestroy(&pOldErrors);
		estrDestroy(&pFixedErrors);
	}

	estrConcatf(ppEString, "<pre>\n\n");
}

char *GetCurrentDescriptiveErrorString(void)
{
	static char *pLastString = NULL;
	ErrorTrackerEntryList *pCurList = NULL;
	ErrorTrackerEntryList *pLastList = NULL;

	pCurList = CB_GetCurrentEntryList();
	pLastList = CB_GetLastEntryList();

	if (!pCurList->bSomethingHasChanged)
	{
		return pLastString;
	}

	pCurList->bSomethingHasChanged = false;
	estrDestroy(&pLastString);

	if (objCountTotalContainersWithType(pCurList->eContainerType))
	{
		char *pNewErrors = NULL;
		char *pOldErrors = NULL;
		ContainerIterator iter = {0};
		Container *currCon = NULL;

		objInitContainerIteratorFromType(pCurList->eContainerType, &iter);
		currCon = objGetNextContainerFromIterator(&iter);
		while (currCon)
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
			if (FindErrorInList(pEntry, pLastList))
			{
				CB_DumpEntryToString(&pOldErrors, pEntry, pCurList->eContainerType);
			}
			else
			{
				CB_DumpEntryToString(&pNewErrors, pEntry, pCurList->eContainerType);
			}
			currCon = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);

		if (pNewErrors)
		{
			estrConcatf(&pLastString, "\n--------------------------------------------NEW ERRORS:\n%s\n\n", pNewErrors);
		}

		if (pOldErrors)
		{
			estrConcatf(&pLastString, "\n---------------------------PRE-EXISTING ERRORS:\n%s\n\n", pOldErrors);

		}
		estrDestroy(&pNewErrors);
		estrDestroy(&pOldErrors);
	}
	estrFixupNewLinesForWindows(&pLastString);
	return pLastString;
}

//callback used to attach errors to autoemails from build scripting (used by production builders)
void OVERRIDE_LATELINK_BuildScriptingAppendErrorsToEstring(char **ppEstring, bool bHTML)	
{
	ErrorTrackerEntryList *pCurList = CB_GetCurrentEntryList();
	ContainerIterator iter = {0};
	Container *currCon = NULL;
//	int i;
	bool bWroteSomething;

/*	CheckForNewXboxDumps();


	if (eaSize(&gppXboxDumpFiles))
	{
		for (i=0; i < eaSize(&gppXboxDumpFiles); i++)
		{
			estrConcatf(ppEstring, "POSSIBLE XBOX DUMP: %s\n", gppXboxDumpFiles[i]);
		}
	}*/

	MoveAndUpdateDumpFiles(pCurList);

	if (bHTML)
	{

/*
<style>
.fatal-errors {
  border: 7px solid red;
  background-color: #edd;
}
</style>


<pre class="fatal-errors">
*/


		estrConcatf(ppEstring, "%s", "\n<style>\n.fatal-errors {\n  border: 7px solid red;\n  background-color: #edd;\n}\n</style>\n\n<pre class=\"fatal-errors\">\n");
	}

	estrConcatf(ppEstring, "-------------FATAL ERRORS-------------:\n");
	bWroteSomething = false;

	objInitContainerIteratorFromType(pCurList->eContainerType, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		if (IsFatal(pEntry))
		{
			CB_DumpEntryToString(ppEstring, pEntry, pCurList->eContainerType);
			bWroteSomething = true;
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	if (!bWroteSomething)
	{
		estrConcatf(ppEstring, "(none)\n");
	}

	bWroteSomething = false;

	
	if (bHTML)
	{
		estrConcatf(ppEstring, "\n</pre>\n<pre>\n");
	}

	estrConcatf(ppEstring, "-------------NON-FATAL ERRORS-------------:\n");

	objInitContainerIteratorFromType(pCurList->eContainerType, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

		if (!IsFatal(pEntry))
		{		
			CB_DumpEntryToString(ppEstring, pEntry, pCurList->eContainerType);
			bWroteSomething = true;
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	if (!bWroteSomething)
	{
		estrConcatf(ppEstring, "(none)\n");
	}
		
	if (bHTML)
	{
		estrConcatf(ppEstring, "</pre>\n");
	}


}

void MoveAndUpdateDumpFiles(ErrorTrackerEntryList *pErrors)
{
	ErrorEntry *pError;
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	objInitContainerIteratorFromType(pErrors->eContainerType, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		pError = CONTAINER_ENTRY(currCon);
	
		if (pError->eType == ERRORDATATYPE_ERROR || pError->eType == ERRORDATATYPE_ASSERT || pError->eType == ERRORDATATYPE_FATALERROR)
		{
			char subString[32];
			char **ppFileList;

			int i;
			int iBest = -1;
			U32 iBestTime = gTimeCBStartedUp;

			ppFileList = fileScanDirFolders("c:\\Night\\dumps", FSF_FILES);
			sprintf(subString, "ET%u.dmp", pError->uID);

			for (i=0; i < eaSize(&ppFileList); i++)
			{
				if (strstri(ppFileList[i], subString))
				{
					U32 iCurTime = fileLastChangedSS2000(ppFileList[i]);
					if (iCurTime > iBestTime)
					{
						iBest = i;
						iBestTime = iCurTime;
					}
					
				}
			}

			if (iBest != -1)
			{
				char newLocation[CRYPTIC_MAX_PATH];
				char systemString[1024];
				char shortName[CRYPTIC_MAX_PATH];
				getFileNameNoDir(shortName, ppFileList[iBest]);
				sprintf(newLocation, "n:\\continuousbuilder\\dumps\\%s", shortName);
				sprintf(systemString, "copy %s %s", ppFileList[iBest], newLocation);
				backSlashes(systemString);
				system(systemString);

				pError->pUserData->pDumpFileName = strdup(newLocation);
				pErrors->bSomethingHasChanged = true;
			}

			fileScanDirFreeNames(ppFileList);
		}

		if (eaSize(&pError->ppMemoryDumps) && !pError->pUserData->pMemoryDumpFileName)
		{
			char memoryDumpSourceFileName[CRYPTIC_MAX_PATH];
			char memoryDumpTargetFileName[CRYPTIC_MAX_PATH];
			char systemString[1024];

			calcMemoryDumpPath(SAFESTR(memoryDumpSourceFileName), pError->uID, pError->ppMemoryDumps[0]->iDumpIndex);

			backSlashes(memoryDumpSourceFileName);
			
			sprintf(memoryDumpTargetFileName, "%s\\CBMemDump_%s.txt", gConfig.pGlobalDumpLocation, errorTrackerLibShortStringFromUniqueHash(pError));

			sprintf(systemString, "copy %s %s", memoryDumpSourceFileName, memoryDumpTargetFileName);

			system(systemString);

			pError->pUserData->pMemoryDumpFileName = strdup(memoryDumpTargetFileName);
			pErrors->bSomethingHasChanged = true;
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

	
//copies all state data from old errors into new errors, also moves dump files to a more useful location
void IntegrateUserDataFromOldErrors(ErrorTrackerEntryList *pCurErrors, ErrorTrackerEntryList *pOldErrors)
{
	ErrorEntry *pOldError, *pNewError;
	if (pOldErrors)
	{
		ContainerIterator iter = {0};
		objInitContainerIteratorFromType(pCurErrors->eContainerType, &iter);
		while (pNewError = objGetNextObjectFromIterator(&iter))
		{
			if ((pOldError = FindErrorInList(pNewError, pOldErrors)))
			{
				if (pOldError->pUserData)
				{
					if (!pNewError->pUserData)
						pNewError->pUserData = StructCreate(parse_ErrorTrackerEntryUserData);
					pCurErrors->bSomethingHasChanged = true;
					pNewError->pUserData->iTimeWhenItFirstHappened = pOldError->pUserData->iTimeWhenItFirstHappened;
					pNewError->pUserData->iEmailNumberWhenItFirstHappened = pOldError->pUserData->iEmailNumberWhenItFirstHappened;

					if (pOldError->pUserData->pDumpFileName)
					{
						if (pNewError->pUserData->pDumpFileName)
							free(pNewError->pUserData->pDumpFileName);
						pNewError->pUserData->pDumpFileName = strdup(pOldError->pUserData->pDumpFileName);
					}

					if (pOldError->pUserData->pMemoryDumpFileName)
					{
						if (pNewError->pUserData->pMemoryDumpFileName)
							free(pNewError->pUserData->pMemoryDumpFileName);
						pNewError->pUserData->pMemoryDumpFileName = strdup(pOldError->pUserData->pMemoryDumpFileName);
					}
				}
			}
		}
		objClearContainerIterator(&iter);
	}
	MoveAndUpdateDumpFiles(pCurErrors);
}





bool IsFatal(ErrorEntry *pEntry)
{
	switch (pEntry->eType)
	{
	case ERRORDATATYPE_ASSERT:
	case ERRORDATATYPE_CRASH:
	case ERRORDATATYPE_COMPILE: // For the continuous builder
	case ERRORDATATYPE_FATALERROR:
		return true;
	}

	return false;
}

bool IsFatalNotCountingCompile(ErrorEntry *pEntry)
{
	switch (pEntry->eType)
	{
	case ERRORDATATYPE_ASSERT:
	case ERRORDATATYPE_CRASH:
	case ERRORDATATYPE_FATALERROR:
		return true;
	}

	return false;
}


void BuildIsBad(bool bFatal)
{
	char **ppNewPeople = NULL;
	char **ppPeopleWhoJustCheckedIn = NULL;
	char **ppWhoToSendTo = NULL;
	char *pEmailString = NULL;
	char *pEmailStringHTML = NULL;
	FILE *pFile;

	int i;
	int iBreakageID;
	char emailFileName[MAX_PATH];
	ErrorTrackerEntryList *pCurList = CB_GetCurrentEntryList();
	ErrorTrackerEntryList *pLastList = CB_GetLastEntryList();
	char *pBreakageMessage = NULL;
	int iNewErrorCount = 0;
	int iOldErrorCount = 0;
	int iFixedErrorCount = 0;
	const char **ppWhoToHumiliate = NULL;
	int iNumYouDoNotHaves = 0;
	
	bool bOnlyEmailBuildMaster = false;
	bool bDontSendEmail = false;

	//a constructed list of errors consisting of only the ones that are new this frame
	ErrorTrackerEntryList newErrorList = {0};


	char *pBreakageJabber = NULL;

	CheckinInfo **ppSVNCheckins = NULL;
	CheckinInfo **ppGimmeCheckins = NULL;
//	char *pBuildID = NULL;
		
	char hostName[256];
	
	if (!CBTypeIsCONT())
	{
		return;
	}
	//errorTrackerLibStallUntilTransactionsComplete(); TODO

	//kill any sendjabbers that are left floating around from last time (as tends to happen)
	system("taskkill /im sendjabber.exe");

	//load up the list of who clicked yes to gimme, in case we need to humiliate them
	Gimme_LoadDidTheyClickYesEntries();

	IntegrateUserDataFromOldErrors(pCurList, pLastList);


	if ((gDynamicState.eLastResult == CBRESULT_FAILED || gDynamicState.eLastResult == CBRESULT_SUCCEEDED_W_ERRS) 
		&& !ErrorListsAreDifferent(pCurList, pLastList) && !(CheckConfigVar("ONLY_SEND_TO")))
	{
		filelog_printf(GetCBLogFileName(NULL), "Build is broken... but errors are the same as last frame... no email sent\n");
		bOnlyEmailBuildMaster = true;
	}

	if (objCountTotalContainersWithType(pCurList->eContainerType))
	{
		char *pNewErrors = NULL;
		char *pOldErrors = NULL;
		char *pFixedErrors = NULL;
		ContainerIterator iter = {0};
		Container *currCon = NULL;

//two passes... first, fatal errors (crashes and asserts). Then, everything else
		objInitContainerIteratorFromType(pCurList->eContainerType, &iter);
		currCon = objGetNextContainerFromIterator(&iter);
		while (currCon)
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

			if (strstri_safe(pEntry->pLastBlamedPerson, "do not have"))
			{
				iNumYouDoNotHaves++;
			}
			else
			{
				if (IsFatal(pEntry))
				{
					if (FindErrorInList(pEntry, pLastList))
					{
						CB_DumpEntryToString(&pOldErrors, pEntry, pCurList->eContainerType);
						iOldErrorCount++;
					}
					else
					{
						CB_DumpEntryToString(&pNewErrors, pEntry, pCurList->eContainerType);
						iNewErrorCount++;
						eaPush(&newErrorList.ppEntries, pEntry);
					}
				}
			}

			currCon = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);

		objInitContainerIteratorFromType(pCurList->eContainerType, &iter);
		currCon = objGetNextContainerFromIterator(&iter);
		while (currCon)
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

			if (!IsFatal(pEntry))
			{
				if (strstri_safe(pEntry->pLastBlamedPerson, "do not have"))
				{
					iNumYouDoNotHaves++;
				}
				else
				{
					if (FindErrorInList(pEntry, pLastList))
					{
						CB_DumpEntryToString(&pOldErrors, pEntry, pCurList->eContainerType);
						iOldErrorCount++;
					}
					else
					{
						CB_DumpEntryToString(&pNewErrors, pEntry, pCurList->eContainerType);
						iNewErrorCount++;
						eaPush(&newErrorList.ppEntries, pEntry);
					}
				}
			}

			currCon = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);

		if (pLastList && objCountTotalContainersWithType(pLastList->eContainerType))
		{
			objInitContainerIteratorFromType(pLastList->eContainerType, &iter);
			currCon = objGetNextContainerFromIterator(&iter);

			while (currCon)
			{
				ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

				if (!strstri_safe(pEntry->pLastBlamedPerson, "do not have"))
				{
					if (!FindErrorInList(pEntry, pCurList))
					{
						CB_DumpEntryToString(&pFixedErrors, pEntry, pLastList->eContainerType);
						iFixedErrorCount++;
					}
				}
				currCon = objGetNextContainerFromIterator(&iter);
			}
			objClearContainerIterator(&iter);
		}

		if (pNewErrors)
			estrConcatf(&pBreakageMessage, "\n--------------------------------------------NEW ERRORS:\n%s\n\n", pNewErrors);
		if (pOldErrors)
			estrConcatf(&pBreakageMessage, "\n---------------------------PRE-EXISTING ERRORS:\n%s\n\n", pOldErrors);
		if (!pNewErrors && !pOldErrors && iNumYouDoNotHaves)
			estrConcatf(&pBreakageMessage, "\n\n\n\n\n-------------------ERRORS FOUND, BUT ALL WERE \"You do not have latest version\"--------------\n");
		if (pFixedErrors)
			estrConcatf(&pBreakageMessage, "\n---------------------------FIXED ERRORS:\n%s\n\n", pFixedErrors);
		estrDestroy(&pNewErrors);
		estrDestroy(&pOldErrors);
		estrDestroy(&pFixedErrors);
	}
	else
	{
		estrConcatf(&pBreakageMessage, "\n\n\n\n\n-------------------NO ERRORS IDENTIFIED   THIS IS ODD--------------\n----Compile log can be found in %s\n\n\n\n", compileOutputFileName);
	}

	iBreakageID = gDynamicState.iNextEmailNumber++;
//	sprintf(GetCBLogFileName(), STACK_SPRINTF("%s\\CBLog.txt", GetCBLogDirectoryName()), gDynamicState.iNextEmailNumber);
	ParserWriteTextFile(STACK_SPRINTF("%s\\CBDynStatus.txt", GetCBDirectoryName()), parse_CBDynamicState, &gDynamicState, 0, 0); 

	sprintf(emailFileName, "%s\\BreakageEmail.txt", GetCBLogDirectoryName(NULL));
	BuildScripting_AddVariable(CBGetRootScriptingContext(), "$EMAIL_FILENAME$", emailFileName, "BUILTIN");

	mkdirtree(emailFileName);

	AddPeopleToSuspectList(&gDynamicState.curSuspects, gDynamicState.lastRev.iSVNRev ? &gDynamicState.lastRev : &gDynamicState.lastCheckedInRev, &gCurRev, 
		&newErrorList, &ppNewPeople, &ppPeopleWhoJustCheckedIn);

	//check for people to humiliate
	for (i=0 ; i < eaSize(&newErrorList.ppEntries); i++)
	{
		ErrorEntry *pError = newErrorList.ppEntries[i];
		if (pError->eType == ERRORDATATYPE_ERROR
			&& pError->pDataFile
			&& pError->pLastBlamedPerson
			&& !strchr(pError->pLastBlamedPerson, ' '))
		{
			if (Gimme_DidTheyClickYes(pError->pLastBlamedPerson, pError->pDataFile, gDynamicState.lastRev.iGimmeTime ? (gDynamicState.lastRev.iGimmeTime - 15 * 60) : (gCurRev.iGimmeTime - 120 * 60), gCurRev.iGimmeTime))
			{
				eaPushUnique(&ppWhoToHumiliate, allocAddCaseSensitiveString(pError->pLastBlamedPerson));
			}
		}
	}

	eaDestroy(&newErrorList.ppEntries);

	
	
	gethostname(hostName, 255);
	estrConcatf(&pEmailString, "This email was sent to you by the %s Continuous Builder, located at http://%s\n\n", GetCBName(), hostName);
	
	if (eaSize(&ppWhoToHumiliate))
	{
		estrConcatf(&pEmailString, "UNTESTED DATA ERRORS!\nUNTESTED DATA ERRORS!\nUNTESTED DATA ERRORS!\nUNTESTED DATA ERRORS!\nUNTESTED DATA ERRORS!\nUNTESTED DATA ERRORS!\n");
		estrConcatf(&pEmailString, "Someone clicked through the gimme warning that asked them to test before checking in, and then broke the build. Their supervisors have been notified.\n");
		estrConcatf(&pEmailString, "---------------------------- Violator%s -----------------------\n", eaSize(&ppWhoToHumiliate) > 1 ? "s" : "");
		for (i=0; i < eaSize(&ppWhoToHumiliate); i++)
		{
			estrConcatf(&pEmailString, "%s\n", ppWhoToHumiliate[i]);
		}

		estrConcatf(&pEmailString, "--------------------------------------------------------------\n\n\n\n\n\n");
	}



	switch (gDynamicState.eLastResult)
	{
	case CBRESULT_SUCCEEDED_W_ERRS:
		if (bFatal)
		{
			estrConcatf(&pEmailString, "The continuous build is now fatally broken (was previously non-fatally broken), and you are a suspect. (%s)\nBreakage ID %d.\n\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()), iBreakageID);
		}
		else
		{
			estrConcatf(&pEmailString, "The continuous build is still non-fatally broken, and you are a suspect. (%s)\nBreakage ID %d.\n\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()), iBreakageID);
		}
		break;
	case CBRESULT_FAILED:
		if (bFatal)
		{
			estrConcatf(&pEmailString, "The continuous build is still fatally broken, and you are a suspect. (%s)\nBreakage ID %d.\n\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()), iBreakageID);
		}
		else
		{
			estrConcatf(&pEmailString, "The continuous build is no longer fatally broken, which you were previously suspected for, although there are still non-fatal errors. (%s)\nBreakage ID %d.\n\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()), iBreakageID);
		}		
		break;

	case CBRESULT_NONE:
		estrConcatf(&pEmailString, "The %s continuous builder has started up and is in a %s broken state. You are a suspect. (%s)\nBreakage ID %d.\n\n", 
			 GetCBName(), bFatal ? "fatally" : "non-fatally", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()), iBreakageID);
		break;
	case CBRESULT_SUCCEEDED:
		estrConcatf(&pEmailString, "The %s continuous builder has just %s broken, and you are a suspect. (%s)\nBreakage ID %d.\n\n", 
			GetCBName(), bFatal ? "fatally" : "non-fatally", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()), iBreakageID);
		break;
	}

	estrConcatf(&pEmailString, "People who checked relevant stuff in since the last build: ");

	for (i=0; i < eaSize(&ppPeopleWhoJustCheckedIn); i++)
	{
		estrConcatf(&pEmailString, "%s%s", i == 0 ? "" : ", ", ppPeopleWhoJustCheckedIn[i]);
	}

	if (!eaSize(&ppPeopleWhoJustCheckedIn))
	{
		int iBrk = 0;
	}

	estrConcatf(&pEmailString, "\n\n");

	if ((gDynamicState.eLastResult == CBRESULT_FAILED || gDynamicState.eLastResult == CBRESULT_SUCCEEDED_W_ERRS) && eaSize(&ppNewPeople))
	{
		estrConcatf(&pEmailString, "People who are newly suspect: ");

		for (i=0; i < eaSize(&ppNewPeople); i++)
		{
			estrConcatf(&pEmailString, "%s%s", i == 0 ? "" : ", ", ppNewPeople[i]);
		}

		estrConcatf(&pEmailString, "\n\n");
	}

/*	if (eaSize(&gppXboxDumpFiles))
	{
		for (i=0; i < eaSize(&gppXboxDumpFiles); i++)
		{
			estrConcatf(&pEmailString, "POSSIBLE XBOX DUMP: %s\n", gppXboxDumpFiles[i]);
		}
	}*/

	estrConcatf(&pEmailString, "%s\n", pBreakageMessage);


	if (gDynamicState.lastRev.iSVNRev)
	{
		estrConcatf(&pEmailString, "---------------------------------------\nALL CHECKINS THIS BUILILD:\n");
		SVN_GetCheckins(gDynamicState.lastRev.iSVNRev, gCurRev.iSVNRev, NULL, gConfig.ppSVNFolders, NULL, &ppSVNCheckins, 600, 0);
		Gimme_GetCheckinsBetweenTimes(gDynamicState.lastRev.iGimmeTime ? gDynamicState.lastRev.iGimmeTime : gCurRev.iGimmeTime - 60 * 60, gCurRev.iGimmeTime, NULL, gConfig.ppGimmeFolders, GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppGimmeCheckins, 600);


		if (eaSize(&ppSVNCheckins))
		{
			estrConcatf(&pEmailString, "\n\nCode checkins:\n");

			for (i=0; i < eaSize(&ppSVNCheckins); i++)
			{
				estrConcatf(&pEmailString, "%s %d: %s\n", 
					ppSVNCheckins[i]->userName, ppSVNCheckins[i]->iRevNum, ppSVNCheckins[i]->checkinComment);

			}
		}

		if (eaSize(&ppGimmeCheckins))
		{

			estrConcatf(&pEmailString, "\n\nAsset Checkins:\n");

			for (i=0; i < eaSize(&ppGimmeCheckins); i++)
			{
				estrConcatf(&pEmailString, "%s: %s\n\n", 
					ppGimmeCheckins[i]->userName, ppGimmeCheckins[i]->checkinComment);
			}

		}
	}

	eaDestroyStruct(&ppSVNCheckins, parse_CheckinInfo);
	eaDestroyStruct(&ppGimmeCheckins, parse_CheckinInfo);

#if 0
	if (gDynamicState.lastGoodRev.iSVNRev)
	{
		estrConcatf(&pEmailString, "---------------------------------------\nALL CHECKINS SINCE THE LAST TOTALLY CLEAN BUILD:\n");
		SVN_GetCheckins(gDynamicState.lastGoodRev.iSVNRev, gCurRev.iSVNRev, gCBState.pSVNFolders, NULL, &ppSVNCheckins, 600);
		Gimme_GetCheckins(gDynamicState.lastGoodRev.iGimmeRev, gCurRev.iGimmeRev, gCBState.pGimmeFolders, GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppGimmeCheckins, 600);


		if (eaSize(&ppSVNCheckins))
		{
			estrConcatf(&pEmailString, "\n\nCode checkins:\n");

			for (i=0; i < eaSize(&ppSVNCheckins); i++)
			{
				estrConcatf(&pEmailString, "%s %d: %s\n", 
					ppSVNCheckins[i]->userName, ppSVNCheckins[i]->iRevNum, ppSVNCheckins[i]->checkinComment);

			}
		}

		if (eaSize(&ppGimmeCheckins))
		{

			estrConcatf(&pEmailString, "\n\nAsset Checkins:\n");

			for (i=0; i < eaSize(&ppGimmeCheckins); i++)
			{
				estrConcatf(&pEmailString, "%s: %s\n\n", 
					ppGimmeCheckins[i]->userName, ppGimmeCheckins[i]->checkinComment);
			}
		}
	}
#endif


	estrConcatf(&pEmailString, "------------------------------------------\nCur build: SVN %d gimme %u(%s)\n",
		gCurRev.iSVNRev, gCurRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gCurRev.iGimmeTime));
	estrConcatf(&pEmailString, "Last build: SVN %d gimme %u(%s)\n", 
		gDynamicState.lastRev.iSVNRev, gDynamicState.lastRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastRev.iGimmeTime));
	estrConcatf(&pEmailString, "Last totally errorless build: SVN %d gimme %u(%s)\n",
		gDynamicState.lastGoodRev.iSVNRev, gDynamicState.lastGoodRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastGoodRev.iGimmeTime));
	estrConcatf(&pEmailString, "Last checked in build: SVN %d gimme %u(%s)\n",
		gDynamicState.lastCheckedInRev.iSVNRev, gDynamicState.lastCheckedInRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastCheckedInRev.iGimmeTime));


	estrCopyWithHTMLEscaping(&pEmailStringHTML, pEmailString, true);


	pFile = fopen(emailFileName, "wt");
	fprintf(pFile, "%s", pEmailStringHTML);
	fclose(pFile);
	estrDestroy(&pEmailString);
	estrDestroy(&pEmailStringHTML);

	//if we only want email about fatal things, then only send emails if the current build or the last build was fatal
	if (CheckConfigVarExistsAndTrue("FATAL_EMAILS_ONLY"))
	{
		if (!bFatal && gDynamicState.eLastResult != CBRESULT_FAILED)
		{
			filelog_printf(GetCBLogFileName(NULL), "Current and last build are both non-fatal, FATAL_EMAILS_ONLY is set, sending no emails");

			bDontSendEmail = true;
		}
	}

	
		
	if (bDontSendEmail)
	{
		//do nothing
	}
	else if (bOnlyEmailBuildMaster)
	{
		eaPushUnique(&ppWhoToSendTo, (char*)allocAddString("admin-builderspam")); // will this work? Interesting to try.
		SendEmailToList(EMAILFLAG_NO_DEFAULT_RECIPIENTS | EMAILFLAG_HTML, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build - Build is %s broken, errors are the same as last time (ID %d)!!", GetCBName(), bFatal ? "fatally" : "non-fatally", iBreakageID));
	}
	else
	//if there were only fixes, no new errors, then send email only to administrators
	if (iNewErrorCount == 0 && iOldErrorCount > 0)
	{
		SendEmailToList(EMAILFLAG_HTML, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build - Some errors fixed, but still %s broken (admins emailed only) (ID %d)!!", GetCBName(), bFatal ? "fatally" : "non-fatally", iBreakageID));
	}
	else
	{
		for (i=0; i < eaSize(&ppPeopleWhoJustCheckedIn); i++)
		{
			if (ppPeopleWhoJustCheckedIn[i])
			{
				eaPushUnique(&ppWhoToSendTo, ppPeopleWhoJustCheckedIn[i]);
			}
		}

		for (i=0; i < eaSize(&ppNewPeople); i++)
		{
			if (ppNewPeople[i])
			{
				eaPushUnique(&ppWhoToSendTo, ppNewPeople[i]);
			}
		}

		switch (gDynamicState.eLastResult)
		{
		case CBRESULT_FAILED:
			if (bFatal)
			{
				SendEmailToList(EMAILFLAG_HTML | EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build - Still Fatally Broken (ID %d)!!", GetCBName(), iBreakageID));
			}
			else
			{
				int j;
				eaDestroy(&ppWhoToSendTo);
				for (j = 0; j < eaSize(&gDynamicState.ppLastFatalBreakageEmailRecipients); j++)
				{
					eaPush(&ppWhoToSendTo, (char*)allocAddString(gDynamicState.ppLastFatalBreakageEmailRecipients[j]));
				}

				SendEmailToList(EMAILFLAG_HTML | EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build - Now Non-Fatally Broken (previously Fatal) (ID %d)!!", GetCBName(), iBreakageID));
			}
			break;
		case CBRESULT_SUCCEEDED_W_ERRS:
			if (bFatal)
			{
				SendEmailToList(EMAILFLAG_HTML | EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build - Now Fatally Broken (previously Non-Fatal) (ID %d)!!", GetCBName(), iBreakageID));
			}
			else
			{
				SendEmailToList(EMAILFLAG_HTML | EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build - Still Non-Fatally Broken (ID %d)!!", GetCBName(), iBreakageID));
			}
			break;
		case CBRESULT_NONE:
		case CBRESULT_SUCCEEDED:
			SendEmailToList(EMAILFLAG_HTML | EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES, &ppWhoToSendTo, emailFileName, STACK_SPRINTF("!!%s Continuous Build Has %s Broken (ID %d)!!", GetCBName(), 
				bFatal ? "Fatally" : "Non-Fatally", iBreakageID));
			break;
		}

		//any time we fatally break, remember everyone we sent emails to so we can email them again when it's fixed
		if (bFatal && gDynamicState.eLastResult != CBRESULT_FAILED)
		{
			int k;
			eaDestroyEx(&gDynamicState.ppLastFatalBreakageEmailRecipients, NULL);
			for (k = 0; k < eaSize(&ppWhoToSendTo); k++)
			{
				eaPush(&gDynamicState.ppLastFatalBreakageEmailRecipients, strdup(ppWhoToSendTo[k]));
			}
		}

		estrStackCreate(&pBreakageJabber);
		estrConcatf(&pBreakageJabber, "Hey!!! The CB thinks you have caused an error!!!!!    The %s Continuous Builder, located at http://%s, has sent you a detailed email explaining how you are a suspect in a build breakage of some kind. Please read it.",
			GetCBName(), hostName);
		SendJabberToList(&ppWhoToSendTo, pBreakageJabber, false);
	}

	eaDestroy(&ppWhoToHumiliate);

	eaDestroy(&ppWhoToSendTo);
	eaDestroy(&ppNewPeople);
	eaDestroy(&ppPeopleWhoJustCheckedIn);
}





#define GETBUILDERRORS_FAIL									\
{															\
	ErrorData data = {0};									\
	data.eType = ERRORDATATYPE_COMPILE;						\
	data.pErrorString = "UNKNOWN COMPILE ERROR";			\
	CB_ProcessErrorData(NULL, &data); \
	if (pInBuf) free(pInBuf);								\
	return EXTENDEDERROR_FATAL;													\
}
/*
CompileErrorConfig PCAndXboxErrorConfig = 
{
	{
		" : ",
		NULL
	},
	{
		"could not connect to xbox",
		"cannot connect to xbox",
		"Could not delete file",
		"The process cannot access the file",
		"error writing to",
		"Build System process failed to initialize",
		NULL
	},

	"--Configuration: ",
	"---",
};

CompileErrorConfig PS3ErrorConfig = 
{
	{
		"error:",
		"sorry, unimplemented",
		"\\ld.exe", 
		"undefined reference to",
		NULL,
	},
	{
		NULL,
	},
	"------ VSI Build started: Project: ",
	"------",
};
*/

int OVERRIDE_LATELINK_BuildScripting_GetExtendedErrors(char *pExtendedErrorType, char *pBuildOutputFile, char **ppExtendedErrorString)
{
	if (stricmp(pExtendedErrorType, "COMPILE") == 0)
	{
		int iSize;
		char *pInBuf = fileAlloc(pBuildOutputFile, &iSize);
		char *pReadHead;
		CompileErrorConfig *pConfigToUse = NULL;

	


		char curConfiguration[256] = "(No project determined)";
		int i;

		if (!pInBuf) GETBUILDERRORS_FAIL;

		if (!eaSize(&gConfig.ppCompileConfigs))
		{
			GETBUILDERRORS_FAIL;
		}


		for (i=0; i < eaSize(&gConfig.ppCompileConfigs); i++)
		{
			pConfigToUse = gConfig.ppCompileConfigs[i];
			if (pConfigToUse->pSignatureString && pConfigToUse->pSignatureString[0] && strstri(pInBuf, pConfigToUse->pSignatureString))
			{
				break;
			}
		}
		//if we never found one, that's OK. Default to the last one.


		for (i=0; i < eaSize(&pConfigToUse->ppRetryCompilerErrors); i++)
		{
			if (strstri(pInBuf, pConfigToUse->ppRetryCompilerErrors[i]))
			{
				free(pInBuf);
				estrCopy2(ppExtendedErrorString, pConfigToUse->ppRetryCompilerErrors[i]);
				return EXTENDEDERROR_RETRY;
			}
		}

		for (i=0; i < eaSize(&pConfigToUse->ppFakeCompilerErrors); i++)
		{
			if (strstri(pInBuf, pConfigToUse->ppFakeCompilerErrors[i]))
			{
				ErrorData data = {0};
				free(pInBuf);
				estrCopy2(ppExtendedErrorString, pConfigToUse->ppFakeCompilerErrors[i]);

				data.eType = ERRORDATATYPE_COMPILE;
				data.pErrorString = STACK_SPRINTF("Found \"fake\" compile error %s", *ppExtendedErrorString);
				CB_ProcessErrorData(NULL, &data);

				return EXTENDEDERROR_NOTANERROR;
			}
		}
		

		if (!strStartsWith(pConfigToUse->pBeginningOfConfigString, "(NONE)"))
		{

		
			pReadHead = strstr(pInBuf, pConfigToUse->pBeginningOfConfigString);
			if (!pReadHead) GETBUILDERRORS_FAIL;

			pReadHead += strlen(pConfigToUse->pBeginningOfConfigString);

			while (pReadHead)
			{
				char *pEndOfConfigurationString = strstr(pReadHead, pConfigToUse->pEndOfConfigString);
				char *pBeginningOfNextConfig = strstri(pReadHead, pConfigToUse->pBeginningOfConfigString);

				if (!pEndOfConfigurationString) GETBUILDERRORS_FAIL;

				strncpy(curConfiguration, pReadHead, pEndOfConfigurationString - pReadHead);

				while (1)
				{
					char *pNextError = NULL;

					for (i=0; i < eaSize(&pConfigToUse->ppErrorStrings); i++)
					{
						char *pPotentialNextError;
						if((pPotentialNextError = strstri(pReadHead, pConfigToUse->ppErrorStrings[i])))
						{
							if (!pNextError || pNextError > pPotentialNextError)
							{
								pNextError = pPotentialNextError;
							}
						}
					}

					if (!pNextError || (pBeginningOfNextConfig && pNextError > pBeginningOfNextConfig))
					{
						//no more errors in this configuration
						if (pBeginningOfNextConfig)
						{
							pReadHead = pBeginningOfNextConfig + strlen(pConfigToUse->pBeginningOfConfigString);
						}
						else
						{
							pReadHead = NULL;
						}
						break;
					}

					{
						int iErrorLength;
						char *pErrorString;
						char temp;
						bool bIgnore = false;

						pErrorString = GetEntireLine(pNextError, pInBuf, &iErrorLength);
					
						temp = pErrorString[iErrorLength];
						pErrorString[iErrorLength] = 0;

						//some compiler messages are not actually errors... eliminate them here
						for (i=0; i < eaSize(&pConfigToUse->ppErrorStringCancellers); i++)
						{
							if (strstri(pErrorString, pConfigToUse->ppErrorStringCancellers[i]))
							{
								bIgnore = true;
								break;
							}
						}

						if (!bIgnore)
						{
							for (i=0 ; i < eaSize(&pConfigToUse->ppErrorStringCancellerRegExes); i++)
							{
								int tempInt;
								if (!regexMatch(pConfigToUse->ppErrorStringCancellerRegExes[i], pErrorString, &tempInt))
								{
									bIgnore = true;
									break;
								}
							}
						}

						if (!bIgnore)
						{
							ErrorData data = {0};
							char *pTempErrorString = NULL;
							estrStackCreate(&pTempErrorString);
							data.eType = ERRORDATATYPE_COMPILE;

							data.pErrorString = pTempErrorString;
					
							estrPrintf(&pTempErrorString, "%s\n%s", curConfiguration, pErrorString);
							CB_ProcessErrorData(NULL, &data);
							estrDestroy(&pTempErrorString);
						}

						pErrorString[iErrorLength] = temp;
					}

					pReadHead = pNextError + 3;
				}
			}
		}
		else
		{
			pReadHead = pInBuf;

			while (1)
			{
				char *pNextError = NULL;

				for (i=0; i < eaSize(&pConfigToUse->ppErrorStrings); i++)
				{
					char *pPotentialNextError;
					if((pPotentialNextError = strstri(pReadHead, pConfigToUse->ppErrorStrings[i])))
					{
						if (!pNextError || pNextError > pPotentialNextError)
						{
							pNextError = pPotentialNextError;
						}
					}
				}

				if (!pNextError)
				{
					break;
				}

				{
					int iErrorLength;
					char *pRawErrorString;
					char temp;
					bool bIgnore = false;
					char *pFixedUpErrorString = NULL;

					pRawErrorString = GetEntireLine(pNextError, pInBuf, &iErrorLength);

					temp = pRawErrorString[iErrorLength];
					pRawErrorString[iErrorLength] = 0;

					estrCopy2(&pFixedUpErrorString, pRawErrorString);
					pRawErrorString[iErrorLength] = temp;
	
					estrTrimLeadingAndTrailingWhitespace(&pFixedUpErrorString);

					//some compiler messages are not actually errors... eliminate them here
					for (i=0; i < eaSize(&pConfigToUse->ppErrorStringCancellers); i++)
					{
						if (strstri(pFixedUpErrorString, pConfigToUse->ppErrorStringCancellers[i]))
						{
							bIgnore = true;
							break;
						}
					}

					if (!bIgnore)
					{
						for (i=0 ; i < eaSize(&pConfigToUse->ppErrorStringCancellerRegExes); i++)
						{
							int tempInt;
							int iRetVal = regexMatch(pConfigToUse->ppErrorStringCancellerRegExes[i], pFixedUpErrorString, &tempInt);

							if (!iRetVal)
							{
								bIgnore = true;
								break;
							}
						}
					}

					if (!bIgnore)
					{
						for (i=0; i < eaSize(&pConfigToUse->ppRegExPrefixStrippers); i++)
						{
							int tempInt;
							int iRetVal = regexMatch(pConfigToUse->ppRegExPrefixStrippers[i]->pMatchRegEx, pFixedUpErrorString, &tempInt);
							if (!iRetVal)
							{
								char *pFound = strstri(pFixedUpErrorString, pConfigToUse->ppRegExPrefixStrippers[i]->pPrefixEnd);
								if (pFound)
								{
									int iCount = pFound - pFixedUpErrorString + strlen(pConfigToUse->ppRegExPrefixStrippers[i]->pPrefixEnd);
									estrRemove(&pFixedUpErrorString, 0, iCount);
									estrTrimLeadingAndTrailingWhitespace(&pFixedUpErrorString);

									break;
								}
							}
						}
					}


					if (!bIgnore)
					{
						ErrorData data = {0};
						data.eType = ERRORDATATYPE_COMPILE;

						data.pErrorString = pFixedUpErrorString;

						CB_ProcessErrorData(NULL, &data);
					}

					estrDestroy(&pFixedUpErrorString);

				}

				pReadHead = pNextError + 3;
			}

		}


		free(pInBuf);
	
		return EXTENDEDERROR_FATAL;
	}


	assertmsgf(0, "No support for extended error type %s", pExtendedErrorType);
	return EXTENDEDERROR_FATAL;
}

/*
\device\flash\xshell.xex
e:\devfightclub\gameclientxbox.exe
\device\flash\processdump.xex
*/


/*
void CheckForNewXboxDumps(void)
{
	U32 iStartTime;

	XboxFileInfo **ppFiles = NULL;
	int i;

	static int siLastStartingTimeCheckedForDumps = 0;

	if (siLastStartingTimeCheckedForDumps == giLastTimeStarted)
	{
		return;
	}

	siLastStartingTimeCheckedForDumps = giLastTimeStarted;
 

	if (!(CheckConfigVarExistsAndTrue("USES_XBOX")))
	{
		return;
	}

	if (!xboxQueryStatusXboxWasEverAttached())
	{
		filelog_printf(GetCBLogFileName(), "Would check for xbox dumps, but no xbox has ever been attached");
		return;
	}

	AddComment(COMMENT_SUBSTATE, "Checking for xbox dumps");

	SetSubStateString("Checking for xbox dumps");

	filelog_printf(GetCBLogFileName(), "Checking for Xbox dumps");

	iStartTime = timeSecondsSince2000_ForceRecalc();

	//we need to have xbox ready and exe = xbshell
	while (1)
	{
		bool bReady;
		static char *pExeName = NULL;

		xboxQueryStatusFromThread(&bReady, NULL, &pExeName);

		if (bReady && strstri(pExeName, "xshell.xex"))
		{
			break;
		}

		if (bReady && strStartsWith(pExeName, "e:\\"))
		{
			filelog_printf(GetCBLogFileName(), "While checking for new xbox dumps, found %s still running. Rebooting xbox",
				pExeName);
			xboxReboot(true);
			Sleep(5000);
		}
		else
		{
			if (timeSecondsSince2000_ForceRecalc() > iStartTime + 600)
			{
				SendEmailToAdministrators("Xbox never returned to xshell, couldn't check for new dumps");
				filelog_printf(GetCBLogFileName(), "Xbox never returned to xshell, couldn't check for new dumps");
				return;
			}
		}
	}

	filelog_printf(GetCBLogFileName(), "Xbox is now ready... going to scan filesystem");

	//this just randomly fails some percentage of the time saying "lost connection to xbox". So retry a bunch of times
	for (i=0; i < 20; i++)
	{
		if (xboxRecurseScanDir("xe:\\dumps\\", &ppFiles))
		{
			break;
		}

		Sleep(3000);
	}

	if (i == 20)
	{
		filelog_printf(GetCBLogFileName(), "After 20 tries, couldn't get xboxRecurseScanDir to work");
		eaPush(&gppXboxDumpFiles, strdup("XBOX_HARD_DISK_SCANNING_FAILED_THERE_MAY_BE_A_DUMP.txt"));
		return;
	}


	
	for (i=0; i < eaSize(&ppFiles); i++)
	{
		if (ppFiles[i]->iModTime > giLastTimeStarted && strEndsWith(ppFiles[i]->fullName, ".dmp"))
		{
			char localName[CRYPTIC_MAX_PATH];
			int iIndex = 0;
			char systemString[1024];

			filelog_printf(GetCBLogFileName(), "Found presumed xbox dump %s", ppFiles[i]->fullName);

			do
			{
				sprintf(localName, "n:\\continuousbuilder\\dumps\\XBOX_%u_%d.dmp", timeSecondsSince2000(), iIndex++);
			}
			while (fileExists(localName));

			mkdirtree_const(localName);

			sprintf(systemString, "\"%s\\xbcp\" %s %s", xboxGetBinDir(), ppFiles[i]->fullName, localName);
			printf("About to execute: %s\n", systemString);

			system_w_timeout(systemString, 600);

			if (!fileExists(localName))
			{
				SendEmailToAdministrators(STACK_SPRINTF("Couldn't copy %s from the xbox to the local disk", ppFiles[i]->fullName));
			}

			eaPush(&gppXboxDumpFiles, strdup(localName));
		}
	}

	eaDestroyEx(&ppFiles, NULL);


}
*/

#define SECS_BETWEEN_CB_MAINTANENCE (12 * 60 * 60)
	


void WriteCurrentStateToXML(char* pXMLFileName)
{
	char *pResponseString = NULL;
	char **ppEString = NULL;
	FILE *pXMLFileHandle;
	BuilderStatusStruct stBSS;
	stBSS.pCurStateString = NULL;
	stBSS.pCurStateTime = NULL;
	stBSS.pLastBuildResult = NULL;
	stBSS.pLastSuccessfullBuildTime = NULL;
	stBSS.pStatusSubString = NULL;
	stBSS.pStatusSubSubString = NULL;
	stBSS.pTimeInBuild = NULL;
	stBSS.iSecondsInBuild = 0;
	stBSS.iSecondsInCurrentState = 0;

	GetCurrentBuilderStatusStruct(&stBSS);

	ParserWriteXML(&pResponseString, parse_BuilderStatusStruct, &stBSS);

	pXMLFileHandle = fopen(pXMLFileName, "wt");
	fprintf(pXMLFileHandle, "%s", pResponseString);
	fclose(pXMLFileHandle);

	estrDestroy(&pResponseString);
}


void RelocateLogs( void )
{
/*	if (CheckConfigVarExistsAndTrue("USES_XBOX"))
	{
		FILE *pXboxLogFile = fopen(STACK_SPRINTF("%s\\XboxPrintfs.txt", GetCBLogDirectoryName()), "wt");
		if (pXboxLogFile)
		{
			char *pStr;
			int iSize;
			int iCounter;

			xboxAccessCapturedPrintfs(&pStr, &iSize, &iCounter);
			fprintf(pXboxLogFile, "%s", pStr);
			fclose(pXboxLogFile);
			xboxFinishedAccessingPrintfs();
		}
	}*/

	logCloseAllLogs();

	// Allways write out the ending variables after the script goes about changing stuff.
	BuildScripting_WriteCurrentVariablesToText(CBGetRootScriptingContext(), STACK_SPRINTF("%s\\VariablesEnd.txt", GetCBLogDirectoryName(NULL)));

//	PublishLogs();
	// Rename current log directory to this builds log directory at this point.

	//copy all files from current directory to new directory, then delete all files from old directory
//	MoveDirectoryContents(GetCBLogDirectoryName(), gpLastTimeStartedString);


}

void AddExFileSpecesToGimmeCmdLine(char **ppCmdLine, char *pVarName)
{
	char *pTempStr = NULL;
	estrPrintf(&pTempStr, "$%s$", pVarName);

	ReplaceConfigVarsInString(&pTempStr);

	if (!strchr(pTempStr, '$'))
	{
		char **ppNames = NULL;
		int i;

		DivideString(pTempStr, ", ", &ppNames, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		for (i=0; i < eaSize(&ppNames); i++)
		{
			estrConcatf(ppCmdLine, " -exFileSpec /bin/%s.exe -exFileSpec /bin/%s.pdb ", ppNames[i], ppNames[i]);
		}

		eaDestroyEx(&ppNames, NULL);
	}

	estrDestroy(&pTempStr);
}

void CheckForOnceADayBuild(void)
{
	SYSTEMTIME t = {0};
	U32 iCurTime = timeSecondsSince2000();
	timerLocalSystemTimeFromSecondsSince2000(&t, iCurTime);

	if(CheckConfigVarExistsAndTrue("FORCE_ONCE_A_DAY"))
	{
		gbIsOnceADayBuild = true;
		return;
	}
	

	if (t.wHour > 1 && t.wHour < 22)
	{
		gbIsOnceADayBuild = false;
		return;
	}

	if (iCurTime > gDynamicState.iLastOnceADayTime + 12 * 60 * 60)
	{
		gbIsOnceADayBuild = true;

	}
	else
	{
		gbIsOnceADayBuild = false;
	}
}


	
//during the main loop we are in our critical section everywhere EXCEPT during the system calls
void ContinuousBuilder_MainLoop(void)
{
	bool bResult;
//	char *pTempString = NULL; //estr
//	int iResult;
	EnterCriticalSection(&gCBCriticalSection);

	SetProductName(gpCBProduct->pProductName, gpCBProduct->pShortProductName);

	//if there are any log files hanging around in logs\currentbuild, move them to logs\aborted_curtime
//	RelocateAbortedLogs();

	siMainThreadID = GetCurrentThreadId();


	while (1)
	{
		SLEEP;

		//every once in a while, stop, purge old logs and stuff
		if (!gbFastTestingMode)
		{
			static U32 iNextPurgeTime = 0;
			if (timeSecondsSince2000_ForceRecalc() > iNextPurgeTime)
			{
				LeaveCriticalSection(&gCBCriticalSection);
				DoOldFilePurging();
				EnterCriticalSection(&gCBCriticalSection);
				iNextPurgeTime = timeSecondsSince2000_ForceRecalc() + 12 * 60 * 60;
			}
		}

			
		switch (geState)
		{
		case CBSTATE_INIT:
			if (BuildScripting_TotallyDisabled(CBGetRootScriptingContext()))
			{
				SetSubStateString("Scripting disabled because of LEAVE_CRASHES_UP_FOREVER");
				break;
			}

			giBuildRunCount++;

			CBErrorProcessing_SetDisableAll(false);
			

			giLastTimeStarted = timeSecondsSince2000_ForceRecalc();
			estrPrintf(&gpLastTimeStartedString, "%s", timeGetLocalDateStringFromSecondsSince2000(giLastTimeStarted));
			estrMakeAllAlphaNumAndUnderscores(&gpLastTimeStartedString);
			CBTiming_Begin(giLastTimeStarted);
			
			CB_SetState(CBSTATE_GETTING);
			
			siCompileRetryCount = 0;

//			xboxReboot(true);
//			eaDestroyEx(&gppXboxDumpFiles, NULL);

			ResetComments();

//			xboxResetPrintfCapturing();


			if (gConfig.pGlobalDumpLocation)
			{
				PurgeDirectoryOfOldFiles(gConfig.pGlobalDumpLocation, GLOBAL_DUMP_PURGE_AGE, NULL);
			}

			if (!gbFastTestingMode)
			{
				PurgeDirectoryOfOldFiles("c:\\core\\tools\\continuousbuilder\\webroot\\dumps", 7, NULL);
				PurgeDirectoryOfOldFiles("c:\\core\\tools\\continuousbuilder\\webroot\\custom\\dumps", 7, NULL);
				Gimme_UpdateFoldersToTime(timeSecondsSince2000(), "c:\\core\\tools\\continuousbuilder\\webroot", NULL, 600, "");
			}


			//first time through, don't reload config, because we just loaded it
			{
				static bool bFirst = true;
			
				if (bFirst)
				{
					bFirst = false;
				}
				else
				{
					char *pPauseString;
					int iPauseMinutes;

					if (sbDontPauseNextTime)
					{
						sbDontPauseNextTime = false;
					}
					else
					{
						pPauseString = GetConfigVar("PAUSE_BETWEEN_RUNS_MINUTES");
						if (pPauseString)
						{
							iPauseMinutes = atoi(pPauseString);
							if (iPauseMinutes)
							{
								int iCount;
								sbForceStopWaiting = false;
								CB_SetState(CBSTATE_WAITING);
								SetSubStateStringf("Waiting %d minutes before restarting", iPauseMinutes);
								
								for (iCount = 0; iCount < iPauseMinutes * 60; iCount++)
								{
									if (sbForceStopWaiting)
									{
										break;
									}
									LeaveCriticalSection(&gCBCriticalSection);
									Sleep(1000);
									EnterCriticalSection(&gCBCriticalSection);
								}

								CB_SetState(CBSTATE_GETTING);
							}
						}
					}

					CB_LoadConfig(false);


				}
			}
			break;

		case CBSTATE_PAUSED:
			LeaveCriticalSection(&gCBCriticalSection);
			Sleep(500);
			EnterCriticalSection(&gCBCriticalSection);
			
			if (timeSecondsSince2000_ForceRecalc() - giTimeEnteredState > 15 * 60)
			{
				gbPauseBetweenRuns = false;
			}
			
			if (!gbPauseBetweenRuns)
			{
				CB_SetState(CBSTATE_INIT);
			}
			break;

		


		case CBSTATE_GETTING:
			PossiblyPauseBetweenSteps();
			if (gbPauseBetweenRuns)
			{
				CB_SetState(CBSTATE_PAUSED);
				break;
			}

			//stick this up here so that variables like CBTYPE and so forth get set early in the run
			//for buildmonitor
			BuildScripting_ClearAndResetVariables(CBGetRootScriptingContext());

			CBReportToCBMonitor_BuildStarting();

			LeaveCriticalSection(&gCBCriticalSection);
			//always do a gimme get on c:\cryptic here
			bResult = false;

			AddComment(COMMENT_SUBSTATE, "Doing gimme get of c:/cryptic");

			CBTiming_Step("Gimme get of c:/cryptic", 0);


			if (!gbFastTestingMode)
			{
				bResult = system_w_timeout("gimme -glvfold c:/cryptic -nopause", NULL, 600);
			}
			

			EnterCriticalSection(&gCBCriticalSection);

		
			if (bResult)
			{
				RestartWithMessage("Gimme get of c:/cryptic failed ");
				break;
			}

			//what's the alternative to just having completed the process or started up?
			//unpausing or aborting due to failed getting or something like that
			//in those cases, we leave the error contexts as is
			if (gbJustCompletedProcessOrJustStartedUp)
			{
				char fileName[CRYPTIC_MAX_PATH];
				gbJustCompletedProcessOrJustStartedUp = false;


				estrPrintf(&pLastDescriptiveString, "%s", GetCurrentDescriptiveErrorString());
				estrClear(&pLastHTMLBugsString);

				GetAllBugsIntoHTML(&pLastHTMLBugsString, true);
				CB_SwitchErrorTrackerContexts();

				sprintf(fileName, "%s\\CBDynStatus.txt", GetCBDirectoryName());
				ParserWriteTextFile(fileName, parse_CBDynamicState, &gDynamicState, 0, 0);
			}

			CheckForOnceADayBuild();


			if (!gbNoSelfPatching)
			{
				CheckForNewerCBAndMaybeRestart();
			}

			if (!CBTypeIsCONT())
			{
				CB_SetState(CBSTATE_TESTING);
				break;
			}

			if (CheckConfigVar("DONT_GET"))
			{	
				CB_SetState(CBSTATE_COMPILING);
				break;
			}

			CB_SetScriptingVariablesFromConfig();

			
		

			FindSharedPrefix(&gpSharedSVNRoot, gConfig.ppSVNFolders);

			
			

			SVN_GetRevNumOfFolders(gpSharedSVNRoot, NULL, &gpSVNRepository, 30);

			


			printf("\n\n---------------------------------------------------------------------\nRestarting process - last time: %s\n---------------------------------------------------------------------\n\n\n", 
				StaticDefineIntRevLookup(enumCBResultEnum, gDynamicState.eLastResult));
			loadstart_printf("About to do SVN and gimme gets\n");

			SetSubStateString("Doing SVN get");
			AddComment(COMMENT_SUBSTATE, "Doing SVN Get");
			filelog_printf(GetCBLogFileName(NULL), "Doing SVN Get\n");

			CBTiming_Step("SVN Get", 0);


			PossiblyPauseBetweenSteps();

			LeaveCriticalSection(&gCBCriticalSection);
			bResult = SVN_UpdateFolders(gConfig.ppSVNFolders, 600);
			EnterCriticalSection(&gCBCriticalSection);

			if (!bResult)
			{
				LeaveCriticalSection(&gCBCriticalSection);
				if (!SVN_AttemptCleanup(gConfig.ppSVNFolders, 1200))
				{
					RestartWithMessage("Initial SVN Update Failed or timed out, cleanup also failed");
				}
				else
				{
					RestartWithMessage("Initial SVN Update Failed or timed out, cleanup attempted and succeeded");
				}

				loadend_printf("failed");
				break;
			}

			LeaveCriticalSection(&gCBCriticalSection);
			gCurRev.iSVNRev = SVN_GetRevNumOfFolders(NULL, gConfig.ppSVNFolders, NULL, 30);
			gCurRev.iGimmeTime = timeSecondsSince2000_ForceRecalc();
			EnterCriticalSection(&gCBCriticalSection);

			if (!gCurRev.iSVNRev || !gCurRev.iGimmeTime)
			{
				RestartWithMessage("Couldn't get SVN or Gimme rev num");
				loadend_printf("failed");
				break;
			}

			CBReportToCBMonitor_ReportSVNAndGimme(gCurRev.iSVNRev, gCurRev.iGimmeTime);

//this is as good a place as any to update our various lists of checkins for html display

			if (gDynamicState.lastCheckedInRev.iSVNRev)
			{
				CheckinInfo **ppTempSVNCheckinsInSuccessfulBuild = NULL;
				CheckinInfo **ppTempGimmeCheckinsInSuccessfulBuild = NULL;
				CheckinInfo **ppTempSVNCheckinsBeingTested = NULL;
				CheckinInfo **ppTempGimmeCheckinsBeingTested = NULL;



				LeaveCriticalSection(&gCBCriticalSection);


				SVN_GetCheckins(gDynamicState.lastCheckedInRev.iSVNRev - 100, gDynamicState.lastCheckedInRev.iSVNRev, NULL, gConfig.ppSVNFolders, NULL,
					&ppTempSVNCheckinsInSuccessfulBuild, 600, 0);


				Gimme_GetCheckinsBetweenTimes(gDynamicState.lastCheckedInRev.iGimmeTime - 60 * 60, gDynamicState.lastCheckedInRev.iGimmeTime, NULL, gConfig.ppGimmeFolders, 
					GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppTempGimmeCheckinsInSuccessfulBuild, 600);
		

				SVN_GetCheckins(gDynamicState.lastCheckedInRev.iSVNRev, gCurRev.iSVNRev, NULL, gConfig.ppSVNFolders, NULL, 
					&ppTempSVNCheckinsBeingTested, 600, 0);

				Gimme_GetCheckinsBetweenTimes(gDynamicState.lastCheckedInRev.iGimmeTime, gCurRev.iGimmeTime, NULL, gConfig.ppGimmeFolders, 
					GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppTempGimmeCheckinsBeingTested, 600);

				EnterCriticalSection(&gCBCriticalSection);

				eaDestroyStruct(&ppSVNCheckinsInSuccessfulBuild, parse_CheckinInfo);
				eaDestroyStruct(&ppGimmeCheckinsInSuccessfulBuild, parse_CheckinInfo);
				eaDestroyStruct(&ppSVNCheckinsBeingTested, parse_CheckinInfo);
				eaDestroyStruct(&ppGimmeCheckinsBeingTested, parse_CheckinInfo);

				ppSVNCheckinsInSuccessfulBuild = ppTempSVNCheckinsInSuccessfulBuild;
				ppGimmeCheckinsInSuccessfulBuild = ppTempGimmeCheckinsInSuccessfulBuild;
				ppSVNCheckinsBeingTested = ppTempSVNCheckinsBeingTested;
				ppGimmeCheckinsBeingTested = ppTempGimmeCheckinsBeingTested;
			}


			CB_LogCheckPoint("SVN get complete");


			SetSubStateString("Doing Gimme get");
			AddComment(COMMENT_SUBSTATE, "Doing Gimme Get");

			CBTiming_Step("Gimme get", 0);

			filelog_printf(GetCBLogFileName(NULL), "Doing Gimme get\n");
			PossiblyPauseBetweenSteps();


			{
				char *pExtraGimmeCmdLine = NULL;
				estrPrintf(&pExtraGimmeCmdLine, "-noSkipBins 0");
		
				
				AddExFileSpecesToGimmeCmdLine(&pExtraGimmeCmdLine, "PROJECTEXES");
				AddExFileSpecesToGimmeCmdLine(&pExtraGimmeCmdLine, "COREEXES");
				AddExFileSpecesToGimmeCmdLine(&pExtraGimmeCmdLine, "COREX64EXES");
				AddExFileSpecesToGimmeCmdLine(&pExtraGimmeCmdLine, "PROJECTX64EXES");
//				AddExFileSpecesToGimmeCmdLine(&pExtraGimmeCmdLine, "PROJECTXBOXEXES");
				LeaveCriticalSection(&gCBCriticalSection);
		
				bResult = Gimme_UpdateFoldersToTime(gCurRev.iGimmeTime, NULL, gConfig.ppGimmeFolders, 1200, pExtraGimmeCmdLine);
				EnterCriticalSection(&gCBCriticalSection);
				estrDestroy(&pExtraGimmeCmdLine);
			}

			if (!bResult)
			{
				RestartWithMessage("Gimme Update Failed or timed out");
				loadend_printf("failed");
				break;
			}


		

			//if last time succeededcheck if there were any checkins... if not, sleep for 5 seconds and get again
			if (gDynamicState.eLastResult == CBRESULT_SUCCEEDED || gDynamicState.eLastResult == CBRESULT_SUCCEEDED_W_ERRS && gDynamicState.lastCheckedInRev.iSVNRev)
			{
				CheckinInfo **ppSVNCheckins = NULL;
				CheckinInfo **ppGimmeCheckins = NULL;
				bool bFailed = false;

				LeaveCriticalSection(&gCBCriticalSection);
				if (!SVN_GetCheckins(gDynamicState.lastCheckedInRev.iSVNRev, gCurRev.iSVNRev, NULL, gConfig.ppSVNFolders, NULL, &ppSVNCheckins, 60, 0))
				{
					SendEmailToAdministrators("SVN_GetCheckins failed");
					bFailed = true;
				}

				if (!Gimme_GetCheckinsBetweenTimes(gDynamicState.lastCheckedInRev.iGimmeTime, gCurRev.iGimmeTime, NULL, gConfig.ppGimmeFolders, GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppGimmeCheckins, 600))
				{
					SendEmailToAdministrators("Gimme_GetCheckins failed");
					bFailed = true;
				}

				EnterCriticalSection(&gCBCriticalSection);

				if (bFailed)
				{
					CB_SetState(CBSTATE_INIT);
				}
				else if (eaSize(&ppSVNCheckins) + eaSize(&ppGimmeCheckins) != 0)
				{
					//if we succeeded last frame and have only gimme checkins, don't recompile
					if (eaSize(&ppSVNCheckins) == 0 && giBuildRunCount != 1)
					{
						filelog_printf(GetCBLogFileName(NULL), "No SVN checkins... skipping compile\n");
						CB_SetState(CBSTATE_TESTING);
					}
					else
					{
						CB_SetState(CBSTATE_COMPILING);
					}
				}
				else if (giBuildRunCount != 1)
				{
					filelog_printf(GetCBLogFileName(NULL), "No checkins... (run count %d) sleeping for 5 seconds, then starting over\n", giBuildRunCount);

//when we're doing our trivial restart, we leave lastRev what it was. This hopefully deals with a nasty
//gimme atomicity case where a checkin at time n-1 isn't found in the checkin list immediately at time n,
//but then isn't between time n and time n+10 either.
//					gDynamicState.lastRev.iGimmeRev = gCurRev.iGimmeRev;
//					gDynamicState.lastRev.iSVNRev = gCurRev.iSVNRev;
					Sleep(5000);
					CB_SetState(CBSTATE_INIT);
					CBTiming_End(false);
					RelocateAbortedLogs();
					CBReportToCBMonitor_BuildEnded(CBRESULT_ABORTED);
				}
				else
				{
					CB_SetState(CBSTATE_COMPILING);
				}


				eaDestroyStruct(&ppSVNCheckins, parse_CheckinInfo);
				eaDestroyStruct(&ppGimmeCheckins, parse_CheckinInfo);
			}
			else
			{
				CB_SetState(CBSTATE_COMPILING);
			}
			loadend_printf("Gimme Time: %u(%s) SVN rev: %u", gCurRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gCurRev.iGimmeTime), gCurRev.iSVNRev);


			break;
			
		case CBSTATE_COMPILING:
			{
				int iBuildResult = 0;
				char *pCompileScriptName;

				//doing this here ensures that all these variables will be set on the buildermonitor as early as possible (some of this
				//is redundant, but that's OK)
				BuildScripting_AddStartingVariables(CBGetRootScriptingContext());

				if (CheckConfigVar("DONT_COMPILE"))
				{
					CB_SetState(CBSTATE_TESTING);
					break;
				}

				PossiblyPauseBetweenSteps();


				if (gbNeedFullCompile && !CheckConfigVar("NO_FULL_COMPILE"))
				{
					filelog_printf(GetCBLogFileName(NULL), "Doing FULL (clean) compile\n");
					pCompileScriptName = gConfig.continuousConfig.pFullCompileScript;
				}
				else
				{
					filelog_printf(GetCBLogFileName(NULL), "Doing normal compile\n");
					pCompileScriptName = gConfig.continuousConfig.pCompileScript;
				}

				CBTiming_Step("Compile Script", 0);
				
				StartBuildScripting(pCompileScriptName, 1);
			

				while (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_RUNNING)
				{
					LeaveCriticalSection(&gCBCriticalSection);
					BuildScripting_Tick(CBGetRootScriptingContext());
					SLEEP;
					EnterCriticalSection(&gCBCriticalSection);

					if (!BuildScripting_CurrentCommandSetsCBStringsByItself(CBGetRootScriptingContext()))
					{
						SetSubStateString(BuildScripting_GetCurStateString(CBGetRootScriptingContext()));
					}
				}


				if (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_SUCCEEDED)
				{
					gbNeedFullCompile = false;
					CB_SetState(CBSTATE_TESTING);
				}
				else
				{
					static char *pFakeError = NULL;

					char *pLogFileName = BuildScripting_GetMostRecentSystemOutputFilename(CBGetRootScriptingContext());

					switch (BuildScripting_GetExtendedErrors("COMPILE", pLogFileName, &pFakeError))
					{
					case EXTENDEDERROR_NOTANERROR:
						{	
							char *pErrorMessage = NULL;
							estrPrintf(&pErrorMessage, "The %s continuous builder encountered \"fake\" compile error <<%s>> and may need your attention", GetCBName(), pFakeError);

							RestartWithMessage(pErrorMessage);

							estrDestroy(&pErrorMessage);
							break;
						}
					case EXTENDEDERROR_FATAL:
						gbNeedFullCompile = true;
						BuildIsBad(true);
						StartOver_Failed();
						break;
					case EXTENDEDERROR_RETRY:
						siCompileRetryCount++;
						if (siCompileRetryCount < 3)
						{
							filelog_printf(GetCBLogFileName(NULL), "Got RETRY compile error message <<%s>>. Restarting compile", pFakeError);
							CB_SetState(CBSTATE_COMPILING);
						}
						else
						{
							char *pErrorMessage = NULL;
							estrPrintf(&pErrorMessage, "The %s continuous builder encountered RETRY compile error <<%s>> and retried 3 times, may need your attention", GetCBName(), pFakeError);
							RestartWithMessage(pErrorMessage);
							estrDestroy(&pErrorMessage);
						}
						break;

					}
				}
				break;
			}

		case CBSTATE_TESTING:
			{
				int iRunResult = 0;
				char fileName[CRYPTIC_MAX_PATH];
				ErrorTrackerEntryList *pErrorList;
	
				PossiblyPauseBetweenSteps();

				if (CBTypeIsCONT())
				{
					CBTiming_Step("Test Script", 0);
					StartBuildScripting(gConfig.continuousConfig.pTestScript, 1);
				}
				else
				{
					StartBuildScripting(gConfig.pScript, 0);
				}

				// Allways write out the starting variables before the script goes about changing stuff.

				sprintf(fileName, "%s\\VariablesStart.txt", GetCBLogDirectoryName(NULL));
				BuildScripting_WriteCurrentVariablesToText(CBGetRootScriptingContext(), fileName);

				while (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_RUNNING)
				{
					LeaveCriticalSection(&gCBCriticalSection);
					BuildScripting_Tick(CBGetRootScriptingContext());
					SLEEP;
					EnterCriticalSection(&gCBCriticalSection);

					if (!BuildScripting_CurrentCommandSetsCBStringsByItself(CBGetRootScriptingContext()))
					{
						SetSubStateString(BuildScripting_GetCurStateString(CBGetRootScriptingContext()));
					}
				}

				switch (BuildScripting_GetState(CBGetRootScriptingContext()))
				{
				case BUILDSCRIPTSTATE_SUCCEEDED:
					iRunResult = 0;
					break;

				case BUILDSCRIPTSTATE_SUCCEEDED_WITH_ERRORS:
					iRunResult = -2;
					break;

				case BUILDSCRIPTSTATE_ABORTED:
					iRunResult = -3;
					break;

				default:
					iRunResult = -1;
				}

				// TODO both of these calls are for blaming
				//errorTrackerLibUpdateBlameCache();
				//errorTrackerLibFlush();

				if (iRunResult == -3)
				{
					StartOver_Aborted();
					break;
				}

		

				pErrorList = CB_GetCurrentEntryList();

				if (objCountTotalContainersWithType(pErrorList->eContainerType))
				{
					if (!ErrorListIsErrorsOnly(pErrorList))
					{
						iRunResult = -1;
					}
					else if (iRunResult == 0)
					{
						iRunResult = -2;
					}
				}

	

				if (iRunResult == 0)
				{
					eCurResult = CBRESULT_SUCCEEDED;
					CB_SetState(CBSTATE_TESTING_NONFATAL);
				}
				else
				{
//					CheckForNewXboxDumps();

					if (iRunResult == -1 && gConfig.continuousConfig.pBadCheckinScript)
					{
						DoBadCheckinStuff();
					}
						


					if (iRunResult == -2)
					{
						eCurResult = CBRESULT_SUCCEEDED_W_ERRS;
						CB_SetState(CBSTATE_TESTING_NONFATAL);
					}
					else
					{
						BuildIsBad(true);
						StartOver_Failed();
					}
				}
					
			}
			break;

	case CBSTATE_TESTING_NONFATAL:
			{
				int iRunResult = 0;
			
				PossiblyPauseBetweenSteps();


				if (gConfig.continuousConfig.pNonFatalTestScript && gConfig.continuousConfig.pNonFatalTestScript[0])
				{
					CBTiming_Step("Non-fatal test Script", 0);
					StartBuildScripting(gConfig.continuousConfig.pNonFatalTestScript, 1);			

					while (BuildScripting_GetState(CBGetRootScriptingContext()) == BUILDSCRIPTSTATE_RUNNING)
					{
						LeaveCriticalSection(&gCBCriticalSection);
						BuildScripting_Tick(CBGetRootScriptingContext());
						SLEEP;
						EnterCriticalSection(&gCBCriticalSection);

						if (!BuildScripting_CurrentCommandSetsCBStringsByItself(CBGetRootScriptingContext()))
						{
							SetSubStateString(BuildScripting_GetCurStateString(CBGetRootScriptingContext()));
						}
					}

					switch (BuildScripting_GetState(CBGetRootScriptingContext()))
					{
					case BUILDSCRIPTSTATE_SUCCEEDED:
						iRunResult = 0;
						break;

					case BUILDSCRIPTSTATE_SUCCEEDED_WITH_ERRORS:
						iRunResult = -2;
						break;

					case BUILDSCRIPTSTATE_ABORTED:
						assertmsgf(0, "A non-fatal CB script tried to abort the build process. This makes no sense");
						break;

					default:
						iRunResult = -2;
					}


					// TODO more blaming stuff that needs to be updated
					//errorTrackerLibUpdateBlameCache();
					//errorTrackerLibFlush();
				}

		
				//make sure not to forget if we had errors back during the main run
				if (iRunResult == 0 && eCurResult == CBRESULT_SUCCEEDED_W_ERRS)
				{
					iRunResult = -2;
				}


				if (iRunResult == 0)
				{
					if (gDynamicState.eLastResult == CBRESULT_FAILED || gDynamicState.eLastResult == CBRESULT_SUCCEEDED_W_ERRS)
					{
						SendAllClearEmail();
					}

					eCurResult = CBRESULT_SUCCEEDED;
					CB_SetState(CBSTATE_CHECKIN);

				}
				else
				{
//					CheckForNewXboxDumps();
					BuildIsBad(false);



					eCurResult = CBRESULT_SUCCEEDED_W_ERRS;
					CB_SetState(CBSTATE_CHECKIN);
				
	
				}
					
			}
			break;


		case CBSTATE_CHECKIN:

			PossiblyPauseBetweenSteps();

			CBTiming_Step("Checkin", 0);
			DoCheckinStuff(eCurResult == CBRESULT_SUCCEEDED_W_ERRS);

			if (eCurResult == CBRESULT_SUCCEEDED_W_ERRS)
			{
				StartOver_SucceededWithErrors();
			}
			else
			{
				StartOver_Succeeded();
			}

			RelocateLogs();

			break;

		case CBSTATE_DONE_SUCCEEDED:
		case CBSTATE_DONE_SUCCEEDED_W_ERRORS:
		case CBSTATE_DONE_FAILED:
		case CBSTATE_DONE_ABORTED:
			PossiblyPauseBetweenSteps();
	
			
			if ((CBTypeIsCONT() || gConfig.bLooping) && !gConfig.bQuitOnScriptCompletion && timeSecondsSince2000_ForceRecalc() - giTimeEnteredState > 30)
			{
				CB_SetState(CBSTATE_INIT);
			}

			if (gConfig.bQuitOnScriptCompletion && timeSecondsSince2000_ForceRecalc() - giTimeEnteredState > 30)
			{
				exit(0);
			}
			break;



		}

		LeaveCriticalSection(&gCBCriticalSection);
		SLEEP;
		EnterCriticalSection(&gCBCriticalSection);
	}

}






void ContinuousBuilderWindowTick(HWND hDlg)
{
	char lastResultString[256];
	char curStateString[256];
	char lastGoodString[1024];
	char lastCheckedInString[1024];
	U32 iTimeInState;

	static bool bInside = 0;
	static int frameTimer = 0;


	EnterCriticalSection(&gCBCriticalSection);


	if (!bInside)
	{
		if (!frameTimer)
		{
			frameTimer = timerAlloc();
		}


		utilitiesLibOncePerFrame(timerElapsedAndStart(frameTimer), 1.0f);
		smtpBgThreadingOncePerFrame();


		bInside = true;
		sprintf(lastResultString, "Last Build Result: %s", StaticDefineIntRevLookup(enumCBResultEnum,gDynamicState.eLastResult));
		SetTextFast(GetDlgItem(hDlg, IDC_LASTRESULT), lastResultString);

		iTimeInState = timeSecondsSince2000_ForceRecalc() - giTimeEnteredState;

		if (!CBTypeIsCONT() && BuildScripting_IsRunning(CBGetRootScriptingContext()))
		{
			sprintf(curStateString, "%s\n", BuildScripting_GetCurDisplayString(CBGetRootScriptingContext()));
		}
		else if (geState == CBSTATE_TESTING)
		{
			sprintf(curStateString, "Current State: %s (%s) (%d:%02d)", StaticDefineIntRevLookup(enumCBStateEnum, geState),
				sCurTestTitle, iTimeInState / 60, iTimeInState % 60);
		}
		else
		{
			sprintf(curStateString, "Current State: %s (%d:%02d)", StaticDefineIntRevLookup(enumCBStateEnum, geState),
				iTimeInState / 60, iTimeInState % 60);
		}
		SetTextFast(GetDlgItem(hDlg, IDC_CURSTATE), curStateString);

		if (gDynamicState.lastGoodRev.iSVNRev)
		{

			sprintf(lastGoodString, "Last Good Build: SVN revision %d, Gimme %u(%s)",
				gDynamicState.lastGoodRev.iSVNRev, gDynamicState.lastGoodRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastGoodRev.iGimmeTime));

		}
		else
		{
			sprintf(lastGoodString, "Last Good Build: None");
		}
		SetTextFast(GetDlgItem(hDlg, IDC_LASTGOODBUILD), lastGoodString);

		if (gDynamicState.lastCheckedInRev.iSVNRev)
		{

			sprintf(lastCheckedInString, "Last Checked In: SVN revision %d, Gimme %u(%s)",
				gDynamicState.lastCheckedInRev.iSVNRev, gDynamicState.lastCheckedInRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastCheckedInRev.iGimmeTime));

		}
		else
		{
			sprintf(lastCheckedInString, "Last Checked In: None");
		}
		SetTextFast(GetDlgItem(hDlg, IDC_LASTCHECKEDIN), lastCheckedInString);

		SetTextFast(GetDlgItem(hDlg, IDC_LASTERROR), pLastDescriptiveString);

		SetTextFast(GetDlgItem(hDlg, IDC_CURRENTERRORS), GetCurrentDescriptiveErrorString());

		if (gbCurrentlyPausedBetweenSteps)
		{
			char *pTempStr = NULL;
			estrPrintf(&pTempStr, "PAUSED - %s", GetSubStateString() ? GetSubStateString() : "");
			SetTextFast(GetDlgItem(hDlg, IDC_SUBSTATE), pTempStr);
			estrDestroy(&pTempStr);
		}
		else
		{
			SetTextFast(GetDlgItem(hDlg, IDC_SUBSTATE), GetSubStateString() ? GetSubStateString() : "");
		}
		SetTextFast(GetDlgItem(hDlg, IDC_SUBSUBSTATE), GetSubSubStateString() ? GetSubSubStateString() : "");

		CBReportToCBMonitor_Update();
		commMonitor(commDefault());
		ContinuousBuilder_MainLoopEveryFrame();
		bInside = false;

		//start status reporting as soon as we get a real host name
		if (!gConfig.bDev && gConfig.pWhereToSendStatusReports && gConfig.pWhereToSendStatusReports[0])
		{	
			static bool bStarted = false;

			if (!bStarted)
			{
				const char *pHostName = getHostName();

				if (stricmp(pHostName, "Error getting hostname") != 0)
				{
					BeginStatusReporting(pHostName, gConfig.pWhereToSendStatusReports, 80);
					bStarted = true;
				}
				else
				{
					printf("ERROR: can't get host name, thus can't start status reporting\n");
				}
			}
		}
	}

	LeaveCriticalSection(&gCBCriticalSection);

	if (sBCommentsChanged)
	{
		char *pCommentsString = NULL;
		int i;

		EnterCriticalSection(&gCBCommentsCriticalSection);
		
		for (i=0; i < eaSize(&sppComments); i++)
		{
			estrConcatf(&pCommentsString, "%s: %s\r\n", 
				timeGetLocalTimeStringFromSecondsSince2000(sppComments[i]->iTime), sppComments[i]->pStr);
		}

		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_ACTIVITY), pCommentsString);

		estrDestroy(&pCommentsString);

		sBCommentsChanged = false;
		LeaveCriticalSection(&gCBCommentsCriticalSection);
	}

	SLEEP;
}







BOOL CALLBACK continuousBuilderDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	switch (iMsg)
	{

	case WM_INITDIALOG:
		ghCBDlg = hDlg;
		
		SetTimer(hDlg, 0, 10, NULL);
		ContinuousBuilderWindowTick(hDlg);

		SetWindowText_UTF8((HWND)GetDlgItem(hDlg, IDC_COMMENT), GetBuilderComment());

		//setup fonts
		{
			
			static HFONT hf = 0;
			HDC hdc;
			long lfHeight;
			RECT windowRect;
	
			hdc = GetDC(NULL);
			lfHeight = -MulDiv(15, GetDeviceCaps(hdc, LOGPIXELSY), 72);

			hf = CreateFont(lfHeight, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, L"Times New Roman");

			SendMessage((HWND)GetDlgItem(hDlg, IDC_LASTRESULT), (UINT)WM_SETFONT, (WPARAM)hf, (WPARAM)true);
			SendMessage((HWND)GetDlgItem(hDlg, IDC_CURSTATE), (UINT)WM_SETFONT, (WPARAM)hf, (WPARAM)true);
			

			lfHeight = -MulDiv(13, GetDeviceCaps(hdc, LOGPIXELSY), 72);
			hf = CreateFont(lfHeight, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, L"Times New Roman");

			SendMessage((HWND)GetDlgItem(hDlg, IDC_SUBSTATE), (UINT)WM_SETFONT, (WPARAM)hf, (WPARAM)true);

			GetWindowRect(hDlg, &windowRect);
			SetWindowPos(hDlg, HWND_NOTOPMOST, windowRect.left, windowRect.top, windowRect.right - windowRect.left, 
				windowRect.bottom - windowRect.top, 0);


			ReleaseDC(NULL, hdc);

	
		}



		return true; 



	case WM_TIMER:
		ContinuousBuilderWindowTick(hDlg);
		CheckDlgButton(hDlg, IDC_PAUSE_BETWEEN_RUNS, gbPauseBetweenRuns?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_PAUSE_BETWEEN_STEPS, gbPauseBetweenSteps?BST_CHECKED:BST_UNCHECKED);

		return true;

	case WM_CLOSE:
		if (CBGetRootScriptingContext() && BuildScripting_IsRunning(CBGetRootScriptingContext()))
		{
			BuildScripting_AbortAndCloseInstantly(CBGetRootScriptingContext());
			Sleep(10000);
			EndDialog(hDlg, 0);
			exit(0);		
		}
		else
		{
			EndDialog(hDlg, 0);
			exit(0);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_ABORT:
			sbDontPauseNextTime = true;

			if (geState == CBSTATE_WAITING)
			{
				sbForceStopWaiting = true;
			}
			BuildScripting_ForceAbort(CBGetRootScriptingContext());
			break;


		case IDC_PAUSE_BETWEEN_RUNS:
			if (HIWORD(wParam) ==  BN_CLICKED)
			{
				gbPauseBetweenRuns = IsDlgButtonChecked(hDlg, IDC_PAUSE_BETWEEN_RUNS);
			}
			break;		
		case IDC_PAUSE_BETWEEN_STEPS:
			if (HIWORD(wParam) ==  BN_CLICKED)
			{
				gbPauseBetweenSteps = IsDlgButtonChecked(hDlg, IDC_PAUSE_BETWEEN_STEPS);
			}
			break;

		case IDC_UPDATE_COMMENT:
			{
				char *pText = NULL;
				FILE *pOutFile;
				estrGetWindowText(&pText, (HWND)GetDlgItem(hDlg, IDC_COMMENT));

				pOutFile = fopen("c:\\continuousBuilder\\comment.txt", "wt");
				if (pOutFile)
				{
					fprintf(pOutFile, "%s", pText);
				}

				fclose(pOutFile);
				estrDestroy(&pText);

				CBReportToCBMonitor_ReportBuildComment();


			}
			break;


		}
		break;


	}

	
	return false;
}

void ContinuousBuilderHandleMsg(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	char *pTempStr;
	char *pContextName = NULL;
	BuildScriptingContext *pCurContext = NULL;

	switch(cmd)
	{
	xcase TO_CONTINUOUSBUILDER_SUBSTATE:
		pTempStr = pktGetStringTemp(pak);
		if (!pktEnd(pak))
		{
			pContextName = pktGetStringTemp(pak);
		}

		SetSubStateString(pTempStr);
		filelog_printf(GetCBScriptingLogFileName(NULL), "Received testing message %s\n", GetSubStateString());
		if (strstri(GetSubStateString(), "Command execution failed:"))
		{
			ErrorData data = {0};									
			data.eType = ERRORDATATYPE_FATALERROR;						
			data.pErrorString = (char*)GetSubStateString();
			CB_ProcessErrorData(NULL, &data);
		}

		AddComment(COMMENT_SUBSTATE, "New Substate: %s", pTempStr);

		if (pContextName)
		{
			pCurContext = BuildScripting_FindDescendantContextByName(CBGetRootScriptingContext(), pContextName);	
		}
		else
		{
			pCurContext = BuildScripting_FindDescendantContextThatIsExpectingCBComments(CBGetRootScriptingContext());
		}

		if (CBTiming_Active())
		{
			if (pCurContext)
			{
				CBTiming_StepEx(pTempStr, BuildScripting_GetContextName(pCurContext), BuildScripting_GetCurTimingDepth(pCurContext) + 1, false, false);
			}
			else
			{
				CBTiming_Step(pTempStr, BuildScripting_GetCurTimingDepth(CBGetRootScriptingContext()) + 1);
			}
		}

		BuildScripting_CheckForTimeoutReset(pCurContext ? pCurContext : CBGetRootScriptingContext());
		
	xcase TO_CONTINUOUSBUILDER_SUBSUBSTATE:


		pTempStr = pktGetStringTemp(pak);
		if (!pktEnd(pak))
		{
			pContextName = pktGetStringTemp(pak);
		}

		SetSubSubStateString(pTempStr);
		filelog_printf(GetCBScriptingLogFileName(NULL), "Received testing message %s\n", GetSubSubStateString());
		if (strstri(GetSubSubStateString(), "Command execution failed:"))
		{
			ErrorData data = {0};									
			data.eType = ERRORDATATYPE_ERROR;						
			data.pErrorString = (char*)GetSubSubStateString();			
			CB_ProcessErrorData(NULL, &data);
		}
		
		if (pContextName)
		{
			pCurContext = BuildScripting_FindDescendantContextByName(CBGetRootScriptingContext(), pContextName);	
		}
		else
		{
			pCurContext = BuildScripting_FindDescendantContextThatIsExpectingCBComments(CBGetRootScriptingContext());
		}

		if (CBTiming_Active())
		{
			if (pCurContext)
			{
				CBTiming_StepEx(pTempStr, BuildScripting_GetContextName(pCurContext), BuildScripting_GetCurTimingDepth(pCurContext) + 2, false, false);
			}
			else
			{
				CBTiming_Step(pTempStr, BuildScripting_GetCurTimingDepth(CBGetRootScriptingContext()) + 2);
			}
		}

		BuildScripting_CheckForTimeoutReset(pCurContext ? pCurContext : CBGetRootScriptingContext());


	xcase TO_CONTINUOUSBUILDER_COMMENT:
		pTempStr = pktGetStringTemp(pak);
		if (!pktEnd(pak))
		{
			pContextName = pktGetStringTemp(pak);
		}

		filelog_printf(GetCBScriptingLogFileName(NULL), "Received comment: %s\n", pTempStr);
		AddComment(COMMENT_COMMENT, "%s", pTempStr);

		if (pContextName)
		{
			pCurContext = BuildScripting_FindDescendantContextByName(CBGetRootScriptingContext(), pContextName);	
		}
		else
		{
			pCurContext = BuildScripting_FindDescendantContextThatIsExpectingCBComments(CBGetRootScriptingContext());
		}

		if (CBTiming_Active())
		{
			if (pCurContext)
			{
				CBTiming_CommentEx(pTempStr, BuildScripting_GetContextName(pCurContext));
			}
			else
			{
				CBTiming_Comment(pTempStr);
			}
		}

		
		BuildScripting_CheckForTimeoutReset(pCurContext ? pCurContext : CBGetRootScriptingContext());

	xcase TO_CONTINUOUSBUILDER_STOP_ERRORTRACKING:
		CBErrorProcessing_SetDisableAll(true);

	xcase TO_CONTINUOUSBUILDER_START_ERRORTRACKING:
		CBErrorProcessing_SetDisableAll(false);

	xcase TO_CONTINUOUSBUILDER_PAUSE_TIMEOUT:
		{
			U32 iNumSeconds = pktGetBits(pak, 32);
			if (!pktEnd(pak))
			{
				pContextName = pktGetStringTemp(pak);
				pCurContext = BuildScripting_FindDescendantContextByName(CBGetRootScriptingContext(), pContextName);
			}
			else
			{
				pCurContext = CBGetRootScriptingContext();
			}

			if (pCurContext)
			{
				BuildScripting_AddTimeBeforeFailure(pCurContext, iNumSeconds);
			}
		}
	}
}

void SendSingleBuildScriptingVar(NetLink *link, char *pVarName)
{
	char *pVarValue = NULL;
	char varName[256];

	sprintf(varName, "$%s$", pVarName);

	if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), varName, &pVarValue))
	{		
		httpSendStr(link, pVarValue);
	}
	else
	{
		httpSendFileNotFoundError(link, "UNKNOWN VARIABLE");
	}

	estrDestroy(&pVarValue);



}


void SendBuildScriptingVars(NetLink *link, bool bComments)
{
	char *pEString = NULL;
	estrConcatf(&pEString, "<pre>\n");
	BuildScripting_DumpAllVariables(CBGetRootScriptingContext(), &pEString, bComments);
	estrConcatf(&pEString, "</pre>\n");

	errorTrackerLibSendWrappedString(link, pEString);	

	estrDestroy(&pEString);
}

void SendFile(NetLink *link, char *pFileName, bool bNeedHTMLEscaping)
{
	int iSize;

	char *pBuf = fileAlloc(pFileName, &iSize);
	if (pBuf)
	{
		char *pEString = NULL;
		int i;

		//log files sometimes end up with NULLs inside them. Replace these all with spaces.
		for (i=0; i < iSize - 1; i++)
		{
			if (pBuf[i] == 0)
			{
				pBuf[i] = ' ';
			}
		}
		
		//special case... if there are no newlines, and at least one <br />, then
		//it's probably already html, so don't HTML-ify it
		if (!strchr(pBuf, '\n') && strstr(pBuf, "<br />"))
		{
			estrCopy2(&pEString, pBuf);
		}
		else
		{

			estrConcatf(&pEString, "<pre>\n");
			if (bNeedHTMLEscaping)
			{
				estrCopyWithHTMLEscaping(&pEString, pBuf, true);
			}
			else
			{
				estrConcatf(&pEString, "%s", pBuf);
			}
			estrConcatf(&pEString, "</pre>\n");
		}

		errorTrackerLibSendWrappedString(link, pEString);	

		estrDestroy(&pEString);

		free(pBuf);
	}
	else
	{
		char *pEString = NULL;
		estrPrintf(&pEString, "<pre>File %s is empty</pre>", pFileName);
		errorTrackerLibSendWrappedString(link, pEString);
		estrDestroy(&pEString);
	}
}

void SendLogFile(NetLink *link, char *pStartTimeString, char *pFName, char *pBuildType, char *pProductName)
{
	char actualFileName[CRYPTIC_MAX_PATH];

	
	sprintf(actualFileName, "%s\\logs\\%s\\%s", GetCBDirectoryNameWithBuildTypeAndProductName(pBuildType, pProductName), pStartTimeString, pFName);
	

	SendFile(link, actualFileName, strEndsWith(pFName, ".html") ? false : true); 
}

void SendLogFileDir(NetLink *link, char *pStartTimeString, char *pSubdirPath, char *pBuildType, char *pProductName)
{
	char directoryName[CRYPTIC_MAX_PATH];
//	char systemString[1024];
	char *pOutString = NULL;
	int i, j;
	char **ppFiles = NULL;
	char **ppDirs = NULL;
	char typeAndProductString[256];

	sprintf(typeAndProductString, "&buildType=%s&productName=%s", pBuildType, pProductName);

	if (pSubdirPath)
	{
		sprintf(directoryName, "%s\\logs\\%s\\%s", GetCBDirectoryNameWithBuildTypeAndProductName(pBuildType, pProductName), pStartTimeString, pSubdirPath);

		if (!dirExists(directoryName) && stricmp_safe(pStartTimeString, gpLastTimeStartedString) == 0)
		{
			sprintf(directoryName, "%s\\%s", GetCBLogDirectoryNameWithBuildTypeAndProductName(pBuildType, pProductName), pSubdirPath);
		}
	}
	else
	{
		sprintf(directoryName, "%s\\logs\\%s", GetCBDirectoryNameWithBuildTypeAndProductName(pBuildType, pProductName), pStartTimeString);

		if (!dirExists(directoryName) && stricmp_safe(pStartTimeString, gpLastTimeStartedString) == 0)
		{
			sprintf(directoryName, "%s", GetCBLogDirectoryNameWithBuildTypeAndProductName(pBuildType, pProductName));
		}
	}

	ppFiles = fileScanDirFoldersNoSubdirRecurse(directoryName, FSF_FILES | FSF_RETURNLOCALNAMES);
	ppDirs = fileScanDirFoldersNoSubdirRecurse(directoryName, FSF_FOLDERS | FSF_RETURNSHORTNAMES);


/*	if (!pBuf)
	{
		httpSendFileNotFoundError(link, "COULDN'T READ DIRECTORY");
		return;
	}


	if (!ppNames)
	{
		httpSendFileNotFoundError(link, "COULDN'T READ DIRECTORY");
		return;
	}

	for (i=0; i < eaSize(&ppNames); i++)
	{
		estrConcatf(&pOutString, "<div><a href = \"/logfile?buildStartTime=%s&fname=%s\">%s</a></div>\n",
			pStartTimeString, ppNames[i], ppNames[i]);
	}

	errorTrackerLibSendWrappedString(link, pOutString);*/
	
	for (i=0; i < eaSize(&ppFiles); i++)
	{
		estrConcatf(&pOutString, "<div>");
	
		for (j=0; j < eaSize(&ppDirs); j++)
		{
			if (strStartsWith(ppFiles[i], ppDirs[j]))
			{
				estrConcatf(&pOutString, "<a href = \"/logs?buildStartTime=%s&subdir=%s%s%s%s\">(Logs from launched stuff)</a> ",
					pStartTimeString, pSubdirPath ? pSubdirPath : "", pSubdirPath ? "/" : "", ppDirs[j], typeAndProductString);
				free(ppDirs[j]);
				eaRemoveFast(&ppDirs, j);
				break;
			}
		}

		if (pSubdirPath)
		{
			estrConcatf(&pOutString, "<a href = \"/logfile?buildStartTime=%s&fname=%s/%s%s\">%s</a></div>\n",
				pStartTimeString, pSubdirPath, ppFiles[i], typeAndProductString, ppFiles[i]);
		}
		else
		{
			estrConcatf(&pOutString, "<a href = \"/logfile?buildStartTime=%s&fname=%s%s\">%s</a></div>\n",
				pStartTimeString, ppFiles[i], typeAndProductString, ppFiles[i]);
		}

	}

	for (i = 0; i < eaSize(&ppDirs); i++)
	{
		estrConcatf(&pOutString, "<div><a href = \"/logs?buildStartTime=%s&subdir=%s%s%s%s\">Subdir %s</a></div> ",
			pStartTimeString, pSubdirPath ? pSubdirPath : "", pSubdirPath ? "/" : "", ppDirs[i], typeAndProductString, ppDirs[i]);
	}

	errorTrackerLibSendWrappedString(link, pOutString);

	fileScanDirFreeNames(ppFiles);
	fileScanDirFreeNames(ppDirs);
	estrDestroy(&pOutString);
}










void SendCurrentSVNCheckins(NetLink *link)
{
	static char *pCheckinString = NULL;
	static int iCurSvnRev_LastTime = 0;
	int i;

	if ((U32)iCurSvnRev_LastTime == gCurRev.iSVNRev)
	{
		errorTrackerLibSendWrappedString(link, pCheckinString);
		return;
	}

	iCurSvnRev_LastTime = gCurRev.iSVNRev;

	estrPrintf(&pCheckinString, "<pre>\nSVN Checkins that are currently being tested/built\n\n");

	for (i=0; i < eaSize(&ppSVNCheckinsBeingTested); i++)
	{
		estrConcatf(&pCheckinString, "%s %d: %s\n", ppSVNCheckinsBeingTested[i]->userName, 
			ppSVNCheckinsBeingTested[i]->iRevNum,
			ppSVNCheckinsBeingTested[i]->checkinComment);
	}

	estrConcatf(&pCheckinString, "</pre>");
	errorTrackerLibSendWrappedString(link, pCheckinString);
}

void SendCurrentGimmeCheckins(NetLink *link)
{
	static char *pCheckinString = NULL;
	static int iCurGimmeTime_LastTime = 0;
	int i;

	if ((U32)iCurGimmeTime_LastTime == gCurRev.iGimmeTime)
	{
		errorTrackerLibSendWrappedString(link, pCheckinString);
		return;
	}

	iCurGimmeTime_LastTime = gCurRev.iGimmeTime;

	estrPrintf(&pCheckinString, "<pre>\nGimme checkins that are currently being tested/built\n\n");


	for (i=0; i < eaSize(&ppGimmeCheckinsBeingTested); i++)
	{
		estrConcatf(&pCheckinString, "%s %s: %s\n", ppGimmeCheckinsBeingTested[i]->userName, 
			timeGetLocalDateStringFromSecondsSince2000(ppGimmeCheckinsBeingTested[i]->iCheckinTimeSS2000),
			ppGimmeCheckinsBeingTested[i]->checkinComment);
	}

	estrConcatf(&pCheckinString, "</pre>");
	errorTrackerLibSendWrappedString(link, pCheckinString);
}


void SendCheckedInSVNCheckins(NetLink *link)
{
	static char *pCheckinString = NULL;
	static int iLastCheckedInSVNRev_LastTime = 0;
	int i;

	if ((U32)iLastCheckedInSVNRev_LastTime == gDynamicState.lastCheckedInRev.iSVNRev)
	{
		errorTrackerLibSendWrappedString(link, pCheckinString);
		return;
	}

	iLastCheckedInSVNRev_LastTime = gDynamicState.lastCheckedInRev.iSVNRev;

	estrPrintf(&pCheckinString, "<pre>\nSVN Checkins that have been checked in (most recent 100 checkin IDs)\n");

	for (i=0; i < eaSize(&ppSVNCheckinsInSuccessfulBuild); i++)
	{
		estrConcatf(&pCheckinString, "%s %d: %s\n", ppSVNCheckinsInSuccessfulBuild[i]->userName, 
			ppSVNCheckinsInSuccessfulBuild[i]->iRevNum,
			ppSVNCheckinsInSuccessfulBuild[i]->checkinComment);
	}

	estrConcatf(&pCheckinString, "</pre>");
	errorTrackerLibSendWrappedString(link, pCheckinString);
}

void SendCheckedInGimmeCheckins(NetLink *link)
{
	static char *pCheckinString = NULL;
	static int iLastCheckedInGimmeTime_LastTime = 0;
	int i;

	if ((U32)iLastCheckedInGimmeTime_LastTime == gDynamicState.lastCheckedInRev.iGimmeTime)
	{
		errorTrackerLibSendWrappedString(link, pCheckinString);
		return;
	}

	iLastCheckedInGimmeTime_LastTime = gDynamicState.lastCheckedInRev.iGimmeTime;

	estrPrintf(&pCheckinString, "<pre>\nGimme Checkins that have been checked in (most recent 2 hours)\n");

	for (i=0; i < eaSize(&ppGimmeCheckinsInSuccessfulBuild); i++)
	{
		estrConcatf(&pCheckinString, "%s %s: %s\n", ppGimmeCheckinsInSuccessfulBuild[i]->userName, 
			timeGetLocalDateStringFromSecondsSince2000(ppGimmeCheckinsInSuccessfulBuild[i]->iCheckinTimeSS2000),
			ppGimmeCheckinsInSuccessfulBuild[i]->checkinComment);
	}

	estrConcatf(&pCheckinString, "</pre>");
	errorTrackerLibSendWrappedString(link, pCheckinString);
}

void GetCurrentBuilderStatusStruct( BuilderStatusStruct* pBSS )
{
	U32 iTimeInState = timeSecondsSince2000_ForceRecalc() - giTimeEnteredState;
	U32 iTimeInBuild = timeSecondsSince2000_ForceRecalc() - giLastTimeStarted;
	
	pBSS->CurrentResult = eCurResult;

	estrConcatf(&pBSS->pCurStateString, "%s", StaticDefineIntRevLookup(enumCBStateEnum, geState));
	estrConcatf(&pBSS->pCurStateTime, "%d:%02d", iTimeInState / 60, iTimeInState % 60);
	pBSS->iSecondsInCurrentState = iTimeInState;
	estrConcatf(&pBSS->pLastBuildResult, "%s", StaticDefineIntRevLookup(enumCBResultEnum,gDynamicState.eLastResult));
	estrConcatf(&pBSS->pTimeInBuild, "%d:%02d", iTimeInBuild / 60, iTimeInBuild % 60);
	pBSS->iSecondsInBuild = iTimeInBuild;

	// last successfull time
	if (gDynamicState.iLastSuccessfulCycleTime)
	{
		estrConcatf(&pBSS->pLastSuccessfullBuildTime, "%d:%02d", gDynamicState.iLastSuccessfulCycleTime / 60, gDynamicState.iLastSuccessfulCycleTime % 60);
	}
	else
	{
		estrConcatf(&pBSS->pLastSuccessfullBuildTime, "Undefined");
	}

	// Step description
	if (GetSubStateString())
	{
		estrConcatf(&pBSS->pStatusSubString, "%s", GetSubStateString());
	}
	if (GetSubSubStateString())
	{
		estrConcatf(&pBSS->pStatusSubSubString, "%s", GetSubSubStateString());
	}
	pBSS->DynamicState = &gDynamicState;
	pBSS->pConfig = &gConfig;

}

char* PrintCurrentStatusToXMLString( char* sBuffer )  // estring 
{
	BuilderStatusStruct stBSS;
	stBSS.pCurStateString = NULL;
	stBSS.pCurStateTime = NULL;
	stBSS.pLastBuildResult = NULL;
	stBSS.pLastSuccessfullBuildTime = NULL;
	stBSS.pStatusSubString = NULL;
	stBSS.pStatusSubSubString = NULL;
	stBSS.pTimeInBuild = NULL;
	stBSS.iSecondsInBuild = 0;
	stBSS.iSecondsInCurrentState = 0;

	GetCurrentBuilderStatusStruct(&stBSS);

	ParserWriteXML(&sBuffer, parse_BuilderStatusStruct, &stBSS);

	return sBuffer;
}

void SendStatusXMLWebPage( NetLink *link )
{
	// AHOBBS - TODO
	char* pXMLString = NULL; // estring
//	char *pStatusEString = NULL;
	U32 iLen;
	EnterCriticalSection(&gCBCriticalSection);
	pXMLString = PrintCurrentStatusToXMLString( pXMLString );
//	errorTrackerLibSendWrappedString(link, pXMLString);	
	iLen = (U32)strlen(pXMLString);
	httpSendBytesRaw(link, (void*)pXMLString, iLen);
	httpSendComplete(link);
	LeaveCriticalSection(&gCBCriticalSection);
	estrDestroy(&pXMLString);
}

char *GetBuilderComment(void)
{
	static char *pRetString = NULL;
	char *pBuf;

	estrDestroy(&pRetString);

	pBuf = fileAlloc("c:\\continuousBuilder\\comment.txt", NULL);

	if (pBuf)
	{
		estrCopy2(&pRetString, pBuf);
//		estrFixupNewLinesForWindows(&pRetString);
		free(pBuf);
		return pRetString;
	}

	return "";

}

char *OVERRIDE_LATELINK_httpGetAppCustomHeaderString(void)
{
	static char *pHeaders = NULL;

	if (!pHeaders)
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(HTML_HEADERS_TXT), L"TXT");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				void *pTxtFile = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program

				if (pTxtFile)
				{
					estrCopy2(&pHeaders, pTxtFile);
				}

			}
		}
	}

	if (pHeaders)
	{
		return pHeaders;
	}
	else
	{
		return "";
	}
}

void SendMainWebPage(NetLink *link)
{
	char *pStatusEString = NULL;
	U32 iTimeInState;
	U32 iTimeInBuild = timeSecondsSince2000_ForceRecalc() - giLastTimeStarted;
	char *pBuilderComment;
	char *pFrontPageVars;

	EnterCriticalSection(&gCBCriticalSection);

	estrConcatf(&pStatusEString, "<link rel=\"shortcut icon\" href=\"//intranet/infrastructure/static_home/%s.ico\" type=\"image/x-icon\" />",
		gDynamicState.eLastResult == CBRESULT_FAILED ? "bad" : "good");


	estrConcatf(&pStatusEString, "<meta http-equiv=\"refresh\" content=\"10\">\n");
	estrConcatf(&pStatusEString, "<a href = \"/Activity\">Recent Activity</a>\n");

	estrConcatf(&pStatusEString, "<a href=\"/CBTiming?buildStartSS2000=cur\">Timing</a>\n");

	if (gbPauseBetweenRuns)
	{
		estrConcatf(&pStatusEString, "<a href=\"/UnPauseCBBetweenRuns\">UN-Pause the %s CB between runs<a>\n", GetCBName());
	}
	else
	{
		estrConcatf(&pStatusEString, "<a href=\"/PauseCBBetweenRuns\">Pause the %s CB between runs<a>\n", GetCBName());
	}

	if (gbPauseBetweenSteps)
	{
		estrConcatf(&pStatusEString, "<a href=\"/UnPauseCBBetweenSteps\">UN-Pause the %s CB between steps<a>\n", GetCBName());
	}
	else
	{
		estrConcatf(&pStatusEString, "<a href=\"/PauseCBBetweenSteps\">Pause the %s CB between steps<a>\n", GetCBName());
	}

	estrConcatf(&pStatusEString, "<a href=\"cryptic://vnc/%s\">VNC</a>\n", makeIpStr(getHostLocalIp()));
	

	estrConcatf(&pStatusEString, "<div class=\"SVNAndGimme\"><b>SVN:</b> %s     <b>Gimme:</b> %s   <b>CoreGimme:</b> %s</div>\n", CBGetPresumedSVNBranch(), 
		CBGetPresumedGimmeProjectAndBranch(), CBGetPresumedCoreGimmeBranch()); 

	pFrontPageVars = GetConfigVar("VARS_TO_SHOW_ON_FRONT_PAGE");
	if (pFrontPageVars)
	{
		char **ppFrontPageVars = NULL;
		DivideString(pFrontPageVars, ",", &ppFrontPageVars, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		estrConcatf(&pStatusEString, "<div class=\"FrontPageVariables\">");

		FOR_EACH_IN_EARRAY(ppFrontPageVars, char, pVarName)
		{
			char fullVarName[64];
			static char *pValue = NULL;
			sprintf(fullVarName, "$%s$", pVarName);
			

			estrClear(&pValue);
			if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), fullVarName, &pValue))
			{
				estrConcatf(&pStatusEString, "<b>%s:</b>%s    ", pVarName, pValue);
			}
			else
			{
				estrConcatf(&pStatusEString, "<b>%s:</b>(unknown)   ", pVarName);
			}
		}
		FOR_EACH_END;

		eaDestroyEx(&ppFrontPageVars, NULL);
		estrConcatf(&pStatusEString, "</div>\n");
	}


	pBuilderComment = GetBuilderComment();

	if (pBuilderComment && pBuilderComment[0])
	{
		char *pFixedUpComment = NULL;
		estrCopyWithHTMLEscaping(&pFixedUpComment, pBuilderComment, false);

		estrReplaceOccurrences(&pFixedUpComment, "\r", "");
		
		estrConcatf(&pStatusEString, "<div class=\"notice\"><h1><pre>%s</pre></h1>\n", pFixedUpComment);
		estrConcatf(&pStatusEString, "<h3>\nTo modify, <a href=\"cryptic://vnc/%s\">VNC</a> and use Windows dialog</h3></div>\n", makeIpStr(getHostLocalIp()));

		estrDestroy(&pFixedUpComment);
	}
	else
	{
		estrConcatf(&pStatusEString, "<div class=\"notice\"><h3>\nTo set a builder comment, <a href=\"cryptic://vnc/%s\">VNC</a> and and use Windows dialog</h3></div>\n", makeIpStr(getHostLocalIp()));
	}

	estrConcatf(&pStatusEString, "<pre>\n");


	iTimeInState = timeSecondsSince2000_ForceRecalc() - giTimeEnteredState;
	if (!CBTypeIsCONT() && BuildScripting_IsRunning(CBGetRootScriptingContext()))
	{
		estrConcatf(&pStatusEString, "Running: %s\n", BuildScripting_GetCurDisplayString(CBGetRootScriptingContext()));
	}
	else
	{
		estrConcatf(&pStatusEString, "Current State: %s (%d:%02d)        ", StaticDefineIntRevLookup(enumCBStateEnum, geState),
			iTimeInState / 60, iTimeInState % 60);
	}
	
	estrConcatf(&pStatusEString, "Last Build Result: %s\n\n", StaticDefineIntRevLookup(enumCBResultEnum,gDynamicState.eLastResult));

	estrConcatf(&pStatusEString, "Current full process time: %d:%02d   ", iTimeInBuild / 60, iTimeInBuild % 60);

	if (CBTypeIsCONT())
	{
		estrConcatf(&pStatusEString, "Last successful full process time: ");
		if (gDynamicState.iLastSuccessfulCycleTime)
		{
			estrConcatf(&pStatusEString, "%d:%02d", gDynamicState.iLastSuccessfulCycleTime / 60, gDynamicState.iLastSuccessfulCycleTime % 60);
		}
		else
		{
			estrConcatf(&pStatusEString, "Undefined");
		}
	}
	estrConcatf(&pStatusEString, "\n");


	if (gbCurrentlyPausedBetweenSteps)
	{
		estrConcatf(&pStatusEString, "PAUSED - ");
	}

	if (GetSubStateString())
	{
		estrConcatf(&pStatusEString, "%s\n", GetSubStateString());
	}
	if (GetSubSubStateString())
	{
		estrConcatf(&pStatusEString, "%s\n", GetSubSubStateString());
	}

	

	if (CBTypeIsCONT())
	{

		if (gDynamicState.lastGoodRev.iSVNRev)
		{
			estrConcatf(&pStatusEString, "Last Totally Errorless Build: SVN revision %d, Gimme %u(%s)\n",
				gDynamicState.lastGoodRev.iSVNRev, gDynamicState.lastGoodRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastGoodRev.iGimmeTime));
		}
		else
		{
			estrConcatf(&pStatusEString, "Last Totally Errorless Build: None\n");
		}

		if (gDynamicState.lastCheckedInRev.iSVNRev)
		{
			estrConcatf(&pStatusEString, "Last Checked In Build: SVN revision %d, Gimme %u(%s)\n",
				gDynamicState.lastCheckedInRev.iSVNRev, gDynamicState.lastCheckedInRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gDynamicState.lastCheckedInRev.iGimmeTime));
		}
		else
		{
			estrConcatf(&pStatusEString, "Last Checked In Build: None\n");
		}


		if (gCurRev.iSVNRev)
		{
			estrConcatf(&pStatusEString, "Current build being tested/checked in: SVN revision %d, Gimme %u(%s)\n",
				gCurRev.iSVNRev, gCurRev.iGimmeTime, timeGetLocalDateStringFromSecondsSince2000(gCurRev.iGimmeTime));
		}
	}

	if (eaSize(&gConfig.ppFilesToLink) || CBTypeIsCONT())
	{
		int i;
		estrConcatf(&pStatusEString, "</pre>\n");

		if (CBTypeIsCONT())
		{
			estrConcatf(&pStatusEString, "<a href=\"file?fname=%s\">Build History</a><br>\n", GetCBAutoHistoryFileName());
		}

		for (i=0; i < eaSize(&gConfig.ppFilesToLink); i++)
		{
			estrConcatf(&pStatusEString, "<a href=\"file?fname=%s\">%s</a><br>\n",
				gConfig.ppFilesToLink[i]->pFileName, gConfig.ppFilesToLink[i]->pLinkName);
		}
		estrConcatf(&pStatusEString, "<pre>\n");
	}

	estrConcatf(&pStatusEString, "----------Errors encountered so far this attempt:\n");
	GetAllBugsIntoHTML(&pStatusEString, false);

	if (estrLength(&pLastHTMLBugsString))
	{
		estrConcatf(&pStatusEString, "----------Errors report from last attempt:\n%s</pre>\n",  pLastHTMLBugsString);
	}

	if (gDynamicState.lastCheckedInRev.iSVNRev)
	{
		estrConcatf(&pStatusEString, "<br><a href=\"curSvnCheckins\">SVN Checkins Currently Being Tested/Built</a><br>\n");
		estrConcatf(&pStatusEString, "<a href=\"curGimmeCheckins\">Gimme Checkins Currently Being Tested/Built</a><br>\n");
		estrConcatf(&pStatusEString, "<a href=\"checkedInSvnCheckins\">SVN Checkins already checked in</a><br>\n");
		estrConcatf(&pStatusEString, "<a href=\"checkedInGimmeCheckins\">Gimme Checkins already checked in</a><br>\n");
	}
	estrConcatf(&pStatusEString, "<a href=\"bsvars\">Current Build Scripting Variables</a>  <a href=\"bsvars?comments=1\">(with comments)</a><br>\n");
	estrConcatf(&pStatusEString, "<a href=\"increaseTimeout?amt=600\">Add 10 minutes to current timeout</a><br>\n");
	estrConcatf(&pStatusEString, "<a href=\"increaseTimeout?amt=3600\">Add 1 hour to current timeout</a><br>\n");




	errorTrackerLibSendWrappedString(link, pStatusEString);	
	
	estrDestroy(&pStatusEString);
	LeaveCriticalSection(&gCBCriticalSection);


}

void SendFullEntryHTML(NetLink *pLink, ErrorEntry *pEntry)
{

	char *pString = NULL;
	U32 iSecsAgo;
	int iResult;
	char *pQueryString = NULL;
	char *pResultString = NULL;
	int iFoundID = 0;
	
	if (!pEntry->pUserData)
	{
		errorTrackerLibSendWrappedString(pLink, "No User Data found");
		return;
	}
	iSecsAgo = timeSecondsSince2000_ForceRecalc() - pEntry->pUserData->iTimeWhenItFirstHappened;
	ETWeb_DumpDataToString(pEntry, &pString, DUMPENTRY_FLAG_NO_JIRA 
		| DUMPENTRY_FLAG_NO_FIRSTSEEN | DUMPENTRY_FLAG_NO_PLATFORMS | DUMPENTRY_FLAG_NO_PRODUCTS 
		| DUMPENTRY_FLAG_NO_TOTALCOUNT | DUMPENTRY_FLAG_NO_USERSCOUNT | DUMPENTRY_FLAG_NO_VERSIONS 
		| DUMPENTRY_FLAG_NO_USERS | DUMPENTRY_FLAG_NO_DAYS_AGO | DUMPENTRY_FLAG_NO_DUMPINFO 
		| DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST | DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS | DUMPENTRY_FLAG_NO_DUMP_TOGGLES);

	if (pEntry->pLastBlamedPerson)
	{
		estrConcatf(&pString, "<div class=\"heading\">Blamed on:</div>");
		estrConcatf(&pString, "<div class=\"firstseen\">%s</div>\n", pEntry->pLastBlamedPerson);
	}

	estrConcatf(&pString, "<div class=\"heading\">First occurence:</div>");
	estrConcatf(&pString, "<div class=\"firstseen\">Email ID %d (%d hours %d minutes ago)</div>\n", 
				pEntry->pUserData->iEmailNumberWhenItFirstHappened, iSecsAgo / 3600, iSecsAgo / 60 % 60);

	if (pEntry->pUserData->pDumpFileName)
	{
		estrConcatf(&pString, "<a href=\"file://%s\">Dump File</a><br>", pEntry->pUserData->pDumpFileName);
	}

	if (pEntry->pUserData->pMemoryDumpFileName)
	{
		estrConcatf(&pString, "<a href = \"file://%s\">Memory Dump File</a><br>", pEntry->pUserData->pMemoryDumpFileName);
	}

	estrPrintf(&pQueryString, "http://errortracker:83/hashlookup?hash=%s", errorTrackerLibStringFromUniqueHash(pEntry));


	iResult = httpBasicGetText(pQueryString, &pResultString);

	if (iResult == 200 && pResultString)
	{
		iFoundID = atoi(pResultString);
	}

	if (iFoundID)
	{
		estrConcatf(&pString, "<a href=\"http://errortracker/detail?id=%d\">Main ErrorTracker Link</a>", iFoundID);
	}
	else
	{
		estrConcatf(&pString, "<div class=\"heading\">Not Found on Main ErrorTracker</div>");
	}

	estrDestroy(&pQueryString);
	estrDestroy(&pResultString);


	errorTrackerLibSendWrappedString(pLink, pString);

	estrDestroy(&pString);


}

bool CBHtmlHandlerFunc(NetLink *link, char **args, char **values, int count)
{
	if (stricmp(args[0], "/memdump") == 0)
	{
		memCheckDumpAllocs();
		httpRedirect(link, "/continuousbuilder");
		return true;
	}

	if (stricmp(args[0], "/bsvars") == 0)
	{
		if (count > 1 && stricmp(args[1], "comments") == 0 && stricmp(values[1], "1") == 0)
		{
			SendBuildScriptingVars(link, 1);
		}
		else
		if (count > 1 && stricmp(args[1], "varname") == 0)
		{
			SendSingleBuildScriptingVar(link, values[1]);
		}
		else
		{
			SendBuildScriptingVars(link, 0);
		}
		return true;
	}

	if (stricmp(args[0], "/file") == 0 && count > 1)
	{
		if (stricmp(args[1], "fname") == 0)
		{
			SendFile(link, values[1] ? values[1] : "", false);
			return true;
		}
	}

	if (stricmp(args[0], "/logfile") == 0 && count > 2)
	{
		char *pBuildStartTime = NULL;
		char *pFName = NULL;
		char *pBuildType = NULL;
		char *pProductName = NULL;
		int i;

		for (i=1; i < count; i++)
		{
			if (stricmp(args[i], "buildStartTime") == 0)
			{
				pBuildStartTime = values[i];
			} 
			else if (stricmp(args[i], "fName") == 0)
			{
				pFName = values[i];
			}
			else if (stricmp(args[i], "buildType") == 0)
			{
				pBuildType = values[i];
			}
			else if (stricmp(args[i], "productName") == 0)
			{
				pProductName = values[i];
			}
		}

		if (pBuildStartTime && pFName)
		{
			SendLogFile(link, pBuildStartTime, pFName, 
				pBuildType ? pBuildType : gpCBType->pShortTypeName,
				pProductName ? pProductName : gpCBProduct->pProductName);
			return true;
		}
	}


	if (stricmp(args[0], "/logs") == 0 && count >= 2)
	{
		char *pBuildStartTime = NULL;
		char *pSubDir = NULL;
		char *pBuildType = NULL;
		char *pProductName = NULL;
		int i;

		for (i=1; i < count; i++)
		{
			if (stricmp(args[i], "buildStartTime") == 0)
			{
				pBuildStartTime = values[i];
			} 
			else if (stricmp(args[i], "subDir") == 0)
			{
				pSubDir = values[i];
			}
			else if (stricmp(args[i], "buildType") == 0)
			{
				pBuildType = values[i];
			}
			else if (stricmp(args[i], "productName") == 0)
			{
				pProductName = values[i];
			}
		}

		if (pBuildStartTime)
		{
			SendLogFileDir(link, pBuildStartTime, pSubDir, 
				pBuildType ? pBuildType : gpCBType->pShortTypeName,
				pProductName ? pProductName : gpCBProduct->pProductName);
			return true;
		}
	}




	if (stricmp(args[0], "/curSvnCheckins") == 0)
	{
		SendCurrentSVNCheckins(link);
		return true;
	}

	if (stricmp(args[0], "/curGimmeCheckins") == 0)
	{
		SendCurrentGimmeCheckins(link);
		return true;
	}

	if (stricmp(args[0], "/checkedInSvnCheckins") == 0)
	{	
		SendCheckedInSVNCheckins(link);
		return true;
	}

	if (stricmp(args[0], "/checkedInGimmeCheckins") == 0)
	{
		SendCheckedInGimmeCheckins(link);
		return true;
	}


	if (stricmp(args[0], "/continuousbuilder") == 0)
	{
		SendMainWebPage(link);




		return true;
	}

	if (stricmp(args[0], "/skip") == 0)
	{
		BuildScripting_SkipCurrentStep();
		httpRedirect(link, "/continuousbuilder");


		return true;
	}


	if (stricmp(args[0], "/statusXML") == 0)
	{
		SendStatusXMLWebPage(link);
		return true;
	}

	if (stricmp(args[0], "/UnPauseCBBetweenRuns") == 0)
	{
		gbPauseBetweenRuns = false;
		httpRedirect(link, "/continuousbuilder");
		return true;
	}

	if (stricmp(args[0], "/PauseCBBetweenRuns") == 0)
	{
		gbPauseBetweenRuns = true;
		httpRedirect(link, "/continuousbuilder");
		return true;
	}

	if (stricmp(args[0], "/UnPauseCBBetweenSteps") == 0)
	{
		gbPauseBetweenSteps = false;
		httpRedirect(link, "/continuousbuilder");
		return true;
	}

	if (stricmp(args[0], "/PauseCBBetweenSteps") == 0)
	{
		gbPauseBetweenSteps = true;
		httpRedirect(link, "/continuousbuilder");
		return true;
	}

	if (stricmp(args[0], "/activity") == 0)
	{
		SendComments(link);
		return true;
	}


	if (stricmp(args[0], "/increaseTimeout") == 0 && count == 2 && stricmp(args[1], "amt") == 0)
	{
		int iAmt = atoi(values[1]);
		if (iAmt)
		{
			BuildScripting_AddTimeBeforeFailure(CBGetRootScriptingContext(), iAmt);
			return true;
		}
	}

//		estrConcatf(ppOutString, "<a href=\"/CBTiming?buildStartSS2000=%u&StepIndex=%s\">%d substeps</a>",

	if (stricmp(args[0], "/CBTiming") == 0)
	{
		if (count == 2 && stricmp(args[1], "buildStartSS2000") == 0)
		{
			U32 iStartTime;

			ANALYSIS_ASSUME(values[1] != 0);
			if (stricmp(values[1], "cur") == 0)
			{
				iStartTime = giLastTimeStarted;
			}
			else
			{
				iStartTime = atoi(values[1]);
			}

			if (iStartTime)
			{
				SendCBTiming(link, iStartTime, "");
				return true;
			}
		}

		if (count == 3 && stricmp(args[1], "buildStartSS2000") == 0 && stricmp(args[2], "StepIndex") == 0)
		{
			U32 iStartTime = atoi(values[1]);

			if (iStartTime)
			{
				SendCBTiming(link, iStartTime, values[2]);
				return true;
			}
		}
	}


	if (count > 2 && stricmp(args[0], "/CBdetail") == 0)
	{
		int iID = 0, iContext = 0;
		if (stricmp(args[1], "CBId") == 0)
		{
			iID = atoi(values[1]);
		}
		if (stricmp(args[2], "Context") == 0)
		{
			iContext = atoi(values[2]);
		}

		if (iID && (iContext == GLOBALTYPE_ERRORTRACKERENTRY || iContext == GLOBALTYPE_ERRORTRACKERENTRY_LAST))
		{
			ContainerIterator iter = {0};
			Container *currCon = NULL;

			ErrorTrackerEntryList *pErrorEntryList = NULL;
			ErrorEntry *pEntry = NULL;
			Container *con = NULL;

			con = objGetContainer(iContext, iID);
			if (con)
			{
				pEntry = CONTAINER_ENTRY(con);
				SendFullEntryHTML(link, pEntry);
				return true;
			}
			return false;
		}
	}

	return false;


}


static DWORD WINAPI continuousBuilderDialogThread( LPVOID lpParam )
{
	CBListen = commListen(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,CONTINUOUS_BUILDER_PORT,ContinuousBuilderHandleMsg,0,0,0);
	//commListen(commDefault(), LINK_HTTP, 80, CBhttpHandleMsg, 0, 0, 0);

	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_CONTINUOUSBUILDER), NULL, (DLGPROC)continuousBuilderDlgProc);


	return true;

}



static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			//shutdownErrorTracker(); TODO make sure this isn't necessary
			return FALSE; 

		// Pass other signals to the next handler.
		default: 
			return FALSE; 
	} 
}

void ReplaceOccurrencesInFile(char *pFileName, char *pSrc, char *pTarget)
{
	char *pBuffer = fileAlloc(pFileName, NULL);
	char *pTempEString = NULL;
	FILE *pOutFile;

	if (!pBuffer)
	{
		return;
	}

	estrCopy2(&pTempEString, pBuffer);
	free(pBuffer);

	estrReplaceOccurrences(&pTempEString, pSrc, pTarget);

	pOutFile = fopen(pFileName, "wt");
	if (pOutFile)
	{
		fprintf(pOutFile, "%s", pTempEString);
		fclose(pOutFile);
	}

	estrDestroy(&pTempEString);
}




void makeCustomWebRootDirectory(char *pCustomDir, char *pSourceDir)
{
	char systemString[1024];
	char tempFileName[CRYPTIC_MAX_PATH];

	sprintf(systemString, "md %s", pCustomDir);
	system(systemString);

	sprintf(systemString, "xcopy %s\\*.txt %s /R /Y", pSourceDir, pCustomDir);
	system(systemString);

	sprintf(systemString, "xcopy %s\\*.html %s /R /Y", pSourceDir, pCustomDir);
	system(systemString);

	sprintf(tempFileName, "%s\\style.html", pCustomDir);
	ReplaceOccurrencesInFile(tempFileName, "Continuous Builder", GetCBFullName());

	sprintf(tempFileName, "%s\\headerEnd.html.format.txt", pCustomDir);
	ReplaceOccurrencesInFile(tempFileName, "Continuous Builder", GetCBFullName());
}

void OVERRIDE_LATELINK_BuildScriptingNewStepExtraStuff(void)
{
	while (gbPauseBetweenSteps)
	{
		gbCurrentlyPausedBetweenSteps = true;
		SLEEP;
	}

	gbCurrentlyPausedBetweenSteps = false;

}

extern int gUseRemoteSymServ;


int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	int iResult;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	SetAppGlobalType(GLOBALTYPE_ERRORTRACKER);
	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);


	//set it so that gimme never pauses
	_putenv_s("GIMME_NO_PAUSE", "1");






	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);


	KillAllEx("continuousbuilder.exe", true, NULL, true, true, NULL);

	system("taskkill /im sendjabber.exe");


	assertmsg(!strStartsWith(getExecutableName(), "n:"), "You can't run the continuous builder from the N drive! Damn it, Segreto, is this you again????? Copy it to c:\\continuousbuilder and run it from there. Grrr. Alex Angry. Alex Smash!");

	gTimeCBStartedUp = timeSecondsSince2000();


	if (!gbNoSelfPatching)
	{
		CheckForNewerCBAndMaybeRestart();
	}

	spRootScriptingContext = BuildScripting_CreateRootContext();

	CB_DoStartup();

/*	if (CheckConfigVarExistsAndTrue("USES_XBOX"))
	{
		xboxBeginCapturingPrintfs();
		xboxBeginStatusQueryThread();
	}*/


	iResult = mkdir("c:\\continuousbuilder");
	iResult = mkdir("c:\\continuousBuilder\\temp");
	iResult = mkdir("c:\\continuousbuilder\\emails");

	Gimme_LoadEmailAliases();

//	sprintf(GetCBLogFileName(), "c:\\continuousbuilder\\logs\\CBLog_%d.txt", gDynamicState.iNextEmailNumber);



	CB_SetScriptingVariablesFromConfig();

	{
		FWStatType stat;
		char tempString[MAX_PATH];

		sprintf(tempString, "c:\\night\\tools\\bin\\filewatcher.exe");
	
		system_detach(tempString, false, false);
	
		Sleep(50);

		fwStat("c:\\core\\data\\dataversion.txt", &stat);
	}

/*
	if (scriptToTest[0])
	{
		printf("Beginning test of %s\n", scriptToTest);
		BuildScripting_SetDefaultDirectories(gConfig.ppScriptDirectories);
		BuildScripting_SetTestingOnlyMode(true);
		StartBuildScripting(scriptToTest, NULL);
	
		while (BuildScripting_GetState() == BUILDSCRIPTSTATE_RUNNING)
		{
			BuildScripting_Tick();
			Sleep(1);
		}


		while (1);
	}
*/
	InitializeCriticalSection(&gCBCriticalSection);
	InitializeCriticalSection(&gCBCommentsCriticalSection);

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	makeCustomWebRootDirectory("c:\\core\\tools\\ContinuousBuilder\\webroot\\custom", "c:\\core\\tools\\ContinuousBuilder\\webroot");

	//BlameCacheDisable(); TODO
	CB_ErrorTrackerInit(ERRORTRACKER_OPTION_DISABLE_EMAILS | ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE | ERRORTRACKER_OPTION_DISABLE_AUTO_BLAME 
		| ERRORTRACKER_OPTION_FORCE_NO_DUMP | ERRORTRACKER_OPTION_DISABLE_AUTO_CLEAR | ERRORTRACKER_OPTION_DISABLE_DISCARD_NOT_LATEST
		| ERRORTRACKER_OPTION_REQUEST_AUTOCLOSE_ON_ERROR | ERRORTRACKER_OPTION_FORCE_RUN_SYMSERV_FROM_CORE_TOOLS_BIN
		| ERRORTRACKER_OPTION_FORCE_SYNTRANS | ERRORTRACKER_OPTION_DISABLE_ERROR_LIMITING,
		DUMPENTRY_FLAG_NO_JIRA);
	ETWeb_SetDefaultGetHandler(CBHtmlHandlerFunc);

	//StructInit(parse_ErrorTrackerSettings, &errorTrackerSettings);

	/*errorTrackerLibInit(ERRORTRACKER_OPTION_DISABLE_EMAILS | ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE | ERRORTRACKER_OPTION_DISABLE_AUTO_BLAME 
		| ERRORTRACKER_OPTION_FORCE_NO_DUMP | ERRORTRACKER_OPTION_DISABLE_AUTO_CLEAR | ERRORTRACKER_OPTION_DISABLE_DISCARD_NOT_LATEST
		| ERRORTRACKER_OPTION_REQUEST_AUTOCLOSE_ON_ERROR | ERRORTRACKER_OPTION_FORCE_RUN_SYMSERV_FROM_CORE_TOOLS_BIN
		| ERRORTRACKER_OPTION_FORCE_SYNTRANS,
		DUMPENTRY_FLAG_NO_JIRA, 
		&errorTrackerSettings);*/
	//pCurErrorContext = errorTrackerLibGetCurrentContext();
	//errorTrackerLibClearContext(pCurErrorContext);

	ParserReadTextFile(STACK_SPRINTF("%s\\CBDynStatus.txt", GetCBDirectoryName()), parse_CBDynamicState, &gDynamicState, 0);


	//this slightly kludgy code block takes the saved last errors written to disk and makes them the current errors. This
	//is needed to bootstrap the double-buffering of error contexts, because the first thing the main loop does is
	//take the current errors and make them the last errors. So we load the last errors, then they become the current 
	//errors, and then immediately the last errors again
	if (gDynamicState.pLastErrorContext)
	{
		//the earray of errorEntries is no longer needed after the text loading has happened,
		//and it can't be structcopied as it's now containers
		/*eaDestroy(&gDynamicState.pLastErrorContext->entryList.ppEntries);
		StructCopyAll(parse_ErrorTrackerEntryList, &gDynamicState.pLastErrorContext->entryList, &pCurErrorContext->entryList);
		StructDestroy(parse_ErrorTrackerContext, gDynamicState.pLastErrorContext);
		gDynamicState.pLastErrorContext = NULL;
		pCurErrorContext->entryList.bSomethingHasChanged = true;*/

		//the kludginess! it burns!
		gbJustCompletedProcessOrJustStartedUp = true;
	}
	//gDynamicState.pLastErrorContext = errorTrackerLibCreateContext();
	//gDynamicState.pLastErrorContext->entryList.eContainerType = GLOBALTYPE_ERRORTRACKERENTRY_LAST;
	


	if (sbEnableServerMonitor)
	{
		int b = chdir("C:\\core\\tools\\bin\\");
		fileAllPathsAbsolute(false);
		FolderCacheAddFolder(folder_cache, "C:\\Core\\data", 3, NULL, true);
		GenericHttpServing_Begin(giServerMonitorPort, "CB", DEFAULT_HTTP_CATEGORY_FILTER, 0);
		fileAllPathsAbsolute(true);
	}
	assert(tmCreateThread(continuousBuilderDialogThread, NULL));




	ContinuousBuilder_MainLoop();


	EXCEPTION_HANDLER_END




}

AUTO_FIXUPFUNC;
TextParserResult fixupErrorTrackerEntryList(ErrorTrackerEntryList *pList, enumTextParserFixupType eType, void *pExtraData)
{	
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	int i;

	switch (eType)
	{
	case FIXUPTYPE_PRE_TEXT_WRITE:	
		eaDestroy(&pList->ppEntries);

		objInitContainerIteratorFromType(pList->eContainerType, &iter);
		
		while ((currCon = objGetNextContainerFromIterator(&iter)))
		{
			ErrorEntry *pEntry = (ErrorEntry*)(currCon->containerData);
			eaPush(&pList->ppEntries, pEntry);
		}
		objClearContainerIterator(&iter);
		break;

	case FIXUPTYPE_POST_TEXT_READ:
		for (i=0; i < eaSize(&pList->ppEntries); i++)
		{
			objAddExistingContainerToRepository(pList->eContainerType, pList->ppEntries[i]->uID, pList->ppEntries[i]);
		}
		break;

	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pList->ppEntries);
		break;


	}

	return PARSERESULT_SUCCESS;
}
#if 0
void PublishPackageToBuildMaster( char* packagePath, char* displayName )
{
	char **ppWhoToSendTo = NULL;
	char* pSubjectLine = NULL;
	bool bNoDefaultRecipients = false;

	DivideString("BuildStatusMachine", ";", &ppWhoToSendTo, 0);

	SendEmailToList_WithAttachment_Internal(EMAILFLAG_NO_DEFAULT_RECIPIENTS, &ppWhoToSendTo, STACK_SPRINTF("%s\\CBPublishEmail.txt", GetCBLogDirectoryName()), STACK_SPRINTF("Publish results are attached in BuildLogs.zip"), packagePath, displayName, NULL);
	eaDestroyEx(&ppWhoToSendTo, NULL);
}

void PackageLogsForPublishing( char* packageName )
{
	char systemString[1024];

	// info-zip command line option:  -j	junk (don't record) directory names
	sprintf(systemString, "info-zip -j %s %s\\*", packageName, GetCBLogDirectoryName());
	system(systemString);
}

void WritePublishEmailFile( char* packageName )
{
	FILE *pFile;
	pFile = fopen(STACK_SPRINTF("%s\\CBPublishEmail.txt", GetCBLogDirectoryName()), "wt");
	fprintf(pFile, "Published results for %s build are available on %s in C:\\ContinuousBuilder\\logs\\%s", GetCBName(), getHostName(), "BUILDIDTEMP");
	fprintf(pFile, "HOST= %s", getHostName());
	fprintf(pFile, "CBNAME= %s", GetCBName());
	fclose(pFile);
}

void PublishLogs()
{
	WritePublishEmailFile( STACK_SPRINTF("%s\\BuildLogs.zip", GetCBLogDirectoryName()) );
	PackageLogsForPublishing( STACK_SPRINTF("%s\\BuildLogs.zip", GetCBLogDirectoryName()) );
	PublishPackageToBuildMaster( STACK_SPRINTF("%s\\BuildLogs.zip", GetCBLogDirectoryName()), "BuildLogs.zip" );
}

#endif

char *GetCBDirectoryName(void)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	if (retVal[0])
	{
		return retVal;
	}

	sprintf(retVal, "c:\\continuousBuilder\\%s\\%s", gpCBProduct->pProductName, gpCBType->pShortTypeName);

	return retVal;
}

char *GetCBDirectoryNameWithBuildTypeAndProductName(char *pBuildTypeName, char *pProductName)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	sprintf(retVal, "c:\\continuousBuilder\\%s\\%s", pProductName, pBuildTypeName);

	return retVal;
}

//returns c:\continuousbuilder\productname\typename\logs\STARTTIME
char *GetCBLogDirectoryName(BuildScriptingContext *pContext)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	assert(gpLastTimeStartedString);

	if (pContext == NULL || pContext == CBGetRootScriptingContext())
	{
		sprintf(retVal, "%s\\logs\\%s", GetCBDirectoryName(), gpLastTimeStartedString);
	}
	else
	{
		sprintf(retVal, "%s\\logs\\%s\\%s", GetCBDirectoryName(), gpLastTimeStartedString, BuildScripting_GetPathNameForChildContext(pContext));
		backSlashes(retVal);
	}
	
	return retVal;
}

char *GetCBLogDirectoryNameWithBuildTypeAndProductName(char *pBuildTypeName, char *pProductName)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	assert(gpLastTimeStartedString);
	
	sprintf(retVal, "%s\\logs\\%s", GetCBDirectoryNameWithBuildTypeAndProductName(pBuildTypeName, pProductName), gpLastTimeStartedString);
	return retVal;
}

char *GetCBLogDirectoryNameFromTime(U32 iTime)
{
	static char *pTimeString = NULL;
	static char *pRetVal = NULL;

	estrPrintf(&pTimeString, "%s", timeGetLocalDateStringFromSecondsSince2000(iTime));
	estrMakeAllAlphaNumAndUnderscores(&pTimeString);

	estrPrintf(&pRetVal, "%s\\logs\\%s", GetCBDirectoryName(), pTimeString);

	return pRetVal;
}



//returns c:\continuousbuilder\productname\typename\logs\STARTTIME\CBLog.txt
char *GetCBLogFileName(BuildScriptingContext *pContext)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";



	sprintf(retVal, "%s\\CBLog.txt", GetCBLogDirectoryName(pContext));

	return retVal;

}


//returns //returns c:\continuousbuilder\productname\typename\logs\STARTTIME\CBScripting_Filename_Log.txt

char *GetCBScriptingLogFileName(BuildScriptingContext *pContext)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	char shortName[CRYPTIC_MAX_PATH];


	getFileNameNoExtNoDirs(shortName, sCurScriptingFileName);
	sprintf(retVal, "%s\\CBSCriptingLog_%s.txt", GetCBLogDirectoryName(pContext), shortName);
	

	return retVal;
}


//returns c:\continuousbuilder\productname\typename\autoBuildHistory.txt
char *GetCBAutoHistoryFileName(void)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	if (retVal[0])
	{
		return retVal;
	}

	sprintf(retVal, "%s\\autoBuildHistory.txt", GetCBDirectoryName());

	return retVal;
}

void CB_GetDescriptiveStateString(char **ppOutString)
{
	estrCopy2(ppOutString, StaticDefineIntRevLookup(enumCBStateEnum, geState));
	
	if (GetSubStateString())
	{
		estrConcatf(ppOutString, " : %s", GetSubStateString());
	}
}

static char *spSubStateEString = NULL;

void SetSubStateString(const char *pNewString)
{
	if (stricmp_safe(spSubStateEString, pNewString) == 0)
	{
		return;
	}

	if (pNewString && pNewString[0])
	{
		estrCopy2(&spSubStateEString, pNewString);
	}
	else
	{
		estrDestroy(&spSubStateEString);
	}

	CBReportToCBMonitor_ReportState();
}

void SetSubStateStringf(FORMAT_STR const char *pFmt, ...)
{
	char *pTemp = NULL;
	if (!pFmt)
	{
		SetSubStateString(NULL);
		return;
	}
	
	estrGetVarArgs(&pTemp, pFmt);
	SetSubStateString(pTemp);
	estrDestroy(&pTemp);
}

const char *GetSubStateString(void)
{
	return spSubStateEString;
}




static char *spSubSubStateEString = NULL;

void SetSubSubStateString(const char *pNewString)
{
	if (pNewString && pNewString[0])
	{
		estrCopy2(&spSubSubStateEString, pNewString);
	}
	else
	{
		estrDestroy(&spSubSubStateEString);
	}
}

void SetSubSubStateStringf(FORMAT_STR const char *pFmt, ...)
{
	char *pTemp = NULL;
	if (!pFmt)
	{
		SetSubSubStateString(NULL);
		return;
	}

	estrGetVarArgs(&pTemp, pFmt);
	SetSubSubStateString(pTemp);
	estrDestroy(&pTemp);
}

const char *GetSubSubStateString(void)
{
	return spSubSubStateEString;
}


bool CB_IsWaitingBetweenBuilds(void)
{
	return geState == CBSTATE_WAITING;
}


static InStringCommandsAuxCommand sBuildScriptingPickerCommand = 
{
	"PICKER",
	BuildScripting_PickerCB,
};

AUTO_RUN;
void CBInStringCommandsInit(void)
{
	BuildScripting_AddAuxInStringCommand( &sBuildScriptingPickerCommand);
}



#define NEED_TO_RECALC {sbNeedRecalcTime = timeSecondsSince2000() + 120;}
//this function is kind of weird in that it has a hierarchy of where it wants to get this from,
//but never wants to recalculate any given level because that might be slow
char *CBGetPresumedGimmeProjectAndBranch(void)
{
	if (!gpCBProduct)
	{
		return "Unkown - CB still initting";
	}
	else
	{

		char *pProductName = gpCBProduct->pProductName;
		char *pDataDir = NULL;

		static char *pOutStringFromGimmeBranch = NULL;
		char *pGimmeBranch = NULL;

		static char *pOutStringFromDataDir = NULL;

		static char *pOutStringFromProductDir = NULL;

		static U32 sbNeedRecalcTime = 0;

		if (sbNeedRecalcTime && timeSecondsSince2000() > sbNeedRecalcTime)
		{
			estrDestroy(&pOutStringFromGimmeBranch);
			estrDestroy(&pOutStringFromDataDir);
			estrDestroy(&pOutStringFromProductDir);
			sbNeedRecalcTime = 0;
		}

		if (!pProductName)
		{
			return "Unknown product";
		}

		if (pOutStringFromGimmeBranch)
		{
			return pOutStringFromGimmeBranch;
		}

		if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), "$GIMMEBRANCH$", &pGimmeBranch))
		{
			estrPrintf(&pOutStringFromGimmeBranch, "%s branch %s",
				pProductName, pGimmeBranch);
			estrDestroy(&pGimmeBranch);
			return pOutStringFromGimmeBranch;
		}

		if (pOutStringFromDataDir)
		{
			return pOutStringFromDataDir;
		}

		if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), "$DATA$", &pDataDir) && dirExists(pDataDir))
		{
			int iBranchNum = Gimme_GetBranchNum(pDataDir);

			if (iBranchNum == -1)
			{
				NEED_TO_RECALC;
				estrPrintf(&pOutStringFromDataDir, "%s branch unknown (gimme error?)", pProductName);
			}
			else
			{
				estrPrintf(&pOutStringFromDataDir, "%s branch %d", pProductName, iBranchNum);
			}

			estrDestroy(&pDataDir);
			return pOutStringFromDataDir;
		}


		if (pOutStringFromProductDir)
		{
			return pOutStringFromProductDir;
		}
		else
		{
			char tempDir[CRYPTIC_MAX_PATH];
			int iBranchNum;

			sprintf(tempDir, "c:\\%s", pProductName);

			if (dirExists(tempDir))
			{
				iBranchNum = Gimme_GetBranchNum(tempDir);

				if (iBranchNum == -1)
				{
					NEED_TO_RECALC;
					estrPrintf(&pOutStringFromProductDir, "%s branch unkown (gimme error?)", pProductName);
				}
				else
				{
					estrPrintf(&pOutStringFromProductDir, "%s branch %d", pProductName, iBranchNum);
				}

				return pOutStringFromProductDir;
			}
			else
			{
				NEED_TO_RECALC;
				estrPrintf(&pOutStringFromProductDir, "%s does not exist", tempDir);
				return pOutStringFromProductDir;
			}
		}
	}
}
	
char *CBGetPresumedCoreGimmeBranch(void)
{
	char *pDataDir = NULL;

	static char *pOutStringFromGimmeBranch = NULL;
	char *pGimmeBranch = NULL;


	static char *pOutStringFromDir = NULL;

	static U32 sbNeedRecalcTime = 0;

	if (sbNeedRecalcTime && timeSecondsSince2000() > sbNeedRecalcTime)
	{
		estrDestroy(&pOutStringFromGimmeBranch);
		estrDestroy(&pOutStringFromDir);
		sbNeedRecalcTime = 0;
	}

	if (pOutStringFromGimmeBranch)
	{
		return pOutStringFromGimmeBranch;
	}

	if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), "$COREGIMMEBRANCH$", &pGimmeBranch))
	{
		estrPrintf(&pOutStringFromGimmeBranch, "branch %s",
			pGimmeBranch);
		estrDestroy(&pGimmeBranch);
		return pOutStringFromGimmeBranch;
	}

	if (pOutStringFromDir)
	{
		return pOutStringFromDir;
	}
	else
	{
		char tempDir[CRYPTIC_MAX_PATH];
		int iBranchNum;

		sprintf(tempDir, "c:\\core");

		if (dirExists(tempDir))
		{
			iBranchNum = Gimme_GetBranchNum(tempDir);
		
			if (iBranchNum == -1)
			{
				NEED_TO_RECALC;
				estrPrintf(&pOutStringFromDir, "branch unknown (gimme error?)");
			}
			else
			{
				estrPrintf(&pOutStringFromDir, "branch %d", iBranchNum);
			}

			return pOutStringFromDir;
		}
		else
		{
			NEED_TO_RECALC;
			estrPrintf(&pOutStringFromDir, "%s does not exist", tempDir);
			return pOutStringFromDir;
		}
	}
}



char *CBGetPresumedSVNBranch(void)
{
	static char *pOutString = NULL;
	static char *pErrorString = NULL;
	static bool sbAlreadyGotFromRootSrc = false;
	char *pRootSrcString = NULL;

	if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), "$SVN_BRANCH$", &pOutString))
	{
		return pOutString;
	}

	if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), "$ROOTSRC$", &pRootSrcString) && dirExists(pRootSrcString))
	{
		int iRevNum ;

		if (sbAlreadyGotFromRootSrc)
		{
			estrDestroy(&pRootSrcString);
			return pOutString;
		}

		iRevNum = SVN_GetRevNumOfFolders(pRootSrcString, NULL, &pOutString, 5);
		estrDestroy(&pRootSrcString);
		
		if (iRevNum)
		{
			sbAlreadyGotFromRootSrc = true;
			return pOutString;
		}
		else
		{
			estrPrintf(&pErrorString, "Unable to get SVN branch from %s", pRootSrcString);
			return pErrorString;
		}
	}

	if (pOutString)
	{
		return pOutString;
	}

	if (!dirExists("c:\\src"))
	{
		return "No c:\\src found";
	}

	if (SVN_GetRevNumOfFolders("c:\\src", NULL, &pOutString, 5))
	{
		return pOutString;
	}
	else
	{
		estrPrintf(&pErrorString, "Unable to get SVN branch from c:\\src");
		return pErrorString;
	}
}


void OVERRIDE_LATELINK_BuildScriptingVariableWasSet(BuildScriptingContext *pContext, const char *pVarName, const char *pValue)
{
	if (pContext != CBGetRootScriptingContext())
	{
		return;
	}

	if (stashFindPointer(gConfig.sVariablesToReportToCBMonitor, pVarName, NULL))
	{
		Packet *pPak = CBReportToCBMonitor_GetPacket(FROM_CB_TO_CBMONITOR_VARIABLESET);

		if (pPak)
		{
			pktSendString(pPak, pVarName);
			pktSendString(pPak, pValue ? pValue : "");
			pktSend(&pPak);
		}
	}
}

void OVERRIDE_LATELINK_BuildScripting_AddStartingVariables(BuildScriptingContext *pContext)
{
	char temp[32];

	if (pContext != CBGetRootScriptingContext())
	{
		return;
	}

	BuildScripting_AddVariable(CBGetRootScriptingContext(), "$OVERALL_RUN_START_TIME$", gpLastTimeStartedString, "BUILTIN_STARTUP");

	sprintf(temp, "%d", giBuildRunCount);
	BuildScripting_AddVariable(CBGetRootScriptingContext(), "$BUILD_RUN_COUNT$", temp, "BUILTIN_STARTUP");

	

	if (CBTypeIsCONT())
	{
		char tempSVNString[64];
		char tempGimmeString[128];
		char tempFileName[CRYPTIC_MAX_PATH];

		sprintf(tempSVNString, "%u", gCurRev.iSVNRev);
		strcpy(tempGimmeString, timeGetLocalDateStringFromSecondsSince2000(gCurRev.iGimmeTime));

		BuildScripting_AddVariable(CBGetRootScriptingContext(), "$SVNREVNUM$", tempSVNString, "CONT-specific internal startup");
		BuildScripting_AddVariable(CBGetRootScriptingContext(), "$GIMMETIME$", tempGimmeString, "CONT-specific internal startup");

		sprintf(tempFileName, "c:\\%s\\.patch\\patch_trivia.txt", gpCBProduct->pProductName);
		BuildScripting_AddVariable(CBGetRootScriptingContext(), "$GIMMEREVNUM$", GetTriviaFromFile(tempFileName, "PatchRevision"),  "CONT-specific internal startup");
	}
}



void OVERRIDE_LATELINK_BuildScripting_SendEmail(bool bHTML, char ***pppRecipients, char *pEmailFileName, char *pSubject)
{
	SendEmailToList_Internal(EMAILFLAG_NO_DEFAULT_RECIPIENTS | (bHTML ? EMAILFLAG_HTML : 0), pppRecipients, pEmailFileName, pSubject);
}

void OVERRIDE_LATELINK_BuildScripting_ExtraErrorProcessing(BuildScriptingContext *pContext, char *pErrorMessage)
{
	ErrorData data = {0};									
	
	data.eType = ERRORDATATYPE_ERROR;						
	data.pErrorString = pErrorMessage;
	CB_ProcessErrorData(NULL, &data);
}

void OVERRIDE_LATELINK_BuildScripting_NewCommand_DoExtraStuff(BuildScriptingContext *pContext)
{
	if (pContext == CBGetRootScriptingContext())
	{
		CBErrorProcessing_SetDisableAll(false);
	}
}

bool OVERRIDE_LATELINK_BuildScripting_DoAuxStuffBeforeLoadingScriptFiles(char **ppErrorString)
{

	if (!CheckConfigVarExistsAndTrue("DONT_GET_NEW_SCRIPTS"))
	{
		LeaveCriticalSection(&gCBCriticalSection);
		if (!Gimme_UpdateFoldersToTime(timeSecondsSince2000_ForceRecalc(), NULL, gConfig.ppScriptDirectories, 600, ""))
		{
			EnterCriticalSection(&gCBCriticalSection);
			estrCopy2(ppErrorString, "Gimme get before loading scripts failed");
			return false;
		}
		EnterCriticalSection(&gCBCriticalSection);
	}

	return true;
}

void OVERRIDE_LATELINK_BuildScripting_AddStep(char *pStepName, char *pParentContextName, int iDepth, bool bEstablishesNewContext, bool bDetachedContext)
{
	if (stricmp(pParentContextName, "root") == 0)
	{
		pParentContextName = NULL;
	}

	CBTiming_StepEx(pStepName, pParentContextName, iDepth, bEstablishesNewContext, bDetachedContext);
}

void OVERRIDE_LATELINK_BuildScripting_EndContext(char *pName, bool bFail)
{
	CBTiming_EndSubTree(pName, bFail);
}


char *OVERRIDE_LATELINK_SVN_GetUserName(void)
{
	return "continuousbuilder";
}

char *OVERRIDE_LATELINK_SVN_GetPassword(void)
{
	return "ZaQuhu99";
}

U32 OVERRIDE_LATELINK_RunStartTime(void)
{
	return giLastTimeStarted;
}

#include "continuousBuilder_c_ast.c"
#include "continuousBuilder_h_ast.c"
#include "Continuousbuilder_pub_h_ast.c"
