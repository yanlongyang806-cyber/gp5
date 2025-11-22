#pragma once
#include "SimpleWindowManager.h"

BOOL patchVersionPickerDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool patchVersionPickerDlgProc_SWMTick(SimpleWindow *pWindow);
