#ifndef _WIN32_IE
#define _WIN32_IE 0x0500 // WinNT 4?  VS2010 gets this implicitly from _WIN32_WINNT and will use 0x0600 (WinXP).
#endif

#include "wininclude.h"
#include "resource.h"
#include "harvest.h"
#include "systemtray.h"

#define NOTIFY_ICON_ID (200)

extern int gDeferredMode;

static HWND shParent = INVALID_HANDLE_VALUE;
static NOTIFYICONDATAW sNotifyIconData;

void systemTrayInit(HWND hParent)
{
	HINSTANCE hInstance = GetModuleHandle(NULL);
	memset(&sNotifyIconData, 0, sizeof(NOTIFYICONDATAW));
	shParent = hParent;

    sNotifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
    sNotifyIconData.hWnd   = hParent;
    sNotifyIconData.uID    = NOTIFY_ICON_ID;
    sNotifyIconData.hIcon  = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME));
    sNotifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    sNotifyIconData.uCallbackMessage = WM_SYSTEMTRAY_NOTIFY;
    sNotifyIconData.uTimeout = 10 * 1000; // It sounds like some Windows versions just ignore this anyway

	if(!gDeferredMode && !harvestCheckManualUserDump())
	{
		sNotifyIconData.uFlags |= NIF_INFO;
		sNotifyIconData.dwInfoFlags = NIIF_INFO;
		wcsncpy_s(SAFESTR(sNotifyIconData.szInfo), L"Gathering crash data, please wait...", 255);
		wcsncpy_s(SAFESTR(sNotifyIconData.szInfoTitle), L"Cryptic Error Handler", 255);
	}

	Shell_NotifyIconW(NIM_ADD, &sNotifyIconData);
	sNotifyIconData.szInfo[0] = 0;
}

void systemTrayShutdown()
{
	Shell_NotifyIconW(NIM_DELETE, &sNotifyIconData);
}
