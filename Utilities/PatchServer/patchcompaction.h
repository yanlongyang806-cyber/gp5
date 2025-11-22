#ifndef CRYPTIC_PATCHCOMPACTION_H
#define CRYPTIC_PATCHCOMPACTION_H

typedef struct PatchServerDb PatchServerDb;

// Initiate asynchronous compaction.
void patchcompactionCompactHogsAsyncStart(void);

// Cleanup temp file from compaction.
void patchcompactionCleanUpAsyncTick(void);

// Process pending hogg compaction.
void patchcompactionCompactHogsAsyncTick(void);

// Return true if compaction is currently running.
bool patchcompactionCompactHogsAsyncIsRunning(void);

// Abort compaction.
void patchcompactionAsyncAbort(void);

// Lock the compaction mutex.
void patchcompactionLock(void);

// Release the compaction mutex.
void patchcompactionUnlock(void);

bool patchcompactionIsFileWaitingCleanup(PatchServerDb *serverdb, const char *fileName);
bool patchcompactionIsFileCompacting(PatchServerDb *serverdb, const char *fileName);

void patchcompactionCleanUpTempHogsOnRestart(PatchServerDb *serverdb);

// Return a string describing the current compaction status.
const char *patchcompactionStatus(void);

#endif  // CRYPTIC_PATCHCOMPACTION_H
