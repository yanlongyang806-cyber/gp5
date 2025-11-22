#include "itemCommon.h"
#include "itemProgressionCommon.h"
#include "rewardCommon.h"
#include "ResourceManager.h"
#include "rand.h"
#include "error.h"
#include "file.h"
#include "inventoryCommon.h"
#include "Autogen/itemCommon_h_ast.h"
#include "Autogen/itemProgressionCommon_h_ast.h"

DictionaryHandle g_hItemProgressionDict;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_TRANS_HELPER;
ItemDef* ItemProgression_trh_GetCatalystItemDef(ATH_ARG NOCONST(Item)* pItem, int iSlot)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	ItemProgressionTierDef* pTier = itemProgression_trh_GetCurrentTier(pItem);

	if (pItem && pDef && pProgDef && pTier)
	{
		ItemProgressionCatalyst* pCat = eaGet(&pTier->eaCatalysts, iSlot);
		if (pCat && pCat->eType == kItemProgressionCatalystType_RankUpRequirement_SpecificItem)
		{
			return GET_REF(pCat->hItem);
		}
		else if (pCat && pCat->eType == kItemProgressionCatalystType_RankUpRequirement_MatchingItemDef)
		{
			return pDef;
		}
	}
	return NULL;
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_GetLevel(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	if (pDef && GET_REF(pDef->hProgressionDef))
	{
		if (pItem->pAlgoProps)
			return pItem->pAlgoProps->uProgressionLevel;
		else
			return 1;
	}
	return 0;
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_GetTotalXP(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	if (pDef && GET_REF(pDef->hProgressionDef))
	{
		if (pItem->pAlgoProps)
			return pItem->pAlgoProps->uProgressionXP;
		else
			return itemProgression_trh_GetInitialXPValue(pItem);
	}
	return 0;
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_ItemCanStack(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	if (pDef && GET_REF(pDef->hProgressionDef))
	{
		U32 uLevel = itemProgression_trh_GetLevel(pItem);
		ItemProgressionTierDef* pTier = itemProgression_trh_GetCurrentTier(pItem);
		//items can stack only if they have 0 xp towards their next level and are the minimum level of their current tier.
		if ((itemProgression_trh_GetXPRequiredForLevel(pItem, uLevel) == itemProgression_trh_GetTotalXP(pItem)) &&
			pTier && 
			(uLevel == pTier->uMinLevel))
			return true;

		return false;
	}
	return true;
}

ItemProgressionTierDef* itemProgressionDef_GetTierAtLevel(ItemProgressionDef* pDef, U32 uLevel, S32 iTierDelta)
{
	int i;

	if (!pDef || uLevel == 0)
		return NULL;

	for (i = 0; i < eaSize(&pDef->eaTiers); i++)
	{
		if (pDef->eaTiers[i]->uMinLevel <= uLevel && pDef->eaTiers[i]->uMaxLevel >= uLevel)
			return eaGet(&pDef->eaTiers, i + iTierDelta);
	}
	return NULL;
}

AUTO_TRANS_HELPER;
ItemProgressionTierDef* itemProgression_trh_GetCurrentTier(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	return itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pItem), 0);
}

AUTO_TRANS_HELPER;
ItemProgressionTierDef* itemProgression_trh_GetNextTier(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	return itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pItem), 1);
}

U32 itemProgressionDef_GetXPRequiredForLevel(ItemProgressionDef* pProgDef, U32 uLevel)
{
	if (pProgDef)
	{
		RewardValTable* pXPTable = GET_REF(pProgDef->hXPRequiredTable);

		if (pXPTable && (S32)USER_TO_TAB_LEVEL(uLevel) < eafSize(&pXPTable->Val))
		{
			return pXPTable->Val[USER_TO_TAB_LEVEL(uLevel)];
		}
	}
	return 0;
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_GetXPRequiredForLevel(ATH_ARG NOCONST(Item)* pItem, U32 uLevel)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	return itemProgressionDef_GetXPRequiredForLevel(pProgDef, uLevel);
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_CalculateLevelFromXP(ATH_ARG NOCONST(Item)* pItem, bool bClampToTier)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	if (pProgDef)
	{
		RewardValTable* pXPTable = GET_REF(pProgDef->hXPRequiredTable);
		U32 uPrevLevel = itemProgression_trh_GetLevel(pItem);
		ItemProgressionTierDef* pTier = itemProgressionDef_GetTierAtLevel(pProgDef, uPrevLevel, 0);
		U32 uXP = SAFE_MEMBER2(pItem, pAlgoProps, uProgressionXP);
		U32 i;

		if (!pTier)
			return 1;

		for (i = 0; i < (U32)eafSize(&pXPTable->Val); i++)
		{
			if (pXPTable->Val[i] > uXP)
			{
				if (bClampToTier)
					return min(pTier->uMaxLevel, i);
				else
					return i;
			}
		}
	}
	return 1;
}

S32 itemProgression_SortCatalystsByXP(const ItemProgressionCatalystSortData** pA, const ItemProgressionCatalystSortData** pB)
{
	return (*pB)->iCount - (*pA)->iCount;
}

AUTO_TRANS_HELPER;
void itemProgression_trh_GetCatalystItemsSortedByXP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Item)* pTarget, ItemDef* pCatalystDef, ItemProgressionCatalystSortData*** peaSortDataOut)
{
	if (NONNULL(pEnt) && pCatalystDef && pTarget)
	{
		BagIterator* pIter = bagiterator_Create();
		while (inv_trh_FindItemByDefName(ATR_PASS_ARGS, pEnt, pIter, pCatalystDef->pchName, false, false))
		{
			NOCONST(Item)* pItem = bagiterator_GetItem(pIter);
			if (pItem->id != pTarget->id)
			{
				ItemProgressionCatalystSortData* pData = StructCreate(parse_ItemProgressionCatalystSortData);
				pData->iCatalystBag = bagiterator_GetCurrentBagID(pIter);
				pData->iCatalystSlot = bagiterator_GetSlotID(pIter);
				pData->uXP = itemProgression_trh_GetTotalXP(pItem);
				pData->iCount = pItem->count;
				eaPush(peaSortDataOut, pData);
			}
			else if (pItem->count > 1)
			{
				ItemProgressionCatalystSortData* pData = StructCreate(parse_ItemProgressionCatalystSortData);
				pData->iCatalystBag = bagiterator_GetCurrentBagID(pIter);
				pData->iCatalystSlot = bagiterator_GetSlotID(pIter);
				pData->uXP = itemProgression_trh_GetTotalXP(pItem);
				pData->iCount = pItem->count-1;
				eaPush(peaSortDataOut, pData);
			}
		}
		eaQSort((*peaSortDataOut), itemProgression_SortCatalystsByXP);
		bagiterator_Destroy(pIter);
	}
}

AUTO_TRANS_HELPER;
bool itemProgression_trh_ReadyToEvo(ATH_ARG NOCONST(Item)* pItem)
{
	if (pItem)
	{
		ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
		ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
		ItemProgressionTierDef* pTier = itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pItem), 0);
		U32 uUnClampedLevel = itemProgression_trh_CalculateLevelFromXP(pItem, false);
		ItemProgressionTierDef* pNextTier = itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pItem), 1);

		if (!pTier || !pNextTier || pTier == pNextTier || pNextTier->uMinLevel > uUnClampedLevel)
			return false;

		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
bool itemProgression_trh_IsMaxLevel(ATH_ARG NOCONST(Item)* pItem)
{
	if (pItem)
	{
		ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
		ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
		ItemProgressionTierDef* pTier = itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pItem), 0);
		U32 uUnClampedLevel = itemProgression_trh_CalculateLevelFromXP(pItem, false);
		ItemProgressionTierDef* pNextTier = itemProgressionDef_GetTierAtLevel(pProgDef, itemProgression_trh_GetLevel(pItem), 1);

		if (!pTier || pNextTier || itemProgression_trh_GetLevel(pItem) < pTier->uMaxLevel)
			return false;

		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
bool itemProgression_trh_GetEvoResultDefName(ATH_ARG NOCONST(Item)* pItem, char** pestrNameOut)
{
	if (pItem)
	{
		ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
		char* estrPattern = NULL;
		char* estrNewDefName = NULL;
		ItemDef* pNewDef = NULL;
		ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
		ItemProgressionTierDef* pNextTier = itemProgression_trh_GetNextTier(pItem);

		//Swap out itemdef
		if (!pNextTier || !itemProgression_trh_GetItemDefNamePattern(pItem, &estrPattern))
			return false;

		estrPrintf(pestrNameOut, FORMAT_OK(estrPattern), pNextTier->iIndex);

		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_GetInitialXPValue(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	if (pDef && GET_REF(pDef->hProgressionDef))
	{
		return itemProgressionDef_GetXPRequiredForLevel(GET_REF(pDef->hProgressionDef), 1);
	}
	return 0;
}

AUTO_TRANS_HELPER;
U32 itemProgression_trh_GetFoodXPValue(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);

	if (pDef && pDef->uProgressionFoodXP > 0)
	{
		return pDef->uProgressionFoodXP;
	}

	if (pDef && GET_REF(pDef->hProgressionDef))
	{
		ItemProgressionDef* pProgDef = GET_REF(pDef->hProgressionDef);
		RewardValTable* pXPTable = SAFE_GET_REF(pProgDef, hFoodValueTable);

		if (!pXPTable)
		{
			Errorf("Item progression is trying to use a non-existent XP table: %s", REF_STRING_FROM_HANDLE(pProgDef->hFoodValueTable));
			return 0;
		}
		return eafGet(&pXPTable->Val, USER_TO_TAB_LEVEL(itemProgression_trh_GetLevel(pItem)));
	}
	return 0;
}

int itemProgression_GetXPRequiredForNextLevel(Item* pItem)
{
	return itemProgression_GetXPRequiredForLevel(pItem, itemProgression_GetLevel(pItem)+1);
}

AUTO_TRANS_HELPER;
F32 itemProgressionDef_trh_CalculateFoodMultiplier(ATH_ARG NOCONST(Item)* pDst, NOCONST(Item)* pFood, bool* bCritEligibleOut)
{
	int i, j;
	F32 fMaxMult = 0.0;
	ItemDef* pDstDef = SAFE_GET_REF(pDst, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDstDef, hProgressionDef);
	ItemDef* pFoodDef = SAFE_GET_REF(pFood, hItem);
	if (pProgDef && pDstDef && pFoodDef)
	{
		for (j = 0; j < eaSize(&pProgDef->eaFoodCategories); j++)
		{
			bool bMatchesCategories = false;
			if (pProgDef->eaFoodCategories[j]->eRequiredItemType != 0 && pProgDef->eaFoodCategories[j]->eRequiredItemType != pFoodDef->eType)
				continue;

			if (pProgDef->eaFoodCategories[j]->bMustMatchNamingConvention &&
				!itemProgression_trh_CompareItemDefNamePatterns(pFood, pDst))
				continue;

			if (eaiSize(&pProgDef->eaFoodCategories[j]->ea32Categories) == 0)
				bMatchesCategories = true;
			else
			{
				for (i = 0; i < ea32Size(&pFoodDef->peCategories); i++)
				{
					if (eaiFind(&pProgDef->eaFoodCategories[j]->ea32Categories, pFoodDef->peCategories[i]) > -1)
					{
						bMatchesCategories = true;
						break;
					}
				}
			}

			if (bMatchesCategories)
			{
				if (pProgDef->eaFoodCategories[j]->fEfficiency > fMaxMult)
				{
					if (bCritEligibleOut)
						(*bCritEligibleOut) = !pProgDef->eaFoodCategories[j]->bNoCrit;
					fMaxMult = pProgDef->eaFoodCategories[j]->fEfficiency;
				}
			}
		}
	}
	return fMaxMult;
}

bool itemProgression_StringIsValidItemDefNamePattern(const char* pchPattern)
{
	if (pchPattern)
	{
		char* pch = strchr(pchPattern, '%');
		if (*(pch+1) != 'd')
			return false;
		//String must contain exactly one format specifier which must be a %d.
		if (strchrCount(pch+1, '%') != 0)
			return false;

		return true;
	}
	return false;
}

int itemProgression_trh_ParseTierArrayIndexFromDefName(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	ItemProgressionTierDef* pTier = pProgDef ? itemProgression_trh_GetCurrentTier(pItem) : NULL;
	char* estrPattern = NULL;

	if (pProgDef && pTier)
	{
		switch (pProgDef->eNamingScheme)
		{
		case kItemProgressionNamingScheme_AppendRank:
			{
				const char* pchFind = NULL;
				int iTier = 0;
				int i;

				pchFind = pDef->pchName + strlen(pDef->pchName);

				//find last occurance of first character in TIER_NAME_PATTERN_APPEND.
				while (*pchFind != TIER_NAME_PATTERN_APPEND_RANK[0])
				{
					pchFind--;
					if (pchFind < pDef->pchName)
						return -1;
				}

				sscanf(pchFind, TIER_NAME_PATTERN_APPEND_RANK, &iTier);
				for (i = 0; i < eaSize(&pProgDef->eaTiers); i++)
				{
					if (pProgDef->eaTiers[i]->iIndex == iTier)
						return i;
				}
			}break;
		case kItemProgressionNamingScheme_AppendPlainNumeral:
			{
				const char* pchFind = NULL;
				int iTier = 0;
				int i;

				pchFind = pDef->pchName + strlen(pDef->pchName);

				//find last occurance of first character in TIER_NAME_PATTERN_APPEND.
				while (*pchFind != TIER_NAME_PATTERN_APPEND_PLAIN_NUMERAL[0])
				{
					pchFind--;
					if (pchFind < pDef->pchName)
						return -1;
				}

				sscanf(pchFind, TIER_NAME_PATTERN_APPEND_PLAIN_NUMERAL, &iTier);
				for (i = 0; i < eaSize(&pProgDef->eaTiers); i++)
				{
					if (pProgDef->eaTiers[i]->iIndex == iTier)
						return i;
				}
			}break;
		case kItemProgressionNamingScheme_PrependTier:
			{
				int iTier = 0;
				int i;
				sscanf(pDef->pchName, TIER_NAME_PATTERN_PREPEND_TIER, &iTier);
				for (i = 0; i < eaSize(&pProgDef->eaTiers); i++)
				{
					if (pProgDef->eaTiers[i]->iIndex == iTier)
						return i;
				}
			}break;
		default:
			{
				ErrorFilenamef(pProgDef->pchFilename, "Item Progression Def %s didn't specify a valid naming scheme type. Must be either Prepend or Append.", pProgDef->pchName);
			}break;
		}
	}
	return -1;
}

void itemProgression_trh_FixupNewItemProps(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	int iTier = itemProgression_trh_ParseTierArrayIndexFromDefName(pItem);
	ItemProgressionTierDef* pTier = pProgDef ? eaGet(&pProgDef->eaTiers, iTier) : NULL;

	if (pTier && pTier->uMinLevel > 1)
	{
		NOCONST(AlgoItemProps)* pProps = item_trh_GetOrCreateAlgoProperties(pItem);
		pProps->uProgressionXP = itemProgression_trh_GetXPRequiredForLevel(pItem, pTier->uMinLevel);
		pProps->uProgressionLevel = pTier->uMinLevel;
	}
}

bool itemProgression_trh_GetItemDefNamePattern(ATH_ARG NOCONST(Item)* pItem, char** pestrOut)
{
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	ItemProgressionDef* pProgDef = SAFE_GET_REF(pDef, hProgressionDef);
	ItemProgressionTierDef* pTier = pProgDef ? itemProgression_trh_GetCurrentTier(pItem) : NULL;
	char* estrPattern = NULL;
	const char* pchIter = NULL;

	if (pProgDef && pTier)
	{
		switch (pProgDef->eNamingScheme)
		{
		case kItemProgressionNamingScheme_AppendRank:
			{
				estrPrintf(&estrPattern, TIER_NAME_PATTERN_APPEND_RANK, pTier->iIndex);
				pchIter = pDef->pchName + (strlen(pDef->pchName) - (estrLength(&estrPattern)));
				if (strnicmp(pchIter, estrPattern, estrLength(&estrPattern)) != 0)
				{
					//item does not end with the appropriate pattern for this tier
					ErrorFilenamef(pProgDef->pchFilename, "Item Def %s did not end with the expected string for its current progression tier. Expected: %s", pDef->pchName, estrPattern);
					return false;
				}
				estrCopy2(pestrOut, pDef->pchName);
				estrSetSize(pestrOut, pchIter-pDef->pchName);
				estrAppend2(pestrOut, TIER_NAME_PATTERN_APPEND_RANK);
			}break;
		case kItemProgressionNamingScheme_AppendPlainNumeral:
			{
				estrPrintf(&estrPattern, TIER_NAME_PATTERN_APPEND_PLAIN_NUMERAL, pTier->iIndex);
				pchIter = pDef->pchName + (strlen(pDef->pchName) - (estrLength(&estrPattern)));
				if (strnicmp(pchIter, estrPattern, estrLength(&estrPattern)) != 0)
				{
					//item does not end with the appropriate pattern for this tier
					ErrorFilenamef(pProgDef->pchFilename, "Item Def %s did not end with the expected string for its current progression tier. Expected: %s", pDef->pchName, estrPattern);
					return false;
				}
				estrCopy2(pestrOut, pDef->pchName);
				estrSetSize(pestrOut, pchIter-pDef->pchName);
				estrAppend2(pestrOut, TIER_NAME_PATTERN_APPEND_PLAIN_NUMERAL);
			}break;
		case kItemProgressionNamingScheme_PrependTier:
			{
				estrPrintf(&estrPattern, TIER_NAME_PATTERN_PREPEND_TIER, pTier->iIndex);
				if (strnicmp(pDef->pchName, estrPattern, estrLength(&estrPattern)) != 0)
				{
					//item does not begin with the appropriate pattern for this tier
					ErrorFilenamef(pProgDef->pchFilename, "Item Def %s did not begin with the expected string for its current progression tier. Expected: %s", pDef->pchName, estrPattern);
					return false;
				}
				estrPrintf(pestrOut, "%s%s", TIER_NAME_PATTERN_PREPEND_TIER, pDef->pchName + estrLength(&estrPattern));
			}break;
		default:
			{
				ErrorFilenamef(pProgDef->pchFilename, "Item Progression Def %s didn't specify a valid naming scheme type. Must be either Prepend or Append.", pProgDef->pchName);
			}break;
		}
		if (!itemProgression_StringIsValidItemDefNamePattern(*pestrOut))
		{
			estrDestroy(pestrOut);
			*pestrOut = NULL;
			return false;
		}

		return true;
	}
	return false;
}

//Returns true if both items belong to the same progression naming convention.
bool itemProgression_trh_CompareItemDefNamePatterns(ATH_ARG NOCONST(Item)* pItemA, ATH_ARG NOCONST(Item)* pItemB)
{
	char* estrPatternA = NULL;
	char* estrPatternB = NULL;
	bool bRet = true;
	
	itemProgression_trh_GetItemDefNamePattern(pItemA, &estrPatternA);
	itemProgression_trh_GetItemDefNamePattern(pItemB, &estrPatternB);
	
	bRet = (stricmp(estrPatternA, estrPatternB) == 0);
	
	if (estrPatternA)
		estrDestroy(&estrPatternA);
	if (estrPatternB)
		estrDestroy(&estrPatternB);
	
	return bRet;
}

AUTO_EXPR_FUNC(ItemEval, UIGen) ACMD_NAME(ItemProgression_GetItemLevel);
U32 exprItemProgression_GetItemLevel(SA_PARAM_OP_VALID Item* pItem)
{
	return itemProgression_GetLevel(pItem);
}

AUTO_EXPR_FUNC(ItemEval, UIGen) ACMD_NAME(ItemProgression_GetZeroBasedItemLevel);
U32 exprItemProgression_GetZeroBasedItemLevel(SA_PARAM_OP_VALID Item* pItem)
{
	return itemProgression_GetLevel(pItem)-1;
}

//Returns the amount of XP the item has earned towards its next level, not the total amount earned overall.
U32 ItemProgression_GetAdjustedItemXP(SA_PARAM_OP_VALID Item* pItem)
{
	U32 uXP = SAFE_MEMBER2(pItem, pAlgoProps, uProgressionXP);

	if (uXP > 0)
	{
		return uXP - itemProgression_GetXPRequiredForLevel(pItem, itemProgression_GetLevel(pItem));
	}

	return 0;
}

//Returns the amount of XP the item needs for its next level.
U32 ItemProgression_GetAdjustedItemXPRequiredForNextLevel(SA_PARAM_OP_VALID Item* pItem)
{
	U32 uLevel = itemProgression_GetLevel(pItem);
	return itemProgression_GetXPRequiredForLevel(pItem, uLevel+1) - itemProgression_GetXPRequiredForLevel(pItem, uLevel);
}

//Returns the amount of XP the item has earned towards its next level, not the total amount earned overall.
AUTO_EXPR_FUNC(ItemEval, UIGen) ACMD_NAME(ItemProgression_GetAdjustedItemXP);
U32 exprItemProgression_GetAdjustedItemXP(SA_PARAM_OP_VALID Item* pItem)
{
	return ItemProgression_GetAdjustedItemXP(pItem);
}

//Returns the amount of XP the item needs for its next level.
AUTO_EXPR_FUNC(ItemEval, UIGen) ACMD_NAME(ItemProgression_GetItemXPRequiredForNextLevel);
U32 exprItemProgression_GetAdjustedItemXPRequiredForNextLevel(SA_PARAM_OP_VALID Item* pItem)
{
	return ItemProgression_GetAdjustedItemXPRequiredForNextLevel(pItem);
}

//Returns the base amount of XP this item would be worth when eaten.
AUTO_EXPR_FUNC(ItemEval, UIGen) ACMD_NAME(ItemProgression_GetItemXPFoodValue);
U32 exprItemProgression_GetItemXPFoodValue(SA_PARAM_OP_VALID Item* pItem)
{
	return itemProgression_GetFoodXPValue(pItem);
}

S32 itemProgression_ValidateDef_SortTier(const ItemProgressionTierDef** pA, const ItemProgressionTierDef** pB)
{
	return (*pA)->iIndex - (*pB)->iIndex;
}

void itemProgression_ValidateDef_PostBinning(ItemProgressionDef* pDef)
{
	int i, j;
	//sort tiers by min level
	eaQSort(pDef->eaTiers, itemProgression_ValidateDef_SortTier);

	//validate tiers
	for (i = 0; i < eaSize(&pDef->eaTiers); i++)
	{
		ItemProgressionTierDef* pTier = pDef->eaTiers[i];

		//Force designers to type in their own index so they are forced to see how many tiers there are at a glance.
		if (pTier->iIndex != i+1)
		{
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d had an incorrect index %d.h There must not be any duplicates or holes.", pDef->pchName, i+1, pTier->iIndex);
		}

		//ensure level ranges make sense
		if (i > 0 && (pTier->uMinLevel != pDef->eaTiers[i-1]->uMaxLevel+1))
		{
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d had an incorrect minimum level %d. Min level must be equal to the previous tier's max level plus 1.", pDef->pchName, pTier->iIndex, pTier->uMinLevel);
		}
		if (i < eaSize(&pDef->eaTiers)-1 && (pTier->uMaxLevel != pDef->eaTiers[i+1]->uMinLevel-1))
		{
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d had an incorrect maximum level %d. Max level must be equal to the next tier's min level minus 1.", pDef->pchName, pTier->iIndex, pTier->uMaxLevel);
		}
		
		//ensure it isn't impossible to evo
		if (i < eaSize(&pDef->eaTiers)-1 && !pTier->uBaseRankUpChance)
		{
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d has a base rank up chance of 0. This means it will be impossible to evo without a ward.", pDef->pchName, pTier->iIndex);
		}

		//ensure final tier doesn't have catalysts defined, since it can't evo
		if (i == eaSize(&pDef->eaTiers)-1 && eaSize(&pTier->eaCatalysts) > 0)
		{
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d is the final evo tier but specifies one or more evo catalysts. These will have no effect.", pDef->pchName, pTier->iIndex);
		}

		//ensure catalysts are well-formed
		for (j = 0; j < eaSize(&pTier->eaCatalysts); j++)
		{
			if (pTier->eaCatalysts[j]->eType == kItemProgressionCatalystType_None)
				ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d has a catalyst that did not specify a catalyst type.", pDef->pchName, pTier->iIndex);
		}
	}

	//validate crit weights
	for (i = 0; i < eaSize(&pDef->eaCritWeights); i++)
	{
		if (pDef->eaCritWeights[i]->uWeight == 0)
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s has an XPCritWeight without a weight.", pDef->pchName);
	}

	//validate food categories
	for (i = 0; i < eaSize(&pDef->eaFoodCategories); i++)
	{
		if (pDef->eaFoodCategories[i]->fEfficiency == 0.0)
			ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s has a FoodCategory with an efficiency of 0.0. This would allow a player to waste a food item for no reason.", pDef->pchName);
	}

}

void itemProgression_ValidateDef_CheckRefs(ItemProgressionDef* pDef)
{
	int i,j;

	if (!GET_REF(pDef->hFoodValueTable))
	{
		ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s didn't specify a RewardValTable for XP value when used as food.", pDef->pchName);
	}

	if (!GET_REF(pDef->hXPRequiredTable))
	{
		ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s didn't specify a RewardValTable for XP required per level.", pDef->pchName);
	}

	for (i = 0; i < eaSize(&pDef->eaTiers); i++)
	{
		ItemProgressionTierDef* pTier = pDef->eaTiers[i];

		for (j = 0; j < eaSize(&pTier->eaCatalysts); j++)
		{
			if (pTier->eaCatalysts[j]->eType == kItemProgressionCatalystType_RankUpRequirement_SpecificItem && !GET_REF(pTier->eaCatalysts[j]->hItem))
				ErrorFilenamef(pDef->pchFilename, "ItemProgressionDef %s's tier %d has a catalyst that references a non-existent itemdef %s.", pDef->pchName, pTier->iIndex, REF_STRING_FROM_HANDLE(pTier->eaCatalysts[j]->hItem));
		}
	}
}

static int ItemProgressionResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ItemProgressionDef* pDef, U32 userID)
{
	switch (eType)
	{	
	case RESVALIDATE_POST_BINNING:
		{
			itemProgression_ValidateDef_PostBinning(pDef);
		}break;
	case RESVALIDATE_CHECK_REFERENCES:
		{
			if (!isProductionMode()) {
				itemProgression_ValidateDef_CheckRefs(pDef);
				return VALIDATE_HANDLED;
			}
		}break;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(AS_ItemProgression) ASTRT_DEPS(ItemTags, RewardValTables);
void itemProgression_Load(void)
{
	g_hItemProgressionDict = RefSystem_RegisterSelfDefiningDictionary("ItemProgressionDef",false, parse_ItemProgressionDef, true, true, NULL);

	resDictManageValidation(g_hItemProgressionDict, ItemProgressionResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hItemProgressionDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hItemProgressionDict, NULL, NULL, NULL, NULL, NULL);
		}
		resLoadResourcesFromDisk(g_hItemProgressionDict, "defs/Items/", "ItemProgression.def", "ItemProgression.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	} 
	else
	{
		resDictRequestMissingResources(g_hItemProgressionDict, 16, false, resClientRequestSendReferentCommand);
	}
}

#include "../Autogen/itemProgressionCommon_h_ast.c"