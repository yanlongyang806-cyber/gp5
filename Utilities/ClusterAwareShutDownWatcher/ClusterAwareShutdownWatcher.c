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
#include "timing.h"
#include "SentryServerComm.h"
#include "utils.h"

char *pMainExecutableName = NULL;
int iMainExecutablePID = 0;
char *pMainExecutableMachine = NULL;

char **ppMachines = NULL;
char **ppExesToTrack = NULL;
NetLink *gpLinkToSentryServer = NULL;


bool gbFinished = false;

char *pShardName = NULL;
AUTO_CMD_ESTRING(pShardName, ShardName) ACMD_COMMANDLINE;

char *pFolderForExes = NULL;
AUTO_CMD_ESTRING(pFolderForExes, FolderForExes) ACMD_COMMANDLINE;

AUTO_COMMAND;
void MainExecutable(char *pMachineName, char *pExeName, int iPid)
{
	estrCopy2(&pMainExecutableMachine, pMachineName);
	estrCopy2(&pMainExecutableName, pExeName);
	iMainExecutablePID = iPid;
}

AUTO_COMMAND;
void Machine(char *pMachineName)
{
	eaPushUnique(&ppMachines, (char*)allocAddString(pMachineName));
}

AUTO_COMMAND;
void ExeToTrack(char *pExe)
{
	if (strEndsWith(pExe, ".exe"))
	{
		pExe[strlen(pExe)-4] = 0;
	}

	eaPushUnique(&ppExesToTrack, (char*)allocAddString(pExe));
}




static bool sbReturned;
static bool sbFoundOne;

void WaitForMainExecutableCB(SentryProcess_FromSimpleQuery_List *pList, void *pUserData)
{
	sbReturned = true;

	FOR_EACH_IN_EARRAY(pList->ppProcesses, SentryProcess_FromSimpleQuery, pProc)
	{
		if (pProc->iPID == iMainExecutablePID && strstri(pProc->pProcessName, pMainExecutableName))
		{
			sbFoundOne = true;
		}
	}
	FOR_EACH_END;
	

}

void WaitForMainExecutable(void)
{
	int i;
	while (1)
	{
		SentryServerComm_QueryMachineForRunningExes_Simple(pMainExecutableMachine, WaitForMainExecutableCB, NULL);
		sbReturned = false;
		sbFoundOne = false;

		for (i = 0; i < 50000 && !sbReturned; i++)
		{
			SentryServerComm_Tick();
			commMonitor(commDefault());
			Sleep(1);
		}

		assertmsgf(sbReturned, "Didn't get a response from sentry server for 50 seconds... something is seriously wrong, and you will have to watch shard %s on %s shutdown yourself",
			pShardName, pMainExecutableMachine);

		if (!sbFoundOne)
		{
			return;
		}

		Sleep(2000);
	}
}

bool ExeICareABout(SentryProcess_FromSimpleQuery *pProc)
{
	if (!pFolderForExes || strstri(pProc->pProcessPath, pFolderForExes))
	{
		FOR_EACH_IN_EARRAY(ppExesToTrack, char, pExeToTrack)
		{
			if (stricmp(pProc->pProcessName, pExeToTrack) == 0)
			{
				return true;
			}
		}
		FOR_EACH_END;
	}

	return false;
}

void WaitForOtherExecutablesCB(SentryProcess_FromSimpleQuery_List *pList, void *pUserData)
{
	sbReturned = true;

	FOR_EACH_IN_EARRAY(pList->ppProcesses, SentryProcess_FromSimpleQuery, pProc)
	{
		if (ExeICareABout(pProc))
		{
			sbFoundOne = true;
			printf("%s: There is still a %s running on %s\n", pShardName, pProc->pProcessName, pList->pMachineName);
		}
	}
	FOR_EACH_END;
	


}

void WaitForOtherExecutables(void)
{
	int i;
	while (eaSize(&ppMachines))
	{
		int iSleepAmount;
		char *pCurMachine = eaRemove(&ppMachines, 0);
		sbReturned = false;
		sbFoundOne = false;

		//query faster as we have more machines, which should tail off quickly
		iSleepAmount = 2000 / (eaSize(&ppMachines) + 1);
		if (iSleepAmount < 200)
		{
			iSleepAmount = 200;
		}

		Sleep(iSleepAmount);

		SentryServerComm_QueryMachineForRunningExes_Simple(pCurMachine, WaitForOtherExecutablesCB, NULL);

		for (i = 0; i < 50000 && !sbReturned; i++)
		{
			SentryServerComm_Tick();
			commMonitor(commDefault());
			Sleep(1);
		}

		assertmsgf(sbReturned, "Didn't get a response from sentry server for 50 seconds... something is seriously wrong, and you will have to watch the shard on %s shutdown yourself",
			pMainExecutableMachine);

		if (sbFoundOne)
		{
			eaPush(&ppMachines, pCurMachine);
		}
		else
		{
			printf("Machine %s now all clear\n", pCurMachine);
		}
	}
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

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);


	hideConsoleWindow();
	assertmsgf(pMainExecutableMachine && pMainExecutableName && pMainExecutableName[0] && iMainExecutablePID, "No main executable specified");
	

	assertmsgf(eaSize(&ppMachines), "No machines specified");
	assertmsgf(eaSize(&ppExesToTrack), "No exes specified");

	WaitForMainExecutable();


	showConsoleWindow_NoTaskbar();

	printf("%s controller has closed!\n", pShardName);

	WaitForOtherExecutables();

	consoleSetColor(0, COLOR_GREEN | COLOR_HIGHLIGHT);
	printf("Shard %s has shutdown. It is pining for the fjords. Press a key...\n", pShardName);
	
	{
		char c = _getch();
	}
	

	EXCEPTION_HANDLER_END

}


