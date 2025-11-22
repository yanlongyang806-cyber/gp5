#include "cmdparse.h"
#include "earray.h"
#include "fileutil.h"
#include "FilespecMap.h"
#include "hoglib.h"
#include "logging.h"
#include "memlog.h"
#include "patchcommonutils.h"
#include "patchdb.h"
#include "patchdb_h_ast.h"
#include "patchfile.h"
#include "patchhal.h"
#include "patchjournal.h"
#include "patchmirroring.h"
#include "patchmirroring_opt.h"
#include "patchmirroring_opt_h_ast.h"
#include "patchproject.h"
#include "patchserver.h"
#include "patchserver_h_ast.h"
#include "patchserverdb.h"
#include "patchserverdb_h_ast.h"
#include "patchtracking.h"
#include "patchupdate.h"
#include "patchxfer.h"
#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "sysutil.h"
#include "textparser.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utils.h"
#include "wininclude.h"
#include "WorkerThread.h"

extern ParseTable	parse_Checkin[];
#define TYPE_parse_Checkin Checkin

#ifdef ECHO_log_printfS
#define ERROR_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_ERRORS, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#define INFO_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_INFO, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#else
#define ERROR_PRINTF(...) log_printf(LOG_PATCHSERVER_ERRORS, __VA_ARGS__)
#define INFO_PRINTF(...) log_printf(LOG_PATCHSERVER_INFO, __VA_ARGS__)
#endif

// If there is more work to be done, the sync routine will continue working for this amount of time.
#define UPDATE_TIME_PER_FRAME	0.05

// Keep track of which sync iteration we're on.
static U32 s_update_counter = 0;

// Additional information for this state
// FIXME: Refactor these variables into something sane and meaningful
static int g_update_i, g_update_j, g_update_k;

// Waiting on a PCL callback
static bool g_update_callback_waiting;

static bool g_update_notifyhalt;
static U32 g_update_timer;
static char g_update_project_name[MAX_PATH];
static bool g_update_is_incremental = false;
static time_t g_update_last_full = 0;
static PatchJournal *g_update_incremental_journal = NULL;
static JournalCheckin *g_update_incremental_checkin = NULL;

// Size of last manifest; used for estimating space usage in manifest copy
static size_t s_last_manifest_size = 0;

// Timers
static time_t databaseStartTime = 0;
const PatchServerDb *databaseStartTimeServerdb = NULL;
static time_t checkinStartTime = 0;
const Checkin *checkinStartTimeCheckin = NULL;

int g_sync_verify_all_checkins = -1;
static int s_sync_deep_verify_all_checkins = false;
bool g_sync_verify_all_checkins_sticky = false;

// Number of files deleted by syncing, so far.
static int s_sync_deletes = 0;

// Mirroring background thread
static WorkerThread *mirroring_thread = NULL;

AUTO_CMD_INT(g_sync_verify_all_checkins, sync_verify_all_checkins);
AUTO_CMD_INT(g_sync_verify_all_checkins, verify_all_checkins);			// Old name, renamed for clarity, as this is separate from a normal verify
AUTO_CMD_INT(s_sync_deep_verify_all_checkins, sync_deep_verify_all_checkins) ACMD_CALLBACK(set_verify_all_checkins);
AUTO_CMD_INT(g_sync_verify_all_checkins_sticky, sync_verify_all_checkins_sticky);

// This is a weaker version of -sync_verify_all_checkins that always considers the database to have changed, but has none of the other effects.
// This is very useful for testing the performance of full sync between identical databases, without having to have the backing data.
static bool s_sync_ignore_manifest_similarity = false;
AUTO_CMD_INT(s_sync_ignore_manifest_similarity, sync_ignore_manifest_similarity);

// If true, write out the new manifest when syncing, rather than the old way, of using the parent's manifest.
static bool s_sync_write_new_manifest = true;
AUTO_CMD_INT(s_sync_write_new_manifest, sync_write_new_manifest);

// If true, and the above is set, use optimized manifest copying code.
static bool s_sync_fast_copy_manifest = true;
AUTO_CMD_INT(s_sync_fast_copy_manifest, sync_fast_copy_manifest);

// sync_deep_verify_all_checkins implies sync_verify_all_checkins.
void set_verify_all_checkins(CMDARGS)
{
	if (s_sync_deep_verify_all_checkins > 0)
		g_sync_verify_all_checkins = s_sync_deep_verify_all_checkins;
}

AUTO_RUN;
void patchmirroringInitMemlog(void)
{
	memlog_enableThreadId(&updatememlog);
}

static void assertServerDBSanity(PatchServerDb *serverdb)
{
	Checkin *last_checkin;

	// If a database has no checkins, verify it is empty and return.
	if (!eaSize(&serverdb->db->checkins))
	{
		assert(serverdb->latest_rev == -1);					// latest_rev is unset.
		assert(!eaSize(&serverdb->db->root.children));		// There are no files in the database.
		return;
	}

	last_checkin = eaTail(&serverdb->db->checkins);
	assert(last_checkin);
	assert(last_checkin->rev>=serverdb->latest_rev);
	assert((eaSize(&serverdb->db->checkins)-1)==last_checkin->rev);
	assert((eaSize(&serverdb->db->checkins)-1)>=serverdb->latest_rev);
}

// Switch to a new state.
#define patchmirroringSwitchStatef(new_state, format, ...) patchmirroringSwitchStatef_dbg(new_state, FORMAT_STRING_CHECKED(format), __VA_ARGS__)

// Clear all mirroring timers.
static void clearUpdateTimers()
{
	databaseStartTime = 0;
	databaseStartTimeServerdb = NULL;
	checkinStartTime = 0;
	checkinStartTimeCheckin = NULL;
}

// Start timing a synch for a particular database.
static void startDatabaseTimer(const PatchServerDb *serverdb)
{
	devassert(databaseStartTime == 0);
	devassert(databaseStartTimeServerdb == NULL);

	databaseStartTime = time(NULL);
	databaseStartTimeServerdb = serverdb;
}

// Stop timing a synch for a particular database.
static void stopDatabaseTimer(PatchServerDb *serverdb)
{
	time_t now = time(NULL);

	devassert(databaseStartTime != 0);
	devassert(serverdb == databaseStartTimeServerdb);
	devassert(databaseStartTime <= now);

	serverdb->update_duration = now - databaseStartTime;
	serverdb->updated = true;

	databaseStartTime = 0;
	databaseStartTimeServerdb = NULL;
}

// Start timing a checkin synch.
static void startCheckinTimer(const PatchServerDb *serverdb, const Checkin *checkin)
{
	time_t now = time(NULL);

	devassert(databaseStartTime != 0);
	devassert(serverdb == databaseStartTimeServerdb);
	devassert(databaseStartTime <= now);
	devassert(checkinStartTime == 0);
	devassert(checkinStartTimeCheckin == NULL);

	checkinStartTime = now;
	checkinStartTimeCheckin = checkin;
}

// Stop timing a checkin synch.
static void stopCheckinTimer(PatchServerDb *serverdb, Checkin *checkin)
{
	time_t now = time(NULL);

	devassert(databaseStartTime != 0);
	devassert(serverdb == databaseStartTimeServerdb);
	devassert(databaseStartTime <= now);
	devassert(checkinStartTime != 0);
	devassert(checkinStartTimeCheckin == checkin);

	checkin->update_duration = now - checkinStartTime;
	checkin->updated = true;

	checkinStartTime = 0;
	checkinStartTimeCheckin = NULL;
}


void updateSetViewCallback(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData)
{
	if(error != PCL_SUCCESS)
		patchupdateError(error, error_details);

	g_update_callback_waiting = false;
}

static FileVersion *findMatchingFileVersion(PatchDB *db, FileVersion *ver)
{
	int i;
	DirEntry *dir_entry = patchFindPath(db, ver->parent->path, 0);
	if(dir_entry)
	{
		for(i = eaSize(&dir_entry->versions)-1; i >= 0; i--)
		{
			if(dir_entry->versions[i]->rev == ver->rev)
				return dir_entry->versions[i];
		}
	}
	return NULL;
}

// Update the PatchDB with this FileVersion.  If it already exists, it will be updated to match.
static void updateAddVersion(PatchDB *db, FileVersion *ver)
{
	FileVersion *old_ver;
	FileVersion *new_ver;
	DirEntry *dir_entry = patchFindPath(db, ver->parent->path, 0);

	PERFINFO_AUTO_START_FUNC();

	// Look for an existing version.
	old_ver = findMatchingFileVersion(db, ver);

	// Optimization: Leave early if there seems to be nothing to update.
	if (old_ver && fileVersionsEqual(old_ver, ver))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Remove the old version, if any.
	if (old_ver)
	{
		patchfileDestroy(&old_ver->patch);
		fileVersionRemoveAndDestroy(db, old_ver);
	}

	// Add the new, updated version.
	new_ver = patchAddVersion(db, db->checkins[ver->rev], ver->checksum,
							  ver->flags&FILEVERSION_DELETED, ver->parent->path,
							  ver->modified, ver->size, ver->header_size, ver->header_checksum, ver->header_data,
							  ver->expires);

	// Tweak it.
	// patchAddVersion() overrides the ver->modified with the previous version's modified time, for deleted files,
	// but this is inappropriate for mirroring; we really should match whatever our parent has.
	if (ver->flags&FILEVERSION_DELETED)
		new_ver->modified = ver->modified;
	if(g_update_is_incremental)
	{
		assert(g_update_incremental_checkin);
		journalAddFile(g_update_incremental_checkin, ver->parent->path, ver->checksum, ver->size, ver->modified, ver->header_size, ver->header_checksum, ver->flags&FILEVERSION_DELETED,
			ver->expires);
	}
	if(new_ver->version != ver->version)
	{
		// this is normal thanks to pruning
		// TODO: Change the update process to sync checkins before pruning, do pruning more intelligently, and always give the warning/devassert here
		// ERROR_PRINTF("Warning: Version number mismatch during update (%s should have been %d but was %d). Correcting\n",
		// 				ver->parent->path, ver->version, new_ver->version);
		new_ver->version = ver->version;
	}

	// Make sure everything is OK.
	devassert(fileVersionsEqual(new_ver, ver));

	PERFINFO_AUTO_STOP_FUNC();
}

// Completion data for syncFileFromParent()
struct updateFileCallback_userdata
{
	FileVersion *ver;
	HALHogFile *halhog;
};

static void updateFileCallback(PCL_Client *client, const char *filename, XferStateInfo *info, PCL_ErrorCode error, const char *error_details, void *userdata)
{
	struct updateFileCallback_userdata *completion_data = userdata;

	// Handle the callback response.
	if(error != PCL_SUCCESS)
	{
		// This case is particularly bad, so alert specifically if files are missing.
		if(error == PCL_FILE_NOT_FOUND)
			AssertOrAlert("SYNC_PARENT_FILE_MISSING", "Can't transfer %s, file not found on parent server", filename);

		// Set the error code.
		patchupdateError(error, error_details);
	}
	else if (completion_data->ver)
		updateAddVersion(g_patchserver_config.serverdbs[g_update_i]->db, completion_data->ver);

	// Log the success.
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "SyncFileUpdated",
		("filename", "%s", filename)
		("error", "%d", error)
		("ver", "%d", !!completion_data->ver));

	// Release the hog handle.
	if (completion_data->halhog)
	{
		patchHALHogFileDestroy(completion_data->halhog, false);
		completion_data->halhog = NULL;
	}

	free(completion_data);
}

void updateNotifyCallback(PCL_ErrorCode error, const char *error_details, void * userData)
{
	if(error != PCL_SUCCESS)
		patchupdateError(error, error_details);

	g_update_callback_waiting = false;
}

void updateListingCallback(	char **projects,
							int *max_branch,
							int *no_upload,
							int count,
							PCL_ErrorCode error,
							const char *error_details,
							void *userData)
{
	int i, j;

	// FIXME: Check error code.

	for(i = 0; i < eaSize(&g_patchserver_config.serverdbs); i++)
		g_patchserver_config.serverdbs[i]->update_me = false;
	for(i = 0; i < eaSize(&g_patchserver_config.projects); i++)
		g_patchserver_config.projects[i]->update_me = false;

	for(i = 0; i < count; i++)
	{
		PatchProject *project = patchserverFindProject(projects[i]);
		if(project)
		{
			if(project->is_db)
			{
				for(j = 0; j < eaSize(&g_patchserver_config.serverdbs); j++)
					if(!stricmp(projects[i], g_patchserver_config.serverdbs[j]->name))
						g_patchserver_config.serverdbs[j]->update_me = true;
			}
			else
			{
				project->update_me = true;
			}
		}
		// TODO: print warning about extra projects?
	}
	g_update_callback_waiting = false;
}

// Background thread commands.
enum MirroringThreadCmdMsg
{
	MirroringThread_LoadManifest = WT_CMD_USER_START,
	MirroringThread_LoadManifest_Done,
	MirroringThread_CopyManifest,
	MirroringThread_CopyManifest_Done
};

// Data for MirroringThread_LoadManifest
struct LoadManifest_Data
{
	U32 update_count;
	char *serverdb_name;
	bool sync_verify_all_checkins;
	bool sync_ignore_manifest_similarity;
	bool update_is_incremental;
	FilespecMap	*frozenmap;
	int latest_rev;
	MirrorConfig *mirrorConfig;
	PatchDB *old_db_new;
};

// Data for MirroringThread_LoadManifest_Done
struct LoadManifest_Result
{
	U32 update_count;
	PatchDB *db_new;
	PatchJournal *update_incremental_journal;
	char *loading_error;
	UpdateStates state;
	char *estrStateText;
	bool set_update_j;
	int new_update_j;
	bool set_update_k;
	int new_update_k;
	bool scan_mirror_config;
	size_t manifest_file_size;
};

// Load a manifest, in the background thread.
static void thread_LoadManifest(void *user_data, void *data, WTCmdPacket *packet)
{
	struct LoadManifest_Data *parameters = data;
	struct LoadManifest_Result result = {0};
	char current[MAX_PATH], update[MAX_PATH];
	Checkin *first_checkin=NULL, *last_checkin;

	PERFINFO_AUTO_START_FUNC();

	result.update_count = parameters->update_count;

	// Free the old manifest, if any.
	// This would be a manifest from a previous aborted sync, that is cleaned up here to avoid stalling
	// the main thread.
	patchDbDestroy(&parameters->old_db_new);

	sprintf(current, "./%s.manifest", parameters->serverdb_name);
	sprintf(update, "./update/%s.manifest", parameters->serverdb_name);

	if(	!parameters->sync_verify_all_checkins &&
		!parameters->sync_ignore_manifest_similarity &&
		!parameters->update_is_incremental &&
		!fileCompare(current, update)) // nothing changed
	{
		result.state = UPDATE_MANIFEST_VIEW;
		estrPrintf(&result.estrStateText, "\"%s\" is the same as current manifest.", update);
		wtQueueMsg(mirroring_thread, MirroringThread_LoadManifest_Done, &result, sizeof(result));
		free(parameters->serverdb_name);
		filespecMapDestroy(parameters->frozenmap);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Get the manifest file size, for later use.
	result.manifest_file_size = fileSize(update);

	result.db_new = patchLoadDb(update, PATCHDB_POOLED_PATHS, parameters->frozenmap);
	if (!result.db_new)
	{
		if (result.manifest_file_size)
			estrPrintf(&result.loading_error, "Manifest is empty.");
		else
			estrPrintf(&result.loading_error, "Unable to parse manifest.");

		// Send result message back to main thread.
		wtQueueMsg(mirroring_thread, MirroringThread_LoadManifest_Done, &result, sizeof(result));

		return;
		PERFINFO_AUTO_STOP_FUNC();
	}

	if(parameters->update_is_incremental)
	{	
		log_printf(LOG_PATCHSERVER_UPDATE, "Creating a journal starting from %d", parameters->latest_rev);
		result.update_incremental_journal = journalCreate(parameters->latest_rev);
		if(eaSize(&result.db_new->checkins))
		{
			// Pad the checkins list with NULLs so that checkins[i]->rev == i still.
			Checkin **padded_checkins=NULL;
			first_checkin = eaHead(&result.db_new->checkins);
			last_checkin = eaTail(&result.db_new->checkins);
			if(first_checkin && last_checkin)
			{
				eaSetSize(&padded_checkins, first_checkin->rev);
				eaInsertEArray(&padded_checkins, &result.db_new->checkins, first_checkin->rev);
				assert(eaTail(&padded_checkins)==last_checkin);
				assert(eaSize(&padded_checkins)==last_checkin->rev+1);
				eaDestroy(&result.db_new->checkins);
				result.db_new->checkins = padded_checkins;
			}
		}
	}

	result.db_new = patchLinkDb(result.db_new, parameters->update_is_incremental);
	StructDestroy(parse_MirrorConfig, parameters->mirrorConfig);
	parameters->mirrorConfig = NULL;

	filespecMapDestroy(parameters->frozenmap);

	if(parameters->update_is_incremental)
	{
		if(eaSize(&result.db_new->checkins))
		{
			result.set_update_j = true;
			result.new_update_j = first_checkin->rev - 1;
			assert(first_checkin);
			result.state = UPDATE_MANIFEST_CHECKIN_VIEW;
			result.scan_mirror_config = true;
			estrPrintf(&result.estrStateText, "Done loading manifest, starting from checkin %d.", g_update_j+1);
		}
		else
		{
			// No new checkins, might have views though
			result.state = UPDATE_MANIFEST_COPY_START;
			estrPrintf(&result.estrStateText, "No new checkins.");
		}
	}
	else
	{
		result.set_update_j = true;
		result.new_update_j = 0; // i decided to do this one differently
		result.set_update_k = true;
		result.new_update_k = -1;
		result.scan_mirror_config = true;
		result.state = UPDATE_MANIFEST_PRUNE_CHECKIN;
		estrPrintf(&result.estrStateText, "Done loading manifest.");
	}
	free(parameters->serverdb_name);

	// Send result message back to main thread.
	wtQueueMsg(mirroring_thread, MirroringThread_LoadManifest_Done, &result, sizeof(result));

	PERFINFO_AUTO_STOP_FUNC();
}

// Data for MirroringThread_CopyManifest
struct CopyManifest_Data
{
	U32 update_count;
	char *serverdb_name;
	bool update_is_incremental;
	PatchJournal *update_incremental_journal;
	JournalCheckin *update_incremental_checkin;
	PatchDB *db_new;
	PatchDB *db_copy;
	FastPatchDB *db_copy_fast;
};

// Data for MirroringThread_CopyManifest_Done
struct CopyManifest_Result
{
	U32 update_count;
	UpdateStates state;
	char *estrStateText;
};

// Copy manifest in background thread.
void thread_CopyManifest(void *user_data, void *data, WTCmdPacket *packet)
{
	struct CopyManifest_Data *parameters = data;
	struct CopyManifest_Result result = {0};
	char fname[MAX_PATH], other_fname[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	result.update_count = parameters->update_count;

	if(parameters->update_is_incremental)
	{
		char *incremental_manifest = NULL;
		int len;
		size_t wrote;
		FILE *outfile;
		U32 now = getCurrentFileTime();

		// Figure out where to store history for this incremental manifest.
		sprintf(fname, "./update/%s.manifest", parameters->serverdb_name);
		sprintf(other_fname, "./history/%s.%i.incremental.manifest.gz", parameters->serverdb_name, now / SECONDS_PER_DAY * SECONDS_PER_DAY);
		makeDirectoriesForFile(other_fname);
		
		// Open up history file.
		outfile = fopen(other_fname, "abz");
		fprintf(outfile, "--update/%s.manifest %u\n", parameters->serverdb_name, now);
		if (!outfile)
			AssertOrAlert("COPYMANIFEST_INCREMENTAL_WRITE_HISTORY", "Unable to open \"%s\"", other_fname);

		// Write manifest.
		incremental_manifest = fileAlloc(fname, &len);
		wrote = fwrite(incremental_manifest, 1, len, outfile);
		if (wrote != len)
			AssertOrAlert("COPYMANIFEST_INCREMENTAL_WRITE_HISTORY", "Unable to write to \"%s\"", other_fname);

		// Close file.
		fclose(outfile);

		// Write out the journal for the changes made during the incremental sync.
		log_printf(LOG_PATCHSERVER_UPDATE, "Flushing a journal");
		journalFlushAndDestroy(&parameters->update_incremental_journal, parameters->serverdb_name);
	}
	else
	{

		// Move our current manifest out of the way, in preparation for being replaced.
		sprintf(fname, "./%s.manifest", parameters->serverdb_name);
		if(fileExists(fname))
		{
			int ret, n=0;
			sprintf(other_fname, "./history/%s.%i.manifest", parameters->serverdb_name, getCurrentFileTime());
			makeDirectoriesForFile(other_fname);
			while(1)
			{
				ret = patchRenameWithAlert(fname, other_fname);
				if(ret == 0)
					break;
				if(n > 90)
					assertmsgf(0, "Cannot rename old manifest file from %s to %s", fname, other_fname);
				n += 1;
				Sleep(1);
			}
			assert(fileGzip(other_fname));
		}

		if (parameters->db_copy_fast)
		{
			// Like below, but condensed version of the struct.
			ParserWriteTextFile(fname, parse_FastPatchDB, parameters->db_copy_fast, 0, 0);
		}
		else if (parameters->db_copy)
		{

			// The new way: copy our manifest, and have a background thread write it out.
			// This isn't perfect, but it's the best we can do until full sync is rewritten to use journaling.
			ParserWriteTextFile(fname, parse_PatchDB, parameters->db_copy, 0, 0);
		}
		else
		{

			// The old way: use the parent's manifest as our manifest when we restart.
			// Warning: This is very dangerous, and causes all sorts of problems.  The primary problem is that
			// it is incompatible with MirrorConfig, and you end up with all sorts of versions in the manifest which
			// aren't in the database.  If this server has a child that doesn't have as restrictive of a MirrorConfig,
			// sync will fail.
			sprintf(other_fname, "./update/%s.manifest", parameters->serverdb_name);
			assert(fileCopy(other_fname, fname) == 0);
		}
		
	}

	StructDestroy(parse_FastPatchDB, parameters->db_copy_fast);
	patchDbDestroy(&parameters->db_copy);
	patchDbDestroy(&parameters->db_new);

	// Go to next state.
	result.state = UPDATE_MANIFEST_VIEW;
	estrPrintf(&result.estrStateText, "%s", parameters->update_is_incremental?"":"Manifest copy complete.");

	// Send result message back to main thread.
	wtQueueMsg(mirroring_thread, MirroringThread_CopyManifest_Done, &result, sizeof(result));

	PERFINFO_AUTO_STOP_FUNC();
}

// Get the current mirroring status.
const char* patchmirrorUpdateStatus(PCL_Client *client)
{
	static char *str = NULL;

	estrClear(&str);
	
	// Prefix the status with the type of update, if we're updating.
	if (g_update_state != UPDATE_NOTIFYME_START && g_update_state != UPDATE_LISTING_START)
		estrPrintf(&str, "%s update: ", g_update_is_incremental ? "Incremental" : "Full");

	// General description.
	switch(g_update_state)
	{
		xcase UPDATE_NOTIFYME_START:
		acase UPDATE_LISTING_START:
			estrConcatf(&str, "Waiting for notification of new files");

		xcase UPDATE_SERVERDB_VIEW:
		acase UPDATE_SERVERDB_FILE:
		acase UPDATE_SERVERDB_DONE:
		{
			PatchServerDb *serverdb = eaGet(&g_patchserver_config.serverdbs, g_update_i);
			if(serverdb)
				estrConcatf(&str, "Processing %s.patchserverdb", serverdb->name);
			else
				estrConcatf(&str, "Preparing to patch .patchserverdb files");
		}

		xcase UPDATE_PROJECT_VIEW:
		acase UPDATE_PROJECT_FILE:
		acase UPDATE_PROJECT_DONE:
		acase UPDATE_CONFIGS_RELOAD:
		{
			PatchProject *project = eaGet(&g_patchserver_config.projects, g_update_i);
			if(project)
				estrConcatf(&str, "Processing %s.patchproject", project->name);
			else
				estrConcatf(&str, "Preparing to patch .patchproject files");
		}

		xcase UPDATE_MANIFEST_VIEW:
		acase UPDATE_MANIFEST_FILE:
		acase UPDATE_MANIFEST_LOAD_START: // this state spends a lot of time waiting on the .manifest to xfer
		{
			PatchServerDb *serverdb = eaGet(&g_patchserver_config.serverdbs, g_update_i);
			if(serverdb)
				estrConcatf(&str, "Retrieving %s.manifest", serverdb->name);
			else
				estrConcatf(&str, "Preparing to retrieve .manifest files");
		}

		xcase UPDATE_MANIFEST_LOAD_WAIT:
			estrConcatf(&str, "Loading %s db", g_patchserver_config.serverdbs[g_update_i]->name); // safe, the thread doesn't alter g_update_i

		xcase UPDATE_MANIFEST_PRUNE_CHECKIN:
			estrConcatf(&str, "Processing %s db. Checking for pruned files (Checkin %d/%d, %d deletes so far)",
								g_patchserver_config.serverdbs[g_update_i]->name,
								g_update_j+1,
								eaSize(&g_patchserver_config.serverdbs[g_update_i]->db->checkins),
								s_sync_deletes);

		xcase UPDATE_MANIFEST_CHECKIN_VIEW:
		acase UPDATE_MANIFEST_CHECKIN_FILE:
		acase UPDATE_MANIFEST_CHECKIN_DONE:
		{
			PatchServerDb *serverdb = g_patchserver_config.serverdbs[g_update_i];
			Checkin *checkin = eaGet(&serverdb->db_new->checkins, g_update_j);
			estrConcatf(&str, "Processing %s db. Retrieving new files", serverdb->name);
			if(checkin)
			{
				// Print the checkin that is being processed.
				estrConcatf(&str, " (Checkin %d/%d", g_update_j, eaSize(&serverdb->db_new->checkins));

				// Print the file that is being processed.
				if(g_update_k >= 0)
					estrConcatf(&str, " File %d/%d", g_update_k, eaSize(&checkin->versions));

				// Print current transfer summary.
				if (client && client->xferrer)
				{
					XferStateInfo **info = NULL;
					int size;
					xferrerGetStateInfo(client->xferrer, &info);
					size = eaSize(&info);
					if (size)
					{
						U32 so_far = 0;
						U32 total = 0;
						FOR_EACH_IN_EARRAY(info, XferStateInfo, i)
						{
							so_far += i->blocks_so_far;
							total += i->blocks_total;
						}
						FOR_EACH_END
						if (so_far && total && total >= so_far)
							estrConcatf(&str, " Blocks %d/%d", so_far, total);
						estrConcatf(&str, " Transfers %d/%d", size, MAX_XFERS);
					}
				}

				estrConcatf(&str, ")");
			}
		}

		xcase UPDATE_MANIFEST_COPY_START:
		acase UPDATE_MANIFEST_COPY_WAIT:
			estrConcatf(&str, "Processing %s db. Writing new db.", g_patchserver_config.serverdbs[g_update_i]->name);
	}

	// Advanced debugging information.
	if (client)
	{
		PCL_ConnectionStatus pcl_status;
		const char *status_text;
		char state_text[256];
		bool idle = false;

		// Get connection status.
		pclGetLinkConnectionStatus(client, &pcl_status);
		switch (pcl_status)
		{
			case PCL_CONNECTION_CONNECTED:
				status_text = "Connected with no view";
				break;
			case PCL_CONNECTION_VIEW_SET:
				status_text = "Connected with view set";
				break;
			case PCL_CONNECTION_INITIAL_CONNECT:
				status_text = "Connecting";
				break;
			case PCL_CONNECTION_NEGOTIATION:
				status_text = "Connect negotiation";
				break;
			case PCL_CONNECTION_RECONNECTING:
				// Note: Currently unsupported.
				status_text = "Reconnecting";
				break;
			case PCL_CONNECTION_DISCONNECTED:
				status_text = "Disconnected";
				break;
			default:
				devassert(0);
				status_text = "Unknown";
		}

		// Get idle state.
		pclIsIdle(client, &idle);

		// Get state string.
		pclGetStateString(client, SAFESTR(state_text));

		// Format advanced information.
		estrConcatf(&str, " (%s, %s, state \"%s\")",
			status_text,
			idle ? "PCL idle" : "PCL working",
			state_text);
	}

	return str;
}

static S32 fileIsIncludedInAProject(PatchServerDb* serverdb,
									const char* filePath,
									PatchProject** projectOut,
									const char* desiredProjectName)
{
	PatchProject*	pFound = NULL;
	S32				foundNonDb = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, i, isize);
		const PatchProject* p = serverdb->projects[i];

		if(p->is_db){
			continue;
		}
		
		foundNonDb = 1;
		
		if(patchprojectIsPathIncluded(p, filePath, NULL)){
			S32 isDesiredProject =	desiredProjectName &&
									!stricmp(p->name, desiredProjectName);
								
			if(	!pFound ||
				isDesiredProject)
			{
				pFound = serverdb->projects[i];
			}

			if(	isDesiredProject ||
				!desiredProjectName)
			{
				break;
			}
		}
	EARRAY_FOREACH_END;
	
	if(!foundNonDb){
		EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, i, isize);
			const PatchProject* p = serverdb->projects[i];

			if(p->is_db){
				pFound = serverdb->projects[i];
				break;
			}
		EARRAY_FOREACH_END;
	}
	
	if(projectOut){
		*projectOut = pFound;
	}
	
	return !!pFound;
}

static MirrorConfig* getMirrorConfig(PatchServerDb* serverdb){
	if(!serverdb->mirrorConfig){
		EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.mirrorConfig, i, isize);
			MirrorConfig* mc = g_patchserver_config.mirrorConfig[i];
			if(!stricmp(mc->db, serverdb->name)){
				serverdb->mirrorConfig = mc;
			}
		EARRAY_FOREACH_END;
	}
	return serverdb->mirrorConfig;
}

static int MirrorConfigComparator(const BranchToMirror **lhs, const BranchToMirror **rhs)
{
	if((*lhs)->branch < (*rhs)->branch)
		return -1;
	else if((*rhs)->branch < (*lhs)->branch)
		return 1;
	else
		return 0;
}

// Find any sandboxes with checkins that are explicitly included in the MirrorConfig
static void SandboxCheckMirrorConfig(PatchDB *db, Checkin *checkin, MirrorConfig *mirrorConfig)
{
	int val;
	if(checkin->incr_from == PATCHREVISION_NONE)
		return;

	PERFINFO_AUTO_START_FUNC();
	if(!stashFindInt(db->sandboxes_included, checkin->sandbox, &val))
	{
		bool addsandbox = false;
		if(mirrorConfig)
		{
			int lastBranchChecked = -1;
			EARRAY_CONST_FOREACH_BEGIN(mirrorConfig->branchToMirror, i, isize);
			{
				const BranchToMirror* b = mirrorConfig->branchToMirror[i];
				lastBranchChecked = b->branch;
				if(b->branch >= checkin->branch)
				{
					// We need this checkin if it's included on this branch, or we have skipped a branch
					if((b->branch == checkin->branch && checkin->rev >= b->firstBranchRevAfterStart)
						|| b->branch > checkin->branch)
					{
						addsandbox = true;
					}
					break;
				}
			}
			EARRAY_FOREACH_END;

			// If a branch is not included in the MirrorConfig, include it by default
			if(lastBranchChecked < checkin->branch)
				addsandbox = true;
		}

		if(!mirrorConfig || addsandbox)
			stashAddInt(db->sandboxes_included, checkin->sandbox, checkin->incr_from, false);
	}
	PERFINFO_AUTO_STOP();
}

static bool DoesManifestHaveSandboxesWithOldIncrFroms(PatchServerDb *serverdb, PatchDB *db)
{
	MirrorConfig *mirrorConfig = getMirrorConfig(serverdb);

	if(!mirrorConfig)
		return false;

	PERFINFO_AUTO_START_FUNC();
	FOR_EACH_IN_STASHTABLE2(db->sandboxes_included, elem)
	{
		char *sandbox = stashElementGetStringKey(elem);
		int incr_from = stashElementGetInt(elem);

		EARRAY_CONST_FOREACH_BEGIN(mirrorConfig->branchToMirror, i, isize);
		{
			const BranchToMirror* b = mirrorConfig->branchToMirror[i];
			// Have to check startAtRevision, because firstBranchRevAfterStart will be pushed too far ahead on incrementals.
			if(b->startAtRevision < S32_MAX && b->startAtRevision > incr_from)
			{
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
	return false;
}

static void FindSandboxesIncludedInMirrorConfig(PatchServerDb *serverdb, PatchDB *db)
{
	MirrorConfig* mc;
	int count;

	PERFINFO_AUTO_START_FUNC();

	// Get mirror config settings as stated in the config file.
	mc = getMirrorConfig(serverdb);
	count = eaSize(&db->checkins);

	if(count)
	{
		int i;
		U32 checkin_time = 0;
		if(db->sandboxes_included)
			stashTableClear(db->sandboxes_included);
		db->sandboxes_included = stashTableCreateWithStringKeys(96, StashDeepCopyKeys_NeverRelease);
		for(i = 0; i < count; i++)
		{
			Checkin *checkin = db->checkins[i];
			if(!checkin) continue;
			if(checkin->sandbox && checkin->sandbox[0])
			{
				SandboxCheckMirrorConfig(db, checkin, mc);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void updateMirrorStartingRevs(	PatchServerDb* serverdb,
										const PatchDB* db)
{
	MirrorConfig* mc;

	PERFINFO_AUTO_START_FUNC();

	// Get mirror config settings as stated in the config file.
	mc = getMirrorConfig(serverdb);
	if(!mc){
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Sort mirror config
	eaQSort(mc->branchToMirror, MirrorConfigComparator);

	// Adjust starting checkin later if the provided checkin number is not valid on this branch.
	EARRAY_CONST_FOREACH_BEGIN(mc->branchToMirror, i, isize);
		BranchToMirror* b = mc->branchToMirror[i];
		
		b->firstBranchRevAfterStart = S32_MAX;

		if(db->checkins)
		{
			bool foundCheckinOnBranch = false;
			EARRAY_CONST_FOREACH_BEGIN_FROM(db->checkins, j, jsize, b->startAtRevision);
				const Checkin* c = db->checkins[j];
				
				if(c && c->branch == b->branch)
				{
					foundCheckinOnBranch = true;
					if(j < b->firstBranchRevAfterStart)
					{
						b->firstBranchRevAfterStart = c->rev;
					}
				}
			EARRAY_FOREACH_END;

			// If we found no checkins, leave firstBranchRevAfterStart equal to startAtRevision to make sure we 
			// get something for this branch. 
			if(!foundCheckinOnBranch && b->startAtRevision < S32_MAX)
			{
				b->firstBranchRevAfterStart = b->startAtRevision;
			}
		}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

// Checks to see if we need to implicitly keep a file version because of a sandbox
static bool walkMirrorConfigWithSandbox(PatchServerDb* serverdb, FileVersion* v, int index, const MirrorConfig *mc, char *sandbox, int incr_from)
{
	int nextVersionBranch = S32_MAX;
	int nextVersionRev = S32_MAX;
	bool nextVersionInSandbox = false;
	int lastBranchChecked = -1;
	int nextIndex = index + 1;

	// Only called when we know that v is not being explicitly included
	// Only called with a sandbox and valid incr_from
	devassert(sandbox && sandbox[0]);
	devassert(incr_from != PATCHREVISION_NONE);

	if(!sandbox || !sandbox[0] || incr_from == PATCHREVISION_NONE)
		return false;

	// v has no sandbox, so it can only be implicitly required if it less than or equal to incr_from
	if(v->rev > incr_from)
		return false;

	PERFINFO_AUTO_START_FUNC();
	// Get the next version relative to the given sandbox
	// It is either the next non-sandbox checkin with a rev less than incr_from, or the next sandbox checkin
	while(nextIndex < eaSize(&v->parent->versions))
	{
		bool match = false;
		FileVersion *possibleNextVer = v->parent->versions[nextIndex];
		if((possibleNextVer->checkin->incr_from == incr_from) && stricmp(possibleNextVer->checkin->sandbox, sandbox) == 0)
		{
			nextVersionInSandbox = true;
			match = true;
		}
		else if(possibleNextVer->checkin->incr_from == PATCHREVISION_NONE && possibleNextVer->rev <= incr_from)
		{
			match = true;
		}
		
		if(match)
		{
			nextVersionBranch = possibleNextVer->checkin->branch;
			nextVersionRev = possibleNextVer->rev;
			break;
		}

		nextIndex = nextIndex + 1;
	}

	EARRAY_CONST_FOREACH_BEGIN(mc->branchToMirror, i, isize);
	{
		const BranchToMirror* b = mc->branchToMirror[i];
		if(b->branch == v->checkin->branch)
		{
			if(v->checkin->branch == nextVersionBranch && !nextVersionInSandbox && nextVersionRev <= incr_from)
			{
				// Covered by non-sandbox version
				PERFINFO_AUTO_STOP();
				return false;
			}
			else if(v->checkin->branch == nextVersionBranch && nextVersionInSandbox && b->firstBranchRevAfterStart >= nextVersionRev)
			{
				// Covered by sandbox version
				PERFINFO_AUTO_STOP();
				return false;
			}
			else if(b->firstBranchRevAfterStart < S32_MAX)
			{
				// Implicitly needed for this branch, because it is not covered by either a sandbox or non-sandbox version
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		else if(b->branch > v->checkin->branch)
		{
			if(b->branch > lastBranchChecked + 1)
			{
				// Needed for a branch not listed in MirrorConfig and so kept
				PERFINFO_AUTO_STOP();
				return true;
			}
			else if(b->firstBranchRevAfterStart == S32_MAX && nextVersionBranch <= b->branch)
			{
				// Covered by version on a later branch
				PERFINFO_AUTO_STOP();
				return false;
			}
			else if(b->firstBranchRevAfterStart < S32_MAX)
			{
				// This expression checks for a FileVersion between v and b
				PERFINFO_AUTO_STOP();
				return b->branch <= nextVersionBranch && (b->branch < nextVersionBranch || b->firstBranchRevAfterStart < nextVersionRev);
			}
		}
		lastBranchChecked = b->branch;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	// We ran out of explicit MirrorConfigs, so see if there are any branches left to require this FileVersion
	return lastBranchChecked < serverdb->max_branch && nextVersionBranch > lastBranchChecked;
}

static bool walkMirrorConfigNoSandbox(PatchServerDb* serverdb, FileVersion* v, int index, const MirrorConfig *mc)
{
	int nextVersionBranch = S32_MAX;
	int nextVersionRev = S32_MAX;
	int lastBranchChecked = -1;
	int nextIndex = index + 1;

	PERFINFO_AUTO_START_FUNC();
	// Get the next version with a matching sandbox
	while(nextIndex < eaSize(&v->parent->versions))
	{
		FileVersion *possibleNextVer = v->parent->versions[nextIndex];
		if((possibleNextVer->checkin->incr_from == v->checkin->incr_from) && stricmp(possibleNextVer->checkin->sandbox, v->checkin->sandbox) == 0)
		{
			nextVersionBranch = possibleNextVer->checkin->branch;
			nextVersionRev = possibleNextVer->rev;
			break;
		}

		nextIndex = nextIndex + 1;
	}

	EARRAY_CONST_FOREACH_BEGIN(mc->branchToMirror, i, isize);
	{
		const BranchToMirror* b = mc->branchToMirror[i];
		if(b->branch == v->checkin->branch)
		{
			if(v->rev >= b->firstBranchRevAfterStart)
			{
				// Explicitly included
				PERFINFO_AUTO_STOP();
				return true;
			}
			else if(v->checkin->branch == nextVersionBranch && nextVersionRev <= b->firstBranchRevAfterStart)
			{
				// covered
				PERFINFO_AUTO_STOP();
				return false;
			}
			else if(b->firstBranchRevAfterStart < S32_MAX)
			{
				// Implicitly needed for this branch
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		else if(b->branch > v->checkin->branch)
		{
			if(b->branch > lastBranchChecked + 1)
			{
				// Skipped a branch, so we need this checkin
				PERFINFO_AUTO_STOP();
				return true;
			}
			else if(b->firstBranchRevAfterStart == S32_MAX && nextVersionBranch <= b->branch)
			{
				// Covered by a checkin on a later branch
				PERFINFO_AUTO_STOP();
				return false;
			}
			else if(b->firstBranchRevAfterStart < S32_MAX)
			{
				// This expression checks for a FileVersion between v and b
				PERFINFO_AUTO_STOP();
				return b->branch <= nextVersionBranch && (b->branch < nextVersionBranch || b->firstBranchRevAfterStart < nextVersionRev);
			}
		}
		lastBranchChecked = b->branch;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	// We ran out of explicit MirrorConfigs, so see if there are any branches left to require this FileVersion
	return lastBranchChecked < serverdb->max_branch && nextVersionBranch > lastBranchChecked;
}

// There is a check in patchserver.c that seems not to allow a sandbox on multiple branches
static bool mirrorConfigIncludesFileVersion(	PatchServerDb* serverdb,
											FileVersion* v)
{
	bool keep = false;
	bool found_branch = false;
	const MirrorConfig* mc = getMirrorConfig(serverdb);
	int index = -1;

	if(!mc){
		return true;
	}
	
	PERFINFO_AUTO_START_FUNC();

	//If v's sandbox is not explicitly included, skip it.
	//If the incr_from is PATCHREVISION_NONE, the rest of the code treats it as if it is not a sandbox.
	if(v->checkin->sandbox && v->checkin->sandbox[0] && v->checkin->incr_from != PATCHREVISION_NONE)
	{
		if(!stashFindInt(serverdb->db_new->sandboxes_included, v->checkin->sandbox, NULL))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// find the index of v in the versions list
	EARRAY_CONST_FOREACH_BEGIN(v->parent->versions, j, jsize);
	{
		const FileVersion* vCheck = v->parent->versions[j];

		if(vCheck == v)
		{
			index = j;
			break;
		}
	}
	EARRAY_FOREACH_END;

	devassert(index >= 0);

	keep = walkMirrorConfigNoSandbox(serverdb, v, index, mc);

	// sandbox checkins cannot be implicitly required by checkins from other sandboxes
	if(!keep && !(v->checkin->sandbox && v->checkin->sandbox[0]))
	{
		// walk all sandboxes that we must keep and figure out if we need this checkin for that.
		FOR_EACH_IN_STASHTABLE2(serverdb->db_new->sandboxes_included, elem);
		{
			char *sandbox = stashElementGetStringKey(elem);
			int incr_from = stashElementGetInt(elem);
			keep = walkMirrorConfigWithSandbox(serverdb, v, index, mc, sandbox, incr_from);
			if(keep)
				break;
		}
		FOR_EACH_END;
	}

	PERFINFO_AUTO_STOP();
	return keep;
}

// Sync a specific file from the parent.
static PCL_ErrorCode syncFileFromParent(PCL_Client *client, const char *fileName, const char *fileNameToWrite, HALHogFile *halhog,
							   int priority, FileVersion *ver)
{
	HALHogFile *localhalhog = halhog;
	PCL_ErrorCode error;
	struct updateFileCallback_userdata *completion_data;

	PERFINFO_AUTO_START_FUNC();

	devassert(client);
	
	if (halhog)
		localhalhog = patchHALHogFileAddRef(halhog);

	completion_data = malloc(sizeof(struct updateFileCallback_userdata));
	completion_data->halhog = localhalhog;
	completion_data->ver = ver;

	error = pclGetFileTo(client, fileName, fileNameToWrite, localhalhog ? localhalhog->hog : NULL, priority, updateFileCallback, completion_data);
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "SyncFileFromParent",
		("filename", "%s", NULL_TO_EMPTY(fileName))
		("fileNameToWrite", "%s", NULL_TO_EMPTY(fileNameToWrite))
		("hog", "%s", localhalhog && localhalhog->hog ? hogFileGetArchiveFileName(localhalhog->hog) : "")
		("priority", "%d", priority)
		("error", "%d", error));

	if (error != PCL_SUCCESS)
	{
		if(localhalhog)
			patchHALHogFileDestroy(localhalhog, false);

		free(completion_data);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

// Return true if the db does not match this version.
static bool dbNeedsUpdate(PatchDB *db, FileVersion *ver)
{
	FileVersion *old_ver = findMatchingFileVersion(db, ver);
	return !old_ver || !fileVersionsEqual(old_ver, ver);
}

// Update the view if anything, including the necessary project, has changed.
// Return true if we need to wait for the view to update.
static bool updateView(PCL_Client *client, const Checkin *checkin, PCL_ErrorCode *error, const char **error_details)
{
	bool views_match;

#define PCL_DO_ERROR()																	\
	do {																				\
		if (*error != PCL_SUCCESS)														\
			return true;																\
	} while(0)

#define PCL_DO(funccall)																\
	do {																				\
		*error = (funccall);															\
		PCL_DO_ERROR();																	\
	} while(0)

	PCL_DO(pclCompareView(client,
		g_update_project_name,
		checkin->branch,
		checkin->sandbox,
		checkin->rev,
		&views_match));

	if(!views_match)
	{
		PCL_DO(pclSetViewByRev(	client,
			g_update_project_name,
			checkin->branch,
			checkin->sandbox,
			checkin->rev,
			false,
			false,
			updateSetViewCallback,
			NULL));

		// Wait for view to change.
		g_update_callback_waiting = true;

		patchupdateLogVerbose("UPDATE_MANIFEST_CHECKIN_FILE: Changing view to %s %d %s %d.", g_update_project_name, checkin->branch, checkin->sandbox, checkin->rev);

		return true;
	}

	return false;

#undef PCL_DO_ERROR
#undef PCL_DO
}

#define PCL_DO_ERROR()																	\
	do {																				\
		if (error != PCL_SUCCESS)														\
		{																				\
			pclGetErrorDetails(client, &error_details);									\
			patchupdateError(error, error_details);										\
			return;																		\
		}																				\
	} while(0)

#define PCL_DO(funccall)																\
	do {																				\
		error = (funccall);																\
		PCL_DO_ERROR();																	\
	} while(0)


static void patchserverdbCheckinFile(PCL_Client *client, PatchServerDb *serverdb, const Checkin* checkin)
{
	S32				max_xfers;
	S32				xfers;
	PCL_ErrorCode error;
	const char *error_details;

	PCL_DO(pclMaxXfers(client, &max_xfers));
	PCL_DO(pclXfers(client, &xfers));
	if(xfers >= max_xfers) // we don't have any room to queue up more files
		return;

	while(	timerElapsed(g_update_timer) < UPDATE_TIME_PER_FRAME &&
			++g_update_k < eaSize(&checkin->versions))
	{
		// We have more versions to get.
		
		FileVersion*		ver = checkin->versions[g_update_k];
		HALHogFile*			halhog;
		FileNameAndOldName	pathInHogg;
		PatchProject*		project;
		
		// Check if this file is in one of my projects.
		if(!fileIsIncludedInAProject(	serverdb,
										ver->parent->path,
										&project,
										g_update_project_name))
			continue;
		
		// Check if this file is in the required revision range.
		if(!mirrorConfigIncludesFileVersion(serverdb, ver))
			continue;

		patchupdateLogVerbose("UPDATE_MANIFEST_CHECKIN_FILE: Updating file \"%s\" version %d.", ver->parent->path, ver->version);
		patchupdateLogVerbose("UPDATE_MANIFEST_CHECKIN_FILE: Current view parameters %s %d %s %d.", client->project, client->branch, client->sandbox, client->rev);
			
		if(stricmp(g_update_project_name, project->name)){
			PCL_DO(pclXfers(client, &xfers));
			if(xfers){
				// Have to wait for current xfers to finish before switching the project.
				
				g_update_k--;
				break;
			}
		
			strcpy(g_update_project_name, project->name);
		}
		
		// If this checkin looks OK, continue.
		if (!g_sync_verify_all_checkins && !dbNeedsUpdate(serverdb->db, ver))
			continue;

		halhog = patchHALGetWriteHogHandle(serverdb, checkin->time, true);
		assertmsgf(halhog,"Couldn't create hogg for update: %s %lu", serverdb->name, checkin->time);
		
		// FIXME: The following appears to protect against data loss from a crash.  However, there's probably other places that are not safe,
		// and verifying frequently is probably a better way to deal with this.  This probably substantially slows down syncing in some cases.
		// For now, I'm disabling this code, and we'll see how things go.
		//if(hogFileNeedsToFlush(hogg))
		//{
		//	g_update_k--;
		//	hogFileDestroy(hogg, false);
		//	break;
		//}

		patchserverdbNameInHogg(serverdb, ver, &pathInHogg);

		if(ver->flags & FILEVERSION_DELETED)
		{
			HogFileIndex hfi;
			
			hfi = hogFileFind(halhog->hog, pathInHogg.name);
			
			if(hfi == HOG_INVALID_INDEX)
			{
				hfi = hogFileFind(halhog->hog, pathInHogg.oldName);
				
				if(hfi == HOG_INVALID_INDEX)
				{
					hogFileModifyUpdateNamed(	halhog->hog,
												pathInHogg.name,
												malloc(0),
												0,
												ver->modified,
												NULL);
				}
			}
			else
			{
				hfi = hogFileFind(halhog->hog, pathInHogg.oldName);
				
				if(hfi != HOG_INVALID_INDEX)
				{
					patchserverdbHogDelete(serverdb, halhog->hog, pathInHogg.oldName, "UnusedVersionFileOldResync");
				}
			}
			
			updateAddVersion(serverdb->db, ver);
		}
		else
		{
			bool new_checkin;
			{
				HogFileIndex hfi = hogFileFind(halhog->hog, pathInHogg.name);
				
				if(hfi == HOG_INVALID_INDEX)
				{
					hfi = hogFileFind(halhog->hog, pathInHogg.oldName);
				}
				
				if(	hfi != HOG_INVALID_INDEX &&
					ver->size == hogFileGetFileSize(halhog->hog, hfi) &&
					ver->checksum == hogFileGetFileChecksum(halhog->hog, hfi) &&
					ver->modified == hogFileGetFileTimestamp(halhog->hog, hfi))
				{
					S32 needsUpdate = 0;
					
					if (s_sync_deep_verify_all_checkins)
					{
						void*	data;
						U32		dataBytes;
						bool	checksumIsValid;
						
						data = hogFileExtract(halhog->hog, hfi, &dataBytes, &checksumIsValid);

						if(!data)
						{
							needsUpdate = 1;
						}
						else
						{
							SAFE_FREE(data);
							
							needsUpdate =	dataBytes != ver->size ||
											!checksumIsValid;
						}
					}

					if(!needsUpdate){
						updateAddVersion(serverdb->db, ver);
						patchHALHogFileDestroy(halhog, false);
						halhog = NULL;
						continue;
					}
				}
			}

			new_checkin = checkin->rev == eaSize(&serverdb->db->checkins) - 1;
			if (!new_checkin)
			{

// Because this happens frequently, turn off this alert for now until we have time to fix it.  See [COR-14117].
#if 0
				static int alert_count = 0;
				const int max_alerts = 15;
				++alert_count;
				if (alert_count < max_alerts)
					ErrorOrAlert("RESYNC_OLD_FILE", "Getting file %s for old checkin %d on project %s from parent, but we should have already had it!",
					pathInHogg.name, ver->rev, client->project);
				else if (alert_count == max_alerts)
					ErrorOrAlert("RESYNC_OLD_FILE_SUPPRESSED", "RESYNC_OLD_FILE issued %d times; automatically suppressing this alert", alert_count);
#endif

				SERVLOG_PAIRS(LOG_PATCHSERVER_UPDATE, "RsyncOldFile",
					("file", "%s", pathInHogg.name)
					("old_checkin", "%d", ver->rev)
					("project", "%s", client->project));
			}

			// Update the view if anything, including the necessary project, has changed.
			if (updateView(client, checkin, &error, &error_details))
			{
				patchHALHogFileDestroy(halhog, false);
				halhog = NULL;
				if(error != PCL_SUCCESS)
					PCL_DO_ERROR();
				// Back up one, so when we get back here we increment to the right ver.
				g_update_k--;
				break;
			}

			// Get the file.
			error = syncFileFromParent(client, ver->parent->path, pathInHogg.name, halhog, 1, ver);

			if(error == PCL_XFERS_FULL)
			{
				g_update_k--; // try this one again next tick
				patchHALHogFileDestroy(halhog, false);
				halhog = NULL;
				break;
			}
			else if(error != PCL_SUCCESS)
			{
				patchHALHogFileDestroy(halhog, false);
				PCL_DO_ERROR();
			}

			printfColor(COLOR_BRIGHT|COLOR_GREEN,
				"Getting ver %d: %s\n",
				ver->rev,
				pathInHogg.name);

			// if we don't mind the overhead of diffing, make a copy of the previous version of the file
			// FIXME: The real problem here isn't the overhead of the bindiffing, but the fact thing thing will stall the main thread
			// while it blocks waiting for the old version to load and copy over.  Placing what is essentially bad data into the new hog
			// into the new hog is a little questionable too.  And this probably won't work at all for compressed bindiffing, because it
			// recompresses the data, which is completely unnecessary.
			// A better way to do this is to teach PCL how to bindiff against an arbitrary file, and let PCL deal with it.
			// (Unfortunately, PCL currently has a similar problem where it blocks the process thread during some parts of the xfer.)
			if( g_patchserver_config.low_bandwidth &&
				HOG_INVALID_INDEX == hogFileFind(halhog->hog, pathInHogg.name) )
			{
				FileVersion* old_ver;

				PERFINFO_AUTO_START("BindiffSetup", 1);

				old_ver = patchFindVersion(				serverdb->db,
														ver->parent->path,
														checkin->branch,
														checkin->sandbox,
														checkin->time,
														checkin->incr_from);

				if(old_ver)
				{
					HALHogFile *old_halhog = patchHALGetWriteHogHandle(	serverdb,
																	old_ver->checkin->time,
																	false);
					if(old_halhog)
					{
						FileNameAndOldName	oldFileName;
						HogFileIndex		hfi;

						patchserverdbNameInHogg(serverdb, old_ver, &oldFileName);
						
						hfi = hogFileFind(old_halhog->hog, oldFileName.name);
						
						if(hfi == HOG_INVALID_INDEX)
						{
							hfi = hogFileFind(old_halhog->hog, oldFileName.oldName);
						}
						
						if(hfi != HOG_INVALID_INDEX)
						{
							U32		size;
							char*	data = hogFileExtract(old_halhog->hog, hfi, &size, NULL);
							
							patchserverdbWriteFile(	serverdb->name,
													halhog->hog,
													pathInHogg.name,
													data,
													size,
													0,
													0,
													ver->modified,
													ver->checkin,
													serverdb->mirrorHoggsLocally);

							patchserverdbHogDelete(serverdb, halhog->hog, pathInHogg.oldName, "BindiffRemoveOldname");
						}
						else
						{
							ERROR_PRINTF(	"Could not find %s in %s to copy for diffing",
											oldFileName.name,
											hogFileGetArchiveFileName(old_halhog->hog));
						}
						patchHALHogFileDestroy(old_halhog, false);
					}
				}

				PERFINFO_AUTO_STOP_CHECKED("BindiffSetup");
			}
		}
		patchHALHogFileDestroy(halhog, false);
		halhog = NULL;
	}
	if(g_update_k >= eaSize(&checkin->versions))
	{
		patchmirroringSwitchStatef(UPDATE_MANIFEST_CHECKIN_DONE, "");
	}
}

#undef PCL_DO_ERROR
#define PCL_DO_ERROR()																		\
	do {																					\
		if (error != PCL_SUCCESS)															\
		{																					\
			patchupdateError(error, error_details);											\
			return false;																	\
		}																					\
	} while (0)

// Finish LoadManifest().
static void LoadManifest_Done(void *user_data, void *data, WTCmdPacket *packet)
{
	struct LoadManifest_Result *result = data;
	
	// It's possible that this is a completion from a previous sync that was aborted.  Make sure it's
	// for the right one.
	if (result->update_count == s_update_counter)
	{
		bool skip_state_update = false;
		PatchServerDb *serverdb = g_patchserver_config.serverdbs[g_update_i];

		if (result->loading_error)
		{
			AssertOrAlert("MIRRORING_MANIFEST_LOAD_ERROR", "Loading manifest \"%s\" from \"%s\":%d failed: %s",
				serverdb->name,
				g_patchserver_config.parent.server, g_patchserver_config.parent.port,
				result->loading_error);
			estrDestroy(&result->loading_error);
			patchmirroringResetConnection();
			skip_state_update = true;
		}
		else
		{

			// Save manifest file size, for later use.
			s_last_manifest_size = result->manifest_file_size;

			// Copy database.
			serverdb->db_new = result->db_new;
			g_update_incremental_journal = result->update_incremental_journal;

			if(serverdb->db_new)
			{
				updateMirrorStartingRevs(serverdb, serverdb->db_new);
				if(result->scan_mirror_config)
				{
					FindSandboxesIncludedInMirrorConfig(serverdb, serverdb->db_new);
					if(g_update_is_incremental && DoesManifestHaveSandboxesWithOldIncrFroms(serverdb, serverdb->db_new))
					{
						patchmirroringNextMirrorForceFull();
						patchmirroringResetConnection();
						skip_state_update = true;
					}
				}
			}
		}

		if(!skip_state_update)
		{
			// Update state.
			if (result->set_update_j)
				g_update_j = result->new_update_j;
			if (result->set_update_k)
				g_update_k = result->new_update_k;
			patchmirroringSwitchStatef(result->state, "%s", result->estrStateText);
		}
	}
	else
	{
		journalDestroy(&result->update_incremental_journal);
	}

	estrDestroy(&result->estrStateText);
}

// Finish CopyManifest().
static void CopyManifest_Done(void *user_data, void *data, WTCmdPacket *packet)
{
	struct CopyManifest_Result *result = data;

	if (result->update_count == s_update_counter)
		patchmirroringSwitchStatef(result->state, "%s", result->estrStateText);
	estrDestroy(&result->estrStateText);
}

// Initialize background thread.
static void initMirroringThread()
{
	if (!mirroring_thread)
	{
		mirroring_thread = wtCreate(16, 16, NULL, "MirroringThread");
		wtRegisterCmdDispatch(mirroring_thread, MirroringThread_LoadManifest, thread_LoadManifest);
		wtRegisterMsgDispatch(mirroring_thread, MirroringThread_LoadManifest_Done, LoadManifest_Done);
		wtRegisterCmdDispatch(mirroring_thread, MirroringThread_CopyManifest, thread_CopyManifest);
		wtRegisterMsgDispatch(mirroring_thread, MirroringThread_CopyManifest_Done, CopyManifest_Done);
		wtSetThreaded(mirroring_thread, true, 0, false);
		wtStart(mirroring_thread);
	}
}

// Load the manifest from the parent in a background thread.
static void LoadManifest(PatchServerDb *serverdb)
{
	MirrorConfig *mc;
	struct LoadManifest_Data data;

	assertServerDBSanity(serverdb);

	// Set up parameters.
	data.update_count = s_update_counter;
	data.serverdb_name = strdup(serverdb->name);
	data.sync_verify_all_checkins = g_sync_verify_all_checkins;
	data.sync_ignore_manifest_similarity = s_sync_ignore_manifest_similarity;
	data.update_is_incremental = g_update_is_incremental;
	data.frozenmap = patchserverdbCreateFilemapFromConfig(&serverdb->frozenmap_config);
	data.latest_rev = serverdb->latest_rev;
	if(mc = getMirrorConfig(serverdb))
	{
		data.mirrorConfig = StructCreate(parse_MirrorConfig);
		StructCopyAll(parse_MirrorConfig, mc, data.mirrorConfig);
	}
	else
	{
		data.mirrorConfig = NULL;
	}
	data.old_db_new = serverdb->db_new;

	// Eliminate current db_new.
	// Ownership has been transferred to the background thread so it can be destroyed.
	serverdb->db_new = NULL;

	// Send command to background thread.
	initMirroringThread();
	wtQueueCmd(mirroring_thread, MirroringThread_LoadManifest, &data, sizeof(data));
}

// Copy the manifest in a background thread.
static void CopyManifest(PatchServerDb *serverdb)
{
	PatchDB *db_copy = NULL;
	FastPatchDB *db_copy_fast = NULL;
	struct CopyManifest_Data data;

	// On a full update, make a quick copy of the manifest, to be serialized by a background thread.
	if (!g_update_is_incremental && s_sync_write_new_manifest)
	{
		volatile S64 start;
		volatile S64 stop;

		PERFINFO_AUTO_START("CloneManifest", 1);
		start = timerCpuTicks64();
		if (s_sync_fast_copy_manifest)
			db_copy_fast = patchserverdbFastCopy(serverdb->db, s_last_manifest_size);
		else
		{
			db_copy = StructCreate(parse_PatchDB);
			StructCopy(parse_PatchDB, serverdb->db, db_copy, 0, 0, 0);
		}
		stop = timerCpuTicks64();

		// TODO: Remove this once we're sure CloneManifest is fast enough.
		patchupdateLog("CloneManifest(name \"%s\" duration %f)", NULL_TO_EMPTY(serverdb->name), timerSeconds64(stop-start));

		PERFINFO_AUTO_STOP_CHECKED("CloneManifest");
	}

	// Set up parameters.
	data.update_count = s_update_counter;
	data.serverdb_name = strdup(serverdb->name);
	data.update_is_incremental = g_update_is_incremental;
	data.update_incremental_journal = g_update_incremental_journal;
	data.update_incremental_checkin = g_update_incremental_checkin;
	data.db_new = serverdb->db_new;
	data.db_copy = db_copy;
	data.db_copy_fast = db_copy_fast;

	// Clear new database.
	serverdb->db_new = NULL;
	g_update_incremental_checkin = NULL;
	g_update_incremental_journal = NULL;

	// Send command to background thread.
	initMirroringThread();
	wtQueueCmd(mirroring_thread, MirroringThread_CopyManifest, &data, sizeof(data));
}

// Start and maintain sync process with parent server.
// This is called by patchupdate.
bool patchmirroringMirrorProcess(PCL_Client *client, bool continuing)
{
	static time_t startTime;
	PCL_ErrorCode error;
	const char *error_details = NULL;
	static StaticCmdPerf statePerf[UPDATE_STATE_COUNT];
	bool progress = false;
	static UpdateStates initial_update_state;
	static int initial_update_state_update_i, initial_update_state_update_j, initial_update_state_update_k;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Preliminary", 1);

	// Process background operations.
	if( g_update_state == UPDATE_MANIFEST_LOAD_WAIT ||	// a background thread is loading a new manifest
		g_update_state == UPDATE_MANIFEST_COPY_WAIT )	// a background thread is copying a manifest and destroying a temporary db
	{
		if (mirroring_thread && !continuing)
			wtMonitor(mirroring_thread);
		PERFINFO_AUTO_STOP();
		if (g_update_state == UPDATE_MANIFEST_LOAD_WAIT)
		{
			ADD_MISC_COUNT(1000000, "UPDATE_MANIFEST_LOAD_WAIT");
		}
		else if (g_update_state == UPDATE_MANIFEST_COPY_WAIT)
		{
			ADD_MISC_COUNT(1000000, "UPDATE_MANIFEST_COPY_WAIT");
		}
		PERFINFO_AUTO_STOP_FUNC();
		return progress;
	}

	if(g_update_callback_waiting) // waiting for patchclient operations to finish
	{
		PERFINFO_AUTO_STOP();
		ADD_MISC_COUNT(1000000, "update_callback_waiting");
		PERFINFO_AUTO_STOP_FUNC();
		return progress;
	}

	// If this is the first call this tick, start the timeout.
	if (!continuing)
		timerStart(g_update_timer);

	// Save initial state, for comparison later.
	initial_update_state = g_update_state;
	initial_update_state_update_i = g_update_i;
	initial_update_state_update_j = g_update_j;
	initial_update_state_update_k = g_update_k;

	PERFINFO_AUTO_STOP();

	if(g_update_state >= 0 && g_update_state < ARRAY_SIZE(statePerf)){
		if(!statePerf[g_update_state].name){
			char buffer[100];
			sprintf(buffer, "State:%s (%d)", StaticDefineIntRevLookup(UpdateStatesEnum, g_update_state), g_update_state);
			statePerf[g_update_state].name = allocAddString(buffer);
		}
		PERFINFO_AUTO_START_STATIC(statePerf[g_update_state].name, &statePerf[g_update_state].pi, 1);
	}else{
		PERFINFO_AUTO_START("State:Unknown", 1);
	}

	switch(g_update_state)
	{
		xcase UPDATE_NOTIFYME_START:
			if(PCL_SUCCESS == pclNotifyMe(client, updateNotifyCallback, NULL))
			{
				printf("Requested notification from parent server.\n");
				g_update_callback_waiting = true;
				g_update_notifyhalt = false;
				patchmirroringSwitchStatef(UPDATE_LISTING_START, "Requesting notification from parent server.");
			}

		xcase UPDATE_LISTING_START:
			clearUpdateTimers();
			startTime = time(NULL);
			s_sync_deletes = 0;
			if (g_update_notifyhalt)
				pclNotifyHalt(client);
			if(!g_patchserver_config.full_mirror_every)
				g_update_is_incremental = false;
			else if(startTime - g_update_last_full >= g_patchserver_config.full_mirror_every)
				g_update_is_incremental = false;
			else
				g_update_is_incremental = true;
			
			if(g_update_is_incremental)
				printf("Running incremental synchronization\n");
			else
				printf("Running full synchronization\n");

			PCL_DO(pclGetProjectList(client, updateListingCallback, NULL));
			g_update_callback_waiting = true;
			g_update_i = -1;
			++s_update_counter;
			patchmirroringSwitchStatef(UPDATE_SERVERDB_VIEW, "Getting project list, update is %s.", g_update_is_incremental?"incremental":"full");

		xcase UPDATE_SERVERDB_VIEW:
		{
			if(++g_update_i < eaSize(&g_patchserver_config.serverdbs)) // we have more patchserverdb files to update
			{
				PatchServerDb *serverdb = g_patchserver_config.serverdbs[g_update_i];

				if(serverdb->update_me) // we've determined that our parent has this serverdb
				{
					PCL_DO(pclClearCounts(client));
					PCL_DO(pclSetDefaultView(	client,
												serverdb->name,
												false,
												updateSetViewCallback,
												NULL));
					g_update_callback_waiting = true;
					patchmirroringSwitchStatef(UPDATE_SERVERDB_FILE, "%s", serverdb->name);
				}
			}
			else
			{
				patchmirroringSwitchStatef(UPDATE_PROJECT_VIEW, "done with the .patchserverdb files, move on to the .patchproject files");
				g_update_i = -1;
			}
		}

		xcase UPDATE_SERVERDB_FILE:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			char			fname[MAX_PATH];
			char			update_fname[MAX_PATH];
			
			sprintf(fname, "%s.patchserverdb", serverdb->name);
			sprintf(update_fname, "./update/%s", fname);
			PCL_DO(syncFileFromParent(client, fname, update_fname, NULL, 1, NULL));
			patchmirroringSwitchStatef(UPDATE_SERVERDB_DONE, "");
		}

		xcase UPDATE_SERVERDB_DONE:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			char			fname[MAX_PATH];
			char			update_fname[MAX_PATH];
			S32				xfers;
			
			PCL_DO(pclXfers(client, &xfers));
			if(xfers) // we aren't actually done yet
				break;

			sprintf(fname, "./%s.patchserverdb", serverdb->name);
			sprintf(update_fname, "./update/%s", fname);
			if(	!g_sync_verify_all_checkins &&
				!fileCompare(fname, update_fname))
			{
			}
			else
			{
				if(fileExists(fname))
				{
					char history_fname[MAX_PATH];
					sprintf(history_fname, "./history/%s.%i.patchserverdb", serverdb->name, getCurrentFileTime());
					makeDirectoriesForFile(history_fname);
					assert(patchRenameWithAlert(fname, history_fname) == 0);
					assert(fileGzip(history_fname));
				}
				assert(fileCopy(update_fname, fname) == 0);
				serverdb->load_me = true;
			}
			patchmirroringSwitchStatef(UPDATE_SERVERDB_VIEW, "");
		}

		xcase UPDATE_PROJECT_VIEW:
			if(++g_update_i < eaSize(&g_patchserver_config.projects)) // we have more projects to update
			{
				PatchProject *project = g_patchserver_config.projects[g_update_i];
				if(project->update_me) // we've determined that our parent has this project, and that it isn't a db project
				{
					assert(!project->is_db);
					PCL_DO(pclClearCounts(client));
					PCL_DO(pclSetDefaultView(	client,
												g_patchserver_config.projects[g_update_i]->name,
												false,
												updateSetViewCallback,
												NULL));
					g_update_callback_waiting = true;
					patchmirroringSwitchStatef(UPDATE_PROJECT_FILE, "%s", project->name);
				}
			}
			else
			{
				patchmirroringSwitchStatef(UPDATE_CONFIGS_RELOAD, "");  // once we have all the new config files, load 'em up
			}

		xcase UPDATE_PROJECT_FILE:
		{
			PatchProject*	project = g_patchserver_config.projects[g_update_i];
			char			fname[MAX_PATH];
			char			update_fname[MAX_PATH];

			sprintf(fname, "%s.patchproject", project->name);
			sprintf(update_fname, "./update/%s", fname);
			PCL_DO(syncFileFromParent(client, fname, update_fname, NULL, 1, NULL));
			patchmirroringSwitchStatef(UPDATE_PROJECT_DONE, "");
		}

		xcase UPDATE_PROJECT_DONE:
		{
			PatchProject*	proj = g_patchserver_config.projects[g_update_i];
			char			fname[MAX_PATH];
			char			update_fname[MAX_PATH];
			S32				xfers;

			PCL_DO(pclXfers(client, &xfers));
			if(xfers) // we aren't actually done yet
				break;

			sprintf(fname, "./%s.patchproject", proj->name);
			sprintf(update_fname, "./update/%s", fname);
			if(	!g_sync_verify_all_checkins &&
				!fileCompare(fname, update_fname))
			{
			}
			else
			{
				if(fileExists(fname))
				{
					char history_fname[MAX_PATH];
					sprintf(history_fname, "./history/%s.%i.patchproject", proj->name, getCurrentFileTime());
					makeDirectoriesForFile(history_fname);
					assert(patchRenameWithAlert(fname, history_fname) == 0);
					assert(fileGzip(history_fname));
				}
				assert(fileCopy(update_fname, fname) == 0);
				if(proj->serverdb) // was previously loaded. new loads are handled in patchserverdbLoad
					proj->reload_me = true;
			}
			patchmirroringSwitchStatef(UPDATE_PROJECT_VIEW, "");
		}

		xcase UPDATE_CONFIGS_RELOAD:
		{
			bool	hierarchy_changed = false;
			S32		xfers;

			PCL_DO(pclXfers(client, &xfers));
			if(xfers) // we aren't actually done yet
				break;

			// serverdbs could be loaded before getting .patchprojects instead, which would ensure that we get the
			// all of the patchprojects. updateListing() would have to change as well, since we don't know what
			// the projects are at that point. this is easier and should be effective, since we won't be serving
			// the unknown projects anyway.
			for(g_update_i = 0; g_update_i < eaSize(&g_patchserver_config.serverdbs); g_update_i++)
				if(g_patchserver_config.serverdbs[g_update_i]->load_me)
					hierarchy_changed |= patchserverdbLoad(g_patchserver_config.serverdbs[g_update_i], false, NULL, false, false, false, true);
			patchserverFixupDbHierarchy();

			for(g_update_i = 0; g_update_i < eaSize(&g_patchserver_config.projects); g_update_i++)
			{
				PatchProject *project = g_patchserver_config.projects[g_update_i];
				if(project->reload_me)
					patchprojectReload(project);
				if(hierarchy_changed)
					patchprojectHierarchyChanged(project);
			}

			patchmirroringSwitchStatef(UPDATE_MANIFEST_VIEW, "all done, start on the dbs.");
			g_update_i = -1;
			g_update_j = 0;
		}

		xcase UPDATE_MANIFEST_VIEW:
		{
			if (g_update_i >= 0 && g_patchserver_config.serverdbs[g_update_i]->db)
				stopDatabaseTimer(g_patchserver_config.serverdbs[g_update_i]);

			if(++g_update_i < eaSize(&g_patchserver_config.serverdbs)) // we have more dbs to update
			{
				const PatchServerDb *serverdb = g_patchserver_config.serverdbs[g_update_i];

				if(serverdb->db)
				{
					S32 found = 0;

					EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, i, isize);
						if(serverdb->projects[i]->useForFullManifest){
							found = 1;
							
							PCL_DO(pclClearCounts(client));
							PCL_DO(pclSetDefaultView(	client,
														serverdb->projects[i]->name,
														false,
														updateSetViewCallback,
														NULL));
														
							break;
						}
					EARRAY_FOREACH_END;
					
					if(!found){
						PCL_DO(pclClearCounts(client));
						PCL_DO(pclSetDefaultView(	client,
													serverdb->name,
													false,
													updateSetViewCallback,
													NULL));
					}
					
				

					startDatabaseTimer(serverdb);
								
					g_update_callback_waiting = true;
					patchmirroringSwitchStatef(UPDATE_MANIFEST_FILE, "Updating %s, manifest view set.", serverdb->name);
				}
			}
			else
			{
				char	startTimeBuffer[200];
				char	endTimeBuffer[200];
				time_t	endTime = time(NULL);

				patchserverMirrorNotifyDirty();
				patchserverNotifyMirrors();

				patchTrackingScanForUpdates(true, endTime - startTime);

				printf(	"Finished %s updating: start %s, end %s (%"FORM_LL"d seconds).\n",
					g_update_is_incremental ? "incremental" : "full",
					timeMakeLocalDateStringFromSecondsSince2000(startTimeBuffer, patchFileTimeToSS2000(startTime)),
					timeMakeLocalDateStringFromSecondsSince2000(endTimeBuffer, patchFileTimeToSS2000(endTime)),
					endTime - startTime);

				// Only verify all checkins once.
				if (g_sync_verify_all_checkins_sticky)
				{
					g_sync_verify_all_checkins = false;
					s_sync_ignore_manifest_similarity = false;
				}

				if(!g_update_is_incremental)
					g_update_last_full = endTime;
				g_update_is_incremental = false;
				patchmirroringSwitchStatef(UPDATE_NOTIFYME_START, "%s update completed.", g_update_is_incremental?"Incremental":"Full");
			}
		}

		xcase UPDATE_MANIFEST_FILE:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			const char*	db_name = serverdb->name;
			char		fname[MAX_PATH];
			char		update_fname[MAX_PATH];
			
			if(g_update_is_incremental)
			{
				assertServerDBSanity(serverdb);
				sprintf(fname, "%s.%d.incremental.manifest", db_name, serverdb->latest_rev+1);
			}
			else
				sprintf(fname, "%s.full.manifest", db_name);
			sprintf(update_fname, "./update/%s.manifest", db_name);
			PCL_DO(syncFileFromParent(client, fname, update_fname, NULL, 1, NULL));
			patchmirroringSwitchStatef(UPDATE_MANIFEST_LOAD_START, "Getting file \"%s\" into \"%s\".", fname, update_fname);
		}

		xcase UPDATE_MANIFEST_LOAD_START:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			S32				xfers;

			PCL_DO(pclXfers(client, &xfers));
			if(xfers) // we aren't actually done yet
				break;

			// spawn a thread to load the manifest
			patchmirroringSwitchStatef(UPDATE_MANIFEST_LOAD_WAIT, "Loading \"update/%s.manifest in a thread.", serverdb->name);
			LoadManifest(serverdb);
		}

		xcase UPDATE_MANIFEST_PRUNE_CHECKIN:
		{
			PatchServerDb *serverdb = g_patchserver_config.serverdbs[g_update_i];
			
			while(	timerElapsed(g_update_timer) < UPDATE_TIME_PER_FRAME &&
					g_update_j < eaSize(&serverdb->db->checkins))
			{
				// we have more checkins to check for pruning
				
				const Checkin *checkin = serverdb->db->checkins[g_update_j];
				while(	timerElapsed(g_update_timer) < UPDATE_TIME_PER_FRAME &&
						++g_update_k < eaSize(&checkin->versions))
				{
					// check for files in the new db and remove them if they don't exist there
					// TODO: climbing the patchdb direntry tree would be a lot more efficient
					FileVersion*	ver = checkin->versions[g_update_k];
					const DirEntry*	dir_entry = NULL;
					
					//if(!fileIsIncludedInAProject(	serverdb,
					//								ver->parent->path,
					//								NULL,
					//								NULL))
					//{
					//	continue;
					//}
					
					dir_entry = patchFindPath(	serverdb->db_new,
												ver->parent->path,
												false);
					if(dir_entry)
					{
						int i;
						for(i = 0; i < eaSize(&dir_entry->versions); i++)
						{
							const FileVersion *ver_new = dir_entry->versions[i];
							if(ver_new->rev == ver->rev)
							{
								if(	ver_new->checksum != ver->checksum ||
									ver_new->size != ver->size)
								{
									// Version doesn't match.
									dir_entry = NULL;
								}
								
								break;
							}
						}
						if(	dir_entry &&
							i >= eaSize(&dir_entry->versions))
						{
							// Didn't find a matching version.
							dir_entry = NULL;
						}
					}

					if(dir_entry)
					{
						// This is a dumb way to do this
						if(!mirrorConfigIncludesFileVersion(serverdb, ver))
						{
							dir_entry = NULL;
						}
					}

					if(!dir_entry)
					{
						++s_sync_deletes;
						patchserverdbRemoveVersion(serverdb, checkin, ver);
					}
				}
				if(g_update_k >= eaSize(&checkin->versions))
				{
					g_update_j++;
					g_update_k = -1;
				}
			}
			if(g_update_j >= eaSize(&serverdb->db->checkins))
			{
				g_update_j = -1;
				patchmirroringSwitchStatef(UPDATE_MANIFEST_CHECKIN_VIEW, "%s: Processing all checkins", serverdb->name);
				printf("%s: Processing all checkins.\n", serverdb->name);
			}
		}

		xcase UPDATE_MANIFEST_CHECKIN_VIEW:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			S32				xfers;
			int				max_rev;

			PCL_DO(pclXfers(client, &xfers));
			if(xfers) // we are still syncing files from the last checkin
				break;

			// Save previous checkin's processing time.
			if (checkinStartTime)
				stopCheckinTimer(serverdb, serverdb->db->checkins[g_update_j]);

			max_rev = serverdb->db_new->latest_rev==-1 ? eaSize(&serverdb->db_new->checkins)-1 : MIN(serverdb->db_new->latest_rev, eaSize(&serverdb->db_new->checkins)-1);
			if(++g_update_j <= max_rev) // we have more checkins to mirror
			{
				const Checkin*		checkin = serverdb->db_new->checkins[g_update_j];
				Checkin*			checkin_clone;
				S32					getCheckinFiles = 1;
				bool				new_checkin;
				bool				checkin_needs_update = true;

				if(	checkin->branch < serverdb->min_branch ||
					checkin->branch > serverdb->max_branch)
				{
					printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
								"Skipping checkin %d because branch %d is out of range (%d-%d).\n",
								checkin->rev,
								checkin->branch,
								serverdb->min_branch,
								serverdb->max_branch);
								
					getCheckinFiles = 0;
				}

				new_checkin = checkin->rev == eaSize(&serverdb->db->checkins);
				devassert(new_checkin || !g_update_is_incremental);
				patchupdateLogVerbose("UPDATE_MANIFEST_CHECKIN_VIEW: Updating %s checkin %d.", new_checkin?"new":"old", checkin->rev);
				assert(checkin->rev <= eaSize(&serverdb->db->checkins));
				if(checkin->rev == eaSize(&serverdb->db->checkins))
				{
					checkin_clone = StructAlloc(parse_Checkin);
					// This is safe, since the db's timeline won't allow access
					//   to this checkin's files.
					eaPush(&serverdb->db->checkins, checkin_clone);
				}
				else if (!StructCompare(parse_Checkin, checkin, serverdb->db->checkins[checkin->rev], 0, 0, 0))
				{
					checkin_needs_update = false;
					checkin_clone = serverdb->db->checkins[checkin->rev];
				}
				else
				{
					checkin_clone = serverdb->db->checkins[checkin->rev];
					/*while(eaSize(&checkin_clone->versions))
					{
						patchfileDestroy(&checkin_clone->versions[0]->patch);
						fileVersionRemoveAndDestroy(serverdb->db, checkin_clone->versions[0]);
					}*/
					StructDeInit(parse_Checkin, checkin_clone);
				}

				// Update the checkin.
				if (checkin_needs_update)
				{
					startCheckinTimer(serverdb, checkin_clone);
					StructCopyFields(parse_Checkin, checkin, checkin_clone, 0, 0);
	
					// Make sure to update the sandbox stash
					if(checkin_clone->sandbox && checkin_clone->sandbox[0])
					{
						if(!serverdb->db->sandbox_stash)
							serverdb->db->sandbox_stash = stashTableCreateWithStringKeys(96, StashDeepCopyKeys_NeverRelease);
						assert(stashAddPointer(serverdb->db->sandbox_stash, checkin_clone->sandbox, checkin_clone, true));
					}
				}

				// For incremental updates, add the new checkin to the journal
				if(g_update_is_incremental)
				{
					assert(g_update_incremental_journal);
					assert(checkin->rev > serverdb->latest_rev);
					g_update_incremental_checkin = journalAddCheckin(g_update_incremental_journal,
						checkin->author,
						checkin->sandbox,
						checkin->branch,
						checkin->time,
						checkin->incr_from,
						checkin->comment);
				}
				
				if(getCheckinFiles)
				{
					g_update_k = -1;
					patchmirroringSwitchStatef(UPDATE_MANIFEST_CHECKIN_FILE, "");
				}
			}
			else
			{
				patchmirroringSwitchStatef(UPDATE_MANIFEST_COPY_START, "All %d checkins copied.", eaSize(&serverdb->db_new->checkins));
			}
		}

		xcase UPDATE_MANIFEST_CHECKIN_FILE:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			const Checkin*	checkin = serverdb->db_new->checkins[g_update_j];
			patchserverdbCheckinFile(client, serverdb, checkin);
		}

		xcase UPDATE_MANIFEST_CHECKIN_DONE:
		{
			PatchServerDb*	serverdb = g_patchserver_config.serverdbs[g_update_i];
			const Checkin*	checkin = serverdb->db_new->checkins[g_update_j];
			S32				xfers;

			PCL_DO(pclXfers(client, &xfers));
			if(xfers) // we are still syncing files
				break;

			MAX1(serverdb->latest_branch, checkin->branch);
			MAX1(serverdb->latest_rev, checkin->rev);
			assertServerDBSanity(serverdb);
			patchmirroringSwitchStatef(UPDATE_MANIFEST_CHECKIN_VIEW, "");
		}

		xcase UPDATE_MANIFEST_COPY_START:
		{
			int i;
			PatchServerDb *serverdb = g_patchserver_config.serverdbs[g_update_i];

			// Copy over named views (existing views skipped automatically)
			for(i = 0; i < eaSize(&serverdb->db_new->namedviews); i++)
			{
				NamedView *view = serverdb->db_new->namedviews[i];
				patchAddNamedView(	serverdb->db,
									view->name,
									view->branch,
									view->sandbox,
									view->rev,
									view->comment,
									view->expires,
									NULL,
									0);
				if(g_update_is_incremental)
				{
					assert(g_update_incremental_journal);
					journalAddName(g_update_incremental_journal, view->name, view->sandbox, view->branch, view->rev, view->comment, view->expires);
				}
				
				// TODO: verify that existing views match the manifest?
			}

			// Verify that pruning high water marks aren't somehow higher here than on the parent, then copy them
			//for(i = 0; i < eaiSize(&serverdb->db->branch_valid_since); i++)
			//{
			//	if(serverdb->db->branch_valid_since[i] <= (int)eaiGet(&serverdb->db_new->branch_valid_since, i))
			//	{
			//		assertmsg(0, ");
			//	}
			//}
			eaiDestroy(&serverdb->db->branch_valid_since);
			serverdb->db->branch_valid_since = serverdb->db_new->branch_valid_since;
			serverdb->db_new->branch_valid_since = NULL;

			for(i=0; i<eaSize(&serverdb->incremental_manifest_patch); i++)
			{
				patchfileDestroy(&serverdb->incremental_manifest_patch[i]);
			}
			eaClear(&serverdb->incremental_manifest_patch);
			eaiClear(&serverdb->incremental_manifest_revs);

			// the main database is ready to go. spawn a thread to copy the manifest and destroy the temporary db
			patchmirroringSwitchStatef(UPDATE_MANIFEST_COPY_WAIT, "");
			CopyManifest(serverdb);
		}
	}

	// Check for progress.
	progress = progress
		|| initial_update_state != g_update_state
		|| initial_update_state_update_i != g_update_i
		|| initial_update_state_update_j != g_update_j
		|| initial_update_state_update_k != g_update_k;

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP_FUNC();

	return progress;
}

// Force the next update to be a full update.
void patchmirroringNextMirrorForceFull()
{
	g_update_last_full = 0;
}

// Manually start mirroring.
void patchmirroringRequestMirror()
{
	// Refuse to start a sync if we don't have a configured parent.
	if (!g_patchserver_config.parent.server)
	{
		printf("Error: No configured parent!\n");
		return;
	}

	// Refuse to start a sync if now isn't a good time to sync.
	if (!patchupdateIsConnectedToParent() || !patchmirroringIsMirroringIdle())
	{
		printf("Error: Not ready to start an update\n");
		return;
	}

	// Start the update cycle.
	g_update_notifyhalt = true;
	g_update_callback_waiting = false;
	patchupdateLog("Update manually requested");
	printf("Starting update...\n");
}

// Reset the mirroring status.
void patchmirroringResetConnection()
{
	g_update_state = UPDATE_NOTIFYME_START;
	g_update_callback_waiting = false;
	g_update_notifyhalt = false;
}

// Initialize updating.
void patchmirroringInit()
{
	g_update_timer = timerAlloc();
}

// Return false if mirroring is actually active, as opposed to just waiting for notification of changes.
bool patchmirroringIsMirroringIdle()
{
	return g_update_state == UPDATE_LISTING_START && g_update_callback_waiting;
}
