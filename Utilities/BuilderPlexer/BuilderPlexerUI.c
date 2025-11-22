#include "BuilderPlexerUI.h"
#include "Resource.h"
#include "BuilderPlexer.h"
#include "estring.h"
#include "winInclude.h"
#include "winutil.h"
#include "earray.h"
#include "timing.h"
#include "alerts.h"
#include "stringCache.h"


static char *gpPickerName = NULL;
static char *gpPickerLabel = NULL;
static char **gppPickerChoices = NULL;
static char **gppPickerDescriptions = NULL;
static char *gpPickerOutput = NULL;
static char *gpPickerDefaultChoice = NULL;
static bool gbPickerResult = false;







BOOL CALLBACK pickerDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	LRESULT lResult;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		SetTimer(hDlg, 0, 1, NULL);

		assert(gppPickerChoices);

		SetTextFast(hDlg, gpPickerName);
		SetTextFast(GetDlgItem(hDlg, IDC_PICKERLABEL), gpPickerLabel);

		SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_RESETCONTENT, 0, 0);

		for (i=0; i < eaSize(&gppPickerChoices); i++)
		{
			lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_ADDSTRING, 0, (LPARAM)gppPickerChoices[i]);
			SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
		}
			
		if (gpPickerDefaultChoice)
		{
			int iIndex = eaFindString(&gppPickerChoices, gpPickerDefaultChoice);

			if (iIndex == -1)
			{
				iIndex = 0;
			}
		
			SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_SELECTSTRING, 0, (LPARAM)gppPickerChoices[iIndex]);
		}
		else
		{
			SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_SELECTSTRING, 0, (LPARAM)gppPickerChoices[0]);
		}


		ShowWindow(hDlg, SW_SHOW);
		return true; 

	case WM_TIMER:
		if (gppPickerDescriptions && eaSize(&gppPickerDescriptions))
		{
			lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PICKERDESC), gppPickerDescriptions[lResult]);
				}
			}
		}


		break;


	case WM_CLOSE:
		EndDialog(hDlg, 0);
		gbPickerResult = false;
		estrPrintf(&gpPickerOutput, "Cancelled by user");
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_PICKER_OK:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					estrCopy2(&gpPickerOutput, gppPickerChoices[lResult]);
					EndDialog(hDlg, 0);
					gbPickerResult = true;
				}
			}
		}
		break;


	}

	
	return false;
}



bool PickerWithDescriptions(char *pPickerName, char *pPickerLabel, char ***pppInChoices, char ***pppInDescriptions, char **ppOutChoice, char *pDefaultChoice)
{
	int i;

	estrCopy2(&gpPickerName, pPickerName);
	estrCopy2(&gpPickerLabel, pPickerLabel);
	estrFixupNewLinesForWindows(&gpPickerLabel);
	eaDestroyEx(&gppPickerChoices, NULL);
	eaDestroyEx(&gppPickerDescriptions, NULL);

	for (i=0; i < eaSize(pppInChoices); i++)
	{
		eaPush(&gppPickerChoices, strdup((*pppInChoices)[i]));
		eaPush(&gppPickerDescriptions, strdup((*pppInDescriptions)[i]));
	}

	estrDestroy(&gpPickerOutput);

	
	gpPickerDefaultChoice = pDefaultChoice;


	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_PICKER), 0, (DLGPROC)pickerDlgProc);


	estrCopy(ppOutChoice, &gpPickerOutput);
	return gbPickerResult;
}

int UI_Picker(char *pTitleString, char ***pppNames, char ***pppDescriptions)
{
	char *pOutChoice = NULL;
	int i;

	if (!(PickerWithDescriptions("BuilderPlexer", pTitleString, pppNames, pppDescriptions, &pOutChoice, "")))
	{
		exit(-1);
	}

	for (i=0 ; i < eaSize(pppNames); i++)
	{
		if (stricmp((*pppNames)[i], pOutChoice) == 0)
		{
			return i;
		}
	}

	exit(-1);

	return 0;
}





static char *spDisplayMessageString = NULL;


BOOL CALLBACK showTextDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	switch (iMsg)
	{

	case WM_INITDIALOG:
		SetTextFast(GetDlgItem(hDlg, IDC_TEXT), spDisplayMessageString);

		ShowWindow(hDlg, SW_SHOW);
		return true; 


	case WM_CLOSE:
		exit(-1);
		EndDialog(hDlg, 0);
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
		
			EndDialog(hDlg, 0);
		}
		break;


	}

	
	return false;
}



void UI_DisplayMessage(char *pMessage, ...)
{
	estrClear(&spDisplayMessageString);
	estrGetVarArgs(&spDisplayMessageString, pMessage);

	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_SHOWTEXT), 0, (DLGPROC)showTextDlgProc);
}



static char *pGetStringLabel = NULL;
static char *pGetStringDest = NULL;



BOOL CALLBACK getStringDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	switch (iMsg)
	{

	case WM_INITDIALOG:
		SetTextFast(GetDlgItem(hDlg, IDC_TEXT), pGetStringLabel);

		ShowWindow(hDlg, SW_SHOW);
		return true; 


	case WM_CLOSE:
		exit(-1);
		estrGetWindowText(&pGetStringDest, GetDlgItem(hDlg, IDC_GETSTRING));
		EndDialog(hDlg, 0);
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
		
			estrGetWindowText(&pGetStringDest, GetDlgItem(hDlg, IDC_GETSTRING));
			EndDialog(hDlg, 0);
		}
		break;


	}

	
	return false;
}


void UI_GetString(char **ppOutString, char *pFmt, ...)
{
	estrClear(&pGetStringLabel);
	estrGetVarArgs(&pGetStringLabel, pFmt);

	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_GETSTRING), 0, (DLGPROC)getStringDlgProc);

	estrCopy(ppOutString, &pGetStringDest);
}
