#ifndef ITEMPROGRESSIONCOMMON_H
#define ITEMPROGRESSIONCOMMON_H

#include "referencesystem.h"

#define MAX_SIMULTANEOUS_FOOD_ITEMS 6 //Maximum number of items that can be fed in a single transaction. Does not include battery.

//This is incredibly stupid, but NW's designers have painted themselves into a corner, so....
//"PREPEND" should be considered legacy/deprecated because it's a garbage way to name items. Everything except for gems should use "Append".
#define TIER_NAME_PATTERN_APPEND_RANK "_R%d"
#define TIER_NAME_PATTERN_PREPEND_TIER "T%d_"
#define TIER_NAME_PATTERN_APPEND_PLAIN_NUMERAL "_%d"

typedef struct RewardValTable RewardValTable;

AUTO_ENUM;
typedef enum ItemProgressionCatalystType {
	kItemProgressionCatalystType_None = 0,
	kItemProgressionCatalystType_RankUpRequirement_SpecificItem,
	kItemProgressionCatalystType_RankUpRequirement_MatchingItemDef,
} ItemProgressionCatalystType;

AUTO_ENUM;
typedef enum ItemProgressionNamingScheme {
	kItemProgressionNamingScheme_None = 0,
	kItemProgressionNamingScheme_AppendRank, ENAMES(AppendRank)
	kItemProgressionNamingScheme_PrependTier, ENAMES(PrependTier)
	kItemProgressionNamingScheme_AppendPlainNumeral, ENAMES(AppendPlainNumeral)
} ItemProgressionNamingScheme;

AUTO_STRUCT;
typedef struct ItemProgressionCatalyst
{
	REF_TO(ItemDef) hItem; AST( STRUCTPARAM )
	ItemProgressionCatalystType eType;
	S32 iNumRequired; AST( DEFAULT(1))
} ItemProgressionCatalyst;

AUTO_STRUCT;
typedef struct ItemProgressionCritWeight
{
	U32 uWeight; AST(NAME(Weight))
	F32 fMult; AST(NAME(Multiplier))
} ItemProgressionCritWeight;

AUTO_STRUCT;
typedef struct ItemProgressionFoodCategories
{
	S32* ea32Categories; AST(NAME(Categories) SUBTABLE(ItemCategoryEnum))
	ItemType eRequiredItemType; AST(NAME(ItemType))
	F32 fEfficiency;
	bool bMustMatchNamingConvention;
	bool bNoCrit;
} ItemProgressionFoodCategories;

AUTO_STRUCT;
typedef struct ItemProgressionTierDef
{
	int iIndex; AST( STRUCTPARAM )
	ItemProgressionCatalyst** eaCatalysts; AST(NAME(CatalystItem))
	U32 uBaseRankUpChance; AST(NAME(BaseRankUpChance))
	U32 uBonusRankUpChancePerFailure; AST(NAME(BonusRankUpChancePerFailure))
	U32 uMinLevel; AST(NAME(MinLevel))
	U32 uMaxLevel; AST(NAME(MaxLevel))
	S32 iEvolutionADCost;
} ItemProgressionTierDef;

AUTO_STRUCT;
typedef struct ItemProgressionDef
{
	const char* pchName; AST( STRUCTPARAM KEY POOL_STRING )
	const char* pchFilename; AST( CURRENTFILE )
	ItemProgressionCritWeight** eaCritWeights; AST(NAME(XPCritWeight))
	ItemProgressionTierDef** eaTiers; AST(NAME(Tier))
	ItemProgressionFoodCategories** eaFoodCategories;
	ItemProgressionNamingScheme eNamingScheme;
	const char* pchEvolutionADCostNumeric; AST(NAME(EvolutionADCostNumeric))
	REF_TO(RewardValTable) hXPRequiredTable; AST(REFDICT(RewardValTable) NAME(XPRequiredTable))
	REF_TO(RewardValTable) hFoodValueTable; AST(REFDICT(RewardValTable) NAME(FoodValueTable))
} ItemProgressionDef;

AUTO_STRUCT;
typedef struct ItemProgressionCatalystArgument
{
	int iCatalystBag;
	int iCatalystSlot;
}ItemProgressionCatalystArgument;

AUTO_STRUCT;
typedef struct ItemProgressionCatalystArgumentList
{
	ItemProgressionCatalystArgument** eaArgs;
}ItemProgressionCatalystArgumentList;

AUTO_ENUM;
typedef enum ItemProgressionUIResultType
{
	kItemProgressionUIResultType_None = 0,
	kItemProgressionUIResultType_Feed,
	kItemProgressionUIResultType_Evo
} ItemProgressionUIResultType;

AUTO_STRUCT;
typedef struct ItemProgressionUILastFoodResult
{
	F32 fCritMult;
	U32 uNetXP;
}ItemProgressionUILastFoodResult;

AUTO_STRUCT;
typedef struct ItemProgressionUILastResult
{
	ItemProgressionUIResultType eType;
	U32 uTime;	//server timestamp when this action occurred 
	bool bEvoSucceeded;
	bool bLevelup;
	U64 u64NewTargetItemID;	//in case we replaced the target item with one we pulled off the top of a stack.
	ItemProgressionUILastFoodResult** eaFoodResults;

}ItemProgressionUILastResult;

AUTO_STRUCT;
typedef struct ItemProgressionCatalystSortData
{
	int iCatalystBag;
	int iCatalystSlot;
	U32 uXP;
	int iCount;
} ItemProgressionCatalystSortData;

extern DictionaryHandle g_hItemProgressionDict;

U32 itemProgression_trh_GetLevel(NOCONST(Item)* pItem);
#define itemProgression_GetLevel(pItem) itemProgression_trh_GetLevel(CONTAINER_NOCONST(Item, pItem))

U32 itemProgression_trh_GetFoodXPValue(NOCONST(Item)* pItem);
#define itemProgression_GetFoodXPValue(pItem) itemProgression_trh_GetFoodXPValue(CONTAINER_NOCONST(Item, pItem))

U32 itemProgression_trh_GetMaxBatteryBonusXP(ATH_ARG NOCONST(Item)* pBattery, U32 uSrcXPAmnt);
#define itemProgression_GetMaxBatteryBonusXP(pBattery, uSrcXPAmnt) itemProgression_trh_GetMaxBatteryBonusXP(CONTAINER_NOCONST(Item, pBattery), uSrcXPAmnt)

U32 itemProgression_trh_GetBatteryXPValue(NOCONST(Item)* pItem);
U32 itemProgression_trh_GetInitialXPValue(NOCONST(Item)* pItem);

U32 itemProgression_trh_GetXPRequiredForLevel(NOCONST(Item)* pItem, U32 uLevel);
#define itemProgression_GetXPRequiredForLevel(pItem, uLevel) itemProgression_trh_GetXPRequiredForLevel(CONTAINER_NOCONST(Item, pItem), uLevel)

void itemProgression_Load();

ItemProgressionTierDef* itemProgressionDef_GetTierAtLevel(ItemProgressionDef* pDef, U32 uLevel, S32 iTierDelta);

ItemProgressionTierDef* itemProgression_trh_GetCurrentTier(NOCONST(Item)* pItem);
#define itemProgression_GetCurrentTier(pItem) itemProgression_trh_GetCurrentTier(CONTAINER_NOCONST(Item, pItem))

bool itemProgression_trh_GetEvoResultDefName(ATH_ARG NOCONST(Item)* pItem, char** pestrNameOut);
#define itemProgression_GetEvoResultDefName(pItem, pestr) itemProgression_trh_GetEvoResultDefName(CONTAINER_NOCONST(Item, pItem), pestr)

ItemProgressionTierDef* itemProgression_trh_GetNextTier(NOCONST(Item)* pItem);
#define itemProgression_GetNextTier(pItem) itemProgression_trh_GetNextTier(CONTAINER_NOCONST(Item,pItem))

U32 itemProgression_trh_CalculateLevelFromXP(NOCONST(Item)* pItem, bool bClampToTier);

bool itemProgression_trh_ReadyToEvo(ATH_ARG NOCONST(Item)* pItem);
#define itemProgression_ReadyToEvo(pItem) itemProgression_trh_ReadyToEvo(CONTAINER_NOCONST(Item, pItem))

bool itemProgression_trh_IsMaxLevel(ATH_ARG NOCONST(Item)* pItem);
#define itemProgression_IsMaxLevel(pItem) itemProgression_trh_IsMaxLevel(CONTAINER_NOCONST(Item, pItem))

int itemProgression_GetXPRequiredForNextLevel(Item* pItem);

F32 itemProgressionDef_trh_CalculateFoodMultiplier(ATH_ARG NOCONST(Item)* pDst, NOCONST(Item)* pFood, bool* bCritEligibleOut);
#define itemProgressionDef_CalculateFoodMultiplier(pDst, pFood) itemProgressionDef_trh_CalculateFoodMultiplier(CONTAINER_NOCONST(Item, pDst), CONTAINER_NOCONST(Item, pFood), NULL)

bool itemProgression_StringIsValidItemDefNamePattern(const char* pchPattern);

bool itemProgression_trh_GetItemDefNamePattern(NOCONST(Item)* pItem, char** pestrOut);
bool itemProgression_trh_CompareItemDefNamePatterns(ATH_ARG NOCONST(Item)* pItemA, ATH_ARG NOCONST(Item)* pItemB);

void itemProgression_trh_FixupNewItemProps(ATH_ARG NOCONST(Item)* pItem);

U32 ItemProgression_GetAdjustedItemXP(SA_PARAM_OP_VALID Item* pItem);
U32 ItemProgression_GetAdjustedItemXPRequiredForNextLevel(SA_PARAM_OP_VALID Item* pItem);

U32 itemProgression_trh_GetTotalXP(ATH_ARG NOCONST(Item)* pItem);
#define itemProgression_GetTotalXP(pItem) itemProgression_trh_GetTotalXP(CONTAINER_NOCONST(Item, pItem))

U32 itemProgression_trh_ItemCanStack(ATH_ARG NOCONST(Item)* pItem);

ItemDef* ItemProgression_trh_GetCatalystItemDef(ATH_ARG NOCONST(Item)* pItem, int iSlot);
#define ItemProgression_GetCatalystItemDef(pItem, iSlot) ItemProgression_trh_GetCatalystItemDef(CONTAINER_NOCONST(Item, pItem), iSlot)

void itemProgression_trh_GetCatalystItemsSortedByXP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Item)* pTarget, ItemDef* pCatalystDef, ItemProgressionCatalystSortData*** peaSortDataOut);
#define itemProgression_GetCatalystItemsSortedByXP(pEnt, pDef, peaSortDataOut) itemProgression_trh_GetCatalystItemsSortedByXP(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pDef, peaSortDataOut)
#endif