#pragma once
GCC_SYSTEM

#include "windefinclude.h"

// Custom messages
#define WM_APP_TRAYICON (WM_APP+1)

void systemTrayAdd(HWND hParent);
void systemTrayRemove(HWND hParent);