#pragma once

#include "UIDefs.h"

// predec's
typedef struct SimpleWindow SimpleWindow;
typedef bool SimpleWindowManager_TickCallback(SimpleWindow *pWindow);

// this kicks off the main UI of the launcher
// returns errorlevel to return for app
extern int UI_ShowBrowserWindowAndRegisterCallback(const char *productName, SimpleWindowManager_TickCallback *pMainTickCB);

// this requests a graceful close of the browser/main app loop
extern void UI_RequestClose(int errorlevel);

// this restarts the launcher back at the login page, usually due to one of several errors in authentication
extern void UI_RestartAtLoginPage(const char *errorMessageToDisplayAfterLoginPageLoaded);

// manage the tray icon
extern void UI_ManageTrayIcon(const char *productName);

// display a message in the browser status window
extern void UI_DisplayStatusMsg(const char *message, bool bSet5SecondTimeout);

// launch a (modal) message box, and wait for user input
extern int UI_MessageBox(const char *text, const char *caption, unsigned int type); // called from patcher

// verify the locID for the requested productName, and if that verified locID is different than the current locID, it will reset the current locID, and it will send CLMSG_RELOAD_PAGE
extern bool UI_UpdateLocale(const char *productName);

// set the state of the patch button on the main launcher page (after login)
extern bool UI_SetPatchButtonState(PatchButtonState state);

// clears the password field in the browser on the login page
extern void UI_ClearPasswordField(void);

// this will load the main launcher page, and set the appropriate launcher state
extern void UI_EnterMainLauncherPage(U32 accountID, U32 accountTicketID);

extern void UI_EnterConflictFlow(U32 ticketID, const char *prefix);

extern bool UI_SetMinimizeTrayIcon(const char *productName, bool bMinimizeTrayIcon);
extern bool UI_GetMinimizeTrayIcon(const char *productName);

extern bool UI_SetShowTrayIcon(const char *productName, bool bShowTrayIcon);
extern bool UI_GetShowTrayIcon(const char *productName);

// install language (in registry) is LCID (e.g. 1033 for english)
// you can use WindowsLocale (defined in AppLocale.h) for these values.
extern bool UI_SetInstallLanguage(const char *productName, int installLanguage);
extern int UI_GetInstallLanguage(const char *productName);

extern bool UI_GetHasCoreLocale(void);

// Should we launch the game as soon as patching is done?
extern bool UI_SetAutoLaunch(const char *productName, bool bAutoLaunch);
extern bool UI_GetAutoLaunch(const char *productName);

void UI_HideTrayIcon(void);
void UI_ShowTrayIcon(void);
