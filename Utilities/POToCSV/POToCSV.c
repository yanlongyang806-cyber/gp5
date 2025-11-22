#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "Estring.h"
#include "earray.h"
#include "StringCache.h"
#include "timing.h"
#include "textparser.h"

#include "fileutil2.h"
#include "Regex.h"
#include "StringUtil.h"
#include "../POToTranslateFiles/POFileUtils.h"

static void printFailure(FORMAT_STR const char *pFmt, ...)
{
	char *pStr = NULL;
	estrGetVarArgs(&pStr, pFmt);
	consolePushColor();
	consoleSetColor(COLOR_RED | COLOR_BRIGHT, 0);
	printf("ERROR! ERROR! ERROR!\n");
	printf("%s", pStr);
	printf("\n\n");
	consolePopColor();
	estrDestroy(&pStr);
}

void WriteStringToCSVFile(char *pString, FILE *pFile)
{
	if (!pString || !pString[0])
	{
		fprintf(pFile, "\" \"");
		return;
	}

	fputc('"', pFile);

	while (*pString)
	{
		if (*pString == '"')
		{
			fputc('"', pFile);
			fputc('"', pFile);
		}
		else
		{
			fputc(*pString, pFile);
		}

		pString++;
	}

	fputc('"', pFile);
}

void WriteStringArrayToCSVFile(char **ppStrings, FILE *pOutFile)
{
	int iNumStrings = eaSize(&ppStrings);
	char *pTempString = NULL;
	int i;

	if (!iNumStrings)
	{
		fprintf(pOutFile, "\" \"");
		return;
	}


	estrStackCreate(&pTempString);

	for (i = 0; i < iNumStrings; i++)
	{
		if (strchr(ppStrings[i], ','))
		{
			printFailure("String <<%s>> contains a comma... this is illegal", ppStrings[i]);
			exit(-1);
		}

		estrConcatf(&pTempString, "%s%s", i == 0 ? "" : ", ", ppStrings[i]);
	}

	WriteStringToCSVFile(pTempString, pOutFile);

	estrDestroy(&pTempString);
}




void WriteBlocksToCSVFile(POBlockRaw ***pppBlocks, char *pOutFileName)
{

	FILE *pOutFile = fopen(pOutFileName, "wt");
	if (!pOutFile)
	{
		printFailure("Couldn't open %s for writing\n", pOutFileName);
		exit(-1);
	}

	fprintf(pOutFile, "Description, Keys, Files, Scopes, Context, ID, String\n");

	FOR_EACH_IN_EARRAY_FORWARDS((*pppBlocks), POBlockRaw, pBlock)
	{
		WriteStringToCSVFile(pBlock->pDescription, pOutFile);
		fputc(',', pOutFile);
		WriteStringArrayToCSVFile(pBlock->ppKeys, pOutFile);
		fputc(',', pOutFile);
		WriteStringArrayToCSVFile(pBlock->ppFiles, pOutFile);
		fputc(',', pOutFile);
		WriteStringArrayToCSVFile(pBlock->ppScopes, pOutFile);
		fputc(',', pOutFile);
		WriteStringToCSVFile(pBlock->pCtxt, pOutFile);
		fputc(',', pOutFile);	
		WriteStringToCSVFile(pBlock->pID, pOutFile);
		fputc(',', pOutFile);
		WriteStringToCSVFile(pBlock->pStr, pOutFile);
		fputc('\n', pOutFile);
	}
	FOR_EACH_END;

	fclose(pOutFile);
}


char *pPOFile = NULL;
AUTO_CMD_ESTRING(pPOFile, POFile) ACMD_CMDLINE;

int main(int argc,char **argv)
{
	int i;


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

	if (pPOFile)
	{
		char *pCSVFile = NULL;
		POBlockRaw **ppBlocks = NULL;


		estrCopy2(&pCSVFile, pPOFile);
		estrReplaceOccurrences(&pCSVFile, ".po", ".csv");
		ReadTranslateBlocksFromFile(pPOFile, &ppBlocks);
		WriteBlocksToCSVFile(&ppBlocks, pCSVFile);
	}




	EXCEPTION_HANDLER_END

}

