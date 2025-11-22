// CrypticLauncher
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "Environment.h"
#include "patcher.h"
#include "LauncherSelfPatch.h"
#include "registry.h"
#include "GameDetails.h"
#include "options.h"
#include "version.h"
#include "resource_CrypticLauncher.h"
#include "History.h"
#include "Shards.h"
#include "Account.h"
#include "UI.h"
#include "browser.h"

// CommonLib
#include "GlobalComm.h"
#include "accountCommon.h"

// UtilitiesLib
#include "utils.h"
#include "earray.h"
#include "cmdparse.h"
#include "sysutil.h"
#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "sock.h"
#include "utilitieslib.h"
#include "trivia.h"
#include "ThreadSafeQueue.h"
#include "timing.h"
#include "MemTrack.h"
#include "file.h"
#include "crypt.h"
#include "bundled_modtimes.h"
#include "systemspecs.h"
#include "GlobalTypes.h"
#include "Prefs.h"
#include "hoglib.h"
#include "fileutil.h"
#include "SimpleWindowManager.h"
#include "UTF8.h"

// NewControllerTracker
#include "NewControllerTracker_Pub.h"

bool gReadPasswordFromSTDIN = false;
AUTO_CMD_INT(gReadPasswordFromSTDIN, readpassword);

static bool sSTDINPasswordIsHashed = false; // this value is used in concert with gReadPasswordFromSTDIN
AUTO_CMD_INT(sSTDINPasswordIsHashed, passwordishashed);

bool gQAMode = false;
AUTO_CMD_INT(gQAMode, qa);

bool gDevMode = false;
AUTO_CMD_INT(gDevMode, dev);

bool gPWRDMode = false;
AUTO_CMD_INT(gPWRDMode, pwrd);

bool gPWTMode = false;
AUTO_CMD_INT(gPWTMode, pwt);

bool gDebugMode = false;
AUTO_CMD_INT(gDebugMode, launcher_debug) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

bool gForceInvalidLocale = false;
AUTO_CMD_INT(gForceInvalidLocale, force_invalid_locale);

static bool sbCrashMe = false;
AUTO_CMD_INT(sbCrashMe, crash);

static bool sUpdateLibcef = true;
AUTO_CMD_INT(sUpdateLibcef, updatelibcef) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

extern int g_force_sockbsd; // from netcomm

// Command queues to and from the PCL thread
XLOCKFREE_HANDLE gQueueToPatchClient = NULL;
XLOCKFREE_HANDLE gQueueToUI = NULL;

// The primary launcher product name - this is set on boot of the launcher, and never changed, even when selecting shards.
const char *gStartupProductName = NULL;

// The current state of the launcher process
static CRITICAL_SECTION sStateMutex = {0};
static CrypticLauncherState sState = CL_STATE_START;

// Original command line
static char *sCommandLine = NULL;

// PW Arc login bypass password
int gBypassPipe = 0;

// PW conflict resolution URL (relative), provided by website
// this is combined with sBaseURL to form a complete URL.
static char *sRelativeConflictURL = NULL;

// Currently displayed URL
static char *sDisplayedURL = NULL;

// this is the shard that LauncherRunGame() will launch
static ShardInfo_Basic *sShardToRun = NULL;

// Should we start the next game with -safemode?
static bool sbUseSafeMode = false;

static char *sCommandProvidedBaseURL = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(baseurl); void cmd_baseurl(char *s) { sCommandProvidedBaseURL = strdup(s); }

// PW Core Client login bypass
// Design description, from email conversation with Shen Hui on 2012-06-21:
//  1.	User logs into CORE with PWRD account name and password
// 	2.	User tries to launch NeverWinter from CORE
// 	3.	CORE creates an anonymous pipe and put MD5(PWRD account name + password) into pipe buffer
// 	4.	CORE launches game with the handle of the anonymous pipe through command line
// 	5.	Game launcher parses pipe handle from command line and use the handle to read MD5(PWRD account name + password) from pipe buffer
//  I think the exact spelling of the option should be:
//    -Bypass <accountname> <handle id>
//  It would be better if we can make the option string case-insensitive.
AUTO_COMMAND ACMD_HIDE;
void Bypass(char *account, char *handle)
{
	void *ptr;
	char dummy;
	int count;

	// Get account name.
	if (account && *account)
	{
		AccountSetUsername(account);
	}
	
	// Get pipe handle.
	gBypassPipe = 0;
	count = sscanf(handle, "%p%c", &ptr, &dummy);
	if (count == 1)
	{
		gBypassPipe = (int)ptr;
	}
}

static bool CrypticLauncherTickFunc(SimpleWindow *pMeaninglessWindow)
{
	static int bInside = false;
	static int counter = 0;

	if (!bInside)
	{
		static int frameTimer;
		F32 frametime;

		bInside = true;

		if (!frameTimer)
			frameTimer = timerAlloc();
		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);	
		counter++;
		bInside =false;
	}
	
	ShardsAlwaysTickFunc();
	if (LauncherGetState() == CL_STATE_LOGGEDIN)
	{
		ShardsLoggedInTickFunc();
	}

	BrowserUpdate();

	// this indicates that we will continue running the program
	return true;
}

static char *sProductNameOverride = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0); void productOverride(char *product) { sProductNameOverride = strdup(product); }

//note that for standalone mode, the "short product name" is never used, so we just set it to XX
static void GetGameID(U32 *gameID, const char **productName)
{
	if (sProductNameOverride)
	{
		*productName = sProductNameOverride;
		*gameID = gdGetIDByName(sProductNameOverride);
		SetProductName(sProductNameOverride, gdGetCode(*gameID));
	}
	else
	{
		char exeName[MAX_PATH];
		getFileNameNoExtNoDirs(exeName, getExecutableName());
		*gameID = gdGetIDByExecutable(exeName);
		*productName = gdGetName(*gameID);
		SetProductName(*productName, gdGetCode(*gameID));
	}
}

static const char* sStateString[] = 
{
	"Start",				// CL_STATE_START,
	"LoadingPageLoaded",	// CL_STATE_LOADINGPAGELOADED
	"LoginPageLoaded",		// CL_STATE_LOGINPAGELOADED,
	"LoggingIn",			// CL_STATE_LOGGINGIN,
	"LoggedIn",				// CL_STATE_LOGGEDIN,
	"GotShards",			// CL_STATE_GOTSHARDS,
	"GettingPageTicket",	// CL_STATE_GETTINGPAGETICKET,
	"GotPageTicket",		// CL_STATE_GOTPAGETICKET,
	"LauncherPageLoaded",	// CL_STATE_LAUNCHERPAGELOADED,
	"SettingView",			// CL_STATE_SETTINGVIEW,
	"WaitingForPatch",		// CL_STATE_WAITINGFORPATCH,
	"GettingFiles",			// CL_STATE_GETTINGFILES,
	"Ready",				// CL_STATE_READY,
	"GettingGameTicket",	// CL_STATE_GETTINGGAMETICKET,
	"Error",				// CL_STATE_ERROR,
	"LoggingInAfterLink",	// CL_STATE_LOGGINGINAFTERLINK,		// Website performed linking, proceeding with login like CL_STATE_LOGGINGIN
	"Linking",				// CL_STATE_LINKING,					// Website is performing linking, ignore page loads
};

static void enter(void)
{
	ATOMIC_INIT_BEGIN;

	InitializeCriticalSection(&sStateMutex); 

	ATOMIC_INIT_END;

	EnterCriticalSection(&sStateMutex);
}

static void leave(void)
{
	LeaveCriticalSection(&sStateMutex);
}

void LauncherSetState(CrypticLauncherState newState)
{
	if (gDebugMode)
	{
		printf("STATE TRANSITION --- '%s' to '%s'\n", sStateString[sState], sStateString[newState]);
	}
	enter();
	sState = newState;
	leave();
}

// Some states (currently those set by getting login tickets, which can be triggered while 
// patching is active by a language change or any other page reload) should not be changed 
// by patching states in order to not disrupt their expected flow.  Once it completes the main 
// thread will need to send a patching command or otherwise make sure patching can continue.
bool LauncherSetPatchingState(CrypticLauncherState newState)
{
	bool stateChanged = false;

	if (gDebugMode)
	{
		printf("STATE TRANSITION FROM PATCHING --- '%s' to '%s'\n", sStateString[sState], sStateString[newState]);
	}
	enter();
	if (sState != CL_STATE_GETTINGPAGETICKET && sState != CL_STATE_GOTPAGETICKET)
	{
		sState = newState;
		stateChanged = true;
	}
	leave();
	if (gDebugMode && !stateChanged)
	{
		printf("STATE TRANSITION FROM PATCHING STOPPED, STATE NOT CHANGED\n");
	}
	
	return stateChanged;
}

CrypticLauncherState LauncherGetState(void)
{
	CrypticLauncherState retVal;
	enter();
	retVal = sState;
	leave();
	return retVal;
}

bool LauncherIsInLoginState(void)
{
	return LauncherGetState() <= CL_STATE_LOGINPAGELOADED;
}

bool LauncherIsInLoggedInState(void)
{
	return LauncherGetState() >= CL_STATE_LOGGEDIN;
}

// Extra command-line arguments to pass to GameClient
#define REGKEY_LAUNCHER_COMMAND_LINE		"LauncherCommandLine"

bool LauncherSetCommandLineOptionsToAppend(const char *productName, const char *commandLine)
{
	return RegistryBackedStrSet(productName, REGKEY_LAUNCHER_COMMAND_LINE, commandLine, true /* bUseHistory */);
}

bool LauncherGetCommandLineOptionsToAppend(const char *productName, char *commandLine, int commandLineMaxLength)
{
	*commandLine = 0; /* default */
	return RegistryBackedStrGet(productName, REGKEY_LAUNCHER_COMMAND_LINE, commandLine, commandLineMaxLength, true /* bUseHistory */);
}

void LauncherSetDisplayedURL(const char *url)
{
	SAFE_FREE(sDisplayedURL);
	sDisplayedURL = strdup(url);
}

const char *LauncherGetDisplayedURL(void)
{
	return sDisplayedURL;
}

bool LauncherIsDisplayingURL(const char *url)
{
	if (sDisplayedURL)
	{
		return stricmp(sDisplayedURL, url) == 0;
	}

	return false;
}

bool LauncherIsDisplayingLoginURL(void)
{
	return LauncherIsDisplayingURL("/" URL_SUFFIX_LOGIN);
}

bool LauncherIsDisplayingPostLoginURL(void)
{
	return LauncherIsDisplayingURL("/" URL_SUFFIX_LAUNCHER);
}

bool LauncherIsDisplayingPrepatchURL(void)
{
	return LauncherIsDisplayingURL("/" URL_SUFFIX_PREPATCH);
}

// Set or reset the launcher's base URL.
static const char* getBaseURL(void)
{
	if (sCommandProvidedBaseURL)
	{
		return sCommandProvidedBaseURL;
	}
	else
	{
		U32 gameID = gdGetIDByName(gStartupProductName);

		if (gQAMode)
		{
			return gdGetQALauncherURL(gameID);
		}
		else if (gDevMode)
		{
			return gdGetDevLauncherURL(gameID);
		}
		else if (gPWRDMode)
		{
			return gdGetPWRDLauncherURL(gameID);
		}
		else if (gPWTMode)
		{
			return gdGetPWTLauncherURL(gameID);
		}
		else
		{
			return gdGetLauncherURL(gameID);
		}
	}
}

void LauncherGetMainURL(char **url)
{
	estrPrintf(url, "%s" URL_SUFFIX_LAUNCHER LAUNCHER_URL_WEB_PROTOCOL_VERSION_STR, getBaseURL());
}

void LauncherGetLoginURL(char **url)
{
	estrPrintf(url, "%s" URL_SUFFIX_LOGIN LAUNCHER_URL_WEB_PROTOCOL_VERSION_STR, getBaseURL());
}

void LauncherGetLoginOrPrepatchURL(char **url)
{
	const char *baseURL = getBaseURL();

	if (gPrePatchShard)
	{
		estrPrintf(url, "%s" URL_SUFFIX_PREPATCH LAUNCHER_URL_WEB_PROTOCOL_VERSION_STR, baseURL);
	}
	else
	{
		estrPrintf(url, "%s" URL_SUFFIX_LOGIN LAUNCHER_URL_WEB_PROTOCOL_VERSION_STR, baseURL);
	}
}

void LauncherSetRelativeConflictURL(const char *url)
{
	SAFE_FREE(sRelativeConflictURL);
	sRelativeConflictURL = strdup(url);
}

bool LauncherHasConflictURL(void)
{
	return sRelativeConflictURL && sRelativeConflictURL[0];
}

// Create a conflict URL.
void LauncherGetConflictURL(char **url, U32 ticket, const char *prefix)
{
	char username[MAX_LOGIN_FIELD];
	const char *baseURL = getBaseURL();

	// Convert a partial conflict path into a complete URL.
	if (baseURL &&
		baseURL[0] &&
		!(strStartsWith(sRelativeConflictURL, URL_PREFIX_HTTP) ||
		  strStartsWith(sRelativeConflictURL, URL_PREFIX_HTTPS)))
	{
		estrAppend2(url, baseURL);
		if (sRelativeConflictURL[0] == '/')
		{
			estrRemove(url, estrLength(url) - 1, 1);
		}
	}

	// Add actual path or URL.
	estrAppend2(url, sRelativeConflictURL);

	// Append flags.
	AccountGetUsername(SAFESTR(username));
	estrConcatf(url, LAUNCHER_URL_WEB_PROTOCOL_VERSION_STR "&pwaccountname=%s&ticket=%s%u", username, NULL_TO_EMPTY(prefix), ticket);
}

#if USE_OLD_OPTIONS

#define REGKEY_PROXY_PATCHING				"ProxyPatching"

bool LauncherSetProxyPatching(const char *productName, bool bProxyPatching)
{
	return RegistryBackedBoolSet(productName, REGKEY_PROXY_PATCHING, bProxyPatching, true /* bUseHistory */);
}

bool LauncherGetProxyPatching(const char *productName)
{
	bool bProxyPatching = false; /* default */
	RegistryBackedBoolGet(productName, REGKEY_PROXY_PATCHING, &bProxyPatching, true /* bUseHistory */);
	return bProxyPatching;
}

#endif

#define BUILD_ROOT_FOLDER_FROM_INSTALL_FOLDER(rootFolder, rootFolderMaxLength, installFolder, productDisplayName, shardCategoryName) \
	snprintf_s(rootFolder, rootFolderMaxLength, "%s/%s/%s", installFolder, productDisplayName, shardCategoryName)

// LauncherGetShardRootFolder()
// Purpose: Figure out where the root folder is
// Method:
//
// Determine the installFolder.
//   if REGKEY_INSTALL_LOCATION is NOT set for the provided shard product...
//     look at the cwd, and see if there is rootFolder for this shard relative to that.
//        if there is no rootFolder for this shard there, then assume that the executable directory for the Launcher is the installFolder
//   If REGKEY_INSTALL_LOCATION is set for the provided shard product, then that is the installFolder.
// rootFolder is relative to the installFolder - <installFolder>/<product display name>/<shard category name>

#define REGKEY_INSTALL_LOCATION				"InstallLocation"

void LauncherGetShardRootFolder(const char *productName, const char *shardCategoryName, char *rootFolder, int rootFolderMaxLength, bool bEnsureFolderExists)
{
	char installFolder[MAX_PATH];
	bool bRootFolderExistsAlready = false;
	const char *productDisplayName;

	assert(productName);
	assert(shardCategoryName);

	// Find the product display name
	productDisplayName = gdGetDisplayName(gdGetIDByName(productName));

	// First look at the registry key
	if (!readRegStr(productName, REGKEY_INSTALL_LOCATION, installFolder, MAX_PATH, false)) // this value is never written by the launcher - only read
	{
		// we DON'T have the installer-set registry key!

		// need to deduce the rootFolder

		// #1 - try the current working directory
		assert(fileGetcwd(installFolder, ARRAY_SIZE_CHECKED(installFolder)));

		// build a rootFolder path using the cwd, product display name, and shard category name
		BUILD_ROOT_FOLDER_FROM_INSTALL_FOLDER(rootFolder, rootFolderMaxLength, installFolder, productDisplayName, shardCategoryName);

		// if that manually built directory does not exist...
		bRootFolderExistsAlready = dirExists(rootFolder);

		if (!bRootFolderExistsAlready)
		{
			// #2 - assume the directory in which the Launcher executable resides
			getExecutableDir(installFolder);

			// build a rootFolder path using the Launcher executable directory, product display name, and shard category name
			BUILD_ROOT_FOLDER_FROM_INSTALL_FOLDER(rootFolder, rootFolderMaxLength, installFolder, productDisplayName, shardCategoryName);
		}
	}
	else
	{
		// build a rootFolder path using the reg key specified install directory, product display name, and shard category name
		BUILD_ROOT_FOLDER_FROM_INSTALL_FOLDER(rootFolder, rootFolderMaxLength, installFolder, productDisplayName, shardCategoryName);
	}

	forwardSlashes(rootFolder);

	if (bEnsureFolderExists && !bRootFolderExistsAlready && !dirExists(rootFolder))
	{
		// make the rootFolder we just constructed
		mkdirtree_const(rootFolder);
	}
}

int LauncherShardPrefSetGet(const char *shardRootFolder)
{
	return PrefSetGet(STACK_SPRINTF("%s/localdata/gameprefs.pref", shardRootFolder));
}

#define REGKEY_PROXY_CLEAR_ONE_SHOT			"ProxyClearOneShot"

static bool setProxyClearOneShot(const char *productName, bool bProxyClearOneShot)
{
	return RegistryBackedBoolSet(productName, REGKEY_PROXY_CLEAR_ONE_SHOT, bProxyClearOneShot, true /* bUseHistory */);
}

static bool getProxyClearOneShot(const char *productName)
{
	bool bProxyClearOneShot = false; /* default */
	RegistryBackedBoolGet(productName, REGKEY_PROXY_CLEAR_ONE_SHOT, &bProxyClearOneShot, true /* bUseHistory */);
	return bProxyClearOneShot;
}

// NOTE: The value passed in to and returned from these functions should NOT be localized.
// "None" for 'no proxy server' is a case to watch for.
#define REGKEY_PROXY						"Proxy"

#if USE_OLD_OPTIONS

bool LauncherSetProxyServer(const char *productName, const char *proxyServer)
{
	return RegistryBackedStrSet(productName, REGKEY_PROXY, proxyServer, true /* bUseHistory */);
}

bool LauncherGetProxyServer(const char *productName, char *proxyServer, int proxyServerMaxLength)
{
	strncpy_s(proxyServer, proxyServerMaxLength, PROXY_NONE, strlen(PROXY_NONE)); /* default */
	return RegistryBackedStrGet(productName, REGKEY_PROXY, proxyServer, proxyServerMaxLength, true /* bUseHistory */);
}

#else

#define REGKEY_PATCH_PROXY					"PatchProxy"

bool LauncherSetGameProxyServer(const char *productName, const char *proxyServer)
{
	return RegistryBackedStrSet(productName, REGKEY_PROXY, proxyServer, true /* bUseHistory */);
}

bool LauncherGetGameProxyServer(const char *productName, char *proxyServer, int proxyServerMaxLength)
{
	strncpy_s(proxyServer, proxyServerMaxLength, PROXY_NONE, strlen(PROXY_NONE)); /* default */
	return RegistryBackedStrGet(productName, REGKEY_PROXY, proxyServer, proxyServerMaxLength, true /* bUseHistory */);
}

bool LauncherSetPatchProxyServer(const char *productName, const char *proxyServer)
{
	return RegistryBackedStrSet(productName, REGKEY_PATCH_PROXY, proxyServer, true /* bUseHistory */);
}

bool LauncherGetPatchProxyServer(const char *productName, char *proxyServer, int proxyServerMaxLength)
{
	strncpy_s(proxyServer, proxyServerMaxLength, PROXY_NONE, strlen(PROXY_NONE)); /* default */
	return RegistryBackedStrGet(productName, REGKEY_PATCH_PROXY, proxyServer, proxyServerMaxLength, true /* bUseHistory */);
}

#endif

void LauncherChooseEUProxyHost(char *proxyHost, int proxyHostMaxLength)
{
	int server = time(NULL) % 2;
	if (server)
	{
		strncpy_s(proxyHost, proxyHostMaxLength, ENV_EU1_PROXY_HOST, strlen(ENV_EU1_PROXY_HOST));
	}
	else
	{
		strncpy_s(proxyHost, proxyHostMaxLength, ENV_EU2_PROXY_HOST, strlen(ENV_EU2_PROXY_HOST));
	}
}

void LauncherFormShardDescriptor(char *shardDescriptor, size_t shardDescriptorMaxSize, const char *productName, const char *shardName)
{
	snprintf_s(shardDescriptor, shardDescriptorMaxSize, "%s:%s", productName, shardName);
}

#define REGKEY_SHOW_ALL_GAMES				"ShowAllGames"

// returns true if changed, false if not
bool LauncherSetShowAllGamesMode(bool bShowAllGamesMode)
{
	return RegistryBackedBoolSet(gStartupProductName, REGKEY_SHOW_ALL_GAMES, bShowAllGamesMode, true /* bUseHistory */);
}

bool LauncherGetShowAllGamesMode()
{
	bool showAllGamesMode = false; /* default */
	RegistryBackedBoolGet(gStartupProductName, REGKEY_SHOW_ALL_GAMES, &showAllGamesMode, true /* bUseHistory */);
	return showAllGamesMode;
}

void LauncherSetUseSafeMode(bool bUseSafeMode)
{
	sbUseSafeMode = bUseSafeMode;
}

bool LauncherGetUseSafeMode(void)
{
	return sbUseSafeMode;
}

static BOOL launcherConsoleCloseHandler(DWORD fdwCtrlType)
{
	EXCEPTION_HANDLER_BEGIN

	switch (fdwCtrlType)
	{
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
	case CTRL_CLOSE_EVENT: 
	case CTRL_BREAK_EVENT: 
	case CTRL_C_EVENT: 
		// Post the message that the x button in the window itself would post.
		// - Just post rather than send so it happens in the expected thread.
		PostMessage(SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0)->hWnd, WM_COMMAND, IDCANCEL, 0);

		// If this event will force the console closed, give time for CEF to process this message before returning.
		// - Windows will force this window closed after 5s, so wait as long as able.
		// - This will probably close sooner, as the main window closing will also force this closed.
		if (fdwCtrlType != CTRL_BREAK_EVENT && fdwCtrlType != CTRL_C_EVENT)
			Sleep(5000);

		return TRUE;

	// Pass other signals to the next handler.
	default: 
		return FALSE; 
	}

	EXCEPTION_HANDLER_END
}

void LauncherCreateConsoleWindow()
{
	newConsoleWindow();
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)launcherConsoleCloseHandler, TRUE);
	showConsoleWindow();
}

void LauncherSetShardToRun(const ShardInfo_Basic *shard)
{
	sShardToRun = (ShardInfo_Basic *)shard;
}

bool LauncherRunGame(U32 accountID, U32 accountTicketID, char **error)
{
	if (sShardToRun)
	{
		char *commandline = NULL, cwd[MAX_PATH], regname[512];
		char commandLineOptionsToAppend[MAX_COMMAND_LINE_OPTIONS_TO_APPEND_CHARS];
		char proxy[MAX_PROXY_CHARS];
		char username[MAX_LOGIN_FIELD];
		char successfulPatchRootFolder[MAX_PATH];
		int i, pid;
		const char *productNameToRun = sShardToRun->pProductName;
		const char *shardNameToRun = sShardToRun->pShardName;
		const char *shardCategoryNameToRun = sShardToRun->pShardCategoryName;
		char shardRootFolder[MAX_PATH];
		bool local = gdIsLocalProductName(productNameToRun);

		// If the language auto-changed from changing shard in show all games it won't have updated the registry
		UI_SetInstallLanguage(productNameToRun, locGetWindowsLocale(getCurrentLocale()));

		sprintf(regname, "%s:%s:%s", productNameToRun, shardNameToRun, shardCategoryNameToRun);
		HistoryAddItemAndSaveToRegistry(regname);

		PatcherGetSuccessfulPatchRootFolder(SAFESTR(successfulPatchRootFolder));
		printf("successfulPatchRootFolder = %s\n", successfulPatchRootFolder);

		// Verify the root folder is correct
		LauncherGetShardRootFolder(productNameToRun, shardCategoryNameToRun, SAFESTR(shardRootFolder), false /* bEnsureFolderExists */);
		assertmsgf(stricmp(successfulPatchRootFolder, shardRootFolder)==0, "Patched to \"%s\", but attempting to launch from \"%s\"", successfulPatchRootFolder, shardRootFolder);

		estrCreate(&commandline);
		estrConcatf(&commandline, "\"%s/GameClient.exe\" %s -AuthTicketNew %u %u -Locale %s %s",
			//estrConcatf(&commandline, "%s/GameClient.exe %s %s",
			local ? STACK_SPRINTF("C:/src/%s/bin", productNameToRun) : successfulPatchRootFolder,
			LauncherGetUseSafeMode() ? "-safemode" : "",
			accountID, accountTicketID,
			locGetName(getCurrentLocale()),
			sShardToRun->pAutoClientCommandLine ? sShardToRun->pAutoClientCommandLine : "");
		if (!local)
		{
			if (eaiSize(&sShardToRun->allLoginServerIPs))
			{
				for (i=eaiSize(&sShardToRun->allLoginServerIPs)-1; i>=0; i--)
				{
					U32 ip = sShardToRun->allLoginServerIPs[i];
					estrConcatf(&commandline, " -server %d.%d.%d.%d", ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255);
				}
			}
			else
			{
				estrConcatf(&commandline, " -server %s", sShardToRun->pShardLoginServerAddress);
			}

			if (sShardToRun->pLoginServerPortsAndIPs && eaSize(&sShardToRun->pLoginServerPortsAndIPs->ppPortIPPairs))
			{
				FOR_EACH_IN_EARRAY(sShardToRun->pLoginServerPortsAndIPs->ppPortIPPairs, PortIPPair, pPair)
				{
					estrConcatf(&commandline, " -?LoginServerShardIPAndPort %s %s %d  ", sShardToRun->pShardName, makeIpStr(pPair->iIP), pPair->iPort);
				}
				FOR_EACH_END;
			}
		}
		else
		{
			estrConcatf(&commandline, " -ConnectToController");
		}

		// If we have prepatch data, send it on to the client
		if (sShardToRun->pPrePatchCommandLine && sShardToRun->pPrePatchCommandLine[0] && !strstri(sShardToRun->pPrePatchCommandLine, "-name HOLD"))
		{
			char *prepatch = NULL, *prepatch_esc = NULL, *prepatch_str = NULL, *prepatch_name = NULL, *prepatch_cwd = NULL;
			// Generate the full command-line to execute after the gameclient finishes
			estrSuperEscapeString(&prepatch_name, shardNameToRun);
			estrSuperEscapeString(&prepatch_str, sShardToRun->pPrePatchCommandLine);
			assert(fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)));
			estrSuperEscapeString(&prepatch_cwd,cwd);
			estrPrintf(&prepatch, "%s %s -prepatch %s %s %s %s %s", getExecutableName(), NULL_TO_EMPTY(sCommandLine), productNameToRun, prepatch_name, sShardToRun->pShardCategoryName, prepatch_str, prepatch_cwd);
			// Append the now double-superescaped data to the gameclient command line
			estrSuperEscapeString(&prepatch_esc, prepatch);
			estrConcatf(&commandline, " -superesc prepatch %s", prepatch_esc);
			estrDestroy(&prepatch);
			estrDestroy(&prepatch_esc);
			estrDestroy(&prepatch_str);
			estrDestroy(&prepatch_name);
			estrDestroy(&prepatch_cwd);
		}

		AccountGetUsername(SAFESTR(username));

		// Special case for the standalone creator client to make it not die on a huge number of missing textures
		if (stricmp(shardCategoryNameToRun, SHARD_CATEGORY_AVATAR)==0)
		{
			estrConcatf(&commandline, " -CostumesOnly -AuthTicket %u %u %s", accountID, accountTicketID, username);
		}

		if (username[0])
		{
			char *escapedUserName = NULL;
			estrSuperEscapeString(&escapedUserName, username);
			estrConcatf(&commandline, " -SuperEsc setUsername %s", escapedUserName);
			estrDestroy(&escapedUserName);
		}

#if USE_OLD_OPTIONS
		LauncherGetProxyServer(productNameToRun, SAFESTR(proxy));
#else
		LauncherGetGameProxyServer(productNameToRun, SAFESTR(proxy));
#endif

		if (strlen(proxy) > 0)
		{
			if (stricmp(proxy, PROXY_US) == 0)
			{
				estrConcatf(&commandline, " -SetProxy " ENV_US_PROXY_HOST " 80");
			}
			else if (stricmp(proxy, PROXY_EU) == 0)
			{
				char euProxyHost[MAX_PROXY_CHARS];

				LauncherChooseEUProxyHost(SAFESTR(euProxyHost));

				estrConcatf(&commandline, " -SetProxy %s 80", euProxyHost);
			}
			// else it will be set to PROXY_NONE
		}

		LauncherGetCommandLineOptionsToAppend(productNameToRun, SAFESTR(commandLineOptionsToAppend));
		if (strlen(commandLineOptionsToAppend) > 0)
		{
			estrConcatf(&commandline, " %s", commandLineOptionsToAppend);
		}

		PrefStoreInt(LauncherShardPrefSetGet(shardRootFolder), "Locale", getCurrentLocale());

		// Touch dynamic.hogg so we know it exists
		{
			HogFile *dynamic_hogg;
			char dynamic_path[MAX_PATH];
			sprintf(dynamic_path, "%s/piggs/dynamic.hogg", successfulPatchRootFolder);
			dynamic_hogg = hogFileRead(dynamic_path, NULL, PIGERR_PRINTF, NULL, HOG_DEFAULT);
			hogFileDestroy(dynamic_hogg, true);
		}

		fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)-1);
		assert(chdir(local ? STACK_SPRINTF("C:/src/%s/bin", productNameToRun) : successfulPatchRootFolder) == 0);
		pid = system_detach(commandline, false, false);
		*error = pid ? NULL : lastWinErr();	// Localized by Windows using getCurrentLocale()
		assert(chdir(cwd) == 0);
		estrDestroy(&commandline);
		LauncherSetShardToRun(NULL);
		return pid != 0;
	}
	else
	{
		*error = "There is no shard to run!";
		return false;
	}
}

static bool historyItemCallbackFunc(char *hist, void *userData)
{
	char ***patchExtraFolders = (char ***)userData;
	char *productName = strdup(hist), *shardCategoryName;
	char extraFolder[MAX_PATH];
	shardCategoryName = strchr(productName, ':');
	assert(shardCategoryName);
	*shardCategoryName = '\0';
	shardCategoryName = strrchr(hist, ':');
	assert(shardCategoryName);
	shardCategoryName += 1;
	LauncherGetShardRootFolder(productName, shardCategoryName, SAFESTR(extraFolder), false /* bEnsureFolderExists */);
	eaPush(patchExtraFolders, strdup(extraFolder));
	free(productName);
	return true;
}

static void unpackIfRequired(const char *dest_filename, int source_resource_id, __time32_t source_timestamp, bool fatal_on_failure)
{
	if (!fileExists(dest_filename) || fileLastChanged(dest_filename) != source_timestamp)
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(source_resource_id), L"FILE");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				FILE *pExeFile;
				void *pExeData = LockResource(gptr);
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);
				size_t iWrittenBytes = 0;

				pExeFile = fopen(dest_filename, "wb");
				if (!pExeFile && fatal_on_failure)
				{
					if (fileExists(dest_filename))
					{
						char delete_me[MAX_PATH];
						sprintf(delete_me, "%s.deleteme", dest_filename);
						if (fileExists(delete_me) && unlink(delete_me) != 0)
						{
							FatalErrorf("Could not delete previously renamed file (%s)", delete_me);
						}
						else if (!MoveFile_UTF8(dest_filename, delete_me))
						{
							const char *error = lastWinErr();
							FatalErrorf("Could not rename file that could not be overwritten (%s): %s", dest_filename, error);
						}
						else
						{
							pExeFile = fopen(dest_filename, "wb");

							if (!pExeFile)
							{
								FatalErrorf("Could not open new file for writing (%s)", dest_filename);
							}
						}
					}
					else
					{
						FatalErrorf("Cannot write embedded file (%s)", dest_filename);
					}
				}

				if (pExeFile)
				{
					int tries = 0;

					while (tries < 5 && iWrittenBytes != iExeSize)
					{
						iWrittenBytes += fwrite((unsigned char *)pExeData + iWrittenBytes, 1, iExeSize - iWrittenBytes, pExeFile);
						tries += 1;
					}

					fclose(pExeFile);
					fileSetTimestamp(dest_filename, source_timestamp);
				}

				if (pExeFile && iWrittenBytes != iExeSize)
				{
					FatalErrorf("Could not write entire file %s! (%u/%u)", dest_filename, iWrittenBytes, iExeSize);
				}
				
				UnlockResource(gptr);
				FreeResource(gptr);
			}
			else
			{
				FatalErrorf("Cannot load resource ID %d for embedded file (%s)", source_resource_id, dest_filename);
			}
		}
		else
		{
			FatalErrorf("Cannot find resource ID %d for embedded file (%s)", source_resource_id, dest_filename);
		}
	}
}

int APIENTRY wWinMain(HINSTANCE hInstance,
					 HINSTANCE hPrevInstance,
					 WCHAR*    pWideCmdLine,
					 int       nCmdShow)
{
	extern char prodVersion[256];
	int errorlevel = 0;
	char path[CRYPTIC_MAX_PATH];
	char *tokCmdLine;
	int argc = 0, oldargc;
	char *args[1000];
	char **argv = args;
	char buf[1024]={0};
	U32 gameID;
	const char *productName;
	char **patchExtraFolders = NULL;

	EXCEPTION_HANDLER_BEGIN
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);	
	WAIT_FOR_DEBUGGER_LPCMDLINE


	// *** Early startup ***
	// Only do what we must do before autoupdate.  If anything crashes here, the customer won't be able to autoupdate to a new version that
	// fixes the bug.
	// Try to start things in an order that is sensitive to crash-handling; try to get CrypticError extracted as soon as possible.

	// Before doing anything else, set the build name.
	strcpy(prodVersion, BUILD_CRYPTICSTAMP);

	// Start out in this mode in case we get crashes during auto run time. Set to a saner mode further down for 
	// dev mode, etc.
	setProductionClientAssertMode();

	// Initialize memory system.
	memMonitorInit();

	// Reserve large memory chunk to be able to handle large files.
	memTrackReserveMemoryChunk(1024*1024*1024);

	// Always run in production mode.
	setDefaultProductionMode(1);


	// *** Auto runs ****
	DO_AUTO_RUNS


	// Initialize filesystem.
	gimmeDLLDisable(1);
	fileAllPathsAbsolute(1);
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);

	// Parse the command line
	// FIXME: This should use the generic commandline-from-file support, rather than this questionable hack.
	tokCmdLine = strdup(lpCmdLine);
	loadCmdline("./cmdline.txt",buf,sizeof(buf));
	args[0] = getExecutableName();
	oldargc = 1 + tokenize_line_quoted_safe(buf,&args[1],ARRAY_SIZE(args)-1,0);
	argc = oldargc + tokenize_line_quoted_safe(tokCmdLine,&args[oldargc],ARRAY_SIZE(args)-oldargc,0);
	cmdParseCommandLine(argc, argv);

	// Set proper assert mode.
	// FIXME: Let's find a better way to determine that we're in development and internal.
	getExecutableDir(path);
	if (!!strstri(path, "/bin"))
		setDefaultAssertMode();
	else
		setProductionClientAssertMode();
	setAssertMode(getAssertMode() | ASSERTMODE_MINIDUMP | ASSERTMODE_FORCEFULLDUMPS);
	setAssertMode(getAssertMode() & ~ASSERTMODE_TEMPORARYDUMPS);

	// For testing launcher<->error tracker comms.
	if (sbCrashMe)
	{
		assertmsg(0, "Testing dumps from launcher");
	}

	// attempt to get product name from executable name
	GetGameID(&gameID, &productName);
	gStartupProductName = productName;

	// figure out the proper locale to display
	LauncherResetAvailableLocalesToDefault(productName);
	UI_UpdateLocale(productName);


	// *** Auto update ***
	// this will launch an auto patch thread, if required (for the launcher itself)
	// it will block until that auto patch is done
	errorlevel = LauncherSelfPatch(productName);


	// *** Middle startup ***

	// Everything possible should happen _after_ this point, so that any bugs don't cause autoupdate to fail.

	// Initialize system specs.
	systemSpecsInit();

	// normally this is called in ServerLibStartup(), which CrypticLauncher doesn't do
	utilitiesLibStartup();
	sockStart();
	cryptMD5Init();
	srand((unsigned int)time(NULL));

#if ENABLE_LIBCEF
	//if (sUpdateLibcef)
	//{
	//	unpackIfRequired("libcef.dll", IDR_LIBCEF, MODTIME_LIBCEF, true);
	//}
	//unpackIfRequired("avcodec-54.dll", IDR_AVCODEC, MODTIME_AVCODEC, true);
	//unpackIfRequired("avformat-54.dll", IDR_AVFORMAT, MODTIME_AVFORMAT, true);
	//unpackIfRequired("avutil-51.dll", IDR_AVUTIL, MODTIME_AVUTIL, true);
	//unpackIfRequired("icudt.dll", IDR_ICUDT, MODTIME_ICUDT, true);
#endif


	// *** Late startup ***

	// NOTE: with an successful autoupdate, the launcher quits and relaunches via LauncherSpawnNewAndQuitCurrent()
	// If LauncherSelfPatch() determines there is nothing to do, then it returns errorlevel 0 as success
	if (!errorlevel)
	{
		if (lpCmdLine && lpCmdLine[0])
		{
			sCommandLine = strdup(lpCmdLine);
		}

		// Create the command queues for the PCL thread.
		{
			XLOCKFREE_CREATE XLFCreateInfo = { 0 };
			XLFCreateInfo.maximumLength = 1024;
			XLFCreateInfo.structureSize = sizeof(XLFCreateInfo);
			XLFQueueCreate(&XLFCreateInfo, &gQueueToPatchClient);
			XLFQueueCreate(&XLFCreateInfo, &gQueueToUI);
		}

		HistoryLoadFromRegistry();

		UI_ManageTrayIcon(productName);

		// If our product name is CrypticLauncher, we should running a general mode
		if (gameID == 0)
		{
			LauncherSetShowAllGamesMode(true);
		}

		{
			char proxy[MAX_PROXY_CHARS];
#if USE_OLD_OPTIONS
			LauncherGetProxyServer(productName, SAFESTR(proxy));

			// Check if proxy patching is needed
			if (LauncherGetProxyPatching(productName))
			{
				// if proxy patching is on, set extern g_force_sockbsd BEFORE creating the comm object(s)
				g_force_sockbsd = 1;

				// FIXUP
				// If proxy patching is on, and LauncherGetProxyServer() returns PROXY_NONE, then set it to PROXY_US
				// By definition now, if Proxy Patching is on, ProxyServer is either PROXY_US or PROXY_EU
				if (stricmp(proxy, PROXY_NONE) == 0)
				{
					LauncherSetProxyServer(productName, PROXY_US);
					strcpy(proxy, PROXY_US);
				}
			}

			// FIXUP
			// Check if proxy server stored in registry is localized (to cover bugs in recent versions)
			if ((stricmp(proxy, PROXY_US) != 0) &&
				(stricmp(proxy, PROXY_EU) != 0) &&
				(stricmp(proxy, PROXY_NONE) != 0))
			{
				LauncherSetProxyServer(productName, PROXY_NONE);
				LauncherSetProxyPatching(productName, false); // even if proxy patching was on before, it's off now.
			}
#else
			LauncherGetPatchProxyServer(productName, SAFESTR(proxy));
			if (stricmp(proxy, PROXY_NONE) != 0)
			{
				// if proxy patching is on, set extern g_force_sockbsd BEFORE creating the comm object(s)
				g_force_sockbsd = 1;
			}

			// FIXUP
			// Check if proxy server stored in registry is localized (to cover bugs in recent versions)
			LauncherGetGameProxyServer(productName, SAFESTR(proxy));
			if ((stricmp(proxy, PROXY_US) != 0) &&
				(stricmp(proxy, PROXY_EU) != 0) &&
				(stricmp(proxy, PROXY_NONE) != 0))
			{
				LauncherSetGameProxyServer(productName, PROXY_NONE);
			}
#endif
		}



		ShardsInit(); // this initializes commDefault()

		// start up the patcher thread
		HistoryEnumerate(historyItemCallbackFunc, &patchExtraFolders);
		// this hands ownership of patchExtraFolders to patcher thread
		PatcherSpawn(productName, patchExtraFolders);
		patchExtraFolders = NULL;

		// If -readpassword is given, look for a username and password on stdin
		if (gReadPasswordFromSTDIN)
		{
			char *success, *success2;
			char userbuf[256], pwbuf[256];
			success = fgets(userbuf, ARRAY_SIZE_CHECKED(userbuf), fileWrap(stdin));
			success2 = fgets(pwbuf, ARRAY_SIZE_CHECKED(pwbuf), fileWrap(stdin));
			if (success && success2)
			{
				success = strrchr(userbuf, '\n');
				if (success) *success = '\0';
				AccountSetUsername(userbuf);
				success = strrchr(pwbuf, '\n');
				if (success) *success = '\0';
				AccountSetPassword(pwbuf, sSTDINPasswordIsHashed);
			}
		}
		// If bypass is enabled, get password from pipe buffer.
		else
		{
			char pipePassword[MAX_PASSWORD];
			size_t pipePasswordLen = 0;

			if (gBypassPipe && (pipePasswordLen = pipe_buffer_read(SAFESTR(pipePassword), gBypassPipe)))
			{
				AccountSetLoginType(ACCOUNTLOGINTYPE_PerfectWorld);
				// null terminate pipePassword
				pipePassword[pipePasswordLen] = 0;
				gReadPasswordFromSTDIN = true;
				sSTDINPasswordIsHashed = true;

				AccountSetPassword(pipePassword, sSTDINPasswordIsHashed);
			}
		}

		// One-time unset of the proxy if it is not set to PROXY_NONE, now that there is a warning.
		{
			if (!getProxyClearOneShot(NULL))
			{
				char proxy[MAX_PROXY_CHARS];
#if USE_OLD_OPTIONS
				LauncherGetProxyServer(productName, SAFESTR(proxy));
#else
				LauncherGetGameProxyServer(productName, SAFESTR(proxy));
#endif
				if (stricmp(proxy, PROXY_NONE) != 0)
				{
					UI_MessageBox(
						_("To improve your connection stability we are disabling the use of the proxy by default. If you are sure you need it enabled, re-set it in the launcher options."),
						_("Disabling proxy"),
						MB_OK|MB_ICONWARNING);
#if USE_OLD_OPTIONS
					LauncherSetProxyServer(productName, PROXY_NONE);
#else
					LauncherSetGameProxyServer(productName, PROXY_NONE);
#endif
				}
				setProxyClearOneShot(productName, true);
			}
		}

		errorlevel = UI_ShowBrowserWindowAndRegisterCallback(productName, CrypticLauncherTickFunc);
	}
	else
	{
		UI_MessageBox(
			_("The patch server could not be found"),
			_("Server error"),
			MB_OK|MB_ICONERROR);
	}

	EXCEPTION_HANDLER_END

	return errorlevel;
}

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_CRYPTICLAUNCHER);
}

//create a text console window
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void console(int iDummy)
{
	LauncherCreateConsoleWindow();
}

void postCommandString(XLOCKFREE_HANDLE queue, CrypticLauncherCommandType type, const char *str_value)
{
	CrypticLauncherCommand *cmd = malloc(sizeof(CrypticLauncherCommand));
	cmd->type = type;
	cmd->str_value = strdup(str_value);
	XLFQueueAdd(queue, cmd);
}

void postCommandInt(XLOCKFREE_HANDLE queue, CrypticLauncherCommandType type, U32 int_value)
{
	CrypticLauncherCommand *cmd = malloc(sizeof(CrypticLauncherCommand));
	cmd->type = type;
	cmd->int_value = int_value;
	XLFQueueAdd(queue, cmd);
}

void postCommandPtr(XLOCKFREE_HANDLE queue, CrypticLauncherCommandType type, void *ptr_value)
{
	CrypticLauncherCommand *cmd = malloc(sizeof(CrypticLauncherCommand));
	cmd->type = type;
	cmd->ptr_value = ptr_value;
	XLFQueueAdd(queue, cmd);
}
