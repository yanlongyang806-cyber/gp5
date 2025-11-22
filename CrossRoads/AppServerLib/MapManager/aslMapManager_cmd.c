#include "aslMapManager_cmd.h"

#include "UGCProjectCommon.h"

#include "Alerts.h"
#include "file.h"
#include "ServerLib.h"
#include "utilitiesLib.h"
#include "logging.h"
#include "StringUtil.h"
#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "aslMapManagerNewMapTransfer_Private.h"

#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"

SA_RET_NN_VALID static StashTable getPlayableNameSpaceStashByNameSpace(void)
{
	static StashTable table = NULL;

	if(!table)
	{
		table = stashTableCreateWithStringKeys(1, StashDefault);

		resRegisterDictionaryForStashTable("PlayableNameSpaceData", RESCATEGORY_SYSTEM, 0, table, parse_UGCPlayableNameSpaceData);
	}

	return table;
}

static void stashReplaceStruct_Internal(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID void *pValue, SA_PARAM_NN_VALID ParseTable pt[])
{
	void *oldValue;
	char *keyOut;

	if(nullStr(pKey) || !pValue) return;

	if(stashGetKey(pTable, pKey, &keyOut))
	{
		// Remove the pointer and destroy the struct
		if(stashRemovePointer(pTable, pKey, &oldValue) && oldValue)
			StructDestroyVoid(pt, oldValue);

		// Re-use the same estring
		stashAddPointer(pTable, keyOut, StructCloneVoid(pt, pValue), true);
	}
	else
		stashAddPointer(pTable, estrDup(pKey), StructCloneVoid(pt, pValue), true);
}

static void stashRemoveStruct_Internal(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID ParseTable pt[])
{
	void *oldValue;
	char *keyOut;

	if(nullStr(pKey)) return;

	if(stashGetKey(pTable, pKey, &keyOut))
	{
		// Remove the pointer and destroy the struct
		if(stashRemovePointer(pTable, pKey, &oldValue) && oldValue)
			StructDestroyVoid(pt, oldValue);

		estrDestroy(&keyOut);
	}
}

static void stashClearStruct_Internal(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_VALID ParseTable pt[])
{
	void *pValue;

	FOR_EACH_IN_STASHTABLE2(pTable, it)
	{
		char *estrKey = stashElementGetKey(it);

		pValue = stashElementGetPointer(it);
		if(pValue)
			StructDestroyVoid(pt, pValue);

		estrDestroy(&estrKey);
	}
	FOR_EACH_END;

	stashTableClear(pTable);
}

static UGCPlayableNameSpaceData *FindPlayableNameSpaceStashTableByNameSpace(const char *strNameSpace)
{
	if(strNameSpace)
	{
		UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = NULL;

		stashFindPointer(getPlayableNameSpaceStashByNameSpace(), strNameSpace, &pUGCPlayableNameSpaceData);

		return pUGCPlayableNameSpaceData;
	}

	return NULL;
}

static U32 s_LastTimePlayableNameSpacesRecd = 0;

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslMapManager_UpdateNameSpacesPlayable(UGCPlayableNameSpaces *pUGCPlayableNameSpaces, bool bReplace)
{
	if(bReplace)
		stashClearStruct_Internal(getPlayableNameSpaceStashByNameSpace(), parse_UGCPlayableNameSpaceData);

	s_LastTimePlayableNameSpacesRecd = timeSecondsSince2000();

	FOR_EACH_IN_EARRAY_FORWARDS(pUGCPlayableNameSpaces->eaUGCPlayableNameSpaceData, UGCPlayableNameSpaceData, pUGCPlayableNameSpaceData)
	{
		if(pUGCPlayableNameSpaceData->bPlayable)
			stashReplaceStruct_Internal(getPlayableNameSpaceStashByNameSpace(), pUGCPlayableNameSpaceData->strNameSpace, pUGCPlayableNameSpaceData, parse_UGCPlayableNameSpaceData);
		else
			stashRemoveStruct_Internal(getPlayableNameSpaceStashByNameSpace(), pUGCPlayableNameSpaceData->strNameSpace, parse_UGCPlayableNameSpaceData);
	}
	FOR_EACH_END;
}

void aslMapManager_PlayableNameSpace_NormalOperation(void)
{
	if(gConf.bUserContent && (isProductionMode() || ugc_DevMode))
	{
		static U32 startedChecking = 0;

		if(0 == startedChecking)
			startedChecking = timeSecondsSince2000();

		if(timeSecondsSince2000() > startedChecking + UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_DELAY) // start caring
		{
			static U32 lastTimeTriggered = 0;

			if(s_LastTimePlayableNameSpacesRecd == 0 || timeSecondsSince2000() > s_LastTimePlayableNameSpacesRecd + UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_PERIOD)
			{
				if(lastTimeTriggered == 0 || timeSecondsSince2000() > lastTimeTriggered + UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_PERIOD)
				{
					lastTimeTriggered = timeSecondsSince2000();
					TriggerAutoGroupingAlert("UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 12*60*60,
						"The MapManager on shard %s is not receiving playable namespaces from the UGCDataManager. Some UGC projects may be unplayable until this is resolved.", GetShardNameFromShardInfoString());
				}
			}
			else
				lastTimeTriggered = 0;
		}
	}
}

void aslMapManager_PlayableNameSpaceCache_Init(void)
{
	// This command will not actually return anything if the UGCDataManager is also starting up. This is here solely to get playable namespaces again if this MapManager was down for any reason
	// while the UGCDataManager remained up.
	RemoteCommand_Intershard_aslUGCDataManager_GetNameSpacesPlayable(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, GetShardNameFromShardInfoString());
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslMapManagerManager_IsNameSpacePlayable_Return(UGCPlayableNameSpaceDataReturn *pUGCPlayableNameSpaceDataReturn)
{
	SlowRemoteCommandReturn_aslMapManager_IsNameSpacePlayable(pUGCPlayableNameSpaceDataReturn->iCmdID, pUGCPlayableNameSpaceDataReturn->ugcPlayableNameSpaceData.bPlayable);
}

bool aslMapManager_IsNameSpacePlayableHelper(char *pNameSpace)
{
	UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = FindPlayableNameSpaceStashTableByNameSpace(pNameSpace);

	return !!(pUGCPlayableNameSpaceData && pUGCPlayableNameSpaceData->bPlayable);
}

char* estrTransferFromShardName = NULL;
AUTO_CMD_ESTRING(estrTransferFromShardName, TransferFromShardName) ACMD_AUTO_SETTING(Misc, MAPMANAGER);

AUTO_COMMAND_REMOTE_SLOW(bool) ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslMapManager_IsNameSpacePlayable(SlowRemoteCommandID cmdID, char *pNameSpace, const char* strReason, bool bExpectedTrasferFromShardNS)
{
	U32 iContainerID;

	servLog(LOG_UGC, "aslMapManager_IsNameSpacePlayable", "%s", strReason);

	iContainerID = UGCProject_GetContainerIDFromUGCNamespace(pNameSpace);
	if(!iContainerID)
	{
		if(!bExpectedTrasferFromShardNS || (estrTransferFromShardName && !namespaceIsUGCOtherShard(pNameSpace, estrTransferFromShardName)))
		{
			ErrorOrAlert("UGC_BAD_NAMESPACE_IN_MAPMANAGER_ISNAMESPACEPLAYABLE", "A corrupted namespace has made it into aslMapManager_IsNameSpacePlayable, this is bad (%s).  Namespace checked because: %s", pNameSpace, strReason);
		}

		SlowRemoteCommandReturn_aslMapManager_IsNameSpacePlayable(cmdID, false);

		return;
	}

	SlowRemoteCommandReturn_aslMapManager_IsNameSpacePlayable(cmdID, aslMapManager_IsNameSpacePlayableHelper(pNameSpace));
}

AUTO_COMMAND_REMOTE_SLOW(UGCPlayableNameSpaceData *);
void aslMapManager_RequestUGCDataForMission(SlowRemoteCommandID cmdID, const char *strNameSpace)
{
	UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = StructClone(parse_UGCPlayableNameSpaceData, FindPlayableNameSpaceStashTableByNameSpace(strNameSpace));
	if(!pUGCPlayableNameSpaceData)
	{
		pUGCPlayableNameSpaceData = StructCreate(parse_UGCPlayableNameSpaceData);
		pUGCPlayableNameSpaceData->strNameSpace = strdup(strNameSpace);
		pUGCPlayableNameSpaceData->bPlayable = true;
	}

	pUGCPlayableNameSpaceData->bProjectIsFeatured = UGCProject_IsFeaturedWindow(pUGCPlayableNameSpaceData->iFeaturedStartTimestamp, pUGCPlayableNameSpaceData->iFeaturedEndTimestamp,
		false, false);
	pUGCPlayableNameSpaceData->bProjectWasFeatured = UGCProject_IsFeaturedWindow(pUGCPlayableNameSpaceData->iFeaturedStartTimestamp, pUGCPlayableNameSpaceData->iFeaturedEndTimestamp,
		true, false);

	SlowRemoteCommandReturn_aslMapManager_RequestUGCDataForMission(cmdID, pUGCPlayableNameSpaceData);

	StructDestroySafe(parse_UGCPlayableNameSpaceData, &pUGCPlayableNameSpaceData);
}

static U32 PlayerCountForAllInstancesOfMap(const char *mapName)
{
    U32 count = 0;

    GameServerList *gameServerList;
    if (stashFindPointer(sGameServerListsByMapDescription, mapName, &gameServerList))
    {
        if ( gameServerList ) 
        {
            FOR_EACH_PARTITION_IN_LIST(gameServerList, partitionInfo, serverInfo)
            {
                count += (U32)partitionInfo->iNumPlayers;
            }
        }
        FOR_EACH_PARTITION_END
    }

    return count;
}

AUTO_COMMAND_REMOTE;
void aslMapManager_GetPopulationForMapList(GlobalType returnServerType, ContainerID returnServerID, MapNameAndPopulationList *mapList)
{
    int i;

    if ( mapList == NULL )
    {
        return;
    }

    // Update the population numbers in place so that we don't have to do extra allocations or worry about freeing the list.
    for ( i = eaSize(&mapList->eaMapList) - 1; i >= 0; i-- )
    {
        MapNameAndPopulation *mapNameAndPopulation = mapList->eaMapList[i];

        if ( mapNameAndPopulation )
        {
            mapNameAndPopulation->uPlayerCount = PlayerCountForAllInstancesOfMap(mapNameAndPopulation->mapName);
        }
    }

    // Return the list with updated population numbers to the calling server.
    RemoteCommand_GetPopulationForMapList_Return(returnServerType, returnServerID, mapList);
}
