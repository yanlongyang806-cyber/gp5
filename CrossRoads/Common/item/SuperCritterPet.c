
#include "referencesystem.h"
#include "itemCommon.h"
#include "supercritterpet.h"
#include "entity.h"
#include "EntitySavedData.h"
#include "PowerModes.h"
#include "NotifyCommon.h"
#include "GameAccountDataCommon.h"
#include "resourceinfo.h"
#include "ResourceManager.h"
#include "file.h"
#include "CharacterClass.h"
#include "entCritter.h"
#include "Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "PowerVars.h"
#include "Powers.h"
#include "CharacterAttribs.h"
#include "CombatEval.h"
#include "Character.h"
#include "Character_mods.h"
#include "PowerApplication.h"
#include "PowerEnhancements.h"
#include "PowerHelpers.h"
#include "WorldGrid.h"


#if defined(GAMESERVER) || defined(GAMECLIENT)
#include "PowersMovement.h"
#include "entCritter.h"
#include "entCritter_h_ast.h"
#endif

#if defined(GAMESERVER) || defined(APPSERVER)
#include "inventoryTransactions.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#endif

#ifdef GAMESERVER
#include "gslSuperCritterPet.h"
#include "gslPowerTransactions.h"
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern DictionaryHandle g_hRewardValTableDict;
DictionaryHandle g_hSuperCritterPetDict;
ExprContext* g_pSCPContext = NULL;

SuperCritterPetConfig g_SCPConfig;

static const char *s_pcVarLevelDelta;
static int s_hVarLevelDelta = 0;
static const char *s_pcVarPetLevel;
static int s_hVarPetLevel = 0;
static const char *s_pcVarPetQuality;
static int s_hVarPetQuality = 0;
static const char *s_pcVarPetGemLevel;
static int s_hVarPetGemLevel = 0;


//get a superCritterPet from an item that is one.
SuperCritterPet* scp_GetPetFromItem(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
		return pItem->pSpecialProps->pSuperCritterPet;
	return NULL;
}

//is an item a SCP?
bool scp_itemIsSCP(Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet){
		return true;
	}
	return false;
}

AUTO_RUN;
int SuperCritterPetRegisterDict(void)
{
	ExprFuncTable* stFuncs;
	// Set up reference dictionary
	g_hSuperCritterPetDict = RefSystem_RegisterSelfDefiningDictionary("SuperCritterPetDef", false, parse_SuperCritterPetDef, true, true, NULL);

	resDictSetDisplayName(g_hSuperCritterPetDict, "SuperCritterPet File", "SuperCritterPet Files", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hSuperCritterPetDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hSuperCritterPetDict, NULL, NULL, NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hSuperCritterPetDict, 16, false, resClientRequestSendReferentCommand);
	}

	//Also set up the expression context while we're here.
	g_pSCPContext = exprContextCreate();
	exprContextSetAllowRuntimePartition(g_pSCPContext);

	s_pcVarLevelDelta = allocAddStaticString("NumLevelsTrained");
	s_pcVarPetLevel = allocAddStaticString("PetLevel");
	s_pcVarPetQuality = allocAddStaticString("PetQuality");
	s_pcVarPetGemLevel = allocAddStaticString("PetGemLevel");

	stFuncs = exprContextCreateFunctionTable("SuperCritterPet");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsGeneric");
	exprContextAddFuncsToTableByTag(stFuncs, "gameutil");
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextSetFuncTable(g_pSCPContext, stFuncs);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarLevelDelta, 0, &s_hVarLevelDelta);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, 0, &s_hVarPetLevel);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, 0, &s_hVarPetQuality);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetGemLevel, 0, &s_hVarPetGemLevel);

	return 1;
}

static void scp_ValidateRefs(SuperCritterPetDef* pPetDef)
{
	int i;
	if (IsServer())
	{
		if (REF_IS_SET_BUT_ABSENT(pPetDef->hCritterDef))
		{
			ErrorFilenamef(pPetDef->pchFileName, "SuperCritterPet %s specifies non-existent CritterDef \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->hCritterDef));
		}
		for(i = 0; i < eaSize(&pPetDef->eaCostumes); i++)
		{
			if (pPetDef->eaCostumes[i])
			{
				if (REF_IS_SET_BUT_ABSENT(pPetDef->eaCostumes[i]->hCostume))
				{
					ErrorFilenamef(pPetDef->pchFileName, "A costume on SuperCritterPet %s specifies non-existent Costume \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->eaCostumes[i]->hCostume));
				}
				if (REF_IS_SET_BUT_ABSENT(pPetDef->eaCostumes[i]->displayMsg.hMessage))
				{
					ErrorFilenamef(pPetDef->pchFileName, "A costume on SuperCritterPet %s specifies non-existent Display Message \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->eaCostumes[i]->displayMsg.hMessage));
				}
			}
		}

		for (i = 0; i < eaSize(&pPetDef->eaRanks); ++i)
		{
			S32 xx;
			SuperCritterPetRankDef *pRank = pPetDef->eaRanks[i];
			const char *str = StaticDefineIntRevLookup(ItemQualityEnum, pRank->eItemQuality);

			if (!str)
				str = "None";

			if (pRank->eItemQuality == kItemQuality_None)
			{
				ErrorFilenamef(pPetDef->pchFileName, "SuperCritterPet %s has an unspecified or invalid ItemQuality", pPetDef->pchName);
			}

			for (xx = i + 1; xx < eaSize(&pPetDef->eaRanks); ++xx)
			{
				SuperCritterPetRankDef *pRankOther = pPetDef->eaRanks[xx];

				if (pRankOther->eItemQuality == pRank->eItemQuality)
				{
					ErrorFilenamef(pPetDef->pchFileName, "SuperCritterPet %s has two matching ranks %s. This is invalid.", 
											pPetDef->pchName, str);
				}
			}


			for (xx = 0; xx < eaSize(&pRank->eaActivePowers); ++xx)
			{
				SuperCritterPetActivePowerDef *pPower = pRank->eaActivePowers[xx];
				PowerDef *pPowerDef = GET_REF(pPower->hPowerDef);
				if (!pPowerDef)
				{
					ErrorFilenamef(pPetDef->pchFileName, "A power on SuperCritterPet %s rank %s is not set or is invalid.", 
									pPetDef->pchName, str);
				}
				else if (pPowerDef->eType != kPowerType_Passive && pPowerDef->eType != kPowerType_Innate)
				{
					ErrorFilenamef(pPetDef->pchFileName, "Power %s on SuperCritterPet %s rank %s is not a Passive or Innate. Invalid!", 
									pPowerDef->pchName, pPetDef->pchName, str);
				}
				else if (!pPower->bAppliesToPlayer && !pPower->bAppliesToSummonedPet)
				{
					ErrorFilenamef(pPetDef->pchFileName, "Power %s on SuperCritterPet %s rank %s is not set to apply to the pet or player.", 
										pPowerDef->pchName, pPetDef->pchName, str);
				}
			}
		}

	}
	else if (IsClient())
	{
		if (REF_IS_SET_BUT_ABSENT(pPetDef->hContinuingPlayerFX))
		{
			ErrorFilenamef(pPetDef->pchFileName, "SuperCritterPet %s specifies non-existent Continuing Player FX \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->hContinuingPlayerFX));
		}
		for(i = 0; i < eaSize(&pPetDef->eaCostumes); i++)
		{
			if (REF_IS_SET_BUT_ABSENT(pPetDef->eaCostumes[i]->hContinuingPlayerFX))
			{
				ErrorFilenamef(pPetDef->pchFileName, "A costume on SuperCritterPet %s specifies non-existent Continuing Player FX \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->eaCostumes[i]->hContinuingPlayerFX));
			}
		}
	}
}

static int SCPResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SuperCritterPetDef *pPetDef, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_CHECK_REFERENCES: // Called when all data has been loaded
		{
			scp_ValidateRefs(pPetDef);
			return VALIDATE_HANDLED;
		}break;
		xcase RESVALIDATE_POST_BINNING: // Called when all data has been loaded
		{
			if (IsGameServerSpecificallly_NotRelatedTypes())
			{
				//Cache the actual class so the client will get it too, for tooltips.
				CritterDef* pCritterDef = GET_REF(pPetDef->hCritterDef);
				CharacterClass* pClass = pCritterDef ? characterclass_GetAdjustedClass(pCritterDef->pchClass, 1, pCritterDef->pcSubRank, pCritterDef) : NULL;
				if (pClass)
					SET_HANDLE_FROM_STRING(g_hCharacterClassDict, pClass->pchName, pPetDef->hCachedClassDef);
				else
					Errorf("Class %s not found during SCP initialization.", pCritterDef->pchClass);
			}
			return VALIDATE_HANDLED;
		}break;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(SuperCritterPet) ASTRT_DEPS(InventoryBagIDs, PowerVars, ItemTags, Critters, CharacterClassInfos);
void SuperCritterPetLoad(void)
{
	if(IsGameServerSpecificallly_NotRelatedTypes() || IsGatewayServer())
	{
		resDictManageValidation(g_hSuperCritterPetDict, SCPResValidateCB);
		resLoadResourcesFromDisk(g_hSuperCritterPetDict,"defs/pets",".scp", "supercritterpet.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}

	if(IsGameServerSpecificallly_NotRelatedTypes() || IsClient() || IsGatewayServer())
	{
		ParserLoadFiles(NULL, "defs/config/SuperCritterPetConfig.def", "SuperCritterPetConfig.bin", PARSER_OPTIONALFLAG, parse_SuperCritterPetConfig, &g_SCPConfig);
	
		exprGenerate(g_SCPConfig.pExprTrainingDuration, g_pSCPContext);
		exprGenerate(g_SCPConfig.pExprUnbindCost, g_pSCPContext);
		exprGenerate(g_SCPConfig.pExprGemUnslotCost, g_pSCPContext);
	}
}

AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_SetNumActiveSlots(ATR_ARGS, ATH_ARG NOCONST(EntitySavedSCPData)* pData, int iNum)
{
	if (ISNULL(pData))
		return TRANSACTION_OUTCOME_FAILURE;

	eaSetSizeStructNoConst(&pData->ppSuperCritterPets, parse_ActiveSuperCritterPet, iNum);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
NOCONST(EntitySavedSCPData)* scp_trh_GetOrCreateEntSCPDataStruct(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bAllowModification)
{
#ifdef GAMECLIENT
	if (bAllowModification)
		assertmsg(0, "Attempting to run scp_trh_GetOrCreateEntSCPDataStruct() on the gameclient with bAllowModification = true! This is a big no-no!");
#endif

	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved))
	{
		NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, NULL);
		int iNum = invbag_trh_maxslots(pEnt, pBag);
		int iPet;
		
		if (bAllowModification)
		{
			if (ISNULL(pEnt->pSaved->pSCPData))
			{
				pEnt->pSaved->pSCPData = StructCreateNoConst(parse_EntitySavedSCPData);
			}

			scp_trh_SetNumActiveSlots(ATR_PASS_ARGS, pEnt->pSaved->pSCPData, iNum);

			for(iPet = 0; iPet < iNum; iPet++)
			{
				NOCONST(ActiveSuperCritterPet)* pActivePet = pEnt->pSaved->pSCPData->ppSuperCritterPets[iPet];
				NOCONST(Item)* pPetItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pBag, iPet);
				ItemDef* pItemDef = SAFE_GET_REF(pPetItem, hItem);
				SuperCritterPetDef* pPetDef = SAFE_GET_REF(pItemDef, hSCPdef);
				if (!pActivePet->pEquipment)
				{
					int iNumEquipSlots = pPetDef ? eaSize(&pPetDef->eaEquipSlots) : 0;
					pActivePet->pEquipment = StructCreateNoConst(parse_InventoryBag);
					pActivePet->pEquipment->n_additional_slots = iNumEquipSlots;
				}
			}
		}
		return pEnt->pSaved->pSCPData;
	}
	return NULL;
}

//Less locking than scp_trh_GetOrCreateEntSCPDataStruct()
AUTO_TRANS_HELPER;
int scp_trh_GetSummonedPetIdx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pSCPData))
	{
		return pEnt->pSaved->pSCPData->iSummonedSCP;
	}
	return -1;
}

//Less locking than scp_trh_GetOrCreateEntSCPDataStruct()
AUTO_TRANS_HELPER;
F32 scp_trh_GetSummonedPetBonusXPPct(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pSCPData))
	{
		return pEnt->pSaved->pSCPData->fCachedPetBonusXPPct;
	}
	return 1.0f;
}

//Less locking than scp_trh_GetOrCreateEntSCPDataStruct()
AUTO_TRANS_HELPER;
EntityRef scp_trh_GetSummonedPetEntRef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pSCPData))
	{
		return pEnt->pSaved->pSCPData->erSCP;
	}
	return 0;
}

float scp_QualityModifer(ItemQuality iQuality)
{
	if (iQuality >=0 && iQuality < eafSize(&g_SCPConfig.eafQualityModifiers))
	{
		return g_SCPConfig.eafQualityModifiers[iQuality];
	}
	return 0.0F;
}

// return the max level of the SCP based on its quality.
U32 scp_MaxLevel(Item* pPetItem)
{
	if(pPetItem)
	{
		int quality = item_GetQuality(pPetItem);

		if (quality >= 0 && quality < eafSize(&g_SCPConfig.eaiMaxLevelsPerQuality))
		{
			return MAX(g_SCPConfig.eaiMaxLevelsPerQuality[quality], 1);
		}
		else if(GET_REF(pPetItem->hItem))
		{
			//this should never happen.
			devassertmsg(0, "Trying to find the max level of a Super Critter Pet from an item with an unexpected Quality.");
		}
	}
	return 1;
}

bool scp_LevelIsValid(U32 uLevel, Item* pPetItem)
{
	RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
	return (uLevel > 0 && uLevel <= scp_MaxLevel(pPetItem) && uLevel <= (U32) eafSize(&pXPTable->Val));
}

F32 scp_GetTotalXPRequiredForLevel(int iLevel, Item* pPetItem)
{
	RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
	if (scp_LevelIsValid(iLevel, pPetItem))
	{
		if (pXPTable)
		{
			return pXPTable->Val[CLAMP_TAB_LEVEL(USER_TO_TAB_LEVEL(iLevel))];
		}
	}
	return 0;
}

NOCONST(SuperCritterPet)* scp_CreateFromDef(SuperCritterPetDef* pDef, Item* pPetItem, int iPlayerLevel)
{
	NOCONST(SuperCritterPet)* pPet = StructCreateNoConst(parse_SuperCritterPet);
	SuperCritterPetDef* pPetDef = RefSystem_ReferentFromString(g_hSuperCritterPetDict, pDef->pchName);
	CritterDef* pCritterDef = GET_REF(pPetDef->hCritterDef);
	CharacterClass* pClass = pCritterDef ? characterclass_GetAdjustedClass(pCritterDef->pchClass, 1, pCritterDef->pcSubRank, pCritterDef) : NULL;
	SET_HANDLE_FROM_STRING(g_hSuperCritterPetDict, pDef->pchName, pPet->hPetDef);
	if (pClass)
		SET_HANDLE_FROM_STRING(g_hCharacterClassDict, pClass->pchName, pPet->hClassDef);
	else
		SET_HANDLE_FROM_STRING(g_hCharacterClassDict, "Default", pPet->hClassDef);

	pPet->uLevel = scp_GetPetStartLevelForPlayerLevel(iPlayerLevel, pPetItem);
	pPet->uXP = scp_GetTotalXPRequiredForLevel(pPet->uLevel, pPetItem);
	return pPet;
}

// return SCPAltCostumeDef for this SCP.
SCPAltCostumeDef* scp_GetPetCostumeDef(Item* pPetItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	if (pPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		if (pDef && pDef->eaCostumes && pPet->iCurrentSkin >= 0 && pPet->iCurrentSkin <= scp_GetMaxCostumeIdx(pPetItem))
		{
			return pDef->eaCostumes[pPet->iCurrentSkin];
		}
		else if(pDef && pDef->eaCostumes)
		{
			// negative iCurrentSkin means use greatest; too high means bad data was set before a bug fix: use greatest.
			return pDef->eaCostumes[scp_GetMaxCostumeIdx(pPetItem)];
		} 
	}
	return NULL;
}


// is the iCostume-th costume unlocked for the active SCP in slot idx?  Non-trh macro scp_IsAltCostumeUnlocked.
AUTO_TRANS_HELPER;
bool scp_trh_IsAltCostumeUnlocked(ATH_ARG NOCONST(Entity)* pEnt, int idx, int iCostume, GameAccountDataExtract* pExtract)
{
	NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_EMPTY_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, idx, pExtract);
	if (NONNULL(pPetItem) && NONNULL(pPetItem->pSpecialProps) && NONNULL(pPetItem->pSpecialProps->pSuperCritterPet))
	{
		NOCONST(SuperCritterPet)* pPet = pPetItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef *pPetDef = GET_REF(pPet->hPetDef);
		if (iCostume < eaiSize(&g_SCPConfig.eaiCostumeUnlockLevels) && iCostume < eaSize(&pPetDef->eaCostumes)){
			return g_SCPConfig.eaiCostumeUnlockLevels[iCostume] <= pPet->uLevel;
		}
	}
	return false;
	
}

// get the last (in earray) costume that is unlocked.
// this is mostly for use if "use latest costume" is selected".
int scp_GetMaxCostumeIdx(Item* pPetItem)
{
	int i;
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	SuperCritterPetDef* pPetDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
	U32 uLevel = scp_GetPetCombatLevel(pPetItem);
	if (pPetDef)
	{
		for (i = eaiSize(&g_SCPConfig.eaiCostumeUnlockLevels) - 1; i >= 0 ; i--)
		{
			if(uLevel >= g_SCPConfig.eaiCostumeUnlockLevels[i] && i < eaSize(&pPetDef->eaCostumes))
			{
				return i;
			}
		}
	}
	else
	{
		return 0;
	}

	devassertmsgf(0, "Couldn't find an unlocked costume for level %i SCP %s", scp_GetPetCombatLevel(pPetItem), scp_GetPetItemName(pPetItem));
	return 0;
}

//get the item of the SCP in Active slot idx.
Item* scp_GetActivePetItem(Entity* pPlayer, int idx)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayer);
	InventoryBag* pPetBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPlayer), InvBagIDs_SuperCritterPets, pExtract);
	return pPetBag ? inv_bag_GetItem(pPetBag, idx) : NULL;
}

//gets the level of an SCP with uiCurXP experience, restrained by limits.
U32 scp_PetXPToLevelLookup(U32 uiCurXP, Item* pPetItem)
{
	RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
	U32 i;
	if (pXPTable)
	{
		U32 iMaxLevel = scp_MaxLevel(pPetItem);
		MIN1(iMaxLevel, (U32)eafSize(&pXPTable->Val));
		for (i = 0; i < iMaxLevel && (U32)pXPTable->Val[i] <= uiCurXP; i++)
		{
			//count up to next level that we qualify for
		}
		return MIN(i, iMaxLevel);	//reward table goes past max level.
	}
	return 1;
}


//converts a pet's level to player level terms.  Used for doing combat math times when level matters.
U32 scp_GetPlayerLevelEquivalentOfPetLevel(int iPetLevel){
	return CLAMP((iPetLevel / g_SCPConfig.fLevelsPerPlayerLevel + g_SCPConfig.fLevelScalingStartsAtPlayerLevel), 1, MAX_USER_LEVEL);
}

// check if an entity is an SCP. Note that this give false negatives if the owner's ER changes!
bool scp_EntIsSuperCritterPet(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYCRITTER && pEnt->erOwner)
	{
		Entity* pOwner = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->erOwner);
		return (pOwner && scp_GetSummonedPetEntRef(pOwner) == pEnt->myRef);
	}
	return false;
}
U32 scp_GetPetLevelAfterTraining(Item* pPetItem)
{
	if (scp_itemIsSCP(pPetItem))
	{
		return scp_PetXPToLevelLookup(scp_GetPetFromItem(pPetItem)->uXP, pPetItem);
	}
	return 1;
}

F32 scp_GetPetPercentToNextLevel(Item* pPetItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	if( pPet )
	{
		RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
		int iLevel = scp_PetXPToLevelLookup(pPet->uXP, pPetItem);
		//the xp table for this is set up so that the xp required for level 2 is on the level 1 row.
		if (scp_LevelIsValid(iLevel+1, pPetItem) && (pXPTable->Val[iLevel]-pXPTable->Val[iLevel-1] > 0))
			return (pPet->uXP-pXPTable->Val[iLevel-1])/(pXPTable->Val[iLevel]-pXPTable->Val[iLevel-1]);
	}
	return 0.0;
}

U32 scp_GetPetCombatLevel(Item* pItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pItem);
	if (pPet)
	{
		return pPet->uLevel;
	}
	return -1;
}

U32 scp_GetActivePetTrainingTimeRemaining(Entity* pPlayer, int idx)
{
#if GAMECLIENT || GAMESERVER
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayer);
	if (pData && idx >= 0 && idx < eaSize(&pData->ppSuperCritterPets))
	{
		return pData->ppSuperCritterPets[idx]->uiTimeFinishTraining > 0 ? pData->ppSuperCritterPets[idx]->uiTimeFinishTraining - timeServerSecondsSince2000() : 0;
	}
#endif
	return 0;
}

U32 scp_GetActivePetTrainingTimeEnding(Entity* pPlayer, int idx)
{
#if GAMECLIENT || GAMESERVER
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayer);
	if (pData && idx >= 0 && idx < eaSize(&pData->ppSuperCritterPets))
	{
		return pData->ppSuperCritterPets[idx]->uiTimeFinishTraining > 0 ? pData->ppSuperCritterPets[idx]->uiTimeFinishTraining : 0;
	}
#endif
	return 0;
}

const char* scp_GetPetItemName(Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pItem->pSpecialProps->pSuperCritterPet->hPetDef);
		CritterDef* pCritterDef = GET_REF(pDef->hCritterDef);
		if (pItem->pSpecialProps->pSuperCritterPet->pchName)
			return pItem->pSpecialProps->pSuperCritterPet->pchName;
		else if (pDef)
			return TranslateDisplayMessage(pCritterDef->displayNameMsg);
	}
	return NULL;
}

const char* scp_GetActivePetName(Entity* pPlayer, int idx)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pPlayer, idx));
	if (pPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		CritterDef* pCritterDef = NULL;
		if (pDef)
		{
			pCritterDef = GET_REF(pDef->hCritterDef);
		}
		if (pPet->pchName)
		{
			return pPet->pchName;
		}
		else if (pCritterDef)
		{
			return TranslateDisplayMessage(pCritterDef->displayNameMsg);
		}
	}
	return NULL;
}

SuperCritterPetDef* scp_GetPetDefFromItem(SA_PARAM_OP_VALID Item* pPetItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	if (pPet)
	{
		return GET_REF(pPet->hPetDef);
	}
	return NULL;
}

const char* scp_GetActivePetDefName(Entity* pPlayer, int idx)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pPlayer, idx));
	if (pPet)
	{
		return REF_STRING_FROM_HANDLE(pPet->hPetDef);
	}
	return NULL;
}

U32 scp_EvalTrainingTime(Entity* pPlayerEnt, Item* pPetItem)
{
	MultiVal val = {0};
	SuperCritterPet* pPet = SAFE_MEMBER2(pPetItem,pSpecialProps,pSuperCritterPet);
	int uiLevelDelta = scp_GetPetLevelAfterTraining(pPetItem) - pPet->uLevel;
	int quality = item_GetQuality(pPetItem);

	if (pPet){

		exprContextSetPartition(g_pSCPContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, pPet->uLevel, &s_hVarPetLevel);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, quality, &s_hVarPetQuality);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarLevelDelta, uiLevelDelta, &s_hVarLevelDelta);

		exprEvaluate(g_SCPConfig.pExprTrainingDuration, g_pSCPContext, &val);

		return MultiValGetInt(&val, NULL);
	}
	return 0;
}

U32 scp_EvalGemRemoveCost(Entity* pPlayerEnt, Item* pPetItem, ItemDef* pGemItemDef, const char* pchCurrency)
{
	MultiVal val = {0};
	int quality = item_GetQuality(pPetItem);
	SuperCritterPet* pPet = SAFE_MEMBER2(pPetItem,pSpecialProps,pSuperCritterPet);
	int uiLevelDelta = scp_GetPetLevelAfterTraining(pPetItem) - pPet->uLevel;
	ItemDef* pCurrencyDef = GET_REF(g_SCPConfig.hGemUnslottingCurrency);

	if(!pCurrencyDef || stricmp(pchCurrency, pCurrencyDef->pchName))
	{
		//wrong currency.
		return 0;
	}

	if (pPet){

		exprContextSetPartition(g_pSCPContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, pPet->uLevel, &s_hVarPetLevel);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, quality, &s_hVarPetQuality);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetGemLevel, pGemItemDef->iLevel, &s_hVarPetGemLevel);

		exprEvaluate(g_SCPConfig.pExprGemUnslotCost, g_pSCPContext, &val);

		return MultiValGetInt(&val, NULL);
	}
	return 0;
}

U32 scp_EvalUnbindCost(Entity* pPlayerEnt, Item* pPetItem)
{
	MultiVal val = {0};
	int quality = item_GetQuality(pPetItem);
	SuperCritterPet* pPet = SAFE_MEMBER2(pPetItem,pSpecialProps,pSuperCritterPet);
	if (pPet){

		exprContextSetPartition(g_pSCPContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, pPet->uLevel, &s_hVarPetLevel);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, quality, &s_hVarPetQuality);

		exprEvaluate(g_SCPConfig.pExprUnbindCost, g_pSCPContext, &val);

		return MultiValGetInt(&val, NULL);
	}
	return 0;
}

EntityRef scp_GetSummonedPetEntRef(Entity* pEnt)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && eaSize(&pData->ppSuperCritterPets) > 0)
	{
		if (pData->iSummonedSCP > -1 && pData->iSummonedSCP < eaSize(&pData->ppSuperCritterPets))
		{
			return pData->erSCP;
		}
	}
	return 0;
}

Item* scp_GetSummonedPetItem(Entity* pEnt)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && eaSize(&pData->ppSuperCritterPets) > 0)
	{
		if (pData->iSummonedSCP >= 0 && pData->iSummonedSCP < eaSize(&pData->ppSuperCritterPets))
		{
			return scp_GetActivePetItem(pEnt, pData->iSummonedSCP);
		}
	}
	return 0;
}

//IGNORES BAG PERMISSIONS ON GAMEACCOUNTDATAEXTRACT.
// The SuperCritterPets bag is available to all players.
// Ignoring pExtract allows us to get this bag directly from inside inv_lite_trh_SetNumericInternal()
// for the purpose of awarding pet XP.
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[]");
NOCONST(InventoryBag)* scp_trh_GetActivePetBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return NULL;

	return eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 35 /* Literal InvBagIDs_SuperCritterPets */);
}

//if this is already bonus xp, use bonus=true so the bonus doesn't get more bonuses (want to add not multiply).
AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_AwardActivePetXP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, float delta, bool bonus)
{
	int iSummonedPetIdx = scp_trh_GetSummonedPetIdx(ATR_PASS_ARGS, pEnt);
	F32 fBonusXPPct = scp_trh_GetSummonedPetBonusXPPct(ATR_PASS_ARGS, pEnt);
#ifdef GAMECLIENT
	assertmsg(0, "scp_trh_AwardActivePetXP() cannot be run on the client!");
#endif
	if (iSummonedPetIdx > -1)
	{
		NOCONST(InventoryBag)* pBag = scp_trh_GetActivePetBag(ATR_PASS_ARGS, pEnt);
		NOCONST(Item)* pPetItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pBag, iSummonedPetIdx);
		
		//no XP for dead pets or pets blocked by region rules
		if (NONNULL(pPetItem) && NONNULL(pPetItem->pSpecialProps) && NONNULL(pPetItem->pSpecialProps->pSuperCritterPet) && !scp_trh_CheckFlag(ATR_PASS_ARGS, pPetItem, kSuperCritterPetFlag_Dead) && scp_trh_GetSummonedPetEntRef(ATR_PASS_ARGS, pEnt))
		{
			F32 fXPDelta = delta * g_SCPConfig.fPetXPMultiplier;
			if (!bonus)
				fXPDelta *= fBonusXPPct;
			pPetItem->pSpecialProps->pSuperCritterPet->uXP += fXPDelta;

#if defined(GAMESERVER) || defined(APPSERVER)
			QueueRemoteCommand_scp_PetGainedXP_CB(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, iSummonedPetIdx, fXPDelta);
#endif
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
bool scp_trh_IsEquipSlotLocked(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iPet, int iEquipSlot)
{
	if (ea32Size(&g_SCPConfig.eaiEquipSlotUnlockLevels) > 0)
	{
		NOCONST(InventoryBag)* pSCPBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, NULL);
		NOCONST(Item)* pPetItem =  pSCPBag ? inv_bag_trh_GetItem(ATR_PASS_ARGS, pSCPBag, iPet) : NULL;
		U32 iPetLevel = pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet ? pPetItem->pSpecialProps->pSuperCritterPet->uLevel : 0;
		if (iPetLevel < g_SCPConfig.eaiEquipSlotUnlockLevels[iEquipSlot])
			return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
bool scp_trh_IsGemSlotLocked(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iPet, int iGemSlot)
{
	if (ea32Size(&g_SCPConfig.eaiGemSlotUnlockLevels) > 0)
	{
		NOCONST(InventoryBag)* pSCPBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, InvBagIDs_SuperCritterPets, NULL);
		NOCONST(Item)* pPetItem =  pSCPBag ? inv_bag_trh_GetItem(ATR_PASS_ARGS, pSCPBag, iPet) : NULL;
		U32 uPetLevel = pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet ? pPetItem->pSpecialProps->pSuperCritterPet->uLevel : 0;
		if (uPetLevel < g_SCPConfig.eaiGemSlotUnlockLevels[iGemSlot])
			return true;
	}
	return false;
}

bool scp_IsGemSlotLockedOnPet(SuperCritterPet* pPet, int iGemSlot)
{
	if (pPet && ea32Size(&g_SCPConfig.eaiGemSlotUnlockLevels) > 0)
	{
		U32 uPetLevel = pPet->uLevel;
		if (uPetLevel < g_SCPConfig.eaiGemSlotUnlockLevels[iGemSlot])
			return true;
	}
	return false;

}


int scp_GetRushTrainingCost(Entity* pPlayerEnt, int iSlot)
{
	Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, iSlot);
	//just scale from the training time lookup.
	int ret = g_SCPConfig.fRushCostPerTrainingSecond * scp_EvalTrainingTime(pPlayerEnt, pPetItem);
	return max(ret, 0);
}

int scp_GetQualityUpgradeCost(Entity* pPlayerEnt, int iSlot)
{
	Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, iSlot);
	int iQuality = item_GetQuality(pPetItem);
	if ( iQuality >= 0 && iQuality < ea32Size(&g_SCPConfig.eafCostToUpgradeQuality))
	{
		return max(g_SCPConfig.eafCostToUpgradeQuality[iQuality], 0);
	}
	return 0;
}

AUTO_TRANS_HELPER;
bool scp_trh_CheckFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag)
{
	return (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet && (pItem->pSpecialProps->pSuperCritterPet->bfFlags & eFlag));
}

AUTO_TRANS_HELPER;
void scp_trh_SetFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag, bool bSet)
{
	if (NONNULL(pItem) && NONNULL(pItem->pSpecialProps) && NONNULL(pItem->pSpecialProps->pSuperCritterPet))
	{
		if (bSet)
			pItem->pSpecialProps->pSuperCritterPet->bfFlags |= eFlag;
		else
			pItem->pSpecialProps->pSuperCritterPet->bfFlags &= ~eFlag;
	}
}

//Unsummons the pet and empties its inventory into the player's.
AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_ResetActivePet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iNumEquipSlots, int iSlot)
{
	if (NONNULL(pPlayerEnt) && NONNULL(pPlayerEnt->pSaved))
	{
		NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pPlayerEnt, true);
		NOCONST(ActiveSuperCritterPet)* pPet = eaGet(&pData->ppSuperCritterPets, iSlot);
		NOCONST(InventoryBag)* pInvBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt,  2 /* Literal InvBagIDs_Inventory */, NULL);
		if (ISNULL(pPet))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		if (pData->iSummonedSCP == iSlot)
			pData->iSummonedSCP = -1;

		pPet->uiTimeFinishTraining = 0;

		if (!pPet->pEquipment)
			pPet->pEquipment = StructCreateNoConst(parse_InventoryBag);

		if (!inv_bag_trh_BagEmpty(ATR_PASS_ARGS, pPet->pEquipment))
		{
			if (!inv_ent_trh_MoveAllItemsFromBag(ATR_PASS_ARGS, pPlayerEnt, pPet->pEquipment, pInvBag, true, NULL, NULL))
				return TRANSACTION_OUTCOME_FAILURE;
		}
		pPet->pEquipment->n_additional_slots = iNumEquipSlots;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

F32 scp_GetBonusXPPercentFromGems(Entity* pEnt, Item* pPetItem)
{
	int iGem, iPow, iAttr;
	F32 retVal = 0.0f;
	
	if (!pPetItem || !pPetItem->pSpecialProps || !pPetItem->pSpecialProps->pSuperCritterPet)
		return retVal;

	for (iGem = 0; iGem < eaSize(&pPetItem->pSpecialProps->ppItemGemSlots); iGem++)
	{
		ItemGemSlot* pGem = pPetItem->pSpecialProps->ppItemGemSlots[iGem];

		if (!pGem)
			continue;

		for (iPow = 0; iPow < eaSize(&pGem->ppPowers); iPow++)
		{
			PowerDef* pPowDef = SAFE_GET_REF(pGem->ppPowers[iPow], hDef);

			if (!pPowDef || pPowDef->eType != kPowerType_Passive)
				continue;

			for (iAttr = 0; iAttr < eaSize(&pPowDef->ppOrderedMods); iAttr++)
			{
				AttribModDef* pAttrDef = pPowDef->ppOrderedMods[iAttr];

				if (!pAttrDef || pAttrDef->eTarget != kModTarget_Self)
					continue;

				if (pAttrDef->offAttrib == kAttribType_RewardModifier)
				{
					RewardModifierParams* pParams = (RewardModifierParams*)pAttrDef->pParams;
					if (REF_STRING_FROM_HANDLE(pParams->hNumeric) == allocAddString("XP"))
					{
						combateval_ContextSetupSimple(NULL, 1, NULL);
						retVal += combateval_EvalNew(entGetPartitionIdx(pEnt), pAttrDef->pExprMagnitude, kCombatEvalContext_Simple, NULL);
					}
				}
			}
		}
	}
	return retVal + 1.0;
}

//Check whether pPet is allowed to put pItem in it's iEquipSlot slot.
bool scp_CanEquip(SuperCritterPet *pPet, int iEquipSlot, Item* pItem)
{
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	SuperCritterPetDef* pPetDef;
	int i;

	if (   !pItemDef
		|| (pItemDef->eType != kItemType_Upgrade && pItemDef->eType != kItemType_Weapon) 
		|| item_IsUnidentified(pItem))
		return false;

	pPetDef = GET_REF(pPet->hPetDef);
	if(!pPetDef)
		return false;
	for (i = 0; i < eaiSize(&pPetDef->eaEquipSlots[iEquipSlot]->peCategories); i++)
	{
		if (!(eaiFind(&pItemDef->peCategories, pPetDef->eaEquipSlots[iEquipSlot]->peCategories[i]) >= 0))
			return false;
	}
	for (i = 0; i < eaiSize(&pItemDef->peRestrictBagIDs); i++)
	{
		if (pItemDef->peRestrictBagIDs[i] == pPetDef->eaEquipSlots[iEquipSlot]->eID)
		{
			return true;
		}
	}
	return false;
}

const char *GetPetEquipmentSlotTypeTranslate(Language lang, SA_PARAM_OP_VALID Item* pPetItem, int iSlot)
{
	SuperCritterPetDef* pDef = scp_GetPetDefFromItem(pPetItem);
	SCPEquipSlotDef* pSlot = pDef ? eaGet(&pDef->eaEquipSlots, iSlot) : NULL;
	if( pSlot && pSlot->peCategories)
	{
		// if there are additional restrictions on the item type, use the first restriction instead.
		// This is so that we can see off-hand sub-types.
		return StaticDefineLangGetTranslatedMessage(lang, ItemCategoryEnum, pSlot->peCategories[0]);
	}
	return StaticDefineLangGetTranslatedMessage(lang, InvBagIDsEnum, pSlot ? pSlot->eID : 0);
}


// returns true if the powerDef has all the categories on g_SCPConfig.piFakeEntStatsPassiveCategories
static bool scp_PassiveHasCorrectCategories(PowerDef *pPowerDef)
{
	FOR_EACH_IN_EARRAY_INT(g_SCPConfig.piFakeEntStatsPassiveCategories, S32, powerCat)
	{
		if (eaiFind(&pPowerDef->piCategories, powerCat) < 0)
		{
			return false;
		}
	}
	FOR_EACH_END

		return true;
}

// gets a list of all the power enhancements that will apply to the power
static void scp_FakeEntProcessPassivesGetEnhancements(	Entity* pPetEnt, 
	Power *pPassivePower, 
	PowerDef *pPassivePowerDef, 
	Power ***peaPowEnhancements)
{
	FOR_EACH_IN_EARRAY(pPetEnt->pChar->ppPowers, Power, pPowEnhancement)
	{
		PowerDef *pEnhancementPowerDef = GET_REF(pPowEnhancement->hDef);
		if (pEnhancementPowerDef && pEnhancementPowerDef->eType == kPowerType_Enhancement && 
			power_EnhancementAttachIsAllowed(PARTITION_CLIENT, pPetEnt->pChar, pEnhancementPowerDef, pPassivePowerDef, false))
		{
			eaPush(peaPowEnhancements, pPowEnhancement);
		}
	}
	FOR_EACH_END
}

// a very contrived application of all the passive powers on the fake entity to generate mods we want to see on stats
// will only apply passives that are character targeted, and the power must have categories that are blessed by the g_SCPConfig
// this feels somewhat wrong being in this file, but it's really only something we need for pet stat showing
static void scp_FakeEntProcessPassives(Entity* pPetEnt)
{
	if (!pPetEnt->pChar)
		return;

	PERFINFO_AUTO_START_FUNC();

	FOR_EACH_IN_EARRAY(pPetEnt->pChar->ppPowers, Power, pPower)
	{
		PowerDef *pPowerDef = GET_REF(pPower->hDef);
		// todo: check the restricted power categories
		if (pPowerDef && pPowerDef->eType == kPowerType_Passive && 
			pPowerDef->eEffectArea == kEffectArea_Character && 
			scp_PassiveHasCorrectCategories(pPowerDef))
		{
			PowerApplication app = {0};

			app.pcharSource = pPetEnt->pChar;
			app.pclass = character_GetClassCurrent(pPetEnt->pChar);

			app.ppow = pPower;
			app.iIdxMulti = pPower->iIdxMultiTable;
			app.fTableScale = pPower->fTableScale;

			app.pdef = pPowerDef;

			app.iLevel = entity_GetCombatLevel(pPetEnt);
			app.bLevelAdjusting = pPetEnt->pChar->bLevelAdjusting;
			app.erModOwner = entGetRef(pPetEnt);
			app.erModSource = entGetRef(pPetEnt);

			combateval_ContextSetupApply(pPetEnt->pChar, pPetEnt->pChar, NULL, &app);

			scp_FakeEntProcessPassivesGetEnhancements(pPetEnt, pPower, pPowerDef, &app.pppowEnhancements);

			character_ApplyModsFromPowerDef(PARTITION_CLIENT, pPetEnt->pChar, &app, 
				0.f, 0.f, kModTarget_Self, false, NULL, NULL, NULL);
		}

	}
	FOR_EACH_END
		PERFINFO_AUTO_STOP();
}

// takes all the strength aspect mods that are on the character and compiles them into the basic attrib struct
// this is totally not correct for powers system mechanics, but we're just using this to display stats
// that way when we call EntGetAttrib() to get the magnitude of an aspect we don't have to recalculate and apply the strength
static void scp_FakeEntCompileStrMods(Entity* pPetEnt)
{
	S32 *piAttribsToCompile = NULL;

	FOR_EACH_IN_EARRAY(pPetEnt->pChar->modArray.ppMods, AttribMod, pAttribMod)
	{
		if (pAttribMod->pDef && IS_STRENGTH_ASPECT(pAttribMod->pDef->offAspect) &&
			IS_NORMAL_ATTRIB(pAttribMod->pDef->offAttrib))
		{
			eaiPushUnique(&piAttribsToCompile, pAttribMod->pDef->offAttrib);
		}
	}
	FOR_EACH_END

		FOR_EACH_IN_EARRAY_INT(piAttribsToCompile, S32, iAttrib)
	{
		F32 fStrAdd = 0.f;
		F32 fStr = character_GetStrengthGeneric(PARTITION_CLIENT, pPetEnt->pChar, iAttrib, &fStrAdd);
		F32 *pF = F32PTR_OF_ATTRIB(pPetEnt->pChar->pattrBasic, iAttrib);
		*pF = (*pF + fStrAdd) * fStr;
	}
	FOR_EACH_END

		eaiDestroy(&piAttribsToCompile);
}

#if defined(GAMESERVER) || defined(GAMECLIENT)
Entity* scp_CreateFakeEntity(Entity *pPlayer, Item* pPetItem, ActiveSuperCritterPet* pActivePet)
{
	Entity* pPetEnt = StructCreateWithComment(parse_Entity, "Fake Entity created by the client to simulate a SuperCritterPet.");
	NOCONST(Entity)* pNCPetEnt = CONTAINER_NOCONST(Entity, pPetEnt);
	SuperCritterPet* pPet = pPetItem && pPetItem->pSpecialProps ? pPetItem->pSpecialProps->pSuperCritterPet : NULL;
	SuperCritterPetDef* pPetDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
	CritterDef* pDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	CharacterClass* pPetClass = pPet ? GET_REF(pPet->hClassDef) : NULL;
	int i = 0;

	if (!pPet || !pPetDef || !pPetClass || !pDef)
		return NULL;
	pNCPetEnt->bFakeEntity = true; // Mark this as a fake entity
	pNCPetEnt->myEntityType = GLOBALTYPE_ENTITYCRITTER;
	pNCPetEnt->pCritter = StructCreateNoConst(parse_Critter);

	pPetEnt->erOwner = pPlayer->myRef;
	pPetEnt->erCreator = pPlayer->myRef;

	SET_HANDLE_FROM_STRING(g_hCritterDefDict, pDef->pchName, pNCPetEnt->pCritter->critterDef);


	pPetEnt->pCritter->pcRank = pDef->pcRank;
	pPetEnt->pCritter->pcSubRank = pDef->pcSubRank;

	if (!pPetClass)
	{
		StructDestroy(parse_Entity, pPetEnt);
		return NULL;
	}

	critter_AddCombat( pPetEnt, pDef, scp_GetPetCombatLevel(pPetItem), 1, pDef->pcSubRank, 0, true, true, pPetClass, false);

	//Add critter inventory
	if(pPetEnt->pChar && IS_HANDLE_ACTIVE(pPetEnt->pChar->hClass))
	{
		CharacterClass *pClass = GET_REF(pPetEnt->pChar->hClass);
		DefaultInventory *pInventory = pClass ? GET_REF(pClass->hInventorySet) : NULL;
		DefaultItemDef **ppitemList = NULL;
		InventoryBag* pPlayerBag = pActivePet ? pActivePet->pEquipment : NULL;
		NOCONST(InventoryBag)* pPetBag = NULL;

		if(pInventory)
			inv_ent_trh_InitAndFixupInventory(ATR_EMPTY_ARGS,pNCPetEnt,pInventory,true,true,NULL);
		else if(eaSize(&pDef->ppCritterItems) > 0)
			ErrorFilenamef(pDef->pchFileName,"%s Critter has items, but no inventory set! No items will be equipped",pDef->pchName);
		for ( i = 0; i < eaSize(&pPetEnt->pInventoryV2->ppInventoryBags); i++)
		{
			if (invbag_flags(pPetEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_EquipBag)
			{
				pPetBag = CONTAINER_NOCONST(InventoryBag, pPetEnt->pInventoryV2->ppInventoryBags[i]);
				break;
			}
		}

		if (pPetBag && pPlayerBag)
		{
			for (i = 0; i < eaSize(&pPlayerBag->ppIndexedInventorySlots); i++)
			{
				if (pPlayerBag->ppIndexedInventorySlots[i]->pItem)
					inv_bag_trh_AddItem(ATR_EMPTY_ARGS, pNCPetEnt, NULL, pPetBag, -1, StructCloneDeConst(parse_Item, pPlayerBag->ppIndexedInventorySlots[i]->pItem), NULL, -1, 0, NULL, NULL, NULL);
			}
		}
		//also give it its own item so gem powers are applied
		inv_bag_trh_AddItem(ATR_EMPTY_ARGS, pNCPetEnt, NULL, pPetBag, -1, StructCloneDeConst(parse_Item, pPetItem), NULL, -1, 0, NULL, NULL, NULL);
		character_DirtyInnateEquip(pPetEnt->pChar);
	}

	// Start up the Character's innate state immediately, without waiting for a combat tick
	if(pPetEnt->pChar)
	{
		Character *pchar = pPetEnt->pChar;

		character_ResetPowersArray(PARTITION_CLIENT, pchar, NULL);

		pchar->bSkipAccrueMods = false;

		character_DirtyInnatePowers(pchar);
		character_DirtyPowerStats(pchar);

		scp_FakeEntProcessPassives(pPetEnt);
		character_FakeEntInitAccrual(pchar);
		character_AccrueMods(PARTITION_CLIENT,pchar,1.0f,NULL);
		scp_FakeEntCompileStrMods(pPetEnt);

		pchar->pattrBasic->fHitPoints = pchar->pattrBasic->fHitPointsMax;

	}
	return pPetEnt;
}
#endif

////////////////// EXPRESSION FUNCTIONS /////////////////////////////
//sometimes we want pets to scale to a player's level based on this formula.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetStartLevelForPlayerLevel);
U32 scp_GetPetStartLevelForPlayerLevel(int playerLevel, SA_PARAM_OP_VALID Item* pPetItem){
	return CLAMP((playerLevel - g_SCPConfig.fLevelScalingStartsAtPlayerLevel) * g_SCPConfig.fLevelsPerPlayerLevel, 1, scp_MaxLevel(pPetItem));
}

// Expression for UI to check if an ent is an SCP.  
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(scp_EntIsSuperCritterPet);
bool scp_exprEntIsSuperCritterPet(SA_PARAM_OP_VALID Entity* pEnt)
{
	return scp_EntIsSuperCritterPet(pEnt);
}

AUTO_EXPR_FUNC(ItemEval);
S32 scp_GetAltCostumeIdxForEnt(SA_PARAM_NN_VALID Entity * pEnt)
{
	Entity * pPlayerEnt = entFromEntityRef(entGetPartitionIdx(pEnt),pEnt->erOwner);
	Item * pPetItem = scp_GetSummonedPetItem(pPlayerEnt);
	if (pPetItem)
	{
		SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
		if (pPet)
		{
			return MIN(scp_GetMaxCostumeIdx(pPetItem),pPet->iCurrentSkin);
		}
	}

	return -1;
}

AUTO_EXPR_FUNC(CEFuncsGeneric);
S32 scp_GetAltCostumeIdx(SA_PARAM_NN_VALID Character * pPetChar)
{
	return scp_GetAltCostumeIdxForEnt(pPetChar->pEntParent);
}

// a special check for if a given entity is a super critter pet
static bool scp_AddPowers_EntIsSuperCritterPet(Entity *pEnt)
{
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYCRITTER && pEnt->erOwner)
	{
		Entity* pOwner = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->erOwner);
		EntitySavedSCPData* pData = NULL;
				
		if (!pOwner)
		{
			return false;
		}
		
		if (scp_GetSummonedPetEntRef(pOwner) == pEnt->myRef)
		{
			return true;
		}

		pData = scp_GetEntSCPDataStruct(pOwner);
		if (!pData)
			return false;
		
		// this is a special case as we are inside the summoning of a SCP, 
		// but we haven't gotten to setting the player's EntitySavedSCPData erSCP
		// if these are true, then we're going to assume this is our SCP
		if (pData->erSCP == 0 && pData->iSummonedSCP >= 0 && pData->bIsSummoningPet)
		{
			return true;
		}
	}

	return false;
}

typedef struct AddSCPPowerCB
{
	int iPartitionIdx;
	EntityRef erEnt;
} AddSCPPowerCB;


static int scp_AddPowerTemporaryCallback(Power *ppow, AddSCPPowerCB *pUserData)
{
	bool bSuccess = false;
	if (ppow && pUserData)
	{
		Entity *pEnt = entFromEntityRef(pUserData->iPartitionIdx, pUserData->erEnt);
		if (pEnt && pEnt->pChar)
		{
			bSuccess = true;
			eaPush(&pEnt->pChar->ppPowersSCP, ppow);
		}
	}

	if(pUserData)
	{
		free(pUserData);
	}

	return bSuccess;
}


// function that is to be called when rebuilding the powers array for an entity,
// this function applies to players and SuperCritterPets.
// it grants powers from the SCPs that are in the player's SuperCritterPet bag
void scp_AddPowersFromActivePets(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt)
{
	Entity *pEntPlayer = NULL;
	Entity *pSuperCritterPet = NULL;
	GameAccountDataExtract* pExtract = NULL;
	InventoryBag* pPetBag = NULL;
	EntitySavedSCPData* pData = NULL;
	S32 i, iCount;
	bool bIsPlayer = false;
	static Power **s_eaOldSCPPowers = NULL;
	static SuperCritterPetActivePowerDef **s_eaActivePowers = NULL;
	static SuperCritterPetActivePowerDef **s_eaPetActivePowers = NULL;
	static Item **s_pSCPItems = NULL;
	

	if (!pEnt->pChar)
		return;
	
	if (ZMTYPE_PVP == zmapInfoGetMapType(NULL))
		return; // until otherwise needed, ignore this when on PVP maps

	if (pEnt->pPlayer)
	{
		EntityRef erSCP;
		pEntPlayer = pEnt;
		bIsPlayer = true;

		erSCP = scp_GetSummonedPetEntRef(pEnt);
		pSuperCritterPet = entFromEntityRef(iPartitionIdx, erSCP);
		
		if (pSuperCritterPet && (!pSuperCritterPet->pChar || pSuperCritterPet->pChar->bResetPowersArray))
			pSuperCritterPet = NULL; // NULL out the pet if we won't need to worry about resetting the powers array

	}
	else if (scp_AddPowers_EntIsSuperCritterPet(pEnt))
	{
		pEntPlayer = entFromEntityRef(iPartitionIdx, pEnt->erOwner);
		bIsPlayer = false;
	}
	
	// make sure the entity we will be checking inventory for is a player
	if (!pEntPlayer || !entCheckFlag(pEntPlayer,ENTITYFLAG_IS_PLAYER))
	{
		return;
	}

	if (!IsServer())
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pEnt->pChar->ppPowersSCP, Power, pPower)
		{
			character_AddPower(iPartitionIdx, pEnt->pChar, pPower, kPowerSource_SuperCritterPet, pExtract);
		}
		FOR_EACH_END

		return;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEntPlayer);
	pPetBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntPlayer), InvBagIDs_SuperCritterPets, pExtract);
	if (!pPetBag)
		return;
			
	eaClear(&s_eaOldSCPPowers);
	eaClear(&s_eaActivePowers);
	eaClear(&s_eaPetActivePowers);
	eaClear(&s_pSCPItems);
	
	pData = scp_GetEntSCPDataStruct(pEntPlayer);
	if (pData && eaSize(&pData->ppSuperCritterPets))
	{
		// got through all the items in the bag, these items should all be SuperCritterPet items
		iCount = invbag_maxslots(pEnt, pPetBag);

		// go through all the SCP items in the bag and generate a unique list of items that we'll look through to add 
		// the active powers
		for (i = 0; i < iCount; i++)
		{
			Item* pItem = inv_bag_GetItem(pPetBag, i);
			SuperCritterPetDef* pSCPDef = scp_GetPetDefFromItem(pItem);

			if (pSCPDef)
			{
				bool bAddItem = true;

				// see if we already have this itemDef, and if we do keep the one with the higher quality
				FOR_EACH_IN_EARRAY(s_pSCPItems, Item, pOtherItem)
				{
					SuperCritterPetDef* pSCPDefOther = scp_GetPetDefFromItem(pOtherItem);
					if (pSCPDef == pSCPDefOther)
					{
						if (item_GetQuality(pItem) > item_GetQuality(pOtherItem))
						{	// the new item is better than the other one, remove it from the list
							eaFindAndRemove(&s_pSCPItems, pOtherItem);
						}
						else
						{
							bAddItem = false;
						}

						break;
					}
				}
				FOR_EACH_END

				if (bAddItem)
				{
					eaPush(&s_pSCPItems, pItem);
				}
			}
		}

		// this is our list of items 
		FOR_EACH_IN_EARRAY_FORWARDS(s_pSCPItems, Item, pItem)
		{
			SuperCritterPetDef* pSCPDef = scp_GetPetDefFromItem(pItem);

			FOR_EACH_IN_EARRAY_FORWARDS(pSCPDef->eaRanks, SuperCritterPetRankDef, pRanks)
			{
				if (item_GetQuality(pItem) == pRanks->eItemQuality)
				{
					// go through the active powers on the SCP def and add the power to the given entity if it applies
					FOR_EACH_IN_EARRAY_FORWARDS(pRanks->eaActivePowers, SuperCritterPetActivePowerDef, pActivePowerDef)
					{
						if ((pActivePowerDef->bAppliesToPlayer && bIsPlayer) ||  
							(pActivePowerDef->bAppliesToSummonedPet && !bIsPlayer))
						{
							eaPush(&s_eaActivePowers, pActivePowerDef);
						}

						// if we are dealing with the player, we need to check if any powers will be applying to the pet
						if (pSuperCritterPet && pActivePowerDef->bAppliesToSummonedPet)
						{
							eaPush(&s_eaPetActivePowers, pActivePowerDef);
						}
					}
					FOR_EACH_END

					break;
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END

		
		if (pSuperCritterPet)
		{
			bool bResetPowersArray = false;
			
			// s_eaPetActivePowers is the list of powers that should be applied to the pet 
			// see if the pet has these, and if we need to update the SCP's powers
			if (eaSize(&pSuperCritterPet->pChar->ppPowersSCP) == eaSize(&s_eaPetActivePowers))
			{	// array sizes are the same, make sure they are th same powers

				FOR_EACH_IN_EARRAY_FORWARDS(s_eaPetActivePowers, SuperCritterPetActivePowerDef, pActivePowerDef)
				{
					PowerDef *pPowerDef = GET_REF(pActivePowerDef->hPowerDef);
					if (pPowerDef)
					{
						bool bFound = false;
						FOR_EACH_IN_EARRAY_FORWARDS(pSuperCritterPet->pChar->ppPowersSCP, Power, pSCPPower)
						{
							if (GET_REF(pSCPPower->hDef) == pPowerDef)
							{	// we found the power
								bFound = true;
								break;
							}
						}
						FOR_EACH_END

						if (!bFound)
						{
							bResetPowersArray = true;
							break;
						}
					}
				}
				FOR_EACH_END
			}
			else
			{	// arrays differ in size, we'll need to reset their powers array
				bResetPowersArray = true;
			}
			

			if (bResetPowersArray)
			{
				pSuperCritterPet->pChar->bResetPowersArray = true;
			}
		}
		
		

		if (eaSize(&pEnt->pChar->ppPowersSCP) > 0 || eaSize(&s_eaActivePowers) > 0)
		{
			// push all the current SCP powers onto a temporary list
			eaPushEArray(&s_eaOldSCPPowers, &pEnt->pChar->ppPowersSCP);
			
			// go through the list of powers we should have and see if we have it or not
			// if we find the power we'll remove from s_eaOldSCPPowers and the powers leftover 
			// in s_eaOldSCPPowers need to be removed and deleted
			FOR_EACH_IN_EARRAY_FORWARDS(s_eaActivePowers, SuperCritterPetActivePowerDef, pActivePowerDef)
			{
				S32 iFoundIdx = -1;
				PowerDef *pPowerDef = GET_REF(pActivePowerDef->hPowerDef);

				if (!pPowerDef)
					continue;

				FOR_EACH_IN_EARRAY_FORWARDS(s_eaOldSCPPowers, Power, pPower)
				{
					if (pPowerDef == GET_REF(pPower->hDef))
					{
						iFoundIdx = FOR_EACH_IDX(-, pPower);
						break;
					}
				}
				FOR_EACH_END

				//
				if (iFoundIdx != -1)
				{	// we have the power already, 
					// remove the power from the array so we know we have it when we check later
					eaRemoveFast(&s_eaOldSCPPowers, iFoundIdx);
				}
				else
				{
#if GAMESERVER
					AddSCPPowerCB *pCBData = calloc(1, sizeof(AddSCPPowerCB));

					pCBData->erEnt = entGetRef(pEnt);
					pCBData->iPartitionIdx = iPartitionIdx;
					character_AddPowerTemporary(pEnt->pChar, pPowerDef, scp_AddPowerTemporaryCallback, pCBData);
#endif
				}

			}
			FOR_EACH_END

			// the remaining Powers in s_eaOldSCPPowers need to be destroyed and removed from the pEnt->pChar->ppPowersSCP
			FOR_EACH_IN_EARRAY(s_eaOldSCPPowers, Power, pPower)
			{
				eaFindAndRemove(&pEnt->pChar->ppPowersSCP, pPower);
				power_Destroy(pPower, pEnt->pChar);
			}
			FOR_EACH_END

			// finally, add all Powers in the ppPowersSCP list
			FOR_EACH_IN_EARRAY_FORWARDS(pEnt->pChar->ppPowersSCP, Power, pPower)
			{
				character_AddPower(iPartitionIdx, pEnt->pChar, pPower, kPowerSource_SuperCritterPet, pExtract);
			}
			FOR_EACH_END

		}
	}


}

// Given the SuperCritterPetDef and the Item of the SCP, fills out a list of SuperCritterPetActivePowerDef that the pet has
S32 scp_GetActivePowerDefs(SuperCritterPetDef *pSCPDef, Item *pItem, SuperCritterPetActivePowerDef ***peaSCPActivePowersOut)
{
	eaClear(peaSCPActivePowersOut);

	FOR_EACH_IN_EARRAY_FORWARDS(pSCPDef->eaRanks, SuperCritterPetRankDef, pRank)
	{
		if (item_GetQuality(pItem) == pRank->eItemQuality)
		{
			eaPushEArray(peaSCPActivePowersOut, &pRank->eaActivePowers);
			return true;			
		}
	}
	FOR_EACH_END
	
	return false;
}


// returns true if equipping the SCP in the given bag will be valid
// the bag InvBagIDs_SuperCritterPets must have unique SCP pets
AUTO_TRANS_HELPER;
bool scp_trh_IsPetUniqueInBag(ATR_ARGS, SuperCritterPetDef* pSCPDef, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	S32 i;

	if(ISNULL(pSCPDef) || ISNULL(pBag))
	{
		return false;
	}

	if(invbag_trh_bagid(pBag) != InvBagIDs_SuperCritterPets)
	{
		// not the active bag
		return true;
	}

	// see if we have the same SCP in this bag
	for(i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); ++i)
	{
		NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[i];
		if(NONNULL(pSlot))
		{
			if(NONNULL(pSlot->pItem))
			{
				// inv_trh_MatchingItemInSlot(ATR_PASS_ARGS, pBag, i, pItemDef->pchName)
				NOCONST(SuperCritterPet)* pPet = SAFE_MEMBER2(pSlot->pItem, pSpecialProps, pSuperCritterPet);
				SuperCritterPetDef* pPetDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
				if (pPetDef == pSCPDef)
				{
					return false;			
				}
			}
		}
	}

	return true;
}

AUTO_TRANS_HELPER;
bool scp_trh_CanMoveSCPCheckSwap(ATR_ARGS, 
									ATH_ARG NOCONST(Entity)* pEnt,	
									SuperCritterPetDef* pSrcSCPDef, 
									SuperCritterPetDef* pDstSCPDef, 
									NOCONST(InventoryBag)* pSrcBag, 
									NOCONST(InventoryBag)* pDstBag)
{
	if (ISNULL(pSrcSCPDef) && ISNULL(pDstSCPDef))
	{
		return true;
	}
	if (NONNULL(pSrcBag) && NONNULL(pDstBag) && pSrcBag->BagID == pDstBag->BagID)
	{
		return true;
	}
	if (NONNULL(pSrcSCPDef) && NONNULL(pDstSCPDef) && pSrcSCPDef == pDstSCPDef)
	{
		return true;
	}
	if (NONNULL(pSrcSCPDef) && !scp_trh_IsPetUniqueInBag(ATR_PASS_ARGS, pSrcSCPDef, pDstBag))
	{
		return false;
	}
	if (NONNULL(pDstSCPDef) && !scp_trh_IsPetUniqueInBag(ATR_PASS_ARGS, pDstSCPDef, pSrcBag))
	{
		return false;
	}
	return true;
}

// Returns true if the pet can be moved into the active SuperCritterPets bag
bool scp_CanBindPet(SA_PARAM_OP_VALID Entity* pEntity, SA_PARAM_OP_VALID Item *pPetItem)
{
	if (!pPetItem)
		return false;

	if (pPetItem->flags & kItemFlag_Bound)
		return true;

	if (pEntity)
	{
		SuperCritterPet* pPet = pPetItem ? scp_GetPetFromItem(pPetItem) : NULL;
		SuperCritterPetDef* pSCPDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), InvBagIDs_SuperCritterPets, pExtract);

		return scp_CanMoveSCPCheckSwap(pEntity, pSCPDef, NULL, NULL, pBag);
	}

	return false;
}

// returns true if the source item is a SCP and can be moved to the destination
bool scp_CanMovePet(SA_PARAM_OP_VALID Entity* pPlayerEnt, S32 eSrcBag, S32 iSrcSlot, S32 eDstBag, S32 iDstSlot)
{
	InventoryBag *pSrcBag = NULL, *pDstBag = NULL;
	Item *pSrcItem = NULL, *pDstItem = NULL;
	SuperCritterPetDef *pSrcSCPDef = NULL, *pDstSCPDef = NULL;
	SuperCritterPet *pSrcPet = NULL;
	GameAccountDataExtract* pExtract = NULL;

	if (!pPlayerEnt)
		return false;
	
	pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	if (!pExtract)
		return false;
	
	pSrcBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pPlayerEnt), eSrcBag, pExtract));
	pDstBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pPlayerEnt), eDstBag, pExtract));
	
	if (!pSrcBag || !pDstBag)
		return false;

	pSrcItem = inv_bag_GetItem(pSrcBag, iSrcSlot);
	pSrcSCPDef = scp_GetPetDefFromItem(pSrcItem);
	if (!pSrcSCPDef)
		return false;

	if (iDstSlot == -1)
	{
		iDstSlot = inv_bag_GetFirstEmptySlot(pPlayerEnt, pDstBag);
	}

	pDstItem = inv_bag_GetItem(pDstBag, iDstSlot);
	pDstSCPDef = scp_GetPetDefFromItem(pDstItem);
	
	
	return scp_CanMoveSCPCheckSwap(pPlayerEnt, pSrcSCPDef, pDstSCPDef, pSrcBag, pDstBag);
}

#include "AutoGen/supercritterpet_h_ast.c"
