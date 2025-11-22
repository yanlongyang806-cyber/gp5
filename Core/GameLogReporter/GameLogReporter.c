#include "GameLogReporter.h"
#include "FolderCache.h"
#include "loggingEnums.h"
#include "netsmtp.h"
#include "logging.h"
#include "memorymonitor.h"
#include "CrypticPorts.h"
#include "winsock.h"

#include "GameLogEconomyReport.h"
#include "GameLogPerCharacterNumericCSV.h"
#include "GameLogNewReportTemplate.h"
#include "GameLogMicrotransReport.h"
#include "GameLogUniquesReport.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "GameLogReporter"););

void DefaultSendEmail(void);

//assumed to be of size kGameLogReportType_Count
GameLogReportTypeMap logTypeTable[] = {
	{
		kGameLogReportType_Economy, 
			EconomyReport_Init, 
			EconomyReport_AnalyzeParsedLog, 
			EconomyReport_GenerateReport,
			DefaultSendEmail
	},
	{
		kGameLogReportType_TrackedItemCSVs, 
			PerCharacterNumericCSV_Init, 
			PerCharacterNumericCSV_AnalyzeParsedLog, 
			PerCharacterNumericCSV_GenerateReport, 
			PerCharacterNumericCSV_SendResultEmail
	},
	{
		kGameLogReportType_Microtransaction, 
			MicrotransReport_Init, 
			MicrotransReport_AnalyzeParsedLog, 
			MicrotransReport_GenerateReport, 
			DefaultSendEmail
	},
	{
		kGameLogReportType_DailyUniques, 
			UniquesReport_Init, 
			UniquesReport_AnalyzeParsedLog, 
			UniquesReport_GenerateReport, 
			DefaultSendEmail
	},
	{
		kGameLogReportType_ExampleReport, 
			NewReportTemplate_Init, 
			NewReportTemplate_AnalyzeParsedLog, 
			NewReportTemplate_GenerateReport, 
			NewReportTemplate_SendResultEmail
	}
};

char g_pchLogPaths[MAX_PATH] = "D:/Neverwinter_Logs/";
char g_pchLogTypes[128] = "GameServer";
char g_pchReportDir[MAX_PATH] = "D:/GameLogReports/Output/";
char g_pchWorkingDir[MAX_PATH] = "D:/GameLogReports/";
char g_pchTimeStart[128] = ""; //date strings of the format YYYY-MM-DD_HH:MM:SS
char g_pchTimeEnd[128] = "";
char g_pchReportType[64] = "Economy";
char g_pchLogCategoriesToParse[256] = "GAMEECON_MONEY,GAMEECON_XP,GAMEECON_TIMECURRENCY";
char g_pchEmailRecipients[256] = ""; //email addresses to send the report to
char g_pchControllerTracker[64] = "";
char g_pchShardName[64] = "";
char g_pchMonitorURL[MAX_PATH] = "";

//comma-delimited list of container ID ranges to accept. Used to filter out certain shards.
// For example, on NW, all UGC characters have an ID in the 300,000,000 range, so you would 
// run this executable with "-ValidShardIDs 100000000,200000000,400000000"
char g_pchShardIDs[256] = "";

//derived from g_pchTimeStart and g_pchTimeEnd
U32 g_uTimeStartSS2000 = 0;
U32 g_uTimeEndSS2000 = 0;


AUTO_CMD_STRING(g_pchLogPaths, LogPath) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchLogTypes, LogTypes) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchReportDir, ReportDir) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchWorkingDir, WorkingDir) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchTimeStart, TimeStart) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchTimeEnd, TimeEnd) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchReportType, ReportType) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchLogCategoriesToParse, Categories) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchEmailRecipients, EmailTo) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchShardIDs, ValidShardIDs) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchControllerTracker, ControllerTracker) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchShardName, ShardName) ACMD_CMDLINE;
AUTO_CMD_STRING(g_pchMonitorURL, MonitorURL) ACMD_CMDLINE;

S32* eaLogCategoriesToParse = NULL;

char* s_estrLastOutputFileWritten = NULL;

GameLogReportTypeMap* pCurFuncTable = NULL;

void SendShardAlert(const char* pchKey, const char* pchAlertString, ...)
{
	char* estrBuf = NULL;
	estrClear(&estrBuf);
	VA_START(args, pchAlertString);
	estrConcatfv_dbg(&estrBuf, __FILE__, __LINE__, pchAlertString, args);
	VA_END();

	if (g_pchControllerTracker && g_pchControllerTracker[0])
	{
		char* estrCmdLine = NULL;
		estrStackCreate(&estrCmdLine);
		estrPrintf(&estrCmdLine, "SendAlert -controllerTrackerName %s -level Warning -criticalsystemname %s -alertKey %s -category Netops -alertString \"%s\"", g_pchControllerTracker, g_pchShardName, pchKey, estrBuf); 
		system(estrCmdLine);
		estrDestroy(&estrCmdLine);
	}
	else
	{
		assertmsgf(0, "Attempted to send a shard alert, but no controllertracker specified. Alert string: %s", estrBuf);
	}

	estrDestroy(&estrBuf);
}

bool ReadLogFiles()
{
	char** ppFiles = NULL;
	char** ppLogTypes = NULL;
	char** ppLogPaths = NULL;
	int i, iType, iPath;
	FileNameBucketList* pList = NULL;
	LogParsingRestrictions restrictions = {0};
	U32* uiValidServerIDs = NULL;
	char* context = NULL;
	const char* pchTok = NULL;
	int iLinesParsed = 0;

	restrictions.iStartingTime = g_uTimeStartSS2000;
	restrictions.iEndingTime = g_uTimeEndSS2000;

	if (g_pchShardIDs[0])
	{
		pchTok = strtok_s(g_pchShardIDs, ",", &context);
		while (pchTok)
		{
			U32 uiID = atoi(pchTok);
			if (uiID > 0)
				ea32Push(&uiValidServerIDs, uiID);
			pchTok = strtok_s(NULL, ",", &context);
		}
	}

	DivideString(g_pchLogTypes, ",", &ppLogTypes, DIVIDESTRING_POSTPROCESS_ESTRINGS);
	DivideString(g_pchLogPaths, ",", &ppLogPaths, DIVIDESTRING_POSTPROCESS_ESTRINGS);
	for (iType = 0; iType < eaSize(&ppLogTypes); iType++)
	{
		char** ppFilesOfType = NULL;
		char* estrFinalPath = NULL;
		for (iPath = 0; iPath < eaSize(&ppLogPaths); iPath++)
		{
			estrClear(&estrFinalPath);
			estrPrintf(&estrFinalPath, "%s/%s/", ppLogPaths[iPath], ppLogTypes[iType]);
			ppFilesOfType = fileScanDir(estrFinalPath);

			if (eaSize(&ppFilesOfType) <= 0)
			{
				log_printf(LOG_GAMELOGREPORTER_ERRORS, "GameLogReporter failed to find any log files in the specified directories: %s", g_pchLogPaths);
			}

			for (i = eaSize(&ppFilesOfType) - 1; i>= 0; i--)
			{
				const char *pKey;
				GlobalType eType;
				enumLogCategory eCategory;

				if (!(pKey = GetKeyFromLogFilename(ppFilesOfType[i])))
				{
					assertmsgf(0, "Can't process filename %s", ppFilesOfType[i]);
				}

				if (!SubdivideLoggingKey(pKey, &eType, &eCategory))
				{
					log_printf(LOG_GAMELOGREPORTER_ERRORS, "Can't subdivide key %s, which we got from file %s", pKey, ppFilesOfType[i]);
				}

				if (eType == GLOBALTYPE_NONE)
				{
					log_printf(LOG_GAMELOGREPORTER_ERRORS, "Log had type GLOBALTYPE_NONE, which we got from file %s", ppFilesOfType[i]);
				}

				if (ea32Find(&eaLogCategoriesToParse, eCategory) != -1)
				{
					eaPush(&ppFiles, ppFilesOfType[i]);
					eaRemoveFast(&ppFilesOfType, i);
				}
			}
			if (ppFilesOfType)
			{
				eaDestroyEx(&ppFilesOfType, NULL);
				ppFilesOfType = NULL;
			}
		}
		estrDestroy(&estrFinalPath);
	}

	if (eaSize(&ppFiles) <= 0)
	{
		log_printf(LOG_GAMELOGREPORTER_ERRORS, "No log files found matching the given parameters!");
		return false;
	}

	pList = CreateFileNameBucketList(NULL, NULL, 0);
	DivideFileListIntoBuckets(pList, &ppFiles);
	ApplyRestrictionsToBucketList(pList, &restrictions);

	fileScanDirFreeNames(ppFiles);

	{
		char* pchNextLog = NULL;
		char* pchNextFilename = NULL;
		U32 uiLastHour = 0;
		U32 iNextTime = 0;
		U32 iNextID = 0;
		ParsedLog** ppLogs = NULL;
		ParsedLog curLog = {0};

		StructInit(parse_ParsedLog, &curLog);

		while (GetNextLogStringFromBucketList(pList, &pchNextLog, &pchNextFilename, &iNextTime, &iNextID))
		{
			if (uiLastHour < (iNextTime/3600)*3600)
			{
				if (uiLastHour > 0)
					loadend_printf("Done.");
				uiLastHour = (iNextTime/3600)*3600;
				loadstart_printf("Parsing logs @ %s... ", timeGetDateStringFromSecondsSince2000(uiLastHour));
			}

			if (iNextTime < restrictions.iStartingTime || iNextTime >= restrictions.iEndingTime)
				continue;

			StructReset(parse_ParsedLog, &curLog);
			if (ReadLineIntoParsedLog(&curLog, pchNextLog, (int)strlen(pchNextLog), &restrictions, NULL, 0, NULL))
			{
				U32 uiShardID = curLog.iServerID - (curLog.iServerID % 100000000);

				if (ea32Size(&uiValidServerIDs) > 0 && ea32Find(&uiValidServerIDs, uiShardID) == -1)
					continue;

				pCurFuncTable->pProcessFunc(&curLog);
				iLinesParsed++;
			}
		}

		loadend_printf("Done.");
	}
	return iLinesParsed > 0;
}

void SendReportEmail(char* attachment)
{
	char *pSubjectToUse = NULL;
	SMTPMessageRequest *pReq;
	char *pResultEstr = NULL;

	estrPrintf(&pSubjectToUse, "%s log report", g_pchReportType);

	pReq = StructCreate(parse_SMTPMessageRequest);

	DivideString(g_pchEmailRecipients, ",", &pReq->to, DIVIDESTRING_POSTPROCESS_ESTRINGS);

	estrPrintf(&pReq->from, "GameLogReporter");

	estrPrintf(&pReq->subject, "%s", pSubjectToUse);
	estrPrintf(&pReq->body, "%s report generated at %s.\n\nLog categories used: %s.\n\n", g_pchReportType, timeGetLocalDateString(), g_pchLogCategoriesToParse);
	estrConcatf(&pReq->body, "Log times: %s",timeGetDateStringFromSecondsSince2000(g_uTimeStartSS2000));
	estrConcatf(&pReq->body, " to %s.\n\n", timeGetDateStringFromSecondsSince2000(g_uTimeEndSS2000));
	
	if (attachment)
	{
		estrPrintf(&pReq->attachfilename, "%s", attachment);
		estrPrintf(&pReq->attachsuggestedname, "%s_report_%s.xml", g_pchReportType, timeGetDateNoTimeStringFromSecondsSince2000(g_uTimeStartSS2000));
	}

	if (!smtpMsgRequestSend_Blocking(pReq, &pResultEstr))
		SendShardAlert("GAMELOGREPORTER_FATALERROR", "Failed to send report email! Mailserver: %s Recipients: %s Result string: %s", smtpGetMailServer(), g_pchEmailRecipients, pResultEstr);

	StructDestroy(parse_SMTPMessageRequest, pReq);
	estrDestroy(&pResultEstr);
}

void SendDownloadLinkEmail(char* pchFilter)
{
	char *pSubjectToUse = NULL;
	SMTPMessageRequest *pReq;
	char *pResultEstr = NULL;
	char* estrHostname = NULL;

	estrForceSize(&estrHostname, 128);

	estrPrintf(&pSubjectToUse, "%s log report", g_pchReportType);

	pReq = StructCreate(parse_SMTPMessageRequest);

	DivideString(g_pchEmailRecipients, ",", &pReq->to, DIVIDESTRING_POSTPROCESS_ESTRINGS);

	estrPrintf(&pReq->from, "GameLogReporter");

	estrPrintf(&pReq->subject, "%s", pSubjectToUse);
	estrPrintf(&pReq->body, "%s report generated at %s.\n\nLog categories used: %s.\n\n", g_pchReportType, timeGetLocalDateString(), g_pchLogCategoriesToParse);
	estrConcatf(&pReq->body, "Log times: %s",timeGetDateStringFromSecondsSince2000(g_uTimeStartSS2000));
	estrConcatf(&pReq->body, " to %s.\n\n", timeGetDateStringFromSecondsSince2000(g_uTimeEndSS2000));
	estrConcatf(&pReq->body, "To download the result files, use this link: http://%s&svrfilter=%s\n", g_pchMonitorURL, pchFilter );
	 
	if (!smtpMsgRequestSend_Blocking(pReq, &pResultEstr))
		SendShardAlert("GAMELOGREPORTER_FATALERROR", "Failed to send report email! Mailserver: %s Recipients: %s Result string: %s", smtpGetMailServer(), g_pchEmailRecipients, pResultEstr);

	StructDestroy(parse_SMTPMessageRequest, pReq);
	estrDestroy(&pResultEstr);
	estrDestroy(&estrHostname);
}

void SendErrorEmail(const char* pchError)
{
	char *pSubjectToUse = NULL;
	SMTPMessageRequest *pReq;
	char *pResultEstr = NULL;

	estrPrintf(&pSubjectToUse, "Unsuccessful %s log report", g_pchReportType);

	pReq = StructCreate(parse_SMTPMessageRequest);

	DivideString(g_pchEmailRecipients, ",", &pReq->to, DIVIDESTRING_POSTPROCESS_ESTRINGS);

	estrPrintf(&pReq->from, "GameLogReporter");

	estrPrintf(&pReq->subject, "%s", pSubjectToUse);
	estrPrintf(&pReq->body, "No %s report was generated at %s because %s. Please notify a programmer.\n\nLog categories used: %s.\n\n", g_pchReportType, timeGetLocalDateString(), pchError, g_pchLogCategoriesToParse);
	estrConcatf(&pReq->body, "Log times: %s",timeGetDateStringFromSecondsSince2000(g_uTimeStartSS2000));
	estrConcatf(&pReq->body, " to %s.\n\n", timeGetDateStringFromSecondsSince2000(g_uTimeEndSS2000));
	estrConcatf(&pReq->body, "(In local time, that's %s",timeGetLocalDateStringFromSecondsSince2000(g_uTimeStartSS2000));
	estrConcatf(&pReq->body, " to %s.)\n\n", timeGetLocalDateStringFromSecondsSince2000(g_uTimeEndSS2000));

	if (!smtpMsgRequestSend_Blocking(pReq, &pResultEstr))
		SendShardAlert("GAMELOGREPORTER_FATALERROR", "Failed to send report email! Mailserver: %s Recipients: %s Result string: %s", smtpGetMailServer(), g_pchEmailRecipients, pResultEstr);

	StructDestroy(parse_SMTPMessageRequest, pReq);
	estrDestroy(&pResultEstr);
}

bool WriteOutputFile(const char* pchFilename, const char* pchData, bool bGZip)
{
	char* estrFullPath = NULL;
	FILE* pReportFile = NULL;
	
	estrPrintf(&estrFullPath, "%s/%s", g_pchReportDir, pchFilename);
	makeDirectoriesForFile(estrFullPath);
	pReportFile = fopen(estrFullPath, "w");

	if (!pReportFile)
	{
		SendShardAlert("GAMELOGREPORTER_FATALERROR", "Output report file %s could not be opened.", estrFullPath);

		estrDestroy(&estrFullPath);

		return false;
	}
	
	fprintf(pReportFile, "%s", pchData);

	fclose(pReportFile);

	printf("Successfully wrote to file %s.\n", estrFullPath);

	if (bGZip)
	{
		fileGzip(estrFullPath);
		printf("Successfully gzipped file %s.\n", estrFullPath);
	}

	estrDestroy(&s_estrLastOutputFileWritten);
	s_estrLastOutputFileWritten = estrFullPath;

	return true;
}

//USAGE: GameLogReporter -logs <dir> -report <type> -output <dir> -timestart <hh:mm:ss dd/mm/yyyy> -timeend <hh:mm:ss dd/mm/yyyy>
int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	U32 uNow = 0;
	const char* pchTok = NULL;
	char* pchContext = NULL;
	char* estrReportOutput = NULL;
	char* pchLogsToParseDup = NULL;
	char* estrLogOutputPath = NULL;
	char* estrCmdLine = NULL;
	bool bSuccess = true;
	char **argv;

	EXCEPTION_HANDLER_BEGIN;
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER;
	DO_AUTO_RUNS;

	memMonitorInit();
	SetAppGlobalType(GLOBALTYPE_GAMELOGREPORTER);

	gimmeDLLDisable(1);

	InitPreparsedStashTable();
	InitServerDataStashTable();

	SetRefSystemSuppressUnknownDicitonaryWarning_All(true);

	FolderCacheChooseMode();

	cmdParseCommandLine(argc, argv);

	logSetDir("");

	for (i = 0; i < argc; i++)
		estrConcatf(&estrCmdLine, "%s ", argv[i]);

	log_printf(LOG_GAMELOGREPORTER_ERRORS, "Beginning new report. Command line: %s", estrCmdLine);

	//determine start and end time cutoffs
	if (g_pchTimeStart[0] && g_pchTimeEnd[0])
	{
		g_uTimeStartSS2000 = timeGetSecondsSince2000FromDateString(g_pchTimeStart);//g_u24HoursAgoSS2000;
		g_uTimeEndSS2000 = timeGetSecondsSince2000FromDateString(g_pchTimeEnd);
	}
	else
	{
		uNow = timeSecondsSince2000();
		g_uTimeStartSS2000 = (uNow/(24*60*60) - 1) * (24*60*60) + timeLocalOffsetFromUTC();//start of yesterday
		g_uTimeEndSS2000 = uNow/(24*60*60) * (24*60*60) + timeLocalOffsetFromUTC();//start of today
	}

	//get list of categories
	pchLogsToParseDup = strdup(g_pchLogCategoriesToParse);
	pchTok = strtok_s(pchLogsToParseDup, ",", &pchContext);
	while(pchTok)
	{
		int iVal = StaticDefineInt_FastStringToInt(enumLogCategoryEnum, pchTok, -1);
		if (iVal < 0)
		{
			printf("ERROR: Unrecognized log category %s... These logs will not be parsed.\n", pchTok);
		}
		ea32Push(&eaLogCategoriesToParse, iVal);
		pchTok = strtok_s(NULL, ",", &pchContext);
	}
	free(pchLogsToParseDup);

	//assert if no categories to parse
	if (ea32Size(&eaLogCategoriesToParse) <= 0)
		SendShardAlert("GAMELOGREPORTER_FATALERROR", "No valid log categories specified. There's nothing to parse. Arg string: %s", g_pchLogCategoriesToParse);

//	utilitiesLibStartup();

	for (i = 0; i < kGameLogReportType_Count; i++)
	{
		if (logTypeTable[i].eType == StaticDefineInt_FastStringToInt(GameLogReportTypeEnum, g_pchReportType, -1))
		{
			pCurFuncTable = &(logTypeTable[i]);
			break;
		}
	}

	//assert if unrecognized type
	if (!pCurFuncTable)
		SendShardAlert("GAMELOGREPORTER_FATALERROR", "Unrecognized report type %s.", g_pchReportType);

	pCurFuncTable->pInitFunc();
	bSuccess = ReadLogFiles();
	pCurFuncTable->pFinishFunc();
	if (bSuccess && g_pchEmailRecipients[0])
	{
		pCurFuncTable->pEmailFunc();
	}
	else if (g_pchEmailRecipients[0])
	{
		//Didn't actually parse any logs. Send an apology email instead.
		SendErrorEmail("no logs were successfully parsed");
	}
	EXCEPTION_HANDLER_END;

	log_printf(LOG_GAMELOGREPORTER_ERRORS, "End report.");

	return 0;
}

void DefaultSendEmail()
{
	SendReportEmail(s_estrLastOutputFileWritten);
}

#include "GameLogReporter_h_ast.c"