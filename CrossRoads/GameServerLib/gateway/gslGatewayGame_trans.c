#include "AutoTransDefs.h"
#include "inventoryCommon.h"
#include "gslGatewayGame.h"
#include "Reward.h"
#include "Entity.h"
#include "EString.h"

#include "inventoryCommon_h_ast.h"
#include "AutoGen/gslGatewayGame_h_ast.h"
#include "Entity_h_ast.h"

AUTO_TRANSACTION 
	ATR_LOCKS(pData, ".Prewardbag, .Plastqueuedrewardbag");
enumTransactionOutcome GatewayGame_QueueRewardBag(ATR_ARGS, NOCONST(GatewayGameData) *pData, InventoryBagGroup *pRewardBags)
{
	int i,n;
	int iCritterPetXP = 0;

	if(ISNULL(pData->pRewardBag))
	{
		pData->pRewardBag = (NOCONST(InventoryBag)*)rewardbag_Create();	
	}

	StructDestroyNoConstSafe(parse_InventoryBag,&pData->pLastQueuedRewardBag);

	for(i=0;i<eaSize(&pRewardBags->eaBags);i++)
	{
		for(n=0;n<eaSize(&pRewardBags->eaBags[i]->ppIndexedInventorySlots);n++)
		{
			Item *pItem = pRewardBags->eaBags[i]->ppIndexedInventorySlots[n]->pItem;

			if(pItem)
			{
				if(!pData->pLastQueuedRewardBag)
				{
					pData->pLastQueuedRewardBag = (NOCONST(InventoryBag)*)rewardbag_Create();
				}
				rewardbag_trh_AddItem(pData->pRewardBag,StructClone(parse_Item,pItem),false);
				rewardbag_trh_AddItem(pData->pLastQueuedRewardBag,StructClone(parse_Item,pItem),false);
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pData, ".Prewardbag, .Plastqueuedrewardbag");
enumTransactionOutcome GatewayGame_DiscardQueuedRewardBag(ATR_ARGS, NOCONST(GatewayGameData) *pData)
{
	StructDestroyNoConstSafe(parse_InventoryBag,&pData->pRewardBag);
	StructDestroyNoConstSafe(parse_InventoryBag,&pData->pLastQueuedRewardBag);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Uiindexbuild, .Pinventoryv2.Ppinventorybags, .Psaved.Ppbuilds, .Pchar.Ilevelexp, .Pplayer.Eaplayernumericthresholds, .Pplayer.Eaastrrecentlyacquiredstickerbookitems, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets")
	ATR_LOCKS(pData, ".Icontainerid, .Prewardbag, .Plastqueuedrewardbag");
enumTransactionOutcome GatewayGame_ClaimQueuedRewardBag(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GatewayGameData) *pData, ItemChangeReason *pReason, GameAccountDataExtract *pExtract, ItemIDList *superCrittersIds)
{
	GiveRewardBagsData *pRewardsData = StructCreate(parse_GiveRewardBagsData);
	int i,iCritterPetXP = 0;
	//These must match to make sure the correct entity is getting the rewards
	if(pEnt->myContainerID != pData->iContainerID)
	{
		StructDestroySafe(parse_GiveRewardBagsData,&pRewardsData);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	//No rewards to give, success!
	if(ISNULL(pData->pRewardBag))
	{
		StructDestroySafe(parse_GiveRewardBagsData,&pRewardsData);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	
	//The reward bag info is not persisted, so let's make a new one
	pData->pRewardBag->pRewardBagInfo = rewardbaginfo_Create();
	pData->pRewardBag->pRewardBagInfo->PickupType = kRewardPickupType_Direct;

	StructDestroyNoConstSafe(parse_InventoryBag,&pData->pLastQueuedRewardBag);

	//Remove critter xp items
	for(i=eaSize(&pData->pRewardBag->ppIndexedInventorySlots)-1;i>=0;i--)
	{
		NOCONST(Item) *pItem = pData->pRewardBag->ppIndexedInventorySlots[i]->pItem;

		if(pItem)
		{
			if(GET_REF(pItem->hItem) == GET_REF(g_RewardConfig.hGatewaySuperCritterPetXP))
			{
				iCritterPetXP += pItem->count;
				StructDestroyNoConstSafe(parse_InventorySlot,&pData->pRewardBag->ppIndexedInventorySlots[i]);
				eaRemove(&pData->pRewardBag->ppIndexedInventorySlots,i);
			}
		}
	}

	eaPush(&pRewardsData->ppRewardBags,(InventoryBag*)pData->pRewardBag);
	pData->pRewardBag = NULL;

	if(iCritterPetXP)
	{
		int iTotalCritters = eaSize(&superCrittersIds->ppIDs);

		for(i=0;i<iTotalCritters;i++)
		{
			BagIterator* pIter = bagiterator_Create();

			if(inv_trh_FindItemByIDEx(ATR_PASS_ARGS,pEnt,pIter,superCrittersIds->ppIDs[i]->uiID,false,true))
			{
				NOCONST(Item)* pSCPItem = bagiterator_GetItem(pIter);

				if(pSCPItem && pSCPItem->pSpecialProps && pSCPItem->pSpecialProps->pSuperCritterPet)
				{
					pSCPItem->pSpecialProps->pSuperCritterPet->uXP += iCritterPetXP / iTotalCritters;
				}
			}

			bagiterator_Destroy(pIter);
		}
	}

	if(!inv_trh_GiveRewardBags(ATR_PASS_ARGS,pEnt,NULL,pRewardsData,1,NULL,pReason,pExtract,NULL))
	{
		StructDestroySafe(parse_GiveRewardBagsData,&pRewardsData);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroySafe(parse_GiveRewardBagsData,&pRewardsData);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags, .Psaved.Uiindexbuild, .Pchar.Ilevelexp, .Pplayer.Eaplayernumericthresholds, .Psaved.Ppbuilds, .Pplayer.Eaastrrecentlyacquiredstickerbookitems, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets")
	ATR_LOCKS(pData, ".Plastrewardbag");
enumTransactionOutcome GatewayGame_GrantRewards(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GatewayGameData) *pData, InventoryBagGroup *pRewardBags, ItemIDList *superCrittersIds, ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i,n;
	int iCritterPetXP = 0;

	GiveRewardBagsData *pRewardsData = StructCreate(parse_GiveRewardBagsData);
	StructDestroyNoConstSafe(parse_InventoryBag,&pData->pLastRewardBag);

	for(i=0;i<eaSize(&pRewardBags->eaBags);i++)
	{
		for(n=eaSize(&pRewardBags->eaBags[i]->ppIndexedInventorySlots)-1;n>=0;n--)
		{
			Item *pItem = pRewardBags->eaBags[i]->ppIndexedInventorySlots[n]->pItem;

			if(pItem)
			{
				if(!pData->pLastRewardBag)
				{
					pData->pLastRewardBag = (NOCONST(InventoryBag)*)rewardbag_Create();	
					//pData->pLastRewardBag->pRewardBagInfo = rewardbaginfo_Create();
					pData->pLastRewardBag->pRewardBagInfo->PickupType = kRewardPickupType_Direct;
				}

				rewardbag_trh_AddItem(pData->pLastRewardBag,StructClone(parse_Item,pItem),false);
				
				if(GET_REF(pItem->hItem) == GET_REF(g_RewardConfig.hGatewaySuperCritterPetXP))
				{
					iCritterPetXP += pItem->count;
					StructDestroySafe(parse_InventorySlot,&pRewardBags->eaBags[i]->ppIndexedInventorySlots[n]);
					eaRemove(&(NOCONST(InventorySlot)**)pRewardBags->eaBags[i]->ppIndexedInventorySlots,n);
				}
			}
		}
	}

	eaCopyStructs(&pRewardBags->eaBags,&pRewardsData->ppRewardBags,parse_InventoryBag);

	if(iCritterPetXP)
	{
		int iTotalCritters = eaSize(&superCrittersIds->ppIDs);

		for(i=0;i<iTotalCritters;i++)
		{
			BagIterator* pIter = bagiterator_Create();

			if(inv_trh_FindItemByIDEx(ATR_PASS_ARGS,pEnt,pIter,superCrittersIds->ppIDs[i]->uiID,false,true))
			{
				NOCONST(Item)* pSCPItem = bagiterator_GetItem(pIter);

				if(pSCPItem && pSCPItem->pSpecialProps && pSCPItem->pSpecialProps->pSuperCritterPet)
				{
					pSCPItem->pSpecialProps->pSuperCritterPet->uXP += iCritterPetXP / iTotalCritters;
				}
			}

			bagiterator_Destroy(pIter);
		}
	}

	if(!inv_trh_GiveRewardBags(ATR_PASS_ARGS,pEnt,NULL,pRewardsData,1,NULL,pReason,pExtract,NULL))
	{
		StructDestroySafe(parse_GiveRewardBagsData,&pRewardsData);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroySafe(parse_GiveRewardBagsData,&pRewardsData);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Pchsavestate");
enumTransactionOutcome GatewayGame_SaveState(ATR_ARGS, NOCONST(GatewayGameData) *pData, char *pchState)
{
	if(pData->pchSaveState)
		StructFreeString(pData->pchSaveState);
	if(pchState && pchState[0] != '\n')
	{
		pData->pchSaveState = StructAllocString(pchState);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}