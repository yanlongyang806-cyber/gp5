#ifndef CRYPTICERROR_UITOOLS_H
#define CRYPTICERROR_UITOOLS_H

void CenterWindowToScreen(HWND hWndToCenter);
void WindowToBottomRightOfScreen(HWND hWndToMove);
void WindowToLeftOfWindow(HWND hWndToMove, HWND hWndOnRight);
void AppendRichText(HWND hRichEdit, const char *str, COLORREF color, bool bIsBold);

#endif
