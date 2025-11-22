#include "gimme.h"
#include "gimmeUtil.h"
#include "gimmeBranch.h"
#include "gimmeUtilPurge.h"
#include <stdio.h>
#include "file.h"
#include "earray.h"
#include "utils.h"
#include "fileutil2.h"
#include "UTF8.h"

static int delete_count;

static int removeFileFromDB(GimmeDir *gimme_dir, GimmeNode *node, int quiet)
{
	char dbname[CRYPTIC_MAX_PATH];
	char *new_fname, old_fname[512];

	sprintf(dbname, "%s%s", gimme_dir->lock_dir, gimmeNodeGetFullPath(node));

	// copy file to "N:/revisions/purged/fname"
	new_fname = strstrInsert(dbname, "N:/revisions", "N:/revisions/purged" );
	strcpy(old_fname, dbname);
	makeDirectoriesForFile(new_fname);
	fileCopy(old_fname, new_fname);

	_chmod(dbname, _S_IREAD | _S_IWRITE);
	GIMME_CRITICAL_START;
	{
		char datebuff[128];
		if (!quiet) {
			gimmeLog(LOG_INFO, "Purgeing file %s, dated %s", dbname, printDate(node->checkintime, datebuff));
		}
		if (!gimme_state.simulate) {
			if (remove(dbname) != 0 && fileExists(dbname)) {
				gimmeLog(LOG_WARN_HEAVY, "Error deleting old version : %s", dbname);
				return 0;
			} else {
				gimmeJournalRm(gimme_dir->database, gimmeNodeGetFullPath(node));
			}
		}
	}
	GIMME_CRITICAL_END;
	// Also delete from database
	gimmeNodeDeleteFromTree(&gimme_dir->database->root->contents, &gimme_dir->database->root->contents_tail, node);
	delete_count++;
	return 1;
}


static int purgeByDateByNode(GimmeDir *gimme_dir, const char *relpath, int quiet)
{
	// Code copied and hacked up from deleteOldVersions()
	int highver, lowver, approvedver;
	char lockdir[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	int branch;

	// do not purge frozen files
	if ( getFreezeBranch(gimme_dir, relpath) != -1 )
		return NO_ERROR;

	sprintf(lockdir,"%s_versions/", relpath);

	for (branch=gimmeGetMinBranchNumber(); branch<=gimmeGetMaxBranchNumber(); branch++) {
		highver = getHighestVersion(gimme_dir, lockdir, &node, branch, relpath);
		approvedver = getApprovedRevision(gimme_dir, lockdir, &node, branch, relpath);

		// check for old revisions that need to be deleted
		while ( // Delete old copies while
			(lowver=getLowestVersion(gimme_dir, lockdir, &node, approvedver, branch, relpath)) != highver && // it is not the approved or latest
			lowver!=-1 && // The version exists,
			node->checkintime <= gimme_state.dateToGet) // and it's older than the date we want to delete
		{
			removeFileFromDB(gimme_dir, node, quiet);
		}
	}

	return NO_ERROR;
}

int gimmeUtilPurgeByDate(int gimme_dir_num, time_t date)
{
	int ret;
	char datebuff[128];
	char temp[512];
	// For each _versions
	// Find "latest" for each valid branch
	// For each file older than date and not in the set of "latest"
	//  delete and journal

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: purge by date called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: purge by date called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeSetBranchConfigRoot(eaGimmeDirs[gimme_dir_num]->lock_dir);

	sprintf(temp, "Are you sure you want to purge old files in \"%s\"?\nThis will probably delete huge amounts of data.", eaGimmeDirs[gimme_dir_num]->lock_dir);
	if ( !gimme_state.nowarn ) {
		if (IDNO==MessageBox_UTF8(NULL, temp, "Confirm Database Purge", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
			gimme_state.pause=0;
			return NO_ERROR;
		}
	}

	gimmeLog(LOG_STAGE, "Purgeing all files in %s, dated %s or older", eaGimmeDirs[gimme_dir_num]->lock_dir, printDate(date, datebuff));
	gimme_state.dateToGet = date;
	delete_count = 0;
	ret = doByFold(eaGimmeDirs[gimme_dir_num]->local_dir, 0, purgeByDateByNode, NULL, "Purgeing old files from database...", NULL, 0);
	gimmeLog(LOG_STAGE, "%d file(s) %spurged", delete_count, gimme_state.simulate?"(would be) ":"");
	return ret;
}

FileScanAction purgeFolderProcessor(char* dir, struct _finddata32_t* data, char *pUserData)
{
	char filename[MAX_PATH];
	int add = !!pUserData;
	if (!(data->attrib & _A_SUBDIR)) {
		int ret;
		sprintf(filename, "%s/%s", dir, data->name);
		gimmeLog(LOG_STAGE, "Purging %s...\n", filename);
		ret = gimmePurgeFile(filename, add);
		if (ret)
			gimmeLog(LOG_WARN_HEAVY, "  Failed\n");
	}
	return FSA_EXPLORE_DIRECTORY;
}

int gimmeUtilPurgeFolder(const char *folder_in, int add)
{
	char folder[MAX_PATH];
	strcpy(folder, folder_in);
	if (strEndsWith(folder, "data") || strEndsWith(folder, "data/")) {
		gimmeLog(LOG_FATAL, "Not pruning a folder ending in data/ for safety reasons."); // Don't accidentally destroy the world!
		return 1;
	}
	fileScanDirRecurseEx(folder, purgeFolderProcessor, (void*)(size_t)add);
	return 0;
}

static int branch_to_purge;
static int purgeByBranchByNode(GimmeDir *gimme_dir, const char *relpath, int quiet)
{
	// Code copied and hacked up from deleteOldVersions()
	int highver, lowver, approvedver;
	char lockdir[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	int branch = branch_to_purge;

	// do not purge frozen files
	if ( getFreezeBranch(gimme_dir, relpath) != -1 )
		return NO_ERROR;

	sprintf(lockdir,"%s_versions/", relpath);

	highver = getHighestVersion(gimme_dir, lockdir, &node, branch+1, relpath);
	if ( highver < 0 )
		return NO_ERROR;
	if (node && node->branch!=branch) {
		highver=-1;
	}
	approvedver = getApprovedRevision(gimme_dir, lockdir, &node, branch+1, relpath);
	if (node && node->branch!=branch) {
		approvedver = -1;
	}

	// check for old revisions that need to be deleted
	while ( // Delete old copies while
		(lowver=getLowestVersion(gimme_dir, lockdir, &node, approvedver, branch, relpath)) != highver && // it is not the approved or latest
		lowver!=-1) // The version exists,
	{
		removeFileFromDB(gimme_dir, node, quiet);
	}

	return NO_ERROR;
}

int gimmeUtilPurgeByBranch(int gimme_dir_num, int branchToRemove)
{
	int ret;
	char temp[512];
	// For each _versions
	// Find "latest" for the next branch up
	// For each file in the specified branch and not the "latest"
	//  delete and journal

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: purge by branch called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: purge by branch called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeSetBranchConfigRoot(eaGimmeDirs[gimme_dir_num]->lock_dir);
	if (branchToRemove == gimmeGetMaxBranchNumber()) {
		gimmeLog(LOG_FATAL, "Error: purge by branch called on the latest branch!");
		return GIMME_ERROR_NODIR;
	}

	sprintf(temp, "Are you sure you want to purge files in \"%s\", branch #%d?\nThis will probably delete huge amounts of data.", eaGimmeDirs[gimme_dir_num]->lock_dir, branchToRemove);
	if ( !gimme_state.nowarn ) {
		if (IDNO==MessageBox_UTF8(NULL, temp, "Confirm Database Purge", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
			gimme_state.pause=0;
			return NO_ERROR;
		}
	}

	gimmeLog(LOG_STAGE, "Purgeing all files in %s, in branch #%d", eaGimmeDirs[gimme_dir_num]->lock_dir, branchToRemove);
	branch_to_purge = branchToRemove;
	delete_count = 0;
	ret = doByFold(eaGimmeDirs[gimme_dir_num]->local_dir, 0, purgeByBranchByNode, NULL, "Purgeing old files from database...", NULL, 0);
	gimmeLog(LOG_STAGE, "%d file(s) %spurged", delete_count, gimme_state.simulate?"(would be) ":"");
	return ret;
}



static int pruneByNode(GimmeDir *gimme_dir, const char *relpath, int quiet)
{
	return deleteOldVersionsRecur(gimme_dir, relpath, gimmeGetMaxBranchNumber());
}

int gimmeUtilPrune(int gimme_dir_num)
{
	int ret;
	char temp[512];
	// For each _versions
	// Delete old versions if there are more old than specified in the rules.txt
	//  delete and journal

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: purge by branch called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: purge by branch called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeSetBranchConfigRoot(eaGimmeDirs[gimme_dir_num]->lock_dir);

	sprintf(temp, "Are you sure you want to prune files in \"%s\"?\nThis will probably delete huge amounts of data.", eaGimmeDirs[gimme_dir_num]->lock_dir);
	if (IDNO==MessageBox_UTF8(NULL, temp, "Confirm Database Prune", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
		gimme_state.pause=0;
		return NO_ERROR;
	}

	gimmeLog(LOG_STAGE, "Pruning all extra files in %s", eaGimmeDirs[gimme_dir_num]->lock_dir);
	gimme_state.num_pruned_versions = 0;
	ret = doByFold(eaGimmeDirs[gimme_dir_num]->local_dir, 0, pruneByNode, NULL, "Pruning extra files from database...", NULL, 0);
	gimmeLog(LOG_STAGE, "%d file(s) %spruned", gimme_state.num_pruned_versions, gimme_state.simulate?"(would be) ":"");
	return ret;
}