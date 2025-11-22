#pragma once
#include "windefinclude.h"

typedef struct SimpleWindow SimpleWindow;
typedef struct QueryableProcessHandle QueryableProcessHandle;

typedef int (*qcmdCB)(int rv, void *userdata);

void qcmdRun(FORMAT_STR const char *cmd, ...);
void qcmdReturn(QueryableProcessHandle *proc);
void qcmdReturnFast(U32 code);
void qcmdCallback(qcmdCB cb, void *userdata);
bool queueRunning(void);
void queueAbort(void);
BOOL gimmectrlQueueDialogCB(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
