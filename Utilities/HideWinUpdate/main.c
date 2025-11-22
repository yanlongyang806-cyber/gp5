#include "stdafx.h"

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	char title[1024];
	GetWindowText(hwnd, title, 1024);
	if(stricmp(title, "Windows Update")==0)
	{
		ShowWindow(hwnd, SW_HIDE);
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