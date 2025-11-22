#ifndef CRYPTICERROR_SYSTEMTRAY_H
#define CRYPTICERROR_SYSTEMTRAY_H

#define WM_SYSTEMTRAY_NOTIFY (WM_USER+100)

void systemTrayInit(HWND hParent);
void systemTrayShutdown();

#endif
