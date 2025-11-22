/***************************************************************************
 
 
 
 *
 ***************************************************************************/

typedef struct GatewaySession GatewaySession;
typedef struct ContainerTracker ContainerTracker;

void SubscribeVendor(GatewaySession *psess, ContainerTracker *ptracker, void *pvParams);
bool IsModifiedVendor(GatewaySession *psess, ContainerTracker *ptracker);
bool CheckModifiedVendor(GatewaySession *psess, ContainerTracker *ptracker);
bool IsReadyVendor(GatewaySession *psess, ContainerTracker *ptracker);
void *CreateMappedVendor(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj);
void DestroyMappedVendor(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj);


typedef struct MappedStoreItem MappedStoreItem;
typedef struct StoreItemInfo StoreItemInfo;
typedef struct StoreDiscountInfo StoreDiscountInfo;

AUTO_STRUCT;
typedef struct MappedVendor
{
	U32 uTimeLastUpdate;						AST(SERVER_ONLY)
	StoreItemInfo **eaStoreItems;				AST(SERVER_ONLY)
	StoreItemInfo **eaUnavailableStoreItems;	AST(SERVER_ONLY)
	StoreDiscountInfo **eaStoreDiscounts;		AST(SERVER_ONLY)

	const char *pchContactName;					AST(NAME(ContactName) POOL_STRING)
	MappedStoreItem **eaItems;					AST(NAME(Items))
} MappedVendor;

typedef struct ContactDef ContactDef;

LATELINK;
ContactDef *GatewayVendor_GetContactDef(const char *pchContactName);
	// Should be overridden for each project


typedef struct StoreBuyItemCBData StoreBuyItemCBData;

void GatewayVendor_PurchasedVendorItem(GatewaySession *psess, StoreBuyItemCBData *pData, bool bSucceeded);

typedef struct StoreSellItemCBData StoreSellItemCBData;

void GatewayVendor_SoldItemToVendor(GatewaySession *psess, StoreSellItemCBData *pData, bool bSucceeded);

// End of File
