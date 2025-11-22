#include "stdafx.h"

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	char title[1024];
	GetWindowText(hwnd, title, 1024);
	strlwr(title, 1024);
	if(strstr(title, "star trek online") || strstr(title, "champions online") || strstr(title, "cryptic launcher"))
	{
		ShowWindow(hwnd, SW_NORMAL);
		SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);
		SetForegroundWindow(hwnd);
	}
	return TRUE;
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
					   HINSTANCE hPrevInstance,
					   LPTSTR    lpCmdLine,
					   int       nCmdShow)
{
	EnumWindows(EnumWindowsProc, (LPARAM)NULL);
	return 0;
}