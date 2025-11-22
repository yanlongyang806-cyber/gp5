#pragma once
#include "SimplewindowManager.h"

BOOL mainScreenDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool mainScreenDlgProc_SWMTick(SimpleWindow *pWindow);

extern U32 gOptionButtonIDs[];
extern U32 gServerScreenButtonIDs[];


void InitShardSetupFileComboBox(HWND hDlg, U32 iResID, char *pDefault, bool bReload, bool bShortNames);
extern char **ppShardSetupFileNames;
extern char **ppShardSetupFileDescriptions;

char *GetAutoSettingsWarningString(void);