#include "ShaderServer.h"
#include "ShaderServerInterface.h"
#include "RdrShader.h"
#include "timing.h"
#include "net.h"
#include "utils.h"
#include "textparser.h"
#include "SelfPatch.h"
#include "CrypticPorts.h"

#ifndef _M_X64
#include "../../libs/XRenderLib/pub/XWrapperInterface.h"

#define COBJMACROS
#include <rpcsal.h>
#include "D3D11.h"
#include "D3DCompiler.h"
#pragma comment(lib, "../../3rdparty/directx/lib/D3DCompiler.lib")
#endif

char serveraddr[1024] = "shaderserver";
AUTO_CMD_STRING(serveraddr, serveraddr);

NetLink *server_link;

static int not_connected;
static bool bWasConnected;
char tmpFile[MAX_PATH];

void shaderServerWorkerInit(NetComm *comm)
{
	not_connected = timerAlloc();
	rdrShaderLibLoadDLLs();
}

void shaderServerWorkerConnect(NetLink *link,void *user_data)
{
	Packet	*pkt;
	assert(link == server_link);
	pkt = pktCreate(link,SHADERSERVER_WORKER_CONNECT);
	pktSendString(pkt,getHostName());
	pktSendBitsAuto(pkt, SHADERSERVER_VERSION);
	pktSendBitsAuto(pkt, shaderserver_checksum.size);
	pktSendBits(pkt, 32, shaderserver_checksum.values[0]);
	pktSendBitsAuto(pkt, 1); // Registering 1 shader compiling type
	pktSendBitsAuto(pkt, SHADERTARGETVERSION_D3DCompiler_42); // This is the kind of shaders we can build
	pktSend(&pkt);
}

void shaderServerWorkerDisconnect(NetLink *link, void *user_data)
{
	assert(server_link == link);
	server_link = NULL;
	printf("Lost connection\n");
}

void shaderServerWorkerAssignmentStart(Packet *pkt)
{
#ifdef _M_X64
	assert(0);
#else
	Packet *response_pkt;
	ShaderCompileRequestData *request;
	ShaderCompileResponseData *response;
	void *compiledResult;
	void *updbData=NULL;
	int terminator;


	request = callocStruct(ShaderCompileRequestData);
	request->request_id = pktGetBitsAuto(pkt);
	request->target = pktGetBitsAuto(pkt);
	request->programText = strdup(pktGetStringTemp(pkt));
	request->entryPointName = strdup(pktGetStringTemp(pkt));
	request->shaderModel = strdup(pktGetStringTemp(pkt));
	request->compilerFlags = pktGetBitsAuto(pkt);
	request->otherFlags = pktGetBitsAuto(pkt);
	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	// Compile data
	response = StructAlloc(parse_ShaderCompileResponseData);
	response->request_id = request->request_id;

	if (request->target == SHADERTARGET_PS3) {
		assert(0);
	} else if (request->target == SHADERTARGET_XBOX || request->target == SHADERTARGET_XBOX_UPDB) {
		XWrapperCompileShaderData xwdata = {0};
		char updbPathBuf[MAX_PATH];
		char errorBuf[4096];
		xwdata.programText = request->programText;
		xwdata.programTextLen = (int)strlen(request->programText);
		xwdata.entryPointName = request->entryPointName;
		xwdata.shaderModel = request->shaderModel;
		if (request->target == SHADERTARGET_XBOX_UPDB) {
			xwdata.updbPath = updbPathBuf;
			xwdata.updbPath_size = sizeof(updbPathBuf); 
		}
        xwdata.flags = 0;
		xwdata.compilerFlags = request->compilerFlags;
		xwdata.errorBuffer = errorBuf;
		xwdata.errorBuffer_size = sizeof(errorBuf);
        if(!XWrapperCompileShader(&xwdata)) {
            compiledResult = NULL;
		    if (xwdata.errorBuffer[0]) {
			    response->errorMessage = StructAllocString(xwdata.errorBuffer);
		    }
        } else {
		    // Fill in response
		    compiledResult = xwdata.compiledResult;
		    response->compiledResultSize = xwdata.compiledResultLen;
		    updbData = xwdata.updbData;
		    response->updbDataSize = xwdata.updbDataLen;
		    if (xwdata.errorBuffer[0]) {
			    response->errorMessage = StructAllocString(xwdata.errorBuffer);
		    }
		    if (xwdata.updbPath[0]) {
			    response->updbPath = StructAllocString(xwdata.updbPath);
		    }
        }

	} else {
		// PC compile
		if ((request->otherFlags & SHADERSERVER_FLAGS_VERSION_MASK) == SHADERTARGETVERSION_D3DCompiler_42)
		{
		LPD3D10BLOB d3d_compiledResult = NULL, d3d_errormsgs = NULL;
		updbData = NULL;
		if (FAILED(D3DCompile(request->programText, (UINT)strlen(request->programText), NULL, NULL, NULL,
			request->entryPointName, request->shaderModel, request->compilerFlags, 0, &d3d_compiledResult, &d3d_errormsgs)))
		{
			compiledResult = NULL;
			response->errorMessage = StructAllocString((char *)d3d_errormsgs->lpVtbl->GetBufferPointer(d3d_errormsgs));
		} else {
			response->compiledResultSize = d3d_compiledResult->lpVtbl->GetBufferSize(d3d_compiledResult);
			compiledResult = malloc(response->compiledResultSize);
			memcpy(compiledResult, d3d_compiledResult->lpVtbl->GetBufferPointer(d3d_compiledResult), response->compiledResultSize);
		}

			if (d3d_errormsgs)
				d3d_errormsgs->lpVtbl->Release(d3d_errormsgs);
			if (d3d_compiledResult)
				d3d_compiledResult->lpVtbl->Release(d3d_compiledResult);
		} else {
			compiledResult = NULL;
			response->errorMessage = StructAllocString("ShaderServer Worker got a compiler request for a target it does not support");
		}
	}

	response_pkt = pktCreate(server_link, SHADERSERVER_WORKER_ASSIGNMENT_DONE);
	pktSendBitsAuto(response_pkt, response->request_id);
	pktSendBitsAuto(response_pkt, response->compiledResultSize);
	pktSendBitsAuto(response_pkt, response->updbDataSize);
	pktSendString(response_pkt, response->errorMessage);
	pktSendString(response_pkt, response->updbPath);
	if (response->compiledResultSize)
		pktSendBytes(response_pkt, response->compiledResultSize, compiledResult);
	if (response->updbDataSize)
		pktSendBytes(response_pkt, response->updbDataSize, updbData);

	pktSendBits(response_pkt, 1, 0); // terminator

	pktSend(&response_pkt);

	StructDestroy(parse_ShaderCompileResponseData, response);
	StructDestroy(parse_ShaderCompileRequestData, request);
	SAFE_FREE(compiledResult);
	SAFE_FREE(updbData);
#endif
}

void shaderServerWorkerSelfPatch(Packet *pak)
{
	int data_size = pktGetBitsAuto(pak);
	void *data = malloc(data_size);
	pktGetBytes(pak, data_size, data);
	selfPatch(data, data_size); // Should not return
	assert(0);
}

void shaderServerWorkerMsg(Packet *pak, int cmd, NetLink* link, void *user_data)
{
 	switch(cmd)
 	{
 		xcase SHADERSERVER_WORKER_ASSIGNMENT_START:
 			shaderServerWorkerAssignmentStart(pak);
		xcase SHADERSERVER_SELFPATCH:
			shaderServerWorkerSelfPatch(pak);
		xdefault:
			assert(0);
 	}
}

void shaderServerWorkerUpdate(NetComm *comm)
{
	if (linkDisconnected(server_link) || (!linkConnected(server_link) && timerElapsed(not_connected) > 20.0))
	{
		linkRemove(&server_link);
		if (bWasConnected) {
			printf("Disconnected.\n");
			bWasConnected = false;
		}
	}
	if (!server_link) {
		printf("Connecting to %s...\n", serveraddr);
		bWasConnected = false;
		server_link = commConnect(comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,serveraddr,SHADERSERVER_PORT,shaderServerWorkerMsg,shaderServerWorkerConnect,shaderServerWorkerDisconnect,0);
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
			printf("Connected.\n");
		}
		timerStart(not_connected);
	} else {

	}
}
