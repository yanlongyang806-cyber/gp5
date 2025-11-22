#include "itemSalvage.h"
#include "gclEntity.h"
#include "inventoryCommon.h"
#include "FCInventoryUI.h"
#include "StringCache.h"
#include "Player.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/itemSalvage_h_ast.h"


static ItemSalvageRewardRequestData *s_pRewardRequestData = {0};

// returns true if the item can be salvaged at all.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageIsItemSalvageable);
bool gclItemSalvageExpr_IsItemSalvageable(SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem)
	{
		return ItemSalvage_IsItemSalvageable(pItem);
	}

	return false;
}

// returns true if the current player can currently salvage 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageCanSalvage);
bool gclItemSalvageExpr_CanSalvage()
{
	Entity* pEnt = entActivePlayerPtr();
	
	if (pEnt)
	{
		return ItemSalvage_CanPerformSalvage(pEnt);
	}

	return false;
}

// returns true if the current player can currently salvage 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageSalvageItem);
void gclItemSalvageExpr_SalvageItem(SA_PARAM_OP_VALID Item *pItem)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pItem && ItemSalvage_IsItemSalvageable(pItem))
	{
		if (ItemSalvage_CanPerformSalvage(pEnt))
		{
			ServerCmd_gslItemSalvage_SalvageItem(pItem->id);
		}
		else
		{
			// possible mis-predict with the client moving out of the area 
			const char* pchFailMsg = entTranslateMessageKey(pEnt, "ItemSalvage_Fail_PowerMode");
			notify_NotifySend(pEnt, kNotifyType_ItemSalvageFailure, pchFailMsg, NULL, NULL);
		}
	}

}

// returns true if our current reward request data matches the given item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageHasRequestRewards);
bool gclItemSalvageExpr_HasRequestRewards(SA_PARAM_OP_VALID Item *pItem)
{
	if (s_pRewardRequestData && pItem)
	{
		if (REF_COMPARE_HANDLES(pItem->hItem, s_pRewardRequestData->hDef) && 
			pItem->id == s_pRewardRequestData->ItemID)
			return true;
	}
	
	return false;
}

// returns the count for the given item def name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageGetRequestedRewardCount);
S32 gclItemSalvageExpr_GetRequestedRewardNumeric(SA_PARAM_OP_VALID Item *pItem, const char *pchItemNumeric)
{
	if (s_pRewardRequestData && pItem && s_pRewardRequestData->pData)
	{
		pchItemNumeric = allocFindString(pchItemNumeric);

		if (!pchItemNumeric)
			return 0;

		if (REF_COMPARE_HANDLES(pItem->hItem, s_pRewardRequestData->hDef) && 
			pItem->id == s_pRewardRequestData->ItemID)
		{
			FOR_EACH_IN_EARRAY(s_pRewardRequestData->pData->eaRewards, InventorySlot, pSlot)
			{
				ItemDef *pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
				if (pItemDef && pItemDef->pchName == pchItemNumeric)
				{
					return pSlot->pItem->count;
				}
			}
			FOR_EACH_END
			
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageClearRecievedSalvageData);
void gclItemSalvageExpr_ClearRecievedSalvageData()
{
	if (s_pRewardRequestData)
	{
		StructDestroy(parse_ItemSalvageRewardRequestData, s_pRewardRequestData);
		s_pRewardRequestData = NULL;
	}
}

// sends a command to the server to request rewards for salvaging a particular item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemSalvageRequestRewards);
void gclItemSalvageExpr_RequestSalvageItemRewards(SA_PARAM_OP_VALID Item *pItem)
{
	static U32 s_uNextRewardRequestTime = 0;
	U32 uCurrentTime = timeSecondsSince2000();
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pItem && uCurrentTime >= s_uNextRewardRequestTime)
	{
		if (ItemSalvage_IsItemSalvageable(pItem) && 
			ItemSalvage_CanPerformSalvage(pEnt))
		{
			ServerCmd_gslItemSalvage_RequestRewardsForItemSalvage(pItem->id);
			s_uNextRewardRequestTime = uCurrentTime + 1;
		}
	}
}

// Command received from the server after we've requested the salvaged rewards for a given item
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void gclItemSalvage_ReceiveRewardsForSalvage(ItemSalvageRewardRequestData* pData)
{
	Entity* pEnt = entActivePlayerPtr();
	StructDestroySafe(parse_ItemSalvageRewardRequestData, &s_pRewardRequestData);

	if (pData && pEnt)
	{
		s_pRewardRequestData = StructCreate(parse_ItemSalvageRewardRequestData);
		
		if (s_pRewardRequestData)
		{
			COPY_HANDLE(s_pRewardRequestData->hDef, pData->hDef);
			s_pRewardRequestData->ItemID = pData->ItemID;
			
			if (!s_pRewardRequestData->pData)
				s_pRewardRequestData->pData = StructCreate(parse_InvRewardRequest);

			if (s_pRewardRequestData->pData)
			{
				inv_FillRewardRequestClient(pEnt, pData->pData, s_pRewardRequestData->pData, true);
			}
		}
	}
}