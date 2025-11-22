#include "winInclude.h"
#include "estring.h"
#include "earray.h"
#include "resource.h"
#include "utils.h"
#include "winutil.h"
#include "InStringCommands.h"
#include "timing.h"
#include "alerts.h"
#include "stringcache.h"
#include "ContinuousBuilder.h"
#include "winutil.h"
#include "net/net.h"
#include "CBReportToCBMonitor.h"
#include "UtilitiesLib.h"
#include "utf8.h"

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
		
			SendMessage_SelectString_UTF8(GetDlgItem(hDlg, IDC_PICKER), gppPickerChoices[iIndex]);
		}
		else
		{
			SendMessage_SelectString_UTF8(GetDlgItem(hDlg, IDC_PICKER), gppPickerChoices[0]);
		}

		SetTimer(hDlg, 0, 1, NULL);

		giPickerStartTime = timeSecondsSince2000();
		gbTriggeredAlert = false;

		ShowWindow(hDlg, SW_SHOW);
		return true; 

	case WM_TIMER:
		commMonitor(commDefault());
		utilitiesLibOncePerFrame(REAL_TIME);
		CBReportToCBMonitor_Update();

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
		}
		break;


	}

	
	return false;
}



//args are bar-separated. First is name of dialog. Second is label. Remaining are choices
//instring command = {{PICKER ChooseBuildType|What type of build is this?|PROD|INCR|BASELINE}}
bool BuildScripting_PickerCB(char *pInString, char **ppOutString, void *pUserData)
{
	char **ppWords = NULL;
	int i;

	DivideString( pInString, "|",&ppWords,  DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	if (eaSize(&ppWords) < 3)
	{
		eaDestroyEx(&ppWords, NULL);
		estrPrintf(ppOutString, "Improper syntax. Should be NAME|Label|Choice1|...");
		return false;
	}

	estrCopy2(&gpPickerName, ppWords[0]);
	estrCopy2(&gpPickerLabel, ppWords[1]);
	eaDestroyEx(&gppPickerChoices, NULL);
	eaDestroyEx(&gppPickerDescriptions, NULL);

	for (i=2; i < eaSize(&ppWords); i++)
	{
		eaPush(&gppPickerChoices, strdup(ppWords[i]));
	}

	estrDestroy(&gpPickerOutput);


	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_PICKER), ghCBDlg, (DLGPROC)pickerDlgProc);



	estrCopy(ppOutString, &gpPickerOutput);
	return gbPickerResult;

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


	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_PICKER), ghCBDlg, (DLGPROC)pickerDlgProc);


	estrCopy(ppOutChoice, &gpPickerOutput);
	return gbPickerResult;
}


