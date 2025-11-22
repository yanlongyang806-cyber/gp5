#include "sysutil.h"
#include "file.h"
#include "errornet.h"
#include "wininclude.h"
#include "estring.h"
#include "logging.h"
#include "harvest.h"
#include "ui.h"
#include "uitools.h"
#include "systemtray.h"
#include "errorprogressdlg.h"
#include "timing.h"
#include "resource.h"
#include "validExecutables.h"
#include "utils.h"
#include "UTF8.h"

#include <psapi.h>
#include <richedit.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#define WM_REQUESTSWITCHMODE (WM_USER+2)

DWORD gChosenProcessID = -1;
char gChosenProcessName[MAX_PATH+1] = {0};
char gChosenProcessDetails[2048];

static HWND shProgressDialog = INVALID_HANDLE_VALUE;
static HWND shOopsDialog     = INVALID_HANDLE_VALUE;
static HWND shInternalDialog = INVALID_HANDLE_VALUE;

static bool sbWindowCreated = false;
static bool sUIThreadComplete = false;
static bool sbWorkComplete = false;
static bool sbShuttingDown = false;
static bool sbDescriptionSubmitted = false;

static void uiEnableSwitchMode();

static char sszDescription[MAX_DESCRIPTION_LENGTH+1] = "";

// Contents of internal text window; unused in customer mode, updated twice otherwise
static char *spInternalText = NULL;

INT_PTR CALLBACK InternalDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK OopsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProgressDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ChooseProcessProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK PopIDDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK FindAProgrammerProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

static DWORD WINAPI UIThread(void *lpParameter);
static void uiSetProgressWindowTitle();

// -----------------------------------------------------------------------------------------

#define CRYPTICERROR_LOGFILE "c:\\Night\\CrypticError.log"
extern bool gbSkipUserInput;

void LogDisk(const char *text)
{
	if(harvestIsEndUserMode() || harvestIsDeveloperMode()) // Only interested in production mode
		return;

	filelog_printf(CRYPTICERROR_LOGFILE, "[PID %d ETID %d EXE %s] %s", 
		harvestGetPid(),
		errorTrackerGetUniqueID(), 
		harvestGetFilename(), 
		text);
}

void LogDiskf(const char *format, ...)
{
	char *str = NULL;
	va_list ap;

	va_start(ap, format);
	estrConcatfv(&str, format, ap);
	va_end(ap);

	LogDisk(str);
	estrDestroy(&str);
}

void LogBold(const char *text)
{
	if (gbSkipUserInput) 
	{
		printf("%s\n", text);
		return;
	}
	while(!sbWindowCreated)
	{
		Sleep(10);
	}

	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), "\n", RGB(0,0,0), 0);
	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), text, RGB(0,0,0), 1);
}

void LogNormal(const char *text)
{
	if (gbSkipUserInput) 
	{
		printf("%s\n", text);
		return;
	}
	while(!sbWindowCreated)
	{
		Sleep(10);
	}

	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), "\n   ", RGB(0,0,0), 0);
	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), text, RGB(0,0,0), 0);
}

void LogNote(const char *text)
{
	if (gbSkipUserInput) 
	{
		printf("%s\n", text);
		return;
	}
	while(!sbWindowCreated)
	{
		Sleep(10);
	}

	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), "\n      ", RGB(0,0,0), 0);
	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), text, RGB(64,64,64), 0);
}

void LogError(const char *text)
{
	if (!gbSkipUserInput)
	{
		while(!sbWindowCreated)
		{
			Sleep(10);
		}

		AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), "\nERROR: ", RGB(255,0,0), 1);
		AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), text, RGB(255,0,0), 1);
	}
	else
		printf("%s\n", text);

	LogDiskf("ERROR: %s", text);
}

void LogImportant(const char *text)
{
	if (gbSkipUserInput) 
	{
		printf("%s\n", text);
		return;
	}
	while(!sbWindowCreated)
	{
		Sleep(10);
	}

	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), "\n   ", RGB(0,0,255), 1);
	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), text, RGB(0,0,255), 1);
}

void LogStatusBar(const char *text)
{
	if (gbSkipUserInput) return;
	while(!sbWindowCreated)
	{
		Sleep(10);
	}

	SetWindowText_UTF8(GetDlgItem(shProgressDialog, IDC_STATUS_BAR), text);
}

void LogTransferProgress(size_t uRemainingBytes, size_t uTotalBytes, F32 fKBytesPerSec)
{
	static char *prettyDuration = NULL;
	int percentage = 0;

	if(uTotalBytes)
		percentage = (int)((((F32)uTotalBytes-(F32)uRemainingBytes) / (F32)uTotalBytes) * 100);

	SendMessage(GetDlgItem(shProgressDialog, IDC_PROGRESS), PBM_SETPOS, percentage, 0);

	//sprintf(temp, "%2.2f KB/s", fKBytesPerSec);
	//SetWindowText_UTF8(GetDlgItem(shProgressDialog, IDC_SPEED), temp);

	timeSecondsDurationToPrettyEString(
		(fKBytesPerSec) ? (uRemainingBytes / (fKBytesPerSec*1000)) : 0,
		&prettyDuration);
	SetWindowText_UTF8(GetDlgItem(shProgressDialog, IDC_REMAINING), prettyDuration);
}

// -----------------------------------------------------------------------------------------

void uiNotifyProcessGone()
{
	EnableWindow(GetDlgItem(shInternalDialog, IDC_DEBUG), FALSE);
	EnableWindow(GetDlgItem(shInternalDialog, IDC_IGNORE), FALSE);
	EnableWindow(GetDlgItem(shInternalDialog, IDC_TERMINATE_PROCESS), FALSE);
	EnableWindow(GetDlgItem(shInternalDialog, IDC_REPORT), FALSE);
}

void uiUpdateInternalText()
{
	char *tempAssertBuf = NULL;
	char *assertbuf_splitloc = NULL;
	int id = errorTrackerGetUniqueID();
	char *msg = errorTrackerGetErrorMessage();

	if(shInternalDialog == INVALID_HANDLE_VALUE)
		return;

	estrCopy2(&spInternalText, ""); 

	if(id == 0)
		estrConcatf(&spInternalText, "-- No ErrorTracker ID Yet --\n\n");
	else
		estrConcatf(&spInternalText, "ErrorTracker ID: %d\nhttp://%s/detail?id=%d\n\n", id, getErrorTracker(), id);

	estrCopy2(&tempAssertBuf, harvestGetStringVar("assertbuf", "---"));

	// Move the callstack above the earlier block of data in the assert buffer, 
	// which happens to have the Process ID at the end of it.
	assertbuf_splitloc = strstri(tempAssertBuf, "Process ID:");
	if(assertbuf_splitloc)
	{
		assertbuf_splitloc = strchr(assertbuf_splitloc, '\n');
		if(assertbuf_splitloc)
		{
			while(*assertbuf_splitloc == '\n')
				assertbuf_splitloc++;
			assertbuf_splitloc--; // Move back to the last newline

			*assertbuf_splitloc = 0;
			assertbuf_splitloc++;
			estrConcatf(&spInternalText, "%s", assertbuf_splitloc);
		}
	}

	estrConcatf(&spInternalText, "%s", tempAssertBuf);

	if(msg && *msg)
	{
		estrConcatf(&spInternalText, "%s", msg);
	}

	estrReplaceOccurrences(&spInternalText, "\n", "\r\n");

	SetWindowText_UTF8(GetDlgItem(shInternalDialog, IDC_EXECUTABLE), harvestGetStringVar("executablename", "--"));
	SetWindowText_UTF8(GetDlgItem(shInternalDialog, IDC_MESSAGE),    harvestGetStringVar("errortext", "--"));

	SetWindowText_UTF8(GetDlgItem(shInternalDialog, IDC_ERROR), spInternalText);

	estrDestroy(&tempAssertBuf);
}

void uiCheckErrorTrackerResponse()
{
	uiUpdateInternalText();

	// Update Oops Dialog's Error Ticket ID
	{
		char *temp = NULL;
		U32 uID = errorTrackerGetUniqueID();
		if(uID > 0)
		{
			estrPrintf(&temp, "%d", uID);
		}
		else
		{
			estrPrintf(&temp, "Unknown");
		}
		SetWindowText_UTF8(GetDlgItem(shOopsDialog, IDC_ERRORID_OOPS), temp);
		estrClear(&temp);
	}

	uiSetProgressWindowTitle();
}

bool uiDescriptionDialogIsComplete()
{
	return sbDescriptionSubmitted;
}

const char * uiGetDescription()
{
	return sszDescription;
}

void uiDisplayInternalDialog()
{
	char *pBuffer = NULL;
	char *dialog_text = NULL;
	char *temp = NULL;

	estrCopy2(&temp, harvestGetStringVar("assertbuf", "---"));
	estrConcatf(&temp, "\n\nStackData: %s", harvestGetStringVar("stackdata", "---"));
	estrReplaceOccurrences(&temp, "\n", "\r\n");

	shInternalDialog = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CRYPTICERROR_INTERNAL), shProgressDialog, InternalDialogProc);
	GetWindowText_UTF8(shInternalDialog, &pBuffer);
	estrPrintf(&dialog_text, "%s [%s]", pBuffer, timeGetLocalDateString());
	SetWindowText_UTF8(shInternalDialog, dialog_text);
	estrDestroy(&dialog_text);
	uiUpdateInternalText();
	WindowToLeftOfWindow(shInternalDialog, shProgressDialog);
	ShowWindow(shInternalDialog, SW_SHOWNOACTIVATE);

	estrDestroy(&pBuffer);
	estrDestroy(&temp);
}

void uiRequestSwitchMode()
{
	while(!sbWindowCreated)
	{
		Sleep(10);
	}

	SendMessage(shProgressDialog, WM_REQUESTSWITCHMODE, 0, 0);
}

static void uiEnableSwitchMode()
{
	bool bShowOopsDialog = true;

	if(harvestCheckManualUserDump())
	{
		ShowWindow(shProgressDialog, SW_SHOW);
		SendMessage(shOopsDialog, WM_COMMAND, IDCANCEL, 0);
		return;
	}

	if(harvestGetMode() == CEM_CUSTOMER)
	{
		ShowWindow(shOopsDialog, SW_SHOW);
	}
	else
	{
		LogNote("Switching to Internal Mode...");
		ShowWindow(shProgressDialog, SW_SHOW);
		uiDisplayInternalDialog();
	}
}

void uiDisableDescription()
{
	if(shInternalDialog != INVALID_HANDLE_VALUE)
		EnableWindow(GetDlgItem(shInternalDialog, IDC_OFFER_ADDITIONAL_DETAILS), FALSE);
}

void uiStart()
{
	DWORD id;
	CreateThread(NULL, 0, UIThread, NULL, 0, &id);
}

bool uiWorkIsComplete()
{
	return sbWorkComplete;
}

void uiPopErrorID()
{
	U32 id = errorTrackerGetUniqueID();
	if(id == 0)
	{
		MessageBox(shProgressDialog, L"No error ticket ID found. Please try again later.", L"CrypticError", MB_OK);
	}
	else
	{
		DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CRYPTICERROR_POPID), shProgressDialog, PopIDDialogProc);
	}
}

void uiShutdown()
{
	if(sbShuttingDown)
		return;

	sbShuttingDown = true;
	sbWorkComplete = true;

	SendMessage(shOopsDialog, WM_COMMAND, IDCANCEL, 0);
	SendMessage(shProgressDialog, WM_COMMAND, IDC_CANCEL_ERROR_REPORT, 0);
	errorProgressDlgCancel();
	systemTrayShutdown();
	PostQuitMessage(0);
}

bool uiIsShuttingDown()
{
	return sbShuttingDown;
}

extern bool gbForceAutoClose;
extern bool gbForceStayUp;
bool shouldAutoClose(bool bHarvestSuccess)
{
	if(sbShuttingDown)
	{
		// someone already called uiShutdown(), no real choice at this point
		return true;
	}

	if(harvestGetMode() == CEM_CUSTOMER)
	{
		// Customers always auto-close
		return true;
	}

	// Forcing dialog to stay up takes precendence
	// Overrides occur after customer check
	if (gbForceStayUp)
		return false;
	if (gbForceAutoClose)
		return true;

	if(harvestNeedsProgrammer())
	{
		// Avoid auto-close if this particular ETID needs a programmer.

		// Note: harvestNeedsProgrammer() has a special case on prodservers and builders.
		return false;
	}

	if(errorTrackerGetDumpFlags() & DUMPFLAGS_AUTOCLOSE)
	{
		// Someone enabled ERRORTRACKER_OPTION_REQUEST_AUTOCLOSE_ON_ERROR ... must be a builder
		return true;
	}

	if(harvestGetMode() == CEM_PRODSERVER)
	{
		if(strstri(harvestGetFilename(), "GameServer"))
		{
			// Never leave a CrypticError up on a prodserver on a busted GameServer
			return true;
		}

		// Automatically close if CrypticError's "main harvesting" (dump creation and error sending) succeeded

		// Note: Please refer to harvest.c's SendDumps() and TerminateCrashedProcess() as either of those returning 
		//       false will cause bHarvestSuccess to be false (Examples: kill file exists, -leaveCrashesUpForever),
		//       as will the codepath that brings you through harvestNoErrorTracker().
		return bHarvestSuccess;
	}

	// Most likely a developer machine; let them read the error and manually close it
	return false;
}

bool shouldForceClose()
{
	if (gbForceStayUp)
		return false;
	if (gbForceAutoClose)
		return true;

	if(errorTrackerGetDumpFlags() & DUMPFLAGS_AUTOCLOSE)
	{
		// Someone enabled ERRORTRACKER_OPTION_REQUEST_AUTOCLOSE_ON_ERROR ... must be a builder
		return true;
	}

	if(harvestGetMode() == CEM_PRODSERVER)
	{
		if(strstri(harvestGetFilename(), "GameServer"))
		{
			// Never leave a CrypticError up on a prodserver on a busted GameServer
			return true;
		}
	}

	return false;
}

// Called from the main (worker) thread, not the UI thread. Any sleeps here are 
// going to occur in the main thread, to ensure you are waiting on UI to do things.
void uiWorkComplete(bool bHarvestSuccess)
{
	sbWorkComplete = true;

	if(shouldAutoClose(bHarvestSuccess))
	{
		uiShutdown();
	}
	else
	{
		while(!sUIThreadComplete)
		{
			Sleep(50);
		}
	}
	
}

void uiDumpSendComplete()
{
	SetWindowText_UTF8(GetDlgItem(shProgressDialog, IDC_CANCEL_ERROR_REPORT), "Close");
}

void uiFinished(bool bHarvestSuccess)
{
	if(shouldAutoClose(bHarvestSuccess))
	{
		uiShutdown();
	}
}

// Returns true if we -really- want a programmer to look at this, and the user is allowing us
bool uiNeedsProgrammer()
{
	if(isProcessGone())
		return false;

	if(!harvestIsEndUserMode() && !harvestCheckManualUserDump())
	{
		if(harvestNeedsProgrammer())
		{
			// IDOK == "cancel anyway"
			return (IDOK != DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CRYPTICERROR_FINDAPROGRAMMER), shProgressDialog, FindAProgrammerProc));
		}
	}

	return false;
}

bool uiChooseProcess()
{
	int result = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CRYPTICERROR_CHOOSE_PROCESS), NULL, ChooseProcessProc);
	if(result != IDOK)
		return false;

	return (gChosenProcessID != -1);
}

static bool ProcessIsDumpable(const char *processName)
{
	static int bDirExists = -1;
	char **pValidExe = gValidExecutables;

	if(bDirExists == -1)
	{
		bDirExists = (dirExists("c:\\Night\\tools\\bin")) ? 1 : 0;
	}

	if(bDirExists)
	{
		return true;
	}

	while(*pValidExe)
	{
		if(!stricmp(processName, *pValidExe))
		{
			return true;
		}

		pValidExe++;
	}
	return false;
}

static void PopulateProcessList(HWND hListBox)
{
	DWORD aProcesses[1024], cbNeeded, cProcesses, currentPID;
	unsigned int i;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return;

	// Calculate how many process identifiers were returned.
	cProcesses = cbNeeded / sizeof(DWORD);

	currentPID = GetCurrentProcessId();
	for ( i = 0; i < cProcesses; i++ )
	{
		if(aProcesses[i] != currentPID)
		{
			char *pProcessName = NULL;
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, aProcesses[i]);
		
			if (hProcess)
			{
				HMODULE hMod;
				DWORD cbIgnored;

				ANALYSIS_ASSUME(hProcess);

				estrStackCreate(&pProcessName);

				if (EnumProcessModules( hProcess, &hMod, sizeof(hMod), &cbIgnored))
				{
					GetModuleBaseName_UTF8( hProcess, hMod, &pProcessName );
					if(estrLength(&pProcessName) && ProcessIsDumpable(pProcessName))
					{
						int index;
						char temp[2048];

						sprintf_s(SAFESTR(temp), "%d - %s", aProcesses[i], pProcessName);


						index = ListBox_AddString_UTF8(hListBox, temp);
						if(index != LB_ERR)
						{
							SendMessage(hListBox, LB_SETITEMDATA, (WPARAM)(int)(index), (LPARAM)aProcesses[i]);
						}
					}

				}

				CloseHandle(hProcess);
				estrDestroy(&pProcessName);
			}

		}
		
	}
}

static void uiPop()
{
	ShowWindow(shProgressDialog, SW_SHOW);
	if(shInternalDialog != INVALID_HANDLE_VALUE)
		ShowWindow(shInternalDialog, SW_SHOW);
}

static void uiSetProgressWindowTitle()
{
	char temp[1024];
	sprintf_s(SAFESTR(temp), "CrypticError PID:%d ETID:%d", harvestGetPid(), errorTrackerGetUniqueID());
	SetWindowText_UTF8(shProgressDialog, temp);
}

// -----------------------------------------------------------------------------------------

INT_PTR CALLBACK InternalDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_COMMAND:
		{
			switch(wParam)
			{
			xcase IDC_DEBUG:             performProcessAction(PA_DEBUG);
			xcase IDC_IGNORE:            performProcessAction(PA_IGNORE);
			xcase IDC_TERMINATE_PROCESS: performProcessAction(PA_TERMINATE);
			xcase IDC_REPORT:            performProcessAction(PA_REPORT);

			xcase IDC_OFFER_ADDITIONAL_DETAILS:
				{
					ShowWindow(shOopsDialog, SW_SHOW);
					EnableWindow(GetDlgItem(shInternalDialog, IDC_OFFER_ADDITIONAL_DETAILS), FALSE);
					break;
				}

			xcase IDC_REMOTE_DEBUGGER:
				{
					LogNote("Running Remote Debugger...");
					runRemoteDebugger();
					break;
				}

			xcase IDC_COPY_TO_CLIPBOARD:
				{
					if(spInternalText)
					{
						LogNote("Copy to Clipboard...");
						winCopyToClipboard(spInternalText);
					}
					else
					{
						LogNote("Skipped copy to Clipboard (nothing to copy)");
					}
					break;
				}

			xcase IDC_VISIT_ERRORTRACKER:
				{
					U32 iID = errorTrackerGetUniqueID();
					if (iID)
					{
						char url[1024];
						LogNote("Visiting ErrorTracker...");
						sprintf(url, "http://%s/detail?id=%d", getErrorTracker(), iID);
						openURL(url);
					}
					else
					{
						LogError("No ErrorTracker Info!");
					}
					break;
				}
			}
			break;
		};
	}

	return 0;
}

// -----------------------------------------------------------------------------------------

INT_PTR CALLBACK OopsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(uiIsShuttingDown())
		return 0;

	switch(uMsg)
	{

	case WM_COMMAND:
		{
			switch(wParam)
			{
			case IDOK:
				{
					char *pTemp = NULL;

					GetWindowText_UTF8(GetDlgItem(hwndDlg, IDC_DETAILS), &pTemp);
					strcpy_trunc(sszDescription, pTemp);
					estrDestroy(&pTemp);

					// Fall through on purpose
				}
			case IDCANCEL:
				{
					if(shInternalDialog != INVALID_HANDLE_VALUE)
						EnableWindow(GetDlgItem(shInternalDialog, IDC_OFFER_ADDITIONAL_DETAILS), FALSE);

					EndDialog(hwndDlg, wParam);
					sbDescriptionSubmitted = true;
					break;
				}
			}
			break;
		};
	}

	return 0;
}

// -----------------------------------------------------------------------------------------

INT_PTR CALLBACK ProgressDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_REQUESTSWITCHMODE:
		{
			uiEnableSwitchMode();
			break;
		}

	case WM_COMMAND:
		{
			switch(wParam)
			{
			case IDCANCEL:
				{
					if(shInternalDialog == INVALID_HANDLE_VALUE)
					{
						ShowWindow(shProgressDialog, SW_HIDE);
						ShowWindow(shInternalDialog, SW_HIDE);
					}
					else
					{
						// Internal users can just kill CE with the X
						if(!uiNeedsProgrammer())
						{
							uiShutdown();
						}
					}
					break;
				}

			case ID_SYSTRAY_EXIT:
			case IDC_CANCEL_ERROR_REPORT:
				{
					if(!uiNeedsProgrammer())
					{
						uiShutdown();
					}
					break;
				}

			case ID_SYSTRAY_SHOWPROGRESS:
				{
					uiPop();
					break;
				}
			}
			break;
		};

	case WM_MOVE:
		{
			if(shInternalDialog != INVALID_HANDLE_VALUE)
			{
				WindowToLeftOfWindow(shInternalDialog, shProgressDialog);
			}
			break;
		}

	case WM_SYSTEMTRAY_NOTIFY:
		{
			if (LOWORD(lParam) == WM_RBUTTONUP)
			{
				HMENU hMenu, hPopupMenu;
				POINT p;
				GetCursorPos(&p);
				hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_SYSTRAY_MENU));
				hPopupMenu = GetSubMenu(hMenu, 0);
				TrackPopupMenu(hPopupMenu, TPM_LEFTALIGN, p.x, p.y, 0, shProgressDialog, NULL);
			}
			else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
			{
				uiPop();
			}
			break;
		}
	}

	return 0;
}

INT_PTR CALLBACK ChooseProcessProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_INITDIALOG:
		{
			CenterWindowToScreen(hwndDlg);
			PopulateProcessList(GetDlgItem(hwndDlg, IDC_PROCESS_LIST));
		}

	case WM_COMMAND:
		{
			switch(wParam)
			{
			case IDCANCEL:
				{
					EndDialog(hwndDlg, IDCANCEL);
					break;
				}

			case IDOK:
				{
					int index = SendMessage(GetDlgItem(hwndDlg, IDC_PROCESS_LIST), LB_GETCURSEL, 0, 0);
					if(index != LB_ERR)
					{
						gChosenProcessName[0] = 0;
						gChosenProcessID = SendMessage(GetDlgItem(hwndDlg, IDC_PROCESS_LIST), LB_GETITEMDATA, (WPARAM)(int)(index), 0);
						if(gChosenProcessID != -1)
						{
							char *pTemp = NULL;
							if(LB_ERR != ListBox_GetText_UTF8(GetDlgItem(hwndDlg, IDC_PROCESS_LIST), index, &pTemp))
							{
								char *afterThis = strstr(pTemp, " - ");
								if(afterThis)
								{
									strcpy_s(SAFESTR(gChosenProcessName), afterThis+3);
								}
							}
						}

						{
							char *pTemp = NULL;
							GetWindowText_UTF8(GetDlgItem(hwndDlg, IDC_DETAILS), &pTemp);
							strcpy_trunc(gChosenProcessDetails, pTemp);
							estrDestroy(&pTemp);
						}
					}
					EndDialog(hwndDlg, IDOK);
					break;
				}
			}
			break;
		};
	}

	return 0;
}

INT_PTR CALLBACK PopIDDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_INITDIALOG:
		{
			char temp[128];
			sprintf_s(SAFESTR(temp), "%d", errorTrackerGetUniqueID());
			SetWindowText_UTF8(GetDlgItem(hwndDlg, IDC_ERRORID), temp);
			CenterWindowToScreen(hwndDlg);
		}

	case WM_COMMAND:
		{
			switch(wParam)
			{
			case IDCANCEL:
			case IDOK:
					EndDialog(hwndDlg, wParam);
			}
			break;
		};
	}

	return 0;
}

INT_PTR CALLBACK FindAProgrammerProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_INITDIALOG:
		{
			SetWindowText_UTF8(GetDlgItem(hwndDlg, IDC_ERRORMESSAGE), errorTrackerGetErrorMessage());
			CenterWindowToScreen(hwndDlg);
		}

	case WM_COMMAND:
		{
			switch(wParam)
			{
			case IDCANCEL:
			case IDOK:
					EndDialog(hwndDlg, wParam);
			}
			break;
		};
	}

	return 0;
}

// -----------------------------------------------------------------------------------------

static DWORD WINAPI UIThread(void *lpParameter)
{
	HWND hTemp;
	MSG msg;
	
	// Create our worker window
	LoadLibrary(TEXT("Riched20.dll"));
	shProgressDialog = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CRYPTICERROR_PROGRESS), NULL, ProgressDialogProc);
	WindowToBottomRightOfScreen(shProgressDialog);
	uiSetProgressWindowTitle();
	ShowWindow(shProgressDialog, SW_HIDE);
	AppendRichText(GetDlgItem(shProgressDialog, IDC_OUTPUT_CTRL), "Initializing...", RGB(0,0,0), 1);
	hTemp = GetDlgItem(shProgressDialog, IDC_PROGRESS);
	SendMessage(hTemp, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(hTemp, PBM_SETPOS,   0, 0);
	sbWindowCreated = true;

	// Create our "Oops we crashed!" window
	shOopsDialog = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CRYPTICERROR_OOPS), shProgressDialog, OopsDialogProc);
	CenterWindowToScreen(shOopsDialog);
	SetWindowText_UTF8(GetDlgItem(shOopsDialog, IDC_ERRORID_OOPS), "Requesting...");
	ShowWindow(shOopsDialog, SW_HIDE);

	// Create our system tray icon
	systemTrayInit(shProgressDialog);

	while(1)
	{
		if( GetMessage( &msg, NULL, 0, 0 ) )
		{
			// ----------------------------------------------------------------------------------
			// Handle Escape Key

			if(msg.message == WM_KEYDOWN)
			{
				if(msg.wParam  == VK_ESCAPE)
				{
					if((msg.hwnd == shOopsDialog) || (GetParent(msg.hwnd) == shOopsDialog))
					{
						PostMessage(shOopsDialog, WM_COMMAND, IDCANCEL, 0);
					}
					else if((msg.hwnd != shOopsDialog) 
						 && (  (GetParent(msg.hwnd) == shProgressDialog)
						    || (          msg.hwnd  == shProgressDialog)
						    || (GetParent(msg.hwnd) == shInternalDialog)
						    || (          msg.hwnd  == shInternalDialog)
							)
						 )
					{
						{
							PostMessage(shProgressDialog, WM_COMMAND, IDC_CANCEL_ERROR_REPORT, 0);
						}
					}
				}
			}
			// ----------------------------------------------------------------------------------

			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		else
			break;
	}

	systemTrayShutdown();
	sUIThreadComplete = true;
	return 0;
}
