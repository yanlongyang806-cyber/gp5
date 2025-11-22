#include "ShaderServer.h"
#include "ShaderServerInterface.h"
#include "textparser.h"
#include "cmdparse.h"
#include "net.h"
#include "earray.h"
#include "MemoryMonitor.h"
#include "ShaderServer.h"
#include "utils.h"
#include "sysutil.h"
#include "SelfPatch.h"
#include "file.h"
#include "FolderCache.h"
#include "ConsoleDebug.h"
#include "StringCache.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "crypt.h"
#include "UnitSpec.h"
#include "utilitiesLib.h"
#include "CrypticPorts.h"


static const char *worker_target_names[] = {
	"DX9_37",
	"C_42",
};
STATIC_ASSERT(ARRAY_SIZE(worker_target_names) == SHADERTARGETVERSION_Max);

NetComm *comm;
NetListen *listener;
Checksum shaderserver_checksum;

typedef struct ShaderServerRequest ShaderServerRequest;
typedef struct Assignment Assignment;

typedef struct ShaderServerStats
{
	const char *name;
	const char *currentIP;
	bool bConnected;
	int connectCount;
	int compileCount;
	int cacheHitCount;
	int cacheMissCount;
	int compileCountThisConnection;
} ShaderServerStats;

ShaderServerStats global_stats = {0};
static bool echoCompileRequests;
AUTO_CMD_INT(echoCompileRequests,echoCompileRequests);

static bool enableCaching=true;
AUTO_CMD_INT(enableCaching,enableCaching);

#ifdef _M_X64
static int cacheSize=4000; // 4gb
#else
static int cacheSize=500;
#endif
AUTO_CMD_INT(cacheSize, cacheSize) ACMD_CMDLINE;

typedef struct ShaderServerCacheElem
{
	U32 timestamp;
	U32 mem_size;
	char *text_simplified; // shader model + entry_point + text
	ShaderCompileResponseData *response;
} ShaderServerCacheElem;

StashTable stShaderServerCache;
size_t cache_byte_size;

typedef struct ShaderServerClient
{
	NetLink *link;
	char name[100];
	char ip_str[20];
	bool is_worker;
	bool is_good_client;
	ShaderTargetVersion *worker_targets; // What targets this worker can compile for
	ShaderServerRequest **requests; // Requests we've received
	Assignment *current_assignment; // The one a worker is working on
	ShaderServerStats *stats;
} ShaderServerClient;

typedef struct Assignment
{
	ShaderServerRequest *req;
	ShaderServerClient *worker;
	S64 timestamp;
} Assignment;

typedef struct ShaderServerRequest
{
	ShaderCompileRequestData *request_data;
	ShaderServerClient *client;
	Assignment **assignments; // Who's working on them
	char *requestCacheKey;
} ShaderServerRequest;

ShaderServerClient **workers;
ShaderServerClient **free_workers;
ShaderServerRequest **requests;

void destroyRequest(ShaderServerRequest *req);

bool bIsServer=false;
AUTO_CMD_INT(bIsServer, server) ACMD_CMDLINE;

bool bTitleNeedsUpdate=true;

void checkUpdateTitle(void)
{
	if (bTitleNeedsUpdate)
	{
		char	buf[200];
		// Gather stats about workers for different target types
		int		worker_stats[SHADERTARGETVERSION_Max][2] = {0};
		char	free_worker_details[100]="";
		int i;
		FOR_EACH_IN_EARRAY(workers, ShaderServerClient, worker)
		{
			for (i=0; i<eaiSize(&worker->worker_targets); i++)
			{
				worker_stats[worker->worker_targets[i]][0]++;
				if (!worker->current_assignment)
					worker_stats[worker->worker_targets[i]][1]++;
			}
		}
		FOR_EACH_END;
		for (i=0; i<SHADERTARGETVERSION_Max; i++)
		{
			strcatf(free_worker_details, "%d", worker_stats[i][1]);
			if (i!=SHADERTARGETVERSION_Max-1)
				strcat(free_worker_details, "/");
		}
		// Update title
		sprintf(buf,"ShaderServer | %d connections  %d workers  %s free workers  %d requests", listenCount(listener), eaSize(&workers), free_worker_details, eaSize(&requests));
		setConsoleTitle(buf);
		bTitleNeedsUpdate = false;
	}
}

void updateTitle(void)
{
	bTitleNeedsUpdate = true;
}

StashTable stStats;
static ShaderServerStats *getStats(const char *name)
{
	ShaderServerStats *stats;
	name = allocAddCaseSensitiveString(name);
	if (!stStats)
		stStats = stashTableCreateWithStringKeys(16, StashDefault);
	if (!stashFindPointer(stStats, name, &stats))
	{
		stats = calloc(1, sizeof(*stats));
		stashAddPointer(stStats, name, stats, false);
		stats->name = name;
	}
	return stats;
}


void handleWorkerConnect(Packet *pkt,NetLink *link,ShaderServerClient **client_p)
{
	ShaderServerClient *client=0;
	int version;
	int exe_size;
	U32 exe_checksum;

	client = calloc(sizeof(*client),1);
	client->link = link;
	client->is_worker = true;
	*client_p = client;

	pktGetString(pkt,SAFESTR(client->name));
	version = pktGetBitsAuto(pkt);
	exe_size = pktGetBitsAuto(pkt);
	exe_checksum = pktGetBits(pkt, 32);
	linkGetIpStr(link, SAFESTR(client->ip_str));
	if (!pktEnd(pkt))
	{
		int i;
		int num_targets = pktGetBitsAuto(pkt);
		assert(num_targets <= SHADERTARGETVERSION_Max);
		assert(num_targets);
		for (i=0; i<num_targets; i++)
		{
			ShaderTargetVersion target = pktGetBitsAuto(pkt);
			eaiPush(&client->worker_targets, target);
		}
	} else {
		// Old client
		eaiPush(&client->worker_targets, SHADERTARGETVERSION_D3DX9_37);
	}
	assert(eaiSize(&client->worker_targets));

	if (version == SHADERSERVER_VERSION) {
#if _M_X64
		if (0) // Don't update 32-bit clients!
#else
		if ((exe_size != shaderserver_checksum.size ||
			exe_checksum != shaderserver_checksum.values[0]) && // executable mismatch
			(eaiSize(&client->worker_targets) > 1 || client->worker_targets[0] != SHADERTARGETVERSION_D3DX9_37)) // not an intentionally old client
#endif
		{
			void *data;
			int data_size;
			// Need to patch
			printf("OLD EXE Worker connect from %s [%p]\n",client->name,link);
			// Send them the new .exe
			data = fileAlloc(getExecutableName(), &data_size);
			if (data) {
				Packet *self_patch = pktCreate(link, SHADERSERVER_SELFPATCH);
				pktSendBitsAuto(self_patch, data_size);
				pktSendBytes(self_patch, data_size, data);
				pktSend(&self_patch);
				fileFree(data);
			}
		} else {
			int i;
			eaPush(&workers,client);
			eaPush(&free_workers,client);
			printf("Worker connect from %s [%p]",client->name,link);
			for (i=0; i<eaiSize(&client->worker_targets); i++)
			{
				printf(" %s", worker_target_names[client->worker_targets[i]]);
			}
			printf("\n");
		}
	} else {
		printf("OLD VERSION Worker connect from %s [%p] %d\n", client->name, link, version);
	}
	updateTitle();
}

void handleClientConnect(Packet *pkt,NetLink *link,ShaderServerClient **client_p)
{
	ShaderServerClient *client=0;
	ShaderServerStats *stats;
	int version;

	client = calloc(sizeof(*client),1);
	client->link = link;
	client->is_worker = false;
	*client_p = client;

	pktGetString(pkt,SAFESTR(client->name));
	version = pktGetBitsAuto(pkt);
	linkGetIpStr(link, SAFESTR(client->ip_str));

	client->stats = stats = getStats(client->name);
	stats->currentIP = client->ip_str;
	stats->connectCount++;
	global_stats.connectCount++;
	stats->compileCountThisConnection = 0;
	stats->bConnected = true;

	client->is_good_client = (version == SHADERSERVER_VERSION);

	// Let them know if they're OK to send requests
	{
		Packet *pak = pktCreate(link, SHADERSERVER_CLIENT_CONNECT_ACK);
		pktSendBits(pak, 1, version == SHADERSERVER_VERSION);
		pktSendBitsAuto(pak, SHADERSERVER_VERSION);
		pktSend(&pak);
	}
	printf("Client connect from %s [%p]\n",client->name,link);
	updateTitle();
}

char *simplifyRequest(ShaderCompileRequestData *request)
{
	char *s;
	char *estr=NULL;
	estrAppend2(&estr, request->shaderModel);
	estrConcatStatic(&estr, " $$ ");
	estrAppend2(&estr, request->entryPointName);
	estrConcatf(&estr, " $$ %d %d %d $$ ", request->target, request->otherFlags, request->compilerFlags);
	s = strdup_uncommented(request->programText, STRIP_ALL);
	estrAppend2(&estr, s);
	free(s);
	estrSetSize(&estr, estrLength(&estr));
	s = strdup(estr);
	estrDestroy(&estr);
	return s;
}

bool assignToWorker(ShaderServerRequest *req)
{
	Packet	*pkt;
	ShaderServerClient *worker=NULL;
	Assignment *as;

	// Find a worker supporting our target
	FOR_EACH_IN_EARRAY(free_workers, ShaderServerClient, worker_walk)
	{
		int i;
		for (i=0; i<eaiSize(&worker_walk->worker_targets); i++)
		{
			if ((req->request_data->otherFlags & SHADERSERVER_FLAGS_VERSION_MASK) == worker_walk->worker_targets[i])
			{
				worker = worker_walk;
			}
		}
		if (worker)
		{
			eaRemoveFast(&free_workers, iworker_walkIndex);
			break;
		}
	}
	FOR_EACH_END;

	if (!worker)
		return false;

	as = callocStruct(Assignment);
	as->req = req;
	as->worker = worker;
	as->timestamp = timerCpuTicks64();
	eaPush(&req->assignments, as);
	worker->current_assignment = as;
	pkt = pktCreate(worker->link,SHADERSERVER_WORKER_ASSIGNMENT_START);
	pktSendBitsAuto(pkt, req->request_data->request_id);
	pktSendBitsAuto(pkt, req->request_data->target);
	pktSendString(pkt, req->request_data->programText);
	pktSendString(pkt, req->request_data->entryPointName);
	pktSendString(pkt, req->request_data->shaderModel);
	pktSendBitsAuto(pkt, req->request_data->compilerFlags);
	pktSendBitsAuto(pkt, req->request_data->otherFlags);
	pktSendBits(pkt, 1, 0);
	pktSend(&pkt);
	updateTitle();
	return true;
}

void queueNewRequest(ShaderServerRequest *req)
{
	// Add to queue for tracking regardless
	eaPush(&requests, req);
	eaPush(&req->client->requests, req);
	// If there's a free worker, send it to them immediately!
	assignToWorker(req);
}

void sendResponseToClient(ShaderServerClient *client, ShaderCompileResponseData *response)
{
	Packet *pkt;
	pkt = pktCreate(client->link, SHADERSERVER_CLIENT_RESPONSE);
	pktSendBitsAuto(pkt, response->request_id);
	pktSendBitsAuto(pkt, response->compiledResultSize);
	pktSendBitsAuto(pkt, response->updbDataSize);
	pktSendString(pkt, response->errorMessage);
	pktSendString(pkt, response->updbPath);
	if (response->compiledResultSize)
		pktSendBytes(pkt, response->compiledResultSize, response->compiledResult);
	if (response->updbDataSize)
		pktSendBytes(pkt, response->updbDataSize, response->updbData);
	pktSendBits(pkt, 1, 0);
	pktSend(&pkt);
}

void handleClientRequest(Packet *pkt,NetLink *link,ShaderServerClient **client_p)
{
	int terminator;
	char debug_name[256];
	ShaderServerClient *client = *client_p;
	ShaderServerRequest *req;
	if (!client || !client->is_good_client) {
		assert(0);
		// Bad request, came in before a connect, or from a client not allowed to connect!
		// Might have to fail silently instead...
		return;
	}

	client->stats->compileCount++;
	global_stats.compileCount++;
	client->stats->compileCountThisConnection++;

	req = callocStruct(ShaderServerRequest);
	req->request_data = callocStruct(ShaderCompileRequestData);
	req->request_data->request_id = pktGetBitsAuto(pkt);
	req->request_data->target = pktGetBitsAuto(pkt);
	req->request_data->programText = strdup(pktGetStringTemp(pkt));
	req->request_data->entryPointName = strdup(pktGetStringTemp(pkt));
	req->request_data->shaderModel = strdup(pktGetStringTemp(pkt));
	req->request_data->compilerFlags = pktGetBitsAuto(pkt);
	req->request_data->otherFlags = pktGetBitsAuto(pkt);
	if (req->request_data->otherFlags == 0xCCCCCCCC)
		req->request_data->otherFlags = 0;
	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);
	if (echoCompileRequests)
	{
		char *s = strchr(req->request_data->programText, '/');
		if (s)
		{
			strcpy_trunc(debug_name, s);
			s = strchr(debug_name, '\n');
			if (s)
				*s = '\0';
		} else {
			strcpy(debug_name, "unknown shader");
		}
	}
	req->client = client;

	if ((req->request_data->otherFlags & SHADERSERVER_FLAGS_VERSION_MASK) >= SHADERTARGETVERSION_Max)
	{
		// Bad request
		printf("Got request for shader target that is not supported: 0x%x\n", req->request_data->otherFlags);
		destroyRequest(req);
		return;
	}

	// Check Cache
	if (enableCaching)
	{
		char *simplified;
		ShaderServerCacheElem *elem;
		if (!stShaderServerCache)
			stShaderServerCache = stashTableCreateWithStringKeys(1024, StashDefault|StashCaseSensitive);
		simplified = simplifyRequest(req->request_data);
		if (stashFindPointer(stShaderServerCache, simplified, &elem))
		{
			if (echoCompileRequests)
				printf("Cache HIT  from %s for %s\n", client->name, debug_name); 
			client->stats->cacheHitCount++;
			global_stats.cacheHitCount++;
			SAFE_FREE(simplified);
			// Just return cached result and destroy structure
			elem->timestamp = timerCpuSeconds();
			elem->response->request_id = req->request_data->request_id;
			sendResponseToClient(client, elem->response);
			destroyRequest(req); // Not in global arrays yet
			return;
		}
		if (echoCompileRequests)
			printf("Cache MISS from %s for %s\n", client->name, debug_name); 
		client->stats->cacheMissCount++;
		global_stats.cacheMissCount++;
		req->requestCacheKey = simplified;
	} else {
		if (echoCompileRequests)
			printf("Uncached request from %s for %s\n", client->name, debug_name); 
	}

	queueNewRequest(req);
}

// Caller needs to remove from client's request array and global request array
void destroyRequest(ShaderServerRequest *req)
{
	StructDestroySafe(parse_ShaderCompileRequestData, &req->request_data);
	FOR_EACH_IN_EARRAY(req->assignments, Assignment, as)
	{
		as->worker->current_assignment = NULL;
		free(as);
	}
	FOR_EACH_END;
	SAFE_FREE(req->requestCacheKey);
	eaDestroy(&req->assignments);
	free(req);
}

void destroyShaderCompileResponseData(ShaderCompileResponseData **response)
{
	SAFE_FREE((*response)->compiledResult);
	SAFE_FREE((*response)->updbData);
	StructDestroySafe(parse_ShaderCompileResponseData, response);
}

#define CACHE_OVERHEAD (sizeof(ShaderServerCacheElem) + sizeof(ShaderCompileResponseData) + 32)

void destroyShaderServerCacheElem(ShaderServerCacheElem *elem)
{
	cache_byte_size -= elem->mem_size;
	SAFE_FREE(elem->text_simplified);
	destroyShaderCompileResponseData(&elem->response);
	free(elem);
}

AUTO_COMMAND;
void clearCache(void)
{
	size_t old_size=cache_byte_size;
	FOR_EACH_IN_STASHTABLE2(stShaderServerCache, elem)
	{
		ShaderServerCacheElem *cache_elem = stashElementGetPointer(elem);
		stashRemovePointer(stShaderServerCache, cache_elem->text_simplified, NULL);
		destroyShaderServerCacheElem(cache_elem);
	}
	FOR_EACH_END;
	printf("Freed %s", friendlyBytes(old_size - cache_byte_size));
	printf(", now using %s.\n", friendlyBytes(cache_byte_size));
}

int cmpCacheElem(const void *a, const void *b)
{
	ShaderServerCacheElem *pa = *(ShaderServerCacheElem **)a;
	ShaderServerCacheElem *pb = *(ShaderServerCacheElem **)b;
	return pa->timestamp - pb->timestamp;
}

AUTO_COMMAND;
void clearCacheLimited(F32 days, F32 mbs)
{
	U32 now = timerCpuSeconds();
	size_t old_size=cache_byte_size;
	ShaderServerCacheElem **all_elems=NULL;
	int i;
	FOR_EACH_IN_STASHTABLE2(stShaderServerCache, elem)
	{
		ShaderServerCacheElem *cache_elem = stashElementGetPointer(elem);
		if (now - cache_elem->timestamp > days * 24*60*60)
		{
			stashRemovePointer(stShaderServerCache, cache_elem->text_simplified, NULL);
			destroyShaderServerCacheElem(cache_elem);
		} else {
			eaPush(&all_elems, cache_elem);
		}
	}
	FOR_EACH_END;

	if (cache_byte_size > mbs * 1024*1024)
	{
		eaQSort(all_elems, cmpCacheElem);
		i=0;
		while (cache_byte_size > mbs*1024*1024)
		{
			ShaderServerCacheElem *cache_elem;
			assert(all_elems);
			cache_elem = all_elems[i];
			stashRemovePointer(stShaderServerCache, cache_elem->text_simplified, NULL);
			destroyShaderServerCacheElem(cache_elem);
			i++;
		}
	}

	if (old_size != cache_byte_size)
	{
		printf("Freed %s", friendlyBytes(old_size - cache_byte_size));
		printf(", now using %s.\n", friendlyBytes(cache_byte_size));
	}

	eaDestroy(&all_elems);
}


void hadleWorkerDone(Packet *pkt,ShaderServerClient **client_p)
{
	ShaderServerClient *worker = *client_p;
	ShaderCompileResponseData *response;
	ShaderServerRequest *req;
	int terminator;
	if (!worker) {
		assert(0); // Shouldn't be able to happen
		return;
	}
	if (!worker->current_assignment) {
		// We have no assignment, either it was canceled, the requested disconnected,
		//  or we were one of two people working on it, and we were too slow.
		// Put us back in the free queue
		eaPush(&free_workers, worker);
		updateTitle();
		return;
	}

	req = worker->current_assignment->req;

	response = callocStruct(ShaderCompileResponseData);
	response->request_id = pktGetBitsAuto(pkt);
	response->compiledResultSize = pktGetBitsAuto(pkt);
	response->updbDataSize = pktGetBitsAuto(pkt);
	response->errorMessage = strdup(pktGetStringTemp(pkt));
	response->updbPath = strdup(pktGetStringTemp(pkt));

	assert(req->request_data->request_id == response->request_id);
	if (response->compiledResultSize) {
		response->compiledResult = malloc(response->compiledResultSize);
		pktGetBytes(pkt, response->compiledResultSize, response->compiledResult);
	} else
		response->compiledResult = NULL;
	if (response->updbDataSize) {
		response->updbData = malloc(response->updbDataSize);
		pktGetBytes(pkt, response->updbDataSize, response->updbData);
	} else
		response->updbData = NULL;

	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	// Send it back to the client
	sendResponseToClient(req->client, response);

	if (enableCaching && req->requestCacheKey && !stashFindPointer(stShaderServerCache, req->requestCacheKey, NULL))
	{
		ShaderServerCacheElem *elem = calloc(sizeof(*elem), 1);
		elem->response = response;
		elem->text_simplified = req->requestCacheKey; // Steal pointer
		elem->timestamp = timerCpuSeconds();
		req->requestCacheKey = NULL;
		verify(stashAddPointer(stShaderServerCache, elem->text_simplified, elem, false));
		elem->mem_size = (int)(CACHE_OVERHEAD + strlen(elem->text_simplified)+1 +
			elem->response->compiledResultSize + elem->response->updbDataSize);
		cache_byte_size += elem->mem_size;
// 		if (1) // About 30% of output data files are duplicates
// 		{
// 			U32 id;
// 			int index=0;
// 			char name[MAX_PATH];
// 			FILE *f;
// 			id = cryptAdler32(elem->response->compiledResult, elem->response->compiledResultSize);
// 			sprintf(name, "c:/temp/ShaderServer/%d-%d.txt", id, index);
// 			while (fileExists(name))
// 			{
// 				index++;
// 				sprintf(name, "c:/temp/ShaderServer/%d-%d.txt", id, index);
// 			}
// 			f = fopen(name, "wt");
// 			if (f)
// 			{
// 				fwrite(elem->text_simplified, estrLength(&elem->text_simplified), 1, f);
// 				fclose(f);
// 			}
// 			sprintf(name, "c:/temp/ShaderServer/%d-%d.dat", id, index);
// 			f = fopen(name, "wb");
// 			if (f)
// 			{
// 				fwrite(elem->response->compiledResult, elem->response->compiledResultSize, 1, f);
// 				fclose(f);
// 			}
// 			id++;
// 		}
	} else {
		// Destroy data
		destroyShaderCompileResponseData(&response);
	}

	// Clean up
	eaFindAndRemove(&requests, req);
	eaFindAndRemoveFast(&req->client->requests, req);
	destroyRequest(req);

	eaPush(&free_workers, worker);
	updateTitle();
}


void shaderServerMsg(Packet *pkt,int cmd,NetLink *link,ShaderServerClient **client_p)
{
	ShaderServerClient	*client=0;

	if (client_p)
		client = *client_p;
 	switch(cmd)
 	{
 		xcase SHADERSERVER_WORKER_CONNECT:
 			handleWorkerConnect(pkt,link,client_p);
		xcase SHADERSERVER_CLIENT_CONNECT:
			handleClientConnect(pkt,link,client_p);
		xcase SHADERSERVER_CLIENT_REQUEST:
			handleClientRequest(pkt,link,client_p);
 		xcase SHADERSERVER_WORKER_ASSIGNMENT_DONE:
 			hadleWorkerDone(pkt,client_p);
		xdefault:
			assert(0);
 	}
}

void shaderServerConnect(NetLink *link,ShaderServerClient **client_p)
{
	linkSetKeepAliveSeconds(link, PING_TIME);
}

void shaderServerDisconnect(NetLink *link,ShaderServerClient **client_p)
{
	ShaderServerClient	*client;

	if (!client_p)
		return;
	client = *client_p;
	if (!client)
		return;

	if (client->stats)
	{
		client->stats->bConnected = false;
		client->stats = NULL;
	}
	eaFindAndRemove(&workers,client);
	eaFindAndRemove(&free_workers,client);
	// Cancel any requests and assignments
	FOR_EACH_IN_EARRAY(requests, ShaderServerRequest, req)
	{
		if (req->client == client) {
			// Our request
			// Free this request
			destroyRequest(req);
			eaRemove(&requests, ireqIndex);
		}
	}
	FOR_EACH_END;
	eaDestroy(&client->requests);

	if (client->current_assignment) {
		ShaderServerRequest *req = client->current_assignment->req;
		// We're working on something, it needs to be reassigned
		client->current_assignment->worker = NULL;
		eaFindAndRemoveFast(&client->current_assignment->req->assignments, client->current_assignment);
		SAFE_FREE(client->current_assignment);
		assignToWorker(req);
	}

	updateTitle();
	printf("Disconnect from %s [%p]\n",client->name,link);
}

void shaderServerUpdate(void)
{
	int no_workers[SHADERTARGETVERSION_Max];
	memset(no_workers, 0, sizeof(no_workers));
	checkUpdateTitle();
	// Check for requests to send to free workers
	if (eaSize(&free_workers)) {
		FOR_EACH_IN_EARRAY(requests, ShaderServerRequest, req)
		{
			if (!eaSize(&req->assignments)) {
				int index=(req->request_data->otherFlags & SHADERSERVER_FLAGS_VERSION_MASK);
				if (index >= ARRAY_SIZE(no_workers))
					continue; // Bad request
				if (no_workers[index])
					continue;
				if (!assignToWorker(req))
					no_workers[index] = 1;
			}
		}
		FOR_EACH_END;
	}
}

void printStat(ShaderServerStats *stats)
{
	printf("%c %15s  compiles:%7d/%4d  hit/miss: %7d/%7d (%5.1f%%) connects:%4d  %s\n", stats->bConnected?'*':'-', stats->name,
		stats->compileCount, stats->compileCountThisConnection,
		stats->cacheHitCount, stats->cacheMissCount, (stats->cacheHitCount*100.f/(stats->cacheHitCount+stats->cacheMissCount)),
		stats->connectCount, stats->currentIP);
}

AUTO_COMMAND;
void printStats(void)
{
	FOR_EACH_IN_STASHTABLE(stStats, ShaderServerStats, stats)
	{
		if (stats->compileCount)
			printStat(stats);
	}
	FOR_EACH_END;
	printf("Global stats:\n");
	printStat(&global_stats);
	//printf("Cache misses: %d   Cache hits: %d (%1.1f%%)\n", shader_server_misc_stats.cacheMisses, shader_server_misc_stats.cacheHits, shader_server_misc_stats.cacheHits*100.f / (float)(shader_server_misc_stats.cacheHits + shader_server_misc_stats.cacheMisses));
	printf("Cache size: %d elems (%s)\n", stashGetCount(stShaderServerCache), friendlyBytes(cache_byte_size));
}

int wmain(int argc, WCHAR** argv_wide)
{
	int timer;
	int cacheCleartimer;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	setCavemanMode();
		
	DO_AUTO_RUNS

	timer = timerAlloc();
	cacheCleartimer = timerAlloc();

	cmdParseCommandLine(argc,argv);

	selfPatchStartup(&shaderserver_checksum);

    {
        extern char tmpFile[MAX_PATH];
        sprintf(tmpFile, "%s/%08x", fileTempDir(), GetCurrentProcessId());
    }

	printf("%s Running.\n", getExecutableName());
	comm = commCreate(20,0);
	if (bIsServer) {
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
		listener = commListen(comm,LINKTYPE_UNSPEC, 0,SHADERSERVER_PORT,shaderServerMsg,shaderServerConnect,shaderServerDisconnect,sizeof(ShaderServerClient *));
		if (listener) {
			printf("Running as server.\n");
		} else {
			printf("Failed to create listener.\n");
		}
	} else {
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
		printf("Running as worker.\n");
		shaderServerWorkerInit(comm);
	}

	for(;;)
	{
		utilitiesLibOncePerFrame(REAL_TIME);
		commMonitor(commDefault());
		commMonitor(comm);
		if (timerElapsed(timer) > 0.01f)
		{
			timerStart(timer);
			if (bIsServer) {
				shaderServerUpdate();
				if (timerElapsed(cacheCleartimer) > 60)
				{
					timerStart(cacheCleartimer);
					clearCacheLimited(3, cacheSize);
				}
			} else {
				shaderServerWorkerUpdate(comm);
			}
		}
		DoConsoleDebugMenu(GetDefaultConsoleDebugMenu());
	}
	EXCEPTION_HANDLER_END
	return 0;
}
