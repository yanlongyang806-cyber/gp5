// Handle asynchronous loading of files to be patched from hoggs

#ifndef CRYPTIC_PATCHSERVER_PATCHFILELOADING_H
#define CRYPTIC_PATCHSERVER_PATCHFILELOADING_H

#include "referencesystem.h"

typedef struct BlockReq BlockReq;
typedef struct PatchClientLink PatchClientLink;
typedef struct PatchFile PatchFile;
typedef struct NetLink NetLink;

typedef void (*WaitingRequestCallback)(void *userdata, PatchFile *patch, PatchClientLink *client, NetLink *link, int req, int id, int extra, int num_block_reqs, BlockReq *block_reqs);

typedef struct WaitingRequest
{
	// Callback information
	WaitingRequestCallback callback;				// Completion function
	void *userdata;									// userdata for completion

	REF_TO(PatchClientLink) refto_client;
	int req;

	// TODO: move this into userdata
	union
	{
		struct
		{
			int id;
			union {
				int print_idx;
				int block_size;
			};
			int num_block_reqs;
			BlockReq *block_reqs;
		};
		REF_TO(NetLink) refto_link;
	};

	// Used for requests delayed due to throttling
	U32 bytes;
	char *fname;
} WaitingRequest;

void patchserverHandleWaitingRequest(PatchFile *patch, WaitingRequest *request);

void patchserverGlobalBucketAdd(S32 addend);

U32 patchserverGlobalBucketSizeLeft(void);

// Number of throttled requests
int patchserverThrottledRequestCount(void);

// For reporting only
U32 patchserverThrottleLastGlobalBucket(void);

void patchserverQueueThrottledRequest(WaitingRequest *request);

// Initialize throttling.
void patchserverThrottleInit(void);

// Processing for the throttle buckets
void patchserverThrottle(void);

// Initialize a PatchFile for transfer, in a background thread.
void patchserverRequestPatchFileInit(PatchFile *patch);

// Perform background processing for patchserverInitPatchFile();
void patchserverInitPatchFileTick(void);

// Number of files currently subject to load processing.
int patchserverLoadProcessRequestsPending(void);

#endif  // CRYPTIC_PATCHSERVER_PATCHFILELOADING_H
