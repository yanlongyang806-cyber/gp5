#include "winInclude.h"
#include "ShardLauncherPatchVersionPicker.h"
#include "ShardLauncher.h"
#include "../../libs/patchclientlib/pcl_client.h"
#include "resource.h"
#include "winutil.h"
#include "earray.h"
#include "net.h"
#include "estring.h"
#include "ShardLauncherUI.h"
#include "UTF8.h"

static char **sppVersionNames = NULL;
static char **sppVersionDescriptions = NULL;




static char *spVersionPickerError = NULL;

static void NameListCB(char ** names, int * branches, char ** sandboxes, U32 * revs, char ** comments, U32 * expires, int count, PCL_ErrorCode error, const char * error_details, void * userData)
{
	int i;
	U32 iCurTime = time(NULL);

	eaDestroyEx(&sppVersionNames, NULL);
	eaDestroyEx(&sppVersionDescriptions, NULL);

	for (i=eaSize(&names) - 1; i >= 0; i--)
	{
		if (expires[i] > iCurTime)
		{
			eaPush(&sppVersionNames, strdup(names[i]));
			eaPush(&sppVersionDescriptions, strdup(comments[i]));
		}
	}
}


bool versionPicker_Init(HWND hDlg, SimpleWindow *pWindow)
{

	PCL_Client *pPCLClient = NULL;
	PCL_ErrorCode eCode;

	static char *pPatchServer = NULL;
	static char *pProductName = NULL;
	static char *pPatchVersion = NULL;


	SimpleWindow *pMainWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);
	assert(pMainWindow);

	GetWindowText_UTF8(GetDlgItem(pMainWindow->hWnd, IDC_PATCHSERVER), &pPatchServer);
	GetWindowText_UTF8(GetDlgItem(pMainWindow->hWnd, IDC_PRODUCTPICKER), &pProductName);
	GetWindowText_UTF8(GetDlgItem(pMainWindow->hWnd, IDC_PATCHVERSION_LABEL), &pPatchVersion);


	LOG("About to try to connect to patchserver %s to get list of versions", pPatchServer);
	if ((eCode = pclConnectAndCreate(&pPCLClient, pPatchServer, DEFAULT_PATCHSERVER_PORT, 60, commDefault(), "c:\\tempShardLauncher", NULL, NULL, NULL, NULL)) != PCL_SUCCESS)
	{
		LOG_FAIL("PCL connection failed during pclConnectAndCreate()");
		estrPrintf(&spVersionPickerError, "Couldn't connect to patch server %s", pPatchServer);

		return false;
	}
	
	if ((eCode = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		LOG_FAIL("pclWait() failed after ConnectAndCreate");
		estrPrintf(&spVersionPickerError, "Couldn't connect to patch server %s (pclWait() failed after ConnectAndCreate)", pPatchServer);
		pclDisconnectAndDestroy(pPCLClient);
		return false;
	}

	if ((eCode = pclSetDefaultView(pPCLClient, STACK_SPRINTF("%sClient", pProductName), false, NULL, NULL)) != PCL_SUCCESS)
	{
		LOG_FAIL("pclSetDefaultView failed");
		estrPrintf(&spVersionPickerError, "Couldn't connect to patch server %s (pclSetDefaultView failed)", pPatchServer);
		pclDisconnectAndDestroy(pPCLClient);
		return false;
	}

	if ((eCode = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		LOG_FAIL("pclWait() failed after SetViewLatest");
		estrPrintf(&spVersionPickerError, "Couldn't connect to patch server %s (pclWait() failed after SetViewLatest)", pPatchServer);
		pclDisconnectAndDestroy(pPCLClient);
		return false;
	}

	if ((eCode = pclGetNameList(pPCLClient, NameListCB, NULL)) != PCL_SUCCESS)
	{
		LOG_FAIL("pclGetNameList() failed");
		estrPrintf(&spVersionPickerError, "Couldn't connect to patch server %s (pclGetNameList() failed)", pPatchServer);
		pclDisconnectAndDestroy(pPCLClient);
		return false;
	}
	if ((eCode = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		LOG_FAIL("pclWait() failed after pclGetNameList");
		estrPrintf(&spVersionPickerError, "Couldn't connect to patch server %s (pclWait() failed after pclGetNameList)", pPatchServer);
		pclDisconnectAndDestroy(pPCLClient);
		return false;
	}

	LOG("All PCL communication succeeded");
	pclDisconnectAndDestroy(pPCLClient);

	SetComboBoxFromEarrayWithDefault(hDlg, IDC_PICKPATCH_COMBOBOX, &sppVersionNames, NULL,
		pPatchVersion);


	return true;
}


bool patchVersionPickerDlgProc_SWMTick(SimpleWindow *pWindow)
{
	if (sppVersionDescriptions && eaSize(&sppVersionDescriptions))
	{
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_PICKPATCH_DESC), sppVersionDescriptions[GetComboBoxSelectedIndex(pWindow->hWnd, IDC_PICKPATCH_COMBOBOX)]);
	}
	
	return false;
}



BOOL patchVersionPickerDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		if (!versionPicker_Init(hDlg, pWindow))
		{
			SimpleWindow *pMainWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);
			if (pMainWindow)
			{
				SetTextFast(GetDlgItem(pMainWindow->hWnd, IDC_PATCHVERSION_LABEL), "ERROR");
				SetTextFast(GetDlgItem(pMainWindow->hWnd, IDC_PATCHVERSION_COMMENT), spVersionPickerError);
			}
			pWindow->bCloseRequested = true;
		}
		break;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			pWindow->bCloseRequested = true;
			break;
		case IDOK:
			{
				SimpleWindow *pMainWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);
				SimpleWindow *pParentWindow;
				if (pMainWindow)
				{
					int iSelected = GetComboBoxSelectedIndex(hDlg, IDC_PICKPATCH_COMBOBOX);
		
					SetTextFast(GetDlgItem(pMainWindow->hWnd, IDC_PATCHVERSION_LABEL), sppVersionNames[iSelected]);
					SetTextFast(GetDlgItem(pMainWindow->hWnd, IDC_PATCHVERSION_COMMENT), sppVersionDescriptions[iSelected]);
				}
				pWindow->bCloseRequested = true;

				pParentWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);

				if (pParentWindow)
				{
					InvalidateRect(GetDlgItem(pParentWindow->hWnd, IDC_PATCHVERSION_LABEL), NULL, false);
					InvalidateRect(GetDlgItem(pParentWindow->hWnd, IDC_PATCHVERSION_COMMENT), NULL, false);
				}
			}

		}
	}

	return false;
}



