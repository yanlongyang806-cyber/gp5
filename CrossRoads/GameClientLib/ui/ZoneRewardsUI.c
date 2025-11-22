#include "UIGen.h"
#include "ZoneRewardsCommon.h"
#include "itemCommon.h"

#include "ExpressionMinimal.h"

#include "AutoGen/ZoneRewardsCommon_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static ItemDefRef ** s_eaItemDefRefs;

// Client requests the updated list of ZoneRewardsDefs from the server.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ZoneRewards_RequestUpdate);
void exprZoneRewards_RequestUpdate()
{
	int iSize = 0;
	RefDictIterator iter;
	ZoneRewardsDef *pZoneRewardsDef;

	// Count how many Items we're tracking. Yes, this will double-count any item that's in multiple sets/sources,
	// but we're ok with that. It'll just have 2 refs instead of 1, wasting the memory of a handle.
	RefSystem_InitRefDictIterator(g_hZoneRewardsDictionary, &iter);
	while( pZoneRewardsDef = RefSystem_GetNextReferentFromIterator(&iter) )
	{
		FOR_EACH_IN_CONST_EARRAY(pZoneRewardsDef->ppItemSet, ZoneRewardsItemSet, pItemSet)
		{
			iSize += eaSize(&pItemSet->ppItemDropInfo);
		}
		FOR_EACH_END
		
		FOR_EACH_IN_CONST_EARRAY(pZoneRewardsDef->ppItemSource, ZoneRewardsItemSource, pItemSource)
		{
			iSize += eaSize(&pItemSource->ppItemDropInfo);
		}
		FOR_EACH_END
	}
	
	// Allocate enough space for all the ItemDefRefs
	eaSetSizeStruct(&s_eaItemDefRefs, parse_ItemDefRef, iSize);

	// Set each ref to an item
	RefSystem_InitRefDictIterator(g_hZoneRewardsDictionary, &iter);
	while( pZoneRewardsDef = RefSystem_GetNextReferentFromIterator(&iter) )
	{
		FOR_EACH_IN_CONST_EARRAY(pZoneRewardsDef->ppItemSet, ZoneRewardsItemSet, pItemSet)
		{
			FOR_EACH_IN_EARRAY(pItemSet->ppItemDropInfo, ZoneRewardsItemDropInfo, pDropInfo)
			{
				--iSize;
				SET_HANDLE_FROM_STRING("ItemDef", pDropInfo->pchItemName, s_eaItemDefRefs[iSize]->hDef);
			}
			FOR_EACH_END
		}
		FOR_EACH_END

		FOR_EACH_IN_CONST_EARRAY(pZoneRewardsDef->ppItemSource, ZoneRewardsItemSource, pItemSource)
		{
			FOR_EACH_IN_EARRAY(pItemSource->ppItemDropInfo, ZoneRewardsItemDropInfo, pDropInfo)
			{
				--iSize;
				SET_HANDLE_FROM_STRING("ItemDef", pDropInfo->pchItemName, s_eaItemDefRefs[iSize]->hDef);
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ZoneRewards_GetRewardsDefsList");
void exprZoneRewards_GetRewardsDefsList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ZoneRewardsDef ***peaDefs = ui_GenGetManagedListSafe(pGen, ZoneRewardsDef);
	RefDictIterator iter;
	ZoneRewardsDef *pRewardsDef;

	eaClear(peaDefs);

	RefSystem_InitRefDictIterator(g_hZoneRewardsDictionary, &iter);
	while( pRewardsDef = RefSystem_GetNextReferentFromIterator(&iter) )
	{
		eaPush(peaDefs, pRewardsDef);
	}

	ui_GenSetManagedListSafe(pGen, peaDefs, ZoneRewardsDef, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ZoneRewards_GetSetsList");
void exprZoneRewards_GetSetsList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchZoneRewardsDefName)
{
	ZoneRewardsDef *pZoneRewardsDef = ZoneRewards_GetRewardsDef(pchZoneRewardsDefName);
	if( pZoneRewardsDef )
	{
		ui_GenSetListSafe(pGen, &pZoneRewardsDef->ppItemSet, ZoneRewardsItemSet);
	}
	else
	{
		ui_GenSetListSafe(pGen, NULL, ZoneRewardsItemSet);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ZoneRewards_GetItemSourcesList");
void exprZoneRewards_GetItemSourcesList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchZoneRewardsDefName)
{
	ZoneRewardsDef *pZoneRewardsDef = ZoneRewards_GetRewardsDef(pchZoneRewardsDefName);
	if( pZoneRewardsDef )
	{
		ui_GenSetListSafe(pGen, &pZoneRewardsDef->ppItemSource, ZoneRewardsItemSource);
	}
	else
	{
		ui_GenSetListSafe(pGen, NULL, ZoneRewardsItemSource);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ZoneRewards_GetItemSetDropInfo");
void exprZoneRewards_GetItemSetDropInfo(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ZoneRewardsItemSet *pItemSet)
{
	if( pItemSet )
	{
		ZoneRewards_UpdateItemDropInfoItemPointers(pItemSet->ppItemDropInfo);
		ui_GenSetListSafe(pGen, &pItemSet->ppItemDropInfo, ZoneRewardsItemDropInfo);
	}
	else
	{
		ui_GenSetListSafe(pGen, NULL, ZoneRewardsItemDropInfo);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ZoneRewards_GetItemSourceDropInfo");
void exprZoneRewards_GetItemSourceDropInfo(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ZoneRewardsItemSource *pItemSource)
{
	if( pItemSource )
	{
		ZoneRewards_UpdateItemDropInfoItemPointers(pItemSource->ppItemDropInfo);
		ui_GenSetListSafe(pGen, &pItemSource->ppItemDropInfo, ZoneRewardsItemDropInfo);
	}
	else
	{
		ui_GenSetListSafe(pGen, NULL, ZoneRewardsItemDropInfo);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ZoneRewards_GetItemSourcesStringFromDropInfo");
SA_RET_NN_STR const char*exprZoneRewards_GetItemSourcesStringFromDropInfo(SA_PARAM_NN_VALID ExprContext* pContext, SA_PARAM_NN_VALID ZoneRewardsItemDropInfo *pItemDropInfo, SA_PARAM_NN_STR const char *pchZoneRewardsDefName)
{
	ZoneRewardsDef *pZoneRewardsDef = ZoneRewards_GetRewardsDef(pchZoneRewardsDefName);
	U32 uCount = 0;
	char *pResult = NULL;
	char *estrItemSources = NULL;
	estrStackCreate(&estrItemSources);

	if( pZoneRewardsDef )
	{
		FOR_EACH_IN_EARRAY(pZoneRewardsDef->ppItemSource, ZoneRewardsItemSource, pItemSource)
		{
			// Find this Item in the ItemSource's item list
			int iIndex = eaSize(&pItemSource->ppItemDropInfo)-1;
			for( ; iIndex>=0 ; --iIndex )
			{
				if( pItemSource->ppItemDropInfo[iIndex]->pchItemName == pItemDropInfo->pchItemName )
				{
					if( uCount == 0 )
					{
						estrAppend2(&estrItemSources, "<ul>");
					}

					uCount++;
					estrConcatf(&estrItemSources, "<li>%s</li>", TranslateDisplayMessage(pItemSource->msgDisplayName));
					break;
				}
			}
		}
		FOR_EACH_END
	}

	if( uCount > 0 )
	{
		estrAppend2(&estrItemSources, "</ul>");

		pResult = exprContextAllocScratchMemory(pContext, strlen(estrItemSources) + 1);
		memcpy(pResult, estrItemSources, strlen(estrItemSources) + 1);
	}

	estrDestroy(&estrItemSources);

	return NULL_TO_EMPTY(pResult);
}
