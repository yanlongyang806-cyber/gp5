/***************************************************************************



***************************************************************************/

#include "itemEnums.h"
#include "GlobalTypeEnum.h"

typedef struct ContactDef ContactDef;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct StoreDef StoreDef;
typedef struct StoreDiscountInfo StoreDiscountInfo;
typedef struct StoreItemInfo StoreItemInfo;
typedef struct StoreSellableItemInfo StoreSellableItemInfo;
typedef struct ContactDialog ContactDialog;
typedef struct BuyBackItemInfo BuyBackItemInfo;

typedef U32 ContainerID;

typedef struct StoreBuyItemCBData {
	ContainerID uEntID;
	const char *pchItemName;
	const char *pchStoreName;
	const char *pchContactName;
	const char *pchCurrencyName;
	U32 iCount;
	U32 iEPValue;
	ContainerID uLockID;
} StoreBuyItemCBData;

typedef struct StoreSellItemCBData{
	ContainerID uEntID;
	BuyBackItemInfo* pItemInfo;
} StoreSellItemCBData;



void store_GetStoreItemInfo(Entity *pPlayerEnt, ContactDef *pContactDef, StoreDef *pStoreDef,
								StoreItemInfo ***peaItemInfo, StoreItemInfo ***peaUnavailableItemInfo,
								StoreDiscountInfo ***peaDiscountInfo, GameAccountDataExtract *pExtract);
void store_GetStoreOwnedItemInfo(Entity *pPlayerEnt, Entity* pOwnerEnt, ContactDef *pContactDef, StoreDef *pStoreDef, StoreItemInfo ***peaItemInfo, InvBagIDs eBagToSearch, GameAccountDataExtract *pExtract);

void store_RefreshStoreItemInfo(Entity *pPlayerEnt, StoreItemInfo ***peaItemInfo, StoreItemInfo ***peaUnavailableItemInfo, GameAccountDataExtract *pExtract);
void store_UpdateStoreProvisioning(Entity *pPlayerEnt, ContactDialog *pDialog, GameAccountDataExtract *pExtract);

void injuryStore_SetTarget(Entity* pPlayerEnt, U32 uiTargetEntType, U32 uiTargetEntID);

bool store_UpdateSellItemInfo(Entity* pEnt, StoreDef* pStore, StoreSellableItemInfo*** peaSellableItems, GameAccountDataExtract *pExtract);

void store_Close(Entity* pPlayerEnt);

void store_BuyItem(Entity *pEnt, const char *pchStoreName, U32 uStoreItemIndex, S32 iCount);
void store_SellItem(Entity *pEnt, S32 iBagID, S32 iSlot, S32 iCount, GlobalType iGlobalType, ContainerID iContainerID);


bool gslPersistedStore_UpdateItemInfo(Entity* pEnt, StoreItemInfo ***peaItemInfo);
void gslPersistedStore_PlayerAddRequest(Entity* pEnt, StoreDef* pStoreDef);
void gslPersistedStore_PlayerRemoveRequests(Entity* pEnt);

