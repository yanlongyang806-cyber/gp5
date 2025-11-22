#include "Alerts.h"
#include "earray.h"
#include "hoglib.h"
#include "patchcommonutils.h"
#include "patchcompaction.h"
#include "patchhal.h"
#include "patchproject.h"
#include "patchserver.h"
#include "patchserverdb.h"
#include "StashTable.h"
#include "StringCache.h"
#include "timing.h"
#include "utils.h"
#include "winutil.h"

typedef struct HALHandleTracker
{
	U32 hoggKey;
	HogFile *hogg;
	HogFile *tempHogg;
	U32 timeLastAccessed;
	bool write;
	U32 refCount;
	StashTable refCountStash;
	U32 refCountTemp;
	StashTable refCountStashTemp;
} HALHandleTracker;

static int gHALCloseDelay = 129600; // 36 hours in seconds -> 36*60*60
AUTO_CMD_INT(gHALCloseDelay, HALCloseDelay);

static void patchHALGetHogFileNameFromHogKeyAndSuffix(	char* hoggFileNameOut,
														size_t hoggFileNameOut_size,
														const char* dbName,
														U32 hoggKey,
														const char* suffix)
{
	if(hoggFileNameOut){
		if (dbName)
			sprintf_s(	SAFESTR2(hoggFileNameOut),
						"./%s/%u%s",
						dbName,
						hoggKey,
						suffix);
		else
			sprintf_s(	SAFESTR2(hoggFileNameOut),
						"%u%s",
						hoggKey,
						suffix);

	}
}

void patchHALGetHogFileNameFromHogKey(	char* hoggFileNameOut,
										size_t hoggFileNameOut_size,
										const char* dbName,
										U32 hoggKey)
{	
	patchHALGetHogFileNameFromHogKeyAndSuffix(SAFESTR2(hoggFileNameOut), dbName, hoggKey, ".hogg");
}

void patchHALGetHogFileNameFromTimeStamp(	char* hoggFileNameOut,
											size_t hoggFileNameOut_size,
											const char* dbName,
											U32 timeStamp)
{
	U32 hoggKey = patchHALGetHogKey(timeStamp);
	
	patchHALGetHogFileNameFromHogKey(SAFESTR2(hoggFileNameOut), dbName, hoggKey);
}

static void patchHALGetTempHogFileNameFromHogKey(	char *hoggFileNameOut,
											size_t hoggFileNameOut_size,
											const char *dbName,
											U32 hoggKey)
{	
	patchHALGetHogFileNameFromHogKeyAndSuffix(SAFESTR2(hoggFileNameOut), dbName, hoggKey, ".delayedwrite.hogg");
}

static bool patchHALGetFileName(char *hoggFileNameOut, size_t hoggFileNameOut_size, PatchServerDb *serverdb, U32 hoggKey, bool forceTemp, bool forceNoTemp)
{
	char buf[MAX_PATH];

	patchHALGetHogFileNameFromHogKey(SAFESTR(buf), serverdb->name, hoggKey);

	if(forceTemp || (!forceNoTemp && patchcompactionCompactHogsAsyncIsRunning() && (patchcompactionIsFileCompacting(serverdb, buf) || patchcompactionIsFileWaitingCleanup(serverdb, buf))))
	{
		patchHALGetTempHogFileNameFromHogKey(SAFESTR2(hoggFileNameOut), serverdb->name, hoggKey);
		return true;
	}
	else
	{
		strcpy_s(hoggFileNameOut, hoggFileNameOut_size, buf);
		return false;
	}
}

HALHogFile* patchHALGetHogHandleByTime(PatchServerDb *serverdb, U32 checkin_time, bool createhogg, bool alert, bool write, bool forceTemp, bool forceNoTemp, bool *isTempName MEM_DBG_PARMS)
{
	U32 hoggKey = patchHALGetHogKey(checkin_time);
	return patchHALGetHogHandleEx(serverdb, hoggKey, createhogg, alert, write, forceTemp, forceNoTemp, isTempName MEM_DBG_PARMS_CALL);
}

static void IncrementHALRefCount(int *refCount, StashTable *refCountStash, const char *stashTableKey)
{
	int result = 0;
	++(*refCount);
	if(!*refCountStash)
		*refCountStash = stashTableCreateWithStringKeys(10, StashDefault);
	stashFindInt(*refCountStash, stashTableKey, &result);
	stashAddInt(*refCountStash, stashTableKey, result + 1, true);
}

HALHogFile* patchHALGetHogHandleEx(PatchServerDb *serverdb, U32 hoggKey, bool createhogg, bool alert, bool write, bool forceTemp, bool forceNoTemp, bool *isTempName MEM_DBG_PARMS)
{
	HALHandleTracker *tracker = NULL;
	HogFile		*hogg = NULL;
	HALHogFile  *halhog = NULL;
	char		fname[MAX_PATH];
	bool		localIsTempName = false;
	int			hogg_ret = 0;
	
	assert(serverdb);

	PERFINFO_AUTO_START_FUNC();

	// Find the tracker so we can split perfinfo based on what kind of hogFileRead we are doing.
	stashIntFindPointer(serverdb->hogg_stash, hoggKey, &tracker);

	patchcompactionLock();

	localIsTempName = patchHALGetFileName(SAFESTR(fname), serverdb, hoggKey, forceTemp, forceNoTemp);

	if(!write && localIsTempName && !createhogg)
	{
		if(tracker && tracker->tempHogg)
		{
			PERFINFO_AUTO_START("IncrementSharedRefCount_Temp", 1);
		}
		else
		{
			PERFINFO_AUTO_START("OpenNew_Temp", 1);
		}
		hogg = hogFileReadReadOnlySafeEx(	fname,
							NULL,
							PIGERR_PRINTF,
							&hogg_ret,
							(HOG_NOCREATE)|(HOG_READONLY),
							PATCHSERVER_HOG_DATALIST_JOURNAL_SIZE);

		if(!hogg)
			localIsTempName = patchHALGetFileName(SAFESTR(fname), serverdb, hoggKey, false, true);
		PERFINFO_AUTO_STOP();
	}

	if(!hogg)
	{
		if(tracker && localIsTempName && tracker->tempHogg)
		{
			PERFINFO_AUTO_START("IncrementSharedRefCount_Temp", 1);
		}
		else if(tracker && !localIsTempName && tracker->hogg)
		{
			if(write && !tracker->write)
			{
				PERFINFO_AUTO_START("UpgradeToWritable", 1);
			}
			else
			{
				PERFINFO_AUTO_START("IncrementSharedRefCount_Normal", 1);
			}
		}
		else if(localIsTempName)
		{
			PERFINFO_AUTO_START("OpenNew_Temp", 1);
		}
		else
		{
			PERFINFO_AUTO_START("OpenNew_Normal", 1);
		}

		hogg = hogFileReadReadOnlySafeEx(	fname,
							NULL,
							PIGERR_PRINTF,
							&hogg_ret,
							(createhogg || localIsTempName ?HOG_DEFAULT:HOG_NOCREATE)|(write?HOG_MUST_BE_WRITABLE:HOG_READONLY),
							PATCHSERVER_HOG_DATALIST_JOURNAL_SIZE);
		PERFINFO_AUTO_STOP();
	}

	if(hogg)
	{
		bool reopen = false;
		if(!stashIntFindPointer(serverdb->hogg_stash, hoggKey, &tracker))
		{
			tracker = calloc(sizeof(HALHandleTracker), 1);
			tracker->hoggKey = hoggKey;
			tracker->refCount = 0;
			stashIntAddPointer(serverdb->hogg_stash, hoggKey, tracker, false);
		}
		
		if(localIsTempName)
		{
			if(!tracker->tempHogg)
				reopen = true;
			else if(tracker->tempHogg != hogg)
				AssertOrAlert("PATCH_HAL_TRACKER_FAIL", "Opening %s but already have a different hogg handle for this hoggKey", fname);
			tracker->tempHogg = hogg;
		}
		else
		{
			if(!tracker->hogg)
				reopen = true;
			else if(tracker->hogg != hogg)
				AssertOrAlert("PATCH_HAL_TRACKER_FAIL", "Opening %s but already have a different hogg handle for this hoggKey", fname);
			tracker->hogg = hogg;
		}

		if(reopen)
		{
			PERFINFO_AUTO_START("OpenExtraHandle", 1);
			// Open another handle to get the ref count correct.
			hogFileAddRef(hogg);
			PERFINFO_AUTO_STOP();
		}

		tracker->timeLastAccessed = timeSecondsSince2000();
		tracker->write |= write;
	}
	else if(alert)
		AssertOrAlert("PATCHDB_HOG_OPEN_FAILED", "Unable to open hog file \"%s\": hog_error %d, errno %d, GetLastError %d",
			fname, hogg_ret, errno, GetLastError());

	patchcompactionUnlock();

	if(isTempName)
		*isTempName = localIsTempName;

	if(hogg)
	{
		char stashTableKey[MAX_PATH+32]; // Add 32 more to handle the line numbers
		halhog = malloc(sizeof(*halhog));
		halhog->hog = hogg;
		halhog->serverdb = serverdb;
		halhog->filename = NULL;
		estrCopy2(&halhog->filename, fname);
		sprintf(stashTableKey, "%s:%d", caller_fname, line);
		halhog->stashTableKey = allocAddString(stashTableKey);
		halhog->temp = localIsTempName;
		if(halhog->temp)
		{
			IncrementHALRefCount(&tracker->refCountTemp, &tracker->refCountStashTemp, halhog->stashTableKey);
			devassert(tracker->refCountTemp == hogFileGetSharedRefCount(halhog->hog) - 1);
		}
		else
		{
			IncrementHALRefCount(&tracker->refCount, &tracker->refCountStash, halhog->stashTableKey);
			devassert(tracker->refCount == hogFileGetSharedRefCount(halhog->hog) - 1);
		}
	}

	devassert(!halhog || halhog->hog);

	PERFINFO_AUTO_STOP();
	return halhog;
}

// Open a hogg handle, and track it in the Patch Server hog stash.
HALHogFile *patchHALGetReadHogHandleEx(PatchServerDb *serverdb, U32 checkin_time, FileNameAndOldName *pathInHogg, const char *filename MEM_DBG_PARMS)
{
	HALHogFile *halhog;
	bool isTempName = false;
	U32 hoggKey;

	PERFINFO_AUTO_START_FUNC();

	hoggKey = patchHALGetHogKey(checkin_time);

	halhog = patchHALGetHogHandleEx(serverdb, hoggKey, false, true, false, false, false, &isTempName MEM_DBG_PARMS_CALL);

	if(halhog && isTempName && (pathInHogg || filename))
	{
		HogFileIndex		hfi;
		char tempName[MAX_PATH];
		if(pathInHogg)
		{
			hfi = hogFileFind(halhog->hog, pathInHogg->name);

			sprintf_s(SAFESTR(tempName), "%s.del", pathInHogg->name);

			if(hfi == HOG_INVALID_INDEX)
			{
				hfi = hogFileFind(halhog->hog, tempName);
			}

			if(hfi == HOG_INVALID_INDEX)
			{
				hfi = hogFileFind(halhog->hog, pathInHogg->oldName);
			}

			sprintf_s(SAFESTR(tempName), "%s.del", pathInHogg->oldName);

			if(hfi == HOG_INVALID_INDEX)
			{
				hfi = hogFileFind(halhog->hog, tempName);
			}
		}
		else if(filename)
		{
			hfi = hogFileFind(halhog->hog, filename);
		}

		if(hfi == HOG_INVALID_INDEX)
		{
			patchHALHogFileDestroy(halhog, true); // close the temp hogg
			halhog = patchHALGetHogHandleEx(serverdb, hoggKey, false, true, false, false, true, &isTempName MEM_DBG_PARMS_CALL);
			assertmsg(!halhog || !isTempName, "Attempted to open non-temp hogg, but got temp hogg instead.");
		}
	}

	PERFINFO_AUTO_STOP();

	devassert(!halhog || halhog->hog);
	return halhog;
}

// Open a hogg handle, and track it in the Patch Server hog stash.
HALHogFile *patchHALGetWriteHogHandleEx(PatchServerDb *serverdb, U32 checkin_time, bool createhogg, bool alert MEM_DBG_PARMS)
{
	HALHogFile *halhog;
	bool isTempName = false;
	U32 hoggKey;

	PERFINFO_AUTO_START_FUNC();

	hoggKey = patchHALGetHogKey(checkin_time);

	halhog = patchHALGetHogHandleEx(serverdb, hoggKey, createhogg, alert, true, false, false, &isTempName MEM_DBG_PARMS_CALL);

	PERFINFO_AUTO_STOP();

	devassert(!halhog || halhog->hog);
	return halhog;
}

// Close a hogg file.
void patchHALCloseHog(PatchServerDb *serverdb, HogFile *hogg, bool temp)
{
	HALHandleTracker *tracker = NULL;
	U32 hoggKey;
	const char *name;

	if(!hogg)
		return;

	PERFINFO_AUTO_START_FUNC();

	name = hogFileGetArchiveFileName(hogg) + strlen(serverdb->name) + 2 + 1;
	hoggKey = strtol(name, NULL, 10);

	// Flush.
	hogFileModifyFlush(hogg);

	// Close.
	if(!stashIntFindPointer(serverdb->hogg_stash, hoggKey, &tracker))
	{
		AssertOrAlert("PATCHDB_HAL_CLOSE_FAILURE", "Attempting to call patchHALCloseHog on %s that is not being tracked.", hogFileGetArchiveFileName(hogg));
		PERFINFO_AUTO_STOP();
		return;
	}

	if(tracker && (temp ? tracker->tempHogg : tracker->hogg) == hogg)
	{
		if(temp)
		{
			tracker->tempHogg = NULL;
		}
		else
		{
			tracker->write = false;
			tracker->hogg = NULL;
		}
	}

	hogFileDestroy(hogg, true);
	PERFINFO_AUTO_STOP();
}

void patchHALCloseAllHogs(PatchServerDb *serverdb)
{
	FOR_EACH_IN_STASHTABLE(serverdb->hogg_stash, HALHandleTracker, tracker)
	{
		if(tracker)
		{
			if(tracker->hogg)
			{
				printf("Closing: %s\n", hogFileGetArchiveFileName(tracker->hogg));
				patchHALCloseHog(serverdb, tracker->hogg, false);
			}

			if(tracker->tempHogg)
			{
				printf("Closing: %s\n", hogFileGetArchiveFileName(tracker->tempHogg));
				patchHALCloseHog(serverdb, tracker->tempHogg, true);
			}
		}
	}
	FOR_EACH_END;
	
	stashTableClear(serverdb->hogg_stash);
}

U32 patchHALGetHogKey(U32 timeStamp)
{
	return timeStamp / HOG_TIMESTAMP_DIVISION * HOG_TIMESTAMP_DIVISION;
}

HogFile *patchHALGetCachedHandle(PatchServerDb *serverdb, int hoggKey, bool temp)
{
	HALHandleTracker *tracker;
	if(stashIntFindPointer(serverdb->hogg_stash, hoggKey, &tracker))
	{
		if(temp)
			return tracker->tempHogg;
		else
			return tracker->hogg;
	}

	return NULL;
}

bool patchHALHogFileInUse(HogFile *hogg)
{
	int count;
	PERFINFO_AUTO_START_FUNC();
	count = hogFileGetSharedRefCount(hogg);
	PERFINFO_AUTO_STOP();
	return count > 1;
}

void patchHALHogDelete(PatchServerDb *serverdb, HogFile *hogg, const char *filename)
{
	int hoggKey;
	HALHandleTracker *tracker = NULL;
	const char *name;

	PERFINFO_AUTO_START_FUNC();

	name = hogFileGetArchiveFileName(hogg) + strlen(serverdb->name) + 2 + 1;

	hoggKey = strtol(name, NULL, 10);

	if(!stashIntFindPointer(serverdb->hogg_stash, hoggKey, &tracker))
	{
		AssertOrAlert("PATCHDB_HAL_DELETE_FAIL", "Attempting to delete %s from file %s, but we are not tracking the hog file.", filename, hogFileGetArchiveFileName(hogg));
	}

	if(tracker->hogg == hogg)
	{
		// Note: hogFileExistsNoChangeCheck() is used here to speed up deleting lists of files that don't actually exist,
		// by avoiding interprocess locking.  This case occurs sometimes when syncing expired and pruned files.  If some
		// outside process is modifying hoggs, after we've loaded them, to include files we don't know about, and we haven't
		// done anything else that would cause the hogg to be reloaded, it's possible that this will fail to delete a file.
		// That should never happen in regular operating conditions.
		if (hogFileExistsNoChangeCheck(hogg, filename))
		{
			PERFINFO_AUTO_START("hogFileModifyDeleteNamed", 1);
			hogFileModifyDeleteNamed(hogg, filename);
			PERFINFO_AUTO_STOP();
		}
	}
	else if(tracker->tempHogg == hogg)
	{
		//Make delete marker filename
		char deletedFilename[MAX_PATH];
		sprintf(deletedFilename,"%s.del",filename);

		PERFINFO_AUTO_START("hogFileModifyDeleteNamed", 1);
		hogFileModifyUpdateNamed(hogg, deletedFilename, malloc(0), 0, getCurrentFileTime(), NULL);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME(GetTrackedHogRefCount) ACMD_ACCESSLEVEL(9);
const char *patchHALGetTrackedHogRefCount(const char *path)
{
	char returnvalue[MAX_PATH];
	char serverdbName[MAX_PATH];
	U32 hoggKey = 0;
	const char *slash = strchr(path, '/');
	bool temp = false;
	HogFile *hogg = NULL;
	PatchProject *project;

	if(slash != 0)
	{
		const char *filename;
		strncpy(serverdbName, path, slash - path);
		filename = slash + 1;
		hoggKey = strtol(filename, NULL, 10);
		if(hoggKey)
		{
			temp = strstri(path, ".delayedwrite.hogg") > 0;
		}
	}

	if(slash == 0 || hoggKey == 0)
	{
		return "Improper format for tracked hog name. <serverdb>/<hoggKey>.hogg or <serverdb>/<hoggKey>.delayedwrite.hogg";
	}

	project = patchserverFindProject(serverdbName);

	if(project)
		hogg = patchHALGetCachedHandle(project->serverdb, hoggKey, temp);

	if(!hogg)
		return "0";

	sprintf(returnvalue, "%d", hogFileGetSharedRefCount(hogg));
	return strdup(returnvalue);
}

// Close hog files that have not been accessed recently
void patchHALTick(PatchServerDb *serverdb)
{
	static HALHandleTracker **ppTrackersToRemove = NULL;
	U32 now = timeSecondsSince2000();
	PERFINFO_AUTO_START_FUNC();

	FOR_EACH_IN_STASHTABLE(serverdb->hogg_stash, HALHandleTracker, tracker);
	{
		bool closed = false;
		PERFINFO_AUTO_START("CheckTracker", 1);
		if(tracker->timeLastAccessed < now - gHALCloseDelay)
		{
			if(tracker->hogg && !patchHALHogFileInUse(tracker->hogg))
			{
				if(g_patchserver_config.printFileCacheUpdates)
				{
					PERFINFO_AUTO_START("ClosePrintf", 1);
					printfColor(COLOR_RED,
								"PatchHAL: Closing \"%s\".\n",
								hogFileGetArchiveFileName(tracker->hogg));
					PERFINFO_AUTO_STOP();
				}
				patchHALCloseHog(serverdb, tracker->hogg, false);
				closed = true;
				tracker->hogg = NULL;
			}
			if(tracker->tempHogg && !patchHALHogFileInUse(tracker->tempHogg) )
			{
				char buf[MAX_PATH];
				patchHALGetHogFileNameFromHogKey(SAFESTR(buf), serverdb->name, tracker->hoggKey);
				if(!patchcompactionIsFileCompacting(serverdb, buf) && !patchcompactionIsFileWaitingCleanup(serverdb, buf))
				{
					if(g_patchserver_config.printFileCacheUpdates)
					{
						PERFINFO_AUTO_START("ClosePrintf", 1);
						printfColor(COLOR_RED,
									"PatchHAL: Closing \"%s\".\n",
									hogFileGetArchiveFileName(tracker->tempHogg));
						PERFINFO_AUTO_STOP();
					}
					patchHALCloseHog(serverdb, tracker->tempHogg, true);
					closed = true;
					tracker->tempHogg = NULL;
				}
			}
		}

		if(!tracker->hogg && !tracker->tempHogg)
		{
			eaPush(&ppTrackersToRemove, tracker);
		}

		PERFINFO_AUTO_STOP();
		if(closed)
			break;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(ppTrackersToRemove, HALHandleTracker, tracker);
	{
		PERFINFO_AUTO_START("CleanEmptyTracker", 1);
		devassert(stashIntRemovePointer(serverdb->hogg_stash, tracker->hoggKey, NULL));
		stashTableDestroy(tracker->refCountStash);
		stashTableDestroy(tracker->refCountStashTemp);
		free(tracker);
		PERFINFO_AUTO_STOP();
	}
	FOR_EACH_END;

	eaClearFast(&ppTrackersToRemove);

	PERFINFO_AUTO_STOP_FUNC();
}

static void DecrementHALRefCount(int *refCount, StashTable *refCountStash, const char *stashTableKey)
{
	int result = 0;
	--(*refCount);
	if(!*refCountStash)
		*refCountStash = stashTableCreateWithStringKeys(10, StashDefault);
	if(stashFindInt(*refCountStash, stashTableKey, &result))
	{
		if(result <= 1)
			stashRemoveInt(*refCountStash, stashTableKey, &result);
		else
			stashAddInt(*refCountStash, stashTableKey, result - 1, true);
	}
}

void patchHALHogFileDestroy(HALHogFile *halhog, bool freehandle)
{
	int result = 0;
	HALHandleTracker *tracker;
	char *name;
	U32 hoggKey;

	if(!halhog)
		return;

	name = halhog->filename + strlen(halhog->serverdb->name) + 2 + 1;
	hoggKey = strtol(name, NULL, 10);

	devassert(stashIntFindPointer(halhog->serverdb->hogg_stash, hoggKey, &tracker));

	if(tracker)
	{
		if(halhog->temp)
		{
			devassert(tracker->refCountTemp == hogFileGetSharedRefCount(halhog->hog) - 1);
			DecrementHALRefCount(&tracker->refCountTemp, &tracker->refCountStashTemp, halhog->stashTableKey);
		}
		else
		{
			devassert(tracker->refCount == hogFileGetSharedRefCount(halhog->hog) - 1);
			DecrementHALRefCount(&tracker->refCount, &tracker->refCountStash, halhog->stashTableKey);
		}
	}

	estrDestroy(&halhog->filename);
	hogFileDestroy(halhog->hog, freehandle);

	free(halhog);
}

HALHogFile *patchHALHogFileAddRefEx(HALHogFile *halhog MEM_DBG_PARMS)
{
	HALHogFile *newHalhog = NULL;
	int result = 0;
	char *name;
	U32 hoggKey;
	HALHandleTracker *tracker;
	char stashTableKey[MAX_PATH+32]; // Add 32 more to handle the line numbers
	
	if(!halhog)
		return NULL;

	name = halhog->filename + strlen(halhog->serverdb->name) + 2 + 1;
	hoggKey = strtol(name, NULL, 10);

	devassert(stashIntFindPointer(halhog->serverdb->hogg_stash, hoggKey, &tracker));

	newHalhog = calloc(1, sizeof(*newHalhog));
	newHalhog->hog = halhog->hog;
	newHalhog->temp = halhog->temp;
	newHalhog->serverdb = halhog->serverdb;
	hogFileAddRef(halhog->hog);
	estrCopy2(&newHalhog->filename, halhog->filename);
	sprintf(stashTableKey, "%s:%d", caller_fname, line);
	newHalhog->stashTableKey = allocAddString(stashTableKey);
	
	if(tracker)
	{
		if(halhog->temp)
		{
			IncrementHALRefCount(&tracker->refCountTemp, &tracker->refCountStashTemp, newHalhog->stashTableKey);
			devassert(tracker->refCountTemp == hogFileGetSharedRefCount(halhog->hog) - 1);
		}
		else
		{
			IncrementHALRefCount(&tracker->refCount, &tracker->refCountStash, newHalhog->stashTableKey);
			devassert(tracker->refCount == hogFileGetSharedRefCount(halhog->hog) - 1);
		}
	}

	devassert(!newHalhog || newHalhog->hog);
	return newHalhog;
}

static void printRefCountStash(StashTable refCountStash)
{
	if(refCountStash)
	{
		FOR_EACH_IN_STASHTABLE2(refCountStash, elem);
		{
			printf("\t%s: %d\n", stashElementGetStringKey(elem), stashElementGetInt(elem));
		}
		FOR_EACH_END;
	}
}

AUTO_COMMAND;
void PrintPatchRefCountInfo(const char *filename)
{
	char serverdbName[MAX_PATH] = {0};
	U32 hoggKey = 0;
	HALHandleTracker *tracker = NULL;
	const char *slash = strchr(filename, '/');
	bool temp = false;
	HogFile *hogg = NULL;
	PatchProject *project;

	if(slash != 0)
	{
		const char *name;
		strncpy(serverdbName, filename, slash - filename);
		name = slash + 1;
		hoggKey = strtol(name, NULL, 10);
		if(hoggKey)
		{
			temp = strstri(filename, ".delayedwrite.hogg") > 0;
		}
	}

	if(slash == 0 || hoggKey == 0)
	{
		printf("Improper format for tracked hog name. <serverdb>/<hoggKey>.hogg or <serverdb>/<hoggKey>.delayedwrite.hogg");
	}

	// get the serverdb
	project = patchserverFindProject(serverdbName);

	if(project)
	{
		// get the tracker
		stashIntFindPointer(project->serverdb->hogg_stash, hoggKey, &tracker);
	}
	else
	{
		printf("There is no serverdb named %s\n", serverdbName);
	}

	if(tracker)
	{
		// walk the refcountstash
		printf("%s:%d\n", filename, temp ? tracker->refCountTemp: tracker->refCount);
		if(temp)
		{
			if(tracker->tempHogg)
			{
				devassert(tracker->refCountTemp == hogFileGetSharedRefCount(tracker->tempHogg) - 1);
				printRefCountStash(tracker->refCountStashTemp);
			}
		}
		else
		{
			if(tracker->hogg)
			{
				devassert(tracker->refCount == hogFileGetSharedRefCount(tracker->hogg) - 1);
				printRefCountStash(tracker->refCountStash);
			}
		}
	}
}