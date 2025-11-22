/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "stdtypes.h"
#include "Entity.h"
#include "Player.h"
#include "Store.h"
#include "contact_common.h"
#include "mission_common.h"
#include "storeCommon.h"
#include "gslGatewaySession.h"
#include "gslGatewayVendor.h"

///////////////////////////////////////////////////////////////////////////

static int FindStoreCollectionContainingStore(ContactDef *pcontact, const char *pchStoreName)
{
	int i;
	int iStoreCollection = -1;

	for(i = 0; i < eaSize(&pcontact->storeCollections) && iStoreCollection < 0; i++)
	{
		int j;
		StoreCollection *pcoll = pcontact->storeCollections[i];
		for(j = 0; j < eaSize(&pcoll->eaStores) && iStoreCollection < 0; j++)
		{
			StoreDef *pstore = GET_REF(pcoll->eaStores[j]->ref);
			if(stricmp(pstore->name, pchStoreName) == 0)
			{
				iStoreCollection = i;
			}
		}
	}

	return iStoreCollection > 0 ? iStoreCollection : 0;
}


AUTO_COMMAND ACMD_NAME(GatewayVendor_PurchaseVendorItem) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayVendor_PurchaseVendorItem(Entity *pent, const char *pchVendorName, const char *pchStoreName, U32 uStoreIndex, U32 uCount)
{
	ContactDef *pcontact = GatewayVendor_GetContactDef(pchVendorName);
	ContactDialog *pdialog;
	StoreItemInfo **eaStoreItems;

	if(!SAFE_MEMBER2(pent, pPlayer, pInteractInfo) || !pcontact)
	{
		verbose_printf("No interact info on player or no contact (0x%08x).", (U32)pcontact);
		return;
	}

	{
		ContainerTracker *ptracker;
		GatewaySession *psess = wgsFindSessionForAccountId(SAFE_MEMBER2(pent, pPlayer, accountID));
		if(!psess)
			return;

		ptracker = session_FindContainerTracker(psess, GW_GLOBALTYPE_VENDOR, pchVendorName);
		if(ptracker && ptracker->pMapped)
		{
			eaStoreItems = ((MappedVendor *)ptracker->pMapped)->eaStoreItems;
		}
		else
		{
			verbose_printf("Failed to find vendor tracker (%s).", pchVendorName);
			return;
		}
	}

	// Create a ContactDialog, which is what store_BuyItem() uses internally
	// to hold state for buying stuff.
	if(SAFE_MEMBER3(pent, pPlayer, pInteractInfo, pContactDialog))
		StructDestroy(parse_ContactDialog, pent->pPlayer->pInteractInfo->pContactDialog);

	pdialog = StructCreate(parse_ContactDialog);

	SET_HANDLE_FROM_REFERENT("ContactDef", pcontact, pdialog->hContactDef);
	pdialog->iCurrentStoreCollection = FindStoreCollectionContainingStore(pcontact, pchStoreName);
	pdialog->eaStoreItems = eaStoreItems;
	// store_BuyItem also looks at:
	//   hPersistStoreDef
	//   uStoreResearchTimeExpire, pchResearchTimerStoreName, uResearchTimerStoreItemIndex, uStoreResearchTimeExpire, bIsResearching
	// which are not currently supported.

	// Finally, buy the item!
	pent->pPlayer->pInteractInfo->pContactDialog = pdialog;
	store_BuyItem(pent, pchStoreName, uStoreIndex, uCount);
	pent->pPlayer->pInteractInfo->pContactDialog = NULL;

	// And get rid of the ContactDialog;
	pdialog->eaStoreItems = NULL;
	StructDestroy(parse_ContactDialog, pdialog);

}

void GatewayVendor_PurchasedVendorItem(GatewaySession *psess, StoreBuyItemCBData *pData, bool bSucceeded)
{
	Packet *pkt;
	Entity *pent = session_GetLoginEntity(psess);
	ContainerTracker *ptracker = NULL;

	if(!psess || !pent)
		return;

	ptracker = session_FindContainerTracker(psess, GW_GLOBALTYPE_VENDOR, pData->pchContactName);

	// Mark the store as needing to be updated.
	// Should probably do all stores, but they will get updated periodically anyway.
	// This is really just to update all the current errors (like for not having
	//   enough cash to buy the item).
	// This should probably be done whenever changes occur in the player's numerics, but
	//   I don't know how to do that well. So, there's a periodic update as
	//   well (every VENDOR_UPDATE_PERIOD_SECS).
	if(ptracker)
	{
		MappedVendor *pvendor = (MappedVendor *)ptracker->pMapped;
		pvendor->uTimeLastUpdate = 0;
	}

	if(SAFE_MEMBER2(pent, pPlayer, pInteractInfo))
	{
		pent->pPlayer->pInteractInfo->bPurchaseInProgress = false;
	}

	SESSION_PKTCREATE(pkt, psess, "Server_PurchaseVendorItem");
	pktSendU32(pkt, bSucceeded ? 1 : 0);
	pktSendString(pkt, pData->pchContactName);
	pktSendString(pkt, pData->pchStoreName);
	pktSendString(pkt, pData->pchItemName);
	pktSendU32(pkt, pData->iCount);
	pktSend(&pkt);
}

///////////////////////////////////////////////////////////////////////////

static StoreRef *FindFirstSellableStore(ContactDef *pcontact)
{
	int i;
	for(i = 0; i < eaSize(&pcontact->storeCollections); i++)
	{
		int j;
		StoreCollection *pcoll = pcontact->storeCollections[i];
		for(j = 0; j < eaSize(&pcoll->eaStores); j++)
		{
			StoreDef *pstore = GET_REF(pcoll->eaStores[j]->ref);
			if(pstore && (pstore->eContents == Store_All || pstore->eContents == Store_Costumes))
			{
				if(pstore->bSellEnabled && GET_REF(pstore->hCurrency))
				{
					return pcoll->eaStores[j];
				}
			}
		}
	}

	return NULL;
}

AUTO_COMMAND ACMD_NAME(GatewayVendor_SellItemToVendor) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayVendor_SellItemToVendor(Entity *pent, const char *pchVendorName, U64 uItemID, U32 uCount)
{
	ContactDef *pcontact = GatewayVendor_GetContactDef(pchVendorName);
	BagIterator *iter;
	int iBagID = -1;
	int iSlot = -1;
	ContactDialog *pdialog;
	StoreRef *pstore;

	if(!pent || !pcontact || uItemID == 0 || uCount > INT_MAX)
		return;

	if(!SAFE_MEMBER2(pent, pPlayer, pInteractInfo) || !pcontact)
	{
		verbose_printf("No interact info on player or no contact (0x%08x).", (U32)pcontact);
		return;
	}

	iter = bagiterator_Create();

	if(!inv_trh_FindItemByID(ATR_EMPTY_ARGS, (NOCONST(Entity) *)pent, uItemID, iter))
	{
		verbose_printf("Can't find item %I64x.", uItemID);
		bagiterator_Destroy(iter);
		return;
	}

	iBagID = bagiterator_GetCurrentBagID(iter);
	iSlot = bagiterator_GetSlotID(iter);
	bagiterator_Destroy(iter);

	if(!(pstore = FindFirstSellableStore(pcontact)))
	{
		verbose_printf("Unable to find a sellable store on (0x%08x).", (U32)pcontact);
		return;
	}

	// Create a ContactDialog, which is used for getting selling info.
	if(SAFE_MEMBER3(pent, pPlayer, pInteractInfo, pContactDialog))
		StructDestroy(parse_ContactDialog, pent->pPlayer->pInteractInfo->pContactDialog);

	pdialog = StructCreate(parse_ContactDialog);
	SET_HANDLE_FROM_REFERENT("ContactDef", pcontact, pdialog->hContactDef);
	COPY_HANDLE(pdialog->hSellStore, pstore->ref);
	pdialog->bSellEnabled = true;

	// Sell the item to the vendor
	pent->pPlayer->pInteractInfo->pContactDialog = pdialog;
	store_SellItem(pent, iBagID, iSlot, uCount, GLOBALTYPE_NONE, 0);
	pent->pPlayer->pInteractInfo->pContactDialog = NULL;

	// And get rid of the ContactDialog;
	StructDestroy(parse_ContactDialog, pdialog);

}

void GatewayVendor_SoldItemToVendor(GatewaySession *psess, StoreSellItemCBData *pData, bool bSucceeded)
{
	Packet *pkt;
	Entity *pent = session_GetLoginEntity(psess);

	if(!psess || !pent)
		return;

	SESSION_PKTCREATE(pkt, psess, "Server_SellItemToVendor");
	pktSendU32(pkt, bSucceeded ? 1 : 0);
	pktSendString(pkt, pData->pItemInfo->pchItemName);
	pktSendU32(pkt, pData->pItemInfo->iCount);
	pktSendU32(pkt, pData->pItemInfo->iCost);
	pktSend(&pkt);
}

///////////////////////////////////////////////////////////////////////////

// End of File
