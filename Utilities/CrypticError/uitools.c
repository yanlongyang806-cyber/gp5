#include "sysutil.h"
#include "wininclude.h"
#include "richedit.h"
#include "uitools.h"
#include "utf8.h"

// Shamelessly pilfered from MFC. Should be in the standard Win32 API. Oh well.
void CenterWindowToScreen(HWND hWndToCenter)
{
	// get coordinates of the window relative to its parent
	RECT rcDlg;
	RECT rcArea;
	RECT rcCenter;
	int xLeft, yTop;
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);

	GetWindowRect(hWndToCenter, &rcDlg);
	GetMonitorInfo(MonitorFromWindow(hWndToCenter, MONITOR_DEFAULTTOPRIMARY), &mi);
	rcCenter = mi.rcWork;
	rcArea = mi.rcWork;

	// find dialog's upper left based on rcCenter
	xLeft = (rcCenter.left + rcCenter.right) / 2 - (rcDlg.right - rcDlg.left) / 2;
	yTop = (rcCenter.top + rcCenter.bottom) / 2 - (rcDlg.bottom - rcDlg.top) / 2;

	// if the dialog is outside the screen, move it inside
	if (xLeft < rcArea.left)
		xLeft = rcArea.left;
	else if (xLeft + (rcDlg.right - rcDlg.left) > rcArea.right)
		xLeft = rcArea.right - (rcDlg.right - rcDlg.left);

	if (yTop < rcArea.top)
		yTop = rcArea.top;
	else if (yTop + (rcDlg.bottom - rcDlg.top) > rcArea.bottom)
		yTop = rcArea.bottom - (rcDlg.bottom - rcDlg.top);

	// map screen coordinates to child coordinates
	SetWindowPos(hWndToCenter, NULL, xLeft, yTop, -1, -1,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowToBottomRightOfScreen(HWND hWndToMove)
{
	// get coordinates of the window relative to its parent
	RECT rcDlg;
	RECT rcArea;
	RECT rcCenter;
	int xLeft, yTop;
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);

	GetWindowRect(hWndToMove, &rcDlg);
	GetMonitorInfo(MonitorFromWindow(hWndToMove, MONITOR_DEFAULTTOPRIMARY), &mi);
	rcCenter = mi.rcWork;
	rcArea = mi.rcWork;

	// find dialog's upper left based on rcCenter
	xLeft = rcArea.right  - (rcDlg.right  - rcDlg.left);
	yTop  = rcArea.bottom - (rcDlg.bottom - rcDlg.top);

	// map screen coordinates to child coordinates
	SetWindowPos(hWndToMove, NULL, xLeft, yTop, -1, -1,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowToLeftOfWindow(HWND hWndToMove, HWND hWndOnRight)
{
	// get coordinates of the window relative to its parent
	RECT rcDlg;
	RECT rcArea;
	int xLeft, yTop;

	GetWindowRect(hWndToMove, &rcDlg);
	GetWindowRect(hWndOnRight, &rcArea);

	// find dialog's upper left based on rcCenter
	xLeft = rcArea.left - (rcDlg.right  - rcDlg.left);
	yTop  = rcArea.top;

	// map screen coordinates to child coordinates
	SetWindowPos(hWndToMove, NULL, xLeft, yTop, -1, -1,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void AppendRichText(HWND hRichEdit, const char *str, COLORREF color, bool bIsBold)
{
	int nOldLines = 0, nNewLines = 0, nScroll = 0;
	long nInsertionPoint = 0;
	CHARFORMAT cf;
	CHARRANGE cr;

	// Save number of lines before insertion of new text
	nOldLines		= SendMessage(hRichEdit, EM_GETLINECOUNT, 0, 0);

	// Initialize character format structure
	cf.cbSize		= sizeof(CHARFORMAT);
	cf.dwMask		= CFM_COLOR|CFM_BOLD;
	cf.dwEffects	= 0;	// To disable CFE_AUTOCOLOR
	cf.crTextColor	= color;

	if(bIsBold)
	{
		cf.dwEffects |= CFE_BOLD;
	}

	// Set insertion point to end of text
	nInsertionPoint = GetWindowTextLength(hRichEdit);
	cr.cpMin = nInsertionPoint;
	cr.cpMax = -1;
	SendMessage(hRichEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

	//  Set the character format
	SendMessage(hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

	// Replace selection. Because we have nothing selected, this will simply insert
	// the string at the current caret position.
	SendMessage_ReplaceSel_UTF8(hRichEdit, str);

	// Get new line count
	nNewLines		= SendMessage(hRichEdit, EM_GETLINECOUNT, 0, 0);

	// Scroll by the number of lines just inserted
	nScroll			= nNewLines - nOldLines;
	SendMessage(hRichEdit, EM_LINESCROLL, 0, nScroll);
}
