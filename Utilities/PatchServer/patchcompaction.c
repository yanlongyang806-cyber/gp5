#include "Alerts.h"
#include "earray.h"
#include "error.h"
#include "file.h"
#include "fileutil2.h"
#include "hoglib.h"
#include "hogutil.h"
#include "logging.h"
#include "patchcommonutils.h"
#include "patchcompaction.h"
#include "patchserver.h"
#include "patchserverdb.h"
#include "StringUtil.h"
#include "sysutil.h"
#include "timing.h"
#include "utils.h"
#include "wininclude.h"
#include "WorkerThread.h"
#include "zutils.h"
#include "patchhal.h"
#include "fileutil.h"

// Compaction cleanup background thread
static WorkerThread *compaction_cleanup_thread = NULL;

// Background thread commands.
enum CompactionCleanupThreadCmdMsg
{
	CompactionCleanupThread_Cleanup = WT_CMD_USER_START,
	CompactionCleanupThread_CleanupDone,
	CompactionCleanupThread_CleanupFailed,
};

static void thread_CompactionCleanup(void *user_data, void *data, WTCmdPacket *packet);
static void CompactionCleanupDone(void *user_data, void *data, WTCmdPacket *packet);
static void CompactionCleanupFailed(void *user_data, void *data, WTCmdPacket *packet);

// Initialize background thread.
static void initCompactionCleanupThread(void)
{
	if (!compaction_cleanup_thread)
	{
		compaction_cleanup_thread = wtCreate(16384, 16, NULL, "CompactionCleanupThread");
		wtRegisterCmdDispatch(compaction_cleanup_thread, CompactionCleanupThread_Cleanup, thread_CompactionCleanup);
		wtRegisterMsgDispatch(compaction_cleanup_thread, CompactionCleanupThread_CleanupDone, CompactionCleanupDone);
		wtRegisterMsgDispatch(compaction_cleanup_thread, CompactionCleanupThread_CleanupFailed, CompactionCleanupFailed);
		wtSetThreaded(compaction_cleanup_thread, true, 0, false);
		wtStart(compaction_cleanup_thread);
	}
}

typedef struct CompactHogsState 
{
	char *file;
	QueryableProcessHandle *pQPH;
	PatchServerDb *serverdb;
	time_t start;
	bool alerted;
	bool needsCopy;
	time_t timeOfFirstAttempt;
	time_t lastCleanupFailureTime;
	bool inBackground;
} CompactHogsState;

static CompactHogsState **g_compact_hoggs_state = NULL;
static CompactHogsState *g_compact_hoggs_state_current = NULL;
static CRITICAL_SECTION g_compact_hoggs_cs;

static CompactHogsState **g_compact_hoggs_to_cleanup = NULL;

AUTO_RUN;
void patchCompactionAutoRun(void)
{
	InitializeCriticalSection(&g_compact_hoggs_cs);
}

// Add a 10 minute stall to compaction
static int gSlowCompact = 0;

AUTO_CMD_INT(gSlowCompact, slowCompaction);

#define SLOW_COMPACT_DELAY 10 * 60 * 1000 // 10 minutes in milliseconds

// Compact even the most recent files. Primarily for testing purposes.
static int gAggressiveCompaction = 0;

AUTO_CMD_INT(gAggressiveCompaction, aggressiveCompaction);

// Verify files written from delayedwrite.hogg
static int gVerifyDelayedWrite = 0;
AUTO_CMD_INT(gVerifyDelayedWrite, verifyDelayedWrites);

// Verify that compacted files now do not need compaction
static int gVerifyCompactionSuccess = 1;
AUTO_CMD_INT(gVerifyCompactionSuccess, verifyCompactionSuccess);

static int gCompactionHandleAlertTimeout = 10*60;
AUTO_CMD_INT(gCompactionHandleAlertTimeout, compactionHandleAlertTimeout);

// Remove compaction backups after compaction is complete.
static bool gRemoveCompactionBackups = true;
AUTO_CMD_INT(gRemoveCompactionBackups, removeCompactionBackups);

// Lower priority of process.
static void setLowPriority()
{
	// On Vista and better, this will lower the disk I/O priority of the current process, but it will not
	// cause it to become completely starved.
	// After testing, this option appears to lower the disk priority too low; compaction will take a very long time
	// to complete.
	//SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);

	// Hopefully, this will work better.  It seems like it shouldn't affect disk priority at all, but we'll see.
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
}

bool patchcompactionRenameWithRetry(char *cwd, char *from, char *to)
{
	makeDirectoriesForFile(STACK_SPRINTF("%s/%s", cwd, to));
	if(patchRenameWithAlert(from, to)){
		int i;

		for(i=0; i<1000; i++)
		{
			Sleep(1);
			if(!rename(from, to)){
				return true;
				break;
			}
		}
	}
	else
		return true;

	return false;
}

// Run in the context of a child process, perform compaction.
AUTO_COMMAND ACMD_NAME(compact) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmd_compact(char *serverdb_name, char *file, int slowCompact)
{
	int ret;
	char hogg_file[MAX_PATH], temp_file[MAX_PATH];

	setConsoleTitleWithPid("Compactor - Defragging");

	// Lower priority so compaction does not overly impact the performance of the main process.
	setLowPriority();

	if(slowCompact)
	{
		printf("Starting slow compaction delay on file %s\\%s.\n",NULL_TO_EMPTY(serverdb_name), NULL_TO_EMPTY(file));
		Sleep(SLOW_COMPACT_DELAY);
	}

	sprintf(hogg_file, "./%s/%s", serverdb_name, file);
	changeFileExt(hogg_file, ".temp.hogg", temp_file);
	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "StartingCompaction", ("filename", "%s", hogg_file));
	ret = hogDefrag(hogg_file, SMALL_PATCHSERVER_HOG_DATALIST_JOURNAL_SIZE, HogDefrag_NoRename|HogDefrag_Tight|HogDefrag_SkipMutex);
	if(ret != 0)
	{
		// Failure of the defrag itself
		logFlush();
		logCloseAllLogs();
		exit(1);
	}

	setConsoleTitleWithPid("Compactor - Verifying diff");

	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactionSuccessful", ("filename", "%s", hogg_file));
	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "StartingDiff", ("filename", "%s", hogg_file));
	ret = hogDiff(hogg_file, temp_file, 1, HogDiff_SkipMutex);
	if(ret != 0)
	{
		// Diff failed, use rv 2 to signal the main server that it should alertF
		logFlush();
		logCloseAllLogs();
		exit(2);
	}

	setConsoleTitleWithPid("Compactor - Finishing");

	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "DiffSuccessful", ("filename", "%s", hogg_file));
	logFlush();
	logCloseAllLogs();
	exit(3); // exit 3 to differentiate between a successful exit and a crash
}

bool filenameIsStorageHog(const char *name)
{
	int i;
	size_t length = strlen(name);
	if(length != 15)
		return false;
	for(i = 0; i < 10; ++i)
	{
		if(name[i] < '0' || name[i] > '9')
			return false;
	}
	if(!strEndsWith(name, ".hogg"))
		return false;

	return true;
}

// Initiate asynchronous compaction.
void patchcompactionCompactHogsAsyncStart(void)
{
	char buf[MAX_PATH];
	U32 num_files;

	PERFINFO_AUTO_START_FUNC();

	if(g_compact_hoggs_to_cleanup && eaSize(&g_compact_hoggs_to_cleanup))
	{
		PERFINFO_AUTO_STOP();
		return; // Compaction still in progress, no reason to start another one.
	}

	if(g_compact_hoggs_state)
	{
		if(eaSize(&g_compact_hoggs_state))
		{
			PERFINFO_AUTO_STOP();
			return; // Compaction still in progress, no reason to start another one.
		}
	}
	else
		eaCreate(&g_compact_hoggs_state);

	initCompactionCleanupThread();

	FOR_EACH_IN_EARRAY(g_patchserver_config.serverdbs, PatchServerDb, serverdb)
	{
		char path[MAX_PATH];
		char **files;

		// Don't compact ignored DBs
		if(g_patchserver_config.prune_config && eaFindString(&g_patchserver_config.prune_config->ignore_projects, serverdb->name) != -1)
			continue;

		loadstart_printf("Setting up compaction for %s...", serverdb->name);
		num_files = eaSize(&g_compact_hoggs_state);
		sprintf(path, "./%s", serverdb->name);
		files = fileScanDir(path);
		FOR_EACH_IN_EARRAY(files, char, file)
		{
			CompactHogsState *state;
			char *name = file + strlen(serverdb->name) + 2 + 1;

			if(!filenameIsStorageHog(name))
			{
				free(file); 
				continue;
			}

			if(!gAggressiveCompaction)
			{
				patchHALGetHogFileNameFromTimeStamp(SAFESTR(buf), NULL, getCurrentFileTime());
				if(stricmp(buf, name)==0) 
				{ 
					free(file); 
					continue; 
				}
				patchHALGetHogFileNameFromTimeStamp(SAFESTR(buf), NULL, getCurrentFileTime()-HOG_TIMESTAMP_DIVISION);
				if(stricmp(buf, name)==0) 
				{ 
					free(file); 
					continue; 
				}
			}

			state = calloc(1, sizeof(CompactHogsState));
			state->file = strdup(name);
			state->serverdb = serverdb;
			eaPush(&g_compact_hoggs_state, state);
			free(file);
		}
		FOR_EACH_END;
		eaDestroy(&files);
		loadend_printf("(%u)", eaSize(&g_compact_hoggs_state)-num_files);
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}

// Return true if compaction is currently running.
bool patchcompactionCompactHogsAsyncIsRunning(void)
{
	 return g_compact_hoggs_state_current || eaSize(&g_compact_hoggs_state) || eaSize(&g_compact_hoggs_to_cleanup);
}

// Abort compaction.
void patchcompactionAsyncAbort(void)
{
	FOR_EACH_IN_EARRAY(g_compact_hoggs_state, CompactHogsState, chs)
	{
		SAFE_FREE(chs->file);
		SAFE_FREE(chs);
	}
	FOR_EACH_END;

	eaDestroy(&g_compact_hoggs_state);

	if(g_compact_hoggs_state_current)
	{
		U32 hoggKey = strtol(g_compact_hoggs_state_current->file, NULL, 10);
		HogFile *hogg = patchHALGetCachedHandle(g_compact_hoggs_state_current->serverdb, hoggKey, true);
		KillQueryableProcess(&g_compact_hoggs_state_current->pQPH);
		if(hogg)
		{
			eaPush(&g_compact_hoggs_to_cleanup, g_compact_hoggs_state_current);
		}
		else
		{
			SAFE_FREE(g_compact_hoggs_state_current->file);
			SAFE_FREE(g_compact_hoggs_state_current);
			g_compact_hoggs_state_current = NULL;
		}
	}
}

bool patchcompactionCleanUpTempHog_CB(HogFile *handle, HogFileIndex index, const char* filename, void * userData)
{
	HogFile *hogg = (HogFile*)userData;
	HogFile *tempHogg = handle;

	if(!hogg)
		return true;

	PERFINFO_AUTO_START_FUNC();
	if(strEndsWith(filename,".del"))
	{
		char *targetFilename = strdup(filename);
		size_t namelength = strlen(targetFilename);
		int errorCode;
		if(namelength > 4)
		{
			targetFilename[namelength-4] = 0;
			errorCode = hogFileModifyDeleteNamed(hogg, targetFilename);
			targetFilename[namelength-4] = '.';
			if(errorCode == 0)
				hogFileModifyDeleteNamed(tempHogg, filename);
		}
		free(targetFilename);
	}
	else 
	{
		NewPigEntry *entry = calloc(1, sizeof(NewPigEntry));
		U32 byte_count;
		U32 buf_size;
		U32 compressedChecksum;
		int errorCode;
		// Copy the files
		entry->fname = filename;
		entry->timestamp = hogFileGetFileTimestamp(tempHogg, index);
		entry->header_data = hogFileGetHeaderData(tempHogg, index, &entry->header_data_size);
		entry->checksum[0] = hogFileGetFileChecksum(tempHogg, index);
		hogFileGetSizes(tempHogg, index, &entry->size, &entry->pack_size);
//		Commenting out this assert, because there are old files on AssetMaster that are not compressed. 
//		Once a cleanup pass has been performed to enforce this, we can turn the assert back on.
//		assertmsgf(entry->pack_size > 0 || entry->size == 0, "File %s written to %s uncompressed", filename, hogFileGetArchiveFileName(tempHogg));

		buf_size = entry->pack_size;
		entry->data = malloc(entry->pack_size);
		entry->must_pack = 1;
		byte_count = hogFileExtractRawBytes(tempHogg, index, entry->data, 0, entry->pack_size, false, 0);
		assert(byte_count == entry->pack_size);

		if(gVerifyDelayedWrite)
			compressedChecksum = patchChecksum(entry->data, entry->pack_size);

		errorCode = hogFileModifyUpdateNamedSync2(hogg, entry);

		if(gVerifyDelayedWrite)
		{
			NewPigEntry *checkentry = calloc(1, sizeof(NewPigEntry));
			HogFileIndex destIndex;
			U32 destChecksum;

			destIndex = hogFileFind(hogg, filename);
			// Copy the files
			checkentry->fname = filename;
			checkentry->timestamp = hogFileGetFileTimestamp(hogg, destIndex);
			checkentry->header_data = hogFileGetHeaderData(hogg, destIndex, &checkentry->header_data_size);
			checkentry->checksum[0] = hogFileGetFileChecksum(hogg, destIndex);
			hogFileGetSizes(hogg, destIndex, &checkentry->size, &checkentry->pack_size);

			buf_size = checkentry->pack_size;
			checkentry->data = malloc(checkentry->pack_size);
			checkentry->must_pack = 1;
			byte_count = hogFileExtractRawBytes(hogg, destIndex, checkentry->data, 0, checkentry->pack_size, false, 0);
			assert(byte_count == entry->pack_size);
			destChecksum = patchChecksum(checkentry->data, checkentry->pack_size);
			assert(destChecksum == compressedChecksum);
			free(checkentry->data);
			free(checkentry);
		}
		if(errorCode == 0)
			hogFileModifyDeleteNamed(tempHogg, filename);
	}

	PERFINFO_AUTO_STOP();
	return true;
}

struct CompactionCleanupData
{
	U32 hoggKey;
	HALHogFile *halHog;
	HALHogFile *tempHalHog;
	char serverdbName[MAX_PATH];
	void *userData;
} CompactionCleanupData;

static void thread_CompactionCleanup(void *user_data, void *data, WTCmdPacket *packet)
{
	F32 totalTime;
	U32 cleanupTimer;
	struct CompactionCleanupData *cleanupData = data;
	struct CompactionCleanupData result = {0};

	if(!devassert(cleanupData))
	{
		//CleanupFailed due to no data
		wtQueueMsg(compaction_cleanup_thread, CompactionCleanupThread_CleanupFailed, &result, sizeof(result));
		return;
	}

	if(!(devassert(cleanupData->tempHalHog && cleanupData->halHog)))
	{
		//CleanupFailed due to invalid hog pointers
		wtQueueMsg(compaction_cleanup_thread, CompactionCleanupThread_CleanupFailed, &result, sizeof(result));
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	result.hoggKey = cleanupData->hoggKey;
	result.halHog = cleanupData->halHog;
	result.tempHalHog = cleanupData->tempHalHog;
	sprintf_s(SAFESTR(result.serverdbName), "%s", cleanupData->serverdbName);
	result.userData = cleanupData->userData;

	cleanupTimer = timerAlloc();

	if(cleanupTimer >= 0)
		timerStart(cleanupTimer);

	hogScanAllFiles(result.tempHalHog->hog, patchcompactionCleanUpTempHog_CB, result.halHog->hog);

	if(cleanupTimer >= 0)
	{
		totalTime = timerElapsed(cleanupTimer);
		timerFree(cleanupTimer);
		servLog(LOG_PATCHSERVER_COMPACT, "CompactionCleanup", "FileName %s Duration %f", hogFileGetArchiveFileName(result.halHog->hog), totalTime);
	}

	wtQueueMsg(compaction_cleanup_thread, CompactionCleanupThread_CleanupDone, &result, sizeof(result));
	PERFINFO_AUTO_STOP();
}

static void CompactionCleanupFailed(void *user_data, void *data, WTCmdPacket *packet)
{
	struct CompactionCleanupData *result = data;
	CompactHogsState *cur = result->userData;

	PERFINFO_AUTO_START_FUNC();

	cur->lastCleanupFailureTime = time(NULL);
	cur->inBackground = false;
	patchHALHogFileDestroy(result->halHog, false);
	patchHALHogFileDestroy(result->tempHalHog, false);

	PERFINFO_AUTO_STOP();
}

static void CompactionCleanupDone(void *user_data, void *data, WTCmdPacket *packet)
{
	// In msg callback on main thread
	char oldName[MAX_PATH];
	HogFile *tempHogg;
	struct CompactionCleanupData *result = data;
	CompactHogsState *cur = result->userData;

	PERFINFO_AUTO_START_FUNC();

	cur->inBackground = false;
	patchHALHogFileDestroy(result->halHog, false);
	patchHALHogFileDestroy(result->tempHalHog, false);
	tempHogg = patchHALGetCachedHandle(cur->serverdb, result->hoggKey, true);

	if(patchHALHogFileInUse(tempHogg) || hogFileGetNumUserFiles(tempHogg))
	{
		cur->lastCleanupFailureTime = time(NULL);
		PERFINFO_AUTO_STOP();
		return;
	}

	sprintf(oldName, "%s", hogFileGetArchiveFileName(tempHogg));

	patchHALCloseHog(cur->serverdb, tempHogg, true);

	if(fileForceRemove(oldName))
	{
		ErrorOrAlert("PATCHDB_COMPACT_CLEANUP_REMOVE_FAILED", "Failed to remove %s after cleanup", oldName);
	}

	PERFINFO_AUTO_STOP();
}

// returns true if cur should be removed from the cleanup array.
static bool patchcompactionCleanUpTempHog(CompactHogsState *cur, PatchServerDb *serverdb, int hoggKey)
{
	HALHogFile *halhog;
	HALHogFile *tempHalHog;
	bool mustCloseHog = false;
	struct CompactionCleanupData data = {0};

	PERFINFO_AUTO_START_FUNC();

	tempHalHog = patchHALGetHogHandle(serverdb, hoggKey, false, true, true, true, false, NULL);

	if(!tempHalHog)
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	halhog = patchHALGetHogHandle(serverdb, hoggKey, false, true, true, false, true, NULL);

	if(!halhog)
	{
		AssertOrAlert("PATCHDB_COMPACT_CLEANUP", "Trying to cleanup after compaction of %s/%u.hogg, but cannot open it.", serverdb->name, hoggKey);
		patchHALHogFileDestroy(tempHalHog, false);
		cur->lastCleanupFailureTime = time(NULL);
		PERFINFO_AUTO_STOP();
		return false;
	}

	data.hoggKey = hoggKey;
	data.halHog = halhog;
	data.tempHalHog = tempHalHog;

	sprintf_s(SAFESTR(data.serverdbName), "%s", serverdb->name);
	data.userData = cur;
	cur->inBackground = true;
	wtQueueCmd(compaction_cleanup_thread, CompactionCleanupThread_Cleanup, &data, sizeof(data));

	PERFINFO_AUTO_STOP();
	return false;
}

static bool patchcompactionRenameCompactedHog(CompactHogsState *cur, int hoggKey)
{
	// Success, move the files around.
	char curName[MAX_PATH];
	char backupName[MAX_PATH];
	char tempName[MAX_PATH];
	char compactedName[MAX_PATH];
	char cwd[MAX_PATH];
	S32 renamed = 0;
	HogFile *hogg;
	bool retval = true;

	PERFINFO_AUTO_START_FUNC();
				
	// Remove the hogg from the cache and close the old handle.
	if(hogg = patchHALGetCachedHandle(cur->serverdb, hoggKey, false))
	{
		patchHALCloseHog(cur->serverdb, hogg, false);
	}

	// Compute some other needed paths
	assert(fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)));
	sprintf(curName, "./%s/%s", cur->serverdb->name, cur->file);
	sprintf(backupName, "./compacted/%s/%u-%s", cur->serverdb->name, getCurrentFileTime(), cur->file);
	changeFileExt(curName, ".temp.hogg", tempName);
	changeFileExt(curName, ".compacted.hogg", compactedName);

	if (fileExists(compactedName))
	{
		if (fileForceRemove(compactedName))
		{
			ErrorOrAlert("PATCHDB_COMPACT_FILE_REMOVE_FAILED", "Failed to remove old %s, when performing post-compaction renames.", compactedName);
			SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "OldFileCleanupFailed", ("compactedName", "%s", compactedName));
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	// Make sure we can rename the compacted hog before we start
	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename1", ("tempName", "%s", tempName) ("compactedName", "%s", compactedName));
	renamed = patchcompactionRenameWithRetry(cwd, tempName, compactedName);

	if(!renamed)
	{
		SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename1Failed", ("tempName", "%s", tempName) ("compactedName", "%s", compactedName));
		PERFINFO_AUTO_STOP();
		return true;
	}

	// Make a backup of the original file in the compacted folder
	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename2", ("curName", "%s", curName) ("backupName", "%s", backupName));
	renamed = patchcompactionRenameWithRetry(cwd, curName, backupName);

	if(!renamed)
	{
		SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename2Failed", ("curName", "%s", curName) ("backupName", "%s", backupName));
		PERFINFO_AUTO_STOP();
		return true;
	}	

	// Move the compacted file into place
	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename3", ("compactedName", "%s", compactedName) ("curName", "%s", curName));
	if(patchRenameWithAlert(compactedName, curName))
	{
		// If this fails, attempt to move the original file back into place.
		SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename4", ("backupName", "%s", backupName) ("curName", "%s", curName));
		renamed = patchcompactionRenameWithRetry(cwd, backupName, curName);

		if(!renamed)
		{
			SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactRename4Failed", ("backupName", "%s", backupName) ("curName", "%s", curName));
			assertmsgf(	0,
				"Can't rename %s to %s\n",
				compactedName,
				curName);
		}
	}

	SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "CompactDone", ("hog", "%s", cur->serverdb->name) ("file", "%s", cur->file)
		("duration", "%"FORM_LL"d", time(NULL) - cur->start));

	if(gVerifyCompactionSuccess)
	{
		HALHogFile *halhog;
		PERFINFO_AUTO_START("VerifyCompactionSuccess", 1);
		halhog = patchHALGetHogHandle(cur->serverdb, hoggKey, false, true, false, false, true, NULL);
		if(halhog && halhog->hog && hogFileShouldDefrag(halhog->hog))
		{
			ErrorOrAlert("PATCHDB_COMPACT_STILL_NEEDS_COMPACT", "Compacted %s/%s, but it still needs to be compacted.",cur->serverdb->name, cur->file);
		}
		patchHALHogFileDestroy(halhog, false);
		PERFINFO_AUTO_STOP();
	}

	// Remove the compaction backup.
	// For older pre-1.6 Patch Servers, backups were never automatically removed.
	if (gRemoveCompactionBackups)
	{
		if(fileForceRemove(backupName))
		{
			ErrorOrAlert("PATCHDB_COMPACT_CLEANUP_REMOVE_FAILED", "Failed to remove backup %s after compaction", backupName);
		}
	}


	PERFINFO_AUTO_STOP();
	return true;
}

// Cleanup temp file from compaction.
void patchcompactionCleanUpAsyncTick(void)
{
	int i = 0;

	PERFINFO_AUTO_START_FUNC();
	if(!g_compact_hoggs_to_cleanup || eaSize(&g_compact_hoggs_to_cleanup)==0)
	{
		PERFINFO_AUTO_STOP();
		return; // No temporary hogs to clean up
	}

	if (compaction_cleanup_thread)
		wtMonitor(compaction_cleanup_thread);

	for(i = 0; i < eaSize(&g_compact_hoggs_to_cleanup); i++)
	{
		CompactHogsState *cur = g_compact_hoggs_to_cleanup[i];
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*cur->file'"
		U32 hoggKey = strtol(cur->file, NULL, 10);

		// It has not been long enough since our last failure to cleanup.
		if(cur->lastCleanupFailureTime && time(NULL) - cur->lastCleanupFailureTime < gCompactionHandleAlertTimeout)
			continue;

		// Waiting for the background thread to finish with it.
		if(cur->inBackground)
			continue;

		if(cur->needsCopy)
		{
			HogFile *hogg;

			patchcompactionLock();

			hogg = patchHALGetCachedHandle(cur->serverdb, hoggKey, false);

			if(hogg && patchHALHogFileInUse(hogg))
			{
				if(cur->timeOfFirstAttempt)
				{
					if(!cur->alerted && time(NULL) - cur->timeOfFirstAttempt > gCompactionHandleAlertTimeout)
					{
						// Alert and give up
						HogFile *tempHogg = patchHALGetCachedHandle(cur->serverdb, hoggKey, true);
						ErrorOrAlert("PATCHDB_DELAYEDWRITE_FAIL", "Copy of %s/%s blocked by shared handles.", cur->serverdb->name, cur->file);	
						if(tempHogg)
						{
							cur->needsCopy = false;
							cur->timeOfFirstAttempt = 0;
							cur->alerted = false;
						}
						else
						{
							eaRemoveFast(&g_compact_hoggs_to_cleanup, i);
							free(cur->file);
							free(cur);
						}

						patchcompactionUnlock();
						PERFINFO_AUTO_STOP();
						return;
					}
				}
				else
				{
					cur->timeOfFirstAttempt = time(NULL);
				}
			}
			else
			{
				if(patchcompactionRenameCompactedHog(cur, hoggKey))
				{
					HogFile *tempHogg = patchHALGetCachedHandle(cur->serverdb, hoggKey, true);
					if(tempHogg)
					{
						cur->needsCopy = false;
						cur->timeOfFirstAttempt = 0;
						cur->alerted = false;
					}
					else
					{
						eaRemoveFast(&g_compact_hoggs_to_cleanup, i);
						free(cur->file);
						free(cur);
					}
				}

				patchcompactionUnlock();
				PERFINFO_AUTO_STOP();
				return;
			}
			patchcompactionUnlock();
		}
		else
		{
			HogFile *tempHogg;

			patchcompactionLock();
			tempHogg = patchHALGetCachedHandle(cur->serverdb, hoggKey, true);

			if(!tempHogg)
			{
				eaRemoveFast(&g_compact_hoggs_to_cleanup, i);
				free(cur->file);
				free(cur);
			}
			else if(patchHALHogFileInUse(tempHogg))
			{
				if(cur->timeOfFirstAttempt)
				{
					if(!cur->alerted && time(NULL) - cur->timeOfFirstAttempt > gCompactionHandleAlertTimeout)
					{
						ErrorOrAlert("PATCHDB_DELAYEDWRITE_FAIL", "Cleanup of %s/%s blocked by shared handles.", cur->serverdb->name, cur->file);	
						cur->alerted = true;
					}
				}
				else
				{
					cur->timeOfFirstAttempt = time(NULL);
				}
			}
			else
			{
				if(patchcompactionCleanUpTempHog(cur, cur->serverdb, hoggKey))
				{
					eaRemoveFast(&g_compact_hoggs_to_cleanup, i);
					free(cur->file);
					free(cur);
				}

				patchcompactionUnlock();
				PERFINFO_AUTO_STOP();
				return;
			}
			patchcompactionUnlock();
		}
	}
	PERFINFO_AUTO_STOP();
}

// Process pending hogg compaction.
void patchcompactionCompactHogsAsyncTick(void)
{
	PERFINFO_AUTO_START_FUNC();
	if(!g_compact_hoggs_state_current &&
	   (!g_compact_hoggs_state || eaSize(&g_compact_hoggs_state)==0)
	  )
	{
		PERFINFO_AUTO_STOP();
		return; // Not currently compacting
	}

	if(g_compact_hoggs_state_current)
	{
		CompactHogsState *cur = g_compact_hoggs_state_current;
		int ret;
		U32 hoggKey = strtol(cur->file, NULL, 10);
		if(!cur->pQPH)
		{
			if(patchHALGetCachedHandle(cur->serverdb, hoggKey, true))
			{
				cur->timeOfFirstAttempt = 0;
				cur->alerted = false;
				cur->needsCopy = false;
				eaPush(&g_compact_hoggs_to_cleanup, cur);
			}
			else
			{
				free(cur->file);
				free(cur);
			}
			g_compact_hoggs_state_current = NULL;
		}
		else if(QueryableProcessComplete(&cur->pQPH, &ret))
		{
			switch (ret)
			{
			case 3: 
				cur->needsCopy = true;
				break;
			case 2:
				AssertOrAlert("PATCHDB_COMPACT_ERROR", "Error during hogg diff on %s/%s. (exit code %d)", cur->serverdb->name, cur->file, ret);
				break;
			case 1:
				AssertOrAlert("PATCHDB_COMPACT_ERROR", "Error during hogg defrag on %s/%s. (exit code %d)", cur->serverdb->name, cur->file, ret);
				break;
			case 0:
				AssertOrAlert("PATCHDB_COMPACT_ERROR", "Compaction process crashed while processing %s/%s. (exit code %d)", cur->serverdb->name, cur->file, ret);
				break;
			default:
				AssertOrAlert("PATCHDB_COMPACT_ERROR", "Unknown error while compacting %s/%s. (exit code %d)", cur->serverdb->name, cur->file, ret);
				break;
			}

			printfColor((ret == 3?COLOR_GREEN:COLOR_RED)|COLOR_BRIGHT, "Done compacting %s/%s\n", cur->serverdb->name, cur->file);
			if(patchHALGetCachedHandle(cur->serverdb, hoggKey, true) || cur->needsCopy)
			{
				cur->timeOfFirstAttempt = 0;
				cur->alerted = false;
				eaPush(&g_compact_hoggs_to_cleanup, cur);
			}
			else
			{
				free(cur->file);
				free(cur);
			}
			g_compact_hoggs_state_current = NULL;
		}
		else
		{
														
			const time_t max_compact_time = 28800;		// 8 hours. Empirically, this is longer than all reasonable compactions.
			time_t delta = time(NULL) - cur->start;
			if(delta > max_compact_time && !cur->alerted)
			{
				TriggerAlert("PATCHDB_COMPACT_TIMEOUT", STACK_SPRINTF("Compactor process for %s/%s has been running for %"FORM_LL"d seconds", cur->serverdb->name, cur->file, delta), ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
					0, GLOBALTYPE_PATCHSERVER, 0, GLOBALTYPE_PATCHSERVER, 0, g_patchserver_config.displayName, 0);
				cur->alerted = true;
			}
		}
	}
	else
	{
		// Spawn a new compactor process
		char cmd[1024], hog_file[MAX_PATH];
		CompactHogsState *cur;
		HogFile *hogg = NULL;
		bool should_close = false;

		PERFINFO_AUTO_START("StartCompactor", 1);

		g_compact_hoggs_state_current = cur = eaPop(&g_compact_hoggs_state);

		sprintf(hog_file, "./%s/%s", cur->serverdb->name, cur->file);

		if(hogg = patchHALGetCachedHandle(cur->serverdb, strtol(cur->file, NULL, 10), false))
		{
			hogFileModifyFlush(hogg);
		}
		else
		{
			hogg = hogFileReadReadOnlySafeEx(hog_file, NULL, PIGERR_PRINTF, NULL, HOG_NOCREATE|HOG_READONLY, PATCHSERVER_HOG_DATALIST_JOURNAL_SIZE);
			should_close = true;
		}

		if(!hogg)
		{
			// Unable to open the hogg file somehow.
			log_printf(LOG_PATCHSERVER_COMPACT, "Unable to read %s", hog_file);
			printfColor(COLOR_RED|COLOR_BRIGHT, "Unable to read %s/%s (%u remaining)\n", cur->serverdb->name, cur->file, eaSize(&g_compact_hoggs_state));
			free(cur->file);
			free(cur);
			g_compact_hoggs_state_current = NULL;
		}
		else
		{
			bool bBlocked = false;
			if(hogFileGetNumUserFiles(hogg) == 0)
			{
				if(patchHALHogFileInUse(hogg))
				{
					bBlocked = true;
				}
				else
				{
					// Move to compaction directory
					char oldName[MAX_PATH];
					char curName[MAX_PATH];
					char cwd[MAX_PATH];
	
					assert(fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)));
					sprintf(curName, "./%s/%s", cur->serverdb->name, cur->file);
					sprintf(oldName, "./compacted/%s/%u-%s", cur->serverdb->name, getCurrentFileTime(), cur->file);

					SERVLOG_PAIRS(LOG_PATCHSERVER_COMPACT, "EmptyFileRename", ("curName", "%s", curName) ("oldName", "%s", oldName));
					log_printf(LOG_PATCHSERVER_COMPACT, "Moving empty file %s", hog_file);
					printfColor(COLOR_GREEN|COLOR_BRIGHT, "Moving %s/%s (%u remaining)\n", cur->serverdb->name, cur->file, eaSize(&g_compact_hoggs_state));
					if(should_close)
						hogFileDestroy(hogg, true);
					else
						patchHALCloseHog(cur->serverdb, hogg, false);

					should_close = false;

					if(!patchcompactionRenameWithRetry(cwd, curName, oldName))
					{
						printf("Failed to move %s to %s.\n", curName, oldName);
					}
					free(cur->file);
					free(cur);
					g_compact_hoggs_state_current = NULL;
				}
			}
			else if(hogFileShouldDefrag(hogg))
			{
				if(patchHALHogFileInUse(hogg))
				{
					bBlocked = true;
				}
				else
				{
					sprintf(cmd, "%s -compact %s %s %d", getExecutableName(), cur->serverdb->name, cur->file, gSlowCompact);
					log_printf(LOG_PATCHSERVER_COMPACT, "Starting %s", cmd);
					printfColor(COLOR_GREEN|COLOR_BRIGHT, "Compacting %s/%s (%u remaining)\n", cur->serverdb->name, cur->file, eaSize(&g_compact_hoggs_state));
					cur->pQPH = StartQueryableProcess(cmd, NULL, false, true, false, NULL);
					cur->start = time(NULL);
					cur->alerted = false;
				}
			}
			else
			{
				log_printf(LOG_PATCHSERVER_COMPACT, "Skipping %s", hog_file);
				printfColor(COLOR_GREEN|COLOR_BRIGHT, "Skipping %s/%s (%u remaining)\n", cur->serverdb->name, cur->file, eaSize(&g_compact_hoggs_state));
				free(cur->file);
				free(cur);
				g_compact_hoggs_state_current = NULL;
			}

			if(bBlocked)
			{
				if(cur->timeOfFirstAttempt)
				{
					if(!cur->alerted && time(NULL) - cur->timeOfFirstAttempt > gCompactionHandleAlertTimeout)
					{
						ErrorOrAlert("PATCHDB_COMPACT_ABORT", "Compaction of %s/%s blocked by shared handles. Giving up.", cur->serverdb->name, cur->file);
						cur->alerted = true;
					}
				}
				else
				{
					cur->timeOfFirstAttempt = time(NULL);
				}

				if(!cur->alerted)
					eaInsert(&g_compact_hoggs_state, g_compact_hoggs_state_current, 0);

				g_compact_hoggs_state_current = NULL;
			}
		}

		if(should_close)
			hogFileDestroy(hogg, false);

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

// Lock the compaction mutex.
void patchcompactionLock()
{
	EnterCriticalSection(&g_compact_hoggs_cs);
}

// Release the compaction mutex.
void patchcompactionUnlock()
{
	LeaveCriticalSection(&g_compact_hoggs_cs);
}

bool patchcompactionIsFileWaitingCleanup(PatchServerDb *serverdb, const char *fileName)
{
	const char *name = fileName + strlen(serverdb->name) + 1 + 2;
	PERFINFO_AUTO_START_FUNC();
	FOR_EACH_IN_EARRAY(g_compact_hoggs_to_cleanup, CompactHogsState, chs)
	{
		if(stricmp(chs->file, name) == 0)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
	return false;
}

bool patchcompactionIsFileCompacting(PatchServerDb *serverdb, const char *fileName)
{
	const char *name = fileName + strlen(serverdb->name) + 1 + 2;
	bool retval;
	PERFINFO_AUTO_START_FUNC();
	retval = (g_compact_hoggs_state_current && g_compact_hoggs_state_current->serverdb == serverdb && stricmp(g_compact_hoggs_state_current->file, name) == 0);
	PERFINFO_AUTO_STOP();
	return retval;
}

void patchcompactionCleanUpTempHogsOnRestart(PatchServerDb *serverdb)
{
	char **files;
	char path[MAX_PATH];

	// Don't compact ignored DBs
	if(g_patchserver_config.prune_config && eaFindString(&g_patchserver_config.prune_config->ignore_projects, serverdb->name) != -1)
		return;

	PERFINFO_AUTO_START_FUNC();

	loadstart_printf("Cleaning up old compaction files for %s...", serverdb->name);
	initCompactionCleanupThread();
	sprintf(path, "./%s", serverdb->name);
	files = fileScanDir(path);
	FOR_EACH_IN_EARRAY(files, char, file)
	{
		char *name = file + strlen(serverdb->name) + 1 + 2;
		char *extension;

		if((extension = strstri(name, ".delayedwrite.hogg")))
		{ 
			char normalname[MAX_PATH];
			CompactHogsState *state;
			U32 hoggKey = 0;
			bool success;
			HALHogFile *tempHalhog;

			*extension = 0;
			success = StringToUint_Paranoid(name, &hoggKey);
			*extension = '.';

			if (success)
			{
				tempHalhog = patchHALGetHogHandle(serverdb, hoggKey, false, true, true, true, false, NULL);

				state = calloc(1, sizeof(CompactHogsState));
				sprintf(normalname, "%u.hogg", hoggKey);
				state->file = strdup(normalname);
				state->serverdb = serverdb;
				eaPush(&g_compact_hoggs_to_cleanup, state);
				patchHALHogFileDestroy(tempHalhog, false);
			}
		}
	}
	FOR_EACH_END;
	loadend_printf("");
	PERFINFO_AUTO_STOP();
}

// Return a string describing the current compaction status.
const char *patchcompactionStatus()
{
	static char *status = NULL;

	if (g_compact_hoggs_state_current)
		estrPrintf(&status, "Compacting %s/%s, %.0f seconds now (%d left)", g_compact_hoggs_state_current->serverdb->name, g_compact_hoggs_state_current->file,
			difftime(time(NULL), g_compact_hoggs_state_current->start), eaSize(&g_compact_hoggs_state));
	else if (eaSize(&g_compact_hoggs_state))
		estrPrintf(&status, "Considering compacting %s/%s (%d left)", g_compact_hoggs_state[0]->serverdb->name, g_compact_hoggs_state[0]->file, eaSize(&g_compact_hoggs_state));
	else if (eaSize(&g_compact_hoggs_to_cleanup))
		estrPrintf(&status, "Cleaning up compacted file %s/%s (%d left)", g_compact_hoggs_to_cleanup[0]->serverdb->name, g_compact_hoggs_to_cleanup[0]->file,
			eaSize(&g_compact_hoggs_to_cleanup));
	else
		estrCopy2(&status, "None");
	return status;
}
