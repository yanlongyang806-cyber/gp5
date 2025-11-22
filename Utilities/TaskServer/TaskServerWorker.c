#include "TaskServer.h"
#include "TaskServerClientInterface.h"
#include "RdrShader.h"
#include "timing.h"
#include "net.h"
#include "utils.h"
#include "file.h"
#include "textparser.h"
#include "SelfPatch.h"
#include "structNet.h"

#ifndef _M_X64
#include "../../libs/XRenderLib/pub/XWrapperInterface.h"

#define COBJMACROS
#include <rpcsal.h>
#include "D3D11.h"
#include "D3DCompiler.h"
#pragma comment(lib, "../../3rdparty/directx/lib/D3DCompiler.lib")
#endif

char serveraddr[1024] = "taskserver";
AUTO_CMD_STRING(serveraddr, serveraddr);

NetLink *server_link;

static int not_connected;
static bool bWasConnected;
char taskServerTmpFileFolder[MAX_PATH];

void taskServerSetupTempDir()
{
	char tempFullPath[MAX_PATH];
	sprintf(taskServerTmpFileFolder, "%s_PID%d/", fileTempDir(), GetCurrentProcessId());
	fileLocateWrite(taskServerTmpFileFolder, tempFullPath);
	taskLogPrintf("Creating temp folder: %s\n", taskServerTmpFileFolder);
	mkdirtree(taskServerTmpFileFolder);
}

void taskServerRemoveTempDir()
{
	taskLogPrintf("Destroying temp folder: %s\n", taskServerTmpFileFolder);
	rmdirtreeEx(taskServerTmpFileFolder, false);
}

char * taskServerGetTmpFileFolder()
{
	return taskServerTmpFileFolder;
}


void taskServerWorkerSendTaskResultBackToServer(TaskClientTaskPacket *taskRequest, void *taskResultData);

void taskServerWorkerInit(NetComm *comm)
{
	not_connected = timerAlloc();
	rdrShaderLibLoadDLLs();
}

void taskServerWorkerConnect(NetLink *link,void *user_data)
{
	DWORD workerProcessPID = GetCurrentProcessId();
	Packet	*pkt;
	assert(link == server_link);
	pkt = pktCreate(link,TASKSERVER_WORKER_CONNECT);
	pktSendString(pkt,getHostName());
	pktSendBitsAuto(pkt, TASKSERVER_VERSION);
	pktSendBitsAuto(pkt, shaderserver_checksum.size);
	pktSendBits(pkt, 32, shaderserver_checksum.values[0]);
	pktSendBitsAuto(pkt, 1); // Registering 1 shader compiling type
	pktSendBitsAuto(pkt, SHADERTASKTARGETVERSION_D3DCompiler_42); // This is the kind of shaders we can build
	pktSendBits(pkt, sizeof(workerProcessPID) * CHAR_BIT, workerProcessPID);
	pktSend(&pkt);
}

void taskServerWorkerDisconnect(NetLink *link, void *user_data)
{
	char *pDisconnectReason = NULL;
	linkGetDisconnectReason(server_link, &pDisconnectReason);
	assert(server_link == link);
	server_link = NULL;
	taskLogPrintf("Lost connection @ %u: %s \n", _time32(NULL), pDisconnectReason);
	estrDestroy(&pDisconnectReason);

	//for now, task workers will kill themselves when they lose connection
	exit(0);
}

void taskServerWorkerAssignmentStart(Packet *pkt)
{
	TaskServer_TaskDef * task_def = NULL;
	TaskClientTaskPacket *taskRequest = NULL; 
	void *taskRequestData = NULL;
	void *taskResultData = NULL;
	int terminator;
	char tempFileAttachmentFull[MAX_PATH] = "";

	taskRequest = ParserRecvStructSafe_Create(parse_TaskClientTaskPacket, pkt);

	task_def = getTaskDataType(taskRequest->task_type);

	taskRequestData = ParserRecvStructSafe_Create(task_def->type_taskRequestData, pkt);

	if (taskRequest->fileAttachment)
	{
		sprintf(tempFileAttachmentFull, "%s/%s", taskServerGetTmpFileFolder(), taskRequest->fileAttachment);

		// SIMPLYGON TODO demote this assert to return an error code, to allow graceful retry or reporting
		assert(fileExists(tempFileAttachmentFull));

		strcpy(taskRequest->fileAttachmentFull, tempFileAttachmentFull);
	}

	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	task_def->onWorkerRecvTask(taskRequest, taskRequestData, &taskResultData);

	taskServerWorkerSendTaskResultBackToServer(taskRequest, taskResultData);
	if (strcmp(tempFileAttachmentFull, ""))
		fileForceRemove(tempFileAttachmentFull);
	if (taskRequest->fileAttachment)
		fileForceRemove(taskRequest->fileAttachmentFull);

	StructDestroyVoid(parse_TaskClientTaskPacket, taskRequest);
	StructDestroyVoid(task_def->type_taskRequestData, taskRequestData);
	StructDestroyVoid(task_def->type_taskResultData, taskResultData);
}

void workerShaderCompileImpl(const TaskClientTaskPacket * taskRequest, ShaderCompileTaskRequestData *request, ShaderCompileTaskResponseData * response)
{
#ifdef _M_X64
	assert(0);
#else
	// Compile data
	if (request->target == SHADERTASKTARGET_PS3) {
		assert(0);
	} else if (request->target == SHADERTASKTARGET_XBOX || request->target == SHADERTASKTARGET_XBOX_UPDB) {
		XWrapperCompileShaderData xwdata = {0};
		char updbPathBuf[MAX_PATH];
		char errorBuf[4096];
		xwdata.programText = request->programText;
		xwdata.programTextLen = (int)strlen(request->programText);
		xwdata.entryPointName = request->entryPointName;
		xwdata.shaderModel = request->shaderModel;
		if (request->target == SHADERTASKTARGET_XBOX_UPDB) {
			xwdata.updbPath = updbPathBuf;
			xwdata.updbPath_size = sizeof(updbPathBuf); 
		}
        xwdata.flags = 0;
		xwdata.compilerFlags = request->compilerFlags;
		xwdata.errorBuffer = errorBuf;
		xwdata.errorBuffer_size = sizeof(errorBuf);
        if(!XWrapperCompileShader(&xwdata)) {
		    if (xwdata.errorBuffer[0]) {
			    response->errorMessage = StructAllocString(xwdata.errorBuffer);
		    }
        } else {
		    // Fill in response
			TextParserBinaryBlock_AssignMemory(&response->compiledResult, xwdata.compiledResult, xwdata.compiledResultLen, false);
		    TextParserBinaryBlock_AssignMemory(&response->updbData, xwdata.updbData, xwdata.updbDataLen, false);
		    if (xwdata.errorBuffer[0]) {
			    response->errorMessage = StructAllocString(xwdata.errorBuffer);
		    }
		    if (xwdata.updbPath[0]) {
			    response->updbPath = StructAllocString(xwdata.updbPath);
		    }
        }

	} else {
		// PC compile
		if ((taskRequest->worker_version) == SHADERTASKTARGETVERSION_D3DCompiler_42)
		{
			LPD3D10BLOB d3d_compiledResult = NULL, d3d_errormsgs = NULL;
			if (FAILED(D3DCompile(request->programText, (UINT)strlen(request->programText), NULL, NULL, NULL,
				request->entryPointName, request->shaderModel, request->compilerFlags, 0, &d3d_compiledResult, &d3d_errormsgs)))
			{
				response->errorMessage = StructAllocString((char *)d3d_errormsgs->lpVtbl->GetBufferPointer(d3d_errormsgs));
			} else {
				response->compiledResultSize = d3d_compiledResult->lpVtbl->GetBufferSize(d3d_compiledResult);
				TextParserBinaryBlock_AssignMemory(&response->compiledResult, 
					d3d_compiledResult->lpVtbl->GetBufferPointer(d3d_compiledResult), response->compiledResultSize, false);
			}

			if (d3d_errormsgs)
				d3d_errormsgs->lpVtbl->Release(d3d_errormsgs);
			if (d3d_compiledResult)
				d3d_compiledResult->lpVtbl->Release(d3d_compiledResult);
		} else {
			response->errorMessage = StructAllocString("TaskServer Worker got a compiler request for a target it does not support");
		}
	}
#endif
}

void taskServerWorkerSendTaskResultBackToServer(TaskClientTaskPacket *taskRequest, void *taskResultData)
{
	int taskResultSent = false;
	Packet *response_pkt;
	TaskServer_TaskDef *task_def = getTaskDataType(taskRequest->task_type);

	if (taskRequest->fileAttachment)
	{
		if (!taskServerSendFile(server_link, taskRequest->fileAttachmentFull, NULL))
			taskHeaderRemoveAttachedFile(taskRequest);
	}

	taskLogPrintf("\tWorker sending result packet.\n");
	response_pkt = pktCreate(server_link, TASKSERVER_WORKER_ASSIGNMENT_DONE);

	ParserSendStructSafe(parse_TaskClientTaskPacket, response_pkt, taskRequest);
	ParserSendStructSafe(task_def->type_taskResultData, response_pkt, taskResultData);

	pktSendBits(response_pkt, 1, 0); // terminator

	taskResultSent = pktSend(&response_pkt);
	taskLogPrintf("\tWorker result packet %s (%d).\n", taskResultSent == 1 ? "sent" : "failed", taskResultSent);
}

void taskServerWorkerSelfPatch(Packet *pak)
{
	int data_size = pktGetBitsAuto(pak);
	void *data = malloc(data_size);
	pktGetBytes(pak, data_size, data);
	selfPatch(data, data_size); // Should not return
	assert(0);
}

void taskServerWorkerMsg(Packet *pak, int cmd, NetLink* link, void *user_data)
{
	if (linkFileSendingMode_ReceiveHelper(link, cmd, pak))
		return;

 	switch(cmd)
 	{
 		xcase TASKSERVER_WORKER_ASSIGNMENT_START:
 			taskServerWorkerAssignmentStart(pak);
		xcase TASKSERVER_SELFPATCH:
			taskServerWorkerSelfPatch(pak);
		xdefault:
			assert(0);
 	}
}

static void taskWorkerHandleFileSendingErrors(char *errorMsg)
{
	taskLogPrintf("\tWorker file sending layer error: %s", errorMsg);
}

static void taskWorkerLogReceivedFile(int iCmd, char *pFileName)
{
	taskLogPrintf("\tWorker received file: %s\n", pFileName);
}

void taskServerWorkerUpdate(NetComm *comm)
{
	if (linkDisconnected(server_link) || (!linkConnected(server_link) && timerElapsed(not_connected) > 20.0))
	{
		linkRemove(&server_link);
		if (bWasConnected) {
			taskLogPrintf("Disconnected.\n");
			bWasConnected = false;
		}
	}
	if (!server_link) {
		taskLogPrintf("Connecting to %s...\n", serveraddr);
		bWasConnected = false;
		server_link = commConnect(comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,serveraddr,giTaskServerPort,taskServerWorkerMsg,taskServerWorkerConnect,taskServerWorkerDisconnect,0);
		linkFileSendingMode_InitSending(server_link);
		linkFileSendingMode_InitReceiving(server_link, taskWorkerHandleFileSendingErrors);
		linkFileSendingMode_RegisterCallback(server_link, TASKSERVER_FILE_RECEIVED, taskServerGetTmpFileFolder(), taskWorkerLogReceivedFile);

		timerStart(not_connected);
		if (server_link)
		{
			linkSetKeepAliveSeconds(server_link, PING_TIME);
			linkSetTimeout(server_link, TIMEOUT_TIME);
		}
	}
	if (linkConnected(server_link))
	{
		if (!bWasConnected) {
			bWasConnected = true;
			taskLogPrintf("Connected.\n");
		}
		timerStart(not_connected);
	} else {

	}
}
