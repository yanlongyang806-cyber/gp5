
#ifndef _GIMME_DATABASE_H
#define _GIMME_DATABASE_H

#include <sys/types.h>
#include <stdlib.h> // for exit()
#include "filewatch.h"

typedef struct GimmeNode {
	struct GimmeNode *next;
	struct GimmeNode *prev;
	int is_dir;
	struct GimmeNode *contents; // NULL if !is_dir;  linked list of all of the children nodes
	struct GimmeNode *contents_tail; // NULL if !is_dir, tail of the linked list
	struct GimmeNode *parent;
	char *name; // name, relative to parent node
	size_t size;
	__time32_t timestamp;
	__time32_t checkintime;
	char *lock;
	int revision; // The revision number
	int branch; // The branch number
} GimmeNode;

typedef struct GimmeDatabase {
	int ref_count; // reference count
	GimmeNode *root; // Root node
	char *filename; // full path to database.txt
	int loc[5]; // The ordering of different keys
	//Pre-Journaling: int needs_flushing; // There is unsaved data in memory
	char *lockfile_name; // name of the lockfile to be deleted when we unlock
	int lockfile_handle; // handle to the lockfile (it is kept open while locked)
	int locked; // whether or not the database is currently locked by us
	int writing; // flag set while writing to database.txt, shouldn't exit while this is going on
	char *journal_txt; // full path to journal.txt
	char *jlockfile_name; // name of the lockfile for journal.txt
	int jlockfile_handle; // handle to the lockfile for journal.txt
	int jlocked; // whether or not journal.txt is locked by us
	int jwriting;  // flag set while writing to journal.txt
	char *jprocessing_name; // The name of the journal.processing file this database processed (and should delete upon a successful write operation)
	int loaded_from_fs; // set to true if this database was loaded by scanning the filesystem instead of by loading the database.txt file
	int zipped; // whether or not the file was loaded zipped and should be saved zipped
} GimmeDatabase;

GimmeDatabase *gimmeDatabaseNew(void);
int gimmeDatabaseLoad(GimmeDatabase *db, const char *dbroot, const char *relpath, int single_file); // dbroot is the root (on N:) of where the database is, relpath is the root of where all transactions will be under, single_file is true if we are only interested in a single file
void gimmeDatabaseDelete(GimmeDatabase *db);
//int gimmeDatabaseSave(GimmeDatabase *db); // only called internally immediately after a load
//void gimmeDatabaseUnlock(GimmeDatabase *db); // only called internally immediately after a save
void gimmeDatabaseRecreate(const char *dbroot);

int gimmeDatabaseLoadBranchFromFS(GimmeDatabase *db, const char *dbroot, const char *relpath);

int gimmeJournalAdd(GimmeDatabase *db, const char *relpath, size_t size, __time32_t file_timestamp, const char *lockee);
int gimmeJournalRm(GimmeDatabase *db, const char *relpath);

GimmeNode *gimmeNodeNew(void);
void gimmeNodeDelete(GimmeNode *node);
void gimmeNodeAdd(GimmeNode **head, GimmeNode **tail, GimmeNode *parent, char *relpath, const char *lockvalue, __time32_t timestamp, size_t size, __time32_t checkintime);
char *gimmeNodeGetFullPath(GimmeNode *node);
GimmeNode *gimmeNodeFind(GimmeNode *base, const char *relpath);
void gimmeNodeDeleteFromTree(GimmeNode **head, GimmeNode **tail, GimmeNode *node);

// Return 0 if parsing stopped in the middle, 1 if it completed all the way through
typedef int (*gimmeNodeOp)(GimmeNode *param);
int gimmeNodeRecurse(GimmeNode *node, gimmeNodeOp op);

const char *gimmeDatabaseGetVersionString(void);

int gimmeAcquireLock(const char *lockfilename);
int gimmeWaitToAcquireLock(char *lockfilename);
int gimmeUnaquireAndDeleteLock(int lockfile_handle, char *lockfile_name);
//void gimmeWriteLockData(int lockfile_handle);
void gimmeEnsureDeleteLock(const char *fn); // ensures the deletion of a file, even if someone else is currently reading from it, etc

extern int gimmeDatabaseInCritical;
extern int gimmeDatabaseRequestClose;
extern int g_force_flush;
extern int g_force_zip;
extern int g_ignore_locks;

#define GIMME_CRITICAL_START gimmeDatabaseInCritical++; fileWatchSetDisabled(1);
#define GIMME_CRITICAL_END				\
	if (gimmeDatabaseInCritical==1) {	\
		if (gimmeDatabaseRequestClose)	\
			exit(-1);					\
		fileWatchSetDisabled(0);		\
	}									\
	gimmeDatabaseInCritical--;			


// 300/100 = 30 seconds max wait before displaying a message
#define MAX_WAITS 300
#define WAIT_TIME 100

#endif