/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "stdtypes.h"
#include "Entity.h"
#include "Store.h"
#include "StoreCommon.h"
#include "contact_common.h"
#include "inventoryCommon.h"

#include "gslGatewaySession.h"
#include "gslGatewayContainerMapping.h"

#include "StoreCommon_h_ast.h"

#include "gslGatewayVendor.h"

#include "gslGatewayVendor_h_ast.h"
#include "gslGatewayVendor_c_ast.h"

AUTO_STRUCT;
typedef struct MappedStoreItem
{
	const char *pchDisplayName;		AST(NAME(DisplayName))
	const char *pchDescription;		AST(NAME(Description))
	const char *pchIcon;			AST(NAME(Icon) POOL_STRING)
	ItemQuality eQuality;			AST(NAME(Quality))

	// The Store Name and Index used to buy the item
	const char *pchStoreName;		AST(NAME(StoreName) POOL_STRING)
	int iStoreIndex;				AST(NAME(StoreIndex) DEFAULT(-1))

	REF_TO(ItemDef) hDef;			AST(NAME(hDef))
	int iCount;						AST(NAME(Count))

	StoreCanBuyError eErrorType;	AST(NAME(ErrorType))
	const char *pchErrorReason;		AST(NAME(ErrorReason))

	// How much the item costs (or how much it will sell for)
	StoreItemCostInfo** eaCostInfo; AST(NAME(CostInfo))
	char *estrFormattedValue;		AST(NAME(FormattedValue) ESTRING)

} MappedStoreItem;



#define VENDOR_UPDATE_PERIOD_SECS (60)

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////


void SubscribeVendor(GatewaySession *psess, ContainerTracker *ptracker, void *pvParams)
{
}

bool IsModifiedVendor(GatewaySession *psess, ContainerTracker *ptracker)
{
	if(ptracker->pMapped)
	{
		MappedVendor *pvendor = (MappedVendor *)ptracker->pMapped;
		U32 now = timeSecondsSince2000();

		if(pvendor->uTimeLastUpdate + VENDOR_UPDATE_PERIOD_SECS < now)		
		{
			return true;
		}
	}

	return false;	
}

bool CheckModifiedVendor(GatewaySession *psess, ContainerTracker *ptracker)
{
	// There's no real way to test for modification. So just make it dirty.
	// If no change happened then no diff will be sent.
	return true;
}

bool IsReadyVendor(GatewaySession *psess, ContainerTracker *ptracker)
{
	return true;
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

ContactDef *DEFAULT_LATELINK_GatewayVendor_GetContactDef(const char *pchContactName)
{
	return NULL;
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

static StoreDef *FindStoreInContactDef(ContactDef *pcontact, const char *pchName)
{
	int i;

	for(i = 0; i < eaSize(&pcontact->stores); i++)
	{
		StoreDef *pdef = GET_REF(pcontact->stores[i]->ref);
		if(pdef && stricmp(pchName, pdef->name) == 0)
		{
			return pdef;
		}
	}

	for(i = 0; i < eaSize(&pcontact->storeCollections); i++)
	{
		int j;
		StoreCollection *pcoll = pcontact->storeCollections[i];
		for(j = 0; j < eaSize(&pcoll->eaStores); j++)
		{
			StoreDef *pdef = GET_REF(pcoll->eaStores[j]->ref);
			if(pdef && stricmp(pchName, pdef->name) == 0)
			{
				return pdef;
			}
		}
	}

	return NULL;
}

static void GetVendorItemInfo(GatewaySession *psess, ContactDef *pcontact, MappedVendor *pvendor)
{
	int i;
	Entity *pent = session_GetLoginEntity(psess);

	if(!pent || !pcontact || !pvendor)
		return;

	pvendor->pchContactName = pcontact->name;

	for(i = 0; i < eaSize(&pcontact->stores); i++)
	{
		StoreDef *pstore = GET_REF(pcontact->stores[i]->ref);
		if(pstore)
		{
			store_GetStoreItemInfo(pent, pcontact, pstore, &pvendor->eaStoreItems,
				&pvendor->eaUnavailableStoreItems, &pvendor->eaStoreDiscounts,
				session_GetCachedGameAccountDataExtract(psess));
		}
	}

	for(i = 0; i < eaSize(&pcontact->storeCollections); i++)
	{
		int j;
		StoreCollection *pcoll = pcontact->storeCollections[i];
		for(j = 0; j < eaSize(&pcoll->eaStores); j++)
		{
			StoreDef *pstore = GET_REF(pcoll->eaStores[j]->ref);
			if(pstore)
			{
				store_GetStoreItemInfo(pent, pcontact, pstore, &pvendor->eaStoreItems,
					&pvendor->eaUnavailableStoreItems, &pvendor->eaStoreDiscounts,
					session_GetCachedGameAccountDataExtract(psess));
			}
		}
	}
}


void *CreateMappedVendor(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj)
{
	int i;
	MappedVendor *pvendor;
	const char *pchResources = allocAddString("resources");

	Entity *pent = session_GetLoginEntity(psess);
	ContactDef *pcontact = GatewayVendor_GetContactDef(ptracker->estrID);

	if(!pent || !psess || !pcontact)
	{
		return NULL;
	}

	pvendor = StructCreate(parse_MappedVendor);

	pvendor->uTimeLastUpdate = timeSecondsSince2000();

	GetVendorItemInfo(psess, pcontact, pvendor);

	for(i = 0; i < eaSize(&pvendor->eaStoreItems); i++)
	{
		MappedStoreItem *pitem;
		StoreItemInfo *pinfo = pvendor->eaStoreItems[i];
		ItemDef *pdefItem = NULL;

		// Currently, we only support buying with resources in Gateway
		if(eaSize(&pinfo->eaCostInfo) != 1)
			continue;
		pdefItem = GET_REF(pinfo->eaCostInfo[0]->hItemDef);
		if(!pdefItem || pdefItem->pchName != pchResources)
			continue;

		pitem = StructCreate(parse_MappedStoreItem);

		if(pinfo->pItem)
		{
			COPY_HANDLE(pitem->hDef, pinfo->pItem->hItem);
			pdefItem = GET_REF(pinfo->pItem->hItem);
		}
		else if(pinfo->pOwnedItem)
		{
			COPY_HANDLE(pitem->hDef, pinfo->pOwnedItem->hItem);
			pdefItem = GET_REF(pinfo->pOwnedItem->hItem);
		}

		pitem->iCount = pinfo->iCount;
		pitem->eErrorType = pinfo->eCanBuyError;
		pitem->pchErrorReason = StructAllocString(pinfo->pchRequirementsText);

		pitem->pchStoreName = pinfo->pchStoreName;
		pitem->iStoreIndex = pinfo->index;
		eaCopyStructs(&pinfo->eaCostInfo, &pitem->eaCostInfo, parse_StoreItemCostInfo);

		pitem->pchDisplayName = StructAllocString(langTranslateDisplayMessage(psess->lang, pdefItem->displayNameMsg));
		pitem->eQuality = pdefItem->Quality;

		pitem->pchDescription = StructAllocString(pinfo->pchTranslatedLongDescription);
		if(!pitem->pchDescription && pdefItem)
			pitem->pchDescription = StructAllocString(langTranslateDisplayMessage(psess->lang, pdefItem->descriptionMsg));

		pitem->pchIcon = pinfo->pchDisplayTex;
		if(!pitem->pchIcon && pdefItem)
			pitem->pchIcon = pdefItem->pchIconName;

		item_GetFormattedResourceString(psess->lang, &pitem->estrFormattedValue, pinfo->eaCostInfo[0]->iCount, "Item.FormattedResources", 100, 0);

		eaPush(&pvendor->eaItems, pitem);
	}

	return pvendor;
}

void DestroyMappedVendor(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj)
{
	StructDestroy(parse_MappedVendor, pvObj);
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

#include "gslGatewayVendor_h_ast.c"
#include "gslGatewayVendor_c_ast.c"

// End of File
