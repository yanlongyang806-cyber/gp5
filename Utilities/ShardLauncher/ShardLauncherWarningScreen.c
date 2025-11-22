#include "shardLauncherWarningScreen.h"
#include "ShardLauncher.h"
#include "resource.h"
#include "EString.h"
#include "cmdparse.h"
#include "UTF8.h"

#define MAX_WARNINGS 64

static char *pWarnings[MAX_WARNINGS] = {0};
static char *pTitles[MAX_WARNINGS] = {0};

static int iNextID = 0;

static char *spExtraButtonCmd = NULL;
static char *spExtraButtonName = NULL;

bool PushExtraButton(void)
{
	char *pTemp = NULL;
	bool bRetVal = false;
	
	if (!(spExtraButtonCmd && spExtraButtonCmd[0]))
	{
		return false;
	}

	estrCopy2(&pTemp, spExtraButtonCmd);

	if (estrReplaceOccurrences(&pTemp, "(OK)", ""))
	{
		bRetVal = true;
	}

	globCmdParse(pTemp);

	estrDestroy(&pTemp);

	return bRetVal;
}


BOOL shardLauncherWarningsDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			SetWindowText_UTF8(hDlg, pTitles[pWindow->iUserIndex % MAX_WARNINGS]);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_WARNING), pWarnings[pWindow->iUserIndex % MAX_WARNINGS]);
			SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SendMessage (GetDlgItem(hDlg, IDC_WARNING), EM_SETSEL, 0, 0);

			if (pWindow->iUserIndex >= 1000000)
			{
				ShowWindow(GetDlgItem(hDlg, IDC_CONTINUE), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_ABORT), SW_SHOW);
			}
			else
			{
				ShowWindow(GetDlgItem(hDlg, IDC_CONTINUE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_ABORT), SW_HIDE);
			}

			if (spExtraButtonCmd)
			{
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EXTRABUTTON), spExtraButtonName);
				ShowWindow(GetDlgItem(hDlg, IDC_EXTRABUTTON), SW_SHOW);

				SAFE_FREE(spExtraButtonName);
			}
			else
			{
				ShowWindow(GetDlgItem(hDlg, IDC_EXTRABUTTON), SW_HIDE);
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_CONTINUE:
			pWindow->bCloseRequested = true;
			return false;
		case IDC_ABORT:
			exit(-1);
			return false;
		case IDC_EXTRABUTTON:
			if (PushExtraButton())
			{
				pWindow->bCloseRequested = true;
			}
			return false;

		}
		break;


	case WM_CLOSE:
			if (pWindow->iUserIndex >= 1000000)
			{
				exit(-1);
			}
			pWindow->bCloseRequested = true;
		
		break;
	}
	

	return false;
}


void DisplayWarning(const char *pTitle, char *pStr)
{
	int iID = iNextID++;
	int iIndex = 0;

	estrCopy2(&pTitles[iID % MAX_WARNINGS], pTitle);

	
	//always want spacing between all lines of warnings, so replace all single /n with /n/n before doing
	//newline fixup
	while (1)
	{
		char *pNextNewLine = strchr(pStr + iIndex, '\n');

		if (!pNextNewLine)
		{
			break;
		}

		iIndex = pNextNewLine - pStr;

		if (pStr[iIndex + 1] && pStr[iIndex + 1] != '\n')
		{
			estrInsert(&pStr, iIndex, "\n", 1);
			iIndex+=2;
		}
		else
		{
			iIndex += 1;
		}
	}

	estrCopy2(&pWarnings[iID % MAX_WARNINGS], pStr);

	estrFixupNewLinesForWindows(&pWarnings[iID % MAX_WARNINGS]);


	SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_WARNING, iID, IDD_WARNING, 
			false, shardLauncherWarningsDlgProc_SWM, NULL, NULL);
}

void DisplayWarningf(const char *pTitle, FORMAT_STR const char* format, ...)
{
	char *pFullString = NULL;
	estrGetVarArgs(&pFullString, format);

	DisplayWarning(pTitle, pFullString);

	estrDestroy(&pFullString);
}


void DisplayConfirmation(char *pStr, char *pExtraButtonName, char *pExtraButtonCmd)
{
	int iID = iNextID++;

	estrCopy2(&pTitles[iID % MAX_WARNINGS], "You must confirm something to proceed");

	estrCopy2(&pWarnings[iID % MAX_WARNINGS], pStr);
	estrFixupNewLinesForWindows(&pWarnings[iID % MAX_WARNINGS]);

	if (pExtraButtonName)
	{
		spExtraButtonName = strdup(pExtraButtonName);
	}
	else
	{
		SAFE_FREE(spExtraButtonName);
	}

	if (pExtraButtonCmd)
	{
		spExtraButtonCmd = strdup(pExtraButtonCmd);
	}
	else
	{
		SAFE_FREE(spExtraButtonCmd);
	}


	SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_WARNING, iID + 1000000, IDD_WARNING, 
			false, shardLauncherWarningsDlgProc_SWM, NULL, NULL);
}

bool AreWarningsCurrentlyDisplaying(void)
{
	if (SimpleWindowManager_FindWindowByType(WINDOWTYPE_WARNING))
	{
		return true;
	}
	return false;
}
