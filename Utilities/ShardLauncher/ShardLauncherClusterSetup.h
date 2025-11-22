#pragma once
#include "SimpleWindowManager.h"

BOOL clusterSetupDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool clusterSetupDlgProc_SWMTick(SimpleWindow *pWindow);
