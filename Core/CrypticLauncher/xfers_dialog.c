// CrypicLauncher options dialog functions

#include "xfers_dialog.h"
#include "resource_CrypticLauncher.h"
#include "patcher.h"
#include "LauncherMain.h"
#include "Shards.h"
#include "UIDefs.h"

// UtilitiesLib
#include "earray.h"
#include "SimpleWindowManager.h"
#include "timing.h"
#include "net.h"
#include "fileutil.h"
#include "windef.h"
#include "Windowsx.h"
#include "Commctrl.h"
#include "UTF8.h"

// PatchClientLib
#include "patchxfer.h"
#include "pcl_typedefs.h"
#include "pcl_client_struct.h"

char *g_colnames[] = {"Path", "State", "Blocks", "%", "Rq'd"};
int g_colwidths[] =  {400,         80,    80,      40,  60};
STATIC_ASSERT(ARRAY_SIZE_CHECKED(g_colnames) == ARRAY_SIZE_CHECKED(g_colwidths));

BOOL XfersPreDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	if (iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST)
	{
		if (wParam == 'x')
		{
			PostMessage(pWindow->hWnd, WM_COMMAND, IDCANCEL, 0);
			return TRUE;
		}
	}
	return FALSE;
}

BOOL XfersDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *window)
{
	SimpleWindow *main_window = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	
	if (!main_window)
	{
		// Main window has been closed
		window->bCloseRequested = true;
		return FALSE;
	}

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			HANDLE lv;
			LVCOLUMN lvc = {0};
			int i;

			// Early out guard
			if (!PatcherIsValid())
			{
				return TRUE;
			}

			lv = GetDlgItem(hDlg, IDC_XFERLIST);

			// Populate columns
			lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvc.fmt = LVCFMT_LEFT;
			for (i=0; i<ARRAY_SIZE_CHECKED(g_colnames); i++)
			{
				lvc.iSubItem = i;
				lvc.pszText = UTF8_To_UTF16_malloc(g_colnames[i]);
				lvc.cx = g_colwidths[i];
				ListView_InsertColumn(lv, i, &lvc);
				SAFE_FREE(lvc.pszText);
			}

			// Add list items
			ListView_SetItemCountEx(lv, 0 /* PatcherGetNumXfers() */, 0);

			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			ListView_SetItemCountEx(GetDlgItem(hDlg, IDC_XFERLIST), 0, 0);
			window->bCloseRequested = true;
			break;
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR) lParam)->code)
		{
			case LVN_GETDISPINFO:
			{
				NMLVDISPINFOA *plvdi = (NMLVDISPINFOA*)lParam;

				PatchXferData patchXferData;

				if (PatcherGetXferData(&patchXferData, plvdi->item.iItem))
				{
					switch (plvdi->item.iSubItem)
					{
						case 0:
							plvdi->item.pszText = patchXferData.path;
							break;
						case 1:
							plvdi->item.pszText = patchXferData.state;
							break;
						case 2:
							plvdi->item.pszText = patchXferData.blocks;
							break;
						case 3:
							plvdi->item.pszText = patchXferData.progress;
							break;
						case 4:
							plvdi->item.pszText = patchXferData.requested;
							break;
					}
				}
				else
				{
					plvdi->item.pszText = "";
				}
				break;
			}
			break;
		}
		break;
	}

	return FALSE;
}


bool XfersTickFunc(SimpleWindow *pWindow)
{
	char rootFolder[MAX_PATH];
	char netStats[MAX_PATH];
	char receivedStats[MAX_PATH];
	char actualStats[MAX_PATH];
	char linkStats[MAX_PATH];
	char ipAddress[64];
	SimpleWindow *main_window = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	int nxfers;
	static U32 last_update = -1;
	static U32 speeds_timer = -1;
	char buf[2048];

	if (!main_window)
	{
		// Main window has been closed
		return true;
	}

	if (last_update == -1)
		last_update = timerAlloc();

	// Don't run the rest more than 10 Hz
	if (timerElapsed(last_update) < 0.1)
		return true;
	timerStart(last_update);

	// This doesn't require a patchclient, so update it now
	if (gControllerTrackerLastIP)
	{
		sprintf(buf, "ct = %s", gControllerTrackerLastIP);
	}
	else
	{
		sprintf(buf, "ct = No link");
	}
	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_CONTROLLERTRACKER), buf);

	// Update the list view
	nxfers = PatcherGetXferDataCount();
	ListView_SetItemCountEx(GetDlgItem(pWindow->hWnd, IDC_XFERLIST), nxfers, 0);

	// Guard against weird client states
	if (!PatcherIsValid())
	{
		return true;
	}

	// Update the status line
	sprintf(buf, "%u/%u transfers", nxfers, MAX_XFERS);
	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_STATUS_XFERS), buf);

	PatcherGetStats(SAFESTR(rootFolder), SAFESTR(netStats), SAFESTR(receivedStats), SAFESTR(actualStats), SAFESTR(linkStats), SAFESTR(ipAddress));

	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_STATUS_ROOT), rootFolder);

	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_STATUS_NETBYTES), netStats);

	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_SPEED_RECV), receivedStats);

	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_SPEED_ACT), actualStats);

	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_SPEED_LINK), linkStats);

	Static_SetText_UTF8(GetDlgItem(pWindow->hWnd, IDC_SERVER), ipAddress);

	return true;
}
