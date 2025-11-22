#ifndef CRYPTIC_PATCHPRUNING_H
#define CRYPTIC_PATCHPRUNING_H

typedef struct DirEntry DirEntry;
typedef struct FileVersion FileVersion;
typedef struct PatchJournal PatchJournal;
typedef struct PatchServerDb PatchServerDb;


// Force a particular file version to be pruned.
// Return true if pruning this FileVersion resulted in the removal of the parent DirEntry.
bool patchpruningPruneFileVersion(	PatchServerDb* serverdb,
									FileVersion* ver,
									PatchJournal* journal,
									const char* reason,
									S32 makeBackupCopy);

// Check a file against the pruning files, and prune it if it is time for it to be pruned.
void patchpruningPruneDirEntry(PatchServerDb *serverdb,
								DirEntry *dir_entry,
								PatchJournal *journal,
								const S32 createMissingMirrorFiles);

// Initiate asynchronous pruning.
void patchpruningPruneAsyncStart(PatchServerDb *serverdb);

// Process pending pruning.
bool patchpruningPruneAsyncTick(void);

// Return true if pruning is running.
bool patchpruningAsyncIsRunning(void);

// Abort pruning.
void patchpruningAsyncAbort(void);

// Return a string describing the current pruning status.
const char *patchpruningStatus(void);

#endif  // CRYPTIC_PATCHPRUNING_H
