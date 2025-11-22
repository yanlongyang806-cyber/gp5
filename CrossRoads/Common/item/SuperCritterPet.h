#ifndef SUPERCRITTERPET_H
#define SUPERCRITTERPET_H

#include "referencesystem.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "inventorycommon.h"

#include "Autogen/itemCommon_h_ast.h"

typedef struct PlayerCostume PlayerCostume;
typedef struct CritterDef CritterDef;
typedef struct DynFxInfo DynFxInfo;
typedef struct HeadshotStyleDef HeadshotStyleDef;
typedef struct EntitySavedSCPData EntitySavedSCPData;
typedef struct ActiveSuperCritterPet ActiveSuperCritterPet;
typedef struct Item Item;
typedef struct NOCONST(EntitySavedSCPData) NOCONST(EntitySavedSCPData);
typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);

extern DictionaryHandle g_hSuperCritterPetDict;


AUTO_STRUCT;
typedef struct SCPEquipSlotDef
{
	InvBagIDs eID;							AST(STRUCTPARAM)
	ItemCategory* peCategories; 			AST(NAME(RestrictCategories) SUBTABLE(ItemCategoryEnum)) 
} SCPEquipSlotDef;

AUTO_STRUCT AST_IGNORE(Level);
typedef struct SCPAltCostumeDef
{
	REF_TO(PlayerCostume) hCostume;			AST(REFDICT(PlayerCostume) NAME(Costume) STRUCTPARAM)
	REF_TO(DynFxInfo) hContinuingPlayerFX;	AST(NAME(ContinuingPlayerFX) REFDICT(DynFxInfo))
	DisplayMessage displayMsg;				AST(NAME(DisplayMsg) STRUCT(parse_DisplayMessage))

	//this is the level requirement to use this costume.  If we need later it could be a more 
	// complex requirement struct.
	U32 iLevel;								AST(NAME(Level))
} SCPAltCostumeDef;

AUTO_STRUCT;
typedef struct SuperCritterPetActivePowerDef
{
	// the power that will be granted.
	REF_TO(PowerDef) hPowerDef;				AST(NAME(PowerDef) REFDICT(PowerDef))
	
	// if the power will be given to the player 
	U32 bAppliesToPlayer : 1;

	// if the power will be given to the summoned SCP
	U32 bAppliesToSummonedPet : 1;

} SuperCritterPetActivePowerDef;

AUTO_STRUCT;
typedef struct SuperCritterPetRankDef
{
	// the SCP's item quality determines the rank
	ItemQuality		eItemQuality;						AST(SUBTABLE(ItemQualityEnum) NAME(ItemQuality) DEFAULT(-1))

	// list of active powers for this rank
	SuperCritterPetActivePowerDef** eaActivePowers;		AST(NAME(ActivePower))

} SuperCritterPetRankDef;


AUTO_STRUCT;
typedef struct SuperCritterPetDef
{
	const char *pchName;					AST(NAME(Name), STRUCTPARAM, KEY, POOL_STRING)
	char *pchFileName;						AST( CURRENTFILE )
	REF_TO(CritterDef) hCritterDef;			AST(NAME(CritterDef) REFDICT(CritterDef))
	REF_TO(CharacterClass) hCachedClassDef;	AST(NO_TEXT_SAVE)
	SCPAltCostumeDef** eaCostumes;		AST(NAME(AltCostume))
	SCPEquipSlotDef** eaEquipSlots;			AST(NAME(EquipmentSlot))
	REF_TO(DynFxInfo) hContinuingPlayerFX;	AST(NAME(ContinuingPlayerFX) REFDICT(DynFxInfo))
	const char* pchStyleDef;				AST(NAME(HeadshotStyle))
	bool bLevelToPlayer;					AST(NAME(LevelToPlayer))
	const char *pchIconName;				AST(NAME(Icon) POOL_STRING )

	// List of ranks that define what powers
	SuperCritterPetRankDef** eaRanks;		AST(NAME(Rank))
} SuperCritterPetDef;

AUTO_STRUCT;
typedef struct SuperCritterPetConfig
{
	Expression* pExprTrainingDuration;			AST(LATEBIND NAME(TrainingDuration))
	Expression* pExprUnbindCost;				AST(LATEBIND NAME(PetUnbindCost))
	Expression* pExprGemUnslotCost;				AST(LATEBIND NAME(GemUnslotCost))
	REF_TO(RewardValTable) hRequiredXPTable;	AST(NAME(RequiredXPTable) REFDICT(RewardValTable))
	REF_TO(ItemDef) hRushTrainingCurrency;		AST(REFDICT(ItemDef))
	F32 fRushCostPerTrainingSecond;				AST(NAME(RushCostPerTrainingSecond))
	S32 iMinRushTrainingCost;					AST(NAME(MinRushTrainingCost))
	int iRenameCost;							AST(NAME(RenameCost))
	REF_TO(ItemDef) hRenamingCurrency;			AST(REFDICT(ItemDef))
	REF_TO(ItemDef) hUnbindingCurrency;			AST(REFDICT(ItemDef))
	REF_TO(ItemDef) hGemUnslottingCurrency;		AST(REFDICT(ItemDef))
	UINT_EARRAY eaiMaxLevelsPerQuality;			AST(NAME(MaxLevelsPerQuality))
	FLOAT_EARRAY eafCostToUpgradeQuality;		AST(NAME(CostToUpgradeQuality))
	FLOAT_EARRAY eafQualityModifiers;					AST(NAME(QualityModifiers))
	int iMaxUpgradeQuality;						AST(NAME(MaxUpgradeQuality))
	F32 fLevelScalingStartsAtPlayerLevel;		AST(NAME(LevelScalingStartsAtPlayerLevel) DEFAULT(0))
	F32 fLevelsPerPlayerLevel;					AST(NAME(LevelsPerPlayerLevel) DEFAULT(1))
	F32 fPetXPMultiplier;						AST(NAME(PetXPGainMultiplier) DEFAULT(1.0))
	UINT_EARRAY eaiGemSlotUnlockLevels;			AST(NAME(GemSlotUnlockLevels))
	UINT_EARRAY eaiEquipSlotUnlockLevels;		AST(NAME(EquipSlotUnlockLevels))
	UINT_EARRAY eaiCostumeUnlockLevels;			AST(NAME(CostumeUnlockLevels))
	
	// a list of categories that a passive power must have in order be evaluated for fake entity stats 
	// the power must have ALL the defined categories
	S32 *piFakeEntStatsPassiveCategories;		AST(NAME(FakeEntStatsPassiveCategories), SUBTABLE(PowerCategoriesEnum))
		
} SuperCritterPetConfig;
extern SuperCritterPetConfig g_SCPConfig;

Item* scp_GetActivePetItem(Entity* pPlayer, int idx);
bool scp_itemIsSCP(Item* pItem);
bool scp_EntIsSuperCritterPet(SA_PARAM_OP_VALID Entity* pEnt);
SuperCritterPet* scp_GetPetFromItem(SA_PARAM_OP_VALID Item* pItem);
SuperCritterPetDef* scp_GetPetDefFromItem(SA_PARAM_OP_VALID Item* pPetItem);
NOCONST(SuperCritterPet)* scp_CreateFromDef(SuperCritterPetDef* pDef, Item* pPetItem, int iLevel);

U32 scp_GetPetCombatLevel(SA_PARAM_OP_VALID Item* pItem);
const char* scp_GetPetItemName(Item* pItem);

float scp_QualityModifer(ItemQuality iQuality);
F32 scp_GetPetPercentToNextLevel(Item* pPetItem);

U32 scp_MaxLevel(Item* pPetItem);
bool scp_LevelIsValid(U32 iLevel, Item* pPetItem);
U32 scp_PetXPToLevelLookup(U32 uiCurXP, Item* pPetItem);
U32 scp_GetPetLevelAfterTraining(Item* pPetItem);
U32 scp_GetPetStartLevelForPlayerLevel(int playerLevel, SA_PARAM_OP_VALID Item* pPetItem);
U32 scp_GetPlayerLevelEquivalentOfPetLevel(int iPetLevel);
F32 scp_GetTotalXPRequiredForLevel(int iLevel, Item* pPetItem);

U32 scp_EvalTrainingTime(Entity* pPlayerEnt, Item* pPetItem);
U32 scp_EvalUnbindCost(Entity* pPlayerEnt, Item* pPetItem);
U32 scp_EvalGemRemoveCost(Entity* pPlayerEnt, Item* pPetItem, ItemDef* pGemItemDef, const char* pchCurrency);

EntityRef scp_GetSummonedPetEntRef(Entity* pEnt);
SA_RET_OP_VALID Item* scp_GetSummonedPetItem(Entity* pEnt);


enumTransactionOutcome scp_trh_AwardActivePetXP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, float delta, bool bonus);

const char* scp_GetActivePetDefName(Entity* pPlayer, int idx);
const char* scp_GetActivePetName(Entity* pPlayer, int idx);
U32 scp_GetActivePetTrainingTimeRemaining(Entity* pPlayer, int idx);
U32 scp_GetActivePetTrainingTimeEnding(Entity* pPlayer, int idx);

SCPAltCostumeDef* scp_GetPetCostumeDef(Item* pPetItem);
int scp_GetMaxCostumeIdx(Item* pPetItem);
bool scp_trh_IsAltCostumeUnlocked(ATH_ARG NOCONST(Entity)* pEnt, int idx, int iCostume, GameAccountDataExtract* pExtract);
#define scp_IsAltCostumeUnlocked(pEnt, idx, iCostume, pExtract) scp_trh_IsAltCostumeUnlocked(CONTAINER_NOCONST(Entity, pEnt), idx, iCostume, pExtract)

bool scp_trh_IsEquipSlotLocked(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iPet, int iEquipSlot);
#define scp_IsEquipSlotLocked(pPlayerEnt, iPet, iEquipSlot) scp_trh_IsEquipSlotLocked(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pPlayerEnt), iPet, iEquipSlot)

bool scp_trh_IsGemSlotLocked(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iPet, int iGemSlot);
#define scp_IsGemSlotLocked(pPlayerEnt, iPet, iGemSlot) scp_trh_IsGemSlotLocked(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pPlayerEnt), iPet, iGemSlot)

bool scp_IsGemSlotLockedOnPet(SuperCritterPet* pPet, int iGemSlot);

bool scp_CanEquip(SuperCritterPet *pPet, int iEquipSlot, Item* pItem);

int scp_GetRushTrainingCost(Entity* pPlayerEnt, int iSlot);
int scp_GetQualityUpgradeCost(Entity* pPlayerEnt, int iSlot);


bool scp_trh_CheckFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag);
#define scp_CheckFlag(pItem, eFlag) scp_trh_CheckFlag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Item, pItem), eFlag)

void scp_trh_SetFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag, bool bSet);

enumTransactionOutcome scp_trh_ResetActivePet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iNumEquipSlots, int iSlot);

F32 scp_GetBonusXPPercentFromGems(Entity* pEnt, Item* pPetItem);

NOCONST(EntitySavedSCPData)* scp_trh_GetOrCreateEntSCPDataStruct(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bCreateIfMissing);
#define scp_GetEntSCPDataStruct(pEnt) ((EntitySavedSCPData*)scp_trh_GetOrCreateEntSCPDataStruct(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), false))

enumTransactionOutcome scp_trh_SetNumActiveSlots(ATR_ARGS, ATH_ARG NOCONST(EntitySavedSCPData)* pData, int iNum);

const char *GetPetEquipmentSlotTypeTranslate(Language lang, SA_PARAM_OP_VALID Item* pPetItem, int iSlot);

#if defined(GAMESERVER) || defined(GAMECLIENT)
Entity* scp_CreateFakeEntity (SA_PARAM_OP_VALID Entity* pPlayer, SA_PARAM_OP_VALID Item* pPetItem, SA_PARAM_OP_VALID ActiveSuperCritterPet* pActivePet);
#endif

S32 scp_GetAltCostumeIdxForPlayer(SA_PARAM_NN_VALID Entity * pPlayerEnt);

// Given the SuperCritterPetDef and the Item of the SCP, fills out a list of SuperCritterPetActivePowerDef that the pet has
S32 scp_GetActivePowerDefs(	SA_PARAM_NN_VALID SuperCritterPetDef *pSCPDef, 
							SA_PARAM_NN_VALID Item *pItem, 
							SA_PARAM_NN_VALID SuperCritterPetActivePowerDef ***peaSCPActivePowersOut);


#ifndef AILIB
#include "AutoGen/SuperCritterPet_h_ast.h"
#endif

void scp_AddPowersFromActivePets(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt);

bool scp_CanBindPet(SA_PARAM_OP_VALID Entity* pEntity, SA_PARAM_OP_VALID Item *pPetItem);
bool scp_CanMovePet(SA_PARAM_OP_VALID Entity* pPlayerEnt, S32 eSrcBag, S32 iSrcSlot, S32 eDstBag, S32 iDstSlot);

bool scp_trh_CanMoveSCPCheckSwap(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
									SuperCritterPetDef* pSrcSCPDef, 
									SuperCritterPetDef* pDstSCPDef, 
									NOCONST(InventoryBag)* pSrcBag, 
									NOCONST(InventoryBag)* pDstBag);
#define scp_CanMoveSCPCheckSwap(pEnt, pSrcSCPDef, pDstSCPDef, pSrcBag, pDstBag) scp_trh_CanMoveSCPCheckSwap(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), pSrcSCPDef, pDstSCPDef, CONTAINER_NOCONST(InventoryBag, (pSrcBag)), CONTAINER_NOCONST(InventoryBag, (pDstBag)))

#endif