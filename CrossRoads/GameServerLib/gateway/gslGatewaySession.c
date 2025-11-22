/***************************************************************************
 
 
 ***************************************************************************/
#include "stdtypes.h"
#include "earray.h"
#include "MemoryPool.h"
#include "net.h"
#include "file.h"
#include "Alerts.h"
#include "ResourceInfo.h"
#include "StringCache.h"

#include "Guild.h"
#include "GlobalTypes.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "gslEntity.h"
#include "Player.h"
#include "Character.h"
#include "Character_tick.h"

#include "gslActivity.h"
#include "AuctionLot.h"
#include "AuctionLot_h_ast.h"
#include "tradeCommon.h"
#include "tradeCommon_h_ast.h"
#include "ItemAssignmentsUICommon.h"

#include "gslGatewayMessages.h"
#include "gslGatewaySession.h"
#include "gslGatewayServer.h"
#include "gslGatewayStructMapping.h"
#include "gslGatewayContainerMapping.h"
#include "GameAccountDataCommon.h"
#include "textparserJSON.h"
#include "gslGatewayMappedEntity.h"
#include "WebRequests.h"

#include "ItemAssignmentsUICommon_h_ast.h"
#include "gslGatewaySession_h_ast.h"
#include "WebRequests_h_ast.h"

#include "autogen/ChatServer_autogen_RemoteFuncs.h"
#include "gslItemAssignments.h"

MP_DEFINE(GatewaySession);

GatewaySession **g_eaSessions = NULL;
GatewaySession **g_eaSessionsToBeFreed = NULL;
ContainerTracker **g_eaTrackersToBeFreed = NULL;

static StashTable s_GatewaySessionsByID = NULL;
	// Used by shard/server monitors to look at sessions. Not used internally.

//#define DEBUG

#define DBG_PRINTF(format, ...) verbose_printf(format,##__VA_ARGS__)
//#define DBG_PRINTF(format, ...)

void wgsInitSessions(void)
{
	static bool s_bInited = false;

	if(!s_bInited)
	{
		eaCreate(&g_eaSessions);
		MP_CREATE(GatewaySession, 16);

		s_GatewaySessionsByID = stashTableCreateInt(16);
		resRegisterDictionaryForStashTable("GatewaySessions", RESCATEGORY_OTHER, 0, s_GatewaySessionsByID, parse_GatewaySession);

		ContainerMappingInit();

		s_bInited = true;
	}
}

int wgsGetSessionCount(void)
{
	return eaSize(&g_eaSessions);
}

GatewaySession *wgsCreateSession(U32 uiMagic, U32 idAccount, U32 lang, NetLink *link)
{
	GatewaySession *psess = NULL;
	int idxSlot;
	char idBuf[128];

	wgsInitSessions();

	psess = MP_ALLOC(GatewaySession);
	StructInit(parse_GatewaySession, psess);

	idxSlot = eaPush(&g_eaSessions, psess);
	DBG_PRINTF("\tAllocated new session @%d\n", idxSlot);

	psess->uiIdxServer = idxSlot;
	psess->uiMagic = uiMagic;
	psess->idAccount = idAccount;
	psess->link = link;

	psess->lang = (Language)lang;

	// Subscribe to the game account data. The proxy doesn't get a response
	//   until the GameAccountData is loaded.
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA),
		ContainerIDToString(idAccount, idBuf),	psess->hGameAccountData);

	if(GET_REF(psess->hGameAccountData))
	{
		session_GameAccountDataSubscribed(psess);
	}

	//Send a login to the chat server, so that mail can work
	RemoteCommand_gatewayChat_LoginUser(GLOBALTYPE_CHATSERVER,0,psess->idAccount);

	stashIntAddPointer(s_GatewaySessionsByID, psess->idAccount, psess, true);

	DBG_PRINTF("New session @%d for (%d)\n", psess->uiIdxServer, psess->uiMagic);

	return psess;
}

GatewaySession *wgsFindSessionForIndex(U32 uiIdxServer, U32 uiMagic)
{
	S32 i;
	for(i = 0; i < eaSize(&g_eaSessions); i++)
	{
		GatewaySession *psess = g_eaSessions[i];
		if(psess && psess->uiMagic == uiMagic)
		{
			return psess;
		}
	}

	DBG_PRINTF("The requested session wasn't found. idx: %d, magic: %d\n", uiIdxServer, uiMagic);
	return NULL;
}

GatewaySession *wgsFindSessionForAccountId(ContainerID iId)
{
	S32 i;
	for(i = eaSize(&g_eaSessions)-1; i >= 0; --i)
	{
		GatewaySession *psess = g_eaSessions[i];
		if(psess->idAccount == iId)
		{
			return psess;
		}		
	}
	DBG_PRINTF("The requested session wasn't found for account id %d.\n", iId);

	return NULL;
}

GatewaySession *wgsFindOwningSessionForDBContainer(GlobalType type, ContainerID iId)
{
	char achID[24];
	itoa(iId, achID, 10);

	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];
		EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, n);
		{
			if(psess->ppContainers[n]->pMapping->globaltype == type
				&& psess->ppContainers[n]->idOwnerAccount == psess->idAccount
				&& stricmp(psess->ppContainers[n]->estrID, achID) == 0)
			{
				return psess;
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	return NULL;
}

void wgsDestroySession(GatewaySession *psess, bool bNotifyProxy)
{
	int idx;

	if(!psess)
		return;

	//Log out from the chat server
	RemoteCommand_gatewayChat_LogoutUser(GLOBALTYPE_CHATSERVER,0,psess->idAccount);

	wgsInvalidateAllRequestsForSession(psess);

	stashIntRemovePointer(s_GatewaySessionsByID, psess->idAccount, NULL);
	eaFindAndRemoveFast(&g_eaSessions, psess);

	if(bNotifyProxy && psess->link)
	{
		session_SendDestroySession(psess);
	}

	DBG_PRINTF("Destroying session @%d for (%d)\n", psess->uiIdxServer,  psess->uiMagic);

	for(idx = eaSize(&psess->ppContainers)-1; idx >= 0; idx--)
	{
		FreeMappedContainer(psess, psess->ppContainers[idx]);
	}

	//
	// The actual freeing is done later to avoid a re-entrancy problem with container
	//   subscription.
	// When an entity is deleted, and that entity is also a LoginEntity, this function
	//   is called from inside RefSystem_RemoveReferentWithReason. When we free a
	//   session, then the entity subscription loses all its references. When that
	//   happens,  RefSystem_RemoveReferentWithReason is called and the referent is
	//   removed from the dictionary. So far, so good.
	// The problem is when we return back to the original call to
	//   RefSystem_RemoveReferentWithReason that started the whole thing. Upon return,
	//   the original referent is now gone, and the remainder of that function will choke.
	// To avoid this, we push the session into a list of sessions to be freed once we
	//   get back from the callback (e.g. on the next tick).
	//
	eaPush(&g_eaSessionsToBeFreed, psess);
}

void wgsFreeDestroyedSessions(void)
{
	//
	// See wgsDestroySession for the reason why we need this function.
	//
	int i;
	for(i = eaSize(&g_eaSessionsToBeFreed)-1; i >= 0; i--)
	{
		GatewaySession *psess = g_eaSessionsToBeFreed[i];

		// This should take care of getting rid of all the ContainerTrackers,
		//   as well as all the REF_TOs inside them.
		DBG_PRINTF("Freeing session @%d for (%d)\n", psess->uiIdxServer,  psess->uiMagic);
		StructDeInit(parse_GatewaySession, psess);
		MP_FREE(GatewaySession, psess);
	}

	eaClear(&g_eaSessionsToBeFreed);
}

void wgsDestroySessionForIndex(U32 uiIdxServer, U32 uiMagic, bool bNotifyProxy)
{
	GatewaySession *psess = wgsFindSessionForIndex(uiIdxServer, uiMagic);
	if(psess)
	{
		wgsDestroySession(psess, bNotifyProxy);
	}
	else
	{
		DBG_PRINTF("Unable to find session to destroy: @%d for (??:%d)\n", uiIdxServer,  uiMagic);
	}
}

void wgsDestroyAllSessionsForLink(NetLink *link)
{
	int i;
	// This has to go in reverse order because we are deleting items from
	//   the list as we traverse it. This can cause the order to change
	//   at or above the current index, but not below.
	for(i = eaSize(&g_eaSessions)-1; i >= 0; i--)
	{
		GatewaySession *pess = g_eaSessions[i];
		if(pess->link == link)
		{
			wgsDestroySession(pess, true);
		}
	}
}

void wgsDestroyAllSessions(void)
{
	GatewaySession *psess;
	while((psess = eaPop(&g_eaSessions)) != NULL)
	{
		wgsDestroySession(psess, true);
	}
}

//
// wgsAllContainersModified
//
// Marks all the containers in every session as modified and 
// the sessions as dirty.
//
void wgsAllContainersModified()
{
	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];
		session_AllContainersModified(psess);
	}
	EARRAY_FOREACH_END;
}

//
// wgsContainerModified
//
// Marks the matching container(s) as modified and the owning session(s) as
// dirty. If pchID is NULL, marks all containers of the given type as dirty.
//
void wgsContainerModified(GatewayGlobalType type, const char *pchID)
{
	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];
		session_ContainerModified(psess, type, pchID);
	}
	EARRAY_FOREACH_END;
}


void wgsNotifyActivityStarted(ActiveActivity *pActivity)
{
	wgsContainerModified(GW_GLOBALTYPE_CRAFTING_LIST, NULL);
	wgsContainerModified(GW_GLOBALTYPE_CRAFTING_DETAIL, NULL);
	wgsContainerModified(GW_GLOBALTYPE_VENDOR, NULL);

	gslGateway_SendStartActivity(pActivity->pchActivityName);
}

void wgsNotifyActivityEnded(ActiveActivity *pActivity)
{
	wgsContainerModified(GW_GLOBALTYPE_CRAFTING_LIST, NULL);
	wgsContainerModified(GW_GLOBALTYPE_CRAFTING_DETAIL, NULL);
	wgsContainerModified(GW_GLOBALTYPE_VENDOR, NULL);

	gslGateway_SendEndActivity(pActivity->pchActivityName);
}

void wgsNotifyEventStarted(EventDef *pEvent)
{
	gslGateway_SendStartEvent(pEvent->pchEventName);
}

void wgsNotifyEventEnded(EventDef *pEvent)
{
	gslGateway_SendEndEvent(pEvent->pchEventName);
}

void DEFAULT_LATELINK_GatewayEntityTick(Entity *pEnt)
{

}

void wgsUpdateAllContainers(void)
{	
	PERFINFO_AUTO_START_FUNC();

	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];

		EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, j);
		{
			ContainerTracker *ptracker = psess->ppContainers[j];

			if(ptracker->bReady && ptracker->pMapping->pfnIsModified)
			{
				if(ptracker->pMapping->pfnIsModified(psess, ptracker))
				{
					session_ContainerTrackerModified(psess, ptracker);
				}
			}

			if(ptracker->pMapping->globaltype == GLOBALTYPE_ENTITYPLAYER)
			{
				GatewayEntityTick(GET_REF(ptracker->hEntity));
			}

			if(!ptracker->bReady && ptracker->pMapping->pfnIsReady)
			{
				if(ptracker->pMapping->pfnIsReady(psess, ptracker))
				{
					ptracker->bReady = true;
					ptracker->bSend = true;

					DBG_PRINTF("Container ready (periodic) %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
						psess->uiIdxServer, psess->uiMagic);
				}
			}

			if(ptracker->bReady && ptracker->bSend)
			{
				session_SendContainer(psess, ptracker);
				ptracker->bSend = false;
				ptracker->bFullUpdate = false;
				ptracker->bModified = false;
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/

void session_GameAccountDataSubscribed(GatewaySession *psess)
{
	Packet *pkt;

	DBG_PRINTF("Got GameAccountData@%d for (%d)\n", psess->uiIdxServer, psess->uiMagic);

	// Tell the proxy that we're ready to go.
	CONNECTION_PKTCREATE(pkt, psess->link, "Server_CreateSession");
	pktSendU32(pkt, psess->uiMagic);
	pktSendU32(pkt, psess->uiIdxServer);
	pktSend(&pkt);
}

void session_GameAccountDataModified(GatewaySession *psess)
{
	GameAccountData *pGameAccountData = GET_REF(psess->hGameAccountData);
	if(pGameAccountData && psess->pGameAccountDataExtract)
	{
		GAD_UpdateExtract(pGameAccountData, psess->pGameAccountDataExtract);
	}

	session_ContainerModified(psess, GW_GLOBALTYPE_LOGIN_ENTITY, NULL);
}

ContainerTracker *session_FindContainerTracker(GatewaySession *psess, GatewayGlobalType type, const char *pchID)
{
	if(!psess)
		return NULL;

	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		if(psess->ppContainers[i]->gatewaytype == type && (!pchID || stricmp(psess->ppContainers[i]->estrID, pchID) == 0))
		{
			return psess->ppContainers[i];
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

ContainerTracker *session_FindDBContainerTracker(GatewaySession *psess, GlobalType type, ContainerID id)
{
	char achID[24];
	itoa(id, achID, 10);

	if(!psess)
		return NULL;

	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		if(psess->ppContainers[i]->pMapping->globaltype == type
			&& stricmp(psess->ppContainers[i]->estrID, achID) == 0)
		{
			return psess->ppContainers[i];
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

ContainerTracker *session_FindFirstContainerTrackerForType(GatewaySession *psess, GatewayGlobalType type)
{
	if(!psess)
		return NULL;

	EARRAY_FOREACH_BEGIN(psess->ppContainers, i);
	{
		if(psess->ppContainers[i]->gatewaytype == type)
		{
			return psess->ppContainers[i];
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

//
// session_ContainerTrackerModified
//
// Marks the container tracker as modified
//
void session_ContainerTrackerModified(GatewaySession *psess, ContainerTracker *ptracker)
{
	int i;

	DBG_PRINTF("Container modified %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
		psess->uiIdxServer, psess->uiMagic);

	ptracker->bModified = true;
	ptracker->bSend = true;
	ptracker->bReady = false;

	if(ptracker->pOfflineCopy)
	{
		cmap_DestroyOfflineEntity(ptracker->pOfflineCopy);
		ptracker->pOfflineCopy = NULL;
	}

	if(ptracker->pMapping->pDependentContainers)
	{
		for(i=0; i<GATEWAY_CONTAINER_MAX_DEPS; i++)
		{
			if(ptracker->pMapping->pDependentContainers[i])
				session_ContainerModified(psess, ptracker->pMapping->pDependentContainers[i], NULL);
			else
				break;
		}
	}
}

//
// session_AllContainersModified
//
// Marks all the containers in a session as modified and the 
// owning session as dirty. 
//

void session_AllContainersModified(GatewaySession *psess)
{
	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		session_ContainerTrackerModified(psess,psess->ppContainers[i]);
	}
	EARRAY_FOREACH_END;
}

//
// session_ContainerModified
//
// Marks the matching container(s) as modified and the owning session as
// dirty. If pchID is NULL, marks all containers of the given type as dirty.
//
void session_ContainerModified(GatewaySession *psess, GatewayGlobalType type, const char *pchID)
{
	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		ContainerTracker *ptracker = psess->ppContainers[i];

		// If the type matches or the type is entity player and this is a login entity
		// AND there was no ID given or it matches.
		if((ptracker->gatewaytype == type
				|| (type == GLOBALTYPE_ENTITYPLAYER && ptracker->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY))
			&& (pchID == NULL || stricmp(pchID, ptracker->estrID) == 0))
		{
			session_ContainerTrackerModified(psess, ptracker);
		}
	}
	EARRAY_FOREACH_END;
}

//
// session_ContainerAdded
//
//
void session_ContainerAdded(GatewaySession *psess, GatewayGlobalType type, const char *pchID)
{
	if(type == GLOBALTYPE_ENTITYPLAYER)
	{
		EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
		{
			ContainerTracker *ptracker = psess->ppContainers[i];

			if(ptracker->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY && stricmp(pchID, ptracker->estrID) == 0)
			{
				gslItemAssignment_CheckRankSchedules(GET_REF(ptracker->hEntity));
			}
		}
		EARRAY_FOREACH_END;
	}
	
}


//
// session_ContainerRemoved
//
// Used to note that a given container has been destroyed by some outside
// influence. For example, when a character is deleted.
//
// If the LoginEntity is destroyed, the session is also destroyed.
//
// WARNING: This function may delete the session passed in. If you are looping
//          through sessions, make sure you are doing so reverse.
//
void session_ContainerRemoved(GatewaySession *psess, GatewayGlobalType type, const char *pchID)
{
	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		ContainerTracker *ptracker = psess->ppContainers[i];
		bool bIsLoginEntity = (type == GLOBALTYPE_ENTITYPLAYER && ptracker->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY);

		// If the type matches or the type is entity player and this is a login entity
		// AND there was no ID given or it matches.
		if((ptracker->gatewaytype == type || bIsLoginEntity)
			&& (pchID == NULL || stricmp(pchID, ptracker->estrID) == 0))
		{
			DBG_PRINTF("Container modified (remove) %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
				psess->uiIdxServer, psess->uiMagic);

			if(bIsLoginEntity)
			{
				// WARNING: This will delete the session passed in. If you are looping
				//          through sessions, make sure you are doing so reverse.
				wgsDestroySession(psess, true);
				return;
			}
			else
			{
				session_ReleaseContainer(psess, ptracker->gatewaytype, ptracker->estrID);
			}
		}
	}
	EARRAY_FOREACH_END;
}


/***************************************************************************/

void session_ReleaseContainers(GatewaySession *psess)
{
	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		ContainerTracker *ptracker = psess->ppContainers[i];

		DBG_PRINTF("Container released (all) %s:%s to @%d for (%d)\n",
			ptracker->pMapping->pchName, ptracker->estrID, psess->uiIdxServer, psess->uiMagic);

		session_SendContainerDestroy(psess, ptracker);
		FreeMappedContainer(psess, ptracker);

		// This is done in a loop rather than using a eaClearStruct because
		// clearing this struct might cause a callback which then tries to use
		// the struct. Thus, we need to remove it before destroying it.

		eaFindAndRemoveFast(&psess->ppContainers, ptracker);
		StructDestroy(parse_ContainerTracker, ptracker);
	}
	EARRAY_FOREACH_END;
}


void session_ReleaseContainer(GatewaySession *psess, GatewayGlobalType type, const char *pchID)
{
	ContainerTracker *ptracker = session_FindContainerTracker(psess, type, pchID);
	if(ptracker)
	{
		DBG_PRINTF("Container released (single) %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
			psess->uiIdxServer, psess->uiMagic);

		session_SendContainerDestroy(psess, ptracker);
		FreeMappedContainer(psess, ptracker);

		// Clearing this struct might cause a callback which then tries to use
		// the struct. Thus, we need to remove it before destroying it.
		eaFindAndRemoveFast(&psess->ppContainers, ptracker);

		//
		// The actual freeing is done later to avoid a re-entrancy problem with container
		//   subscription.
		// When an container is deleted from the objectDB (Like a guild or saved pet), this function
		//   is called from inside RefSystem_RemoveReferentWithReason. When we free a
		//   container tracker, then the subscription loses all its references. When that
		//   happens,  RefSystem_RemoveReferentWithReason is called and the referent is
		//   removed from the dictionary. So far, so good.
		// The problem is when we return back to the original call to
		//   RefSystem_RemoveReferentWithReason that started the whole thing. Upon return,
		//   the original referent is now gone, and the remainder of that function will choke.
		// To avoid this, we push the container tracker into a list of container trackers to be freed once we
		//   get back from the callback (e.g. on the next tick).
		//
		eaPush(&g_eaTrackersToBeFreed,ptracker);
	}
}


void session_FreeReleasedContainers(void)
{
	int i;

	//
	// See wgsDestroySession for the reason why we need this function.
	//

	for(i=eaSize(&g_eaTrackersToBeFreed)-1;i>=0;i--)
	{
		StructDestroy(parse_ContainerTracker, g_eaTrackersToBeFreed[i]);
	}

	eaClear(&g_eaTrackersToBeFreed);
}

/***************************************************************************/

void session_SendDestroySession(GatewaySession *psess)
{
	Packet *pkt;

	SESSION_PKTCREATE(pkt, psess, "Server_DestroySession");
	pktSend(&pkt);
}

/***************************************************************************/

// Anything internal should use GetContainerEx because it bypasses safety checks
//   that won't be true because it's internal. For example, GetContainer requires
//   that pets and group objects already have trackers. To actually get them
//   the first time, you'll have to do it through GetContainerEx
ContainerTracker *session_GetContainerEx(GatewaySession *psess, ContainerMapping *pmapping, const char *pchID, void *pvParams, bool bSend)
{
	ContainerTracker *ptracker;

	PERFINFO_AUTO_START_FUNC();

	ptracker = session_CreateContainerTracker(psess, pmapping, pchID);

	if(ptracker)
	{
		pmapping->pfnSubscribe(psess, ptracker, pvParams);

		ptracker->bSend = bSend;

		DBG_PRINTF("Subscribed to %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
			psess->uiIdxServer, psess->uiMagic);
	}

	PERFINFO_AUTO_STOP();
	return ptracker;
}

ContainerTracker *session_GetContainer(GatewaySession *psess, ContainerMapping *pmapping, const char *pchID, void *pvParams)
{
	return session_GetContainerEx(psess, pmapping, pchID, pvParams, true);
}

ContainerTracker *session_CreateContainerTracker(GatewaySession *psess, ContainerMapping *pmap, const char *pchID)
{
	ContainerTracker *ptracker;

	PERFINFO_AUTO_START_FUNC();

	if(!pmap)
		return NULL;

	ptracker = session_FindContainerTracker(psess, pmap->gatewaytype, pchID);
	if(!ptracker && pchID && stricmp(pchID, "0") != 0)
	{
		// This is a currently untracked container.
		// Add it to the list
		ptracker = StructCreate(parse_ContainerTracker);
		ptracker->gatewaytype = pmap->gatewaytype;
		estrCopy2(&(ptracker->estrID), pchID);
		ptracker->bReady = false;
		ptracker->pMapping = pmap;

		eaPush(&psess->ppContainers, ptracker);
	}

	PERFINFO_AUTO_STOP();

	return ptracker;
}


/***************************************************************************/

void session_SendContainer(GatewaySession *psess, ContainerTracker *ptracker)
{
	Packet *pkt;
	char *estr = NULL;
	char *estrMsg = NULL;
	char *pchToSend;
	bool bFullUpdate = true;

	PERFINFO_AUTO_START_FUNC();

	DBG_PRINTF("Sending container %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
		psess->uiIdxServer, psess->uiMagic);

	WriteContainerJSON(&estr, psess, ptracker);
	pchToSend = estr;
	if(ptracker->estrDiff)
	{
		unsigned int ulen = estrLength(&ptracker->estrDiff);
		if(!ptracker->bFullUpdate && ulen < estrLength(&estr))
		{
			bFullUpdate = false;

			if(ulen)
			{
				pchToSend = ptracker->estrDiff;
			}
			else
			{
				// Empty diff, don't send anything.
				pchToSend = NULL;
			}
		}
	}

	if(pchToSend)
	{
		estrConcatf(&estrMsg, "Server_%s", ptracker->pMapping->pchName);
		if(!ptracker->pMapping->pPacketTracker)
		{
			ptracker->pMapping->pPacketTracker = PacketTrackerFind("GatewayContainer", 0, allocAddString(estrMsg));
		}
		pkt = session_pktCreateWithTracker(psess, estrMsg, ptracker->pMapping->pPacketTracker);
		pktSendString(pkt, ptracker->estrID);
		pktSendBits(pkt, 8, bFullUpdate ? 1 : 0);
		pktSendString(pkt, pchToSend);
		pktSend(&pkt);
	}

	estrDestroy(&ptracker->estrDiff);
	estrDestroy(&estr);
	estrDestroy(&estrMsg);

	PERFINFO_AUTO_STOP();
}

void session_SendContainerDestroy(GatewaySession *psess, ContainerTracker *ptracker)
{
	Packet *pkt;
	char *estrMsg = NULL;

	DBG_PRINTF("Sending container delete %s:%s to @%d for (%d)\n", ptracker->pMapping->pchName, ptracker->estrID,
		psess->uiIdxServer, psess->uiMagic);

	estrConcatf(&estrMsg, "Server_%s", ptracker->pMapping->pchName);
	if(!ptracker->pMapping->pPacketTracker)
	{
		ptracker->pMapping->pPacketTracker = PacketTrackerFind("GatewayContainer", 0, allocAddString(estrMsg));
	}
	pkt = session_pktCreateWithTracker(psess, estrMsg, ptracker->pMapping->pPacketTracker);
	pktSendString(pkt, ptracker->estrID);
	pktSendBits(pkt, 8, 0);
	pktSendString(pkt, "destroy *");
	pktSend(&pkt);

	estrDestroy(&estrMsg);
	PERFINFO_AUTO_STOP();
}

/***************************************************************************/

void session_SendMessage(GatewaySession *psess, const char *pTitle, const char *pString)
{
	Packet *pkt;

	SESSION_PKTCREATE(pkt, psess, "Server_Message");
	pktSendString(pkt, pTitle);
	pktSendString(pkt, pString);
	pktSend(&pkt);
}

/***************************************************************************/

void session_sendClientCmd(GatewaySession *psess, const char *cmdLine)
{
	Packet *pkt;
	char *estr = NULL;

	if(psess)
	{
		SESSION_PKTCREATE(pkt, psess, "Server_ClientCmd");
		pktSendString(pkt,cmdLine);
		pktSend(&pkt);
	}
}

/***************************************************************************/

GameAccountDataExtract *session_GetCachedGameAccountDataExtract(GatewaySession *psess)
{
	if(psess)
	{
		if(!psess->pGameAccountDataExtract)
		{
			GameAccountData *pGameAccountData = GET_REF(psess->hGameAccountData);
			if(pGameAccountData)
			{
				psess->pGameAccountDataExtract = GAD_CreateExtract(pGameAccountData);
			}
		}

		if(psess->pGameAccountDataExtract)
		{
			return psess->pGameAccountDataExtract;
		}
	}

	return NULL;
}

/***************************************************************************/

Packet *session_pktCreateWithTracker(GatewaySession *psess, const char *pchMessage, PacketTracker *packetTracker)
{
	Packet *pkt;

	pkt = pktCreateWithTracker(psess->link, SESSION_CMD, packetTracker);
	pktSendU32(pkt, psess->uiMagic);
	pktSendU32(pkt, psess->uiIdxServer);

	pktSendString(pkt, pchMessage);

	return pkt;
}

/***************************************************************************/

Entity *session_GetLoginEntity(GatewaySession *psess)
{
	Entity *pEnt = NULL;

	if(!psess)
		return NULL;

	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		if(psess->ppContainers[i]->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY
			&& psess->ppContainers[i]->idOwnerAccount == psess->idAccount)
		{
			pEnt = GET_REF(psess->ppContainers[i]->hEntity);
			break;
		}
	}
	EARRAY_FOREACH_END;

	return pEnt;
}

static Entity *makeOrGetEntityOfflineCopy(ContainerTracker *ptracker)
{
	if(!ptracker->pOfflineCopy)
	{
		ptracker->pOfflineCopy = cmap_CreateOfflineEntity(ptracker);
	}

	return ptracker->pOfflineCopy;
}

Entity *session_GetLoginEntityOfflineCopy(GatewaySession *psess)
{
	Entity *pEnt = NULL;

	if(!psess)
		return NULL;

	EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
	{
		if(psess->ppContainers[i]->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY
			&& psess->ppContainers[i]->idOwnerAccount == psess->idAccount)
		{
			pEnt = makeOrGetEntityOfflineCopy(psess->ppContainers[i]);
			break;
		}
	}
	EARRAY_FOREACH_END;

	return pEnt;
}

Entity *session_GetEntityOfflineCopy(GatewaySession *psess, ContainerTracker *ptracker)
{
	if(GlobalTypeParent(ptracker->gatewaytype) == GLOBALTYPE_ENTITY)
	{
		Entity *pSubscribedEnt = GET_REF(ptracker->hEntity);
		
		EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
		{
			if(psess->ppContainers[i]->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY)
			{
				if(GET_REF(psess->ppContainers[i]->hEntity) == pSubscribedEnt)
					return makeOrGetEntityOfflineCopy(psess->ppContainers[i]);
				else
					break;
			}
		}
		EARRAY_FOREACH_END;
	}

	return makeOrGetEntityOfflineCopy(ptracker);
}

/***************************************************************************/

void wgsDBContainerModified(GlobalType type, ContainerID id)
{
	char achID[24];
	itoa(id, achID, 10);

	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];
		session_ContainerModified(psess, type, achID);
	}
	EARRAY_FOREACH_END;
}

void wgsDBContainerAdded(GlobalType type, ContainerID id)
{
	char achID[24];
	itoa(id, achID, 10);

	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];
		session_ContainerAdded(psess, type, achID);
	}
	EARRAY_FOREACH_END;
	wgsDBContainerModified(type, id);
}

void wgsDBContainerRemoved(GlobalType type, ContainerID id)
{
	char achID[24];
	itoa(id, achID, 10);

	EARRAY_FOREACH_REVERSE_BEGIN(g_eaSessions, i);
	{
		GatewaySession *psess = g_eaSessions[i];
		session_ContainerRemoved(psess, type, achID);
	}
	EARRAY_FOREACH_END;
}

void SubscribeDBContainer(GatewaySession *psess, ContainerTracker *ptracker, void *unused)
{
	ptracker->phRef = (RefTo *)OFFSET_PTR(ptracker, ptracker->pMapping->offReference);

	if(IS_HANDLE_ACTIVE(*ptracker->phRef))
	{
		verbose_printf("DB container %s:%s already subscribed\n", ptracker->pMapping->pchName, ptracker->estrID);
		return;
	}

	RefSystem_SetHandleFromString(GlobalTypeToCopyDictionaryName(ptracker->pMapping->globaltype),
		ptracker->estrID, REF_HANDLEPTR(*ptracker->phRef));

	// Special work for some DB containers
	if(ptracker->pMapping->gatewaytype == GW_GLOBALTYPE_LOGIN_ENTITY)
	{
		// Remove everything from the container tracker list and start
		//   fresh with this container.
		eaFindAndRemoveFast(&psess->ppContainers, ptracker);
		session_ReleaseContainers(psess);
		eaPush(&psess->ppContainers, ptracker);
	}
	else if(ptracker->pMapping->gatewaytype == GLOBALTYPE_ENTITYPLAYER)
	{
		// Remove all the other player entities and pets of that entity from the
		//   container tracker list.
		EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
		{
			if(psess->ppContainers[i]->gatewaytype == GLOBALTYPE_ENTITYPLAYER
				&& stricmp(psess->ppContainers[i]->estrID, ptracker->estrID) != 0)
			{
				// This is an entity, but not the one we're loading now.
				int j;
				Entity *pEntity = GET_REF(psess->ppContainers[i]->hEntity);

				if(pEntity)
				{
					// unsubscribe our saved pets
					for(j = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; j >= 0 ; j--)
					{
						char achID[24];
						itoa(pEntity->pSaved->ppOwnedContainers[j]->conID, achID, 10);

						session_ReleaseContainer(psess, GLOBALTYPE_ENTITYSAVEDPET, achID);
					}
				}
				session_ReleaseContainer(psess, psess->ppContainers[i]->gatewaytype, psess->ppContainers[i]->estrID);
			}
		}
		EARRAY_FOREACH_END;
	}
	else if(ptracker->pMapping->gatewaytype == GLOBALTYPE_GUILD)
	{
		EARRAY_FOREACH_REVERSE_BEGIN(psess->ppContainers, i);
		{
			// Remove other guilds and guild projects from the container list
			if((psess->ppContainers[i]->gatewaytype == GLOBALTYPE_GUILD
					|| psess->ppContainers[i]->gatewaytype == GLOBALTYPE_GROUPPROJECTCONTAINERGUILD)
				&& stricmp(psess->ppContainers[i]->estrID, ptracker->estrID) != 0)
			{
				// This is not the container we're loading now.
				session_ReleaseContainer(psess, psess->ppContainers[i]->gatewaytype, psess->ppContainers[i]->estrID);
			}
		}
		EARRAY_FOREACH_END;

		session_GetContainerEx(psess, FindContainerMapping(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), ptracker->estrID, NULL, true);
	}
}

static bool EntityIsCorrectVersion(ContainerTracker *ptracker)
{
	Entity *pEntity = GET_REF(ptracker->hEntity);
	bool bRet = true;

	if(ptracker->gatewaytype != GLOBALTYPE_ENTITYPLAYER || !pEntity)
		return true;

	if(!pEntity)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if(ptracker->u32FixupStart)
	{
		if(pEntity->pSaved->uGameSpecificFixupVersion != (U32)gameSpecificFixup_Version())
		{
			if(ptracker->u32FixupStart + 30 > timeSecondsSince2000())
			{
				// It's been a minute. The user will be told that the need to
				// log in to the game to see their character in Gateway.
				bRet = true;
			}
			else
			{
				// Keep waiting
				bRet = false;
			}
		}
		else
		{
			// Fixup is complete. Yay!
			ptracker->u32FixupStart = 0;
		}
	}
	else if(pEntity->pSaved->uGameSpecificFixupVersion != (U32)gameSpecificFixup_Version())
	{
		bRet = false;
		if(pEntity->pSaved->uGameSpecificFixupVersion > (U32)gameSpecificFixup_Version())
		{
			if(isProductionMode())
			{
				TriggerAlert("ENTITY_HAS_FUTURE_FIXUP", 
					STACK_SPRINTF("Player Entity %u has fixup version %u, which is higher than the game build(%d).  It is likely that the build was rolled back without rolling back the database.", 
					entGetContainerID(pEntity), pEntity->pSaved->uGameSpecificFixupVersion, gameSpecificFixup_Version()),
					ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0);
			}
			else
			{
				ErrorDetailsf("Entity=%u, EntityFixupversion=%u, BuildFixupVersion=%d", entGetContainerID(pEntity), pEntity->pSaved->uGameSpecificFixupVersion, gameSpecificFixup_Version());
				Errorf("Player entity has fixup version which is higher than the game build.");
			}
		}

		ptracker->u32FixupStart = timeSecondsSince2000();

		PERFINFO_AUTO_START("gameSpecificFixup", 1);
		gameSpecificFixup(pEntity);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
	return bRet;
}

bool IsReadyDBContainer(GatewaySession *psess, ContainerTracker *ptracker)
{
	bool bReady = false;

	PERFINFO_AUTO_START_FUNC();

	if(GET_REF(*ptracker->phRef))
	{
		bReady = true;
	}

	if(bReady)
	{
		if(GlobalTypeParent(ptracker->pMapping->globaltype) == GLOBALTYPE_ENTITY)
		{
			Entity *pEntity = GET_REF(ptracker->hEntity); // Can't be NULL, we just tested it via phRef above.

			ptracker->idOwnerAccount = SAFE_MEMBER2(pEntity, pPlayer, accountID);

			if(ptracker->gatewaytype == GLOBALTYPE_ENTITYPLAYER)
			{
				bReady = EntityIsCorrectVersion(ptracker);

				if(bReady)
				{
					int i;
					ContainerMapping *pmapPet = FindContainerMapping(GLOBALTYPE_ENTITYSAVEDPET);

					for(i = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i >= 0 ; i--)
					{
						ContainerTracker *ptrackPet;
						char achID[24];
						itoa(pEntity->pSaved->ppOwnedContainers[i]->conID, achID, 10);

						ptrackPet = session_FindContainerTracker(psess, pmapPet->gatewaytype, achID);
						if(!ptrackPet)
						{
							ptrackPet = session_GetContainerEx(psess, pmapPet, achID, NULL, false);
						}

						if(!IS_HANDLE_ACTIVE(pEntity->pSaved->ppOwnedContainers[i]->hPetRef))
						{
							RefSystem_SetHandleFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),
								achID, REF_HANDLEPTR(pEntity->pSaved->ppOwnedContainers[i]->hPetRef));
						}

						if(ptrackPet)
						{
							bReady = bReady && ptrackPet->bReady;
						}
					}

					if(bReady)
					{
						// Puppets and Entity are ready and all should be the correct version.

						if(ptracker->idOwnerAccount == psess->idAccount && IS_HANDLE_ACTIVE(pEntity->pPlayer->pPlayerAccountData->hTempData) == false)
						{
							COPY_HANDLE(pEntity->pPlayer->pPlayerAccountData->hTempData, psess->hGameAccountData);
						}

						{
							// If this entity is also the login entity, make sure that
							// the login entity is sent before the entity is.
							ContainerTracker *pt = session_FindContainerTracker(psess, GW_GLOBALTYPE_LOGIN_ENTITY, ptracker->estrID);
							if(pt)
							{
								bReady = bReady && (pt->bReady && !pt->bSend);
							}
						}

						if (ptracker->bModified && pEntity)
						{
							ContainerTracker *pGuildTracker = session_FindFirstContainerTrackerForType(psess, GLOBALTYPE_GUILD);
							Guild *pGuild = SAFE_GET_REF(pGuildTracker, hGuild);
							bool bGuildIsMember = pGuild && guild_IsGuildMember(pEntity, pGuild);
							if (bGuildIsMember)
							{
								session_ContainerModified(psess, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pGuildTracker->estrID);
							}
						}
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return bReady;
}

#include "gslGatewaySession_h_ast.c"


/* End of File */
