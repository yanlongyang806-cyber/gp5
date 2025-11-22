#include "wininclude.h"
#include "resource.h"
#include "genericDialog.h"
#include "file.h"
#include "utils.h"
#include "utilitiesLib.h"
#include "winutil.h"
#include "FolderCache.h"

char *filename=NULL;
char *text=NULL;
HWND hText;

void fileChangedCallback(const char *relpath, int when)
{
	int h, v;
	char *oldtext = text;
	bool different;
	text = fileAlloc(filename, NULL);
	different = (strcmp(text, oldtext)!=0);
	SAFE_FREE(oldtext);
	if (!different)
		return;
	// Get and restore scroll position
	v = GetScrollPos(hText, SB_VERT);
	h = GetScrollPos(hText, SB_HORZ);
	SetWindowText(hText, text);
	for (; v; v--)
		SendMessage(hText, EM_SCROLL, SB_LINEDOWN, 0);
}

LRESULT CALLBACK DlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
	{
		RECT rect;
		char *cmdline = GetCommandLine();
		char *s;
		if (cmdline[0] == '\"') {
			s = strchr(cmdline+1, '\"');
			assert(s);
			if (*s)
				s++;
			if (*s)
				s++;
			filename = s;
		} else {
			s = strchr(cmdline, ' ');
			if (s && *s)
				s++;
			filename = s;
		}
		if (!filename || !*filename) {
			text = "Usage: ReloadingFileViewer filename";
		} else {
			while (*filename == ' ')
				filename++;
			if (*filename=='\"') {
				filename++;
				s = strrchr(filename, '\"');
				assert(s);
				*s='\0';
			}
			SetWindowText(hDlg, filename);
			text = fileAlloc(filename, NULL);
			if (!text)
				text = "File not found!";
			else {
				FolderCache *fc = FolderCacheCreate();
				char dir[MAX_PATH];
				strcpy(dir, filename);
				getDirectoryName(dir);
				FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);
				FolderCacheAddFolder(fc, dir, 1);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, getFileName(filename), fileChangedCallback);
			}
		}
		hText = GetDlgItem(hDlg, IDC_EDIT);
		SetWindowText(hText, text);
		// flash the window if it is not the focus
		flashWindow(hDlg);


		GetClientRect(hDlg, &rect); 
		doDialogOnResize(hDlg, (WORD)(rect.right - rect.left), (WORD)(rect.bottom - rect.top), IDC_ALIGNME, IDC_UPPERLEFT);

		SetTimer(hDlg, 1, 100, NULL);

		break;
	}
	case WM_SIZE:
	{
		WORD w = LOWORD(lParam);
		WORD h = HIWORD(lParam);
		doDialogOnResize(hDlg, w, h, IDC_ALIGNME, IDC_UPPERLEFT);
		break;
	}
	case WM_COMMAND:
		if ( LOWORD(wParam) == IDCANCEL )
		{
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		if ( LOWORD(wParam) == IDOK )
		{
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		break;
	case WM_TIMER:
		{
			static bool bFirst=true;
			if (bFirst) {
				SendMessage(hText, EM_SETSEL, -1, 0);
				bFirst = false;
			}
			utilitiesLibOncePerFrame(0.1f, 1.f);
			FolderCacheDoCallbacks();
		}
		break;
	}

	return FALSE;
}

int __stdcall WinMain(__in HINSTANCE hInstance, __in_opt HINSTANCE hPrevInstance, __in_opt LPSTR lpCmdLine, __in int nShowCmd)
{
	utilitiesLibStartup();
	DialogBox(hInstance, (LPCTSTR) (IDD_DIALOG1), NULL, DlgProc);
	return 0;
}