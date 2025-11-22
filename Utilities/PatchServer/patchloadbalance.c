#include "Alerts.h"
#include "HttpClient.h"
#include "net.h"
#include "patchcommonutils.h"
#include "patchloadbalance.h"
#include "patchloadbalance_h_ast.h"
#include "patchserver.h"
#include "ReferenceSystem.h"
#include "textparser.h"
#include "timing.h"
#include "WorkerThread.h"

// HTTP information to send to a client
struct LoadBalancerRequest
{
	REF_TO(PatchClientLink) client;						// Client setting the view
	ClientHttpInfo *info;								// HTTP info
};

// Background thread commands.
enum LoadBalancerRequestThreadCmdMsg
{
	LoadBalancerRequestThreadCmd_Request = WT_CMD_USER_START,
	LoadBalancerRequestThreadMsg_Done,
};

// Load balancer request thread
static WorkerThread *request_thread = NULL;

// Load balancer request comm
static NetComm *s_comm = NULL;

// Current view status callback
static patchSendViewStatusCallback temp_SendViewStatusCallback;

// Handle a load balancer request.
static void LoadBalancerRequestThread(void *user_data, void *data, WTCmdPacket *packet)
{
	struct LoadBalancerRequest *request = *(struct LoadBalancerRequest **)data;
	char server[1024];
	U16 port;
	char prefix[1024];
	bool success;
	HttpClient *pHttpClient;
	char *http_request = NULL;
	char *result = NULL;
	int code;
	char *c;

	devassert(request->info->load_balancer);

	// Parse information.
	success = patchParseHttpInfo(request->info->info, SAFESTR(server), &port, SAFESTR(prefix));
	if (!success)
	{
		TriggerAlertDeferred("Load_Balancer_Parse_Error", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "Parse error in \"%s\"",
			request->info->info);
		goto fail;
	}

	// Connect to server.
	pHttpClient = httpClientConnect(server, port, NULL, NULL, NULL, NULL, s_comm, true, 0);
	if (!pHttpClient)
	{
		TriggerAlertDeferred("Load_Balancer_Connect_Failed", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"Failed to connect to \"%s\" to server load balancer request", request->info->info);
		goto fail;
	}

	// Create HTTP request.
	estrStackCreate(&http_request);
	estrPrintf(&http_request,
		"GET /%s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: CrypticPatchServerLB/" CRYPTIC_PATCHSERVER_VERSION_SHORT "\r\n"
		"\r\n",
		prefix, 
		server,
		port);

	// Send HTTP request.
	httpClientSendBytesRaw(pHttpClient, http_request, estrLength(&http_request));
	estrDestroy(&http_request);

	// Wait for response.
	estrStackCreate(&result);
	success = httpClientWaitForResponseText(pHttpClient, &result);
	if (!success)
	{
		TriggerAlertDeferred("Load_Balancer_Timed_Out", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"No response from \"%s\" to load balancer request", request->info->info);
		estrDestroy(&result);
		httpClientDestroy(&pHttpClient);
		goto fail;
	}

	// Make sure it was a successful response.
	code = httpClientGetResponseCode(pHttpClient);
	if (!httpResponseCodeIsSuccess(code))
	{
		TriggerAlertDeferred("Load_Balancer_Request_Error", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"Error code from \"%s\": %d", request->info->info, code);
		estrDestroy(&result);
		httpClientDestroy(&pHttpClient);
		goto fail;
	}

	// Trim off scheme.
	if (!strStartsWith(result, "http://"))
	{
		TriggerAlertDeferred("Load_Balancer_Bad_Scheme", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"Response URL from \"%s\" has incorrect scheme", request->info->info);
		estrDestroy(&result);
		httpClientDestroy(&pHttpClient);
		goto fail;
	}
	estrRemove(&result, 0, 7);

	// Remove trailing stuff.
	c = strchr(result, '\r');
	if (c)
		estrSetSize(&result, c - result);
	c = strchr(result, '\n');
	if (c)
		estrSetSize(&result, c - result);

	// Make sure something is left.
	if (!estrLength(&result))
	{
		TriggerAlertDeferred("Load_Balancer_Empty", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"Emprt URL from \"%s\"", request->info->info);
		estrDestroy(&result);
		httpClientDestroy(&pHttpClient);
		goto fail;
	}

	// Save info.
	request->info->load_balancer = false;
	free(request->info->info);
	request->info->info = strdup(result);
	estrDestroy(&result);
	httpClientDestroy(&pHttpClient);

	// Send result message back to main thread.
	wtQueueMsg(request_thread, LoadBalancerRequestThreadMsg_Done, &request, sizeof(request));
	return;

	// If anything goes wrong, just don't sent HTTP info.
fail:
	StructDestroy(parse_ClientHttpInfo, request->info);
	request->info = NULL;
	wtQueueMsg(request_thread, LoadBalancerRequestThreadMsg_Done, &request, sizeof(request));
}

// Send the results of a load balancer request.
static void LoadBalancerRequestThread_Done(void *user_data, void *data, WTCmdPacket *packet)
{
	struct LoadBalancerRequest *request = *(struct LoadBalancerRequest **)data;
	PatchClientLink *client = GET_REF(request->client);
	if (client)
		temp_SendViewStatusCallback(client, true, NULL, request->info);
	else if (request->info)
		StructDestroy(parse_ClientHttpInfo, request->info);
	REMOVE_HANDLE(request->client);
	free(request);
}

// Initialize background thread.
static void initLoadBalancerRequestThread()
{
	if (!request_thread)
	{
		request_thread = wtCreate(131072, 131072, NULL, "LoadBalancerRequestThread");
		wtRegisterCmdDispatch(request_thread, LoadBalancerRequestThreadCmd_Request, LoadBalancerRequestThread);
		wtRegisterMsgDispatch(request_thread, LoadBalancerRequestThreadMsg_Done, LoadBalancerRequestThread_Done);
		wtSetThreaded(request_thread, true, 0, false);
		wtStart(request_thread);
		s_comm = commCreate(0, 1);
	}
}

// Resolve a load balancer request.
void patchLoadBalanceRequest(PatchClientLink *client, ClientHttpInfo *http_info)
{
	struct LoadBalancerRequest *request;

	PERFINFO_AUTO_START_FUNC();

	devassert(client);
	devassert(http_info->load_balancer);

	// Create request.
	request = calloc(1, sizeof(*request));
	ADD_SIMPLE_POINTER_REFERENCE(request->client, client);
	request->info = http_info;

	// Queue request to background thread.
	initLoadBalancerRequestThread();
	wtQueueCmd(request_thread, LoadBalancerRequestThreadCmd_Request, &request, sizeof(request));

	PERFINFO_AUTO_STOP_FUNC();
}

// Fulfill any completed load balancer requests.
void patchLoadBalanceProcess(patchSendViewStatusCallback callback)
{
	PERFINFO_AUTO_START_FUNC();
	if (request_thread)
	{
		temp_SendViewStatusCallback = callback;
		wtMonitor(request_thread);
	}
	PERFINFO_AUTO_STOP();
}

#include "patchloadbalance_h_ast.c"
