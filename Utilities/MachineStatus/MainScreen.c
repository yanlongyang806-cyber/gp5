#include "SimpleWindowManager.h"
#include "utilitiesLib.h"
#include "net.h"
#include "MainScreen.h"

#include "MachineStatusPub.h"
#include "MachineStatus.h"
#include "timing.h"
#include "earray.h"
#include "EString.h"
#include "textparser.h"
#include "MachineStatusPub_h_ast.h"
#include "winutil.h"
#include "GlobalComm.h"
#include "..\..\libs\PatchClientLib\PatchClientLibStatusMonitoring.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "resource.h"
#include "../../libs/ServerLib/pub/GenericHttpServing.h"

void HidePatch(int iIndex);
void ShowPatch(int iIndeX);
void HideAllPatches(void);
void ShowOnlyPatch(int iIndex);
void DismissPatch(int iIndex);

static HBRUSH sRed = 0;
#define RED_COLOR RGB(240,0,0)

#define MAX_SHARDS 10

U32 iShardBoxIDs[MAX_SHARDS] = 
{
	IDC_SHARD1,
	IDC_SHARD2,
	IDC_SHARD3,
	IDC_SHARD4,
	IDC_SHARD5,
	IDC_SHARD6,
	IDC_SHARD7,
	IDC_SHARD8,
	IDC_SHARD9,
	IDC_SHARD10,
};

U32 iGSStringIDs[MAX_SHARDS] = 
{
	IDC_GS_STRING1,
	IDC_GS_STRING2,
	IDC_GS_STRING3,
	IDC_GS_STRING4,
	IDC_GS_STRING5,
	IDC_GS_STRING6,
	IDC_GS_STRING7,
	IDC_GS_STRING8,
	IDC_GS_STRING9,
	IDC_GS_STRING10,
};

U32 iHideIDs[MAX_SHARDS] = 
{
	IDC_HIDE1,
	IDC_HIDE2,
	IDC_HIDE3,
	IDC_HIDE4,
	IDC_HIDE5,
	IDC_HIDE6,
	IDC_HIDE7,
	IDC_HIDE8,
	IDC_HIDE9,
	IDC_HIDE10,
};

U32 iShowIDs[MAX_SHARDS] = 
{
	IDC_SHOW1,
	IDC_SHOW2,
	IDC_SHOW3,
	IDC_SHOW4,
	IDC_SHOW5,
	IDC_SHOW6,
	IDC_SHOW7,
	IDC_SHOW8,
	IDC_SHOW9,
	IDC_SHOW10,
};

U32 iShowOnlyIDs[MAX_SHARDS] = 
{
	IDC_SHOWONLY1,
	IDC_SHOWONLY2,
	IDC_SHOWONLY3,
	IDC_SHOWONLY4,
	IDC_SHOWONLY5,
	IDC_SHOWONLY6,
	IDC_SHOWONLY7,
	IDC_SHOWONLY8,
	IDC_SHOWONLY9,
	IDC_SHOWONLY10,
};

U32 iLastContactIDs[MAX_SHARDS] =
{
	IDC_LASTCONTACT1,
	IDC_LASTCONTACT2,
	IDC_LASTCONTACT3,
	IDC_LASTCONTACT4,
	IDC_LASTCONTACT5,
	IDC_LASTCONTACT6,
	IDC_LASTCONTACT7,
	IDC_LASTCONTACT8,
	IDC_LASTCONTACT9,
	IDC_LASTCONTACT10,
};

U32 iIgnoreServerIDs[MAX_SHARDS] = 
{
	IDC_IGNORESERVER1,
	IDC_IGNORESERVER2,
	IDC_IGNORESERVER3,
	IDC_IGNORESERVER4,
	IDC_IGNORESERVER5,
	IDC_IGNORESERVER6,
	IDC_IGNORESERVER7,
	IDC_IGNORESERVER8,
	IDC_IGNORESERVER9,
	IDC_IGNORESERVER10,
};


#define MAX_PATCH_DISPLAYS 4

U32 iPatchBoxIDs[MAX_PATCH_DISPLAYS] =
{
	IDC_PATCH1,
	IDC_PATCH2,
	IDC_PATCH3,
	IDC_PATCH4,
};
	
U32 iPatchTextIDs[MAX_PATCH_DISPLAYS] =
{
	IDC_PATCHTEXT1,
	IDC_PATCHTEXT2,
	IDC_PATCHTEXT3,
	IDC_PATCHTEXT4,
};

U32 iPatchHideIDs[MAX_PATCH_DISPLAYS] =
{
	IDC_PATCHHIDE1,
	IDC_PATCHHIDE2,
	IDC_PATCHHIDE3,	
	IDC_PATCHHIDE4,
};
	
U32 iPatchShowIDs[MAX_PATCH_DISPLAYS] =
{
	IDC_PATCHSHOW1,
	IDC_PATCHSHOW2,
	IDC_PATCHSHOW3,	
	IDC_PATCHSHOW4,
};

U32 iPatchShowOnlyIDs[MAX_PATCH_DISPLAYS] =
{
	IDC_PATCHSHOWONLY1,
	IDC_PATCHSHOWONLY2,
	IDC_PATCHSHOWONLY3,	
	IDC_PATCHSHOWONLY4,
};

U32 iPatchDismissIDs[MAX_PATCH_DISPLAYS] =
{
	IDC_PATCHDISMISS1,
	IDC_PATCHDISMISS2,
	IDC_PATCHDISMISS3,	
	IDC_PATCHDISMISS4,
};
bool mainScreenDlgProc_SWMTick(SimpleWindow *pWindow)
{
	static U32 siLastUpdateTime = 0;
	int i;

	utilitiesLibOncePerFrame(REAL_TIME);
	commMonitor(commDefault());
	GenericHttpServing_Tick();

	if (gbSomethingChanged || siLastUpdateTime != timeSecondsSince2000())
	{
		int iNumShards;
		
		//first remove any dead shards
		for (i = eaSize(&gppCurrentShards) - 1; i >= 0; i--)
		{
			MachineStatusUpdate *pShard = gppCurrentShards[i];

			if (pShard->iTime < timeSecondsSince2000() - SHARD_OUT_OF_CONTACT_DISCONNECT_TIME)
			{
				StructDestroy(parse_MachineStatusUpdate, pShard);
				eaRemove(&gppCurrentShards, i);
			}
		}

		iNumShards = eaSize(&gppCurrentShards);		
		if (iNumShards > MAX_SHARDS)
		{
			iNumShards = MAX_SHARDS;
		}

		siLastUpdateTime = timeSecondsSince2000();

		


		for (i = iNumShards; i < MAX_SHARDS; i++)
		{
			ShowWindow(GetDlgItem(pWindow->hWnd, iShardBoxIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, iGSStringIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, iHideIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, iShowIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, iShowOnlyIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, iLastContactIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, iIgnoreServerIDs[i]), SW_HIDE);
		}

		for (i = 0; i < iNumShards; i++)
		{
			static char *pGSString = NULL;
			MachineStatusUpdate *pShard = gppCurrentShards[i];
			ShowWindow(GetDlgItem(pWindow->hWnd, iShardBoxIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(pWindow->hWnd, iGSStringIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(pWindow->hWnd, iHideIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(pWindow->hWnd, iShowIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(pWindow->hWnd, iShowOnlyIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(pWindow->hWnd, iLastContactIDs[i]), SW_SHOW);

			SetTextFast(GetDlgItem(pWindow->hWnd, iShardBoxIDs[i]), pShard->pShardName);

			if (ea32Size(&pShard->pGameServerPIDs))
			{
				static char *pRAMString = NULL;

				estrMakePrettyBytesString(&pRAMString, pShard->iGSRam);

				estrPrintf(&pGSString, "%d GS%s, %.2f%% CPU, %s", ea32Size(&pShard->pGameServerPIDs), ea32Size(&pShard->pGameServerPIDs) == 1 ? "" : "s", pShard->fGSCpuLast60 * 100.0f, pRAMString);
			}
			else
			{
				estrPrintf(&pGSString, "No gameservers");
			}

			SetTextFast(GetDlgItem(pWindow->hWnd, iGSStringIDs[i]), pGSString);

			if (pShard->iTime < timeSecondsSince2000() - SHARD_OUT_OF_CONTACT_SHOW_TIME)
			{
				static char *pOutOfContactString = NULL;
				estrPrintf(&pOutOfContactString, "Last contact: %d seconds ago", timeSecondsSince2000() - pShard->iTime);
				SetTextFast(GetDlgItem(pWindow->hWnd, iLastContactIDs[i]), pOutOfContactString);
			}
			else
			{
				SetTextFast(GetDlgItem(pWindow->hWnd, iLastContactIDs[i]), "");
			}

			if (pShard->iPIDOfIgnoredServer)
			{
				ShowWindow(GetDlgItem(pWindow->hWnd, iIgnoreServerIDs[i]), SW_SHOW);
				SetTextFastf(GetDlgItem(pWindow->hWnd, iIgnoreServerIDs[i]), "Ignored/stalled server PID: %d",
					pShard->iPIDOfIgnoredServer);
			}
			else
			{
				ShowWindow(GetDlgItem(pWindow->hWnd, iIgnoreServerIDs[i]), SW_HIDE);
			}
		}
	}
	
	
	return false;
}

void HideShard(int iShardNum)
{
	NetLink *pLink;


	if (iShardNum >= eaSize(&gppCurrentShards))
	{
		return;
	}

	pLink = FindNetLinkFromShardName(gppCurrentShards[iShardNum]->pShardName);

	if (pLink)
	{
		Packet *pPak = pktCreate(pLink, FROM_MACHINESTATUS_HIDE_ALL);
		pktSend(&pPak);
	}
}

void ShowShard(int iShardNum)
{
	NetLink *pLink;

	if (iShardNum >= eaSize(&gppCurrentShards))
	{
		return;
	}

	pLink = FindNetLinkFromShardName(gppCurrentShards[iShardNum]->pShardName);

	if (pLink)
	{
		Packet *pPak = pktCreate(pLink, FROM_MACHINESTATUS_SHOW_ALL);
		pktSend(&pPak);
	}

}

void ShowOnlyShard(int iShardNum)
{
	int i;

	for (i = 0; i < eaSize(&gppCurrentShards); i++)
	{
		if (i == iShardNum)
		{
			ShowShard(i);
		}
		else
		{
			HideShard(i);
		}
	}

	HideAllPatches();
}

void HideAllShards(void)
{
	int i;

	for (i = 0; i < eaSize(&gppCurrentShards); i++)
	{
		HideShard(i);
	}
}

BOOL mainScreenDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i;
	

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			if (!sRed)
			{
				sRed = CreateSolidBrush(RED_COLOR);
	
			}


			for (i = 0; i < MAX_PATCH_DISPLAYS; i++)
			{
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchBoxIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchTextIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchHideIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowOnlyIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchDismissIDs[i]), SW_HIDE);
			}	

		}
		break;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		for (i = 0; i < ARRAY_SIZE(iIgnoreServerIDs); i++)
		{
			if ((HANDLE)lParam == GetDlgItem(hDlg, iIgnoreServerIDs[i]))
			{
				
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, RED_COLOR);
					return (BOOL)((intptr_t)sRed);
			
			}
		}
		break;


	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		for (i = 0; i < MAX_SHARDS; i++)
		{
			if (LOWORD(wParam) == iHideIDs[i])
			{
				HideShard(i);
				break;
			}

			if (LOWORD(wParam) == iShowIDs[i])
			{
				ShowShard(i);
				break;
			}

			if (LOWORD(wParam) == iShowOnlyIDs[i])
			{
				ShowOnlyShard(i);
				break;
			}
		}

		for (i = 0; i < MAX_PATCH_DISPLAYS; i++)
		{
			if (LOWORD(wParam) == iPatchHideIDs[i])
			{
				HidePatch(i);
				break;
			}
			
			if (LOWORD(wParam) == iPatchShowIDs[i])
			{
				ShowPatch(i);
				break;
			}
		
			if (LOWORD(wParam) == iPatchShowOnlyIDs[i])
			{
				ShowOnlyPatch(i);
				break;
			}

			if (LOWORD(wParam) == iPatchDismissIDs[i])
			{
				DismissPatch(i);
				break;
			}

		}
	}

	return false;
}

void MainScreen_GetPatchingUpdate(PCLStatusMonitoringUpdate *pUpdate)
{

}

#define MAX_DIR_NAME_LENGTH 30

char *GetDescriptiveUpdateString(PCLStatusMonitoringUpdate *pUpdate)
{
	static char *spRetVal = NULL;
	static char *spDuration1 = NULL;
	static char *spDuration2 = NULL;

	static char *spDirToUse = NULL;

	switch (pUpdate->internalStatus.eState)
	{
	xcase PCLSMS_UPDATE:
		estrCopy2(&spDirToUse, pUpdate->internalStatus.pPatchDir);
		if (estrLength(&spDirToUse) > MAX_DIR_NAME_LENGTH)
		{
			estrSetSize(&spDirToUse, MAX_DIR_NAME_LENGTH);
			estrConcatf(&spDirToUse, "...");
		}
	
		estrPrintf(&spRetVal, "Name: %s\nDir: %s\nStatus: %s\n",
			pUpdate->internalStatus.pPatchName,
			spDirToUse,
			pUpdate->internalStatus.pUpdateString);
		
	xcase PCLSMS_FAILED:
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pUpdate->iSucceededOrFailedTime, &spDuration1);
		timeSecondsDurationToPrettyEString(pUpdate->iSucceededOrFailedTime - pUpdate->iTimeBegan, &spDuration2);

		if (pUpdate->iNumUpdatesReceived)
		{
			estrPrintf(&spRetVal, "FAILED %s ago after %s of patching, presumed failure cause: %s\n",
				spDuration1, spDuration2, pUpdate->internalStatus.pUpdateString);
		}
		else
		{
			estrPrintf(&spRetVal, "FAILED %s ago, patching presumably began %s earlier, but never updated. Presumed failure cause: %s\n",
				spDuration1, spDuration2, pUpdate->internalStatus.pUpdateString);
		}
		
	xcase PCLSMS_SUCCEEDED:
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pUpdate->iSucceededOrFailedTime, &spDuration1);
		timeSecondsDurationToPrettyEString(pUpdate->iSucceededOrFailedTime - pUpdate->iTimeBegan, &spDuration2);

		estrPrintf(&spRetVal, "SUCCEEDED %s ago after %s of patching",
			spDuration1, spDuration2);
	
	xcase PCLSMS_TIMEOUT:
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pUpdate->iMostRecentUpdateTime, &spDuration1);	
		estrPrintf(&spRetVal, "STALLED for %s", spDuration1);

	xcase PCLSMS_FAILED_TIMEOUT:
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pUpdate->iMostRecentUpdateTime, &spDuration1);
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pUpdate->iTimeBegan, &spDuration2);
		estrPrintf(&spRetVal, "TIMEOUT FAIL. Patching began %s ago, but last contact was %s ago, presumed failed",
			spDuration2, spDuration1);

	xcase PCLSMS_INTERNAL_CREATE:
		estrPrintf(&spRetVal, "NEWLY CREATED. You should never see this");
	}


	estrFixupNewLinesForWindows(&spRetVal);
	return spRetVal;

}

void MainScreen_PatchingPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	SimpleWindow *pWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAIN);
	PCLStatusMonitoringUpdate *pUpdate;

	PCLStatusMonitoringIterator *pIterator = NULL;
	int iCurPatchIndex = 0;

	PCLStatusMonitoring_Tick();


	if (!pWindow)
	{
		return;
	}

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIterator)))
	{
		if (iCurPatchIndex < MAX_PATCH_DISPLAYS)
		{
			ShowWindow(GetDlgItem(pWindow->hWnd, iPatchBoxIDs[iCurPatchIndex]), SW_SHOW);
			ShowWindow(GetDlgItem(pWindow->hWnd, iPatchTextIDs[iCurPatchIndex]), SW_SHOW);
	
			SetTextFast(GetDlgItem(pWindow->hWnd, iPatchBoxIDs[iCurPatchIndex]), pUpdate->internalStatus.pMyIDString);
			SetTextFast(GetDlgItem(pWindow->hWnd, iPatchTextIDs[iCurPatchIndex]), GetDescriptiveUpdateString(pUpdate));

			
			switch (pUpdate->internalStatus.eState)
			{
			case PCLSMS_INTERNAL_CREATE:
			case PCLSMS_UPDATE:
			case PCLSMS_TIMEOUT:
				if (pUpdate->internalStatus.iMyPID)
				{
					ShowWindow(GetDlgItem(pWindow->hWnd, iPatchHideIDs[iCurPatchIndex]), SW_SHOW);
					ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowIDs[iCurPatchIndex]), SW_SHOW);
					ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowOnlyIDs[iCurPatchIndex]), SW_SHOW);
				}
				else
				{
					ShowWindow(GetDlgItem(pWindow->hWnd, iPatchHideIDs[iCurPatchIndex]), SW_HIDE);
					ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowIDs[iCurPatchIndex]), SW_HIDE);
					ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowOnlyIDs[iCurPatchIndex]), SW_HIDE);
				}

				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchDismissIDs[iCurPatchIndex]), SW_HIDE);
				break;

			case PCLSMS_FAILED:
			case PCLSMS_SUCCEEDED:
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchHideIDs[iCurPatchIndex]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowIDs[iCurPatchIndex]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowOnlyIDs[iCurPatchIndex]), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchDismissIDs[iCurPatchIndex]), SW_SHOW);
				break;
	
			case PCLSMS_FAILED_TIMEOUT:
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchHideIDs[iCurPatchIndex]), SW_SHOW);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowIDs[iCurPatchIndex]), SW_SHOW);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowOnlyIDs[iCurPatchIndex]), SW_SHOW);
				ShowWindow(GetDlgItem(pWindow->hWnd, iPatchDismissIDs[iCurPatchIndex]), SW_SHOW);
				break;	
			}
		}


		iCurPatchIndex++;
	}

	while (iCurPatchIndex < MAX_PATCH_DISPLAYS)
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, iPatchBoxIDs[iCurPatchIndex]), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, iPatchTextIDs[iCurPatchIndex]), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, iPatchHideIDs[iCurPatchIndex]), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowIDs[iCurPatchIndex]), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, iPatchShowOnlyIDs[iCurPatchIndex]), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, iPatchDismissIDs[iCurPatchIndex]), SW_HIDE);

		iCurPatchIndex++;
	}
	
}

void HidePatch(int iIndex)
{
	PCLStatusMonitoringUpdate *pUpdate;

	PCLStatusMonitoringIterator *pIterator = NULL;
	int iCurPatchIndex = 0;

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIterator)))
	{
		if (iCurPatchIndex == iIndex)
		{
			if (pUpdate->internalStatus.uMyHWND)
			{
				ShowWindow(	(HWND)(pUpdate->internalStatus.uMyHWND), SW_HIDE);
			}
		}

		iCurPatchIndex++;
	}
}

void ShowPatch(int iIndex)
{
	PCLStatusMonitoringUpdate *pUpdate;

	PCLStatusMonitoringIterator *pIterator = NULL;
	int iCurPatchIndex = 0;

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIterator)))
	{
		if (iCurPatchIndex == iIndex)
		{
			if (pUpdate->internalStatus.uMyHWND)
			{
				ShowWindow(	(HWND)(pUpdate->internalStatus.uMyHWND), SW_SHOW);
			}
		}

		iCurPatchIndex++;
	}
}

void HideAllPatches(void)
{
	PCLStatusMonitoringUpdate *pUpdate;
	PCLStatusMonitoringIterator *pIterator = NULL;
	
	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIterator)))
	{
		if (pUpdate->internalStatus.iMyPID)
		{
			char cmdLine[1024];
			sprintf(cmdLine, "SHOW %d HIDE", pUpdate->internalStatus.iMyPID);
			system(cmdLine);
		}
	}
}


void ShowOnlyPatch(int iIndex)
{
	PCLStatusMonitoringUpdate *pUpdate;

	PCLStatusMonitoringIterator *pIterator = NULL;
	int iCurPatchIndex = 0;

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIterator)))
	{
		if (pUpdate->internalStatus.iMyPID)
		{
			char cmdLine[1024];
			sprintf(cmdLine, "SHOW %d %s", pUpdate->internalStatus.iMyPID, iCurPatchIndex == iIndex ? "SHOW" : "HIDE");
			system(cmdLine);
		}
		

		iCurPatchIndex++;
	}

	HideAllShards();
}

void DismissPatch(int iIndex)
{
	int iCurPatchIndex = 0;
	PCLStatusMonitoringUpdate *pUpdate;
	PCLStatusMonitoringIterator *pIterator = NULL;

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIterator)))
	{
		if (iCurPatchIndex == iIndex)
		{
			PCLStatusMonitoring_DismissSucceededOrFailedByName(pUpdate->internalStatus.pMyIDString);
			return;
		}

		iCurPatchIndex++;
	}
}