#include "options.h"
#include "AutoGen/options_c_ast.h"
#include "LauncherMain.h"
#include "registry.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "GameDetails.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "LauncherLocale.h"
#include "resource_CrypticLauncher.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "Shards.h"
#include "Account.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "UI.h"
#include "UIDefs.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "patcher.h" // for Set/GetVerifyLevel() and Set/GetDisableMicropatching()
#include "browser.h"

// UtilitiesLib
#include "earray.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "SimpleWindowManager.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "Prefs.h"
#include "AppLocale.h"
#include "EString.h"
#include "StringUtil.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "textparserJSON.h"
#include "utf8.h"

// NewControllerTracker
#include "NewControllerTracker_pub.h"

// Microsoft
#include "windef.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "Windowsx.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed
#include "Commctrl.h" // TODO: remove when USE_OLD_OPTIONS==1 code removed

#include <string.h>

AUTO_STRUCT;
typedef struct IntKeyValuePair
{
	int key;
	char *value;	AST(ESTRING)
}
IntKeyValuePair;

AUTO_STRUCT;
typedef struct StringKeyValuePair
{
	char *key;		AST(ESTRING)
	char *value;	AST(ESTRING)
}
StringKeyValuePair;

AUTO_STRUCT AST_SAVE_ORIGINAL_CASE_OF_FIELD_NAMES;
typedef struct CrypticLauncherOptions
{
	int language;
	IntKeyValuePair **languageOptions;
	bool showAllGames;
	bool showTrayIcon;
	bool minimizeToTray;
	bool fastLaunch;
	bool patchOnDemand;
	bool forceVerify;
	int patchProxy;
	IntKeyValuePair **patchProxyOptions;
	bool fullScreen;
	bool safeMode;
	int gameProxy;
	IntKeyValuePair **gameProxyOptions;
	char *commandLine;	AST(ESTRING)
	char *errorMessage;	AST(ESTRING)
	char **fieldsToHide;
	StringKeyValuePair **userData; // stuff that JS doesn't care about, but C code needs when called back in ApplyOptions()
}
CrypticLauncherOptions;

#define FLAG_X 16
#define FLAG_Y 11

typedef struct OptionsStartupData
{
	const ShardInfo_Basic *shard;
	const char *startupProductName;
}
OptionsStartupData;

// these must match the order of AppLocale LocaleTable[]
static int flag_id_map[] = {
	IDB_FLAG_US,
	IDB_FLAG_ZI, // traditional chinese (taiwan)
	IDB_FLAG_KO,
	IDB_FLAG_JA,
	IDB_FLAG_DE,
	IDB_FLAG_FR,
	IDB_FLAG_ES,
	IDB_FLAG_IT,
	IDB_FLAG_RU,
	IDB_FLAG_PL,
	IDB_FLAG_ZH, // simplified chinese (china)
	IDB_FLAG_TR,
	IDB_FLAG_PT
};
static HBITMAP flags[ARRAY_SIZE_CHECKED(flag_id_map)];

// TODO remove when USE_OLD_OPTIONS==1 code is removed
AUTO_RUN;
void LoadFlags(void)
{
	int n = locGetMaxLocaleCount(), i;
	for (i=0; i<n; i++)
	{
		flags[i] = LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(flag_id_map[i]), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
	}
}

// TODO remove when USE_OLD_OPTIONS==1 code is removed
BOOL OptionsPreDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *window)
{
	if (iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST &&
	   wParam == VK_RETURN)
	{
		window->pDialogCB(hDlg, WM_COMMAND, IDOK, 0, window);
		return TRUE;
	}
	return FALSE;
}

// TODO remove when USE_OLD_OPTIONS==1 code is removed
BOOL OptionsDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *window)
{
	const char *productName = NULL;
	const char *productDisplayName = NULL;
	OptionsStartupData * optionsStartupData = window->pUserData;
	const ShardInfo_Basic *shard = NULL;
	U32 gameID = 0;
	char shardRootFolder[MAX_PATH] = {0};

	if (!SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0))
	{
		window->bCloseRequested = true;
		return TRUE;
	}

	//	printf("options msg - %d\n", iMsg);
	if (iMsg == WM_INITDIALOG ||
		iMsg == WM_COMMAND || 
		iMsg == WM_MEASUREITEM ||
		iMsg == WM_DRAWITEM)
	{
		assert(optionsStartupData);
		if (optionsStartupData->shard)
		{
			shard = optionsStartupData->shard;
			productName = shard->pProductName;
			LauncherGetShardRootFolder(productName, shard->pShardCategoryName, SAFESTR(shardRootFolder), false /* bEnsureFolderExists */);
		}
		else
		{
			productName = optionsStartupData->startupProductName;
		}

		gameID = gdGetIDByName(productName);
		productDisplayName = gdGetDisplayName(gameID);
	}

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			HWND handle;
			int i;
			bool fullscreen;
			char commandLineOptionsToAppend[MAX_COMMAND_LINE_OPTIONS_TO_APPEND_CHARS];
			char proxy[MAX_PROXY_CHARS];

			if (gDebugMode)
			{
				printf("Windows code page %u\n", GetACP());
				printf("options window initialized with productName '%s'\n", productName);
			}

			// If in all-games mode, set the title of the dialogs to match the game we are setting options for.
			if (LauncherGetShowAllGamesMode())
			{
				//nameDetails(shard->pProductName, NULL, &dispName);
				SetWindowText_UTF8(hDlg, STACK_SPRINTF(FORMAT_OK(_("%s Options")), productDisplayName));
			}

			// Check the fullscreen box if needed
			handle = GetDlgItem(hDlg, IDC_FULLSCREEN);
			if (shard)
			{
				fullscreen = PrefGetInt(LauncherShardPrefSetGet(shardRootFolder), "GfxSettings.Fullscreen", 1);
				if (fullscreen)
				{
					Button_SetCheck(handle, BST_CHECKED);
				}
			}
			else
			{
				Button_Enable(handle, FALSE);
			}

			// Check the safe mode box if needed
			if (LauncherGetUseSafeMode())
			{
				handle = GetDlgItem(hDlg, IDC_SAFEMODE);
				Button_SetCheck(handle, BST_CHECKED);
			}

			// Set the command line dialog if needed
			LauncherGetCommandLineOptionsToAppend(productName, SAFESTR(commandLineOptionsToAppend));
			handle = GetDlgItem(hDlg, IDC_COMMANDLINE);
			Edit_SetText_UTF8(handle, commandLineOptionsToAppend);

			// Set the tray icon boxes if needed
			if (UI_GetShowTrayIcon(productName))
			{
				handle = GetDlgItem(hDlg, IDC_SHOWTRAY);
				Button_SetCheck(handle, BST_CHECKED);
			}

			if (UI_GetMinimizeTrayIcon(productName))
			{
				handle = GetDlgItem(hDlg, IDC_MINTRAY);
				Button_SetCheck(handle, BST_CHECKED);
			}

			if (UI_GetAutoLaunch(productName))
			{
				handle = GetDlgItem(hDlg, IDC_FASTLAUNCH);
				Button_SetCheck(handle, BST_CHECKED);
			}

			// Check the verify box if needed
			handle = GetDlgItem(hDlg, IDC_VERIFY);
			if (GetVerifyLevel(productName) > 0)
			{
				Button_SetCheck(handle, BST_CHECKED);
			}

			// Populate the language dropdown
			{
				int n = locGetMaxLocaleCount();

				handle = GetDlgItem(hDlg, IDC_LANGUAGE);
				ComboBox_ResetContent(handle);
				for (i=0; i<n; i++)
				{
					if (LauncherIsLocaleAvailable(i))
					{
						ComboBox_SetItemData(handle, ComboBox_AddString_UTF8(handle, locGetDisplayName(i)), i);
					}
				}
				ComboBox_SelectString_UTF8(handle, 0, locGetDisplayName(getCurrentLocale()));
				ComboBox_SetText_UTF8(handle, locGetDisplayName(getCurrentLocale()));
				if (UI_GetHasCoreLocale())
				{
					ComboBox_Enable(handle, FALSE);
				}
			}

			// Select the correct option in the proxy dropdown
			handle = GetDlgItem(hDlg, IDC_PROXY);
			ComboBox_AddString_UTF8(handle, LOC_PROXY_NONE);
			ComboBox_AddString_UTF8(handle, LOC_PROXY_US);
			ComboBox_AddString_UTF8(handle, LOC_PROXY_EU);

#if USE_OLD_OPTIONS
			LauncherGetProxyServer(productName, SAFESTR(proxy));
#else
			LauncherGetGameProxyServer(productName, SAFESTR(proxy));
#endif
			if (strlen(proxy) > 0)
			{
				i = ComboBox_FindStringExact(handle, -1, cgettext(proxy));
				if (i == CB_ERR)
				{
					i = 0;
				}
			}
			else
			{
				i = 0;
			}
			ComboBox_SetCurSel(handle, i);

			// Check the show-all-games box if needed
			if (LauncherGetShowAllGamesMode())
			{
				Button_SetCheck(GetDlgItem(hDlg, IDC_MULTIGAME), BST_CHECKED);
			}

#if USE_OLD_OPTIONS
			// Check the proxy-patching box if needed
			if (LauncherGetProxyPatching(productName))
			{
				Button_SetCheck(GetDlgItem(hDlg, IDC_PROXYPATCH), BST_CHECKED);
			}
#else
			{
				char patchProxyServer[MAX_PROXY_CHARS];
				LauncherGetPatchProxyServer(productName, SAFESTR(patchProxyServer));
				if (stricmp(patchProxyServer, PROXY_NONE) != 0)
				{
					Button_SetCheck(GetDlgItem(hDlg, IDC_PROXYPATCH), BST_CHECKED);
				}
			}
#endif

			// Check the micropatching box if needed
			if (GetDisableMicropatching(productName))
			{
				Button_SetCheck(GetDlgItem(hDlg, IDC_DISABLE_MICROPATCHING), BST_CHECKED);
			}

			return TRUE;
		}
		break;

	case WM_DESTROY:
		SAFE_FREE(window->pUserData);
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_PROXYPATCH:
			switch ( HIWORD(wParam) )
			{
			case BN_CLICKED:
				if (Button_GetCheck((HWND)lParam) == BST_CHECKED)
				{
					int ret = UI_MessageBox(
						_("Enabling this option may reduce patching speed. Are you sure you want to enable it?"),
						_("Are you sure?"),
						MB_YESNO|MB_ICONWARNING);

					if (ret == IDNO)
					{
						Button_SetCheck((HWND)lParam, BST_UNCHECKED);
					}
				}

				{
					// this code ensures the proxy server is set to US if it was None
					char *pProxyText = NULL;
					HWND proxyHandle = GetDlgItem(hDlg, IDC_PROXY);
					ComboBox_GetText_UTF8(proxyHandle, &pProxyText);
					if (stricmp_safe(pProxyText, LOC_PROXY_NONE) == 0)
					{
						int proxyIndex = ComboBox_FindStringExact(proxyHandle, -1, LOC_PROXY_US);
						if (proxyIndex == CB_ERR)
						{
							proxyIndex = 0;
						}
						ComboBox_SetCurSel(proxyHandle, proxyIndex);
					}
					estrDestroy(&pProxyText);
				}

				break;
			}
			break;

		case IDCANCEL:
			// Handler for the red X in the corner
			window->bCloseRequested = true;
			break;

		case IDOK:
			{
				HWND hFullscreen, hSafeMode, hCommandLine, hVerify, handle;
				int len, cursel;
				bool need_restart_warning = false;
				bool minimizeTrayIcon, showTrayIcon;
				char *pCommandLineOptionsToAppend = NULL;
				char prevProxyFromRegistry[MAX_PROXY_CHARS];
				char *pCurProxyFromTextField = NULL;

				if (shard)
				{
					// Record the fullscreen state.
					hFullscreen = GetDlgItem(hDlg, IDC_FULLSCREEN);
					PrefStoreInt(LauncherShardPrefSetGet(shardRootFolder), "GfxSettings.Fullscreen", Button_GetCheck(hFullscreen) == BST_CHECKED ? 1 : 0);
				}

				// Record the safemode setting.
				hSafeMode = GetDlgItem(hDlg, IDC_SAFEMODE);
				LauncherSetUseSafeMode(Button_GetCheck(hSafeMode) == BST_CHECKED);

				// Record the command line setting
				hCommandLine = GetDlgItem(hDlg, IDC_COMMANDLINE);
				len = Edit_GetTextLength(hCommandLine);

				Edit_GetText_UTF8(hCommandLine, &pCommandLineOptionsToAppend);
				LauncherSetCommandLineOptionsToAppend(productName, NULL_TO_EMPTY(pCommandLineOptionsToAppend));
				estrDestroy(&pCommandLineOptionsToAppend);

				// Record the verify state.
				hVerify = GetDlgItem(hDlg, IDC_VERIFY);
				if (Button_GetCheck(hVerify) == BST_CHECKED)
				{
					// is this value new?
					if (SetVerifyLevel(productName, 2) && shard)
					{
						StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
						pCmdData->shard = shard;
						pCmdData->bSetShardToAutoPatch = true;
						pCmdData->localeId = getCurrentLocale();
						patcherQueueRestartPatch(pCmdData);
					}
				}
				else
				{
					SetVerifyLevel(productName, 0);
				}

				// Record the tray icon settings.
				showTrayIcon = Button_GetCheck(GetDlgItem(hDlg, IDC_SHOWTRAY)) == BST_CHECKED;
				UI_SetShowTrayIcon(productName, showTrayIcon);
				minimizeTrayIcon = Button_GetCheck(GetDlgItem(hDlg, IDC_MINTRAY)) == BST_CHECKED;
				UI_SetMinimizeTrayIcon(productName, minimizeTrayIcon);

				UI_ManageTrayIcon(productName);

				UI_SetAutoLaunch(productName, Button_GetCheck(GetDlgItem(hDlg, IDC_FASTLAUNCH)) == BST_CHECKED);

				// Record the proxy setting.
				handle = GetDlgItem(hDlg, IDC_PROXY);

#if USE_OLD_OPTIONS
				LauncherGetProxyServer(productName, SAFESTR(prevProxyFromRegistry));
#else
				LauncherGetGameProxyServer(productName, SAFESTR(prevProxyFromRegistry));
#endif
				// currProxyFromTextField will be localized (e.g. "None" may not be "None" when localized).
				// the value coming from LauncherGetProxyServer() by definition is NOT localized.
				// so, be careful here!
				ComboBox_GetText_UTF8(handle, &pCurProxyFromTextField);
				// if the currProxyFromTextField is different than prevProxyFromRegistry
				if (stricmp_safe(cgettext(prevProxyFromRegistry), pCurProxyFromTextField) != 0)
				{
					// if the value in the text field (proxy) is NOT _("None")
					if (stricmp_safe(pCurProxyFromTextField, LOC_PROXY_NONE) != 0)
					{
						// confirm it with the user that they want the proxy enabled
						int ret = UI_MessageBox(
							_("Using a proxy server may cause connection stability problems. Are you sure you want to enable it?"),
							_("Are you sure?"),
							MB_YESNO|MB_ICONWARNING);

						// if they want it enabled...
						if (ret == IDYES)
						{
							// set the proxy server IN ENGLISH into the registry
							if (stricmp_safe(pCurProxyFromTextField, LOC_PROXY_EU) == 0)
							{
#if USE_OLD_OPTIONS
								LauncherSetProxyServer(productName, PROXY_EU);
#else
								LauncherSetGameProxyServer(productName, PROXY_EU);
#endif
							}
							else if (stricmp_safe(pCurProxyFromTextField, LOC_PROXY_US) == 0)
							{
#if USE_OLD_OPTIONS
								LauncherSetProxyServer(productName, PROXY_US);
#else
								LauncherSetGameProxyServer(productName, PROXY_US);
#endif
							}
							else
							{
								assertmsgf(false, "There should be EU or US in this field, but we found %s!", pCurProxyFromTextField);
							}
						}
					}
					else
					{
#if USE_OLD_OPTIONS
						// we're intentionally passing "None" to this function, rather than the possibly localized version in 'proxy'.
						LauncherSetProxyServer(productName, PROXY_NONE);
#else
						LauncherSetGameProxyServer(productName, PROXY_NONE);
#endif
					}

					estrDestroy(&pCurProxyFromTextField);
				}

				// Record the show-all-games setting.
				if (LauncherSetShowAllGamesMode(Button_GetCheck(GetDlgItem(hDlg, IDC_MULTIGAME)) == BST_CHECKED))
				{
					need_restart_warning = true;
				}

				{
					bool proxyPatchingOn = (Button_GetCheck(GetDlgItem(hDlg, IDC_PROXYPATCH)) == BST_CHECKED) ? true : false;
#if USE_OLD_OPTIONS
					// Record the proxy-patching setting.
					if (LauncherSetProxyPatching(productName, proxyPatchingOn))
					{
						need_restart_warning = true;
					}
#else
					if (proxyPatchingOn)
					{
						// temporarily copy the game proxy server setting to the patch proxy server setting
						char gameProxyServer[MAX_PROXY_CHARS];
						LauncherGetGameProxyServer(productName, SAFESTR(gameProxyServer));
						if (LauncherSetPatchProxyServer(productName, gameProxyServer))
						{
							need_restart_warning = true;
						}
					}
					else
					{
						if (LauncherSetPatchProxyServer(productName, PROXY_NONE))
						{
							need_restart_warning = true;
						}
					}
#endif
				}

				// Record the disable micropatching setting.
				// Don't show the warning dialog if you change the setting from the login page.
				if (SetDisableMicropatching(productName, Button_GetCheck(GetDlgItem(hDlg, IDC_DISABLE_MICROPATCHING)) == BST_CHECKED) &&
					!LauncherIsInLoginState())
				{
					need_restart_warning = true;
				}

				// Display the restart warning if needed
				if (need_restart_warning)
				{
					UI_MessageBox(
						_("You must restart the launcher for this change to take effect."),
						_("Restart required"),
						MB_OK|MB_ICONINFORMATION);
				}

				// Record and activate the language
				// (this must be the last thing we do - as above proxy setting is localized in the OLD language)
				handle = GetDlgItem(hDlg, IDC_LANGUAGE);
				cursel = ComboBox_GetCurSel(handle);
				if (cursel == CB_ERR)
					cursel = 0;
				else
					cursel = ComboBox_GetItemData(handle, cursel);
				UI_SetInstallLanguage(productName, locGetWindowsLocale(cursel));
				UI_UpdateLocale(productName);

				// Close the dialog
				window->bCloseRequested = true;
			}
		}
		break;
	case WM_MEASUREITEM: 
		{
			LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT) lParam; 

			if (lpmis->itemHeight < FLAG_Y + 2) 
				lpmis->itemHeight = FLAG_Y + 2; 
		}
		break; 
	case WM_DRAWITEM: 
		{
			LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT) lParam; 
			COLORREF clrBackground, clrForeground; 
			TEXTMETRIC tm; 
			HDC hdc; 
			int x, y, len;
			char *name;

			if (lpdis->itemID == -1)            // empty item 
				break; 

			// Determine the bitmaps used to draw the icon. 



			// The colors depend on whether the item is selected. 
			clrForeground = SetTextColor(lpdis->hDC, GetSysColor(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)); 
			clrBackground = SetBkColor(lpdis->hDC, GetSysColor(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHT : COLOR_WINDOW)); 

			// Calculate the vertical and horizontal position. 

			GetTextMetrics(lpdis->hDC, &tm); 
			y = (lpdis->rcItem.bottom + lpdis->rcItem.top - 
				tm.tmHeight) / 2; 
			x = LOWORD(GetDialogBaseUnits()) / 4; 

			// Get and display the text for the list item. 
			len = ComboBox_GetLBTextLen(lpdis->hwndItem, lpdis->itemID);
			assert(len);
			name = malloc(len+1);
			ComboBox_GetLBText(lpdis->hwndItem, lpdis->itemID, name);
			assert(name && name[0]);

			ExtTextOut_UTF8(lpdis->hDC, FLAG_X + 2 * x, y, 
				ETO_CLIPPED | ETO_OPAQUE, &lpdis->rcItem, 
				name, len, NULL); 

			// Restore the previous colors. 

			SetTextColor(lpdis->hDC, clrForeground); 
			SetBkColor(lpdis->hDC, clrBackground); 

			// Show the icon. 
			hdc = CreateCompatibleDC(lpdis->hDC); 
			assert(hdc);

			SelectObject(hdc, flags[lpdis->itemData]); 
			y = (lpdis->rcItem.bottom + lpdis->rcItem.top - FLAG_Y) / 2; 
			//BitBlt(lpdis->hDC, x, y, 
			//	FLAG_X, FLAG_Y, hdc, 0, 0, SRCAND); 
			//BitBlt(lpdis->hDC, x, y, 
			//	FLAG_X, FLAG_Y, hdc, 0, 0, SRCPAINT); 
			BitBlt(lpdis->hDC, x, y, 
				FLAG_X, FLAG_Y, hdc, 0, 0, SRCCOPY); 

			DeleteDC(hdc); 

			// If the item has the focus, draw focus rectangle. 

			if (lpdis->itemState & ODS_FOCUS) 
				DrawFocusRect(lpdis->hDC, &lpdis->rcItem); 

			free(name);
		}
		break; 
	}

	return FALSE;
}

// TODO remove when USE_OLD_OPTIONS==1 code is removed
bool OptionsTickFunc(SimpleWindow *pWindow)
{
	return true;
}

void ShowOptions(const ShardInfo_Basic *shard, const char* options_reason)
{
	if (BrowserIsIE() || USE_OLD_OPTIONS)
	{
		SimpleWindow *window;
		OptionsStartupData *optionsStartupData = malloc(sizeof(OptionsStartupData));
		optionsStartupData->shard = shard;
		optionsStartupData->startupProductName = gStartupProductName;
		SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_OPTIONS, 0, IDD_OPTIONS, false, OptionsDialogFunc, OptionsTickFunc, (void*)optionsStartupData); // have to strip const here
		window = SimpleWindowManager_FindWindow(CL_WINDOW_OPTIONS, 0);
		if (window)
		{
			window->pPreDialogCB = OptionsPreDialogFunc;
		}
	}
	else
	{
#if !USE_OLD_OPTIONS
		char *jsonStr = NULL;
		const char *productName;
		CrypticLauncherOptions *launcherOptions;
		IntKeyValuePair *proxyLocation = NULL;
		IntKeyValuePair **proxyOptions = NULL;
		char shardRootFolder[MAX_PATH] = {0};

		if (shard)
		{
			productName = shard->pProductName;
			LauncherGetShardRootFolder(productName, shard->pShardCategoryName, SAFESTR(shardRootFolder), false /* bEnsureFolderExists */);
		}
		else
		{
			productName = gStartupProductName;
		}

		// set up proxy options once, and reuse for game and patch proxy
		proxyLocation = StructCreate(parse_IntKeyValuePair);
		proxyLocation->key = 0;
		proxyLocation->value = estrDup(PROXY_NONE);
		eaPush(&proxyOptions, proxyLocation);
		proxyLocation = StructCreate(parse_IntKeyValuePair);
		proxyLocation->key = 1;
		proxyLocation->value = estrDup(PROXY_US);
		eaPush(&proxyOptions, proxyLocation);
		proxyLocation = StructCreate(parse_IntKeyValuePair);
		proxyLocation->key = 2;
		proxyLocation->value = estrDup(PROXY_EU);
		eaPush(&proxyOptions, proxyLocation);

		launcherOptions = StructCreate(parse_CrypticLauncherOptions);

		launcherOptions->errorMessage = options_reason ? estrDup(options_reason) : NULL;

		// set languageOptions into array, and set language
		if (!UI_GetHasCoreLocale())
		{
			int i,n = locGetMaxLocaleCount();

			launcherOptions->language = getCurrentLocale();

			for (i = 0; i < n; i++)
			{
				if (LauncherIsLocaleAvailable(i))
				{
					IntKeyValuePair *languageOption = StructCreate(parse_IntKeyValuePair);
					languageOption->key = i;
					languageOption->value = estrDup(locGetDisplayName(i));
					eaPush(&launcherOptions->languageOptions, languageOption);
				}
			}
		}

		launcherOptions->showAllGames = LauncherGetShowAllGamesMode();
		launcherOptions->showTrayIcon = UI_GetShowTrayIcon(productName);
		launcherOptions->minimizeToTray = UI_GetMinimizeTrayIcon(productName);
		launcherOptions->fastLaunch = UI_GetAutoLaunch(productName);
		launcherOptions->patchOnDemand = GetDisableMicropatching(productName) ? false : true;
		launcherOptions->forceVerify = GetVerifyLevel(productName) > 0;

		// determine proxyKey (key for proxyServer that is chosen)
		{
			int patchProxyKey = -1;
			char proxyServer[MAX_PROXY_CHARS];
			LauncherGetPatchProxyServer(productName, SAFESTR(proxyServer));
			FOR_EACH_IN_EARRAY(proxyOptions, IntKeyValuePair, proxyLoc)
				if (strcmp(proxyLoc->value, proxyServer) == 0)
				{
					patchProxyKey = proxyLoc->key;
					break;
				}
			FOR_EACH_END
			assertmsgf(patchProxyKey >= 0, "Patch Proxy Server '%s' not found in list", proxyServer);
			launcherOptions->patchProxy = patchProxyKey;
		}

		launcherOptions->patchProxyOptions = proxyOptions;

		if (shard)
		{
			launcherOptions->fullScreen = PrefGetInt(LauncherShardPrefSetGet(shardRootFolder), "GfxSettings.Fullscreen", 1) ? true : false;
		}
		else
		{
			launcherOptions->fullScreen = false;
		}

		launcherOptions->safeMode = LauncherGetUseSafeMode();

		{
			int gameProxyKey = -1;
			char proxyServer[MAX_PROXY_CHARS];
			LauncherGetGameProxyServer(productName, SAFESTR(proxyServer));
			FOR_EACH_IN_EARRAY(proxyOptions, IntKeyValuePair, proxyLoc)
				if (strcmp(proxyLoc->value, proxyServer) == 0)
				{
					gameProxyKey = proxyLoc->key;
					break;
				}
				FOR_EACH_END
					assertmsgf(gameProxyKey >= 0, "Game Proxy Server '%s' not found in list", proxyServer);
				launcherOptions->gameProxy = gameProxyKey;
		}

		launcherOptions->gameProxyOptions = proxyOptions; // note the IMPORTANT note before StructDestroy below.

		// set advanced command line
		{
			char commandLineOptionsToAppend[MAX_COMMAND_LINE_OPTIONS_TO_APPEND_CHARS];
			LauncherGetCommandLineOptionsToAppend(productName, SAFESTR(commandLineOptionsToAppend));
			launcherOptions->commandLine = estrDup(commandLineOptionsToAppend);
		}

		// set userData
		{
			StringKeyValuePair *productNamePair, *shardNamePair, *shardCategoryNamePair;

			productNamePair = StructCreate(parse_StringKeyValuePair);
			shardNamePair = StructCreate(parse_StringKeyValuePair);
			shardCategoryNamePair = StructCreate(parse_StringKeyValuePair);

			productNamePair->key = estrDup("productName");
			productNamePair->value = estrDup(productName);
			eaPush(&launcherOptions->userData, productNamePair);

			if (shard)
			{
				shardNamePair->key = estrDup("shardName");
				shardNamePair->value = estrDup(shard->pShardName);
				eaPush(&launcherOptions->userData, shardNamePair);

				shardCategoryNamePair->key = estrDup("shardCategoryName");
				shardCategoryNamePair->value = estrDup(shard->pShardCategoryName);
				eaPush(&launcherOptions->userData, shardCategoryNamePair);
			}
		}

		ParserWriteJSON(&jsonStr, parse_CrypticLauncherOptions, launcherOptions, WRITEJSON_DONT_WRITE_NEWLINES, 0, 0);

		assert(jsonStr);
		BrowserInvokeScript("LauncherAPI.showOptions", INVOKE_SCRIPT_ARG_STRING_OBJ, jsonStr, INVOKE_SCRIPT_ARG_NULL);

		// IMPORTANT
		// to successfully release memory here, we want to NULL out our second special copy of proxyOptions
		// (so we do not free it twice)
		launcherOptions->gameProxyOptions = NULL;

		StructDestroy(parse_CrypticLauncherOptions, launcherOptions);
#endif
	}
}

bool verifyOKWithInstability(const char *proxyServer, bool *bWeHaveAskedAlready, bool *bVerifyOK)
{
	if (*bWeHaveAskedAlready == false)
	{
		// if the chosen value is not none
		if (strcmp(proxyServer, PROXY_NONE) != 0)
		{
			// confirm it with the user that they want the proxy enabled
			*bVerifyOK = (UI_MessageBox(
				_("Using a proxy server may cause connection stability problems. Are you sure you want to enable it?"),
				_("Are you sure?"),
				MB_YESNO|MB_ICONWARNING) == IDYES) ? true : false;

			*bWeHaveAskedAlready = true;
		}
	}

	return *bVerifyOK;
}

void ApplyOptions(const char *jsonStr)
{
#if !USE_OLD_OPTIONS
	CrypticLauncherOptions *launcherOptions;
	char *result = NULL;
	const char *productName = NULL;
	const char *shardName = NULL;
	const char *shardCategoryName = NULL;
	bool bNeedRestartWarning = false;
	char prevProxyFromRegistry[MAX_PROXY_CHARS];
	bool bWeHaveAskedAlready = false;
	bool bVerifyOK = true;

	launcherOptions = ParserReadJSON(jsonStr, parse_CrypticLauncherOptions, &result);
	FOR_EACH_IN_EARRAY(launcherOptions->userData, StringKeyValuePair, userDataPair)
		if (strcmp(userDataPair->key, "productName") == 0)
		{
			productName = userDataPair->value;
		}
		else if (strcmp(userDataPair->key, "shardName") == 0)
		{
			shardName = userDataPair->value;
		}
		else if (strcmp(userDataPair->key, "shardCategoryName") == 0)
		{
			shardCategoryName = userDataPair->value;
		}
		else
		{
			assertmsgf(false, "unrecognized userData key '%s'", userDataPair->key);
		}
	FOR_EACH_END

	UI_SetInstallLanguage(productName, locGetWindowsLocale(launcherOptions->language));
	UI_UpdateLocale(productName);

	// IGNORE launcherOptions->languageOptions

	if (LauncherSetShowAllGamesMode(launcherOptions->showAllGames))
	{
		bNeedRestartWarning = true;
	}
	UI_SetShowTrayIcon(productName, launcherOptions->showTrayIcon);
	UI_SetMinimizeTrayIcon(productName, launcherOptions->minimizeToTray);
	UI_SetAutoLaunch(productName, launcherOptions->fastLaunch);
	UI_ManageTrayIcon(productName); // must call to manage tray icon state after possible changes
	if (SetDisableMicropatching(productName, launcherOptions->patchOnDemand ? false : true) && !LauncherIsInLoginState())
	{
		// Don't show the warning dialog if you change the setting from the login page.
		bNeedRestartWarning = true;
	}

	if (launcherOptions->forceVerify)
	{
		// is this value new?
		if (SetVerifyLevel(productName, 2) && shardName)
		{
			const ShardInfo_Basic *shard = ShardsGetByProductName(productName, shardName);
			if (shard)
			{
				StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
				pCmdData->shard = shard;
				pCmdData->bSetShardToAutoPatch = true;
				patcherQueueRestartPatch(pCmdData);
			}
		}
	}
	else
	{
		SetVerifyLevel(productName, 0);
	}

	LauncherGetPatchProxyServer(productName, SAFESTR(prevProxyFromRegistry));
	FOR_EACH_IN_EARRAY(launcherOptions->patchProxyOptions, IntKeyValuePair, patchProxyLoc)
		if (patchProxyLoc->key == launcherOptions->patchProxy)
		{
			if (strcmp(prevProxyFromRegistry, patchProxyLoc->value) != 0)
			{
				if (verifyOKWithInstability(patchProxyLoc->value, &bWeHaveAskedAlready, &bVerifyOK))
				{
					if (LauncherSetPatchProxyServer(productName, patchProxyLoc->value))
					{
						bNeedRestartWarning = true;
					}
				}
			}

			break;
		}
	FOR_EACH_END

	if (productName && shardCategoryName)
	{
		char shardRootFolder[MAX_PATH] = {0};
		LauncherGetShardRootFolder(productName, shardCategoryName, SAFESTR(shardRootFolder), false /* bEnsureFolderExists */);
		PrefStoreInt(LauncherShardPrefSetGet(shardRootFolder), "GfxSettings.Fullscreen", launcherOptions->fullScreen ? 1 : 0);
	}

	LauncherSetUseSafeMode(launcherOptions->safeMode);

	LauncherGetGameProxyServer(productName, SAFESTR(prevProxyFromRegistry));
	FOR_EACH_IN_EARRAY(launcherOptions->gameProxyOptions, IntKeyValuePair, gameProxyLoc)
		if (gameProxyLoc->key == launcherOptions->gameProxy)
		{
			if (strcmp(prevProxyFromRegistry, gameProxyLoc->value) != 0)
			{
				if (verifyOKWithInstability(gameProxyLoc->value, &bWeHaveAskedAlready, &bVerifyOK))
				{
					LauncherSetGameProxyServer(productName, gameProxyLoc->value);
				}
			}

			break;
		}
	FOR_EACH_END

	LauncherSetCommandLineOptionsToAppend(productName, NULL_TO_EMPTY(launcherOptions->commandLine));

	// IGNORE launcherOptions->fieldsToHide

	StructDestroy(parse_CrypticLauncherOptions, launcherOptions);

	// Display the restart warning if needed
	if (bNeedRestartWarning)
	{
		UI_MessageBox(
			_("You must restart the launcher for this change to take effect."),
			_("Restart required"),
			MB_OK|MB_ICONINFORMATION);
	}
#endif
}

#include "AutoGen/options_c_ast.c"