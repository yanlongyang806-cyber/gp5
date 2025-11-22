#include "net/net.h"
#include "monitor.h"
#include "sentry_comm.h"
#include "query.h"
#include "earray.h"
#include "mathutil.h"
#include "sentrycomm.h"
#include "structnet.h"
#include "autogen/sentrypub_h_ast.h"
#include "expression.h"
#include "error.h"
#include "timing.h"
#include "StringCache.h"
#include "StashTable.h"
#include "monitor_c_ast.h"
#include "sock.h"
#include "rand.h"
#include "CrypticPorts.h"

static void sendResponse(NetLink *link,SentryClient *client)
{
	Packet	*response = pktCreate(link,MONITORSERVER_MSG);
	int		ok=0;

	if (!client)
		pktSendString(response,"NotFound");
	else if (!client->link)
		pktSendString(response,"NotConnected");
	else
		pktSendString(response,"OK");
	pktSend(&response);
}

void handleKill(Packet *pak_in,NetLink *link)
{
	char			*machine,*process_name;
	SentryClient	*client;

	machine = pktGetStringTemp(pak_in);
	process_name = pktGetStringTemp(pak_in);
	client = sentrySendKill(machine,process_name);
	sendResponse(link,client);
}

void handleLaunch(Packet *pak_in,NetLink *link)
{
	char			*machine,*command;
	SentryClient	*client;

	machine = pktGetStringTemp(pak_in);
	command = pktGetStringTemp(pak_in);
	client = sentrySendLaunch(machine,command);
	sendResponse(link,client);
}

void handleLaunchAndWait(Packet *pak_in,NetLink *link)
{
	char			*machine,*command;
	SentryClient	*client;

	machine = pktGetStringTemp(pak_in);
	command = pktGetStringTemp(pak_in);
	client = sentrySendLaunchAndWait(machine,command);
	sendResponse(link,client);
}

void handleCreateFile(Packet *pak_in,NetLink *link)
{
	char			*pMachineToSendTo,*pFileNameToCreate;
	int iCompressedSize, iUncompressedSize;
	void *pBuffer;
	SentryClient	*client;


	pMachineToSendTo = pktGetStringTemp(pak_in);
	pFileNameToCreate = pktGetStringTemp(pak_in);
	iCompressedSize = pktGetBits(pak_in, 32);
	iUncompressedSize = pktGetBits(pak_in, 32);

	printf("Request to create file %s on machine %s\n", pFileNameToCreate, pMachineToSendTo);


	pBuffer = calloc(iCompressedSize, 1);

	pktGetBytes(pak_in, iCompressedSize, pBuffer);

	client = sentrySendCreateFile(pMachineToSendTo, pFileNameToCreate, iCompressedSize, iUncompressedSize, pBuffer);
	sendResponse(link,client);

	free(pBuffer);
}

void handleCreateFileMultipleMachines(Packet *pak_in)
{
	char **ppMachines = NULL;
	char **ppFileNames = NULL;
	char *pMachineName;
	int iCompressedSize, iUncompressedSize;
	void *pBuffer;
	SentryClient	*client;
	int i;

	while (1)
	{
		pMachineName = pktGetStringTemp(pak_in);
		if (pMachineName[0])
		{
			eaPush(&ppMachines, pMachineName);
			eaPush(&ppFileNames, pktGetStringTemp(pak_in));
		}
		else
		{
			break;
		}
	}


	iCompressedSize = pktGetBits(pak_in, 32);
	iUncompressedSize = pktGetBits(pak_in, 32);

	printf("Request to create files on %d machines\n", eaSize(&ppMachines));

	pBuffer = calloc(iCompressedSize, 1);
	pktGetBytes(pak_in, iCompressedSize, pBuffer);

	for (i = 0; i < eaSize(&ppMachines); i++)
	{
		printf("Sending %s to %s\n", ppFileNames[i], ppMachines[i]);
		client = sentrySendCreateFile(ppMachines[i], ppFileNames[i], iCompressedSize, iUncompressedSize, pBuffer);
	}

	eaDestroy(&ppMachines);
	eaDestroy(&ppFileNames);
	free(pBuffer);
}




static void sendRow(Packet *pak,Table **tables)
{
	int		i,j,k,colheight=1;
	Table	*table;
	Stat	*stat;
	char	val_str[100],*s;
	char	stat_str[1024];

	for(i=0;i<colheight;i++)
	{
		pktSendString(pak,"");
		for(j=0;j<eaSize(&tables);j++)
		{
			table = tables[j];
			for(k=0;k<eaSize(&table->col_names);k++)
			{
				if (i < eaSize(&table->rows))
				{
					stat = table->rows[i]->cols[k];
					if (stat)
					{
						if (stat->value)
						{
							sprintf(val_str,"%f",stat->value);
							s = val_str;
						}
						else
							s = stat->str;
						sprintf(stat_str,"%s \"%s\" %d",stat->key,s,stat->uid);
						pktSendString(pak,stat_str);
						pktSendF64(pak,stat->value);
					}
				}
			}
			colheight = MAX(colheight,eaSize(&table->rows));
		}
	}
}

void handleQuery(Packet *pak_in,NetLink *link)
{
	char		*inclusive;
	QueryState	query = {0};
	Packet		*pak;
	int			i;

	inclusive = pktGetStringTemp(pak_in);

	pak = pktCreate(link,MONITORSERVER_QUERY);
	queryInit(&query,inclusive);
	for(i=0;i<eaSize(&sentries);i++)
	{
		SentryClient	*sentry = sentries[i];

		if (!queryGatherMatches(&query,sentry->stats))
			continue;
		queryFillTableHoles(&query,sentry);
		sendRow(pak,query.tables);
	}
	pktSend(&pak);
	queryFree(&query);
}





ExprFuncTable* sSharedFuncTable = NULL;

void InitSharedFuncTable(void)
{
	if (!sSharedFuncTable)
	{
		sSharedFuncTable = exprContextCreateFunctionTable("SentryServerMonitor");
		exprContextAddFuncsToTableByTag(sSharedFuncTable, "util");
		exprContextAddFuncsToTableByTag(sSharedFuncTable, "SentryServer");
	}
}

AUTO_EXPR_FUNC(SentryServer) ACMD_NAME(secsSince2000);
U32 exprFuncSecsSince2000(void)
{
	return timeSecondsSince2000();
}

AUTO_EXPR_FUNC(SentryServer) ACMD_NAME(StatCount);
int exprFuncStatCount(ExprContext *pContext, const char *pKey, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	SentryClient *pClient = exprContextGetVarPointer(pContext, "me", parse_SentryClient);
	int i;
	ExprContext *pInnerContext;
	int iCount = 0;

	if (!pClient)
	{
		Errorf("Couldn't find \"me\" in exprFuncStatCount");
		return 0;
	}

	pInnerContext = exprContextCreate();
	exprContextSetFuncTable(pInnerContext, sSharedFuncTable);

	for (i=0; i < eaSize(&pClient->stats); i++)
	{
		if (stricmp(pClient->stats[i]->key, pKey) == 0)
		{
			MultiVal answer = {0};
			
			exprContextSetPointerVar(pInnerContext, "stat", pClient->stats[i], parse_Stat, false, true);

			exprEvaluateSubExpr(subExpr, pContext, pInnerContext, &answer, false);

			if (QuickGetInt(&answer))
			{
				iCount++;
			}

			exprContextRemoveVar(pInnerContext, "stat");
		}
	}


	exprContextDestroy(pInnerContext);

	return iCount;
}




	




void handleExpressionQuery(Packet *pInPack, NetLink *pLink)
{
	SentryClientList list = {0};
	int i;
	U32 iSearchFlags = pktGetBits(pInPack, 32);
	char *pExpressionToEvaluateStr = pktGetStringTemp(pInPack);
	Packet *pOutPack;
	Expression *pExpression = NULL;
	ExprContext *pContext = NULL;

	InitSharedFuncTable();
	pContext = exprContextCreate();
	exprContextSetFuncTable(pContext, sSharedFuncTable);


	for (i=0; i < eaSize(&sentries); i++)
	{
		if (sentries[i]->link || (iSearchFlags & EXPRESSIONQUERY_FLAG_SEARCH_DISCONNECTED_SERVERS))
		{
			MultiVal answer = {0};
			

			exprContextSetPointerVar(pContext, "me", sentries[i], parse_SentryClient, false, true);

			if (!pExpression)
			{
				pExpression = exprCreate();
				exprGenerateFromString(pExpression, pContext, pExpressionToEvaluateStr, NULL);
			}


			exprEvaluate(pExpression, pContext, &answer);
			
			if (QuickGetInt(&answer))
			{
				eaPush(&list.ppClients, sentries[i]);
			}

			exprContextRemoveVar(pContext, "me");
		}
	}

	exprContextDestroy(pContext);
	exprDestroy(pExpression);

	if (iSearchFlags & EXPRESSIONQUERY_FLAG_SEND_BACK_SAFE_RESULT)
	{
		pOutPack = pktCreate(pLink, MONITORSERVER_EXPRESSIONQUERY_RESULT_SAFE);
		
		ParserSendStructSafe(parse_SentryClientList, pOutPack, &list);

		pktSend(&pOutPack);

	}
	else
	{

		pOutPack = pktCreate(pLink, MONITORSERVER_EXPRESSIONQUERY_RESULT);
		
		ParserSend(parse_SentryClientList, pOutPack, NULL, &list, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);

		pktSend(&pOutPack);
	}

	eaDestroy(&list.ppClients);
}


void monitorMsg(Packet *pkt,int cmd,NetLink *link,void *user_data);



AUTO_STRUCT;
typedef struct CachedPacket
{
	U64 iIndex;
	int iCmd;
	Packet *pPkt; NO_AST
} CachedPacket;

AUTO_FIXUPFUNC;
TextParserResult CachedPacket_fixup(CachedPacket *pCachedPacket, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		{
			pktFree(&pCachedPacket->pPkt);
		}
	}

	return 1;
}


static bool sbRandomlyForgetPackets = false;
AUTO_CMD_INT(sbRandomlyForgetPackets, RandomlyForgetPackets);

StashTable sLinkDataForVerificationByIP;

AUTO_STRUCT;
typedef struct CachedLinkDataForVerification
{
	U32 iIP; 
	U32 iSessionKey; //something that will be different every time the shard runs, such as its
			//secondssince2000
	U64 iLastPacketIndexFullyProcessed;

	CachedPacket **ppCachedPackets;
} CachedLinkDataForVerification;

CachedPacket *FindCachedPacket(CachedLinkDataForVerification *pLinkData, U64 iIndex, bool bRemoveIfFound)
{
	int i;
	CachedPacket *pRetVal;

	for (i = 0; i < eaSize(&pLinkData->ppCachedPackets); i++)
	{
		if (pLinkData->ppCachedPackets[i]->iIndex == iIndex)
		{
			pRetVal = pLinkData->ppCachedPackets[i];
			if (bRemoveIfFound)
			{
				eaRemoveFast(&pLinkData->ppCachedPackets, i);
			}

			return pRetVal;
		}
	}


	return NULL;
}


CachedLinkDataForVerification *FindCachedLinkData(NetLink *link, U32 iSessionKey)
{
	CachedLinkDataForVerification *pRetVal;
	U32 iIP = linkGetIp(link);

	if (!sLinkDataForVerificationByIP)
	{
		sLinkDataForVerificationByIP = stashTableCreateInt(16);
	}

	if (!stashIntFindPointer(sLinkDataForVerificationByIP, iIP, &pRetVal))
	{
		pRetVal = StructCreate(parse_CachedLinkDataForVerification);
		pRetVal->iIP = iIP;
		pRetVal->iSessionKey = iSessionKey;

		stashIntAddPointer(sLinkDataForVerificationByIP, iIP, pRetVal, false);
	}
	else
	{
		if (pRetVal->iSessionKey != iSessionKey)
		{
			StructReset(parse_CachedLinkDataForVerification, pRetVal);
			pRetVal->iIP = iIP;
			pRetVal->iSessionKey = iSessionKey;
		}
	}

	return pRetVal;
}

void CachePacket(CachedLinkDataForVerification *pLinkData, Packet *pkt, U64 iPacketIndex, int iCmd)
{
	CachedPacket *pNewCachedPacket;
	
	if (FindCachedPacket(pLinkData, iPacketIndex, false))
	{
		return;
	}

	pNewCachedPacket = StructCreate(parse_CachedPacket);
	pNewCachedPacket->iCmd = iCmd;
	pNewCachedPacket->iIndex = iPacketIndex;
	
	pNewCachedPacket->pPkt = pktCreateTemp(NULL);
	pktSetHasVerify(pNewCachedPacket->pPkt, true);

	pktAppend(pNewCachedPacket->pPkt, pkt, -1);

	eaPush(&pLinkData->ppCachedPackets, pNewCachedPacket);
}


void SendConfirmation(NetLink *link, U64 iIndex)
{
	if (sbRandomlyForgetPackets && randomIntRange(0, 100) < 60)
	{
		return;
	}
	else
	{
		Packet *pOutPacket = pktCreate(link, MONITORSERVER_PACKET_VERIFIED);
		pktSendBits64(pOutPacket, 64, iIndex);
		pktSend(&pOutPacket);
	}
}

void CheckForCachedPacketsThatCanNowBeProcessed(NetLink *link, CachedLinkDataForVerification *pLinkData)
{
	while (1)
	{
		CachedPacket *pPacket = FindCachedPacket(pLinkData, pLinkData->iLastPacketIndexFullyProcessed + 1, true);
		if (!pPacket)
		{
			return;
		}

//		printf("Now sending cached packed %"FORM_LL"d\n", pPacket->iIndex);

		pLinkData->iLastPacketIndexFullyProcessed += 1;
		SendConfirmation(link, pLinkData->iLastPacketIndexFullyProcessed);
		monitorMsg(pPacket->pPkt, pPacket->iCmd, link, NULL);

		StructDestroy(parse_CachedPacket, pPacket);
	}
}

		
		



void handlePacketWVerification(Packet *pkt, NetLink *link, void *user_data)
{
	U32 iSessionKey = pktGetBits(pkt, 32);
	U64 iNewPacketIndex = pktGetBits64(pkt, 64);
	U64 iSendersHighestVerifiedIndex = pktGetBits64(pkt, 64);
	CachedLinkDataForVerification *pLinkData = FindCachedLinkData(link, iSessionKey);
	U32 iCmd = pktGetBits(pkt, 32);
	Packet *pOutPacket;
	U64 i;

	

	//if the sender's iSendersHighestVerifiedIndex is greater than ours, then use his instead,
	//as we must have crashed and restarted, or something likethat
	if (iSendersHighestVerifiedIndex > pLinkData->iLastPacketIndexFullyProcessed)
	{
//		printf("Changing iLastPacketIndexFullyProcessed to %"FORM_LL"d", iSendersHighestVerifiedIndex);
		pLinkData->iLastPacketIndexFullyProcessed = iSendersHighestVerifiedIndex;
	}

	//easiest case... new packet is one index higher than previous index
	if (iNewPacketIndex == pLinkData->iLastPacketIndexFullyProcessed + 1)
	{
//		printf("Easy case... last received packet was %"FORM_LL"d, now receiving %"FORM_LL"d\n", pLinkData->iLastPacketIndexFullyProcessed,
//			iNewPacketIndex);

		SendConfirmation(link, iNewPacketIndex);
		pLinkData->iLastPacketIndexFullyProcessed = iNewPacketIndex;
		monitorMsg(pkt, iCmd, link, user_data);

		CheckForCachedPacketsThatCanNowBeProcessed(link, pLinkData);
		return;
	}

	//index is lower than an index we've already received... send confirmation but then ignore it
	if (iNewPacketIndex <= pLinkData->iLastPacketIndexFullyProcessed)
	{
//		printf("Received %"FORM_LL"d, ignoring it because we are already past that\n", iNewPacketIndex);
		SendConfirmation(link, iNewPacketIndex);
		return;
	}

//	printf("Want to receive %"FORM_LL"d, receiving %"FORM_LL"d instead, caching\n", 
//		pLinkData->iLastPacketIndexFullyProcessed + 1, iNewPacketIndex);

	//index must be higher than we expect... so we need to cache the packet and request re-sends
	CachePacket(pLinkData, pkt, iNewPacketIndex, iCmd);
	
	pOutPacket = pktCreate(link, MONITORSERVER_REQUESTING_RESENDS);
	
	assertmsgf(iSendersHighestVerifiedIndex < iNewPacketIndex, "IP %s sending us corrupted packet indices",
		makeIpStr(pLinkData->iIP));

	for (i = iSendersHighestVerifiedIndex + 1; i < iNewPacketIndex; i++)
	{
		pktSendBits64(pOutPacket, 64, i);
	}

	pktSendBits64(pOutPacket, 64, 0);
	pktSend(&pOutPacket);


}

void handleGetFileCRC(Packet *pkt, NetLink *pLink, int iLinkID)
{
	char *pMachineName = pktGetStringTemp(pkt);
	int iRequestID = pktGetBits(pkt, 32);
	char *pFileName = pktGetStringTemp(pkt);

	if (!sentrySendGetFileCRC(pMachineName, iRequestID, pFileName, iLinkID))
	{
		Packet *pOutPacket = pktCreate(pLink, MONITORSERVER_HEREISFILECRC);
		pktSendBits(pOutPacket, 32, iRequestID);
		pktSendBits(pOutPacket, 32, 0);
		pktSend(&pOutPacket);
	}
}

void handleGetDirectoryContents(Packet *pkt, NetLink *pLink, int iLinkID)
{
	char *pMachineName = pktGetStringTemp(pkt);
	int iRequestID = pktGetBits(pkt, 32);
	char *pDirectoryName = pktGetStringTemp(pkt);

	if (!sentrySendGetDirectoryContents(pMachineName, iRequestID, pDirectoryName, iLinkID))
	{
		Packet *pOutPacket = pktCreate(pLink, MONITORSERVER_HEREAREDIRECTORYCONTENTS);
		pktSendBits(pOutPacket, 32, iRequestID);
		pktSendBits(pOutPacket, 1, 0);
		pktSend(&pOutPacket);
	}
}

void handleSimpleQueryProcessOneMachine(Packet *pkt, NetLink *link)
{
	SentryClient *pSentry;
	Packet *pOutPacket = pktCreate(link, MONITORSERVER_SIMPLEQUERY_PROCESSES_ON_ONE_MACHINE_RESPONSE);
	SentryProcess_FromSimpleQuery_List *pList = StructCreate(parse_SentryProcess_FromSimpleQuery_List);
	
	pList->pMachineName = pktMallocString(pkt);
	pList->iQueryID = pktGetBits(pkt, 32);
	pSentry = sentryFindByName(pList->pMachineName);

	if (pSentry)
	{
		int i;
		int iNumStats = eaSize(&pSentry->stats);
		SentryProcess_FromSimpleQuery *pProcess = NULL;

		pList->bSucceeded = true;

		for ( i = 0; i < iNumStats; i++)
		{
			Stat *pStat = pSentry->stats[i];

			if (stricmp(pStat->key, "Process_name") == 0)
			{
				pProcess = StructCreate(parse_SentryProcess_FromSimpleQuery);
				pProcess->pProcessName = strdup(pStat->str);
				eaPush(&pList->ppProcesses, pProcess);
			}
			else if (stricmp(pStat->key, "process_path") == 0)
			{
				if (pProcess)
				{
					pProcess->pProcessPath = strdup(pStat->str);
				}
			}
			else if (stricmp(pStat->key, "process_pid") == 0)
			{
				if (pProcess)
				{
					pProcess->iPID = atoi(pStat->str);
				}
			}
		}
	}

	ParserSendStructSafe(parse_SentryProcess_FromSimpleQuery_List, pOutPacket, pList);
	pktSend(&pOutPacket);
	StructDestroy(parse_SentryProcess_FromSimpleQuery_List, pList);
}
		

void handleSimpleQueryMachines(Packet *pkt, NetLink *link)
{
	Packet *pOutPacket = pktCreate(link, MONITORSERVER_SIMPLEQUERY_MACHINES_RESPONSE);
	SentryMachines_FromSimpleQuery *pList = StructCreate(parse_SentryMachines_FromSimpleQuery);
	pList->iQueryID = pktGetBits(pkt, 32);

	FOR_EACH_IN_EARRAY(sentries, SentryClient, pSentry)
	{
		eaPush(&pList->ppMachines, strdup(pSentry->name));
	}
	FOR_EACH_END;


	ParserSendStructSafe(parse_SentryMachines_FromSimpleQuery, pOutPacket, pList);
	pktSend(&pOutPacket);
	StructDestroy(parse_SentryMachines_FromSimpleQuery, pList);
}
		

void handleGetFileContents(Packet *pkt, NetLink *link, int iLinkID)
{
	int iRequestID = pktGetBits(pkt, 32);
	char *pMachineName = pktGetStringTemp(pkt);
	char *pFileName = pktGetStringTemp(pkt);

	if (!sentrySendGetFileContents(pMachineName, iRequestID, pFileName, iLinkID))
	{
		FileContents_FromSimpleQuery contents = {0};
		Packet *pOutPacket = pktCreate(link, MONITORSERVER_GETFILECONTENTS_RESPONSE);
		contents.iQueryID = iRequestID;
		contents.pMachineName = pMachineName;
		contents.pFileName = pFileName;
		ParserSendStructSafe(parse_FileContents_FromSimpleQuery, pOutPacket, &contents);
		pktSend(&pOutPacket);
	}
}

void monitorMsg(Packet *pkt,int cmd,NetLink *link,void *user_data)
{
	switch(cmd)
	{
		xcase MONITORCLIENT_KILL:
			handleKill(pkt,link);
		xcase MONITORCLIENT_LAUNCH:
			handleLaunch(pkt,link);
		xcase MONITORCLIENT_LAUNCH_AND_WAIT:
			handleLaunchAndWait(pkt,link);
		xcase MONITORCLIENT_QUERY:
			handleQuery(pkt,link);
		xcase MONITORCLIENT_EXPRESSIONQUERY:
			handleExpressionQuery(pkt,link);
		xcase MONITORCLIENT_CREATEFILE:
			handleCreateFile(pkt,link);
		xcase MONITORCLIENT_DEBUG_PRINT:
			printf("DebugPrint<<%s>>\n", pktGetStringTemp(pkt));


		xcase MONITORCLIENT_PACKET_W_VERIFICATION_INFO:
			if (sbRandomlyForgetPackets && randomIntRange(0, 100) < 60)
			{
				return;
			}
			handlePacketWVerification(pkt, link, user_data);
		xcase MONITORCLIENT_GETFILECRC:
			handleGetFileCRC(pkt, link, (int)((intptr_t)user_data));
		xcase MONITORCLIENT_CREATEFILE_MULTIPLEMACHINES:
			handleCreateFileMultipleMachines(pkt);
		xcase MONITORCLIENT_GETDIRECTORYCONTENTS:
			handleGetDirectoryContents(pkt, link, (int)((intptr_t)user_data));
		xcase MONITORCLIENT_SIMPLEQUERY_PROCESSES_ON_ONE_MACHINE:
			handleSimpleQueryProcessOneMachine(pkt, link);
		xcase MONITORCLIENT_SIMPLEQUERY_MACHINES:
			handleSimpleQueryMachines(pkt, link);
		xcase MONITORCLIENT_GETFILECONTENTS:
			handleGetFileContents(pkt, link, (int)((intptr_t)user_data));

	}
}

static int siNextMonitorID = 1;
static StashTable sMonitoringLinksByID = NULL;

void monitorConnect(NetLink *link,void *user_data)
{
	int iID = siNextMonitorID++;
	if (!siNextMonitorID)
	{
		siNextMonitorID++;
	}

	linkSetUserData(link, (void*)(intptr_t)iID);
	if (!sMonitoringLinksByID)
	{
		sMonitoringLinksByID = stashTableCreateInt(16);
	}

	stashIntAddPointer(sMonitoringLinksByID, iID, link, false);
}


void monitorDisconnect(NetLink *link,void *user_data)
{
	stashIntRemovePointer(sMonitoringLinksByID, (int)((intptr_t)user_data), NULL);
}

void monitorListen(NetComm *comm)
{
	commListen(comm,LINKTYPE_UNSPEC, 0,SENTRYSERVERMONITOR_PORT,monitorMsg,monitorConnect,monitorDisconnect,0);
}


NetLink *GetMonitoringLinkFromID(int iMonitorLinkID)
{
	NetLink *pLink;
	if (stashIntFindPointer(sMonitoringLinksByID, iMonitorLinkID, &pLink))
	{
		return pLink;
	}
	return NULL;
}

#include "monitor_c_ast.c"