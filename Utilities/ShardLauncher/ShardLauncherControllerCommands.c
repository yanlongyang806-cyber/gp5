#include "ShardLauncher.h"
#include "ShardLauncherControllerCommands.h"
#include "textparser.h"
#include "earray.h"
#include "estring.h"
#include "resource.h"
#include "winutil.h"
#include "ShardLauncherUI.h"
#include "stringUtil.h"
#include "utf8.h"

BOOL controllerCommandsDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	SimpleWindow *pParentWindow;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_CONTROLLERCOMMANDS), gpRun->pControllerCommandsArbitraryText);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			pWindow->bCloseRequested = true;
			return false;
		case IDOK:
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_CONTROLLERCOMMANDS), &gpRun->pControllerCommandsArbitraryText);
			pWindow->bCloseRequested = true;



			pParentWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);

			if (pParentWindow)
			{
				InvalidateRect(GetDlgItem(pParentWindow->hWnd, IDC_CONTROLLERCOMMANDSTXT), NULL, false);
			}

			return false;
		}
		break;
	}





	return false;
}