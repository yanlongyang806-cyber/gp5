#include "gimmeDatabase.h"
#include "gimme.h"
#include "file.h"
#include "fileutil2.h"
#include "utils.h"
#include "stdtypes.h"
#include <string.h>

#include "genericlist.h"
#include "timing.h"
#include <fcntl.h>
#include <stdlib.h>
#include <WinCon.h>
#include <time.h>
#include "strings_opt.h"
#include "mathutil.h"
#include "../../3rdparty/zlib/zlib.h"
#include "MemoryPool.h"
#include "earray.h"
#include <share.h>
#include "utilitiesLib.h"
#include "UTF8.h"

void doUnlock(GimmeDatabase *db);


enum {
	LOC_TIME,
	LOC_FNAME,
	LOC_LOCK,
	LOC_SIZE,
	LOC_CHECKINTIME,
};
static char *keys[5] = {"TIMESTAMP", "FILENAME", "LOCK", "SIZE", "CHECKINTIME"};

static int bytes_per_update=65536; // Used to determine how often output is displayed to the console
int g_force_flush = 0;
int g_force_zip = 0;
int g_ignore_locks = 0; // Dangerous, don't use this unless gimme is manually locked
int gimmeDatabaseInCritical=0;
int gimmeDatabaseRequestClose=0;
static int show_steps = 0;

static BOOL CtrlHandler(DWORD fdwCtrlType) 
{ 
	int i;
	switch (fdwCtrlType) 
	{ 
	case CTRL_CLOSE_EVENT: 
	case CTRL_LOGOFF_EVENT: 
	case CTRL_SHUTDOWN_EVENT: 
	case CTRL_BREAK_EVENT: 
	case CTRL_C_EVENT: 
		if (gimmeDatabaseInCritical) {
			gimmeDatabaseRequestClose = true;
			gimmeLog(LOG_FATAL, "Exit request received, will exit after operation finishes...");
			return TRUE;
		}
		for (i=0; i<eaSize(&eaGimmeDirs); i++) {
			if (eaGimmeDirs[i]->database && eaGimmeDirs[i]->database->locked) {
				gimmeLog(LOG_FATAL, "Warning: forceful exit detected while database %s is still locked! Unlocking...", eaGimmeDirs[i]->local_dir);
				doUnlock(eaGimmeDirs[i]->database);
			}
		}
		return FALSE; 
		// Pass other signals to the next handler. 
	default: 
		return FALSE; 
	} 
} 

static void initCtrlHandler(void) {
	static int inited=false;
	if (!inited) {
		BOOL fSuccess;		
		
		fSuccess = SetConsoleCtrlHandler( 
			(PHANDLER_ROUTINE) CtrlHandler,  // handler function 
			TRUE);  	// add to list 
		inited=true;
		if (!fSuccess) {
			gimmeLog(LOG_WARN_HEAVY, "Error initing Control Hanlder!  Do NOT forcefully close this program, database inconsitencies may result.");
		}
	}
}


GimmeDatabase *gimmeDatabaseNew()
{
	GimmeDatabase *ret = calloc(sizeof(GimmeDatabase), 1);
	ret->root=gimmeNodeNew();
	ret->root->is_dir=1;
	ret->root->name=strdup("ROOT");
	ret->loc[0]=-1;
	ret->loc[1]=-1;
	ret->loc[2]=-1;
	ret->loc[3]=-1;
	ret->loc[4]=-1;
	return ret;
}

static void doUnlock(GimmeDatabase *db) {
	if (!db) return;
	if (!db->locked) return;
	if (db->writing) {
		gimmeLog(LOG_FATAL, "ERROR!  TRYING TO UNLOCK WHILE STILL WRITING!  DO NOT DELETE LOCKFILE!");
		return;
	}
	if (!g_ignore_locks) {
		if (db->lockfile_handle && 0==_close(db->lockfile_handle)) { // we had it open
			db->lockfile_handle=0;
		} else {
			// no handle, or error closing the file, just delete the lockfile
		}
		if (db->lockfile_name!=NULL) {
			gimmeEnsureDeleteLock(db->lockfile_name);
			free(db->lockfile_name);
			db->lockfile_name=0;
		}
	}
	db->locked = 0;
}

static void doUnlockJ(GimmeDatabase *db) {
	if (!db) return;
	if (!db->jlocked) return;
	if (db->jwriting) {
		gimmeLog(LOG_FATAL, "ERROR!  TRYING TO UNLOCK JORNAL WHILE STILL WRITING!  DO NOT DELETE LOCKFILE!");
		return;
	}
	if (!g_ignore_locks) {
		if (db->jlockfile_handle && 0==_close(db->jlockfile_handle)) { // we had it open
			db->jlockfile_handle=0;
		} else {
			// no handle, or error closing the file, just delete the lockfile
		}
		if (db->jlockfile_name!=NULL) {
			gimmeEnsureDeleteLock(db->jlockfile_name);
		}
	}
	db->jlocked = 0;
}

void gimmeDatabaseDelete(GimmeDatabase *db) {
//Pre-Journaling:	assert(!db->needs_flushing && "Attempt to unlock database when there is still data to be written.\nJournal files might be in a state of havoc.");
	if ( !db )
		return;
	assert(!db->locked);
	doUnlock(db);
	if (db->root)
		gimmeNodeDelete(db->root);
	SAFE_FREE(db->filename);
	SAFE_FREE(db->jlockfile_name);
	SAFE_FREE(db->journal_txt);
	SAFE_FREE(db->lockfile_name);
	SAFE_FREE(db);
}

void gimmeDatabaseUnlock(GimmeDatabase *db) {
//Pre-Journaling:	assert(!db->needs_flushing && "Attempt to unlock database when there is still data to be written.\nJournal files might be in a state of havoc.");
	doUnlock(db);
}

void gimmeWriteLockData(int lockfile_handle) {
	assert(!g_ignore_locks);
	_write(lockfile_handle, gimmeGetUserName(), (unsigned int)strlen(gimmeGetUserName()));
	_write(lockfile_handle, "\r\n", (unsigned int)strlen("\r\n"));
	_write(lockfile_handle, getLocalHostNameAndIPs(), (unsigned int)strlen(getLocalHostNameAndIPs()));
	if (gimme_state.command_line) {
		_write(lockfile_handle, "\r\n", (unsigned int)strlen("\r\n"));
		_write(lockfile_handle, gimme_state.command_line, (unsigned int)strlen(gimme_state.command_line));
	}
	_write(lockfile_handle, "\r\n", (unsigned int)strlen("\r\n"));
	_write(lockfile_handle, gimmeDatabaseGetVersionString(), (unsigned int)strlen(gimmeDatabaseGetVersionString()));
}

int estimated_write_file_size=-1;
int write_progress_total=0;

void updateWritePercentageBar(int num_written)
{
	static int sum;
	static int lastupdate=0;

	if (num_written<0) { // Reset total
		estimated_write_file_size = write_progress_total = -num_written;
		sum = 0;
		lastupdate = 0;
	} else {
		sum+= num_written;
		if (sum - lastupdate > bytes_per_update ||
			sum == write_progress_total)
		{
			if (sum>write_progress_total) {
				sum = (int)(write_progress_total*0.99f);
			}
			if (!gimme_state.quiet)
				printPercentage(sum, write_progress_total);
			lastupdate=sum;
		}
	}
}


static FILE *outfile;
static int *outmap;
static int max;
static int writeGimmeNode(GimmeNode *node) {
	int i;
	int num_written=0;
	char buf[1024];
	int num;
#define WRITE_IT()					\
	num = (int)strlen(buf);			\
	fwrite(buf, 1, num, outfile);	\
	num_written+=num;

	if (node->is_dir) return 1;

	for (i=0; i<max; i++) {
		switch (outmap[i]) {
			case LOC_TIME:
				STR_COMBINE_BEGIN(buf);
					STR_COMBINE_CAT_D(node->timestamp);
					STR_COMBINE_CAT_C('\t');
				STR_COMBINE_END(buf);
				WRITE_IT();
			xcase LOC_FNAME:
				//num_written+=fprintf(outfile, "\"%s\"\t", gimmeNodeGetFullPath(node));
				STR_COMBINE_BEGIN(buf);
					STR_COMBINE_CAT_C('\"');
					STR_COMBINE_CAT(gimmeNodeGetFullPath(node));
					STR_COMBINE_CAT_C('\"');
					STR_COMBINE_CAT_C('\t');
				STR_COMBINE_END(buf);
				WRITE_IT();
			xcase LOC_LOCK:
				//num_written+=fprintf(outfile, "\"%s\"\t", node->lock?node->lock:"");
				STR_COMBINE_BEGIN(buf);
					STR_COMBINE_CAT_C('\"');
					STR_COMBINE_CAT(node->lock?node->lock:"");
					STR_COMBINE_CAT_C('\"');
					STR_COMBINE_CAT_C('\t');
				STR_COMBINE_END(buf);
				WRITE_IT();
			xcase LOC_SIZE:
				//num_written+=fprintf(outfile, "%d\t", node->size);
				STR_COMBINE_BEGIN(buf);
					STR_COMBINE_CAT_D((int)node->size);
					STR_COMBINE_CAT_C('\t');
				STR_COMBINE_END(buf);
				WRITE_IT();
			xcase LOC_CHECKINTIME:
				//num_written+=fprintf(outfile, "%d\t", node->checkintime);
				STR_COMBINE_BEGIN(buf);
					STR_COMBINE_CAT_D(node->checkintime);
					STR_COMBINE_CAT_C('\t');
				STR_COMBINE_END(buf);
				WRITE_IT();
				break;
			default:
				num_written+=fprintf(outfile, "-1\t");
		}
	}
	num_written+=1;
	fwrite("\n", 1, 1, outfile);
	updateWritePercentageBar(num_written);
	return 1;
}

static int skew=0;

static void writeData(char *data, int total_size, FILE *outfile)
{
	int i;
	for (i=0; i<total_size;) {
		int size = MIN(64*1024, total_size - i);
		fwrite(data + i, 1, size, outfile);
		i+=size;
		printPercentage(i+skew, total_size+skew);
	}
}

int gimmeDatabaseSave(GimmeDatabase *db)
{
	int i;
	int timer = timerAlloc();
	char tempfname[CRYPTIC_MAX_PATH];
	StuffBuff sb;
	F32 parseStart=0;
	int save_compressed=db->zipped || g_force_zip;

	assert(!g_ignore_locks); // Don't write the database if it's not really locked!

	if (!db->locked) {
		gimmeLog(LOG_FATAL, "ERROR!  Trying to save while database is not locked!  Changes probably lost!");
		timerFree(timer);
		return 1;
	}

	if (!gimme_state.quiet) {
		if (show_steps) {
			gimmeLog(LOG_TOSCREENONLY, "[database] Saving file list database [Step 1/2]...");
		} else {
			gimmeLog(LOG_TOSCREENONLY, "[database] Saving file list database...");
		}
	}
	timerStart(timer);

	// 1. Write to memory
	max=0;

	initStuffBuff(&sb, (int)(estimated_write_file_size * 1.1f));
	outfile = fileOpenStuffBuff(&sb);

	if (!show_steps) {
		updateWritePercentageBar(-estimated_write_file_size*10);
	}

	// Write header
	for (i=0; i<ARRAY_SIZE(db->loc); i++) {
		if (db->loc[i] > max) 
			max = db->loc[i];
	}
	max++;
	outmap = malloc(sizeof(int)*max);
	for (i=0; i<max; i++) {
		outmap[i]=-1;
	}
	for (i=0; i<ARRAY_SIZE(db->loc); i++) {
		if (db->loc[i]!=-1) {
			outmap[db->loc[i]] = i;
		}
	}
	// By this point, outmap contains, in order, what fields should be outputted in what order
	for (i=0; i<max; i++) {
		if (outmap[i]==-1) {
			fprintf(outfile, "\"UNKNOWNFIELD\" ");
		} else {
			fprintf(outfile, "\"%s\" ", keys[outmap[i]]);
		}
	}
	fprintf(outfile, "\n");

	// Recurse the tree and write all nodes
	gimmeNodeRecurse(db->root->contents, writeGimmeNode);

	free(outmap);
	fclose(outfile);
	sb.idx--; // Remove the null terminator
	outfile = NULL;

	if (show_steps) {
		// Flush to 100%
		updateWritePercentageBar(-1);
		updateWritePercentageBar(1);
		skew = 0;
	} else {
		skew = (int)(0.10 * sb.idx);
	}

	if (!gimme_state.quiet && show_steps) {
		gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs", timerElapsed(timer));
		gimmeLog(LOG_TOSCREENONLY, ".     \n");
		parseStart = timerElapsed(timer);
		gimmeLog(LOG_TOSCREENONLY, "[database] Saving file list database [Step 2/2]...");
	}

	// 2. Write memory to disk
	db->writing = 1;

	// Back up old file
	fileRenameToBak(db->filename);
	// Add .gz if it's not already there!
	if (save_compressed && !strEndsWith(db->filename, ".gz")) {
		size_t len = strlen(db->filename) + 4;
		char *newfname = malloc(len);
		strcpy_s(newfname, len, db->filename);
		strcat_s(newfname, len, ".gz");
		free(db->filename);
		db->filename = newfname;
	}
	sprintf_s(SAFESTR(tempfname), "%s.tmp", db->filename);
	if (save_compressed) {
		outfile = fopen(tempfname, "wbz");
	} else {
		outfile = fopen(tempfname, "w");
	}
	setvbuf(outfile, NULL, _IOFBF, 65536);
	writeData(sb.buff, sb.idx, outfile);
	fclose(outfile);

	// not needed? remove(db->filename);
	rename(tempfname, db->filename);

	// Done writing the database.  If we had a journal.processing, we can get rid of it now, it's been processed
	if (db->jprocessing_name) {
		fileRenameToBak(db->jprocessing_name);
		free(db->jprocessing_name);
		db->jprocessing_name=NULL;
	}

	db->writing = 0;

	freeStuffBuff(&sb);

	if (!gimme_state.quiet)
		gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs.     \n", timerElapsed(timer) - parseStart);
	gimmeLog(LOG_TOFILEONLY, "Database save in %1.3fs.", timerElapsed(timer));
	timerFree(timer);

	//Pre-Journaling: db->needs_flushing = false;
	return 0;
}




int gimmeDatabaseLoadJournal(GimmeDatabase *db, char *journal_name)
{
	int count;
	char *data;
	char *s;
	char *args[100];

	data = fileAlloc(journal_name, &count);
	if (data==NULL || count==0) {
		gimmeLog(LOG_FATAL, "[***database] Error opening journal file '%s'", journal_name);
		return -1;
	}
	s = data;
	if (data[0] == 0) {
		// Sometimes a null gets put at the beginning of this file... perhaps fileTouch messed up...
		data[0] = ' ';
	}

	// Read entries and add to database
	while(s)
	{
		char *fn;
		__time32_t t = 0, ct;
		char *lock=NULL;
		char *user;
		char *op;
		size_t size = 0;

		count=tokenize_line(s,args,&s);

		if (count == 0 || args[0][0]=='#' || (args[0][0]=='/' && args[0][1]=='/'))
			continue;
		ct = atoi(args[0]);
		user = args[1];
		op = args[2];
		fn = args[3];
		if (strcmp(op, "ADD1")==0) {
			size = atoi(args[4]);
			t = atoi(args[5]);
			lock = args[6];
			if (strlen(lock)==0) lock=NULL;
		}
		if (fn==NULL) {
			gimmeLog(LOG_FATAL, "[***database] Fatal Error!  Journal file possibly corrupted!  Null fields found (or bad data), notify a program that journal.processing is bad.");
		}
		if (strcmp(op, "ADD1")==0) {
            gimmeNodeAdd(&db->root->contents, &db->root->contents_tail, NULL, fn, lock, t, size, ct);
		} else if (strcmp(op, "RM1")==0) {
			GimmeNode *node = gimmeNodeFind(db->root->contents, fn);
			if (node==NULL) {
				gimmeLog(LOG_TOFILEONLY, "\"%s\"\n[database] Error: RM command found in JOURNAL.TXT referencing a non-existent node", fn);
			} else {
				gimmeNodeDeleteFromTree(&db->root->contents, &db->root->contents_tail, node);
			}
		}
	}
	free(data);
	return 0;
}

static GimmeDatabase *cur_db=NULL;
static const char *cur_dbroot=NULL;
static int load_count;
static int load_folder_count;
static int load_status;

#define MAX_FOLDERS_TO_PARSE	120

static FileScanAction loadProcesseor(FileScanContext* context) {
	char fullpath[CRYPTIC_MAX_PATH*2];
	char *relpath;

	if ((gimme_state.db_mode!=GIMME_NO_DB) && load_folder_count>MAX_FOLDERS_TO_PARSE) {
		load_status = -1;
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	if (context->fileInfo->attrib & _A_SUBDIR) {
		load_folder_count++;
		return FSA_EXPLORE_DIRECTORY;
	}

	// Find relpath
	sprintf_s(SAFESTR(fullpath), "%s/%s", context->dir, context->fileInfo->name);
	if (strnicmp(fullpath, cur_dbroot, strlen(cur_dbroot))==0) {
		relpath = fullpath+strlen(cur_dbroot);
		while (*relpath=='/' || *relpath=='\\') relpath++;
	} else {
		assert(!"DBRoot and files being scanned don't match");
		return FSA_STOP;
	}
	if (strchr(relpath, '/')==0 && strchr(relpath, '\\')==0) {
		// This is a file in the root
		if (!strEndsWith(relpath, ".lock")) {
			// Anything not a .lock file and not a folder in the root should be excluded
			return FSA_EXPLORE_DIRECTORY;
		}
	}

	{
		char *fn;
		__time32_t t, ct;
		char *lock;
		char lockee[1024];
		int size;

		fn = relpath;
		if (strEndsWith(relpath, ".lock")) {
			// read username from lockfile
			gimmeGetUserNameFromLock(SAFESTR(lockee), fullpath);
			lock = lockee;
		} else if (strEndsWith(relpath, ".comments")) {
			return FSA_EXPLORE_DIRECTORY;
		} else if (strEndsWith(relpath, ".batchinfo")) {
			return FSA_EXPLORE_DIRECTORY;
		} else if (strEndsWith(relpath, ".databaselock")) {
			return FSA_EXPLORE_DIRECTORY;
		} else {
			lock = NULL;
		}
		assert(lock==NULL || !(strlen(lock)==0));
		//TODO: set checked in time ---> ct = context->fileInfo->time_create;
// 		if (gimmeIsSambaDrive(fullpath)) {
// 			ct = t = context->fileInfo->time_write; // + gimmeGetTimeAdjust(); // do not use on samba
// 		} else {
// 			ct = t = context->fileInfo->time_write + gimmeGetTimeAdjust(); // do not use on samba
// 		}
		ct = t = context->fileInfo->time_write; // Looks like Linux samba now behave like Windows servers?  Treat them the same, but our timestamps on disk are possibly originally off.

		size = context->fileInfo->size;
		gimmeNodeAdd(&cur_db->root->contents, &cur_db->root->contents_tail, NULL, fn, lock, t, size, ct);
		load_count++;
	}
	return FSA_EXPLORE_DIRECTORY;
}

int gimmeDatabaseLoadBranchFromFS(GimmeDatabase *db, const char *dbroot, const char *relpath)
{
	FileScanContext context;
	char path[CRYPTIC_MAX_PATH];
	int timer = timerAlloc();

	timerStart(timer);

	load_count = 0;
	load_folder_count = 0;
	load_status = 0;
	cur_db = db;
	cur_dbroot = dbroot;
	context.processor = loadProcesseor;

	while (*relpath=='/' || *relpath=='\\') relpath++;
	strcpy(path, dbroot);
	if (!strEndsWith(dbroot, "/")) {
		strcat(path, "/");
	}
	strcat(path, relpath);

	fileScanDirRecurseContext(path, &context);
	if (load_status==-1) {
		gimmeLog(LOG_TOFILEONLY, "ABORTED after %d files in %d folders under %s in %1.4gs", load_count, load_folder_count, path, timerElapsed(timer));
		timerFree(timer);
		return 1;
	}
	gimmeLog(LOG_TOFILEONLY, "Found %d files in %d folders under %s in %1.4gs", load_count, load_folder_count, path, timerElapsed(timer));
	timerFree(timer);
	return NO_ERROR;
}

int printPercentageDoubleTotal(int bytes_processed, int total)
{
	if (!gimme_state.quiet && total) {
		int numprinted=0;
		total*=2;
		numprinted+=printf("%5.1f%% ", 100.0f*bytes_processed / (float)total);
		numprinted+=printPercentageBar(10*bytes_processed / total, 10);
		backSpace(numprinted, total==bytes_processed);
	}
	return 1;
}


static int lineNumber;

// Lock database.txt
// Load database.txt
// Load journal file(s) if available
//		Look for journal.proc (result of previously pre-maturely exited program) and
//				 journal.txt
//		1. Lock journal.txt
//		2. If exist journal.proc, append journal.txt to journal.proc, else copy journal.txt to journal.proc
//		3. remove journal.txt
//		4. Unlock journal.txt
//		5. merge journal.proc with database in memory
// Then save DB, delete journal.proc, unlock DB
int gimmeDatabaseLoad(GimmeDatabase *db, const char *dbroot, const char *relpath, int single_file)
{
	int count, count2, filesize;
	char *data;
	char *s;
	char *args[100];
	int i, j;
	char temp[CRYPTIC_MAX_PATH];
	char tempfilename[CRYPTIC_MAX_PATH];
	char buf[CRYPTIC_MAX_PATH];
	char journal_txt_processing[CRYPTIC_MAX_PATH];
	char history_txt[CRYPTIC_MAX_PATH];
	int timer = timerAlloc();
	F32	waitTime=0;
	F32 parseStart=0;
	int lockfile_handle;
	int ret;
	int lastupdate;
	bool need_to_save;

	assert(ARRAY_SIZE(db->loc) == ARRAY_SIZE(keys));

	initCtrlHandler();

	lineNumber = 0;

	// Set up needed paths
	sprintf_s(SAFESTR(tempfilename), "%sdatabase.txt.databaselock", dbroot);
	db->lockfile_name = strdup(tempfilename);
	sprintf_s(SAFESTR(tempfilename), "%sjournal.txt.databaselock", dbroot);
	db->jlockfile_name = strdup(tempfilename);
	sprintf_s(SAFESTR(tempfilename), "%sjournal.txt", dbroot);
	db->journal_txt = strdup(tempfilename);


	// Determine whether to load the whole thing or a subset
	if (single_file && (gimme_state.db_mode!=GIMME_FORCE_DB)) {
		char temp[CRYPTIC_MAX_PATH];
		GimmeDBMode ggdbm_save = gimme_state.db_mode;

		gimme_state.db_mode = GIMME_NO_DB; // do not load the DB on a single file!

		gimmeLog(LOG_TOFILEONLY, "Loading from file system...");
		// make revisions folder name
		sprintf_s(SAFESTR(temp ),"%s_versions/", relpath);
		gimmeDatabaseLoadBranchFromFS(db, dbroot, temp);
		// look for lockfile(s)
		sprintf_s(SAFESTR(temp), "%s*.lock", relpath);
		ret = gimmeDatabaseLoadBranchFromFS(db, dbroot, temp);
		// look for message store file(s)
		sprintf_s(SAFESTR(temp), "%s.ms_versions", relpath);
		ret = gimmeDatabaseLoadBranchFromFS(db, dbroot, temp);
		sprintf_s(SAFESTR(temp), "%s.ms*.lock", relpath);
		ret = gimmeDatabaseLoadBranchFromFS(db, dbroot, temp);

		db->loaded_from_fs = true;
		gimme_state.db_mode = ggdbm_save;
		if (ret==0) {
			timerFree(timer);
			return ret;
		}
	} else {
		// folder and all subtrees needed
		// decide whether or not to load everything or just the folder
		bool scan_folder=true;
		if (relpath==NULL)
			scan_folder=false;
		else if (strlen(relpath)==0)
			scan_folder=false;
		else if (strcmp(relpath, "/")==0)
			scan_folder=false;
		else if (strcmp(relpath, "\\")==0)
			scan_folder=false;
		if (gimme_state.db_mode==GIMME_NO_DB) // override
			scan_folder=true;
		if (gimme_state.db_mode==GIMME_FORCE_DB)
			scan_folder=false;
		if (scan_folder) {
			gimmeLog(LOG_TOFILEONLY, "Loading from file system...");
			ret = gimmeDatabaseLoadBranchFromFS(db, dbroot, relpath);
			db->loaded_from_fs = true;
			if (ret==0) {
				timerFree(timer);
                return ret;
			}
			// If it fails, assume it was from scanning too many files
			// fall through to normal load
		}
	}
	gimmeLog(LOG_TOFILEONLY, "Loading full database...");
	db->loaded_from_fs = false;

	if (db->root->contents!=NULL) { // stale data sitting around, get rid of it, we're loading the whole thing anyway!
		gimmeNodeDelete(db->root);
		db->root = gimmeNodeNew();
	}

	// Check for required version of gimme database
	{
		int myrev;
		int dbrev;
		char verfilename[CRYPTIC_MAX_PATH];
		int count;
		char *mem;
		sscanf(strchr(gimmeDatabaseGetVersionString(), ':')+2, "%d", &myrev);

		sprintf_s(SAFESTR(verfilename), "%sdatabase.txt.ver", dbroot);
		mem = fileAlloc(verfilename, &count);
		if (mem==NULL || count==0 || strlen(mem)==0) {
			dbrev=0;
		} else {
			sscanf(mem, "%d", &dbrev);
		}
		if (mem!=NULL) {
			free(mem);
			mem=NULL;
		}
		if (myrev<dbrev) {
			gimmeLog(LOG_FATAL, "Your GIMME.EXE version (%d) is too old to access the database (%d),\n   get the latest version of C:\\NIGHT\\TOOLS\\BIN\\GIMME.EXE and C:\\NIGHT\\TOOLS\\BIN\\GIMMEDLL.DLL first!\n", myrev, dbrev);
			timerFree(timer);
			return GIMME_ERROR_DB;
		}
	}

	if (!gimme_state.quiet) {
		if (show_steps) {
			gimmeLog(LOG_TOSCREENONLY, "[database] Loading file list database [Step 1/3]...");
		} else {
			gimmeLog(LOG_TOSCREENONLY, "[database] Loading file list database...");
		}
	}

	timerStart(timer);

	// Attempt to lock the database file
	lockfile_handle = gimmeWaitToAcquireLock(db->lockfile_name);
	db->lockfile_handle = lockfile_handle;
	db->locked = 1;
	waitTime = timerElapsed(timer);

	sprintf_s(SAFESTR(temp), "%sdatabase.txt", dbroot);
	data = fileAllocEx(temp, &count, show_steps?printPercentage:printPercentageDoubleTotal);
	if (!data) {
		sprintf_s(SAFESTR(temp), "%sdatabase.txt.gz", dbroot);
		data = fileAllocEx_dbg(temp, &count, "rbz", show_steps?printPercentage:printPercentageDoubleTotal MEM_DBG_PARMS_INIT);
		if (data)
			db->zipped = true;
	}
	filesize = count;
	updateWritePercentageBar(-filesize);

	if (!gimme_state.quiet && show_steps) {
		if (waitTime>1.0) {
			gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs", timerElapsed(timer)-waitTime);
			gimmeLog(LOG_TOSCREENONLY, " (+%1.3fs wait)", waitTime);
		} else {
			gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs", timerElapsed(timer));
		}
		gimmeLog(LOG_TOSCREENONLY, ".     \n");

		gimmeLog(LOG_TOSCREENONLY, "[database] Loading file list database [Step 2/3]...");
	}

	parseStart = timerElapsed(timer);
	if (data==NULL || count==0) {
		gimmeLog(LOG_FATAL, "[***database] Error opening database file '%sdatabase.txt[.gz]'", dbroot);
		doUnlock(db);
		return 1;
	}
	db->filename = strdup(temp);
	s = data;
	// Get locations
	count2=tokenize_line(s,args,&s);
	for (i=0; i<count2; i++) {
		for (j=0; j<ARRAY_SIZE(db->loc); j++) {
			if (stricmp(keys[j], args[i])==0) {
				db->loc[j]=i;
			}
		}
	}
	if (db->loc[LOC_CHECKINTIME]==-1) {
		// duplicate file timestamp over to checkin time if checkin time doesn't exist
		db->loc[LOC_CHECKINTIME]=db->loc[LOC_TIME];
	}
	if (db->loc[0]==-1 || db->loc[1]==-1 || db->loc[2]==-1 || db->loc[3]==-1 || db->loc[4]==-1) {
		gimmeLog(LOG_FATAL, "[***database] Fatal Error!  Source control database possibly corrupted!  One or more expected keys not defined.  Please notify programmers immediately.");
		doUnlock(db);
		timerFree(timer);
		return -1;
	}

	// Read entries and add to database
	lastupdate=0;
	while(s)
	{
		char *fn;
		__time32_t t, ct;
		char *lock;
		int size;

		count=tokenize_line(s,args,&s);
		lineNumber++;

		if (count == 0 || args[0][0]=='#' || (args[0][0]=='/' && args[0][1]=='/'))
			continue;
		fn = args[db->loc[LOC_FNAME]];
		lock = args[db->loc[LOC_LOCK]];
		if (strlen(lock)==0) lock=NULL;
		t = atoi(args[db->loc[LOC_TIME]]);
		ct = atoi(args[db->loc[LOC_CHECKINTIME]]);
		size = atoi(args[db->loc[LOC_SIZE]]);
		if (fn==NULL) {
			gimmeLog(LOG_FATAL, "[***database] Fatal Error!  Source control database possibly corrupted!  Null fields found (or bad data), notify a program to overwrite database.txt with database.bak.");
		}
		gimmeNodeAdd(&db->root->contents, &db->root->contents_tail, NULL, fn, lock, t, size, ct);
		{
			int bytes_processed = (int)(s - data);
			if (bytes_processed - lastupdate > bytes_per_update ||
				bytes_processed == filesize)
			{
				if (show_steps) {
					printPercentage(bytes_processed, filesize);
				} else {
					printPercentageDoubleTotal(bytes_processed + filesize, filesize);
				}
				lastupdate=bytes_processed;
			}
		}
	}

	if (db->loc[LOC_CHECKINTIME]==db->loc[LOC_TIME]) {
		// when writing later, add checkin time to the end
		db->loc[LOC_CHECKINTIME]=count2; 
	}

	free(data);

	if (!gimme_state.quiet && show_steps) {
		gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs", timerElapsed(timer)-parseStart);
		gimmeLog(LOG_TOSCREENONLY, ".     \n");
	}

	parseStart = timerElapsed(timer);
	if (!gimme_state.quiet && show_steps)
		gimmeLog(LOG_TOSCREENONLY, "[database] Loading file list database [Step 3/3]...");

	// Load journal file(s) if available
	//		Look for journal.proc (result of previously pre-maturely exited program) and
	//				 journal.txt
	//		1. Lock journal.txt
	//		2. If exist journal.proc, append journal.txt to journal.proc, else copy journal.txt to journal.proc
	//		3. remove journal.txt
	//		4. Unlock journal.txt
	//		5. merge journal.proc with database in memory
	////////////////////
	//		1. Lock journal.txt
	db->jlockfile_handle = gimmeWaitToAcquireLock(db->jlockfile_name);
	GIMME_CRITICAL_START; // Do not exit during this process!
	db->jlocked = 1;
	{
		//		2. If exist journal.proc, append journal.txt to journal.proc, else copy journal.txt to journal.proc
		//		3. remove journal.txt
		char journal_txt_processing2[CRYPTIC_MAX_PATH];

		sprintf_s(SAFESTR(journal_txt_processing), "%sjournal.txt.processing", dbroot);
		if (fileExists(journal_txt_processing)) { // Recovery in case of gimme being closed prematurely previously
			bool error=0;
			sprintf_s(SAFESTR(journal_txt_processing2), "%sjournal.txt.processing_temp", dbroot);
			if (fileExists(journal_txt_processing2)) {
				gimmeLog(LOG_FATAL, "\nDatabase journal is in inconsistent state\nManual fix might be needed.  Contact programmers!");
				error=true;
			} else if (fileExists(db->journal_txt)) { // don't need to append things if no journal.txt exists

				// append journal.txt to journal.txt.processing and call it journal.processing2
				backSlashes(db->journal_txt);
				backSlashes(journal_txt_processing);
				backSlashes(journal_txt_processing2);
				sprintf_s(SAFESTR(buf), "copy /B %s+%s %s >nul", journal_txt_processing, db->journal_txt, journal_txt_processing2);
				_flushall();
				if (0!=system(buf)) {
					error=true;
					gimmeLog(LOG_FATAL, "\n\"%s\"\nError running copy command\nManual fix might be needed.  Contact programmers!", buf);
				} else if (0!=fileRenameToBak(db->journal_txt)) {
					error=true;
					gimmeLog(LOG_FATAL, "\nError deleting %s\nManual fix might be needed.  Contact programmers!", db->journal_txt);
				} else if (0!=remove(journal_txt_processing)) {
					error=true;
					gimmeLog(LOG_FATAL, "\nError deleting %s\nManual fix might be needed.  Contact programmers!", journal_txt_processing);
				} else if (0!=rename(journal_txt_processing2, journal_txt_processing)) {
					error=true;
					gimmeLog(LOG_FATAL, "\nError renaming %s to %s\nManual fix might be needed.  Contact programmers!", journal_txt_processing2, journal_txt_processing);
				}
			}
			if (error) {
				//Error on 1 of the 4 operations
				doUnlock(db);
				GIMME_CRITICAL_END;
				return -1;
			}
		} else {
			if (fileExists(db->journal_txt)) {
				if (0!=rename(db->journal_txt, journal_txt_processing)) {
					gimmeLog(LOG_FATAL, "\nErroring renaming %s to %s", db->journal_txt, journal_txt_processing);
					doUnlockJ(db);
					doUnlock(db);
					GIMME_CRITICAL_END;
					return -1;
				}
			}
		}
	}
	//		4. Unlock journal.txt
	doUnlockJ(db);
	// GIMME_CRITICAL_END: + doUnlock(db)
	if (gimmeDatabaseInCritical==1 && gimmeDatabaseRequestClose) {
		doUnlock(db);
		exit(-1);
	}
	gimmeDatabaseInCritical--;

	GIMME_CRITICAL_START;
	//		5. merge journal.proc with database in memory
	need_to_save = false;
	if (fileExists(journal_txt_processing)) {
		int ret;

		db->jprocessing_name = strdup(journal_txt_processing);
		if (0!=(ret=gimmeDatabaseLoadJournal(db, journal_txt_processing))) {
			return ret;
		}

		sprintf_s(SAFESTR(history_txt), "%shistory.txt", dbroot);
		backSlashes(history_txt);
		backSlashes(journal_txt_processing);
		need_to_save = true;
	}

	// timer output
	if (!gimme_state.quiet) {
		gimmeLog(LOG_TOFILEONLY, "Database load in %1.3fs + %1.3fs wait", timerElapsed(timer)-waitTime, waitTime);
		if (show_steps) {
			gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs", timerElapsed(timer)-parseStart);
		} else {
			gimmeLog(LOG_TOSCREENONLY, "done in %1.3fs", timerElapsed(timer));
		}
		gimmeLog(LOG_TOSCREENONLY, ".     \n");
	}
	timerFree(timer);

	//		6. save new database.txt
	if (need_to_save)
	{
		// Only save the Database if it's big enough that we think it's useful to save
		srand(time(NULL));
		if ((rand()%3 && !g_force_flush) || g_force_flush==-1 || g_ignore_locks) {
			// Append some debug info into the journal
			//f = fopen(history_txt, "a");
			//fprintf(f, "# Not appending journal by %s (%s)\n", gimmeGetUserName(), gimmeDatabaseGetVersionString());
			//fclose(f);
		} else {
			FILE *f;
			// Append some debug info into the journal

			f = fopen(history_txt, "a");
			fprintf(f, "# Begin History append by %s (%s)\n", gimmeGetUserName(), gimmeDatabaseGetVersionString());
			fclose(f);
			sprintf_s(SAFESTR(buf), "type %s >> %s", journal_txt_processing, history_txt);
			_flushall();
			if (0!=system(buf)) {
				gimmeLog(LOG_FATAL, "\n\"%s\"\nError running copy command\nHistory.txt may not have been updated.  Contact programmers!", buf);
			}
			f = fopen(history_txt, "a");
			fprintf(f, "# End History append by %s (%s)\n", gimmeGetUserName(), gimmeDatabaseGetVersionString());
			fclose(f);

			// Then save DB, delete journal.proc, unlock DB
			gimmeDatabaseSave(db);
			f = fopen(history_txt, "a");
			fprintf(f, "# Finished saving DB by %s (%s)\n", gimmeGetUserName(), gimmeDatabaseGetVersionString());
			fclose(f);

		}
	}
	gimmeDatabaseUnlock(db);
	GIMME_CRITICAL_END;

	return 0;
}

void gimmeDatabaseRecreate(const char *_dbroot) {
	// This ia basically a hack for disaster recovery and testing purposes 
	GimmeDatabase *db = gimmeDatabaseNew();
	char lockfilename[CRYPTIC_MAX_PATH];
	char dbroot[CRYPTIC_MAX_PATH];
	char jlockfilename[CRYPTIC_MAX_PATH];

	initCtrlHandler();

	strcpy(dbroot, _dbroot);
	strcat(dbroot, "/");
	sprintf_s(SAFESTR(lockfilename), "%sdatabase.txt.databaselock", dbroot);
	db->lockfile_handle = gimmeWaitToAcquireLock(lockfilename);
	db->lockfile_name =strdup(lockfilename);
	db->locked = 1;

	sprintf_s(SAFESTR(jlockfilename), "%sjournal.txt.databaselock", dbroot);
	db->jlockfile_handle = gimmeWaitToAcquireLock(jlockfilename);
	db->jlockfile_name = strdup(jlockfilename);
	db->jlocked = 1;

	sprintf_s(SAFESTR(lockfilename), "%sjournal.txt", dbroot);
	remove(lockfilename);
	sprintf_s(SAFESTR(lockfilename), "%sjournal.processing", dbroot);
	remove(lockfilename);
	sprintf_s(SAFESTR(lockfilename), "%sjournal.processing_temp", dbroot);
	remove(lockfilename);
	sprintf_s(SAFESTR(lockfilename), "%sdatabase.txt", dbroot);
	fileRenameToBak(lockfilename);

	gimmeDatabaseLoadBranchFromFS(db, dbroot, "/");

	db->loc[LOC_CHECKINTIME]=4;
	db->loc[LOC_FNAME]=2;
	db->loc[LOC_LOCK]=3;
	db->loc[LOC_SIZE]=0;
	db->loc[LOC_TIME]=1;
	db->filename = lockfilename; // database.txt
	gimmeDatabaseSave(db);
	doUnlockJ(db);
	gimmeDatabaseUnlock(db);

}



void gimmeNodeAdd(GimmeNode **head, GimmeNode **tail, GimmeNode *parent, char *fn, const char *lockvalue, __time32_t timestamp, size_t size, __time32_t checkintime)
{
	char *s, *s2;
	GimmeNode *node = *head, *lastnode=NULL;
	GimmeNode *node2;
	int is_folder=0;
	bool is_new=false;
	int stricmpres;

	//printf("Adding %s - %s - %d\n", fn, lockvalue, timestamp);

	// filter the string a little
	while (fn[0]=='/' || fn[0]=='\\') fn++;
	if (fn[0]=='.' && (fn[1]=='/' || fn[1]=='\\')) fn+=2;
	s = strchr(fn, '/');
	if (!s) {
		is_folder=0;
	} else {
		*s=0;
		is_folder=1;
		s++;
	}

	if (tail && *tail) {
		stricmpres = stricmp(fn, (*tail)->name);
		if (stricmpres>0) {
			// This new node is supposed to be right past the tail
			lastnode = *tail;
			node = NULL;
			is_new = true;
		} else if (stricmpres==0) {
			// This node *is* the tail
			node = *tail;
			is_new = false;
			lastnode = (void*)-1; // Not valid
		}
	}

	while (node) {
		stricmpres = stricmp(fn, node->name);
		if (stricmpres==0) {
			// Found the folder!
			if (!node->is_dir) {
				if (is_folder) { // this *is* a folder
					gimmeLog(LOG_FATAL, "ERROR: Possible database corruption, notify programmers.  A file is in the list named the same as a folder");
				} else { // They're both files, treat this as an Update command
					break;
				}
			} else {
				gimmeNodeAdd(&node->contents, &node->contents_tail, node, s, lockvalue, timestamp, size, checkintime);
			}
			return;
		} else if (stricmpres<0) {
			// We want to insert the new one *before* this one
			is_new=true;
			break;
		}
		lastnode = node;
		node = node->next;
	}
	if (node!=NULL && !is_new) { // We're doing an update
		node2 = node;
		if (node2->lock) {
			free(node2->lock);
			node2->lock = NULL;
		}
		// keep name
		assert(node2->parent == parent);
		assert(!is_folder);
	} else {
		is_new=true;
		// Was not found in the list
		node2 = gimmeNodeNew();
		node2->parent = parent;
		node2->name = strdup(fn);
		node2->revision = -1;
		node2->branch = -1;
	}
	node2->is_dir = is_folder;
	if (is_folder) {
		gimmeNodeAdd(&node2->contents, &node2->contents_tail, node2, s, lockvalue, timestamp, size, checkintime);
	} else {
		if (lockvalue==NULL)
			node2->lock = NULL;
		else			
			node2->lock = strdup(lockvalue);
		node2->timestamp = timestamp;
		node2->size = size;
		node2->checkintime = checkintime;
		// Parse revision number and branch number
		s = strrchr(node2->name,'#');
		if (!lockvalue) { // locks don't have revision numbers
			// revision number
			if (s) {
				assert(*(s-1)=='v');
				assert(*(s-2)=='_');
				node2->revision = atoi(s+1);
				// Find branch number
				*s=0;
				s2 = strrchr(node2->name, '#');
				*s='#';
				s = s2;
			}
		}
		// Branch number
		if (s) {
			if (strncmp(s-3, "_vb#", 3)==0) {
				// It is in fact a branch number, and not some other # symbol
				node2->branch = atoi(s+1);
			}
		}
		if (node2->branch==-1)
			node2->branch = 0;
	}
	if (node!=NULL && !is_new) { // We are doing an update
		return;
	}
	if (lastnode) { // There was a list to begin with, add to it/insert in the middle
		GimmeNode *next = lastnode->next;
		node2->prev = lastnode;
		lastnode->next = node2;
		node2->next = next;
	} else {  // node==NULL -> there was no list to begin with, or we want this to be the new first element
		node2->next = *head;
		*head = node2;
	}
	if (node2->next) {
		node2->next->prev = node2;
	}
	if (node2->next==NULL && tail) {
		// This is the new tail
		*tail = node2;
	}
	return;
}

MP_DEFINE(GimmeNode);

GimmeNode *gimmeNodeNew()
{
	GimmeNode *ret;
	MP_CREATE(GimmeNode, 8192);
	ret = MP_ALLOC(GimmeNode);
	ret->size=-1;
	return ret;
}

void gimmeNodeDelete(GimmeNode *node)
{
	GimmeNode *walk = node;
	GimmeNode *next;

	while (walk) {
		assert( walk->next != walk );
		next = walk->next;
		if (walk->contents)
			gimmeNodeDelete(walk->contents);
		SAFE_FREE(walk->lock);
		SAFE_FREE(walk->name);
		MP_FREE(GimmeNode, walk);
		walk = next;
	}
}

char *gimmeNodeGetFullPath(GimmeNode *node) 
{
	static char ret[CRYPTIC_MAX_PATH];
	if (node->parent) {
		gimmeNodeGetFullPath(node->parent);
	} else {
		ret[0]=0;
	}
	strcat(ret, "/");
	strcat(ret, node->name);
	return ret;
}

GimmeNode *gimmeNodeFind(GimmeNode *node, const char *_fn) {
	char buf[CRYPTIC_MAX_PATH], *fn;
	char *s;
	int is_folder;

	strcpy(buf, _fn);
	fn = buf;
	//printf("Adding %s - %s - %d\n", fn, lockvalue, timestamp);
	while (fn[0]=='/' || fn[0]=='\\') fn++;
	s = strchr(fn, '/');
	if (!s) {
		is_folder=0;
	} else {
		*s=0;
		is_folder=1;
		s++;
	}
	while (node) {
		int stricmpres = stricmp(fn, node->name);
		if (stricmpres==0) {
			// Found it!
			if (is_folder && *s) {
				return gimmeNodeFind(node->contents, s);
			} else {
				return node;
			}
		} else if (stricmpres<0) {
			// If the node was here, it would be before this!  Not gonna find it.
			return NULL;
		}
		node = node->next;
	}
	return NULL;
}

int gimmeNodeRecurse(GimmeNode *node, gimmeNodeOp op)
{
	GimmeNode *walk=node;
	while (walk) {
		// Do self
		if (op && !op(walk))
			return 0;
		// Do children
		if (!gimmeNodeRecurse(walk->contents, op))
			return 0;
		// Do siblings
		walk = walk->next;
	}
	return 1;
}

void gimmeNodeDeleteFromTree(GimmeNode **head, GimmeNode **tail, GimmeNode *node)
{
	GimmeNode *parent = node->parent;
	
	assert(!node->contents);

	if (parent == NULL) {
		// We're at the base!
		if (*tail==node) {
			*tail = node->prev;
		}
		listRemoveMember(node, head);
		gimmeNodeDelete(node);
		return;
	}
	// remove from parent's list and free memory
	if (parent->contents_tail==node) {
		parent->contents_tail=node->prev;
	}
	listRemoveMember(node, &parent->contents);
	gimmeNodeDelete(node);
	if (parent->contents == NULL) {
		// The parent has no children, remove it as well
		// In theory this will never happen within the way Gimme currently works, so this hasn't been tested
		gimmeNodeDeleteFromTree(head, tail, parent);
	}
}

const char *gimmeDatabaseGetVersionString(void) {
	static char buf[64];
	if (!buf[0]) {
		if (!gBuildVersion)
			return "$Revision: 155762 $";
		sprintf(buf, "%cRevision: %d %c", '$', gBuildVersion, '$');
	}
	return buf;
}

void gimmeEnsureDeleteLock(const char *fn) {
	while (-1==remove(fn)) {
		if (!fileExists(fn)) {
			gimmeLog(LOG_FATAL, "ERROR!  Lockfile deleted by someone other than you!");
			break;
		}
		Sleep(1);
	}
}

int gimmeAcquireLock(const char *lockfilename) {
	int handle;
	mkdirtree((char*)lockfilename);
	_wsopen_s_UTF8(&handle, lockfilename, _O_CREAT | _O_EXCL | _O_WRONLY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
	if (handle>=0) {
		gimmeWriteLockData(handle);
	}
	return handle;
}

int gimmeUnaquireAndDeleteLock(int lockfile_handle, char *lockfile_name)
{
	if (lockfile_handle && 0==_close(lockfile_handle)) { // we had it open
		lockfile_handle=0;
	} else {
		// no handle, or error closing the file, just delete the lockfile
	}
	if (lockfile_name!=NULL) {
		gimmeEnsureDeleteLock(lockfile_name);
		lockfile_name=0;
	}
	return 0;
}

int gimmeWaitToAcquireLock(char *lockfilename) {
	int lockfile_handle;
	int loopcount=MAX_WAITS-5000/WAIT_TIME; // wait only 5 seconds the first time

	getLocalHostNameAndIPs(); // Call this once first to cache the results, so that we are not needlessly locking the database while looking up our hostname!

	if (g_ignore_locks)
		return 0;

	while ((lockfile_handle = gimmeAcquireLock(lockfilename)) < 0) {
		if (loopcount++ > MAX_WAITS) { 
			// We seem to have timed out, try and find out who has this locked
			int count;
			char *mem = fileAlloc(lockfilename, &count);
			gimmeLog(LOG_TOSCREENONLY, "\nStill waiting to acquire database lock; Currently locked by\n");
			if (mem==NULL || count==0 || strlen(mem)==0) {
				// error reading the file... perhaps someone else is just writing to it now...
				gimmeLog(LOG_TOSCREENONLY, "UNKNOWN\n");
			} else {
				gimmeLog(LOG_TOSCREENONLY, "%s", mem);
			}
			if (mem!=NULL) {
				free(mem);
				mem=NULL;
			}
			loopcount=0;
		} 
		Sleep(WAIT_TIME); 
	}
	return lockfile_handle;
}


int gimmeJournal(GimmeDatabase *db, const char *line) {
	FILE *f;
	__time32_t real_timestamp;

	assert(db->journal_txt);
	assert(db->jlockfile_name);
	assert(!db->jlocked);
	assert(!db->jwriting);

	assert(!g_ignore_locks); // Not designed for this, but I guess it could do this...

	db->jwriting=1;
	if (!g_ignore_locks)
		db->jlockfile_handle = gimmeWaitToAcquireLock(db->jlockfile_name);
	db->jlocked=1;

	real_timestamp = fileTouch(db->journal_txt, NULL).st_mtime;

	f = fopen(db->journal_txt, "a");
	fprintf(f, "%d\t%s\n", real_timestamp, line);
	fclose(f);

	db->jwriting=0;

	doUnlockJ(db);

	return 0;
}

int gimmeJournalAdd(GimmeDatabase *db, const char *relpath, size_t size, __time32_t file_timestamp, const char *lockee) {
	char line[1024];
	// ADD v1
	sprintf_s(SAFESTR(line), "\"%s\"\tADD1\t\"%s\"\t%d\t%d\t\"%s\"", gimmeGetUserName(), relpath, size, file_timestamp, lockee==NULL?"":lockee);
	return gimmeJournal(db, line);
}
int gimmeJournalRm(GimmeDatabase *db, const char *relpath) {
	char line[1024];
	// RM v1
	sprintf_s(SAFESTR(line), "\"%s\"\tRM1\t\"%s\"", gimmeGetUserName(), relpath);
	return gimmeJournal(db, line);
}


