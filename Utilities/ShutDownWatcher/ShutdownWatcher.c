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

char **ppMachines = NULL;
char **ppExesToTrack = NULL;
NetLink *gpLinkToSentryServer = NULL;


bool gbFinished = false;

char *pFolderForExes = NULL;
AUTO_CMD_ESTRING(pFolderForExes, FolderForExes) ACMD_COMMANDLINE;

AUTO_COMMAND;
void MainExecutable(char *pExeName, int iPid)
{
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


char *GetQuotedString(char *pInString)
{
	char *pFirstQuote = strchr(pInString, '"');
	char *pSecondQuote;
	if (!pFirstQuote)
	{
		return "INVALID_STRING";
	}

	pSecondQuote = strchr(pFirstQuote + 1, '"');
	if (!pSecondQuote)
	{
		return "INVALID_STRING";
	}

	*pSecondQuote = 0;
	return pFirstQuote + 1;
}

	

void FoundExe(char *pMachineName, char *pProcessName)
{
	gbFinished = false;

	printf("%s still running on %s\n", pProcessName, pMachineName);
}

void ProcessStrings(char **ppStrings)
{
	int iIndex = 0;
	char *pMachineName;

	consoleSetColor(0, COLOR_GREEN | COLOR_BLUE | COLOR_RED);
	printf("%s: SHUTDOWN WATCHER about to check for still-running executables\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
	consoleSetColor(0, COLOR_RED | COLOR_HIGHLIGHT);
	gbFinished = true;

	while(1)
	{
		//skip forward until we find a machine name
		while (iIndex < eaSize(&ppStrings) && !strStartsWith(ppStrings[iIndex], "Machine "))
		{
			iIndex++;
		}

		if (iIndex == eaSize(&ppStrings))
		{
			return;
		}

		//get machine name
		pMachineName = GetQuotedString(ppStrings[iIndex]);
		if (eaFindString(&ppMachines, pMachineName) == -1)
		{
			//this machine isn't one we care about. all we need to do is inc index and return to main loop
			iIndex++;
		}
		else
		{
			//we do care about this index. Look for all processes on it
			iIndex++;

			while (iIndex < eaSize(&ppStrings) && !strStartsWith(ppStrings[iIndex], "Machine "))
			{
				if (strStartsWith(ppStrings[iIndex], "Process_Name "))
				{
					char *pProcessName = GetQuotedString(ppStrings[iIndex]);

					
					if (eaFindString(&ppExesToTrack, pProcessName) >= 0)
					{
						if (pFolderForExes)
						{
							iIndex++;

							if (iIndex < eaSize(&ppStrings) && strStartsWith(ppStrings[iIndex], "Process_Path "))
							{
								char *pProcessFolder = GetQuotedString(ppStrings[iIndex]);

								if (strstri(pProcessFolder, pFolderForExes))
								{
									FoundExe(pMachineName, pProcessName);
								}
							}
							else //should only happen if sentry.exe is out of date
							{
								iIndex--;
								FoundExe(pMachineName, pProcessName);
							}
						}
						else
						{
							FoundExe(pMachineName, pProcessName);
						}
					}	
				}
				iIndex++;
			}
		}
	}
}




static bool sbReturned = false;
static bool sbFoundOne = false;	

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
			printf("There is still a %s running on %s\n", pProc->pProcessName, pList->pMachineName);
			sbFoundOne = true;
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

		assertmsgf(sbReturned, "Didn't get a response from sentry server for 50 seconds... something is seriously wrong, and you will have to watch the shard shutdown yourself");

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
	assertmsgf(pMainExecutableName && pMainExecutableName[0] && iMainExecutablePID, "No main executable specified");

	assertmsgf(eaSize(&ppMachines), "No machines specified");
	assertmsgf(eaSize(&ppExesToTrack), "No exes specified");

	//wait for main executable to no longer exist
	while (processExists(pMainExecutableName, iMainExecutablePID))
	{
		Sleep(1000);
	}

	showConsoleWindow();
	printf("Process has closed!\n");

	
	WaitForOtherExecutables();



	consoleSetColor(0, COLOR_GREEN | COLOR_HIGHLIGHT);
	printf("The shard has shutdown. It is pining for the fjords. Press a key...\n");
	
	{
		char c = _getch();
	}


	EXCEPTION_HANDLER_END

}


