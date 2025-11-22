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
#include "utils.h"
#include "qsortG.h"

char *pLeftDir = NULL;
AUTO_CMD_ESTRING(pLeftDir, Left);

char *pRightDir = NULL;
AUTO_CMD_ESTRING(pRightDir, Right);

char **ppSubstringsToExclude = NULL;
AUTO_COMMAND;
void Exclude(char *pSubstr)
{
	eaPush(&ppSubstringsToExclude, strdup(pSubstr));
}

char *pErrorFile = NULL;
AUTO_CMD_ESTRING(pErrorFile, ErrorFile);

bool bCompareFileContents = false;
AUTO_CMD_INT(bCompareFileContents, CompareFileContents);

bool bCompareFileDates = false;
AUTO_CMD_INT(bCompareFileDates, CompareFileDates);

void FAIL(FORMAT_STR const char  *fmt, ...)
{
	char *pFullStr = NULL;
	estrGetVarArgs(&pFullStr, fmt);

	if (pErrorFile)
	{
		FILE *pFile = fopen(pErrorFile, "wt");
		fprintf(pFile, "%s\n", pFullStr);
		fclose(pFile);
	}

	printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", pFullStr);

	exit(-1);
}

bool Excluded(char *pName)
{
	FOR_EACH_IN_EARRAY(ppSubstringsToExclude, char, pSubStr)
	{
		if (strstri(pName, pSubStr))
		{
			return true;
		}
	}
	FOR_EACH_END;

	return false;
}

void NormalizeList(char *pDirName, char **ppInList, char ***pppOutList)
{

	char *pTemp = NULL;
	int iDirNameLen = strlen(pDirName);

	estrStackCreate(&pTemp);

	FOR_EACH_IN_EARRAY(ppInList, char, pFileName)
	{
		estrCopy2(&pTemp, pFileName + iDirNameLen);
		backSlashes(pTemp);
		estrReplaceOccurrences(&pTemp, "\\\\", "\\");

		if (Excluded(pTemp))
		{
			continue;
		}

		eaPush(pppOutList, strdup(pTemp));
	}
	FOR_EACH_END;

	estrDestroy(&pTemp);
}


void CompareFileContents(char *pDir1, char *pDir2, char *pFile)
{
	char name1[CRYPTIC_MAX_PATH];
	char name2[CRYPTIC_MAX_PATH];
	char *pBuf1, *pBuf2;
	int iSize1, iSize2;

	sprintf(name1, "%s%s", pDir1, pFile);
	sprintf(name2, "%s%s", pDir2, pFile);


	pBuf1 = fileAlloc(name1, &iSize1);
	pBuf2 = fileAlloc(name2, &iSize2);

	if (!pBuf1)
	{
		FAIL("Couldn't open %s for comparing\n", name1);
	}
	if (!pBuf2)
	{
		FAIL("Couldn't open %s for comparing\n", name2);
	}

	if (iSize1 != iSize2)
	{
		FAIL("Nonmatching file size for %s\n", pFile);
	}

	if (memcmp(pBuf1, pBuf2, iSize1))
	{
		FAIL("Nonmatching file contents for %s\n", pFile);
	}
}

int main(int argc,char **argv)
{
	bool bNeedToConfigure = false;
	char **ppLeft_Raw = NULL;
	char **ppRight_Raw = NULL;
	char **ppLeft = NULL;
	char **ppRight = NULL;
	int iLeft = 0;
	int iRight = 0;
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

	if (!(pLeftDir && pRightDir))
	{
		printf("Usage: CompareDirectories -left dir1 -right dir2 [-ErrorFile filename] [-exclude str1][-exclude str2]... [-CompareFileDates] [-CompareFileContents]\n");
		return -1;
	}

	if (!dirExists(pLeftDir))
	{
		FAIL("Left dir %s does not exist", pLeftDir);
	}

	if (!dirExists(pRightDir))
	{
		FAIL("Right dir %s does not exist", pRightDir);
	}

	ppLeft_Raw = fileScanDir(pLeftDir);
	ppRight_Raw = fileScanDir(pRightDir);

	NormalizeList(pLeftDir, ppLeft_Raw, &ppLeft);
	NormalizeList(pRightDir, ppRight_Raw, &ppRight);

	fileScanDirFreeNames(ppLeft_Raw);
	fileScanDirFreeNames(ppRight_Raw);

	eaQSort(ppLeft, strCmp);
	eaQSort(ppRight, strCmp);

	while (iLeft < eaSize(&ppLeft) && iRight < eaSize(&ppRight))
	{
		char *pLeftFile = ppLeft[iLeft];
		char *pRightFile = ppRight[iRight];

		if (stricmp(pLeftFile, pRightFile) != 0)
		{
			if (strCmp(&pLeftFile, &pRightFile) < 0)
			{
				FAIL("%s exists in %s, but not in %s\n",
					pLeftFile, pLeftDir, pRightDir);
			}
			else
			{
				FAIL("%s exists in %s, but not in %s\n", 
					pRightFile, pRightDir, pLeftDir);
			}
		}

		if (bCompareFileDates)
		{

			char name1[CRYPTIC_MAX_PATH];
			char name2[CRYPTIC_MAX_PATH];

			sprintf(name1, "%s%s", pLeftDir, pLeftFile);
			sprintf(name2, "%s%s", pRightDir, pLeftFile);

			if (fileLastChangedSS2000(name1) != fileLastChangedSS2000(name2))
			{
				FAIL("Modification time for %s doesn't match\n",
					pLeftFile);
			}
		}

		if (bCompareFileContents)
		{
			CompareFileContents(pLeftDir, pRightDir, pLeftFile);
		}

		iLeft++;
		iRight++;
	}

	if (iLeft < eaSize(&ppLeft))
	{
		FAIL("%s exists in %s, but not in %s\n", 
			ppLeft[iLeft], pLeftDir, pRightDir);
	}
	else if (iRight < eaSize(&ppRight))
	{
		FAIL("%s exists in %s, but not in %s\n", 
			ppRight[iRight], pRightDir, pLeftDir);
	}

	return 0;

EXCEPTION_HANDLER_END

}

