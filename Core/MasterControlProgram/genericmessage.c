#include "mastercontrolprogram.h"
#include "UTF8.h"

char gGenericMessage[1024] = "";

	



BOOL genericMessageMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_GENERICMESSAGE), gGenericMessage);
			
		}
		break;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;
		break;


	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDC_OK)
		{
			pWindow->bCloseRequested = true;
		
			return FALSE;

		}
		break;

	}
	
	return FALSE;
}
