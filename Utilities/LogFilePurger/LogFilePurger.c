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
#include "textparser.h"
#include "LogFilePurger_c_ast.h"
#include "fileutil2.h"
#include "LogParsing.h"
#include "StashTable.h"
#include "gimmeDLLWrapper.h"
#include "logging.h"

AUTO_STRUCT;
typedef struct LogPurgeDefinition
{
	const char *pLogKey; AST(KEY POOL_STRING STRUCTPARAM) //includes both server type and log filename
	int iPurgeDaysCutoff;
	bool bFoundAtLeastOne; NO_AST
} LogPurgeDefinition;

AUTO_STRUCT;
typedef struct LogPurgeDefinitionList
{
	LogPurgeDefinition **ppDefinitions; AST(NAME(PurgeDefinition))
} LogPurgeDefinitionList;

/*
{
	PurgeDefinition
	{
		SeverType GAMESERVER
		LogName MISSION_EVENTS
		PurgeDaysCutoff 3
	}
}
*/
static char sLogDefinitionListFileName[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(sLogDefinitionListFileName, PurgeDefFileName);

static char sLogPurgeDirectory[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(sLogPurgeDirectory, PurgeDirectory);

static bool sbActuallyDelete = false;
AUTO_CMD_INT(sbActuallyDelete, ActuallyDelete);

static bool siDeletionThrottleCount = 10;
AUTO_CMD_INT(siDeletionThrottleCount, DelThrottleCount);

static int siDeletionThrottleSleepMS = 100;
AUTO_CMD_INT(siDeletionThrottleSleepMS, DelThrottleMS);

//ignore all files other than .gz files... needed in order to do
//gz-to-xz recompression
static bool sbOnlyGZFiles = false;
AUTO_CMD_INT(sbOnlyGZFiles, OnlyGZFiles);

//only one of this and -ActuallyDelete can be set... after finding
//the "files to purge", go in and use 7z.exe to recompress them from .gz to .xz,
//using this set of command lines (for which Aaron LaF is to blame):
//
// 7z e PATCHSERVER_INFO.log_2011-12-06_17-00-00.gz
// 7z a -txz -mx9 -mmt=off PATCHSERVER_INFO.log_2011-12-06_17-00-00.xz PATCHSERVER_INFO.log_2011-12-06_17-00-00
static bool sbDoGZToXZRecompression = false;
AUTO_CMD_INT(sbDoGZToXZRecompression, DoGZToXZRecompression);

//for testing purposes... don't delete .gz files after recompressing, just to avoid doing irreversible things
static bool sbDontDeleteGZFilesAfterRecompression = false;
AUTO_CMD_INT(sbDontDeleteGZFilesAfterRecompression, DontDeleteGZFilesAfterRecompression);

//what level of compression to use... has to be one of the values
//that can go after -mx on a 7zip command line
static int siRecompressionLevel = 9;
AUTO_CMD_INT(siRecompressionLevel, RecompressionLevel);

//extra args to pass to 7z.exe during compression
static char *spExtra7zArgs = NULL;
AUTO_CMD_ESTRING(spExtra7zArgs, Extra7zArgs);

//directory for temporary uncompressed files during decompression
static char *spUncompressedTempDir = NULL;
AUTO_CMD_ESTRING(spUncompressedTempDir, UncompressedTempDir);


static LogPurgeDefinitionList *spPurgeDefinitionList = NULL;

AUTO_STRUCT;
typedef struct KeyGroupReport
{
	const char *pKey;

	int iTotalFileCount;
	S64 iTotalFileSize;

	int iToDeleteCount;
	S64 iToDeleteSize;

	U32 iOldest;
} KeyGroupReport;

static char **sppFilesWithNoKey = NULL;
static char **sppFilesToDelete = NULL;

static StashTable sKeyGroupsByKey = NULL;

KeyGroupReport **sppReports = NULL;

void FAIL(FORMAT_STR const char *pFmt, ...)
{
	char *pFailString = NULL;
	estrGetVarArgs(&pFailString, pFmt);

	consolePushColor();
	consoleSetColor(COLOR_RED | COLOR_HIGHLIGHT, 0);

	printf("%s\n\n", pFailString);
	consolePopColor();
	exit(-1);
}

static bool IsLogDirectory(char *pDirName)
{
	char testDir[CRYPTIC_MAX_PATH];

	if (strEndsWith(pDirName, "logServer"))
	{
		return true;
	}

	sprintf(testDir, "%s/LogServer", pDirName);
	return dirExists(testDir);
}

void AttemptToLoadDefinitionList(void)
{
	if (!sLogDefinitionListFileName[0])
	{
		if (sbActuallyDelete)
		{
			FAIL("PurgeDefFileName not specified, can't ActuallyDelete");
		}
		return;
	}

	spPurgeDefinitionList = StructCreate(parse_LogPurgeDefinitionList);
	if (!ParserReadTextFile(sLogDefinitionListFileName, parse_LogPurgeDefinitionList, spPurgeDefinitionList, 0))
	{
		char *pTempString = NULL;
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserReadTextFile(sLogDefinitionListFileName, parse_LogPurgeDefinitionList, spPurgeDefinitionList, 0);
		ErrorfPopCallback();

		FAIL("Couldn't read Purge definition list from %s. Parse errors:\n%s", 
			sLogDefinitionListFileName, pTempString);
	}

	if (!eaSize(&spPurgeDefinitionList->ppDefinitions))
	{
		FAIL("Loaded purge definitions from %s, but it appears to be empty", sLogDefinitionListFileName);
	}

	FOR_EACH_IN_EARRAY(spPurgeDefinitionList->ppDefinitions, LogPurgeDefinition, pDefinition)
	{
		char tempDupKey[1024];

		if (!(pDefinition->pLogKey && pDefinition->pLogKey[0]))
		{
			FAIL("Purge definition file %s includes a definition with no log key, this is illegal",
				sLogDefinitionListFileName);
		}

		strcpy(tempDupKey, pDefinition->pLogKey);
		forwardSlashes(tempDupKey);
		pDefinition->pLogKey = allocAddString(tempDupKey);

		if (pDefinition->iPurgeDaysCutoff < 1)
		{
			FAIL("Purge definition file %s includes a definition, for log key %s, with an invalid purge days cutoff (must be >= 1)",
				sLogDefinitionListFileName, pDefinition->pLogKey);
		}
	}
	FOR_EACH_END;
}


void FoundFileWithNoKey(char *pFullName, struct _finddata32_t* fileInfo)
{
	eaPush(&sppFilesWithNoKey, strdup(pFullName));
}

void FoundFileWithKey(char *pFullName, const char *pKey, U32 iTime, struct _finddata32_t* fileInfo)
{
	KeyGroupReport *pReport;
	LogPurgeDefinition *pPurgeDefinition;
	static int siCount = 0;

	siCount++;
	if (siCount % 4096 == 0)
	{
		printf("Scanned %d files\n", siCount);
	}

	if (!stashFindPointer(sKeyGroupsByKey, pKey, &pReport))
	{
		pReport = StructCreate(parse_KeyGroupReport);
		pReport->pKey = pKey;
		stashAddPointer(sKeyGroupsByKey, pKey, pReport, false);
		eaPush(&sppReports, pReport);
	}

	pReport->iTotalFileCount++;
	pReport->iTotalFileSize += fileInfo->size;

	if (!pReport->iOldest || iTime < pReport->iOldest)
	{
		pReport->iOldest = iTime;
	}

	if (spPurgeDefinitionList)
	{
		pPurgeDefinition = eaIndexedGetUsingString(&spPurgeDefinitionList->ppDefinitions, pKey);
		if (pPurgeDefinition)
		{
			pPurgeDefinition->bFoundAtLeastOne = true;

			if (timeSecondsSince2000() - pPurgeDefinition->iPurgeDaysCutoff * 24 * 60 * 60 > iTime)
			{
				char *pDuped = strdup(pFullName);
				backSlashes(pDuped);
				eaPush(&sppFilesToDelete, pDuped);
				pReport->iToDeleteCount++;
				pReport->iToDeleteSize += fileInfo->size;
			}
		}
	}
}


static FileScanAction sFileScanProcessor(FileScanContext *pContext)
{
	char fullName[CRYPTIC_MAX_PATH];
	const char *pKey;
	char key_ForwardSlashes[1024];
	

	U32 iTime;

	if (pContext->fileInfo->attrib & _A_SUBDIR) 
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	if (sbOnlyGZFiles && !strEndsWith(pContext->fileInfo->name, ".gz"))
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	sprintf(fullName, "%s\\%s", pContext->dir, pContext->fileInfo->name);


	pKey = GetKeyFromLogFilename(fullName);
	if (!pKey || stricmp(pKey, "Unknown") == 0)
	{
		FoundFileWithNoKey(fullName, pContext->fileInfo);
		return FSA_EXPLORE_DIRECTORY;	
	}

	iTime = GetTimeFromLogFilename(fullName);

	if (!iTime)
	{
		FoundFileWithNoKey(fullName, pContext->fileInfo);
		return FSA_EXPLORE_DIRECTORY;
	}

	strcpy(key_ForwardSlashes, pKey);
	forwardSlashes(key_ForwardSlashes);

	FoundFileWithKey(fullName, allocAddString(key_ForwardSlashes), iTime, pContext->fileInfo);

	return FSA_EXPLORE_DIRECTORY;
}

FileScanContext sFileScanContext = 
{
	sFileScanProcessor
};

int SortReportsByFileCount(const KeyGroupReport **pReport1, const KeyGroupReport **pReport2)
{
	if ((*pReport1)->iToDeleteSize > (*pReport2)->iToDeleteSize)
	{
		return -1;
	}
	else if ((*pReport1)->iToDeleteSize < (*pReport2)->iToDeleteSize)
	{
		return 1;
	}	

	if ((*pReport1)->iToDeleteCount > (*pReport2)->iToDeleteCount)
	{
		return -1;
	}
	else if ((*pReport1)->iToDeleteCount < (*pReport2)->iToDeleteCount)
	{
		return 1;
	}

	if ((*pReport1)->iTotalFileSize > (*pReport2)->iTotalFileSize)
	{
		return -1;
	}
	else if ((*pReport1)->iTotalFileSize < (*pReport2)->iTotalFileSize)
	{
		return 1;
	}

	if ((*pReport1)->iTotalFileCount > (*pReport2)->iTotalFileCount)
	{
		return -1;
	}
	else if ((*pReport1)->iTotalFileCount < (*pReport2)->iTotalFileCount)
	{
		return 1;
	}



	return 0;

}


int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	gimmeDLLDisable(1);

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	printf("Basic commands: -PurgeDefFileName, -PurgeDirectory, -OnlyGZFiles, ZERO OR ONE OF -ActuallyDelete -DoGZToXZRecompression, -DontDeleteGZFilesAfterRecompression, -Extra7zArgs, -RecompressionLevel, -UncompressedTempDir, -DelThrottleCount (def 10), -DelThrottleMS (def 100)\n");

	if (sbActuallyDelete && sbDoGZToXZRecompression)
	{
		FAIL("can't specify both -ActuallyDelete and -DoGZToXZRecompression");
	}

	if (sbDoGZToXZRecompression && !sbOnlyGZFiles)
	{
		FAIL("-DoGZToXZRecompression requires -OnlyGZFiles");
	}



//	hideConsoleWindow();

	if (!sLogPurgeDirectory[0])
	{
		FAIL("-PurgeDirectory not specified");
	}

	if (!IsLogDirectory(sLogPurgeDirectory))
	{
		FAIL("%s does not appear to be a log directory (it has no /logserver subdirectory)",
			sLogPurgeDirectory);
	}

	AttemptToLoadDefinitionList();

	sKeyGroupsByKey = stashTableCreateAddress(16);

	fileScanDirRecurseContext(sLogPurgeDirectory, &sFileScanContext);

	eaQSort(sppReports, SortReportsByFileCount);

	if (eaSize(&sppFilesWithNoKey))
	{

		printf("WARNING: %d files in %s did not have properly formatted log file names and can not be processed:\n",
			eaSize(&sppFilesWithNoKey), sLogPurgeDirectory);
		
		for (i = 0; i < eaSize(&sppFilesWithNoKey); i++)
		{
			printf("%s\n", sppFilesWithNoKey[i]);
		}

		printf("\n\n\n");
	}

	if (spPurgeDefinitionList)
	{
		FOR_EACH_IN_EARRAY(spPurgeDefinitionList->ppDefinitions, LogPurgeDefinition, pDefinition)
		{
			if (!pDefinition->bFoundAtLeastOne)
			{
				printf("WARNING: Did not find a single file that matched specified key %s. This may indicate a typo\n\n",
					pDefinition->pLogKey);
			}
		}
		FOR_EACH_END;
	}


	for (i = 0; i < eaSize(&sppReports); i++)
	{
		static char *spSize = NULL;

		estrMakePrettyBytesString(&spSize, sppReports[i]->iTotalFileSize);
		printf("Log key %s. %d files totalling %s.\n",
			sppReports[i]->pKey, sppReports[i]->iTotalFileCount, spSize);
		printf("Oldest file: %s (local)\n", 
			timeGetLocalDateStringFromSecondsSince2000(sppReports[i]->iOldest));

		if (sppReports[i]->iToDeleteCount)
		{
			estrMakePrettyBytesString(&spSize, sppReports[i]->iToDeleteSize);
			printf("%d files totalling %s will be deleted.\n",
				sppReports[i]->iToDeleteCount, spSize);
		}
		else
		{
			printf("Nothing will be deleted\n");
		}

		printf("\n");
	}

	if (sbActuallyDelete)
	{
		char logFileName[CRYPTIC_MAX_PATH];
		sprintf(logFileName, "%s\\LogFilePurger_Deleted.txt", sLogPurgeDirectory);
		printf("About to delete %d files... will log all deletions to %s\n", 
			eaSize(&sppFilesToDelete), logFileName);

		for (i = 0;i < eaSize(&sppFilesToDelete); i++)
		{
			if (siDeletionThrottleCount && siDeletionThrottleSleepMS && (i % siDeletionThrottleCount == 0))
			{
				Sleep(siDeletionThrottleSleepMS);
			}

			filelog_printf(logFileName, "Deleting %s\n", sppFilesToDelete[i]);
			
			if (_unlink(sppFilesToDelete[i]))
			{
				printf("Error: could not delete %s\n", sppFilesToDelete[i]);
				filelog_printf(logFileName, "Error: could not delete %s\n", sppFilesToDelete[i]);
			}


			if (i % 100 == 0)
			{
				printf("Deleted %d/%d files\n", i, eaSize(&sppFilesToDelete));
			}
		}
	}
	else if (sbDoGZToXZRecompression)
	{
		char logFileName[CRYPTIC_MAX_PATH];
		char *pShortFileName = NULL;
		char *pDirectory = NULL;
		S64 iBytesSaved = 0;
		char *pCompressionTempDir = NULL;
		if (spUncompressedTempDir)
		{
			estrPrintf(&pCompressionTempDir, "%s\\%d", spUncompressedTempDir, getpid());
			mkdirtree_const(STACK_SPRINTF("%s\\foo.txt", pCompressionTempDir));
		}


		sprintf(logFileName, "%s\\LogFilePurger_%d_Recompressed.txt", sLogPurgeDirectory, getpid());
		printf("About to recompress %d files... will log all recompressions to %s\n", 
			eaSize(&sppFilesToDelete), logFileName);

		//want to be low priority because compression will use all cores and thus could squeeze out actual useful stuff going on on this machine
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);



		for (i = 0;i < eaSize(&sppFilesToDelete); i++)
		{
			static char *spSystemString = NULL;
			int unused;
			S64 iOldSize;
			S64 iNewSize;

			estrClear(&pShortFileName);
			estrClear(&pDirectory);
			estrGetDirAndFileNameAndExtension(sppFilesToDelete[i], &pDirectory, &pShortFileName, NULL);

			if (siDeletionThrottleCount && siDeletionThrottleSleepMS && (i % siDeletionThrottleCount == 0))
			{
				Sleep(siDeletionThrottleSleepMS);
			}

			unused = _chdir(pDirectory);

			filelog_printf(logFileName, "Recompressing %s\n", sppFilesToDelete[i]);


			estrPrintf(&spSystemString, "7z e %s.gz", pShortFileName);
		
			if (pCompressionTempDir)
			{
				estrConcatf(&spSystemString, " -o%s", pCompressionTempDir);
			}
			

			if (system(spSystemString))
			{
				FAIL("Unable to uncompress %s", sppFilesToDelete[i]);
			}

			estrPrintf(&spSystemString, "7z a -txz -mx%d %s %s.xz %s%s%s", siRecompressionLevel, spExtra7zArgs ? spExtra7zArgs : "", 
				pShortFileName, 
				pCompressionTempDir ? pCompressionTempDir : "",
				pCompressionTempDir ? "\\" : "",
				pShortFileName);
			
			if (system(spSystemString))
			{
				estrPrintf(&spSystemString, "erase %s%s%s", 
					pCompressionTempDir ? pCompressionTempDir : "",
					pCompressionTempDir ? "\\" : "",
					pShortFileName);
				system(spSystemString);
				FAIL("Unable to recompress %s", sppFilesToDelete[i]);
			}

			iOldSize = fileSize(sppFilesToDelete[i]);
			estrPrintf(&spSystemString, "%s/%s.xz", pDirectory, pShortFileName);

			iNewSize = fileSize(spSystemString);

			if (iOldSize && iNewSize)
			{
				char *pSavedString = NULL;
				iBytesSaved += iOldSize - iNewSize;
				estrMakePrettyBytesString(&pSavedString, iBytesSaved);
				printfColor(COLOR_GREEN | COLOR_BRIGHT, "Saved %s so far...\n", pSavedString);
				estrDestroy(&pSavedString);
			}

			estrPrintf(&spSystemString, "erase %s%s%s", 
				pCompressionTempDir ? pCompressionTempDir : "",
				pCompressionTempDir ? "\\" : "",
				pShortFileName);
			system(spSystemString);

			if (!sbDontDeleteGZFilesAfterRecompression)
			{
				estrPrintf(&spSystemString, "erase %s.gz", pShortFileName);
				system(spSystemString);
			}



			if (i % 100 == 0)
			{
				printf("Recompressed %d/%d files\n", i, eaSize(&sppFilesToDelete));
			}
		}
	}
		



	EXCEPTION_HANDLER_END

}


#include "LogFilePurger_c_ast.c"


