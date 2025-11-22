#include "earray.h"
#include "entity.h"
#include "Player.h"
#include "UIGen.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"
#include "ItemUpgrade.h"
#include "GameAccountDataCommon.h"
#include "gclEntity.h"
#include "FCInventoryUI.h"
#include "itemProgressionCommon.h"

#include "Autogen/itemProgressionCommon_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/gclItemProgression_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ItemProgressionUI_ClearAllFood();

AUTO_ENUM;
typedef enum ItemProgressionUITargetItemType
{
	kItemProgressionUITargetItemType_None = 0,
	kItemProgressionUITargetItemType_Item,
	kItemProgressionUITargetItemType_SlottedGem,
	kItemProgressionUITargetItemType_SlottedSCPEquipment,
} ItemProgressionUITargetItemType;

//Update ItemProgressionUI_Reset() if you add anything that needs to be freed.
typedef struct ItemProgressionUIState
{
	ItemProgressionUITargetItemType eType;
	U64 u64TargetItemID;
	int iTargetItemGemSlot;
	int iTargetSCPIdx;
	int iTargetSCPEquipSlot;

	U64 uiWardID;

	U64* ea64FoodItems;

	Item* pTargetItem;
	Item* pWardItem;
	InventorySlot** eaFoodSlots;
} ItemProgressionUIState;

static ItemProgressionUIState s_ItemProgressionState = {0};

AUTO_STRUCT;
typedef struct ItemProgressionUICatalyst
{
	REF_TO(ItemDef) hDefRequired;
	S32 iNumOwned;
	S32 iNumRequired;
} ItemProgressionUICatalyst;

static ItemProgressionUILastResult* s_pLastResult = NULL;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void ItemProgressionUI_ReceiveLastActionResult(ItemProgressionUILastResult* pResult)
{
	if (!pResult)
	{
		Errorf("Item Progression UI received a NULL last result from the server.");
		return;
	}
	if (s_pLastResult && s_pLastResult->uTime > pResult->uTime)
	{
		Errorf("Item Progression UI received a last result from the server that had an out-of-order timestamp.");
		return;
	}
	if (s_pLastResult)
	{
		StructDestroy(parse_ItemProgressionUILastResult, s_pLastResult);
	}

	s_pLastResult = StructClone(parse_ItemProgressionUILastResult, pResult);

	ItemProgressionUI_ClearAllFood();

	if (s_pLastResult->u64NewTargetItemID > 0)
		s_ItemProgressionState.u64TargetItemID = s_pLastResult->u64NewTargetItemID;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLastResultTimestamp);
U32 ItemProgressionUI_GetLastResultTimestamp()
{
	if (s_pLastResult)
		return s_pLastResult->uTime;
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLastResultType);
const char* ItemProgressionUI_GetLastResultType()
{
	if (s_pLastResult)
		return StaticDefineInt_FastIntToString(ItemProgressionUIResultTypeEnum, s_pLastResult->eType);
	return "Error";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLastResultEvoSuccess);
bool ItemProgressionUI_GetLastResultEvoSuccess()
{
	if (s_pLastResult)
		return s_pLastResult->bEvoSucceeded;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLastResultLevelUp);
bool ItemProgressionUI_GetLastResultLevelUp()
{
	if (s_pLastResult)
		return s_pLastResult->bLevelup;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLastResultFoodCrit);
F32 ItemProgressionUI_GetLastResultFoodSlotCrit(int iSlot)
{
	if (s_pLastResult)
	{
		ItemProgressionUILastFoodResult* pFoodResult = eaGet(&s_pLastResult->eaFoodResults, iSlot);
		if (pFoodResult)
			return pFoodResult->fCritMult;
	}
	return 1.0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLastResultFoodNetXP);
U32 ItemProgressionUI_GetLastResultFoodNetXP(int iSlot)
{
	if (s_pLastResult)
	{
		ItemProgressionUILastFoodResult* pFoodResult = eaGet(&s_pLastResult->eaFoodResults, iSlot);
		if (pFoodResult)
			return pFoodResult->uNetXP;
	}
	return 0;
}

static void ItemProgressionUI_RefreshItemPointers()
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		static U32 uLastRefresh = 0;
		if (uLastRefresh < g_ui_State.uiFrameCount)
		{
			BagIterator* pIter = bagiterator_Create();
			int i;

			//Delete previous copies
			if (s_ItemProgressionState.pTargetItem)
				StructDestroy(parse_Item, s_ItemProgressionState.pTargetItem);

			if (s_ItemProgressionState.pWardItem)
				StructDestroy(parse_Item, s_ItemProgressionState.pWardItem);

			eaDestroyStruct(&s_ItemProgressionState.eaFoodSlots, parse_InventorySlot);
			s_ItemProgressionState.eaFoodSlots = NULL;

			//Set timestamp
			uLastRefresh = g_ui_State.uiFrameCount;

			//Refresh target
			inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.u64TargetItemID, pIter);

			if (s_ItemProgressionState.iTargetItemGemSlot == -1)
			{
				s_ItemProgressionState.pTargetItem = StructCloneReConst(parse_Item, bagiterator_GetItem(pIter));
			}
			else
			{
				//create a fake gem item for the UI
				Item* pHolder = CONTAINER_RECONST(Item, bagiterator_GetItem(pIter));
				if (pHolder && pHolder->pSpecialProps)
				{
					ItemGemSlot* pSlot = eaGet(&pHolder->pSpecialProps->ppItemGemSlots, s_ItemProgressionState.iTargetItemGemSlot);
					NOCONST(Item)* pFakeGem = inv_ItemInstanceFromDefName(REF_STRING_FROM_HANDLE(pSlot->hSlottedItem), 0, 0, NULL, NULL, NULL, false, NULL);
					item_trh_GetOrCreateAlgoProperties(pFakeGem);
					pFakeGem->pAlgoProps->uProgressionLevel = pSlot->uProgressionLevel;
					pFakeGem->pAlgoProps->uProgressionXP = pSlot->uProgressionXP;
					s_ItemProgressionState.pTargetItem = CONTAINER_RECONST(Item, pFakeGem);
				}
			}

			//Refresh Ward
			bagiterator_Reset(pIter);
			inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.uiWardID, pIter);

			s_ItemProgressionState.pWardItem = StructCloneReConst(parse_Item, bagiterator_GetItem(pIter));

			//Refresh food
			for (i = 0; i < ea64Size(&s_ItemProgressionState.ea64FoodItems); i++)
			{
				bagiterator_Reset(pIter);
				inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.ea64FoodItems[i], pIter);

				if (!bagiterator_Stopped(pIter))
					eaSet(&s_ItemProgressionState.eaFoodSlots, StructClone(parse_InventorySlot, bagiterator_GetSlot(pIter)), i);
				else
					eaSet(&s_ItemProgressionState.eaFoodSlots, NULL, i);
			}

			bagiterator_Destroy(pIter);
		}
	}
}

S32 ItemProgressionUI_GetNumCatalystsOwned(int iSlot)
{
	Entity* pPlayer = entActivePlayerPtr();
	ItemProgressionUI_RefreshItemPointers();

	if (!s_ItemProgressionState.pTargetItem || !pPlayer)
		return 0;
	else
	{
		ItemDef* pDef = ItemProgression_GetCatalystItemDef(s_ItemProgressionState.pTargetItem, iSlot);

		if (pDef)
		{
			//If evoing requires a copy of the target item, lie about the count
			if (pDef == GET_REF(s_ItemProgressionState.pTargetItem->hItem) && s_ItemProgressionState.iTargetItemGemSlot == -1)
				return inv_ent_AllBagsCountItems(pPlayer, pDef->pchName) - 1;
			else
				return inv_ent_AllBagsCountItems(pPlayer, pDef->pchName);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_Reset);
void ItemProgressionUI_Reset()
{
	eaDestroyStruct(&s_ItemProgressionState.eaFoodSlots, parse_InventorySlot);
	StructDestroy(parse_Item, s_ItemProgressionState.pWardItem);
	StructDestroy(parse_Item, s_ItemProgressionState.pTargetItem);
	ea64Destroy(&s_ItemProgressionState.ea64FoodItems);
	memset(&s_ItemProgressionState, 0, sizeof(ItemProgressionUIState));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetTargetItem);
SA_RET_OP_VALID Item* ItemProgressionUI_GetTargetItem()
{
	ItemProgressionUI_RefreshItemPointers();
	return s_ItemProgressionState.pTargetItem;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetWardItem);
SA_RET_OP_VALID Item* ItemProgressionUI_GetWardItem()
{
	ItemProgressionUI_RefreshItemPointers();
	return s_ItemProgressionState.pWardItem;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(itemProgressionUI_IsMaxLevel);
bool itemProgressionUI_IsMaxLevel(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem && itemProgression_IsMaxLevel(pItem))
		return true;

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_SetTargetItem);
void ItemProgressionUI_SetTargetItem(const char *pchKey, int iGemSlot)
{
	if (!pchKey || !pchKey[0])
	{
		s_ItemProgressionState.u64TargetItemID = 0;
		s_ItemProgressionState.iTargetItemGemSlot = -1;
		s_ItemProgressionState.iTargetSCPIdx = -1;
		s_ItemProgressionState.iTargetSCPEquipSlot = -1;
	}
	else
	{
		UIInventoryKey Key = {0};
		Item *pItem;
		if (!gclInventoryParseKey(pchKey, &Key))
			return;

		if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
			return;

		pItem = Key.pSlot->pItem;
		
		s_ItemProgressionState.u64TargetItemID = pItem->id;
		s_ItemProgressionState.iTargetItemGemSlot = iGemSlot;

		if (Key.eType == GLOBALTYPE_ENTITYCRITTER)
		{
			//Super Critter Pet
			s_ItemProgressionState.iTargetSCPIdx = Key.eBag;
			s_ItemProgressionState.iTargetSCPEquipSlot = Key.iSlot;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_SetWardItem);
void ItemProgressionUI_SetWardItem(const char *pchKey)
{
	if (!pchKey || !pchKey[0])
	{
		s_ItemProgressionState.uiWardID = 0;
	}
	else
	{
		UIInventoryKey Key = {0};
		Item *pItem;
		if (!gclInventoryParseKey(pchKey, &Key))
			return;

		if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
			return;

		pItem = Key.pSlot->pItem;

		s_ItemProgressionState.uiWardID = pItem->id;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetFoodSlots);
bool ItemProgressionUI_GetFoodSlots(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemProgressionUI_RefreshItemPointers();
	if (pGen)
	{
		InventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);

		eaClearFast(peaSlots);

		eaCopy(peaSlots, &s_ItemProgressionState.eaFoodSlots);

		eaSetSize(peaSlots, MAX_SIMULTANEOUS_FOOD_ITEMS);

		ui_GenSetManagedListSafe(pGen, peaSlots, InventorySlot, false);
		return true;
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_InventorySlot);
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetCatalystSlots);
bool ItemProgressionUI_GetCatalystSlots(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemProgressionUI_RefreshItemPointers();
	if (s_ItemProgressionState.pTargetItem && pGen)
	{
		ItemProgressionTierDef* pTier = itemProgression_GetCurrentTier(s_ItemProgressionState.pTargetItem);
		ItemProgressionUICatalyst ***peaSlots = ui_GenGetManagedListSafe(pGen, ItemProgressionUICatalyst);
		int i;
		eaClearFast(peaSlots);

//		eaCopy(peaSlots, &s_ItemProgressionState.eaFoodItems);

		eaSetSizeStruct(peaSlots, parse_ItemProgressionUICatalyst, eaSize(&pTier->eaCatalysts));

		for (i = 0; i < eaSize(peaSlots); i++)
		{
			if (pTier->eaCatalysts[i]->eType == kItemProgressionCatalystType_RankUpRequirement_SpecificItem)
			{
				COPY_HANDLE((*peaSlots)[i]->hDefRequired, pTier->eaCatalysts[i]->hItem);
			}
			else if (pTier->eaCatalysts[i]->eType == kItemProgressionCatalystType_RankUpRequirement_MatchingItemDef)
			{
				COPY_HANDLE((*peaSlots)[i]->hDefRequired, s_ItemProgressionState.pTargetItem->hItem);
			}
			(*peaSlots)[i]->iNumOwned = ItemProgressionUI_GetNumCatalystsOwned(i);
			(*peaSlots)[i]->iNumRequired = pTier->eaCatalysts[i]->iNumRequired;
		}
		ui_GenSetManagedListSafe(pGen, peaSlots, ItemProgressionUICatalyst, false);
		return true;
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_ItemProgressionUICatalyst);
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetEstimatedFoodXPGain);
U32 ItemProgressionUI_GetEstimatedFoodXPGain()
{
	int i;
	U32 uXPTotal = 0;
	ItemProgressionUI_RefreshItemPointers();

	if (!s_ItemProgressionState.pTargetItem || eaSize(&s_ItemProgressionState.eaFoodSlots) <= 0)
		return 0;

	for (i = 0; i < eaSize(&s_ItemProgressionState.eaFoodSlots); i++)
	{
		if (s_ItemProgressionState.eaFoodSlots[i] && s_ItemProgressionState.eaFoodSlots[i]->pItem)
		{
			F32 fMultiplier = itemProgressionDef_CalculateFoodMultiplier(s_ItemProgressionState.pTargetItem, s_ItemProgressionState.eaFoodSlots[i]->pItem);
			U32 uFoodXP = itemProgression_GetFoodXPValue(s_ItemProgressionState.eaFoodSlots[i]->pItem) * fMultiplier;
			uXPTotal += uFoodXP;
		}
	}

	return uXPTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_ClearAllFood);
void ItemProgressionUI_ClearAllFood()
{
	ea64Clear(&s_ItemProgressionState.ea64FoodItems);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_SetFoodItem);
void ItemProgressionUI_SetFoodItem(const char *pchKey, int index)
{
	ItemProgressionUI_RefreshItemPointers();
	if (!pchKey || !pchKey[0])
	{
		ea64Set(&s_ItemProgressionState.ea64FoodItems, 0, index);
		return;
	}
	else if (index >= 0 && index < MAX_SIMULTANEOUS_FOOD_ITEMS)
	{
		UIInventoryKey Key = {0};
		Item *pItem;
		if (!gclInventoryParseKey(pchKey, &Key))
			return;

		if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
			return;

		pItem = Key.pSlot->pItem;
		ea64Set(&s_ItemProgressionState.ea64FoodItems, pItem->id, index);
	}
	else if (index == -1)
	{
		UIInventoryKey Key = {0};
		Item *pItem;
		int i;
		int iCount = 0;
		if (!gclInventoryParseKey(pchKey, &Key))
			return;

		if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
			return;

		pItem = Key.pSlot->pItem;
		iCount = pItem->count;
		for (i=0;i<MAX_SIMULTANEOUS_FOOD_ITEMS;i++)
		{
			InventorySlot* pInvSlot = eaGet(&s_ItemProgressionState.eaFoodSlots, i);
			if (!pInvSlot)
			{
				ea64Set(&s_ItemProgressionState.ea64FoodItems, pItem->id, i);
				iCount--;
				if (iCount <= 0 || (pItem->id == s_ItemProgressionState.pTargetItem->id && iCount == 1))
					break;
			}
			else if (s_ItemProgressionState.pTargetItem && (ea64Get(&s_ItemProgressionState.ea64FoodItems, i) == s_ItemProgressionState.pTargetItem->id))
				iCount--;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_ReadyForEvo);
bool ItemProgressionUI_ReadyForEvo()
{
	ItemProgressionUI_RefreshItemPointers();
	return itemProgression_ReadyToEvo(s_ItemProgressionState.pTargetItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetEvoResultItem);
SA_RET_OP_VALID Item* ItemProgressionUI_GetEvoResultItem()
{
	static REF_TO(ItemDef) hEvoResult;
	static Item* pFakeItem = NULL;
	ItemProgressionUI_RefreshItemPointers();

	if (pFakeItem && !REF_COMPARE_HANDLES(pFakeItem->hItem, hEvoResult))
	{
		StructDestroy(parse_Item, pFakeItem);
		pFakeItem = NULL;
	}

	if (s_ItemProgressionState.pTargetItem)
	{
		char* estrName = NULL;
		itemProgression_GetEvoResultDefName(s_ItemProgressionState.pTargetItem, &estrName);
		if (estrName)
		{
			SET_HANDLE_FROM_STRING(g_hItemDict, estrName, hEvoResult);
			pFakeItem = CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(estrName, 1, 1, NULL, NULL, NULL, false, NULL));
		}
		estrDestroy(&estrName);
		return pFakeItem;
	}
	return NULL;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetEvoSuccessChance);
U32 ItemProgressionUI_GetEvoSuccessChance()
{
	ItemProgressionUI_RefreshItemPointers();

	if (s_ItemProgressionState.pTargetItem)
	{
		ItemProgressionTierDef* pTier = itemProgression_GetCurrentTier(s_ItemProgressionState.pTargetItem);
		ItemDef* pWardDef = SAFE_GET_REF(s_ItemProgressionState.pWardItem, hItem);
		
		if (pTier)
			return min(pTier->uBaseRankUpChance + (pWardDef ? pWardDef->uProgressionEvoSuccessBonus : 0), 100);
	}
	return 0;
}

//TODO: Error on early returns
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_FeedItem);
void ItemProgressionUI_FeedItem()
{
	Entity* pEnt = entActivePlayerPtr();
	ItemProgressionUI_RefreshItemPointers();
	if (s_ItemProgressionState.pTargetItem && pEnt)
	{
		BagIterator* pIter = bagiterator_Create();
		int iTargetBag, iTargetSlot;
		int i;
		ItemProgressionCatalystArgumentList list = {0};

		//Find target bag/slot
		inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.u64TargetItemID, pIter);

		if (bagiterator_Stopped(pIter))
		{
			bagiterator_Destroy(pIter);
			return;
		}

		iTargetBag = bagiterator_GetCurrentBagID(pIter);
		iTargetSlot = bagiterator_GetSlotID(pIter);

		//Find all food
		for (i = 0; i < ea64Size(&s_ItemProgressionState.ea64FoodItems); i++)
		{
			bagiterator_Reset(pIter);
			inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.ea64FoodItems[i], pIter);

			/* If we couldn't find the item, that's fine - it may have been left over in an "empty" slot from a previous feeding. */
			if (!bagiterator_Stopped(pIter))
			{
				ItemProgressionCatalystArgument* pArg = StructCreate(parse_ItemProgressionCatalystArgument);

				pArg->iCatalystBag = bagiterator_GetCurrentBagID(pIter);
				pArg->iCatalystSlot = bagiterator_GetSlotID(pIter);
				eaPush(&list.eaArgs, pArg);
			}
		}

		if (eaSize(&list.eaArgs) > 0)
		{
			if (s_ItemProgressionState.iTargetItemGemSlot >= 0)
				ServerCmd_itemProgression_FeedSlottedGem(iTargetBag, iTargetSlot, s_ItemProgressionState.iTargetItemGemSlot, &list);
			else
				ServerCmd_itemProgression_FeedItem(iTargetBag, iTargetSlot, &list);

		}

		StructDeInit(parse_ItemProgressionCatalystArgumentList, &list);
		bagiterator_Destroy(pIter);
	}
}

//TODO: Error on early returns
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_EvolveItem);
void ItemProgressionUI_EvolveItem()
{
	Entity* pEnt = entActivePlayerPtr();
	ItemProgressionUI_RefreshItemPointers();
	if (s_ItemProgressionState.pTargetItem && pEnt)
	{
		BagIterator* pIter = bagiterator_Create();
		int iTargetBag, iTargetSlot, iWardBag, iWardSlot;

		//Find target bag/slot
		inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.u64TargetItemID, pIter);

		if (bagiterator_Stopped(pIter))
		{
			bagiterator_Destroy(pIter);
			return;
		}

		iTargetBag = bagiterator_GetCurrentBagID(pIter);
		iTargetSlot = bagiterator_GetSlotID(pIter);

		//Find Ward bag/slot
		if (s_ItemProgressionState.pWardItem)
		{
			bagiterator_Reset(pIter);
			inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), s_ItemProgressionState.uiWardID, pIter);

			if (bagiterator_Stopped(pIter))
			{
				bagiterator_Destroy(pIter);
				return;
			}

			iWardBag = bagiterator_GetCurrentBagID(pIter);
			iWardSlot = bagiterator_GetSlotID(pIter);
		}
		else
		{
			iWardBag = -1;
			iWardSlot = -1;
		}

		if (s_ItemProgressionState.iTargetItemGemSlot >= 0)
			ServerCmd_itemProgression_EvoSlottedGem(iTargetBag, iTargetSlot, s_ItemProgressionState.iTargetItemGemSlot, iWardBag, iWardSlot);
		else
			ServerCmd_itemProgression_EvoItem(iTargetBag, iTargetSlot, iWardBag, iWardSlot);

		bagiterator_Destroy(pIter);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetEvoNumericCost);
int ItemProgressionUI_GetEvoNumericCost()
{
	ItemProgressionUI_RefreshItemPointers();
	if (s_ItemProgressionState.pTargetItem)
	{
		ItemProgressionTierDef* pCurTier = itemProgression_GetCurrentTier(s_ItemProgressionState.pTargetItem);
		if (pCurTier)
			return pCurTier->iEvolutionADCost;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetEvoNumeric);
const char* ItemProgressionUI_GetEvoNumeric()
{
	ItemProgressionUI_RefreshItemPointers();
	if (s_ItemProgressionState.pTargetItem)
	{
		ItemDef* pDef = GET_REF(s_ItemProgressionState.pTargetItem->hItem);
		ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
		if (pProgDef)
			return pProgDef->pchEvolutionADCostNumeric;
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetLevelOfNextEvo);
int ItemProgressionUI_GetLevelOfNextEvo(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem)
	{
		ItemProgressionTierDef* pNextTier = itemProgression_GetNextTier(pItem);
		if (pNextTier)
			return pNextTier->uMinLevel;
	}

	return 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_GetEstimatedXPFromFoodSlot);
int ItemProgressionUI_GetEstimatedXPFromFoodSlot(int iSlot)
{
	InventorySlot* pFood = NULL;

	ItemProgressionUI_RefreshItemPointers();

	pFood = eaGet(&s_ItemProgressionState.eaFoodSlots, iSlot);

	if (s_ItemProgressionState.pTargetItem && pFood && pFood->pItem)
	{
		F32 fMultiplier = itemProgressionDef_CalculateFoodMultiplier(s_ItemProgressionState.pTargetItem, pFood->pItem);
		return itemProgression_GetFoodXPValue(pFood->pItem) * fMultiplier;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_IsValidTarget);
bool ItemProgressionUI_IsValidTarget(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);

	if(pItemDef)
	{
		return (GET_REF(pItemDef->hProgressionDef) && !itemProgression_IsMaxLevel(pItem));
	}

	return false;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_IsValidFoodForItem);
bool ItemProgressionUI_IsValidFoodForItem(SA_PARAM_OP_VALID Item* pFood, SA_PARAM_OP_VALID Item* pTarget)
{
	if (pTarget && pFood)
	{
		return itemProgressionDef_CalculateFoodMultiplier(pTarget, pFood) > 0.0;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_IsValidFood);
bool ItemProgressionUI_IsValidFood(SA_PARAM_OP_VALID Item* pItem)
{
	ItemProgressionUI_RefreshItemPointers();
	return ItemProgressionUI_IsValidFoodForItem(pItem, s_ItemProgressionState.pTargetItem);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_FoodIsAlreadySlotted);
bool ItemProgressionUI_FoodIsAlreadySlotted(SA_PARAM_OP_VALID Item* pItem)
{
	ItemProgressionUI_RefreshItemPointers();
	if (pItem)
	{
		int iCount = pItem->count;
		int i;
		for (i = 0; i < eaSize(&s_ItemProgressionState.eaFoodSlots); i++)
		{
			InventorySlot* pSlot = eaGet(&s_ItemProgressionState.eaFoodSlots, i);
			if (pSlot && pSlot->pItem && (pSlot->pItem->id == pItem->id))
				iCount--;
		}

		if (s_ItemProgressionState.pTargetItem && (s_ItemProgressionState.pTargetItem->id == pItem->id))
			iCount--;

		return iCount <= 0;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemProgressionUI_IsValidWard);
bool ItemProgressionUI_IsValidWard(SA_PARAM_OP_VALID Item* pTarget)
{
	ItemDef* pDef = SAFE_GET_REF(pTarget, hItem);

	if (pDef && pDef->eType == kItemType_UpgradeModifier)
		return true;

	return false;
}

#include "Autogen/gclItemProgression_c_ast.c"