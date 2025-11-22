#include "utils.h"
#include "earray.h"
#include "logparserscheduledtasks.h"
#include "Message.h"
#include "GameStringFormat.h"
#include "logging.h"
#include "utilitiesLib.h"
#include "alerts.h"
#include "HttpXpathSupport.h"
#include "fileutil.h"
#include "winfiletime.h"
#include "GenericFileServing.h"
#include "objTransactions.h"
#include "url.h"
#include "HttpXpathSupport.h"
#include "LogParser.h"
#include "sysutil.h"
#include "UTF8.h"

//loaded from a def file
static ScheduledTasksDef g_Tasks = {0};

static bool s_bEnableScheduledTasks = false;
static char s_pchMailServer[100] = {0};

extern int siStartingPortNum;
extern char LogDirectoryForLaunched[];

typedef struct ScheduledTaskHandle
{
	QueryableProcessHandle* pHandle;
	U32 uiTaskStart;
	ScheduledTask* pDef;
} ScheduledTaskHandle;

ScheduledTaskHandle** g_eaProcHandles = NULL;

U32 LogParserScheduledTasks_GetTaskNextRunTimestamp(ScheduledTask* pTask);

//Whether to process the tasks defined in LogParserScheduledTasks.def
AUTO_CMD_INT(s_bEnableScheduledTasks, EnableScheduledTasks) ACMD_CALLBACK(OnEnableScheduler) ACMD_COMMANDLINE;

void OnEnableScheduler(CMDARGS)
{
	if (s_bEnableScheduledTasks)
	{
		int i;
		for (i = 0; i < eaSize(&g_Tasks.eaTasks); i++)
		{
			g_Tasks.eaTasks[i]->uiNextRunSS2000 = LogParserScheduledTasks_GetTaskNextRunTimestamp(g_Tasks.eaTasks[i]);
		}
	}
}

//Which mailserver tasks that have bAddMailserverArg set should use.
AUTO_CMD_STRING(s_pchMailServer, ScheduledTaskMailServer) ACMD_COMMANDLINE;

char gScheduledTaskConfigFile[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(gScheduledTaskConfigFile, LogParserScheduledTasksFile);

AUTO_COMMAND;
bool EnableSingleScheduledTask(const char* pchName, bool bEnable)
{
	int i;
	for (i = 0; i < eaSize(&g_Tasks.eaTasks); i++)
	{
		if (stricmp(g_Tasks.eaTasks[i]->pchName, pchName) == 0)
		{
			g_Tasks.eaTasks[i]->bDisable = !bEnable;
			if (bEnable)
				g_Tasks.eaTasks[i]->uiNextRunSS2000 = LogParserScheduledTasks_GetTaskNextRunTimestamp(g_Tasks.eaTasks[i]);
			return true;
		}
	}
	return false;
}

static const char* GetDownloadDir()
{
	if (g_Tasks.pchDownloadDir && g_Tasks.pchDownloadDir[0])
		return g_Tasks.pchDownloadDir;
	else
		return "C:/Reporting/Downloadable_Files/";
}

static const char* GetWorkingDir()
{
	if (g_Tasks.pchWorkingDir && g_Tasks.pchWorkingDir[0])
		return g_Tasks.pchWorkingDir;
	else
		return "C:/Reporting/";
}

int ScheduledTaskSort(const ScheduledTask** A, const ScheduledTask** B)
{
	if ((*A)->uiNextRunSS2000 < (*B)->uiNextRunSS2000)
		return -1;
	else if ((*A)->uiNextRunSS2000 > (*B)->uiNextRunSS2000)
		return 1;
	return 0;
}

void LogParserScheduledTasks_OncePerFrame()
{
	if (eaSize(&g_eaProcHandles) > 0)
	{
		int iRetVal;
		FOR_EACH_IN_EARRAY(g_eaProcHandles, ScheduledTaskHandle, pTaskHandle)
			if (QueryableProcessComplete(&pTaskHandle->pHandle, &iRetVal))
			{
				pTaskHandle->pDef->iRunDurationsSum += (timeSecondsSince2000() - pTaskHandle->uiTaskStart);
				pTaskHandle->pDef->uiNumRunsSinceStartup++;
				pTaskHandle->pDef->uiLastRunEndSS2000 = timeSecondsSince2000();
				free(pTaskHandle);
				eaRemoveFast(&g_eaProcHandles, FOR_EACH_IDX(g_eaProcHandles, pTaskHandle));
			}
			FOR_EACH_END
	}

	if (!g_Tasks.eaTasks || !g_Tasks.eaTasks[0] || !s_bEnableScheduledTasks)
		return;

	while (g_Tasks.eaTasks[0]->uiNextRunSS2000 <= timeSecondsSince2000())
	{
		ScheduledTask* pTask = g_Tasks.eaTasks[0];

		LogParserScheduledTasks_RunTask(pTask);

		eaQSort(g_Tasks.eaTasks, ScheduledTaskSort);
	}
}

U32 LogParserScheduledTasks_GetTaskIntervalInSeconds(ScheduledTask* pTask)
{
	U32 uiRunIntervalSS2000;

	assertmsgf(pTask, "Attempted to determine interval of a NULL scheduled task.");

	switch (pTask->eScheduleType)
	{
	case kScheduledTaskRepeatType_Hourly:
		{
			uiRunIntervalSS2000 = SECONDS_PER_HOUR;
		}break;
	case kScheduledTaskRepeatType_Daily:
		{
			uiRunIntervalSS2000 = SECONDS_PER_DAY;
		}break;
	case kScheduledTaskRepeatType_Weekly:
		{
			uiRunIntervalSS2000 = SECONDS_PER_DAY*7;
		}break;
	case kScheduledTaskRepeatType_Specified:
		{
			if (pTask->uiSpecifiedIntervalSeconds < 1800)
			{
				Errorf("Scheduled task \"%s\" given a specified interval of less than half an hour, which is invalid. Defaulting to half-hourly.", pTask->pchName);
				uiRunIntervalSS2000 = 1800;
			}
			else
				uiRunIntervalSS2000 = pTask->uiSpecifiedIntervalSeconds;
		}break;
	default:
		{
			Errorf("Scheduled task \"%s\" being run with an invalid repeat type. Defaulting to daily.", pTask->pchName);
			uiRunIntervalSS2000 = SECONDS_PER_DAY;
		}
	}
	return uiRunIntervalSS2000;
}

U32 LogParserScheduledTasks_GetTaskNextRunTimestamp(ScheduledTask* pTask)
{
	U32 uNowSS2000 = timeSecondsSince2000();
	U32 uiInterval = LogParserScheduledTasks_GetTaskIntervalInSeconds(pTask);
	if (pTask->uiFirstRunSS2000 < uNowSS2000)
		return pTask->uiFirstRunSS2000 + (((uNowSS2000 - pTask->uiFirstRunSS2000) / uiInterval) + 1) * uiInterval;
	else
		return pTask->uiFirstRunSS2000;
}

static void GetDownloadableFilesRecurse(ScheduledTaskDownloadLink*** peaDownloadsOut, const char* pchFullPath, const char* pchFilter, const char* pchSubDir)
{
	WIN32_FIND_DATAA wfd;
	HANDLE h;
	__time32_t time;
	ScheduledTaskDownloadLink* pDownloadLink = NULL;
	char* estrFileMask = NULL;

	estrPrintf(&estrFileMask, "%s/*.*", pchFullPath);
	h = FindFirstFile_UTF8(estrFileMask, &wfd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
			

				if (wfd.cFileName[0] != '.')
				{
					char* pchFull = NULL;
					char* pchSub = NULL;
					if (pchSub)
						estrPrintf(&pchSub, "%s/%s", pchSub, wfd.cFileName);
					else
						estrPrintf(&pchSub, "%s", wfd.cFileName);

					estrPrintf(&pchFull, "%s/%s", pchFullPath, wfd.cFileName);

					GetDownloadableFilesRecurse(peaDownloadsOut, pchFull, pchFilter, pchSub);
					estrDestroy(&pchFull);
					estrDestroy(&pchSub);
				}
				continue;
			}

			if (!pchFilter || strstri(pchFullPath, pchFilter))
			{
				pDownloadLink = StructCreate(parse_ScheduledTaskDownloadLink);
				pDownloadLink->uiSizeInBytes = wfd.nFileSizeLow + (((U64)wfd.nFileSizeHigh) << 32LL);
				_FileTimeToUnixTime(&wfd.ftLastWriteTime, &time, false);
				if (pchSubDir)
					estrPrintf(&pDownloadLink->pchLink, "<a href=\"/file/logparser/%u/fileSystem/%s/%s\">Download</a>", GetAppGlobalID(), pchSubDir, wfd.cFileName);
				else
					estrPrintf(&pDownloadLink->pchLink, "<a href=\"/file/logparser/%u/fileSystem/%s\">Download</a>", GetAppGlobalID(), wfd.cFileName);
				pDownloadLink->uiLastModified = timeGetSecondsSince2000FromWindowsTime32(time);
				if (pchSubDir)
					estrPrintf(&pDownloadLink->pchName, "%s/%s", pchSubDir, wfd.cFileName);
				else
				{
					estrCopy2(&pDownloadLink->pchName, wfd.cFileName);
				}
				eaPush(peaDownloadsOut, pDownloadLink);
			}
		}
		while(FindNextFile_UTF8(h, &wfd));
		FindClose(h);
	}

	estrDestroy(&estrFileMask);
}

static void GetDownloadableReportInfo(ScheduledTaskDownloadLink*** peaDownloadsOut, const  char* pchFilter)
{
	GetDownloadableFilesRecurse(peaDownloadsOut, g_Tasks.pchDownloadDir, pchFilter, NULL);
}

//scheduler
static bool GetScheduledTasksInfoStructForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	ScheduledTasksMonitorInfoStruct info = {0};
	int i;
	bool bRetVal;
	const char* pchDownloadFilter = NULL;
	for (i = 0; i < eaSize(&pArgList->ppUrlArgList); i++)
	{
		if (stricmp(pArgList->ppUrlArgList[i]->arg, "svrfilter") == 0)
		{
			pchDownloadFilter = pArgList->ppUrlArgList[i]->value;
			break;
		}
	}
	estrClear(&info.pchEnableScheduler);
	estrClear(&info.pchDisableScheduler);
	estrClear(&info.pchStatus);
	if (pchDownloadFilter)
	{
		eaDestroyStruct(&info.eaTasks, parse_ScheduledTaskInfo);
	}
	else
	{
		eaSetSizeStruct(&info.eaTasks, parse_ScheduledTaskInfo, eaSize(&g_Tasks.eaTasks));
		for (i = 0; i < eaSize(&info.eaTasks); i++)
		{
			info.eaTasks[i]->pchName = g_Tasks.eaTasks[i]->pchName;
			info.eaTasks[i]->uiLastRun = g_Tasks.eaTasks[i]->uiLastRunStartSS2000;
			info.eaTasks[i]->uiNextRun = g_Tasks.eaTasks[i]->uiNextRunSS2000;
			info.eaTasks[i]->bDisabled = g_Tasks.eaTasks[i]->bDisable;
			estrClear(&info.eaTasks[i]->pchEnableTask);
			estrClear(&info.eaTasks[i]->pchDisableTask);
			if (info.eaTasks[i]->bDisabled)
				estrPrintf(&info.eaTasks[i]->pchEnableTask, "EnableSingleScheduledTask %s 1", g_Tasks.eaTasks[i]->pchName);
			else
				estrPrintf(&info.eaTasks[i]->pchDisableTask, "EnableSingleScheduledTask %s 0", g_Tasks.eaTasks[i]->pchName);

			if (g_Tasks.eaTasks[i]->uiNumRunsSinceStartup > 0)
				info.eaTasks[i]->uiAvgDuration = g_Tasks.eaTasks[i]->iRunDurationsSum/g_Tasks.eaTasks[i]->uiNumRunsSinceStartup;
			else 
				info.eaTasks[i]->uiAvgDuration = 0;
		}


		if (s_bEnableScheduledTasks)
		{
			estrPrintf(&info.pchDisableScheduler, "EnableScheduledTasks 0");
			estrPrintf(&info.pchStatus, "The scheduler is currently running.");
		}
		else
		{
			estrPrintf(&info.pchEnableScheduler, "EnableScheduledTasks 1");
			estrPrintf(&info.pchStatus, "The scheduler is currently DISABLED.");
		}
	}
	GetDownloadableReportInfo(&info.eaDownloads, pchDownloadFilter);

	bRetVal = ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
		&info, parse_ScheduledTasksMonitorInfoStruct, iAccessLevel, 0, pStructInfo, eFlags);

	StructReset(parse_ScheduledTasksMonitorInfoStruct, &info);
	return bRetVal;
}

void LogParserScheduledTasks_Init(void) 
{
	int i; 
	char* pchConfigFileName = NULL;
	const char* pchClusterName = ShardCommon_GetClusterName();
	const char* pchShardName = GetShardNameFromShardInfoString();
	bool bLoaded = false;

	//load files
	if (pchShardName && pchShardName[0])
	{
		estrPrintf(&pchConfigFileName, "server/LogParserConfig/%s_LogParserScheduledTasks.txt", pchShardName);
		printf("Attempting to load shard-level config file %s...", pchConfigFileName);
		bLoaded = ParserReadTextFile(pchConfigFileName, parse_ScheduledTasksDef, &g_Tasks, 0);
		if (bLoaded)
			printf("Success!\n");
		else
			printf("Failed!\n");
	}

	if (!bLoaded && pchClusterName && pchClusterName[0])
	{
		estrClear(&pchConfigFileName);
		estrPrintf(&pchConfigFileName, "server/LogParserConfig/%s_LogParserScheduledTasks.txt", pchClusterName);
		printf("Attempting to load cluster-level config file %s...", pchConfigFileName);
		bLoaded = ParserReadTextFile(pchConfigFileName, parse_ScheduledTasksDef, &g_Tasks, 0);
		if (bLoaded)
			printf("Success!\n");
		else
			printf("Failed!\n");
	}

	if (!bLoaded)
	{
		printf("Attempting to load default config file server/LogParserConfig/LogParserScheduledTasks.txt...");
		bLoaded = ParserReadTextFile("server/LogParserConfig/LogParserScheduledTasks.txt", parse_ScheduledTasksDef, &g_Tasks, 0);
		if (bLoaded)
			printf("Success!\n");
		else
			printf("Failed!\n");
	}
	
	if (eaSize(&g_Tasks.eaTasks) > 0)
	{
		//Set timestamps and validate repeat type.
		for (i = eaSize(&g_Tasks.eaTasks)-1; i >= 0; i--)
		{
			ScheduledTask* pTask = g_Tasks.eaTasks[i];
		
			if (pTask)
			{
				if (!pTask->pchName)
				{
					ErrorFilenamef(pTask->pchFilename, "Scheduled task index %d is missing a name. It will not be run.", i);
					eaRemove(&g_Tasks.eaTasks, i);
					continue;
				}
				else if (!pTask->pchExecutable)
				{
					ErrorFilenamef(pTask->pchFilename, "Scheduled task \"%s\" is missing an executable string. It will not be run.", pTask->pchName);
					eaRemove(&g_Tasks.eaTasks, i);
					continue;
				}
				else if (pTask->eScheduleType == 0)
				{
					ErrorFilenamef(pTask->pchFilename, "Scheduled task \"%s\" has an invalid repeat type. It will not be run.", pTask->pchName);
					eaRemove(&g_Tasks.eaTasks, i);
					continue;
				}
				else if (pTask->eScheduleType == kScheduledTaskRepeatType_Specified && pTask->uiSpecifiedIntervalSeconds < 1800)
				{
					ErrorFilenamef(pTask->pchFilename, "Scheduled task \"%s\" given a specified interval of less than 1800 seconds (half an hour), which is invalid. It will not be run.", pTask->pchName);
					eaRemove(&g_Tasks.eaTasks, i);
					continue;
				}
				pTask->uiFirstRunSS2000 = timeGetSecondsSince2000FromLocalDateString(pTask->pchFirstRunDateString);
				pTask->uiNextRunSS2000 = LogParserScheduledTasks_GetTaskNextRunTimestamp(pTask);

				estrCopy2(&pTask->estrNextRunLocalDateString, timeGetFilenameLocalDateStringFromSecondsSince2000(pTask->uiNextRunSS2000));
			}
		}

		if (g_Tasks.pchDefaultMailServer && g_Tasks.pchDefaultMailServer[0])
			strcpy(s_pchMailServer, g_Tasks.pchDefaultMailServer);

		eaQSort(g_Tasks.eaTasks, ScheduledTaskSort);
	}

	if (pchConfigFileName)
		estrDestroy(&pchConfigFileName);

	GenericFileServing_ExposeDirectory(strdup(g_Tasks.pchDownloadDir));

	RegisterCustomXPathDomain(".scheduler", GetScheduledTasksInfoStructForHttp, NULL);
}

void LogParserScheduledTasks_RunTask(ScheduledTask* pTask)
{
	char* estrCommand = NULL;
	char pchLastRunDate[20];
	char pchAdjustedLastRunDate[20];
	char pchThisRunDate[20];
	char pchLastRunDayStartDate[20];
	U32 uiRunIntervalSS2000 = LogParserScheduledTasks_GetTaskIntervalInSeconds(pTask);
	int i;
	int iPID = 0;
	char* pchLogDir = NULL;
	QueryableProcessHandle* pHandle;
	char* estrMonitorURL = NULL;
	U32 uStartOfLocalDaySS2000;
	U32 uNow = timeSecondsSince2000();
	SYSTEMTIME t;

	timerLocalSystemTimeFromSecondsSince2000(&t,pTask->uiNextRunSS2000 - uiRunIntervalSS2000 + pTask->iTaskTimestampOffsetInSeconds);
	t.wHour = t.wMinute = t.wSecond = t.wMilliseconds = 0;
	uStartOfLocalDaySS2000 = timerSecondsSince2000FromLocalSystemTime(&t);

	if (gbStandAlone || gbNoShardMode)
	{
		if (eaSize(&gStandAloneOptions.ppDirectoriesToScan) > 0)
		{
			int j;
			for (j = 0; j < eaSize(&gStandAloneOptions.ppDirectoriesToScan); j++)
			{
				if (j == 0)
					estrConcatf(&pchLogDir, "%s", gStandAloneOptions.ppDirectoriesToScan[j]);
				else
					estrConcatf(&pchLogDir, ",%s", gStandAloneOptions.ppDirectoriesToScan[j]);
			}
		}
		else if (LogDirectoryForLaunched && LogDirectoryForLaunched[0])
		{
			estrCopy2(&pchLogDir, LogDirectoryForLaunched);
		}
	}
	else
	{
		estrPrintf(&pchLogDir, "%s", logGetDir());

		//In dev mode we need to be looking in the LogServer folder inside the default_log_dir.
		if (isDevelopmentMode())
			estrAppend2(&pchLogDir, "LogServer/");
	}

	if (gbStandAlone || gbNoShardMode)
	{
		estrPrintf(&estrMonitorURL, "%s:%u%s.scheduler", getComputerName(), gbNoShardMode ? DEFAULT_WEBMONITOR_GLOBAL_LOG_PARSER : siStartingPortNum, LinkToThisServer());		
	}
	else
	{
		estrPrintf(&estrMonitorURL, "%s%s.scheduler", GetShardControllerTrackerFromShardInfoString(), LinkToThisServer());		
	}

	estrStackCreate(&estrCommand);

	timeMakeFilenameDateStringFromSecondsSince2000(pchLastRunDate, pTask->uiNextRunSS2000 - uiRunIntervalSS2000 + pTask->iTaskTimestampOffsetInSeconds);
	timeMakeFilenameDateStringFromSecondsSince2000(pchAdjustedLastRunDate, pTask->uiNextRunSS2000 - uiRunIntervalSS2000 + pTask->iTaskTimestampOffsetInSeconds + pTask->uiLastRunAdjustment);
	timeMakeFilenameDateStringFromSecondsSince2000(pchThisRunDate, pTask->uiNextRunSS2000 + pTask->iTaskTimestampOffsetInSeconds);
	timeMakeFilenameDateStringFromSecondsSince2000(pchLastRunDayStartDate, uStartOfLocalDaySS2000);

	strfmt_FromArgsEx(&estrCommand, pTask->pchExecutable, false, langGetCurrent(), STRFMT_STRING("AdjustedLastRun", pchAdjustedLastRunDate), STRFMT_STRING("LastRun", pchLastRunDate), STRFMT_STRING("LastRunDayStart", pchLastRunDayStartDate), STRFMT_STRING("CoreExecPath", fileCoreExecutableDir()), STRFMT_STRING("ExecPath", fileExecutableDir()), STRFMT_STRING("ThisRun", pchThisRunDate), STRFMT_STRING("LogDir", pchLogDir), STRFMT_STRING("WorkingDir", GetWorkingDir()), STRFMT_STRING("DownloadDir", GetDownloadDir()), STRFMT_END);

	for (i = 0; i < eaSize(&pTask->eaArgs); i++)
	{
		estrConcatf(&estrCommand, " ");
		strfmt_FromArgsEx(&estrCommand, pTask->eaArgs[i], false, langGetCurrent(), STRFMT_STRING("AdjustedLastRun", pchAdjustedLastRunDate), STRFMT_STRING("LastRun", pchLastRunDate), STRFMT_STRING("LastRunDayStart", pchLastRunDayStartDate), STRFMT_STRING("CoreExecPath", fileCoreExecutableDir()), STRFMT_STRING("ExecPath", fileExecutableDir()), STRFMT_STRING("ThisRun", pchThisRunDate), STRFMT_STRING("LogDir", pchLogDir), STRFMT_STRING("WorkingDir", GetWorkingDir()), STRFMT_STRING("DownloadDir", GetDownloadDir()), STRFMT_END);
	}

	if (pTask->bAddMailserverArg && s_pchMailServer && s_pchMailServer[0])
	{
		estrConcatf(&estrCommand, " -Mailserver %s", s_pchMailServer);
	}

	if (pTask->bAddShardInfoArgs)
	{
		const char* pchControllerTracker = GetShardControllerTrackerFromShardInfoString();
		if (stricmp(pchControllerTracker, "(null)") != 0)
			estrConcatf(&estrCommand, " -ControllerTracker %s", pchControllerTracker);
		estrConcatf(&estrCommand, " -ShardName %s", GetShardNameFromShardInfoString());
		estrConcatf(&estrCommand, " -MonitorURL %s", estrMonitorURL);
	}
	printf("Executing scheduled task \"%s\" at %s...\n", pTask->pchName, timeGetLocalDateString());

	//Start non-blocking process. Calling system() would hang LogParser for potentially a very long time.
	pHandle = StartQueryableProcessEx(estrCommand, NULL, false, true, false, NULL, &iPID);

	if (!iPID || !pHandle)
	{
		WARNING_NETOPS_ALERT("LOGPARSER_SCHEDULER_TASK_FAIL", "LogParser failed to start scheduled task \"%s\". Full command line: %s", pTask->pchName, estrCommand);
	}
	else
	{
		ScheduledTaskHandle* pTaskHandle = calloc(1, sizeof(ScheduledTaskHandle));
		pTaskHandle->pHandle = pHandle;
		pTaskHandle->pDef = pTask;
		pTaskHandle->uiTaskStart = timeSecondsSince2000();
		eaPush(&g_eaProcHandles, pTaskHandle);
	}

	estrDestroy(&estrCommand);

	pTask->uiNextRunSS2000 = LogParserScheduledTasks_GetTaskNextRunTimestamp(pTask);

	pTask->uiLastRunStartSS2000 = timeSecondsSince2000();

	estrCopy2(&pTask->estrNextRunLocalDateString, timeGetDateStringFromSecondsSince2000(pTask->uiNextRunSS2000));
}

#include "LogParserScheduledTasks_h_ast.c"