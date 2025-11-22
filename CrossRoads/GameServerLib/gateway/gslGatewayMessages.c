/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "CrypticPorts.h"
#include "EString.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "net.h"
#include "netprivate.h"
#include "ResourceInfo.h"
#include "sock.h"
#include "timing.h"
#include "Message.h"
#include "url.h"
#include "LoginCommon.h"
#include "MemoryPool.h"
#include "gslTransactions.h"
#include "gslCommandParse.h"
#include "gslCostume.h"
#include "gslAccountProxy.h"
#include "CostumeCommonEntity.h"
#include "GameAccountDataCommon.h"
#include "utilitiesLib.h" // For GetShardNameFromShardInfoString
#include "ShardCluster.h"
#include "EntitySavedData.h"
#include "structnet.h"
#include "itemCommon.h"
#include "Player.h"
#include "CostumeCommonLoad.h"
#include "Reward.h"
#include "textparserJSON.h"
#include "referencesystem.h"
#include "gslActivity.h"
#include "ActivityCommon.h"

#include "GatewayPerf.h"
#include "GatewayUtil.h"
#include "GatewayWatcher.h"

#include "../../libs/UtilitiesLib/AutoGen/loggingEnums_h_ast.h"
#include "AutoGen/TextureServer_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/HeadshotServer_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "autogen/ServerLib_autogen_remotefuncs.h"
#include "autogen/itemCommon_h_ast.h"

#include "gslGatewayMessages_c_ast.h"

#include "gslGatewayMessages.h"
#include "gslGatewayServer.h"
#include "gslGatewaySession.h"
#include "gslGatewayStructMapping.h"
#include "gslGatewayContainerMapping.h"
#include "gslGatewayMappedEntity.h"
#include "gslGatewayVendor.h"
#include "gslGatewayGame.h"

static GatewayWatcher *s_pwatcher = NULL;
static NetLink **s_ppLinks = NULL;

typedef struct GatewayLink
{
	bool bLinkIsSlave;
		// If true, the link is considered this process' slave.

	char achShard[128];
		// The name of the shard the proxy lives on

} GatewayLink;

////////////////////////////////////////////////////////////////////////////
//
// SlowRequest
//
// Holds on to state while waiting for slow commands.
//
AUTO_STRUCT;
typedef struct SlowRequest
{
	U32 id;
	NetLink *link;		NO_AST
	char *estrName;		AST(ESTRING)
	GatewaySession *psess;

	// Some handy slots for the caller to stash stuff
	int i;
	U32 u32;
	char *estr;			AST(ESTRING)

} SlowRequest;

static SlowRequest **s_eaReqs;

MP_DEFINE(SlowRequest);

static SlowRequest *CreateRequest(NetLink *link, char *pchName, GatewaySession *psess)
{
	static U32 s_id = 1;
	SlowRequest *preq;

	MP_CREATE(SlowRequest, 128);

	preq = MP_ALLOC(SlowRequest);
	preq->id = s_id;
	preq->link = link;
	preq->psess = psess;
	estrCopy2(&preq->estrName, pchName);

	eaPush(&s_eaReqs, preq);

	s_id++;
	if(s_id > 0x7ffffff)
	{
		s_id = 1;
	}

	return preq;
}

static void DestroyRequest(SlowRequest *preq)
{
	eaFindAndRemoveFast(&s_eaReqs, preq);
	MP_FREE(SlowRequest, preq);
}

static SlowRequest *GetRequestById(U32 id)
{
	EARRAY_FOREACH_BEGIN(s_eaReqs, i);
	{
		SlowRequest *preq = s_eaReqs[i];
		if(preq->id == id)
		{
			return preq;
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

static void InvalidateAllRequestsForLink(NetLink *link)
{
	EARRAY_FOREACH_REVERSE_BEGIN(s_eaReqs, i);
	{
		SlowRequest *preq = s_eaReqs[i];
		if(preq->link == link)
		{
			preq->link = NULL;
		}
	}
	EARRAY_FOREACH_END;
}

void wgsInvalidateAllRequestsForSession(GatewaySession *psess)
{
	EARRAY_FOREACH_REVERSE_BEGIN(s_eaReqs, i);
	{
		SlowRequest *preq = s_eaReqs[i];
		if(preq->psess == psess)
		{
			preq->link = NULL;
		}
	}
	EARRAY_FOREACH_END;
}

static bool RequestValid(SlowRequest *preq)
{
	if(preq)
	{
		if(preq->link)
		{
			return true;
		}

		DestroyRequest(preq);
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////
//
// Ping
//

static void HandlePing(Packet *pktIn, NetLink *link)
{
	Packet *pkt = pktCreate(link, PING_CMD);
	pktSendString(pkt, "pong!");
	pktSend(&pkt);
}


////////////////////////////////////////////////////////////////////////////
//
// Connection handlers
//

static void HandleConnectionPing(Packet *pktIn, NetLink *link)
{
	Packet *pkt;
	CONNECTION_PKTCREATE(pkt, link, "Server_ConnectionPing");
	pktSendString(pkt, "pong!");
	pktSend(&pkt);
}	

static void gslGateway_SendAllActiveEvents(NetLink *link);
	
static void HandleHello(Packet *pktIn, NetLink *link, GatewayLink *gwlink)
{
	Packet *pkt;
	pktGetString(pktIn, gwlink->achShard, sizeof(gwlink->achShard));

	if(stricmp(gwlink->achShard, GetShardNameFromShardInfoString()) == 0)
	{
		gslGatewayServer_Log(LOG_GATEWAY_SERVER, "New connection from my proxy.");
		printf("\tfrom my proxy (%s).\n", gwlink->achShard);
		gwlink->bLinkIsSlave = true;

		gateway_WatcherConnected(s_pwatcher);

		gslGateway_SendAllActiveEvents(link);
	}
	else
	{
		gslGatewayServer_Log(LOG_GATEWAY_SERVER, "New connection from a remote proxy (%s).", gwlink->achShard);
		printf("\tfrom a remote proxy (%s).\n", gwlink->achShard);
	}

	CONNECTION_PKTCREATE(pkt, link, "Server_Hello");
	pktSendString(pkt, GetShardNameFromShardInfoString());
	pktSend(&pkt);

	gslGatewayServer_ShardClusterOverviewChanged(GetShardClusterOverview_EvenIfNotInCluster());

}
	
static void HandleLog(Packet *pktIn, NetLink *link)
{
	char *pch;
	enumLogCategory eCat;

	pch = pktGetStringTemp(pktIn);
	eCat = StaticDefineIntGetIntDefault(enumLogCategoryEnum, pch, LOG_GATEWAY_SERVER);
	pch = pktGetStringTemp(pktIn);
	gslGatewayServer_Log(eCat, "%s", pch);

}

static void HandlePerf(Packet *pktIn, NetLink *link)
{
	char *pch = pktGetStringTemp(pktIn);

	gateperf_SetCurFromString(pch);
}

static void HandleShutdownMessage(Packet *pktIn, NetLink *link)
{
	if(s_pwatcher)
	{
		char *pch = pktGetStringTemp(pktIn);
		gateway_SetShutdownMessage(s_pwatcher, pch);
	}
}


static void HandleRequestCreateSession(Packet *pktIn, NetLink *link)
{
	U32 magic = pktGetU32(pktIn);
	U32 idAccount = pktGetU32(pktIn);
	U32 lang = pktGetU32(pktIn);

	wgsCreateSession(magic, idAccount, lang, link);

	// When the session is ready (i.e. the GameAccountData has been fetched)
	//   a CreateSession will be returned.
}

static void HandleRequestDestroySession(Packet *pktIn, NetLink *link)
{
	U32 idxMagic = pktGetU32(pktIn);
	U32 idxServer = pktGetU32(pktIn);

	wgsDestroySessionForIndex(idxServer, idxMagic, false);
}

static void HandleRequestResource(Packet *pktIn, NetLink *link)
{
	Packet *pkt;
	char achDict[512];
	char achRef[512];
	char *pos;
	char *estr = NULL;
	U32 lang = 1; /* English, by default */

	pktGetString(pktIn, SAFESTR(achDict));
	pktGetString(pktIn, SAFESTR(achRef));

	// If the ref has a / in it, then it is followed by the Language to use
	//   during the mapping.
	if((pos = strchr(achRef, '/')) != NULL)
	{
		// Get the lang and trim the info
		lang = atoi(pos+1);
		*pos = '\0';
	}

	WriteMappedStructJSON(&estr, achDict, achRef, lang);

	if(pos)
	{
		// Put the lang info back if it was there.
		*pos = '/';
	}

	CONNECTION_PKTCREATE(pkt, link, "Server_Resource");
	pktSendString(pkt, achDict);
	pktSendString(pkt, achRef);
	if(estr)
	{
		pktSendString(pkt, estr);
		estrDestroy(&estr);
	}
	else
	{
		pktSendString(pkt, "undefined");
	}
	pktSend(&pkt);
}

static void HandleRequestDefaultResource(Packet *pktIn, NetLink *link)
{
	Packet *pkt;
	char pchDict[1024];
	char *estr = NULL;

	pktGetString(pktIn, SAFESTR(pchDict));

	WriteEmptyMappedStructJSON(&estr, pchDict);

	CONNECTION_PKTCREATE(pkt, link, "Server_DefaultResource");
	pktSendString(pkt, pchDict);
	if(estr)
	{
		pktSendString(pkt, estr);
		estrDestroy(&estr);
	}
	else
	{
		pktSendString(pkt, "undefined");
	}
	pktSend(&pkt);
}

static void HandleRequestDictionaryKeys(Packet *pktIn, NetLink *link)
{
	Packet *pkt;
	char pchDict[1024];
	ResourceIterator iterator;
	char *estr = NULL;
	char *pchName;
	bool bGotOne = false;

	pktGetString(pktIn, SAFESTR(pchDict));

	CONNECTION_PKTCREATE(pkt, link, "Server_DictionaryKeys");

	if(resInitIterator(pchDict, &iterator))
	{
		estrCopy2(&estr, "{\n");
		estrConcatf(&estr, "\"%s\":\n[\n", pchDict);

		while (resIteratorGetNext(&iterator, &pchName, NULL))
		{
			if(bGotOne)
			{
				estrAppend2(&estr, ",\n");
			}
			estrConcatf(&estr, "\t\"%s\"", pchName);
			bGotOne = true;
		}
		resFreeIterator(&iterator);
		estrAppend2(&estr, "\n]}\n");

		pktSendString(pkt, pchDict);
		pktSendString(pkt, estr);
		estrDestroy(&estr);
	}
	else
	{
		pktSendString(pkt, pchDict);
		pktSendString(pkt, "undefined");
	}

	pktSend(&pkt);
}

////////////////////////////////////////////////////////////////////////////
//
// Texture handler and helpers
//

static void HandleRequestTexture(Packet *pktIn, NetLink *link)
{
	char *pch = pktGetStringTemp(pktIn);
	SlowRequest *preq = CreateRequest(link, pch, NULL);

	RemoteCommand_GetTexture(GLOBALTYPE_TEXTURESERVER, 0, pch, GetAppGlobalType(), GetAppGlobalID(), preq->id);
}

void OVERRIDE_LATELINK_TextureServerReturn(int iRequestID, TextParserBinaryBlock *pTexture, char *pErrorString)
{
	SlowRequest *preq= GetRequestById(iRequestID);

	if(RequestValid(preq))
	{
		Packet *pkt;

		CONNECTION_PKTCREATE(pkt, preq->link, "Server_Texture");

		pktSendString(pkt, preq->estrName);

		if(pErrorString && pErrorString[0])
		{
			pktSendU32(pkt, 0); // 0 means error
			pktSendString(pkt, pErrorString);
		}
		else
		{
			void *pv;
			int iSize;

			pv = TextParserBinaryBlock_PutIntoMallocedBuffer(pTexture, &iSize);

			pktSendU32(pkt, iSize);
			pktSendBytes(pkt, iSize, pv);

			free(pv);
		}

		pktSend(&pkt);
		DestroyRequest(preq);
	}
}

////////////////////////////////////////////////////////////////////////////

static void HandleRequestTranslations(Packet *pktIn, NetLink *link)
{
	Packet *pkt;
	U32 i;

	Language lang = (Language)pktGetU32(pktIn);
	U32 count = pktGetU32(pktIn);

	CONNECTION_PKTCREATE(pkt, link, "Server_Translations");
	pktSendU32(pkt, lang);
	pktSendU32(pkt, count);

	for(i = 0; i < count; i++)
	{
		char * pchKey = pktGetStringTemp(pktIn);

		pktSendString(pkt, pchKey);
		pktSendString(pkt, langTranslateMessageKey(lang, pchKey));
	}
	pktSend(&pkt);
}

////////////////////////////////////////////////////////////////////////////

static void GetPlayerIDFromNameWithRestore_CB(TransactionReturnVal *returnVal, SlowRequest *preq)
{
	Packet *pkt;
	ContainerID returnID = 0;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &returnID);

	if(RequestValid(preq))
	{
		CONNECTION_PKTCREATE(pkt, preq->link, "Server_Onlined");

		pktSendString(pkt, preq->estrName);
		if(eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID)
		{
			pktSendU32(pkt, returnID);
		}
		else
		{
			pktSendU32(pkt, 0);
		}
		pktSend(&pkt);

		DestroyRequest(preq);
	}
}

static void HandleOnlineEntityByName(Packet *pktIn, NetLink *link)
{
	char *pchTmp = pktGetStringTemp(pktIn);

	SlowRequest *preq = CreateRequest(link, pchTmp, NULL);
	gslGetPlayerIDFromNameWithRestore(pchTmp, 0, GetPlayerIDFromNameWithRestore_CB, preq);
}

////////////////////////////////////////////////////////////////////////////

static void Request_aslLogin2_GetCharacterChoicesCmd_CB(TransactionReturnVal *returnStruct, SlowRequest *preq)
{
	extern ParseTable parse_Login2CharacterChoices[];
	Login2CharacterChoices *pList;

	if(RequestValid(preq))
	{
		if(RemoteCommandCheck_aslLogin2_GetCharacterChoicesCmd(returnStruct, &pList) == TRANSACTION_OUTCOME_SUCCESS)
		{
			char *estr = NULL;
			Packet *pkt;

			CONNECTION_PKTCREATE(pkt, preq->link, "Server_CharactersForAccountName");

			pktSendString(pkt, preq->estrName);
			ParserWriteJSON(&estr, parse_Login2CharacterChoices, pList, 0, 0, 0);
			pktSendString(pkt, estr);
			pktSend(&pkt);

			StructDestroy(parse_Login2CharacterChoices, pList);
			estrDestroy(&estr);
		}

		DestroyRequest(preq);
	}
}

void GetAccountIDFromDisplayName_CB(TransactionReturnVal *returnVal, SlowRequest *preq)
{
	if(RequestValid(preq))
	{
		U32 accountID;

		RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(returnVal, &accountID);
		if(accountID)
		{
			RemoteCommand_aslLogin2_GetCharacterChoicesCmd(
				objCreateManagedReturnVal(Request_aslLogin2_GetCharacterChoicesCmd_CB, preq),
				GLOBALTYPE_LOGINSERVER, SPECIAL_CONTAINERID_RANDOM, accountID, false);
		}
	}
}

static void HandleRequestCharactersForAccountName(Packet *pktIn, NetLink *link)
{
	char *pch = pktGetStringTemp(pktIn);
	SlowRequest *preq = CreateRequest(link, pch, NULL);

	RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
		objCreateManagedReturnVal(GetAccountIDFromDisplayName_CB, preq),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pch);
}

////////////////////////////////////////////////////////////////////////////

void GetGuildIdForName_CB(TransactionReturnVal *returnVal, SlowRequest *preq)
{
	if(RequestValid(preq))
	{
		U32 containerID;
		Packet *pkt;

		RemoteCommandCheck_aslGuild_GetGuildIdForName(returnVal, &containerID);

		CONNECTION_PKTCREATE(pkt, preq->link, "Server_GuildIdForName");
		pktSendString(pkt, preq->estrName);
		pktSendU32(pkt, containerID);
		pktSend(&pkt);

		DestroyRequest(preq);
	}
}

static void HandleRequestGuildIdForName(Packet *pktIn, NetLink *link)
{
	char *pch = pktGetStringTemp(pktIn);
	SlowRequest *preq = CreateRequest(link, pch, NULL);

	RemoteCommand_aslGuild_GetGuildIdForName(
		objCreateManagedReturnVal(GetGuildIdForName_CB, preq),
		GLOBALTYPE_GUILDSERVER, 0, pch, 0);
}

////////////////////////////////////////////////////////////////////////////
//
// Sessions
//

static void HandleSessionPing(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Packet *pkt;
	SESSION_PKTCREATE(pkt, psess, "Server_Ping");

	pktSendString(pkt, "pong!");

	pktSend(&pkt);
}

static void HandleSessionGetContainer(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	char *pchType = pktGetStringTemp(pktIn);
	ContainerMapping *pmapping = FindContainerMappingForName(pchType);

	if(pmapping)
	{
		ContainerTracker *ptracker;
		void *pvParams = NULL;
		char achID[1024];
		pktGetString(pktIn, achID, 1024);

		if(pmapping->tpiParams)
		{
			pvParams = StructCreateVoid(pmapping->tpiParams);
			ParserReceiveStructAsCheckedNameValuePairs(pktIn, pmapping->tpiParams, pvParams);
		}

		ptracker = session_GetContainer(psess, pmapping, achID, pvParams);
		if(ptracker)
		{
			// If the client is specifically requesting it, it must not have
			//   the container at all. Make sure to send a full update as a
			//   starting point.
			verbose_printf("Forced full update for container %s:%s to @%d for %d\n",
				ptracker->pMapping->pchName, ptracker->estrID, psess->uiIdxServer, psess->uiMagic);
			
			ptracker->bFullUpdate = true;
		}

		if(pvParams)
		{
			StructDestroyVoid(pmapping->tpiParams, pvParams);
		}
	}
}

static void HandleSessionReleaseContainer(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	char *pchType = pktGetStringTemp(pktIn);
	ContainerMapping *pmapping = FindContainerMappingForName(pchType);

	if(pmapping)
	{
		char *pchTmp = pktGetStringTemp(pktIn);
		session_ReleaseContainer(psess, pmapping->gatewaytype, pchTmp);
	}
}

static void HandleSessionUpdateContainer(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	char *pchType = pktGetStringTemp(pktIn);
	ContainerMapping *pmapping = FindContainerMappingForName(pchType);

	if(pmapping && pmapping->pfnCheckModified)
	{
		char *pchTmp = pktGetStringTemp(pktIn);
		ContainerTracker *ptracker = session_FindContainerTracker(psess, pmapping->gatewaytype, pchTmp);
		if(ptracker)
		{
			pmapping->pfnCheckModified(psess, ptracker);
		}
	}
}

static void HandleSessionSendCommand(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	CmdParseStructList structList = {0};
	bool bUnknownCommand = false;
	
	char *pString = pktGetStringTemp(pktIn);
	Entity *pent = session_GetLoginEntity(psess);
	if(pent)
		GameServerParseGateway(pString, CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL | CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED,
			pent, -1, &bUnknownCommand, CMD_CONTEXT_HOWCALLED_CLIENTWRAPPER, NULL);
	else
		Errorf("POSSIBLE ASSERT: Gateway Server recieved a server command without an entity owned by the session!");

	if(bUnknownCommand)
	{
		Errorf("POSSIBLE ASSERT: Server received unrecognized private command %s. You probably don't have the required accesslevel.", pString);
	}
}


void DEFAULT_LATELINK_GetItemInfoComparedSMF(char **pestrResult,
	Language lang,
	SA_PARAM_OP_VALID Item *pItem,
	SA_PARAM_OP_VALID Entity *pEnt,
	S32 eActiveGemSlotType)
{
	printf("Missing implementation for GetItemInfoComparedJSON\n");
}

AUTO_STRUCT;
typedef struct GatewayTooltip
{
	char *pchTip; AST(NAME(Tip))
} GatewayTooltip;

#include "inventoryCommon.h"
#include "strings_opt.h"
static void HandleSessionRequestItemTooltip(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Packet *pkt;
	char achID[1024];
	char *estrTip = NULL;
	Item *pitem = NULL;
	Entity *pent;
	ContainerTracker *ptracker;

	pktGetString(pktIn, SAFESTR(achID));

	ptracker = session_FindFirstContainerTrackerForType(psess, GLOBALTYPE_ENTITYPLAYER);
	if(ptracker)
	{
		pent = session_GetEntityOfflineCopy(psess,ptracker);
		
		if(pent)
		{
			U64 item_id = atoui64(achID);

			if(item_id)
			{
				BagIterator *pIter = bagiterator_Create();
				if(inv_trh_FindItemByIDEx(ATR_EMPTY_ARGS, (NOCONST(Entity) *)pent, pIter, item_id, true, true))
				{
					pitem = (Item *)bagiterator_GetItem(pIter);
				}
				bagiterator_Destroy(pIter);
			}
			else
			{
				ItemDef *pDef = item_DefFromName(achID);

				if(pDef)
				{
					pitem = item_FromDefName(pDef->pchName);
				}
			}

			if(pitem)
			{
				GetItemInfoComparedSMF(&estrTip,
					psess->lang,
					pitem,
					pent,
					0 // S32 eActiveGemSlotType
				);
				if(item_id == 0)
				{
					StructDestroy(parse_Item,pitem);
				}
			}
		}
	}

	SESSION_PKTCREATE(pkt, psess, "Server_ItemTooltip");
	pktSendString(pkt, achID);
	pktSendBits(pkt, 8, 1);
	if(estrTip)
	{
		char *estr = NULL;
		GatewayTooltip tip = { estrTip };

		ParserWriteJSON(&estr, parse_GatewayTooltip, &tip,
			0, 0, TOK_SERVER_ONLY|TOK_EDIT_ONLY|TOK_NO_NETSEND);

		pktSendString(pkt, estr);
		estrDestroy(&estr);
		estrDestroy(&estrTip);
	}
	else
	{
		pktSendString(pkt, "undefined");
	}
	pktSend(&pkt);
}

////////////////////////////////////////////////////////////////////////////
// Gateway Game handler and helpers
//
static void HandleSaveGatewayGameData(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	char *pString = pktGetStringTemp(pktIn);
	Entity *pent = session_GetLoginEntity(psess);

	if(pent)
	{
		gslGatewayGame_SaveState(pent->myContainerID, pString);
	}

}

static void HandleGatewayGameQueueReward(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Entity *pent = session_GetLoginEntity(psess);

	if(pent)
	{
		char *pchRewardTable = pktGetStringTemp(pktIn);
		U32 iTier = pktGetU32(pktIn);

		gslGatewayGame_QueueRewardTable(pent, pchRewardTable, iTier);
	}
}

static void HandleGatewayGameGrantReward(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Entity *pent = session_GetLoginEntity(psess);

	if(pent)
	{
		char *pchRewardTable = pktGetStringTemp(pktIn);
		U64 *piPetIDs = NULL;
		U32 iTier = pktGetU32(pktIn);
		U32 iPets = pktGetU32(pktIn);
		U32 i;

		for(i=0;i<iPets;i++)
		{
			char achID[1024];
			pktGetString(pktIn,SAFESTR(achID));

			ea64Push(&piPetIDs,atoui64(achID));
		}

		gslGatewayGame_GrantRewardTable(pent, pchRewardTable, iTier, piPetIDs);
	}
}

static void HandleGatewayGameClaimRewards(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Entity *pent = session_GetLoginEntity(psess);

	if(pent)
	{
		U32 iPets = pktGetU32(pktIn);
		U64 *piPetIDs = NULL;
		
		U32 i;

		for(i=0;i<iPets;i++)
		{
			char achID[1024];
			pktGetString(pktIn,SAFESTR(achID));

			ea64Push(&piPetIDs,atoui64(achID));
		}

		gslGatewayGame_ClaimRewards(pent,piPetIDs);
	}
}

static void HandleGatewayGameDiscardRewards(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Entity *pent = session_GetLoginEntity(psess);

	if(pent)
	{
		gslGatewayGame_DiscardRewards(pent);
	}
} 

AUTO_STRUCT;
typedef struct GatewayRewardRequestItem
{
	REF_TO(ItemDef) hItemDef;			AST(NAME(ItemDef))
		U32 iCount;							AST(NAME(count))
}GatewayRewardRequestItem;

AUTO_STRUCT;
typedef struct GatewayRewardRequest
{
	EARRAY_OF(GatewayRewardRequestItem) ppItems;	AST(NAME(Item))
}GatewayRewardRequest;

static void HandleRewardRequest(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	Entity *pent = session_GetLoginEntity(psess);
	InvRewardRequest *request = StructCreate(parse_InvRewardRequest);
	Packet *pkt;

	if(pent)
	{
		char *pchID = pktGetStringTemp(pktIn);
		char *pchRewardTable = pktGetStringTemp(pktIn);
		U32 uTier = pktGetU32(pktIn);
		char *estrJSON = NULL;
		RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pchRewardTable);

		GatewayRewardRequest *pRequest = StructCreate(parse_GatewayRewardRequest);
		InventoryBagGroup rewardBags = {0};

		if (pTable && reward_PowerExec_GenerateBags(pent, pTable, entity_CalculateExpLevelSlow(pent), 0, uTier, NULL, &rewardBags.eaBags))
		{
			int iBag,i;

			for(iBag=0;iBag<eaSize(&rewardBags.eaBags);iBag++)
			{
				for(i=0;i<eaSize(&rewardBags.eaBags[iBag]->ppIndexedInventorySlots);i++)
				{
					Item *pItem = rewardBags.eaBags[iBag]->ppIndexedInventorySlots[i]->pItem;
					
					if(pItem)
					{
						GatewayRewardRequestItem *pRewardItem = StructCreate(parse_GatewayRewardRequestItem);

						COPY_HANDLE(pRewardItem->hItemDef,pItem->hItem);
						pRewardItem->iCount = pItem->count;

						eaPush(&pRequest->ppItems,pRewardItem);
					}
				}
			}
		}

		SESSION_PKTCREATE(pkt, psess, "Server_RewardRequest");
		pktSendString(pkt,pchID);

		ParserWriteJSON(&estrJSON,parse_GatewayRewardRequest,pRequest,WRITEJSON_DONT_WRITE_EMPTY_OR_DEFAULT_FIELDS,0,0);
		pktSendString(pkt,estrJSON);
		pktSend(&pkt);

		estrDestroy(&estrJSON);
	}
}

////////////////////////////////////////////////////////////////////////////
//
// Headshot handler and helpers
//
static void SendHeadshotFail(GatewaySession *psess, char *pchId, char *pchReason)
{
	Packet *pkt;

	SESSION_PKTCREATE(pkt, psess, "Server_Headshot");
	pktSendString(pkt, pchId);
	pktSendU32(pkt, 0); // 0 means error
	pktSendString(pkt, pchReason);
	pktSend(&pkt);
}

static void GetHeadshotFailed_CB(SlowRequest *preq, void *pUserData2)
{
	if(RequestValid(preq))
	{
		SendHeadshotFail(preq->psess, preq->estrName, "No headshot server.");
		DestroyRequest(preq);
	}
}

static void HandleSessionRequestHeadshot(Packet *pktIn, NetLink *link, GatewaySession *psess)
{
	SlowRequest *preq;
	char pchUrl[2048];
	char achID[24];
	UrlArgumentList *pArgList;
	U32 iCostumeIndex = 0;
	PlayerCostume *pcostume = NULL;
	bool bDelete = true;

	pktGetString(pktIn, SAFESTR(pchUrl));
	iCostumeIndex = pktGetU32(pktIn);
	pArgList = urlToUrlArgumentList(pchUrl);

	{
		char *pchCur;
		ContainerID id;
		GlobalType type = GLOBALTYPE_ENTITYPLAYER;
		ContainerTracker *ptracker;

		//pFileName should be "17.jpg" to get the headshot for player 17, or "234_ENTITYSAVEDPET.jpg" to get the 
		//headshot for pet 234, or "costumeName_PLAYERCOSTUME.jpg" to get a specific costume def

		pchCur = strrchr(pArgList->pBaseURL,'_');

		if(pchCur)
		{
			char *pchDot = strchr(pchCur, '.');

			if(pchDot)
			{
				pchCur++;
				*pchDot = '\0';
				type = NameToGlobalType(pchCur);
				*pchDot = '.';
			}
		}

		if(type == GLOBALTYPE_ENTITYCRITTER)
		{
			pchCur = strrchr(pArgList->pBaseURL,'_');

			*pchCur = '\0';

			pcostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict,pArgList->pBaseURL);

			*pchCur = '_';
			bDelete = false;
			
		}
		else
		{
			// Figure out the container id and container type
			id = strtoul(pArgList->pBaseURL, NULL, 10);
			itoa(id, achID, 10);

			ptracker = session_FindContainerTracker(psess, type, achID);
			if(!ptracker || !GET_REF(*ptracker->phRef))
			{
				SendHeadshotFail(psess, pchUrl, "Container not available.");
				return;
			}
			else
			{
				if(GlobalTypeParent(ptracker->pMapping->globaltype) == GLOBALTYPE_ENTITY)
				{
					Entity *pent = GET_REF(ptracker->hEntity);

					if(!pent)
					{
						SendHeadshotFail(psess, pchUrl, "Unable to get entity.");
					}
					else
					{
						costumeEntity_ResetStoredCostume(pent);
						pent->pSaved->iCostumeSetIndexToShow = iCostumeIndex;
						pcostume = (PlayerCostume*)costumeEntity_CreateCostumeWithItemsAndPowers(1, pent, NULL, session_GetCachedGameAccountDataExtract(psess));
						if (!pcostume)
						{
							pcostume = costumeEntity_GetActiveSavedCostume(pent);
							bDelete = false;
						}
					}
				}
				else
				{
					// What to do about other container types?
					SendHeadshotFail(psess, pchUrl, "Not an entity.");
					return;
				}
			}	
		}
	}

	//After all that, if we have a player costume to send, let's get the headshot. Otherwise, send a fail message
	if (pcostume)
	{
		char *estrCostume = NULL;
		preq = CreateRequest(link, pchUrl, psess);
		estrStackCreate(&estrCostume);
		ParserWriteText(&estrCostume, parse_PlayerCostume, pcostume, 0, 0, 0);

		RemoteCommand_GetHeadshot(
			GLOBALTYPE_HEADSHOTSERVER, 0,
			pArgList->pBaseURL, pArgList, estrCostume,
			GetAppGlobalType(), GetAppGlobalID(),
			preq->id, GetHeadshotFailed_CB, preq, NULL);
		estrDestroy(&estrCostume);
		if (bDelete)
			StructDestroy(parse_PlayerCostume, pcostume);
	}
	else
	{
		SendHeadshotFail(psess, pchUrl, "Cannot find entity costume.");
	}
}

void OVERRIDE_LATELINK_GetHeadShot_ReturnInternal(TextParserBinaryBlock *pTexture, char *pErrorString, U32 iRequestID)
{
	SlowRequest *preq= GetRequestById(iRequestID);

	if(RequestValid(preq))
	{
		Packet *pkt;

		SESSION_PKTCREATE(pkt, preq->psess, "Server_Headshot");

		pktSendString(pkt, preq->estrName);

		if(pErrorString && pErrorString[0])
		{
			pktSendU32(pkt, 0); // 0 means error
			pktSendString(pkt, pErrorString);
		}
		else
		{
			void *pv;
			int iSize;

			pv = TextParserBinaryBlock_PutIntoMallocedBuffer(pTexture, &iSize);

			pktSendU32(pkt, iSize);
			pktSendBytes(pkt, iSize, pv);

			free(pv);
		}
		pktSend(&pkt);

		DestroyRequest(preq);
	}
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


#define MTPURCHASE "MTPurchase"

AUTO_COMMAND ACMD_NAME(GatewayCStore_Purchase) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayCStore_Purchase(Entity *pent, U32 uProductID, int iExpectedPrice)
{
	ContainerID accountID = SAFE_MEMBER2(pent, pPlayer, accountID);
	GatewaySession *psess = wgsFindSessionForAccountId(accountID);
	if(!psess)
		return;

	if(pent && psess && uProductID <= INT_MAX && iExpectedPrice >= 0)
	{
		int reqID = gslAP_InitiatePurchase(pent->myContainerID, accountID, uProductID, iExpectedPrice);
		SlowRequest *preq = CreateRequest(psess->link, MTPURCHASE, psess);
		preq->u32 = uProductID;
		preq->i = reqID;
		estrPrintf(&preq->estr, "containerid %d, account %d, productid %d, price %d", pent->myContainerID, psess->idAccount, uProductID, iExpectedPrice);
	}
}


void wgsMicrotransactionUpdate(void)
{
	extern MicroTransactionProduct *FindProductForProductID(U32 id); // from gslGatewayCStore.c

	EARRAY_FOREACH_BEGIN(s_eaReqs, i);
	{
		SlowRequest *preq = s_eaReqs[i];
		if(RequestValid(preq) && stricmp(MTPURCHASE, preq->estrName) == 0)
		{
			Packet *pkt;
			char *pchError = NULL;
			MicroPurchaseErrorType eError = kMicroPurchaseErrorType_Unknown;
			MicroTransactionProduct *pProd;

			PurchaseRequestStatus eStatus = gslAP_GetPurchaseStatus(preq->psess->idAccount, preq->i, &pchError, &eError);

			pProd = FindProductForProductID(preq->u32);

			switch (eStatus)
			{
				case PURCHASE_SUCCESS:
					gslGatewayServer_Log(LOG_GATEWAY_PROXY, "LOG: Microtrans Success: req %d, %s, %s", preq->i, preq->estr, pProd && pProd->pDef ? pProd->pDef->pchName : "Unknown");

					SESSION_PKTCREATE(pkt, preq->psess, "Server_PurchaseMicrotransaction");
					pktSendU32(pkt, 1);
					pktSendU32(pkt, preq->u32); // product id
					pktSendString(pkt, ""); // error
					pktSend(&pkt);

					DestroyRequest(preq);
					break;

				case PURCHASE_FAILED:
					gslGatewayServer_Log(LOG_GATEWAY_PROXY, "ERROR: Microtrans Failure: req %d, %s, %s, error %s", preq->i, preq->estr, pProd && pProd->pDef ? pProd->pDef->pchName : "Unknown", pchError);

					SESSION_PKTCREATE(pkt, preq->psess, "Server_PurchaseMicrotransaction");
					pktSendU32(pkt, 0);
					pktSendU32(pkt, preq->u32); // product id

					if(pProd && pProd->pDef)
					{
						char *estrError = NULL;

						microtrans_GetCanPurchaseErrorString(eError, pProd->pDef, preq->psess->lang, &estrError);
						pktSendString(pkt, estrError);

						estrDestroy(&estrError);
					}
					else
					{
						pktSendString(pkt, pchError);
					}
					pktSend(&pkt);

					DestroyRequest(preq);
					break;
			}

			if(pchError)
				free(pchError);
		}
	}
	EARRAY_FOREACH_END;

}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/***********************************************************************
 * wgsMessageHandler
 *
 */
void wgsMessageHandler(Packet *pktIn, int cmd, NetLink *link, GatewayLink *gwlink)
{
	Packet *pkt;

	PERFINFO_AUTO_START_FUNC();

	switch(cmd)
	{
		case PING_CMD:
			{
				HandlePing(pktIn, link);
			}
			break;

		case CONNECTION_CMD:
			{
				char *pch = pktGetStringTemp(pktIn);

				if(stricmp(pch, "Proxy_ConnectionPing")==0)
				{
					HandleConnectionPing(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestHello")==0)
				{
					HandleHello(pktIn, link, gwlink);
				}
				else if(stricmp(pch, "Proxy_Log")==0)
				{
					HandleLog(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_Perf")==0)
				{
					HandlePerf(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_ShutdownMessage")==0)
				{
					HandleShutdownMessage(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestCreateSession")==0)
				{
					HandleRequestCreateSession(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestDestroySession")==0)
				{
					HandleRequestDestroySession(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestResource")==0)
				{
					HandleRequestResource(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestDefaultResource")==0)
				{
					HandleRequestDefaultResource(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestDictionaryKeys")==0)
				{
					HandleRequestDictionaryKeys(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestTexture")==0)
				{
					HandleRequestTexture(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestTranslations")==0)
				{
					HandleRequestTranslations(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestCharactersForAccountName")==0)
				{
					HandleRequestCharactersForAccountName(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestOnlineEntityByName")==0)
				{
					HandleOnlineEntityByName(pktIn, link);
				}
				else if(stricmp(pch, "Proxy_RequestGuildIdForName")==0)
				{
					HandleRequestGuildIdForName(pktIn, link);
				}
				else
				{
					printf("Unknown connection message: %s\n", pch);
				}
			}
			break;

		case SESSION_CMD:
			{
				char *pch = pktGetStringTemp(pktIn);
				U32 magic = pktGetU32(pktIn);
				U32 idxServer = pktGetU32(pktIn);

				GatewaySession *psess = wgsFindSessionForIndex(idxServer, magic);
				if(!psess)
				{
					verbose_printf("ERROR: Can't find session for idxServer:%u, magic:%u (0x%x)\n", idxServer, magic, magic);

					pkt = pktCreate(link, SESSION_CMD);

					pktSendU32(pkt, magic);
					pktSendU32(pkt, idxServer);
					pktSendString(pkt, "DestroySession");

					pktSend(&pkt);
					break;
				}

				if(stricmp(pch, "Proxy_Ping")==0)
				{
					HandleSessionPing(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_RequestContainer")==0)
				{
					HandleSessionGetContainer(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_UpdateContainer")==0)
				{
					HandleSessionUpdateContainer(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_ReleaseContainer")==0)
				{
					HandleSessionReleaseContainer(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_RequestHeadshot")==0)
				{
					HandleSessionRequestHeadshot(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_SendCommand")==0)
				{
					HandleSessionSendCommand(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_RequestItemTooltip")==0)
				{
					HandleSessionRequestItemTooltip(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_SaveGatewayGameData")==0)
				{
					HandleSaveGatewayGameData(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_GatewayGameGrantReward")==0)
				{
					HandleGatewayGameGrantReward(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_GatewayGameQueueReward") == 0)
				{
					HandleGatewayGameQueueReward(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_GatewayGameClaimQueuedRewards")==0)
				{
					HandleGatewayGameClaimRewards(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_GatewayGameDiscardQueuedRewards")==0)
				{
					HandleGatewayGameDiscardRewards(pktIn, link, psess);
				}
				else if(stricmp(pch, "Proxy_RewardRequest")==0)
				{
					HandleRewardRequest(pktIn, link, psess);
				}
			}
			break;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

/***********************************************************************
 * wgsConnectHandler
 *
 */
int wgsConnectHandler(NetLink* link, GatewayLink *gwlink)
{
	printf("New GatewayProxy connection\n");

	eaPush(&s_ppLinks, link);

	// The Watcher is notified once we get a Hello packet, because we're
	//   waiting for a slave to connect (and other shards might try to connect
	//   in the meantime).

	return 0;
}

/***********************************************************************
 * wgsDisconnectHandler
 *
 */
int wgsDisconnectHandler(NetLink* link, GatewayLink *gwlink)
{
	printf("GatewayProxy disconnected!\n");
	gslGatewayServer_Log(LOG_GATEWAY_SERVER, "GatewayProxy \"%s\" disconnected!\n", gwlink->achShard);

	wgsDestroyAllSessionsForLink(link);
	InvalidateAllRequestsForLink(link);

	eaFindAndRemove(&s_ppLinks, link);

	if(gwlink->bLinkIsSlave)
	{
		gslGatewayServer_Log(LOG_GATEWAY_SERVER, "My GatewayProxy disconnected!\n");
		gateway_WatcherDisconnected(s_pwatcher);
	}

	return 0;
}

void wgsStartGatewayProxy(void)
{
	char *estrOptions = NULL;

	gslGatewayServer_Log(LOG_GATEWAY_SERVER, "Starting GatewayProxy with options (%s)...", estrOptions ? estrOptions : "none");

	s_pwatcher = gateway_CreateAndStartWatcher("GatewayProxy",
		"game\\server\\GatewayProxy",
		estrOptions,
		kGatewayWatcherFlags_AddLoginServers | kGatewayWatcherFlags_AddShardName | kGatewayWatcherFlags_WatchConnection,
		"GatewayServer", GATEWAYSERVER_PORT_START, GATEWAYSERVER_PORT_END,
		wgsMessageHandler,
		wgsConnectHandler,
		wgsDisconnectHandler,
		sizeof(GatewayLink));

	estrDestroy(&estrOptions);

}

void OVERRIDE_LATELINK_GatewayNotifyLockStatus(bool bLocked)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		Packet *pkt;
		CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_Lock");
		pktSendU32(pkt, bLocked);
		pktSend(&pkt);
	}
	EARRAY_FOREACH_END;
}

void wgsBroadcastMessageToAllConnections(const char *pTitle, const char *pString)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		Packet *pkt;
		CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_Broadcast");
		pktSendString(pkt, pTitle);
		pktSendString(pkt, pString);
		pktSend(&pkt);
	}
	EARRAY_FOREACH_END;
}

/***********************************************************************
 * gslGatewayServer_ShardClusterOverviewChanged
 *
 */
void gslGatewayServer_ShardClusterOverviewChanged(Cluster_Overview *pOverview)
{
	if(!pOverview)
		return;
	
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		GatewayLink *gwlink = (GatewayLink *)(s_ppLinks[i]->user_data);
		if(gwlink->bLinkIsSlave)
		{
			char *estr = NULL;

			Packet *pkt;
			CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_ClusterInfo");
			ParserWriteJSON(&estr, parse_Cluster_Overview, pOverview, 0, 0, 0);
			pktSendString(pkt, estr);
			pktSend(&pkt);

			estrDestroy(&estr);
		}
	}
	EARRAY_FOREACH_END;
}

AUTO_STRUCT;
typedef struct MappedActivites
{
	const char **ppchMappedEvents;				AST(POOL_STRING NAME(Events))
	const char **ppchMappedActivites;			AST(POOL_STRING NAME(Activites))
}MappedActivites;

static void gslGateway_SendAllActiveEvents(NetLink *link)
{
	char *estr = NULL;
	MappedActivites *pMapping = StructCreate(parse_MappedActivites);
	Packet *pkt;
	int i;

	for(i=0;i<eaSize(&g_Activities.ppActivities);i++)
	{
		eaPush(&pMapping->ppchMappedActivites,g_Activities.ppActivities[i]->pchActivityName);
	}

	for(i=0;i<eaSize(&g_ActiveEvents.ppDefs);i++)
	{
		EventDef *pDef = GET_REF(g_ActiveEvents.ppDefs[i]->hEvent);

		if(pDef)
			eaPush(&pMapping->ppchMappedEvents,pDef->pchEventName);
	}

	CONNECTION_PKTCREATE(pkt, link, "Server_ActiveEvents");
	ParserWriteJSON(&estr, parse_MappedActivites, pMapping, 0, 0, 0);
	pktSendString(pkt,estr);
	pktSend(&pkt);

	estrDestroy(&estr);
}

void gslGateway_SendStartActivity(const char *pchActivityName)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		GatewayLink *gwlink = (GatewayLink *)(s_ppLinks[i]->user_data);
		
		if(gwlink->bLinkIsSlave)
		{
			Packet *pkt;
			CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_StartActivity");
			pktSendString(pkt,pchActivityName);
			pktSend(&pkt);
		}
	}
	EARRAY_FOREACH_END;
}

void gslGateway_SendEndActivity(const char *pchActivityName)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		GatewayLink *gwlink = (GatewayLink *)(s_ppLinks[i]->user_data);
		if(gwlink->bLinkIsSlave)
		{
			Packet *pkt;
			CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_EndActivity");
			pktSendString(pkt,pchActivityName);
			pktSend(&pkt);
		}
	}
	EARRAY_FOREACH_END;
}

void gslGateway_SendStartEvent(const char *pchEventName)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		GatewayLink *gwlink = (GatewayLink *)(s_ppLinks[i]->user_data);
		if(gwlink->bLinkIsSlave)
		{
			Packet *pkt;
			CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_StartEvent");
			pktSendString(pkt,pchEventName);
			pktSend(&pkt);
		}
	}
	EARRAY_FOREACH_END;
}

void gslGateway_SendEndEvent(const char *pchEventName)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		GatewayLink *gwlink = (GatewayLink *)(s_ppLinks[i]->user_data);
		if(gwlink->bLinkIsSlave)
		{
			Packet *pkt;
			CONNECTION_PKTCREATE(pkt, s_ppLinks[i], "Server_EndEvent");
			pktSendString(pkt,pchEventName);
			pktSend(&pkt);
		}
	}
	EARRAY_FOREACH_END;
}

#include "gslGatewayMessages_c_ast.c"

/* End of File */
