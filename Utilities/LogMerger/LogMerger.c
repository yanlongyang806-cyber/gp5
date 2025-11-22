#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "Estring.h"
#include "earray.h"
#include "StringCache.h"
#include "process_Util.h"
#include "net.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "timing.h"
#include "LogParsingFileBuckets.h"
#include "LogMerger_c_ast.h"
#include "fileutil2.h"
#include "loggingEnums.h"
#include "CrypticPorts.h"
#include "serverLib.h"
#include "logging.h"

//args for logserver:
//-logSetDir c:\temp\logMergeTest\MergedLogsOut -noShardMode -allFilesHeavy -LogFramePerf 0 -NoServerLogPrintf -TryToRotateAllFilesAtOnce -DirectoryForCompletionFiles c:\temp\logMergeTest\CompletionFiles -MessagesAreAlreadySortedByTime

//must be set the same as -logSetDir for the logserver
static char *spDirectoryForLogFiles = NULL;
AUTO_CMD_ESTRING(spDirectoryForLogFiles, DirectoryForLogFiles) ACMD_COMMANDLINE;

#define MAX_SECONDS_TO_KEEP_IN_RAM 20
#define SEND_PAK_SIZE ( 1024 * 1024)

AUTO_STRUCT;
typedef struct DirToLoadLogsFrom
{
	char *pDirName;
	bool bDupsAreExpected;
} DirToLoadLogsFrom;

DirToLoadLogsFrom **ppDirsToLoadFrom = NULL;

AUTO_COMMAND;
void AddLogDir(char *pName, int iDupsAreExpected)
{
	DirToLoadLogsFrom *pDir = StructCreate(parse_DirToLoadLogsFrom);
//	assertmsgf(dirExists(pName), "%s does not seem to exist", pName);
	pDir->pDirName = strdup(pName);
	pDir->bDupsAreExpected = !!iDupsAreExpected;

	eaPush(&ppDirsToLoadFrom, pDir);
}


AUTO_STRUCT;
typedef struct SingleLog
{
	GlobalType eServerType; 
	enumLogCategory eLogCategory;
	U32 iTime;
	U32 iID;
	char *pString;
	bool bDupExpected;
} SingleLog;

AUTO_STRUCT;
typedef struct LogList_OneCategory
{
	char fileName[CRYPTIC_MAX_PATH];
	enumLogCategory eLogCategory; AST(KEY)
	SingleLog **ppLogs;
} LogList_OneCategory;

AUTO_STRUCT;
typedef struct LogLists_OneServerType
{
	GlobalType eServerType; AST(KEY)
	LogList_OneCategory **ppCategoryLists;
} LogLists_OneServerType;

AUTO_STRUCT;
typedef struct AllLogs_OneSecond
{
	U32 iTime; AST(KEY)
	LogLists_OneServerType **ppServerTypeLists;
} AllLogs_OneSecond;

AllLogs_OneSecond **ppAllLogs = NULL;

AUTO_STRUCT;
typedef struct WrappedBucketList
{
	FileNameBucketList *pList; AST(LATEBIND)
	char *pDirName;
	char *pNextLog;
	char *pNextFileName;
	U32 iNextLogTime;
	U32 iNextLogID;
	bool bDupsAreExpected;
	U64 iStartingBytes;
} WrappedBucketList;

WrappedBucketList **ppWrappedBuckets = NULL;

void LoadNextLogIntoWrappedBucket(WrappedBucketList *pWrappedBucket)
{
	U32 siLastTime = pWrappedBucket->iNextLogTime;

	if (!GetNextLogStringFromBucketList(pWrappedBucket->pList, &pWrappedBucket->pNextLog, &pWrappedBucket->pNextFileName,
		&pWrappedBucket->iNextLogTime, &pWrappedBucket->iNextLogID))
	{
		pWrappedBucket->pNextLog = NULL;
	}
	else
	{
		assert(pWrappedBucket->iNextLogTime >= siLastTime);
	}
}

//wrapper around SubdivideLoggingKey...  files that are in the "root" directory of wherever we're scanning
//for log files won't get a globaltype properly, because it comes from the directory. So use
//LOGSERVER as the globaltype for those files
bool SubdivideLoggingKey_WithLogServerDefualt(const char *pKey, char *pFileName, GlobalType *pOutServerType, enumLogCategory *pOutCategory)
{
	char keyCopy[CRYPTIC_MAX_PATH + 100];
	char *pFirstSlash;
	char *pDir = NULL;

	if (SubdivideLoggingKey(pKey, pOutServerType, pOutCategory))
	{
		return true;
	}

	strcpy(keyCopy, pKey);
	pFirstSlash = strchr(keyCopy, '/');
	if (!pFirstSlash)
	{
		pFirstSlash = strchr(keyCopy, '\\');
	}

	if (!pFirstSlash)
	{
		return false;
	}

	*pOutCategory = StaticDefineInt_FastStringToInt(enumLogCategoryEnum, pFirstSlash + 1, -1);
	if (*pOutCategory == -1)
	{
		return false;
	}

	*pFirstSlash = 0;
	estrStackCreate(&pDir);
	estrGetDirAndFileName(pFileName, &pDir, NULL);
	if (strEndsWith(pDir, keyCopy))
	{
		estrDestroy(&pDir);
		*pOutServerType = GLOBALTYPE_LOGSERVER;
		return true;
	}
	else
	{
		estrDestroy(&pDir);
		return false;
	}
}





void InitAllBuckets(U32 iRestartingTime)
{
	LogParsingRestrictions restrictions = {0};

	if (iRestartingTime)
	{
		restrictions.iStartingTime = iRestartingTime;
		restrictions.iEndingTime = timeSecondsSince2000();
	}

	//DirToLoadLogsFrom **ppDirsToLoadFrom
	FOR_EACH_IN_EARRAY(ppDirsToLoadFrom, DirToLoadLogsFrom, pDir)
	{
		WrappedBucketList *pWrappedBucket = StructCreate(parse_WrappedBucketList);
		char **ppFiles;
		int i;



		pWrappedBucket->pDirName = strdup(pDir->pDirName);
		pWrappedBucket->bDupsAreExpected = pDir->bDupsAreExpected;

		ppFiles = fileScanDir(pDir->pDirName);

		assertmsgf(eaSize(&ppFiles), "Couldn't load any files from %s", pDir->pDirName);

		for (i = eaSize(&ppFiles) - 1; i>= 0; i--)
		{
			const char *pKey;
			GlobalType eType;
			enumLogCategory eCategory;
			
			if (!(pKey = GetKeyFromLogFilename(ppFiles[i])))
			{
				assertmsgf(0, "Can't process filename %s", ppFiles[i]);
			}

			if (!SubdivideLoggingKey_WithLogServerDefualt(pKey, ppFiles[i], &eType, &eCategory))
			{
				assertmsgf(0, "Can't subdivide key %s, which we got from file %s", pKey, ppFiles[i]);
			}

			if (eType == GLOBALTYPE_NONE)
			{
				assertmsgf(0, "Can't subdivide key %s, which we got from file %s", pKey, ppFiles[i]);
			}
		}


		

		printf("Going to merge %d files from %s\n", eaSize(&ppFiles), pDir->pDirName);

		pWrappedBucket->pList = CreateFileNameBucketList(NULL, NULL, 0);
		DivideFileListIntoBuckets(pWrappedBucket->pList, &ppFiles);
		ApplyRestrictionsToBucketList(pWrappedBucket->pList, &restrictions);
		pWrappedBucket->iStartingBytes = GetBucketListRemainingBytes(pWrappedBucket->pList);

		fileScanDirFreeNames(ppFiles);

		LoadNextLogIntoWrappedBucket(pWrappedBucket);

		eaPush(&ppWrappedBuckets, pWrappedBucket);
	}
	FOR_EACH_END;
}

SingleLog *GetNextLogFromAllBuckets(void)
{

	WrappedBucketList *pBestWrappedBucket = NULL;
	U32 iBestTime;
	U32 iBestID;

	static U32 siLastTime = 0;
	


	SingleLog *pLog;
	const char *pKey;

	FOR_EACH_IN_EARRAY(ppWrappedBuckets, WrappedBucketList, pWrappedBucket)
	{
		if (pWrappedBucket->pNextLog)
		{
			if (pBestWrappedBucket == NULL || pWrappedBucket->iNextLogTime < iBestTime 
				|| pWrappedBucket->iNextLogTime == iBestTime &&  pWrappedBucket->iNextLogID < iBestID)
			{
				pBestWrappedBucket = pWrappedBucket;
				iBestTime = pWrappedBucket->iNextLogTime;
				iBestID = pWrappedBucket->iNextLogID;
			}
		}
	}
	FOR_EACH_END;

	if (!pBestWrappedBucket)
	{
		return NULL;
	}

	pKey = GetKeyFromLogFilename(pBestWrappedBucket->pNextFileName);
	pLog = StructCreate(parse_SingleLog);
	SubdivideLoggingKey_WithLogServerDefualt(pKey, pBestWrappedBucket->pNextFileName, &pLog->eServerType, &pLog->eLogCategory);
	pLog->iTime = pBestWrappedBucket->iNextLogTime;
	pLog->iID = pBestWrappedBucket->iNextLogID;
	pLog->pString = strdup(pBestWrappedBucket->pNextLog);
	pLog->bDupExpected = pBestWrappedBucket->bDupsAreExpected;

	LoadNextLogIntoWrappedBucket(pBestWrappedBucket);

	assert(pLog->iTime >= siLastTime);
	siLastTime = pLog->iTime;

	return pLog;
}

int SortLogsByID(const SingleLog **ppLog1, const SingleLog **ppLog2)
{
	return (*ppLog2)->iID - (*ppLog1)->iID;
}

#define pLogI ((*pppLogs)[i])
#define pLogJ ((*pppLogs)[j])

void FixupListOfLogs(SingleLog ***pppLogs)
{
	int i, j;

	eaQSort(*pppLogs, SortLogsByID);

	i = eaSize(pppLogs) - 1;


	//for all pairs of logs which have the same ID, which there may be none of or lots of, check if the logs are identical.
	//If they are, remove one. If they are, and neither one has bDupExpected, print an error
	while (i > 0)
	{
		j = i - 1;

		while (j >= 0 && pLogI->iID == pLogJ->iID)
		{
			if (stricmp(pLogI->pString, pLogJ->pString) == 0)
			{
				if (pLogI->bDupExpected)
				{
					//remove J, then decrement i and j
					StructDestroy(parse_SingleLog, eaRemove(pppLogs, j));
					j--;
					i--;
					continue;
				}

				if (pLogJ->bDupExpected)
				{
					//remove I, then break out, which will decrement i and then start us over
					StructDestroy(parse_SingleLog, eaRemove(pppLogs, i));
					break;
				}

				//neither I nor J had dup expected
				printf("ERROR: Found duplicate logs unexpectedtly, %s/%s. Log string: %s",
					GlobalTypeToName(pLogI->eServerType), StaticDefineInt_FastIntToString(enumLogCategoryEnum, pLogI->eLogCategory), pLogI->pString);

				//remove J, then decrement
				StructDestroy(parse_SingleLog, eaRemove(pppLogs, j));
				j--;
				i--;
				continue;
			}
			else
			{
				j--;
			}
		}

		i--;
	}
}



void WriteSingleLog(char *pFileName, SingleLog *pLog)
{
	logDirectWriteWithTime(pFileName, pLog->pString, pLog->iTime);
}


void SendOneSecondOfLogs(void)
{
	AllLogs_OneSecond *pOneSecondToSend = eaRemove(&ppAllLogs, 0);

	if (!pOneSecondToSend)
	{
		return;
	}


	FOR_EACH_IN_EARRAY(pOneSecondToSend->ppServerTypeLists, LogLists_OneServerType, pOneServerType)
	{
		FOR_EACH_IN_EARRAY(pOneServerType->ppCategoryLists, LogList_OneCategory, pOneCategory)
		{
			sprintf(pOneCategory->fileName,  "%s/%s", GlobalTypeToName(pOneServerType->eServerType), 
				StaticDefineIntRevLookup(enumLogCategoryEnum, pOneCategory->eLogCategory));

			FixupListOfLogs(&pOneCategory->ppLogs);
			FOR_EACH_IN_EARRAY_FORWARDS(pOneCategory->ppLogs, SingleLog, pLog)
			{
				WriteSingleLog(pOneCategory->fileName, pLog);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	StructDestroy(parse_AllLogs_OneSecond, pOneSecondToSend);
}

void FlushLists(void)
{
	FILE *pFile;
	char fileName[CRYPTIC_MAX_PATH];

	while (eaSize(&ppAllLogs))
	{
		SendOneSecondOfLogs();
	}

	sprintf(fileName, "%s/Totally.Complete", 
		gpDirectoryForLogCompletionFiles);
	pFile = fopen(fileName, "wt");
	fprintf(pFile, "Complete!");
	fclose(pFile);



}



void PutLogIntoLists(SingleLog *pLog)
{
	AllLogs_OneSecond *pOneSecond = eaIndexedGetUsingInt(&ppAllLogs, pLog->iTime);
	LogLists_OneServerType *pOneServerType;
	LogList_OneCategory *pOneCategory;

	if (!pOneSecond)
	{
		if (eaSize(&ppAllLogs) == MAX_SECONDS_TO_KEEP_IN_RAM)
		{
			SendOneSecondOfLogs();
		}

		pOneSecond = StructCreate(parse_AllLogs_OneSecond);
		pOneSecond->iTime = pLog->iTime;
		eaPush(&ppAllLogs, pOneSecond);
	}

	pOneServerType = eaIndexedGetUsingInt(&pOneSecond->ppServerTypeLists, pLog->eServerType);
	if (!pOneServerType)
	{
		pOneServerType = StructCreate(parse_LogLists_OneServerType);
		pOneServerType->eServerType = pLog->eServerType;
		eaPush(&pOneSecond->ppServerTypeLists, pOneServerType);
	}

	pOneCategory = eaIndexedGetUsingInt(&pOneServerType->ppCategoryLists, pLog->eLogCategory);
	if (!pOneCategory)
	{
		pOneCategory = StructCreate(parse_LogList_OneCategory);
		pOneCategory->eLogCategory = pLog->eLogCategory;
		eaPush(&pOneServerType->ppCategoryLists, pOneCategory);
	}

	eaPush(&pOneCategory->ppLogs, pLog);
}

AUTO_STRUCT;
typedef struct LogFileCompletion
{
	char *pLogFileName; 
	char *pCompletionFileName; AST(ESTRING)
	U32 iTimeStamp;
	bool bCompletionFileExists;
} LogFileCompletion;

static void WaitForTypedWord(char *pWord)
{
	int iLen = (int)strlen(pWord);
	int iIndex = 0;

	while (1)
	{
		char ch = _getch();
		if (ch == pWord[iIndex])
		{
			iIndex++;
			if (iIndex == iLen)
			{
				return;
			}
		}
		else
		{
			iIndex = 0;
		}
	}
}

static void GetHumanVerification(FORMAT_STR const char* format, ...)
{
	char *pFullString = NULL;

	

	estrGetVarArgs(&pFullString, format);
	consolePushColor();
	consoleSetColor(0, COLOR_GREEN | COLOR_BRIGHT);
	printf("%s\n", pFullString);
	consolePopColor();
	estrDestroy(&pFullString);
	printf("Type \"yes\" to proceed\n");
	WaitForTypedWord("yes");
	printf("Proceeding...\n");
	
}

//returns the point in time that we believe we are complete through, as SS2000. 
U32 ProcessCompletionFiles(void)
{
	char **ppLogFiles_Raw = NULL;
	
	LogFileCompletion **ppCompletions = NULL;

	//+1 for the slash
	int iLogDirNameLen = (int)strlen(spDirectoryForLogFiles) + 1;

	int i;

	//the most recent complete file found (meaning that logs in go through that time plus an hour)
	static U32 siNewestCompleteTime = 0;

	//the oldest incomplete file found (meaning that all complete and incomplete files with that
	//timestamp or later must be deleted, and parsing re-started at that time)
	static U32 siOldestIncompleteTime = 0;


	ppLogFiles_Raw = fileScanDir(spDirectoryForLogFiles);

	for (i = 0; i < eaSize(&ppLogFiles_Raw); i++)
	{
		LogFileCompletion *pCompletion = StructCreate(parse_LogFileCompletion);
		pCompletion->pLogFileName = strdup(ppLogFiles_Raw[i]);

		estrPrintf(&pCompletion->pCompletionFileName, "%s/%s.complete", 
			gpDirectoryForLogCompletionFiles, pCompletion->pLogFileName + iLogDirNameLen);

		pCompletion->bCompletionFileExists = fileExists(pCompletion->pCompletionFileName);
		pCompletion->iTimeStamp = GetTimeFromLogFilename(pCompletion->pLogFileName);

		if (pCompletion->bCompletionFileExists)
		{
			siNewestCompleteTime = MAX(siNewestCompleteTime, pCompletion->iTimeStamp);
		}
		else
		{
			if (!siOldestIncompleteTime)
			{
				siOldestIncompleteTime = pCompletion->iTimeStamp;
			}
			else
			{
				siOldestIncompleteTime = MIN(siOldestIncompleteTime, pCompletion->iTimeStamp);
			}
		}


		eaPush(&ppCompletions, pCompletion);
	}

	fileScanDirFreeNames(ppLogFiles_Raw);

	if (!siNewestCompleteTime && !siOldestIncompleteTime)
	{
		GetHumanVerification("No log files have already been written, nothing is incomplete, this appears to be a new run... proceed?");
		return 0;
	}

	if (siOldestIncompleteTime)
	{
		char *pLocalTimeString = timeGetLocalDateStringFromSecondsSince2000(siOldestIncompleteTime);
		char filenameTimeString[256];
		timeMakeFilenameDateStringFromSecondsSince2000(filenameTimeString, siOldestIncompleteTime);

		GetHumanVerification("There are incomplete log files written, starting with %s. Erase all incomplete files and continue from that time?",
			filenameTimeString);

		FOR_EACH_IN_EARRAY(ppCompletions, LogFileCompletion, pCompletion)
		{
			if (pCompletion->iTimeStamp >= siOldestIncompleteTime)
			{
				assertmsgf(DeleteFile(pCompletion->pLogFileName), "Unable to delete file %s", pCompletion->pLogFileName);

				if (pCompletion->bCompletionFileExists)
				{
					assertmsgf(DeleteFile(pCompletion->pCompletionFileName), "Unable to delete file %s", pCompletion->pCompletionFileName);
				}
			}
		}
		FOR_EACH_END;


		eaDestroyStruct(&ppCompletions, parse_LogFileCompletion);
		return siOldestIncompleteTime;
	}

	if (siNewestCompleteTime)
	{
		char fullyCompleteFile[CRYPTIC_MAX_PATH];
		char *pLocalTimeString = timeGetLocalDateStringFromSecondsSince2000(siNewestCompleteTime);
		char filenameTimeString[256];
		timeMakeFilenameDateStringFromSecondsSince2000(filenameTimeString, siNewestCompleteTime);

		sprintf(fullyCompleteFile, "%s/Totally.Complete",gpDirectoryForLogCompletionFiles);
		if (fileExists(fullyCompleteFile))
		{
			printf("%s exists, indicating that we have already fully merged these logs...\n");
			exit(0);
		}



		GetHumanVerification("We have complete files up through local time filename %s, but we do not appear to have fully completed merging... continue?");
		eaDestroyStruct(&ppCompletions, parse_LogFileCompletion);
		return siNewestCompleteTime + 60 * 60;
	}
	
	eaDestroyStruct(&ppCompletions, parse_LogFileCompletion);
	return 0;
}
/*
void LaunchLogServer(void)
{
	char *spCommandLine = NULL;
	char cwd[CRYPTIC_MAX_PATH];
	char dirCopy[CRYPTIC_MAX_PATH];

	estrPrintf(&spCommandLine, "LogServerX64.exe -logSetDir %s -noShardMode -allFilesHeavy -LogFramePerf 0 -NoServerLogPrintf -TryToRotateAllFilesAtOnce -DirectoryForCompletionFiles %s -MessagesAreAlreadySortedByTime",
		spDirectoryForLogFiles, gpDirectoryForLogCompletionFiles);

	getcwd(cwd, ARRAY_SIZE(cwd));
	strcpy(dirCopy, spDirectoryForLaunchingLogServer);
	backSlashes(dirCopy);
	chdir(dirCopy);
	system_detach_with_fulldebug_fixup(spCommandLine, 0, 0);
	chdir(cwd);
}*/

//set up our local logging with all the options that a logserver would get
void ConfigureLogging(void)
{

	gbZipAllLogs = true;
	logSetMaxLogSize(10*1024*1024); // alert for logs over 10 million characters
	logSetDir(spDirectoryForLogFiles);
	logEnableHighPerformance();
	logAutoRotateLogFiles(true);
	logSetUseLogTimeForFileRotation(true);
	
	globCmdParsef("logSetDir %s", spDirectoryForLogFiles);
	globCmdParse("allFilesHeavy 1");
	globCmdParse("LogFramePerf 0");
	globCmdParse("TryToRotateAllFilesAtOnce 1");
	//Note that DirectoryForLogCompletionFiles is already set from the command line

}


int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;
	SingleLog *pLog;
	int iCount = 0;
	U32 iRestartTime;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	if (eaSize(&ppDirsToLoadFrom) < 2)
	{
		printf("ERROR: you must specify at least two directories to load logs from, via -AddLogDir dirname X, where X is 1 if you expect duplicate logs in that dir\n");
		exit(-1);
	}

	if (!spDirectoryForLogFiles || !gpDirectoryForLogCompletionFiles)
	{
		printf("ERROR: you must specify both -DirectoryForLogFiles and -DirectoryForCompletionFiles");
		exit(-1);
	}


	iRestartTime = ProcessCompletionFiles();

	ConfigureLogging();


	eaIndexedEnable(&ppAllLogs, parse_AllLogs_OneSecond);

	InitAllBuckets(iRestartTime);

	while ((pLog = GetNextLogFromAllBuckets()))
	{
		static U32 siLastLogTime = 0;

		if (pLog->iTime < iRestartTime)
		{
			StructDestroy(parse_SingleLog, pLog);
			continue;
		}

		assert(pLog->iTime >= siLastLogTime);
		siLastLogTime = pLog->iTime;

		assertmsgf(pLog->eLogCategory >= 0 && pLog->eLogCategory < LOG_LAST, 
			"Unknown log category %d for log msg %s from server type %s",
			pLog->pString, GlobalTypeToName(pLog->eServerType));

		iCount++;
		if (iCount % 100000 == 0)
		{
			printf("We have read %d logs...\n", iCount);

			FOR_EACH_IN_EARRAY(ppWrappedBuckets, WrappedBucketList, pWrappedBucket)
			{
				U32 iStartTime;
				U32 iEndTime;
				U32 iCurTime;
				static char *spTotalDurationStr = NULL;
				static char *spDoneDurationStr = NULL;
				static char *spRemainingDurationStr = NULL;

				GetLikelyStartingAndEndingTimesFromBucketList(pWrappedBucket->pList, &iStartTime, &iEndTime);
				iCurTime = pWrappedBucket->iNextLogTime;

				timeSecondsDurationToPrettyEString(iEndTime - iStartTime, &spTotalDurationStr);
				timeSecondsDurationToPrettyEString(iCurTime - iStartTime, &spDoneDurationStr);
				timeSecondsDurationToPrettyEString(iEndTime - iCurTime, &spRemainingDurationStr);

				printf("From dir %s, we have approximately %s of logs. We have processed %s of them, %s of logs remain\n",
					pWrappedBucket->pDirName, spTotalDurationStr, spDoneDurationStr, spRemainingDurationStr);

			}
			FOR_EACH_END;

			printf("\n");
		}
	


		PutLogIntoLists(pLog);
	}	

	FlushLists();

	printf("Done with all sending... flushing then closing\n");
	logFlush();
	logWaitForQueueToEmpty();


	EXCEPTION_HANDLER_END

}

#include "LogMerger_c_ast.c"

