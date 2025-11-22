#include "winInclude.h"
#include "ShardLauncherWatchTheLaunch.h"
#include "ShardLauncherUI.h"
#include "resource.h"
#include "winutil.h"
#include "ShardLauncher.h"
#include "earray.h"
#include "GlobalTypes_h_ast.h"
#include "ShardLauncherMainScreen.h"
#include "structDefines.h"
#include "ShardLauncher_h_Ast.h"
#include "utilitiesLib.h"
#include "SentryServerComm.h"
#include "../../libs/PatchClientLib/PatchClientLibStatusMonitoring.h"
#include "net.h"
#include "ShardLauncherWatchTheLaunch_c_ast.h"
#include "ShardLauncherWarningScreen.h"


static HBRUSH sBad = 0;
#define BAD_COLOR RGB(240,0,0)

static HBRUSH sGood = 0;
#define GOOD_COLOR RGB(0,240, 0)



static char *spLogText = NULL;
static char *spLogText_Copy = NULL;

//if set, then we either succeeded or failed
static char *spConclusionText = NULL;

//whenever we set spConclusionText, we also set this to either
//true or false
static bool sbSucceeded = false; 


CRITICAL_SECTION sPatchTasksCritSec = {0};

//tracks what commands we need to send through the sentry server to restart the various patching
//tasks
AUTO_STRUCT;
typedef struct PatchTaskForRestarting
{
	char *pTaskName; AST(KEY)
	char *pMachineName;
	char *pCommandLine;
} PatchTaskForRestarting;

static PatchTaskForRestarting **sppPatchTasksForRestarting = NULL;


void AddPatchingTaskForRestarting(char *pTaskName, const char *pMachineName, char *pCommandLine)
{
	PatchTaskForRestarting *pTask = StructCreate(parse_PatchTaskForRestarting);
	pTask->pTaskName = strdup(pTaskName);
	pTask->pMachineName = strdup(pMachineName);
	pTask->pCommandLine = strdup(pCommandLine);

	EnterCriticalSection(&sPatchTasksCritSec);
	eaIndexedEnable(&sppPatchTasksForRestarting, parse_PatchTaskForRestarting);
	eaPush(&sppPatchTasksForRestarting, pTask);
	LeaveCriticalSection(&sPatchTasksCritSec);
}

PatchTaskForRestarting *FindPatchTaskForRestarting(char *pTaskName)
{
	PatchTaskForRestarting *pTask;

	EnterCriticalSection(&sPatchTasksCritSec);
	pTask = eaIndexedGetUsingString(&sppPatchTasksForRestarting, pTaskName);
	LeaveCriticalSection(&sPatchTasksCritSec);

	return pTask;
}





CRITICAL_SECTION sWatchTheLaunchCritSec = {0};

static U32 siPatchingIDs[] = 
{
	IDC_PATCHING1,
	IDC_PATCHING2,
	IDC_PATCHING3,
	IDC_PATCHING4,
	IDC_PATCHING5,
};

static U32 siPatchVNCIDs[] = 
{
	IDC_PATCHVNC1,
	IDC_PATCHVNC2,
	IDC_PATCHVNC3,
	IDC_PATCHVNC4,
	IDC_PATCHVNC5,
};

static U32 siPatchCancelIDs[] = 
{
	IDC_PATCHCANCEL1,
	IDC_PATCHCANCEL2,
	IDC_PATCHCANCEL3,
	IDC_PATCHCANCEL4,
	IDC_PATCHCANCEL5,
};

static U32 siPatchRestartIDs[] = 
{
	IDC_PATCHRESTART1,
	IDC_PATCHRESTART2,
	IDC_PATCHRESTART3,
	IDC_PATCHRESTART4,
	IDC_PATCHRESTART5,
};

static char *spPatchTaskNames[ARRAY_SIZE(siPatchingIDs)] = {0};
static bool sbPatchingFailed[ARRAY_SIZE(siPatchingIDs)] = {0};

AUTO_RUN_EARLY;
void WatchTheLaunch_Init(void)
{
	InitializeCriticalSection(&sWatchTheLaunchCritSec);
	InitializeCriticalSection(&sPatchTasksCritSec);
}

void WatchTheLaunch_Log(WTLLogType eLogType, const char *pString)
{
	char *pStringCopy = NULL;

	if (!pString || !pString[0])
	{
		return;
	}


	estrStackCreate(&pStringCopy);
	estrCopy2(&pStringCopy, pString);
	if (pStringCopy[estrLength(&pStringCopy) - 1] != '\n')
	{
		estrConcatChar(&pStringCopy, '\n');
	}

	estrFixupNewLinesForWindows(&pStringCopy);

	switch (eLogType)
	{
	case WTLLOG_NORMAL:
	case WTLLOG_WARNING:
		EnterCriticalSection(&sWatchTheLaunchCritSec);
		estrConcatf(&spLogText, "%s", pStringCopy);
		LeaveCriticalSection(&sWatchTheLaunchCritSec);
		break;

	case WTLLOG_SUCCEEDED:
		EnterCriticalSection(&sWatchTheLaunchCritSec);
		estrCopy2(&spConclusionText, pStringCopy);
		sbSucceeded = true;
		LeaveCriticalSection(&sWatchTheLaunchCritSec);
		break;

	case WTLLOG_FATAL:
		EnterCriticalSection(&sWatchTheLaunchCritSec);
		estrCopy2(&spConclusionText, pStringCopy);
		sbSucceeded = false;
		LeaveCriticalSection(&sWatchTheLaunchCritSec);
		break;
	}

	estrDestroy(&pStringCopy);
}

bool watchTheLaunchDlgProc_SWMTick(SimpleWindow *pWindow)
{
	static int siCounter = 0;
	int i;

	utilitiesLibOncePerFrame(REAL_TIME);
	SentryServerComm_Tick();
	commMonitor(commDefault());
	Sleep(1);
	PCLStatusMonitoring_Tick();
	siCounter++;

	if (siCounter % 5 == 0)
	{
		int iLen1, iLen2;
		bool bChanged = false;
		
		EnterCriticalSection(&sWatchTheLaunchCritSec);

		if (spConclusionText)
		{
			SetTextFast(GetDlgItem(pWindow->hWnd, IDC_OUTCOME), spConclusionText);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDOK), SW_SHOW);
		}

		iLen1 = estrLength(&spLogText);
		iLen2 = estrLength(&spLogText_Copy);

		if (iLen1 != iLen2)
		{
			estrConcatf(&spLogText_Copy, "%s", spLogText + iLen2);
			bChanged = true;

		}

		LeaveCriticalSection(&sWatchTheLaunchCritSec);

		if (bChanged)
		{
			SetTextFast(GetDlgItem(pWindow->hWnd, IDC_TEXT), spLogText_Copy);
			SendMessage( GetDlgItem(pWindow->hWnd, IDC_TEXT),      
				(UINT) EM_LINESCROLL,
				0, 10000 );
		}






		{
			PCLStatusMonitoringIterator *pIter = NULL;
			PCLStatusMonitoringUpdate *pUpdate = NULL;
			char *pTemp = NULL;
			i = 0;
			estrStackCreate(&pTemp);

			while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIter)))
			{
				if (i < ARRAY_SIZE(siPatchingIDs))
				{
					ShowWindow(GetDlgItem(pWindow->hWnd, siPatchingIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(pWindow->hWnd, siPatchVNCIDs[i]), SW_SHOW);
	
					estrPrintf(&pTemp, "Patching:%s - %s\r\n%s",
						pUpdate->internalStatus.pMyIDString,
						StaticDefineInt_FastIntToString(PCLStatusMonitoringStateEnum, pUpdate->internalStatus.eState),
						pUpdate->internalStatus.pUpdateString ? pUpdate->internalStatus.pUpdateString : "" );

					SetTextFast(GetDlgItem(pWindow->hWnd, siPatchingIDs[i]), pTemp);
					estrCopy2(&spPatchTaskNames[i], pUpdate->internalStatus.pMyIDString);

					if (pUpdate->internalStatus.eState == PCLSMS_SUCCEEDED)
					{
						ShowWindow(GetDlgItem(pWindow->hWnd, siPatchCancelIDs[i]), SW_HIDE);
						ShowWindow(GetDlgItem(pWindow->hWnd, siPatchRestartIDs[i]), SW_HIDE);
					}
					else
					{
						ShowWindow(GetDlgItem(pWindow->hWnd, siPatchCancelIDs[i]), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, siPatchRestartIDs[i]), SW_SHOW);
					}

					if (pUpdate->internalStatus.eState == PCLSMS_FAILED || pUpdate->internalStatus.eState == PCLSMS_FAILED_TIMEOUT)
					{
						if (!sbPatchingFailed[i])
						{
							InvalidateRect(GetDlgItem(pWindow->hWnd, siPatchingIDs[i]), NULL, false);
						}
						sbPatchingFailed[i] = true;
					}
					else
					{
						if (sbPatchingFailed[i])
						{
							InvalidateRect(GetDlgItem(pWindow->hWnd, siPatchingIDs[i]), NULL, false);
						}
						sbPatchingFailed[i] = false;
					}

				
				}
				//do something
				i++;
			}
			estrDestroy(&pTemp);

			for (; i < ARRAY_SIZE(siPatchingIDs); i++)
			{
					ShowWindow(GetDlgItem(pWindow->hWnd, siPatchingIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(pWindow->hWnd, siPatchVNCIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(pWindow->hWnd, siPatchCancelIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(pWindow->hWnd, siPatchRestartIDs[i]), SW_HIDE);
			}
		}

	}

	return false;
}


static char *spHumanConfirmationText = NULL;
static char *spHumanConfirmationButton = NULL;
static char *spHumanConfirmationButtonCommand = NULL;


static volatile bool sbHumanConfirmationDone = false;
static bool sbWaitingForConfirmation = false;

BOOL watchTheLaunchDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i;

	switch (iMsg)
	{
	case WM_TIMER:
		if (spHumanConfirmationText)
		{
			if (sbWaitingForConfirmation)
			{
				SAFE_FREE(spHumanConfirmationText);
				SAFE_FREE(spHumanConfirmationButton);
				SAFE_FREE(spHumanConfirmationButtonCommand);
			}
			else
			{
				sbWaitingForConfirmation = true;
				DisplayConfirmation(spHumanConfirmationText, spHumanConfirmationButton, spHumanConfirmationButtonCommand);
				SAFE_FREE(spHumanConfirmationText);
				SAFE_FREE(spHumanConfirmationButton);
				SAFE_FREE(spHumanConfirmationButtonCommand);
			}
		}
		else
		{
			if (sbWaitingForConfirmation)
			{
				if (!AreWarningsCurrentlyDisplaying())
				{
					sbWaitingForConfirmation = false;
					sbHumanConfirmationDone = true;
				}
			}
		}
		break;



	case WM_INITDIALOG:

		if (!sBad)
		{
			sBad = CreateSolidBrush(BAD_COLOR);
			sGood = CreateSolidBrush(GOOD_COLOR);
		}

		ShowWindow(GetDlgItem(pWindow->hWnd, IDOK), SW_HIDE);
		SetTimer(hDlg, 0, 1, NULL);

		break;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		for (i = 0; i < ARRAY_SIZE(siPatchingIDs); i++)
		{
			if ((HANDLE)lParam == GetDlgItem(hDlg, siPatchingIDs[i]))
			{
				if (sbPatchingFailed[i])
				{
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, BAD_COLOR);
					return (BOOL)((intptr_t)sBad);
				}
				else
				{
					return false;
				}
			}
		}


		if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_OUTCOME))
		{
			if (!spConclusionText)
			{
				return false;
			}
			else
			{
				if (sbSucceeded)
				{
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, GOOD_COLOR);
					return (BOOL)((intptr_t)sGood);
				}
				else
				{
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, BAD_COLOR);
					return (BOOL)((intptr_t)sBad);
				}



			}
		}
		return false;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		for (i = 0; i < ARRAY_SIZE(siPatchingIDs); i++)
		{

			if (LOWORD(wParam) == siPatchVNCIDs[i])
			{
				char systemString[256];
				char *pMachineName = NULL;
				estrStackCreate(&pMachineName);
				estrCopy2(&pMachineName, spPatchTaskNames[i]);
				estrTruncateAtFirstOccurrence(&pMachineName, ':');
				sprintf(systemString, "crypticURLHandler cryptic://vnc/%s", pMachineName);
				system_detach(systemString, true, true);
				estrDestroy(&pMachineName);

				return false;
			}
			else if (LOWORD(wParam) == siPatchCancelIDs[i])
			{
				PCLStatusMonitoring_AbortPatchingTask(spPatchTaskNames[i]);

				return false;
			}
			else if (LOWORD(wParam) == siPatchRestartIDs[i])
			{
				PatchTaskForRestarting *pTask = FindPatchTaskForRestarting(spPatchTaskNames[i]);

				if (pTask)
				{

			
					SentryServerComm_KillProcess_1Machine(pTask->pMachineName, "patchclient", NULL);
					SentryServerComm_KillProcess_1Machine(pTask->pMachineName, "patchclientX64", NULL);
					SentryServerComm_RunCommand_1Machine(pTask->pMachineName, pTask->pCommandLine);
					
				}

				return false;
			}
		}

		switch (LOWORD (wParam))
		{
		case IDOK:
			exit(0);

		case IDC_ABORT:
			//TODO abort all patching
			exit(-1);



		}
		break;
	}
	
	return false;
}

//this function is called from RuntheShard, which is in another thread... so it sets some global variables to get the 
//warning screen going, then waits for some more global variables to see if the warning screen is done
void GetHumanConfirmationDuringShardRunning(FORMAT_STR const char* format, ...)
{
	char *pTemp = NULL;
	estrGetVarArgs(&pTemp, format);
	spHumanConfirmationText = strdup(pTemp);
	estrDestroy(&pTemp);

	SAFE_FREE(spHumanConfirmationButton);
	SAFE_FREE(spHumanConfirmationButtonCommand);

	while (!sbHumanConfirmationDone)
	{
		Sleep(1);
	}
}

void GetHumanConfirmationDuringShardRunning_WithExtraButton(char *pButtonName, char *pButtonCommand, FORMAT_STR const char* format, ...)
{
	char *pTemp = NULL;
	estrGetVarArgs(&pTemp, format);
	spHumanConfirmationText = strdup(pTemp);
	estrDestroy(&pTemp);

	spHumanConfirmationButton = strdup(pButtonName);
	spHumanConfirmationButtonCommand = strdup(pButtonCommand);

	while (!sbHumanConfirmationDone)
	{
		Sleep(1);
	}
}


#include "ShardLauncherWatchTheLaunch_c_ast.c"

