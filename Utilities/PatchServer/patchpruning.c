#include "earray.h"
#include "file.h"
#include "FilespecMap.h"
#include "hoglib.h"
#include "logging.h"
#include "patchcommonutils.h"
#include "patchfile.h"
#include "patchjournal.h"
#include "patchpruning.h"
#include "patchserver.h"
#include "patchserverdb.h"
#include "pcl_typedefs.h"
#include "structDefines.h"
#include "timing.h"
#include "utils.h"
#include "wininclude.h"
#include "patchhal.h"

typedef struct PruneState
{
	char *file;
	PatchServerDb *serverdb;
} PruneState;

static PruneState **g_prune_state = NULL;

static int g_never_really_prune = 0;
static int g_patchserver_full_expire = 0;
static int g_patchserver_advance_prune_time = 0;

AUTO_CMD_INT(g_never_really_prune, neverReallyPrune) ACMD_CMDLINE;
AUTO_CMD_INT(g_patchserver_full_expire, full_expire) ACMD_CMDLINE;
AUTO_CMD_INT(g_patchserver_advance_prune_time, advance_prune_time) ACMD_CMDLINE;

static struct {
	S32 prunedFileCount;
	S64 prunedBytesCount;
	
	struct {
		S32 fileCount;
		S64 bytesCount;
	} goodBranch, badBranch, written, upToDate, noHogg, notInHogg;
} pruningStats;

static void pruneUpdateTitleBar(void){
	static U32	lastTime;
	U32			curTime = timeGetTime();
	
	lastTime = FIRST_IF_SET(lastTime, curTime);

	if(curTime - lastTime >= 500){
		void updateTitleBar(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
		
		U64 bytesToWrite;
		
		updateTitleBar(NULL, 1, NULL);
		lastTime = curTime;
		
		if(	patchserverdbGetHoggMirrorQueueSize(&bytesToWrite) &&
			bytesToWrite >= 1024 * 1024 * 1024)
		{
			printf("Waiting for write queue to flush...\n");

			while(patchserverdbGetHoggMirrorQueueSize(NULL)){
				Sleep(100);
			}

			printf("Write queue flushed, continuing pruning...\n");
		}
	}
}

static void mirrorFileVersionToDisk(PatchServerDb* serverdb,
									FileVersion* ver)
{
	char				hoggMirrorFilePath[MAX_PATH * 2];
	S32					fileNeedsToBeMirrored = 0;
	FileNameAndOldName	pathInHogg;
	const char*			usedPathInHogg;

	patchserverdbNameInHogg(serverdb, ver, &pathInHogg);
	usedPathInHogg = pathInHogg.name;
	
	patchserverdbGetHoggMirrorFilePath(	SAFESTR(hoggMirrorFilePath),
							serverdb->name,
							usedPathInHogg,
							ver->checkin->time);
							
	if(!fileExists(hoggMirrorFilePath)){
		usedPathInHogg = pathInHogg.oldName;
		
		patchserverdbGetHoggMirrorFilePath(	SAFESTR(hoggMirrorFilePath),
								serverdb->name,
								usedPathInHogg,
								ver->checkin->time);
	}
							
	if(	!fileExists(hoggMirrorFilePath) ||
		fileSize(hoggMirrorFilePath) != ver->size ||
		fileLastChanged(hoggMirrorFilePath) != ver->modified)
	{
		fileNeedsToBeMirrored = 1;
		pruningStats.written.fileCount++;
		pruningStats.written.bytesCount += ver->size;
	}
	else
	{
		pruningStats.upToDate.fileCount++;
		pruningStats.upToDate.bytesCount += ver->size;
	}
	
	if(fileNeedsToBeMirrored)
	{
		HALHogFile* halhog = patchHALGetWriteHogHandle(serverdb, ver->checkin->time, false);
		
		if(!halhog)
		{
			pruningStats.noHogg.fileCount++;
			pruningStats.noHogg.bytesCount += ver->size;
			
			//printfColor(COLOR_BRIGHT|COLOR_RED,
			//			"Can't open hogg to mirror file: %s/%s\n",
			//			serverdb->name,
			//			pathInHogg);
		}
		else
		{
			HogFileIndex hfi = hogFileFind(halhog->hog, usedPathInHogg);
			
			if(hfi == HOG_INVALID_INDEX)
			{
				pruningStats.notInHogg.fileCount++;
				pruningStats.notInHogg.bytesCount += ver->size;

				printfColor(COLOR_BRIGHT|COLOR_RED,
							"File to mirror was not found in hogg: %s/%s\n",
							serverdb->name,
							usedPathInHogg);
			}
			else
			{
				U32		dataBytes;
				char*	data = hogFileExtract(halhog->hog, hfi, &dataBytes, NULL);
				
				if(!data)
				{
					printfColor(COLOR_BRIGHT|COLOR_RED,
								"Couldn't extract file from hogg: %s/%s\n",
								serverdb->name,
								usedPathInHogg);
				}
				else if(dataBytes != ver->size)
				{
					printfColor(COLOR_BRIGHT|COLOR_RED,
								"Extracted wrong # bytes from hogg: %s/%s\n",
								serverdb->name,
								usedPathInHogg);
				}
				else
				{
					patchserverdbQueueWriteToDisk(	serverdb->name,
													usedPathInHogg,
													data,
													ver->checkin->time,
													ver->modified,
													0,
													ver->size,
													0);
				}
			}
		}
		patchHALHogFileDestroy(halhog, false);
	}
}

// Force a particular file version to be pruned.
// Return true if pruning this FileVersion resulted in the removal of the parent DirEntry.
bool patchpruningPruneFileVersion(	PatchServerDb* serverdb,
									FileVersion* ver,
									PatchJournal* journal,
									const char* reason,
									S32 makeBackupCopy)
{
	HALHogFile*			halhog = patchHALGetWriteHogHandle(serverdb, ver->checkin->time, false);
	FileNameAndOldName	pathInHogg;
	bool result;

	PERFINFO_AUTO_START_FUNC();
	
	reason = FIRST_IF_SET(reason, "No reason specified");
	
	patchserverdbNameInHogg(serverdb, ver, &pathInHogg);
	printfColor(COLOR_BRIGHT |
					(ver->flags & FILEVERSION_DELETED ?
						COLOR_RED :
						COLOR_GREEN),
				"Pruning %s (Reason: %s)\n",
				pathInHogg.name,
				reason);
				
	if(	halhog &&
		makeBackupCopy)
	{
		char			backupPath[MAX_PATH * 2];
		HogFileIndex	hfi;
		
		hfi = hogFileFind(halhog->hog, pathInHogg.name);
		
		if(hfi == HOG_INVALID_INDEX)
		{
			hfi = hogFileFind(halhog->hog, pathInHogg.oldName);
		}
		
		if(hfi != HOG_INVALID_INDEX)
		{
			S32		fileBytes;
			char*	fileData;
			bool	checksumIsValid;
			
			sprintf(backupPath,
					"./NoBackup/prunedFromWebUI/%s/%s",
					serverdb->name,
					pathInHogg.name);
					
			printf("Making backup: %s...", backupPath);
					
			fileData = hogFileExtract(halhog->hog, hfi, &fileBytes, &checksumIsValid);
			
			if(!fileData){
				printf("Failed to extract from hogg!\n");
			}
			else if(!makeDirectoriesForFile(backupPath)){
				printf("Failed to create backup folder!\n");
			}else{
				FILE* f = fopen(backupPath, "wb");
				
				if(!f){
					printf("Failed to open backup file for writing!\n");
				}else{
					if(fwrite(fileData, 1, fileBytes, f) != fileBytes){
						printf("Failed to write to opened backup file!");
					}else{
						printf("Done!\n");
					}
					fclose(f);
					fileSetTimestamp(backupPath, hogFileGetFileTimestamp(halhog->hog, hfi));
				}
			}
			
			SAFE_FREE(fileData);
		}		
	}
	
	pruningStats.prunedFileCount++;
	pruningStats.prunedBytesCount += ver->size;
	
	log_printf(LOG_PATCHSERVER_PRUNE,
					"%s:%s(reason:%s)",
					halhog ? hogFileGetArchiveFileName(halhog->hog) : "nohogg",
					pathInHogg.name,
					reason);
				
	if(serverdb->mirrorHoggsLocally)
	{
		patchserverdbQueueDeleteToDisk(	serverdb->name,
										pathInHogg.name,
										ver->checkin->time);

		patchserverdbQueueDeleteToDisk(	serverdb->name,
										pathInHogg.oldName,
										ver->checkin->time);
	}

	if(halhog)
	{
		patchserverdbHogDelete(serverdb, halhog->hog, pathInHogg.name, "Prune");
		patchserverdbHogDelete(serverdb, halhog->hog, pathInHogg.oldName, "PruneOld");
		patchHALHogFileDestroy(halhog, false);
	}

	// journal the manifest change
	{
		S32 doFlushJournal = !journal;
		
		if(doFlushJournal){
			journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);
		}
		
		journalAddPrune(journal, ver->parent->path, ver->rev);
		
		if(doFlushJournal){
			journalFlushAndDestroy(&journal, serverdb->name);
		}
	}
	
	// change the local manifest
	patchfileDestroy(&ver->patch);
	
	// Invalidate views in this branch and all later branches.
	FOR_BEGIN_FROM(j, ver->checkin->branch, serverdb->max_branch + 1);
		patchUpdateCheckoutTime(serverdb->db, j);
	FOR_END;

	// Remove the FileVersion.
	// this changes dir_entry->versions, but only what we've already touched
	// it might result in the entire dir_entry being removed, however
	result = fileVersionRemoveAndDestroy(serverdb->db, ver);

	PERFINFO_AUTO_STOP_FUNC();

	return result;
}

// Check a file against the pruning files, and prune it if it is time for it to be pruned.
void patchpruningPruneDirEntry(PatchServerDb *serverdb,
								DirEntry *dir_entry,
								PatchJournal *journal,
								const S32 createMissingMirrorFiles)
{
	// TODO: determine if projects need to have their views cleared
	int keep_vers, keep_days, branch;
	U32 now;
	bool has_view_expires = false;

	PERFINFO_AUTO_START_FUNC();

	// Get current time, to use as reference for pruning this DirEntry.
	now = getCurrentFileTime();

	// If requested, advance the pruning time by a certain amount.
	now += g_patchserver_advance_prune_time;

	if(	!dir_entry ||
		!eaSize(&dir_entry->versions))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return; // just a directory
	}

	if(!filespecMapGetInt(serverdb->keepvers, dir_entry->path, &keep_vers))
	{
		keep_vers = PATCHSERVER_KEEP_UNLIMITED;
	}
	else if(keep_vers == PATCHSERVER_KEEP_BINS) // special case for bin files
	{
		keep_vers = 2;
	}

	if(!filespecMapGetInt(serverdb->keepdays, dir_entry->path, &keep_days))
	{
		keep_days = PATCHSERVER_KEEP_UNLIMITED;
	}

	EARRAY_CONST_FOREACH_BEGIN(dir_entry->versions, i, isize);
		FileVersion *ver = dir_entry->versions[i];
		
		if(	ver->checkin->branch >= serverdb->min_branch &&
			ver->checkin->branch <= serverdb->max_branch)
		{
			pruningStats.goodBranch.fileCount++;
			pruningStats.goodBranch.bytesCount += ver->size;
		}else{
			pruningStats.badBranch.fileCount++;
			pruningStats.badBranch.bytesCount += ver->size;
		}

		if(	createMissingMirrorFiles &&
			keep_days < 0 &&
			keep_vers < 0)
		{
			pruneUpdateTitleBar();

			mirrorFileVersionToDisk(serverdb, ver);
		}
	EARRAY_FOREACH_END;

	if(g_patchserver_config.prune_config && g_patchserver_config.prune_config->view_expires)
		has_view_expires = true;

	if(	keep_days < 0 &&
		keep_vers < 0 && 
		!has_view_expires)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return; // no pruning set for this file
	}

	for(branch = 0; branch <= serverdb->max_branch; branch++)
	{
		S32 kept = 0;
		
		EARRAY_FOREACH_REVERSE_BEGIN(dir_entry->versions, i);
		{
			FileVersion*	ver = dir_entry->versions[i];

			if(ver->checkin->branch != branch)
			{
				continue;
			}

			if(keep_vers < 0 && keep_days < 0 && ver->expires == 0)
				continue;
			
			pruneUpdateTitleBar();

			if((keep_vers >= 0 &&
				kept < keep_vers
				||
				keep_days >= 0 &&
				(now - ver->checkin->time)/SECONDS_PER_DAY <= (U32)keep_days
				||
				ver->expires == U32_MAX)
				&& !(serverdb->expire_immediately && ver->expires && ver->expires < now))
			{
				kept++;
				
				if(createMissingMirrorFiles){
					mirrorFileVersionToDisk(serverdb, ver);
				}
			}
			else if(ver->expires < now) // note that versions in a live view are not counted against keep_vers
			{
				char reason[100];
				char time[50];
				timeMakeLocalDateStringFromSecondsSince2000(time, patchFileTimeToSS2000(ver->expires));
				sprintf(reason, "keeping %d vers, %d days, expires %s", keep_vers, keep_days, time);
				if(!g_never_really_prune)
				{
					bool removed_direntry = patchpruningPruneFileVersion(serverdb, ver, journal, reason, 0);
					if (removed_direntry)
					{
						PERFINFO_AUTO_STOP_FUNC();
						return;
					}
				}
				else
					printfColor(COLOR_RED|COLOR_BRIGHT, "(PRETEND) Pruning %s: %s\n", ver->parent->path, reason);
			}
		}
		EARRAY_FOREACH_END;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

typedef struct ServerDbAndJournal
{
	PatchServerDb *serverdb;
	PatchJournal *journal;
} ServerDbAndJournal;

static void pruneCB(DirEntry *dir_entry, ServerDbAndJournal *userdata)
{
	patchpruningPruneDirEntry(	userdata->serverdb,
								dir_entry,
								userdata->journal,
								userdata->serverdb->mirrorHoggsOnStartup);
}

static void pruneCBAsync(DirEntry *dir_entry, ServerDbAndJournal *userdata)
{
	PruneState *state = calloc(1, sizeof(PruneState));
	state->file = strdup(dir_entry->path);
	state->serverdb = userdata->serverdb;
	eaPush(&g_prune_state, state);
}

typedef struct SetExpiresData {
	NamedView *view;
	int incr_from;
	bool latest;
	PatchJournal *journal;
} SetExpiresData;

static void setExpiresCB(DirEntry *dir, const SetExpiresData *sed)
{
	FileVersion *ver = patchFindVersionInDir(	dir,
		sed->view->branch,
		sed->view->sandbox,
		sed->view->rev,
		sed->incr_from);

	if(ver)
	{
		// Normally, a version expires when its view expires.
		U32 expires = sed->view->expires ? sed->view->expires : U32_MAX;

		// Don't expire a version that belongs to the latest view.
		if(sed->latest) expires = U32_MAX;

		// Don't expire a version that is that last revision available.
		if(ver == eaTail(&ver->parent->versions)) expires = U32_MAX;

		// Update expiration data.
		if(ver->expires == expires) return;
		if(ver->expires == U32_MAX || ver->expires < expires || g_patchserver_full_expire)
		{
			ver->expires = expires;
			journalAddFileExpires(sed->journal, dir->path, ver->rev, expires);
		}
	}
}

static void setExpires(PatchServerDb *serverdb, NamedView *view)
{
	Checkin *checkin = SAFE_DEREF(view->sandbox) ?
		patchGetSandboxCheckin(serverdb->db, view->sandbox) :
	NULL;
	SetExpiresData sed;

	// FIXME: it's not valid for there to be a sandbox and no checkin

	sed.view = view;
	sed.incr_from = checkin ? checkin->incr_from : PATCHREVISION_NONE; 
	sed.latest = view == eaTail(&serverdb->db->namedviews);
	sed.journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);

	patchForEachDirEntry(serverdb->db, setExpiresCB, &sed);

	journalFlushAndDestroy(&sed.journal, serverdb->name);
}

void patchpruningPruneAsyncStart(PatchServerDb *serverdb)
{
#define ERASE_STRING(str) {int n; for(n=estrLength(&str); n>0; --n) printf("\b"); }
	ServerDbAndJournal userdata;
	char *buf = NULL;
	int i;

	loadstart_printf("Marking important file versions...");
	estrPrintf(&buf, "0/%d", eaSize(&serverdb->db->namedviews));
	printf("%s", buf);
	for(i = 0; i < eaSize(&serverdb->db->namedviews); i++)
	{
		NamedView *view = serverdb->db->namedviews[i];
		if(g_patchserver_full_expire || view->dirty || i+1 == eaSize(&serverdb->db->namedviews) || view->expires == 0 || view->expires == U32_MAX)
		{
			setExpires(serverdb, serverdb->db->namedviews[i]);
			view->dirty = false;
			journalAddViewCleanFlush(eaSize(&serverdb->db->checkins) - 1, serverdb->name, view->name);
			ERASE_STRING(buf);
			estrPrintf(&buf, "%d/%d", i, eaSize(&serverdb->db->namedviews));
			printf("%s", buf);
		}
	}
	ERASE_STRING(buf);
	loadend_printf("");

	serverdb->keepdays = patchserverdbCreateFilemapFromConfig(&serverdb->keepdays_config);
	serverdb->keepvers = patchserverdbCreateFilemapFromConfig(&serverdb->keepvers_config);

	loadstart_printf("Adding %s files to prune list...", serverdb->name);
	{
		userdata.serverdb = serverdb;
		ZeroStruct(&pruningStats);
		
		patchForEachDirEntryReverse(serverdb->db, pruneCBAsync, &userdata);
	}
	loadend_printf("done");
#undef ERASE_STRING
}

bool patchpruningPruneAsyncTick(void)
{
	U32 timer;
	PatchJournal *journal = NULL;
	PatchServerDb *last_serverdb = NULL;
	bool result;

	PERFINFO_AUTO_START_FUNC();

	if(eaSize(&g_prune_state)==0)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false; // Not currently pruning
	}

	timer = timerAlloc();
	while(timerElapsed(timer) < 0.1 && eaSize(&g_prune_state))
	{
		DirEntry *dir;
		PruneState *cur = eaPop(&g_prune_state);
		if(last_serverdb != cur->serverdb)
		{
			if(journal)
				journalFlushAndDestroy(&journal, last_serverdb->name);
			journal = journalCreate(eaSize(&cur->serverdb->db->checkins)-1);
			last_serverdb = cur->serverdb;
		}
		dir = patchFindPath(cur->serverdb->db, cur->file, 0);
		if(dir)
			patchpruningPruneDirEntry(	cur->serverdb,
										dir,
										journal,
										cur->serverdb->mirrorHoggsOnStartup);
		free(cur->file);
		free(cur);
	}
	timerFree(timer);
	if(journal && last_serverdb)
		journalFlushAndDestroy(&journal, last_serverdb->name);

	result = eaSize(&g_prune_state)!=0;

	PERFINFO_AUTO_STOP_FUNC();
	return result;
}

// Return true if pruning is running.
bool patchpruningAsyncIsRunning(void)
{
	return eaSize(&g_prune_state);
}

// Abort pruning.
void patchpruningAsyncAbort(void)
{
	FOR_EACH_IN_EARRAY(g_prune_state, PruneState, ps)
		SAFE_FREE(ps->file);
		SAFE_FREE(ps);
	FOR_EACH_END
	eaDestroy(&g_prune_state);
}

// Return a string describing the current pruning status.
const char *patchpruningStatus()
{
	static char *status = NULL;

	if (eaSize(&g_prune_state))
		estrPrintf(&status, "Considering pruning %s/%s (%d left)", eaTail(&g_prune_state)->serverdb->name, eaTail(&g_prune_state)->file, eaSize(&g_prune_state));
	else
		estrCopy2(&status, "None");

	return status;
}
