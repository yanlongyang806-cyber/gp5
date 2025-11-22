#include "winInclude.h"
#include "estring.h"
#include "earray.h"
#include "resource.h"
#include "utils.h"
#include "winutil.h"
#include "timing.h"
#include "alerts.h"
#include "stringcache.h"
#include "winutil.h"
#include "ShardLauncherUI.h"
#include "stringutil.h"
#include "objPath.h"
#include "stringCache.h"
#include "UTF8.h"

int GetComboBoxSelectedIndex(HWND hDlg, U32 iResID)
{
	LRESULT lResult;
	lResult = SendMessage(GetDlgItem(hDlg, iResID), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if (lResult != CB_ERR) 
	{
		lResult = SendMessage(GetDlgItem(hDlg, iResID), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
		if (lResult != CB_ERR) 
		{
			return (int)lResult;
		}
	}

	return -1;
}

void SetComboBoxFromEarrayWithDefault(HWND hDlg, U32 iResID, void ***pppArray, ParseTable *pTable,
	char *pStartingValue)
{
	const char *pStartingStr = NULL;
	int i;
	LRESULT lResult;

	SendMessage(GetDlgItem(hDlg, iResID), CB_RESETCONTENT, 0, 0);


	for (i=0; i < eaSize(pppArray); i++)
	{
		const char *pName;

		if (pTable)
		{
			char *pEName = NULL;
			objGetKeyEString(pTable, (*pppArray)[i], &pEName);
			pName = allocAddCaseSensitiveString(pEName);
		}
		else
		{
			pName = allocAddCaseSensitiveString((*pppArray)[i]);
		}

		lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, iResID), pName);
		SendMessage(GetDlgItem(hDlg, iResID), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);

		if (!pStartingStr || (pStartingValue && stricmp(pName, pStartingValue) == 0))
		{
			pStartingStr = pName;
		}
	}

	SendMessage_SelectString_UTF8(GetDlgItem(hDlg, iResID), pStartingStr);




}


#if 0

static char *gpPickerName = NULL;
static char *gpPickerLabel = NULL;
static char **gppPickerChoices = NULL;
static char **gppPickerDescriptions = NULL;
static char *gpPickerOutput = NULL;
static char *gpPickerDefaultChoice = NULL;
static bool gbPickerResult = false;
bool gbTriggeredAlert;

static U32 giPickerStartTime;






BOOL CALLBACK pickerDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	LRESULT lResult;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		assert(gppPickerChoices);

		SetTextFast(hDlg, gpPickerName);
		SetTextFast(GetDlgItem(hDlg, IDC_PICKERLABEL), gpPickerLabel);

		SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_RESETCONTENT, 0, 0);

		for (i=0; i < eaSize(&gppPickerChoices); i++)
		{
			lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_PICKER), gppPickerChoices[i]);
			SendMessage(GetDlgItem(hDlg, IDC_PICKER), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
		}
			
		if (gpPickerDefaultChoice)
		{
			int iIndex = eaFindString(&gppPickerChoices, gpPickerDefaultChoice);

			if (iIndex == -1)
			{
				iIndex = 0;
			}
		
			SendMessage(GetDlgItem(hDlg, IDC_PICKER), gppPickerChoices[iIndex]);
		}
		else
		{
			SendMessage(GetDlgItem(hDlg, IDC_PICKER), gppPickerChoices[0]);
		}

		SetTimer(hDlg, 0, 1, NULL);

		giPickerStartTime = timeSecondsSince2000();
		gbTriggeredAlert = false;

		ShowWindow(hDlg, SW_SHOW);
		return true; 

	case WM_TIMER:
		if (!gbTriggeredAlert && timeSecondsSince2000() - giPickerStartTime > 5 * 60)
		{
			TriggerAlert(allocAddString("BUILDSCRIPTINGTIMEOVERFLOW"), "Waiting for user input for 5 minutes", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_NONE,
				0, GLOBALTYPE_NONE, 0, getHostName(), 0);
			gbTriggeredAlert = true;


		}
		if (timeSecondsSince2000() - giPickerStartTime > 10 * 60)
		{
			EndDialog(hDlg, 0);
			gbPickerResult = false;
			estrPrintf(&gpPickerOutput, "Timed out waiting for user input");
		}

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
		break;
		}

	}

	
	return false;
}




bool PickerWithDescriptions(char *pPickerName, char *pPickerLabel, char ***pppInChoices, char ***pppInDescriptions, char **ppOutChoice, char *pDefaultChoice)
{
	int i;

	estrCopy2(&gpPickerName, pPickerName);
	estrCopy2(&gpPickerLabel, pPickerLabel);
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


U32 iChoiceNameIDs[] = 
{
	IDC_NAME1,
	IDC_NAME2,
	IDC_NAME3,
	IDC_NAME4,
	IDC_NAME5,
	IDC_NAME6,
	IDC_NAME7,
	IDC_NAME8,
	IDC_NAME9,
	IDC_NAME10,
	IDC_NAME11,
	IDC_NAME12,
	IDC_NAME13,
	IDC_NAME14,
	IDC_NAME15,
	IDC_NAME16,
	IDC_NAME17,
	IDC_NAME18,
	IDC_NAME19,
	IDC_NAME20,
	IDC_NAME21,
	IDC_NAME22,
	IDC_NAME23,
	IDC_NAME24,
	IDC_NAME25,
	IDC_NAME26,
};

U32 iRestoreIDs[] = 
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
};


U32 iEditIDs[] = 
{
	IDC_EDIT1,
	IDC_EDIT2,
	IDC_EDIT3,
	IDC_EDIT4,
	IDC_EDIT5,
	IDC_EDIT6,
	IDC_EDIT7,
	IDC_EDIT8,
	IDC_EDIT9,
	IDC_EDIT10,
	IDC_EDIT11,
	IDC_EDIT12,
	IDC_EDIT13,
	IDC_EDIT14,
	IDC_EDIT15,
	IDC_EDIT16,
	IDC_EDIT17,
	IDC_EDIT18,
	IDC_EDIT19,
	IDC_EDIT20,
	IDC_EDIT21,
	IDC_EDIT22,
	IDC_EDIT23,
	IDC_EDIT24,
	IDC_EDIT25,
	IDC_EDIT26,
};


U32 iDescIDs[] = 
{
	IDC_DESC1,
	IDC_DESC2,
	IDC_DESC3,
	IDC_DESC4,
	IDC_DESC5,
	IDC_DESC6,
	IDC_DESC7,
	IDC_DESC8,
	IDC_DESC9,
	IDC_DESC10,
	IDC_DESC11,
	IDC_DESC12,
	IDC_DESC13,
	IDC_DESC14,
	IDC_DESC15,
	IDC_DESC16,
	IDC_DESC17,
	IDC_DESC18,
	IDC_DESC19,
	IDC_DESC20,
	IDC_DESC21,
	IDC_DESC22,
	IDC_DESC23,
	IDC_DESC24,
	IDC_DESC25,
	IDC_DESC26,
};


U32 iComboIDs[] = 
{
	IDC_COMBO1,
	IDC_COMBO3,
	IDC_COMBO4,
	IDC_COMBO5,
	IDC_COMBO6,
	IDC_COMBO7,
	IDC_COMBO8,
	IDC_COMBO9,
	IDC_COMBO10,
	IDC_COMBO11,
	IDC_COMBO12,
	IDC_COMBO13,
	IDC_COMBO14,
	IDC_COMBO15,
	IDC_COMBO16,
	IDC_COMBO17,
	IDC_COMBO18,
	IDC_COMBO19,
	IDC_COMBO20,
	IDC_COMBO21,
	IDC_COMBO22,
	IDC_COMBO23,
	IDC_COMBO24,
	IDC_COMBO25,
	IDC_COMBO26,
	IDC_COMBO27,
};

U32 iCheckIDs[] = 
{
	IDC_CHECK1,
	IDC_CHECK2,
	IDC_CHECK3,
	IDC_CHECK4,
	IDC_CHECK5,
	IDC_CHECK6,
	IDC_CHECK7,
	IDC_CHECK8,
	IDC_CHECK9,
	IDC_CHECK10,
	IDC_CHECK11,
	IDC_CHECK12,
	IDC_CHECK13,
	IDC_CHECK14,
	IDC_CHECK15,
	IDC_CHECK16,
	IDC_CHECK17,
	IDC_CHECK18,
	IDC_CHECK19,
	IDC_CHECK20,
	IDC_CHECK21,
	IDC_CHECK22,
	IDC_CHECK23,
	IDC_CHECK24,
	IDC_CHECK25,
	IDC_CHECK26,
};



#define BAD_COLOR RGB(240,256,256)


MultiPickerChoice **sppChoices = NULL;

BOOL CALLBACK multiPickerDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	int i,j;
	LRESULT lResult;

	static HBRUSH normal;
	static HBRUSH bad;

	switch (iMsg)
	{

	case WM_INITDIALOG:

		bad = CreateSolidBrush(BAD_COLOR);

		SetTextFast(GetDlgItem(hDlg, IDC_TITLE), gpPickerLabel);

		for (i=0; i < eaSize(&sppChoices); i++)
		{
			MultiPickerChoice *pChoice = sppChoices[i];

			ShowWindow(GetDlgItem(hDlg, iChoiceNameIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(hDlg, iDescIDs[i]), SW_SHOW);
			ShowWindow(GetDlgItem(hDlg, iRestoreIDs[i]), SW_SHOW);

			if (pChoice->ppChoices)
			{
				ShowWindow(GetDlgItem(hDlg, iEditIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, iCheckIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, iComboIDs[i]), SW_SHOW);

				SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_RESETCONTENT, 0, 0);

				for (j=0; j < eaSize(&pChoice->ppChoices); j++)
				{
					lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[j]);
					SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)j);
				}
					
				if (*pChoice->ppOutChoice)
				{
					int iIndex = eaFindString(&gppPickerChoices, *pChoice->ppOutChoice);

					if (iIndex == -1)
					{
						iIndex = 0;
					}
				
					SendMessage(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[iIndex]);
				}
				else
				{
					SendMessage(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[0]);
				}

			}
			else if (pChoice->bIsBool)
			{
				ShowWindow(GetDlgItem(hDlg, iEditIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, iCheckIDs[i]), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, iComboIDs[i]), SW_HIDE);

				if (stricmp_safe(*pChoice->ppOutChoice, "1") == 0)
				{
					CheckDlgButton(hDlg, iCheckIDs[i], 
						BST_CHECKED);
				}
				else
				{
					CheckDlgButton(hDlg, iCheckIDs[i], 
						BST_UNCHECKED);
				}
			}
			else 
			{
				ShowWindow(GetDlgItem(hDlg, iEditIDs[i]), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, iCheckIDs[i]), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, iComboIDs[i]), SW_HIDE);
				SetTextFast(GetDlgItem(hDlg, iEditIDs[i]), *pChoice->ppOutChoice);
			}
			SetTextFast(GetDlgItem(hDlg, iChoiceNameIDs[i]), pChoice->pName);
			SetTextFast(GetDlgItem(hDlg, iDescIDs[i]), pChoice->pDesc);
		}

		for (i = eaSize(&sppChoices); i < ARRAY_SIZE(iChoiceNameIDs); i++)
		{
			ShowWindow(GetDlgItem(hDlg, iChoiceNameIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iDescIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iEditIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iRestoreIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iComboIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iCheckIDs[i]), SW_HIDE);
		}

		if (eaSize(&sppChoices) != ARRAY_SIZE(iChoiceNameIDs))
		{
			RECT topButtonRect;
			RECT bottomButtonRect;
			RECT windowRect;
			
			int iYDelta;


			GetWindowRect(GetDlgItem(hDlg, iRestoreIDs[ARRAY_SIZE(iChoiceNameIDs)-1]), &bottomButtonRect);
			GetWindowRect(GetDlgItem(hDlg, iRestoreIDs[eaSize(&sppChoices)]), &topButtonRect);
			GetWindowRect(hDlg, &windowRect);

			iYDelta = bottomButtonRect.bottom - topButtonRect.bottom;

			SetWindowPos(hDlg, HWND_NOTOPMOST, windowRect.left, windowRect.top, windowRect.right - windowRect.left, 
				windowRect.bottom - windowRect.top - iYDelta, 0);
		}


//		SetTimer(hDlg, 0, 1, NULL);

		ShowWindow(hDlg, SW_SHOW);
		return true; 

	case WM_CTLCOLOREDIT:
		for (i=0; i < eaSize(&sppChoices); i++)
		{
			MultiPickerChoice *pChoice = sppChoices[i];
			HANDLE hCurValHandle = pChoice->ppChoices ? GetDlgItem(hDlg, iComboIDs[i]) :  GetDlgItem(hDlg, iEditIDs[i]);

			if ((HANDLE)lParam == hCurValHandle)
			{
				char curText[1024];

				if (pChoice->ppChoices)
				{
					lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
						if (lResult != CB_ERR) 
						{
							strcpy(curText, pChoice->ppChoices[lResult]);
						}
					}
				}
				else
				{
					GetWindowText(hCurValHandle, SAFESTR(curText));
				}

				if (stricmp_safe(pChoice->pDefaultChoice, curText) == 0)
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

	case WM_CTLCOLORSTATIC:
		for (i=0; i < eaSize(&sppChoices); i++)
		{
			MultiPickerChoice *pChoice = sppChoices[i];
			HANDLE hCurValHandle =  GetDlgItem(hDlg, iCheckIDs[i]);

			if ((HANDLE)lParam == hCurValHandle)
			{
				char curText[1024];

				if (IsDlgButtonChecked(hDlg, iCheckIDs[i]))
				{
					sprintf(curText, "1");
				}
				else
				{
					sprintf(curText, "");
				}
		
				if (stricmp_safe(pChoice->pDefaultChoice, curText) == 0)
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
			for (i=0; i < eaSize(&sppChoices); i++)
			{
				MultiPickerChoice *pChoice = sppChoices[i];
				if (pChoice->ppChoices)
				{
					lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
						if (lResult != CB_ERR) 
						{
							estrCopy2(pChoice->ppOutChoice, pChoice->ppChoices[lResult]);
						}
					}
				}
				else if (pChoice->bIsBool)
				{
					if (IsDlgButtonChecked(hDlg, iCheckIDs[i]))
					{
						estrCopy2(pChoice->ppOutChoice, "1");
					}
					else
					{
						estrCopy2(pChoice->ppOutChoice, "");
					}
				}				
				else
				{
					HANDLE hCurValHandle = GetDlgItem(hDlg, iEditIDs[i]);
					char curText[1024];
					GetWindowText(hCurValHandle, SAFESTR(curText));
					estrCopy2(pChoice->ppOutChoice, curText);
				}
			}


			EndDialog(hDlg, 0);
			break;
		
		}
	
		for (i=0; i < eaSize(&sppChoices); i++)
		{
			if (LOWORD(wParam) == iCheckIDs[i])
			{
				InvalidateRect(GetDlgItem(hDlg, iCheckIDs[i]), NULL, false);

				break;
			}
		}



		for (i=0; i < eaSize(&sppChoices); i++)
		{
			if (iRestoreIDs[i] == LOWORD (wParam))
			{
				char curText[1024];
				MultiPickerChoice *pChoice = sppChoices[i];
				
				if (pChoice->ppChoices)
				{
					int iDefaultNum = eaFindString(&pChoice->ppChoices, pChoice->pDefaultChoice);
					int iStartingNum = eaFindString(&pChoice->ppChoices, *pChoice->ppOutChoice);
					lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
						if (lResult != CB_ERR) 
						{
							if (iDefaultNum >= 0 && lResult != iDefaultNum)
							{
								SendMessage(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[iDefaultNum]);
							}
							else if (iStartingNum >= 0)
							{
								SendMessage(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[iStartingNum]);
							}
						}
					}
				}
				else if (pChoice->bIsBool)
				{
					bool bDefaultVal = (stricmp_safe(pChoice->pDefaultChoice, "1") == 0);
					
					CheckDlgButton(hDlg, iCheckIDs[i], bDefaultVal ? BST_CHECKED : BST_UNCHECKED);
					InvalidateRect(GetDlgItem(hDlg, iCheckIDs[i]), NULL, false);



				}
				else
				{
					GetWindowText(GetDlgItem(hDlg, iEditIDs[i]), SAFESTR(curText));
					
					if (stricmp_safe(curText, pChoice->pDefaultChoice) == 0)
					{
						SetTextFast(GetDlgItem(hDlg, iEditIDs[i]), *pChoice->ppOutChoice);
					}
					else
					{
						SetTextFast(GetDlgItem(hDlg, iEditIDs[i]), pChoice->pDefaultChoice);
					}
				}
				break;
			}
		}
		



		break;


	}

	return false;
}

bool MultiPicker(char *pName, MultiPickerChoice **ppChoices)
{
	assert(eaSize(&ppChoices) <= MAX_CHOICES_ONE_MULTIPICKER);
	sppChoices = ppChoices;
	estrCopy2(&gpPickerLabel, pName);

	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_MULTIPICKER), 0, (DLGPROC)multiPickerDlgProc);
	return gbPickerResult;


}



#include "ShardLauncherUI_h_Ast.c"
#endif
