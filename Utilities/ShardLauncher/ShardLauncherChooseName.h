#pragma once
#include "SimpleWindowManager.h"


BOOL chooseNameDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool chooseNameDlgProc_SWMTick(SimpleWindow *pWindow);