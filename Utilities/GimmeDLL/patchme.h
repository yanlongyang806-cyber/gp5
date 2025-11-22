#pragma once

#include "gimmeDLLPublicInterface.h"

typedef struct PCL_Client PCL_Client;

bool patchmeDoOperation(const char *fullpath, GIMMEOperation op, GimmeQuietBits quiet, bool is_single_file, GimmeErrorValue *ret);
bool patchmeDoOperationEx(const char **file_paths, int file_count, GIMMEOperation op, GimmeQuietBits quiet, bool is_single_file, GimmeErrorValue *ret);
bool patchmeDoUnqueuedOperation(const char *fullpath, GIMMEOperation op, GimmeQuietBits quiet, GimmeErrorValue *ret, bool setDirty);
void patchmeSetDefaultCheckinComment(const char *comment);
bool patchmeQueryIsFileLocked(const char *fullpath, const char **ret);
bool patchmeQueryLastAuthor(const char *fullpath, const char **ret);
int patchmeQueryAvailable(void);
bool patchmeQueryBranchName(const char *localpath, const char **ret);
bool patchmeQueryBranchNumber(const char *localpath, int *ret);
bool patchmeQueryCoreBranchNumForDir(const char *localpath, int *ret);
bool patchmeDoCommandWrapperInternal(int argc, char **argv, GimmeErrorValue *ret);

bool patchmeGetPreviousBranchInfo(PCL_Client *client, int *branchOut, char **name); // hack to get some branch info
void patchmeGetSpecificVersion(const char *fullpath, int branch, int revision);
void patchmeGetSpecificVersionTo(const char *fullpath, const char *destpath, int branch, int revision);

void patchmeCheckCommandLine(void);
const char *patchmeGetTempFileName(const char *orig_name, int uid);
void patchmeDiffFile(const char *fullpath);

bool patchmeForceDirtyBit(const char *fullpath);

// Tell patchme to use more memory, but go faster.
void patchmeUseMoreMemory(void);

// Verifies server and client time are similar
bool patchmeVerifyServerTimeDifference(PCL_Client *client, U32 *client_time, U32 *server_time);

// These are sensitive strings, do not change them!
#define GIMME_GLA_ERROR_NO_SC	"Source Control not available"
#define GIMME_GLA_ERROR			"Not in database"
#define GIMME_GLA_CHECKEDOUT	"You have this file checked out"
#define GIMME_GLA_NOT_LATEST	"You do not have the latest version"
