#include "earray.h"
#include "EString.h"
#include "MultiWorkerThread.h"
#include "patcher_comm.h"
#include "patchfile.h"
#include "patchfileloading.h"
#include "patchserver.h"
#include "pcl_client.h"
#include "sysutil.h"
#include "timing.h"

// Number of threads to use for load processing
static U32 s_num_process_threads = 4;
AUTO_CMD_INT(s_num_process_threads, num_process_threads);

// Global bandwidth bucket.
static U32 s_global_bucket = 0;
static U32 s_last_global_bucket = 0;

// Bandwidth throttle timer
static U32 s_bucket_timer;

// Requests that were delayed because the client had no bandwidth left
static WaitingRequest **s_throttled_requests = NULL;

// Load processing thread pool
MultiWorkerThreadManager *s_process_patch_file_thread = NULL;

// Number of queued processing requests.
static int s_requests_pending = 0;

void patchserverHandleWaitingRequest(PatchFile *patch, WaitingRequest *request)
{
	PatchClientLink * client = GET_REF(request->refto_client);
	DirEntry *dir;

	// Refresh ver pointer
	if (!patch->special && !patch->nonhogfile)
	{
		patch->ver = NULL;
		dir = patchFindPath(patch->serverdb->db, patch->fileName.realPath, false);
		if(dir)
		{
			FOR_EACH_IN_EARRAY(dir->versions, FileVersion, ver)
				if(ver->checkin->time == patch->checkin_time)
				{
					patch->ver = ver;
					break;
				}
			FOR_EACH_END
		}
		if(!patch->ver)
		{
			Errorf("Cannot find file version for %s(%d)", patch->fileName.realPath, patch->checkin_time);
			REMOVE_HANDLE(request->refto_client);
			SAFE_FREE(request->fname);
			SAFE_FREE(request);
			return;
		}
	}

	// Handle request for patch client.
	if(client)
		request->callback(request->userdata, patch, client, NULL, request->req, request->id, request->print_idx, request->num_block_reqs, request->block_reqs);

	// Handle request associated with NetLink (for HTTP)
	else if(!request->req)
	{
		NetLink *link = GET_REF(request->refto_link);
		if(link)
			request->callback(request->userdata, patch, NULL, link, 0, 0, 0, 0, NULL);
		REMOVE_HANDLE(request->refto_link);
	}

	else
	{
		// Oh well, client doesn't exist, cleanup only
		SAFE_FREE(request->block_reqs);
	}
	REMOVE_HANDLE(request->refto_client);
	SAFE_FREE(request->fname);
	SAFE_FREE(request);
}

#define BUCKET_MAX (1024 * 1024 * 100)
void refillBucket(PatchClientLink *client, U32 bytes)
{
	if(client->bucket == U32_MAX)
		return;
	client->bucket += bytes;
	if(client->bucket > BUCKET_MAX)
		client->bucket = BUCKET_MAX;
}

int refillBuckets(NetLink *link, S32 index, PatchClientLink *client, PatchClientLink ***userdata)
{
	assertmsg(g_patchserver_config.bandwidth_config, "Trying to fill buckets, but no config");
	if(userdata && client->bucket < 4096)
		// Client is exhausted, add them to the list to receive more bandwidth
		eaPush(userdata, client);
	else if(client->bucket > 262144 && s_last_global_bucket < 1024)
		// Server has no bandwidth and this client has a lot available, don't give them more.
		return true;
	refillBucket(client, MIN(g_patchserver_config.bandwidth_config->per_user,
		g_patchserver_config.bandwidth_config->total / patchserverConnections()));
	return true;
}

void patchserverGlobalBucketAdd(S32 addend)
{
	s_global_bucket += addend;
	if (s_global_bucket < 0)
		s_global_bucket = 0;
}

U32 patchserverGlobalBucketSizeLeft()
{
	return s_global_bucket;
}

// Number of throttled requests
int patchserverThrottledRequestCount()
{
	return eaSize(&s_throttled_requests);
}


U32 patchserverThrottleLastGlobalBucket()
{
	return s_last_global_bucket;
}

void patchserverQueueThrottledRequest(WaitingRequest *request)
{
	eaPush(&s_throttled_requests, request);
}

// Initialize throttling.
void patchserverThrottleInit()
{
	s_bucket_timer = timerAlloc();
}

// Processing for the throttle buckets
void patchserverThrottle()
{
	PERFINFO_AUTO_START_FUNC();
	if(timerElapsed(s_bucket_timer)*1000 >= g_patchserver_config.bandwidth_config->time_slice)
	{
		int i, j;
		// NOTE: Make this static to save on allocation costs, should peak up to a decent size and then can stay there. <NPK 2009-05-19>
		static PatchClientLink **exhausted_clients=NULL;
		timerStart(s_bucket_timer);
		eaClear(&exhausted_clients);

		iterateAllPatchLinks(refillBuckets, &exhausted_clients);
		if(s_global_bucket > 1024 && eaSize(&exhausted_clients))
		{
			// NOTE: We give out a little more bandwidth than we have under the assumption that most users won't use everything we give them.
			//       Cap the burst to 11x (1x normal + 10x extra) normal bandwidth. <NPK 2009-05-19>
			U32 extra = ((double)s_global_bucket / eaSize(&exhausted_clients)) * 1.1;
			extra = MIN(extra, g_patchserver_config.bandwidth_config->per_user * 10);
			for(i = 0; i < eaSize(&exhausted_clients); i++)
				refillBucket(exhausted_clients[i], extra);
		}
					
		for(i=0; i<eaSize(&s_throttled_requests); i++)
		{
			WaitingRequest *request = s_throttled_requests[i];
			PatchClientLink *client = GET_REF(request->refto_client);
			if(!client)
			{
				// Client has disconnected already
				REMOVE_HANDLE(request->refto_client);
				SAFE_FREE(request->fname);
				SAFE_FREE(request);
				s_throttled_requests[i] = NULL;
				continue;
			}
			if(client->bucket >= request->bytes && s_global_bucket >= request->bytes)
			{
				PatchFile *patch = findPatchFile(request->fname, client);
				assertmsgf(patch, "Can't load patchfile %s for throttled request", request->fname);
				patchserverHandleWaitingRequest(patch, request);
				s_throttled_requests[i] = NULL;
			}
		}
		for(i=0,j=0; i<eaSize(&s_throttled_requests); i++)
		{
			WaitingRequest *request = s_throttled_requests[i];
			if(request)
			{
				s_throttled_requests[j] = request;
				j++;
			}
		}
		eaSetSize(&s_throttled_requests, j);

		s_last_global_bucket = s_global_bucket;
		s_global_bucket = g_patchserver_config.bandwidth_config->total;
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// Perform background processing on a PatchFile
static void thread_ProcessPatchFile(void *pUserData)
{
	PatchFile *patch = pUserData;
	PatchFileLoadInfo *loadinfo = patch->loadinfo;
	bool compressed_only = loadinfo->compressed && !loadinfo->uncompressed;
	int retries = 0;
	static int global_retries = 0;

	// Create compressed data, if missing.
	if (!loadinfo->compressed)
	{
		char *compressed = loadinfo->compressed;
		pclZipData(loadinfo->uncompressed, loadinfo->uncompressed_len, &loadinfo->compressed_len, &compressed);
		loadinfo->compressed = compressed;
	}

	// Initialize uncompressed data.
	if(!compressed_only)
	{
		patchfiledataInitForXfer(&patch->uncompressed, loadinfo->uncompressed, loadinfo->uncompressed_len, patch->estring);

		// Make sure actual CRC matches.
		if(!patch->nonhogfile && loadinfo->crc != patch->uncompressed.crc)
		{
			ErrorOrAlertDeferred(true, "PATCHDB_LOAD_CHECKSUM_FAIL", "checksum failure while loading %s %s/%s, ignoring\n",
				patch->serverdb->name,
				patch->serverdb->name,
				patchFileGetUsedName(patch));
		}
	}

	// Initialize compressed data.
	patchfiledataInitForXfer(&patch->compressed, loadinfo->compressed, loadinfo->compressed_len, false);

	// Notify the main thread that we're done.
	while (!mwtQueueOutput(s_process_patch_file_thread, patch))
	{
		delay(1);
		retries++;
		_InterlockedIncrement(&global_retries);
	}
}

// Finish initializing a PatchFile, in the main thread.
static void patchserverFinishProcessPatchFile(void *pUserData)
{
	PatchFile *patch = pUserData;
	int i;

	PERFINFO_AUTO_START_FUNC();

	--s_requests_pending;

	// Set load state.
	patch->load_state = patch->loadinfo->compressed && !patch->loadinfo->uncompressed ? LOADSTATE_COMPRESSED_ONLY : LOADSTATE_ALL;
	SAFE_FREE(patch->loadinfo);

	// Add patchfile to cache.
	if (!patch->nonhogfile)
		patchfileAddToCache(patch);

	// Handle requests that were waiting on this file to be loaded.
	for(i = 0; i < eaSize(&patch->requests); i++)
	{
		patchserverHandleWaitingRequest(patch, patch->requests[i]);
		if(patch->load_state == LOADSTATE_LOADING)
		{
			// this request needed a higher load_state, hold off on the rest until the file's done loading
			int new_size = eaSize(&patch->requests) - ++i;
			CopyStructsFromOffset(patch->requests, i, new_size);
			eaSetSize(&patch->requests, new_size);
			break;
		}
	}

	// If there are special files that need to be made, make them now.
	if (patch->load_state >= LOADSTATE_ALL)
	{
		EARRAY_CONST_FOREACH_BEGIN(patch->special_files, j, n);
		{
			bool changed;
			char *estr = NULL;
			PatchFile *special = patch->special_files[j];

			// Determine if we have to actually change the data.
			// If the PatchFile data doesn't change, we can just duplicate it.
			changed = !!special->prepend;

			if (changed)
			{

				// Create the patch file.
				if (special->prepend)
					estrCopy2(&estr, special->prepend);
				if (patch->uncompressed.data)
					estrConcat(&estr, patch->uncompressed.data, patch->uncompressed.len);

				special->loadinfo = malloc(sizeof(*special->loadinfo));
				special->loadinfo->uncompressed = estr;
				special->loadinfo->uncompressed_len = estrLength(&estr);
				special->loadinfo->compressed = NULL;
				special->loadinfo->compressed_len = 0;
				special->loadinfo->crc = 0;
				special->nonhogfile = true;

				estr = NULL;

				special->estring = true;
				special->load_state = LOADSTATE_LOADING;

				patchserverRequestPatchFileInit(special);
			}
			else
			{
				int k;
				patchfileDupOverwrite(special, patch);
				special->load_state = LOADSTATE_ALL;
				for(k = 0; k < eaSize(&special->requests); k++)
					patchserverHandleWaitingRequest(special, special->requests[k]);
				eaDestroy(&special->requests);
			}

			SAFE_FREE(special->prepend);
			special->special_parent = NULL;
		}
		EARRAY_FOREACH_END;
		eaDestroy(&patch->special_files);
	}

	// Clean up.
	if(patch->load_state != LOADSTATE_LOADING)
	{
		eaDestroy(&patch->requests);
		if(patch->delete_me)
			patchfileDestroy(&patch);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Initialize a PatchFile for transfer, in a background thread.
void patchserverRequestPatchFileInit(PatchFile *patch)
{
	devassert(patch->load_state == LOADSTATE_LOADING);
	devassert(patch->loadinfo->compressed || patch->loadinfo->uncompressed);

	// Create the thread pool, if not yet created.
	if (!s_process_patch_file_thread)
		s_process_patch_file_thread = mwtCreate(2048, 4096, s_num_process_threads, NULL, NULL, thread_ProcessPatchFile, patchserverFinishProcessPatchFile, "ProcessPatchFileThread");

	// Send processing request to background thread.
	++s_requests_pending;
	mwtQueueInput(s_process_patch_file_thread, patch, true);
}

// Perform background processing for patchserverInitPatchFile();
void patchserverInitPatchFileTick()
{
	if (s_process_patch_file_thread)
		mwtProcessOutputQueue(s_process_patch_file_thread);
}

// Number of files currently subject to load processing.
int patchserverLoadProcessRequestsPending()
{
	return s_requests_pending;
}
