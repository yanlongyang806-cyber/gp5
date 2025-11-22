#include "patcher.h"
#include "LauncherSelfPatch.h"
#include "Environment.h"
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "registry.h"
#include "GameDetails.h"
#include "Shards.h" // for gPrePatchShard - deserves a cleanup
#include "Account.h" // for Account username and pwd
#include "UIDefs.h" // for CL_BUTTONSTATE_* enums only
#include "UI.h" // TODO, remove this - we shouldn't be calling UI layer here

// UtilitiesLib
#include "AppLocale.h"
#include "net.h"
#include "utils.h"
#include "sysutil.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "EString.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "ThreadSafeQueue.h"
#include "hoglib.h"
#include "earray.h"
#include "StringUtil.h"
#include "MemTrack.h"
#include "SimpleWindowManager.h"
#include "UTF8.h"

// PatchClientLib
#include "patchxfer.h"
#include "pcl_typedefs.h"
#include "pcl_client.h"
#include "pcl_client_struct.h"

// NewControllerTracker
#include "NewControllerTracker_pub.h"

// Common
#include "accountCommon.h"

// windows
#include "windefinclude.h"

#define HUGE_ERROR_STRING 2048

// PATCHING DEFINES
#define PATCH_SERVER_CONNECTION_TIMEOUT 600

// Number of seconds to wait after a PCL error to restart
#define PATCH_RESTART_DELAY 10

// Number of times to attempt to set the view.  See setView() for an explanation.
#define SETVIEW_MAX_ATTEMPTS 2

// Message limiting macros, don't let the queue fill with unimportant messages
#define QUEUE_CAPPED_MESSAGE_LIMIT 100 //size of these queues hardcoded to 1024 in LauncherMain.c, this should be significantly lower than that
#define uiQueueCappedMessage(msg)					{long lEntryCount; XLFQueueGetEntryCount(gQueueToUI,&lEntryCount); if(lEntryCount<QUEUE_CAPPED_MESSAGE_LIMIT){msg;}}

// Message passing macros
#define uiQueueRefreshWindowTitle(msg)				postCommandString(gQueueToUI, CLCMD_REFRESH_WINDOW_TITLE, (msg))
#define uiQueueRefreshWindowTitleCapped(msg)		uiQueueCappedMessage(uiQueueRefreshWindowTitle(msg))
#define uiQueueDisplayStatusMsg(msg)				postCommandString(gQueueToUI, CLCMD_DISPLAY_MESSAGE, (msg))
#define uiQueueDisplayStatusMsgCapped(msg)			uiQueueCappedMessage(uiQueueDisplayStatusMsg(msg))
#define uiQueueSetProgress(patchProgressCmd)		uiQueueCappedMessage(postCommandPtr(gQueueToUI, CLCMD_SET_PROGRESS, (patchProgressCmd)))
#define uiQueueSetButtonState(state)				postCommandInt(gQueueToUI, CLCMD_SET_BUTTON_STATE, (U32)(state))
#define uiQueuePushButton()							postCommandPtr(gQueueToUI, CLCMD_PUSH_BUTTON, NULL)
#define uiQueueStartLoginForGame(shard)				postCommandPtr(gQueueToUI, CLCMD_START_LOGIN_FOR_GAME, (shard))
#define uiQueuePatchingDone(productName)			postCommandString(gQueueToUI, CLCMD_PATCHING_DONE, (productName))
#define uiQueuePatchingFailedAndQuit(errorlevel)	postCommandInt(gQueueToUI, CLCMD_PATCHING_FAILED_AND_QUIT, (errorlevel))
#define uiQueuePatchingVerifyCheck(shard)			postCommandPtr(gQueueToUI, CLCMD_PATCHING_VERIFY_CHECK, (shard))
#define uiQueuePatchingVerifyComplete(productName)	postCommandString(gQueueToUI, CLCMD_PATCHING_VERIFY_COMPLETE, (productName))

// predec's
static bool SetIsInitialDownload(const char *productName, bool bIsInitialDownload);
static bool GetIsInitialDownload(const char *productName);

static void PatcherClearStats(void);
static void PatcherUpdateStats(void);
static void PatcherUpdateXferData(void);

// --- configuration info ---

// Patch project name
static char *sProject;

// 	What version to patch up to
static char *sViewName;

// Attempt to get locale-specific patch for this locale ID
static LocaleID sLocaleId;

static PCL_Client *sPatchClient = NULL; // must acquire sPatchClientMutex to access
static PatchSpeedData sSpeedReceived, sSpeedActual, sSpeedLink; // must acquire sPatchClientMutex to access
static char sSuccessfulPatchRootFolder[MAX_PATH] = {0};
static const char *sPatchProductName = NULL;
static CRITICAL_SECTION sPatchClientMutex = {0};
static bool sbIsPatcherVerifying = false;
static F32 sElapsedLast = 0;
static int sPatchTicks = 0;
static int sPatcherHTTPPercent = 0;
// Patch speed tracking
static PCL_ErrorCode sRestartError = 0;
static int sRestartLast = 0;
static NetComm *sPatchClientComm = NULL;
static S64 sLastReceived = 0;
// If set, patch to this shard as soon as possible.
static ShardInfo_Basic *sShardToAutoPatch = NULL;

// this is for xfers_dialog only
static PatchXferData sPatchXferData[MAX_XFERS];
static int sPatchXferDataCount = 0;

// Number of attempts so far to set a view.  See setView() for an explanation.
static int sSetViewAttempt = 0;

int gDoNotAutopatch = 0;
AUTO_CMD_INT(gDoNotAutopatch, dontautopatch) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static int sPatchAll = 0;
AUTO_CMD_INT(sPatchAll, patchall) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// If set, check this file next frame against the current patcher's filespec.
static const char *sDebugCheckFilespec = NULL;
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void PatchStreamingIsRequired(char *filename)
{
	sDebugCheckFilespec = strdup(filename);
}

static unsigned int __stdcall thread_Patch(void *userdata);

#define acquirePatchCS() acquirePatchClientCS(MEM_DBG_PARMS_INIT_VOID)
#define releasePatchCS() releasePatchClientCS(MEM_DBG_PARMS_INIT_VOID)

static void acquirePatchClientCS(MEM_DBG_PARMS_VOID)
{
	ATOMIC_INIT_BEGIN;

	InitializeCriticalSection(&sPatchClientMutex);

	ATOMIC_INIT_END;

//	printf("acquired patch client mutex at %s(%d)\n", caller_fname, line);
	EnterCriticalSection(&sPatchClientMutex);
}

static void releasePatchClientCS(MEM_DBG_PARMS_VOID)
{
	LeaveCriticalSection(&sPatchClientMutex);
//	printf("released patch client mutex at %s(%d)\n", caller_fname, line);
}

// FIXME: The fact that there's two places you can get an autoupdate (of the launcher) is a substantial problem, especially as the second one is
// essentially a second-class citizen, lacking any real user reporting of what's going on.  This should be resolved somehow.  Probably,
// we just need to restart immediately, auto-update, then restart again.
static void connectCallback(PCL_Client *client, bool updated, PCL_ErrorCode error, const char *error_details, void *userData)
{
	// did we auto-update the launcher? (second chance)
	if (updated)
	{
		char username[MAX_LOGIN_FIELD];
		assertmsg(AccountGetUsername(SAFESTR(username)), "Second Chance Auto Update should always happen post-login, and should therefore have a username!");
		LauncherSpawnNewAndQuitCurrent((const char*)userData, username, "second chance");
	}
}

static void PatcherInitConfig(void)
{
	SAFE_FREE(sProject);
	SAFE_FREE(sViewName);
}

void PatcherGetToken(bool bInitialDownload, char *token, int tokenMaxLength)
{
	S32 gameID = gdGetIDByName(gStartupProductName);
	const char *launcherProductName = gdGetName(0);

	// null terminate the token string
	assert(tokenMaxLength > 0);
	token[0] = 0;

	// patch token is of the form (Download)CrypticLauncher(GameCode)

	// Modify the autoupdate token to indicate we're the initial download.
	// FIXME: This breaks second-pass autoupdating, but that step was not completely functional anyway.  Figure out what should be done.
	if (bInitialDownload)
	{
		strcat_s_trunc(token, tokenMaxLength, "Download");
	}

	// start with Cryptic Launcher display name
	strcat_s_trunc(token, tokenMaxLength, launcherProductName);
	strcat_s_trunc(token, tokenMaxLength, "2"); // we're redirecting to a new patch channel

	// if gameID is non-zero...
	if (gameID)
	{
		// ... append the 2/3 letter code for the game to the token
		strcat_s_trunc(token, tokenMaxLength, gdGetCode(gameID));
	}
}

bool PatcherIsDevelopmentPath(const char *path)
{
	return 
		strstri(path, "/Core/tools/bin/") ||
		strstri(path, "/FightClub/tools/bin/") ||
		strstri(path, "/StarTrek/tools/bin/") ||
		strstri(path, "/Night/tools/bin/") ||
		strstri(path, "/Creatures/tools/bin/") ||
		strstri(path, "/Bronze/tools/bin/") ||
		strstri(path, "/src/");
}

bool IsDefaultPatchServer(const char *patchServer)
{
	return stricmp(patchServer, ENV_US_PATCH_SERVER_HOST) == 0;
}

static const char *getPatchServer(void)
{
	// note: it is intended that sPatchServer has no bearing on game patching patch server - only on (cryptic launcher) auto patch
	if (gPWRDMode)
	{
		return ENV_PWRD_PATCH_SERVER_HOST;
	}
	else if (gPWTMode)
	{
		return ENV_PWT_PATCH_SERVER_HOST;
	}
	else
	{
		return ENV_US_PATCH_SERVER_HOST;
	}
}

// Report a PCL error and exit.
void PatcherHandleError(int error, bool bConnecting)
{
	if (error)
	{
		char patch_error[2048];
		int error_error = pclGetErrorString(error, SAFESTR(patch_error));
		Errorf("Launcher PCL Error %s", patch_error);
		if (bConnecting && error == PCL_TIMED_OUT)
			MessageBox_UTF8(NULL,
			_("Unable to establish connection to Patch Server.  Please check your Internet connection.  If problems persist, please contact Technical Support."),
			_("Connection problem"),
			MB_OK|MB_ICONERROR);
		else
			MessageBox_UTF8(NULL,
			cgettext(patch_error),
			_("Patcher error"),
			MB_OK|MB_ICONERROR);
		exit(error + 100);
	}
}

typedef struct tagTHREADNAME_INFO
{
	DWORD dwType;       // Must be 0x1000.
	LPCSTR szName;      // Pointer to name (in user addr space).
	DWORD dwThreadID;   // Thread ID (-1=caller thread).
	DWORD dwFlags;      // Reserved for future use, must be zero.
} THREADNAME_INFO;

// Thread name setting code from FMOD
#define MS_VC_EXCEPTION 0x406D1388

static void setThreadName(unsigned long dwThreadID, const char *szThreadName)
{
	THREADNAME_INFO info;

	info.dwType     = 0x1000;
	info.szName     = szThreadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags    = 0;
	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
	}
#pragma warning(suppress : 6312)
	__except(EXCEPTION_CONTINUE_EXECUTION)
#pragma warning(suppress : 6322)
	{
	}
}

typedef struct PatcherSpawnData
{
	const char *productName;
	char **patchExtraFolders;
}
PatcherSpawnData;

void PatcherSpawn(const char *productName, char **patchExtraFolders)
{
	DWORD threadid;
	PatcherSpawnData *spawnData = malloc(sizeof(PatcherSpawnData));
	spawnData->productName = productName;
	spawnData->patchExtraFolders = patchExtraFolders;

	// Spawn a PCL thread - pass ownership of spawnData to thread - it will destroy
	_beginthreadex(NULL, 0, thread_Patch, (void*)spawnData /* userdata */, 0, &threadid);
	setThreadName(threadid, "PCLThread");
}

#define REGKEY_LAST_VERIFY_CANCEL			"LastVerifyCancel"

// NOTE: this function is run in the UI Thread!
void PatcherCancel(void)
{
	if (sbIsPatcherVerifying)
	{
		assert(sPatchProductName != NULL);

		// NOTE: this value MUST be changed here if we want verify to be cancel-able.
		// you cannot send a message to the patching thread, which is preferred, because in some instances, that thread "blocks", not processing cmds in the patching command queue.
		if (GetVerifyLevel(sPatchProductName) > 0)
		{
			writeRegInt(sPatchProductName, REGKEY_LAST_VERIFY_CANCEL,  timeSecondsSince2000()); // this is written, but never read by launcher
			SetVerifyLevel(sPatchProductName, 0);
		}
	}
	else
	{
		uiQueuePushButton();
	}
}

// note: sPatchClientMutex is acquired prior to the execution of this function
static bool displayDownloadProgress(PatchProcessStats *stats, void *userdata)
{
	char *productDisplayName = (char*)userdata;
	FOR_EACH_IN_EARRAY(stats->state_info, XferStateInfo, state)
		stats->received += state->blocks_so_far * state->block_size;
	FOR_EACH_END

	sSpeedReceived.cur = stats->received;
	sSpeedActual.cur = stats->actual_transferred;
	sPatcherHTTPPercent = stats->actual_transferred ? 100*stats->http_actual_transferred/stats->actual_transferred : 0;
	
	if (stats->received && stats->elapsed - sElapsedLast > 0.1)
	{
		PatchProgressCommand* patchProgressCmd = malloc(sizeof(PatchProgressCommand)); // this is freed in the UI thread after the msg is cracked open
		U32 precisionTotal; // thrown away (not passed to JS)
		F32 total_time;

		if (stats->received < sLastReceived)
		{
			stats->received = sLastReceived;
		}
		else
		{
			sLastReceived = stats->received;
		}

		patchProgressCmd->percent = stats->received * 100.0 / stats->total;
		total_time = stats->received ? stats->elapsed * stats->total / stats->received : 0;

		patchProgressCmd->elapsedMinutes = (U32)stats->elapsed / 60;
		patchProgressCmd->elapsedSeconds = (U32)stats->elapsed % 60;
		patchProgressCmd->totalMinutes = (U32)total_time / 60;
		patchProgressCmd->totalSeconds = (U32)total_time % 60;
		patchProgressCmd->numReceivedFiles = stats->received_files;
		patchProgressCmd->numTotalFiles = stats->total_files;
		patchProgressCmd->showDetails = 1;

		humanBytes(stats->received, &patchProgressCmd->numReceived, &patchProgressCmd->unitsReceived, &patchProgressCmd->precisionReceived);
		humanBytes(stats->total, &patchProgressCmd->numTotal, &patchProgressCmd->unitsTotal, &precisionTotal);
		humanBytes(stats->actual_transferred, &patchProgressCmd->numActual, &patchProgressCmd->unitsActual, &patchProgressCmd->precisionActual);

		uiQueueRefreshWindowTitleCapped(STACK_SPRINTF("%.1f%% %s", patchProgressCmd->percent, productDisplayName));
		uiQueueSetProgress(patchProgressCmd);
		sElapsedLast = stats->elapsed;
	}
	return false;
}

// note: sPatchClientMutex is acquired prior to the execution of this function
static bool mirrorCallback(	PCL_Client* client,
								  SimpleWindow* window,
								  F32 elapsed,
								  const char* curFileName,
								  ProgressMeter* progress)
{
	static U32 lastTime = 0;

	if ((timeGetTime() - lastTime) > 250 ||
		progress->files_so_far == progress->files_total)
	{
		PatchProgressCommand* patchProgressCmd = calloc(sizeof(PatchProgressCommand), 1); // this is freed in the UI thread after the msg is cracked open
		patchProgressCmd->percent = 100.0f;
		patchProgressCmd->unitsActual = "B";
		patchProgressCmd->unitsReceived = "B";
		patchProgressCmd->unitsTotal = "B";
		uiQueueSetProgress(patchProgressCmd);

		uiQueueDisplayStatusMsgCapped(STACK_SPRINTF(FORMAT_OK(_("Mirroring %d: %s")), progress->files_so_far, curFileName));

		lastTime = timeGetTime();
	}

	return true;
}

// note: sPatchClientMutex is acquired prior to the execution of this function
static bool verifyCallback(	PCL_Client* client,
	void *productName,
	F32 elapsed,
	const char* curFileName,
	ProgressMeter* progress)
{
	static U32 lastTime = 0;
	sbIsPatcherVerifying = true;

	if (timeGetTime() - lastTime > 250 ||
		progress->files_so_far == progress->files_total)
	{
		{
			const char *trimname = strrchr(curFileName, '/');

			if (!trimname)
			{
				trimname = curFileName;
			}
			else
			{
				trimname++;
			}

			uiQueueDisplayStatusMsgCapped(STACK_SPRINTF(FORMAT_OK(_("Verifying %d/%d: %s")), progress->files_so_far, progress->files_total, trimname));
		}

		{
			PatchProgressCommand* patchProgressCmd = calloc(sizeof(PatchProgressCommand), 1); // this is freed in the UI thread after the msg is cracked open
			patchProgressCmd->percent = 100.0f;
			patchProgressCmd->unitsActual = "B";
			patchProgressCmd->unitsReceived = "B";
			patchProgressCmd->unitsTotal = "B";

			if (progress->files_total)
			{
				patchProgressCmd->percent = (100.0f * progress->files_so_far) / progress->files_total;
			}

			uiQueueSetProgress(patchProgressCmd);
		}

		lastTime = timeGetTime();
	}

	// we want the verify to stop if verifyLevel is set to 0.
	return GetVerifyLevel(productName) > 0;
}

// note: sPatchClientMutex is acquired prior to the execution of this function
static bool verifyCompleteCallback(PCL_Client* client,
	void *productName,
	F32 elapsed,
	const char* curFileName,
	ProgressMeter* progress)
{
	uiQueuePatchingVerifyComplete(productName);
	return true;
}

static void createPatcherComm(const char *productName)
{
	if (!sPatchClientComm)
	{
		char proxy[MAX_PROXY_CHARS];
		sPatchClientComm = commCreate(1, 0);

#if USE_OLD_OPTIONS
		// Actually setup the proxy
		if (LauncherGetProxyPatching(productName))
		{
			if (LauncherGetProxyServer(productName, SAFESTR(proxy)))
			{
				if (stricmp(proxy, PROXY_EU) == 0)
				{
					char euProxyHost[MAX_PROXY_CHARS];

					LauncherChooseEUProxyHost(SAFESTR(euProxyHost));

					commSetProxy(sPatchClientComm, euProxyHost, 80);
				}
				else if (stricmp(proxy, PROXY_US) == 0)
				{
					commSetProxy(sPatchClientComm, ENV_US_PROXY_HOST, 80);
				}
				else
				{
					// this condition should not be possible - if proxy patching is on, ProxyServer must be US or EU
					// force proxy patching to OFF.
					LauncherSetProxyPatching(productName, false);
					LauncherSetProxyServer(productName, PROXY_NONE);
				}
			}
		}
#else
		if (LauncherGetPatchProxyServer(productName, SAFESTR(proxy)))
		{
			if (stricmp(proxy, PROXY_NONE) == 0)
			{
				// do nothing
			}
			else if (stricmp(proxy, PROXY_EU) == 0)
			{
				char euProxyHost[MAX_PROXY_CHARS];

				LauncherChooseEUProxyHost(SAFESTR(euProxyHost));

				commSetProxy(sPatchClientComm, euProxyHost, 80);
			}
			else if (stricmp(proxy, PROXY_US) == 0)
			{
				commSetProxy(sPatchClientComm, ENV_US_PROXY_HOST, 80);
			}
			else 
			{
				// this condition should not be possible - might indicate a programming error, or something else.
				// force proxy patching to OFF.
				LauncherSetPatchProxyServer(productName, PROXY_NONE);
			}
		}
#endif
	}
}

static bool disconnectFromPatchServer(PCL_Client **patchClient)
{
	bool retVal = false;
	if (*patchClient)
	{
		pclDisconnectAndDestroy(*patchClient);
		*patchClient = NULL;
		retVal = true;
	}
	return retVal;
}

// returns errorlevel 0 if success
// returns errorlevel 1 if failed with single attempt
// returns errorlevel 2 if failed with multiple attempts
static int connectToPatchServer(const ShardInfo_Basic *shard, char **patchExtraFolders)
{
	int errorlevel = 0;
	PCL_ErrorCode error;
	int attempts = 0;
	char autoupdate_path[MAX_PATH] = {0};
	char token[MAX_PATH];
	char prepatch_path[MAX_PATH];
	char requestedShardPatchFolder[MAX_PATH];
	const char *productName;
	const char *patchServer = getPatchServer();
	const char *autoPatchServer = getAutoPatchServer();

	assert(shard);

	productName = shard->pProductName;

	LauncherGetShardRootFolder(productName, shard->pShardCategoryName, SAFESTR(requestedShardPatchFolder), true /* bEnsureFolderExists */);

	acquirePatchCS();
	// we have a patch operation going - we need to stop that one and start a different one

	PatcherGetToken(gPrePatchShard ? false : GetIsInitialDownload(productName) /* bIsInitialDownload */, SAFESTR(token));

	do
	{
		if (disconnectFromPatchServer(&sPatchClient))
		{
			attempts++;
		}

		// Should we autopatch (the game)?
		strcpy(autoupdate_path, getExecutableName());
		forwardSlashes(autoupdate_path);
		if (gDoNotAutopatch ||
			PatcherIsDevelopmentPath(autoupdate_path) ||
			!IsDefaultPatchServer(autoPatchServer))
		{
			autoupdate_path[0] = '\0';
		}

		// FIXME: COR-18033
		// for the moment, we are disabling second-chance auto update entirely.
		// Note: connectCallback()'s passing of username/password via stdin does not work at the moment.
		autoupdate_path[0] = '\0';

		makeDirectories(requestedShardPatchFolder);
		if (attempts > 0)
			ShellExecute( NULL, L"open", L"ipconfig.exe", L"/flushdns", L"", SW_HIDE );
		printf("Connecting with root %s\n", requestedShardPatchFolder);
		error = pclConnectAndCreate(&sPatchClient,
			patchServer,
			PATCH_SERVER_PORT,
			PATCH_SERVER_CONNECTION_TIMEOUT,
			sPatchClientComm,
			requestedShardPatchFolder,
			token,
			*autoupdate_path ? autoupdate_path : NULL,
			connectCallback,
			autoupdate_path);

		if (error || !sPatchClient)
		{
			errorlevel = attempts ? 2 : 1;
		}
		else
		{
			// Boost the amount the PCL can request at once to 500k
			pclSetMaxNetBytes(sPatchClient, 1024 * 500);

			pclSetRetryTimes(sPatchClient, PATCH_RETRY_TIMEOUT, PATCH_RETRY_BACKOFF);
			error = pclWait(sPatchClient);
		}
	}
	while ((error == PCL_LOST_CONNECTION) && (errorlevel == 0));
	PatcherHandleError(error, false);

	if (errorlevel == 0)
	{
		if (gDebugMode)
		{
			pclVerboseLogging(sPatchClient, 1);
		}
		//pclVerifyAllFiles(sPatchClient, g_config.verifyAllFiles);

		// Setup the progress callback
		error = pclSetProcessCallback(sPatchClient, displayDownloadProgress, (void*)gdGetDisplayName(gdGetIDByName(productName)));
		PatcherHandleError(error, false);
		error = pclSetMirrorCallback(sPatchClient, mirrorCallback, NULL);
		PatcherHandleError(error, false);

		// Enable clean-hoggs mode
		pclAddFileFlags(sPatchClient, PCL_CLEAN_HOGGS);

		// Add extra folders based on shards that have been patched in the past
		FOR_EACH_IN_EARRAY(patchExtraFolders, char, patchExtraFolder)
			pclAddExtraFolder(sPatchClient, patchExtraFolder, HOG_NOCREATE|HOG_READONLY);
		FOR_EACH_END

		// Setup the prepatch folder as an extra
		strcpy(prepatch_path, sPatchClient->root_folder);
		strcat(prepatch_path, "/prepatch");
		pclAddExtraFolder(sPatchClient, prepatch_path, gPrePatchShard?HOG_DEFAULT:HOG_READONLY);

		if (gPrePatchShard)
		{
			pclSetWriteFolder(sPatchClient, prepatch_path);
			pclAddFileFlags(sPatchClient, PCL_NO_DELETE|PCL_NO_MIRROR);
		}
		else if (!shard->pPrePatchCommandLine || !shard->pPrePatchCommandLine[0])
		{
			// No prepatch, erase all the old data.
			char **files = fileScanDirFolders(prepatch_path, FSF_FILES);
			FOR_EACH_IN_EARRAY(files, char, file)
				if (strEndsWith(file, ".hogg"))
				{
					int ignored = remove(file);
				}
			FOR_EACH_END
			fileScanDirFreeNames(files);
		}
	}

	releasePatchCS();

	return errorlevel;
}

// Return true if this is a shard used for testing by customers
static bool isExternalTestCategory(const char *category)
{
	return !stricmp_safe(category, SHARD_CATEGORY_PLAYTEST) || !stricmp_safe(category, SHARD_CATEGORY_BETA);
}

// assumes sPatchClient has been set
// returns errorlevel 0 if all is good
// returns errorlevel 1 if no project could be found/assigned
// returns true if already patched, and false if it needs to be patched still
static int startPatch(const ShardInfo_Basic *shard, bool bSetShardToAutoPatch, char **patchExtraFolders, int localeId)
{
	bool bLocalShard = false;
	int errorlevel = 0;
	int argc;
	char *argv[50];
	char *commandline, *project;
	char patchClientRootFolder[MAX_PATH] = {0};
	int i;

	assert(shard);

	acquirePatchCS();
	if (sPatchClient)
	{
		strcpy(patchClientRootFolder, sPatchClient->root_folder);
		printf("startPatch, current root %s new %s:%s\n", patchClientRootFolder, shard->pProductName, shard->pShardName);
	}
	else
	{
		printf("startPatch, new %s:%s\n", shard->pProductName, shard->pShardName);
	}
	sSuccessfulPatchRootFolder[0] = 0;
	sPatchProductName = shard->pProductName;
	releasePatchCS();

	// Skip patching if connecting to localhost
	if (gdIsLocalProductName(sPatchProductName))
	{
		if (LauncherSetPatchingState(CL_STATE_READY))
		{
			PatchProgressCommand* patchProgressCmd;

			//cancel any current patch operations
			acquirePatchCS();
			disconnectFromPatchServer(&sPatchClient);
			releasePatchCS();

			patchProgressCmd = calloc(sizeof(PatchProgressCommand), 1); // this is freed in the UI thread after the msg is cracked open
			patchProgressCmd->percent = 100.0f;
			patchProgressCmd->unitsActual = "B";
			patchProgressCmd->unitsReceived = "B";
			patchProgressCmd->unitsTotal = "B";
			uiQueueSetProgress(patchProgressCmd);

			uiQueueDisplayStatusMsg(_("Ready (local shard mode)"));
			uiQueueSetButtonState(CL_BUTTONSTATE_PLAY);
		}
		SetLastShardPatchedDescriptor(sPatchProductName, shard->pShardName);
		bLocalShard = true;
	}

	if (!bLocalShard)
	{
		PatchProgressCommand* patchProgressCmd = calloc(sizeof(PatchProgressCommand), 1); // this is freed in the UI thread after the msg is cracked open
		patchProgressCmd->unitsActual = "B";
		patchProgressCmd->unitsReceived = "B";
		patchProgressCmd->unitsTotal = "B";
		uiQueueSetProgress(patchProgressCmd);

		SetShardToAutoPatch(bSetShardToAutoPatch ? shard : NULL);

		uiQueueRefreshWindowTitle(gdGetDisplayName(gdGetIDByName(sPatchProductName)));
		uiQueueDisplayStatusMsg(_("Connecting ..."));

		sPatchTicks = 0;
		sLastReceived = 0;

		PatcherInitConfig();

		commandline = estrCreateFromStr(gPrePatchShard ? shard->pPrePatchCommandLine : shard->pPatchCommandLine);

		// The language will be appended to this.
		if (strcmpi(shard->pShardCategoryName, SHARD_CATEGORY_AVATAR) == 0)
		{
			project = "%sClientAvatar";
		}
		else if (isExternalTestCategory(shard->pShardCategoryName))
		{
			project = "%sClientBeta";
		}
		else
		{
			project = "%sClient";
		}

		estrReplaceOccurrences_CaseInsensitive(&commandline, STACK_SPRINTF("%sServer", sPatchProductName), STACK_SPRINTF(FORMAT_OK(project), sPatchProductName));
		argc = tokenize_line(commandline, argv, 0);
		for (i = 0; i < argc; ++i)
		{
			char *arg = argv[i];
			if (!stricmp(arg, "-project"))
			{
				char *new_project = NULL;
				assert(i+1 < argc);

				estrStackCreate(&new_project);
				estrCopy2(&new_project, argv[i+1]);
				if (getIsTransgaming())
					estrAppend2(&new_project, "Mac");

				sProject = strdup(new_project);
				estrDestroy(&new_project);
			}
			else if (!stricmp(arg, "-name"))
			{
				assert(i+1 < argc);
				sViewName = strdup(argv[i+1]);
			}
		}
		estrDestroy(&commandline);

		// Save locale ID for when we set the view.
		sLocaleId = localeId;

		// Error checking for -name HOLD setting
		if (stricmp(sViewName, "HOLD")==0)
		{
			Errorf("-name HOLD got to patching, something is wrong. Command line is \"%s\"", gPrePatchShard ? shard->pPrePatchCommandLine : shard->pPatchCommandLine);
		}

		// TODO: do more sanity checking
		if (!SAFE_DEREF(sProject))
		{
			if (LauncherSetPatchingState(CL_STATE_LAUNCHERPAGELOADED))
			{
				uiQueueSetButtonState(CL_BUTTONSTATE_DISABLED);
				uiQueueDisplayStatusMsg(_("Cannot patch, no project given."));
			}
			errorlevel = 1;
		}

		if (errorlevel == 0)
		{
			errorlevel = connectToPatchServer(shard, patchExtraFolders);
		}

		if (errorlevel == 0)
		{
			uiQueuePatchingVerifyCheck((void*)shard); // have to strip const here
		}
	}

	return errorlevel;
}

// Attempt to set the view.
static void setView()
{
	++sSetViewAttempt;

	// If this is not our first attempt, clear any saved error.
	if (sSetViewAttempt > 1)
		pclClearError(sPatchClient);

	if (SAFE_DEREF(sViewName))
	{
		char *project = NULL;

		estrStackCreate(&project);

		// Compose final project name.
		// Warning: If you change this, you must change SETVIEW_MAX_ATTEMPTS as well.
		switch (sSetViewAttempt)
		{
			case 1:
				// Use the current language as the suffix.
				estrPrintf(&project, "%s%s", sProject, locGetName(sLocaleId));
				pclSoftenError(sPatchClient, PCL_INVALID_VIEW);
				break;
			case 2:
				// Try with empty suffix.
				estrCopy2(&project, sProject);
				pclUnsoftenError(sPatchClient, PCL_INVALID_VIEW);
				break;
			default:
				devassertmsgf(0, "Invalid view attempt %d", sSetViewAttempt);
		}

		// Display status message.
		uiQueueDisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Version %s")), sViewName));
		printf("Setting view by name (attempt %d): %s\n", sSetViewAttempt, project);

		// Set the view.
		pclSetNamedView(
			sPatchClient,
			project,
			sViewName,
			true /* getManifest */,
			true /* saveTrivia */,
			NULL /* callback */,
			NULL /* userdata */);

		estrDestroy(&project);
	}
	else
	{
		printf("Setting view by time\n");
		pclSetViewByTime(
			sPatchClient,
			sProject,
			0 /* branch */,
			"" /* sandbox */,
			INT_MAX /* latest time */,
			true /* getManifest */,
			true /* saveTrivia */,
			NULL /* callback */,
			NULL /* userdata */);
	}

	
}

static void continueStartPatch(const ShardInfo_Basic *shard, bool bVerifyPatch)
{
	acquirePatchCS();

	assert(sPatchClient);

	if (bVerifyPatch)
	{
		pclVerifyAllFiles(sPatchClient, 1);
		pclSetExamineCallback(sPatchClient, verifyCallback, (void *)shard->pProductName);
		pclSetExamineCompleteCallback(sPatchClient, verifyCompleteCallback, (void *)shard->pProductName);
	}

	pclSendLog(sPatchClient, "patch started", "project %s view %s", sProject, sViewName);

	// Reset view attempt counter.
	sSetViewAttempt = 0;
	setView();

	releasePatchCS();
}

static void patchTick(const char *productName, char **patchExtraFolders)
{
	static time_t sRestartTime = 0;

	if (sRestartTime || PatcherIsValid())
	{
		static char *spinner[] = { "", ".", "..", "..." };
		PCL_ErrorCode err;
		// Find the product display name
		const char *productDisplayName = gdGetDisplayName(gdGetIDByName(productName));

		sPatchTicks = (sPatchTicks + 1) % (4<<9);

		switch (LauncherGetState())
		{
		case CL_STATE_SETTINGVIEW:
			{
				acquirePatchCS();

				// Check for pre-existing error condition.
				devassertmsg(sPatchClient, "null patchclient");
				err = pclErrorState(sPatchClient);

				// Process.
				if (!err)
					err = pclProcessTracked(sPatchClient);


				// clear patching stats for xfers_dialog
				PatcherClearStats();
				PatcherUpdateXferData();

				if (err == PCL_WAITING)
				{
					uiQueueDisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Version %s %s")), sViewName, spinner[sPatchTicks>>9]));
				}
				else if (err == PCL_SUCCESS)
				{
					const ShardInfo_Basic *shardToAutoPatch = GetShardToAutoPatch();

					// Unsoften invalid view; see setView().
					pclUnsoftenError(sPatchClient, PCL_INVALID_VIEW);

					// Forcing off bindiffing to deal with Akamai. Once we fix dealing with Akamai, remove this line again
					if (sPatchClient->xferrer && sPatchClient->xferrer->http_server && strstri(sPatchClient->xferrer->http_server, "akamai"))
						pclAddFileFlags(sPatchClient, PCL_DISABLE_BINDIFF);

					// update patching stats for xfers_dialog
					PatcherUpdateStats();

					// Finished setting up our view
					// If we are in fast-launch mode, just start downloading.
					if (LauncherSetPatchingState(CL_STATE_WAITINGFORPATCH))
					{
						if (shardToAutoPatch)
						{
							patcherQueueDoButtonAction((void *)shardToAutoPatch); // have to strip const here
							//SetShardToAutoPatch(NULL);
						}
						else
						{
							uiQueueSetButtonState(CL_BUTTONSTATE_PATCH);
						}
					}
				}
				else if (err == PCL_INVALID_VIEW && sSetViewAttempt < SETVIEW_MAX_ATTEMPTS)
				{
					// Try again.
					setView();
				}
				else
				{
					// Some kind of error
					if (LauncherSetPatchingState(CL_STATE_ERROR))
					{
						char errormsg[256];
						uiQueueRefreshWindowTitle(productDisplayName);
						pclGetErrorString(err, SAFESTR(errormsg));
						uiQueueDisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Error %d: %s")), err, cgettext(errormsg)));
						uiQueueSetButtonState(CL_BUTTONSTATE_PATCH);
						sRestartTime = time(NULL) + PATCH_RESTART_DELAY;
						sRestartError = err;
					}
					// Make sure to clear the, now useless, patch client.
					disconnectFromPatchServer(&sPatchClient);
				}
				releasePatchCS();
			}
			break;
		case CL_STATE_GETTINGFILES:
			{
				acquirePatchCS();
				err = pclProcessTracked(sPatchClient);

				// update patching stats for xfers_dialog
				PatcherUpdateStats();
				PatcherUpdateXferData();

				if (err == PCL_WAITING)
				{
					uiQueueDisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Patching %s")), spinner[sPatchTicks>>9]));
				}
				else if (err == PCL_SUCCESS)
				{
					// Got all the files, good to go.
					U64 actual_transferred;

					// Clear the verify flag (no matter what, even if we were not verifying)
					SetVerifyLevel(productName, 0);
					sbIsPatcherVerifying = false;
					sPatchProductName = NULL;

					pclSendLog(sPatchClient, "patch completed", "project %s view %s", sProject, sViewName);
					pclSendLogHttpStats(sPatchClient);
					pclActualTransferred(sPatchClient, &actual_transferred);
					printf("Total transfer: %"FORM_LL"u\n", actual_transferred);

					// Disconnect from the server so we don't hog resources.
					strcpy(sSuccessfulPatchRootFolder, sPatchClient->root_folder);
					disconnectFromPatchServer(&sPatchClient);

					if (gPrePatchShard)
					{
						// Prepatching, exit
						exit(0);
					}

					SetIsInitialDownload(productName, false /* bIsInitialDownload */);

					// this must be set immediately, not in a different thread
					if (LauncherSetPatchingState(CL_STATE_READY))
					{
						PatchProgressCommand* patchProgressCmd = calloc(sizeof(PatchProgressCommand), 1); // this is freed in the UI thread after the msg is cracked open
						patchProgressCmd->unitsActual = "B";
						patchProgressCmd->unitsReceived = "B";
						patchProgressCmd->unitsTotal = "B";
						patchProgressCmd->percent = 100.0f;
						uiQueueSetProgress(patchProgressCmd);
						uiQueueRefreshWindowTitle(productDisplayName);
						uiQueueDisplayStatusMsg(_("Ready"));
						uiQueueSetButtonState(CL_BUTTONSTATE_PLAY);
						uiQueuePatchingDone(productName);
					}
				}
				else
				{
					// Some kind of error
					if (LauncherSetPatchingState(CL_STATE_ERROR))
					{
						char errormsg[256];
						SetVerifyLevel(productName, 0);
						sbIsPatcherVerifying = false;
						sPatchProductName = NULL;
						uiQueueRefreshWindowTitle(productDisplayName);
						pclGetErrorString(err, SAFESTR(errormsg));
						uiQueueDisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Error %d: %s.")), err, cgettext(errormsg)));
						uiQueueSetButtonState(CL_BUTTONSTATE_PATCH);
						sRestartTime = time(NULL) + PATCH_RESTART_DELAY;
						sRestartError = err;
					}
					// Make sure to clear the, now useless, patch client.
					disconnectFromPatchServer(&sPatchClient);
				}
				releasePatchCS();
			}
			break;
		case CL_STATE_ERROR:
			{
				time_t now = time(NULL);
				int timeleft;
				char errormsg[256];

				// No restart time setup, bail
				if (!sRestartTime)
					break;

				timeleft = sRestartTime - now;
				if (timeleft == sRestartLast)
					break;

				sRestartLast = timeleft;

				if (timeleft <= 0)
				{
					sRestartError = 0;
					sRestartTime = 0;
					uiQueuePushButton();
				}
				else
				{
					char *msg;
					pclGetErrorString(sRestartError, SAFESTR(errormsg));
					msg = STACK_SPRINTF(FORMAT_OK(_("Error %d: %s. Restart in %u.")), sRestartError, cgettext(errormsg), timeleft);
					uiQueueDisplayStatusMsg(msg);
					printfColor(COLOR_RED|COLOR_BRIGHT, "%s\n", msg);
				}
			}
			break;
		}
	}
}

static int restartPatch(const ShardInfo_Basic *shard, bool bSetShardToAutoPatch, char **patchExtraFolders, int localeId)
{
	// Cancel file transfer
	acquirePatchCS();
	disconnectFromPatchServer(&sPatchClient);
	releasePatchCS();
	if (LauncherSetPatchingState(CL_STATE_LAUNCHERPAGELOADED))
	{
		return startPatch(shard, bSetShardToAutoPatch, patchExtraFolders, localeId);
	}
	// If we aren't in a valid state for this, just wait until the next update.
	return 0;
}

#define REGKEY_VERIFY_ON_NEXT_UPDATE		"VerifyOnNextUpdate"

// 0 = no verify
// 1 = ask verify
// 2 = force verify
bool SetVerifyLevel(const char *productName, int verifyLevel)
{
	return RegistryBackedIntSet(productName, REGKEY_VERIFY_ON_NEXT_UPDATE, verifyLevel, false /* bUseHistory */);
}

int GetVerifyLevel(const char *productName)
{
	int verifyLevel = 0; /* default */
	RegistryBackedIntGet(productName, REGKEY_VERIFY_ON_NEXT_UPDATE, &verifyLevel, false /* bUseHistory */);
	return verifyLevel;
}

#define REGKEY_DISABLE_MICROPATCHING		"DisableMicropatching"

bool SetDisableMicropatching(const char *productName, bool bDisableMicropatching)
{
	return RegistryBackedBoolSet(productName, REGKEY_DISABLE_MICROPATCHING, bDisableMicropatching, true /* bUseHistory */);
}

bool GetDisableMicropatching(const char *productName)
{
	bool disabledMicropatching = false; /* default */
	RegistryBackedBoolGet(productName, REGKEY_DISABLE_MICROPATCHING, &disabledMicropatching, true /* bUseHistory */);
	return disabledMicropatching;
}

#define REGKEY_LAST_SHARD					"LastShard"

bool SetLastShardPatchedDescriptor(const char *productName, const char *shardName)
{
	char shardDescriptor[512];
	LauncherFormShardDescriptor(SAFESTR(shardDescriptor), productName, shardName);
	return RegistryBackedStrSet(productName, REGKEY_LAST_SHARD, shardDescriptor, false /* bUseHistory */);
}

bool GetLastShardPatchedDescriptor(const char *productName, char *lastShardDescriptor, int lastShardDescriptorMaxLength)
{
	*lastShardDescriptor = 0; /* default */
	return RegistryBackedStrGet(productName, REGKEY_LAST_SHARD, lastShardDescriptor, lastShardDescriptorMaxLength, false /* bUseHistory */);
}

// this registry key is set by the installer for a fresh download, and cleared after the first fresh download completes
#define REGKEY_INITIAL_DOWNLOAD	"InitialDownload"

static bool SetIsInitialDownload(const char *productName, bool bIsInitialDownload)
{
	return RegistryBackedBoolSet(productName, REGKEY_INITIAL_DOWNLOAD, bIsInitialDownload, true /* bUseHistory */);
}

static bool GetIsInitialDownload(const char *productName)
{
	bool bIsInitialDownload = false; /* default */
	RegistryBackedBoolGet(productName, REGKEY_INITIAL_DOWNLOAD, &bIsInitialDownload, true /* bUseHistory */);
	return bIsInitialDownload;
}

void SetShardToAutoPatch(const ShardInfo_Basic *shard)
{
	sShardToAutoPatch = (ShardInfo_Basic *)shard;
}

const ShardInfo_Basic *GetShardToAutoPatch(void)
{
	return sShardToAutoPatch;
}

bool PatcherIsValid(void)
{
	bool retVal;
	acquirePatchCS();
	retVal = sPatchClient && sPatchClient->xferrer;
	releasePatchCS();
	return retVal;
}

void PatcherGetSuccessfulPatchRootFolder(char *rootFolder, int rootFolderMaxLength)
{
	acquirePatchCS();
	strncpy_s(rootFolder, rootFolderMaxLength, SAFESTR(sSuccessfulPatchRootFolder));
	releasePatchCS();
}

static void computeSpeedAvgs(PatchSpeedData *data, S64 *half, S64 *five, S64 *thirty)
{
	F64 total=0, cur;
	F32 curtime;
	int i, curi;

	for (i=0; i<PATCH_SPEED_SAMPLES; i++)
	{
		curi = ((data->head - 1 - i) + PATCH_SPEED_SAMPLES) % PATCH_SPEED_SAMPLES;
		curtime = data->times[curi];
		if (curtime <= 0)
			cur = 0;
		else
			cur = data->deltas[curi] / curtime;
		total += cur;
		if (i==0)
			*half = cur;
		else if (i==9)
			*five = total / 10;
		else if (i==59)
			*thirty = total / 60;
	}
}

// ------------------------

// Strategy for updating patching stats without worrying about critical sections
// PatcherClearStats() and PatcherUpdateStats() are called from patchTick(), and assume that mutex is grabbed.  Results are put in static char arrays for display at later point.
// PatcherGetStats() is called from UI thread, and will make copies of static char arrays without grabbing mutex.
// This is not perfect, but it is good enough.

static char sRootFolder[MAX_PATH] = {0};
static char sNetStats[MAX_PATH] = {0};
static char sReceivedStats[MAX_PATH] = {0};
static char sActualStats[MAX_PATH] = {0};
static char sLinkStats[MAX_PATH] = {0};
static char sIPAddress[64] = {0};

static void PatcherClearStats(void)
{
	sRootFolder[0] = 0;
	sNetStats[0] = 0;
	sReceivedStats[0] = 0;
	sActualStats[0] = 0;
	sLinkStats[0] = 0;
	strcpy(sIPAddress, "server = No link");
}

// note: sPatchClientMutex is acquired prior to the execution of this function
static void PatcherUpdateStats(void)
{
	F32 rec_num, tot_num, act_num;
	char *rec_units, *tot_units, *act_units;
	U32 rec_prec, tot_prec, act_prec;
	S64 half, five, thirty;

	assert(sPatchClient);

	sprintf(sRootFolder, "root = \"%s\"", sPatchClient->root_folder);

	humanBytes(sPatchClient->xferrer->net_bytes_free, &rec_num, &rec_units, &rec_prec);
	humanBytes(sPatchClient->xferrer->max_net_bytes, &tot_num, &tot_units, &tot_prec);
	sprintf(sNetStats, "net_bytes_free = %.*f%s/%.*f%s, %d%% HTTP", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, sPatcherHTTPPercent);

	computeSpeedAvgs(&sSpeedReceived, &half, &five, &thirty);
	humanBytes(half, &rec_num, &rec_units, &rec_prec);
	humanBytes(five, &tot_num, &tot_units, &tot_prec);
	humanBytes(thirty, &act_num, &act_units, &act_prec);
	sprintf(sReceivedStats, "Received: %.*f%s/%.*f%s/%.*f%s", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, act_prec, act_num, act_units);

	computeSpeedAvgs(&sSpeedActual, &half, &five, &thirty);
	humanBytes(half, &rec_num, &rec_units, &rec_prec);
	humanBytes(five, &tot_num, &tot_units, &tot_prec);
	humanBytes(thirty, &act_num, &act_units, &act_prec);
	sprintf(sActualStats, "Actual: %.*f%s/%.*f%s/%.*f%s", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, act_prec, act_num, act_units);

	computeSpeedAvgs(&sSpeedLink, &half, &five, &thirty);
	humanBytes(half, &rec_num, &rec_units, &rec_prec);
	humanBytes(five, &tot_num, &tot_units, &tot_prec);
	humanBytes(thirty, &act_num, &act_units, &act_prec);
	sprintf(sLinkStats, "Link: %.*f%s/%.*f%s/%.*f%s", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, act_prec, act_num, act_units);

	if (sPatchClient->link)
	{
		char ipbuf[64];
		linkGetIpStr(sPatchClient->link, SAFESTR(ipbuf));
		sprintf(sIPAddress, "server = %s", ipbuf);
	}
	else
	{
		strcpy(sIPAddress, "server = No link");
	}
}

void PatcherGetStats(
	char *rootFolder, int rootFolderMaxLength,
	char *netStats, int netStatsMaxLength,
	char *receivedStats, int receivedStatsMaxLength,
	char *actualStats, int actualStatsMaxLength,
	char *linkStats, int linkStatsMaxLength,
	char *ipAddress, int ipAddressMaxLength)
{
	assert(rootFolder);
	assert(netStats);
	assert(receivedStats);
	assert(actualStats);
	assert(linkStats);
	assert(ipAddress);

	strcpy_s(rootFolder, rootFolderMaxLength, sRootFolder);
	strcpy_s(netStats, netStatsMaxLength, sNetStats);
	strcpy_s(receivedStats, receivedStatsMaxLength, sReceivedStats);
	strcpy_s(actualStats, actualStatsMaxLength, sActualStats);
	strcpy_s(linkStats, linkStatsMaxLength, sLinkStats);
	strcpy_s(ipAddress, ipAddressMaxLength, sIPAddress);
}

// ------------------------

// Strategy for updating patching xfer data without worrying about critical sections
// PatcherUpdateXferData() is called from patchTick(), and assumes that mutex is grabbed.  Results are put in static data for display at later point.
// PatcherGetXferData() and PatcherGetXferDataCount() are called from UI thread, and will make copies of static data without grabbing mutex.
// This is not perfect, but it is good enough.

// note: sPatchClientMutex is acquired prior to the execution of this function
static void PatcherUpdateXferData(void)
{
	if (sPatchClient && sPatchClient->xferrer)
	{
		int i = 0;

		FOR_EACH_IN_EARRAY(sPatchClient->xferrer->xfers_order, PatchXfer, xfer)
			strcpy(sPatchXferData[i].path, xfer->filename_to_write);
			sPatchXferData[i].state = xferGetState(xfer);
			sprintf(sPatchXferData[i].blocks, "%u/%u", xfer->blocks_so_far, xfer->blocks_total);
			{
				if (xfer->blocks_total == 0)
				{
					strcpy(sPatchXferData[i].progress, "0.0");
				}
				else
				{
					sprintf(sPatchXferData[i].progress, "%.1f", 100.0 * xfer->blocks_so_far / xfer->blocks_total);
				}
			}

			{
				F32 req_num;
				char *req_units;
				U32 req_prec;

				humanBytes(xfer->bytes_requested, &req_num, &req_units, &req_prec);
				sprintf(sPatchXferData[i].requested, "%.*f%s", req_prec, req_num, req_units);
			}

			i++;
		FOR_EACH_END

		sPatchXferDataCount = i;
	}
	else
	{
		sPatchXferDataCount = 0;
	}
}

bool PatcherGetXferData(PatchXferData *xferData, int itemIndex)
{
	if (itemIndex < sPatchXferDataCount)
	{
		memcpy(xferData, &sPatchXferData[itemIndex], sizeof(PatchXferData));
		return true;
	}
	else
	{
		return false;
	}
}

int PatcherGetXferDataCount(void)
{
	return sPatchXferDataCount;
}

// ------------------------

static void updateSpeedData(PatchSpeedData *data, F32 time)
{
	S64 delta = data->cur - data->last;
	if (delta < 0)
	{
		delta = 0;
	}
	data->last = data->cur;
	data->deltas[data->head] = delta;
	data->times[data->head] = time;
	data->head = (data->head + 1) % PATCH_SPEED_SAMPLES;
}

void OVERRIDE_LATELINK_netreceive_socksRecieveError(NetLink *link, U8 code)
{
	// TODO, figure out a different way to notify the user when the proxy fails to connect
	UI_MessageBox(
		_("An error has occurred while connecting to the proxy server, if this happens again it may be helpful to try disabling the proxy and restarting the game client."),
		_("Proxy server error"),
		MB_OK|MB_ICONERROR);
}

static unsigned int __stdcall thread_Patch(void *userdata)
{
	EXCEPTION_HANDLER_BEGIN
	int errorlevel = 0;
	HRESULT ret;
	bool run = true;
	U32 deltaTimer = timerAlloc();
	PatcherSpawnData *spawnData = (PatcherSpawnData *)userdata;
	createPatcherComm(spawnData->productName);

	while (run)
	{
		CrypticLauncherCommand *cmd = NULL;
		bool bWaitingForContinueStartPatch = false;

		autoTimerThreadFrameBegin(__FUNCTION__);
		
		while (((ret = XLFQueueRemove(gQueueToPatchClient, &cmd)) == S_OK) || bWaitingForContinueStartPatch)
		{
			if (cmd)
			{
				ANALYSIS_ASSUME(cmd);
				switch (cmd->type)
				{
#pragma warning(suppress:6001) // /analyze " Using uninitialized memory '*cmd'"
				case CLCMD_START_PATCH:
					{
						StartPatchCommand *pCmdData = (StartPatchCommand *)cmd->ptr_value;
						if (gDebugMode)
						{
#pragma warning(suppress:6001) // /analyze " Using uninitialized memory '*pCmdData'"
							printf("Patch: Got command START_PATCH %s:%s\n", pCmdData->shard->pProductName, pCmdData->shard->pShardName);
						}
						errorlevel = startPatch(pCmdData->shard, pCmdData->bSetShardToAutoPatch, spawnData->patchExtraFolders, pCmdData->localeId);
						if (errorlevel != 0)
						{
							uiQueuePatchingFailedAndQuit(errorlevel);
							run = false;
						}
						else
						{
							bWaitingForContinueStartPatch = true;
						}
						free(cmd->ptr_value);
					}
					break;

				case CLCMD_CONTINUE_START_PATCH:
					{
						ContinueStartPatchCommand *pCmdData = (ContinueStartPatchCommand *)cmd->ptr_value;
						if (gDebugMode)
						{
#pragma warning(suppress:6001) // /analyze " Using uninitialized memory '*pCmdData'"
							printf("Patch: Got command CONTINUE_START_PATCH %s:%s\n", pCmdData->shard->pProductName, pCmdData->shard->pShardName);
						}
						continueStartPatch(pCmdData->shard, pCmdData->bVerifyPatch);
						bWaitingForContinueStartPatch = false;
						free(cmd->ptr_value);
					}
					break;

				case CLCMD_RESTART_PATCH:
					{
						StartPatchCommand *pCmdData = (StartPatchCommand *)cmd->ptr_value;
						if (gDebugMode)
						{
#pragma warning(suppress:6001) // /analyze " Using uninitialized memory '*pCmdData'"
							printf("Patch: Got command RESTART_PATCH %s:%s\n", pCmdData->shard->pProductName, pCmdData->shard->pShardName);
						}
						errorlevel = restartPatch(pCmdData->shard, pCmdData->bSetShardToAutoPatch, spawnData->patchExtraFolders, pCmdData->localeId);
						if (errorlevel != 0)
						{
							uiQueuePatchingFailedAndQuit(errorlevel);
							run = false;
						}
						else
						{
							bWaitingForContinueStartPatch = true;
						}
						free(cmd->ptr_value);
					}
					break;

				case CLCMD_DO_BUTTON_ACTION:
					{
						const ShardInfo_Basic *shard = (ShardInfo_Basic*)(cmd->ptr_value);
						if (gDebugMode)
						{
							printf("Patch: Got command DO_BUTTON_ACTION %s:%s\n", shard->pProductName, shard->pShardName);
						}
						switch (LauncherGetState())
						{
						case CL_STATE_ERROR:
							{
								StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
								pCmdData->shard = shard;
								pCmdData->bSetShardToAutoPatch = true;
								pCmdData->localeId = sLocaleId;
								patcherQueueStartPatch(pCmdData);
							}
							break;

						case CL_STATE_WAITINGFORPATCH:
							{
								if (LauncherSetPatchingState(CL_STATE_GETTINGFILES))
								{
									U32 size;
									uiQueueDisplayStatusMsg(_("Patching"));
									if (!isExternalTestCategory(shard->pShardCategoryName))
									{
										SetLastShardPatchedDescriptor(shard->pProductName, shard->pShardName);
									}

									acquirePatchCS();

									// Resized the reserved chunk to just over the size of the largest file.
									pclGetLargestFileSize(sPatchClient, &size);
									memTrackReserveMemoryChunk(size + 10*1024*1024);

									uiQueueSetButtonState(CL_BUTTONSTATE_CANCEL);
									if (sPatchAll || GetDisableMicropatching(shard->pProductName))
									{
										pclGetAllFiles(sPatchClient, NULL, NULL, NULL);
									}
									else
									{
										pclGetRequiredFiles(sPatchClient, true, false, NULL, NULL, NULL);
									}

									releasePatchCS();

									sElapsedLast = 0;
								}
							}
							break;

						case CL_STATE_GETTINGFILES:
							// if you are already getting files, and you press the button, you want to cancel the patch, and set it up to be ready to start.
							{
								StartPatchCommand *pCmdData = malloc(sizeof(StartPatchCommand)); // this is freed in the patcher thread after the msg is cracked open.
								pCmdData->shard = shard;
								pCmdData->bSetShardToAutoPatch = false; // do not auto patch - this is how we cancel here
								patcherQueueRestartPatch(pCmdData);
							}
							break;

						case CL_STATE_READY:
							// if you are in ready state, all files are patched, and the game is ready to run
							uiQueueStartLoginForGame((void *)shard); // have to strip const here
							break;

						case CL_STATE_GETTINGGAMETICKET:
							break; // Already starting the game, just ignore it.

						default:
							//assertmsg(0, "The button should be disabled");
							break;
						}
					}
					break;

				case CLCMD_STOP_THREAD:
					if (gDebugMode)
					{
						printf("Patch: Got command STOP_THREAD\n");
					}
					run = false;
					break;

				case CLCMD_FIX_STATE:
					if (gDebugMode)
					{
						printf("Patch: Got command FIX_STATE\n");
					}
					acquirePatchCS();
					if (sPatchClient && eaSize(&sPatchClient->state))
					{
						switch (sPatchClient->state[0]->call_state)
						{
						case STATE_SET_VIEW:
							LauncherSetPatchingState(CL_STATE_SETTINGVIEW);
							break;
						case STATE_GET_FILE_LIST:
						case STATE_GET_REQUIRED_FILES:
							LauncherSetPatchingState(CL_STATE_GETTINGFILES);
						}
					}
					releasePatchCS();
					break;

				default:
					assert(false); // unhandled command!
					break;
				}

				free(cmd);
				cmd = NULL;
			}

			if (bWaitingForContinueStartPatch)
			{
				// wait a bit then recheck the cmd queue.
				// this is for the case when CLCMD_START_PATCH is received, but CLCMD_CONTINUE_START_PATCH hasn't been received (yet)
				commMonitor(sPatchClientComm);
			}
		}
		assert(ret == XLOCKFREE_STRUCTURE_EMPTY);
		commMonitor(sPatchClientComm);

		patchTick(spawnData->productName, spawnData->patchExtraFolders);

		// Speed tracking
		if (timerElapsed(deltaTimer) > 0.5)
		{
			F32 time;
			acquirePatchCS();
			if (sPatchClient && sPatchClient->link)
			{
				sSpeedLink.cur = linkStats(sPatchClient->link)->recv.real_bytes;
			}
			time = timerElapsedAndStart(deltaTimer);
			updateSpeedData(&sSpeedReceived, time);
			updateSpeedData(&sSpeedActual, time);
			updateSpeedData(&sSpeedLink, time);
			releasePatchCS();
		}

		// Perform debugging filespec check, if requested.
		if (sDebugCheckFilespec)
		{
			acquirePatchCS();
			if (sPatchClient)
			{
				bool not_required;
				char *debug = NULL;
				PCL_ErrorCode error;

				estrStackCreate(&debug);
				error = pclIsNotRequired(sPatchClient, sDebugCheckFilespec, &not_required, &debug);
				if (error)
					printf("error checking filespec\n");
				else
					printf("%s: %s, %s\n", sDebugCheckFilespec, not_required ? "OPTIONAL" : "REQUIRED", debug);
				estrDestroy(&debug);
			}
			else
			{
				printf("No PCL client.\n");
			}
			releasePatchCS();

			SAFE_FREE(sDebugCheckFilespec);
		}

		autoTimerThreadFrameEnd();
	}
	timerFree(deltaTimer);

	// spawnData is created just before the patcher thread is spawned (PatcherSpawn())
	eaDestroyEx(&spawnData->patchExtraFolders, NULL); 
	free(spawnData); 
	return 0;
	EXCEPTION_HANDLER_END
}

