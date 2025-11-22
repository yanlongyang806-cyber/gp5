#ifndef ITEMSALVAGE_H
#define ITEMSALVAGE_H

typedef struct Expression Expression;
typedef struct RewardTable RewardTable;

#include "itemCommon.h"


typedef enum EItemSalvageFailReason
{
	EItemSalvageFailReason_NONE,

	EItemSalvageFailReason_CANNOT_SALVAGE,

	EItemSalvageFailReason_CANNOT_PERFORM,

} EItemSalvageFailReason;

AUTO_STRUCT;
typedef struct ItemSalvageRecipeDef
{
	// list of qualities, matching any satisfies
	ItemQuality* peItemQualities;				AST(NAME(ItemQualities) SUBTABLE(ItemQualityEnum))

	// item categories, must match all categories
	ItemCategory *peRequiredItemCategories;		AST(NAME(RequiredItemCategories) SUBTABLE(ItemCategoryEnum))

	// item categories, if any match, is rejected
	ItemCategory *peExcludeItemCategories;		AST(NAME(ExcludeItemCategories) SUBTABLE(ItemCategoryEnum))
	
	// matching any
	ItemType *peItemTypes;						AST(NAME(ItemTypes) SUBTABLE(ItemTypeEnum))
	
	S32 iLevelMin;								AST(NAME(LevelMin))
	S32 iLevelMax;								AST(NAME(LevelMax))

	S32 iSlotType;								AST(SUBTABLE(SlotTypeEnum))

	// matching any satisfies
	InvBagIDs* peAllowedRestrictBagIDs;			AST(NAME(AllowedRestrictBagIDs) SUBTABLE(InvBagIDsEnum))
	
	// matching any
	CharacterClassRef **ppCharacterClasses;		AST(NAME(CharacterClasses) STRUCT(parse_CharacterClassRef))

	// Grant this reward table
	REF_TO(RewardTable) hRewardTable;			AST(NAME(RewardTable) SERVER_ONLY)
	
} ItemSalvageRecipeDef;


AUTO_STRUCT;
typedef struct ItemSalvageCheckDef
{
	// list of qualities, must match any quality
	ItemQuality* peItemQualities;				AST(NAME(ItemQualities) SUBTABLE(ItemQualityEnum))
		
	// required. item categories, must match any category
	ItemCategory *peAllowedItemCategories;		AST(NAME(AllowedItemCategories) SUBTABLE(ItemCategoryEnum))

	// item categories, if matches any, cannot be salvaged
	ItemCategory *peExcludeItemCategories;		AST(NAME(ExcludeItemCategories) SUBTABLE(ItemCategoryEnum))

	// matching any
	ItemType *peItemTypes;						AST(NAME(ItemTypes) SUBTABLE(ItemTypeEnum))

	S32 iLevelMin;								AST(NAME(LevelMin))
	S32 iLevelMax;								AST(NAME(LevelMax))

} ItemSalvageCheckDef;

AUTO_STRUCT;
typedef struct ItemSalvageDef
{
	// list that defines how to map an item to a reward table
	ItemSalvageRecipeDef	**eaItemSalvageRecipes;	AST(NAME(ItemSalvageRecipe) SERVER_ONLY)
	
	// Grant this reward table if all salvage nodes fail
	REF_TO(RewardTable) hDefaultRewardTable;	AST(NAME(RewardTable) SERVER_ONLY)
	
	// holds the data to check if an item is salvageable at all
	ItemSalvageCheckDef *pSalvageableCheck;

	// the power mode that is required in order to actually perform on an item
	S32 iRequiredPowerMode;						AST(SUBTABLE(PowerModeEnum) DEFAULT(-1))
		
	char* pchFilename;							AST(CURRENTFILE)

} ItemSalvageDef;

AUTO_STRUCT;
typedef struct ItemSalvageRewardRequestData
{
	// the item we will be salvaging
	REF_TO(ItemDef) hDef;
	U64 ItemID;

	// the reward data for salvaging that item
	InvRewardRequest* pData;
	
} ItemSalvageRewardRequestData;

// returns if the item is a candidate for being salvaged at all
S32 ItemSalvage_trh_IsItemSalvageable(ATH_ARG NOCONST(Item) *pItem);
#define ItemSalvage_IsItemSalvageable(pItem) ItemSalvage_trh_IsItemSalvageable(CONTAINER_NOCONST(Item, pItem))

// given an item, finds the appropriate reward table.
// assumes that the item is already salvageable
RewardTable* ItemSalvage_trh_GetRewardTableForItem(ATH_ARG NOCONST(Item) *pItem);
#define ItemSalvage_GetRewardTableForItem(pItem) ItemSalvage_trh_GetRewardTableForItem(CONTAINER_NOCONST(Item, pItem))


// returns true if they can perform the salvage, 
//  checks things like if the player has a given powerMode on them (example: being near some object in the world)
S32 ItemSalvage_CanPerformSalvage(Entity *pEnt);




#endif