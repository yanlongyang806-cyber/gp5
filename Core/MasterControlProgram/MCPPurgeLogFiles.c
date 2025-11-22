
#include "mastercontrolprogram.h"
#include "sTringutil.h"
#include "fileUtil2.h"
#include "sysutil.h"
#include "winutil.h"
#include "utils.h"
#include "UTF8.h"

BOOL purgeLogFilesMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
			
			SetTextFast(GetDlgItem(hDlg, IDC_DIRNAME), fileLogDir());
			SetTextFast(GetDlgItem(hDlg, IDC_DAYSOLD), "7");
			SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), "Ready to begin purging");
	
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			{
				int iDaysOld;
				static char *pDirString = NULL;
				static char *pDaysOldString = NULL;
				int iFilesPurged;


				GetWindowText_UTF8(GetDlgItem(hDlg, IDC_DIRNAME), &pDirString);
				GetWindowText_UTF8(GetDlgItem(hDlg, IDC_DAYSOLD), &pDaysOldString);

				if (!StringToInt(pDaysOldString, &iDaysOld) || iDaysOld < 1)
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), "Must specify a positive integer for \"days old\"");
					break;
				}

				if (!dirExists(pDirString))
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), STACK_SPRINTF("Directory %s does not exist", pDirString));
					break;
				}

				if (strlen(pDirString) < 4)
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), STACK_SPRINTF("I'm sorry dave, I'm afraid I can't do that"));
					break;
				}

				newConsoleWindow();
				showConsoleWindow();
				iFilesPurged = PurgeDirectoryOfOldFiles(pDirString, iDaysOld, NULL);
				SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), STACK_SPRINTF("Purged %d files", iFilesPurged));
			}
			break;

		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		}

	}
	
	return FALSE;
}
