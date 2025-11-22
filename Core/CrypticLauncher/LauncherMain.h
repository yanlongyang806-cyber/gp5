#pragma once

#define USE_OLD_OPTIONS 0

typedef struct ShardInfo_Basic ShardInfo_Basic;

typedef enum CrypticLauncherCommandType
{
	// To PCL
	CLCMD_START_PATCH,
	CLCMD_CONTINUE_START_PATCH,
	CLCMD_DO_BUTTON_ACTION,
	CLCMD_STOP_THREAD,
	CLCMD_FIX_STATE,
	CLCMD_RESTART_PATCH,
	CLCMD_VERIFY_PATCH,
	// From PCL
	CLCMD_REFRESH_WINDOW_TITLE,
	CLCMD_DISPLAY_MESSAGE,
	CLCMD_SET_PROGRESS,
	CLCMD_SET_BUTTON_STATE,
	CLCMD_PUSH_BUTTON,
	CLCMD_START_LOGIN_FOR_GAME,
	CLCMD_PATCHING_DONE,
	CLCMD_PATCHING_FAILED_AND_QUIT,
	CLCMD_PATCHING_VERIFY_CHECK,
	CLCMD_PATCHING_VERIFY_COMPLETE,
}
CrypticLauncherCommandType;

typedef struct CrypticLauncherCommand
{
	CrypticLauncherCommandType type;
	union
	{
		U32 int_value;
		char *str_value;
		void *ptr_value;
	};
}
CrypticLauncherCommand;

typedef struct StartPatchCommand
{
	const ShardInfo_Basic *shard;
	bool bSetShardToAutoPatch;
	int localeId;
}
StartPatchCommand;

typedef struct ContinueStartPatchCommand
{
	const ShardInfo_Basic *shard;
	bool bVerifyPatch;
}
ContinueStartPatchCommand;

// for clarity, the order of these values matches the command interface to the JS
typedef struct PatchProgressCommand
{
	F32 percent;
	U32 elapsedMinutes;
	U32 elapsedSeconds;
	U32 totalMinutes;
	U32 totalSeconds;
	U32 numReceivedFiles;
	U32 numTotalFiles;
	U32 precisionReceived;
	F32 numReceived;
	char *unitsReceived;
	F32 numTotal;
	char *unitsTotal;
	U32 precisionActual;
	F32 numActual;
	char *unitsActual;
	int showDetails;
}
PatchProgressCommand;

// Enum values for custom messages
// !!!: Some of these values are hard-coded as numbers in the website. Do not change the numeric value of anything to be safe. <NPK 2009-06-02>
enum
{
	CLMSG_PAGE_LOADED=1, // not used by js
	CLMSG_ACTION_BUTTON_CLICKED=2,
	CLMSG_OPTIONS_CLICKED=3,
	CLMSG_LOGIN_SUBMIT=4,
	CLMSG_OPTIONS_SAVED=5,
	CLMSG_OPEN_XFER_DEBUG=6, // not used by js
	CLMSG_RELOAD_PAGE=7, // not used by js
};

// There are some greater than/less than checks performed on these values, so order of these enums does matter to an extent.
// It's hard to say whether that order has been properly maintained over time.
// The 2 range checks are:
//  - options.c - 'are we on the login page or past it?'
//  - ui.c - 'are we logged in?'
typedef enum CrypticLauncherState
{
	CL_STATE_START,
	CL_STATE_LOADINGPAGELOADED,
	CL_STATE_LOGINPAGELOADED,
	CL_STATE_LOGGINGIN,					// We're trying to login to the account server
	CL_STATE_LOGGEDIN,					// We've successfully logged in to the account server
	CL_STATE_GOTSHARDS,					// We've heard back from the controller tracker
	CL_STATE_GETTINGPAGETICKET,
	CL_STATE_GOTPAGETICKET,
	CL_STATE_LAUNCHERPAGELOADED,
	CL_STATE_SETTINGVIEW,
	CL_STATE_WAITINGFORPATCH,
	CL_STATE_GETTINGFILES,
	CL_STATE_READY,
	CL_STATE_GETTINGGAMETICKET,
	CL_STATE_ERROR,
	CL_STATE_LOGGINGINAFTERLINK,		// Website performed linking, proceeding with login like CL_STATE_LOGGINGIN
	CL_STATE_LINKING,					// Website is performing linking, ignore page loads
}
CrypticLauncherState;

typedef void *XLOCKFREE_HANDLE;

#define MAX_COMMAND_LINE_OPTIONS_TO_APPEND_CHARS	1024
#define MAX_PROXY_CHARS								1024

#define PROXY_NONE									"None"
#define PROXY_US									"US"
#define PROXY_EU									"EU"

#define LOC_PROXY_NONE								_("None")
#define LOC_PROXY_US								_("US")
#define LOC_PROXY_EU								_("EU")

// URL constants
// it's actually important for functionality of launcher that all pages that the launcher should recognize for 'state' begin with URL_SUFFIX_LAUNCHER
#define URL_SUFFIX_LAUNCHER					"launcher"
#define LURL_SUFFIX_LAUNCHER				L"launcher"
#define URL_SUFFIX_PREPATCH					URL_SUFFIX_LAUNCHER "_prepatch" 
#define URL_SUFFIX_LOGIN					URL_SUFFIX_LAUNCHER "_login"

#define URL_PREFIX_HTTP						"http://"
#define URL_PREFIX_HTTPS					"https://"

#define PRODUCT_NAME_ALL							"all"

extern void LauncherSetShardToRun(const ShardInfo_Basic *shard);

// returns true if game launched successfully, false if not
// *error is NULL if LauncherRunGame is successful; otherwise, it returns lastWinErr() after system_detach call failure.
extern bool LauncherRunGame(U32 accountID, U32 accountTicketID, char **error);

// app state set/get (THESE ARE THREAD SAFE!)
extern void LauncherSetState(CrypticLauncherState newState);
extern bool LauncherSetPatchingState(CrypticLauncherState newState);
extern CrypticLauncherState LauncherGetState(void);

// uses CrypticLauncherState internally
extern bool LauncherIsInLoginState(void);
extern bool LauncherIsInLoggedInState(void);

// command line options to append set/get
extern bool LauncherSetCommandLineOptionsToAppend(const char *productName, const char *commandLine);
extern bool LauncherGetCommandLineOptionsToAppend(const char *productName, char *commandLine, int commandLineMaxLength);

// displayed URL set/get
extern void LauncherSetDisplayedURL(const char *url);
extern const char *LauncherGetDisplayedURL(void);
extern bool LauncherIsDisplayingURL(const char *url);
extern bool LauncherIsDisplayingLoginURL(void);
extern bool LauncherIsDisplayingPostLoginURL(void);
extern bool LauncherIsDisplayingPrepatchURL(void);

// base/derived URL get
extern void LauncherGetMainURL(char **url);
extern void LauncherGetLoginURL(char **url);
extern void LauncherGetLoginOrPrepatchURL(char **url);

// conflict URL set/get
extern void LauncherSetRelativeConflictURL(const char *url);
extern bool LauncherHasConflictURL(void);
extern void LauncherGetConflictURL(char **url, U32 ticket, const char *prefix);

#if USE_OLD_OPTIONS

// proxy server (for patching and everything)
extern bool LauncherSetProxyServer(const char *productName, const char *proxyServer);
extern bool LauncherGetProxyServer(const char *productName, char *proxyServer, int proxyServerMaxLength);

#else

extern bool LauncherSetGameProxyServer(const char *productName, const char *proxyServer);
extern bool LauncherGetGameProxyServer(const char *productName, char *proxyServer, int proxyServerMaxLength);

extern bool LauncherSetPatchProxyServer(const char *productName, const char *proxyServer);
extern bool LauncherGetPatchProxyServer(const char *productName, char *proxyServer, int proxyServerMaxLength);

#endif

extern void LauncherChooseEUProxyHost(char *proxyHost, int proxyHostMaxLength);

// form a shard descriptor from productName and shardName
extern void LauncherFormShardDescriptor(char *shardDescriptor, size_t shardDescriptorMaxSize, const char *productName, const char *shardName);

#if USE_OLD_OPTIONS

// proxy patching set/get
extern bool LauncherSetProxyPatching(const char *productName, bool bProxyPatching);
extern bool LauncherGetProxyPatching(const char *productName); // called from patcher

#endif

// get shard root folder
extern void LauncherGetShardRootFolder(const char *productName, const char *shardCategoryName, char *rootFolder, int rootFolderMaxLength, bool bEnsureFolderExists);

// get pref set
extern int LauncherShardPrefSetGet(const char *shardRootFolder);

// get/set showAllGamesMode
extern bool LauncherSetShowAllGamesMode(bool bShowAllGamesMode);
extern bool LauncherGetShowAllGamesMode();

// get/set useSafeMode
extern void LauncherSetUseSafeMode(bool bUseSafeMode);
extern bool LauncherGetUseSafeMode(void);

// special handling of console window for CEF
extern void LauncherCreateConsoleWindow();

// thread safe message queue
extern void postCommandString(XLOCKFREE_HANDLE queue, CrypticLauncherCommandType type, const char *str_value); // called from patcher
extern void postCommandInt(XLOCKFREE_HANDLE queue, CrypticLauncherCommandType type, U32 int_value); // called from patcher
extern void postCommandPtr(XLOCKFREE_HANDLE queue, CrypticLauncherCommandType type, void *ptr_value); // called from patcher

#define patcherQueueRestartPatch(startPatchCommand)	postCommandPtr(gQueueToPatchClient, CLCMD_RESTART_PATCH, (startPatchCommand))
#define patcherQueueDoButtonAction(shard)			postCommandPtr(gQueueToPatchClient, CLCMD_DO_BUTTON_ACTION, (shard))
#define patcherQueueStartPatch(startPatchCommand)	postCommandPtr(gQueueToPatchClient, CLCMD_START_PATCH, (startPatchCommand))

// global data
extern bool gReadPasswordFromSTDIN;
extern int gBypassPipe; // for Arc Bypass login
extern bool gQAMode; // us-qa
extern bool gDevMode; // us-dev
extern bool gPWRDMode; // Beijing
extern bool gPWTMode; // Taiwan
extern bool gDebugMode; // adds addition logging details
extern bool gForceInvalidLocale; // Starts up in an invalid locale (won't be supported by any shards)

extern XLOCKFREE_HANDLE gQueueToPatchClient; // called from patcher
extern XLOCKFREE_HANDLE gQueueToUI; // called from patcher

extern const char *gStartupProductName;

