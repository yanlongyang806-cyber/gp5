#pragma once

#include "wininclude.h"
#include "gimmeDatabase.h"
#include "stdtypes.h"
#include "gimmeDLLPublicInterface.h"


typedef int (*voidFunc)(void);

#define IGNORE_TIMEDIFF 3

enum {
	GIMME_DB_DATA,
	GIMME_DB_SRC,
};

typedef enum CommentLevel {
	CM_DONTASK,
	CM_ASK,
	CM_REQUIRED,
} CommentLevel;

typedef enum GimmeLogLevel {
	LOG_FATAL,
	LOG_INFO, // one-lined
	LOG_WARN_LIGHT,
	LOG_WARN_HEAVY,
	LOG_STAGE, // just info, but need a carriage return
	LOG_TOFILEONLY, // only writes to file
	LOG_TOSCREENONLY, // only writes to screen
} GimmeLogLevel;

typedef enum ExclusionType {
	ET_OK,
	ET_EXCLUDED,
	ET_NOTINFILESPEC,
	ET_ERROR,
} ExclusionType;

#define REV_BLEEDINGEDGE -2
#define REV_BYTIME -3
#define REV_BLEEDINGEDGE_FORCE -4
#define REV_BLEEDINGEDGE_REVERT -5


typedef struct {
	char	filespec[128];
	int		num_revisions;
} RuleSet;

typedef struct FilespecMap FilespecMap;

typedef struct GimmeDir
{
	char	local_dir[CRYPTIC_MAX_PATH];
	char	lock_dir[CRYPTIC_MAX_PATH];
	GimmeDatabase *database;
	int		has_loaded_main;
	__time32_t  dateOfApproval;
	RuleSet** eaRules;
	int		active_branch;
	FilespecMap *hogMapping;
	bool should_use_hogs_cached;
	bool should_use_hogs;
} GimmeDir;

extern GimmeDir **eaGimmeDirs;
extern char *command_line;

enum
{
	CHECKIN_NOTE_NOCHANGE,
	CHECKIN_NOTE_UNDOCHECKOUT,
	CHECKIN_NOTE_NOTINDB,
	CHECKIN_NOTE_NEW,
	CHECKIN_NOTE_DELETED,
	CHECKIN_NOTE_GETLATEST,
	CHECKIN_NOTE_CHECKOUT,
	CHECKIN_NOTE_EMPTYFILE,
	CHECKIN_NOTE_MAX
};

static char * checkin_notes[CHECKIN_NOTE_MAX] = 
{
	" (No changes detected, just undoing check-out)",
	" (Undo check-out)",
	" (Not in DB)",
	" (NEW)",
	" (DELETED)",
	" (Get latest)",
	" (Checkout)",
	" (Empty File)"
};


typedef struct GimmeQueuedAction {
	GimmeDir *gimme_dir;
	char *relpath;
	GIMMEOperation operation;
	int quiet;
	const char *notes;
} GimmeQueuedAction;

typedef enum GimmeDBMode {
	GIMME_AUTO_DB,	// default
	GIMME_NO_DB,
	GIMME_FORCE_DB,
} GimmeDBMode;

typedef enum GimmeGetLatestOnDeleted {
	GLOD_PROMPT,
	GLOD_REVERTALL,
	GLOD_LEAVEALL,
} GimmeGetLatestOnDeleted;

// Custom logging function for setCustomGimmeLog().
typedef void (*CustomGimmeLogType)(const char *str);

#define GIMME_MAX_FILESPECS 10

// This is the state that is zeroed between major calls to gimme, things like
// database load status and config load should not be in here, otherwise
// it will re-load them unnecessarily.
typedef struct GimmeState {
	int no_underscore; // set to 0 in order to include underscored files/folders
	int simulate;
	int pause;
	int delayPause; // Instead of actually pausing store that we need to pause in the registry, and then gimme -doRegPause will pause if necessary (for easier .bat files)
	int ignore_errors;
	__time32_t dateToGet;
	int nowarn;
	int doing_sync;
	voidFunc queuedActionCallback;
	int leavecheckedout;
	int num_command_line_operations;
	int cur_command_line_operation;
	int cur_operation_section; // 0=scanning files, 1=copying files
	int do_extra_checks;
	int just_queue;
	int force_get_latest;
	int no_need_to_init_wsa;
	int launch_editor;
	int doing_branch_switch;
	int no_comments;
	int ignore_diff;
	int no_async;
	int repeat_last_comment;
	int fail_on_checkedout;
	int no_shell_extension;
	int shell_extension_even_on_x64;
	int register_switch_to;

	int *databases_to_update_hoggs;

	GimmeQueuedAction **queuedActions;

	char *command_line;
	char *editor;
	int quiet;
	GimmeDBMode db_mode;

	char *config_file1;
	int num_filespecs;
	char filespec[GIMME_MAX_FILESPECS][CRYPTIC_MAX_PATH];
	int filespec_mode[GIMME_MAX_FILESPECS]; // 0==only these, 1==not these

	int file_update_count;
	int file_get_count;
	int undo_checkout_count;
	int new_file_count;
	int remove_count;
	U64 bytes_transfered;
	int timer;
	int num_pruned_versions;
	int doStartFileWatcher;

	int updateRemoteFileCache;
	int noRemoteFileCache;

	int justDoingPurge;

	GimmeGetLatestOnDeleted what_to_do_when_getting_latest_on_deleted;

	char is_file_locked_cached_path[CRYPTIC_MAX_PATH];
	U32 is_file_locked_cached_time;
	char is_file_locked_cached_result[CRYPTIC_MAX_PATH];

	char last_author_cached_path[CRYPTIC_MAX_PATH];
	U32 last_author_cached_time;
	char last_author_cached_result[CRYPTIC_MAX_PATH];

} GimmeState;
extern GimmeState gimme_state;

const char *gimmeGetErrorString(int errorno);
int gimmeGetApprovedVersion(int gimme_dir_num, int quiet); // retrieves all files older to or equal to the last date of approval
int gimmeGetLatestVersion(int gimme_dir_num, int quiet); // retrieves *all* of the latest version files
GimmeErrorValue gimmeDoOperation(const char *fname, GIMMEOperation operation, GimmeQuietBits quiet); // checks out or puts back a file, returns 0 if successfull
GimmeErrorValue gimmeDoCommand(const char *cmdline);
int gimmeWasLastCheckinAnUndo(void); // Tells whether or not the last checkin request ended up reverting the timestamp
int gimmePurgeFile(const char *fname, int add);
#define gimmeMakeLocalNameFromRel(gimme_db_num, relpath, buf) gimmeMakeLocalNameFromRel_s(gimme_db_num, relpath, SAFESTR(buf))
int gimmeMakeLocalNameFromRel_s(int gimme_db_num, const char *relpath, char *buf, size_t buf_size); // Given a database number, makes a local path from a relative
char *getLocalHostNameAndIPs(void);
char *gimmeQueryIsFileLocked(const char *fname); // returns a pointer to the username of the locker or NULL if not locked
int  gimmeQueryIsFileLockedByMeOrNew(const char *fname);
int  gimmeGetUserNameFromLock(char *buf, size_t buf_size, const char *lockname);
char *gimmeQueryLastAuthor(const char *fname);
int gimmeRelinkFile(const char *fname, int branch, int quiet); // relink a file to the next highest branch
int gimmeRelinkFileToBranch(const char *fname, int from_branch, int to_branch, int quiet);// relink a file to a specific branch

int gimmeIsSambaDrive(const char *path); // Returns true if the drive is a Samba drive and doesn't need time adjusting
int gimmeGetTimeAdjust(void); // returns the amount that needs to be added to the result of a _stat call to sync up with PST
int pststat(const char *path, FWStatType *buffer);
int pstutime(const char *filename, struct _utimbuf *times);
int pstSetFileCreatedTime(const char *filename, __time32_t created);

const char *gimmeGetUserName(void); // Gets the username we are identified as by Gimme
bool gimmeShouldUseHogs(GimmeDir *gimme_dir);
CustomGimmeLogType setCustomGimmeLog(CustomGimmeLogType fptr); // Set custom function for Gimme logging

// Internal functions (only used by gimmeMain.c and gimmeUtil.c)
void gimmeLog_dbg(GimmeLogLevel loglevel, FORMAT_STR char const *fmt, ...);
#define gimmeLog(loglevel, fmt, ...) gimmeLog_dbg(loglevel, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void GimmeDllLog(const char *pLogLine);
FWStatType fileTouch(const char *filename, const char *reffile);
int showLockedFilesByDb(const char *myname, int dir_num_min, int dir_num_max, int quiet);
int checkinFoldByDb(GIMMEOperation operation, int dir_num_min, int dir_num_max, int quiet);
extern voidFunc queuedActionCallback;
int gimmeDirDatabaseCloseAll(void);
CommentLevel getCommentLevel(const char *path);
GimmeErrorValue gimmeDoOperationRelPath(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, GimmeQuietBits quiet);
int rmFold(char *folder, int quiet);
int checkoutFold(char *folder, int quiet);
int undoCheckoutFold(char *folder, int quiet);
int checkinFold(char *folder, int quiet);
int getLatestVersionFolder(const char *folder, int revision, int quiet);
int gimmeGetVersionByTime(int gimme_dir_num, __time32_t time, int quiet);
int gimmeGetFolderVersionByTime(int gimme_dir_num, char *folder, __time32_t time, int quiet);
int showLockedFilesFold(char *folder, int quiet, const char *username);
int forcePutFold(char *folder, int quiet);
int gimmeLoadConfig(void);
__time32_t getServerTime(GimmeDir *gimme_dir);
void setCommentLevel(CommentLevel level);
void setCommentLevelOverride(CommentLevel level);
void gimmeSetOption(const char *optionName, int value);
int gimmeGetOption(const char *optionName);
int doQueuedActions(void);
char *findRelPath(char *fname, GimmeDir *gimme_dir);
int copyFile(char *src,char *dst);
int copyFileToLocal(GimmeDir *gimme_dir, char *dbname, const char *localname_const, U32 timestamp, bool make_readonly);
GimmeDir *findGimmeDir(const char *fullpath);
int gimmeDirDatabaseLoad(GimmeDir *gimme_dir, const char *fullpath);
int gimmeDirDatabaseClose(GimmeDir *gimme_dir);
char *isLocked(GimmeDir *gimme_dir, const char *relpath);
#define makeLocalNameFromRel(gimme_dir, relpath, buf) makeLocalNameFromRel_s(gimme_dir, relpath, SAFESTR(buf))
void makeLocalNameFromRel_s(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size);
#define makeDBNameFromNode(gimme_dir, node, buf) makeDBNameFromNode_s(gimme_dir, node, SAFESTR(buf))
void makeDBNameFromNode_s(GimmeDir *gimme_dir, GimmeNode *node, char *buf, size_t buf_size);
int makeDBName(GimmeDir *gimme_dir, const char *relpath, int revision, GimmeNode **ret_node);
void makeLockName(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch);
void makeCommentFileName(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch);
void makeBatchInfoFileName(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch);
int getHighestVersion(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char *relpath);
int getLowestVersion(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int approvedver, int branch, const char *relpath);
int getApprovedRevision(GimmeDir *gimme_dir, const char *lockdir, GimmeNode **ret_node, int branch, const char *relpath);
char *getApprovedFile(GimmeDir *gimme_dir);
int getHighestBranch(GimmeNode *node);
int doByFold(const char *folder_in, int quiet,
			 int (*relPathFunc)(GimmeDir *, const char *, int),
			 int (*nodeFunc)(GimmeDir *, GimmeNode *, int),
			 char *statusmessage,
			 int (*auxfunc)(GimmeDir *, char*),
			 int allow_queueing);
int gimmeAddFilespec(char *filespec, int exclude);
ExclusionType gimmeCheckExclusion(GimmeDir *gimme_dir, const char *relpath);
bool gimmeIsBinFile(GimmeDir *gimme_dir, const char *relpath);
char *gimmeGetAuthorFromNode(GimmeNode *node);
int deleteOldVersionsRecur(GimmeDir *gimme_dir, const char *relpath, int branch_num);
int gimmeQueryAvailable(void);
int gimmeCheckDisconnected(void);
int makeLock(GimmeDir *gimme_dir, const char *relpath);
void gimmeOfflineTransactionLog(bool addDate, FORMAT_STR char const *fmt, ...);
#define gimmeOfflineTransactionLog(addDate, fmt, ...) gimmeOfflineTransactionLog(addDate, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gimmeUserLog( char *str );
void gimmeUserLogf( FORMAT_STR char *str, ... );
#define gimmeUserLogf(str, ...) gimmeUserLogf(FORMAT_STRING_CHECKED(str), __VA_ARGS__)
void gimmePerfLog( char *str );
void gimmePerfLogf( FORMAT_STR char *str, ... );
#define gimmePerfLogf(str, ...) gimmePerfLogf(FORMAT_STRING_CHECKED(str), __VA_ARGS__)
void gimmeGettingLatestOn(int gimme_dir_num);
void gimmeForceHoggUpdate(int gimme_dir_num);
bool gimmeIsForcedHoggUpdate(int gimme_dir_num);
GimmeErrorValue gimmeBlockFile(const char *fullpath, const char *block_string);
GimmeErrorValue gimmeUnblockFile(const char *fullpath);
int gimmeFileIsBlocked(const char *fullpath);
const char *gimmeGetBlockString(const char *fullpath); // returns a static buffer
void gimmeLoadProcessSpecs(GimmeDir *gimme_dir);
int gimmeRunProcess(GimmeDir *gimme_dir, const char *relpath);
void gimmeOutputDatabaseStats(char *outFile, int dbnum, int sortCol);
void gimmeQueryClearCaches(void);

#define gimmeFileIsDeletedFile(fname) ( (strEndsWith((fname),".deleted")) || (strEndsWith((fname),".del")) )