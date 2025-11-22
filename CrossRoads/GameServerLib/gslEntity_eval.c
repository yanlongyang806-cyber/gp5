/***************************************************************************



***************************************************************************/

#include "aiLib.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "Character.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "EntityGrid.h"
#include "EString.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "gslEntity.h"
#include "gslMission.h"
#include "gslPartition.h"
#include "interaction_common.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "PowerModes.h"
#include "url.h"
#include "HttpClient.h"
#include "httpAsync.h"
#include "gslHttpAsync.h"
#include "Player.h"
#include "EntitySavedData.h"
#include "utilitiesLib.h"
#include "accountnet.h"
#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/websrv_h_ast.h"
#include "GlobalTypeEnum.h"

// Use WebSrv for Web Game Events
static bool sbSendWebGameEventsToWebSrv = false;
AUTO_CMD_INT(sbSendWebGameEventsToWebSrv, UseWebSrv) ACMD_CATEGORY(GameServer_Setting);

// ----------------------------------------------------------------------------------
// Expression functions to test player and critter entities
// ----------------------------------------------------------------------------------

// Returns the number of players currently on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(NumPlayersOnMap);
int exprFuncNumPlayersOnMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	return partition_GetPlayerCount(iPartitionIdx);
}


// Determine if a single entity is alive
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntityIsAlive);
U32 exprFuncEntityIsAlive(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) {
		return entIsAlive((*peaEnts[0]));
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "EntityIsAlive : No entities given");
	return 0;
}

// Determine if a single entity is alive
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntityIsNearDeath);
U32 exprFuncEntityIsNearDeath(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) 
	{
		Entity *pEnt = *peaEnts[0];
		return pEnt->pChar && pEnt->pChar->pNearDeath;
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "EntityIsNearDeath : No entities given");
	return 0;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(IsStatusTableMissingAnyLuckyCharmEntity);
U32 exprFuncIsStatusTableMissingAnyLuckyCharmEntity(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ENTARRAY_IN pppOwner, int eLuckyCharmType, bool bCheckFirstOnly)
{
	S32 i;
	Entity *pEnt = NULL, *pOwner = NULL;
	if (peaEnts == NULL || eaSize(peaEnts) <= 0) 
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "IsStatusTableMissingAnyLuckyCharmEntity : No entity is given");
		return false;
	}

	if (pppOwner == NULL || eaSize(pppOwner) <= 0) 
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "IsStatusTableMissingAnyLuckyCharmEntity : No owner is given");
		return false;
	}

	// Get the entity
	pEnt = (*peaEnts)[0];

	// Get the owner
	pOwner = (*pppOwner)[0];

	if (pEnt == NULL || pOwner == NULL)
		return false;

	if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if (pTeamMapValues->eaPetTargetingInfo[i]->eType == eLuckyCharmType)
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pTeamMapValues->eaPetTargetingInfo[i]->erTarget);

					if (pMatchingEnt == NULL)
						continue;
					else
					{
						// See if this guy exists in the status table
						if (!aiStatusFind(pEnt, pEnt->aibase, pMatchingEnt, false))
						{
							return true;
						}

						if (bCheckFirstOnly)
						{
							return false;
						}
					}
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if (pPlayerMapValues->eaPetTargetingInfo[i]->eType == eLuckyCharmType)
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);

					if (pMatchingEnt == NULL)
						continue;
					else
					{
						// See if this guy exists in the status table
						AIStatusTableEntry *pStatus = aiStatusFind(pEnt, pEnt->aibase, pMatchingEnt, false);
						AITeamStatusEntry *teamStatus;

						if (pStatus == NULL || !pStatus->visible)
						{
							return true;
						}

						teamStatus = aiGetTeamStatus(pEnt, pEnt->aibase, pStatus);
						if(!teamStatus || !teamStatus->legalTarget)
						{
							return true;
						}

						if (bCheckFirstOnly)
						{
							return false;
						}
					}


				}
			}
		}
	}

	return false;
}


// Determine if a single entity is in combat
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntityIsInCombat);
U32 exprFuncEntityIsInCombat(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) {
		Entity *pEnt = (*peaEnts)[0];
		if (pEnt->pChar) {
			return character_HasMode(pEnt->pChar,kPowerMode_Combat);
		} else {
			return false;
		}
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "EntityIsInCombat : No entities given");
	return 0;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerIsInCombat);
int exprFuncPlayerIsInCombat(ExprContext* context)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if (pPlayerEnt)
	{
		if (pPlayerEnt->pChar) {
			return character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat);
		} else {
			return false;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerIsAvailable);
U32 exprFuncPlayerIsAvailable(ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) {
		Entity *pEnt = (*peaEnts)[0];

		return (pEnt && pEnt->pChar && !pEnt->pChar->uiTimeCombatExit &&
			!interaction_IsPlayerInteracting(pEnt) &&
			!interaction_IsPlayerInDialog(pEnt));
	}

	return 0;
}

// Gets the Gang ID of ent array passed in.
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetGang);
ExprFuncReturnVal exprFuncGetGang(ExprContext *pContext, ACMD_EXPR_INT_OUT piOutInt, ACMD_EXPR_ENTARRAY_IN peaEntsIn, ACMD_EXPR_ERRSTRING errString)
{
	int iNum = eaSize(peaEntsIn);
	Entity* pEnt;

	if (iNum == 1) {
		pEnt = (*peaEntsIn)[0];

		if (pEnt->pChar) {
			*piOutInt = pEnt->pChar->gangID;
		} else {
			*piOutInt = -1;
		}

		return ExprFuncReturnFinished;
	} else {
		estrPrintf(errString, "GetGang only handles one entity at a time; an array of %d was passed in.", iNum);
		return ExprFuncReturnError;
	}
}


// Sets the Gang ID of all entities in the ent array passed in to <gang>
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SetGang);
void exprFuncSetGang(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEntsIn, int iGang)
{
	int i;
	for(i=eaSize(peaEntsIn)-1; i>=0; --i) {
		Entity *pEnt = (*peaEntsIn)[i];

		if (pEnt->pChar) {
			pEnt->pChar->gangID = iGang;
			entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		}
	}
}


AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(IsMapOwner);
ExprFuncReturnVal exprFuncIsMapOwner(ExprContext *pContext, ACMD_EXPR_INT_OUT pbRet, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errString)
{
	Entity *pEnt;

	(*pbRet) = false;

	if (eaSize(peaEnts)>1) {
		estrPrintf(errString, "Too many entities passed in: %d (1 allowed)", eaSize(peaEnts));
		return ExprFuncReturnError;
	} else if (eaSize(peaEnts) == 0) {
		// This is fine; return false
		return ExprFuncReturnFinished;
	}

	devassert((*peaEnts)); // Fool static check
	pEnt = (*peaEnts)[0];

	if (pEnt) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		if (partition_OwnerIDFromIdx(iPartitionIdx) && (entGetContainerID(pEnt) == partition_OwnerIDFromIdx(iPartitionIdx))) {
			*pbRet = true;
			return ExprFuncReturnFinished;
		}
	}

	return ExprFuncReturnFinished;
}


// ----------------------------------------------------------------------------------
// Getting Entities
// ----------------------------------------------------------------------------------

// Get an entarray of all entities close to a named point
static ExprFuncReturnVal exprFuncGetEntsWithinDistOfPointHelper(ExprContext *pContext,
																ACMD_EXPR_PARTITION partition,
														 Mat4 mPoint, F32 fDist, ACMD_EXPR_ENTARRAY_OUT peaEntsOut,
														 int bAll, int bDead)
{
	static Entity **eaEnts = NULL;
	int i;
	entGridProximityLookupExEArray(partition, mPoint[3], &eaEnts, fDist, 0, 0, NULL);

	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		Entity *pEnt = eaEnts[i];
		if (pEnt && 
			(bDead || entIsAlive(pEnt)) &&
			(bAll || !exprFuncHelperShouldExcludeFromEntArray(pEnt))) {
			eaPush(peaEntsOut, pEnt);
		}
	}

	return ExprFuncReturnFinished;
}


// Get an entarray of all entities close to a named point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfPoint);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfPoint(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_LOC_MAT4_IN mPoint, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, 0, 0);
};


// Get an entarray of all entities close to a named point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfPointAll);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfPointAll(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_LOC_MAT4_IN mPoint, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, 1, 0);
};


// Get an entarray of all entities close to a named point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfPointDeadAll);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfPointDeadAll(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_LOC_MAT4_IN mPoint, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, 1, 1);
};

// Get an entarray of all entities close to another ent
static ExprFuncReturnVal exprFuncGetEntsWithinDistOfEntHelper(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_ENTARRAY_IN peaEntsIn, F32 fDist, int bAll, int bDead)
{
	int iNum = eaSize(peaEntsIn);
	Mat4 mPoint;
	Entity* pEnt;

	if (iNum == 1) {
		Vec3 pos;
		pEnt = (*peaEntsIn)[0];

		identityMat4(mPoint);
		entGetPos(pEnt, pos);

		mPoint[3][0] = pos[0];
		mPoint[3][1] = pos[1];
		mPoint[3][2] = pos[2];

		return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, bAll, bDead);
	} else {
		return ExprFuncReturnError;
	}
}

// Get an entarray of all entities close to another ent
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfEnt);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfEnt(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_ENTARRAY_IN peaEntsIn, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfEntHelper(pContext, partition, peaEntsOut, peaEntsIn, fDist, 0, 0);
}

// Get an entarray of all entities close to another ent
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfEntAll);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfEntAll(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_ENTARRAY_IN peaEntsIn, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfEntHelper(pContext, partition, peaEntsOut, peaEntsIn, fDist, 1, 0);
}

// Get an entarray of all entities close to another ent
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfEntDeadAll);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfEntDeadAll(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_ENTARRAY_IN peaEntsIn, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfEntHelper(pContext, partition, peaEntsOut, peaEntsIn, fDist, 1, 1);
}

// Forces the given entities into a current build by index
AUTO_EXPR_FUNC(ai, encounter_action); 
ExprFuncReturnVal EntityBuildForceCurrentBuild(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN entsIn, S32 buildIndex)
{
	S32 i;
	if(!eaSize(entsIn))
		return ExprFuncReturnFinished;

	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		entity_BuildSetCurrent((*entsIn)[i], buildIndex, false);
	}

	return ExprFuncReturnFinished;
}

// Returns the current build index of the given entity,
// entsIn must only be 1 entity, 
// returns -1 on error or no build 
AUTO_EXPR_FUNC(ai, encounter_action); 
S32 EntityBuildGetCurrentBuildIndex(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	S32 size = eaSize(entsIn);
	if(!size || size != 1)
	{
		// print out error, 
		return -1;
	}
	
	return entity_BuildGetCurrentIndex((*entsIn)[0]);
}


// ----------------------------------------------------------------------------------
// Game Account Tests
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerIsLifetimeSubscription);
int player_FuncPlayerIsLifetimeSubscription(ExprContext *pContext)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	return(entity_LifetimeSubscription(pPlayerEnt));
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerGetDaysSubscribed);
U32 player_FuncPlayerGetDaysSubscribed(ExprContext *pContext)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	return entity_GetDaysSubscribed(pPlayerEnt);
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerIsPressSubscription);
int player_FuncPlayerIsPressSubscription(ExprContext *pContext)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const GameAccountData *pData = entity_GetGameAccount(pPlayerEnt);
	if (pPlayerEnt && pData) {
		return(pData->bPress);
	}
	return 0;
}
