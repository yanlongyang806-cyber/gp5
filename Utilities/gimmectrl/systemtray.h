#pragma once
GCC_SYSTEM

#include "windefinclude.h"

#define NIIF_INFO       0x00000001
#define NIIF_WARNING    0x00000002
#define NIIF_ERROR      0x00000003

void systemTrayAdd(HWND hParent);
void systemTrayRemove(HWND hParent);
void systemTrayNotfy(HWND hParent, const char *title, const char *msg, U32 icon);