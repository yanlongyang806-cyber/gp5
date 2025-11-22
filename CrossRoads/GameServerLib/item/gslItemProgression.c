#include "Entity.h"
#include "itemCommon.h"
#include "itemProgressionCommon.h"
#include "rewardCommon.h"
#include "ResourceManager.h"
#include "rand.h"
#include "error.h"
#include "GameAccountDataCommon.h"
#include "GameAccountData\GameAccountData.h"
#include "inventoryCommon.h"
#include "LoggedTransactions.h"
#include "entitylib.h"
#include "Autogen/Entity_h_ast.h"
#include "Autogen/itemCommon_h_ast.h"
#include "Autogen/itemProgressionCommon_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


AUTO_TRANS_HELPER;
F32 itemProgression_trh_RollCritMultiplier(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	if (pDef)
	{
		ItemProgressionDef* pProgressionDef = GET_REF(pDef->hProgressionDef);
		U32 uWeightSum = 0;
		U32 uRolledWeight;
		int i;
		for (i = 0; i < eaSize(&pProgressionDef->eaCritWeights); i++)
		{
			uWeightSum += pProgressionDef->eaCritWeights[i]->uWeight;
		}
		uRolledWeight = randomPositiveF32() * uWeightSum;
		uWeightSum = 0;
		for(i = 0; i < eaSize(&pProgressionDef->eaCritWeights); i++)
		{
			uWeightSum += pProgressionDef->eaCritWeights[i]->uWeight;
			if(uWeightSum >= uRolledWeight)
				return pProgressionDef->eaCritWeights[i]->fMult;
		}
	}
	return 1.0;
}

/*
	Determines success or failure of an evolution depending on the item and slotted ward.

	If this function returns false, the transaction should succeed, consume your catalysts, ward, and numerics, but not give you anything in return. Neener neener.
*/
AUTO_TRANS_HELPER;
bool itemProgression_trh_RollEvoSuccess(ATH_ARG NOCONST(Item)* pItem, ATH_ARG NOCONST(Item)* pWard)
{
	ItemProgressionTierDef* pTier = itemProgression_trh_GetCurrentTier(pItem);
	ItemDef* pWardDef = SAFE_GET_REF(pWard, hItem);
	if (pTier)
	{
		U32 uTotalSuccessChance = pTier->uBaseRankUpChance;
		uTotalSuccessChance += pWardDef ? pWardDef->uProgressionEvoSuccessBonus : 0;
		if ((randomU32() % 100) < uTotalSuccessChance)
			return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
void itemProgression_trh_AddXP(ATH_ARG NOCONST(Item)* pItem, U32 uXP)
{
	NOCONST(AlgoItemProps)* pProps = item_trh_GetOrCreateAlgoProperties(pItem);
	if (pProps->uProgressionXP == 0)
	{
		pProps->uProgressionXP = itemProgression_trh_GetXPRequiredForLevel(pItem, 1);
	}

	pProps->uProgressionXP += uXP;
	pProps->uProgressionLevel = itemProgression_trh_CalculateLevelFromXP(pItem, true);

}

AUTO_TRANS_HELPER;
enumTransactionOutcome itemProgression_trh_FeedItem(ATH_ARG NOCONST(Item)* pDst, ATH_ARG NOCONST(Item)** eaFood, ItemProgressionUILastResult* pResult)
{
	if (NONNULL(pDst) && eaSize(&eaFood) > 0)
	{
		int i;
		U32 uXPTotal = 0;
		U32 uOldLevel = 0;

		pResult->eType = kItemProgressionUIResultType_Feed;
		eaSetSizeStruct(&pResult->eaFoodResults, parse_ItemProgressionUILastFoodResult, eaSize(&eaFood));

		for (i = 0; i < eaSize(&eaFood); i++)
		{
			U32 uFoodXP = itemProgression_trh_GetFoodXPValue(eaFood[i]);
			bool bCritEligible = false;
			F32 fFoodEfficiency = itemProgressionDef_trh_CalculateFoodMultiplier(pDst, eaFood[i], &bCritEligible);
			F32 fCrit = bCritEligible ? itemProgression_trh_RollCritMultiplier(pDst) : 1.0;
			
			uFoodXP *= fFoodEfficiency;
			uFoodXP *= fCrit;

			pResult->eaFoodResults[i]->fCritMult = fCrit;
			pResult->eaFoodResults[i]->uNetXP = uFoodXP;

			uXPTotal += uFoodXP;
		}

		if (uXPTotal == 0)
			return TRANSACTION_OUTCOME_FAILURE;

		uOldLevel = itemProgression_trh_GetLevel(pDst);
		//add XP
		itemProgression_trh_AddXP(pDst, uXPTotal);

		if (itemProgression_trh_GetLevel(pDst) > uOldLevel)
			pResult->bLevelup = true;

		//refresh powers array (callback)
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
bool itemProgression_trh_ConsumeCatalystItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Item)* pTarget, const ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	if (NONNULL(pTarget))
	{
		ItemDef* pDef = GET_REF(pTarget->hItem);
		ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
		ItemProgressionTierDef* pTier = itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pTarget), 0);
		int i, j;
		for (i = 0; i < eaSize(&pTier->eaCatalysts); i++)
		{
			ItemDef* pReq = ItemProgression_trh_GetCatalystItemDef(pTarget, i);
			if (!IS_HANDLE_ACTIVE(pReq->hProgressionDef))
			{
				//for items that don't gain XP we just do a simple bulk remove
				if (!inv_trh_FindItemCountByDefName(ATR_PASS_ARGS, pEnt, pReq->pchName, pTier->eaCatalysts[i]->iNumRequired, true, pReason, pExtract))
					return false;
			}
			else
			{
				//for items that gain XP we need to sort them and consume the lowest-XP items first.
				ItemProgressionCatalystSortData** eaSortData = NULL;
				int iNumRequired = pTier->eaCatalysts[i]->iNumRequired;

				itemProgression_trh_GetCatalystItemsSortedByXP(ATR_PASS_ARGS, pEnt, pTarget, pReq, &eaSortData);
				for (j = 0; j < eaSize(&eaSortData); j++)
				{
					NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, eaSortData[j]->iCatalystBag, pExtract);
					int iNumToRemove = min(inv_bag_trh_GetSlotItemCount(ATR_PASS_ARGS, pBag, eaSortData[j]->iCatalystSlot), iNumRequired);
					NOCONST(Item)* pRemoved = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, false, pBag, eaSortData[j]->iCatalystSlot, iNumToRemove, false, pReason);
					StructDestroyNoConst(parse_Item, pRemoved);
					iNumRequired -= iNumToRemove;
					if (iNumRequired <= 0)
						break;
				}

				eaDestroyStruct(&eaSortData, parse_ItemProgressionCatalystSortData);
				//didn't find enough
				if (iNumRequired > 0)
					return false;
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome itemProgression_trh_EvolveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Item)* pDst, ATH_ARG NOCONST(Item)* pWard, ItemProgressionUILastResult* pResult, const ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	if (NONNULL(pDst))
	{
		NOCONST(AlgoItemProps)* pProps = item_trh_GetOrCreateAlgoProperties(pDst);
		char* estrPattern = NULL;
		char* estrNewDefName = NULL;
		ItemDef* pDef = SAFE_GET_REF(pDst, hItem);
		ItemDef* pNewDef = NULL;
		ItemDef* pWardDef = SAFE_GET_REF(pWard, hItem);
		ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
		ItemProgressionTierDef* pCurrentTier = itemProgression_trh_GetCurrentTier(pDst);
		ItemProgressionTierDef* pNextTier = itemProgression_trh_GetNextTier(pDst);

		pResult->eType = kItemProgressionUIResultType_Evo;

		//ensure pointers are valid and our ward is the correct itemtype
		if (!pDst || !pCurrentTier || !pNextTier || (pWardDef && pWardDef->eType != kItemType_UpgradeModifier))
			return TRANSACTION_OUTCOME_FAILURE;

		//Verify eligibility
		if (!itemProgression_trh_ReadyToEvo(pDst))
			return TRANSACTION_OUTCOME_FAILURE;
	
		//Roll for success
		if (!itemProgression_trh_RollEvoSuccess(pDst, pWard))
		{
			//If we "Failed" the evo, the transaction is still a success so catalysts get properly destroyed.
			pResult->bEvoSucceeded = false;

			//remove catalysts if we didn't have insurance.
			if (!pWardDef || !pWardDef->bProgressionEvoInsurance)
			{
				if (!itemProgression_trh_ConsumeCatalystItems(ATR_PASS_ARGS, pEnt, pDst, pReason, pExtract))
					return TRANSACTION_OUTCOME_FAILURE;
			}
			return TRANSACTION_OUTCOME_SUCCESS;
		}

		//Charge AD
		if (pProgDef->pchEvolutionADCostNumeric && pCurrentTier->iEvolutionADCost && !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pProgDef->pchEvolutionADCostNumeric, -pCurrentTier->iEvolutionADCost, pReason))
			return TRANSACTION_OUTCOME_FAILURE;

		//Remove all catalysts
		if (!itemProgression_trh_ConsumeCatalystItems(ATR_PASS_ARGS, pEnt, pDst, pReason, pExtract))
			return TRANSACTION_OUTCOME_FAILURE;

		pResult->bEvoSucceeded = true;
		//Swap out itemdef
		if (!itemProgression_trh_GetItemDefNamePattern(pDst, &estrPattern))
			return TRANSACTION_OUTCOME_FAILURE;

		estrPrintf(&estrNewDefName, FORMAT_OK(estrPattern), pNextTier->iIndex);

		pNewDef = RefSystem_ReferentFromString(g_hItemDict, estrNewDefName);

		if (!pNewDef)
		{
			ErrorFilenamef(pDef->pchFileName, "Item was supposed to advance to the next progression tier, but the new item def %s doesn't exist.", estrNewDefName);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		SET_HANDLE_FROM_STRING(g_hItemDict, estrNewDefName, pDst->hItem);

		//Change level
		pProps->uProgressionLevel++;
		pProps->uProgressionLevel = itemProgression_trh_CalculateLevelFromXP(pDst, true);
		
		//Have to update algoprops based off new def's level/quality.
		pProps->Quality = pNewDef->Quality;
		pProps->Level_UseAccessor = pNewDef->iLevel;
		pProps->MinLevel_UseAccessor = pNewDef->pRestriction ? pNewDef->pRestriction->iMinLevel : 0;

		//Regenerate powers
		item_trh_FixupPowers(pDst);

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER;
bool itemProgression_trh_PopulateFoodItemsFromList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, NOCONST(Item)* pDst, ItemProgressionCatalystArgumentList* pList, NOCONST(Item)*** peaItemsOut, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	int i;

	if (!pList || !pDst)
		return false;
	for (i = 0; i < eaSize(&pList->eaArgs); i++)
	{
		NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, pList->eaArgs[i]->iCatalystBag, pExtract);
		NOCONST(Item)* pItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, true, pBag, pList->eaArgs[i]->iCatalystSlot, 1, false, pReason);

		if (!pItem || pItem->id == pDst->id)
			return false;

		eaPush(peaItemsOut, pItem);
	}
	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".pInventoryV2.ppInventoryBags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome itemProgression_tr_FeedItem(ATR_ARGS, NOCONST(Entity)* pEnt, int iDstBag, int iDstSlot, ItemProgressionCatalystArgumentList* pList, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	NOCONST(Item)** eaFoodItems = NULL;
	NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iDstBag, pExtract);
	NOCONST(Item)* pDstItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pDstBag, iDstSlot);
	bool bSplitStack = false;
	ItemProgressionUILastResult* pResult = NULL;
	enumTransactionOutcome eRet = TRANSACTION_OUTCOME_FAILURE;

	//Can't feed items that are waiting to evolve.
	if (itemProgression_trh_ReadyToEvo(pDstItem))
	{
		goto itemProgression_FeedItemExit;
	}

	if (!itemProgression_trh_PopulateFoodItemsFromList(ATR_PASS_ARGS, pEnt, pDstItem, pList, &eaFoodItems, pReason, pExtract))
	{
		goto itemProgression_FeedItemExit;
	}

	if (pDstItem->count > 1)
	{
		Errorf("itemProgression_tr_FeedItem() called on a stacked target item. Use itemProgression_tr_FeedItemStack() instead.");

		goto itemProgression_FeedItemExit;
	}

	pResult = StructCreate(parse_ItemProgressionUILastResult);

	if (itemProgression_trh_FeedItem(pDstItem, eaFoodItems, pResult) == TRANSACTION_OUTCOME_FAILURE)
	{
		goto itemProgression_FeedItemExit;
	}

	QueueRemoteCommand_itemProgression_ForwardLastResultToClient(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pEnt->myContainerID, pResult);

	eRet = TRANSACTION_OUTCOME_SUCCESS;

itemProgression_FeedItemExit:

	if (pResult)
		StructDestroy(parse_ItemProgressionUILastResult, pResult);

	if (eaFoodItems)
		eaDestroyStructNoConst(&eaFoodItems, parse_Item);

	return eRet;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".pInventoryV2.ppInventoryBags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Eaastrrecentlyacquiredstickerbookitems, .Pplayer.Eaplayernumericthresholds, .Psaved.Pscpdata.Isummonedscp");
enumTransactionOutcome itemProgression_tr_FeedItemStack(ATR_ARGS, NOCONST(Entity)* pEnt, int iDstBag, int iDstSlot, ItemProgressionCatalystArgumentList* pList, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	NOCONST(Item)** eaFoodItems = NULL;
	NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iDstBag, pExtract);
	NOCONST(Item)* pStack = inv_bag_trh_GetItem(ATR_PASS_ARGS, pDstBag, iDstSlot);
	NOCONST(Item)* pDstItem = NULL;
	bool bSplitStack = false;
	ItemProgressionUILastResult* pResult = NULL;
	U64* ea64IDs = NULL;
	enumTransactionOutcome eRet = TRANSACTION_OUTCOME_FAILURE;

	if (pStack->count <= 1)
	{
		Errorf("itemProgression_tr_FeedItemStack() called with a target item that isn't stacked.");

		goto itemProgression_FeedItemStackExit;
	}

	//Can't feed items that are waiting to evolve.
	if (itemProgression_trh_ReadyToEvo(pStack))
	{
		goto itemProgression_FeedItemStackExit;
	}

	if (!itemProgression_trh_PopulateFoodItemsFromList(ATR_PASS_ARGS, pEnt, pStack, pList, &eaFoodItems, pReason, pExtract))
	{
		goto itemProgression_FeedItemStackExit;
	}

	pDstItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, true, pDstBag, iDstSlot, 1, false, NULL);
	
	if (!pDstItem)
	{
		Errorf("itemProgression_tr_FeedItemStack() couldn't find a target item after removing all food.");

		goto itemProgression_FeedItemStackExit;
	}

	pResult = StructCreate(parse_ItemProgressionUILastResult);

	if (itemProgression_trh_FeedItem(pDstItem, eaFoodItems, pResult) == TRANSACTION_OUTCOME_FAILURE)
	{
		goto itemProgression_FeedItemStackExit;
	}

	{
		NOCONST(Item)* pClone = StructCloneNoConst(parse_Item, pDstItem);
		if (!inv_ent_trh_AddItem(ATR_PASS_ARGS, pEnt, NULL, iDstBag, -1, pClone, 0, &ea64IDs, NULL, pExtract))
		{
			pClone = StructCloneNoConst(parse_Item, pDstItem);
			//attempt to add to any slot in inventory if the original bag was full
			if (!inv_ent_trh_AddItem(ATR_PASS_ARGS, pEnt, NULL, InvBagIDs_Inventory, -1, pClone, 0, &ea64IDs, NULL, pExtract))
			{
				goto itemProgression_FeedItemStackExit;
			}
		}
	}

	if (ea64IDs && ea64IDs[0])
		pResult->u64NewTargetItemID = ea64IDs[0];

	QueueRemoteCommand_itemProgression_ForwardLastResultToClient(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pEnt->myContainerID, pResult);

	eRet = TRANSACTION_OUTCOME_SUCCESS;

itemProgression_FeedItemStackExit:

	if (pDstItem)
		StructDestroyNoConst(parse_Item, pDstItem);

	if (pResult)
		StructDestroy(parse_ItemProgressionUILastResult, pResult);

	if (eaFoodItems)
		eaDestroyStructNoConst(&eaFoodItems, parse_Item);

	return eRet;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".pInventoryV2.ppInventoryBags, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome itemProgression_tr_FeedSlottedGem(ATR_ARGS, NOCONST(Entity)* pEnt, int iHolderBag, int iHolderSlot, int iHolderGemSlot, ItemProgressionCatalystArgumentList* pList, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	NOCONST(Item)** eaFoodItems = NULL;
	NOCONST(InventoryBag)* pHolderBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iHolderBag, pExtract);
	NOCONST(Item)* pHolderItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pHolderBag, iHolderSlot);
	NOCONST(Item)* pGemItem = NULL;
	ItemProgressionUILastResult* pResult = NULL;
	enumTransactionOutcome eRet = TRANSACTION_OUTCOME_FAILURE;

	if (!itemProgression_trh_PopulateFoodItemsFromList(ATR_PASS_ARGS, pEnt, pHolderItem, pList, &eaFoodItems, pReason, pExtract))
	{
		goto itemProgression_FeedSlottedGemExit;
	}

	//remove gem
	if (!inv_trh_RemoveGemmedItem(ATR_PASS_ARGS, pEnt, pHolderItem, iHolderGemSlot, &pGemItem))
	{
		goto itemProgression_FeedSlottedGemExit;
	}

	pResult = StructCreate(parse_ItemProgressionUILastResult);

	//do feeding
	if (!pGemItem || itemProgression_trh_FeedItem(pGemItem, eaFoodItems, pResult) == TRANSACTION_OUTCOME_FAILURE)
	{
		goto itemProgression_FeedSlottedGemExit;
	}

	//re-add gem
	if (!inv_trh_GemItem(pEnt, pHolderItem, pGemItem, iHolderGemSlot))
	{
		goto itemProgression_FeedSlottedGemExit;
	}

	item_trh_FixupPowers(pHolderItem);

	QueueRemoteCommand_itemProgression_ForwardLastResultToClient(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pEnt->myContainerID, pResult);

	eRet = TRANSACTION_OUTCOME_SUCCESS;

itemProgression_FeedSlottedGemExit:

	if (pResult)
		StructDestroy(parse_ItemProgressionUILastResult, pResult);

	if (eaFoodItems)
		eaDestroyStructNoConst(&eaFoodItems, parse_Item);

	return eRet;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".pInventoryV2.ppInventoryBags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pinventoryv2.Pplitebags, .Pplayer.Eaplayernumericthresholds");
enumTransactionOutcome itemProgression_tr_EvoItem(ATR_ARGS, NOCONST(Entity)* pEnt, int iDstBag, int iDstSlot, int iWardBag, int iWardSlot, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iDstBag, pExtract);
	NOCONST(InventoryBag)* pWardBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iWardBag, pExtract);
	NOCONST(Item)* pDstItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pDstBag, iDstSlot);
	NOCONST(Item)* pWardItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, false, pWardBag, iWardSlot, 1, false, pReason);
	ItemProgressionUILastResult* pResult = NULL;

	pResult = StructCreate(parse_ItemProgressionUILastResult);

	if (itemProgression_trh_EvolveItem(ATR_PASS_ARGS, pEnt, pDstItem, pWardItem, pResult, pReason, pExtract) == TRANSACTION_OUTCOME_FAILURE)
	{
		if (pResult)
			StructDestroy(parse_ItemProgressionUILastResult, pResult);

		if (pWardItem)
			StructDestroyNoConst(parse_Item, pWardItem);


		return TRANSACTION_OUTCOME_FAILURE;
	}

	QueueRemoteCommand_itemProgression_ForwardLastResultToClient(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pEnt->myContainerID, pResult);

	if (pResult)
		StructDestroy(parse_ItemProgressionUILastResult, pResult);

	if (pWardItem)
		StructDestroyNoConst(parse_Item, pWardItem);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".pInventoryV2.ppInventoryBags, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pinventoryv2.Pplitebags, .Pplayer.Eaplayernumericthresholds");
enumTransactionOutcome itemProgression_tr_EvoSlottedGem(ATR_ARGS, NOCONST(Entity)* pEnt, int iHolderBag, int iHolderSlot, int iHolderGemSlot, int iWardBag, int iWardSlot, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	NOCONST(InventoryBag)* pHolderBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iHolderBag, pExtract);
	NOCONST(InventoryBag)* pWardBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iWardBag, pExtract);
	NOCONST(Item)* pHolderItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pHolderBag, iHolderSlot);
	NOCONST(Item)* pGemItem = NULL;
	NOCONST(Item)* pWardItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, false, pWardBag, iWardSlot, 1, false, pReason);
	ItemProgressionUILastResult* pResult = NULL;

	//remove gem
	if (!inv_trh_RemoveGemmedItem(ATR_PASS_ARGS, pEnt, pHolderItem, iHolderGemSlot, &pGemItem))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pResult = StructCreate(parse_ItemProgressionUILastResult);

	//do evo
	if (!pGemItem || itemProgression_trh_EvolveItem(ATR_PASS_ARGS, pEnt, pGemItem, pWardItem, pResult, pReason, pExtract) == TRANSACTION_OUTCOME_FAILURE)
	{
		if (pResult)
			StructDestroy(parse_ItemProgressionUILastResult, pResult);

		if (pWardItem)
			StructDestroyNoConst(parse_Item, pWardItem);

		return TRANSACTION_OUTCOME_FAILURE;
	}

	//re-add gem
	if (!inv_trh_GemItem(pEnt, pHolderItem, pGemItem, iHolderGemSlot))
	{
		if (pResult)
			StructDestroy(parse_ItemProgressionUILastResult, pResult);

		if (pWardItem)
			StructDestroyNoConst(parse_Item, pWardItem);

		return TRANSACTION_OUTCOME_FAILURE;
	}

	QueueRemoteCommand_itemProgression_ForwardLastResultToClient(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pEnt->myContainerID, pResult);

	if (pResult)
		StructDestroy(parse_ItemProgressionUILastResult, pResult);

	if (pWardItem)
		StructDestroyNoConst(parse_Item, pWardItem);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void itemProgression_FeedItem(Entity* pEnt, int iDstBag, int iDstSlot, ItemProgressionCatalystArgumentList* pList)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item* pDst = inv_GetItemFromBag(pEnt, iDstBag, iDstSlot, pExtract);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pEnt, "ItemProgression:FeedItem", NULL);

	if (pDst->count > 1)
		AutoTrans_itemProgression_tr_FeedItemStack(LoggedTransactions_CreateManagedReturnValEnt("ItemProgression_FeedItemStack",pEnt,NULL,NULL),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
			iDstBag,iDstSlot,pList, &reason, pExtract);
	else
		AutoTrans_itemProgression_tr_FeedItem(LoggedTransactions_CreateManagedReturnValEnt("ItemProgression_FeedItem",pEnt,NULL,NULL),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
			iDstBag,iDstSlot,pList, &reason, pExtract);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void itemProgression_FeedSlottedGem(Entity* pEnt, int iDstBag, int iDstSlot, int iDstGemSlot, ItemProgressionCatalystArgumentList* pList)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pEnt, "ItemProgression:FeedItem", NULL);

	AutoTrans_itemProgression_tr_FeedSlottedGem(LoggedTransactions_CreateManagedReturnValEnt("ItemProgression_FeedItem",pEnt,NULL,NULL),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
		iDstBag,iDstSlot,iDstGemSlot,pList, &reason, pExtract);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void itemProgression_EvoItem(Entity* pEnt, int iDstBag, int iDstSlot, int iWardBag, int iWardSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pEnt, "ItemProgression:EvoItem", NULL);

	AutoTrans_itemProgression_tr_EvoItem(LoggedTransactions_CreateManagedReturnValEnt("ItemProgression_EvoItem",pEnt,NULL,NULL),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
		iDstBag, iDstSlot, iWardBag, iWardSlot, &reason, pExtract);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void itemProgression_EvoSlottedGem(Entity* pEnt, int iDstBag, int iDstSlot, int iDstGemSlot, int iWardBag, int iWardSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pEnt, "ItemProgression:EvoSlottedGem", NULL);

	AutoTrans_itemProgression_tr_EvoSlottedGem(LoggedTransactions_CreateManagedReturnValEnt("ItemProgression_EvoSlottedGem",pEnt,NULL,NULL),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
		iDstBag, iDstSlot, iDstGemSlot, iWardBag, iWardSlot, &reason, pExtract);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
void itemProgression_ForwardLastResultToClient(U32 entID, ItemProgressionUILastResult* pResult)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,entID);

	pResult->uTime = timeSecondsSince2000();

	ClientCmd_ItemProgressionUI_ReceiveLastActionResult(pEntity, pResult);
	/*
	if(eResult == kItemUpgradeResult_Success)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.Success",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashSuccess, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	else if(eResult == kItemUpgradeResult_Failure)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.Failure",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashFailure, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	else if(eResult == kItemUpgradeResult_FailureNoLoss)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.FailureNoLoss",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashFailure, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	else if(eResult == kItemUpgradeResult_UserCancelled)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.UserCancelled",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashFailure, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	*/
}