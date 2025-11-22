#include "ShardLauncherOptions.h"
#include "textparser.h"
#include "earray.h"
#include "estring.h"
#include "ShardLauncherOptions_h_ast.h"
#include "ShardLauncher.h"
#include "resource.h"
#include "winutil.h"
#include "ShardLauncherUI.h"
#include "stringUtil.h"
#include "ShardLauncher_h_ast.h"
#include "ShardLauncherMainScreen.h"
#include "GlobalTypes.h"
#include "GlobalTypes_h_ast.h"
#include "ShardLauncher_pub_h_ast.h"
#include "utf8.h"

static HBRUSH sBad = 0;


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

U32 iClusterIDs[] = 
{
	IDC_CLUSTER1,
	IDC_CLUSTER2,
	IDC_CLUSTER3,
	IDC_CLUSTER4,
	IDC_CLUSTER5,
	IDC_CLUSTER6,
	IDC_CLUSTER7,
	IDC_CLUSTER8,
	IDC_CLUSTER9,
	IDC_CLUSTER10,
	IDC_CLUSTER11,
	IDC_CLUSTER12,
	IDC_CLUSTER13,
	IDC_CLUSTER14,
	IDC_CLUSTER15,
	IDC_CLUSTER16,
	IDC_CLUSTER17,
	IDC_CLUSTER18,
	IDC_CLUSTER19,
	IDC_CLUSTER20,
	IDC_CLUSTER21,
	IDC_CLUSTER22,
	IDC_CLUSTER23,
	IDC_CLUSTER24,
	IDC_CLUSTER25,
	IDC_CLUSTER26,
};

void LaunchClusterSpecificWindow(ShardLauncherConfigOption *pOption, int iWindowIndex);

#define BAD_COLOR RGB(240,0,0)

static HBRUSH sYellow = 0;
#define YELLOW_COLOR RGB(240,240,0)

static HBRUSH sPurple = 0;
#define PURPLE_COLOR RGB(180,0,180)


//when the template is "?", meaning that a value MUST be set, then make sure the option is not
//a bool, and that "?" is added to the list of choices, if any
//
//Note that bools get turned into 3-option choices, so that there's a way to choose either true or false
//and make it clear a choice was made.
void FixupOptionBasedOnTemplateChoice(ShardLauncherConfigOption *pOption, ShardLauncherConfigOptionChoice *pTemplateChoice)
{
	if (!pTemplateChoice)
	{
		return;
	}

	if (stricmp(pTemplateChoice->pValue, "?") != 0)
	{
		return;
	}

	if (pOption->bIsBool)
	{
		pOption->bIsBool = false;
		eaPush(&pOption->ppChoices, strdup("?"));
		eaPush(&pOption->ppChoices, strdup("0"));
		eaPush(&pOption->ppChoices, strdup("1"));
	}
	else
	{
		if (pOption->ppChoices)
		{
			if (eaFindString(&pOption->ppChoices, "?") == -1)
			{
				eaPush(&pOption->ppChoices, strdup("?"));
			}
		}
	}
}

ShardLauncherConfigOptionList *FindOptionListFromWindowIndex(int iIndex)
{
	if (iIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER)
	{
		return gpRun->pOptionLibrary->ppLists[iIndex];
	}
	else if (iIndex < CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
	{
		return gpRun->pOptionLibrary->pServerTypeSpecificList;
	}
	else
	{
		int iRealIndex = iIndex - CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER;
		assert(eaSize(&gpRun->pOptionLibrary->ppClusterLevelLists) > iRealIndex);
		return gpRun->pOptionLibrary->ppClusterLevelLists[iRealIndex];
	}
}

char *GetOptionNameWithIndex(char *pName, int iIndex)
{
	if (iIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER || iIndex >= CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
	{
		return pName;
	}
	else
	{
		return GetServerTypeSpecificOptionName(pName, iIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER);
	}
}


ShardLauncherConfigOptionChoice *FindTemplateChoice(ShardLauncherRun *pRun, char *pOptionName, int iWindowIndex)
{
	if (iWindowIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER)
	{
		return eaIndexedGetUsingString(&pRun->ppTemplateChoices, pOptionName);
	}
	else
	{
		return eaIndexedGetUsingString(&pRun->ppTemplateChoices, GetServerTypeSpecificOptionName(pOptionName, iWindowIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER));
	}
}

ShardLauncherConfigOptionChoice *FindRunChoice(ShardLauncherRun *pRun, char *pOptionName, int iWindowIndex)
{
	if (iWindowIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER || iWindowIndex >= CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
	{
		return eaIndexedGetUsingString(&pRun->ppChoices, pOptionName);
	}
	else
	{
		return eaIndexedGetUsingString(&pRun->ppChoices, GetServerTypeSpecificOptionName(pOptionName, iWindowIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER));
	}
}

bool OptionHasClusterSpecificSettings(ShardLauncherRun *pRun, char *pOptionName, int iWindowIndex)
{
	int i;
	char *pTempName;
	char *pBaseName;

	if (iWindowIndex >= CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
	{
		return false;
	}
	
	if (iWindowIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER)
	{
		pBaseName = pOptionName;
	}
	else
	{
		pBaseName = GetServerTypeSpecificOptionName(pOptionName, iWindowIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER);
	}

	for (i = 0; i < SHARDTYPE_LAST; i++)
	{
		pTempName = GetShardOrShardTypeSpecificOptionName(pBaseName, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, i));

		if (eaIndexedGetUsingString(&pRun->ppChoices, pTempName))
		{
			return true;
		}
	}

	for (i = 0; i < eaSize(&pRun->ppClusterShards); i++)
	{
		pTempName = GetShardOrShardTypeSpecificOptionName(pBaseName, pRun->ppClusterShards[i]->pShardName);

		if (eaIndexedGetUsingString(&pRun->ppChoices, pTempName))
		{
			return true;
		}
	}

	return false;
}

//checks if it's a cluster-specific name... if so, returns the cluster-specific part, otherwise returns the
//option name
static char *GetOptionNameForDisplay(char *pInName)
{
	if (pInName[0] == '{')
	{
		static char *spRetVal = NULL;
		estrCopy2(&spRetVal, pInName + 1);
		estrTruncateAtFirstOccurrence(&spRetVal, '}');
		return spRetVal;
	}
	else
	{
		return pInName;
	}
}


bool options_Init(HWND hDlg, SimpleWindow *pWindow, bool bNoResize)
{
	MultiPickerChoiceList *pChoiceList = StructCreate(parse_MultiPickerChoiceList);
	ShardLauncherConfigOptionList *pOptionList = FindOptionListFromWindowIndex(pWindow->iUserIndex);
	int iOptionNum;
	int i;

	StructDestroySafeVoid(parse_MultiPickerChoiceList, &pWindow->pUserData);

	pWindow->pUserData = pChoiceList;


	for (iOptionNum = 0; iOptionNum < eaSize(&pOptionList->ppOptions); iOptionNum++)
	{
		ShardLauncherConfigOption *pOption = pOptionList->ppOptions[iOptionNum];
		MultiPickerChoice *pPicker = StructCreate(parse_MultiPickerChoice);

		ShardLauncherConfigOptionChoice *pTemplateChoice = FindTemplateChoice(gpRun, pOption->pName, pWindow->iUserIndex);
		ShardLauncherConfigOptionChoice *pCurChoice = FindRunChoice(gpRun, pOption->pName, pWindow->iUserIndex);

		FixupOptionBasedOnTemplateChoice(pOption, pTemplateChoice);

		if (gpRun->bClustered && pWindow->iUserIndex < CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
		{
			ShowWindow(GetDlgItem(hDlg, iClusterIDs[iOptionNum]), SW_SHOW);
		}
		else
		{
			ShowWindow(GetDlgItem(hDlg, iClusterIDs[iOptionNum]), SW_HIDE);
		}


		eaPush(&pChoiceList->ppMultiPickerChoices, pPicker);
		
		pPicker->pName = strdup(pOption->pName);
		pPicker->pDesc = strdup(pOption->pDescription);
		
		if (pTemplateChoice)
		{
			estrCopy2(&pPicker->pDefaultChoice, pTemplateChoice->pValue);
		}
		else 
		{
			estrCopy2(&pPicker->pDefaultChoice, pOption->pDefaultChoice ? pOption->pDefaultChoice : "");
		}

		if (pCurChoice)
		{
			estrCopy2(&pPicker->pStartingChoice, pCurChoice->pValue);
		}
		else if (pTemplateChoice)
		{
			estrCopy2(&pPicker->pStartingChoice, pTemplateChoice->pValue);
		}
	
		pPicker->bAlphaNumOnly = pOption->bAlphaNumOnly;
	
		if (pOption->ppChoices)
		{
			for (i=0; i < eaSize(&pOption->ppChoices); i++)
			{
				eaPush(&pPicker->ppChoices, strdup(pOption->ppChoices[i]));
			}
		}
		else
		{
			pPicker->bIsBool = pOption->bIsBool;
		}

		ShowWindow(GetDlgItem(hDlg, iChoiceNameIDs[iOptionNum]), SW_SHOW);
		ShowWindow(GetDlgItem(hDlg, iDescIDs[iOptionNum]), SW_SHOW);
		ShowWindow(GetDlgItem(hDlg, iRestoreIDs[iOptionNum]), SW_SHOW);

		SetTextFast(GetDlgItem(hDlg, iChoiceNameIDs[iOptionNum]), GetOptionNameForDisplay(pOption->pName));

		if (pWindow->iUserIndex >= SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER)
		{
			static char *pTempDesc = NULL;
			estrCopy2(&pTempDesc, pOption->pDescription);
			estrReplaceOccurrences(&pTempDesc, "$SERVERTYPE$", GlobalTypeToName(pWindow->iUserIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER));

			SetTextFast(GetDlgItem(hDlg, iDescIDs[iOptionNum]), pTempDesc);
		}
		else
		{
			SetTextFast(GetDlgItem(hDlg, iDescIDs[iOptionNum]), pOption->pDescription);
		}

		if (pPicker->ppChoices)
		{
			ShowWindow(GetDlgItem(hDlg, iEditIDs[iOptionNum]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iCheckIDs[iOptionNum]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iComboIDs[iOptionNum]), SW_SHOW);

			SetComboBoxFromEarrayWithDefault(hDlg, iComboIDs[iOptionNum], &pPicker->ppChoices, NULL, pPicker->pStartingChoice);
		}
		else if (pPicker->bIsBool)
		{
			ShowWindow(GetDlgItem(hDlg, iEditIDs[iOptionNum]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iCheckIDs[iOptionNum]), SW_SHOW);
			ShowWindow(GetDlgItem(hDlg, iComboIDs[iOptionNum]), SW_HIDE);

			if (stricmp_safe(pPicker->pStartingChoice, "1") == 0)
			{
				CheckDlgButton(hDlg, iCheckIDs[iOptionNum], 
					BST_CHECKED);
			}
			else
			{
				CheckDlgButton(hDlg, iCheckIDs[iOptionNum], 
					BST_UNCHECKED);
			}

		}
		else 
		{
			ShowWindow(GetDlgItem(hDlg, iEditIDs[iOptionNum]), SW_SHOW);
			ShowWindow(GetDlgItem(hDlg, iCheckIDs[iOptionNum]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, iComboIDs[iOptionNum]), SW_HIDE);
			SetTextFast(GetDlgItem(hDlg, iEditIDs[iOptionNum]), pPicker->pStartingChoice);
		}
	}

	if (!sBad)
	{
		sBad = CreateSolidBrush(BAD_COLOR);
		sYellow = CreateSolidBrush(YELLOW_COLOR);
		sPurple = CreateSolidBrush(PURPLE_COLOR);
	}

	SetTextFast(GetDlgItem(hDlg, IDC_TITLE), pOptionList->pListName);
	SetTextFast(hDlg, pOptionList->pListName);


	if (stricmp(pOptionList->pListName, "AUTO_SETTINGS") == 0)
	{
		char *pWarningString = GetAutoSettingsWarningString();

		if (pWarningString)
		{
			SetTextFast(GetDlgItem(hDlg, IDC_WARNINGTEXT), pWarningString);
			ShowWindow(GetDlgItem(hDlg, IDC_WARNINGTEXT), SW_SHOW);
		}
		else
		{
			ShowWindow(GetDlgItem(hDlg, IDC_WARNINGTEXT), SW_HIDE);
		}
	}
	else
	{
		ShowWindow(GetDlgItem(hDlg, IDC_WARNINGTEXT), SW_HIDE);
	}

	for (i = eaSize(&pOptionList->ppOptions); i < ARRAY_SIZE(iChoiceNameIDs); i++)
	{
		ShowWindow(GetDlgItem(hDlg, iChoiceNameIDs[i]), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iDescIDs[i]), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iEditIDs[i]), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iRestoreIDs[i]), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iComboIDs[i]), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iCheckIDs[i]), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, iClusterIDs[i]), SW_HIDE);
	}

	if (!bNoResize)
	{
		if (eaSize(&pOptionList->ppOptions) != ARRAY_SIZE(iChoiceNameIDs))
		{
			RECT topButtonRect;
			RECT bottomButtonRect;
			RECT windowRect;
		
			int iYDelta;


			GetWindowRect(GetDlgItem(hDlg, iRestoreIDs[ARRAY_SIZE(iChoiceNameIDs)-1]), &bottomButtonRect);
			GetWindowRect(GetDlgItem(hDlg, iRestoreIDs[eaSize(&pOptionList->ppOptions)]), &topButtonRect);
			GetWindowRect(hDlg, &windowRect);

			iYDelta = bottomButtonRect.bottom - topButtonRect.bottom;

			SetWindowPos(hDlg, HWND_NOTOPMOST, windowRect.left, windowRect.top, windowRect.right - windowRect.left, 
				windowRect.bottom - windowRect.top - iYDelta, 0);
		}
	}

	return true;
}

void GetMultiPickerString(char *pStr, int iStrSize, MultiPickerChoice *pChoice, HWND hDlg, int iChoiceIndex)
{
	if (pChoice->ppChoices)
	{
		char *pChoiceName = pChoice->ppChoices[GetComboBoxSelectedIndex(hDlg, iComboIDs[iChoiceIndex])];

		strcpy_s(pStr, iStrSize, pChoiceName ? pChoiceName : "");
	}
	else if (pChoice->bIsBool)
	{
		if (IsDlgButtonChecked(hDlg, iCheckIDs[iChoiceIndex]))
		{
			pStr[0] = '1';
			pStr[1] = 0;
		}
		else
		{
			pStr[0] = 0;
		}
	}
	else
	{	
		char *pTemp = NULL;
		GetWindowText_UTF8(GetDlgItem(hDlg, iEditIDs[iChoiceIndex]), &pTemp);
		
		if (estrLength(&pTemp))
		{
			strcpy_s(pStr, iStrSize, pTemp);
		}
		else
		{
			pStr[0] = 0;
		}
		estrDestroy(&pTemp);
	}
}


BOOL shardLauncherOptionsDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	MultiPickerChoiceList *pChoiceList = (MultiPickerChoiceList*)pWindow->pUserData;
	int i;



	switch (iMsg)
	{
	case WM_INITDIALOG:
		if (!gpRun->pOptionLibrary)
		{
			pWindow->bCloseRequested = true;
			break;
		}

		if (!options_Init(hDlg, pWindow, false))
		{
			assert(0);
		}
		break;


	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORSTATIC:
		if (!gpRun->pOptionLibrary)
		{
			pWindow->bCloseRequested = true;
			break;
		}

		if (!pChoiceList)
		{
			return false;
		}

		if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_WARNINGTEXT))
		{
			HDC hdc = (HDC)wParam;
			SetBkColor(hdc, BAD_COLOR);
			return (BOOL)((intptr_t)sBad);
		}

		for (i=0; i < eaSize(&pChoiceList->ppMultiPickerChoices); i++)
		{
			MultiPickerChoice *pChoice = pChoiceList->ppMultiPickerChoices[i];

			if ((HANDLE)lParam == GetDlgItem(hDlg, iComboIDs[i]) || (HANDLE)lParam == GetDlgItem(hDlg, iEditIDs[i]) || (HANDLE)lParam == GetDlgItem(hDlg, iCheckIDs[i]) )
			{
				char curText[1024];

				ShardLauncherConfigOptionChoice *pTemplateChoice = FindTemplateChoice(gpRun, pChoice->pName, pWindow->iUserIndex);
				ShardLauncherConfigOptionChoice *pRunChoice = FindRunChoice(gpRun, pChoice->pName, pWindow->iUserIndex);


				GetMultiPickerString(SAFESTR(curText), pChoice, hDlg, i);
				
				if (pWindow->iUserIndex < CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER && OptionHasClusterSpecificSettings(gpRun, pChoice->pName, pWindow->iUserIndex))
				{
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, PURPLE_COLOR);
					return (BOOL)((intptr_t)sPurple);	
				}

				if (pTemplateChoice && stricmp(pTemplateChoice->pValue, "?") == 0 && stricmp(curText, "?") == 0)
				{
					HDC hdc = (HDC)wParam;
					SetBkColor(hdc, YELLOW_COLOR);
					return (BOOL)((intptr_t)sYellow);	
				}

				if (stricmp_safe(pChoice->pDefaultChoice, curText) == 0)
				{
					return false;
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


	case WM_COMMAND:
		if (!gpRun->pOptionLibrary)
		{
			pWindow->bCloseRequested = true;
			break;
		}

		if (!pChoiceList)
		{
			return false;
		}

		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			if (pWindow->pUserData)
			{
				StructDestroy(parse_MultiPickerChoiceList, pWindow->pUserData);
				pWindow->pUserData = NULL;
				pWindow->bCloseRequested = true;
				return false;
			}
			break;

		case IDOK:
			{
				SimpleWindow *pParentWindow;
				ShardLauncherConfigOptionList *pOptionList = FindOptionListFromWindowIndex(pWindow->iUserIndex);
				for (i=0; i < eaSize(&pChoiceList->ppMultiPickerChoices); i++)
				{
					MultiPickerChoice *pChoice = pChoiceList->ppMultiPickerChoices[i];
					ShardLauncherConfigOption *pOption = pOptionList->ppOptions[i];
					char curText[1024];

					ShardLauncherConfigOptionChoice *pTemplateChoice = FindTemplateChoice(gpRun, pOption->pName, pWindow->iUserIndex);
					ShardLauncherConfigOptionChoice *pCurChoice = FindRunChoice(gpRun, pOption->pName, pWindow->iUserIndex);

					GetMultiPickerString(SAFESTR(curText), pChoice, hDlg, i);

					//if the chosen value equals the template value, or there is no template choice
					//and then chosen value is empty, then there should be no override
					if (pTemplateChoice && stricmp_safe(curText, pTemplateChoice->pValue) == 0
						|| !pTemplateChoice && stricmp_safe(curText, NULL) == 0)
					{
						if (pCurChoice)
						{
							int iIndex;

							if (pWindow->iUserIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER || pWindow->iUserIndex >= CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
							{
								iIndex = eaIndexedFindUsingString(&gpRun->ppChoices, pOption->pName);
							}
							else
							{
								iIndex = eaIndexedFindUsingString(&gpRun->ppChoices, GetServerTypeSpecificOptionName(pOption->pName, pWindow->iUserIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER));
							}
							assert(iIndex != -1);
							StructDestroy(parse_ShardLauncherConfigOptionChoice, pCurChoice);
							eaRemove(&gpRun->ppChoices, iIndex);
						}
					}
					else
					{
						//there SHOULD be an override
						if (!pCurChoice)
						{
							ShardLauncherConfigOptionChoice *pNewChoice = StructCreate(parse_ShardLauncherConfigOptionChoice);
							pNewChoice->pConfigOptionName = strdup(GetOptionNameWithIndex(pOption->pName, pWindow->iUserIndex));
							pNewChoice->pValue = curText[0] ? strdup(curText) : NULL;

							eaPush(&gpRun->ppChoices, pNewChoice);
						}
						else
						{
							SAFE_FREE(pCurChoice->pValue);
							pCurChoice->pValue = curText[0] ? strdup(curText) : NULL;
						}
					}
				}
				StructDestroy(parse_MultiPickerChoiceList, pWindow->pUserData);
				pWindow->pUserData = NULL;
				pWindow->bCloseRequested = true;

				if (pWindow->iUserIndex >= CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER)
				{
					SimpleWindow **ppOptionWindows = NULL;
					SimpleWindow *pRootWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);
					
					//when we close a cluster level options screen, just to be careful, re-init all
					//other open option windows, and the entire parent window
					SimpleWindowManager_FindAllWindowsByType(WINDOWTYPE_OPTIONS, &ppOptionWindows);
					FOR_EACH_IN_EARRAY(ppOptionWindows, SimpleWindow, pOtherWindow)
					{
						if (pOtherWindow != pWindow)
						{
							options_Init(pOtherWindow->hWnd, pOtherWindow, true);
							InvalidateRect(pOtherWindow->hWnd, NULL, false);
						}
					}
					FOR_EACH_END;

					eaDestroy(&ppOptionWindows);

					if (pRootWindow)
					{
						InvalidateRect(pRootWindow->hWnd, NULL, false);
					}
				}
				else
				{
					pParentWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);

					if (pParentWindow)
					{
						if (pWindow->iUserIndex < SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER)
						{
							InvalidateRect(GetDlgItem(pParentWindow->hWnd, gOptionButtonIDs[pWindow->iUserIndex]), NULL, false);
						}
						else
						{
							int iTempIndex = ea32Find((int**)&gpRun->pOptionLibrary->pServerSpecificScreenTypes, pWindow->iUserIndex - SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER);
							if (iTempIndex != -1)
							{
								InvalidateRect(GetDlgItem(pParentWindow->hWnd, gServerScreenButtonIDs[iTempIndex]), NULL, false);
							}
						}
					}
				}


				return false;
			}
		}
		

		for (i=0; i < eaSize(&pChoiceList->ppMultiPickerChoices); i++)
		{
			if (LOWORD(wParam) == iEditIDs[i])
			{
				if (pChoiceList->ppMultiPickerChoices[i]->bAlphaNumOnly)
				{
					char *pTempStr = NULL;
					U32 iSel = SendMessage(GetDlgItem(hDlg, iEditIDs[i]), EM_GETSEL, 0, 0);

					GetWindowText_UTF8(GetDlgItem(hDlg, iEditIDs[i]), &pTempStr);

					estrMakeAllAlphaNumAndUnderscoresEx(&pTempStr, "$");

					SetTextFast(GetDlgItem(hDlg, iEditIDs[i]), pTempStr);
					estrDestroy(&pTempStr);

					SendMessage(GetDlgItem(hDlg, iEditIDs[i]), EM_SETSEL, LOWORD(iSel), LOWORD(iSel));
				}

			}
		}


		for (i=0; i < eaSize(&pChoiceList->ppMultiPickerChoices); i++)
		{
			if (LOWORD(wParam) == iCheckIDs[i])
			{
				InvalidateRect(GetDlgItem(hDlg, iCheckIDs[i]), NULL, false);
			}
		}

		for (i=0; i < eaSize(&pChoiceList->ppMultiPickerChoices); i++)
		{
			if (LOWORD(wParam) == iClusterIDs[i])
			{
				ShardLauncherConfigOptionList *pOptionList = FindOptionListFromWindowIndex(pWindow->iUserIndex);
				ShardLauncherConfigOption *pOption = pOptionList->ppOptions[i];
				LaunchClusterSpecificWindow(pOption, pWindow->iUserIndex);
			}
		}

		for (i=0; i < eaSize(&pChoiceList->ppMultiPickerChoices); i++)
		{
			if (iRestoreIDs[i] == LOWORD (wParam))
			{

				MultiPickerChoice *pChoice = pChoiceList->ppMultiPickerChoices[i];
				
				if (pChoice->ppChoices)
				{
					int iDefaultNum = eaFindString(&pChoice->ppChoices, pChoice->pDefaultChoice);
					int iStartingNum = eaFindString(&pChoice->ppChoices, pChoice->pStartingChoice);
					int iCurNum = GetComboBoxSelectedIndex(hDlg, iComboIDs[i]);
					LRESULT lResult = SendMessage(GetDlgItem(hDlg, iComboIDs[i]), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				
					if (iDefaultNum >= 0 && iCurNum != iDefaultNum)
					{
						SendMessage_SelectString_UTF8(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[iDefaultNum]);
					}
					else if (iStartingNum >= 0)
					{
						SendMessage_SelectString_UTF8(GetDlgItem(hDlg, iComboIDs[i]), pChoice->ppChoices[iStartingNum]);
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
					char *pCurText = NULL;
					GetWindowText_UTF8(GetDlgItem(hDlg, iEditIDs[i]), &pCurText);
					
					if (stricmp_safe(pCurText, pChoice->pDefaultChoice) == 0)
					{
						SetTextFast(GetDlgItem(hDlg, iEditIDs[i]), pChoice->pStartingChoice);
					}
					else
					{
						SetTextFast(GetDlgItem(hDlg, iEditIDs[i]), pChoice->pDefaultChoice);
					}

					estrDestroy(&pCurText);
				}
				break;
			}
		}
	
	}



	return false;
	
}

int FindIndexOfClusterLevelOptionList(const char *pOptionName)
{
	int i;
	char temp[1024];
	sprintf(temp, "%s%s", CLUSTER_SPECIFIC_OPTIONS_PREFIX, pOptionName);
	assert(gpRun->pOptionLibrary);

	for (i = 0; i < eaSize(&gpRun->pOptionLibrary->ppClusterLevelLists); i++)
	{
		if (stricmp(gpRun->pOptionLibrary->ppClusterLevelLists[i]->pListName, temp) == 0)
		{
			return i;
		}
	}

	return -1;
}

void LaunchClusterSpecificWindow(ShardLauncherConfigOption *pOption, int iWindowIndex)
{
	ShardLauncherConfigOptionList *pNewList;
	char *pOptionName = GetOptionNameWithIndex(pOption->pName, iWindowIndex);
	int iIndex = FindIndexOfClusterLevelOptionList(pOptionName);
	ShardLauncherConfigOption *pNewOption;
	int i;

	if (iIndex != -1)
	{
		SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_OPTIONS, iIndex + CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER, 
			IDD_MULTIPICKER,
			false, shardLauncherOptionsDlgProc_SWM, NULL, NULL);
		return;
	}

	pNewList = StructCreate(parse_ShardLauncherConfigOptionList);
	pNewList->pListName = strdupf("%s%s", CLUSTER_SPECIFIC_OPTIONS_PREFIX, pOptionName);

	//create master verison
	pNewOption = StructClone(parse_ShardLauncherConfigOption, pOption);
	SAFE_FREE(pNewOption->pName);
	pNewOption->pName = strdup(pOptionName);
	eaPush(&pNewList->ppOptions, pNewOption);

	//shard-type-specific versions
	for (i = SHARDTYPE_UNDEFINED + 1; i < SHARDTYPE_LAST; i++)
	{
		pNewOption = StructClone(parse_ShardLauncherConfigOption, pOption);
		SAFE_FREE(pNewOption->pName);
		pNewOption->pName = strdup(GetShardOrShardTypeSpecificOptionName(pOptionName, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, i)));
		SAFE_FREE(pNewOption->pDescription);
		pNewOption->pDescription = strdupf("For shards of type %s", StaticDefineInt_FastIntToString(ClusterShardTypeEnum, i));
		eaDestroyStruct(&pNewOption->ppThingsToDoIfSet, parse_ShardLauncherConfigOptionThingToDo);
		eaDestroyStruct(&pNewOption->ppThingsToDoIfNotSet, parse_ShardLauncherConfigOptionThingToDo);

		eaPush(&pNewList->ppOptions, pNewOption);
	}

	//shard-specific versions
	for (i = 0; i < eaSize(&gpRun->ppClusterShards); i++)
	{
		pNewOption = StructClone(parse_ShardLauncherConfigOption, pOption);
		SAFE_FREE(pNewOption->pName);
		pNewOption->pName = strdup(GetShardOrShardTypeSpecificOptionName(pOptionName, gpRun->ppClusterShards[i]->pShardName));
		SAFE_FREE(pNewOption->pDescription);
		pNewOption->pDescription = strdupf("For shards %s", gpRun->ppClusterShards[i]->pShardName);
		eaDestroyStruct(&pNewOption->ppThingsToDoIfSet, parse_ShardLauncherConfigOptionThingToDo);
		eaDestroyStruct(&pNewOption->ppThingsToDoIfNotSet, parse_ShardLauncherConfigOptionThingToDo);

		eaPush(&pNewList->ppOptions, pNewOption);
	}

	iIndex = eaPush(&gpRun->pOptionLibrary->ppClusterLevelLists, pNewList);
	SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_OPTIONS, iIndex + CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER, 
		IDD_MULTIPICKER,
		false, shardLauncherOptionsDlgProc_SWM, NULL, NULL);

}


	



#include "ShardLauncherOptions_h_ast.c"
