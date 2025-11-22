#pragma once

#include "pcl_typedefs.h"

typedef struct PatchXfer PatchXfer;
typedef struct ShardInfo_Basic ShardInfo_Basic;

// Number of timer samples to keep
#define PATCH_SPEED_SAMPLES 64

// port number for patch server network connection
#define PATCH_SERVER_PORT	7255

// standard values for pclSetRetryTimes() that the launcher uses
#define PATCH_RETRY_TIMEOUT 5
#define PATCH_RETRY_BACKOFF 5

typedef struct PatchSpeedData
{
	U32 deltas[PATCH_SPEED_SAMPLES];
	F32 times[PATCH_SPEED_SAMPLES];
	S64 cur, last;
	U8 head;
}
PatchSpeedData;

typedef struct PatchXferData
{
	char path[MAX_PATH];
	char *state; // memory is in pcl
	char blocks[64];
	char progress[32];
	char requested[64];
}
PatchXferData;

extern void PatcherSpawn(const char *productName, char **patchExtraFolders);

extern void PatcherCancel(void); // NOTE: this function is run in the UI Thread!

// Should we do a full verification of all files?
// 0 = no verify
// 1 = ask verify
// 2 = force verify
extern bool SetVerifyLevel(const char *productName, int verifyLevel); // called from ui
extern int GetVerifyLevel(const char *productName); // called from ui

// with micropatching disabled, all files come down in the patch
// with micropatching enabled, only required files come down in the patch
extern bool SetDisableMicropatching(const char *productName, bool bDisableMicropatching); // called from ui
extern bool GetDisableMicropatching(const char *productName); // called from ui

// launcher keeps track of the last shard patched, so that when you run it again, you will by default patch to the same shard
extern bool SetLastShardPatchedDescriptor(const char *productName, const char *shardName);
extern bool GetLastShardPatchedDescriptor(const char *productName, char *lastShardDescriptor, int lastShardDescriptorMaxLength); // called from ui

extern void SetShardToAutoPatch(const ShardInfo_Basic *shard); // called from UI.c
extern const ShardInfo_Basic *GetShardToAutoPatch(void); // called from LauncherMain.c, UI.c

// the token is passed to pclConnectAndCreate()
extern void PatcherGetToken(bool bInitialDownload, char *token, int tokenMaxLength);

extern bool PatcherIsDevelopmentPath(const char *path);

extern bool IsDefaultPatchServer(const char *patchServer);

extern void PatcherHandleError(int error, bool bConnecting);

extern void PatcherGetStats(
	char *rootFolder, int rootFolderMaxLength,
	char *netStats, int netStatsMaxLength,
	char *receivedStats, int receivedStatsMaxLength,
	char *actualStats, int actualStatsMaxLength,
	char *linkStats, int linkStatsMaxLength,
	char *ipAddress, int ipAddressMaxLength);

extern bool PatcherGetXferData(PatchXferData *xferData, int itemIndex);
extern int PatcherGetXferDataCount(void);

// BEGIN MUTEX-USING FUNCTIONS
extern bool PatcherIsValid(void);
extern void PatcherGetSuccessfulPatchRootFolder(char *rootFolder, int rootFolderMaxLength);
// END MUTEX-USING FUNCTIONS

extern int gDoNotAutopatch;
