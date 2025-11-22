#include "TaskServer.h"
#include "TaskServerClientInterface.h"
#include "textparser.h"
#include "cmdparse.h"
#include "net.h"
#include "earray.h"
#include "MemoryMonitor.h"
#include "utils.h"
#include "fileutil.h"
#include "sysutil.h"
#include "SelfPatch.h"
#include "file.h"
#include "FolderCache.h"
#include "ConsoleDebug.h"
#include "StringCache.h"
#include "ScratchStack.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "crypt.h"
#include "UnitSpec.h"
#include "utilitiesLib.h"
#include "SentryServerComm.h"
#include "process_util.h"
#include "Alerts.h"
#include "sock.h"
#include "sysUtil.h"
#include "structNet.h"
#include "SimplygonInterface.h"

static bool taskSpawnsWaitForDebugger = false;
AUTO_CMD_INT(taskSpawnsWaitForDebugger,taskSpawnsWaitForDebugger);

static bool taskSpawnDebuggable = false;
AUTO_CMD_INT(taskSpawnDebuggable,taskSpawnDebuggable);

static bool serverVerboseLog = 1;
AUTO_CMD_INT(serverVerboseLog, serverVerboseLog);

static bool sbDoAutoSpawningStartup = false;
static int siAutoSpawningStartupChildServersPerMachine = 0;
static char **sppAutoSpawningMachines = NULL;
static void SpawnAWorker(char *pMachineName);

static const char *worker_target_names[] = {
	"DX9_37",
	"C_42",
};
STATIC_ASSERT(ARRAY_SIZE(worker_target_names) == SHADERTASKTARGETVERSION_Max);

NetComm *comm;
NetListen *listener;
Checksum shaderserver_checksum;

// SIMPLYGON TODO - move to netlink
#define LFSM_SENDFILE_BUFSIZE (64 * 1024)

typedef struct LFSM_FileTransfer
{
	FILE *pSourceFile;
	int iHandle;
	S64 iFileTotalSize;
	S64 iRemainingSize;
} LFSM_FileTransfer;

typedef struct TaskServerRequest TaskServerRequest;
typedef struct Assignment Assignment;

typedef struct TaskServerStats
{
	const char *name;
	const char *currentIP;
	bool bConnected;
	int connectCount;
	int compileCount;
	int cacheHitCount;
	int cacheMissCount;
	int compileCountThisConnection;
} TaskServerStats;

TaskServerStats global_stats = {0};
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

typedef struct ShaderCacheElem
{
	U32 timestamp;
	U32 mem_size;
	char *text_simplified; // shader model + entry_point + text
	ShaderCompileTaskResponseData *response;
} ShaderCacheElem;

#define CACHE_OVERHEAD (sizeof(ShaderCacheElem) + sizeof(ShaderCompileTaskResponseData) + 32)

StashTable stTaskServerCache;
size_t cache_byte_size;

typedef struct TaskServerClient
{
	NetLink *link;
	char name[100];
	char ip_str[20];
	DWORD remoteProcessPID;
	bool is_worker;
	bool is_good_client;
	ShaderTaskTargetVersion *worker_targets; // What targets this worker can compile for
	TaskServerRequest **requests; // Requests we've received
	Assignment *current_assignment; // The one a worker is working on
	TaskServerStats *stats;
} TaskServerClient;

typedef struct Assignment
{
	TaskServerRequest *req;
	TaskServerClient *worker;
	S64 timestamp;
} Assignment;


typedef enum TaskServer_TaskRequestState
{
	TSTRS_ReceivedFromClient,
	TSTRS_Dispatched,
	TSTRS_WorkerDone,
	TSTRS_SendingAttachment,
	TSTRS_ReadyToReturnToClient,
} TaskServer_TaskRequestState;


typedef struct TaskServerRequest
{
	TaskClientTaskPacket *request_data;
	void *task_data;
	ParseTable *type_TaskData;

	TaskClientTaskPacket *taskResponsePacket;
	void *taskResultData;

	TaskServer_TaskRequestState taskState;
	LFSM_FileTransfer linkFileTransfer;

	TaskServerClient *client;
	Assignment **assignments; // Who's working on them
} TaskServerRequest;

TaskServerClient **workers;
TaskServerClient **free_workers;
TaskServerRequest **requests;
TaskServerRequest **g_taskResults;

void destroyRequest(TaskServerRequest *req);
void sendTaskResponseToClient(TaskServerClient *client, TaskClientTaskPacket *taskRequest, void *taskResultData);

bool bIsServer=false;
AUTO_CMD_INT(bIsServer, server) ACMD_CMDLINE;

bool bTitleNeedsUpdate=true;

char *simplifyRequest(ShaderCompileTaskRequestData *request)
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

bool handleClientShaderCompileRequest(TaskServerClient * client, TaskClientTaskPacket * taskRequest, ShaderCompileTaskRequestData *taskRequestData)
{
	char debug_name[256];

	client->stats->compileCount++;
	global_stats.compileCount++;
	client->stats->compileCountThisConnection++;

	if (echoCompileRequests)
	{
		char *s;

		taskLogPrintf("Server rcvd task request TASKSERVER_TASK_COMPILER_SHADER %d %s\n", taskRequest->request_id, taskRequestData->shaderModel);

		s = strchr(taskRequestData->programText, '/');
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

	// Check Cache
	if (enableCaching)
	{
		char *simplified;
		ShaderCacheElem *elem;
		if (!stTaskServerCache)
			stTaskServerCache = stashTableCreateWithStringKeys(1024, StashDefault|StashCaseSensitive);
		simplified = simplifyRequest(taskRequestData);
		if (stashFindPointer(stTaskServerCache, simplified, &elem))
		{
			if (echoCompileRequests)
				printf("Cache HIT  from %s for %s\n", client->name, debug_name); 
			client->stats->cacheHitCount++;
			global_stats.cacheHitCount++;
			SAFE_FREE(simplified);
			// Just return cached result and destroy structure
			elem->timestamp = timerCpuSeconds();
			sendTaskResponseToClient(client, taskRequest, elem->response);
			return false;
		}
		if (echoCompileRequests)
			printf("Cache MISS from %s for %s\n", client->name, debug_name); 
		client->stats->cacheMissCount++;
		global_stats.cacheMissCount++;
		taskRequest->requestCacheKey = simplified;
	} else {
		if (echoCompileRequests)
			printf("Uncached request from %s for %s\n", client->name, debug_name); 
	}

	return true;
}

bool workerShaderCompile(TaskClientTaskPacket * taskRequest, ShaderCompileTaskRequestData *taskRequestData, void **compileResult)
{
	ShaderCompileTaskResponseData * result = NULL;
	if (echoCompileRequests)
		taskLogPrintf("Worker rcvd task request TASKSERVER_TASK_COMPILER_SHADER %d %s\n%s\n", taskRequest->request_id, taskRequestData->shaderModel, taskRequestData->programText);
	result = StructAlloc(parse_ShaderCompileTaskResponseData);
	*compileResult = result;

	workerShaderCompileImpl(taskRequest, taskRequestData, result);

	return true;
}

bool handleWorkerShaderCompileResponse(TaskClientTaskPacket * taskRequest, ShaderCompileTaskRequestData *taskRequestData, ShaderCompileTaskResponseData *compileResult)
{
	ShaderCompileTaskResponseData * result = NULL;
	bool bDestroyTaskResultData = false;
	if (echoCompileRequests)
		taskLogPrintf("Worker rcvd task request TASKSERVER_TASK_COMPILER_SHADER %d %s\n%s\n", taskRequest->request_id, taskRequestData->shaderModel, taskRequestData->programText);

	if (enableCaching && taskRequest->requestCacheKey && !stashFindPointer(stTaskServerCache, taskRequest->requestCacheKey, NULL))
	{
		ShaderCacheElem *elem = calloc(sizeof(*elem), 1);

		// steal entire compile result for the cache, so don't let shader server code destroy it
		elem->response = compileResult;
		bDestroyTaskResultData = false;

		elem->text_simplified = taskRequest->requestCacheKey; // Steal pointer
		taskRequest->requestCacheKey = NULL;
		elem->timestamp = timerCpuSeconds();
		verify(stashAddPointer(stTaskServerCache, elem->text_simplified, elem, false));
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
// 			sprintf(name, "c:/temp/TaskServer/%d-%d.txt", id, index);
// 			while (fileExists(name))
// 			{
// 				index++;
// 				sprintf(name, "c:/temp/TaskServer/%d-%d.txt", id, index);
// 			}
// 			f = fopen(name, "wt");
// 			if (f)
// 			{
// 				fwrite(elem->text_simplified, estrLength(&elem->text_simplified), 1, f);
// 				fclose(f);
// 			}
// 			sprintf(name, "c:/temp/TaskServer/%d-%d.dat", id, index);
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
		bDestroyTaskResultData = true;
	}

	return bDestroyTaskResultData;
}


bool handleClientRemeshRequest(TaskServerClient * client, TaskClientTaskPacket * taskRequest, SpawnRequestData *taskRequestData)
{
	if (echoCompileRequests)
		taskLogPrintf("Server rcvd task request TASKSERVER_TASK_REMESH_CLUSTER %d %s\n", taskRequest->request_id, taskRequestData->label);
	return true;
}

bool workerRemesh(TaskClientTaskPacket * taskRequest, SpawnRequestData *taskRequestData, void **remeshResult)
{
	int systemExitCode = 0;
	int systemErrnoCode = 0;
	char remeshSpawn[MAX_PATH * 2];
	char remeshOutput[MAX_PATH];
	char remeshOutputName[MAX_PATH];
	SpawnRequestData * remeshTaskResult = NULL;

	if (echoCompileRequests)
		taskLogPrintf("Worker rcvd task request TASKSERVER_TASK_REMESH_CLUSTER %d %s\n", taskRequest->request_id, taskRequestData->label);

	if (!taskRequest->fileAttachment)
	{
		taskLogPrintf("Worker rejected request %d due to missing input data: %s.\n", taskRequest->request_id, taskRequestData->label);
		return false;
	}

	strcpy(remeshOutput, taskRequest->fileAttachmentFull);
	fileStripFileExtension(remeshOutput);
	strcat(remeshOutput, "_result.hogg");
	fileGetFilename(remeshOutput, remeshOutputName);
	// SIMPLYGON TODO - form remesh.exe path from taskserver.exe location
	sprintf(remeshSpawn, "Remesh.exe %s -inputClusterDataFile \"%s\" -outputRemeshDataFile \"%s\" %s", 
		taskSpawnDebuggable ? "" : "-productionmode", taskRequest->fileAttachmentFull, remeshOutput, taskSpawnsWaitForDebugger ? "-WaitForDebugger" : "");
	systemExitCode = system(remeshSpawn);
	if (systemExitCode)
	{
		systemErrnoCode = errno;
		taskLogPrintf("Remesh.exe %s: system() = %d, errno = %d.\n", (int)systemExitCode == (int)-1 ? "crashed" : "failed", 
			systemExitCode, systemErrnoCode);
	}

	strcpy(taskRequest->fileAttachmentFull, remeshOutput);
	StructFreeString(taskRequest->fileAttachment);
	taskRequest->fileAttachment = StructAllocString(remeshOutputName);

	remeshTaskResult = StructClone(parse_SpawnRequestData, taskRequestData);
	remeshTaskResult->remeshSystemExitCode = systemExitCode;
	remeshTaskResult->remeshSystemErrnoCode = systemErrnoCode;
	*remeshResult = remeshTaskResult;
	return true;
}

bool handleWorkerRemeshResponse(TaskClientTaskPacket * taskRequest, SpawnRequestData *taskRequestData, SpawnRequestData *remeshResult)
{
	if (echoCompileRequests)
		taskLogPrintf("Server rcvd task result TASKSERVER_TASK_REMESH_CLUSTER %d %s\n", taskRequest->request_id, remeshResult->label);
	return true;
}

TaskServer_TaskDef taskServerTaskTypes[TASKSERVER_TASK_MAX] = 
{
	{ // client connect dummy task
		NULL,
		NULL,
		0,
		NULL,
		NULL,
		NULL,
	},
	{
		parse_ShaderCompileTaskRequestData,
		parse_ShaderCompileTaskResponseData,
		SHADERTASKTARGETVERSION_Max,
		handleClientShaderCompileRequest,
		workerShaderCompile, 
		handleWorkerShaderCompileResponse, 
	},
	{
		parse_SpawnRequestData,
		parse_SpawnRequestData,
		REMESHWORKERVERSION_Max,
		handleClientRemeshRequest,
		workerRemesh,
		handleWorkerRemeshResponse
	},
};

TaskServer_TaskDef * getTaskDataType(int taskType)
{
	assert(taskType >= TASKSERVER_TASK_COMPILER_SHADER && taskType < TASKSERVER_TASK_MAX);
	return taskServerTaskTypes + taskType;
}



void checkUpdateTitle(void)
{
	if (bTitleNeedsUpdate)
	{
		char	buf[200];
		// Gather stats about workers for different target types
		int		worker_stats[SHADERTASKTARGETVERSION_Max][2] = {0};
		char	free_worker_details[100]="";
		int i;
		FOR_EACH_IN_EARRAY(workers, TaskServerClient, worker)
		{
			for (i=0; i<eaiSize(&worker->worker_targets); i++)
			{
				worker_stats[worker->worker_targets[i]][0]++;
				if (!worker->current_assignment)
					worker_stats[worker->worker_targets[i]][1]++;
			}
		}
		FOR_EACH_END;
		for (i=0; i<SHADERTASKTARGETVERSION_Max; i++)
		{
			strcatf(free_worker_details, "%d", worker_stats[i][1]);
			if (i!=SHADERTASKTARGETVERSION_Max-1)
				strcat(free_worker_details, "/");
		}
		// Update title
		sprintf(buf,"TaskServer | %d connections  %d workers  %s free workers  %d requests", listenCount(listener), eaSize(&workers), free_worker_details, eaSize(&requests));
		setConsoleTitle(buf);
		bTitleNeedsUpdate = false;
	}
}

void updateTitle(void)
{
	bTitleNeedsUpdate = true;
}

StashTable stStats;
static TaskServerStats *getStats(const char *name)
{
	TaskServerStats *stats;
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


void handleWorkerConnect(Packet *pkt,NetLink *link,TaskServerClient **client_p)
{
	TaskServerClient *client=0;
	int version;
	int exe_size;
	U32 exe_checksum;

	linkSetKeepAliveSeconds(link, PING_TIME);
	linkSetTimeout(link, 0);

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
		assert(num_targets <= SHADERTASKTARGETVERSION_Max);
		assert(num_targets);
		for (i=0; i<num_targets; i++)
		{
			ShaderTaskTargetVersion target = pktGetBitsAuto(pkt);
			eaiPush(&client->worker_targets, target);
		}
		client->remoteProcessPID = pktGetBits(pkt, sizeof(client->remoteProcessPID) * CHAR_BIT);
	} else {
		// Old client
		eaiPush(&client->worker_targets, SHADERTASKTARGETVERSION_D3DX9_37);
		client->remoteProcessPID = ~0;
	}
	assert(eaiSize(&client->worker_targets));

	if (version == TASKSERVER_VERSION) {
#if _M_X64
		if (0) // Don't update 32-bit clients!
#else
		if ((exe_size != shaderserver_checksum.size ||
			exe_checksum != shaderserver_checksum.values[0]) && // executable mismatch
			(eaiSize(&client->worker_targets) > 1 || client->worker_targets[0] != SHADERTASKTARGETVERSION_D3DX9_37)) // not an intentionally old client
#endif
		{
			void *data;
			int data_size;
			// Need to patch
			printf("OLD EXE Worker connect from %s [%p]\n",client->name,link);
			// Send them the new .exe
			data = fileAlloc(getExecutableName(), &data_size);
			if (data) {
				Packet *self_patch = pktCreate(link, TASKSERVER_SELFPATCH);
				pktSendBitsAuto(self_patch, data_size);
				pktSendBytes(self_patch, data_size, data);
				pktSend(&self_patch);
				fileFree(data);
			}
		} else {
			int i;
			eaPush(&workers,client);
			eaPush(&free_workers,client);
			taskLogPrintf("Worker connect from %s [%p]",client->name,link);
			for (i=0; i<eaiSize(&client->worker_targets); i++)
			{
				taskLogPrintf(" %s", worker_target_names[client->worker_targets[i]]);
			}
			taskLogPrintf("\n");
		}
	} else {
		taskLogPrintf("OLD VERSION Worker connect from %s [%p] %d\n", client->name, link, version);
	}
	updateTitle();
}

void handleClientConnect(Packet *pkt,NetLink *link,TaskServerClient **client_p)
{
	TaskServerClient *client=0;
	TaskServerStats *stats;
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

	client->is_good_client = (version == TASKSERVER_VERSION);

	// Let them know if they're OK to send requests
	{
		Packet *pak = pktCreate(link, TASKSERVER_CLIENT_CONNECT_ACK);
		pktSendBits(pak, 1, version == TASKSERVER_VERSION);
		pktSendBitsAuto(pak, TASKSERVER_VERSION);
		pktSend(&pak);
	}
	taskLogPrintf("Client connect from %s [%p]\n",client->name,link);
	updateTitle();
}

bool assignToWorker(TaskServerRequest *req)
{
	Packet	*pkt;
	TaskServerClient *worker=NULL;
	Assignment *as;
	int task_type = req->request_data->task_type;

	// Find a worker supporting our target
	FOR_EACH_IN_EARRAY(free_workers, TaskServerClient, worker_walk)
	{
		int i;
		for (i=0; i<eaiSize(&worker_walk->worker_targets); i++)
		{
			if (task_type != TASKSERVER_TASK_COMPILER_SHADER ||
				req->request_data->worker_version == worker_walk->worker_targets[i])
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

	if (serverVerboseLog)
	{
		taskLogPrintf("Assigning task id %d (req 0x%p) to worker (link 0x%p) \\\\%s\\TaskServer.exe (PID %u).", 
			req->request_data->request_id, req, worker->link, worker->name, worker->remoteProcessPID);
	}

	req->taskState = TSTRS_Dispatched;
	if (req->request_data->fileAttachment)
	{
		if (!taskServerSendFile(worker->link, req->request_data->fileAttachmentFull, NULL))
			taskHeaderRemoveAttachedFile(req->request_data);
	}

	pkt = pktCreate(worker->link,TASKSERVER_WORKER_ASSIGNMENT_START);
	ParserSendStructSafe(parse_TaskClientTaskPacket, pkt, req->request_data);
	ParserSendStructSafe(req->type_TaskData, pkt, req->task_data);
	pktSendBits(pkt, 1, 0);
	pktSend(&pkt);

	updateTitle();
	return true;
}

void queueNewRequest(TaskServerRequest *req)
{
	// Add to queue for tracking regardless
	eaPush(&requests, req);
	eaPush(&req->client->requests, req);
	// If there's a free worker, send it to them immediately!
	assignToWorker(req);
}

void sendTaskResponseToClient(TaskServerClient *client, TaskClientTaskPacket *taskRequest, void *taskResultData)
{
	int clientSendPacketSuccess = false;
	Packet *pkt;
	const TaskServer_TaskDef *task_def = getTaskDataType(taskRequest->task_type);

	taskLogPrintf("Sending client %s task result id %d (\"%s\").\n", 
		client->name, taskRequest->request_id, taskRequest->fileAttachment ? taskRequest->fileAttachment : "");
	pkt = pktCreate(client->link, TASKSERVER_CLIENT_RESPONSE);
	ParserSendStructSafe(parse_TaskClientTaskPacket, pkt, taskRequest);
	ParserSendStructSafe(task_def->type_taskResultData, pkt, taskResultData);	
	pktSendBits(pkt, 1, 0);
	clientSendPacketSuccess = pktSend(&pkt);
	taskLogPrintf("Client task result send %s %d.\n", clientSendPacketSuccess == 1 ? "succeeded" : "failed", clientSendPacketSuccess);

}

void handleClientRequest(Packet *pkt,NetLink *link,TaskServerClient **client_p)
{
	int terminator;
	TaskServerClient *client = *client_p;
	TaskServerRequest *req = NULL;
	const TaskServer_TaskDef *task_def = NULL;

	if (!client || !client->is_good_client) {
		assert(0);
		// Bad request, came in before a connect, or from a client not allowed to connect!
		// Might have to fail silently instead...
		return;
	}

	req = callocStruct(TaskServerRequest);
	req->taskState = TSTRS_ReceivedFromClient;
	req->request_data = ParserRecvStructSafe_Create(parse_TaskClientTaskPacket, pkt);

	task_def = getTaskDataType(req->request_data->task_type);
	req->type_TaskData = task_def->type_taskRequestData;

	req->task_data = ParserRecvStructSafe_Create(req->type_TaskData, pkt);

	if (req->request_data->fileAttachment)
	{
		sprintf(req->request_data->fileAttachmentFull, "%s/%s", taskServerGetTmpFileFolder(), req->request_data->fileAttachment);

		// SIMPLYGON TODO demote this assert to return an error code, to allow graceful retry or reporting
		assert(fileExists(req->request_data->fileAttachmentFull));
	}

	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	req->client = client;

	if (req->request_data->worker_version >= task_def->max_worker_version)
	{
		// Bad request
		taskLogPrintf("Got request for shader target/worker version that is not supported: 0x%x\n", req->request_data->worker_version);
		destroyRequest(req);
		return;
	}

	if (!task_def->onServerRecvTask(client, req->request_data, req->task_data))
		destroyRequest(req); // Not in global arrays yet
	else
		queueNewRequest(req);
}


// Caller needs to remove from client's request array and global request array
void requestClearAssignments(TaskServerRequest *req)
{
	FOR_EACH_IN_EARRAY(req->assignments, Assignment, as)
	{
		as->worker->current_assignment = NULL;
		free(as);
	}
	FOR_EACH_END;
	eaDestroy(&req->assignments);
}

// Caller needs to remove from client's request array and global request array
void destroyRequest(TaskServerRequest *req)
{
	bool bDestroyTaskResultData = false;
	TaskServer_TaskDef *task_def = NULL;
	TaskClientTaskPacket *taskResponsePacket = req->taskResponsePacket;

	if (taskResponsePacket)
	{
		task_def = getTaskDataType(taskResponsePacket->task_type);
		if (task_def->onServerRecvCompleteTask)
			bDestroyTaskResultData = task_def->onServerRecvCompleteTask(req->request_data, req->task_data, req->taskResultData);
		if (taskResponsePacket->fileAttachment)
		{
			// destroy the response result file attachment
			fileForceRemove(taskResponsePacket->fileAttachmentFull);
		}

		StructDestroySafe(parse_TaskClientTaskPacket, &req->taskResponsePacket);
		taskResponsePacket = NULL;
		if (bDestroyTaskResultData)
			StructDestroySafeVoid(task_def->type_taskResultData, &req->taskResultData);
	}
	if (req->request_data->fileAttachment)
	{
		// destroy the original request input file attachment
		fileForceRemove(req->request_data->fileAttachmentFull);
	}

	StructDestroySafe(parse_TaskClientTaskPacket, &req->request_data);
	StructDestroySafeVoid(req->type_TaskData, &req->task_data);

	requestClearAssignments(req);
	free(req);
}

void destroyShaderCompileTaskResponseData(ShaderCompileTaskResponseData **response)
{
	StructDestroySafe(parse_ShaderCompileTaskResponseData, response);
}

void destroyShaderCacheElem(ShaderCacheElem *elem)
{
	cache_byte_size -= elem->mem_size;
	SAFE_FREE(elem->text_simplified);
	destroyShaderCompileTaskResponseData(&elem->response);
	free(elem);
}

AUTO_COMMAND;
void clearCache(void)
{
	size_t old_size=cache_byte_size;
	FOR_EACH_IN_STASHTABLE2(stTaskServerCache, elem)
	{
		ShaderCacheElem *cache_elem = stashElementGetPointer(elem);
		stashRemovePointer(stTaskServerCache, cache_elem->text_simplified, NULL);
		destroyShaderCacheElem(cache_elem);
	}
	FOR_EACH_END;
	printf("Freed %s", friendlyBytes(old_size - cache_byte_size));
	printf(", now using %s.\n", friendlyBytes(cache_byte_size));
}

int cmpCacheElem(const void *a, const void *b)
{
	ShaderCacheElem *pa = *(ShaderCacheElem **)a;
	ShaderCacheElem *pb = *(ShaderCacheElem **)b;
	return pa->timestamp - pb->timestamp;
}

AUTO_COMMAND;
void clearCacheLimited(F32 days, F32 mbs)
{
	U32 now = timerCpuSeconds();
	size_t old_size=cache_byte_size;
	ShaderCacheElem **all_elems=NULL;
	int i;
	FOR_EACH_IN_STASHTABLE2(stTaskServerCache, elem)
	{
		ShaderCacheElem *cache_elem = stashElementGetPointer(elem);
		if (now - cache_elem->timestamp > days * 24*60*60)
		{
			stashRemovePointer(stTaskServerCache, cache_elem->text_simplified, NULL);
			destroyShaderCacheElem(cache_elem);
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
			ShaderCacheElem *cache_elem;
			assert(all_elems);
			cache_elem = all_elems[i];
			stashRemovePointer(stTaskServerCache, cache_elem->text_simplified, NULL);
			destroyShaderCacheElem(cache_elem);
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


void requestQueueSendToClient(TaskServerRequest *req)
{
	eaFindAndRemove(&requests, req);
	eaFindAndRemoveFast(&req->client->requests, req);
	eaPush(&g_taskResults, req);
	requestClearAssignments(req);
}

void requestSendResultToClient(TaskServerRequest *req)
{
	sendTaskResponseToClient(req->client, req->taskResponsePacket, req->taskResultData);

	destroyRequest(req);
}

bool linkFileSendingMode_BeginSend(NetLink *pLink, int iFileTransferCmd, LFSM_FileTransfer *pFileTransfer, 
	const char * pSourceFileName, char * pRemoteFileName)
{
	if (pFileTransfer->pSourceFile)
		fclose(pFileTransfer->pSourceFile);

	pFileTransfer->iFileTotalSize = fileSize64(pSourceFileName);
	pFileTransfer->iRemainingSize = pFileTransfer->iFileTotalSize;
	pFileTransfer->pSourceFile = fopen(pSourceFileName, "rb");
	pFileTransfer->iHandle = linkFileSendingMode_BeginSendingFile(pLink, iFileTransferCmd, pRemoteFileName, pFileTransfer->iFileTotalSize);

	return pFileTransfer->pSourceFile != NULL;
}

bool linkFileSendingMode_SendChunk(NetLink *pLink, LFSM_FileTransfer *pFileTransfer)
{
	bool bCompleted = false;
	S64 iBytesToRead = LFSM_SENDFILE_BUFSIZE;
	U8 *buf = ScratchAlloc(LFSM_SENDFILE_BUFSIZE);

	if (iBytesToRead > pFileTransfer->iRemainingSize)
		iBytesToRead = pFileTransfer->iRemainingSize;

	if ((S64)fread(buf, 1, iBytesToRead, pFileTransfer->pSourceFile) != iBytesToRead)
	{
		linkFileSendingMode_CancelSend(pLink, pFileTransfer->iHandle);
		bCompleted = true;
	}
	else
	{
		linkFileSendingMode_SendBytes(pLink, pFileTransfer->iHandle, buf, iBytesToRead);
		pFileTransfer->iRemainingSize -= iBytesToRead;
		if (!pFileTransfer->iRemainingSize)
			bCompleted = true;
	}

	if (bCompleted)
	{
		// close on completion, regardless of success/fail
		fclose(pFileTransfer->pSourceFile);
		pFileTransfer->pSourceFile = NULL;
	}

	ScratchFree(buf);
	buf = NULL;

	return bCompleted;
}

bool requestSendResultAttachmentToClientComplete(TaskServerRequest *req)
{
	bool bSendComplete = false;
	if (req->taskState == TSTRS_WorkerDone)
	{
		req->taskState = TSTRS_SendingAttachment;
		linkFileSendingMode_BeginSend(req->client->link, TASKSERVER_FILE_RECEIVED, 
			&req->linkFileTransfer, req->taskResponsePacket->fileAttachmentFull, 
			req->taskResponsePacket->fileAttachment);
	}
	else
	{
		bSendComplete = linkFileSendingMode_SendChunk(req->client->link, &req->linkFileTransfer);
	}

	return bSendComplete;
}

bool requestSendResultToClientCompleted(TaskServerRequest *req)
{
	bool resultSendComplete = true;
	if (!req->client || !req->client->link)
		// if client disconnects mid-send, immediately stop sending the result
		return resultSendComplete;

	if (!req->taskResponsePacket->fileAttachment)
	{
		req->taskState = TSTRS_ReadyToReturnToClient;
		requestSendResultToClient(req);
	}
	else
	{
		TaskClientTaskPacket * requestData = req->request_data;
		taskLogPrintf("\tTransmitting queued task attachment %d (\"%s\")...\n", requestData->request_id, 
			requestData->fileAttachment ? requestData->fileAttachment : "");
		resultSendComplete = requestSendResultAttachmentToClientComplete(req);
		if (resultSendComplete)
			requestSendResultToClient(req);
	}
	return resultSendComplete;
}

void handleWorkerDone(Packet *pkt,TaskServerClient **client_p)
{
	TaskServerClient *worker = *client_p;
	TaskClientTaskPacket *taskResponsePacket = NULL;
	void *taskResultData = NULL;
	TaskServerRequest *req;
	TaskServer_TaskDef *task_def = NULL;
	int terminator;
	bool bDestroyTaskResultData = true;
	if (!worker) {
		assert(0); // Shouldn't be able to happen
		return;
	}
	if (!worker->current_assignment)
	{
		taskLogPrintf("Worker \\\\%s\\TaskServer.exe (PID %u) sent result packet, but it has no assignment\n", 
			worker->name, worker->remoteProcessPID);
		// We have no assignment, either it was canceled, the requested disconnected,
		//  or we were one of two people working on it, and we were too slow.
		// Put us back in the free queue
		eaPush(&free_workers, worker);
		updateTitle();
		return;
	}

	taskLogPrintf("Queuing worker \\\\%s\\TaskServer.exe (PID %u) result packet.\n", 
		worker->name, worker->remoteProcessPID);
	req = worker->current_assignment->req;
	req->taskState = TSTRS_WorkerDone;

	taskResponsePacket = ParserRecvStructSafe_Create(parse_TaskClientTaskPacket, pkt);
	assert(req->request_data->request_id == taskResponsePacket->request_id);

	task_def = getTaskDataType(taskResponsePacket->task_type);
	taskResultData = ParserRecvStructSafe_Create(task_def->type_taskResultData, pkt);

	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	if (taskResponsePacket->fileAttachment)
	{
		sprintf(taskResponsePacket->fileAttachmentFull, "%s/%s", taskServerGetTmpFileFolder(), taskResponsePacket->fileAttachment);

		// SIMPLYGON TODO demote this assert to return an error code, to allow graceful retry or reporting
		assert(fileExists(taskResponsePacket->fileAttachmentFull));
	}

	req->taskResponsePacket = taskResponsePacket;
	req->taskResultData = taskResultData;

	// Clean up
	requestQueueSendToClient(req);

	eaPush(&free_workers, worker);
	updateTitle();
}


void taskServerMsg(Packet *pkt,int cmd,NetLink *link,TaskServerClient **client_p)
{
	TaskServerClient	*client=0;
	if (linkFileSendingMode_ReceiveHelper(link, cmd, pkt))
		return;

	if (client_p)
		client = *client_p;
 	switch(cmd)
 	{
 		xcase TASKSERVER_WORKER_CONNECT:
 			handleWorkerConnect(pkt,link,client_p);
		xcase TASKSERVER_CLIENT_CONNECT:
			handleClientConnect(pkt,link,client_p);
		xcase TASKSERVER_CLIENT_REQUEST:
			handleClientRequest(pkt,link,client_p);
 		xcase TASKSERVER_WORKER_ASSIGNMENT_DONE:
 			handleWorkerDone(pkt,client_p);
		xdefault:
			assert(0);
 	}
}

static void taskServerHandleFileSendingErrors(char *errorMsg)
{
	taskLogPrintf("\tServer file sending layer error: %s", errorMsg);
}

static void taskServerLogReceivedFile(int iCmd, char *pFileName)
{
	taskLogPrintf("\tServer received file: %s\n", pFileName);
}

void taskServerConnect(NetLink *link,TaskServerClient **client_p)
{
	linkSetKeepAliveSeconds(link, PING_TIME);
	linkFileSendingMode_InitSending(link);
	linkFileSendingMode_InitReceiving(link, taskServerHandleFileSendingErrors);
	linkFileSendingMode_RegisterCallback(link, TASKSERVER_FILE_RECEIVED, taskServerGetTmpFileFolder(), taskServerLogReceivedFile);


}

//when a worker disconnects trigger an alert. Then, if we're in auto-spawn mode, try to respawn it, but throttled
void HandleWorkerDisconnect(NetLink *link, TaskServerClient *pClient)
{
	char *pIPStr = makeIpStr(linkGetIp(link));
	printf("Got a disconnect from worker %s... ", pIPStr);
	if (!sbDoAutoSpawningStartup)
	{
		char *pDisconnectReason = NULL;
		linkGetDisconnectReason(link, &pDisconnectReason);
		CRITICAL_NETOPS_ALERT("WORKER_DISCONNECTED", "A worker task server running on %s has disconnected due to %s. We are NOT in AutoSpawning mode, so it will not be restarted",
			pIPStr, pDisconnectReason);
		estrDestroy(&pDisconnectReason);
		printf("...but no auto-spawning, so doing nothing\n");
	}
	else
	{
		static SimpleEventCounter *pCounter = NULL;
		static U32 siNoRespawnsBeforeThisTime = 0;
		U32 iCurTime = timeSecondsSince2000();

		if (siNoRespawnsBeforeThisTime > iCurTime)
		{
			printf("...but had too many disconnects already, so doing nothing\n");

			return;
		}

		if (!pCounter)
		{
			//we don't want to alert on a single network interruption... so we alert if we get 2*n+1 disconnects within 5 minutes,
			//where n is the expected number of child servers
			pCounter = SimpleEventCounter_Create(2 * eaSize(&sppAutoSpawningMachines) * siAutoSpawningStartupChildServersPerMachine + 1,
				300, 300);
		}

		if (SimpleEventCounter_ItHappened(pCounter, timeSecondsSince2000()))
		{
			siNoRespawnsBeforeThisTime = iCurTime + 1200;
			CRITICAL_NETOPS_ALERT("TOO_MANY_WORKER_DISCONNECTS", "Got so many worker disconnects recently that something must be wrong, won't try restarting for 20 minutes, task server is probably borked completely");
			printf("...but just now had too many disconnects, so doing nothing\n");

		}
		else
		{
			printf("...so attempting to respawn\n");

			SpawnAWorker(pIPStr);
		}
	}
}

void taskServerDisconnect(NetLink *link,TaskServerClient **client_p)
{
	TaskServerClient	*client;
	char *pDisconnectReason = NULL;

	if (!client_p)
		return;
	client = *client_p;
	if (!client)
		return;

	if (client->is_worker)
	{
		HandleWorkerDisconnect(link, client);
	}


	if (client->stats)
	{
		client->stats->bConnected = false;
		client->stats = NULL;
	}
	eaFindAndRemove(&workers,client);
	eaFindAndRemove(&free_workers,client);
	// Cancel any requests and assignments
	FOR_EACH_IN_EARRAY(requests, TaskServerRequest, req)
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
		TaskServerRequest *req = client->current_assignment->req;
		// We're working on something, it needs to be reassigned
		client->current_assignment->worker = NULL;
		eaFindAndRemoveFast(&client->current_assignment->req->assignments, client->current_assignment);
		SAFE_FREE(client->current_assignment);
		assignToWorker(req);
	}

	updateTitle();
	linkGetDisconnectReason(link, &pDisconnectReason);
	taskLogPrintf("Disconnect from %s [%p] @ %d; reason %s\n",client->name, link, _time32(NULL), pDisconnectReason);
	estrDestroy(&pDisconnectReason);

	client->link = NULL;
}

void taskServerUpdate(void)
{
	int numDispatchedResults = 0;
	int no_workers[SHADERTASKTARGETVERSION_Max];
	memset(no_workers, 0, sizeof(no_workers));
	checkUpdateTitle();
	// Check for requests to send to free workers
	if (eaSize(&free_workers)) {
		FOR_EACH_IN_EARRAY(requests, TaskServerRequest, req)
		{
			if (!eaSize(&req->assignments)) {
				int index = req->request_data->worker_version;
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


	FOR_EACH_IN_EARRAY_FORWARDS(g_taskResults, TaskServerRequest, requestResult)
	{
		if (!requestSendResultToClientCompleted(requestResult))
			break;
		numDispatchedResults = irequestResultIndex + 1;
	}
	FOR_EACH_END;
	if (numDispatchedResults)
	{
		taskLogPrintf("Removing %d completed tasks\n", numDispatchedResults);
		eaRemoveRange(&g_taskResults, 0, numDispatchedResults);
	}
}

void printStat(TaskServerStats *stats)
{
	printf("%c %15s  compiles:%7d/%4d  hit/miss: %7d/%7d (%5.1f%%) connects:%4d  %s\n", stats->bConnected?'*':'-', stats->name,
		stats->compileCount, stats->compileCountThisConnection,
		stats->cacheHitCount, stats->cacheMissCount, (stats->cacheHitCount*100.f/(stats->cacheHitCount+stats->cacheMissCount)),
		stats->connectCount, stats->currentIP);
}

AUTO_COMMAND;
void printStats(void)
{
	FOR_EACH_IN_STASHTABLE(stStats, TaskServerStats, stats)
	{
		if (stats->compileCount)
			printStat(stats);
	}
	FOR_EACH_END;
	printf("Global stats:\n");
	printStat(&global_stats);
	//printf("Cache misses: %d   Cache hits: %d (%1.1f%%)\n", shader_server_misc_stats.cacheMisses, shader_server_misc_stats.cacheHits, shader_server_misc_stats.cacheHits*100.f / (float)(shader_server_misc_stats.cacheHits + shader_server_misc_stats.cacheMisses));
	printf("Cache size: %d elems (%s)\n", stashGetCount(stTaskServerCache), friendlyBytes(cache_byte_size));
}



//tells the taskServer to spawn child task servers on other machines automatically
//pmachines is a comma-separated list of machines, presumably quoted
AUTO_COMMAND ACMD_CMDLINE;
void DoAutoSpawningStartup(int iChildServersPerMachine, char *pMachines)
{
	DivideString(pMachines, ", ", &sppAutoSpawningMachines, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE 
		| DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	assertmsgf(eaSize(&sppAutoSpawningMachines), "Didn't find any machines listed for DoAutoSpawningStartup");
	assertmsgf(iChildServersPerMachine > 0, "ChildServersPerMachine in DoAutoSpawningStartup must be > 0");

	sbDoAutoSpawningStartup = true;
	siAutoSpawningStartupChildServersPerMachine = iChildServersPerMachine;
}

static char *pFilesToSend[] = 
{
	"nvdxt.exe",
	"nvtt.dll",
	"nvtt.pdb",
	"Remesh.exe",
	"Remesh.pdb",
	SIMPLYGON_STANDARD_SDK_LICENSE_DATA_FILE,
	SIMPLYGON_STANDARD_SDK_DLL_X64,
	SIMPLYGON_STANDARD_SDK_DLL_WIN32,
	SIMPLYGON_CRYPTICNW_SDK_DLL_X64,
	SIMPLYGON_CRYPTICNW_SDK_DLL_X64_PDB,
	SIMPLYGON_CRYPTICNW_SDK_DLL_WIN32,
	SIMPLYGON_CRYPTICNW_SDK_DLL_WIN32_PDB,
	"TaskServer.exe",
	"TaskServer.pdb",
	"msvcr100.dll"
};


static void SpawnAWorker(char *pMachineName)
{
	char *pFullCmdLine = NULL;
	char exeDir[CRYPTIC_MAX_PATH];
	char *pExecutableDir = getExecutableDir(exeDir);
	const char *pLocalMachineName = getHostName();
	char **ppMachines = NULL;
	eaPush(&ppMachines, pMachineName);

	estrPrintf(&pFullCmdLine, "WORKINGDIR(%s) %s\\TaskServer.exe -serveraddr %s", pExecutableDir, pExecutableDir, pLocalMachineName);
	backSlashes(pFullCmdLine);

	SentryServerComm_RunCommand(ppMachines, pFullCmdLine);

	estrDestroy(&pFullCmdLine);
	eaDestroy(&ppMachines);






}

static void AutoSpawningStartup(void)
{
	char exeDir[CRYPTIC_MAX_PATH];
	char *pTopDir = getExecutableTopDir();
	char *pExecutableDir = getExecutableDir(exeDir);
	const char *pLocalMachineName = getHostName();
	char *pFullCmdLine = NULL;

	char **sppChildMachinesOnly = NULL;

	int i;

	backSlashes(pExecutableDir);
	
	printf("Doing auto spawning... top dir is %s, executable dir is %s\n", pTopDir, pExecutableDir);

	printf("%d machines:\n", eaSize(&sppAutoSpawningMachines));
	for (i = 0; i < eaSize(&sppAutoSpawningMachines); i++)
	{
		if (stricmp(pLocalMachineName, sppAutoSpawningMachines[i]) == 0)
		{
			printf("%s is the parent machine\n", sppAutoSpawningMachines[i]);
		}
		else
		{
			printf("Child machine: %s\n", sppAutoSpawningMachines[i]);
			eaPush(&sppChildMachinesOnly, sppAutoSpawningMachines[i]);
		}
	}




	printf("Going to kill taskServer.exe, taskServerx64.exe, and remesh.exe on all machines\n");
	KillAllEx("taskServerX64.exe", true, NULL, false, false, pTopDir);

	SentryServerComm_KillProcesses(sppChildMachinesOnly, "taskServerX64.exe", pTopDir);
	SentryServerComm_KillProcesses(sppChildMachinesOnly, "taskServer.exe", pTopDir);
	SentryServerComm_KillProcesses(sppAutoSpawningMachines, "remesh.exe", pTopDir);

	KillAllEx("taskServer.exe", true, NULL, false, false, pTopDir);

	commMonitor(comm);

	printf("Killed...\n");

	for (i = 0; i < ARRAY_SIZE(pFilesToSend); i++)
	{
		char tempName[CRYPTIC_MAX_PATH];
		sprintf(tempName, "%s\\%s", pExecutableDir, pFilesToSend[i]);
		backSlashes(tempName);

		assertmsgf(fileExists(tempName), "%s does not exist", tempName);


		printf("Sending %s to all machines\n", tempName);
		SentryServerComm_SendFile(sppChildMachinesOnly, tempName, tempName);
		commMonitor(comm);
	}

	printf("Launching %d copies of taskserver.exe on each machine\n", siAutoSpawningStartupChildServersPerMachine);

	estrPrintf(&pFullCmdLine, "WORKINGDIR(%s) %s\\TaskServer.exe -serveraddr %s -TaskServerPort %u", pExecutableDir, pExecutableDir, pLocalMachineName, giTaskServerPort);
	backSlashes(pFullCmdLine);

	for (i = 0 ; i < siAutoSpawningStartupChildServersPerMachine; i++)
	{
		SentryServerComm_RunCommand(sppAutoSpawningMachines, pFullCmdLine);
		commMonitor(comm);
	}

	eaDestroy(&sppChildMachinesOnly);

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

	taskServerSetupLog();

	timer = timerAlloc();
	cacheCleartimer = timerAlloc();

	cmdParseCommandLine(argc,argv);

	selfPatchStartup(&shaderserver_checksum);

	taskServerSetupTempDir();



	printf("%s Running.\n", getExecutableName());
	comm = commCreate(20,0);

	SentryServerComm_SetNetComm(comm);

	if (sbDoAutoSpawningStartup)
	{
		loadstart_printf("Doing auto-spawning startup...");
		AutoSpawningStartup();
		loadend_printf("done");
	}

	if (bIsServer) {
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
		listener = commListen(comm,LINKTYPE_SHARD_NONCRITICAL_100MEG, 0,giTaskServerPort,taskServerMsg,taskServerConnect,taskServerDisconnect,sizeof(TaskServerClient *));
		if (listener) {
			taskLogPrintf("Running as server on port %d.\n", giTaskServerPort);
		} else {
			taskLogPrintf("Failed to create listener. Check for a port conflict; port is %d.\n", giTaskServerPort);
			exit(-1);
		}
	} else {
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
		taskLogPrintf("Running as worker.\n");
		taskServerWorkerInit(comm);
	}


	for(;;)
	{
		F32 frameTime = timerElapsed(timer);
		commMonitor(comm);
		commMonitor(commDefault()); //used by status reporting for critical systems
		SentryServerComm_Tick();

		if (frameTime > 0.01f)
		{
			timerStart(timer);
			if (bIsServer) {
				taskServerUpdate();
				if (timerElapsed(cacheCleartimer) > 60)
				{
					timerStart(cacheCleartimer);
					clearCacheLimited(3, cacheSize);
				}
			} else {
				taskServerWorkerUpdate(comm);
			}
			utilitiesLibOncePerFrame(frameTime, 1.0f);
		}
		DoConsoleDebugMenu(GetDefaultConsoleDebugMenu());
	}

	taskServerRemoveTempDir();

	EXCEPTION_HANDLER_END
	return 0;
}
