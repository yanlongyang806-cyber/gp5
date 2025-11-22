#ifndef INVENTORY_UIEXPR_H
#define INVENTORY_UIEXPR_H

#include "ReferenceSystem.h"

typedef struct ItemDef ItemDef;
typedef struct InventoryBag InventoryBag;
typedef struct Entity Entity;
typedef struct Item Item;

AUTO_STRUCT;
typedef struct ChoosableItem
{
	REF_TO(ItemDef) hItemDef;
	Item* pItem; AST(UNOWNED)
	S32 iBagIdx;
	S32 iSlotIdx; AST(NAME(slotIdx))
	S32 iNumPicks;
	bool bSelected;
	Item* pItemCopy;
} ChoosableItem;

void Item_ItemPowersAutoDesc(Item *pItem, char **pestrDesc, const char *pchPowerMessageKey, const char *pchAttribMessageKey, S32 eActiveGemSlotType);
S32 Item_IsBeingTraded(Entity* pEnt, Item* pItem);

S32 GetChoosableItemList(InventoryBag **eaRewardBags, ChoosableItem*** peaChoosableItems);
void GetSelectedChoosableItems(ChoosableItem ***peaData, ItemDef ***peaItemsDefs);

#endif