/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "stdtypes.h"
#include "estring.h"
#include "error.h"
#include "textparserJSON.h"
#include "WebRequests.h"
#include "Entity.h"
#include "timing.h"
#include "gslAccountProxy.h"
#include "gslGatewaySession.h"
#include "AccountDataCache.h"
#include "gslAccountProxy.h"
#include "MicroTransactions.h"
#include "Money.h"
#include "StringCache.h"
#include "itemEnums.h"

#include "gslGatewayContainerMapping.h"

#include "itemEnums_h_ast.h"
#include "gslAccountProxy_h_ast.h"
#include "WebRequests_h_ast.h"
#include "Microtransactions_h_ast.h"

#include "gslGatewayCStore_c_ast.h"

static U32 s_timeLastCatalogUpdate = 0;


AUTO_STRUCT;
typedef struct CStoreProduct
{
	const char *pchKey;					AST(NAME(Key) POOL_STRING)
	U32 uID;							AST(NAME(ID))
	bool bNew;							AST(NAME(New))
	bool bFeatured;						AST(NAME(Featured))
	int iCount;							AST(NAME(Count))
	ItemQuality eQuality;				AST(NAME(Quality))
	const char **eaItemDefNames;		AST(NAME(ItemDefNames) POOL_STRING)
	REF_TO(MicroTransactionDef) hDef;	AST(NAME(hDef))
} CStoreProduct;

AUTO_STRUCT;
typedef struct UserProductInfo
{
	U32 uID;						AST(NAME(id))
	bool bPrereqsMet;				AST(NAME(PrereqsMet))
	MicroPurchaseErrorType eError;	AST(NAME(ErrorType))
	char *estrErrorReason;			AST(NAME(ErrorReason) ESTRING)
	S64 i64FullPrice;				AST(NAME(FullPrice))
	S64 i64Price;					AST(NAME(Price))
} UserProductInfo;


typedef struct CStoreCategory CStoreCategory;
AUTO_STRUCT;
typedef struct CStoreCategory
{
	STRING_POOLED pchName;				AST(NAME(Name) POOL_STRING)
	REF_TO(Message) hDisplayName;		AST(NAME(DisplayName))
	MTCategoryType eType;				AST(NAME(Type))
	S32 iSortIndex;						AST(NAME(SortIndex))
	STRING_POOLED pchParentCategory;	AST(NAME(ParentCategory) POOL_STRING)
	const char **eaChildren;			AST(NAME(Children) POOL_STRING)
	UINT_EARRAY eaProductIDs;			AST(NAME(ProductIDs))

} CStoreCategory;

AUTO_STRUCT;
typedef struct MappedCStore
{
	CStoreCategory **eaCategories;		AST(NAME(Categories) UNOWNED)
	CStoreProduct **eaProducts;			AST(NAME(Products) UNOWNED)
	UserProductInfo **eaUserProducts;	AST(NAME(UserProducts))
} MappedCStore;

// All users share the basic category and product lists, so a cache is kept.
static CStoreCategory **s_eaCachedCategories = NULL;
static CStoreProduct **s_eaCachedProducts = NULL;


static MicroTransactionInfo *GetFullCatalog(void)
{
	if(HasProductCatalogChanged(s_timeLastCatalogUpdate))
	{
		gslAPProductListUpdateCache();
		s_timeLastCatalogUpdate = timeSecondsSince2000();
	}

	return GetMicrotransactionProducts(NULL);
}

static MicroTransactionUserCatalogResponse *GetUserCatalog(GatewaySession *psess)
{
	MicroTransactionUserCatalogRequest req;
	Entity *pent = session_GetLoginEntity(psess);

	if(pent)
	{
		req.characterID = pent->myContainerID;
		req.accountID = psess->idAccount;

		return GetMicrotransactionCatalogForCharacter(&req);
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

static CStoreCategory *CreateCStoreCategory(MicroTransactionCategory *psrc)
{
	CStoreCategory *pcat = StructCreate(parse_CStoreCategory);

	pcat->pchName = psrc->pchName;
	COPY_HANDLE(pcat->hDisplayName, psrc->displayNameMesg.hMessage);
	pcat->eType = psrc->eType;
	pcat->iSortIndex = psrc->iSortIndex;
	pcat->pchParentCategory = psrc->pchParentCategory;

	return pcat;
}

static CStoreCategory *FindCStoreCategory(CStoreCategory ***peaCats, const char *pchName)
{
	int i;
	for(i = 0; i < eaSize(peaCats); i++)
	{
		if(stricmp(pchName, (*peaCats)[i]->pchName) == 0)
		{
			return (*peaCats)[i];
		}
	}

	return NULL;
}


static CStoreProduct *CreateCStoreProduct(MicroTransactionProduct *psrc)
{
	CStoreProduct *pprod = StructCreate(parse_CStoreProduct);

	COPY_HANDLE(pprod->hDef, psrc->hDef);
	pprod->uID = psrc->pProduct->uID;
	pprod->pchKey = psrc->pDef->pchName;

	EARRAY_FOREACH_BEGIN(psrc->pDef->eaParts, i);
	{
		MicroTransactionPart *ppart = psrc->pDef->eaParts[i];
		if(ppart && ppart->ePartType == kMicroPart_Item)
		{
			ItemDef *pitemDef = GET_REF(ppart->hItemDef);

			if(pprod->iCount == 0)
			{
				pprod->iCount = ppart->iCount;
				pprod->eQuality = pitemDef->Quality;
			}

			if(pitemDef && pitemDef->eType != kItemType_Numeric)
			{
				eaPush(&pprod->eaItemDefNames, allocAddString(pitemDef->pchName));
			}
		}
	}
	EARRAY_FOREACH_END;

	return pprod;
}

static MicroTransactionProduct *FindMicroTransactionProduct(MicroTransactionProduct ***peaProds, U32 uID)
{
	int i;
	for(i = 0; i < eaSize(peaProds); i++)
	{
		if((*peaProds)[i]->uID == uID)
		{
			return (*peaProds)[i];
		}
	}

	return NULL;
}

MicroTransactionProduct *FindProductForProductID(U32 id)
{
	MicroTransactionInfo *pinfo = GetFullCatalog();
	return FindMicroTransactionProduct(&pinfo->ppProducts, id);
}


static int CompareCStoreSortIndex(const CStoreCategory **left, const CStoreCategory **right)
{
	return (*left)->iSortIndex - (*right)->iSortIndex;
}

static void UpdateCachedCatalogAndProducts(MicroTransactionInfo *pinfo)
{
	const char *pchMainCategoryName = NULL;

	if(s_eaCachedCategories)
		eaDestroyStruct(&s_eaCachedCategories, parse_CStoreCategory);

	if(s_eaCachedProducts)
		eaDestroyStruct(&s_eaCachedProducts, parse_CStoreProduct);

	// First loop and make all the categories.
	EARRAY_FOREACH_BEGIN(pinfo->ppCategories, i);
	{
		MicroTransactionCategory *psrc = pinfo->ppCategories[i];

		if(psrc->eType == kMTCategory_Main)
		{
			pchMainCategoryName = psrc->pchName;
		}
		else
		{
			eaPush(&s_eaCachedCategories, CreateCStoreCategory(psrc));
		}
	}
	EARRAY_FOREACH_END;

	if(!pchMainCategoryName)
		return;

	// Loop over the products and put their ids into the categories.
	EARRAY_FOREACH_BEGIN(pinfo->ppProducts, i);
	{
		bool bInMain = false;
		MicroTransactionProduct *pprod = pinfo->ppProducts[i];
		CStoreProduct *pmapped;

		// First, see if this product is in the category marked "Main"
		EARRAY_FOREACH_BEGIN(pprod->pProduct->ppCategories, j);
		{
			char *pchCat = strchr(pprod->pProduct->ppCategories[j], '.');

			if(pchCat)
				pchCat++;
			else
				pchCat = pprod->pProduct->ppCategories[j];

			if(stricmp(pchCat, pchMainCategoryName) == 0)
			{
				bInMain = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		// It's not in Main, so skip it.
		if(!bInMain)
			continue;

		// OK, ow we can build the mapped product
		pmapped = CreateCStoreProduct(pprod);
		eaPush(&s_eaCachedProducts, pmapped);

		// Set its flags and put it into the appropriate category lists.
		EARRAY_FOREACH_BEGIN(pprod->pProduct->ppCategories, j);
		{
			char *pchCat = strchr(pprod->pProduct->ppCategories[j], '.');
			CStoreCategory *pcat;

			if(pchCat)
				pchCat++;
			else
				pchCat = pprod->pProduct->ppCategories[j];

			pcat = FindCStoreCategory(&s_eaCachedCategories, pchCat);
			if(pcat)
			{
				if(pcat->eType == kMTCategory_New)
					pmapped->bNew = true; 
				if(pcat->eType == kMTCategory_Featured)
					pmapped->bFeatured = true; 

				ea32Push(&pcat->eaProductIDs, pmapped->uID);
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	// Generate the All product id list and get rid of unused categories.
	EARRAY_FOREACH_REVERSE_BEGIN(s_eaCachedCategories, i);
	{
		CStoreCategory *pcat = s_eaCachedCategories[i];

		EARRAY_FOREACH_BEGIN(s_eaCachedCategories, j);
		{
			CStoreCategory *pcatChild = s_eaCachedCategories[j];
			if(pcatChild != pcat && pcatChild->pchParentCategory)
			{
				if(stricmp(pcatChild->pchParentCategory, pcat->pchName) == 0)
				{
					if(ea32Size(&pcatChild->eaProductIDs) > 0)
					{
						int k;
						for(k = 0; k < ea32Size(&pcatChild->eaProductIDs); k++)
						{
							ea32PushUnique(&pcat->eaProductIDs, pcatChild->eaProductIDs[k]);
						}
						eaPush(&pcat->eaChildren, pcatChild->pchName);
					}
				}
			}
		}
		EARRAY_FOREACH_END;

		if(ea32Size(&pcat->eaProductIDs) == 0)
		{
			StructDestroy(parse_CStoreCategory, pcat);
			eaRemoveFast(&s_eaCachedCategories, i);
		}
	}
	EARRAY_FOREACH_END;

	// Sort the categories as requested.
	eaQSort(s_eaCachedCategories, CompareCStoreSortIndex);

}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////


void SubscribeCStore(GatewaySession *psess, ContainerTracker *ptracker, void *pvParams)
{
	GetFullCatalog();
}

bool IsModifiedCStore(GatewaySession *psess, ContainerTracker *ptracker)
{
	return HasProductCatalogChanged(s_timeLastCatalogUpdate);
}

bool CheckModifiedCStore(GatewaySession *psess, ContainerTracker *ptracker)
{
	// Will force an update if the client asks for one.
	return true;
}

bool IsReadyCStore(GatewaySession *psess, ContainerTracker *ptracker)
{
	MicroTransactionInfo *pFullCat = GetFullCatalog();

	if(pFullCat)
	{
		UpdateCachedCatalogAndProducts(pFullCat);
	}

	if(!ptracker->pCatalog)
	{
		ptracker->pCatalog = GetUserCatalog(psess);
		if(ptracker->pCatalog && stricmp(ptracker->pCatalog->result_string, "processing") == 0)
		{
			StructDestroy(parse_MicroTransactionUserCatalogResponse, ptracker->pCatalog);
			ptracker->pCatalog = NULL;
		}
	}

	return pFullCat && ptracker->pCatalog;
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

void DestroyMappedCStore(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj)
{
	StructDestroy(parse_MappedCStore, pvObj);
}

void *CreateMappedCStore(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj)
{
	MicroTransactionInfo *pinfo = GetFullCatalog();
	MappedCStore *pStore = StructCreate(parse_MappedCStore);

	pStore->eaCategories = s_eaCachedCategories;
	pStore->eaProducts = s_eaCachedProducts;

	// Insert all of the user prices.
	EARRAY_FOREACH_BEGIN(ptracker->pCatalog->ppProducts, i);
	{
		MTUserProduct *pprod = ptracker->pCatalog->ppProducts[i];
		UserProductInfo *puser = StructCreate(parse_UserProductInfo);
		Entity *pent;
		GameAccountData *pgad;
		MicroTransactionProduct *pmtp;

		puser->uID = pprod->pProduct->uID;
		puser->bPrereqsMet = pprod->bPrereqsMet;
		puser->i64FullPrice = pprod->pProduct->pFullMoneyPrice->Internal._internal_SubdividedAmount;
		puser->i64Price = pprod->pProduct->pMoneyPrice->Internal._internal_SubdividedAmount;

		pent = session_GetLoginEntityOfflineCopy(psess);
		pgad = GET_REF(psess->hGameAccountData);
		pmtp = FindMicroTransactionProduct(&pinfo->ppProducts, puser->uID);
		if(pent && pgad && pmtp)
		{
			puser->eError = microtrans_GetCanPurchaseError(0, pent, pgad, pmtp->pDef,
				psess->lang, &puser->estrErrorReason);
		}
			
		eaPush(&pStore->eaUserProducts, puser);
	}
	EARRAY_FOREACH_END;

	// This is a bit of a cheat. IsReady potentially made the UNOWNED pointers
	// in pMapped go bad. This is a problem because the container system will
	// want to use them to do a StructDiff shortly. Instead, we'll remove the
	// old pMapped version here. This means that every send to the client
	// is a full send (rather than a diff). Because this structure shouldn't
	// change very often, this shouldn't be a big deal.
	if(ptracker->pMapped)
	{
		DestroyMappedCStore(psess, ptracker, ptracker->pMapped);
		ptracker->pMapped = NULL;
	}

	return pStore;
}

#include "gslGatewayCStore_c_ast.c"

// End of File
