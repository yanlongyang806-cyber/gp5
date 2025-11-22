#include "faraday_websrv.h"

#include "Alerts.h"
#include "cmdparse.h"
#include "CmdParseJson.h"
#include "HttpClient.h"
#include "StringUtil.h"
#include "url.h"
#include "textparser.h"
#include "Organization.h"
#include "net.h"
#include "httputil.h"


// WebSrv port
static int gWebSrvPort = 8000;
AUTO_CMD_INT(gWebSrvPort, WebSrvPort) ACMD_CMDLINE;


// WebSrv host
static char gWebSrvHost[256] = {0};
AUTO_CMD_STRING(gWebSrvHost, WebSrvHost) ACMD_CMDLINE;


// WebSrv timeout
static int gWebSrvTimeout = MINUTES(2);
AUTO_CMD_INT(gWebSrvTimeout, WebSrvTimeout) ACMD_CMDLINE;


// WebSrv communication enabled
static bool gWebSrvEnabled = true;
AUTO_CMD_INT(gWebSrvEnabled, WebSrvEnabled) ACMD_CMDLINE;


static void WebSrvConnected(HttpClient * pClient, void * pUserData)
{
	char * pRawRequest = NULL;
	CmdSlowReturnForServerMonitorInfo * pSlowReturnInfo = pUserData;
	char * pRawJSON = pSlowReturnInfo->pUserData;
	UrlArgumentList args = {0};

	PERFINFO_AUTO_START_FUNC();

	assert(pRawJSON);

	// Create request
	StructInit(parse_UrlArgumentList, &args);
	estrCopy2(&args.pMimeType, MIMETYPE_JSON);
	estrPrintf(&args.pBaseURL, "http://%s/rpc/", gWebSrvHost);
	urlAddValue(&args, pRawJSON, "", HTTPMETHOD_JSON);
	urlCreateHTTPRequest(&pRawRequest, ORGANIZATION_NAME_SINGLEWORD " JSONRPC", gWebSrvHost, "/rpc/", &args);
	StructDeInit(parse_UrlArgumentList, &args);
	free(pRawJSON);
	pSlowReturnInfo->pUserData = NULL;

	// Send request
	httpClientSendBytesRaw(pClient, pRawRequest, estrLength(&pRawRequest));
	estrDestroy(&pRawRequest);

	PERFINFO_AUTO_STOP_FUNC();
}


static void WebSrvTimedOut(HttpClient * pClient, void * pUserData)
{
	CmdSlowReturnForServerMonitorInfo * pSlowReturnInfo = pUserData;

	PERFINFO_AUTO_START_FUNC();

	TriggerAlert("FARADAY_WEBSRV_TIMEOUT",
		"Faraday timed out communicating with WebSrv.",
		ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0,
		GetAppGlobalType(), GetAppGlobalID(),
		GetAppGlobalType(), GetAppGlobalID(),
		getHostName(), 0);

	DoSlowCmdReturn(JSONRPCE_INTERNAL_ERROR, "Internal error (WebSrv timeout)", pSlowReturnInfo);
	SAFE_FREE(pSlowReturnInfo->pUserData);
	free(pSlowReturnInfo);
	httpClientDestroy(&pClient);

	PERFINFO_AUTO_STOP_FUNC();
}


void WebSrvResponded(HttpClient * pClient, const char * pData, int iLen, void * pUserData)
{
	CmdSlowReturnForServerMonitorInfo * pSlowReturnInfo = pUserData;

	PERFINFO_AUTO_START_FUNC();

	assert(!pSlowReturnInfo->pUserData);

	if (httpClientGetResponseCode(pClient) == HTTP_OK)
	{
		DoSlowCmdReturn(JSONRPCE_SUCCESS_RAW, pData, pSlowReturnInfo);
	}
	else
	{
		TriggerAlertf("FARADAY_WEBSRV_ERROR",
			ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0,
			GetAppGlobalType(), GetAppGlobalID(),
			GetAppGlobalType(), GetAppGlobalID(),
			getHostName(), 0,
			"WebSrv returned HTTP response code %d", httpClientGetResponseCode(pClient));

		DoSlowCmdReturn(JSONRPCE_INTERNAL_ERROR, "Internal error (WebSrv error)", pSlowReturnInfo);
	}
	free(pSlowReturnInfo);
	httpClientDestroy(&pClient);

	PERFINFO_AUTO_STOP_FUNC();
}


void FaradayMissingHandler(CmdContext * pContext, const char * pRawJSON)
{
	PERFINFO_AUTO_START_FUNC();

	if (!gWebSrvEnabled)
	{
		DoSlowCmdReturn(JSONRPCE_INTERNAL_ERROR, "Internal error (WebSrv forwarding disabled)",
			&pContext->slowReturnInfo);

		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (nullStr(gWebSrvHost))
	{
		TriggerAlert("FARADAY_WEBSRV_MISCONFIGURED",
			"Faraday is not configured to connect to WebSrv properly.",
			ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0,
			GetAppGlobalType(), GetAppGlobalID(),
			GetAppGlobalType(), GetAppGlobalID(),
			getHostName(), 0);

		DoSlowCmdReturn(JSONRPCE_INTERNAL_ERROR, "Internal error (WebSrv forwarding failed)",
			&pContext->slowReturnInfo);
	}
	else
	{
		HttpClient * pClient = NULL;
		CmdSlowReturnForServerMonitorInfo * pSlowReturnInfo = malloc(sizeof(*pSlowReturnInfo));
		*pSlowReturnInfo = pContext->slowReturnInfo;
		pSlowReturnInfo->pUserData = strdup(pRawJSON);

		pClient = httpClientConnect(gWebSrvHost, gWebSrvPort, 
			WebSrvConnected, NULL, WebSrvResponded, WebSrvTimedOut, commDefault(), false, gWebSrvTimeout);
		httpClientSetUserData(pClient, pSlowReturnInfo);
	}

	PERFINFO_AUTO_STOP_FUNC();
}