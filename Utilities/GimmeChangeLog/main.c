#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "resource.h"
#include "utils.h"

#define START_UPDATE 0
#define MAX_UPDATES 50

char updates[MAX_UPDATES][10];

char start_date[64] = "";
char end_date[64] = "";
char branch[10] = "";
//char filespec[MAX_PATH] = "";
char directory[MAX_PATH] = "";
char filter[MAX_PATH] = "*";
SYSTEMTIME start_t, end_t;


static char *firstSlash(char *s) 
{
	char *c;
	for (c=s; *c; c++) {
		if (*c=='/' || *c=='\\') return c;
	}
	return NULL;
}



void ComboBoxAdd(HWND hDlg, int dlg_id, char *str)
{
	SendMessage(GetDlgItem(hDlg, dlg_id), CB_ADDSTRING, 0, (LPARAM)str);
}

void AddBranches(HWND hDlg, int dlg_id)
{
	int i;

	for ( i = START_UPDATE; i < START_UPDATE + MAX_UPDATES; ++i )
	{
		ComboBoxAdd(hDlg, dlg_id,_itoa(i, &updates[i-START_UPDATE][0], 10));
	}
}


char *monthNumToStr(int month)
{
	switch ( month )
	{
		case 1:
			return "Jan";
		case 2:
			return "Feb";
		case 3:
			return "Mar";
		case 4:
			return "Apr";
		case 5:
			return "May";
		case 6:
			return "Jun";
		case 7:
			return "Jul";
		case 8:
			return "Aug";
		case 9:
			return "Sep";
		case 10:
			return "Oct";
		case 11:
			return "Nov";
		case 12:
			return "Dec";
	}

	return "";
}

LRESULT CALLBACK DlgProc( HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			{
				AddBranches(hDlg, IDC_BRANCH);

				MonthCal_SetCurSel(GetDlgItem(hDlg, IDC_STARTDATE), &start_t);
				MonthCal_SetCurSel(GetDlgItem(hDlg, IDC_ENDDATE), &end_t);

				SetWindowText(GetDlgItem(hDlg, IDC_BRANCH), branch);
				SetWindowText(GetDlgItem(hDlg, IDC_DIRECTORY), directory);
				SetWindowText(GetDlgItem(hDlg, IDC_FILTER), filter);
			}
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDOK:
					{
						MonthCal_GetCurSel(GetDlgItem(hDlg, IDC_STARTDATE), &start_t);
						sprintf(start_date, "%s %d %d", monthNumToStr(start_t.wMonth), start_t.wDay, start_t.wYear);
						MonthCal_GetCurSel(GetDlgItem(hDlg, IDC_ENDDATE), &end_t);
						sprintf(end_date, "%s %d %d", monthNumToStr(end_t.wMonth), end_t.wDay, end_t.wYear);

						GetWindowText(GetDlgItem(hDlg, IDC_BRANCH), branch, 10);
						GetWindowText(GetDlgItem(hDlg, IDC_DIRECTORY), directory, MAX_PATH);
						GetWindowText(GetDlgItem(hDlg, IDC_FILTER), filter, MAX_PATH);
						EndDialog(hDlg, IDOK);
					}
					return TRUE;
				case IDCANCEL:
					EndDialog(hDlg, IDCANCEL);
					return TRUE;
			}
	}

	return FALSE;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	char cmdline[4096];

	while ( 1 )
	{
		if ( IDOK != DialogBox(NULL, (LPCTSTR)(IDD_DIALOG1), NULL, DlgProc) )
			return 0;
		
		if ( !strlen(start_date) || !strlen(end_date) || !strlen(branch) || !strlen(directory) || !strlen(filter) )
			MessageBox(NULL, "You must input all parameters", "Error", MB_OK|MB_ICONEXCLAMATION);
		else if ( firstSlash(filter) != NULL )
			MessageBox(NULL, "The filter does not support wildcard directories", "Error", MB_OK|MB_ICONEXCLAMATION);
		else break;
	}

	forwardSlashes(directory);

	sprintf(cmdline, "gimmeChangeLog.bat -startdate %s -enddate %s -branch %s -spec %s/%s", start_date, end_date, branch, directory, filter);
	//sprintf(cmdline, "gimmeChangeLog.bat -startdate Sep 1 2005 -enddate Nov 8 2005 -branch 10 -spec data/defs/NPC/*");
	//sprintf(cmdline, "gimmeChangeLog.bat -startdate Jan 1 2006 -enddate Apr 1 2006 -branch 12 -spec data/defs/powers/*.def");
	system(cmdline);

	return 0;
}