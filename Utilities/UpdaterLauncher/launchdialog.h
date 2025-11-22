#include <windows.h>
#include "resource.h"


extern char updater[MAX_PATH];
extern char folder[MAX_PATH];
extern char server[MAX_PATH];

extern int noself;
extern int cov;
extern int console;
extern int majorpatch;
extern int test;
extern int nolaunch;


LRESULT CALLBACK LaunchDlgProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam );