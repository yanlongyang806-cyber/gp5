#pragma once

typedef struct Checksum Checksum;
typedef struct NetComm NetComm;

typedef struct TaskServerClient TaskServerClient;
typedef struct TaskClientTaskPacket TaskClientTaskPacket;

typedef struct ShaderCompileTaskRequestData ShaderCompileTaskRequestData;
typedef struct ShaderCompileTaskResponseData ShaderCompileTaskResponseData;

typedef struct TaskServer_TaskDef
{
	ParseTable * type_taskRequestData;
	ParseTable * type_taskResultData;
	int max_worker_version;
	bool (*onServerRecvTask)(TaskServerClient * client, TaskClientTaskPacket * taskRequest, void *taskRequestData);
	bool (*onWorkerRecvTask)(TaskClientTaskPacket * taskRequest, void *taskRequestData, void **taskResultData);
	bool (*onServerRecvCompleteTask)(TaskClientTaskPacket * taskRequest, void *taskRequestData, void *taskResultData);
} TaskServer_TaskDef;

TaskServer_TaskDef * getTaskDataType(int taskType);

void taskServerWorkerInit(NetComm *comm);
void taskServerWorkerUpdate(NetComm *comm);

void workerShaderCompileImpl(const TaskClientTaskPacket * taskRequest, ShaderCompileTaskRequestData *request, ShaderCompileTaskResponseData * response);

char * taskServerGetTmpFileFolder();
void taskServerSetupTempDir();
void taskServerRemoveTempDir();

extern Checksum shaderserver_checksum;

// In seconds:
#define PING_TIME 1
#define TIMEOUT_TIME 10