#pragma once
#include <sys/types.h>
#include "gimme.h"
#include "gimmeUtilStat.h"

// in gimmeUtil.c
// These are functions called *only* by the stand-alone version of gimme, and this file (gimmeUtil.c/.h)
// need not be included in other projects (such as the mapserver), since these functions are not
// referenced outside of the standalone
int gimmeUtilApprove(int gimme_dir_num, __time32_t *time);
int gimmeUtilDiffFile(const char *fname);
int gimmeUtilCheck(int gimme_dir_num);
int gimmeUtilLabel(int gimme_dir_num, char *label);
GimmeErrorValue gimmeUtilLockFix(bool auto_unlock);
int gimmeUtilSwitchBranch(const char *localpath, int newbranch);
void gimmeUtilBranchStat(void);
void gimmeRegister(void);
void gimmeUtilCheckAutoRegister(void);
char *gimmeDetectDiffProgram(void);
int gimmeUtilBranchReport(int gimme_dir_num, int branchToReport);
void gimmeReconnect(void);
void gimmeUtilSyncNoCheckout(void);
void gimmeUtilSyncOnlyNewer(void);
int gimmeUtilSync(int dbnum);
void gimmeUtilCheckForHoggUpdate(int gimme_dir_num);
void gimmeUtilCheckForRunEvery(int gimme_dir_num);

// Returns 1 if they entered comments and clicked OK, 0 if they canceled
int gimmeDialogCheckin(GimmeQueuedAction **filenames);
char *gimmeDialogCheckinGetComments(void); // Gets the comments of the most recent checkin, if any
char *gimmeDialogCheckinGetBatchInfo(void);

void gimmeLogComments(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, const char *comments);
void gimmeLogBatchInfo(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, const char *comments);

typedef struct
{
	int branch;
	int version;
	char *user_name;
	char *timestamp;
	char *operation;
	char *comments;
} CommentInfo;

void destroyCommentInfo(CommentInfo *comment);
void gimmeGetComment(GimmeDir *gimme_dir, const char *relpath, int branch, int version, CommentInfo **comment);
void gimmeGetComments(GimmeDir *gimme_dir, const char *relpath, int branch, CommentInfo ***comments);
void gimmeGetBatchInfo(GimmeDir *gimme_dir, const char *relpath, int branch, CommentInfo ***comments);

void checkFileWatcherConsistency(const char *folder);

void gimmeBackupDatabases(void);

void gimmeGetCachedFilename(const char *path, char *dest, size_t dest_size);
char *gimmeFileAllocCached(const char *path, int *lenp);

void gimmeCacheLock(void);
void gimmeCacheUnlock(void);

const char * const * gimmeQueryGroupListForUser(const char *username);
const char * const * gimmeQueryGroupList(void);
const char * const * gimmeQueryFullGroupList(void);
const char * const * gimmeQueryFullUserList(void);


//////////////////////////////////////////////////////////////////////////
// Gimme UI Utility
//////////////////////////////////////////////////////////////////////////

typedef enum RESZType {
	RESZ_RIGHT		= 1<<0,	//Right aligned 
	RESZ_BOTTOM		= 1<<1,	//Bottom aligned
	RESZ_WIDTH		= 1<<2,	//Width stretches right
	RESZ_HEIGHT		= 1<<3, //Hight stretches down
} RESZType;

void DlgItemGetRelativeRect(HWND hDlg, HWND hDlgItem, LPRECT rc);
void DlgItemDoResize(HWND hDlg, HWND hDlgItem, int widthDiff, int heightDiff, LPRECT windowRect, U32 flags);
void DlgWindowCheckResize(HWND hDlg, U32 moving, LPRECT newRect, LPRECT origRect);