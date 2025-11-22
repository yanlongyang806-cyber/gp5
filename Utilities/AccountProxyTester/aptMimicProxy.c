#include "aptMimicProxy.h"

#include "accountnet.h"
#include "aptMain.h"
#include "Money.h"
#include "net.h"
#include "structNet.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"

#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/aptMimicProxy_h_ast.h"

static NetLink *spAccountServerLink = NULL;
static bool sbReady = false;
static AccountProxyMimicTest seTest = apMimic_None;

U32 uBaseAccountID = 11028;
U32 uRepeatThreshold = 20000;
U32 uPurchaseProductID = 1300;
U32 uPurchaseProductPrice = 500;
const char *pPurchaseProductCurrency = "StarTrekChain";

#define APT_SHARD_NAME "AccountProxyTestCO"
#define APT_CLUSTER_NAME "AccountProxyTestCluster"
#define APT_ENVIRONMENT_NAME "CO.APTest"
#define APT_PRODUCT "Night"
#define APT_CATEGORY "dev"

/******************************************************************************/
/* Outgoing packet wrappers                                                   */
/******************************************************************************/

static void aptMimicProxy_SendProtocolVersion(NetLink *link)
{
	Packet *pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_PROTOCOL_VERSION);
	pktSendU32(pkt, ACCOUNT_PROXY_PROTOCOL_VERSION);
	pktSend(&pkt);
}

static void aptMimicProxy_SendIdentity(NetLink *link)
{
	AccountProxyBeginEndWalk identity = {0};
	Packet *pkt = NULL;

	estrCopy2(&identity.pProxy, APT_SHARD_NAME);
	estrCopy2(&identity.pCluster, APT_CLUSTER_NAME);
	estrCopy2(&identity.pEnvironment, APT_ENVIRONMENT_NAME);

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_BEGIN);
	ParserSendStructSafe(parse_AccountProxyBeginEndWalk, pkt, &identity);
	pktSend(&pkt);

	StructDeInit(parse_AccountProxyBeginEndWalk, &identity);
}

static void aptMimicProxy_SendRequestAccountData(NetLink *link, U32 uAccountID)
{
	AccountProxyAccountDataRequest request = {0};
	Packet *pkt = NULL;

	request.uAccountID = uAccountID;
	request.pShard = StructAllocString(APT_SHARD_NAME);
	request.pCluster = StructAllocString(APT_CLUSTER_NAME);
	request.pProduct = StructAllocString(APT_PRODUCT);
	request.pCategory = StructAllocString(APT_CATEGORY);

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_ACCOUNT_DATA);
	ParserSendStructSafe(parse_AccountProxyAccountDataRequest, pkt, &request);
	pktSend(&pkt);

	StructDeInit(parse_AccountProxyAccountDataRequest, &request);
}

static void aptMimicProxy_SendSimpleSet(NetLink *link, U32 uAccountID, const char *pKey, S64 iValue, bool bIncrement)
{
	AccountProxySimpleSetRequest request = {0};
	Packet *pkt = NULL;

	request.uAccountID = uAccountID;
	estrCopy2(&request.pProxy, APT_SHARD_NAME);
	estrCopy2(&request.pKey, pKey);
	request.operation = bIncrement ? AKV_OP_INCREMENT : AKV_OP_SET;
	request.iValue = iValue;

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_SIMPLE_SET);
	ParserSendStructSafe(parse_AccountProxySimpleSetRequest, pkt, &request);
	pktSend(&pkt);

	StructDeInit(parse_AccountProxySimpleSetRequest, &request);
}

static void aptMimicProxy_SendKeySet(NetLink *link, U32 uAccountID, const char *pKey, S64 iValue, bool bIncrement)
{
	AccountProxySetRequest request = {0};
	Packet *pkt = NULL;

	request.uAccountID = uAccountID;
	estrCopy2(&request.pProxy, APT_SHARD_NAME);
	estrCopy2(&request.pKey, pKey);
	request.operation = bIncrement ? AKV_OP_INCREMENT : AKV_OP_SET;
	request.iValue = iValue;

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_SET);
	ParserSendStructSafe(parse_AccountProxySetRequest, pkt, &request);
	pktSend(&pkt);

	StructDeInit(parse_AccountProxySetRequest, &request);
}

static void aptMimicProxy_SendKeyCommit(NetLink *link, U32 uAccountID, const char *pKey, const char *pLock)
{
	AccountProxyCommitRollbackRequest request = {0};
	Packet *pkt = NULL;

	request.uAccountID = uAccountID;
	estrCopy2(&request.pKey, pKey);
	estrCopy2(&request.pLock, pLock);

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_COMMIT);
	ParserSendStructSafe(parse_AccountProxyCommitRollbackRequest, pkt, &request);
	pktSend(&pkt);

	StructDeInit(parse_AccountProxyCommitRollbackRequest, &request);
}

static void aptMimicProxy_SendAuth(NetLink *link, U32 uAccountID, U32 uProductID, U32 uPrice, const char *pCurrency)
{
	// I'm really, really trying not to have to allocate a lot of crap here
	AuthCaptureRequest request = {0};
	TransactionItem item = {0};
	Money price = {0};
	Packet *pkt = NULL;

	request.uAccountID = uAccountID;
	request.pCurrency = strdupf("_%s", pCurrency);
	request.pProxy = StructAllocString(APT_SHARD_NAME);
	request.bAuthOnly = true;
	request.bVerifyPrice = true;

	eaPush(&request.eaItems, &item);
	item.uProductID = uProductID;
	item.pPrice = &price;
	moneyInitFromInt(&price, uPrice, pCurrency);

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_AUTHCAPTURE);
	ParserSendStructSafe(parse_AuthCaptureRequest, pkt, &request);
	pktSend(&pkt);

	moneyDeinit(&price);
	item.pPrice = NULL;
	StructDeInit(parse_TransactionItem, &item);
	eaDestroy(&request.eaItems);
	StructDeInit(parse_AuthCaptureRequest, &request);
}

static void aptMimicProxy_SendCapture(NetLink *link, U32 uAccountID, U32 uPurchaseID)
{
	CaptureRequest request = {0};
	Packet *pkt = NULL;

	request.uAccountID = uAccountID;
	request.uPurchaseID = uPurchaseID;
	request.pProxy = StructAllocString(APT_SHARD_NAME);
	request.bCapture = true;

	pkt = pktCreate(link, TO_ACCOUNTSERVER_PROXY_CAPTURE);
	ParserSendStructSafe(parse_CaptureRequest, pkt, &request);
	pktSend(&pkt);

	StructDeInit(parse_CaptureRequest, &request);
}

/******************************************************************************/
/* Mimic request wrappers                                                     */
/******************************************************************************/

static U32 aptMimicProxy_GetAccountIDForRequest(U32 uRequestID)
{
	return uBaseAccountID + (uRequestID - 1) % uRepeatThreshold;
}

static void aptMimicProxy_RequestAccountData(NetLink *link, U32 uRequestID)
{
	// For now, use a fixed account ID - change later
	aptMimicProxy_SendRequestAccountData(link, aptMimicProxy_GetAccountIDForRequest(uRequestID));
}

static void aptMimicProxy_RequestSimpleSet(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendSimpleSet(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), "CO.Test:APTestKV", uRequestID / uRepeatThreshold + 1, false);
}

static void aptMimicProxy_RequestSimpleSetScoped(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendSimpleSet(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), "$ENV:APTestKV", 2, false);
}

static void aptMimicProxy_RequestSimpleIncrement(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendSimpleSet(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), pPurchaseProductCurrency, uPurchaseProductPrice / 2, true);
}

static void aptMimicProxy_RequestKeySet(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendKeySet(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), "CO.Test:APTestKV", uRequestID / uRepeatThreshold + 1, false);
}

static void aptMimicProxy_RequestKeySetScoped(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendKeySet(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), "$ENV:APTestKV", 2, false);
}

static void aptMimicProxy_RequestKeyChange(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendKeySet(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), pPurchaseProductCurrency, uPurchaseProductPrice / 2, true);
}

static void aptMimicProxy_RequestPurchase(NetLink *link, U32 uRequestID)
{
	aptMimicProxy_SendAuth(link, aptMimicProxy_GetAccountIDForRequest(uRequestID), uPurchaseProductID, uPurchaseProductPrice, pPurchaseProductCurrency);
}

typedef void MimicProxyRequestCB(NetLink *link, U32 uRequestID);

typedef struct MimicProxyTestStatus
{
	bool bRunThisTest;
	AccountProxyMimicTest eTest;

	U32 uRequestsToSend;
	U32 uRequestDelayMs;

	S64 uTestStarted;
	U32 uRequestsSent;
	U32 uSuccesses;
	U32 uFailures;

	MimicProxyRequestCB *pCallback;
} MimicProxyTestStatus;

static MimicProxyTestStatus sTestStatus[] = {
	{false, apMimic_None, 0, 0, 0, 0, 0, 0, NULL},
	{true, apMimic_GetAccountData, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestAccountData},
	{true, apMimic_SimpleSet, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestSimpleSet},
	{true, apMimic_SimpleSetScoped, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestSimpleSetScoped},
	{true, apMimic_SimpleIncrement, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestSimpleIncrement},
	{true, apMimic_KeySet, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestKeySet},
	{true, apMimic_KeySetScoped, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestKeySetScoped},
	{true, apMimic_KeyChange, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestKeyChange},
	{true, apMimic_Purchase, 20000, 0, 0, 0, 0, 0, aptMimicProxy_RequestPurchase},
};

/******************************************************************************/
/* Incoming packet handlers                                                   */
/******************************************************************************/

static void aptMimicProxy_HandleProtocolVersion(Packet *pkt, NetLink *link)
{
	U32 uProtocolVersion = pktGetU32(pkt);
	assert(uProtocolVersion == ACCOUNT_PROXY_PROTOCOL_VERSION);
	aptMimicProxy_SendIdentity(link);
	sbReady = true;
}

static void aptMimicProxy_HandleAccountData(Packet *pkt, NetLink *link)
{
	++sTestStatus[seTest].uSuccesses;
}

static void aptMimicProxy_HandleSimpleSet(Packet *pkt, NetLink *link)
{
	++sTestStatus[seTest].uSuccesses;
}

static void aptMimicProxy_HandleKeySet(Packet *pkt, NetLink *link)
{
	// Unfortunately, for this one we have to actually read the response
	AccountProxySetResponse response = {0};

	ParserRecvStructSafe(parse_AccountProxySetResponse, pkt, &response);
	if (response.result == AKV_SUCCESS)
		aptMimicProxy_SendKeyCommit(link, response.uAccountID, response.pKey, response.pLock);
	else
		++sTestStatus[seTest].uFailures;
	StructDeInit(parse_AccountProxySetResponse, &response);
}

static void aptMimicProxy_HandleKeyCommit(Packet *pkt, NetLink *link)
{
	++sTestStatus[seTest].uSuccesses;
}

static void aptMimicProxy_HandleAuth(Packet *pkt, NetLink *link)
{
	// We also have to read the response here - sadness :(
	AuthCaptureResponse response = {0};

	ParserRecvStructSafe(parse_AuthCaptureResponse, pkt, &response);
	if (response.eResult == PURCHASE_RESULT_PENDING)
		aptMimicProxy_SendCapture(link, response.uAccountID, response.uPurchaseID);
	else
		++sTestStatus[seTest].uFailures;
	StructDeInit(parse_AuthCaptureResponse, &response);
}

static void aptMimicProxy_HandleCapture(Packet *pkt, NetLink *link)
{
	++sTestStatus[seTest].uSuccesses;
}

/******************************************************************************/
/* Link callbacks                                                             */
/******************************************************************************/

static void aptMimicProxy_PacketCB(Packet *pkt, int cmd, NetLink *link, void *data)
{
	switch (cmd)
	{
	case FROM_ACCOUNTSERVER_PROXY_PROTOCOL_VERSION:
		aptMimicProxy_HandleProtocolVersion(pkt, link);
		break;
	case FROM_ACCOUNTSERVER_PROXY_ACCOUNT_DATA:
		aptMimicProxy_HandleAccountData(pkt, link);
		break;
	case FROM_ACCOUNTSERVER_PROXY_SIMPLE_SET:
		aptMimicProxy_HandleSimpleSet(pkt, link);
		break;
	case FROM_ACCOUNTSERVER_PROXY_SET_RESULT:
		aptMimicProxy_HandleKeySet(pkt, link);
		break;
	case FROM_ACCOUNTSERVER_PROXY_ACK:
		aptMimicProxy_HandleKeyCommit(pkt, link);
		break;
	case FROM_ACCOUNTSERVER_PROXY_AUTHCAPTURE:
		aptMimicProxy_HandleAuth(pkt, link);
		break;
	case FROM_ACCOUNTSERVER_PROXY_CAPTURE:
		aptMimicProxy_HandleCapture(pkt, link);
		break;
	default:
		break;
	}
}

static void aptMimicProxy_ConnectCB(NetLink *link, void *data)
{
	linkSetKeepAliveSeconds(link, 10);
	aptMimicProxy_SendProtocolVersion(link);
}

static void aptMimicProxy_DisconnectCB(NetLink *link, void *data)
{
	printf("Disconnected from AS - exiting!\n");
	gbDone = true;
}

/******************************************************************************/
/* Main                                                                       */
/******************************************************************************/

static void aptMimicProxy_NextTest(S64 uCurTime)
{
	while (++seTest < apMimic_Count && !sTestStatus[seTest].bRunThisTest);

	if (seTest == apMimic_Count)
	{
		printf("Done with all tests!\n");
		gbDone = true;
	}
	else
	{
		sTestStatus[seTest].uTestStarted = uCurTime;
	}
}

void aptMimicProxyMain(void)
{
	S64 uLastRequestSent = 0;

	spAccountServerLink = commConnect(commDefault(), LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH, getAccountServer(),
		DEFAULT_ACCOUNTPROXYSERVER_PORT, aptMimicProxy_PacketCB, aptMimicProxy_ConnectCB, aptMimicProxy_DisconnectCB, 0);

	if (!spAccountServerLink)
	{
		printf("Couldn't create Account Server connection!\n");
		return;
	}

	while (!gbDone)
	{
		S64 curTime = timeMsecsSince2000();

		autoTimerThreadFrameBegin(__FUNCTION__);

		commMonitor(commDefault());
		utilitiesLibOncePerFrame(REAL_TIME);

		if (seTest == apMimic_None && linkConnected(spAccountServerLink))
		{
			aptMimicProxy_NextTest(curTime);
		}
		else if (sTestStatus[seTest].uRequestsSent >= sTestStatus[seTest].uRequestsToSend && (sTestStatus[seTest].uSuccesses + sTestStatus[seTest].uFailures) >= sTestStatus[seTest].uRequestsSent)
		{
			F32 fSeconds = (curTime - sTestStatus[seTest].uTestStarted) / 1000.0f;

			printf("Test %s: %d requests, %0.1f seconds, %0.1f per sec (%d failures)\n", StaticDefineInt_FastIntToString(AccountProxyMimicTestEnum, seTest), sTestStatus[seTest].uRequestsSent,
				fSeconds, sTestStatus[seTest].uRequestsSent / fSeconds, sTestStatus[seTest].uFailures);

			aptMimicProxy_NextTest(curTime);
		}
		else if (sTestStatus[seTest].uRequestsSent < sTestStatus[seTest].uRequestsToSend && curTime - uLastRequestSent >= sTestStatus[seTest].uRequestDelayMs)
		{
			uLastRequestSent = curTime;
			sTestStatus[seTest].pCallback(spAccountServerLink, ++sTestStatus[seTest].uRequestsSent);
		}

		autoTimerThreadFrameEnd();
	}
}

#include "AutoGen/aptMimicProxy_h_ast.c"