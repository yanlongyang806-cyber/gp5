#include "gslStickerBook.h"

#include "StickerBookCommon.h"

#include "stdtypes.h"
#include "EntityLib.h"
#include "Entity.h"
#include "Player.h"
#include "StringCache.h"
#include "EArray.h"
#include "TransactionOutcomes.h"
#include "AutoTransDefs.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "NotifyEnum.h"
#include "LoggedTransactions.h"
#include "StringCache.h"
#include "inventoryCommon.h"
#include "GameAccountDataCommon.h"
#include "StringUtil.h"
#include "NotifyCommon.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_AutoGen_ClientCmdWrappers.h"

void gslStickerBookUpdate(Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer && GLOBALTYPE_ENTITYPLAYER == pEnt->myEntityType)
	{
		NOCONST(Entity) *pEntNoConst = CONTAINER_NOCONST(Entity, pEnt);
		FOR_EACH_IN_EARRAY(pEntNoConst->pPlayer->eaAstrRecentlyAcquiredStickerBookItems, char, astrItemName)
		{
			bool bCompleted = false;
			StickerBookItemInfo *pStickerBookItemInfo = eaIndexedGetUsingString(&pEnt->pPlayer->eaStickerBookItemInfo, astrItemName);
			if(!pStickerBookItemInfo)
			{
				ItemDef *pItemDef = (ItemDef *)RefSystem_ReferentFromString(g_hItemDict, astrItemName);
				if(pItemDef)
					if(!pItemDef->pRestriction ||
							(itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)
								&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)))
					{
						bCompleted = true;
						AutoTrans_trStickerBookItemCompleted(LoggedTransactions_CreateManagedReturnValEnt("StickerBookItemComplete", pEnt, NULL, NULL),
							GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), astrItemName);
					}
			}
			if(!bCompleted)
				AutoTrans_trStickerBookItemClear(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), astrItemName);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pEntNoConst->pPlayer->eaAstrRecentlyModifiedStickerBookItemSets, char, astrRefString)
		{
			StickerBookItemSet *pStickerBookItemSet = StickerBook_ItemSetGetByRefString(astrRefString);
			bool bCompleted = false;
			StickerBookItemSetInfo *pStickerBookItemSetInfo = eaIndexedGetUsingString(&pEnt->pPlayer->eaStickerBookItemSetInfo, astrRefString);
			if(pStickerBookItemSet && !pStickerBookItemSetInfo)
			{
				if(StickerBook_CountPointsForSet(pStickerBookItemSet, pEnt, /*pbFullCount=*/NULL) == StickerBook_CountTotalPointsForSet(pStickerBookItemSet, pEnt, /*pbFullCount=*/NULL))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

					ItemChangeReason reason = {0};
					inv_FillItemChangeReason(&reason, pEnt, "Internal:StickerBookItemSetComplete", NULL_TO_EMPTY(pStickerBookItemSet->pchRewardTitleItem));

					bCompleted = true;

					AutoTrans_trStickerBookItemSetCompleted(LoggedTransactions_CreateManagedReturnValEnt("StickerBookItemSetComplete", pEnt, NULL, NULL),
						GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), astrRefString, InvBagIDs_Titles, &reason, pExtract);
				}
			}
			if(!bCompleted)
				AutoTrans_trStickerBookItemSetClear(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), astrRefString);
		}
		FOR_EACH_END;
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eastickerbookiteminfo[], .Pplayer.Eaastrrecentlyacquiredstickerbookitems, .Pplayer.Eaastrrecentlymodifiedstickerbookitemsets");
enumTransactionOutcome trStickerBookItemCompleted(ATR_ARGS, NOCONST(Entity)* pEnt, const char *pchItemName)
{
	const char *astrItemName = allocAddString(pchItemName);
	NOCONST(StickerBookItemInfo) *pStickerBookItemInfo;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	pStickerBookItemInfo = StructCreateNoConst(parse_StickerBookItemInfo);
	pStickerBookItemInfo->astrItemDefName = (char *)astrItemName;
	if(eaIndexedPushUsingStringIfPossible(&pEnt->pPlayer->eaStickerBookItemInfo, pchItemName, pStickerBookItemInfo))
	{
		// We are adding this item for the first time.
		StickerBookTrackedItem *pStickerBookTrackedItem = StickerBook_GetTrackedItem(astrItemName);
		if(pStickerBookTrackedItem)
		{
			FOR_EACH_IN_EARRAY(pStickerBookTrackedItem->ppItems, StickerBookItem, pStickerBookItem)
			{
				if(pStickerBookItem->pStickerBookItemSet)
					eaPushUnique(&pEnt->pPlayer->eaAstrRecentlyModifiedStickerBookItemSets, (char *)pStickerBookItem->pStickerBookItemSet->pchRefString);
			}
			FOR_EACH_END;
		}
	}

	eaFindAndRemoveFast(&pEnt->pPlayer->eaAstrRecentlyAcquiredStickerBookItems, astrItemName);

	QueueRemoteCommand_gslStickerBook_SendItemNotifications(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pchItemName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eaastrrecentlyacquiredstickerbookitems");
enumTransactionOutcome trStickerBookItemClear(ATR_ARGS, NOCONST(Entity)* pEnt, const char *pchItemName)
{
	const char *astrItemName = allocAddString(pchItemName);

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	eaFindAndRemoveFast(&pEnt->pPlayer->eaAstrRecentlyAcquiredStickerBookItems, astrItemName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppbuilds, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Eaplayernumericthresholds, .Pplayer.Eaastrrecentlyacquiredstickerbookitems, .Pplayer.Eastickerbookitemsetinfo[], .Pplayer.Eaastrrecentlymodifiedstickerbookitemsets");
enumTransactionOutcome trStickerBookItemSetCompleted(ATR_ARGS, NOCONST(Entity)* pEnt, const char *pchRefString, int iBagID, ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	const char *astrRefString = allocAddString(pchRefString);
	NOCONST(StickerBookItemSetInfo) *pStickerBookItemSetInfo;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	if(InvBagIDs_Titles != iBagID)
		return TRANSACTION_OUTCOME_FAILURE;

	pStickerBookItemSetInfo = StructCreateNoConst(parse_StickerBookItemSetInfo);
	pStickerBookItemSetInfo->astrStickerBookItemSetRef = (char *)astrRefString;
	if(eaIndexedPushUsingStringIfPossible(&pEnt->pPlayer->eaStickerBookItemSetInfo, pchRefString, pStickerBookItemSetInfo))
	{
		StickerBookItemSet *pStickerBookItemSet = StickerBook_ItemSetGetByRefString(pchRefString);
		if(pStickerBookItemSet)
			if(pStickerBookItemSet->pchRewardTitleItem && pStickerBookItemSet->pchRewardTitleItem[0])
				inv_ent_trh_AddItemFromDef(ATR_PASS_ARGS, pEnt, NULL, iBagID, -1, pStickerBookItemSet->pchRewardTitleItem, 1, 0, NULL, ItemAdd_UseOverflow, pReason, pExtract);
	}

	eaFindAndRemoveFast(&pEnt->pPlayer->eaAstrRecentlyModifiedStickerBookItemSets, astrRefString);

	QueueRemoteCommand_gslStickerBook_SendItemSetNotifications(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pchRefString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eaastrrecentlymodifiedstickerbookitemsets");
enumTransactionOutcome trStickerBookItemSetClear(ATR_ARGS, NOCONST(Entity)* pEnt, const char *pchRefString)
{
	const char *astrRefString = allocAddString(pchRefString);

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	eaFindAndRemoveFast(&pEnt->pPlayer->eaAstrRecentlyModifiedStickerBookItemSets, astrRefString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

extern const char *g_PlayerVarName;

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerStickerBookPoints);
int gslStickerBookPlayerPoints(ExprContext *pContext)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pPlayerEnt)
		return StickerBook_CountPoints(pPlayerEnt, /*pbFullCount=*/NULL);
	return 0;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal gslStickerBookPlayerPointsForCollectionType_StaticCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_ENUM(StickerBookCollectionType) const char *typeName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	StickerBookCollectionType type = StaticDefineIntGetInt(StickerBookCollectionTypeEnum, typeName);
	if(0 == type)
	{
		estrPrintf(errEstr, "StickerBook Collection type is invalid: '%s'", NULL_TO_EMPTY(typeName));
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerStickerBookPointsForCollectionType) ACMD_EXPR_STATIC_CHECK(gslStickerBookPlayerPointsForCollectionType_StaticCheck);
ExprFuncReturnVal gslStickerBookPlayerPointsForCollectionType(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_ENUM(StickerBookCollectionType) const char *typeName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	StickerBookCollectionType type = StaticDefineIntGetInt(StickerBookCollectionTypeEnum, typeName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(!pPlayerEnt)
	{
		*points = 0;
		return ExprFuncReturnFinished;
	}
	if(0 == type)
	{
		estrPrintf(errEstr, "StickerBook Collection type is invalid: '%s'", NULL_TO_EMPTY(typeName));
		return ExprFuncReturnError;
	}
	*points = StickerBook_CountPointsForCollectionType(type, pPlayerEnt, /*pbFullCount=*/NULL);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal gslStickerBookPlayerPointsForCollection_StaticCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_RES_DICT(StickerBook) const char *pchCollectionName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	StickerBookCollection *pStickerBookCollection = pchCollectionName ? StickerBook_GetCollection(pchCollectionName) : NULL;
	if(!pStickerBookCollection)
	{
		estrPrintf(errEstr, "StickerBook Collection does not exist: '%s'", NULL_TO_EMPTY(pchCollectionName));
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerStickerBookPointsForCollection) ACMD_EXPR_STATIC_CHECK(gslStickerBookPlayerPointsForCollection_StaticCheck);
ExprFuncReturnVal gslStickerBookPlayerPointsForCollection(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_RES_DICT(StickerBook) const char *pchCollectionName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	StickerBookCollection *pStickerBookCollection = pchCollectionName ? StickerBook_GetCollection(pchCollectionName) : NULL;
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(!pPlayerEnt)
	{
		*points = 0;
		return ExprFuncReturnFinished;
	}
	if(!pStickerBookCollection)
	{
		estrPrintf(errEstr, "StickerBook Collection does not exist: '%s'", NULL_TO_EMPTY(pchCollectionName));
		return ExprFuncReturnError;
	}
	*points = StickerBook_CountPointsForCollection(pStickerBookCollection, pPlayerEnt, /*pbFullCount=*/NULL);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal gslStickerBookPlayerPointsForSet_StaticCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_RES_DICT(StickerBook) const char *pchCollectionName, const char *pchSetName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	const char *astrSetName = allocAddString(NULL_TO_EMPTY(pchSetName));
	StickerBookItemSet *pStickerBookItemSet = NULL;
	StickerBookCollection *pStickerBookCollection = pchCollectionName ? StickerBook_GetCollection(pchCollectionName) : NULL;
	if(!pStickerBookCollection)
	{
		estrPrintf(errEstr, "StickerBook Collection does not exist: '%s'", NULL_TO_EMPTY(pchCollectionName));
		return ExprFuncReturnError;
	}
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSetThis)
	{
		if(astrSetName == pStickerBookItemSetThis->pchName)
		{
			pStickerBookItemSet = pStickerBookItemSetThis;
			break;
		}
	}
	FOR_EACH_END;
	if(!pStickerBookItemSet)
	{
		estrPrintf(errEstr, "StickerBook Collection Set does not exist in Collection '%s': '%s'", pchCollectionName, astrSetName);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerStickerBookPointsForSet) ACMD_EXPR_STATIC_CHECK(gslStickerBookPlayerPointsForSet_StaticCheck);
ExprFuncReturnVal gslStickerBookPlayerPointsForSet(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_RES_DICT(StickerBook) const char *pchCollectionName, const char *pchSetName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	const char *astrSetName = allocAddString(NULL_TO_EMPTY(pchSetName));
	StickerBookItemSet *pStickerBookItemSet = NULL;
	StickerBookCollection *pStickerBookCollection = pchCollectionName ? StickerBook_GetCollection(pchCollectionName) : NULL;
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(!pPlayerEnt)
	{
		*points = 0;
		return ExprFuncReturnFinished;
	}
	if(!pStickerBookCollection)
	{
		estrPrintf(errEstr, "StickerBook Collection does not exist: '%s'", NULL_TO_EMPTY(pchCollectionName));
		return ExprFuncReturnError;
	}
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSetThis)
	{
		if(astrSetName == pStickerBookItemSetThis->pchName)
		{
			pStickerBookItemSet = pStickerBookItemSetThis;
			break;
		}
	}
	FOR_EACH_END;
	if(!pStickerBookItemSet)
	{
		estrPrintf(errEstr, "StickerBook Collection Set does not exist in Collection '%s': '%s'", pchCollectionName, astrSetName);
		return ExprFuncReturnError;
	}
	*points = StickerBook_CountPointsForSet(pStickerBookItemSet, pPlayerEnt, /*pbFullCount=*/NULL);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal gslStickerBookPlayerPointsForLocation_StaticCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_RES_DICT(StickerBook) const char *pchCollectionName, const char *pchLocationName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	const char *astrLocationName = allocAddString(NULL_TO_EMPTY(pchLocationName));
	StickerBookItemLocation *pStickerBookItemLocation = NULL;
	StickerBookCollection *pStickerBookCollection = pchCollectionName ? StickerBook_GetCollection(pchCollectionName) : NULL;
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(!pPlayerEnt)
		return ExprFuncReturnFinished;
	if(!pStickerBookCollection)
	{
		estrPrintf(errEstr, "StickerBook Collection does not exist: '%s'", NULL_TO_EMPTY(pchCollectionName));
		return ExprFuncReturnError;
	}
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocationThis)
	{
		if(astrLocationName == pStickerBookItemLocationThis->pchName)
		{
			pStickerBookItemLocation = pStickerBookItemLocationThis;
			break;
		}
	}
	FOR_EACH_END;
	if(!pStickerBookItemLocation)
	{
		estrPrintf(errEstr, "StickerBook Collection Location does not exist in Collection '%s': '%s'", pchCollectionName, astrLocationName);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerStickerBookPointsForLocation) ACMD_EXPR_STATIC_CHECK(gslStickerBookPlayerPointsForLocation_StaticCheck);
ExprFuncReturnVal gslStickerBookPlayerPointsForLocation(ExprContext *pContext, ACMD_EXPR_INT_OUT points,
	ACMD_EXPR_RES_DICT(StickerBook) const char *pchCollectionName, const char *pchLocationName,
	ACMD_EXPR_ERRSTRING errEstr)
{
	const char *astrLocationName = allocAddString(NULL_TO_EMPTY(pchLocationName));
	StickerBookItemLocation *pStickerBookItemLocation = NULL;
	StickerBookCollection *pStickerBookCollection = pchCollectionName ? StickerBook_GetCollection(pchCollectionName) : NULL;
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(!pPlayerEnt)
	{
		*points = 0;
		return ExprFuncReturnFinished;
	}
	if(!pStickerBookCollection)
	{
		estrPrintf(errEstr, "StickerBook Collection does not exist: '%s'", NULL_TO_EMPTY(pchCollectionName));
		return ExprFuncReturnError;
	}
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocationThis)
	{
		if(astrLocationName == pStickerBookItemLocationThis->pchName)
		{
			pStickerBookItemLocation = pStickerBookItemLocationThis;
			break;
		}
	}
	FOR_EACH_END;
	if(!pStickerBookItemLocation)
	{
		estrPrintf(errEstr, "StickerBook Collection Location does not exist in Collection '%s': '%s'", pchCollectionName, astrLocationName);
		return ExprFuncReturnError;
	}
	*points = StickerBook_CountPointsForLocation(pStickerBookItemLocation, pPlayerEnt, /*pbFullCount=*/NULL);
	return ExprFuncReturnFinished;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(9);
void StickerBook_Reset(Entity *pEntity)
{
	if(pEntity && pEntity->pPlayer && GLOBALTYPE_ENTITYPLAYER == pEntity->myEntityType)
	{
		AutoTrans_trStickerBookReset(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEntity));
		StructDestroySafe(parse_StickerBookPointCache, &pEntity->pPlayer->pStickerBookPointCache);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eastickerbookiteminfo, .Pplayer.Eastickerbookitemsetinfo, .Pplayer.Eaastrrecentlyacquiredstickerbookitems, .Pplayer.Eaastrrecentlymodifiedstickerbookitemsets");
enumTransactionOutcome trStickerBookReset(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	eaClearStructNoConst(&pEnt->pPlayer->eaStickerBookItemInfo, parse_StickerBookItemInfo);
	eaClearStructNoConst(&pEnt->pPlayer->eaStickerBookItemSetInfo, parse_StickerBookItemSetInfo);
	eaClear(&pEnt->pPlayer->eaAstrRecentlyAcquiredStickerBookItems);
	eaClear(&pEnt->pPlayer->eaAstrRecentlyModifiedStickerBookItemSets);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void gslStickerBook_SetHasUnviewedChanges(Entity *pEnt, bool bHasUnviewedChanges)
{
	if (SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		pEnt->pPlayer->pUI->uiStickerBookHasUnviewedChanges = bHasUnviewedChanges;
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, false);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND_REMOTE;
void gslStickerBook_SendItemNotifications(const char *pcItemName, CmdContext *pContext)
{
	if(pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pcItemName);

		if(pEnt && pItemDef)
		{
			StickerBookTrackedItem *pStickerBookTrackedItem = StickerBook_GetTrackedItem(allocAddString(pcItemName));
			if(pStickerBookTrackedItem)
			{
				FOR_EACH_IN_EARRAY(pStickerBookTrackedItem->ppItems, StickerBookItem, pStickerBookItem)
				{
					StickerBookItemSet *pStickerBookItemSet = pStickerBookItem->pStickerBookItemSet;
					StickerBookCollection *pStickerBookCollection = pStickerBookItemSet ? pStickerBookItemSet->pStickerBookCollection : NULL;
					if(pStickerBookItemSet && pStickerBookCollection)
					{
						char *estrBuffer = NULL;
						estrStackCreate(&estrBuffer);

						entFormatGameMessageKey(pEnt, &estrBuffer, "StickerBook.ItemAcquired",
							STRFMT_ENTITY(pEnt),
							STRFMT_ITEMDEF(pItemDef),
							STRFMT_DISPLAYMESSAGE("ItemName", pItemDef->displayNameMsg),
							STRFMT_DISPLAYMESSAGE("StickerBookItemSetName", pStickerBookItem->pStickerBookItemSet->msgDisplayName),
							STRFMT_INT("Points", pStickerBookItem->iPoints),
							STRFMT_END);

						if(estrBuffer && estrBuffer[0])
						{
							notify_NotifySendWithTagAndValue(pEnt, kNotifyType_StickerBookItemGranted, estrBuffer, pStickerBookItemSet->pchRefString, pItemDef->pchName, pStickerBookItem->iPoints);

							gslStickerBook_SetHasUnviewedChanges(pEnt, true);
						}

						estrDestroy(&estrBuffer);
					}
				}
				FOR_EACH_END;
			}
		}
	}
}

AUTO_COMMAND_REMOTE;
void gslStickerBook_SendItemSetNotifications(const char *pchRefString, CmdContext *pContext)
{
	if(pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if(pEnt)
		{
			StickerBookItemSet *pStickerBookItemSet = StickerBook_ItemSetGetByRefString(pchRefString);
			if(pStickerBookItemSet)
			{
				char *estrBuffer = NULL;
				estrStackCreate(&estrBuffer);

				entFormatGameMessageKey(pEnt, &estrBuffer, "StickerBook.SetComplete",
					STRFMT_ENTITY(pEnt),
					STRFMT_DISPLAYMESSAGE("StickerBookItemSetName", pStickerBookItemSet->msgDisplayName),
					STRFMT_INT("Points", pStickerBookItemSet->iPoints),
					STRFMT_END);

				if(estrBuffer && estrBuffer[0])
				{
					notify_NotifySendWithTagAndValue(pEnt, kNotifyType_StickerBookSetComplete, estrBuffer, pStickerBookItemSet->pchRefString, NULL, pStickerBookItemSet->iPoints);

					gslStickerBook_SetHasUnviewedChanges(pEnt, true);
				}

				estrDestroy(&estrBuffer);
			}
		}
	}
}
