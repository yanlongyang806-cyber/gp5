#include "UI.h"
#include "UIDefs.h"
#include "Environment.h"
#include "browser.h"
#include "registry.h"
#include "History.h"
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "GameDetails.h"
#include "Shards.h"
#include "resource_CrypticLauncher.h"
#include "Account.h"
#include "patcher.h" // for Set/GetVerifyLevel() and Set/GetShardToAutoPatch()
#include "version.h"
#include "options.h" // ideally, this isn't here
#include "xfers_dialog.h" // ideally, this isn't here

// Common
#include "GlobalComm.h"
#include "accountCommon.h"

// UtilitiesLib
#include "url.h"
#include "systemtray.h"
#include "SimpleWindowManager.h"
#include "earray.h"
#include "textparser.h"
#include "sysutil.h"
#include "ThreadSafeQueue.h"
#include "timing.h"
#include "StringUtil.h"
#include "sock.h"
#include "UTF8.h"

// NewControllerTracker
#include "NewControllerTracker_Pub.h"
#include "autogen/NewControllerTracker_pub_h_ast.h"

// UI DEFINES
#define HTML_ELEMENT_NAME_MESSAGE			"msg"
#define HTML_ELEMENT_NAME_LAUNCHER_VERSION	"launcher_version"
#define HTML_ELEMENT_NAME_SHARDS_LABEL		"shards_label"
#define HTML_ELEMENT_NAME_SHARDS			"shards"
#define HTML_ELEMENT_NAME_SHARD_BUTTON_1	"shard_button_1"
#define HTML_ELEMENT_NAME_SHARD_BUTTON_2	"shard_button_2"
#define HTML_ELEMENT_NAME_SHARD_BUTTON_3	"shard_button_3"
#define HTML_ELEMENT_NAME_LINK_SUCCESS		"link_success"
#define HTML_ELEMENT_NAME_MIGRATE_SUCCESS	"migrate_success"
#define HTML_ELEMENT_NAME_LINK_PAGE			"link_page"
#define HTML_ELEMENT_NAME_USERNAME			"name"
#define HTML_ELEMENT_NAME_PASSWORD			"pass"
#define HTML_ELEMENT_NAME_LOGIN_TYPE		"login_type"
#define HTML_ELEMENT_NAME_LOGIN_BLOCK		"login_block"
#define HTML_ELEMENT_NAME_LANGUAGE_OPTIONS  "language_options"
#define HTML_ELEMENT_NAME_JSONOPTIONS		"jsonOptions"


#define patcherQueueContinueStartPatch(continueStartPatchCommand)	postCommandPtr(gQueueToPatchClient, CLCMD_CONTINUE_START_PATCH, (continueStartPatchCommand))
#define patcherQueueFixState()										postCommandPtr(gQueueToPatchClient, CLCMD_FIX_STATE, NULL)
#define patcherQueueStopThread()									postCommandInt(gQueueToPatchClient, CLCMD_STOP_THREAD, 0)

static int updateShardList(const char *productName);
static void refreshWindowTitle(const char *title);
static void setDisplayTimeout(bool bSet);
static void maintainDisplayTimeout(void);
static bool changeLocale(LocaleID locID);

static U32 sTaskbarCreateMessage = 0;
static SimpleWindow *sLauncherWindow;
static char *sQueuedDisplayMessage = NULL;
static time_t sDisplayMessageLastTime = 0;

// If this is set, select this shard on page load.  Used when reloading page due to shard change.
static const ShardInfo_Basic *sReloadShard = NULL;

// Allow CORE client to override our locale.
// This is subject to various limitations.  For instance, it will only work if that locale is actually available for that product.
static char *sCoreLocale = NULL;  // "English", "German", "French", etc - look at AppLocale.c in LocaleTable.name for valid strings
AUTO_COMMAND ACMD_NAME(CoreLocale) ACMD_CMDLINE;
void cmd_CoreLocale(const char *locale)
{
	SAFE_FREE(sCoreLocale);
	sCoreLocale = strdup(locale);
}

static const ShardInfo_Basic *UI_GetSelectedShard(char **err)
{
	const ShardInfo_Basic *retVal = NULL;
	char *cstr;

	if (gPrePatchShard)
	{
		retVal = gPrePatchShard;
	}
	else if (BrowserGetSelectElementValue(HTML_ELEMENT_NAME_SHARDS, &cstr))
	{
		char *temp = strchr(cstr, ':');
		if (temp)
		{
			*temp = '\0';

			retVal = ShardsGetByProductName(cstr /* productName */, temp+1 /* shardName */);
			if (!retVal && err)
			{
				estrPrintf(err, "Can't find a shard to match %s:%s", cstr, temp+1);
			}
		}
		else
		{
			if (err)
			{
				estrPrintf(err, "Invalid shard line selected: %s", cstr);
			}
		}

		GlobalFree(cstr);
	}
	// there are times this is called when its OK not to retrieve a selected shard, because the shard list is not on the page we're looking at (e.g. login)

	return retVal;
}

static void UI_ShowOptions(const ShardInfo_Basic* shard, const char* options_reason)
{
	if (shard)
	{
		LauncherResetAvailableLocalesFromSingleShard(shard);
		UI_UpdateLocale(shard->pProductName);
	}

	ShowOptions(shard, options_reason);
}

static void doButtonActionForSelectedShard(void)
{
	const ShardInfo_Basic *shard = UI_GetSelectedShard(NULL);
	if (shard)
	{
		LauncherResetAvailableLocalesFromSingleShard(shard);
		UI_UpdateLocale(shard->pProductName);
		patcherQueueDoButtonAction((void *)shard); // have to strip const here
	}
}

#define REGKEY_LAST_VERIFY_COMPLETE			"LastVerifyComplete"
#define REGKEY_LAST_VERIFY_START			"LastVerifyStart"
#define REGKEY_LAST_VERIFY_SKIP				"LastVerifySkip"

static bool UI_BrowserTickFunc(SimpleWindow *pWindow)
{
	HRESULT ret;
	CrypticLauncherCommand *cmd;

	// This relies on the page being fully loaded, don't process the queue unless that is true
	if (!BrowserIsPageComplete())
	{
		return true;
	}

	while ((ret = XLFQueueRemove(gQueueToUI, &cmd)) == S_OK)
	{
		switch (cmd->type)
		{
		case CLCMD_REFRESH_WINDOW_TITLE:
			refreshWindowTitle(cmd->str_value); // already translated, don't call cgettext()
			break;

		case CLCMD_DISPLAY_MESSAGE:
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**cmd[4]'"
			UI_DisplayStatusMsg(cmd->str_value, false /* bSet5SecondTimeout */); // already translated, don't call cgettext()
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**cmd[4]'"
			free(cmd->str_value);
			break;

		case CLCMD_SET_PROGRESS:
			{
				char patchProgressString[1024];
				PatchProgressCommand *pCD = (PatchProgressCommand *)cmd->ptr_value;

#pragma warning(suppress:6001) // /analyze " Using uninitialized memory '*pCmdData'"
				sprintf_s(SAFESTR(patchProgressString),	"%.4f,%d:%.2d,%d:%.2d,%d,%d,%.*f%s,%.0f%s,%.*f%s,%d", pCD->percent, pCD->elapsedMinutes, pCD->elapsedSeconds, pCD->totalMinutes, pCD->totalSeconds, pCD->numReceivedFiles, pCD->numTotalFiles, pCD->precisionReceived, pCD->numReceived, pCD->unitsReceived, pCD->numTotal, pCD->unitsTotal, pCD->precisionActual, pCD->numActual, pCD->unitsActual, pCD->showDetails);

				if (gDebugMode)
				{
					printf("invoking 'do_set_progress(%s)' on %s\n", patchProgressString, LauncherGetDisplayedURL());
				}

				BrowserInvokeScript("do_set_progress", INVOKE_SCRIPT_ARG_STRING, patchProgressString, INVOKE_SCRIPT_ARG_NULL);
				free(cmd->ptr_value);
			}

			break;

		case CLCMD_SET_BUTTON_STATE:
			UI_SetPatchButtonState(cmd->int_value);
			break;

		case CLCMD_PUSH_BUTTON:
			doButtonActionForSelectedShard();
			break;

		case CLCMD_START_LOGIN_FOR_GAME:
			AccountLinkInit(false /* bUseConnectCB */);
			if (AccountLinkWaitAndLogin())
			{
				LauncherSetShardToRun(cmd->ptr_value);
				LauncherSetState(CL_STATE_GETTINGGAMETICKET);
			}
			else
			{
				// launcher state remains CL_STATE_READY - let the user keep trying - this is a valid state to stay in.
				UI_DisplayStatusMsg(_("Unable to connect to account server (3)"), false /* bSet5SecondTimeout */);
			}
			break;

		case CLCMD_PATCHING_DONE:
			// cmd->str_value is productName
			if (UI_GetAutoLaunch(cmd->str_value) && !(GetKeyState(VK_SHIFT)&0x8000))
			{
				doButtonActionForSelectedShard();
			}
			break;

		case CLCMD_PATCHING_FAILED_AND_QUIT:
			UI_MessageBox(
				_("Error connecting to patch server"),
				_("Error"),
				MB_OK|MB_ICONERROR);
			UI_HideTrayIcon();
			exit(cmd->int_value);
			break;

		case CLCMD_PATCHING_VERIFY_CHECK:
			{
				const ShardInfo_Basic *shard = cmd->ptr_value;
				const char *productName = shard->pProductName;
				int verifyLevel = GetVerifyLevel(productName);
				ContinueStartPatchCommand *pCmdData = malloc(sizeof(ContinueStartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.

				pCmdData->shard = shard;
				pCmdData->bVerifyPatch = false;

				if (verifyLevel > 0) // at least ask
				{
					int uiret;

					writeRegInt(productName, REGKEY_LAST_VERIFY_START,  timeSecondsSince2000()); // this is written, but never read by launcher

					if (verifyLevel > 1) // force
					{
						uiret = IDYES;
					}
					else
					{
						uiret = UI_MessageBox(_("An error was detected by the game, would you like to verify all files? NOTE: This may take 10-20 minutes."),
							_("Verify?"),
							MB_YESNO|MB_ICONWARNING);
					}

					if (uiret == IDYES)
					{
						pCmdData->bVerifyPatch = true;
					}
					else
					{
						writeRegInt(productName, REGKEY_LAST_VERIFY_SKIP,  timeSecondsSince2000()); // this is written, but never read by launcher
						SetVerifyLevel(productName, 0);
					}
				}

				// Even though this is in the main thread, it is triggered by a command from the 
				// patching thread, so make sure this state change is still allowed.
				if (LauncherSetPatchingState(CL_STATE_SETTINGVIEW))
				{
					UI_SetPatchButtonState(CL_BUTTONSTATE_DISABLED);
					patcherQueueContinueStartPatch(pCmdData);
				}
			}
			break;

		case CLCMD_PATCHING_VERIFY_COMPLETE:
			// cmd->str_value is productName
			if (GetVerifyLevel(cmd->str_value) > 0)
			{
				writeRegInt(cmd->str_value, REGKEY_LAST_VERIFY_COMPLETE, timeSecondsSince2000()); // this is written, but never read by launcher
				SetVerifyLevel(cmd->str_value, 0);
			}
			break;

		default:
			assert(false); // unhandled command!
			break;
		}
		free(cmd);
	}
	assert(ret == XLOCKFREE_STRUCTURE_EMPTY);
	maintainDisplayTimeout();

	return true;
}

static void UI_SetReloadShard(const ShardInfo_Basic *shard)
{
	sReloadShard = shard;
}

static const ShardInfo_Basic *UI_GetReloadShard()
{
	return sReloadShard;
}

static bool shardsOnChangeCallback(void *userOnChangeData)
{
	char *err = NULL;
	const ShardInfo_Basic* shard = UI_GetSelectedShard(&err);
	assertmsgf(shard, "%s", err);
	estrDestroy(&err);
	LauncherResetAvailableLocalesFromSingleShard(shard);
	if (!UI_UpdateLocale(shard->pProductName))
	{
		// If the page isn't reloading, just restart the patch
		StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
		pCmdData->shard = shard;
		pCmdData->bSetShardToAutoPatch = true;
		pCmdData->localeId = getCurrentLocale();
		patcherQueueStartPatch(pCmdData);
	}
	else
	{
		// If the page is reloading, set the shard so it knows what to patch to when it is done
		UI_SetReloadShard(shard);
	}
	return true; // continue
}

static bool excludeWindow(HWND h)
{
	static char* sWindowClassNameExclusions[] = {
		"button",
		"tooltip",
		"sysshadow",
		"shell_traywnd",
		"gdkwindowtemp"
	};

	char *pBuf= NULL;
	U32 i;

	if (!IsWindowVisible(h))
		return true;

	GetClassName_UTF8(h, &pBuf);
	for (i=0; i<ARRAY_SIZE_CHECKED(sWindowClassNameExclusions); i++)
	{
		if (strstri(pBuf, sWindowClassNameExclusions[i])!=NULL)
		{
			estrDestroy(&pBuf);
			return true;
		}
	}

	estrDestroy(&pBuf);
	return false;
}

// Return true if an IP should be considered "internal" for purposes of access to internal shards.
static bool isInternalIp(U32 ip)
{
	return (ip & 0xFFFF) == 0x1FAC; // 172.31.x.y (0xAC.0x1F.x.y)
}

// Return true if the local host has an IP that passes isInternalIp().
static bool isInternalHost()
{
	return isInternalIp(getHostPublicIp()) || isInternalIp(getHostLocalIp());
}

// Clear the environment.
static void clearEnvironment()
{
	gDevMode = false;
	gQAMode = false;
	gPWRDMode = false;
	gPWTMode = false;
}

static bool browseToURL(const char *url, const char **eaEstrKeyValuePostData)
{
	bool retVal = false;
	LauncherSetDisplayedURL(""); // this is set when CLMSG_PAGE_LOADED handled (after the page is loaded and displayed)
	retVal = BrowserDisplayHTMLFromURL(url, eaEstrKeyValuePostData);
	return retVal;
}

// this reloads the current page based on certain launcher states - this is called to handle CLMSG_RELOAD_PAGE
static void UI_ReloadPage(void)
{
	switch (LauncherGetState())
	{
	case CL_STATE_LOGINPAGELOADED:
		{
			char *url = NULL;
			LauncherGetLoginURL(&url);
			if (!browseToURL(url, NULL))
			{
				assertmsgf(false, "failed to browse to login URL '%s'", url);
			}
			estrDestroy(&url);
		}
		break;
	case CL_STATE_LAUNCHERPAGELOADED:
	case CL_STATE_SETTINGVIEW:
	case CL_STATE_WAITINGFORPATCH:
	case CL_STATE_GETTINGFILES:
	case CL_STATE_READY:
	case CL_STATE_ERROR:
		if(LauncherIsDisplayingPrepatchURL())
		{
			char *url = NULL;
			LauncherGetLoginOrPrepatchURL(&url);
			if (!browseToURL(url, NULL))
			{
				assertmsgf(false, "Failed to browse to prepatch URL '%s'", url);
			}
			estrDestroy(&url);
		}
		else
		{
			AccountLinkInit(false /* bUseConnectCB */);
			if (AccountLinkWaitAndLogin())
			{
				LauncherSetState(CL_STATE_GETTINGPAGETICKET);
			}
			else
			{
				// launcher state remains in one of the states above
				UI_DisplayStatusMsg(_("Unable to contact account server"), false /* bSet5SecondTimeout */);
			}
		}
	}
}

void UI_RestartAtLoginPage(const char *errorMessageToDisplayAfterLoginPageLoaded)
{
	char *url = NULL;

	// Restart from the beginning.
	LauncherSetState(CL_STATE_LOADINGPAGELOADED);

	LauncherGetLoginURL(&url);
	if (!browseToURL(url, NULL))
	{
		assertmsgf(false, "failed to browse to login URL '%s'", url);
	}
	estrDestroy(&url);

	if (errorMessageToDisplayAfterLoginPageLoaded)
	{
		sQueuedDisplayMessage = strdup(errorMessageToDisplayAfterLoginPageLoaded);
	}
}

static void deriveRelativeConflictURL(void)
{
	char *relativeConflictURL;

	// Grab the conflict page.
	// optional - should not error if element not there
	if (BrowserGetInputElementValue(HTML_ELEMENT_NAME_LINK_PAGE, &relativeConflictURL))
	{
		LauncherSetRelativeConflictURL(relativeConflictURL);
		GlobalFree(relativeConflictURL);
	}
	else
	{
		Errorf("Cannot find %s element on launcher web page!", HTML_ELEMENT_NAME_LINK_PAGE);
	}
}

void UI_RequestClose(int errorlevel)
{
	if (sLauncherWindow)
	{
		sLauncherWindow->bCloseRequested = true;
		sLauncherWindow->pUserData = (void*)errorlevel;
	}

	patcherQueueStopThread();
}

static bool checkEnvAndSetUsername(const char *username)
{
	bool bEnvUser = (strcmp(username, "cryptic") == 0) ? true : false;
	AccountSetUsername(username);
	return bEnvUser;
}

static void checkAndSetLoginType(const char *loginType)
{
	// we no longer handle "default", "cryptic", or "pwe" loginType
	// those correspond to ACCOUNTLOGINTYPE_Default, ACCOUNTLOGINTYPE_Cryptic, or ACCOUNTLOGINTYPE_PerfectWorld
	if (!stricmp_safe(loginType, "either"))
	{
		AccountSetLoginType(ACCOUNTLOGINTYPE_CrypticAndPW);
	}
	else if (!stricmp_safe(loginType, "force_migrate"))
	{
		AccountSetForceMigrate(LauncherHasConflictURL());
		AccountSetLoginType(ACCOUNTLOGINTYPE_CrypticAndPW);
	}
	else
	{
		assertmsgf(0, "Invalid login_type %s", loginType);
	}
}

static bool checkAndSetPassword(const char *password, bool bEnvUser, char **pEnvStatusMsg)
{
	bool bUseRealAuth = true;

	if (bEnvUser && isInternalHost())
	{
		const char *newEnvironment = password;

		if (stricmp(newEnvironment, "live") == 0)
		{
			clearEnvironment();
			bUseRealAuth = false;
		}
		else if ((stricmp(newEnvironment, "dev") == 0) && ipFromString(ENV_DEV_ACCOUNTSERVER_HOST))
		{
			clearEnvironment();
			gDevMode = true;
			bUseRealAuth = false;
		}
		else if ((stricmp(newEnvironment, "qa") == 0) && ipFromString(ENV_QA_ACCOUNTSERVER_HOST))
		{
			clearEnvironment();
			gQAMode = true;
			bUseRealAuth = false;
		}
		else if ((stricmp(newEnvironment, "pwrd") == 0) && ipFromString(ENV_PWRD_ACCOUNTSERVER_HOST))
		{
			clearEnvironment();
			gPWRDMode = true;
			bUseRealAuth = false;
		}
		else if ((stricmp(newEnvironment, "pwt") == 0) && ipFromString(ENV_PWT_ACCOUNTSERVER_HOST))
		{
			clearEnvironment();
			gPWTMode = true;
			bUseRealAuth = false;
		}
	}

	if (bUseRealAuth)
	{
		AccountSetPassword(password, false /* hashed */);
	}
	else
	{
		estrPrintf(pEnvStatusMsg, FORMAT_OK(_("You are now in the <b>%s</b> environment.")), password);
	}

	return bUseRealAuth;
}

static BOOL shardInShardList(const ShardInfo_Basic *shard)
{
	char shardDescriptor[512];
	LauncherFormShardDescriptor(SAFESTR(shardDescriptor), shard->pProductName, shard->pShardName);

	return BrowserSetSelectElementValue(HTML_ELEMENT_NAME_SHARDS, shardDescriptor);
}

static BOOL browserWindowProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			HICON hIcon;

			//printf("ui browser window initialized - %p\n", hDlg);

			// Get a copy of the message used to indicate a new taskbar has been created
			ATOMIC_INIT_BEGIN;
			{
				sTaskbarCreateMessage = RegisterWindowMessage(L"TaskbarCreated");
			}
			ATOMIC_INIT_END;

			SetWindowPos(hDlg, NULL, 0, 0, 800 + (GetSystemMetrics(SM_CXBORDER) * 2), 600 + GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYBORDER), SWP_NOMOVE|SWP_NOZORDER);

			// Load the small and large icons
			hIcon = LoadImage(GetModuleHandle(NULL),
				MAKEINTRESOURCE(IDI_LAUNCHER),
				IMAGE_ICON,
				GetSystemMetrics(SM_CXSMICON),
				GetSystemMetrics(SM_CYSMICON),
				0);
			assertmsg(hIcon, "Can't load small icon");
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			hIcon = LoadImage(GetModuleHandle(NULL),
				MAKEINTRESOURCE(IDI_LAUNCHER),
				IMAGE_ICON,
				GetSystemMetrics(SM_CXICON),
				GetSystemMetrics(SM_CYICON),
				0);
			assertmsg(hIcon, "Can't load big icon");
			SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

 			if (BrowserInit(pWindow->hWnd))
			{
				BrowserSetLanguageCode(getCurrentLocale());
				if (!BrowserDisplayHTMLStr(_("<em>Loading. Please wait.</em>")))
				{
					assertmsg(false, "Failed to display loading message.");
				}
			}
			else
			{
				assertmsg(false, "Unknown failure initializing browser.");
			}

			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			UI_RequestClose(0);
			break;
		}
		break;

	case WM_CLOSE:
		// Non-dialog handler for red X. (Transgaming.)
		if(!SimpleWindowManager_GetUseDialogs()) {
			UI_RequestClose(0);
		}
		break;

	case WM_SYSCOMMAND:
		switch (wParam)
		{
		case SC_MINIMIZE:
			if (UI_GetMinimizeTrayIcon(NULL))
			{
				UI_ShowTrayIcon();
				ShowWindow(hDlg, SW_HIDE);
				return TRUE;
			}
		}
		break;

	case WM_APP:
		{
			UrlArgumentList *pArgList = (UrlArgumentList *)lParam;
			switch (wParam)
			{
			case CLMSG_PAGE_LOADED: // Page loaded (app:1)
				{
					char *pathname = NULL;

					SetFocus(pWindow->hWnd);

					// Find the URL of the current page
					if (BrowserGetCurrentURL(&pathname))
					{
						static LocaleID sCachedLastLocale = DEFAULT_LOCALE_ID;
						const ShardInfo_Basic *shard = NULL;
						char username[MAX_LOGIN_FIELD];
						char* old_version;

						// Check for double notifications
						if (!LauncherIsDisplayingURL(pathname) ||
							sCachedLastLocale != getCurrentLocale())
						{
							LauncherSetDisplayedURL(pathname);
							sCachedLastLocale = getCurrentLocale();

							// If we're in CL_STATE_LOGGINGINAFTERLINK, ignore page loads.
							if (LauncherGetState() != CL_STATE_LOGGINGINAFTERLINK)
							{
								// Check if the website is signaling that that the linking state is done.
								// Note: This could have been done by checking the pathname, as below, but we decided to do it differently for flexibility.
								if (LauncherGetState() == CL_STATE_LINKING)
								{
									// link_success - Unlinked PWE account was logged in with in the first place, and successfully linked
									// PWE account linking is done.
									if (BrowserExistsElement(HTML_ELEMENT_NAME_LINK_SUCCESS))
									{
										// Initiate authentication via accountnet
										AccountLinkInit(false /* bUseConnectCB */);
										if (AccountLinkWaitAndLogin())
										{
											LauncherSetState(CL_STATE_LOGGINGINAFTERLINK);
										}
										else
										{
											UI_RestartAtLoginPage(STACK_SPRINTF(FORMAT_OK(_("Unable to authenticate: %s")), AccountLastError()));
											// this will result in launcher state going to CL_STATE_LOADINGPAGELOADED, and then CL_STATE_LOGINPAGELOADED
										}
									}

									// migrate_success - Unlinked Cryptic account has gone through force migrate (linking) flow
									// Cryptic account force migrate is done.
									if (BrowserExistsElement(HTML_ELEMENT_NAME_MIGRATE_SUCCESS))
									{
										char *url = NULL;

										// Restart from the beginning.
										LauncherSetState(CL_STATE_LOADINGPAGELOADED);

										LauncherGetLoginURL(&url);
										if (!browseToURL(url, NULL))
										{
											assertmsgf(false, "failed to browse to login URL '%s'", url);
										}
										estrDestroy(&url);
									}
								}
								else if (LauncherGetState() == CL_STATE_START && (!strcmp(pathname, "blank") || !strcmp(pathname, "/blank")))
								{
									char *url = NULL;

									if (gForceInvalidLocale)
									{
										changeLocale(LOCALE_ID_INVALID);
									}

									LauncherSetState(CL_STATE_LOADINGPAGELOADED);

									// Request the login page
									LauncherGetLoginOrPrepatchURL(&url);
									if (!browseToURL(url, NULL))
									{
										assertmsgf(false, "failed to browse to login or prepatch URL '%s'", url);
									}
									estrDestroy(&url);
								}
								else
								{
									// Display the build timestamp
									if (BrowserGetElementInnerHTML(HTML_ELEMENT_NAME_LAUNCHER_VERSION, &old_version))
									{
										char *new_version = STACK_SPRINTF("%s%s", old_version, BUILD_CRYPTICSTAMP);
										GlobalFree(old_version);
										BrowserSetElementInnerHTML(HTML_ELEMENT_NAME_LAUNCHER_VERSION, new_version);
									}

									if (LauncherIsDisplayingLoginURL())
									{
										char *language_options = NULL;
										bool bRegistryUsernameReturned = AccountGetUsername(SAFESTR(username));
										bool validUsername = false;

										LauncherSetState(CL_STATE_LOGINPAGELOADED);

										// Set username to saved value if present
										if (gReadPasswordFromSTDIN)
										{
											validUsername = BrowserSetInputElementValue(HTML_ELEMENT_NAME_USERNAME, username, false);
											BrowserSetInputElementValue(HTML_ELEMENT_NAME_PASSWORD, "******", true);
											BrowserFocusElement(HTML_ELEMENT_NAME_PASSWORD);
										}
										else if (bRegistryUsernameReturned)
										{
											validUsername = BrowserSetInputElementValue(HTML_ELEMENT_NAME_USERNAME, username, false);
											UI_ClearPasswordField();
											BrowserFocusElement(HTML_ELEMENT_NAME_PASSWORD);
										}
										else
										{
											validUsername = BrowserSetInputElementValue(HTML_ELEMENT_NAME_USERNAME, "", true);
											UI_ClearPasswordField();
											BrowserFocusElement(HTML_ELEMENT_NAME_USERNAME);
										}

										if (!validUsername)
										{
											gReadPasswordFromSTDIN = false;
											UI_ClearPasswordField();
											BrowserFocusElement(HTML_ELEMENT_NAME_USERNAME);
										}

										// Automatically log in if we got a username/password on stdin.
										if (gReadPasswordFromSTDIN && username[0])
										{
											deriveRelativeConflictURL();
											AccountLinkInit(false /* bUseConnectCB */);
											if (AccountLinkWaitAndLogin())
											{
												LauncherSetState(CL_STATE_LOGGINGIN);
												gReadPasswordFromSTDIN = false;
											}
											else
											{
												// launcher state remains CL_STATE_LOGINPAGELOADED
												UI_DisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Unable to authenticate: %s")), AccountLastError()), false /* bSet5SecondTimeout */);
											}
										}
										else
										{
											// launcher state remains CL_STATE_LOGINPAGELOADED
										}

										// handle displaying sQueuedDisplayMessage, if non-null
										if (sQueuedDisplayMessage)
										{
											UI_DisplayStatusMsg(cgettext(sQueuedDisplayMessage), true /* bSet5SecondTimeout */);
											SAFE_FREE(sQueuedDisplayMessage);
										}

										// ask the webpage for supported languages for this product
										if (BrowserGetElementInnerHTML(HTML_ELEMENT_NAME_LANGUAGE_OPTIONS, &language_options))
										{
											char *context = NULL;
											char *language = strtok_s(language_options, " ,", &context);

											LauncherClearAvailableLocales();
											while (language)
											{
												if (gDebugMode)
												{
													printf("language seen from launcher web page: %s\n", language);
												}
												LauncherAddAvailableLocaleByCode(language);
												language = strtok_s(NULL, " ,", &context);
											}
											GlobalFree(language_options);
										}
										else
										{
											if (gDebugMode)
											{
												printf("language_options not in login page - defaulting to launcher implemented languages!");
											}
											LauncherResetAvailableLocalesToDefault(gStartupProductName);
										}
									}
									else if (LauncherIsDisplayingPostLoginURL())
									{
										int displayedShards = 0;
										int numShards = 0;
										bool use_buttons = false;
										const char *productName = gStartupProductName;

										LauncherSetState(CL_STATE_LAUNCHERPAGELOADED);

										displayedShards = updateShardList(productName);
										numShards = ShardsGetCount();

										shard = UI_GetReloadShard();
										UI_SetReloadShard(NULL);

										if (!shard)
										{
											shard = GetShardToAutoPatch();
										}

										// At this point if shard was set it was done by internal logic, so it must be in the list
										if (gDebugMode && shard && !shardInShardList(shard))
										{
											printf("Shard set by page reload or autopatch not found in the shard list\n");
										}

										/* [WEB-5535] - Website taking over this code
										// Check if there are any shards for the correct product
										if (ShardsAreDown(productName))
										{
											if (gDebugMode)
											{
												printf("invoking 'set_server_status(0)' on %s\n", LauncherGetDisplayedURL());
											}
											BrowserInvokeScript("set_server_status", INVOKE_SCRIPT_ARG_INT, 0, INVOKE_SCRIPT_ARG_NULL);
										}
										*/

										if (displayedShards == 0)
										{
											if (numShards == 0)
											{
												// This account cannot see any shards, so this is an account error.
												UI_DisplayStatusMsg(
													STACK_SPRINTF(FORMAT_OK(_("Account error, please click <a href=\"%ssupport/account_error\">here</a> for more information")),
													gdGetURL(gdGetIDByName(productName))),
													false /* bSet5SecondTimeout */);
											}
											else
											{
												// The account can see shards, but none are available in this language. Launch options with error display
												UI_DisplayStatusMsg(_("No shards available in this language."), false);
												UI_ShowOptions(NULL, OPTIONS_ERROR_NO_SHARDS_IN_LANGUAGE);
											}
										}
										else if (displayedShards == 1)
										{
											// pick the first shard as default
											shard = ShardsGetFirstDisplayed();
											if (!shardInShardList(shard) || 
												stricmp(shard->pProductName, productName)!=0 || 
												stricmp(shard->pShardCategoryName, SHARD_CATEGORY_LIVE)!=0)
											{
												BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARDS_LABEL, true);
												BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARDS, true);
											}
										}
										else
										{
											BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARDS_LABEL, true);

											use_buttons = ShardsUseButtons(productName);

											if (use_buttons)
											{
												bool button1, button2, button3;
												ShardsGetButtonStates(productName, &button1, &button2, &button3);
												if (button1)
												{
													BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARD_BUTTON_1, true);
												}
												if (button2)
												{
													BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARD_BUTTON_2, true);
												}
												if (button3)
												{
													BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARD_BUTTON_3, true);
												}
											}
											else
											{
												BrowserSetElementDisplay(HTML_ELEMENT_NAME_SHARDS, true);
											}
										}

										// select a shard
										if (displayedShards)
										{
											if (!shard || !shardInShardList(shard))
											{
												// if no shard picked out yet, find the last one we patched to
												shard = ShardsFindLast();
												if (!shard || !shardInShardList(shard))
												{
													// if we can't find the last shard, find the default
													shard = ShardsGetDefault(productName);
												}
											}
										}

										// Do a patch check against the initially selected shard.
										if (shard && shardInShardList(shard))
										{
											char shardDescriptor[512];
											LauncherFormShardDescriptor(SAFESTR(shardDescriptor), shard->pProductName, shard->pShardName);

											if (BrowserSetSelectElementValue(HTML_ELEMENT_NAME_SHARDS, shardDescriptor))
											{
												LauncherResetAvailableLocalesFromSingleShard(shard);

												if (use_buttons)
												{
													U32 gameID = gdGetIDByName(shard->pProductName);
													const char *liveShard = gdGetLiveShard(gameID);
													const char *ptsShard1 = gdGetPtsShard1(gameID);
													const char *ptsShard2 = gdGetPtsShard2(gameID);

													if (liveShard && stricmp(shard->pShardName, liveShard) == 0)
													{
														BrowserSetElementClassName(HTML_ELEMENT_NAME_SHARD_BUTTON_1, "shard-button-selected");
													}
													else if (ptsShard1 && stricmp(shard->pShardName, ptsShard1) == 0)
													{
														BrowserSetElementClassName(HTML_ELEMENT_NAME_SHARD_BUTTON_2, "shard-button-selected");
													}
													else if (ptsShard2 && stricmp(shard->pShardName, ptsShard2) == 0)
													{
														BrowserSetElementClassName(HTML_ELEMENT_NAME_SHARD_BUTTON_3, "shard-button-selected");
													}
												}

												UI_UpdateLocale(shard->pProductName);

												{
													StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
													pCmdData->shard = shard;
													pCmdData->bSetShardToAutoPatch = true;
													pCmdData->localeId = getCurrentLocale();
													patcherQueueStartPatch(pCmdData);
												}
											}
											else
											{
												assertmsg(false, "failed to set shard into selector.");
											}
										}
										else if(gDebugMode && displayedShards > 0)
										{
											printf("Internal logic error: shards are displayed but none were found\n");
										}

										if (!BrowserSetSelectElementOnChangeCallback(HTML_ELEMENT_NAME_SHARDS, shardsOnChangeCallback, NULL))
										{
											assertmsg(false, "failed to set onChangeCallback.");
										}
									}
									else if (LauncherIsDisplayingPrepatchURL())
									{
										LauncherSetState(CL_STATE_LAUNCHERPAGELOADED);

										BrowserSetElementVisible(HTML_ELEMENT_NAME_SHARDS, false);
										BrowserSetElementVisible(HTML_ELEMENT_NAME_LOGIN_BLOCK, false);
										assertmsg(gPrePatchShard, "Loading prepatch page, but no prepatch data");

										{
											StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
											pCmdData->shard = gPrePatchShard;
											pCmdData->bSetShardToAutoPatch = true;
											pCmdData->localeId = getCurrentLocale();
											patcherQueueStartPatch(pCmdData);
										}
									}
									else
									{
										// We have an unrecognized page that has a "/launcher*" suffix or is an unexpected "blank" page 
										// (the only way we'd get a CLMSG_PAGE_LOADED msg). Realistically, I don't ever think we'll get this?
										AccountLinkWaitAndLogin();
										Errorf("unrecognized web page with '%s' suffix detected", pathname);
									}
								}
							}
						}

						GlobalFree(pathname);
					}
					else
					{
						assertmsg(false, "failed to retrieve current browser URL.");
					}
				}
				break;

			case CLMSG_ACTION_BUTTON_CLICKED: // Action button clicked (app:2)
				PatcherCancel();
				break;

			case CLMSG_OPTIONS_CLICKED: // Options link (app:3)
				{
					UI_ShowOptions(UI_GetSelectedShard(NULL), NULL);
				}
				break;

			case CLMSG_LOGIN_SUBMIT: // Login form (app:4)
				if ((LauncherGetState() != CL_STATE_LOGGINGIN) &&
					!gBypassPipe)
				{
					char *inputUsername;
					char *inputPassword;
					char *inputLoginType;
					char *errmsg = NULL;
					char *envStatusMsg = NULL;
					bool bEnvUser = false;
					bool bUseRealAuth = false;
					const char *argUsername  = urlFindValue(pArgList, HTML_ELEMENT_NAME_USERNAME);
					const char *argPassword  = urlFindValue(pArgList, HTML_ELEMENT_NAME_PASSWORD);
					const char *argLoginType = urlFindValue(pArgList, HTML_ELEMENT_NAME_LOGIN_TYPE);

					AccountSetPassword("", false /* hashed */);
					UI_DisplayStatusMsg("", false /* bSet5SecondTimeout */);
					AccountSetLoginType(ACCOUNTLOGINTYPE_Default);

					// Grab the username
					if (argUsername)
					{
						bEnvUser = checkEnvAndSetUsername(argUsername);
					}
					else if (BrowserGetInputElementValue(HTML_ELEMENT_NAME_USERNAME, &inputUsername))
					{
						bEnvUser = checkEnvAndSetUsername(inputUsername);
						GlobalFree(inputUsername);
					}
					else
					{
						assertmsgf(false, "Cannot find %s input element", HTML_ELEMENT_NAME_USERNAME);
					}

					// must be done before we grab and validate the loginType
					deriveRelativeConflictURL();

					// Grab the login type.
					// optional - should not error if element not there
					if (argLoginType)
					{
						checkAndSetLoginType(argLoginType);
					}
					else if (BrowserGetInputElementValue(HTML_ELEMENT_NAME_LOGIN_TYPE, &inputLoginType))
					{
						checkAndSetLoginType(inputLoginType);
						GlobalFree(inputLoginType);
					}
					else
					{
						assertmsgf(false, "Cannot find %s input element", HTML_ELEMENT_NAME_LOGIN_TYPE);
					}

					// Grab the password
					if (argPassword)
					{
						bUseRealAuth = checkAndSetPassword(argPassword, bEnvUser, &envStatusMsg);

						// erase it from the arglist
						urlRemoveValue(pArgList, HTML_ELEMENT_NAME_PASSWORD);
					}
					else if (BrowserGetInputElementValue(HTML_ELEMENT_NAME_PASSWORD, &inputPassword))
					{
						bUseRealAuth = checkAndSetPassword(inputPassword, bEnvUser, &envStatusMsg);

						// erase it from memory, even though we are freeing it just below here
						memset(inputPassword, 0, strlen(inputPassword));

						GlobalFree(inputPassword);
					}
					else
					{
						assertmsgf(false, "Cannot find %s input element", HTML_ELEMENT_NAME_PASSWORD);
					}

					// at this point in the logic - we have username/password/loginType - or else we would have asserted and quit.
					if (bUseRealAuth)
					{
						// Initiate authentication via accountnet
						AccountLinkInit(false /* bUseConnectCB */);
						if (AccountLinkWaitAndLogin())
						{
							LauncherSetState(CL_STATE_LOGGINGIN);
						}
						else
						{
							// launcher->state remains CL_STATE_LOGINPAGELOADED
							UI_DisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Unable to authenticate: %s")), AccountLastError()), false /* bSet5SecondTimeout */);
						}
					}
					else
					{
						UI_DisplayStatusMsg(envStatusMsg, false /* bSet5SecondTimeout */);
						estrDestroy(&envStatusMsg);
						UI_ClearPasswordField();
						BrowserSetInputElementValue(HTML_ELEMENT_NAME_USERNAME, "", true);
						BrowserFocusElement(HTML_ELEMENT_NAME_USERNAME);
						ShardsControllerTrackerClearIPs();
						LauncherSetState(CL_STATE_LOGINPAGELOADED);
					}
				}
				break;

			case CLMSG_OPTIONS_SAVED: // (app:5)
				{
					char *jsonOptions = BrowserInvokeScriptString("LauncherAPI.getOptions", INVOKE_SCRIPT_ARG_NULL);

					if (jsonOptions)
					{
						ApplyOptions(jsonOptions);
						GlobalFree(jsonOptions);
					}
				}
				break;

			case CLMSG_OPEN_XFER_DEBUG: // X keypress
				{
					SimpleWindow *xfers_dialog = SimpleWindowManager_FindWindow(CL_WINDOW_XFERS, 0);
					if (xfers_dialog)
					{
						PostMessage(xfers_dialog->hWnd, WM_COMMAND, IDCANCEL, 0);
					}
					else
					{
						SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_XFERS, 0, IDD_XFERS, false, XfersDialogFunc, XfersTickFunc, NULL);
						xfers_dialog = SimpleWindowManager_FindWindow(CL_WINDOW_XFERS, 0);
						if (xfers_dialog)
						{
							xfers_dialog->pPreDialogCB = XfersPreDialogFunc;
						}
						SetActiveWindow(pWindow->hWnd);
					}
				}
				break;

			case CLMSG_RELOAD_PAGE: // Reload launcher page
				UI_ReloadPage();
				break;
			}

			StructDestroy(parse_UrlArgumentList, pArgList);
		}
		break;
	case WM_APP_TRAYICON:
		{
			if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_RBUTTONUP)
			{
				WINDOWINFO wi = {0};
				wi.cbSize = sizeof(WINDOWINFO);
				GetWindowInfo(hDlg, &wi);
				if (!(wi.dwStyle & WS_VISIBLE) || IsIconic(hDlg))
				{
					ShowWindow(hDlg, SW_NORMAL);
					SetForegroundWindow(hDlg);
					if (!UI_GetShowTrayIcon(NULL))
					{
						UI_HideTrayIcon();
					}
				}
				else
				{
					HWND h = GetTopWindow(NULL);
					while(h != NULL)
					{
						if (!excludeWindow(h))
							break;
						h = GetNextWindow(h, GW_HWNDNEXT);
					}
					if (h == hDlg)
					{
						// We are on top
						SendMessage(hDlg, WM_SYSCOMMAND, SC_MINIMIZE, (LPARAM)NULL);
					}
					else
					{
						SetForegroundWindow(hDlg);
					}
				}
			}
			//else if (LOWORD(lParam) == WM_LBUTTONDOWN || LOWORD(lParam) == WM_RBUTTONDOWN)
			//{
			//	HWND h = GetTopWindow(NULL);
			//	U32 i = 0;
			//	TCHAR buf[100], buf2[100];
			//	foreground = GetForegroundWindow();
			//	printf("%p %p %p\n", hDlg, foreground, h);
			//	while(h != NULL)
			//	{
			//		i += 1;
			//		if (IsWindowVisible(h))
			//		{
			//			GetClassName(h, buf, ARRAY_SIZE_CHECKED(buf));
			//			GetWindowText(h, buf2, ARRAY_SIZE_CHECKED(buf2));
			//			printf("%u %p %s %s %s\n", i, h, buf, buf2, (IsIconic(h)?"icon":""));
			//		}
			//		h = GetNextWindow(h, GW_HWNDNEXT);
			//	}
			//	printf("\n");
			//}
		}
		break;
	case WM_GETDLGCODE:
		{
			LPMSG lpMsg = (LPMSG)lParam;
			if (lpMsg && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_RETURN)
			{
				Errorf("Got a GETDLGCODE with a return");
				BrowserProcessKeystrokes(lpMsg[0], pWindow);
				return DLGC_WANTALLKEYS;
			}
			return DLGC_WANTALLKEYS;

		}
		break;
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_CHAR:
	case WM_DEADCHAR:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_IME_CHAR:
	case WM_KEYLAST:
		{
			MSG dummy;
			dummy.hwnd = hDlg;
			dummy.message = iMsg;
			dummy.wParam = wParam;
			dummy.lParam = lParam;
			dummy.time = pWindow->iLastMessageTime;
			BrowserProcessKeystrokes(dummy, pWindow);
		}
		break;
	case WM_PAINT:
		BrowserPaint(hDlg);
		break;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDBLCLK:
	case WM_MOUSEMOVE:
	case WM_MOUSELEAVE:
	case WM_MOUSEWHEEL:
		{
			MSG dummy;
			dummy.hwnd = hDlg;
			dummy.message = iMsg;
			dummy.wParam = wParam;
			dummy.lParam = lParam;
			dummy.time = pWindow->iLastMessageTime;
			BrowserProcessMouse(dummy);
		}
		break;
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		{
			MSG dummy;
			dummy.hwnd = hDlg;
			dummy.message = iMsg;
			dummy.wParam = wParam;
			dummy.lParam = lParam;
			dummy.time = pWindow->iLastMessageTime;
			if(BrowserProcessFocus(dummy))
				return TRUE;
		}
		break;
	default:

		if (sTaskbarCreateMessage && sTaskbarCreateMessage == iMsg)
		{
			UI_ShowTrayIcon();
		}
		break;
	}

	return FALSE;
}



int UI_ShowBrowserWindowAndRegisterCallback(const char *productName, SimpleWindowManager_TickCallback *pMainTickCB)
{
	const char *launcherProductDisplayName = gdGetDisplayName(0);
	SimpleWindowManager_Init(launcherProductDisplayName, true);

	SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_MAIN, 0, IDD_CL_START_STANDALONE, true, browserWindowProc, UI_BrowserTickFunc, NULL);
	sLauncherWindow = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	assertmsg(sLauncherWindow, "Unable to open main launcher window");
	sLauncherWindow->pPreDialogCB = BrowserMessageCallback;
	refreshWindowTitle(gdGetDisplayName(gdGetIDByName(productName)));

	SimpleWindowManager_Run(pMainTickCB, NULL); // this is the main loop of the launcher - this thread 'blocks' here until launcher is quit
	UI_HideTrayIcon();

	BrowserShutdown();

	return (int)sLauncherWindow->pUserData;
}

void UI_HideTrayIcon(void)
{
	if (sLauncherWindow)
	{
		systemTrayRemove(sLauncherWindow->hWnd);
	}
}

void UI_ShowTrayIcon(void)
{
	if (sLauncherWindow)
	{
		systemTrayAdd(sLauncherWindow->hWnd);
	}
}

void UI_ManageTrayIcon(const char *productName)
{
	// Update the running systray params to match the active settings
	if (sLauncherWindow)
	{
		if (UI_GetShowTrayIcon(productName))
		{
			UI_ShowTrayIcon();
		}
		else if (!IsWindowVisible(sLauncherWindow->hWnd) || IsIconic(sLauncherWindow->hWnd))
		{
			if (!UI_GetMinimizeTrayIcon(productName))
			{
				ShowWindow(sLauncherWindow->hWnd, SW_NORMAL);
				UI_HideTrayIcon();
			}
		}
		else
		{
			UI_HideTrayIcon();
		}
	}
}

void UI_DisplayStatusMsg(const char* message, bool bSet5SecondTimeout)
{
	BrowserSetElementInnerHTML(HTML_ELEMENT_NAME_MESSAGE, message);
	setDisplayTimeout(bSet5SecondTimeout);
}

static void setDisplayTimeout(bool bSet)
{
	if (bSet)
	{
		sDisplayMessageLastTime = time(NULL);
	}
	else
	{
		sDisplayMessageLastTime = 0;
	}
}

static void maintainDisplayTimeout(void)
{
	if ((LauncherGetState() == CL_STATE_LOGINPAGELOADED) &&
		sDisplayMessageLastTime &&
		(time(NULL) - sDisplayMessageLastTime) >= 5)
	{
		UI_DisplayStatusMsg("", false /* bSet5SecondTimeout */);
	}
}

static void refreshWindowTitle(const char *title)
{
	assert(sLauncherWindow && title);

	if (sLauncherWindow && title)
	{
		SetWindowText_UTF8(sLauncherWindow->hWnd, title);
	}
}

int UI_MessageBox(const char *text, const char *caption, unsigned int type)
{
	HWND hWnd;

	if (sLauncherWindow)
	{
		hWnd = sLauncherWindow->hWnd;
	}
	else
	{
		hWnd = NULL;
	}
	return MessageBox_UTF8(hWnd, text, caption, type);
}

static bool changeLocale(LocaleID locID)
{
	// If the locale ID is changing, update the page
	if (locID != getCurrentLocale())
	{
		LauncherSetLocale(locID);
		BrowserSetLanguageCode(locID);
		if (sLauncherWindow)
		{
			SendMessage(sLauncherWindow->hWnd, WM_APP, CLMSG_RELOAD_PAGE, 0);
		}
		return true;
	}
	return false;
}

// the locale that we update to with this function is either sCoreLocale or the "installed language" in the registry, for this product
bool UI_UpdateLocale(const char *productName)
{
	LocaleID locID;

	// Override with the CORE locale, if specified.
	if (sCoreLocale)
	{
		locID = locGetIDByName(sCoreLocale);
	}
	else
	{
		locID = locGetIDByWindowsLocale(UI_GetInstallLanguage(productName));
	}

	// verify that the language choice chosen is still valid 
	if (!LauncherIsLocaleAvailable(locID))
	{
		locID = LauncherChooseFallbackLocale(locID);
	}

	return changeLocale(locID);
}


// return value - true indicates we should keep iterating
//                false indicates we should stop iterating
//
// bShouldInsert - indicates we should display this item in the list of shard options 
static bool optionProductCallbackFunc(void *userOptionData, int userOptionIndex, bool *bShouldInsert, char *optionValue, size_t optionValueMaxSize, char *optionText, size_t optionTextMaxSize)
{
	ShardInfo_Basic **ppShards = (ShardInfo_Basic **)userOptionData;
	bool bKeepIterating = false;

	*bShouldInsert = false;
	if (userOptionIndex < eaSize(&ppShards))
	{
		ShardInfo_Basic *shard = ppShards[userOptionIndex];
		if (ShardShouldDisplay(shard))
		{
			LauncherFormShardDescriptor(optionValue, optionValueMaxSize, shard->pProductName, shard->pShardName);

			snprintf_s(optionText, optionTextMaxSize, "%s - %s", gdGetDisplayName(gdGetIDByName(shard->pProductName)), shard->pShardName);
			*bShouldInsert = true;
		}

		bKeepIterating = true;
	}

	return bKeepIterating;
}

static bool optionRegularCallbackFunc(void *userOptionData, int userOptionIndex, bool *bShouldInsert, char *optionValue, size_t optionValueMaxSize, char *optionText, size_t optionTextMaxSize)
{
	ShardInfo_Basic **ppShards = (ShardInfo_Basic **)userOptionData;
	bool bKeepIterating = false;

	*bShouldInsert = false;
	if (userOptionIndex < eaSize(&ppShards))
	{
		ShardInfo_Basic *shard = ppShards[userOptionIndex];
		if (ShardShouldDisplay(shard))
		{
			LauncherFormShardDescriptor(optionValue, optionValueMaxSize, shard->pProductName, shard->pShardName);

			snprintf_s(optionText, optionTextMaxSize, "%s", shard->pShardName);
			*bShouldInsert = true;
		}

		bKeepIterating = true;
	}

	return bKeepIterating;
}

static int updateShardList(const char *productName)
{
	int numShards = ShardsGetCount();

	if (numShards)
	{
		bool bShowProduct;
		void *userdata;
		char *msg;
		int displayedShards = 0;

		if (ShardsGetMessage(&msg))
		{
			UI_DisplayStatusMsg(cgettext(msg), false /* bSet5SecondTimeout */);
		}

		bShowProduct = ShardsPrepareForDisplay(productName, &userdata);

		// Update the shard select box
		displayedShards = BrowserSetSelectElementOptions(HTML_ELEMENT_NAME_SHARDS, bShowProduct ? optionProductCallbackFunc : optionRegularCallbackFunc, userdata);

		if (displayedShards == -1)
		{
			assertmsg(false, "failed to set shard list elements.");
		}

		return displayedShards;
	}

	return 0;
}

bool UI_SetPatchButtonState(PatchButtonState state) 
{
	char *state_str = "";
	switch (state)
	{
		case CL_BUTTONSTATE_DISABLED:
			state_str = "disabled";
			break;
		case CL_BUTTONSTATE_PATCH:
			state_str = "patch";
			break;
		case CL_BUTTONSTATE_PLAY:
			state_str = "play";
			break;
		case CL_BUTTONSTATE_CANCEL:
			state_str = "cancel";
			break;
	}
	if (gDebugMode)
	{
		printf("invoking 'do_set_button_state(%s)' on %s\n", state_str, LauncherGetDisplayedURL());
	}
	return BrowserInvokeScript("do_set_button_state", INVOKE_SCRIPT_ARG_STRING, state_str, INVOKE_SCRIPT_ARG_NULL);
}

void UI_ClearPasswordField(void)
{
	BrowserSetInputElementValue(HTML_ELEMENT_NAME_PASSWORD, "", true);
}

void UI_EnterMainLauncherPage(U32 accountID, U32 accountTicketID)
{
	char *url = NULL;
	char **eaEstrKeyValuePostData = NULL;
	char *estrAccountID = NULL;
	char *estrTicketID = NULL;

	UI_DisplayStatusMsg(_("Loading launcher"), /*bSet5SecondTimeout=*/false);

	eaPush(&eaEstrKeyValuePostData, estrCreateFromStr("accountid"));
	estrPrintf(&estrAccountID, "%u", accountID);
	eaPush(&eaEstrKeyValuePostData, estrAccountID);
	estrAccountID = NULL;
	eaPush(&eaEstrKeyValuePostData, estrCreateFromStr("ticketid"));
	estrPrintf(&estrTicketID, "%u", accountTicketID);
	eaPush(&eaEstrKeyValuePostData, estrTicketID);
	estrTicketID = NULL;

	LauncherGetMainURL(&url);
	if(!browseToURL(url, eaEstrKeyValuePostData))
	{
		assertmsgf(false, "failed to browse to main launcher URL '%s'", url);
	}

	eaDestroyEString(&eaEstrKeyValuePostData);
	estrDestroy(&url);

	LauncherSetState(CL_STATE_GOTPAGETICKET);
}

void UI_EnterConflictFlow(U32 ticketID, const char *prefix)
{
	// We succeeded after we retried as a Cryptic login, start force migrate flow.
	char *url = NULL;

	UI_ClearPasswordField();

	// Create conflict URL.
	estrStackCreate(&url);
	LauncherGetConflictURL(&url, ticketID, prefix);

	// Navigate to conflict page.
	if (!browseToURL(url, NULL))
	{
		assertmsgf(false, "failed to browse to conflict URL '%s'", url);
	}
	estrDestroy(&url);

	LauncherSetState(CL_STATE_LINKING);
}

// ---------------------------------

#define REGKEY_MINIMIZE_TO_TRAY				"MinimizeToTray"

bool UI_SetMinimizeTrayIcon(const char *productName, bool bMinimizeTrayIcon)
{
	return RegistryBackedBoolSet(productName, REGKEY_MINIMIZE_TO_TRAY, bMinimizeTrayIcon, true /* bUseHistory */);
}

bool UI_GetMinimizeTrayIcon(const char *productName)
{
	bool minimizeTrayIcon = false; /* default */
	RegistryBackedBoolGet(productName, REGKEY_MINIMIZE_TO_TRAY, &minimizeTrayIcon, true /* bUseHistory */);
	return minimizeTrayIcon;
}

#define REGKEY_SHOW_TRAY_ICON				"ShowTrayIcon"

bool UI_SetShowTrayIcon(const char *productName, bool bShowTrayIcon)
{
	return RegistryBackedBoolSet(productName, REGKEY_SHOW_TRAY_ICON, bShowTrayIcon, true /* bUseHistory */);
}

bool UI_GetShowTrayIcon(const char *productName)
{
	bool showTrayIcon = true; /* default */
	RegistryBackedBoolGet(productName, REGKEY_SHOW_TRAY_ICON, &showTrayIcon, true /* bUseHistory */);
	return showTrayIcon;
}

#define REGKEY_INSTALL_LANGUAGE				"InstallLanguage"

bool UI_SetInstallLanguage(const char *productName, int installLanguage)
{
	// stored this integer as a string.  urgh.  probably a result of installer nonsense
	return RegistryBackedStrSet(productName, REGKEY_INSTALL_LANGUAGE, STACK_SPRINTF("%u", installLanguage), true /* bUseHistory */);
}

int UI_GetInstallLanguage(const char *productName)
{
	char *tmp;
	char strInstallLanguage[MAX_PATH];
	int installLanguage = LOCALE_ENGLISH;
	sprintf(strInstallLanguage, "%u", installLanguage); /* default */
	RegistryBackedStrGet(productName, REGKEY_INSTALL_LANGUAGE, SAFESTR(strInstallLanguage), true /* bUseHistory */);
	installLanguage = strtol(strInstallLanguage, &tmp, 10);
	if (strInstallLanguage == tmp)
	{
		// it's possible apparently that the registry key might be there, but be empty - return english in this case.
		return LOCALE_ENGLISH;
	}
	else
	{
		return installLanguage;
	}
}

bool UI_GetHasCoreLocale(void)
{
	return sCoreLocale != NULL;
}

#define REGKEY_AUTO_LAUNCH					"AutoLaunch"

// Should we launch the game as soon as patching is done?
bool UI_SetAutoLaunch(const char *productName, bool bAutoLaunch)
{
	return RegistryBackedBoolSet(productName, REGKEY_AUTO_LAUNCH, bAutoLaunch, true /* bUseHistory */);
}

bool UI_GetAutoLaunch(const char *productName)
{
	bool autoLaunch = false; /* default */
	RegistryBackedBoolGet(productName, REGKEY_AUTO_LAUNCH, &autoLaunch, true /* bUseHistory */);
	return autoLaunch;
}


#include "autogen/NewControllerTracker_pub_h_ast.c"
