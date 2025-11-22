#pragma once

#include "WinInclude.h"
#include "ShardLauncher.h"
#include "SimpleWindowManager.h"

//ShardLauncherRun *DoStartingScreen(bool *pbNeedToConfigure);

BOOL startScreenDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool startScreenDlgProc_SWMTick(SimpleWindow *pWindow);
