#include "UIGen.h"
#include "StickerBookCommon.h"
#include "itemCommon.h"
#include "StringCache.h"
#include "GameStringFormat.h"

#include "Entity.h"
#include "Player.h"
#include "gclEntity.h"

#include "ExpressionMinimal.h"

#include "AutoGen/itemCommon_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static ItemDefRef **s_eaItemDefRefs = NULL;
bool exprStickerBook_HasCompletedItem(SA_PARAM_NN_STR const char *pchItemName);

// Client requests the updated list of StickerBook participating ItemDefs from the server.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StickerBook_RequestUpdate);
void exprStickerBook_RequestUpdate()
{
	int iSize = 0;
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;

	// Count how many Items we're tracking. Yes, this will double-count any item that's in multiple sets,
	// but we're ok with that. It'll just have 2 refs instead of 1, wasting the memory of a handle.
	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
	{
		FOR_EACH_IN_CONST_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
		{
			iSize += eaSize(&pStickerBookItemSet->ppItems);
			if(pStickerBookItemSet->pchRewardTitleItem && pStickerBookItemSet->pchRewardTitleItem[0])
				iSize++;
		}
		FOR_EACH_END;

		FOR_EACH_IN_CONST_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
		{
			iSize += eaSize(&pStickerBookItemLocation->ppItems);
		}
		FOR_EACH_END;
	}

	// Allocate enough space for all the ItemDefRefs
	eaSetSizeStruct(&s_eaItemDefRefs, parse_ItemDefRef, iSize);

	// Set each ref to an item
	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
	{
		FOR_EACH_IN_CONST_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
		{
			FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
			{
				--iSize;
				SET_HANDLE_FROM_STRING("ItemDef", pStickerBookItem->pchItemName, s_eaItemDefRefs[iSize]->hDef);
			}
			FOR_EACH_END;

			if(pStickerBookItemSet->pchRewardTitleItem && pStickerBookItemSet->pchRewardTitleItem[0])
			{
				iSize--;
				SET_HANDLE_FROM_STRING("ItemDef", pStickerBookItemSet->pchRewardTitleItem, s_eaItemDefRefs[iSize]->hDef);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_CONST_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
		{
			FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
			{
				--iSize;
				SET_HANDLE_FROM_STRING("ItemDef", pStickerBookItem->pchItemName, s_eaItemDefRefs[iSize]->hDef);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	devassert(0 == iSize);
}

static int StickerBook_SortPositionCmp(const StickerBookCollection *const *a, const StickerBookCollection *const *b)
{
	return (*b)->iSortPosition - (*a)->iSortPosition;
}

//Return a list of filtered items
StickerBookItem*** StickerBook_FilteredItemSet(StickerBookItem** itemSet, bool filterCompleted)
{
	static StickerBookItem** eaFiltered = NULL;
	Entity* pEnt = entActivePlayerPtr();
	NOCONST(Entity) *pEntNoConst;

	if(!eaFiltered)
		eaCreate(&eaFiltered);

	eaClear(&eaFiltered);

	//If we don't have a player, return the empty list
	if(!SAFE_MEMBER(pEnt, pPlayer))
		return &eaFiltered;

	pEntNoConst = CONTAINER_NOCONST(Entity, pEnt);

	FOR_EACH_IN_EARRAY(itemSet, StickerBookItem, pStickerBookItem)
		ItemDef* pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);	
		//Deal with restrictions
		if(pItemDef && (!pItemDef->pRestriction || 
			(itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)
			&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL))))
		{
			//Also filter completed
			if(!filterCompleted || !!!eaIndexedGetUsingString(&pEnt->pPlayer->eaStickerBookItemInfo, pItemDef->pchName))
				eaPush(&eaFiltered, pStickerBookItem);
		}
	FOR_EACH_END

	return &eaFiltered;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetItemSetFromRefString");
SA_RET_OP_VALID StickerBookItemSet *exprStickerBook_GetItemSetFromRefString(const char *pchRefString)
{
	return StickerBook_ItemSetGetByRefString(pchRefString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetCollectionFromRefString");
SA_RET_OP_VALID StickerBookCollection *exprStickerBook_GetCollectionFromRefString(const char *pchRefString)
{
	return StickerBook_CollectionGetByRefString(pchRefString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetCollectionList");
void exprStickerBook_GetCollectionList(SA_PARAM_NN_VALID UIGen *pGen)
{
	StickerBookCollection ***peaCollections = ui_GenGetManagedListSafe(pGen, StickerBookCollection);
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;

	eaClear(peaCollections);

	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
		eaPush(peaCollections, pStickerBookCollection);

	eaQSort(*peaCollections, StickerBook_SortPositionCmp);

	ui_GenSetManagedListSafe(pGen, peaCollections, StickerBookCollection, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetFilteredCollectionList");
void exprStickerBook_GetFilteredCollectionList(SA_PARAM_NN_VALID UIGen *pGen, StickerBookCollectionType type)
{
	StickerBookCollection ***peaCollections = ui_GenGetManagedListSafe(pGen, StickerBookCollection);
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;

	eaClear(peaCollections);

	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(type == pStickerBookCollection->eStickerBookCollectionType)
			eaPush(peaCollections, pStickerBookCollection);
	}

	eaQSort(*peaCollections, StickerBook_SortPositionCmp);

	ui_GenSetManagedListSafe(pGen, peaCollections, StickerBookCollection, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetSetList");
void exprStickerBook_GetSetList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCollectionName)
{
	StickerBookCollection *pStickerBookCollection = StickerBook_GetCollection(pchCollectionName);
	bool bDownloaded = true;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pItemSet)
		FOR_EACH_IN_EARRAY(pItemSet->ppItems, StickerBookItem, pStickerItem)
			bDownloaded = bDownloaded && RefSystem_ReferentFromString("ItemDef", pStickerItem->pchItemName);

			if(!bDownloaded)
				break;
		FOR_EACH_END;

		if(pItemSet->pchRewardTitleItem && pItemSet->pchRewardTitleItem[0])
			bDownloaded = bDownloaded && RefSystem_ReferentFromString("ItemDef", pItemSet->pchRewardTitleItem);

		if(!bDownloaded)
			break;
	FOR_EACH_END;

	if(pStickerBookCollection && bDownloaded)
	{
		StickerBook_UpdateItemSetPointers(pStickerBookCollection->ppItemSet);
		ui_GenSetListSafe(pGen, &pStickerBookCollection->ppItemSet, StickerBookItemSet);
	}
	else
		ui_GenSetListSafe(pGen, NULL, StickerBookItemSet);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetLocationList");
void exprStickerBook_GetLocationList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCollectionName)
{
	StickerBookCollection *pStickerBookCollection = StickerBook_GetCollection(pchCollectionName);
	bool bDownloaded = true;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pItemSet)
		FOR_EACH_IN_EARRAY(pItemSet->ppItems, StickerBookItem, pStickerItem)
			bDownloaded = bDownloaded && RefSystem_ReferentFromString("ItemDef", pStickerItem->pchItemName);

			if(!bDownloaded)
				break;
		FOR_EACH_END;

		if(!bDownloaded)
			break;
	FOR_EACH_END;

	if(pStickerBookCollection)
		ui_GenSetListSafe(pGen, &pStickerBookCollection->ppItemLocation, StickerBookItemLocation);
	else
		ui_GenSetListSafe(pGen, NULL, StickerBookItemLocation);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetSetItemList");
void exprStickerBook_GetSetItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet, bool filterCompleted)
{
	if(pStickerBookItemSet)
	{
		StickerBook_UpdateItemPointers(pStickerBookItemSet->ppItems);
		ui_GenSetListSafe(pGen, StickerBook_FilteredItemSet(pStickerBookItemSet->ppItems, filterCompleted), StickerBookItem);
	}
	else
		ui_GenSetListSafe(pGen, NULL, StickerBookItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetLocationItemList");
void exprStickerBook_GetLocationItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation, bool filterCompleted)
{
	if(pStickerBookItemLocation)
	{
		StickerBook_UpdateItemPointers(pStickerBookItemLocation->ppItems);
		ui_GenSetListSafe(pGen, StickerBook_FilteredItemSet(pStickerBookItemLocation->ppItems, filterCompleted), StickerBookItem);
	}
	else
		ui_GenSetListSafe(pGen, NULL, StickerBookItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetItemLocationsString");
SA_RET_NN_STR const char *exprStickerBook_GetItemLocationsString(SA_PARAM_NN_VALID ExprContext* pContext, SA_PARAM_NN_VALID StickerBookItem *pStickerBookItem, SA_PARAM_NN_STR const char *pchCollectionName,
	SA_PARAM_NN_STR const char *pchListMessageKey, SA_PARAM_NN_STR const char *pchItemMessageKey, SA_PARAM_NN_STR const char *pchItemWithSubLocationMessageKey)
{
	StickerBookCollection *pStickerBookCollection = StickerBook_GetCollection(pchCollectionName);
	char *pResult = NULL;
	char *estrItemLocations = NULL;
	char *estrItemLocation = NULL;
	estrStackCreate(&estrItemLocations);
	estrStackCreate(&estrItemLocation);

	if(pStickerBookCollection)
	{
		FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
		{
			// Find this Item in the Location's item list
			FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItemOther)
			{
				if(pStickerBookItemOther->pchItemName == pStickerBookItem->pchItemName)
				{
					estrClear(&estrItemLocation);

					if(REF_STRING_FROM_HANDLE(pStickerBookItemOther->msgSubLocation.hMessage))
						FormatGameMessageKey(&estrItemLocation, pchItemWithSubLocationMessageKey,
							STRFMT_DISPLAYMESSAGE("Location", pStickerBookItemLocation->msgDisplayName),
							STRFMT_DISPLAYMESSAGE("SubLocation", pStickerBookItemOther->msgSubLocation),
							STRFMT_END);
					else
						FormatGameMessageKey(&estrItemLocation, pchItemMessageKey,
							STRFMT_DISPLAYMESSAGE("Location", pStickerBookItemLocation->msgDisplayName),
							STRFMT_END);

					estrAppend2(&estrItemLocations, estrItemLocation);

					break;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	if(estrItemLocations && estrItemLocations[0])
	{
		char *estrResult = NULL;
		estrStackCreate(&estrResult);

		FormatGameMessageKey(&estrResult, pchListMessageKey,
			STRFMT_STRING("Locations", estrItemLocations),
			STRFMT_END);

		pResult = exprContextAllocScratchMemory(pContext, estrLength(&estrResult));
		memcpy(pResult, estrResult, estrLength(&estrResult) + 1);

		estrDestroy(&estrResult);
	}

	estrDestroy(&estrItemLocations);
	estrDestroy(&estrItemLocation);

	return NULL_TO_EMPTY(pResult);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetItemSetsString");
SA_RET_NN_STR const char *exprStickerBook_GetItemSetsString(SA_PARAM_NN_VALID ExprContext* pContext, SA_PARAM_NN_VALID StickerBookItem *pStickerBookItem, SA_PARAM_NN_STR const char *pchCollectionName,
	SA_PARAM_NN_STR const char *pchListMessageKey, SA_PARAM_NN_STR const char *pchItemWithPointsMessageKey, SA_PARAM_NN_STR const char *pchItemWithoutPointsMessageKey)
{
	StickerBookCollection *pStickerBookCollection = StickerBook_GetCollection(pchCollectionName);
	char *pResult = NULL;
	char *estrItemSets = NULL;
	char *estrItemSet = NULL;
	estrStackCreate(&estrItemSets);
	estrStackCreate(&estrItemSet);

	if(pStickerBookCollection)
	{
		FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
		{
			// Find this Item in the Location's item list
			FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItemOther)
			{
				if(pStickerBookItemOther->pchItemName == pStickerBookItem->pchItemName)
				{
					estrClear(&estrItemSet);
					if(pStickerBookItemOther->iPoints > 0)
					{
						FormatGameMessageKey(&estrItemSet, pchItemWithPointsMessageKey,
							STRFMT_DISPLAYMESSAGE("Set", pStickerBookItemSet->msgDisplayName),
							STRFMT_INT("Points", pStickerBookItemOther->iPoints),
							STRFMT_END);
					}
					else
					{
						FormatGameMessageKey(&estrItemSet, pchItemWithoutPointsMessageKey,
							STRFMT_DISPLAYMESSAGE("Set", pStickerBookItemSet->msgDisplayName),
							STRFMT_END);
					}

					estrAppend2(&estrItemSets, estrItemSet);

					break;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	if(estrItemSets && estrItemSets[0])
	{
		char *estrResult = NULL;
		estrStackCreate(&estrResult);

		FormatGameMessageKey(&estrResult, pchListMessageKey,
			STRFMT_STRING("Sets", estrItemSets),
			STRFMT_END);

		pResult = exprContextAllocScratchMemory(pContext, estrLength(&estrResult));
		memcpy(pResult, estrResult, estrLength(&estrResult) + 1);

		estrDestroy(&estrResult);
	}

	estrDestroy(&estrItemSets);
	estrDestroy(&estrItemSet);

	return NULL_TO_EMPTY(pResult);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetTotalPoints");
U32 exprStickerBook_GetTotalPoints()
{
	return StickerBook_CountTotalPoints(entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetTotalPointsForCollectionType");
U32 exprStickerBook_GetTotalPointsForCollectionType(ACMD_EXPR_ENUM(StickerBookCollectionType) const char *typeName)
{
	StickerBookCollectionType type = (StickerBookCollectionType)StaticDefineIntGetInt(StickerBookCollectionTypeEnum, typeName);
	if(0 == type)
		return 0;
	return StickerBook_CountTotalPointsForCollectionType(type, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetTotalPointsForCollection");
U32 exprStickerBook_GetTotalPointsForCollection(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection)
{
	return StickerBook_CountTotalPointsForCollection(pStickerBookCollection, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetTotalPointsForSet");
U32 exprStickerBook_GetTotalPointsForSet(SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet)
{
	return StickerBook_CountTotalPointsForSet(pStickerBookItemSet, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetTotalPointsForLocation");
U32 exprStickerBook_GetTotalPointsForLocation(SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation)
{
	return StickerBook_CountTotalPointsForLocation(pStickerBookItemLocation, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_HasCompletedItem");
bool exprStickerBook_HasCompletedItem(SA_PARAM_NN_STR const char *pchItemName)
{
	if(pchItemName)
	{
		Entity *pEnt = entActivePlayerPtr();
		if(SAFE_MEMBER(pEnt, pPlayer))
			return !!eaIndexedGetUsingString(&pEnt->pPlayer->eaStickerBookItemInfo, allocAddString(pchItemName));
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_CollectionHasLocations");
bool exprStickerBook_CollectionHasLocations(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection)
{
	return pStickerBookCollection && eaSize(&pStickerBookCollection->ppItemLocation) > 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetPoints");
U32 exprStickerBook_GetPoints()
{
	return StickerBook_CountPoints(entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetPointsForCollectionType");
U32 exprStickerBook_GetPointsForCollectionType(ACMD_EXPR_ENUM(StickerBookCollectionType) const char *typeName)
{
	StickerBookCollectionType type = (StickerBookCollectionType)StaticDefineIntGetInt(StickerBookCollectionTypeEnum, typeName);
	if(0 == type)
		return 0;
	return StickerBook_CountPointsForCollectionType(type, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetPointsForCollection");
U32 exprStickerBook_GetPointsForCollection(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection)
{
	return StickerBook_CountPointsForCollection(pStickerBookCollection, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetPointsForSet");
U32 exprStickerBook_GetPointsForSet(SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet)
{
	return StickerBook_CountPointsForSet(pStickerBookItemSet, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_GetPointsForLocation");
U32 exprStickerBook_GetPointsForLocation(SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation)
{
	return StickerBook_CountPointsForLocation(pStickerBookItemLocation, entActivePlayerPtr(), /*pbFullCount=*/NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_HasUnviewedChanges");
bool exprStickerBook_HasUnviewedChanges()
{
	Entity *pEnt = entActivePlayerPtr();
	return (U32)SAFE_MEMBER3(pEnt, pPlayer, pUI, uiStickerBookHasUnviewedChanges);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StickerBook_Viewed");
void exprStickerBook_Viewed()
{
	// Prevents a little spam
	Entity *pEnt = entActivePlayerPtr();
	if(SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		if(pEnt->pPlayer->pUI->uiStickerBookHasUnviewedChanges)
		{
			pEnt->pPlayer->pUI->uiStickerBookHasUnviewedChanges = false;

			ServerCmd_gslStickerBook_SetHasUnviewedChanges(false);
		}
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(9);
void StickerBook_Reset()
{
	Entity *pEnt = entActivePlayerPtr();
	if(SAFE_MEMBER(pEnt, pPlayer))
		StructDestroySafe(parse_StickerBookPointCache, &pEnt->pPlayer->pStickerBookPointCache);

	ServerCmd_StickerBook_Reset();
}
