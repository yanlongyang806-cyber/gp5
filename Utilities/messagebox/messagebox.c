#include "wininclude.h"
#include "Windowsx.h"
#include "sysutil.h"
#include "cmdparse.h"
#include "SimpleWindowManager.h"
#include "gimmeDLLWrapper.h"
#include "file.h"
#include "ThreadManager.h"
#include "resource.h"

#define err(...) { fprintf(fileWrap(stderr), __VA_ARGS__); exit(-1); }

char *g_separator = "\n";

int iLifeSpan = 0;
AUTO_CMD_INT(iLifeSpan, LifeSpan) ACMD_CMDLINE;

AUTO_COMMAND ACMD_CMDLINE;
void separator(char *sep)
{
	g_separator = strdup(sep);
}

// The dialog title
char *g_title = "Message";
AUTO_COMMAND ACMD_CMDLINE;
void title(char *str)
{
	g_title = strdup(str);
}

// The main message text
char *g_message = "Text";
AUTO_COMMAND ACMD_CMDLINE;
void message(char *str)
{
	g_message = strdup(str);
}

// The initial value for input dialogs
char *g_value = "";
AUTO_COMMAND ACMD_CMDLINE;
void value(char *str)
{
	g_value = strdup(str);
}

// If true, the type is just a normal MessageBox() value. Otherwise it is a custom one.
bool g_simple = true;

// The dialog type
enum  
{
	MB_INPUT,
};
U32 g_type = MB_OK;
#define CHECK_STRING(var, val) if(stricmp(str, #val)==0) var = MB_##val
#define CHECK_STRING2(var, val) else if(stricmp(str, #val)==0) var = MB_##val
AUTO_COMMAND ACMD_CMDLINE;
void type(char *str)
{
	if(strStartsWith(str, "MB_"))
		str += 3;
	
	CHECK_STRING(g_type, OK);
	CHECK_STRING2(g_type, OKCANCEL);
	CHECK_STRING2(g_type, RETRYCANCEL);
	CHECK_STRING2(g_type, YESNO);
	CHECK_STRING2(g_type, YESNOCANCEL);
	else if(stricmp(str, "INPUT")==0) { g_type = MB_INPUT; g_simple = false; }
	else err("Unknown type %s", str);
}

// Style hints (only useful on simple dialogs)
U32 g_style = 0;
#undef CHECK_STRING
#undef CHECK_STRING2
#define CHECK_STRING(var, val) if(stricmp(str, #val)==0) var |= MB_##val
#define CHECK_STRING2(var, val) else if(stricmp(str, #val)==0) var |= MB_##val
AUTO_COMMAND ACMD_CMDLINE;
void style(char *str)
{
	if(strStartsWith(str, "MB_"))
		str += 3;

	CHECK_STRING(g_style, ICONEXCLAMATION);
	CHECK_STRING2(g_style, ICONWARNING);
	CHECK_STRING2(g_style, ICONINFORMATION);
	CHECK_STRING2(g_style, ICONASTERISK);
	CHECK_STRING2(g_style, ICONQUESTION);
	CHECK_STRING2(g_style, ICONSTOP);
	CHECK_STRING2(g_style, ICONERROR);
	CHECK_STRING2(g_style, ICONHAND);
	CHECK_STRING2(g_style, DEFBUTTON1);
	CHECK_STRING2(g_style, DEFBUTTON2);
	CHECK_STRING2(g_style, DEFBUTTON3);
	CHECK_STRING2(g_style, DEFBUTTON4);
	CHECK_STRING2(g_style, APPLMODAL);
	CHECK_STRING2(g_style, SYSTEMMODAL);
	CHECK_STRING2(g_style, TASKMODAL);
	CHECK_STRING2(g_style, DEFAULT_DESKTOP_ONLY);
	CHECK_STRING2(g_style, RIGHT);
	CHECK_STRING2(g_style, RTLREADING);
	CHECK_STRING2(g_style, SETFOREGROUND);
	CHECK_STRING2(g_style, TOPMOST);
	CHECK_STRING2(g_style, SERVICE_NOTIFICATION);
	CHECK_STRING2(g_style, SERVICE_NOTIFICATION_NT3X);
	else err("Unknown style %s", str);
}

#undef CHECK_STRING
#undef CHECK_STRING2
#define CHECK_STRING(var, val) if(stricmp(str, #val)==0) var |= MB_ICON##val
#define CHECK_STRING2(var, val) else if(stricmp(str, #val)==0) var |= MB_ICON##val
AUTO_COMMAND ACMD_CMDLINE;
void icon(char *str)
{
	if(strStartsWith(str, "MB_"))
		str += 3;
	if(strStartsWith(str, "ICON"))
		str += 4;

	CHECK_STRING(g_style, EXCLAMATION);
	CHECK_STRING2(g_style, WARNING);
	CHECK_STRING2(g_style, INFORMATION);
	CHECK_STRING2(g_style, ASTERISK);
	CHECK_STRING2(g_style, QUESTION);
	CHECK_STRING2(g_style, STOP);
	CHECK_STRING2(g_style, ERROR);
	CHECK_STRING2(g_style, HAND);
	else err("Unknown icon %s", str);
}

char g_inputbuf[1024] = "";
int g_return = 0;

BOOL InputDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch(iMsg)
	{
	case WM_INITDIALOG:
		SetWindowText(hDlg, g_title);
		Static_SetText(GetDlgItem(hDlg, IDC_STATIC1), g_message);
		Edit_SetText(GetDlgItem(hDlg, IDC_EDIT1), g_value);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		xcase IDOK:
			Edit_GetText(GetDlgItem(hDlg, IDC_EDIT1), g_inputbuf, ARRAY_SIZE_CHECKED(g_inputbuf));
			g_return = 0;
			pWindow->bCloseRequested = true;
		xcase IDCANCEL:
			g_return = 1;
			pWindow->bCloseRequested = true;
		}
		return TRUE;
	}

	return FALSE;
}

extern bool gbPrintCommandLine;

DWORD WINAPI lifeSpanThread(LPVOID lpParam)
{
	Sleep(iLifeSpan * 1000);
	exit(0);
}

int main(int argc, char **argv)
{
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	
	gbPrintCommandLine = false;
	gimmeDLLDisable(true);
	fileAllPathsAbsolute(true);
	cmdParseCommandLine(argc, argv);

	if (iLifeSpan)
	{
		assert(tmCreateThread(lifeSpanThread, NULL));
	}

	if(g_simple)
	{
		int ret = MessageBox(NULL, g_message, g_title, g_type|g_style);
		return ret;
	}
	else switch(g_type)
	{
		case MB_INPUT:
			{
				SimpleWindowManager_Init("messagebox", true);
				SimpleWindowManager_AddOrActivateWindow(0, 0, IDD_INPUTOKCANCEL, true, InputDialogProc, NULL, NULL);
				SimpleWindowManager_Run(NULL, NULL);
				printf("%s", g_inputbuf);
				return g_return;
			}
			break;
		default:
			return -1;
	}
	
	EXCEPTION_HANDLER_END

	return 0;
}