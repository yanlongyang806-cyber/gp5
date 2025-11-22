#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "timing.h"
#include "StringUtil.h"
#include "fileUtil2.h"
#include "earray.h"
#include "LogParsing.h"
#include "gimmeDLLWrapper.h"

//should be the directory whose subdirectories actually container log files... ie "patchedfightclubserver/fightclubserver/logs"
char rootDirName[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(rootDirName, rootDirName);

//logs this # of days older, or older, will be sorted down into subdirectories
int iMinDaysOldToSort = 5;
AUTO_CMD_INT(iMinDaysOldToSort, MinDaysOldToSort);

void TryToMoveFile(char *pDirName, char *pShortFileName, U32 iTime)
{
	SYSTEMTIME t;
	
	char newDirName[CRYPTIC_MAX_PATH];

	static char *pTemp = NULL;
	

	timerLocalSystemTimeFromSecondsSince2000(&t,iTime);

	sprintf(newDirName, "%s/%d/%s/%d", pDirName, t.wYear, GetMonthName(t.wMonth), t.wDay);

	estrPrintf(&pTemp, "%s/foo.txt", newDirName);
	mkdirtree(pTemp);

	estrPrintf(&pTemp, "move %s/%s %s", pDirName, pShortFileName, newDirName);
	backSlashes(pTemp);

	system_w_timeout(pTemp, 600);
}


int main(int argc,char **argv)
{
	int i;

	char **ppFileNames = NULL;
	U32 iCutoffTime;

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

	if (!rootDirName[0] || rootDirName[1] != ':')
	{
		printf("No -rootDirName specified, or not an absolute path. Fail!\n");
		printf("Syntax: -rootDirName name [-MinDaysOld n]\n");
		return -1;
	}

	ppFileNames = fileScanDirRecurseNLevels(rootDirName, 1);

	iCutoffTime = timeSecondsSince2000() - iMinDaysOldToSort * 24 * 60 * 60;

	for (i = 0; i < eaSize(&ppFileNames); i++)
	{
		U32 iTime;
		static char *pShortName = NULL;
		static char *pDirName = NULL;

		if (i % 100 == 0)
		{
			printf("%d / %d\n", i, eaSize(&ppFileNames));
		}

		estrGetDirAndFileName(ppFileNames[i], &pDirName, &pShortName);
		iTime = GetTimeFromLogFilename(pShortName);
		if (iTime && iTime < iCutoffTime)
		{
			TryToMoveFile(pDirName, pShortName, iTime);
		}
	}

	EXCEPTION_HANDLER_END

}
