#pragma once

#include "TimedCallback.h"

BOOL mainScreenDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool mainScreenDlgProc_SWMTick(SimpleWindow *pWindow);

typedef struct PCLStatusMonitoringUpdate PCLStatusMonitoringUpdate;
void MainScreen_GetPatchingUpdate(PCLStatusMonitoringUpdate *pUpdate);
void MainScreen_PatchingPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
