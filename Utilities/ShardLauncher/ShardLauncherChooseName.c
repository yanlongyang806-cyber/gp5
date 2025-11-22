#include "ShardLauncherChooseName.h"
#include "ShardLauncher.h"
#include "winUtil.h"
#include "resource.h"
#include "estring.h"
#include "TextParser.h"
#include "shardLauncherMainScreen.h"
#include "ShardLauncherUi.h"
#include "timing.h"
#include "ShardLauncherRunTheShard.h"
#include "ShardLauncherWarningScreen.h"
#include "ShardLauncherWatchTheLaunch.h"
#include "utf8.h"

bool chooseNameDlgProc_SWMTick(SimpleWindow *pWindow)
{
	static char *spName = NULL;
	GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_NAME), &spName);

	if (!estrLength(&spName))
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDOK), SW_HIDE);
	}
	else
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDOK), SW_SHOW);
	}

	return false;
}



BOOL chooseNameDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		switch (gRunType)
		{
		case RUNTYPE_PATCHANDLAUNCH:
			SetTextFast(GetDlgItem(hDlg, IDC_CAPTION), "You are about to PATCH AND LAUNCH your production shard. Please enter a name for this run so it can be repeated later.");
			break;
		case RUNTYPE_LAUNCH:
			SetTextFast(GetDlgItem(hDlg, IDC_CAPTION), "You are about to LAUNCH (without patching) your production shard. Please enter a name for this run so it can be repeated later.");
			break;
		case RUNTYPE_PATCH:
			SetTextFast(GetDlgItem(hDlg, IDC_CAPTION), "You are about to PATCH (but not launch) your production shard. Please enter a name for this run so it can be repeated later.");
			break;
		case RUNTYPE_SAVECHANGES:
			SetTextFast(GetDlgItem(hDlg, IDC_CAPTION), "You are SAVING CHANGES to your production shard configuration, but not patching or launching it at all. Please enter a name for this run so it can be repeated later.");
			break;
		}

		if (!gpRun->pRunName || !gpRun->pRunName[0])
		{
			estrPrintf(&gpRun->pRunName, "Name me");
		}

		SetTextFast(GetDlgItem(hDlg, IDC_NAME), gpRun->pRunName);
		SetTextFast(GetDlgItem(hDlg, IDC_COMMENT), gpRun->pComment);
		break;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_NAME), &gpRun->pRunName);
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_COMMENT), &gpRun->pComment);

			if (gRunType != RUNTYPE_SAVECHANGES)
			{
				char *pWarningString = NULL;
				if (CheckRunForWarnings(gpRun, &pWarningString))
				{
					estrConcatf(&pWarningString, "\n\n---------------\n\nPlease click OK again to proceed if you are very very confident");
					DisplayWarning("WARNING!", pWarningString);
					estrDestroy(&pWarningString);
					break;
				}
			}


			if (gRunType != RUNTYPE_SAVECHANGES)
			{
				gpRun->iLastRunTime = timeSecondsSince2000();
			}

			SaveRunToDisk(gpRun, timeSecondsSince2000());

			if (gRunType != RUNTYPE_SAVECHANGES)
			{
				RunTheShard(gpRun);
				SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_WATCHTHELAUNCH, 0, IDD_WATCHLAUNCH,
					true, watchTheLaunchDlgProc_SWM, watchTheLaunchDlgProc_SWMTick, NULL);
			}
			pWindow->bCloseRequested = true;
			break;
		case IDC_BACK:
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_MAINSCREEN, 0, IDD_MAINSCREEN,
				true, mainScreenDlgProc_SWM, mainScreenDlgProc_SWMTick, NULL);
			pWindow->bCloseRequested = true;
			break;
		}
	}






	return false;
}