#include "ContinuousBuilder.h"
#include "CBConfig.h"
#include "CBStartup.h"
#include "resource.h"
#include "earray.h"
#include "textparser.h"
#include "file.h"
#include "WinInclude.h"
#include "Estring.h"
#include "WinUtil.h"
#include "CBOverrideables.h"
#include "CBConfig_h_ast.h"
#include "cbZoomedInTextEditor.h"
#include "net/net.h"
#include "CBReportToCBMonitor.h"
#include "UtilitiesLib.h"
#include "UTF8.h"

bool gbAutoStart = false;

AUTO_CMD_INT(gbAutoStart, AutoStart) ACMD_COMMANDLINE;


static OverrideableVariable ***spppActualVars; //will point to either gConfig.ppOverrideableVariables or gConfig.ppDevOnlyOverrideableVariables

static OverrideableVariable **sppCurrentVars; //subset of sppActualVars, pointing into that array (NOT COPIES) based on which
	//ParentOverrides are true

//super double-click catching code from jdrago

#define WM_DOUBLECLICKED_CHILD (WM_USER+500)

LRESULT FAR PASCAL CatchDoubleClickProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam) {
	WNDPROC pOriginalProc = (WNDPROC)((intptr_t)GetWindowLongPtr(hWnd, GWL_USERDATA));
	// assert(pOriginalProc);

	if(Message == WM_LBUTTONDBLCLK)
	{
		PostMessage(GetParent(hWnd), WM_DOUBLECLICKED_CHILD, GetDlgCtrlID(hWnd), 0);
		return 0;
	}

	return CallWindowProc(pOriginalProc, hWnd, Message, wParam, lParam); }

void interceptDoubleClicks(HWND hDlg, int id) {
	WNDPROC pOriginalProc;
	HWND hCtrl = GetDlgItem(hDlg, id);
	if(hCtrl == INVALID_HANDLE_VALUE)
		return;

	pOriginalProc = (WNDPROC)((intptr_t)SetWindowLongPtr(hCtrl, GWL_WNDPROC, (LONG_PTR) CatchDoubleClickProc));
	SetWindowLongPtr(hCtrl, GWL_USERDATA, (LONG_PTR) pOriginalProc); }



char *GetDefaultValue(OverrideableVariable *pVar)
{
	if (gConfig.bDev)
	{
		if (pVar->pDevModeDefaultValue && pVar->pDevModeDefaultValue[0])
		{
			return pVar->pDevModeDefaultValue;
		}
	}

	return pVar->pNormalDefaultValue;
}

static U32 iVarNameIDs[] = 
{
	IDC_VARNAME1,
	IDC_VARNAME2,
	IDC_VARNAME3,
	IDC_VARNAME4,
	IDC_VARNAME5,
	IDC_VARNAME6,
	IDC_VARNAME7,
	IDC_VARNAME8,
	IDC_VARNAME9,
	IDC_VARNAME10,
	IDC_VARNAME11,
	IDC_VARNAME12,
	IDC_VARNAME13,
	IDC_VARNAME14,
	IDC_VARNAME15,
	IDC_VARNAME16,
	IDC_VARNAME17,
	IDC_VARNAME18,
	IDC_VARNAME19,
	IDC_VARNAME20,
	IDC_VARNAME21,
	IDC_VARNAME22,
	IDC_VARNAME23,
	IDC_VARNAME24,
	IDC_VARNAME25,
	IDC_VARNAME26,
	IDC_VARNAME27,
	IDC_VARNAME28,
	IDC_VARNAME29,
	IDC_VARNAME30,
	IDC_VARNAME31,
	IDC_VARNAME32,
	IDC_VARNAME33,
	IDC_VARNAME34,
	IDC_VARNAME35,
};

static U32 iVarValueIDs[] = 
{
	IDC_VARVALUE1,
	IDC_VARVALUE2,
	IDC_VARVALUE3,
	IDC_VARVALUE4,
	IDC_VARVALUE5,
	IDC_VARVALUE6,
	IDC_VARVALUE7,
	IDC_VARVALUE8,
	IDC_VARVALUE9,
	IDC_VARVALUE10,
	IDC_VARVALUE11,
	IDC_VARVALUE12,
	IDC_VARVALUE13,
	IDC_VARVALUE14,
	IDC_VARVALUE15,
	IDC_VARVALUE16,
	IDC_VARVALUE17,
	IDC_VARVALUE18,
	IDC_VARVALUE19,
	IDC_VARVALUE20,
	IDC_VARVALUE21,
	IDC_VARVALUE22,
	IDC_VARVALUE23,
	IDC_VARVALUE24,
	IDC_VARVALUE25,
	IDC_VARVALUE26,
	IDC_VARVALUE27,
	IDC_VARVALUE28,
	IDC_VARVALUE29,
	IDC_VARVALUE30,
	IDC_VARVALUE31,
	IDC_VARVALUE32,
	IDC_VARVALUE33,
	IDC_VARVALUE34,
	IDC_VARVALUE35,
};

static U32 iVarDescIDs[] = 
{
	IDC_VARDESC1,
	IDC_VARDESC2,
	IDC_VARDESC3,
	IDC_VARDESC4,
	IDC_VARDESC5,
	IDC_VARDESC6,
	IDC_VARDESC7,
	IDC_VARDESC8,
	IDC_VARDESC9,
	IDC_VARDESC10,
	IDC_VARDESC11,
	IDC_VARDESC12,
	IDC_VARDESC13,
	IDC_VARDESC14,
	IDC_VARDESC15,
	IDC_VARDESC16,
	IDC_VARDESC17,
	IDC_VARDESC18,
	IDC_VARDESC19,
	IDC_VARDESC20,
	IDC_VARDESC21,
	IDC_VARDESC22,
	IDC_VARDESC23,
	IDC_VARDESC24,
	IDC_VARDESC25,
	IDC_VARDESC26,
	IDC_VARDESC27,
	IDC_VARDESC28,
	IDC_VARDESC29,
	IDC_VARDESC30,
	IDC_VARDESC31,
	IDC_VARDESC32,
	IDC_VARDESC33,
	IDC_VARDESC34,
	IDC_VARDESC35,
};

static U32 iRestoreIDs[] = 
{
	IDC_RESTORE1,
	IDC_RESTORE2,
	IDC_RESTORE3,
	IDC_RESTORE4,
	IDC_RESTORE5,
	IDC_RESTORE6,
	IDC_RESTORE7,
	IDC_RESTORE8,
	IDC_RESTORE9,
	IDC_RESTORE10,
	IDC_RESTORE11,
	IDC_RESTORE12,
	IDC_RESTORE13,
	IDC_RESTORE14,
	IDC_RESTORE15,
	IDC_RESTORE16,
	IDC_RESTORE17,
	IDC_RESTORE18,
	IDC_RESTORE19,
	IDC_RESTORE20,
	IDC_RESTORE21,
	IDC_RESTORE22,
	IDC_RESTORE23,
	IDC_RESTORE24,
	IDC_RESTORE25,
	IDC_RESTORE26,
	IDC_RESTORE27,
	IDC_RESTORE28,
	IDC_RESTORE29,
	IDC_RESTORE30,
	IDC_RESTORE31,
	IDC_RESTORE32,
	IDC_RESTORE33,
	IDC_RESTORE34,
	IDC_RESTORE35,
};

#define BAD_COLOR RGB(240,0,0)

void MakeCurrentVars(HWND hDlg, bool bFirstTime)
{
	int i;
	int j;

	if (!bFirstTime)
	{
		for (i=0; i < eaSize(&sppCurrentVars); i++)
		{
			OverrideableVariable *pVar = sppCurrentVars[i];
			HANDLE hCurValHandle = GetDlgItem(hDlg, iVarValueIDs[i]);
		
			GetWindowText_UTF8(hCurValHandle, &pVar->pCurVal);

		}
	}

	eaDestroy(&sppCurrentVars);

	for (i=0; i < eaSize(spppActualVars); i++)
	{
		OverrideableVariable *pVar = (*spppActualVars)[i];

		if (pVar->pParentOverride && pVar->pParentOverride[0])
		{
			for (j=0; j < eaSize(spppActualVars); j++)
			{
				OverrideableVariable *pOtherVar = (*spppActualVars)[j];

				if (stricmp(pOtherVar->pVarName, pVar->pParentOverride) == 0)
				{
					if (StringIsAllWhiteSpace(pOtherVar->pCurVal) || strcmp(pOtherVar->pCurVal, "0") == 0)
					{
						//do nothing, the parent is not set
					}
					else
					{
						eaPush(&sppCurrentVars, pVar);
					}
					break;
				}
			}

			assertmsgf(j < eaSize(spppActualVars), "Couldn't find ParentOverride variable named %s. Your CB config is faulty.",
				pVar->pParentOverride);
		}
		else
		{
			eaPush(&sppCurrentVars, pVar);
		}
	}
}

//this is so kludgy, there must be a better way
void GetWindowRect_Client(HWND hParent, HWND hChild, RECT *pRect)
{
	POINT p;
	GetWindowRect(hChild, pRect);
	p.x = pRect->left;
	p.y = pRect->top;
	ScreenToClient(hParent, &p);
	pRect->left = p.x;
	pRect->top = p.y;

	p.x = pRect->right;
	p.y = pRect->bottom;
	ScreenToClient(hParent, &p);
	pRect->right = p.x;
	pRect->bottom = p.y;
}



void CheckForParentsAndSetupScreen(HWND hDlg, bool bFirstTime)
{
	int i;
	int iCurSize;
	static bool bInside = false;
	OverrideableVariable **ppCopy = NULL;

	if (bInside)
	{
		return;
	}
	bInside = true;


	if (!bFirstTime)
	{
		eaCopy(&ppCopy, &sppCurrentVars);
	}

	MakeCurrentVars(hDlg, bFirstTime);

	if (!bFirstTime)
	{
		bool bChanged = false;

		if (eaSize(&ppCopy) != eaSize(&sppCurrentVars))
		{
			bChanged = true;
		}
		eaDestroy(&ppCopy);

		if (!bChanged)
		{
			bInside = false;
			return;
		}
	}

	iCurSize = MIN(eaSize(&sppCurrentVars), ARRAY_SIZE(iVarNameIDs));
	assert(iCurSize);
	assert(iCurSize <= ARRAY_SIZE(iVarNameIDs));

	for (i=0; i < iCurSize; i++)
	{
		OverrideableVariable *pVar = sppCurrentVars[i];

		SetTextFast(GetDlgItem(hDlg, iVarNameIDs[i]), pVar->pVarName);
		SetTextFast(GetDlgItem(hDlg, iVarDescIDs[i]), pVar->pDescription);
		SetTextFast(GetDlgItem(hDlg, iVarValueIDs[i]), pVar->pCurVal);
	}

	for (i = 0; i < ARRAY_SIZE(iVarNameIDs); i++)
	{
		ShowWindow(GetDlgItem(hDlg, iVarNameIDs[i]), i < eaSize(&sppCurrentVars) ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iVarDescIDs[i]), i < eaSize(&sppCurrentVars) ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iVarValueIDs[i]), i < eaSize(&sppCurrentVars) ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iRestoreIDs[i]), i < eaSize(&sppCurrentVars) ? SW_SHOW : SW_HIDE);
	}

	{
		RECT topButtonRect;
		RECT bottomButtonRect;
		RECT curWindowRect;

		static RECT name0;
		static RECT value0;
		static RECT desc0;
		static RECT restore0;
		static RECT refWindowRect;


		static bool bFirst = true;
		int iYDelta;

		GetWindowRect(hDlg, &curWindowRect);


		if (bFirst)
		{
			bFirst = false;
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iVarNameIDs[0]), &name0);
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iVarValueIDs[0]), &value0);
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iVarDescIDs[0]), &desc0);
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iRestoreIDs[0]), &restore0);
			GetWindowRect(hDlg, &refWindowRect);

		}



		//move the bottom of the window up/down so that we only see as many options as exist
		GetWindowRect(GetDlgItem(hDlg, iRestoreIDs[ARRAY_SIZE(iRestoreIDs)-1]), &bottomButtonRect);
		GetWindowRect(GetDlgItem(hDlg, iRestoreIDs[iCurSize-1]), &topButtonRect);

		iYDelta = bottomButtonRect.bottom - topButtonRect.bottom;

		SetWindowPos(hDlg, HWND_NOTOPMOST, curWindowRect.left, curWindowRect.top, refWindowRect.right - refWindowRect.left, 
			refWindowRect.bottom - refWindowRect.top - iYDelta, SWP_NOMOVE);

		

		//for each option, move it left/right for purposes of indenting
		for (i=0; i < iCurSize; i++)
		{
			RECT tempRect;
			int iIndent;

			if (sppCurrentVars[i]->pParentOverride && sppCurrentVars[i]->pParentOverride[0])
			{
				iIndent = 15;
			}
			else
			{
				iIndent = 0;
			}
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iVarNameIDs[i]), &tempRect);
			SetWindowPos(GetDlgItem(hDlg, iVarNameIDs[i]), 0, name0.left + iIndent, tempRect.top, 0, 0, SWP_NOSIZE);
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iVarValueIDs[i]), &tempRect);
			SetWindowPos(GetDlgItem(hDlg, iVarValueIDs[i]), 0, value0.left + iIndent, tempRect.top, 0, 0, SWP_NOSIZE);
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iVarDescIDs[i]), &tempRect);
			SetWindowPos(GetDlgItem(hDlg, iVarDescIDs[i]), 0, desc0.left + iIndent, tempRect.top, 0, 0, SWP_NOSIZE);
			GetWindowRect_Client(hDlg, GetDlgItem(hDlg, iRestoreIDs[i]), &tempRect);
			SetWindowPos(GetDlgItem(hDlg, iRestoreIDs[i]), 0, restore0.left + iIndent, tempRect.top, 0, 0, SWP_NOSIZE);
		}

	}


//		SetTimer(hDlg, 0, 1, NULL);

	InvalidateRect(hDlg, NULL, false);

	ShowWindow(hDlg, SW_SHOW);
	bInside = false;
}


BOOL CALLBACK overrideablesDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	int i;
//	LRESULT lResult;

	static HBRUSH normal;
	static HBRUSH bad;

	int iCurSize = MIN(eaSize(&sppCurrentVars), ARRAY_SIZE(iVarNameIDs));


	switch (iMsg)
	{

	case WM_INITDIALOG:

		SetTimer(hDlg, 0, 1, NULL);

		bad = CreateSolidBrush(BAD_COLOR);
		CheckForParentsAndSetupScreen(hDlg, true);

		for (i=0; i < ARRAY_SIZE(iVarNameIDs); i++)
		{
			interceptDoubleClicks(hDlg, iVarValueIDs[i]);
		}
		
		return true; 

	case WM_TIMER:
		commMonitor(commDefault());
		CBReportToCBMonitor_Update();
		utilitiesLibOncePerFrame(REAL_TIME);
		break;

	case WM_DOUBLECLICKED_CHILD:
		for (i=0; i < iCurSize; i++)
		{
			if (wParam == iVarValueIDs[i])
			{
				char *pCommentString = NULL;
				char *pEString = NULL;

				char *pVarName = NULL;
				
		
				GetWindowText_UTF8(GetDlgItem(hDlg, iVarValueIDs[i]), &pEString);

				GetWindowText_UTF8(GetDlgItem(hDlg, iVarNameIDs[i]), &pVarName);

				estrPrintf(&pCommentString, "Please set the value for variable %s", pVarName);

				InvokeZoomedInTextEditor(pCommentString, &pEString, 
					(*spppActualVars)[i]->bSortedIntList ? ZITE_SORTED_INTS : 
					((*spppActualVars)[i]->bCommaSeparatedZoomInList ? ZITE_COMMA_SEPARATED : ZITE_NORMAL));

				SetTextFast(GetDlgItem(hDlg, iVarValueIDs[i]), pEString);

				estrDestroy(&pCommentString);
				estrDestroy(&pEString);
				estrDestroy(&pVarName);
				break;
			}
		}
		break;



	case WM_CTLCOLOREDIT:
		for (i=0; i < iCurSize; i++)
		{
			OverrideableVariable *pVar = sppCurrentVars[i];
			HANDLE hCurValHandle = GetDlgItem(hDlg, iVarValueIDs[i]);

			if ((HANDLE)lParam == hCurValHandle)
			{
				static char *pCurText = NULL;
				GetWindowText_UTF8(hCurValHandle, &pCurText);

				if (stricmp(GetDefaultValue(pVar), pCurText) == 0)
				{
					return false;
				}
				else
				{
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, BAD_COLOR);
					return (BOOL)((intptr_t)bad);
				}
			}
		}
		return false;


	case WM_CLOSE:
		exit(-1);
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
			for (i=0; i < iCurSize; i++)
			{
				OverrideableVariable *pVar = sppCurrentVars[i];
				HANDLE hCurValHandle = GetDlgItem(hDlg, iVarValueIDs[i]);
	
				estrGetWindowText(&pVar->pCurVal, hCurValHandle);	
			}


			EndDialog(hDlg, 0);
			break;
		
		}
	
		for (i=0; i < iCurSize; i++)
		{
			if (iRestoreIDs[i] == LOWORD (wParam))
			{
				static char *pCurText = NULL;
				OverrideableVariable *pVar = sppCurrentVars[i];
				GetWindowText_UTF8(GetDlgItem(hDlg, iVarValueIDs[i]), &pCurText);
				
				if (stricmp(pCurText, GetDefaultValue(pVar)) == 0)
				{
					SetTextFast(GetDlgItem(hDlg, iVarValueIDs[i]), pVar->pCurVal);
				}
				else
				{
					SetTextFast(GetDlgItem(hDlg, iVarValueIDs[i]), GetDefaultValue(pVar));
				}

				CheckForParentsAndSetupScreen(hDlg, false);
				break;
			}

			if (iVarValueIDs[i] == LOWORD(wParam))
			{
				CheckForParentsAndSetupScreen(hDlg, false);
				break;
			}
		}
		



		break;


	}

	return false;
}

OverrideableVariable *FindOverrideableVar(const char *pName)
{
	int i;

	for (i=0; i < eaSize(spppActualVars); i++)
	{
		if (stricmp(pName, (*spppActualVars)[i]->pVarName) == 0)
		{
			return (*spppActualVars)[i];
		}
	}

	return NULL;
}


static void SetCurOverrideableValue(char *pName, char *pValue)
{
	OverrideableVariable *pVar = FindOverrideableVar(pName);

	if (pVar)
	{
		estrCopy2(&pVar->pCurVal, pValue);
	}
}

static void LoadCurrentOverrides(char *pFileName)
{
	char *pBuf = fileAlloc(pFileName, NULL);

	char **ppLines = NULL;

	int i;

	assertmsgf(pBuf, "Couldn't load overrides file %s\n", pFileName);

	DivideString(pBuf, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	for (i=0; i < eaSize(&ppLines); i++)
	{
		char **ppSubLines = NULL;

		if (strStartsWith(ppLines[i], "//") || strStartsWith(ppLines[i], "#"))
		{
			continue;
		}

		DivideString(ppLines[i], "=", &ppSubLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		if (eaSize(&ppSubLines) == 1 && strEndsWith(ppLines[i], "="))
		{
			SetCurOverrideableValue(ppSubLines[0], "");
		}
		else
		{
			char *pTemp = NULL;
			assertmsgf(eaSize(&ppSubLines) == 2, "Invalid syntax. Expected name = value. File %s. Line: \"%s\"",
				pFileName, ppLines[i]);

			if (strStartsWith(ppSubLines[1], "(ESC)"))
			{
				estrAppendUnescaped(&pTemp, ppSubLines[1] + 5);
				SetCurOverrideableValue(ppSubLines[0], pTemp);
				estrDestroy(&pTemp);
			}
			else
			{
				SetCurOverrideableValue(ppSubLines[0], ppSubLines[1]);
			}
		}
	
		eaDestroyEx(&ppSubLines, NULL);
	}

	eaDestroyEx(&ppLines, NULL);
}

void FixupOverrideables(void)
{
	int i, j;

	for (i=0; i < eaSize(spppActualVars); i++)
	{
		OverrideableVariable *pVar = (*spppActualVars)[i];

		assertmsg(GetDefaultValue(pVar) && pVar->pVarName && pVar->pDescription
			&& GetDefaultValue(pVar)[0] && pVar->pVarName[0] && pVar->pDescription[0], 
			"An overrideable variable must have non-empty name, description and default value");

		//now look for any other overrideable with the same name. If we find any, copy their default values
		//then delete them

		j = i + 1;

		while (j < eaSize(spppActualVars))
		{
			OverrideableVariable *pOtherVar = (*spppActualVars)[j];
			if (stricmp(pVar->pVarName, pOtherVar->pVarName) == 0)
			{
				SAFE_FREE(pVar->pDevModeDefaultValue);
				SAFE_FREE(pVar->pNormalDefaultValue);

				pVar->pDevModeDefaultValue = pOtherVar->pDevModeDefaultValue;
				pVar->pNormalDefaultValue = pOtherVar->pNormalDefaultValue;

				pOtherVar->pDevModeDefaultValue = NULL;
				pOtherVar->pNormalDefaultValue = NULL;

				StructDestroy(parse_OverrideableVariable, pOtherVar);
				eaRemove(spppActualVars, j);
			}
			else
			{
				j++;
			}
		}
	}
}

		
void WriteOutCurrentOverrides(char *pFileName)
{
	char *pStr = NULL;
	int i;
	FILE *pFile;

	mkdirtree_const(pFileName);

	estrPrintf(&pStr, "//This file AUTOGENERATED by CB.exe\n\n");

	for (i=0; i < eaSize(spppActualVars); i++)
	{
		OverrideableVariable *pVar = (*spppActualVars)[i];
		estrConcatf(&pStr, "//%s\n%s = ", pVar->pDescription, pVar->pVarName);
		
		if (strchr(pVar->pCurVal, '\n') || strchr(pVar->pCurVal, '\r'))
		{
			estrConcatf(&pStr, "(ESC)");
			estrAppendEscaped(&pStr, pVar->pCurVal);
		}
		else
		{
			estrConcatf(&pStr, "%s", pVar->pCurVal);
		}

		estrConcatf(&pStr, "\n\n");
	}

	pFile = fopen(pFileName, "wt");
	if (pFile)
	{
		fprintf(pFile, "%s", pStr);
		fclose(pFile);
	}


	estrDestroy(&pStr);
}


void CB_DoOverrides(bool bDevOnlyOverrideables)
{
	int i;
	char fileName[CRYPTIC_MAX_PATH];

	assert(ARRAY_SIZE(iVarNameIDs) == ARRAY_SIZE(iVarValueIDs) && ARRAY_SIZE(iVarValueIDs) == ARRAY_SIZE(iVarDescIDs)
		&& ARRAY_SIZE(iVarNameIDs) == ARRAY_SIZE(iRestoreIDs));

	if (bDevOnlyOverrideables)
	{
		spppActualVars = &gConfig.ppDevOnlyOverrideableVariables;
	}
	else
	{
		spppActualVars = &gConfig.ppOverrideableVariables;
	}




	if (!eaSize(spppActualVars))
	{
		return;
	}

	//remove duplicated-named defaults, using the one later in the list, also verify 
	//everything is legal
	FixupOverrideables();


	sprintf(fileName, "c:\\ContinuousBuilder\\%s\\overrides_%s%s.txt", gpCBProduct->pProductName,
		gpCBType->pShortTypeName,
		bDevOnlyOverrideables ? "_DEVONLY" : "");

	if (fileExists(fileName))
	{
		LoadCurrentOverrides(fileName);
	}



	if (bDevOnlyOverrideables)
	{
/*we need to do dev-only overrides if one of three things is true:
	1. gConfig.bDev
	2. any values set to non-defaults
	3. shift key being held down
*/
		
		
		bool bNeedToDoIt = false;

		if (gConfig.bDev ||  GetAsyncKeyState(VK_SHIFT) & 0x8000000)
		{
			bNeedToDoIt = true;
		}
		else 
		{
			for (i=0; i < eaSize(spppActualVars); i++)
			{
				OverrideableVariable *pVar = (*spppActualVars)[i];

				if (pVar->pCurVal && stricmp(pVar->pCurVal, pVar->pNormalDefaultValue) != 0)
				{
					bNeedToDoIt = true;
					break;
				}
			}
		}

		if (!bNeedToDoIt)
		{

			for (i=0; i < eaSize(spppActualVars); i++)
			{
				estrCopy2(&((*spppActualVars)[i]->pCurVal), GetDefaultValue((*spppActualVars)[i]));
			}

			return;
		}
	}






	for (i=0; i < eaSize(spppActualVars); i++)
	{
		if ((*spppActualVars)[i]->pCurVal == NULL)
		{
			estrCopy2(&((*spppActualVars)[i]->pCurVal), GetDefaultValue((*spppActualVars)[i]));
		}
	}

	if (!gbAutoStart)
	{
		DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_OVERRIDEABLES), ghCBDlg, (DLGPROC)overrideablesDlgProc);
	}

	WriteOutCurrentOverrides(fileName);
}
