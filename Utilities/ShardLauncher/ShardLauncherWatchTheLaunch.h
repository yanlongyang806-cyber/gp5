#pragma once
#include "SimpleWindowManager.h"

BOOL watchTheLaunchDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool watchTheLaunchDlgProc_SWMTick(SimpleWindow *pWindow);

typedef enum WTLLogType
{
	WTLLOG_NORMAL,
	WTLLOG_SUCCEEDED,
	WTLLOG_WARNING,
	WTLLOG_FATAL,
} WTLLogType;

void WatchTheLaunch_Log(WTLLogType eLogType, const char *pString);

void AddPatchingTaskForRestarting(char *pTaskName, const char *pMachineName, char *pCommandLine);

void GetHumanConfirmationDuringShardRunning(FORMAT_STR const char* format, ...);
void GetHumanConfirmationDuringShardRunning_WithExtraButton(char *pButtonName, char *pButtonCommand, FORMAT_STR const char* format, ...);
