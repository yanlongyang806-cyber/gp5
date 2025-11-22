#include "askOptions.h"

#ifndef _XBOX
#include "wininclude.h"
#include "AskOptionsResource.h"
#include "sysutil.h"
#include "RegistryReader.h"
#include "winutil.h"

static const char *g_appname;
static OptionList *g_option_list;
static int g_num_options;
static F32 g_timeout;
static int g_timer_enabled;
static int g_ret;
static RegReader rr;

static void initRR() {
	static char old_appname[MAX_PATH];
	if (!rr || 0!=stricmp(old_appname, g_appname)) {
		char key[512];
		if (rr) {
			destroyRegReader(rr);
		}
		rr = createRegReader();
		sprintf_s(SAFESTR(key), "HKEY_CURRENT_USER\\SOFTWARE\\Cryptic\\%s\\AskOptions", g_appname);
		strcpy(old_appname, g_appname);
		initRegReader(rr, key);
	}
}

static int regGetInt(const char *key, int deflt) {
	int value;
	initRR();
	value = deflt;
	if (0==rrReadInt(rr, key, &value))
		value = deflt;
	return value;
}

static void regPutInt(const char *key, int value) {
	initRR();
	rrWriteInt(rr, key, value);
}


static void updateEnabledState(HWND hDlg)
{
	int i;
	bool enabled=true;
	for (i=0; i<g_num_options; i++) {
		int id = IDC_CHECK1 + i;
		if (g_option_list[i].flags & OLF_CHILD) {
			EnableWindow(GetDlgItem(hDlg, id), enabled);
		} else {
			enabled = (bool)SendMessage(GetDlgItem(hDlg, id), BM_GETCHECK, 0, 0);
		}
	}
}

static HFONT getFont(void)
{
	static HFONT ret=NULL;
	if (!ret) {
		ret = GetStockObject(ANSI_VAR_FONT);
	}
	return ret;
}

void CreateCheckBox(HWND hDlg, const char *text, int x, int y, int width, int height, int id)
{
	HWND hwndTemp;
	hwndTemp = CreateWindow( 
		"BUTTON",   // predefined class 
		text,       // button text 
		WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX ,  // styles 
		x, y, width, height,
		hDlg,       // parent window 
		(HMENU) (intptr_t)id,
		(HINSTANCE) (intptr_t)GetWindowLong(hDlg, GWL_HINSTANCE), 
		NULL);      // pointer not needed 
	SendMessage(hwndTemp, WM_SETFONT, (WPARAM)getFont(), (LPARAM)TRUE);
}


static BOOL CALLBACK askOptionsDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	switch (iMsg) {
	case WM_INITDIALOG:
	{
		RECT rect;
		int x, y;
		int width=160;
		int height=15;
		GetClientRect(hDlg, &rect);
		width = rect.right / 2 - 15;
		x = 7;
		y = 7;
		for (i=0; i<g_num_options; i++) {
			int id = IDC_CHECK1 + i;
			*g_option_list[i].value = regGetInt(g_option_list[i].regKey, *g_option_list[i].value);
			CreateCheckBox(hDlg, g_option_list[i].name,
				x + ((g_option_list[i].flags & OLF_CHILD)?15:0), y, 
				width, height, id);
			y+=height;
			if (y >= rect.bottom - height - 30) {
				y = 7;
				x += rect.right / 2;
			}
			SendMessage(GetDlgItem(hDlg, id), BM_SETCHECK, *g_option_list[i].value, 0);
		}
		updateEnabledState(hDlg);
		SetWindowText(hDlg, g_appname);
		g_timer_enabled = true;
		SetTimer(hDlg, 0, 100, NULL);
		break;
	}
	case WM_NOTIFY:
		break;
	case WM_TIMER:
		g_timeout-=0.1;
		if (g_timer_enabled) {
			char buf[128];
			if (g_timeout <= 0) {
				PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
			} else {
				sprintf_s(SAFESTR(buf), "OK (auto-accept in %1.1fs)", g_timeout);
				SetDlgItemText(hDlg, IDOK, buf);
			}
		} else {
			char buf[512];
			GetDlgItemText(hDlg, IDOK, buf, ARRAY_SIZE(buf)-1);
			if (stricmp(buf, "OK")!=0)
				SetDlgItemText(hDlg, IDOK, "OK");
		}
		break;

	case WM_COMMAND:
		g_timer_enabled = false;

		switch (LOWORD (wParam))
		{
		case IDOK:
			// Syncronize Data and exit
			for (i=0; i<g_num_options; i++) {
				*g_option_list[i].value = (int)SendMessage(GetDlgItem(hDlg, IDC_CHECK1+i), BM_GETCHECK, 0, 0);
				regPutInt(g_option_list[i].regKey, *g_option_list[i].value);
			}
			g_ret = 1;
			EndDialog(hDlg, 0);
			break;
		case IDCANCEL:
			// Ignore data and exit
			g_ret = 0;
			EndDialog(hDlg, 0);
			break;
		}
		updateEnabledState(hDlg);
		break;
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
		g_timer_enabled = false;
		break;
	}
	return FALSE;
}

// "flags" is for future use (hide cancel button, etc)
int doAskOptions(const char *appName, OptionList *options, int numOptions, float timeout, int flags, const char *showOnceHelpText)
{
	int res;
	g_appname = appName;
	g_option_list = options;
	g_num_options = numOptions;
	g_timeout = timeout;
	if (showOnceHelpText && regGetInt("ShowHelpText", 1)) {
		regPutInt("ShowHelpText", 0);
		MessageBox(compatibleGetConsoleWindow(), showOnceHelpText, g_appname, MB_OK);
	}
	res = DialogBox (NULL, MAKEINTRESOURCE (IDD_ASK_OPTIONS), compatibleGetConsoleWindow(), (DLGPROC)askOptionsDlgProc);
	return g_ret;
}

#else // _XBOX

int doAskOptions(const char *appName, OptionList *options, int numOptions, float timeout, int flags, const char *showOnceHelpText)
{
	return 1;
}

#endif
