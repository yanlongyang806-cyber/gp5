#include "gimme.h"

#include <sys/types.h>
#include <sys/utime.h>
#include <io.h>
#include <stdio.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "utils.h"
#include "file.h"
#include "fileutil.h"
#include "fileutil2.h"

#include "wininclude.h"
#include "sysutil.h"
#include "osdependent.h"
#include <Shellapi.h>
#include <winsock.h>
#include <time.h>
#include "gimmeDatabase.h"
#include <conio.h>
#include <WinCon.h>
#include <direct.h>
#include <crtdbg.h>
#include "earray.h"
#include "RegistryReader.h"
#include "error.h"
#include "logging.h"
#include "gimmeBranch.h"
#include "gimmeUtil.h"
#include "timing.h"
#include "threadedFileCopy.h"
#include "textparser.h"
#include "StashTable.h"
#include "SharedMemory.h"
#include "genericDialog.h"
#include "fileWatch.h"
#include "FilespecMap.h"
#include "hoglib.h"
#include "piglib.h"
#include "mathutil.h"
#include "strings_opt.h"
#include "mutex.h"
#include <share.h>
#include "resource.h"
#include "winutil.h"
#include "UTF8.h"

#pragma comment (lib, "wsock32.lib")

GimmeState gimme_state;

static char *rule_file_src = "%s/rules.txt";
static char *config_file2 = "N:/revisions/gimmecfg.txt";

GimmeDir **eaGimmeDirs = NULL;

static char *gimmeErrorString[] = {
	"NO ERROR",
"Error copying file to/from the database",
"Error opening rulesfile",
"Couldn't make lock file",
"Unable to remove lock file",
"Can't find source control folder to match filename",
"Lock failure - trying to edit a lock file or trying to lock a file which is not mirrored",
"You can't put that file back because you don't have it locked, someone else does",
"Can't delete local file - some program still has it open!",
"File is already checked out by someone else",
"File not found in source control",
"Local file not found",
"No source control available",
"File already deleted",
"Error accessing database file",
"File modified in the future",
"Operation canceled by the user",
"Failed to load GimmeDLL.DLL", // This string will never be used, since it's in the DLL, but only needed if the DLL can't be loaded...
"Invalid command line syntax",
};

char *gimme_gla_error_no_sc = "Source Control not available";
char *gimme_gla_error = "Not in database";
char *gimme_gla_checkedout = "You have this file checked out";
char *gimme_gla_not_latest = "You do not have the latest version";

static char *gimmeOpString[] = {
	"GIMME_CHECKOUT",     //   0 - checkout
	"GIMME_CHECKIN",      //   1 - checkin
	"GIMME_FORCECHECKIN", //   2 - forcefully checkin even if someone else has it checked out
	"GIMME_DELETE",       //   3 - mark a file as deleted
	"GIMME_GLV",          //   4 - just get the latest verison (don't checkout)
	"GIMME_UNDO_CHECKOUT",//   5 - undo checkout
	"GIMME_CHECKIN_LEAVE_CHECKEDOUT", // Check in, but leave checked out
	"GIMME_LABEL",
};


static char statusbuf[1000];
static int status=0;

int getLatestVersionFile(GimmeDir *gimme_dir, const char *relpath, GimmeNode *versionsNode, int revision, int quiet, bool allowAsync);
int getLowestVersion(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int approvedver, int branch, const char *relpath);
int getHighestVersionFromNode(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int branch, const char *relpath);
int getHighestVersion(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char * relpath);
int getSpecificRevisionFromNode(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int revision, int branch, const char * relpath);
int getSpecificRevision(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int revision, int branch, const char * relpath);
int getApprovedRevision(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char * relpath);
int getRevisionByTimeFromNode(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int branch, const char * relpath);
int getRevisionByTime(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char * relpath);
int getRules(GimmeDir *gimme_dir);
RuleSet *getRuleSet(GimmeDir *gimme_dir, const char *relpath);
bool gimmeIsBinFile(GimmeDir *gimme_dir, const char *relpath);
int getAll(const char *folder, int revision, int quiet);

int updateHogFileAfterCopy(GimmeDir *gimme_dir, char *localname, U32 timestamp, U8 **file_data, U32 file_size);
void gimmeCloseAllHogFiles(void);

char *getLocalIP(void);
int gimmeLoadConfig(void);
int gimmeDirDatabaseLoad(GimmeDir *gimme_dir, const char *fullpath); // Locks and reads the database
char *findRelPath(char *fname, GimmeDir *gimme_dir);
FWStatType fileTouch(const char *filename, const char *reffile);
GimmeDir *findGimmeDir(const char *fullpath);
char *isLocked(GimmeDir *gimme_dir, const char *relpath);
int makeDBName(GimmeDir *gimme_dir, const char *relpath, int revision, GimmeNode **ret_node);
int makeDBNameQuick(GimmeDir *gimme_dir, GimmeNode *versionNode, int revision, GimmeNode **ret_node, const char * rel_path);
int serverSideFileTouch(const char *filename);

// i want this particular message box to say "yes to all" "yes" and "no", instead of the default "ok to all" buttons
int okToAllCancelCallback(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_OKTOALL), "Yes To All");
			SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), "Yes");
			SetWindowText_UTF8(GetDlgItem(hDlg, IDCANCEL), "No");
			break;
	}
	return FALSE;
}


static void clearLine(void) {
	static char fmt[100]="";
	if (fmt[0]==0) {
		HANDLE hConsoleOut = GetStdHandle( STD_OUTPUT_HANDLE );
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(hConsoleOut, &csbi);
		sprintf_s(SAFESTR(fmt), "\r%%-%dc\r", csbi.dwMaximumWindowSize.X-1);
	}
	printf(FORMAT_OK(fmt), ' ');
}


void gimmeOfflineLog(char const *fmt, ...)
{
	va_list ap;
	char str[1000];
	FILE *logfile=NULL;

	logfile = fopen("C:\\gimme_offline_log.txt", "a+");
	if (!logfile) {
		printf("\nERROR: Cannot open c:\\gimme_offline_log.txt for writing!\n");
		return;
	}

	_strdate( str ); // Date
	fprintf(logfile, "%s ", str);
	_strtime( str ); // Time
	fprintf(logfile, "%s ", str);

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	fprintf(logfile, "%s\n", str); // Log

	fflush(logfile);
	fclose(logfile);
}

#undef gimmeOfflineTransactionLog
void gimmeOfflineTransactionLog(bool addDate, char const *fmt, ...)
{
	va_list ap;
	char str[1000];
	FILE *logfile=NULL;

	logfile = fopen("C:\\gimme_disconnected.txt", "a+");
	if (!logfile) {
		printf("\nERROR: Cannot open c:\\gimme_disconnected.txt for writing!\n");
		return;
	}

	if (addDate) {
		_strdate( str ); // Date
		fprintf(logfile, "%s ", str);
		_strtime( str ); // Time
		fprintf(logfile, "%s ", str);
	}

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	fprintf(logfile, "%s\n", str); // Log

	fflush(logfile);
	fclose(logfile);
}

static int gimme_logging_enabled=1;

// Custom logging function set by setCustomGimmeLog().
static CustomGimmeLogType custom_log_function = NULL;

// Set custom function for Gimme logging
CustomGimmeLogType setCustomGimmeLog(CustomGimmeLogType fptr)
{
	CustomGimmeLogType old = custom_log_function;
	custom_log_function = fptr;
	return old;
}

void gimmeLog_dbg(GimmeLogLevel loglevel, char const *fmt, ...)
{
	char str[1000];
	va_list ap;
	static FILE *logfile=NULL;
	static int needs_clearning=1;
	static int createdConsoleWindow=0;

	if (gimme_logging_enabled && !logfile) {
		logfile = fopen("C:\\gimme_log.txt", "w");
		if (!logfile && !IsUsingVista()) {
			printf("\nERROR: Cannot open c:\\gimme_log.txt for writing!\n");
		}
	}

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	if (loglevel!=LOG_TOSCREENONLY && custom_log_function)
		custom_log_function(str);
	if (loglevel!=LOG_TOSCREENONLY && logfile) {
		fprintf(logfile, "%s\n", str);
		fflush(logfile);
	}

	if (loglevel==LOG_TOFILEONLY || (loglevel==LOG_WARN_LIGHT && gimme_state.nowarn)) return;

	if (!createdConsoleWindow) {
		newConsoleWindow();
		createdConsoleWindow = 1;
	}

	showConsoleWindow(); // In case it was hidden (i.e. in a gimme -stat)

	if (needs_clearning) {
		clearLine();
		needs_clearning=0;
	}
	// Set the colors, handle pausing
	switch(loglevel) {
		case LOG_TOSCREENONLY:
			consoleSetFGColor(COLOR_BRIGHT);
			break;
		case LOG_FATAL:
		case LOG_WARN_HEAVY:
			consoleSetFGColor(COLOR_RED | COLOR_BRIGHT);
			gimme_state.pause=1;
			break;
		default:
			consoleSetFGColor(COLOR_RED | COLOR_GREEN | COLOR_BLUE);
	}

	printf("%s", str);
	if (loglevel!=LOG_TOSCREENONLY) {
		if (loglevel==LOG_INFO && !gimme_state.simulate) {
			printf("\r");
			needs_clearning=1;
		} else {
			printf("\n");
		}
	}
	if (str[strlen(str)-1]=='\r') {
		needs_clearning=1;
	}
	consoleSetFGColor(COLOR_RED | COLOR_GREEN | COLOR_BLUE);
}

static void flushStatus(void) {
	if (status==3) {
		gimmeLog(LOG_TOSCREENONLY, "%s", statusbuf);
		gimmeLog(LOG_TOSCREENONLY, "\r");
		status=0;
	} else if (status==2) {
		gimmeLog(LOG_INFO, "%s", statusbuf);
		status=0;
	} else if (status) {
		gimmeLog(LOG_WARN_HEAVY, "%s", statusbuf);
		status=0;
	}
}

int gimmeIsSambaDrive(const char *path)
{
	static U8 cache[26];
	char drive[4];
    int idx;

	return 0; // Looks like Linux samba now behave like Windows servers?

    strncpy_s(drive, ARRAY_SIZE(drive), path, 3);
	if (drive[0]=='.') {
		char buf[CRYPTIC_MAX_PATH];
		if (fileGetcwd(buf, ARRAY_SIZE(buf))) {
			 strncpy_s(drive, ARRAY_SIZE(drive), buf, 3);
		}
	}
	backSlashes(drive);
	drive[0] = toupper(drive[0]);
	idx = drive[0] - 'A';
	if (drive[1]!=':' || drive[2]!='\\' || idx<0 || idx>=ARRAY_SIZE(cache)) {
		if (drive[0]=='\\' && drive[1]=='\\') {
			// UNC share, do something!
			
			DWORD serial, maxcomplength, flags;
			static struct {
				char name[MAX_PATH];
				int value;
			} unc_cache[16];
			static int unc_cache_count=0;
			BOOL ret;
			char drivebuf[MAX_PATH];
			char *s;
			strcpy(drivebuf, path);
			backSlashes(drivebuf);
			s = strchr(drivebuf+2, '\\');
			if (!s || !s[1]) {
				printf("Invalid path passed to gimmeIsSambaDrive: %s\n", path);
				return false;
			}
			s = strchr(s+1, '\\');
			if (s) {
				s[1] = '\0';
			} else {
				strcat(drivebuf, "\\");
			}
			for (idx=0; idx<unc_cache_count; idx++) {
				if (stricmp(unc_cache[idx].name, drivebuf)==0)
					break;
			}
			if (idx == unc_cache_count && unc_cache_count < ARRAY_SIZE(unc_cache)) {
				unc_cache_count++;
				strcpy(unc_cache[idx].name, drivebuf);
			}
			
			if (idx == unc_cache_count || !(unc_cache[idx].value&2)) {
				int value;
				char *pNameBuf = NULL;
				char *pFSNameBuf = NULL;
				ret = GetVolumeInformation_UTF8(drivebuf, &pNameBuf,
					&serial, &maxcomplength, &flags, &pFSNameBuf);
				if (!ret) {
					printf("Invalid path passed to gimmeIsSambaDrive or unable to query: %s\n", drivebuf);
					estrDestroy(&pNameBuf);
					estrDestroy(&pFSNameBuf);
					return false;
				}
				
				if (stricmp(pFSNameBuf, "NTFS")==0 && flags == (FS_CASE_IS_PRESERVED|FS_CASE_SENSITIVE|FS_PERSISTENT_ACLS|FILE_VOLUME_QUOTAS)) {
					value = 1;
				} else {
					value = 0;
				}
				if (idx == unc_cache_count)
					return value;
				unc_cache[idx].value = value | 2;

				estrDestroy(&pNameBuf);
				estrDestroy(&pFSNameBuf);
			}
			return unc_cache[idx].value&1;
		} else {
			printf("Invalid path passed to gimmeIsSambaDrive: %s\n", path);
			return false;
		}
	}
	if (!(cache[idx]&2))
	{
		char *pNameBuf = NULL;
		char *pFSNameBuf = NULL;
		DWORD serial, maxcomplength, flags;
		BOOL ret;
		ret = GetVolumeInformation_UTF8(drive, &pNameBuf,
			&serial, &maxcomplength, &flags, &pFSNameBuf);
		if (ret && (stricmp(pFSNameBuf, "NTFS")==0 && flags == (FS_CASE_IS_PRESERVED|FS_CASE_SENSITIVE|FS_PERSISTENT_ACLS|FILE_VOLUME_QUOTAS))) {
			cache[idx]=1|2;
		} else {
			cache[idx]=0|2;
		}

		estrDestroy(&pNameBuf);
		estrDestroy(&pFSNameBuf);
	}
	return cache[idx]&1;
}

int gimmeGetTimeAdjust(void) { // returns the amount that needs to be added to the result of a _stat call to sync up with PST
	int adjust = 0;
	FILETIME ft, ft2;
	SYSTEMTIME st;
	BOOL b;
	ft.dwHighDateTime=29554851;  // 15:09 PST
	ft.dwLowDateTime=3306847104;

	b = FileTimeToLocalFileTime(&ft, &ft2);
	b = FileTimeToSystemTime(&ft2, &st);
	if (st.wHour == 15) {
		//ok
		adjust=0;
	} else {
		adjust=3600*(15 - st.wHour);
	}
	return adjust;
}
// Returns time in PST
int pststat(const char *path, FWStatType *buffer) {
	int ret;
	int adjust = gimmeGetTimeAdjust();
	ret = fwStat(path, buffer);
	//buffer->st_atime = buffer->st_mtime; // Debug: store raw time
// Time adjusts not needed for a Samba network server
	if (ret!=0 || gimmeIsSambaDrive(path)) {
		return ret;
	}
	// MS: st_atime and st_ctime aren't supported by FileWatcher.
	//buffer->st_atime += adjust;
	//buffer->st_ctime += adjust;
	buffer->st_mtime += adjust;
	return ret;
}

int pstutime(const char *filename, struct _utimbuf *times) {
	struct _utimbuf utb;
	utb = *times;
// Time adjusts not needed for a Samba network server
	if (gimmeIsSambaDrive(filename)) {
	} else
	{
		//utb.actime -=gimmeGetTimeAdjust();
		utb.modtime -=gimmeGetTimeAdjust();
	}
	return _utime(filename, &utb);
}


int pstSetFileCreatedTime(const char *filename, __time32_t created)
{
	int ret = 0;
	FILETIME ft;
	U64 time;
	HANDLE fileHandle = NULL;
	DWORD error;


	// Time adjusts not needed for a Samba network server
	if (!gimmeIsSambaDrive(filename))
	{
		created -= gimmeGetTimeAdjust();
	}

	created -= 50000;

	time = UInt32x32To64(created, 10000000) + 116444736000000000;
	ft.dwLowDateTime = (DWORD)time;
	ft.dwHighDateTime = (DWORD)(time>>32);

	fileHandle = CreateFile_UTF8(filename, FILE_WRITE_ATTRIBUTES, 
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
		FILE_ATTRIBUTE_NORMAL, NULL);

	error = GetLastError();

	ret = SetFileTime( fileHandle, &ft, NULL, NULL );

	CloseHandle( fileHandle );

	return ret;
}


static const char *gimmeOpToString(GIMMEOperation op) {
	return gimmeOpString[op];
}

int gimmeDirDatabaseClose(GimmeDir *gimme_dir) {
	gimme_dir->database->ref_count--;
	return 0;
}

int gimmeDirDatabaseLoad(GimmeDir *gimme_dir, const char *fullpath) // Locks and reads the database
{
	static char loadedroot[CRYPTIC_MAX_PATH];
	static int loadedwith = -1;

	// if gimme_state.db_mode changes, we want to reload the database with the new db settings, rather than using the cached one.
	if ( loadedwith == -1 )
		loadedwith = gimme_state.db_mode;

	if (eaSize(&eaGimmeDirs)==0)
		return GIMME_ERROR_NO_SC;
	if (gimme_dir->database && gimme_dir->database->ref_count>0 && loadedwith == gimme_state.db_mode) { // already loaded
		if (strnicmp(loadedroot, fullpath, strlen(loadedroot))!=0) {
			assert(!"Database load called a second time, and it's not in the same tree!  Bad!");
			//TODO: just append to the current tree?
		}
		gimme_dir->database->ref_count++;
	} else {
		char dbname[CRYPTIC_MAX_PATH];
		char fullpath2[CRYPTIC_MAX_PATH];
		char *relpath;
		bool single_file=false;
		int saved_process_priority = GetPriorityClass(GetCurrentProcess());
		int saved_thread_priority = GetThreadPriority(GetCurrentThread());

		loadedwith = gimme_state.db_mode;

		// Load the Database
		SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);

		if (gimme_dir->database) {
			gimmeDatabaseDelete(gimme_dir->database);
			gimme_dir->database=NULL;
		}
		gimme_dir->database = gimmeDatabaseNew();

		// Determine what portion of the database needs to be loaded
		sprintf_s(SAFESTR(dbname), "%s/", gimme_dir->lock_dir);
		if (fullpath) {
			strcpy(fullpath2, fullpath);
			relpath = findRelPath(fullpath2, gimme_dir);
			if (dirExists(fullpath2)) {
				// we're good, it's a directory
				single_file = false;
			} else {
//				// reduce down to the path the file is in
//				char *z;
//				forwardSlashes(relpath);
//				z = strrchr(relpath, '/');
//				if (!z) {
//					assert(!"passed in invalid path to gimmeDirDatabaseLoad!");
//				}
//				*++z=0; // take off the file name, leave just a path
//				assert(dirExists(fullpath2));
				single_file = true;
			}
			strcpy(loadedroot, fullpath2);
		} else {
			relpath="/";
			strcpy(loadedroot, gimme_dir->local_dir);
		}

		if (NO_ERROR!=gimmeDatabaseLoad(gimme_dir->database, dbname, relpath, single_file)) {
			SetPriorityClass(GetCurrentProcess(),saved_process_priority);
			SetThreadPriority(GetCurrentThread(),saved_thread_priority);
			return GIMME_ERROR_DB;
		}
		gimme_dir->database->ref_count=1;
		SetPriorityClass(GetCurrentProcess(),saved_process_priority);
		SetThreadPriority(GetCurrentThread(),saved_thread_priority);
	}
	return NO_ERROR;
}

static int gimmeDirDatabaseFlush(GimmeDir *gimme_dir) {
	// Could use this if we queue journal entries to be written, write them all here,
	// but queueing up journal entries makes database consistency more suceptable to
	// fall apart when people close Gimme while it's doing something
	return NO_ERROR;
}

int gimmeDirDatabaseCloseAll(void) {
	int i;
	int fret=0;
	for (i=0; i<eaSize(&eaGimmeDirs); i++) {
		if (eaGimmeDirs[i]->database) {
			gimmeDatabaseDelete(eaGimmeDirs[i]->database);
			eaGimmeDirs[i]->database = NULL;
		}
	}
	gimmeCloseAllHogFiles();
	return fret;
}

// Returns 0 on success
int deleteLocalFile(GimmeDir *gimme_dir, const char *filename_const, bool recycle)
{
	char filename[CRYPTIC_MAX_PATH];
	bool do_on_disk = true;
	bool do_into_hogs = gimmeShouldUseHogs(gimme_dir);
	int ret=0;
	Strncpyt(filename, filename_const);
	if (do_into_hogs)
	{
		ret = updateHogFileAfterCopy(gimme_dir, filename, 0, NULL, -1);
	}
	if (do_on_disk)
	{
		ret = _chmod(filename, _S_IREAD | _S_IWRITE);
		if (ret==-1) {
			// File doesn't exist on disk
		} else {
			_chdir(".."); // otherwise we can't prune the folder
			if (recycle) {
				ret = fileMoveToRecycleBin(filename);
			} else {
				ret = remove(filename);
			}
			rmdirtree((char*)filename);
		}
	}
	return ret;
}



const char *gimmeGetErrorString(int errorno) {
	if (errorno>=ARRAY_SIZE(gimmeErrorString)) {
		return gimmeErrorString[0];
	}
	return gimmeErrorString[errorno];
}

char *getApprovedFile(GimmeDir *gimme_dir)
{
	static char *approve_file_src = "%s/approved.txt";
	static char *approve_branch_file_src = "%s/approved.%d.txt";
	static char buf[CRYPTIC_MAX_PATH];
	if (gimme_dir->active_branch>0) {
		sprintf_s(SAFESTR(buf), FORMAT_OK(approve_branch_file_src), gimme_dir->lock_dir, gimme_dir->active_branch);
	} else {
		sprintf_s(SAFESTR(buf), FORMAT_OK(approve_file_src), gimme_dir->lock_dir);
	}
	return buf;
}

int getHighestBranch(GimmeNode *node)
{
	int highest = -1;
	if ( !node )
		return highest;
	if ( !node->is_dir )
		node = node->parent->contents;
	else
		node = node->contents;

	while ( node )
	{
		if ( node->branch > highest )
			highest = node->branch;
		node = node->next;
	}

	return highest;
}

void gimmeLogWhoAmI(void)
{
	GimmeDir *gimme_dir = eaGimmeDirs[0];
	char fn[CRYPTIC_MAX_PATH];
	sprintf_s(SAFESTR(fn), "%s/../whoareyou/%s.%s.%s.txt", gimme_dir->lock_dir, gimmeGetUserName(), getComputerName(), getLocalIP());
	if (0!=serverSideFileTouch(fn)) {
		sprintf_s(SAFESTR(fn), "%s/../../whoareyou/%s.%s.%s.txt", gimme_dir->lock_dir, gimmeGetUserName(), getComputerName(), getLocalIP());
		if (0!=serverSideFileTouch(fn)) {
			if (fileIsUsingDevData()) {
				gimmeLog(LOG_FATAL, "Error accesing %s\n\nEither the network is down or you have been disconnected from N:, double click on My Computer\nand then double click on N: to reconnect it.", gimme_dir->lock_dir);
			}
		}
	}
}

static FilespecMap *g_mapping;
static StashTable g_exclusions;
static StashTable g_inclusions;
typedef struct HogFileMapping {
	char *name;
	HogFile *hog_file;
} HogFileMapping;
static HogFileMapping **g_hog_file_mappings;

static void gimmeCloseAllHogFiles(void)
{
	int i;
	for (i=0; i<eaSize(&g_hog_file_mappings); i++) {
		if (g_hog_file_mappings[i]->hog_file) {
			hogFileDestroy(g_hog_file_mappings[i]->hog_file, true);
			g_hog_file_mappings[i]->hog_file = NULL;
		}
	}
}
static void gimmeFlushAllHogFiles(void)
{
	int i;
	for (i=0; i<eaSize(&g_hog_file_mappings); i++) {
		if (g_hog_file_mappings[i]->hog_file) {
			hogFileModifyFlush(g_hog_file_mappings[i]->hog_file);
		}
	}
}

static FileScanAction getHogMappingsCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char *s, *mem;
	char filename[CRYPTIC_MAX_PATH];
	HogFileMapping *hog_file_mapping;
	int mapping_index;
	if (!strEndsWith(data->name, ".txt"))
		return FSA_NO_EXPLORE_DIRECTORY;

	// Assemble implied hog filename
	hog_file_mapping = calloc(sizeof(*hog_file_mapping),1);
	strcpy(filename, dir);
	while (strEndsWith(filename, "/"))
		filename[strlen(filename)-1]=0;
	*strrchr(filename, '/')=0;
	strcat(filename, "/");
	strcat(filename, data->name);
	filename[strlen(filename) - strlen(".txt")] = 0;
	strcat(filename, ".hogg");
	hog_file_mapping->name = strdup(filename);
	mapping_index = eaPush(&g_hog_file_mappings, hog_file_mapping);

	sprintf_s(SAFESTR(filename), "%s/%s", dir, data->name);

	if (!(mem = fileAlloc(filename, 0)))
		assert(0);
	s = mem;
	while(s)
	{
		int include=mapping_index;
		int old_include=-1;
		char *nextline=NULL;
		// Find next line
		char *cr = strchr(s, '\r');
		char *lf = strchr(s, '\n');
		if (cr && lf)
			nextline = MIN(cr, lf);
		else if (cr)
			nextline = cr;
		else if (lf)
			nextline = lf;
		if (nextline) {
			*nextline = 0;
			nextline++;
			while (*nextline == '\r' || *nextline == '\n') {
				nextline++;
			}
		}
		if (*s=='#' || !*s) {
			s = nextline;
			continue;
		}
		if (s[0]=='!') {
			include = -1;
			s++;
		}
		// Add filespec
		// Record excludes separately for debugging
		// If already exists, and this is an exclude, do nothing
		// If already exists, and this is not an exclude, assert the original was an exclude
		if (include!=-1) {
			// Including
			if (filespecMapGetExactInt(g_mapping, s, &old_include)) {
				assertmsg(old_include==-1, "Two files both including the same filespec");
				filespecMapAddInt(g_mapping, s, include);
			} else {
				// New
				filespecMapAddInt(g_mapping, s, include);
			}
			stashAddInt(g_inclusions, s, mapping_index, true);
		} else { // include == -1
			// Excluding
			if (filespecMapGetExactInt(g_mapping, s, &old_include)) {
				if (!strEndsWith(s, ".bak")) {
					assertmsg(old_include!=-1, "Two files both excluding the same filespec?");
				}
			} else {
				// New
				filespecMapAddInt(g_mapping, s, include);
			}
			stashAddInt(g_exclusions, s, mapping_index, true);
		}
		s = nextline;
	}
	fileFree(mem);
	return FSA_NO_EXPLORE_DIRECTORY; // Do not recurse
}

static int verifyHogExclusions(StashElement element)
{
	char *key;
	key = stashElementGetStringKey(element);
	if (stashFindElement(g_inclusions, key, NULL)) {
		// In include, good!
	} else {
		// Was not included anywhere, orphaned!
		if (!(stricmp(key, "piggs/*")==0 ||
			stricmp(key, "*.bak")==0))
		{
			printf("HoggExclusions: One file excludes something that no other includes!");
		}
	}
	return 1;
}

static int verifyHogInclusions(StashElement element)
{
	char *key;
	key = stashElementGetStringKey(element);
	if (stashFindElement(g_exclusions, key, NULL)) {
		// In exclude list, good!
	} else {
		// Was included, but not specifically excluded, it will be caught in two specs!!
		if (!(stricmp(key, "*")==0))
		{
			assertmsg(0, "Two files include the same path!");
		}
	}
	return 1;
}


static FilespecMap *getHogMappings(const char *localdir, GimmeDir *gimme_dir)
{
	char path[CRYPTIC_MAX_PATH];
	if (!gimmeShouldUseHogs(gimme_dir))
		return NULL;
	g_mapping = filespecMapCreate();
	g_exclusions = stashTableCreateWithStringKeys(10, StashDefault|StashDeepCopyKeys_NeverRelease);
	g_inclusions = stashTableCreateWithStringKeys(10, StashDefault|StashDeepCopyKeys_NeverRelease);
	sprintf_s(SAFESTR(path), "%s/piggs/hoggs", localdir);
	forwardSlashes(path);
	fileScanDirRecurseEx(path, getHogMappingsCallback, NULL);
	// Verify no lone excludes remain in the mapping
	stashForEachElement(g_exclusions, verifyHogExclusions);
	// Also verify for every include, there is an exclude, other than the top level
	if (stashFindElement(g_inclusions, "*", NULL)) { // Only valid if we have a generic data.hogg catching *
		stashForEachElement(g_inclusions, verifyHogInclusions);
	}
	return g_mapping;
}

// returns 0 on failure, 1 on success, 2 if config not found and S.C. disabled
int gimmeLoadConfig(void) {
	char		*s,*mem,*args[100],*mem2;
	int			count,count2;
	char		*config_file=gimme_state.config_file1?gimme_state.config_file1:"C:/gimmecfg.txt";
	GimmeDir *gimme_dir;

	static int loaded=0;
	if (loaded) return loaded;

	//threadedFileCopyInit(16);

	loaded=2; // So that recursive calls via the textparser don't get stuck!

	if (!(mem = gimmeFileAllocCached(config_file, 0))) {
		config_file=config_file2;
		if (!(mem = gimmeFileAllocCached(config_file2, 0))) {
			// Allow no config -> no S.C.
			if (fileIsUsingDevData()) {
				gimmeLog(LOG_FATAL, "Error opening config file (%s)\nWill proceed without source control\n\nEither the network is down or you have been disconnected from N:, double click on My Computer\nand then double click on N: to reconnect it.", config_file2);
			} else {
				gimme_logging_enabled = 0;
				printf("No source control found, proceeding without it.\n");
			}
			loaded=2;
			return loaded;
		}
	}

	eaSetSize(&eaGimmeDirs, 0);
	gimme_dir = calloc(sizeof(GimmeDir),1);
	gimme_dir->active_branch=-1;

	s = mem;
	while(s && (count=tokenize_line(s,args,&s))>=0)
	{
		if (count == 0 || args[0][0]=='#')
			continue;
		if (gimme_dir->local_dir[0]==0) {
			// First line parsed
			strcpy(gimme_dir->local_dir, args[0]);
			forwardSlashes(gimme_dir->local_dir);
		} else {
			// Second line parsed
			strcpy(gimme_dir->lock_dir, args[0]);
			forwardSlashes(gimme_dir->lock_dir);
			// Get approval date
			gimme_dir->dateOfApproval=0;

			if (!gimme_state.justDoingPurge) {
				// Get active branch number
				gimmeSetBranchConfigRoot(gimme_dir->lock_dir);
				gimme_dir->active_branch = gimmeGetBranchNumber(gimme_dir->local_dir);

				mem2 = gimmeFileAllocCached(getApprovedFile(gimme_dir), &count2);
				if (mem2) {
					sscanf(mem2, "%d", &gimme_dir->dateOfApproval);
					free(mem2);
				} else {
					gimme_dir->dateOfApproval = 0;
				}

				// Get hog file mappings
				gimme_dir->hogMapping = getHogMappings(gimme_dir->local_dir, gimme_dir);
			}

			// Store and make another one
			eaPush(&eaGimmeDirs, gimme_dir);
			gimme_dir = calloc(sizeof(GimmeDir),1);
			gimme_dir->active_branch=-1;
		}
	}

	// Before setting loaded flag
	if (!gimme_state.justDoingPurge)
	{ // Prime the network cache
		int i;
		for (i=0; i<eaSize(&eaGimmeDirs); i++) {
			gimmeQueryCoreBranchNumForDir(eaGimmeDirs[i]->local_dir);
		}
		gimmeQueryGroupList();
	}

	if (gimme_dir->lock_dir[0]!=gimme_dir->local_dir[0]) {
		gimmeLog(LOG_FATAL, "Error in config file: odd number of non-comment lines");
		loaded = 2;
	} else {
		loaded = 1;
		if (gimme_state.updateRemoteFileCache) // Only for command-line gimme
			gimmeLogWhoAmI();
	}
	free(gimme_dir);
	free(mem);

	return loaded;
}

// "touches" a file, but with the server's time/datestamp
// Returns 0 on success
static int serverSideFileTouch(const char *filename) {
	int file_handle;
	char c;
	int ret;
	if (ret=_wsopen_s_UTF8(&file_handle, filename, _O_RDWR | _O_CREAT | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE ))
		return ret;
	_lseek(file_handle, 0, SEEK_SET);
	if(_read(file_handle, &c, 1)>0) {
		_lseek(file_handle, 0, SEEK_SET);
		_write(file_handle, &c, 1);
	} else { // 0-byte file
		_lseek(file_handle, 0, SEEK_SET);
		c=0;
		_write(file_handle, &c, 1);
		_chsize(file_handle, 0);
	}
	_close(file_handle);
	return 0;
}

// "touches" a file, updating it's modified date, returning the date used
FWStatType fileTouch(const char *filename, const char *reffile) {
	FWStatType sbuf;
	struct _utimbuf utb;

	if (pststat(filename, &sbuf)!=0) { // file not there
		if (reffile==NULL) {
			// don't worry about it, it'll be created
		} else {
			return sbuf;
		}
	}

	if (gimme_state.simulate) return sbuf;

	if (reffile==NULL) { // current time
		serverSideFileTouch(filename);
		pststat(filename, &sbuf); // get fileTouch's time to return
	} else {
		if (pststat(reffile, &sbuf)!=0) {
			gimmeLog(LOG_FATAL, "Internal Error, could not get time from reference file!");
		} else {
			utb.actime = sbuf.st_atime; // Need to set this to something, otherwise it'll fail
			utb.modtime = sbuf.st_mtime;
			_chmod(filename, _S_IWRITE | _S_IREAD);
			if (pstutime(filename, &utb)!=0) {
				gimmeLog(LOG_WARN_LIGHT, "Internal Error calling utime");   // This will occur when we try and set the modification time on a file that is currently being executed
			}
		}
	}
	return sbuf;
}

__time32_t getServerTime(GimmeDir *gimme_dir) {
	char fn[CRYPTIC_MAX_PATH];
	char dbPath[CRYPTIC_MAX_PATH];
	char *c;
	strcpy(dbPath, gimme_dir->lock_dir);
	c = strstri(dbPath, "revisions");
	c = strchr(c, '/');
	if ( c ) *c = 0;
	sprintf_s(SAFESTR(fn), "%s/timestamp/%s.txt", dbPath, gimmeGetUserName());
	if (!fileExists(fn))
		mkdirtree(fn);
	//sprintf_s(SAFESTR(fn), "%s/../timestamp/%s.txt", gimme_dir->lock_dir, gimmeGetUserName());
	return fileTouch(fn, NULL).st_mtime;
}

int deleteOldVersionsRecur(GimmeDir *gimme_dir, const char *relpath, int branch_num)
{
	int highver, lowver;
	int approvedver;
	char lockdir[CRYPTIC_MAX_PATH];
	char dbname[CRYPTIC_MAX_PATH];
	RuleSet *rs;
	GimmeNode *node;
	int ret;

	if (branch_num < 0) return NO_ERROR;

	//classify file and check number of revisions here
	if (NO_ERROR!=(ret=getRules(gimme_dir))) {
		return ret;
	}
	rs = getRuleSet(gimme_dir, relpath);
	if (rs->num_revisions <= 0 && !gimmeIsBinFile(gimme_dir, relpath)) {
		return NO_ERROR;
	}

	sprintf_s(SAFESTR(lockdir),"%s_versions/", relpath);

	highver = getHighestVersion(gimme_dir, lockdir, &node, branch_num, relpath);
	if (highver >=0 && node->branch==branch_num) {
		int num_revisions = rs->num_revisions;
		// check for old revisions that need to be deleted
		approvedver = getApprovedRevision(gimme_dir, lockdir, &node, branch_num, relpath);
		if (num_revisions==0) num_revisions=1; // Make sure to keep at least one copy!
		if (num_revisions==-3) num_revisions=2; // special case for bin files
		while ( // Delete old copies while
			(lowver=getLowestVersion(gimme_dir, lockdir, &node, approvedver, branch_num, relpath)) <= highver-num_revisions && // it is older than the number of revisions to keep,
			lowver!=-1 && // The version exists,
			num_revisions>0) // We're not instructed to keep all versions,
		{
			sprintf_s(SAFESTR(dbname), "%s%s", gimme_dir->lock_dir, gimmeNodeGetFullPath(node));
			if (!gimme_state.simulate) {
				_chmod(dbname, _S_IREAD | _S_IWRITE);
				GIMME_CRITICAL_START;
				{
					if (remove(dbname) != 0 && fileExists(dbname)) {
						gimmeLog(LOG_WARN_HEAVY, "Error deleting old version : %s", dbname);
					} else {
						gimmeLog(LOG_TOFILEONLY, "Deleted old version : %s", dbname);
						gimmeJournalRm(gimme_dir->database, gimmeNodeGetFullPath(node));
					}
				}
				GIMME_CRITICAL_END;
			}
			// Also delete from database in memory
			gimmeNodeDeleteFromTree(&gimme_dir->database->root->contents, &gimme_dir->database->root->contents_tail, node);
			gimme_state.num_pruned_versions++;
		}
	}
	return deleteOldVersionsRecur(gimme_dir, relpath, branch_num-1);
}

// Known issue: Upon checkin on branch 0, it may delete the approved version of branch 1
static int deleteOldVersions(GimmeDir *gimme_dir, const char *relpath) {
	return deleteOldVersionsRecur(gimme_dir, relpath, gimme_dir->active_branch);
}

void makeLocalNameFromRel_s(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size)
{
	sprintf_s(SAFESTR2(buf), "%s%s", gimme_dir->local_dir, relpath);
}

int gimmeMakeLocalNameFromRel_s(int gimme_db_num, const char *relpath, char *buf, size_t buf_size)
{
	char temp[CRYPTIC_MAX_PATH];
	gimmeLoadConfig();
	if (gimme_db_num >= eaSize(&eaGimmeDirs)) {
		strcpy_s(buf, buf_size, relpath);
		return GIMME_ERROR_NO_SC;
	}
	strcpy(temp, relpath); // in case relpath==buf
	makeLocalNameFromRel_s(eaGimmeDirs[gimme_db_num], temp, buf, buf_size);
	return NO_ERROR;
}

// returns actual revision number or -1
int makeDBName(GimmeDir *gimme_dir, const char *relpath, int revision, GimmeNode **ret_node)
{
	char lockdir[CRYPTIC_MAX_PATH];
	sprintf_s(SAFESTR(lockdir),"%s_versions/", relpath);
	*ret_node = NULL; // zero it in case it's not in the database

	if (revision==REV_BLEEDINGEDGE || revision==REV_BLEEDINGEDGE_FORCE || revision==REV_BLEEDINGEDGE_REVERT) {
		revision = getHighestVersion(gimme_dir, lockdir, ret_node, 
			gimme_dir->active_branch, relpath );
	} else if (revision==REV_BYTIME) {
		revision = getRevisionByTime(gimme_dir, lockdir, ret_node, 
			gimme_dir->active_branch, relpath );
	} else {
		revision = getSpecificRevision(gimme_dir, lockdir, ret_node, revision, 
			gimme_dir->active_branch, relpath );
	}
	return revision;
}

int makeDBNameQuick(GimmeDir *gimme_dir, GimmeNode *versionNode, int revision, GimmeNode **ret_node, const char * relpath)
{
	*ret_node = NULL; // zero it in case it's not in the database

	if (versionNode==NULL)
		return -1;

	if (revision==REV_BLEEDINGEDGE || revision==REV_BLEEDINGEDGE_FORCE || revision==REV_BLEEDINGEDGE_REVERT) {
		revision = getHighestVersionFromNode(gimme_dir, versionNode, ret_node, 
			gimme_dir->active_branch, relpath );
	} else if (revision==REV_BYTIME) {
		revision = getRevisionByTimeFromNode(gimme_dir, versionNode, ret_node, 
			gimme_dir->active_branch, relpath );
	} else {
		revision = getSpecificRevisionFromNode(gimme_dir, versionNode, ret_node, revision, 
			gimme_dir->active_branch, relpath );
	}
	return revision;
}

// Copy a file and may sync the timestamp (if dest is network drive, timestamp may not be synced), overwriting read-only files
int copyFile(char *src,char *dst)
{
	int ret;
	char buf[CRYPTIC_MAX_PATH*2+20];
	int is_exe = strEndsWith(dst, ".exe") || strEndsWith(dst, ".dll") || strEndsWith(dst, ".pdb");
	int len = 0;

	if (len = strlen(src) >= CRYPTIC_MAX_PATH-1 || strlen(dst) >= CRYPTIC_MAX_PATH-1)
	{
		if ( len >= CRYPTIC_MAX_PATH-1 )
			gimmeLog(LOG_WARN_LIGHT, "\"%s\" filename too long.\n", src);
		else
			gimmeLog(LOG_WARN_LIGHT, "\"%s\" filename too long.\n", dst);
		return GIMME_ERROR_COPY;
	}

	if (gimme_state.simulate) return NO_ERROR;

	_chmod(dst, _S_IREAD | _S_IWRITE);
	if (gimmeGetOption("Verify")) {
		sprintf_s(SAFESTR(buf), "echo F | xcopy /V /Q /Y /I \"%s\" \"%s\">nul", backSlashes(src), backSlashes(dst));
		if (is_exe) {
			strcat(buf, " 2>nul");
		}
		_flushall();
		ret = system(buf);
		if (ret!=0 && is_exe) {
			// Try again!
			gimmeLog(LOG_WARN_LIGHT, "Attempt to update an executable (%s) while running, renaming old .exe to .bak and trying again...", getFileName(dst));
			fileRenameToBak(dst);
			_flushall();
			ret = system(buf);
		} else if (ret!=0 && gimmeGetOption("Verify")) {
			// Try again!
			ret = system(buf);
		}
	} else {
		//sprintf_s(SAFESTR(buf), "copy \"%s\" \"%s\">nul", backSlashes(src), backSlashes(dst));
		_flushall();
		if ( (ret = !CopyFile_UTF8(backSlashes(src), backSlashes(dst), FALSE)) && is_exe )
		{
			gimmeLog(LOG_WARN_LIGHT, "Attempt to update an executable (%s) while running, renaming old .exe to .bak and trying again...", getFileName(dst));
			fileRenameToBak(dst);
			_flushall();
			ret = !CopyFile_UTF8(src, dst, FALSE);
		}
	}
	
	if (ret!=0) {
		ret = GIMME_ERROR_COPY;
	} else {
		fileTouch(dst, src);
		// set the checkin time as the file created time
		//pstSetFileCreatedTime( dst, time(NULL));
	}
	return ret;
}

// Takes ownership of data (and NULLs out parent's pointer)
// Pass null data for a delete
int updateHogFileAfterCopy(GimmeDir *gimme_dir, char *localname, U32 timestamp, U8 **file_data, U32 file_size)
{
	char *relpath;
	int index;
	int ret = NO_ERROR;
	if (!gimme_dir->hogMapping)
		return ret;
	timestamp = gimmeTimeToUTC(timestamp);
	forwardSlashes(localname);
	relpath = findRelPath(localname, gimme_dir);
	while (relpath[0]=='/')
		relpath++;
	if (filespecMapGetInt(gimme_dir->hogMapping, relpath, &index)) {
		// Found something
		if (index==-1) {
			// Excluded!
		} else {
			// Copy to this hog!
			HogFileMapping *mapping = g_hog_file_mappings[index];
			bool bNewlyCreated;
			if (!mapping->hog_file) {
				mapping->hog_file = hogFileRead(mapping->name, &bNewlyCreated, PIGERR_ASSERT, &ret, HOG_DEFAULT);
				if (!mapping->hog_file) {
					gimmeLog(LOG_FATAL, "Error loading hogg file: %s, error code: %d", mapping->name, ret);
					ret = GIMME_ERROR_COPY;
				}
				if (bNewlyCreated) {
					int gimme_dir_num = eaFind(&eaGimmeDirs, gimme_dir);
					gimmeGettingLatestOn(gimme_dir_num); // created a new hogg, need to run the update script!
					gimmeForceHoggUpdate(gimme_dir_num);
				}
			}
			if (mapping->hog_file) {
				// Add/update
				U8 *data = NULL;
				if (file_data && *file_data) {
					data = *file_data; // Take ownership of data
					*file_data = NULL;
				}
				ret = hogFileModifyUpdateNamed(mapping->hog_file, relpath, data, file_size, timestamp, NULL);
				// Flush done on program exit or return from gimmeDoOperation()
				//if (ret==0) {
				//	ret = hogFileModifyFlush(mapping->hog_file);
				//}
				if (ret) {
					gimmeLog(LOG_FATAL, "Error updating hogg file: %s, with file: %s, error code: %d", mapping->name, relpath, ret);
					ret = GIMME_ERROR_COPY;
				}
			}
		}
	} else {
		// No hog file mappings at all, perhaps?
		// Could be sparse hogg files (e.g. C:\Core\
		//assert(filespecMapGetNumElements(gimme_dir->hogMapping)==0);
	}
	return ret;
}

int copyFileToLocal(GimmeDir *gimme_dir, char *dbname, const char *localname_const, U32 timestamp, bool make_readonly)
{
	char localname[1024];
	int ret=NO_ERROR;
	bool do_on_disk = true;
	bool do_into_hogs = gimmeShouldUseHogs(gimme_dir);
	Strncpyt(localname, localname_const);
	// Copy file on disk, set timestamp
	if (do_on_disk) {
		mkdirtree(localname);
		ret = copyFile(dbname, localname);
		if (ret==NO_ERROR) {
			// change timestamp
			struct _utimbuf utb;
			utb.actime = timestamp;
			utb.modtime = timestamp;
			pstutime(localname, &utb);

			// set the file create time to the check in time
			//pstSetFileCreatedTime(localname, checkintime);
		} else {
			// Error copying file, delete local
			remove(localname);
		}
		// Make read only
		if (make_readonly) {
			_chmod(localname, _S_IREAD);
		} else {
			_chmod(localname, _S_IREAD | _S_IWRITE);
		}
	}
	if (ret==NO_ERROR && do_into_hogs)
	{
		U32 size;
		U8 *data;
		data = fileAlloc(localname, &size);
		if (!data) {
			gimmeLog(LOG_FATAL, "Error loading file: %s", localname);
		} else {
			if (ret = updateHogFileAfterCopy(gimme_dir, localname, timestamp, &data, size)) {
				// Error!  Return error code
			}
			SAFE_FREE(data);
		}
	}
	return ret;
}


// Mark as deleted, and keep the last version that was added to the database (under the name .deleted)
static int markAsDeleted(GimmeDir *gimme_dir, const char *relpath) {
	int highver;
	char lockdir[CRYPTIC_MAX_PATH];
	char dbname[CRYPTIC_MAX_PATH];
	char olddbname[CRYPTIC_MAX_PATH];
	char relDelPath[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	FWStatType sbuf;
	int ret;
	int branch = getFreezeBranch(gimme_dir, relpath);

	if (branch == -1)
		branch = gimme_dir->active_branch;

	sprintf_s(SAFESTR(lockdir),"%s_versions/", relpath);
	highver = getHighestVersion(gimme_dir, lockdir, &node, gimme_dir->active_branch, relpath) + 1;
	if (node==NULL) {
		// internal inconsitency detected, but this is possible because of a bug in old versions, just let it go
		return NO_ERROR;
	}
	if (gimmeFileIsDeletedFile(node->name)) {
		//if (!gimme_state.nowarn) gimmeLog(LOG_WARN_LIGHT, "Warning: Attempt to delete a file already marked as deleted; ignoring.");
	}
	if (branch==0) {
		// Old style
		sprintf_s(SAFESTR(relDelPath), "%s%s_v#%d_%s.deleted", lockdir, getFileName((char*)relpath), highver, gimmeGetUserName());
	} else {
		// New style
		sprintf_s(SAFESTR(relDelPath), "%s%s_vb#%d_v#%d_%s.deleted", lockdir, getFileName((char*)relpath), branch, highver, gimmeGetUserName());
	}
	if ( strlen(relDelPath) + strlen(gimme_dir->lock_dir) >= MAX_PATH )
	{
		// some hacky pointer arithematic to chop off the 'eted' and make it just '.del'
		char *c = strstri(relDelPath, ".deleted");
		c += 4;
		*c = 0;
	}
	sprintf_s(SAFESTR(olddbname), "%s%s", gimme_dir->lock_dir, gimmeNodeGetFullPath(node));
	sprintf_s(SAFESTR(dbname), "%s%s", gimme_dir->lock_dir, relDelPath);

	if (gimme_state.simulate) return NO_ERROR;

	mkdirtree(dbname);
	GIMME_CRITICAL_START;
	{
		if (NO_ERROR!=(ret=copyFile(olddbname,dbname))) {
			//remove(dbname);
			//GIMME_CRITICAL_END;
			//return ret;
			// An error occured, but we still want the file marked as deleted, fall through to the touch
		}
		sbuf = fileTouch(dbname, NULL);

		gimmeJournalAdd(gimme_dir->database, relDelPath, sbuf.st_size, sbuf.st_mtime, NULL);
	}
	GIMME_CRITICAL_END;
	gimmeNodeAdd(&gimme_dir->database->root->contents, &gimme_dir->database->root->contents_tail, NULL, relDelPath, NULL, sbuf.st_mtime, sbuf.st_size, getServerTime(gimme_dir));

	return deleteOldVersions(gimme_dir, relpath);
}

typedef struct CopyCallbackData {
	__time32_t timestamp;
	int make_readonly;
	bool do_on_disk;
	bool do_into_hogs;
	GimmeDir *gimme_dir;
} CopyCallbackData;

static void copyCompletedCallback(TFCRequest* request, int success, size_t total_bytes)
{
	CopyCallbackData *userData = (CopyCallbackData*)request->userData;
	if (!gimme_state.simulate) {
		if (success) {
			assert(request->data);
			if (userData->do_on_disk) {
				// change timestamp
				struct _utimbuf utb;
				utb.actime = userData->timestamp;
				utb.modtime = userData->timestamp;
				pstutime(request->dst, &utb);
			}
			if (userData->do_into_hogs) {
				updateHogFileAfterCopy(userData->gimme_dir, request->dst, userData->timestamp, &request->data, (U32)request->data_size);
			}
			gimmeLog(LOG_INFO, "%s: Successfully replaced", request->dst);
		} else {
			// Error copying file, delete local
			if (userData->do_on_disk) {
				remove(request->dst);
			}
			gimmeLog(LOG_WARN_HEAVY, "%s: Copy FAILED", request->dst);
		}
		// Make read only
		if (userData->make_readonly) {
			_chmod(request->dst, _S_IREAD);
		} else {
			_chmod(request->dst, _S_IREAD | _S_IWRITE);
		}
	} else {
		gimmeLog(LOG_INFO, "%s: (would be replaced)", request->dst);
	}
	free(request->userData);
}

static void copyProcessCallback(TFCRequest* request, size_t bytes, size_t total)
{
	// todo
}

static int copyFileFromDBAsync(GimmeDir *gimme_dir, const char *relpath, int revision, int quiet, int make_readonly) {
	char dbname[CRYPTIC_MAX_PATH], localname[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	TFCRequest *request;
	CopyCallbackData *userData;
	bool do_on_disk = true;
	bool do_into_hogs = gimmeShouldUseHogs(gimme_dir);

	// Get the source file to copy from
	if (0>makeDBName(gimme_dir, relpath, revision, &node)) {
		if (!quiet) gimmeLog(LOG_FATAL, "No existing version found in database!");
		return GIMME_ERROR_NOT_IN_DB;
	}
	// We don't care about this case anymore, the checks are higher up, and we want to be able to get at files that were deleted
	//if (strEndsWith(node->name, ".deleted")) {
	//	if (!quiet) gimmeLog(LOG_FATAL, "File has been deleted from the database, unable to checkout.");
	//	return GIMME_ERROR_NOT_IN_DB;
	//}
	makeLocalNameFromRel(gimme_dir, relpath, localname);
	makeDBNameFromNode(gimme_dir, node, dbname);

	userData = calloc(sizeof(*userData),1);
	userData->make_readonly = make_readonly;
	userData->timestamp = node->timestamp;
	userData->do_into_hogs = do_into_hogs;
	userData->do_on_disk = do_on_disk;
	userData->gimme_dir = gimme_dir;
	request = createTFCRequestEx(dbname, localname, do_on_disk?TFC_NORMAL:TFC_DO_NOT_WRITE, copyProcessCallback, copyCompletedCallback, userData);

	gimme_state.bytes_transfered += node->size;

	if (gimme_state.simulate) {
		copyCompletedCallback(request, 1, 1);
		return NO_ERROR;
	}

	if (do_on_disk)
		mkdirtree(localname);
	threadedFileCopyStartCopy(request);
	return NO_ERROR;
}


static int copyFileFromDB(GimmeDir *gimme_dir, const char *relpath, int revision, int quiet, int make_readonly) {
	char dbname[CRYPTIC_MAX_PATH], localname[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	int ret;

	// Get the source file to copy from
	if (0>makeDBName(gimme_dir, relpath, revision, &node)) {
		if (!quiet) gimmeLog(LOG_FATAL, "No existing version found in database!");
		return GIMME_ERROR_NOT_IN_DB;
	}
	// We don't care about this case anymore, the checks are higher up, and we want to be able to get at files that were deleted
	//if (strEndsWith(node->name, ".deleted")) {
	//	if (!quiet) gimmeLog(LOG_FATAL, "File has been deleted from the database, unable to checkout.");
	//	return GIMME_ERROR_NOT_IN_DB;
	//}
	makeLocalNameFromRel(gimme_dir, relpath, localname);
	makeDBNameFromNode(gimme_dir, node, dbname);

	if (gimme_state.simulate) return NO_ERROR;

	GIMME_CRITICAL_START;
	ret = copyFileToLocal(gimme_dir, dbname, localname, node->timestamp, make_readonly);
	GIMME_CRITICAL_END;
	return ret;
}

static int copyFileToDB(GimmeDir *gimme_dir, const char *relpath) {
	int highver;
	char lockdir[CRYPTIC_MAX_PATH];
	char dbname[CRYPTIC_MAX_PATH], reldbname[CRYPTIC_MAX_PATH];
	char localname[CRYPTIC_MAX_PATH];
	FWStatType sbuf;
	int freeze_branch = -1;
	bool do_into_hogs = gimmeShouldUseHogs(gimme_dir);
	int ret;

	sprintf_s(SAFESTR(lockdir),"%s_versions/", relpath);			
	highver = getHighestVersion(gimme_dir, lockdir, NULL, gimme_dir->active_branch, relpath) + 1;
	// is the file frozen on a branch?
	freeze_branch = getFreezeBranch(gimme_dir, relpath);

	if ((freeze_branch == -1 && gimme_dir->active_branch==0) || freeze_branch == 0) {
		sprintf_s(SAFESTR(reldbname),"%s%s_v#%d_%s.txt", lockdir, getFileName((char*)relpath), highver, gimmeGetUserName());
	} else {
		sprintf_s(SAFESTR(reldbname),"%s%s_vb#%d_v#%d_%s.txt", lockdir, getFileName((char*)relpath), 
			freeze_branch == -1 ? gimme_dir->active_branch : freeze_branch, highver, gimmeGetUserName());
	}

	sprintf_s(SAFESTR(dbname), "%s%s", gimme_dir->lock_dir, reldbname);
	makeLocalNameFromRel(gimme_dir, relpath, localname);

	if (gimme_state.simulate) return NO_ERROR;

	mkdirtree(dbname);
	GIMME_CRITICAL_START;
	{
		if (NO_ERROR!=(ret=copyFile(localname,dbname))) {
			remove(dbname);
			GIMME_CRITICAL_END;
			return ret;
		}
		// do not re-timestamp	fileTouch(dbname, NULL);
		// Now, update the local timestamp to be the same
		pststat(localname, &sbuf);
		if (!gimmeIsSambaDrive(dbname)) {
			struct _utimbuf utb = {0};
			// Adjust the times to match how things used to work on old samba
			utb.actime = sbuf.st_atime; // Need to set this to something, otherwise it'll fail
			utb.modtime = sbuf.st_mtime + gimmeGetTimeAdjust();
			_chmod(dbname, _S_IWRITE | _S_IREAD);
			if (pstutime(dbname, &utb)!=0) {
				gimmeLog(LOG_WARN_LIGHT, "Internal Error calling utime");   // This will occur when we try and set the modification time on a file that is currently being executed
			}
		}
		_chmod(dbname, _S_IREAD); // Make read-only so people don't accidentally modify them - doesn't seem to work.

		gimmeJournalAdd(gimme_dir->database, reldbname, sbuf.st_size, sbuf.st_mtime, NULL);
	}
	GIMME_CRITICAL_END;
	if (do_into_hogs) {
		U32 size;
		U8 *data;
		data = fileAlloc(localname, &size);
		// Update hogg file so it matches what's checked in
		updateHogFileAfterCopy(gimme_dir, localname, sbuf.st_mtime, &data, size);
	}

	// Add to the database (in memory)
	gimmeNodeAdd(&gimme_dir->database->root->contents, &gimme_dir->database->root->contents_tail, NULL, reldbname, NULL, sbuf.st_mtime, sbuf.st_size, getServerTime(gimme_dir));

	return deleteOldVersions(gimme_dir, relpath);
}

static int getRules(GimmeDir *gimme_dir) {
	char		*s,*mem,*args[100];
	int			count;
	char		rule_file[CRYPTIC_MAX_PATH]="";

	// Do not open the rules file on subsequent calls
	if (eaSize(&gimme_dir->eaRules)) {
		return NO_ERROR;
	}

	//loadFreezeFiles( gimme_dir->lock_dir );

	sprintf_s(SAFESTR(rule_file), FORMAT_OK(rule_file_src), gimme_dir->lock_dir);
	if (!(mem = gimmeFileAllocCached(rule_file, 0))) {
		gimmeLog(LOG_FATAL, "Error opening rulesfile (%s)", rule_file);
		return GIMME_ERROR_RULESFILE;
	}

	s = mem;
	while(s && (count=tokenize_line(s,args,&s)))
	{
		RuleSet *rs;
		if (count != 2 || args[0][0]=='#')
			continue;
		rs = calloc(sizeof(RuleSet),1);
		strcpy(rs->filespec, args[0]);
		rs->num_revisions = atoi(args[1]);
		eaPush(&gimme_dir->eaRules, rs);
	}
	free(mem);
	return NO_ERROR;
}

static RuleSet *getRuleSet(GimmeDir *gimme_dir, const char *relpath) {
	int i;
	assert(eaSize(&gimme_dir->eaRules)>0 && "No rulesets defined or error reading rulesets!");
	for (i=0; i<eaSize(&gimme_dir->eaRules)-1 && !simpleMatch(gimme_dir->eaRules[i]->filespec, relpath); i++);
	return gimme_dir->eaRules[i];
}

int gimmeAddFilespec(char *filespec, int exclude)
{
	if (gimme_state.num_filespecs >= GIMME_MAX_FILESPECS)
	{
		gimmeLog(LOG_WARN_HEAVY, "Only %d filespecs allowed.  Ignoring %s", GIMME_MAX_FILESPECS, filespec);
		return 0;
	}
	strcpy(gimme_state.filespec[gimme_state.num_filespecs], filespec);
	gimme_state.filespec_mode[gimme_state.num_filespecs++] = exclude;
	return 1;
}

ExclusionType gimmeCheckExclusion(GimmeDir *gimme_dir, const char *relpath) {
	int ret, i;
	// Check for exclusions
	if (getFileName((char*)relpath)[0]=='.') return ET_EXCLUDED;
	if (NO_ERROR!=(ret=getRules(gimme_dir))) return ET_ERROR+ret;
	if (getRuleSet(gimme_dir, relpath)->num_revisions==-2) return ET_EXCLUDED;
	for (i = 0; i < gimme_state.num_filespecs; i++){
		if (gimme_state.filespec[i][0]==0) continue;
		if (gimme_state.filespec_mode[i]==1) { // exclude
			if (simpleMatch(gimme_state.filespec[i], relpath))
				return ET_NOTINFILESPEC;
		} else {
			if (!simpleMatch(gimme_state.filespec[i], relpath))
				return ET_NOTINFILESPEC;
		}
	}
	return ET_OK;
}

bool gimmeIsBinFile(GimmeDir *gimme_dir, const char *relpath) {
	if (eaSize(&gimme_dir->eaRules)<=0) getRules(gimme_dir);
	return getRuleSet(gimme_dir, relpath)->num_revisions==-3;
}

static bool isslashornull(char c) {
	return c=='/' || c=='\\' || c==0;
}

GimmeDir *findGimmeDir(const char *fullpath)
{
	int		i;

	gimmeLoadConfig();
	for(i=0;i<eaSize(&eaGimmeDirs);i++)
	{
		if (strnicmp(eaGimmeDirs[i]->local_dir,fullpath,strlen(eaGimmeDirs[i]->local_dir))==0 && isslashornull(fullpath[strlen(eaGimmeDirs[i]->local_dir)])) {
			gimmeSetBranchConfigRoot(eaGimmeDirs[i]->lock_dir);
			return eaGimmeDirs[i];
		}
	}
	return NULL;
}

char *findRelPath(char *fname, GimmeDir *gimme_dir) {
	// Must be a full path to start with
	if (fname[1]!=':' && (fname[0]!='/' && fname[1]!='/') && (fname[0]!='\\' && fname[1]!='\\')) { 
		return fname;
	}
	if (gimme_dir==NULL)
		gimme_dir = findGimmeDir(fname);
	if (strnicmp(fname, gimme_dir->local_dir, strlen(gimme_dir->local_dir))==0) {
		return fname+strlen(gimme_dir->local_dir);
	}
	if (strnicmp(fname, gimme_dir->lock_dir, strlen(gimme_dir->lock_dir))==0) {
		return fname+strlen(gimme_dir->lock_dir);
	}
	assert(false);
	return NULL;
}

// Makes the relative path to the lock
void makeLockName(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch)
{
	int frozen_branch = getFreezeBranch( gimme_dir, relpath );

	// if we are frozen on a branch and its not the same as the branch we are trying to use,
	// set the branch to the frozen branch
	if ( frozen_branch != -1 && frozen_branch != branch )
		branch = frozen_branch;

	// Don't use sprintf, since relpath and buf might be the same buffer
	if (branch==0) {
		strcpy_s(buf, buf_size, relpath);
		strcat_s(buf, buf_size, ".lock");
	} else {
		strcpy_s(buf, buf_size, relpath);
		strcatf_s(buf, buf_size, "_vb#%d.lock", branch);
	}
}

// Makes the absolute path to the comment file
void makeCommentFileName(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch)
{
	int frozen_branch = getFreezeBranch( gimme_dir, relpath );
	
	assert(relpath != buf);
	
	// if we are frozen on a branch and its not the same as the branch we are trying to use,
	// set the branch to the frozen branch
	if ( frozen_branch != -1 && frozen_branch != branch )
		branch = frozen_branch;

	if (branch==0) {
		sprintf_s(SAFESTR2(buf), "%s%s.comments", gimme_dir->lock_dir, relpath);
	} else {
		sprintf_s(SAFESTR2(buf), "%s%s_vb#%d.comments", gimme_dir->lock_dir, relpath, branch);
	}
}

// Makes the absolute path to the batch info file
void makeBatchInfoFileName(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch)
{
	int frozen_branch = getFreezeBranch( gimme_dir, relpath );
	
	assert(relpath != buf);
	
	// if we are frozen on a branch and its not the same as the branch we are trying to use,
	// set the branch to the frozen branch
	if ( frozen_branch != -1 && frozen_branch != branch )
		branch = frozen_branch;

	if (branch==0) {
		sprintf_s(SAFESTR2(buf), "%s%s.batchinfo", gimme_dir->lock_dir, relpath);
	} else {
		sprintf_s(SAFESTR2(buf), "%s%s_vb#%d.batchinfo", gimme_dir->lock_dir, relpath, branch);
	}
}

void gimmeQueryClearCaches(void)
{
	gimme_state.is_file_locked_cached_path[0] = '\0';
	gimme_state.last_author_cached_path[0] = '\0';
}


// returns a pointer to the username of the locker or NULL if not locked
char *gimmeQueryIsFileLocked(const char *fname)
{
	char fname_temp[CRYPTIC_MAX_PATH], *fname2, fullpath[CRYPTIC_MAX_PATH], *relpath, buf[CRYPTIC_MAX_PATH];
	GimmeDir *gimme_dir;
	GimmeNode *node;
	char *ret=NULL;
	char fileLocateBuf[CRYPTIC_MAX_PATH];
	int freeze_branch = -1;

	strcpy(fname_temp, fname);

	fname2 = fileLocateWrite(fname_temp, fileLocateBuf);
	if (fname2==NULL) {
		makefullpath(fname_temp,fullpath);
	} else {
		makefullpath(fname2,fullpath); // This might not do anything unless people have things like "./" in their gameDataDirs
	}
	forwardSlashes(fullpath);

	if (gimme_state.is_file_locked_cached_path[0] && (timerCpuSeconds() - gimme_state.is_file_locked_cached_time) < 5 && stricmp(fullpath, gimme_state.is_file_locked_cached_path)==0)
		return gimme_state.is_file_locked_cached_result[0]?gimme_state.is_file_locked_cached_result:NULL;

	gimme_dir = findGimmeDir(fullpath);
	if (eaSize(&eaGimmeDirs)==0) {
		return NULL;
	}
	if (!gimme_dir)
	{
		gimmeLog(LOG_FATAL, "Can't find source control folder to match %s!",fullpath);
		return NULL;
	}
	forwardSlashes(fname_temp);
	gimmeDirDatabaseLoad(gimme_dir, fname_temp);
	gimmeDirDatabaseClose(gimme_dir);

	relpath = findRelPath(fullpath, gimme_dir);

	freeze_branch = getFreezeBranch(gimme_dir, relpath);
	makeLockName(gimme_dir, relpath, SAFESTR(buf), freeze_branch == -1 ? gimme_dir->active_branch : freeze_branch);
	node = gimmeNodeFind(gimme_dir->database->root->contents, buf);
	if (!node) {
		ret = NULL;
	} else {
		ret = node->lock;
	}

	gimme_state.is_file_locked_cached_time = timerCpuSeconds();
	strcpy(gimme_state.is_file_locked_cached_path, fullpath);
	strcpy(gimme_state.is_file_locked_cached_result, ret?ret:"");

	return ret;
}

int  gimmeQueryIsFileLockedByMeOrNew(const char *fname) {
	// Logic moved up to the wrapper layer
	extern int GimmeQueryIsFileLockedByMeOrNew(const char *fname);
	return GimmeQueryIsFileLockedByMeOrNew(fname);
}

static GimmeDir *findGimmeLockDir(char *fullpath)
{
	int		i;

	gimmeLoadConfig();
	for(i=0;i<eaSize(&eaGimmeDirs);i++)
	{
		if (strnicmp(eaGimmeDirs[i]->lock_dir,fullpath,strlen(eaGimmeDirs[i]->lock_dir))==0)
			return eaGimmeDirs[i];
	}
	return 0;
}

void makeDBNameFromNode_s(GimmeDir *gimme_dir, GimmeNode *node, char *buf, size_t buf_size)
{
	sprintf_s(SAFESTR2(buf), "%s/%s", gimme_dir->lock_dir, gimmeNodeGetFullPath(node));
}



char *getLocalIP(void)
{
	char hostname[80];
	static char ret[1024];
	static bool cached=false;
	struct hostent *phe;
	int i;
	WSADATA wsaData;

	if (cached) return ret;

	if (!gimme_state.no_need_to_init_wsa) {
		if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
			return NULL;
		}
		gimme_state.no_need_to_init_wsa = 1;
	}

	if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
		gimmeLog(LOG_WARN_LIGHT, "Error %d when getting local host name.", WSAGetLastError());
		strcpy(ret, "Error getting hostname");
		cached=true;
		return ret;
	}

	phe = gethostbyname(hostname);
	if (phe == 0) {
		gimmeLog(LOG_WARN_LIGHT, "Error: Bad host lookup.");
		strcpy(ret, "Error: bad host lookup");
		cached=true;
		return ret;
	}

	for (i = 0; phe->h_addr_list[i] != 0; ++i) {
		struct in_addr addr;
		memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
		strcpy(ret, inet_ntoa(addr));
		break;
	}

	if (!gimme_state.no_need_to_init_wsa) {
		WSACleanup();
	}
	cached=true;
	return ret;
}

char *getLocalHostNameAndIPs(void)
{
	char hostname[80];
	static char ret[1024];
	static bool cached=false;
	struct hostent *phe;
	int i;
	WSADATA wsaData;

	if (cached) return ret;

	if (!gimme_state.no_need_to_init_wsa) {
		if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
			return NULL;
		}
		gimme_state.no_need_to_init_wsa = 1;
	}

	if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
		gimmeLog(LOG_WARN_LIGHT, "Error %d when getting local host name.", WSAGetLastError());
		strcpy(ret, "Error getting hostname");
		cached=true;
		return ret;
	}

	phe = gethostbyname(hostname);
	if (phe == 0) {
		gimmeLog(LOG_WARN_LIGHT, "Error: Bad host lookup.");
		strcpy(ret, "Error: bad host lookup");
		cached=true;
		return ret;
	}

	sprintf_s(SAFESTR(ret), "%s\n", hostname);
	for (i = 0; phe->h_addr_list[i] != 0; ++i) {
		struct in_addr addr;
		memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
		strcat(ret, inet_ntoa(addr));
		strcat(ret, "\n");
	}

	if (!gimme_state.no_need_to_init_wsa) {
		WSACleanup();
	}
	cached=true;
	return ret;
}

int gimmeGetUserNameFromLock(char *buf, size_t buf_size, const char *lockname)
{
	int count;
	char *mem = fileAlloc(lockname, &count);
	if (mem==NULL || count==0 || strlen(mem)==0) {
		if (mem!=NULL) {
			free(mem);
			mem=NULL;
		}
		strcpy_s(buf, buf_size, "ERROR READING USERNAME FROM LOCKFILE");
		return 1;
	} else {
		*(mem + strcspn(mem, "\r\n"))=0;
		strcpy_s(buf, buf_size, mem);
		free(mem);
	}
	return 0;
}

int makeLock(GimmeDir *gimme_dir, const char *relpath)
{
char	buf[CRYPTIC_MAX_PATH];
char	lockee[1024];
char	dbname[CRYPTIC_MAX_PATH];
int		lockfile_handle=-1;
FWStatType sbuf;
int		freeze_branch = -1;

	if (gimme_state.simulate) return NO_ERROR;

	freeze_branch = getFreezeBranch(gimme_dir, relpath);
	makeLockName(gimme_dir, relpath, SAFESTR(buf), freeze_branch == -1 ? gimme_dir->active_branch : freeze_branch);
	sprintf_s(SAFESTR(dbname), "%s%s", gimme_dir->lock_dir, buf);

	GIMME_CRITICAL_START;
	{
		if ((lockfile_handle = gimmeAcquireLock(dbname)) < 0) {
			// File already exists, check to see if it's checked out by us!
			int ret = gimmeGetUserNameFromLock(SAFESTR(lockee), dbname);
			if (ret) {
				gimmeLog(LOG_FATAL, "Couldn't make lock file: %s",buf);
				GIMME_CRITICAL_END;
				return GIMME_ERROR_LOCKFILE_CREATE;
			} else {
				if (strcmp(lockee, gimmeGetUserName())==0) {
					// already checked out by us!
					if (!gimme_state.doing_sync)
						gimmeLog(LOG_WARN_HEAVY, "\"%s\"\nThe file was only partially flagged as checked out.  Perhaps you closed Gimme while checking it in or out?", relpath);
					// Fall through to code below
				} else {
					// We only get here if someone killed gimme at an inoportune time
					gimmeLog(LOG_FATAL, "\"%s\"\nSorry, you can't get this file because %s already has it checked out!",relpath, lockee);
					GIMME_CRITICAL_END;
					return GIMME_ERROR_ALREADY_CHECKEDOUT;
				}
			}
		} else {
			// succeeded
			_close(lockfile_handle);
		}

		pststat(dbname, &sbuf);
		gimmeJournalAdd(gimme_dir->database, buf, sbuf.st_size, sbuf.st_mtime, gimmeGetUserName());
	}
	GIMME_CRITICAL_END;
	gimmeNodeAdd(&gimme_dir->database->root->contents, &gimme_dir->database->root->contents_tail, NULL, buf, gimmeGetUserName(), sbuf.st_mtime, sbuf.st_size, getServerTime(gimme_dir));

	return NO_ERROR;
}

char *isLocked(GimmeDir *gimme_dir, const char *relpath)
{
	char	buf[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	int freeze_branch = -1;

	// because bin files don't get checked out
	if (gimmeIsBinFile(gimme_dir, relpath))
		return NULL;

	freeze_branch = getFreezeBranch(gimme_dir, relpath);

	makeLockName(gimme_dir, relpath, SAFESTR(buf), freeze_branch == -1 ? gimme_dir->active_branch : freeze_branch);
	node = gimmeNodeFind(gimme_dir->database->root->contents, buf);
	if (node==NULL)
		return NULL;
	if (!node->lock) {
		return "UNNAMED";
	}
	return node->lock;
}

char *isLockedQuick(GimmeDir *gimme_dir, const char *relpath, GimmeNode *versionNode)
{
	char	buf[CRYPTIC_MAX_PATH];
	char	*lockname;
	GimmeNode *node=NULL;
	int ret=-1;
	int freeze_branch = -1;

	if (!versionNode)
		return NULL;

	freeze_branch = getFreezeBranch(gimme_dir, relpath);

	makeLockName(gimme_dir, relpath, SAFESTR(buf), freeze_branch == -1 ? gimme_dir->active_branch : freeze_branch);
	lockname = getFileName(buf);
	if (stricmp(lockname, versionNode->name)>0) {
		// It's forward
		while (versionNode && (ret = stricmp(lockname, versionNode->name))>0)
			versionNode = versionNode->next;
	} else {
		// It's behind
		while (versionNode && (ret = stricmp(lockname, versionNode->name))<0)
			versionNode = versionNode->prev;
	}
	if (ret==0)
		node = versionNode;
	if (node==NULL)
		return NULL;
	if (!node->lock) {
		return "UNNAMED";
	}
	return node->lock;
}

// safety_delete is when we are deleting a lock just for safety measues.  No errors should be displayed if the file
//   is not found and it should not be logged if nothing was deleted
static int deleteLock(GimmeDir *gimme_dir, const char *relpath, bool touch_only, bool safety_delete)
{
	char	buf[CRYPTIC_MAX_PATH];
	char	dbname[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	int ret=NO_ERROR;
	int freeze_branch = -1;

	if (gimme_state.simulate) return NO_ERROR;

	freeze_branch = getFreezeBranch(gimme_dir, relpath);
	makeLockName(gimme_dir, relpath, SAFESTR(buf), freeze_branch == -1 ? gimme_dir->active_branch : freeze_branch);
	sprintf_s(SAFESTR(dbname), "%s%s", gimme_dir->lock_dir, buf);
	GIMME_CRITICAL_START;
	{
		if (touch_only) {
			FWStatType sbuf = fileTouch(dbname, NULL);
			gimmeJournalAdd(gimme_dir->database, buf, sbuf.st_size, sbuf.st_mtime, gimmeGetUserName());
		} else {
			if (!safety_delete) {
				gimmeEnsureDeleteLock(dbname);
				gimmeJournalRm(gimme_dir->database, buf);
			} else {
				if (remove(dbname)==0) {
					gimmeJournalRm(gimme_dir->database, buf);
				}
			}
		}
	}
	GIMME_CRITICAL_END;

	node = gimmeNodeFind(gimme_dir->database->root->contents, buf);
	if (node==NULL)
		return GIMME_ERROR_LOCKFILE_REMOVE;
	gimmeNodeDeleteFromTree(&gimme_dir->database->root->contents, &gimme_dir->database->root->contents_tail, node);
	return ret;
}

// Returns the lowest version number and sets the filename of the lowest into fname
// does not check whatever version is passed in as "approvedver"
// only looks at nodes who match the branch number passed in
int getLowestVersion(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int approvedver, int branch, const char * relpath)
{
	GimmeNode *node;
	int min_version=-1;
	int freezeBranch = -1;

	// figure out if the file is frozen on a branch
	freezeBranch = getFreezeBranch(gimme_dir, relpath);
	if ( freezeBranch != -1 )
		branch = freezeBranch;

	node = gimmeNodeFind(gimme_dir->database->root->contents, lockdir);
	if (node==NULL)
		return -1;

	node = node->contents;
	while (node) {
		if (node->branch == branch) {
			if (node->revision != -1) {
				if (approvedver!=node->revision && (min_version==-1 || node->revision < min_version)) {
					min_version = node->revision;
					if (ret_node) {
						*ret_node = node;
					}
				}
			}
		}
		node = node->next;
	}
	return min_version;
}


int getHighestVersionFromNodeRecursive(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int branch, 
							  const char *relpath)
{
	GimmeNode *node=versionNode, *nodelock=NULL;
	int max_version=-1;
	__time32_t lock_timestamp;
	char lockname[CRYPTIC_MAX_PATH];
	char lockdir[CRYPTIC_MAX_PATH];
	size_t n;

	if (node==NULL)
		return -1;
	assert(node && node->is_dir && node->contents);
	strcpy(lockdir, gimmeNodeGetFullPath(versionNode));
	assert(!strEndsWith(lockdir, "/"));

	// If we scanned the filesystem, it's possible we might see a file that is still in the process of being copied,
	//  so in that case, we only want the latest file that is *older* than the lock (since the lock get touched after
	//  a file is done being copied
	if (gimme_dir->database->loaded_from_fs) {
		// Find the timestamp of the lock if it exists
		n = strlen(lockdir) - strlen("_versions");
		strncpy_s(SAFESTR(lockname), lockdir, n);
		// strncpy_s terminates
// 		lockname[n] = 0;
		makeLockName(gimme_dir, lockname, SAFESTR(lockname), branch);
		nodelock = gimmeNodeFind(gimme_dir->database->root->contents, lockname);
	}
	if (nodelock && nodelock->lock && stricmp(nodelock->lock, gimmeGetUserName())!=0) {
		lock_timestamp = nodelock->timestamp;
	} else {
		lock_timestamp = -1;
	}

	// Only look at nodes that are part of this branch!
	node = node->contents;
	while (node) {
		if (node->branch == branch) {
			if (node->revision != -1) {
				if (node->revision > max_version && (lock_timestamp==-1 || node->checkintime<=lock_timestamp)) {
					max_version = node->revision;
					if (ret_node) {
						*ret_node = node;
					}
				}
			}
		}
		node = node->next;
	}
	if (max_version!=-1 || branch<=0) {
		return max_version;
	} else {
		return getHighestVersionFromNodeRecursive(gimme_dir, versionNode, ret_node, branch-1, relpath);
	}
}

int getHighestVersionFromNode(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int branch, 
							  const char *relpath)
{
	int freezeBranch = -1;

	if (ret_node)
		*ret_node = NULL;

	// figure out if the file is frozen on a branch
	freezeBranch = getFreezeBranch(gimme_dir, relpath);
	if ( freezeBranch == -1 )
		return getHighestVersionFromNodeRecursive(gimme_dir, versionNode, ret_node, branch, relpath);
	else
		return getHighestVersionFromNodeRecursive(gimme_dir, versionNode, ret_node, freezeBranch, relpath);
}

int getHighestVersion(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char * relpath)
{
	GimmeNode *node;
	node = gimmeNodeFind(gimme_dir->database->root->contents, lockdir);
	return getHighestVersionFromNode(gimme_dir, node, ret_node, branch, relpath);
}

static int getSpecificRevisionFromNodeRecursive(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int revision, 
									   int branch, const char * relpath)
{
	GimmeNode *node=versionNode;
	int ret=-1;

	if (node==NULL)
		return -1;

	node = node->contents;
	while (node) {
		if (node->branch==branch) {
			if (node->revision != -1) {
				if (node->revision == revision) {
					ret = node->revision;
					if (ret_node) {
						*ret_node = node;
					}
				}
			}
		}
		node = node->next;
	}
	if (ret!=-1 || branch==0) {
		return ret;
	} else {
		return getSpecificRevisionFromNodeRecursive(gimme_dir, versionNode, ret_node, revision, branch-1, relpath);
	}
}

static int getSpecificRevisionFromNode(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int revision, 
									   int branch, const char * relpath)
{
	int freezeBranch = -1;

	// figure out if the file is frozen on a branch
	freezeBranch = getFreezeBranch(gimme_dir, relpath);

	if ( freezeBranch == -1 )
		return getSpecificRevisionFromNodeRecursive(gimme_dir, versionNode, ret_node, revision, branch, relpath);
	else
		return getSpecificRevisionFromNodeRecursive(gimme_dir, versionNode, ret_node, revision, freezeBranch, relpath);
}

static int getSpecificRevision(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int revision, int branch, const char * relpath)
{
	GimmeNode *node;
	node = gimmeNodeFind(gimme_dir->database->root->contents, lockdir);
	return getSpecificRevisionFromNode(gimme_dir, node, ret_node, revision, branch, relpath);
}


static GimmeNode gnDeleteMe = { NULL, NULL, 0, NULL, NULL, NULL, "dummy.deleted", 0, 0, 0, NULL, 0, 0 };

static int getRevisionByTimeFromNodeRecursive(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int branch, const char * relpath)
{
	GimmeNode *node=versionNode;
	int ret=-1;

	if (node==NULL)
		return -1;

	node = node->contents;
	while (node) {
		if (node->branch == branch) {
			if (node->revision != -1) {
				if ((node->checkintime < gimme_state.dateToGet+IGNORE_TIMEDIFF) && node->revision > ret) {
					ret = node->revision;
					if (ret_node) {
						*ret_node = node;
					}
				}
			}
		}
		node = node->next;
	}
	if (ret==-1 && branch>0) {
		// Nothing found at this level, try the next branch
		return getRevisionByTimeFromNodeRecursive(gimme_dir, versionNode, ret_node, branch-1, relpath);
	}
	if (ret==-1) {
		// If the oldest version of this file is newer than the time we want,
		// we in fact want to delete this file
		if (ret_node) {
			*ret_node = &gnDeleteMe;
		}
		return -2;
		//return defret;
	}
	return ret;
}

static int getRevisionByTimeFromNode(GimmeDir *gimme_dir, GimmeNode *versionNode, GimmeNode **ret_node, int branch, const char * relpath)
{
	int freezeBranch = -1;

	// figure out if the file is frozen on a branch
	freezeBranch = getFreezeBranch(gimme_dir, relpath);
	if ( freezeBranch == -1 )
		return getRevisionByTimeFromNodeRecursive(gimme_dir, versionNode, ret_node, branch, relpath);
	else
		return getRevisionByTimeFromNodeRecursive(gimme_dir, versionNode, ret_node, freezeBranch, relpath);

}

static int getRevisionByTime(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char * relpath)
{
	GimmeNode *node;
	node = gimmeNodeFind(gimme_dir->database->root->contents, lockdir);
	return getRevisionByTimeFromNode(gimme_dir, node, ret_node, branch, relpath);
}

int getApprovedRevision(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char * relpath)
{
	__time32_t dateSave = gimme_state.dateToGet; // Save it, since getApprovedVersion may be used inside of another call
	int ret;
	gimme_state.dateToGet = gimme_dir->dateOfApproval;
	ret = getRevisionByTime(gimme_dir, lockdir, ret_node, branch,relpath);
	gimme_state.dateToGet = dateSave;
	return ret;
}

static int checkinOp(GIMMEOperation operation) {
	switch (operation) {
		case GIMME_CHECKOUT:     //   0 - checkout
			return 0;
		case GIMME_CHECKIN:      //   1 - checkin
		case GIMME_FORCECHECKIN: //   2 - forcefully checkin even if someone else has it checked out
		case GIMME_DELETE:       //   3 - mark a file as deleted
		case GIMME_ACTUALLY_DELETE:// 8 - actually delete a file
		case GIMME_UNDO_CHECKOUT://   5 - undo checkout
			return 1;
		case GIMME_GLV:           //   4 - just get the latest verison (don't checkout)
			return 0;
	}
	return 0;
}

static void addToQueue(GimmeDir *gimme_dir, GIMMEOperation operation, int quiet, const char *relpath, const char *comments)
{
	if (operation == GIMME_FORCECHECKIN || !gimmeIsBinFile(gimme_dir, relpath))
	{
		GimmeQueuedAction *action = calloc(sizeof(GimmeQueuedAction), 1);	
		action->gimme_dir = gimme_dir;		
		action->notes = comments;			
		action->operation = operation;		
		action->quiet = quiet;				
		action->relpath = strdup(relpath);	
		eaPush(&gimme_state.queuedActions, action);
	}
}

bool glob_was_last_checkin_undo = false;
int gimmeWasLastCheckinAnUndo(void) // Tells whether or not the last checkin request ended up reverting the timestamp
{
	return glob_was_last_checkin_undo;
}


GimmeErrorValue gimmeCheckOut(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, GimmeNode *node, const char *localfname, const char *myname, const char *username, GimmeQuietBits quiet)
{
	int ret = NO_ERROR;
	GimmeNode *ms_node = NULL;
	char ms_relpath[MAX_PATH];
	char ms_localfname[MAX_PATH];
	bool ms_notindb = 1;

	if (username && stricmp(username, "no one"))
	{
		if (stricmp(username, myname)==0) {
			// check to see if local file exists, if not, glv
			if (!fileExists(localfname)) {
				// get latest version
				if (!quiet) gimmeLog(LOG_WARN_LIGHT, "File checked out by you, but has been deleted, getting latest version... (%s)", relpath);
				operation=GIMME_GLV;
			} else {
				if (!quiet) gimmeLog(LOG_INFO, "File already checked out by you, ignoring request. (%s)", relpath);
				_chmod(localfname, _S_IREAD | _S_IWRITE);
				return NO_ERROR;
			}
		} else {
			gimmeLog(LOG_FATAL, "\"%s\"\nSorry, you can't get this file because %s already has it checked out!",relpath, username);
			return GIMME_ERROR_ALREADY_CHECKEDOUT;
		}
	}
	if (gimme_state.just_queue && (operation)!=GIMME_FORCECHECKIN) {
		addToQueue(gimme_dir, (operation), quiet, relpath, checkin_notes[CHECKIN_NOTE_CHECKOUT]);
		return NO_ERROR;
	}

	if (strEndsWith(relpath, ".ms"))
	{
		strcpy(ms_relpath, relpath);
		ms_relpath[strlen(ms_relpath)-3] = 0;
		strcpy(ms_localfname, localfname);
		ms_localfname[strlen(ms_localfname)-3] = 0;
	}
	else
	{
		sprintf(ms_relpath, "%s.ms", relpath);
		sprintf(ms_localfname, "%s.ms", localfname);
	}

	// Used to be:
	// The ordering is important here for issues of speed:
	//  first, make sure the file exists in the database
	//  first, make the lock,
	//  then write back the database, and release it,
	//  then actually copy the file
	// now:
	//  Because we don't copy files whose dates are ahead of the date on the lock file,
	//  we need to copy the file before locking it.  Luckily we now also
	//  do not leave the database locked, so it's fine to copy it first
	if (-1==makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node)) {
		if (!quiet) gimmeLog(LOG_WARN_LIGHT, "%s: No existing version found in database!", relpath);
		if (!gimme_state.simulate) {
			_chmod(localfname, _S_IREAD | _S_IWRITE);
			_chmod(ms_localfname, _S_IREAD | _S_IWRITE);
		}
		return GIMME_ERROR_NOT_IN_DB;
	}
	if (gimmeFileIsDeletedFile(node->name)) {
		if (!fileExists(localfname)) {
			return GIMME_ERROR_ALREADY_DELETED;
		}
		gimmeLog(LOG_WARN_LIGHT, "\"%s\"\nPossible warning: You have checked out a file marked as deleted (will be resolved when you check in everything).", relpath);
	}
	ret = getLatestVersionFile(gimme_dir, relpath, NULL, REV_BLEEDINGEDGE_FORCE, quiet, false);
	if (NO_ERROR!=ret)
		return ret;

	if (-1!=makeDBName(gimme_dir, ms_relpath, REV_BLEEDINGEDGE, &ms_node))
	{
		ret = getLatestVersionFile(gimme_dir, ms_relpath, NULL, REV_BLEEDINGEDGE_FORCE, quiet, false);
		if (NO_ERROR!=ret)
			return ret;
		ms_notindb = 0;
	}
	if (operation!=GIMME_GLV) {
		if (NO_ERROR!=(ret=makeLock(gimme_dir, relpath)) && !gimme_state.simulate)
		{
			return ret;
		}
		if (!ms_notindb && NO_ERROR!=(ret=makeLock(gimme_dir, ms_relpath)) && !gimme_state.simulate)
		{
			deleteLock(gimme_dir, relpath, 0, 1);
			return ret;
		}
	}
	if (!gimme_state.simulate) gimmeDirDatabaseFlush(gimme_dir);
	if (!gimme_state.simulate) {
		_chmod(localfname, _S_IREAD | _S_IWRITE);
		_chmod(ms_localfname, _S_IREAD | _S_IWRITE);
	}
	if (status && (!quiet || ret!=NO_ERROR)) {
		flushStatus();
	}
	if (ret!=NO_ERROR) {
		return ret;
	}
	if (!quiet) gimmeLog(LOG_WARN_LIGHT, "You have checked out: %s",localfname);
	return ret;
}

// This is the main work-horse function
GimmeErrorValue gimmeDoOperationRelPath(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, GimmeQuietBits quiet) {
	int		latestrev=-1;
	int		readd=0, undo_checkout=0;
	const char	*myname = gimmeGetUserName();
	GimmeNode *node;
	FWStatType filestat;
	const char	*username;
	char	localfname[CRYPTIC_MAX_PATH];
	char	buf[1024];
	int ret;
	glob_was_last_checkin_undo = false;

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	// For all code paths, this macro must be called before anything is done (but, preferably, after error checking)
#define ADD_TO_QUEUE(comments, operation)								\
	if (gimme_state.just_queue && (operation)!=GIMME_FORCECHECKIN) {			\
		addToQueue(gimme_dir, (operation), quiet, relpath, comments);	\
		return NO_ERROR;												\
	}

	if (strstr(relpath, "_vb#")!=0) {
		gimmeLog(LOG_WARN_HEAVY, "You cannot operate on files that contain \"_vb#\" in their name");
		return GIMME_ERROR_FILENOTFOUND;
	}

	if (operation==GIMME_FORCECHECKIN && strEndsWith(relpath, ".bak")) {
		return NO_ERROR;
	}

	if (operation!=GIMME_FORCECHECKIN && gimmeIsBinFile(gimme_dir, relpath)) {
		return NO_ERROR;
	}

	makeLocalNameFromRel(gimme_dir, relpath, localfname);

	// Check for exclusions
	{
		ExclusionType extype = gimmeCheckExclusion(gimme_dir, relpath);
		if (extype>ET_ERROR) {
			return extype - ET_ERROR;
		}
		if (extype==ET_EXCLUDED) {
			//if (!quiet && gimme_state.pause && !gimme_state.simulate) gimmeLog(LOG_WARN_LIGHT, "%s\nFile excluded from soucre control, ignoring.", relpath);
			if (operation==GIMME_CHECKOUT) {
				// Make writeable
				_chmod(localfname, _S_IWRITE | _S_IREAD);
			}
			return NO_ERROR;
		} else if (extype==ET_NOTINFILESPEC) {
			return NO_ERROR;
		}
	}

	if (NO_ERROR!=gimmeDirDatabaseLoad(gimme_dir, localfname)) {
		gimmeLog(LOG_FATAL, "Error loading database");
		return GIMME_ERROR_DB;
	}
	gimmeDirDatabaseClose(gimme_dir); // It's fine to do this here because we don't recurse in here anywhere

	username = isLocked(gimme_dir, relpath);
	latestrev=makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);

	assert(-2!=latestrev);

	if (checkinOp(operation))
	{ // checkin

		if ((operation==GIMME_CHECKIN || operation==GIMME_FORCECHECKIN) && !fileExists(localfname)) {
			if (latestrev!=-1 && gimmeFileIsDeletedFile(node->name)) // Already deleted (-forceputfold)
				return NO_ERROR;
			if (operation == GIMME_FORCECHECKIN)
				operation = GIMME_ACTUALLY_DELETE;
			else
				operation=GIMME_DELETE;
		}

		if (operation==GIMME_CHECKIN || operation==GIMME_FORCECHECKIN) {
			// Check date and time of file
			time_t serverTime = getServerTime(gimme_dir);
			pststat(localfname, &filestat);
			if (filestat.st_mtime > serverTime+50*60) {
				gimmeLog(LOG_FATAL, "You cannot check in a file modified in the future! (%s)", localfname);
				return GIMME_ERROR_FUTURE_FILE;
			}
		}

		if (latestrev!=-1 && gimmeFileIsDeletedFile(node->name)) {
			// file is in DB, but has been deleted
			if (operation==GIMME_DELETE) {
				ADD_TO_QUEUE(" (Already deleted)", operation);
				//if (!quiet && fileExists(localfname)) gimmeLog(LOG_WARN_LIGHT, "\"%s\"\nYou cannot remove that file from the database because it is already deleted.", relpath);
				if (fileExists(localfname)) {
					char dbname[CRYPTIC_MAX_PATH];
					FWStatType sbuf;
					deleteLocalFile(gimme_dir, localfname, true);
					// Re-mark it as deleted, in case the journal is out of sync
					makeDBNameFromNode(gimme_dir, node, dbname);
					sbuf = fileTouch(dbname, NULL);
					gimmeJournalAdd(gimme_dir->database, gimmeNodeGetFullPath(node), sbuf.st_size, sbuf.st_mtime, NULL);
					gimme_state.remove_count++;
				}
				if (username && stricmp(myname,username)==0) // if it's checked out by us
				{
					// Delete lockfile
					if (NO_ERROR!=(ret=deleteLock(gimme_dir, relpath, false, false))) {
						// there was an error, but let it go anyway
					}
					if (!gimme_state.simulate) gimmeDirDatabaseFlush(gimme_dir);
				}
				return GIMME_ERROR_ALREADY_DELETED;
			} else if (operation==GIMME_UNDO_CHECKOUT) {
				undo_checkout=1;
			} else {
				if (username) {
					// Remove this warning : if (!gimme_state.nowarn) gimmeLog(LOG_WARN_LIGHT, "\"%s\"\nWarning: file marked as deleted, *but* it has been checked out by %s.", relpath, username);
				}
				readd=1;
			}
			if (!username)
				username = "no one";
		} else if (latestrev!=-1) { // file does exist in DB
			if (!username)
				username = "no one";
			if (stricmp(myname,username)!=0 && !(stricmp(username, "no one")==0 && (operation==GIMME_FORCECHECKIN || operation==GIMME_DELETE || operation==GIMME_ACTUALLY_DELETE))) // if it's not checked out by the user, or if they're forcing it in, then if if it's checked out by anyone other than the user, then error
			{
				if (!quiet)
					gimmeLog(LOG_FATAL, "\"%s\"\nYou can't check that file back in because you don't have it locked - %s does.",relpath,username);
				else
					gimmeLog(LOG_WARN_HEAVY, "\"%s\"\nYou can't check that file back in because you don't have it locked - %s does.",relpath,username);
				return GIMME_ERROR_NOTLOCKEDBYYOU;
			}
			if (operation!=GIMME_UNDO_CHECKOUT && operation!=GIMME_DELETE && operation!=GIMME_ACTUALLY_DELETE) {
				// Check to see if they've actually changed anything
				pststat(localfname, &filestat);
				if (node->size == (size_t)filestat.st_size) {
					// Same size, check the contents
					char dbname[CRYPTIC_MAX_PATH];
					makeDBNameFromNode(gimme_dir, node, dbname);

					if (node->timestamp == filestat.st_mtime || (!gimme_state.ignore_diff && fileCompare(dbname, localfname)==0)) {
						if (operation!=GIMME_FORCECHECKIN && !gimme_state.leavecheckedout && !(quiet&GIMME_QUIET_NOUNDOCHECKOUT))
							gimmeLog(LOG_WARN_LIGHT, "No changes detected, just undoing check-out. (%s)", relpath);
						undo_checkout=1;
						glob_was_last_checkin_undo = true;
						ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_NOCHANGE], (operation!=GIMME_FORCECHECKIN)?GIMME_UNDO_CHECKOUT:operation);
					}
				}
				if (!undo_checkout) {
					static int okToAllLinkBreak = 0;
					static int okToAllNotLinked = 0;
					// There have been actual changes made
					// Check to see if we're breaking a link
					// The file is still linked
					if (!okToAllLinkBreak && !gimme_state.no_comments && 
						gimmeGetWarnOnLinkBreak(gimme_dir->active_branch) && 
						gimme_dir->active_branch!=gimmeGetMinBranchNumber() && 
						gimmeIsNodeLinkedPrev(node, gimme_dir->active_branch) &&
						getFreezeBranch(gimme_dir, relpath) == -1)
					{
						char temp[1024];
						sprintf_s(SAFESTR(temp), "%s\nChecking in this file will break it's link with branch %d (%s), any changes in the %s branch will no longer be inherited to this one.\nAre you sure you want to check in this file and break the link?",
							localfname, gimme_dir->active_branch-1, gimmeGetBranchPrevName(gimme_dir->active_branch), gimmeGetBranchPrevName(gimme_dir->active_branch));

						ret = okToAllCancelDialogEx(temp, "Confirm Link Break", okToAllCancelCallback);
						if ( ret == IDCANCEL )
							return GIMME_ERROR_CANCELED;
						else if ( ret == IDOKTOALL )
							okToAllLinkBreak = 1;
						//if (IDNO==MessageBox(NULL, temp, "Confirm Link Break", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
						//	return GIMME_ERROR_CANCELED;
						//}
					}
					// Check to make sure the link to the next version isn't broken (if so, let them
					//  know they have to do their work twice!)
					if (!okToAllNotLinked && !gimme_state.no_comments && gimmeGetWarnOnLinkBroken(gimme_dir->active_branch) && gimme_dir->active_branch!=gimmeGetMaxBranchNumber() && gimmeIsNodeLinkBroken(node, gimme_dir->active_branch)) {
						char temp[1024];
						int brokenBranch = gimmeIsNodeLinkBroken(node, gimme_dir->active_branch);
						sprintf_s(SAFESTR(temp), "%s\r\nThis file is no longer linked to branch %d (%s), if the change you are checking in is a bugfix, it will have to be duplicated in the %s branch as well!",
							localfname, brokenBranch, gimmeGetBranchLaterName(brokenBranch-1), gimmeGetBranchLaterName(brokenBranch-1));
						
						if ( okToAllDialog(temp, "WARNING:  File no longer linked") )
							okToAllNotLinked = 1;
						//	MessageBox(NULL, temp, "WARNING:  File no longer linked", MB_OK | MB_SYSTEMMODAL |MB_ICONWARNING);
					}
				}
			} else if (operation==GIMME_UNDO_CHECKOUT) {
				undo_checkout=1;
				ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_UNDOCHECKOUT], operation);
			}
		} else { // file does not exist in DB
			if (operation==GIMME_DELETE || operation==GIMME_ACTUALLY_DELETE) {
				ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_NOTINDB], operation);
				//gimmeLog(LOG_FATAL, "\"%s\"\nYou can't remove that file because it does not exist in the database", relpath);
				if (NO_ERROR!=(ret=deleteLock(gimme_dir, relpath, false, true))) {
					// there was an error, but let it go anyway
				}
				deleteLocalFile(gimme_dir, localfname, true);
				if (!gimme_state.simulate) gimmeDirDatabaseFlush(gimme_dir);
				return GIMME_NO_ERROR;
			}
			if (operation==GIMME_UNDO_CHECKOUT) {
				gimmeLog(LOG_FATAL, "\"%s\"\nYou can't undo a checkout on that file because it does not exist in the database", relpath);
				return GIMME_ERROR_NOT_IN_DB;
			}
			if (operation==GIMME_CHECKIN && strchr(localfname, '%')){
				gimmeLog(LOG_FATAL, "\"%s\"\nYou can't check in a file with a percent sign in the file name.", relpath);
				return GIMME_ERROR_NOT_IN_DB;
			}
			ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_NEW], operation);
			// Get a lock on it so that others do not grab a partially copied file
			if (NO_ERROR!=(ret=makeLock(gimme_dir, relpath)) && !gimme_state.simulate)
			{
				return ret;
			}
			gimme_state.new_file_count++;
			username = gimmeGetUserName(); // it's locked by us
		}

		if (operation==GIMME_DELETE || operation==GIMME_ACTUALLY_DELETE) {
			ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_DELETED], operation);
		} else {
			ADD_TO_QUEUE(NULL, operation);
		}
		if (operation==GIMME_DELETE) { // mark as deleted
			char temp[512];
			sprintf_s(SAFESTR(temp), "Are you sure you want to delete the file\n\"%s%s\"\nfrom the local drive?\n\nThis file will be removed from the\nrevision control database the next time you do a\ncheckin on the folder.  You can undo this operation by\ndoing a gimme undo checkout and getting latest.", gimme_dir->local_dir, relpath);
			if (!gimme_state.nowarn && !gimme_state.simulate && IDNO==MessageBox_UTF8(NULL, temp, "Confirm File Delete", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
				return NO_ERROR;
			}
			if ((ret=gimmeCheckOut(gimme_dir, relpath, operation, node, localfname, myname, username, quiet))!=NO_ERROR)	{
				return ret;
			}
			if (fileMoveToRecycleBin(localfname)!=0)
				return GIMME_ERROR_FILENOTFOUND;
		} else if (operation==GIMME_ACTUALLY_DELETE) {
			rmdirtree(localfname);
			if ((ret=markAsDeleted(gimme_dir, relpath))!=NO_ERROR) {
				return ret;
			}
			gimme_state.remove_count++;
		}else { // check in
			//char ms_relpath[MAX_PATH];
			//char ms_localfname[MAX_PATH];
			// check for existence
			if (!fileExists(localfname) && operation!=GIMME_UNDO_CHECKOUT) {
				assert(!"This shouldn't ever happen, file has been deleted, so operation should not be DELETE");
				gimmeLog(LOG_FATAL, "\"%s\"\nThe local file has been deleted, you cannot check in the file\nDo another Get Latest to get the latest version first.", relpath);
				return GIMME_ERROR_FILENOTFOUND;
			}

			//if (strEndsWith(relpath, ".ms"))
			//{
			//	strcpy(ms_relpath, relpath);
			//	ms_relpath[strlen(ms_relpath)-3] = 0;
			//	strcpy(ms_localfname, localfname);
			//	ms_localfname[strlen(ms_localfname)-3] = 0;
			//}
			//else
			//{
			//	sprintf(ms_relpath, "%s.ms", relpath);
			//	sprintf(ms_localfname, "%s.ms", localfname);
			//}

			if (gimme_state.leavecheckedout && stricmp(username, "no one")==0) {
				// We are adding a new file to the database, but want to leave it checked out
				makeLock(gimme_dir, relpath);
				//if (fileExists(ms_localfname))
				//	makeLock(gimme_dir, ms_relpath);
			}

			if (!undo_checkout) {
				char fullpath[CRYPTIC_MAX_PATH];
				const char *blockStr;
				makeLocalNameFromRel(gimme_dir, relpath, fullpath);
				// is the file blocked
				blockStr = gimmeGetBlockString(fullpath);
				if ( blockStr )
				{
					gimmeLog( LOG_FATAL, "%s", blockStr );
					return GIMME_ERROR_NOTLOCKEDBYYOU;
				}
				else if ( !gimmeRunProcess(gimme_dir, relpath) )
					return GIMME_ERROR_NOTLOCKEDBYYOU;
				if ((ret=copyFileToDB(gimme_dir, relpath))!=NO_ERROR) {
					return ret;
				}
				gimme_state.file_update_count++;
				//if (fileExists(ms_localfname) && (ret=copyFileToDB(gimme_dir, ms_relpath))!=NO_ERROR) {
				//	return ret;
				//}
				//gimme_state.file_update_count++;
			} else if (operation==GIMME_UNDO_CHECKOUT) { // undo checkout
				if (gimmeFileIsDeletedFile(node->name)) {
					deleteLocalFile(gimme_dir, localfname, true);
					//if (fileExists(ms_localfname))
					//	deleteLocalFile(gimme_dir, ms_localfname, true);
				} else {
					char fullpath[CRYPTIC_MAX_PATH];
					makeLocalNameFromRel(gimme_dir, relpath, fullpath);
					gimmeUnblockFile(fullpath);
					ret = getLatestVersionFile(gimme_dir, relpath, NULL, REV_BLEEDINGEDGE_FORCE, quiet, false);
					if (status && (!quiet || ret!=NO_ERROR)) {
						flushStatus();
					}
					if (ret!=NO_ERROR) {
						return ret;
					}
					//getLatestVersionFile(gimme_dir, ms_relpath, NULL, REV_BLEEDINGEDGE_FORCE, quiet, false);
				}
			} else {
				// undo_checkout but not operation == GIMME_UNDO_CHECKOUT (i.e. file dates the same, or contents the same
				// change timestamp
				struct _utimbuf utb;
				utb.actime = node->timestamp;
				utb.modtime = node->timestamp;
				pstutime(localfname, &utb);	

				// set the file create time to the checkin time
				//pstSetFileCreatedTime(localfname, node->checkintime);
			}

			if (undo_checkout) {
				gimme_state.undo_checkout_count++;
			}

			// Make read only
			if (!gimme_state.simulate && !gimme_state.leavecheckedout && !gimmeIsBinFile(gimme_dir, relpath)) {
				_chmod(localfname, _S_IREAD );
				//_chmod(ms_localfname, _S_IREAD );
			}
		}

		// Delete lockfile if there was an old revision and it was checked out
		if (operation!=GIMME_DELETE && (strcmp(username, "no one")!=0 || gimme_state.leavecheckedout)) {
			if (!gimme_state.leavecheckedout) {
				if (NO_ERROR!=(ret=deleteLock(gimme_dir, relpath, false, false))) {
					// there was an error, but let it go anyway
				}
			} else {
				// just do a touch on the lockfile
				if (NO_ERROR!=(ret=deleteLock(gimme_dir, relpath, true, false))) {
					// there was an error, but let it go anyway
				}
			}
		}

		if (latestrev==-1) {
			if (!quiet) gimmeLog(LOG_INFO, "You have added %s to the database", localfname);
		} else if (operation==GIMME_DELETE) {
			if (!gimme_state.simulate) {
				deleteLocalFile(gimme_dir, localfname, true);
			}
			if (!quiet) gimmeLog(LOG_INFO, "You have removed %s from the local drive.  Check it in to remove it from the database.", localfname);
		} else if (readd==1) {
			if (!quiet) gimmeLog(LOG_INFO, "You have re-added %s to the database", localfname);
		} else if (operation==GIMME_UNDO_CHECKOUT) {
			if (!quiet) gimmeLog(LOG_INFO, "You have reverted %s to the latest version from the database", localfname);
		} else if (undo_checkout) {
			// no status message
		} else {
			if (!quiet) gimmeLog(LOG_INFO, "You have updated: %s (Old rev. was #%d)", localfname, latestrev);
		}
		if (!gimme_state.simulate) {
			if (!(undo_checkout && operation==GIMME_FORCECHECKIN))
				gimmeDirDatabaseFlush(gimme_dir);
		}
		return NO_ERROR;
	}
	else if (operation==GIMME_GLV)
	{ // Get latest version of a file
		ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_GETLATEST], operation);
		ret = getLatestVersionFile(gimme_dir, relpath, NULL, REV_BLEEDINGEDGE, quiet, false);
		if (status && (!quiet || ret!=NO_ERROR)) {
			flushStatus();
		}
		return ret;
	}
	else
	{ // checkout
#if 0
		if (username)
		{
			if (stricmp(username, myname)==0) {
				// check to see if local file exists, if not, glv
				if (!fileExists(localfname)) {
					// get latest version
					if (!quiet) gimmeLog(LOG_WARN_LIGHT, "File checked out by you, but has been deleted, getting latest version... (%s)", relpath);
					operation=GIMME_GLV;
				} else {
					if (!quiet) gimmeLog(LOG_INFO, "File already checked out by you, ignoring request. (%s)", relpath);
					_chmod(localfname, _S_IREAD | _S_IWRITE);
					return NO_ERROR;
				}
			} else {
				gimmeLog(LOG_FATAL, "\"%s\"\nSorry, you can't get this file because %s already has it checked out!",relpath, username);
				return GIMME_ERROR_ALREADY_CHECKEDOUT;
			}
		}
		ADD_TO_QUEUE(checkin_notes[CHECKIN_NOTE_CHECKOUT], operation);

		// Used to be:
		// The ordering is important here for issues of speed:
		//  first, make sure the file exists in the database
		//  first, make the lock,
		//  then write back the database, and release it,
		//  then actually copy the file
		// now:
		//  Because we don't copy files whose dates are ahead of the date on the lock file,
		//  we need to copy the file before locking it.  Luckily we now also
		//  do not leave the database locked, so it's fine to copy it first
		if (-1==makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node)) {
			if (!quiet) gimmeLog(LOG_WARN_LIGHT, "%s: No existing version found in database!", relpath);
			if (!gimme_state.simulate) _chmod(localfname, _S_IREAD | _S_IWRITE);
			return GIMME_ERROR_NOT_IN_DB;
		}
		if (gimmeFileIsDeletedFile(node->name)) {
			if (!fileExists(localfname)) {
				return GIMME_ERROR_ALREADY_DELETED;
			}
			gimmeLog(LOG_WARN_LIGHT, "\"%s\"\nPossible warning: You have checked out a file marked as deleted (will be resolved when you check in everything).", relpath);
		}
		ret = getLatestVersionFile(gimme_dir, relpath, NULL, REV_BLEEDINGEDGE_FORCE, quiet, false);
		if (NO_ERROR!=ret)
			return ret;
		if (operation!=GIMME_GLV) {
			if (NO_ERROR!=(ret=makeLock(gimme_dir, relpath)) && !gimme_state.simulate)
			{
				return ret;
			}
		}
		if (!gimme_state.simulate) gimmeDirDatabaseFlush(gimme_dir);
		if (!gimme_state.simulate) _chmod(localfname, _S_IREAD | _S_IWRITE);
		if (status && (!quiet || ret!=NO_ERROR)) {
			flushStatus();
		}
		if (ret!=NO_ERROR) {
			return ret;
		}
		if (!quiet) gimmeLog(LOG_WARN_LIGHT, "You have checked out: %s",localfname);
#else
		ret = gimmeCheckOut(gimme_dir, relpath, operation, node, localfname, myname, username, quiet);
#endif

		if (ret!=NO_ERROR) 
			return ret;

		if (gimme_state.launch_editor) {
			if (gimme_state.editor) {
				sprintf_s(SAFESTR(buf),"\"%s\" \"%s\"",gimme_state.editor,localfname);
				_flushall();
				system_detach(buf, 0, false);
			} else {
				fileOpenWithEditor(localfname);
			}
		}

		return NO_ERROR;
	}
}

int gimmeQueryAvailable(void)
{
	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs))
		return 1;
	return 0;
}

int gimmeCheckDisconnected(void)
{
	if (fileExists("C:\\gimme_disconnected.txt")) {
		gimmeLog(LOG_FATAL, "You have checkouts pending that were done while disconnected.  Please run \"gimme -reconnect\" first.");
		return 1;
	}
	return 0; // OK
}



int gimmeOfflineCheckout(const char *fullpath, GIMMEOperation operation, GimmeQuietBits quiet)
{
	FWStatType filestat;
	pststat(fullpath, &filestat);
	if (operation == GIMME_CHECKOUT)
	{
		// attempt to change read-only status and log
		if (filestat.st_mode & _S_IWRITE)
		{
			gimmeLog(LOG_WARN_LIGHT, "File is already writeable, assumed to already be checked out : %s", fullpath);
		} else {
			gimmeOfflineTransactionLog(true, "CHECKOUT\t%s\t%d\t%d", fullpath, time(NULL)+gimmeGetTimeAdjust(), filestat.st_mtime);
			_chmod(fullpath, _S_IREAD | _S_IWRITE);
			gimmeLog(LOG_WARN_LIGHT, "File is now writeable and flagged as checked out disconnected : %s", fullpath);
		}
	} else {
		gimmeLog(LOG_WARN_HEAVY, "Cannot check in files while disconnected, file left checked out : %s", fullpath);
		//_chmod(fullpath, _S_IREAD);
	}
	return 0;
}

GimmeErrorValue gimmeDoOperation(const char *fname, GIMMEOperation operation, GimmeQuietBits quiet)
{
	char	fullpath[CRYPTIC_MAX_PATH],*relpath, *fname2, fname_temp[CRYPTIC_MAX_PATH];
	GimmeDir	*gimme_dir;
	int ret;
	int leavecheckedout_save = gimme_state.leavecheckedout;
	char fileLocateBuf[CRYPTIC_MAX_PATH];

	gimmeQueryClearCaches();

	if (operation==GIMME_CHECKIN_LEAVE_CHECKEDOUT) {
		operation=GIMME_CHECKIN;
	}

	gimme_state.quiet = quiet;
	strcpy(fname_temp, fname);
	if (fname[1]!=':' || !fileExists(fname)) {
		fname2 = fileLocateWrite(fname_temp, fileLocateBuf);
	} else {
		// File already exists and it's absolute, just call makefullpath on it.
		fname2=NULL; 
	}
	if (fname2==NULL) {
		makefullpath(fname_temp,fullpath);
	} else {
		makefullpath(fname2,fullpath); // This might not do anything unless people have things like "./" in their gameDataDirs
	}
	forwardSlashes(fullpath);

	gimme_dir = findGimmeDir(fullpath);
	if (eaSize(&eaGimmeDirs)==0) {
		if (!quiet) gimmeLog(LOG_FATAL, "No databases defined, operating disconnected.");
		if ( IDOK == MessageBox_UTF8( NULL, "You are not connected to the database.  Would you like to continue working offline?\nIf you click OK, you will need to run gimme -reconnect once you are connected to the database.\nYou may click Cancel to cancel this operation, connect to the database, and then try again.", 
									   "Gimme Database Error", MB_OKCANCEL) )
			gimmeOfflineCheckout(fullpath, operation, quiet);
		if (gimme_state.command_line!=NULL) {
			gimmeOfflineLog("command line: %s", gimme_state.command_line);
		} else {
			gimmeOfflineLog("%s %s", gimmeOpToString(operation), fname);
		}
		return GIMME_ERROR_NO_SC;
	}
	if (!gimme_dir)
	{
		gimmeLog(LOG_FATAL, "Can't find source control folder to match %s!",fullpath);
		return GIMME_ERROR_NODIR;
	}
//	if (stricmp(&fullpath[strlen(fullpath)-5],".lock")==0)
//	{
//		gimmeLog(LOG_FATAL, "%s is a lock file - you can't edit that!",fullpath);
//		return GIMME_ERROR_LOCKFILE;
//	}

	gimmeSetBranchConfigRoot(gimme_dir->lock_dir);
	relpath = findRelPath(fullpath, gimme_dir);

	ret = gimmeDoOperationRelPath(gimme_dir, relpath, operation, quiet);

	gimme_state.leavecheckedout = leavecheckedout_save;
	// Flush hog file modifications here!
	gimmeFlushAllHogFiles();
	return ret;
}

int gimmePurgeFile(const char *fname, int add)
{
	char	localfname[CRYPTIC_MAX_PATH],*relpath, fname_temp[CRYPTIC_MAX_PATH];
	GimmeDir	*gimme_dir;
	int		i;

	strcpy(fname_temp, fname);

	gimmeLoadConfig();
	gimme_dir = NULL;
	for(i=0;i<eaSize(&eaGimmeDirs) && !gimme_dir;i++)
	{
		if (strnicmp(eaGimmeDirs[i]->lock_dir,fname,strlen(eaGimmeDirs[i]->lock_dir))==0 && isslashornull(fname[strlen(eaGimmeDirs[i]->lock_dir)]))
			gimme_dir = eaGimmeDirs[i];
	}
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "No databases defined");
		return GIMME_ERROR_NO_SC;
	}
	if (!gimme_dir)
	{
		gimmeLog(LOG_FATAL, "Can't find source control folder to match %s! (Gimme Purge can only be called on files in the revisions database",fname);
		return GIMME_ERROR_NODIR;
	}
	if (!strstr(fname, "_versions/") && !strEndsWith(fname, ".lock")) {
		gimmeLog(LOG_FATAL, "This doesn't appear to be a file in a _versions/ folder");
		return GIMME_ERROR_NODIR;
	}

	relpath = findRelPath(fname_temp, gimme_dir);
	sprintf_s(SAFESTR(localfname), "%s%s", gimme_dir->local_dir, relpath);
	if (strEndsWith(localfname, ".lock")) {
		if (strstr(localfname, "_vb#")) {
			*strstr(localfname, "_vb#")=0;
		} else {
			*strstr(localfname, ".lock")=0;
		}
	} else {
		*strstr(localfname, "_versions/")=0;
	}

	if (NO_ERROR!=gimmeDirDatabaseLoad(gimme_dir, localfname)) {
		gimmeLog(LOG_FATAL, "Error loading database");
		return GIMME_ERROR_DB;
	}
	gimmeDirDatabaseClose(gimme_dir); // It's fine to do this here because we don't recurse in here anywhere

	if (!add) {
		char *new_fname, old_fname[512];
		
		// copy file to "N:/revisions/purged/fname"
		new_fname = strstrInsert(fname, "/revisions/", "/revisions/purged/" );
		strcpy(old_fname, fname);
		makeDirectoriesForFile(new_fname);
		fileCopy(old_fname, new_fname);

		_chmod(fname, _S_IREAD | _S_IWRITE);
		if (fileForceRemove(fname) != 0 && fileExists(fname)) {
			gimmeLog(LOG_WARN_HEAVY, "Error deleting version : %s", fname);
		} else {
			rmdirtree(fname_temp);
			gimmeJournalRm(gimme_dir->database, relpath);
		}
	} else {
		FWStatType sbuf;
		pststat(fname, &sbuf);
		if (!fileExists(fname)) {
			gimmeLog(LOG_WARN_HEAVY, "File does not exist : %s", fname);
		} /*else*/ /*put it in the journal anyway*/ {
			char *lockee = NULL;
			if (strEndsWith(fname, ".lock")) {
				// It's a lock file, get the user name from the file
				GimmeNode *node = gimmeNodeFind(gimme_dir->database->root->contents, relpath);
				if (node)
					lockee = node->lock;
			}

			gimmeJournalAdd(gimme_dir->database, relpath, sbuf.st_size, sbuf.st_mtime, lockee);
		}
	}

	return NO_ERROR;
}

static int count;
static const char *cur_myname;
static int cur_gimme_dir;
static int cur_put_back;
static int cur_operation;

static int addNewFiles(GimmeDir *gimme_dir, char *localdir) {
	int ret;
	int		count,i;
	char	*relpath,**file_list,*lockee;
	int		fret=NO_ERROR;
	if (!gimme_state.quiet) gimmeLog(LOG_STAGE, "Scanning folders for new files...");
	file_list = fileScanDirFolders(localdir, FSF_NOHIDDEN | FSF_FILES | (gimme_state.no_underscore?0:FSF_UNDERSCORED));
    count = eaSize( &file_list );
	for(i=0;i<count;i++)
	{
		relpath = file_list[i];
		relpath = findRelPath(relpath, gimme_dir);
		if (!strEndsWith(relpath, ".bak")) {
			GimmeNode *node;
			int rev;
			rev = makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
			if (node==NULL) {
				if (NO_ERROR!=(ret=gimmeDoOperationRelPath(gimme_dir,relpath,cur_operation,0))) {
					fret = ret;
				}
			} else if (gimme_state.do_extra_checks && !gimmeIsBinFile(gimme_dir, relpath)) {
				// Find the node for the latest version
				lockee = isLocked(gimme_dir, relpath);
				makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
				if (lockee && stricmp(lockee, gimmeGetUserName())==0) {
					// It's checked out by us, don't bother
				} else {
					FWStatType statbuf;
					int readonly=1;
					int newer=0;
					pststat(file_list[i], &statbuf);
					if (statbuf.st_mtime > node->timestamp) {
						newer=1;
					}
					if (statbuf.st_mode & _S_IWRITE) {
						readonly=0;
					}
					if (newer && !readonly) {
						// It's writeable, modified, and is not a new file and is not checked out by us, bad!
						gimmeLog(LOG_WARN_HEAVY, "WARNING, file on local drive is not checked out but newer than latest version and writeable (%s), ignoring file", relpath);
					} else if (newer) {
						// It's newer, but not writeable, and is not a new file and is not checked out by us, bad!
						gimmeLog(LOG_WARN_LIGHT, "WARNING, file on local drive is not checked out but newer than latest version (%s), ignoring file", relpath);
					} else if (!readonly) {
						// It's writeable, and is not a new file and is not checked out by us, bad!
						gimmeLog(LOG_WARN_LIGHT, "WARNING, file on local drive has been changed to writeable (%s), ignoring file, making read-only", relpath);
						_chmod(file_list[i], _S_IREAD);
					}
				}
			}
		}
	}
	fileScanDirFreeNames(file_list);
	return fret;
}

static int comment_level=-1;
static int comment_override=0;
CommentLevel getCommentLevel(const char *path)
{
	if (comment_level==-1) {
		RegReader reader;
		reader = createRegReader();
		initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
		if (!rrReadInt(reader, "CommentLevel", &comment_level)) {
			comment_level = CM_ASK;
		}
		destroyRegReader(reader);
	}
	if (!comment_override) {
		// HACK!
		if (path && (strstri((char*)path, "data")!=0 || strstri((char*)path, "test")!=0)) {
			// It's the data tree
			return CM_REQUIRED;
		}
	}
	return comment_level;
}

void setCommentLevelOverride(CommentLevel level)
{
	comment_override = 1;
	comment_level = level;
}

void setCommentLevel(CommentLevel level)
{
	if (level>=CM_DONTASK && level<=CM_REQUIRED) {
		RegReader reader;
		reader = createRegReader();
		initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
		rrWriteInt(reader, "CommentLevel", level);
		destroyRegReader(reader);
		printf("Comment level set to %d\n", getCommentLevel(NULL));
	} else {
		assert(0);
	}
}

int doQueuedActions(void)
{
	// Shouldn't ever get in here except for when called through gimmeMain.c
	if (!gimme_state.queuedActionCallback)
		FatalErrorf("Gimme is trying to do queued actions, but it's not the standalone version, so it doesn't know how!  Get Jimb?");
	if (gimme_state.queuedActionCallback)
		return gimme_state.queuedActionCallback();
	return 0;
}

static int rmFoldByNode(GimmeDir *gimme_dir, const char *relpath, int quiet) {
	gimme_state.nowarn=1;
	return gimmeDoOperationRelPath(gimme_dir, relpath, GIMME_DELETE, quiet);
}

static int checkoutFoldByNode(GimmeDir *gimme_dir, const char *relpath, int quiet) {
	int ret;
	gimme_state.nowarn=1;
	ret = gimmeDoOperationRelPath(gimme_dir, relpath, GIMME_CHECKOUT, quiet);
	if (ret==GIMME_ERROR_ALREADY_DELETED)
		return NO_ERROR;
	return ret;
}

static int checkinFoldByNode(GimmeDir *gimme_dir, const char *relpath, int quiet) {
	char *lock;
	if ((cur_operation==GIMME_CHECKIN || cur_operation==GIMME_UNDO_CHECKOUT) && (!(lock=isLocked(gimme_dir, relpath)) || (stricmp(lock, gimmeGetUserName())!=0)))
		return NO_ERROR;
	gimme_state.nowarn=1;
	return gimmeDoOperationRelPath(gimme_dir, relpath, cur_operation, quiet);
}

static int showLockedFilesFoldByNode(GimmeDir *gimme_dir, const char *relpath, int quiet) {
	char *username;
	username = isLocked(gimme_dir, relpath);
	if (username && (!cur_myname || stricmp(username,cur_myname)==0))
	{
		gimmeLog(LOG_STAGE, "\t%-8.8s  %s",username,relpath);
		count++;
	}
	return NO_ERROR;
}


static bool nodeHasSubFolder(GimmeNode *node) {
	node = node->contents;
	while (node) {
		if (node->is_dir)
			return true;
		node = node->next;
	}
	return false;
}

static int g_nodesTotal=0;
static int g_nodesVisited=0;
static void statusCallbackScanning(int nodesVisited, int nodesTotal)
{
	static int nextUpdate=0;
	static int numPerUpdate=1;
	char buf[256];
	if (nodesTotal!=-1) {
		g_nodesTotal = nodesTotal;
		g_nodesVisited = nodesVisited;
		nextUpdate = 0;
		numPerUpdate = nodesTotal / 1000;
	} else {
		g_nodesVisited += nodesVisited;
	}
	if (g_nodesVisited == g_nodesTotal ||
		g_nodesVisited >= nextUpdate)
	{
		nextUpdate = nextUpdate+numPerUpdate;
		if (gimme_state.num_command_line_operations) {
			sprintf_s(SAFESTR(buf), "Gimme Stage %d of %d: ", gimme_state.cur_command_line_operation*2-1, gimme_state.num_command_line_operations*2);
		} else {
			sprintf_s(SAFESTR(buf), "Gimme: ");
		}
		strcatf(buf, "Scanning files %d of %d (%4.1f%%)... %5.1f KB/s", g_nodesVisited, g_nodesTotal, g_nodesVisited * 100.f / g_nodesTotal, threadedFileCopyBPS()/1024.f);
		setConsoleTitle(buf);
	}
}

static void statusCallbackCopying(int left, int numThreads, int *threadCounts) {
	static bool hitonce=false;   
	static int last_left;
	static int last_file_get_count;
	if (left>0) {
		hitonce=true;
	} else if (left==-1) {
		left = last_left + gimme_state.file_get_count - last_file_get_count;
	}
	if (hitonce && gimme_state.file_get_count && (left != last_left || gimme_state.file_get_count != last_file_get_count) ) {
		char buf[256];
		if (gimme_state.num_command_line_operations) {
			sprintf_s(SAFESTR(buf), "Gimme Stage %d of %d: ", gimme_state.cur_command_line_operation*2, gimme_state.num_command_line_operations*2);
		} else {
			sprintf_s(SAFESTR(buf), "Gimme: ");
		}
		if (left==0)
			hitonce=false; // Rest it
		if (gimme_state.cur_operation_section==0) {
			// Still scanning files too
			return; // Do nothing!
			//strcatf(buf, "Scanning files %d of %d (%2d%%)... ", g_nodesVisited, g_nodesTotal, g_nodesVisited * 100 / g_nodesTotal);
			//strcatf(buf, "Copying files...  %d of %d", gimme_state.file_get_count - left, gimme_state.file_get_count);
		} else {
			// Display copying files status
			strcatf(buf, "Copying files...  %d of %d (%2d%%)", gimme_state.file_get_count - left, gimme_state.file_get_count, (gimme_state.file_get_count - left)*100/gimme_state.file_get_count);
		}
//#ifdef _DEBUG
//		{
//			int i;
//			strcat(buf, "  ");
//			for (i=0; i<numThreads; i++) {
//				strcatf(buf, "%d/", threadCounts[i]);
//			}
//			if (numThreads)
//				buf[strlen(buf)-1]=0;
//		}
//#endif
		strcatf(buf, " %5.1f KB/s", threadedFileCopyBPS()/1024.f);
		setConsoleTitle(buf);
	}
	if (left>0) {
		last_left = left;
	}
	last_file_get_count = gimme_state.file_get_count;
}

static void statusCallbackDone(void)
{
	char buf[256];
	if (gimme_state.num_command_line_operations) {
		sprintf_s(SAFESTR(buf), "Gimme Stage %d of %d: ", gimme_state.cur_command_line_operation*2, gimme_state.num_command_line_operations*2);
	} else {
		sprintf_s(SAFESTR(buf), "Gimme: ");
	}
	strcatf(buf, "Done.");
	setConsoleTitle(buf);
}

static int countVersionNodes(GimmeNode *node)
{
	int count=0;
	for (;; node = node->next) {
		if (node==NULL) break;
		if (!node->is_dir) continue;
		if (gimme_state.no_underscore && node->name[0]=='_') continue;
		if (!nodeHasSubFolder(node) && strEndsWith(node->name, "_versions")) {
			if (strEndsWith(node->name, ".bak_versions")) continue;
			count++;
		} else { // must be a normal folder
			count+=countVersionNodes(node->contents);
		}
	}
	return count;
}

static char *getRelPathFromVersionNode(GimmeNode *node, char *outbuf, size_t outbuf_size)
{
	strcpy_s(outbuf, outbuf_size, gimmeNodeGetFullPath(node));
	outbuf[strlen(outbuf)-strlen("_versions")]=0;
	return outbuf;
}

// Operates on all folder nodes that end in "_versions"
static int doByNodeOnVersions(GimmeDir *gimme_dir, GimmeNode *node, int quiet, 
							  int (*relPathFunc)(GimmeDir *, const char *relpath, int),
							  int (*nodeFunc)(GimmeDir *, GimmeNode *node, int)
							  )
{
	int ret;
	int finalret=NO_ERROR;

	for (;; node = node->next) {
		threadedFileCopyPoll(statusCallbackCopying);
		flushStatus();
		if (node==NULL) break;
		if (!node->is_dir) continue;
		if (gimme_state.no_underscore && node->name[0]=='_') continue;
		if (!nodeHasSubFolder(node) && simpleMatch("*_versions", node->name)) {
			// get rel path
			if (strEndsWith(node->name, ".bak_versions")) continue;
			statusCallbackScanning(1, -1);
			// actually call the worker function
			if (relPathFunc) {
				char relpath[CRYPTIC_MAX_PATH];
				getRelPathFromVersionNode(node, SAFESTR(relpath));
				ret = relPathFunc(gimme_dir, relpath, quiet);
			} else {
				ret = nodeFunc(gimme_dir, node, quiet);
			}
			if (ret!=NO_ERROR) {
				finalret=ret;
			}
		} else { // must be a normal folder
			//assert(!simpleMatch("*_versions", node->name)); Actual folders *might* end in _Versions too!
			ret = doByNodeOnVersions(gimme_dir, node->contents, quiet, relPathFunc, nodeFunc);
			if (ret!=NO_ERROR) {
				finalret=ret;
			}
		}
	}
	return finalret;
}


// Does basic setup/checking that is needed for any by folder operation (rm, checkout)
int doByFold(const char *folder_in, int quiet,
						int (*relPathFunc)(GimmeDir *, const char*, int),
						int (*nodeFunc)(GimmeDir *, GimmeNode *, int),
						char *statusmessage,
						int (*auxfunc)(GimmeDir *, char*),
						int allow_queueing)
{
	char *fname, *relpath;
	char fullpath[CRYPTIC_MAX_PATH];
	GimmeDir *gimme_dir;
	int finalret=NO_ERROR;
	GimmeNode *node;
	char folder[CRYPTIC_MAX_PATH];
	char buf[CRYPTIC_MAX_PATH];
	int ret;

	assert(!relPathFunc ^ !nodeFunc);

	strcpy(folder, folder_in);

	// *** initialize stuff, do error checking
	if (!fileIsAbsolutePath(folder)) {
		fname = fileLocateWrite((char*)folder, buf);
	} else {
		strcpy(buf, folder);
		fname = buf;
	}
	if (fname==NULL) {
		if (!quiet) gimmeLog(LOG_FATAL, "Cannot locate folder or no folder specified.\n");
		return GIMME_ERROR_FILENOTFOUND;
	}
	if (!dirExists(folder)) {
		strcat(folder, "/");
		mkdirtree(folder);
		if (!dirExists(folder)) {
			if (!quiet) gimmeLog(LOG_FATAL, "Cannot locate folder or no folder specified.\n");
			return GIMME_ERROR_FILENOTFOUND;
		}
	}
	makefullpath((char*)fname,fullpath);
	forwardSlashes(fullpath);
	gimme_dir = findGimmeDir(fullpath);
	if (!gimme_dir)
	{
		if (!quiet) gimmeLog(LOG_FATAL, "Can't find source control folder to match %s!\n",fullpath);
		return GIMME_ERROR_NODIR;
	}

	// convert pathname to relative
	relpath = findRelPath(fullpath, gimme_dir);

	status=0;

	if (NO_ERROR!=(ret=gimmeDirDatabaseLoad(gimme_dir, fullpath))) {
		gimmeLog(LOG_FATAL, "Error loading database\n");
		return ret;
	}

	if (allow_queueing && getCommentLevel(gimme_dir->local_dir) != CM_DONTASK) {
		gimme_state.just_queue=1;
	}
	// *** find node
	if (relpath==NULL || relpath[0]=='\0' || stricmp(relpath, "/")==0) {
		node = gimme_dir->database->root;
	} else {
		node = gimmeNodeFind(gimme_dir->database->root->contents, relpath);
	}

	// handle empty node
	if (node==NULL || node->contents==NULL) { // no node found, just run the auxfunc if there is one
		if (auxfunc) {
			finalret = auxfunc(gimme_dir, fullpath);
		} else { // if there is no aux func, then we know they wanted a node, let them know it wasn't there
			gimmeLog(LOG_FATAL, "Error finding folder \"%s\", it does not exist in the database.\n", relpath);
			// Also, is error can occur when there is an empty database and you try to do a folder operation on it... this will go away after one checkin!
			finalret = GIMME_ERROR_NOT_IN_DB;
		}
		if (allow_queueing && getCommentLevel(gimme_dir->local_dir) != CM_DONTASK) {
			gimme_state.just_queue=0;
			if (ret==NO_ERROR) {
				ret = doQueuedActions();
			}
		}
		gimmeDirDatabaseClose(gimme_dir);
		return finalret;
	}


	// Print a status message
	gimmeLog(LOG_STAGE, FORMAT_OK(statusmessage), gimme_dir->local_dir, relpath);

	statusCallbackScanning(0, countVersionNodes(node->contents));
	// Run the function on each node
	finalret = doByNodeOnVersions(gimme_dir, node->contents, quiet, relPathFunc, nodeFunc);

	// run auxfunc
	if (auxfunc) {
		ret = auxfunc(gimme_dir, fullpath);
		if (ret!=NO_ERROR)
			finalret=ret;
	}

	if (allow_queueing && getCommentLevel(gimme_dir->local_dir) != CM_DONTASK) {
		gimme_state.just_queue=0;
		if (ret==NO_ERROR) {
			ret = doQueuedActions();
		}
	}
	gimmeDirDatabaseClose(gimme_dir);
	return finalret;
}

int rmFold(char *folder, int quiet) {
	char temp[512];
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;
	sprintf_s(SAFESTR(temp), "Are you sure you want to delete \"%s\"\n   and all its contents from the local drive and revision control database?", folder);
	if (!gimme_state.nowarn && IDNO==MessageBox_UTF8(NULL, temp, "Confirm Folder Delete", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
		gimme_state.pause=0;
		return NO_ERROR;
	}
	return doByFold(folder, quiet, rmFoldByNode, NULL, "Removing files in folder \"%s%s\"...", NULL, 0);
}


int checkoutFold(char *folder, int quiet) {
	int ret;
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;
	gimme_state.editor="-NULL-";
	ret = doByFold(folder, quiet, checkoutFoldByNode, NULL, "Checking out files in folder \"%s%s\"...", NULL, 0);
	if (ret==NO_ERROR) {
		gimmeLog(LOG_STAGE, "All files checked out.");
	} else {
		gimmeLog(LOG_STAGE, "All other files checked out.");
	}
	return ret;
}

int undoCheckoutFold(char *folder, int quiet) {
	int ret;
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	cur_operation = GIMME_UNDO_CHECKOUT;
	gimme_state.file_update_count=0;
	gimme_state.remove_count=0;
	gimme_state.new_file_count=0;
	gimme_state.undo_checkout_count=0;
	ret=doByFold(folder, quiet, checkinFoldByNode, NULL, "Undoing checkout on files in folder \"%s%s\"...", NULL, 0);

	gimmeLog(LOG_STAGE, "%d checkout%s %sundone.", gimme_state.undo_checkout_count, gimme_state.undo_checkout_count==1?"":"s", gimme_state.simulate?"(would be) ":"");

	return ret;
}

int checkinFold(char *folder, int quiet) {
	int ret;
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;
	cur_operation = GIMME_CHECKIN;
	gimme_state.file_update_count=0;
	gimme_state.remove_count=0;
	gimme_state.new_file_count=0;
	gimme_state.undo_checkout_count=0;
	ret=doByFold(folder, quiet, checkinFoldByNode, NULL, "Checking files in folder \"%s%s\"...", addNewFiles, 1);

	gimmeLog(LOG_STAGE, "%d file%s %s%s.", gimme_state.file_update_count-gimme_state.new_file_count, (gimme_state.file_update_count-gimme_state.new_file_count)==1?"":"s", gimme_state.simulate?"(would be) ":"", gimme_state.leavecheckedout?"checkpointed":"checked in");
	if (gimme_state.remove_count)
		gimmeLog(LOG_STAGE, "%d file%s %sremoved from database.", gimme_state.remove_count, gimme_state.remove_count==1?"":"s", gimme_state.simulate?"(would be) ":"");
	if (gimme_state.new_file_count)
		gimmeLog(LOG_STAGE, "%d new file%s %sadded.", gimme_state.new_file_count, gimme_state.new_file_count==1?"":"s", gimme_state.simulate?"(would be) ":"");
	if (gimme_state.undo_checkout_count)
		gimmeLog(LOG_STAGE, "%d checkout%s %sundone.", gimme_state.undo_checkout_count, gimme_state.undo_checkout_count==1?"":"s", gimme_state.simulate?"(would be) ":"");

	return ret;
}

int forcePutFold(char *folder, int quiet) {
	int ret;
	cur_operation = GIMME_FORCECHECKIN;
	ret=doByFold(folder, quiet, checkinFoldByNode, NULL, "Force checking in files in folder \"%s%s\"...", addNewFiles, 0);
	return ret;
}

int showLockedFilesFold(char *folder, int quiet, const char *username) {
	int ret;
	cur_myname = username;
	ret = doByFold(folder, quiet, showLockedFilesFoldByNode, NULL, "Locked files in folder \"%s%s\"...", NULL, 0);
	return ret;
}

int checkinFoldByDb(GIMMEOperation operation, int dir_num_min, int dir_num_max, int quiet)
{
	int		j;
	int gimme_pause_save = gimme_state.pause;
	int finalret=0;
	gimme_state.pause=0;

	gimmeLoadConfig();

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	if (dir_num_max >= eaSize(&eaGimmeDirs) || dir_num_max==-1) {
		dir_num_max = eaSize(&eaGimmeDirs)-1;
	}

	cur_operation = operation;
	for(j=dir_num_min;j<=dir_num_max;j++) {
		int ret;
		count=0;
		ret = checkinFold(eaGimmeDirs[j]->local_dir, quiet);
		if (ret)
			finalret = ret;
	}
	gimme_state.pause = gimme_pause_save;
	return finalret;
}


int showLockedFilesByDb(const char *myname, int dir_num_min, int dir_num_max, int quiet)
{
	int		j;
	int gimme_pause_save = gimme_state.pause;
	int finalret=0;
	gimme_state.pause=0;

	gimmeLoadConfig();

	if (dir_num_max >= eaSize(&eaGimmeDirs) || dir_num_max==-1) {
		dir_num_max = eaSize(&eaGimmeDirs)-1;
	}

	for(j=dir_num_min;j<=dir_num_max;j++) {
		int ret;
		count=0;
		ret = showLockedFilesFold(eaGimmeDirs[j]->local_dir, quiet, myname);
		if (ret)
			finalret = ret;
	}
	gimme_state.pause = gimme_pause_save;
	return finalret;
}

static const char *g_revert_leave_filename;
LRESULT CALLBACK DlgRevertLeaveProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_FILENAME), g_revert_leave_filename);

			flashWindow(hDlg);
			return FALSE;
		}
		break;
	case WM_COMMAND:
		{
			int cmdNum = LOWORD(wParam);
			if (cmdNum == IDC_LEAVE ||
				cmdNum == IDC_LEAVEALL ||
				cmdNum == IDC_REVERT ||
				cmdNum == IDC_REVERTALL)
			{
				EndDialog(hDlg, cmdNum);
				return TRUE;
			}
		}
		break;
	}
	return FALSE;
}
// The caller is responsible for printing the contents of statusbuf after a call to this func (if status=1)
// It is assumed that the caller opened the database
// versionsNode can be passed in as a pointer to the version folder for this file for speedier access
static int getLatestVersionFile(GimmeDir *gimme_dir, const char *relpath, GimmeNode *versionNode, int revision, int quiet, bool allowAsync) {
	char localname[CRYPTIC_MAX_PATH];
	char *username;
	bool make_readonly=true;
	int b;
	GimmeNode *node;
	FWStatType filestat;
	bool readonly=true;
	bool remove_by_branch=false;
	bool file_exists;
	int ret;
	
	assert(gimme_dir->database);

	// Check for exclusions
	{
		ExclusionType extype = gimmeCheckExclusion(gimme_dir, relpath);
		if (extype>ET_ERROR) {
			return extype - ET_ERROR;
		}
		if (extype==ET_EXCLUDED || extype==ET_NOTINFILESPEC) {
			return NO_ERROR;
		}
	}

	// check to see if this is a bin file, which gets handled differently
	if (gimmeIsBinFile(gimme_dir, relpath)) make_readonly=0;

	sprintf_s(SAFESTR(statusbuf), "%s: ", relpath);
	status=0;
	makeLocalNameFromRel(gimme_dir, relpath, localname);
	if (!versionNode) {
		char lockdir[CRYPTIC_MAX_PATH];
		sprintf_s(SAFESTR(lockdir),"%s_versions/", relpath);
		versionNode = gimmeNodeFind(gimme_dir->database->root->contents, lockdir);
	}
	// check check-outs
	username = isLockedQuick(gimme_dir, relpath, versionNode);
	if (username) {
		if (stricmp(username, gimmeGetUserName())==0) {
			make_readonly=0;
			if (!fileExists(localname) || revision==REV_BLEEDINGEDGE_REVERT) {
				// get latest version or leave it alone, depending
				bool shouldRevert;
				if (revision==REV_BLEEDINGEDGE_REVERT || gimme_state.ignore_errors || gimme_state.what_to_do_when_getting_latest_on_deleted == GLOD_REVERTALL)
				{
					// Old behavior and if they clicked Yes To All
					shouldRevert = true;
				} else if (gimme_state.what_to_do_when_getting_latest_on_deleted == GLOD_LEAVEALL) {
					// They clicked No To All
					shouldRevert = false;
				} else {
					// Prompt!
					int ret;
					bool bRetry=false;
					g_revert_leave_filename = localname;
					do {
						bRetry = false;
						ret = (int)DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_MB_REVERT_OR_LEAVE), NULL, (DLGPROC)DlgRevertLeaveProc);
						if (ret == IDC_REVERT) {
							if (IDYES==MessageBox_UTF8(NULL, "Are you sure you wish to revert changes to this file?", "Confirm Revert", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING))
								shouldRevert = true;
							else
								bRetry = true;
						} else if (ret == IDC_REVERTALL) {
							if (IDYES==MessageBox_UTF8(NULL, "Are you sure you wish to revert changes to this file and all other files you have deleted after checking them out?", "Confirm Revert", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING)) {
								shouldRevert = true;
								gimme_state.what_to_do_when_getting_latest_on_deleted = GLOD_REVERTALL;
							} else
								bRetry = true;
						} else if (ret == IDC_LEAVE) {
							shouldRevert = false;
						} else {
							assert(ret == IDC_LEAVEALL);
							shouldRevert = false;
							gimme_state.what_to_do_when_getting_latest_on_deleted = GLOD_LEAVEALL;
						}
					} while (bRetry);
				}
				if (!shouldRevert) {
					if (gimme_state.fail_on_checkedout){
						strcat(statusbuf, "Error: File already checked out by you.");
						status=1;
						return GIMME_ERROR_ALREADY_CHECKEDOUT;
					}
					else {
						strcat(statusbuf, "file already checked out by you, not reverting.");
						if (!quiet) status=2;
						return NO_ERROR;
					}
				}
				filelog_printf("c:\\GimmeRevert", "Reverting file %s", localname);
				strcat(statusbuf, "file checked out by you, but has been deleted, getting latest version... ");
				status=2;
//				revision = REV_BLEEDINGEDGE_FORCE;
			} else if (revision!=REV_BLEEDINGEDGE_FORCE && !gimme_state.force_get_latest) {
				if (gimme_state.fail_on_checkedout){
					strcat(statusbuf, "Error: File already checked out by you.");
					status=1;
					return GIMME_ERROR_ALREADY_CHECKEDOUT;
				}
				else {
					strcat(statusbuf, "file already checked out by you.");
					if (!quiet) status=2;
					return NO_ERROR;
				}
			}
		} else {
//			strcat(statusbuf, "checked out by ");
//			strcat(statusbuf, username);
//			strcat(statusbuf, "; ");
		}
	}
	// check to see if we already have the latest version
	if (-1==makeDBNameQuick(gimme_dir, versionNode, revision, &node, relpath)) {
		// It doesn't exist in this branch, check to see if it's in a later one
		if (-1!=getHighestVersionFromNode(gimme_dir, versionNode, NULL, gimmeGetMaxBranchNumber(), relpath)) {
			// It exists in a later branch, remove this one
			remove_by_branch=true;
		} else {
			strcat(statusbuf, "No existing version found in database!");
			status=1;
			return GIMME_ERROR_NOT_IN_DB;
		}
	}
	// test filestamps
	file_exists = 0==pststat(localname, &filestat);
	if (!remove_by_branch && file_exists) {
		// test for un-readonly
		if (filestat.st_mode & _S_IWRITE ) { // writeable
			readonly=false;
		}
		b=  (filestat.st_mtime >= node->timestamp-IGNORE_TIMEDIFF) &&
			(filestat.st_mtime <= node->timestamp+IGNORE_TIMEDIFF) &&
			((size_t)filestat.st_size == node->size); 
		if ((filestat.st_mtime == node->timestamp+3600 || filestat.st_mtime+3600 == node->timestamp)
			&& filestat.st_size == node->size && readonly)
		{
			b = true;
		}

		if (b && !gimmeFileIsDeletedFile(node->name) && !gimme_state.force_get_latest) { // timestamps equal
			strcat(statusbuf, "File already up to date.");
			if (!gimme_state.simulate) {
				if (make_readonly && (filestat.st_mode & _S_IWRITE)) {
					_chmod(localname, _S_IREAD);
				} else if (!make_readonly && !(filestat.st_mode & _S_IWRITE)) {
					_chmod(localname, _S_IREAD | _S_IWRITE);
				}
			}
			return NO_ERROR;
		}
		// test for overwriting newer file (unless we want the approved version)
		if (revision!=REV_BYTIME && !gimme_state.doing_branch_switch && !gimmeIsBinFile(gimme_dir, relpath)) {
			b = filestat.st_mtime > node->timestamp+IGNORE_TIMEDIFF;
			if ((filestat.st_mtime == node->timestamp+3600 || filestat.st_mtime+3600 == node->timestamp)
				&& filestat.st_size == node->size && readonly) {
				// Off by exactly one hour, but the same size and it's read-only, assume DST bug
				b = false;
			}
			if (b && !readonly) { // Timestamps say they're different, but are they really?
				char dbname[CRYPTIC_MAX_PATH];
				makeDBNameFromNode(gimme_dir, node, dbname);
				b = (!gimme_state.ignore_diff&&fileCompare(localname, dbname)!=0);
				if (!b) {
					readonly = 1; // fake it so that the message below doesn't get printed
				}
			}
			if (b) {
				// file on drive is newer than in database

				if (gimmeFileIsDeletedFile(node->name) && !readonly) {
					// the database one is deleted, but we have a newer one, let's just leave the local one,
					// they are probably trying to re-add something
					return NO_ERROR;
				}
				if (!readonly) { // And it's writeable!!!
					strcat(statusbuf, "WARNING, file on local drive is not checked out but newer than latest version and writeable\n\tRenaming file to .bak and getting latest...");
					status=1;
				} else {
					if (!gimme_state.nowarn) strcat(statusbuf, "WARNING, file on local drive is not checked out but newer than latest version\n\tRenaming file to .bak and getting latest...");
					if (status==0 || status==3) status=2;
				}
				if (!gimme_state.simulate) fileRenameToBak(localname);
			}
		}
		if (!readonly && !status && !gimme_state.doing_branch_switch && !gimmeIsBinFile(gimme_dir, relpath)) {
			if (!gimme_state.nowarn) {
				strcat(statusbuf, "WARNING, file on local drive has been changed to writeable\n\tRenaming to .bak and getting latest...");
				status=2;
			}
			if (!gimme_state.simulate) fileRenameToBak(localname);
		}
	}

	if ((remove_by_branch || gimmeFileIsDeletedFile(node->name)) && !(revision==REV_BLEEDINGEDGE_FORCE || revision==REV_BLEEDINGEDGE_REVERT || gimme_state.force_get_latest)) {
		// File to be deleted
		if (file_exists) {
			if (!gimme_state.simulate) {
				deleteLocalFile(gimme_dir, localname,false);
				gimme_state.file_get_count++;
				strcat(statusbuf, "Deleted.");
			} else {
				strcat(statusbuf, "(would be deleted).");
			}
			if (status==0 || status==3) status=2;
		}
	} else if (!remove_by_branch) {
		if (!quiet && !status) {
			status=3;
			strcat(statusbuf, " (copying...)");
			flushStatus();
			statusbuf[strlen(statusbuf) - strlen(" (copying...)")]=0;
		}
		if (allowAsync) {
			// Asynchronous copy
			if ((ret=copyFileFromDBAsync(gimme_dir, relpath, revision, 0, make_readonly))!=NO_ERROR) {
				strcat(statusbuf, " ERROR copying file!");
				status=1;
				return ret;
			} else {
				strcat(statusbuf, " Copy queued.");
				if (status==0 || status==3) status=2;
				statusCallbackCopying(-1, 0, NULL);
				gimme_state.file_get_count++;
			}
		} else {
			if ((ret=copyFileFromDB(gimme_dir, relpath, revision, 0, make_readonly))!=NO_ERROR) {
				strcat(statusbuf, " ERROR copying file!");
				status=1;
				return ret;
			} else {
				if (!gimme_state.simulate) {
					strcat(statusbuf, "Successfully replaced.");
				} else {
					strcat(statusbuf, "(would be replaced).");
				}
				if (!status) status=2;
				gimme_state.file_get_count++;
			}
		}
	}

	return NO_ERROR;
}

static int cur_revision=-1;

static int getVersionByNode(GimmeDir *gimme_dir, GimmeNode *node, int quiet) {
	char relpath[CRYPTIC_MAX_PATH];
	getRelPathFromVersionNode(node, SAFESTR(relpath));
	return getLatestVersionFile(gimme_dir, relpath, node, cur_revision, quiet, !gimme_state.no_async);
}

static int getAll(const char *folder, int revision, int quiet) {
	int ret;
	F32 elapsed;
	F32 scanningElapsed = 0.0f;
	bool displayed_scanning_notice=false;
	gimme_state.file_get_count=0;
	cur_revision=revision;

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	checkFileWatcherConsistency(folder);

	gimme_state.timer = timerAlloc();
	gimme_state.bytes_transfered = 0;
	gimme_state.no_async = gimmeGetOption("Verify"); // No async if verify is on.
	gimme_state.cur_operation_section = 0;

	ret = doByFold(folder, quiet, NULL, getVersionByNode, "Checking local files in folder \"%s%s\"...", NULL, 0);
	if (threadedFileCopyNumPending()) {
		scanningElapsed = timerElapsed(gimme_state.timer);
		gimmeLog(LOG_STAGE, "Scanning files completed in %1.2f seconds, copying files...", scanningElapsed);
		displayed_scanning_notice = true;
	}
	gimme_state.cur_operation_section = 1;
	threadedFileCopyWaitForCompletion(statusCallbackCopying);
	gimmeFlushAllHogFiles();
	elapsed = timerElapsed(gimme_state.timer);

	if (displayed_scanning_notice) {
		gimmeLog(LOG_STAGE, "%d file(s) updated (%1.2f KB, %1.2f seconds scanning, %1.2f seconds total, %1.2f KB/s).", gimme_state.file_get_count, gimme_state.bytes_transfered/1024.f, scanningElapsed, elapsed, gimme_state.bytes_transfered/1024.f/elapsed);
	} else {
		gimmeLog(LOG_STAGE, "%d file(s) updated (%1.2f KB, %1.2f seconds total, %1.2f KB/s).", gimme_state.file_get_count, gimme_state.bytes_transfered/1024.f, elapsed, gimme_state.bytes_transfered/1024.f/elapsed);
	}
	gimmePerfLogf("files,%d, kb,%1.2f, seconds,%1.2f, kbps,%1.2f", gimme_state.file_get_count, gimme_state.bytes_transfered/1024.f, elapsed, gimme_state.bytes_transfered/1024.f/elapsed);
	timerFree(gimme_state.timer);
	gimme_state.timer = 0;
	gimme_state.cur_operation_section = 0;
	statusCallbackDone();
	return ret;
}

void gimmeGettingLatestOn(int gimme_dir_num)
{
	if (-1!=eaiFind(&gimme_state.databases_to_update_hoggs, gimme_dir_num))
		return; // Already done
	eaiPush(&gimme_state.databases_to_update_hoggs, gimme_dir_num);
}
static int *databases_to_force_update_hoggs;
void gimmeForceHoggUpdate(int gimme_dir_num)
{
	if (-1!=eaiFind(&databases_to_force_update_hoggs, gimme_dir_num))
		return; // Already done
	eaiPush(&databases_to_force_update_hoggs, gimme_dir_num);
}
bool gimmeIsForcedHoggUpdate(int gimme_dir_num)
{
	return eaiFind(&databases_to_force_update_hoggs, gimme_dir_num)!=-1;
}


int gimmeGetVersionByTime(int gimme_dir_num, __time32_t time, int quiet)
{
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	gimmeLoadConfig();
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: get version by time called when no source control folders are configured (check %s)", config_file2);
		if (gimme_state.command_line!=NULL) {
			gimmeOfflineLog("command line: %s", gimme_state.command_line);
		}
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: get version by time called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeGettingLatestOn(gimme_dir_num);
	gimme_state.dateToGet = time;
	return getAll(eaGimmeDirs[gimme_dir_num]->local_dir, REV_BYTIME, quiet);
}

int gimmeGetFolderVersionByTime(int gimme_dir_num, char *folder, __time32_t time, int quiet)
{
	char fullPath[MAX_PATH];
	char folder_fullpath[MAX_PATH];
	makefullpath((char*)folder,folder_fullpath);
	forwardSlashes(folder_fullpath);

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	gimmeLoadConfig();
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: get version by time called when no source control folders are configured (check %s)", config_file2);
		if (gimme_state.command_line!=NULL) {
			gimmeOfflineLog("command line: %s", gimme_state.command_line);
		}
		return GIMME_ERROR_NODIR;
	}
	if (isFullPath(folder)) {
		GimmeDir *gimme_dir;
		gimme_dir = findGimmeDir(folder_fullpath);
		if (gimme_dir)
			gimme_dir_num = eaFind(&eaGimmeDirs, gimme_dir);
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: get version by time called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeGettingLatestOn(gimme_dir_num);
	gimme_state.dateToGet = time;
	strcpy_s(SAFESTR(fullPath), eaGimmeDirs[gimme_dir_num]->local_dir);
	if ( isFullPath(folder) )
	{
		unsigned char *c = folder_fullpath, *d = fullPath;
		while ( c && d && *c && *d && 
			(tolower(*c) == tolower(*d) || (*c == '\\' && *d == '/') || (*c == '/' && *d == '\\')) )
		{
			c++; d++;
		}
		strcat(fullPath, c);
	}
	return getAll(fullPath, REV_BYTIME, quiet);
}

int getLatestVersionFolderEx(const char *folder, int revision, int quiet, int do_search)
{
	bool did_something=false;
	int gimme_dir_num;
	int ret=0;
	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;
	gimme_dir_num = eaFind(&eaGimmeDirs, findGimmeDir(folder));
	if (gimme_dir_num != -1)
		gimmeGettingLatestOn(gimme_dir_num);
	if (gimme_dir_num == -1 && do_search) {
		int i;
		// Not found, search for subfolders that match
		for(i=0;i<eaSize(&eaGimmeDirs);i++)
		{
			if (strnicmp(eaGimmeDirs[i]->local_dir,folder,strlen(folder))==0 && isslashornull(eaGimmeDirs[i]->local_dir[strlen(folder)])) {
				did_something = true;
				ret |= getLatestVersionFolderEx(eaGimmeDirs[i]->local_dir, revision, quiet, 0);
			}
		}

	}

	if (!did_something)
		ret|=getAll(folder, revision, quiet);
	return ret;
}

int getLatestVersionFolder(const char *folder, int revision, int quiet)
{
	return getLatestVersionFolderEx(folder, revision, quiet, 0); // Changing this to 1 will cause it to recursively search for things to get latest on
}

// Gets the latest/bleeding edge version
int gimmeGetLatestVersion(int gimme_dir_num, int quiet)
{
	gimmeLoadConfig();

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: get latest version called when no source control folders are configured (check %s)", config_file2);
		if (gimme_state.command_line!=NULL) {
			gimmeOfflineLog("command line: %s", gimme_state.command_line);
		}
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: -getlatest called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeGettingLatestOn(gimme_dir_num);
	return getAll(eaGimmeDirs[gimme_dir_num]->local_dir, REV_BLEEDINGEDGE, quiet);
}

// Gets the approved version by grabbing all revisions with a timestamp older than the approved date, or the oldest
// if none such exists
int gimmeGetApprovedVersion(int gimme_dir_num, int quiet)
{
	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: get approved version called when no source control folders are configured (check %s)", config_file2);
		if (gimme_state.command_line!=NULL) {
			gimmeOfflineLog("command line: %s", gimme_state.command_line);
		}
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: get approved version called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeGettingLatestOn(gimme_dir_num);
	gimme_state.dateToGet = eaGimmeDirs[gimme_dir_num]->dateOfApproval;
	return getAll(eaGimmeDirs[gimme_dir_num]->local_dir, REV_BYTIME, quiet);
}

char *sharedMemoryGetUserName(void)
{
	static char *username = NULL;
	const int nameLen = 64;
	SharedMemoryHandle *shared_memory=NULL;
	SM_AcquireResult ret;

	if ( username )
		return username;

	sharedMemoryPushDefaultMemoryAccess(PAGE_READWRITE);
	ret = sharedMemoryAcquire(&shared_memory, "GIMME_USERNAME");
	if (ret==SMAR_DataAcquired) {
		username = sharedMemoryGetDataPtr(shared_memory);
	} else if (ret==SMAR_Error) {
		username = calloc(nameLen,1);
		username[0] = 0;
	} else if (ret==SMAR_FirstCaller) {
		sharedMemorySetSize(shared_memory, nameLen);
		username = sharedMemoryGetDataPtr(shared_memory);
		sharedMemoryUnlock(shared_memory);
		username[0] = 0;
	}
	sharedMemoryPopDefaultMemoryAccess();

	return username;
}

int promptUsernameExProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	char * username = sharedMemoryGetUserName();
	switch ( uMsg )
	{
	case WM_ACTIVATE:
		if ( username[0] )
		{
			char *pText = NULL;
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_INPUT_STRING), &pText);
			if ( stricmp(pText, username) )
			{
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_INPUT_STRING), username);
				wParam ^= LOWORD(wParam);
				wParam |= IDOK;
				SendMessage(hDlg, WM_COMMAND, wParam, lParam);
			}

			estrDestroy(&pText);
		}
		break;
	}
	return FALSE;
}

char *promptForUserName(void)
{
	return requestStringDialogEx("Enter a username for this session", "Enter Username", promptUsernameExProc);
}

int isValidUserName(char *name)
{
	char *c = name;
	if ( !name || name[0] == 0 )
		return 0;
	while( *c )
	{
		if ( !isalnum(*c) || *c=='_' )
			return 0;
		++c;
	}
	return 1;
}

const char *gimmeGetUserName(void) {
	static char name[1024];
	static bool got_name=false;
	static bool got_name_sync=false;
	if (!got_name || got_name_sync != gimme_state.doing_sync) {
		char *env;
		_dupenv_s(&env, NULL, "GIMME_USERNAME");
		if (env) {
			strcpy(name, env);
		} else {
			strcpy(name, getUserName());
		}
		if (gimme_state.doing_sync)
			strcat(name, "-SYNC");
		got_name=true;
		got_name_sync = gimme_state.doing_sync;
	}
	// "prompt" is a special user that will pop up a prompt asking for a user name 
	// if it doesnt exist in shared memory
	if ( stricmp(name, "prompt") == 0 )
	{
		char *sharedUserName = sharedMemoryGetUserName();
		if ( !sharedUserName[0] )
		{
			char *prompt = NULL;
			
			while ( !prompt )
			{
				prompt = promptForUserName();
				if ( prompt && isValidUserName(prompt) )
					strcpy(name, prompt);
				else
					prompt = NULL;
			}
		}
	}
	return name;
}

bool gimmeShouldUseHogs(GimmeDir *gimme_dir)
{
	static bool cached=false;
	static bool should_use_hogs;
	if (!cached) {
		char *s;
		_dupenv_s(&s, NULL, "USEHOGGS");
		should_use_hogs = s && strchr(s, '1');
		cached = true;
	}
	if (should_use_hogs) // Use them everywhere!
		return true;
	if (!gimme_dir->should_use_hogs_cached) {
		if (gimme_dir->local_dir[0] == gimme_dir->lock_dir[0] &&
			gimme_dir->lock_dir[1] == ':')
		{
			// No hoggs for remotely hosted folders (e.g. X:\biz mapped to X:\reivsions\biz
			gimme_dir->should_use_hogs = false;
			gimme_dir->should_use_hogs_cached = true;
		} else {
			char buf[CRYPTIC_MAX_PATH];
			sprintf_s(SAFESTR(buf), "%s/piggs/use_hoggs.txt", gimme_dir->local_dir);
			gimme_dir->should_use_hogs = !!fileExists(buf);
			gimme_dir->should_use_hogs_cached = true;
		}
	}
	return gimme_dir->should_use_hogs;
}

static char last_option[1024];
static int last_option_ret;

void gimmeSetOption(const char *optionName, int value)
{
	RegReader reader;
	last_option[0]=0;
	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
	rrWriteInt(reader, optionName, value);
	destroyRegReader(reader);
}

int gimmeGetOption(const char *optionName)
{
	RegReader reader;
	if (strcmp(last_option, optionName)!=0) {
		reader = createRegReader();
		initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
		if (!rrReadInt(reader, optionName, &last_option_ret)) {
			last_option_ret = 0;
		}
		destroyRegReader(reader);
		strcpy_s(SAFESTR(last_option), optionName);
	}
	return last_option_ret;
}

char *gimmeGetAuthorFromNode(GimmeNode *node)
{
	static char last_author[CRYPTIC_MAX_PATH];
	unsigned char *c;
	// find last author via the file name
	c = strrchr(node->name, '#');
	if (!c)
		return "internal error";
	do {
		++c;
	} while (isdigit((unsigned char)*c));
	strcpy(last_author, ++c);
	if (strEndsWith(last_author, ".txt")) {
		last_author[strlen(last_author)-4]=0;
	} else if (gimmeFileIsDeletedFile(last_author)) {
		last_author[strlen(last_author)-8]=0;
	} else {
		assert(0);
	}
	return last_author;
}

char *gimmeQueryLastAuthor(const char *fname) {
	char fullpath[CRYPTIC_MAX_PATH];
	int latestrev;
	char *username;
	char *relpath;
	GimmeDir *gimme_dir;
	GimmeNode *node;
	bool b;
	FWStatType filestat;
	char buf[CRYPTIC_MAX_PATH];
	char *ret;

	if (fname==NULL || fname[0] == 0)
		return gimme_gla_error;
	if (!fileIsAbsolutePath(fname)) {
		fname = fileLocateWrite(fname, buf);
	}
	makefullpath((char*)fname,fullpath);
	forwardSlashes(fullpath);

	if (gimme_state.last_author_cached_result[0] && (timerCpuSeconds() - gimme_state.last_author_cached_time) < 5 && stricmp(fname, gimme_state.last_author_cached_path)==0)
		return gimme_state.last_author_cached_result;

	gimme_dir = findGimmeDir(fullpath);
	if (eaSize(&eaGimmeDirs)==0 || !gimme_dir) {
		ret = gimme_gla_error_no_sc;
	} else {
//		if (stricmp(&fullpath[strlen(fullpath)-5],".lock")==0)
//		{
//			gimmeLog(LOG_FATAL, "%s is a lock file - you can't operate on that!",fullpath);
//			ret = gimme_gla_error;
//		} else
		{

			gimmeDirDatabaseLoad(gimme_dir, fullpath);
			gimmeDirDatabaseClose(gimme_dir);

			relpath = findRelPath(fullpath, gimme_dir);
			username = isLocked(gimme_dir, relpath);
			latestrev=makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
			if (latestrev==-1) {
				ret = gimme_gla_error;
			} else if (username && stricmp(username, gimmeGetUserName())==0) {
				ret = gimme_gla_checkedout;
			} else {
				// Check to see if we have the latest
				b = pststat(fullpath, &filestat)==-1;
				if (b && gimmeFileIsDeletedFile(node->name)) {
					// Doesn't exist on disk and has been deleted, return name for history's sake (GetTex)
					ret =  gimmeGetAuthorFromNode(node);
				} else {
					b=  (filestat.st_mtime >= node->timestamp-IGNORE_TIMEDIFF) &&
						(filestat.st_mtime <= node->timestamp+IGNORE_TIMEDIFF); 
					if ((filestat.st_mtime == node->timestamp+3600 || filestat.st_mtime+3600 == node->timestamp)
						&& filestat.st_size == node->size && !(filestat.st_mode & _S_IWRITE)) {
						// Off by exactly one hour, but the same size and it's read-only, assume DST bug
						b = true;
					}
					if (!b || gimmeFileIsDeletedFile(node->name)) {
						ret = gimme_gla_not_latest;
					} else {
						// find last author via the file name
						ret = gimmeGetAuthorFromNode(node);
					}
				}
			}
		}
	}
	gimme_state.last_author_cached_time = timerCpuSeconds();
	strcpy(gimme_state.last_author_cached_path, fname);
	strcpy(gimme_state.last_author_cached_result, ret?ret:"");
	return ret;
}

int getNextHighestBranch( GimmeNode *baseNode, int branch )
{
	int nhb = 0x7FFFFFFF;
	GimmeNode *node = baseNode;

	while( node )
	{
		if ( node->branch > branch &&
			node->branch < nhb )
			nhb = node->branch;
		node = node->next;
	}

	if ( nhb == 0x7FFFFFFF )
		return branch;
	return nhb;
}

GimmeNode * highestRevisionOnBranch(GimmeNode *base, int branch)
{
	int rev = 0;
	GimmeNode *node = NULL, *cur = base;

	while ( cur )
	{
		if ( cur->branch == branch && cur->revision >= rev )
		{
			rev = cur->revision;
			node = cur;
		}
		cur = cur->next;
	}

	return node;
}

int gimmeRelinkFileToBranch(const char *fname, int from_branch, int to_branch, int quiet)
{
	char *relpath, /*fullpath[CRYPTIC_MAX_PATH], fname_temp[CRYPTIC_MAX_PATH], path_on_N[CRYPTIC_MAX_PATH], *c,*/ localfname[CRYPTIC_MAX_PATH];
	GimmeDir *gimme_dir = NULL;
	int ret = NO_ERROR, i, next_highest_branch = -1;
	GimmeNode *node, *curNode;

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	if (fname==NULL) {
		if (!quiet) gimmeLog(LOG_FATAL, "Cannot locate folder or no folder specified.\n");
		return GIMME_ERROR_FILENOTFOUND;
	}

	strcpy( localfname, fname );
	relpath = findRelPath(localfname, gimme_dir);

	for(i=0;i<eaSize(&eaGimmeDirs) && !gimme_dir;i++)
	{
		if (strnicmp(eaGimmeDirs[i]->local_dir,fname,strlen(eaGimmeDirs[i]->local_dir))==0 && isslashornull(fname[strlen(eaGimmeDirs[i]->local_dir)]))
			gimme_dir = eaGimmeDirs[i];
	}

	if ( to_branch != -1 && from_branch > to_branch )
	{
		gimmeLog( LOG_WARN_LIGHT, "from_branch is a later branch than to_branch.  Swapping.\n" );
		SWAP32(from_branch, to_branch);
	}

	// if they are trying to relink a branch to something other than the next highest branch
	if ( !quiet && to_branch != -1 && to_branch - from_branch != 1 )
	{
		gimmeLog(LOG_WARN_HEAVY, "[relink %s %d -> %d] Are you sure you want to do this?  All versions between the two branches will be purged from the database [y/N]", localfname, from_branch, to_branch );
		if ( !consoleYesNo() )
		{
			gimmeUserLog("\tRelink Canceled");
			return ret;
		}
	}

	if (NO_ERROR!=(ret=gimmeDirDatabaseLoad(gimme_dir, localfname))) {
		gimmeLog(LOG_FATAL, "Error loading database\n");
		return GIMME_ERROR_DB;
	}

	gimmeDirDatabaseClose(gimme_dir);

	// find the root of the file in the database
	node = gimme_dir->database->root->contents;
	while ( node->is_dir ) node = node->contents;
	if ( strEndsWith(node->name, ".lock") )
	{
		if ( !quiet )
			MessageBox_UTF8(NULL,"Cannot relink a file that is checked out", "Gimme Error", MB_OK);
		gimmeLog(LOG_WARN_HEAVY, "Cannot relink a file that is checked out: %s",localfname);
		return GIMME_ERROR_LOCKFILE;
	}
	// find the latest version of the file to relink
	node = highestRevisionOnBranch(node, from_branch);

	curNode = node->parent->contents;
	if ( to_branch == -1 )
		next_highest_branch = getNextHighestBranch(curNode, node->branch);
	else 
		next_highest_branch = to_branch;

	if ( next_highest_branch == node->branch )
	{
		char message[256];
		sprintf_s(SAFESTR(message), "The file is already linked to branch %d.  Doing nothing.", next_highest_branch );
		if ( !quiet )
		{
			MessageBox_UTF8( NULL, message, "Relink File", MB_OK );
			return ret;
		}
	}

	{
		GimmeNode *curRev = highestRevisionOnBranch(curNode, next_highest_branch);
		char file1[CRYPTIC_MAX_PATH], file2[CRYPTIC_MAX_PATH];
		sprintf_s(SAFESTR(file1), "%s%s_versions/%s", gimme_dir->lock_dir, relpath, node->name );
		sprintf_s(SAFESTR(file2), "%s%s_versions/%s", gimme_dir->lock_dir, relpath, curRev->name );
		backSlashes(file1);
		backSlashes(file2);
		if ( fileCompare(file1, file2) != 0 )
		{
			char message[256];
			sprintf_s(SAFESTR(message), "The latest version on branch %d does not match the latest version on branch %d.  Relinking these branches could cause a loss of data.\n\nWould you still like to continue?",
							  node->branch, next_highest_branch );
			if ( !quiet && IDOK != MessageBox_UTF8( NULL, message, "WARNING!!!", MB_OKCANCEL ) )
			{
				return ret;
			}
		}
	}

	while ( curNode )
	{
		char file_to_purge[CRYPTIC_MAX_PATH];
		if ( curNode->branch == next_highest_branch ||
			(curNode->branch == node->branch && curNode->revision > node->revision ))
		{
			GimmeNode *tempNode = curNode->next;
			sprintf_s(SAFESTR(file_to_purge), "%s%s_versions/%s", gimme_dir->lock_dir, relpath, curNode->name);
			printf("Purging File: %s\n", file_to_purge);
			gimmePurgeFile( file_to_purge, 0 );
			gimmeNodeDeleteFromTree( &node->parent->contents, &node->parent->contents_tail, curNode );
			curNode = tempNode;
		}
		else
			curNode = curNode->next;
	}

	return ret;
}


// max user log size of 5 megs
#define MAX_GIMME_USER_LOG_SIZE 5242880
static const char *gimmeUserLogFileName = "C:\\gimmeUserLog.txt";
static const char *gimmePerfLogFileName = "C:\\gimmePerfLog.txt";

static void gimmeLocalLog( const char *logFileName, const char *str)
{
	SYSTEMTIME st;
	char datestr[256];

	ThreadAgnosticMutex hMutex = acquireThreadAgnosticMutex("Global\\gimmeUserLog.txt.lock");
	FILE *gimmeUserLogFile = NULL;

	GetLocalTime(&st);
	sprintf_s(SAFESTR(datestr), "%d/%d/%d - %d:%d:%d\t", st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond );
	if ( fileSize(logFileName) > MAX_GIMME_USER_LOG_SIZE )
	{
		char backup[MAX_PATH];
		strcpy(backup, logFileName);
		strcat(backup, ".bak");
		fileCopy(logFileName, backup);
		fileForceRemove(logFileName);
	}
	gimmeUserLogFile = fopen(logFileName, "at");
	if (gimmeUserLogFile) {
		fwrite(datestr, sizeof(char), strlen(datestr), gimmeUserLogFile);
		fwrite(str, sizeof(char), strlen(str), gimmeUserLogFile);
		fwrite("\n", sizeof(char), 1, gimmeUserLogFile);
		fclose(gimmeUserLogFile);
	}

	releaseThreadAgnosticMutex(hMutex);
}

void gimmeUserLog( char *str )
{
	gimmeLocalLog(gimmeUserLogFileName, str);
}

#undef gimmeUserLogf
void gimmeUserLogf( char *str, ... )
{
	va_list list;
	char buff[4096];

	va_start(list, str);
	vsprintf(buff, str, list);
	gimmeUserLog(buff);
	va_end(list);
}

void gimmePerfLog( char *str )
{
	gimmeLocalLog(gimmePerfLogFileName, str);
}

#undef gimmePerfLogf
void gimmePerfLogf( char *str, ... )
{
	va_list list;
	char buff[4096];

	va_start(list, str);
	vsprintf(buff, str, list);
	gimmePerfLog(buff);
	va_end(list);
}

char *gimmeBlockFileRegPath = "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme\\BlockedFiles";

GimmeErrorValue gimmeBlockFile(const char *fullpath, const char *block_string)
{
	char fullpath_local[CRYPTIC_MAX_PATH];
	RegReader reader;
	strcpy(fullpath_local, fullpath);

	reader = createRegReader();
	initRegReader(reader, gimmeBlockFileRegPath);
	rrWriteString(reader, forwardSlashes(fullpath_local), block_string);
	destroyRegReader(reader);
	return GIMME_NO_ERROR;
}

GimmeErrorValue gimmeUnblockFile(const char *fullpath)
{
	char fullpath_local[CRYPTIC_MAX_PATH];
	RegReader reader;
	strcpy(fullpath_local, fullpath);

	reader = createRegReader();
	initRegReader(reader, gimmeBlockFileRegPath);
	rrDelete(reader, forwardSlashes(fullpath_local));
	destroyRegReader(reader);
	return GIMME_NO_ERROR;
}

int gimmeFileIsBlocked(const char *fullpath)
{
	if ( gimmeGetBlockString(fullpath) )
		return 1;
	return 0;
}

const char *gimmeGetBlockString(const char *fullpath)
{
	char fullpath_local[CRYPTIC_MAX_PATH];
	RegReader reader;
	static char out[1024];
	const char *ret=NULL;
	strcpy(fullpath_local, fullpath);

	reader = createRegReader();
	initRegReader(reader, gimmeBlockFileRegPath);
	if ( rrReadString(reader, forwardSlashes(fullpath_local), SAFESTR(out)) )
		ret = out;
	destroyRegReader(reader);
	return ret;
}


typedef struct GimmeProcessSpec {
	char *filespec;
	char *process;
	char *cmdLine;
	int success_return; // the value of a successful return from the process (defaults to 0)
	int run_once; // true if the process should only be run once for a batch checkin
	int has_run; // true if the run_once is true and the process has been run
	int run_once_return; // if run_once and has_run are true, this is the return value of the previous call to the process
} GimmeProcessSpec;

typedef struct GimmeProcessSpecList {
	GimmeProcessSpec **specs;
} GimmeProcessSpecList;

GimmeProcessSpecList *procSpecList = NULL;
GimmeProcessSpec **processSpecs = NULL;

//static char *gimmeProcessSpecFile = "N:\\revisions\\gimmeProcessSpec.txt";

static ParseTable tpiProcessSpec[] =
{
	//{"BEGIN",			TOK_START, 0 },
	{"SPEC",			TOK_STRING(GimmeProcessSpec, filespec, 0) },
	{"PROCESS",			TOK_STRING(GimmeProcessSpec, process, 0) },
	{"CMDLINE",			TOK_STRING(GimmeProcessSpec, cmdLine, 0) },
	{"RUNONCE",			TOK_INT(GimmeProcessSpec, run_once, 0) },
	{"SUCCESSRETURN",	TOK_INT(GimmeProcessSpec, success_return, 0) },
	{"END",				TOK_END, 0 },
	{"",0,0}
};

static ParseTable tpiProcSpecList[] =
{
	{"BEGIN",		TOK_STRUCT(GimmeProcessSpecList, specs, tpiProcessSpec) },
	{"",0,0}
};

AUTO_RUN;
void initGimmeProcessTPIs(void)
{
	ParserSetTableInfo(tpiProcSpecList, sizeof(GimmeProcessSpecList), "tpiProcSpecList", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(tpiProcessSpec, sizeof(GimmeProcessSpec), "tpiProcessSpec", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

void gimmeLoadProcessSpecs( GimmeDir *gimme_dir )
{
	char specFile[CRYPTIC_MAX_PATH];

	sprintf_s(SAFESTR(specFile), "%s\\gimmeProcessSpec.txt", gimme_dir->lock_dir);
	procSpecList = calloc(sizeof(GimmeProcessSpecList), 1);
	gimmeCacheLock();
	gimmeGetCachedFilename(specFile, SAFESTR(specFile));
	ParserLoadFiles(0, specFile, 0, 0, tpiProcSpecList, procSpecList);
	gimmeCacheUnlock();
	processSpecs = procSpecList->specs;
	//ParserDestroyStruct(tpiProcSpecList, procSpecList);
}

int gimmeRunProcess(GimmeDir *gimme_dir, const char *relpath)
{
	int i;
	char cmdLine[512];
	char fullpath[CRYPTIC_MAX_PATH];

	if ( processSpecs == NULL )
		gimmeLoadProcessSpecs(gimme_dir);
	
	// find a process that this file is connected to
	for ( i = 0; i < eaSize(&processSpecs); ++i )
	{
		if ( simpleMatch(processSpecs[i]->filespec, relpath) )
		{
			int ret = 0;
			//char *c;

			makeLocalNameFromRel(gimme_dir, relpath, fullpath);
			strcpy(cmdLine, gimme_dir->local_dir);
			
			// remove bottom level directory
			//c = strrchr(cmdLine, '/');
			//if ( !c )
			//	c = strrchr(cmdLine, '//');
			//if ( c ) *c = 0;

			if ( !(processSpecs[i]->process[0] == '/' || processSpecs[i]->process[0] == '\\') )
				strcat(cmdLine, "/");
			strcat(cmdLine, processSpecs[i]->process);
			forwardSlashes(cmdLine);

			if ( processSpecs[i]->cmdLine && strstri(processSpecs[i]->cmdLine, "%FILE%") )
			{
				char params[512];//, newParams[512];
				strcpy(params, processSpecs[i]->cmdLine);
				strstriReplace(params, "%FILE%", fullpath);
				//strstriReplace(params, "%FILE%", "%s");
				//sprintf_s(SAFESTR(newParams), params, fullpath );
				strcat(cmdLine, " ");
				//strcat(cmdLine, newParams);
				strcat(cmdLine, params);
			}

			if ( processSpecs[i]->run_once )
			{
				if ( processSpecs[i]->has_run )
				{
					ret = processSpecs[i]->run_once_return == processSpecs[i]->success_return;
					if ( !ret )
						gimmeLog(LOG_FATAL, "Process %s failed for %s", cmdLine, fullpath);
					return ret;
				}

				//processSpecs[i]->run_once_return = system(cmdLine);
				processSpecs[i]->run_once_return = spawnProcess(cmdLine, _P_WAIT);
				ret = processSpecs[i]->run_once_return == processSpecs[i]->success_return;
				if ( !ret )
						gimmeLog(LOG_FATAL, "Process %s failed for %s", cmdLine, fullpath);
				return ret;
			}
			else
			{
				//ret = (system(cmdLine) == processSpecs[i]->success_return);
				ret = (spawnProcess(cmdLine, _P_WAIT) == processSpecs[i]->success_return);
				if ( !ret )
						gimmeLog(LOG_FATAL, "Process %s failed for %s", cmdLine, fullpath);
				return ret;
			}
		}
	}

	// this file was not connected to a process, return success
	return 1;
}

#define PRINT_NODE_STATS(file, node) {\
	fprintf((file), "%-100s %-15d\n", (node)->name, (node)->size); \
}

#define PRINT_STATS(file, nodeStat) {\
	fprintf((file), "%-25s | %-20"FORM_LL"d | %-15d | %-8.4f | %-8.4f | %-10d | %-8.4f | %-10d | %-8.4f\n", (nodeStat)->extension, \
	(nodeStat)->size, (nodeStat)->num, (nodeStat)->percentSize, (nodeStat)->percentNum, (nodeStat)->numHead, \
	((nodeStat)->numHead/(float)(nodeStat)->num) * 100.0f, (nodeStat)->numHeadAbsolute, \
	((nodeStat)->numHeadAbsolute/(float)(nodeStat)->num) * 100.0f); \
}

#define DBSTATS_STASH_SIZE 1<<16

typedef struct GimmeDatabaseNodeStat
{
	char extension[32];
	U64 size;
	U32 num;
	U64 sizeHead;
	U32 numHead;
	U32 numHeadAbsolute;
	F32 percentNum;
	F32 percentSize;
} GimmeDatabaseNodeStat;

StashTable dbstatsHash = NULL;
int stackDepth = 0;
FILE *stackFile = NULL;
int allocations = 0;
int sortColumn = 0;

int nodeStatCmp(const GimmeDatabaseNodeStat **a, const GimmeDatabaseNodeStat **b)
{
	// columns 
	//    0          1         2           3        4          5         6          7           8
	//"extension", "size", "num Files", "%size", "%files", "numHead", "%head", "numHeadAbs", "%headAbs"
	switch ( sortColumn )
	{
	case 0:
		return strcmp((*a)->extension, (*b)->extension); 
	case 1:
		return (*a)->size < (*b)->size ? -1 : ((*a)->size > (*b)->size ? 1 : 0);
	case 2:
		return (*a)->num < (*b)->num ? -1 : ((*a)->num > (*b)->num ? 1 : 0);
	case 3:
		return (*a)->percentSize < (*b)->percentSize ? -1 : ((*a)->percentSize > (*b)->percentSize ? 1 : 0);
	case 4:
		return (*a)->percentNum < (*b)->percentNum ? -1 : ((*a)->percentNum > (*b)->percentNum ? 1 : 0);
	case 5:
		return (*a)->numHead < (*b)->numHead ? -1 : ((*a)->numHead > (*b)->numHead ? 1 : 0);
	case 6:
		return (*a)->numHead/(float)(*a)->num < (*b)->numHead/(float)(*b)->num ? -1 : ((*a)->numHead/(float)(*a)->num > (*b)->numHead/(float)(*b)->num ? 1 : 0);
	case 7:
		return (*a)->numHeadAbsolute < (*b)->numHeadAbsolute ? -1 : ((*a)->numHeadAbsolute > (*b)->numHeadAbsolute ? 1 : 0);
	case 8:
		return (*a)->numHeadAbsolute/(float)(*a)->num < (*b)->numHeadAbsolute/(float)(*b)->num  ? -1 : ((*a)->numHeadAbsolute/(float)(*a)->num > (*b)->numHeadAbsolute/(float)(*b)->num ? 1 : 0);
	default:
		return strcmp((*a)->extension, (*b)->extension); 
	}
}

void pushStack(char *fn)
{
	int i;
	if ( !stackFile )
		stackFile = fopen("C:\\stack.txt", "wt");

	for ( i = 0; i < stackDepth; ++i )
		fprintf(stackFile, "\t");
	fprintf(stackFile, "%s\n", fn);
	++stackDepth;
}

void popStack()
{
	--stackDepth;
}

void addNodeToDBStatsHash(GimmeDir *gimme_dir, GimmeNode *node)
{
	GimmeDatabaseNodeStat *nodeStat = NULL;
	GimmeNode *outNode;
	int head_revision = 0, abs_head_revision = 0;
	char name[256], key[32], relpath[CRYPTIC_MAX_PATH], *c;

	// figure out the file extension
	// its in the format:
	//	filename.extension_versioninfo.txt
	strcpy(name, node->name);
	c = strstri(name, "_vb#");
	if ( c ) *c = 0;
	else
	{
		c = strstri(name, "_v#");
		if ( c ) *c = 0;
	}
	//c = strrchr(name, '.');
	//if ( c ) strcpy(key, c);
	//c = strchr(key, '_');
	//if ( c ) *c = 0;
	c = strrchr(name, '.');
	if ( c )
		strcpy(key, c);
	else
		strcpy(key, "*");


	if ( !dbstatsHash )
		dbstatsHash = stashTableCreateWithStringKeys(DBSTATS_STASH_SIZE, StashDeepCopyKeys_NeverRelease);

	if ( !strEndsWith(node->name, "lock") )
	{
		getRelPathFromVersionNode(node,SAFESTR(relpath));
		// is this the head revision on its branch?
		getHighestVersionFromNode(gimme_dir, node->parent, &outNode, node->branch, relpath);
		if ( outNode == node )
			head_revision = 1;
		// is this the absolute head revision?
		getHighestVersionFromNode(gimme_dir, node->parent, &outNode, getHighestBranch(node), relpath);
		if ( outNode == node )
			abs_head_revision = 1;
	}
	
	if ( !stashFindPointer(dbstatsHash, key, &nodeStat) )
	{
		nodeStat = (GimmeDatabaseNodeStat *)calloc(sizeof(GimmeDatabaseNodeStat), 1);
		++allocations;
		strcpy(nodeStat->extension, key);
		nodeStat->num = 1;
		nodeStat->size += node->size;
		if ( head_revision )
			++nodeStat->numHead;
		if ( abs_head_revision )
			++nodeStat->numHeadAbsolute;
		stashAddPointer(dbstatsHash, key, nodeStat, 1);
	}
	else
	{
		nodeStat->num++;
		nodeStat->size += node->size;
		if ( head_revision )
			++nodeStat->numHead;
		if ( abs_head_revision )
			++nodeStat->numHeadAbsolute;
	}

}

void gimmeOutputNodeStats(GimmeDir *gimme_dir, GimmeNode *node)
{
	addNodeToDBStatsHash(gimme_dir, node);
}

void gimmeOutputNodeStats_recurse(GimmeDir *gimme_dir, GimmeNode *node)
{
	while ( node )
	{
		if ( node->is_dir )
			gimmeOutputNodeStats_recurse(gimme_dir, node->contents);
		else
			addNodeToDBStatsHash(gimme_dir, node);
		
		node = node->next;
	}
}

void gimmeOutputDatabaseStats(char *outFile, int dbnum, int sortCol)
{
	FILE *file = fopen(outFile, "wt");
	char buf[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	F32 totalPercentSize = 0.0f;
	//GimmeDatabase *db = gimmeDatabaseNew();
	GimmeDir *gimme_dir;

	sortColumn = sortCol;

	gimmeLoadConfig();
	gimme_dir = eaGimmeDirs[dbnum];

	dbstatsHash = stashTableCreateWithStringKeys(DBSTATS_STASH_SIZE, StashDeepCopyKeys_NeverRelease);

	// i am sure there is a reason that the lock_dir doesnt have a slash after it already, but i dont know what it is.
	sprintf_s(SAFESTR(buf), "%s/", gimme_dir->lock_dir);
	gimme_dir->database = gimmeDatabaseNew();
	gimmeDatabaseLoad(gimme_dir->database, buf/*"N:/revisions/data/"*/, NULL, 0);
	node = gimme_dir->database->root->contents;
	// fill out the GimmeDatabaseNodeStat structs from the files in the database
	while ( node ) 
	{
		if ( node->is_dir )
			gimmeOutputNodeStats_recurse(gimme_dir, node->contents);
		else
			gimmeOutputNodeStats(gimme_dir, node);
		node = node->next;
	}

	// print output
	{
		StashTableIterator iter;
		StashElement elem;
		U64 totalSize = 0;
		U64 totalFiles = 0;
		U64 totalHead = 0;
		U64 totalHeadAbs = 0;
		int i;
		GimmeDatabaseNodeStat **eaNodeStats = NULL;
		

		fprintf(file, "%-25s   %-20s   %-15s   %-8s   %-8s  %-10s   %-8s   %-10s   %-9s\n", "extension", "size", "num Files", "%size", "%files", "numHead", "%head", "numHeadAbs", "%headAbs");
		fprintf(file, "------------------------------------------------------------------------------------------------------------------------------------\n");
		for ( stashGetIterator(dbstatsHash, &iter); stashGetNextElement(&iter, &elem);)
		{
			GimmeDatabaseNodeStat *nodeStat = stashElementGetPointer(elem);
			totalSize += nodeStat->size;
			totalFiles += nodeStat->num;
			totalHead += nodeStat->numHead;
			totalHeadAbs += nodeStat->numHeadAbsolute;
			eaPush(&eaNodeStats, nodeStat);
		}
		qsort(eaNodeStats, eaSize(&eaNodeStats), sizeof(GimmeDatabaseNodeStat*), nodeStatCmp);
		//for ( stashGetIterator(dbstatsHash, &iter); stashGetNextElement(&iter, &elem);)
		for ( i = 0; i < eaSize(&eaNodeStats); ++i )
		{
			GimmeDatabaseNodeStat *nodeStat = eaNodeStats[i];//stashElementGetPointer(elem);
			nodeStat->percentNum = ((F32)nodeStat->num/totalFiles) * 100.0f;
			nodeStat->percentSize = ((F32)nodeStat->size/totalSize) * 100.0f;
			totalPercentSize += nodeStat->percentSize;
			PRINT_STATS(file, nodeStat);
		}

		{
			GimmeDatabaseNodeStat dummyNodeStat;
			strcpy(dummyNodeStat.extension, "total");
			dummyNodeStat.num = totalFiles;
			dummyNodeStat.size = totalSize;
			dummyNodeStat.numHead = totalHead;
			dummyNodeStat.numHeadAbsolute = totalHeadAbs;
			dummyNodeStat.percentNum = 100.0f;
			dummyNodeStat.percentSize = 100.0f;
			fprintf(file, "------------------------------------------------------------------------------------------------------------------------------------\n");
			PRINT_STATS(file, &dummyNodeStat);
		}
	}

	//gimmeDatabaseDelete(gimme_dir->database);

	fclose(file);
}
