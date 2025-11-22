#include "CombatPowerStateSwitching.h"
#include "ResourceManager.h"
#include "Character.h"
#include "CombatEval.h"
#include "CombatConfig.h"
#include "Entity.h"
#include "PowerAnimFX.h"
#include "MemoryPool.h"
#include "StringCache.h"
#include "GameAccountDataCommon.h"
#include "EntityIterator.h"
#include "file.h"
#include "PowerSlots.h"

#include "CombatPowerStateSwitching_h_ast.h"

#if GAMECLIENT || GAMESERVER
	#include "Character_combat.h"
	#include "PowersMovement.h"
	#include "CharacterAttribsMinimal_h_ast.h"
	#include "EntityMovementDefault.h"
#endif
#if GAMECLIENT
	#include "gclCombatPowerStateSwitching.h"
	#include "gclCursorModePowerTargeting.h"
	#include "gclCursorModePowerLocationTargeting.h"
	#include "gclCursorMode.h"
#endif
#if GAMESERVER
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif

DictionaryHandle g_hCombatPowerStateSwitchingPowerDefDict;

#define POWER_MODE_LINKING_ACTID				0x4000FEE5

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void CombatPowerStateSwitching_Generate(CombatPowerStateSwitchingDef *pDef);

// -------------------------------------------------------------------------------------------------------------------
static void _RebuildPowerArraysForAffectedEnts(CombatPowerStateSwitchingDef *pDef)
{
	Entity *ent;
	EntityIterator *iter;

	if (!pDef)
		return;

	iter = entGetIteratorAllTypesAllPartitions(ENTITYFLAG_IS_PLAYER, 0);
	while(ent = EntityIteratorGetNext(iter))
	{
		if(ent->pChar && ent->pChar->pCombatPowerStateInfo &&
			GET_REF(ent->pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef) == pDef)
		{
			ent->pChar->bResetPowersArray = true;
		}
	}
	EntityIteratorRelease(iter);
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_FIXUPFUNC;
TextParserResult fixupCombatPowerStateSwitchingDef(CombatPowerStateSwitchingDef* pDef, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_RELOAD:
		{
			if (IsServer() && isDevelopmentMode())
			{
				_RebuildPowerArraysForAffectedEnts(pDef);
			}
		}
	}

	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
static int CombatPowerStateSwitchingDefValidateCB(	enumResourceValidateType eType, 
													const char *pDictName, 
													const char *pResourceName, 
													CombatPowerStateSwitchingDef *pDef, 
													U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			CombatPowerStateSwitching_Generate(pDef);

			if (IsClient())
				return VALIDATE_NOT_HANDLED;


			// validate that there is at least one CombatPowerStateDef
			if (pDef->iSpecialPowerSwitchedSlot == -1 && eaSize(&pDef->eaStates) == 0)
			{
				ErrorFilenamef(pDef->pchFilename, "No Modes defined.");
				return VALIDATE_HANDLED;
			}

			// validate there isn't a duplicate CombatPowerStateDef
			FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pMode)
			{
				if (!pMode->pchName || !pMode->pchName[0])
				{
					ErrorFilenamef(pDef->pchFilename, "Missing Mode Name.");
					continue;
				}

				FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pModeCompare)
				{
					if (pModeCompare != pMode && pModeCompare->pchName == pMode->pchName)
					{
						ErrorFilenamef(pDef->pchFilename, "Duplicate Mode names (%s). This is not allowed.", pMode->pchName);
					}
				}	
				FOR_EACH_END
			}
			FOR_EACH_END

			// go through all the power sets 
			FOR_EACH_IN_EARRAY(pDef->eaPowerSet, CombatPowerStatePowerSet, pPowerSet)
			{
				if (!GET_REF(pPowerSet->hBasePowerDef))
				{
					ErrorFilenamef(pDef->pchFilename, "No BasePowerDef defined in one of the sets.");
					continue;
				}

				// validate that there isn't a duplicate hBasePowerDef
				FOR_EACH_IN_EARRAY(pDef->eaPowerSet, CombatPowerStatePowerSet, pPowerSetCompare)
				{
					if (pPowerSetCompare != pPowerSet && 
						GET_REF(pPowerSetCompare->hBasePowerDef) == GET_REF(pPowerSet->hBasePowerDef))
					{
						ErrorFilenamef(pDef->pchFilename, "Duplicate PowerSet BasePowers (%s). This is not allowed.", 
							REF_HANDLE_GET_STRING(pPowerSetCompare->hBasePowerDef));
					}
				}
				FOR_EACH_END

				// validate that there is a mode defined for each power
				FOR_EACH_IN_EARRAY(pPowerSet->eaPowers, CombatPowerStatePower, pPowerLink)
				{
					if (!GET_REF(pPowerLink->hPowerDef))
					{
						ErrorFilenamef(pDef->pchFilename, "No Power defined for the PowerSet of (%s)", 
										REF_HANDLE_GET_STRING(pPowerSet->hBasePowerDef));
						continue;
					}

					if (pDef->iSpecialPowerSwitchedSlot >= 0)
						continue;

					if (!pPowerLink->pchState || !pPowerLink->pchState[0])
					{
						ErrorFilenamef(pDef->pchFilename, "No Mode defined for a power in set of (%s)", 
										REF_HANDLE_GET_STRING(pPowerSet->hBasePowerDef));
					}
					else if (!CombatPowerStateSwitching_GetStateByName(pPowerLink->pchState, pDef))
					{
						ErrorFilenamef(pDef->pchFilename, "Mode not found for set of (%s)", 
										REF_HANDLE_GET_STRING(pPowerSet->hBasePowerDef));
					}
				}
				FOR_EACH_END
			}
			FOR_EACH_END
		} 
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

// ------------------------------------------------------------------------------------------------------------------------------
AUTO_RUN;
int CombatPowerStateSwitching_AutoRunInit(void)
{
	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType()) 
		return 0;

	g_hCombatPowerStateSwitchingPowerDefDict = RefSystem_RegisterSelfDefiningDictionary("CombatPowerStateSwitchingDef",
																				false, 
																				parse_CombatPowerStateSwitchingDef, 
																				true, 
																				true, 
																				NULL);

	resDictManageValidation(g_hCombatPowerStateSwitchingPowerDefDict, CombatPowerStateSwitchingDefValidateCB);
	resDictSetDisplayName(g_hCombatPowerStateSwitchingPowerDefDict, "CombatPowerStateSwitchingDef", "CombatPowerStateSwitchingDefs", RESCATEGORY_DESIGN);

	resDictMaintainInfoIndex(g_hCombatPowerStateSwitchingPowerDefDict, ".Name", NULL, NULL, NULL, NULL);

	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
static void CombatPowerStateSwitching_Generate(CombatPowerStateSwitchingDef *pDef)
{
	FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pStateDef)
	{
		if (pStateDef->pExprAttribDecayPerTick)
		{
			combateval_Generate(pStateDef->pExprAttribDecayPerTick, kCombatEvalContext_Activate);
		}
		if (pStateDef->pExprPerTickExitMode)
		{
			combateval_Generate(pStateDef->pExprPerTickExitMode, kCombatEvalContext_Activate);
		}
		
	}
	FOR_EACH_END
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(AS_CombatPowerStateSwitching) ASTRT_DEPS(Powers);
void CombatPowerStateSwitching_LoadDefs(void)
{
	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType()) 
		return;

	resLoadResourcesFromDisk(g_hCombatPowerStateSwitchingPowerDefDict, "defs/powers", 
								".powerstates", "CombatPowerStateSwitching.bin",  
								PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
}


// -------------------------------------------------------------------------------------------------------------------
static U32 _GetCurrentAnimFxActID(CombatPowerStateSwitchingInfo *pInfo)
{
	return POWER_MODE_LINKING_ACTID + pInfo->iCurActIdOffset;
}

// --------------------------------------------------------------------------------------------------------------------
static CombatPowerStateDef* CombatPowerStateSwitching_GetDefaultState(CombatPowerStateSwitchingDef *pDef)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaStates, CombatPowerStateDef, pStateDef)
	{
		if (pStateDef->bDefaultState)
			return pStateDef;
	}
	FOR_EACH_END

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
CombatPowerStateSwitchingDef* CombatPowerStateSwitching_GetDefByName(const char *pchDefName)
{
	CombatPowerStateSwitchingDef *pDef = NULL;
	if (pchDefName)
	{
		pDef = RefSystem_ReferentFromString(g_hCombatPowerStateSwitchingPowerDefDict, pchDefName);
	}
	return pDef;
}

// --------------------------------------------------------------------------------------------------------------------
// Returns the CombatPowerStatePowerSet that corresponds to the given PowerDef
CombatPowerStatePowerSet* CombatPowerStateSwitching_FindPowerStateSetDef(	SA_PARAM_NN_VALID CombatPowerStateSwitchingDef *pPowerStateDef, 
																			SA_PARAM_NN_VALID PowerDef *pPowerDef)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pPowerStateDef->eaPowerSet, CombatPowerStatePowerSet, pSet)
	{
		if (pPowerDef == GET_REF(pSet->hBasePowerDef))
		{
			return pSet;
		}
	}
	FOR_EACH_END

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_InitCharacter(Character *pChar, const char *pszDef)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (pszDef)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pInfo = NULL;

		PM_CREATE_SAFE(pChar);

		if (!pChar->pCombatPowerStateInfo)
		{
			pInfo = StructAlloc(parse_CombatPowerStateSwitchingInfo);
			REF_HANDLE_SET_FROM_STRING(g_hCombatPowerStateSwitchingPowerDefDict, pszDef, pInfo->hCombatPowerStateSwitchingDef);
			pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
			if (pDef)
			{
				CombatPowerStateDef *pStartStateDef = CombatPowerStateSwitching_GetDefaultState(pDef);

				pChar->pCombatPowerStateInfo = pInfo;
				pInfo->iSpecialPowerSwitchedSlot = pDef->iSpecialPowerSwitchedSlot;
				// set the default state, if any
				if (pStartStateDef)
					CombatPowerStateSwitching_EnterState(pChar, pDef, pInfo, pStartStateDef, 0);
			}
			else
			{
				Errorf("CombatPowerStateSwitching_InitCharacter: Could not find def %s.", pszDef);
				StructDestroy(parse_CombatPowerStateSwitchingInfo, pInfo);
				return;
			}
		}
		else
		{
			pInfo = pChar->pCombatPowerStateInfo;
			pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
		}
		
		if (pInfo)
		{
			CombatPowerStateDef *pStartStateDef = CombatPowerStateSwitching_GetDefaultState(pDef);
			if (pStartStateDef)
				CombatPowerStateSwitching_EnterState(pChar, pDef, pInfo, pStartStateDef, 0);
		}
	}
#endif
}

// --------------------------------------------------------------------------------------------------------------------
static bool CombatPowerStateSwitching_CanQueue(	CombatPowerStateSwitchingDef *pDef, PowerActivation *pPowActCurrent, 
												PowerDef *pDefAct)
{
	switch (pDef->eQueueType)
	{
		xcase EPowerActivationQueueType_ALWAYS_ALLOW:
			return true;

		xcase EPowerActivationQueueType_ALWAYS_QUEUE:
		{
			if (! pDefAct->bAlwaysQueue)
			{	// the power is not flagged to always queue, so we are allowed to cancel it and start the state switch
				return true;
			}

			if (pPowActCurrent->eActivationStage != kPowerActivationStage_PostMaintain)
			{
				return (pDefAct->fTimeAllowQueue >= pDefAct->fTimeActivate - pPowActCurrent->fTimeActivating);
			}
			else
			{
				return pDefAct->fTimeAllowQueue >= pPowActCurrent->fStageTimer;
			}

		} return false;
	}
	
	return false;
}

// --------------------------------------------------------------------------------------------------------------------
// for checking if a state is valid for swapping, 
// returns true if in the activating state, but if the server allow some leniency 
static bool _CheckActivatingStateForStateSwap(CombatPowerStateSwitchingInfo *pInfo)
{
	if (pInfo->eState == EPowerStateActivationState_ACTIVATING)
	{
#if GAMESERVER
		if (pInfo->fActivationTimer < 0.15f)
			return false; // 
#endif 
		return true;
	}
	
	return false;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_CanStateSwitch(	Character *pChar, 
												CombatPowerStateSwitchingInfo *pInfo,
												CombatPowerStateSwitchingDef *pDef,
												ECombatPowerStateModeFailReason *peFailOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else	
	if (pDef->iCombatLevelLockout && pDef->iCombatLevelLockout > pChar->iLevelCombat)
		return false;

	if (_CheckActivatingStateForStateSwap(pInfo))
	{
		if (peFailOut) 
			*peFailOut = ECombatPowerStateModeFailReason_ACTIVATING;
		return false;
	}

	if (pDef->bDisableActivationWhileKnocked)
	{
		if (pmKnockIsActive(pChar->pEntParent))
		{
			if (peFailOut) 
				*peFailOut = ECombatPowerStateModeFailReason_KNOCKED;
			return false;
		}
	}

	if (pDef->eQueueType != EPowerActivationQueueType_ALWAYS_ALLOW)
	{
		if (pChar->pPowActCurrent)
		{
			PowerDef *pPowerDef = GET_REF(pChar->pPowActCurrent->hdef);
			if (!pPowerDef || !CombatPowerStateSwitching_CanQueue(pDef, pChar->pPowActCurrent, pPowerDef))
				return false;
		}
	}

	if (pDef->eaiDisableActivationAttribs)
	{
		S32 i;
		for (i = eaiSize(&pDef->eaiDisableActivationAttribs) - 1; i >= 0; --i)
		{
			S32 attrib = pDef->eaiDisableActivationAttribs[i];
			if (attrib >= 0 && IS_NORMAL_ATTRIB(attrib))
			{
				F32 fAttrib = *F32PTR_OF_ATTRIB(pChar->pattrBasic, attrib);
				if (fAttrib > 0)
				{
					if (peFailOut) 
						*peFailOut = ECombatPowerStateModeFailReason_DISABLEDATTRIB;
					return false;
				}
			}
		}
	}
#endif
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_CanEnterState(Character *pChar, CombatPowerStateSwitchingInfo *pInfo,
											CombatPowerStateDef *pMode, ECombatPowerStateModeFailReason *peReason)
{
	if (pChar->pNearDeath || !entIsAlive(pChar->pEntParent))
	{
		if (peReason) 
			*peReason = ECombatPowerStateModeFailReason_NEARDEATHORDEAD;
		return false;
	}
		
	if (_CheckActivatingStateForStateSwap(pInfo))
	{
		if (peReason) 
			*peReason = ECombatPowerStateModeFailReason_ACTIVATING;
		return false;
	}

	if (pMode->eRequiredAttrib)
	{
		F32 fAttrib = *F32PTR_OF_ATTRIB(pChar->pattrBasic, pMode->eRequiredAttrib);
		if (fAttrib < pMode->fRequiredAttribAmountToEnter)
		{
			if (peReason) 
				*peReason = ECombatPowerStateModeFailReason_COST;
			return false;
		}
	}

	if (pMode->iDisallowedPowerMode >= 0)
	{
		if (character_HasMode(pChar, pMode->iDisallowedPowerMode))
		{
			if (peReason) 
				*peReason = ECombatPowerStateModeFailReason_DISALLOWEDMODE;
			return false;
		}
	}

	if (peReason) 
		*peReason = ECombatPowerStateModeFailReason_NONE;
	
	return true;
}


// -------------------------------------------------------------------------------------------------------------------
CombatPowerStateDef* CombatPowerStateSwitching_GetStateByName(const char *pchState, CombatPowerStateSwitchingDef *pDef)
{
	if (pchState)
	{
		FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pMode)
		{
			if (pMode->pchName == pchState)
				return pMode;
		}
		FOR_EACH_END
	}
	
	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_DoesPowerMatchMode(Power *pPower, const char *pchModeName)
{
	return pPower && pPower->pchCombatPowersState && pPower->pchCombatPowersState == pchModeName;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_IsPowerIDSlottedInSet(	Character *pChar, 
														CombatPowerStateSwitchingDef *pDef, 
														U32 powerID, 
														const char *pchModeName)
{
	S32 iSlot = character_PowerIDSlot(pChar, powerID);

	if (iSlot >= 0)
	{
		FOR_EACH_IN_EARRAY(pDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
		{
			if (pSet->iBasePowerSlot == iSlot)
			{
				return true;
			}

			FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlot)
			{
				if (pSlot->pchState == pchModeName && pSlot->iPowerSlot == iSlot)
				{
					return true;
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_IsPowerSlottedInSet(	Character *pChar, 
													CombatPowerStateSwitchingDef *pDef, 
													Power *pPower, 
													const char *pchModeName)
{
	if (pPower)
		CombatPowerStateSwitching_IsPowerIDSlottedInSet(pChar, pDef, pPower->uiID, pchModeName);

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
// pchModeName assumed to be a pooled string
static S32 _ShouldCancelActivationDueToStateExit(	Character *pChar, 
													CombatPowerStateSwitchingDef *pDef,
													PowerActivation *pAct, 
													const char *pchModeName)
{
	Power *pPower = character_ActGetPower(pChar, pAct);
	if (pPower && CombatPowerStateSwitching_DoesPowerMatchMode(pPower, pchModeName))
	{
		return true;
	}

	if (pDef->eaPowerSlotSet)
	{
		pPower = character_FindPowerByID(pChar, pAct->ref.uiID);
		return CombatPowerStateSwitching_IsPowerSlottedInSet(pChar, pDef, pPower, pchModeName);
	}

	return false;
}



// -------------------------------------------------------------------------------------------------------------------
static S32 _DoesPowerContainSwitchState(Power *pPower, const char *pchModeName)
{
	if (pPower->ppSubCombatStatePowers)
	{
		FOR_EACH_IN_EARRAY(pPower->ppSubCombatStatePowers, Power, pLinkedPower)
		{
			if (pLinkedPower->pchCombatPowersState == pchModeName)
				return true;
		}
		FOR_EACH_END
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
// pchModeName assumed to be a pooled string
static S32 _ShouldCancelActivationDueToStateEnter(Character *pChar, CombatPowerStateSwitchingDef *pDef, 
													PowerActivation *pAct, const char *pchModeName)
{
	switch (pDef->eQueueType)
	{
		xcase EPowerActivationQueueType_ALWAYS_ALLOW:
		{
			Power *pPower = NULL;

			if (pChar->pPowActCurrent == pAct)
				return false;

			pPower = character_ActGetPower(pChar, pAct);
			if (pPower)
			{
				return _DoesPowerContainSwitchState(pPower, pchModeName);
			}
		} 

		xcase EPowerActivationQueueType_ALWAYS_QUEUE:
		{
			if (pChar->pPowActCurrent != pAct)
				return true;
			else
			{
				PowerDef *pPowerDef = GET_REF(pAct->hdef);
				if (pPowerDef && !pPowerDef->bAlwaysQueue)
					return true;
			}
		}
	}

	

	return false;
}

#if GAMECLIENT
// -------------------------------------------------------------------------------------------------------------------
static S32 _ShouldCancelCursorPowerTargetingDueToStateEnter(Character *pChar, const char *pchModeName)
{
	Power *pPower = gclCursorPowerTargeting_GetCurrentPower();
	if (pPower)
	{
		return _DoesPowerContainSwitchState(pPower, pchModeName);
	}

	pPower = gclCursorPowerLocationTargeting_GetCurrentPower();
	if (pPower)
	{
		return _DoesPowerContainSwitchState(pPower, pchModeName);
	}

	return false;
}
#endif


// --------------------------------------------------------------------------------------------------------------------
// utility function to activate the power
static void _ApplyPower(Character *pChar, CombatPowerStateDef *pModeDef)
{
#if GAMESERVER
	PowerDef *pPowerDef = GET_REF(pModeDef->hApplyPowerDef);
	if(pPowerDef)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pChar->pEntParent);
		ApplyUnownedPowerDefParams applyParams = {0};

		applyParams.erTarget = entGetRef(pChar->pEntParent);
		applyParams.pcharSourceTargetType = pChar;
		applyParams.pclass = character_GetClassCurrent(pChar);
		applyParams.iLevel = entity_GetCombatLevel(pChar->pEntParent);
		applyParams.fTableScale = 1.f;
		applyParams.erModOwner = applyParams.erTarget;
		applyParams.pExtract = pExtract;
		character_ApplyUnownedPowerDef(entGetPartitionIdx(pChar->pEntParent), pChar, pPowerDef, &applyParams);
	}
#endif
}

// -------------------------------------------------------------------------------------------------------------------
// attempts to cancel all attribMods on the character that were applied from the hActivatePowerDef
static void _CancelPower(Character *pChar, CombatPowerStateDef *pModeDef)
{
#if GAMESERVER
	PowerDef *pPowerDef = GET_REF(pModeDef->hApplyPowerDef);
	if(pPowerDef)
	{
		character_CancelModsFromDef(pChar, pPowerDef, entGetRef(pChar->pEntParent), 0, false, true);
	}
#endif
}



// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_ExitState(	Character *pChar, 
											CombatPowerStateSwitchingDef *pDef, 
											CombatPowerStateSwitchingInfo *pInfo, 
											CombatPowerStateDef *pModeDef, 
											U32 uiEndTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else


	{	
		int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
		const char *pchModeName = pModeDef ? pModeDef->pchName : NULL;


		if (pChar->pPowActOverflow && _ShouldCancelActivationDueToStateExit(pChar, pDef, pChar->pPowActOverflow, pchModeName))
		{
			character_ActOverflowCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->pPowActQueued && _ShouldCancelActivationDueToStateExit(pChar, pDef, pChar->pPowActQueued, pchModeName)
			&& pChar->pPowActCurrent)
		{
			character_ActQueuedCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->eChargeMode == kChargeMode_CurrentMaintain && pChar->pPowActCurrent && 
			_ShouldCancelActivationDueToStateExit(pChar, pDef, pChar->pPowActCurrent, pchModeName))
		{
			U8 uchActId = pChar->pPowActCurrent->uchID;
			ChargeMode eMode = pChar->eChargeMode;
			
			character_ActDeactivate(iPartitionIdx, pChar, &uchActId, &eMode, 
									uiEndTime, uiEndTime, false);
		}

#if GAMECLIENT
		gclCombatpowerStateSwitching_OnExit(pChar, pDef, pInfo, pModeDef);
#endif

	}
	pInfo->pchLastState = pModeDef ? pModeDef->pchName : NULL;
	
	if (IsServer())
	{
		pInfo->iAcknowledgedActID = -1;
		entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
	}

	if (!pModeDef)
	{
		return; // nothing to do
	}
	
#if GAMESERVER
	_CancelPower(pChar, pModeDef);
#endif

	// schedule the animFX to be stopped
	{
		U32 uiAnimFxID = _GetCurrentAnimFxActID(pInfo);
		EntityRef erSource = entGetRef(pChar->pEntParent);

		if (pModeDef->ppchStickyStanceWords)
		{
			pmBitsStop(pChar->pPowersMovement, uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, uiEndTime, false);
		}

		if (pModeDef->ppchStickyFX)
		{
			pmFxStop(pChar->pPowersMovement, uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, erSource, uiEndTime, NULL);
		}
		
	
		if (pModeDef->ppchKeyword)
		{
			pmReleaseAnim(pChar->pPowersMovement, uiEndTime, uiAnimFxID, __FUNCTION__);
		}

		if (pModeDef->bEnableStrafingOverride)
		{
			mrSurfaceSetStrafingOverride(pChar->pEntParent->mm.mrSurface, false, false, uiEndTime);
		}
	}

	pInfo->pchCurrentState = NULL;

#endif
}

// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_EnterState(	Character *pChar, 
											CombatPowerStateSwitchingDef *pDef,
											CombatPowerStateSwitchingInfo *pInfo, 
											CombatPowerStateDef *pModeDef, 
											U32 uiStartTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	pInfo->pchCurrentState = pModeDef ? pModeDef->pchName : NULL;
		
#if GAMECLIENT
	gclCombatpowerStateSwitching_OnEnter(pChar, pInfo);
#endif	
	if (!pModeDef)
		return; // nothing to do
	
	{	
		int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);

		if (pChar->pPowActOverflow && _ShouldCancelActivationDueToStateEnter(pChar, pDef, pChar->pPowActOverflow, pModeDef->pchName))
		{
			character_ActOverflowCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->pPowActQueued && _ShouldCancelActivationDueToStateEnter(pChar, pDef, pChar->pPowActQueued, pModeDef->pchName))
		{
			character_ActQueuedCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->pPowActCurrent)
		{
			if (pChar->eChargeMode == kChargeMode_CurrentMaintain)
			{
				if (_ShouldCancelActivationDueToStateEnter(pChar, pDef, pChar->pPowActCurrent, pModeDef->pchName))
				{
					U8 uchActId = pChar->pPowActCurrent->uchID;
					ChargeMode eMode = pChar->eChargeMode;
					character_ActDeactivate(iPartitionIdx, pChar, &uchActId, &eMode, uiStartTime, uiStartTime, false);
				}
			}
			else if (_ShouldCancelActivationDueToStateEnter(pChar, pDef, pChar->pPowActCurrent, pModeDef->pchName))
			{
				character_ActCurrentCancelReason(iPartitionIdx, pChar, 
													g_CombatConfig.alwaysQueue.bCurrentPowerForceCancel,
													g_CombatConfig.alwaysQueue.bCurrentPowerRefundCost,
													g_CombatConfig.alwaysQueue.bCurrentPowerRechargePower, 
													kAttribType_Null);
			}
		}
		

#if GAMECLIENT
		if (_ShouldCancelCursorPowerTargetingDueToStateEnter(pChar, pModeDef->pchName))
		{
			gclCursorMode_ChangeToDefault();
		}
#endif
	}


	if (IsServer())
	{
		pInfo->iAcknowledgedActID = pInfo->iCurActIdOffset;
		entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
		_ApplyPower(pChar, pModeDef);
	}
		
	// schedule the animFX
	{
		U32 uiAnimFxID = _GetCurrentAnimFxActID(pInfo);
		EntityRef erSource = entGetRef(pChar->pEntParent);
		
		if (pModeDef->ppchStickyStanceWords)
		{
			U32 uiStickyStanceStartTime = pmTimestampFrom(uiStartTime, pDef->fActivateTime);

			pmBitsStartSticky(	pChar->pPowersMovement, uiAnimFxID, 0,
								kPowerAnimFXType_CombatPowerStateSwitching,
								erSource,
								uiStickyStanceStartTime, pModeDef->ppchStickyStanceWords,
								false, false, false);
		}

		if (pModeDef->ppchStickyFX)
		{
			pmFxStart(	pChar->pPowersMovement,
						uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, erSource, uiStartTime,
						pModeDef->ppchStickyFX, 
						NULL, 0.f, 0.f, 0.f, 0.f, NULL, NULL, NULL, 0, 0);
		}

		if (pModeDef->ppchKeyword)
		{
			pmBitsStartFlash(	pChar->pPowersMovement, uiAnimFxID, 1,
								kPowerAnimFXType_CombatPowerStateSwitching, erSource,
								uiStartTime, pModeDef->ppchKeyword, 
								false, false, false, true, false, false, true, false);
		}

		if (pModeDef->ppchFX)
		{
			pmFxStart(	pChar->pPowersMovement,
						uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, erSource, uiStartTime,
						pModeDef->ppchFX, 
						NULL, 0.f, 0.f, 0.f, 0.f, NULL, NULL, NULL, 
						EPMFXStartFlags_FLASH, 0);
		}

		if (pModeDef->bEnableStrafingOverride)
		{
			mrSurfaceSetStrafingOverride(pChar->pEntParent->mm.mrSurface, pModeDef->bIsStrafing, true, uiStartTime);
		}

		if (uiStartTime != 0 && pDef->fActivateTime > 0)
		{
			pInfo->fActivationTimer = pDef->fActivateTime;
			pInfo->eState = EPowerStateActivationState_ACTIVATING;
			
			character_SetSleep(pChar, pDef->fActivateTime - gConf.combatUpdateTimer);

			if (pDef->fActivateSpeedScale != 1.f)
			{
				U32 uiStopTime = pmTimestampFrom(uiStartTime, pDef->fActivateTime);

				if (pDef->fActivateSpeedScale == 0.f)
				{
					pmIgnoreStart(pChar, pChar->pPowersMovement, uiAnimFxID, 
									kPowerAnimFXType_CombatPowerStateSwitching, uiStartTime, NULL);
					pmIgnoreStop(pChar, pChar->pPowersMovement, uiAnimFxID, 
									kPowerAnimFXType_CombatPowerStateSwitching, uiStopTime);
				}
				else
				{
					mrSurfaceSpeedScaleStart(	pChar->pEntParent->mm.mrSurface, uiAnimFxID, 
												pDef->fActivateSpeedScale, uiStartTime);

					mrSurfaceSpeedPenaltyStop(pChar->pEntParent->mm.mrSurface, uiAnimFxID, uiStopTime);
				}
			}
			
		}
		else
		{
			pInfo->eState = EPowerStateActivationState_FINAL;
		}
	}

	
	

#endif
}

// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_Update(Character *pChar, F32 fRate)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else

	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pInfo = NULL;
		CombatPowerStateDef *pMode = NULL;

		pInfo = pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;
		
		if (pInfo->fCooldown > 0)
		{
			pInfo->fCooldown -= fRate;
			entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
		}
		
		switch (pInfo->eState)
		{
			xcase EPowerStateActivationState_ACTIVATING:
			{
				pInfo->fActivationTimer -= fRate;
				if (pInfo->fActivationTimer > 0)
				{
					character_SetSleep(pChar, pInfo->fActivationTimer);
				}
				else
				{
					pInfo->fActivationTimer = 0;
					pInfo->eState = EPowerStateActivationState_FINAL;
				}
			}

			xcase EPowerStateActivationState_FINAL:
			{

			}
		}

		pMode = CombatPowerStateSwitching_GetStateByName(pInfo->pchCurrentState, pDef);
		if (pMode && pInfo->eState == EPowerStateActivationState_FINAL)
		{
			bool bExit = false;
			bool bContextSetup = false;

			if (pChar->pNearDeath || !entIsAlive(pChar->pEntParent))
				bExit = true;

			if (!bExit && pMode->eRequiredAttrib)
			{
				F32 *pfAttrib = F32PTR_OF_ATTRIB(pChar->pattrBasic, pMode->eRequiredAttrib);
				bool bProcessAttribDecay = true;

				if (!IsServer())
				{
					if (pInfo->iAcknowledgedActID != pInfo->iCurActIdOffset)
						bProcessAttribDecay = false;
				}

				// if we have decay, handle it and make sure we at least wake up when it would be empty
				if (pMode->pExprAttribDecayPerTick != NULL && bProcessAttribDecay)
				{
					F32 fNumTicks = fRate/gConf.combatUpdateTimer;
					F32 fTimeToExpire;
					F32 fAttribDecayPerTick = 0.f;

					bContextSetup = true;
					combateval_ContextReset(kCombatEvalContext_Activate);
					combateval_ContextSetupActivate(pChar, NULL, NULL, kCombatEvalPrediction_True);
					fAttribDecayPerTick = combateval_EvalNew(	entGetPartitionIdx(pChar->pEntParent), 
																pMode->pExprAttribDecayPerTick, 
																kCombatEvalContext_Activate, NULL);

					if (fAttribDecayPerTick)
					{
						(*pfAttrib) -= fNumTicks * fAttribDecayPerTick;
						entity_SetDirtyBit(pChar->pEntParent, parse_CharacterAttribs, pChar->pattrBasic, 0);
						entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, 0);

						// make sure we wake up when the attrib amount would expire
						fTimeToExpire = (*pfAttrib) / (1.f / gConf.combatUpdateTimer * fAttribDecayPerTick);
						character_SetSleep(pChar, fTimeToExpire);
					}
				}

				if (*pfAttrib <= 0.f)
				{	
					bExit = true;
				}
			}

			if (!bExit && pMode->pExprPerTickExitMode)
			{
				F32 fRet;
				if (!bContextSetup)
				{
					combateval_ContextReset(kCombatEvalContext_Activate);
					combateval_ContextSetupActivate(pChar, NULL, NULL, kCombatEvalPrediction_True);
				}
				fRet = combateval_EvalNew(	entGetPartitionIdx(pChar->pEntParent), 
											pMode->pExprPerTickExitMode, 
											kCombatEvalContext_Activate, NULL);
				bExit = fRet != 0.f;
			}

			

			if (bExit)
			{
				// exit the mode
				CombatPowerStateSwitching_ExitState(pChar, pDef, pInfo, pMode, pmTimestamp(0));
#if GAMESERVER
				if (entIsPlayer(pChar->pEntParent))
				{
					ClientCmd_gclCombatPowerStateSwitching_ExitState(pChar->pEntParent, pMode->pchName, NULL);
				}
#endif
				// find out what state we need to go to, if any
			}
			
		}
	}
#endif
}


// -------------------------------------------------------------------------------------------------------------------
static CombatPowerStatePowerSet* CombatPowerStateSwitching_GetPowerSetForPowerDef(	Character *pChar, 
																				CombatPowerStateSwitchingDef *pDef, 
																				PowerDef *pPowerDef)
{
	// loop through the def and find the power set
	// if these sets get big or are called alot, we might want to make a lookup 
	FOR_EACH_IN_EARRAY(pDef->eaPowerSet, CombatPowerStatePowerSet, pSet)
	{
		if (GET_REF(pSet->hBasePowerDef) == pPowerDef)
			return pSet;
	}
	FOR_EACH_END
		
	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
// returns corresponding powerSlot to the new state based on the old state and powerslot 
S32 CombatPowerStateSwitching_GetCorrespondingStatePowerSlot(Character *pChar, const char *pchNewState, const char *pchOldState, S32 iOldSlot)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingInfo *pInfo = pChar->pCombatPowerStateInfo;
		CombatPowerStateSwitchingDef *pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);

		if (pDef && pDef->eaPowerSlotSet)
		{
			FOR_EACH_IN_EARRAY(pDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
			{
				if (pchOldState && !pchNewState)
				{	// was in a defined state but now the default state
					// match the old state then return the basePowerSlot
					FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlotPower)
					{
						if (pSlotPower->iPowerSlot == iOldSlot && pSlotPower->pchState == pchOldState)
							return pSet->iBasePowerSlot;
					}
					FOR_EACH_END
				}
				else if (!pchOldState && pchNewState && iOldSlot == pSet->iBasePowerSlot)
				{	// old state was default going to a new state

					FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlotPower)
					{
						if (pSlotPower->pchState == pchNewState)
							return pSlotPower->iPowerSlot;
					}
					FOR_EACH_END
				}
			}
			FOR_EACH_END
		}
	}

	return -1;
}


// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_GetSwitchedStatePowerSlot(Character *pChar, S32 iSlot)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingInfo *pInfo = pChar->pCombatPowerStateInfo;

		if (pInfo->pchCurrentState)
		{
			CombatPowerStateSwitchingDef *pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
			
			if (pDef && pDef->eaPowerSlotSet)
			{
				FOR_EACH_IN_EARRAY(pDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
				{
					if (pSet->iBasePowerSlot == iSlot)
					{
						FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlotPower)
						{
							if (pSlotPower->pchState == pInfo->pchCurrentState)
								return pSlotPower->iPowerSlot;
						}
						FOR_EACH_END
						return -1;
					}
				}
				FOR_EACH_END
			}
		}
	}

	return -1;
}

// -------------------------------------------------------------------------------------------------------------------
Power* CombatPowerStateSwitching_GetSwitchedStatePower(Character *pChar, Power *pPower)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (pChar->pCombatPowerStateInfo && pPower)
	{
		CombatPowerStateSwitchingInfo *pInfo = NULL;
	
		pInfo = pChar->pCombatPowerStateInfo;
		
		if (pInfo->iSpecialPowerSwitchedSlot >= 0)
		{
			S32 iSlot = character_PowerIDSlot(pChar, pPower->uiID);
			if (iSlot != pInfo->iSpecialPowerSwitchedSlot)
				return NULL;
		}
		

		if ((pInfo->iSpecialPowerSwitchedSlot >= 0 || pInfo->pchCurrentState) && pPower->ppSubCombatStatePowers)
		{
			FOR_EACH_IN_EARRAY(pPower->ppSubCombatStatePowers, Power, pLinkedPower)
			{
				if (pLinkedPower->pchCombatPowersState == pInfo->pchCurrentState)
					return pLinkedPower;
			}
			FOR_EACH_END
		}
	}
#endif

	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
// 
Power* CombatPowerStateSwitching_GetBasePower(Power *pPower)
{
	if (pPower->pParentPower)
		pPower = pPower->pParentPower;
	if (pPower->pCombatPowerStateParent)
		return pPower->pCombatPowerStateParent;

	if (eaSize(&pPower->ppSubCombatStatePowers) > 0)
		return pPower;
	
	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
// Returns true if the given character can activate any power given our current state
bool CombatPowerStateSwitching_CanQueuePower(Character *pChar)
{
	if (pChar->pCombatPowerStateInfo && pChar->pCombatPowerStateInfo->eState == EPowerStateActivationState_ACTIVATING)
	{
		CombatPowerStateSwitchingDef *pDef = GET_REF(pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef);
		
		if (pDef)
		{
			F32 fTimeFinishedAllowed = 0.0f;
#if GAMESERVER // add some leniency on the server 
			fTimeFinishedAllowed = 0.3f;
#endif
			// see if we've gotten through enough of the activation to allow for a power to be queued
			return pDef->fActivatePowerQueueTime >= (pChar->pCombatPowerStateInfo->fActivationTimer - fTimeFinishedAllowed);
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------------------------------------
// Returns the time that would needed to be added to a queued power for when it should activate
F32 CombatPowerStateSwitching_GetActivateDelay(Character *pChar)
{
	if (pChar->pCombatPowerStateInfo && pChar->pCombatPowerStateInfo->eState == EPowerStateActivationState_ACTIVATING)
	{
		return pChar->pCombatPowerStateInfo->fActivationTimer;
	}

	return 0.f;
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void gslCombatPowerStateSwitching_ActivateServer(Entity* pEnt, const char *pchState, U32 uiActivateTime, S8 iActId)
{
#if GAMESERVER
	if (pEnt && pEnt->pChar)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pInfo = NULL;
		CombatPowerStateDef *pNextMode = NULL;

		if (!pEnt->pChar->pCombatPowerStateInfo)
		{
			return;
		}

		pInfo = pEnt->pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;
			
		// allowing some bit of wiggle in the timing
		if ((pInfo->fCooldown - pEnt->pChar->fTimeSlept) >= 0.3f)
		{	
			ClientCmd_gclCombatPowerStateSwitching_ExitState(pEnt, pchState, pInfo->pchCurrentState);
			return; 
		}

		if (!CombatPowerStateSwitching_CanStateSwitch(pEnt->pChar, pInfo, pDef, NULL))
		{
			ClientCmd_gclCombatPowerStateSwitching_ExitState(pEnt, pchState, pInfo->pchCurrentState);
			return;
		}
		
		if (pchState && pchState[0] != 0)
		{
			pchState = allocFindString(pchState);
			pNextMode = CombatPowerStateSwitching_GetStateByName(pchState, pDef);
			if (!CombatPowerStateSwitching_CanEnterState(pEnt->pChar, pInfo, pNextMode, NULL))
			{
				ClientCmd_gclCombatPowerStateSwitching_ExitState(pEnt, pNextMode->pchName, pInfo->pchCurrentState);
				return;
			}
		}
		
		{
			CombatPowerStateDef *pOldMode = NULL;
			if (pInfo->pchCurrentState)
				pOldMode = CombatPowerStateSwitching_GetStateByName(pInfo->pchCurrentState, pDef);
			CombatPowerStateSwitching_ExitState(pEnt->pChar, pDef, pInfo, pOldMode, uiActivateTime);
		}

		if (pchState && pchState[0] != 0)
		{
			if (pNextMode)
			{
				pInfo->iCurActIdOffset = iActId;
				CombatPowerStateSwitching_EnterState(pEnt->pChar, pDef, pInfo, pNextMode, uiActivateTime);
			}

			pInfo->fCooldown = pDef->fSwitchCooldown;
			entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		}
	}
#endif
}

// Returns true if the given character has a CombatPowerState and is in the given state
//  an empty string "" is the same as the default state 
AUTO_EXPR_FUNC(CEFuncsActivation);
bool CombatPowerStateSwitching_IsInState(SA_PARAM_OP_VALID Character *pChar, const char *pchState)
{
	if (pChar && pChar->pCombatPowerStateInfo)
	{
		if ((!pchState || *pchState == 0) && !pChar->pCombatPowerStateInfo->pchCurrentState)
			return true;

		return pChar->pCombatPowerStateInfo->pchCurrentState && pchState && 
				!stricmp(pChar->pCombatPowerStateInfo->pchCurrentState, pchState);
	}

	return false;
}



// -------------------------------------------------------------------------------------------------------------------
static void _DestroySubCombatStatePowers(Character *pChar, Power *pPow)
{
	FOR_EACH_IN_EARRAY(pPow->ppSubCombatStatePowers, Power, pLinkedPower)
	{
		power_Destroy(pLinkedPower, pChar);
	}
	FOR_EACH_END
}

// -------------------------------------------------------------------------------------------------------------------
static S32 _AreSubCombatStatePowersValid(Power *pPower, CombatPowerStatePowerSet* pLinkedSet)
{
	S32 iSize = eaSize(&pLinkedSet->eaPowers);
	if(eaSize(&pLinkedSet->eaPowers) != eaSize(&pPower->ppSubCombatStatePowers))
		return false;

	{
		S32 i;
		for (i = 0; i < iSize; ++i)
		{
			CombatPowerStatePower *pStatePowerDef = pLinkedSet->eaPowers[i];
			Power *pStatePower = pPower->ppSubCombatStatePowers[i];

			if (GET_REF(pStatePowerDef->hPowerDef) != GET_REF(pStatePower->hDef))
			{
				return false;
			}
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_CreateModePowers(Character *pChar, Power *pPow)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pInfo = NULL;
		PowerDef *pPowerDef = NULL;

		pInfo = pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;

		pPowerDef = GET_REF(pPow->hDef);
		if(pPowerDef)
		{
			CombatPowerStatePowerSet* pLinkedSet = CombatPowerStateSwitching_GetPowerSetForPowerDef(pChar, pDef, pPowerDef);
			if (pLinkedSet)
			{
				S32 bGood = false;
				if(pLinkedSet->eaPowers)
				{
					bGood = _AreSubCombatStatePowersValid(pPow, pLinkedSet);
					if(!bGood)
					{
						_DestroySubCombatStatePowers(pChar, pPow);
					}
				}

				if (!bGood)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pLinkedSet->eaPowers, CombatPowerStatePower, pLinked)
					{
						Power *pLinkedPower = NULL;
						const char *pszLinkedPowerDefName = REF_STRING_FROM_HANDLE(pLinked->hPowerDef);

						if (pszLinkedPowerDefName && 
							(pLinkedPower = power_Create(pszLinkedPowerDefName)))
						{
							eaPush(&pPow->ppSubCombatStatePowers, pLinkedPower);

							pLinkedPower->pCombatPowerStateParent = pPow;
							pLinkedPower->eSource = pPow->eSource;
							pLinkedPower->pSourceItem = pPow->pSourceItem;
							pLinkedPower->fYaw = pPow->fYaw;

							pLinkedPower->pchCombatPowersState = pLinked->pchState;
							power_CreateSubPowers(pLinkedPower);
						}
						else
						{
							Errorf("CombatPowerStateSwitching_CreateModePowers: Power_Create did not find %s", (pszLinkedPowerDefName ? pszLinkedPowerDefName : "Invalid Name!"));
						}
					}
					FOR_EACH_END
				}
				
				if(eaSize(&pLinkedSet->eaPowers) != eaSize(&pPow->ppSubCombatStatePowers))
					Errorf("CombatPowerStateSwitching_CreateModePowers not executed properly on power %s", pPowerDef->pchName);
			}
			else if (pPow->ppSubCombatStatePowers)
			{
				_DestroySubCombatStatePowers(pChar, pPow);
				eaDestroy(&pPow->ppSubCombatStatePowers);
			}
		}
	}
}


// -------------------------------------------------------------------------------------------------------------------
// Quick function to fix backpointers to parent power, generally only used by the client
void CombatPowerStateSwitching_FixSubPowers(Character *pChar, Power *pPow)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pInfo = NULL;
		PowerDef *pPowerDef = NULL;

		pInfo = pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pInfo->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;

		pPowerDef = GET_REF(pPow->hDef);
		if(pPowerDef)
		{
			CombatPowerStatePowerSet* pLinkedSet = CombatPowerStateSwitching_GetPowerSetForPowerDef(pChar, pDef, pPowerDef);
			if (pLinkedSet)
			{
				FOR_EACH_IN_EARRAY(pPow->ppSubCombatStatePowers, Power, pLinkedPower)
				{
					PowerDef *pLinkedPowerDef = GET_REF(pLinkedPower->hDef);

					pLinkedPower->pCombatPowerStateParent = pPow;
					pLinkedPower->eSource = pPow->eSource;
					pLinkedPower->pSourceItem = pPow->pSourceItem;
					pLinkedPower->fYaw = pPow->fYaw;
					
					// find the mode
					if (pLinkedPowerDef)
					{
						FOR_EACH_IN_EARRAY(pLinkedSet->eaPowers, CombatPowerStatePower, pLinked)
						{
							if (GET_REF(pLinkedPower->hDef) == pLinkedPowerDef)
							{
								pLinkedPower->pchCombatPowersState = pLinked->pchState;
								break;
							}
						}
						FOR_EACH_END
					}

					power_FixSubPowers(pLinkedPower);
				}
				FOR_EACH_END
			}
			else if (pPow->ppSubCombatStatePowers)
			{
				eaDestroy(&pPow->ppSubCombatStatePowers);
			}
		}
	}
}


// -------------------------------------------------------------------------------------------------------------------
// Returns true if the state switched powers should not track their own recharge time and share with their associated power
S32 CombatPowerStateSwitching_ShouldShareRecharge(SA_PARAM_NN_VALID Character *pChar)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = GET_REF(pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef);

		return (!pDef || !pDef->bDoNotPropagateRecharge);
	}

	return true;
}

#include "CombatPowerStateSwitching_h_ast.c"
