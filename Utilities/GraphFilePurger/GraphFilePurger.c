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
#include "GraphFilePurger_c_ast.h"
#include "fileutil2.h"
#include "LogParsing.h"
#include "StashTable.h"
#include "gimmeDLLWrapper.h"
#include "logging.h"

char *pGraphDirectory = NULL;
AUTO_CMD_ESTRING(pGraphDirectory, GraphDirectory);

char *pPurgeDefinitionFileName = NULL;
AUTO_CMD_ESTRING(pPurgeDefinitionFileName, PurgeDefinitionFile);

bool bActuallyDelete = false;
AUTO_CMD_INT(bActuallyDelete, ActuallyDelete);

static bool siDeletionThrottleCount = 10;
AUTO_CMD_INT(siDeletionThrottleCount, DelThrottleCount);

static int siDeletionThrottleSleepMS = 100;
AUTO_CMD_INT(siDeletionThrottleSleepMS, DelThrottleMS);


AUTO_ENUM;
typedef enum GraphFileType
{
	GFT_CSV,
	GFT_DATA,
	GFT_GRAPH,

	GFT_COUNT
} GraphFileType;

AUTO_STRUCT;
typedef struct GraphFileTypeSpecificInfo
{
	int iFileCount;
	S64 iFileSize;

	int iFilesToBeDeleted;
	S64 iSizeToBeDeleted;

} GraphFileTypeSpecificInfo;

AUTO_STRUCT;
typedef struct GraphInfo
{
	char *pGraphName;
	int iTotalFileCount;
	S64 iTotalFileSize;

	int iFilesToBeDeleted;
	S64 iSizeToBeDeleted;

	int iOldestFile; //in days

	GraphFileTypeSpecificInfo fileTypeSpecificInfo[GFT_COUNT]; AST(INDEX(0, CSV) INDEX(1,DATA) INDEX(2,GRAPH))

} GraphInfo;

AUTO_STRUCT;
typedef struct GraphPurgeDefinition
{
	char *pGraphName; AST(KEY STRUCTPARAM)
	int iDays_Default;
	int iDays[GFT_COUNT]; AST(INDEX(0, DAYS_CSV) INDEX(1, DAYS_DATA) INDEX(2, DAYS_GRAPH))
	bool bUsed; NO_AST
} GraphPurgeDefinition;

AUTO_STRUCT;
typedef struct GraphPurgeDefinitionList
{
	GraphPurgeDefinition **ppPurgeDefinition;
} GraphPurgeDefinitionList;

static StashTable sGraphInfosByName = NULL;

static GraphInfo **sppGraphInfos = NULL;

static GraphPurgeDefinitionList *spPurgeDefinitionList;

static char **sppFilesToDelete = NULL;

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

static GraphInfo *spGraph;
static GraphFileType seFileType;

static U32 GetTimeFromLocalDateName(char *pFileName)
{
	U32 iRetVal = timeGetSecondsSince2000FromLocalDateString(pFileName);

	return iRetVal;
}

GraphPurgeDefinition *FindPurgeDefinitionForGraph(char *pGraphName)
{
	GraphPurgeDefinition *pRetVal;

	if (!spPurgeDefinitionList)
	{
		return NULL;
	}

	pRetVal = eaIndexedGetUsingString(&spPurgeDefinitionList->ppPurgeDefinition, pGraphName);

	if (!pRetVal)
	{
		pRetVal = eaIndexedGetUsingString(&spPurgeDefinitionList->ppPurgeDefinition, "default");
	}

	return pRetVal;
}

static FileScanAction sFileScanProcessor(FileScanContext *pContext)
{
	char *pExpectedExtension;
	U32 iFileTime;
	int iDaysOld;
	GraphPurgeDefinition *pPurgeDefinition;

	if (pContext->fileInfo->attrib & _A_SUBDIR) 
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	if (seFileType == GFT_GRAPH)
	{
		pExpectedExtension = ".html";
	}
	else
	{
		pExpectedExtension = ".txt";
	}

	if (!strEndsWith(pContext->fileInfo->name, pExpectedExtension))
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	if (seFileType == GFT_DATA)
	{
		iFileTime = atoi(pContext->fileInfo->name);
	}
	else
	{
		iFileTime = GetTimeFromLocalDateName(pContext->fileInfo->name);
	}

	if (!iFileTime)
	{
		FAIL("Unable to get time for file %s found for graph %s... something is wrong",
			pContext->fileInfo->name, spGraph->pGraphName);
	}

	iDaysOld = (timeSecondsSince2000() - iFileTime) / (24 * 60 * 60);

	spGraph->iTotalFileCount++;
	spGraph->iTotalFileSize += pContext->fileInfo->size;

	spGraph->fileTypeSpecificInfo[seFileType].iFileCount++;
	spGraph->fileTypeSpecificInfo[seFileType].iFileSize += pContext->fileInfo->size;

	pPurgeDefinition = FindPurgeDefinitionForGraph(spGraph->pGraphName);

	if (pPurgeDefinition)
	{
		int iDaysToUse = pPurgeDefinition->iDays[seFileType];
		if (!iDaysToUse)
		{
			iDaysToUse = pPurgeDefinition->iDays_Default;
		}

		if (iDaysToUse)
		{
			if (iDaysOld > iDaysToUse)
			{
				char fullFileName[CRYPTIC_MAX_PATH];
				sprintf(fullFileName, "%s/%s", pContext->dir, pContext->fileInfo->name);
				backSlashes(fullFileName);
				eaPush(&sppFilesToDelete, strdup(fullFileName));
				spGraph->iFilesToBeDeleted++;
				spGraph->iSizeToBeDeleted += pContext->fileInfo->size;
			}
		}

		pPurgeDefinition->bUsed = true;
	}

	if (iDaysOld > spGraph->iOldestFile)
	{
		spGraph->iOldestFile = iDaysOld;
	}

	return FSA_EXPLORE_DIRECTORY;
}



FileScanContext sFileScanContext = 
{
	sFileScanProcessor
};


void AddGraphFromName(char *pGraphName)
{
	GraphInfo *pGraphInfo = NULL;
	char oldCSVFolderName[CRYPTIC_MAX_PATH];
	char oldDataFolderName[CRYPTIC_MAX_PATH];
	char oldGraphFolderName[CRYPTIC_MAX_PATH];

	if (!sGraphInfosByName)
	{
		sGraphInfosByName = stashTableCreateWithStringKeys(16, StashDefault);

	}

	if (stashFindPointer(sGraphInfosByName, pGraphName, NULL))
	{
		FAIL("Somehow are two copies of graph %s", pGraphName);
	}

	pGraphInfo = StructCreate(parse_GraphInfo);
	pGraphInfo->pGraphName = strdup(pGraphName);
	stashAddPointer(sGraphInfosByName, pGraphInfo->pGraphName, pGraphInfo, false);
	eaPush(&sppGraphInfos, pGraphInfo);

	sprintf(oldCSVFolderName, "%s/%s/OldCSVs", pGraphDirectory, pGraphName);
	sprintf(oldDataFolderName, "%s/%s/OldData", pGraphDirectory, pGraphName);
	sprintf(oldGraphFolderName, "%s/%s/OldGraphs", pGraphDirectory, pGraphName);

	spGraph = pGraphInfo;
	seFileType = GFT_CSV;
	fileScanDirRecurseContext(oldCSVFolderName, &sFileScanContext);
	seFileType = GFT_DATA;
	fileScanDirRecurseContext(oldDataFolderName, &sFileScanContext);
	seFileType = GFT_GRAPH;
	fileScanDirRecurseContext(oldGraphFolderName, &sFileScanContext);
}

void FindGraphInfos(void)
{
	char **ppFiles = fileScanDirFoldersNoSubdirRecurse(pGraphDirectory, FSF_FOLDERS | FSF_RETURNSHORTNAMES);
	int i;

	for (i = 0; i < eaSize(&ppFiles); i++)
	{
		AddGraphFromName(ppFiles[i]);
	}



	fileScanDirFreeNames(ppFiles);
}



int SortGraphsByTotalSize(const GraphInfo **pGraph1, const GraphInfo **pGraph2)
{
	if ((*pGraph1)->iSizeToBeDeleted < (*pGraph2)->iSizeToBeDeleted)
	{
		return -1;
	}
	else if ((*pGraph1)->iSizeToBeDeleted > (*pGraph2)->iSizeToBeDeleted)
	{
		return 1;
	}	



	if ((*pGraph1)->iTotalFileSize < (*pGraph2)->iTotalFileSize)
	{
		return -1;
	}
	else if ((*pGraph1)->iTotalFileSize > (*pGraph2)->iTotalFileSize)
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

	if (argc < 2 || !pGraphDirectory)
	{
		printf("graphFilePurger is a tool to purge the contents of logparser graph folders, typically localdata/logParserGraphs\n");
		printf("(This directory will have a bunch of subdirectories, one named for each logParser Graph)\n");
		printf("\n-GraphDirectory dirName\n\tSpecifies the directory containing the graph files\n\n");
		printf("\n-PurgeDefinitionFile fileName\n\tSpecifies the file which defines which files to purge\n");
		printf("\t(sample file in cryptic\\tools\\bin\\GraphFilePurger_ExamplePurgeDef.txt)\n");
		printf("\t(If unspecified, won't delete anything, will just give space report)\n\n");
		printf("\n-ActuallyDelete\n\tIf specified, actually delete old files, rather than just giving a report\n\n");
		printf("-DelThrottleCount\n\tDelete this many files at a time (to avoid overloading HD or CPU)\n\n");
		printf("-DelThrottleSleepMS\n\tSleep this many seconds between deletions\n\n");
		exit(0);
	}


	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	if (pPurgeDefinitionFileName)
	{
		spPurgeDefinitionList = StructCreate(parse_GraphPurgeDefinitionList);
		if (!ParserReadTextFile(pPurgeDefinitionFileName, parse_GraphPurgeDefinitionList, spPurgeDefinitionList, 0))
		{
			char *pTempString = NULL;
			ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
			ParserReadTextFile(pPurgeDefinitionFileName, parse_GraphPurgeDefinitionList, spPurgeDefinitionList, 0);
			ErrorfPopCallback();

			FAIL("Couldn't read Purge definition list from %s. Parse errors:\n%s", 
				pPurgeDefinitionFileName, pTempString);
		}

		FOR_EACH_IN_EARRAY(spPurgeDefinitionList->ppPurgeDefinition, GraphPurgeDefinition, pPurgeDefinition)
		{
			int iDataDays = pPurgeDefinition->iDays[GFT_DATA] ? pPurgeDefinition->iDays[GFT_DATA] : pPurgeDefinition->iDays_Default;
			int iGraphDays = pPurgeDefinition->iDays[GFT_GRAPH] ? pPurgeDefinition->iDays[GFT_GRAPH] : pPurgeDefinition->iDays_Default;
			int iCSVDays = pPurgeDefinition->iDays[GFT_CSV] ? pPurgeDefinition->iDays[GFT_CSV] : pPurgeDefinition->iDays_Default;

			if (iDataDays && !iGraphDays)
			{
				FAIL("Purge definition %s would delete some data files but not graph files... this is illegal, because data files are what the logParser uses to actually find the graphs and CSVs",
					pPurgeDefinition->pGraphName);
			}
			
			if (iDataDays && !iCSVDays)
			{
				FAIL("Purge definition %s would delete some data files but not CSV files... this is illegal, because data files are what the logParser uses to actually find the graphs and CSVs",
					pPurgeDefinition->pGraphName);
			}

			if (iDataDays && iDataDays < iGraphDays)
			{
				FAIL("Purge definition %s would delete newer data files than graph files... this is illegal, because data files are what the logParser uses to actually find the graphs and CSVs",
					pPurgeDefinition->pGraphName);
			}

			if (iDataDays && iDataDays < iCSVDays)
			{
				FAIL("Purge definition %s would delete newer data files than CSV files... this is illegal, because data files are what the logParser uses to actually find the graphs and CSVs",
					pPurgeDefinition->pGraphName);
			}
		}
		FOR_EACH_END;
	}

	FindGraphInfos();

	if (spPurgeDefinitionList)
	{
		FOR_EACH_IN_EARRAY(spPurgeDefinitionList->ppPurgeDefinition, GraphPurgeDefinition, pPurgeDefinition)
		{
			if (!pPurgeDefinition->bUsed && stricmp(pPurgeDefinition->pGraphName, "default") != 0)
			{
				printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN, "WARNING! Found a purge definition named %s, but found no actual files corresponding to it. This may indicate a typo...",
					pPurgeDefinition->pGraphName);
			}
		}
		FOR_EACH_END;
	}

	eaQSort(sppGraphInfos, SortGraphsByTotalSize);

	FOR_EACH_IN_EARRAY(sppGraphInfos, GraphInfo, pGraphInfo)
	{
		char *pSizeString = NULL;
		char *pSizeString_Graph = NULL;
		char *pSizeString_CSV = NULL;
		char *pSizeString_Data = NULL;
		estrMakePrettyBytesString(&pSizeString, pGraphInfo->iTotalFileSize);
		estrMakePrettyBytesString(&pSizeString_Graph, pGraphInfo->fileTypeSpecificInfo[GFT_GRAPH].iFileSize);
		estrMakePrettyBytesString(&pSizeString_CSV, pGraphInfo->fileTypeSpecificInfo[GFT_CSV].iFileSize);
		estrMakePrettyBytesString(&pSizeString_Data, pGraphInfo->fileTypeSpecificInfo[GFT_DATA].iFileSize);
		
		printf("\n%s has %d files taking up %s, oldest %d days ago\n", pGraphInfo->pGraphName, 
			pGraphInfo->iTotalFileCount, pSizeString, pGraphInfo->iOldestFile);
		printf("Graph files %s, CSV files %s, data files %s\n", pSizeString_Graph, pSizeString_CSV, pSizeString_Data);


		if (pGraphInfo->iFilesToBeDeleted)
		{
			estrMakePrettyBytesString(&pSizeString, pGraphInfo->iSizeToBeDeleted);
			printf("Would delete %d files, taking up %s\n", pGraphInfo->iFilesToBeDeleted, pSizeString);
		}
	}
	FOR_EACH_END;

	if (bActuallyDelete)
	{
		char logFileName[CRYPTIC_MAX_PATH];
		sprintf(logFileName, "%s\\GraphFilePurger_Deleted.txt", pGraphDirectory);
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


	EXCEPTION_HANDLER_END

}


#include "GraphFilePurger_c_ast.c"


