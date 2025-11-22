/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "gslGatewaySession.h"

#include "entity.h"
#include "itemServer.h"
#include "inventoryCommon.h"
#include "autogen/itemEnums_h_ast.h"
#include "itemCommon.h"
#include "gslNumericConversion.h"
#include "gslUGCTips.h"

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayInventory_OpenRewardPack) ACMD_LIST(gGatewayCmdList) ACMD_GLOBAL;
void GatewayInventory_OpenRewardPack(Entity *pent, U64 uItemID)
{
	int iBagID = -1;
	int iSlot = -1;
	BagIterator *iter = bagiterator_Create();
	Item *pItem;
	ItemDef *pItemDef;

	if(!pent || uItemID == 0)
		return;

	if(!inv_trh_FindItemByIDEx(ATR_EMPTY_ARGS, (NOCONST(Entity) *)pent, iter, uItemID, false, true))
	{
		verbose_printf("GatewayInventory_OpenRewardPack: Can't find item %I64x.", uItemID);
		bagiterator_Destroy(iter);

		return;
	}

	iBagID = bagiterator_GetCurrentBagID(iter);
	iSlot = bagiterator_GetSlotID(iter);
	pItem = (Item*)bagiterator_GetItem(iter);
	bagiterator_Destroy(iter);

	pItemDef = GET_REF(pItem->hItem);

	if(pItemDef && pItemDef->eType == kItemType_RewardPack)
		ItemOpenRewardPackWithChoices(1, pent, pItem, pItemDef, iBagID, iSlot, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayInventory_SortBag) ACMD_LIST(gGatewayCmdList) ACMD_GLOBAL;
void GatewayInventory_SortBag(Entity* pent, const char *pchBagID, S32 bCombineStacks)
{
	S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pchBagID);

	if(pent && iBagID >= 0)
	{
		gslBagSort(pent, iBagID, !!bCombineStacks);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayInventory_DiscardItem) ACMD_LIST(gGatewayCmdList) ACMD_GLOBAL;
void GatewayInventory_DiscardItem(Entity *pent, U64 uItemID, U32 uCount)
{
	int iBagID = -1;
	int iSlot = -1;
	BagIterator *iter = bagiterator_Create();

	if(!pent || uItemID == 0 || uCount > INT_MAX)
		return;

	if(!inv_trh_FindItemByIDEx(ATR_EMPTY_ARGS, (NOCONST(Entity) *)pent, iter, uItemID, false, true))
	{
		verbose_printf("GatewayInventory_DiscardItem: Can't find item %I64x.", uItemID);
		bagiterator_Destroy(iter);

		return;
	}
	iBagID = bagiterator_GetCurrentBagID(iter);
	iSlot = bagiterator_GetSlotID(iter);
	bagiterator_Destroy(iter);

	if(iBagID > 0 && iSlot >= 0)
	{
		item_RemoveFromBag(pent, iBagID, iSlot, uCount, "Reward.YouLostItem");
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Gateway_ConvertNumeric) ACMD_LIST(gGatewayCmdList);
void gslGateway_ConvertNumeric(Entity *pEnt, const char *conversionName)
{
	gslConvertNumeric(pEnt, conversionName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Gateway_WithdrawTips) ACMD_LIST(gGatewayCmdList);
void gslGateway_WithdrawTips(Entity *pEnt, U32 iAmount)
{
	gslUGCTipsWithdraw(pEnt, iAmount);
}
// End of File
