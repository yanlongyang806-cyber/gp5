#include "itemSalvage.h"
#include "Entity.h"
#include "EntityLib.h"
#include "inventoryCommon.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "LoggedTransactions.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "Reward.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/inventoryCommon_h_ast.h"

extern ItemSalvageDef g_ItemSalvageDef;

typedef struct ItemSalvageCBData
{
	ContainerID	entityID;

	REF_TO(ItemDef) hSalvagedItemDef;
	
	InventoryBag** eaRewardBags;
		
} ItemSalvageCBData;



AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pplayer.Eaplayernumericthresholds, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Pplayer.Eaastrrecentlyacquiredstickerbookitems");
enumTransactionOutcome item_tr_SalvageItem(ATR_ARGS, 
											NOCONST(Entity)* pEnt,
											S32 eSalvagedItemBagID,
											S32 eSalvagedItemBagSlot,
											U64 uSalvagedItemItemID,
											const char *pchRewardTableName,
											GiveRewardBagsData* pRewardBagsData,
											const ItemChangeReason *pReason,
											GameAccountDataExtract* pExtract)
{
	NOCONST(Item)* pItem;
	ItemDef *pItemDef = NULL;
	RewardTable *pRewardTable;

	// get the item we are going to be destroying and make sure it is the right one
	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, eSalvagedItemBagID, eSalvagedItemBagSlot, pExtract);

	if (ISNULL(pItem) || pItem->id != uSalvagedItemItemID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (!ItemSalvage_trh_IsItemSalvageable(pItem))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (!pchRewardTableName)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pRewardTable = ItemSalvage_trh_GetRewardTableForItem(pItem);
	if (!pRewardTable || stricmp(pRewardTable->pchName, pchRewardTableName) != 0)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}


	pItemDef = GET_REF(pItem->hItem);

	// try and remove the item from the inventory
	pItem = CONTAINER_NOCONST(Item, invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, eSalvagedItemBagID, 
														eSalvagedItemBagSlot, 1, false, pReason, pExtract));
	if (ISNULL(pItem))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	StructDestroyNoConst(parse_Item, pItem);

	// give the rewards 
	if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, NULL, pRewardBagsData, kRewardOverflow_AllowOverflowBag, 
								NULL, pReason, pExtract, NULL))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_ITEM_SALVAGE, "ItemSalvaged", 
											"ItemSalvaged %s RewardTable %s", 
											pItemDef ? pItemDef->pchName : "error", 
											pRewardTable->pchName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// --------------------------------------------------------------------------------------------------------------------
static ItemSalvageCBData *gslItemSalvage_CreateCBData(Entity *pEnt)
{
	ItemSalvageCBData *pCBData = calloc(1, sizeof(ItemSalvageCBData));
	if (pCBData)
	{
		pCBData->entityID = entGetContainerID(pEnt);
	}

	return pCBData;
}

// --------------------------------------------------------------------------------------------------------------------
static void gslItemSalvage_DestroyCBData(ItemSalvageCBData **ppCBData)
{
	if (*ppCBData)
	{
		eaDestroyStruct(&(*ppCBData)->eaRewardBags, parse_InventoryBag);
		REMOVE_HANDLE((*ppCBData)->hSalvagedItemDef);
		free(*ppCBData);
		*ppCBData = NULL;
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void ItemSalvage_CB(TransactionReturnVal* pReturn, ItemSalvageCBData* pCBData)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pCBData->entityID);
	
	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		ItemDef *pSalvagedItemDef = GET_REF(pCBData->hSalvagedItemDef);

		// Send the reward data to the client 
		{
			ItemRewardPackTransferData* pData = StructCreate(parse_ItemRewardPackTransferData);

			COPY_HANDLE(pData->hRewardPackItem, pCBData->hSalvagedItemDef);
			pData->pRewards = StructCreate(parse_InvRewardRequest);

			pData->ePackResultQuality = inv_FillRewardRequest(pCBData->eaRewardBags, pData->pRewards);

			// use the salvaged itemDef's quality if we have the itemDef
			if (pSalvagedItemDef)
				pData->ePackResultQuality = pSalvagedItemDef->Quality;

			pData->eRewardPackType = kItemRewardPackType_SalvagedItem;

			if (eaSize(&pData->pRewards->eaItemRewards) || eaSize(&pData->pRewards->eaNumericRewards))
			{
				ClientCmd_gclReceiveRewardPackData(pEnt, pData);
			}

			StructDestroy(parse_ItemRewardPackTransferData, pData);
		}
		
		// Send the unpack message
		if (pSalvagedItemDef)
		{
			char* pchSalvagedMsg = NULL;
			entFormatGameMessageKey(pEnt, &pchSalvagedMsg, "ItemSalvage_Success", 
											STRFMT_ITEMDEF_KEY("SalvagedItem", pSalvagedItemDef),
											STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_ItemSalvageSuccess, pchSalvagedMsg, pSalvagedItemDef->pchName, NULL);
			estrDestroy(&pchSalvagedMsg);
		}
	
	}
	else if (pEnt) // Failure
	{
		ItemDef *pSalvagedItemDef = GET_REF(pCBData->hSalvagedItemDef);

		// Send the unpack failure message
		if (pSalvagedItemDef)
		{
			char* pchFailMsg = NULL;
			entFormatGameMessageKey(pEnt, &pchFailMsg, "ItemSalvage_Fail",
											STRFMT_ITEMDEF_KEY("SalvagedItem", pSalvagedItemDef),
											STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_ItemSalvageFailure, pchFailMsg, pSalvagedItemDef->pchName, NULL);
			estrDestroy(&pchFailMsg);
		}
	}

	gslItemSalvage_DestroyCBData(&pCBData);
}

// --------------------------------------------------------------------------------------------------------------------
static bool gslItemSalvage_GenerateRewardBags(Entity *pEnt, Item *pItem, RewardTable *pRewardTable, InventoryBag ***peaRewardBags)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	S32 iLevel = item_GetLevel(pItem);

	reward_GenerateBagsForItemSalvage(iPartitionIdx, pEnt, pRewardTable, iLevel, peaRewardBags);
	return (eaSize(peaRewardBags) != 0);
}

// --------------------------------------------------------------------------------------------------------------------
static bool gslItemSalvage_SalvageItemInternal(Entity *pEnt, Item *pItem, ItemDef *pItemDef, RewardTable *pRewardTable)
{
	InvBagIDs eBagID = InvBagIDs_None;
	int iSlotIdx = 0;
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Item *pTestItem = inv_GetItemAndSlotsByID(pEnt, pItem->id, &eBagID, &iSlotIdx);

	if (!pTestItem)
	{
		// this really shouldn't happen!
		return false;
	}
	
	if (pRewardTable)
	{
		ItemSalvageCBData* pCBData = gslItemSalvage_CreateCBData(pEnt);
		GiveRewardBagsData Rewards = {0};
				
		if (!gslItemSalvage_GenerateRewardBags(pEnt, pItem, pRewardTable, &pCBData->eaRewardBags))
		{
			ErrorDetailsf("%s\nSalvaged Item: %s; Reward Table: %s", ENTDEBUGNAME(pEnt), pItemDef->pchName, pRewardTable->pchName);
			Errorf("Salvage RewardTable gave no rewards");
			gslItemSalvage_DestroyCBData(&pCBData);
			return false;
		}

		COPY_HANDLE(pCBData->hSalvagedItemDef, pItem->hItem);
		Rewards.ppRewardBags = pCBData->eaRewardBags;

		// call the transaction
		{
			ItemChangeReason reason = {0};
			GameAccountDataExtract *pExtract = NULL;
			TransactionReturnVal* pReturn = NULL;
				
			pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemSalvage", pEnt, ItemSalvage_CB, pCBData);
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			inv_FillItemChangeReason(&reason, pEnt, "Item:SalvagedItem", pItemDef ? pItemDef->pchName : NULL);
			
			AutoTrans_item_tr_SalvageItem(pReturn, objServerType(), 
				GLOBALTYPE_ENTITYPLAYER, pCBData->entityID,
				eBagID, iSlotIdx, pItem->id, pRewardTable->pchName, &Rewards, &reason, pExtract);
		}

		return true;
	}

	return false;
}



// --------------------------------------------------------------------------------------------------------------------
// makes sure the item is salvageable, that we can perform the salvage and then returns the reward table 
static RewardTable* gslItemSalvage_ValidateAndGetRewardTableForItem(Entity *pEnt, Item *pItem, 
																	ItemDef *pItemDef, bool bCheckCanPerform, 
																	EItemSalvageFailReason *pFailOut)
{
	RewardTable *pRewardTable = NULL;
	bool bFailed = false;

	*pFailOut = EItemSalvageFailReason_NONE;
		 
	// see if the item is salvageable
	// see what reward we will get from it
	if (!ItemSalvage_IsItemSalvageable(pItem))
	{
		*pFailOut = EItemSalvageFailReason_CANNOT_SALVAGE;
		return NULL;
	}

	if (bCheckCanPerform && !ItemSalvage_CanPerformSalvage(pEnt))
	{
		*pFailOut = EItemSalvageFailReason_CANNOT_PERFORM;
		return NULL;
	}

	// find the correct reward table
	return ItemSalvage_GetRewardTableForItem(pItem);
}


// --------------------------------------------------------------------------------------------------------------------
// called when the client first clicks to salvage an item. This will generate the rewards (Assumes reward table is not random!)
// and then sends the rewards back to the client
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_SERVERCMD ACMD_PRIVATE;
void gslItemSalvage_RequestRewardsForItemSalvage(Entity *pEnt, U64 uItemID)
{
	if (pEnt && g_ItemSalvageDef.pSalvageableCheck)
	{
		Item *pItem = inv_GetItemByID(pEnt, uItemID);
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		RewardTable *pRewardTable = NULL;
		EItemSalvageFailReason eFailReason;

		if (!pItemDef)
		{
			return;
		}

		pRewardTable = gslItemSalvage_ValidateAndGetRewardTableForItem(pEnt, pItem, pItemDef, false, &eFailReason);
		if (!pRewardTable)
		{
			return;
		}
	
		// generate the rewards and then send them to the client
		{
			InventoryBag** eaRewardBags = NULL;
			ItemSalvageRewardRequestData* pRewardRequestData = NULL;

			if (!gslItemSalvage_GenerateRewardBags(pEnt, pItem, pRewardTable, &eaRewardBags))
			{
				eaDestroyStruct(&eaRewardBags, parse_InventoryBag);
				return;
			}
			
			// create and fill out the reward request data, then send it to the client
			pRewardRequestData = StructCreate(parse_ItemSalvageRewardRequestData);
			
			COPY_HANDLE(pRewardRequestData->hDef, pItem->hItem);
			pRewardRequestData->pData = StructCreate(parse_InvRewardRequest);
			pRewardRequestData->ItemID = pItem->id;
			
			inv_FillRewardRequest(eaRewardBags, pRewardRequestData->pData);

			// here you go mr. client 
			ClientCmd_gclItemSalvage_ReceiveRewardsForSalvage(pEnt, pRewardRequestData);
			
			StructDestroy(parse_ItemSalvageRewardRequestData, pRewardRequestData);
			eaDestroyStruct(&eaRewardBags, parse_InventoryBag);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_SERVERCMD ACMD_PRIVATE;
void gslItemSalvage_SalvageItem(Entity *pEnt, U64 uItemID)
{
	if (pEnt && g_ItemSalvageDef.pSalvageableCheck)
	{
		Item *pItem = inv_GetItemByID(pEnt, uItemID);
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		RewardTable *pRewardTable = NULL;
		EItemSalvageFailReason eFailReason;
		bool bFailed = false;

		if (!pItemDef)
		{
			const char* pchFailMsg = entTranslateMessageKey(pEnt, "ItemSalvage_Fail_CouldNotFindItem");
			notify_NotifySend(pEnt, kNotifyType_ItemSalvageFailure, pchFailMsg, NULL, NULL);
			return;
		}

		pRewardTable = gslItemSalvage_ValidateAndGetRewardTableForItem(pEnt, pItem, pItemDef, true, &eFailReason);
		
		if (pRewardTable)
		{
			if (!gslItemSalvage_SalvageItemInternal(pEnt, pItem, pItemDef, pRewardTable))
			{
				pRewardTable = NULL;
			}
		}

		if (!pRewardTable)
		{
			switch (eFailReason)
			{
				case EItemSalvageFailReason_NONE:
				case EItemSalvageFailReason_CANNOT_SALVAGE:
				{
					char* pchFailMsg = NULL;
					entFormatGameMessageKey(pEnt, &pchFailMsg, "ItemSalvage_Fail_ItemNotSalvageable", 
											STRFMT_ITEMDEF_KEY("SalvagedItem", pItemDef),
											STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_ItemSalvageFailure, pchFailMsg, pItemDef->pchName, NULL);
					estrDestroy(&pchFailMsg);
				} break;

				case EItemSalvageFailReason_CANNOT_PERFORM:
				{
					// possible mis-predict with the client moving out of the area 
					const char* pchFailMsg = entTranslateMessageKey(pEnt, "ItemSalvage_Fail_PowerMode");
					notify_NotifySend(pEnt, kNotifyType_ItemSalvageFailure, pchFailMsg, NULL, NULL);
				} break;
			}
			
			return;
		}
	}
}



