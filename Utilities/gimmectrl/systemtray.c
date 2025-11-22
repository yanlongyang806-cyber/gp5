#include "systemtray.h"
#include "gimmectrl.h"
#include "resource.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0500 // WinNT 4?  VS2010 gets this implicitly from _WIN32_WINNT and will use 0x0600 (WinXP).
#endif
#include "wininclude.h"
#include "UTF8.h"

static HWND hLastWnd = NULL;

void systemTrayAdd(HWND hParent)
{
	NOTIFYICONDATA nid = {0};

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd   = hParent;
	nid.uID    = 0;
	nid.hIcon  = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON));
	nid.uCallbackMessage = WM_APP_TRAYICON;
	nid.uFlags = NIF_ICON | NIF_MESSAGE;
	nid.uTimeout = 10 * 1000; // It sounds like some Windows versions just ignore this anyway

	hLastWnd = hParent;

	Shell_NotifyIcon(NIM_ADD, &nid);
}

void systemTrayRemove(HWND hParent)
{
	NOTIFYICONDATA nid = {0};

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd   = hParent;
	nid.uID    = 0;

	Shell_NotifyIcon(NIM_DELETE, &nid);
}

void systemTrayNotfy(HWND hParent, const char *title, const char *msg, U32 icon)
{
	NOTIFYICONDATA nid = {0};

	if(!hParent) hParent = hLastWnd;

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd   = hParent;
	nid.uID    = 0;

	UTF8_To_UTF16_Static(msg, nid.szInfo, ARRAY_SIZE(nid.szInfo));
	UTF8_To_UTF16_Static(title, nid.szInfoTitle, ARRAY_SIZE(nid.szInfoTitle));

	nid.dwInfoFlags = icon;
	nid.uFlags = NIF_INFO;
	nid.uTimeout = 10 * 1000; // It sounds like some Windows versions just ignore this anyway

	Shell_NotifyIcon(NIM_MODIFY, &nid);
}