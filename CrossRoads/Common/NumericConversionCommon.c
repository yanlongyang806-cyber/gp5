#include "NumericConversionCommon.h"
#include "ResourceManager.h"
#include "GameBranch.h"
#include "file.h"
#include "itemCommon.h"
#include "timing.h"
#include "Entity.h"
#include "GamePermissionsCommon.h"
#include "Player.h"
#include "GameAccountDataCommon.h"

#include "ShardClock.h"

#include "AutoGen/ShardIntervalTimingCommon_h_ast.h"
#include "AutoGen/NumericConversionCommon_h_ast.h"

#include "error.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_NumericConversionDictionary = NULL;

AUTO_TRANS_HELPER;
S32
NumericConversion_trh_GetBonusQuantity(ATH_ARG NOCONST(Entity) *pEnt, NumericConversionDef *conversionDef, GameAccountDataExtract* pExtract)
{
	int i;
	S32 sReturn = 0;

	for(i=0;i<eaSize(&conversionDef->BonusDefs);i++)
	{
		if(conversionDef->BonusDefs[i]->GamePermissionValue)
		{
			char *estrBuffer = NULL;
			bool ret = false;

			GenerateGameTokenKey(&estrBuffer,
				kGameToken_Inv,
				GAME_PERMISSION_OWNED,
				conversionDef->BonusDefs[i]->GamePermissionValue);

			if (pExtract)
				ret = (eaIndexedGetUsingString(&pExtract->eaTokens, estrBuffer) != NULL);

			if(ret == false)
				continue;
		}

		sReturn += conversionDef->BonusDefs[i]->bonusQuantity;
	}

	return sReturn;
}

bool NumericConversion_TimeIsInCurrentInterval(U32 uTime, NumericConversionDef *conversionDef)
{
	if (conversionDef->eIntervalMode==kShardIntervalTimingMode_Off)
	{
		// Old explicit computation
	    U32 curTime = ShardClock_SecondsSince2000();

	    // compute the start time of the current time interval
		U32 startOfCurrentInterval = ( curTime / conversionDef->timeIntervalSeconds ) * conversionDef->timeIntervalSeconds;
	
	    if ( uTime < startOfCurrentInterval )
	    {
			return(false);
	    }
	}
	else
	{
		// Shard daily or weekly.
		return(ShardIntervalTiming_TimeIsInCurrentInterval(uTime, conversionDef->eIntervalMode));
	}
	return(true);
}

U32 NumericConversion_IntervalNumOfTime(U32 uTime, NumericConversionDef *conversionDef)
{
	if (conversionDef->eIntervalMode==kShardIntervalTimingMode_Off)
	{
		return(uTime / conversionDef->timeIntervalSeconds);
	}
	else
	{
		// Shard daily or weekly.
		return(ShardIntervalTiming_IntervalNumOfTime(uTime, conversionDef->eIntervalMode));
	}
	return(true);
}



S32
NumericConversion_QuantityRemaining(Entity *pEnt, NumericConversionState * const * const * hConversionStates, NumericConversionDef *conversionDef)
{
    U32 lastConversionTime = 0;
    S32 lastConversionQuantity = 0;
    U32 curTime = ShardClock_SecondsSince2000();
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 iQuantityPerInterval = conversionDef->quantityPerInterval + NumericConversion_GetBonusQuantity(pEnt, conversionDef, pExtract);
	NumericConversionState *conversionState = eaIndexedGetUsingString(hConversionStates, conversionDef->name);

    if ( conversionState )
    {
        lastConversionTime = conversionState->lastConversionTime;
        lastConversionQuantity = conversionState->quantityConverted;
    }

	if (!NumericConversion_TimeIsInCurrentInterval(lastConversionTime, conversionDef))
	{
		return iQuantityPerInterval;
	}
		
    if ( iQuantityPerInterval >= lastConversionQuantity )
    {
        return iQuantityPerInterval - lastConversionQuantity;
    }

    // if we get here it means that a previous conversion was too large
	ErrorDetailsf("EntityID=%u, curTime=%u, lastConversionTime=%u, lastConversionQuantity=%d, conversionName=%s", pEnt->myContainerID, curTime, lastConversionTime, lastConversionQuantity, conversionDef->name);
    Errorf("Numeric conversion state quantity is too large");

    return 0;
}

S32 
NumericConversion_QuantityRemainingFromString(Entity *pEnt, const char *conversionName)
{
	NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);

	if ( pEnt == NULL || pEnt->pPlayer == NULL || conversionDef == NULL )
	{
		return false;
	}

	return NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef);
}



bool 
NumericConversion_Validate(NumericConversionDef *pDef)
{
    if( !resIsValidName(pDef->name) )
    {
        ErrorFilenamef( pDef->filename, "NumericConversion name is illegal: '%s'", pDef->name );
        return 0;
    }

    if (!GET_REF(pDef->hSourceNumeric) && REF_STRING_FROM_HANDLE(pDef->hSourceNumeric)) {
        ErrorFilenamef(pDef->filename, "NumericConversion references non-existent source numeric item '%s'", REF_STRING_FROM_HANDLE(pDef->hSourceNumeric));
    }
    else
    {
        ItemDef *sourceItemDef = GET_REF(pDef->hSourceNumeric);
        if ( sourceItemDef->eType != kItemType_Numeric )
        {
            ErrorFilenamef(pDef->filename, "NumericConversion references source item that is not a numeric '%s'", REF_STRING_FROM_HANDLE(pDef->hSourceNumeric));
        }
    }

    if (!GET_REF(pDef->hDestNumeric) && REF_STRING_FROM_HANDLE(pDef->hDestNumeric)) {
        ErrorFilenamef(pDef->filename, "NumericConversion references non-existent destination numeric item '%s'", REF_STRING_FROM_HANDLE(pDef->hDestNumeric));
    }
    else
    {
        ItemDef *destItemDef = GET_REF(pDef->hDestNumeric);
        if ( destItemDef->eType != kItemType_Numeric )
        {
            ErrorFilenamef(pDef->filename, "NumericConversion references destination item that is not a numeric '%s'", REF_STRING_FROM_HANDLE(pDef->hDestNumeric));
        }
    }

	if (pDef->eIntervalMode!=kShardIntervalTimingMode_Off)
	{
	    if ( pDef->timeIntervalSeconds != 0 )
	    {
	        ErrorFilenamef(pDef->filename, "NumericConversion specifies a shard daily or weekly interval mode but also has a non-zero explicit interval.");
	    }
	}
	else
	{
	    if ( pDef->timeIntervalSeconds == 0 )
	    {
	        ErrorFilenamef(pDef->filename, "NumericConversion time interval must be non-zero if it is not shard daily or weekly interval mode.");
	    }
	}

    if ( pDef->quantityPerInterval <= 0 )
    {
        ErrorFilenamef(pDef->filename, "NumericConversion quantity per interval must be greater than zero");
    }

    return 1;
}

static int 
NumericConversion_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, NumericConversionDef *pNumericConversion, U32 userID)
{
    switch(eType)
    {
        xcase RESVALIDATE_POST_TEXT_READING:	
            NumericConversion_Validate(pNumericConversion);
            return VALIDATE_HANDLED;
        xcase RESVALIDATE_FIX_FILENAME:
            {
                char *pchPath = NULL;
                resFixPooledFilename(&pNumericConversion->filename, GameBranch_GetDirectory(&pchPath, "defs/numericConversion"), NULL, pNumericConversion->name, ".numericConversion");
                estrDestroy(&pchPath);
            }
            return VALIDATE_HANDLED;
    }
    return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int 
RegisterNumericConversionDictionary(void)
{

    g_NumericConversionDictionary = RefSystem_RegisterSelfDefiningDictionary("NumericConversion", false, parse_NumericConversionDef, true, true, NULL);

    if (IsGameServerSpecificallly_NotRelatedTypes())
    {
        resDictManageValidation(g_NumericConversionDictionary, NumericConversion_ResValidateCB);
    }
    if (IsServer())
    {
        resDictProvideMissingResources(g_NumericConversionDictionary);
        if (isDevelopmentMode() || isProductionEditMode()) {
            resDictMaintainInfoIndex(g_NumericConversionDictionary, ".Name", NULL, NULL, NULL, NULL);
        }
    } 
    else if (IsClient())
    {
        resDictRequestMissingResources(g_NumericConversionDictionary, 8, false, resClientRequestSendReferentCommand);
    }

    return 1;
}

void 
NumericConversion_Load(void)
{
    char *pchPath = NULL;
    char *pchBinFile = NULL;

    resLoadResourcesFromDisk(g_NumericConversionDictionary, 
        GameBranch_GetDirectory(&pchPath, "defs/numericConversion"), ".numericConversion",
        GameBranch_GetFilename(&pchBinFile, "NumericConversion.bin"), 
        PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

    estrDestroy(&pchPath);
    estrDestroy(&pchBinFile);
}

AUTO_STARTUP(NumericConversion) ASTRT_DEPS(Items);
void 
NumericConversion_LoadDefs(void)
{
    NumericConversion_Load();
}

NumericConversionDef* 
NumericConversion_DefFromName(const char* numericConversionName)
{
    if (numericConversionName)
    {
        return (NumericConversionDef*)RefSystem_ReferentFromString(g_NumericConversionDictionary, numericConversionName);
    }
    return NULL;
}

U32 NumericConversion_QuantityConverted(Entity *pEnt, const char *numericConverstionName)
{
	NumericConversionDef *conversionDef = NumericConversion_DefFromName(numericConverstionName);
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	if ( pEnt == NULL || pEnt->pPlayer == NULL || conversionDef == NULL )
	{
		return false;
	}

	return conversionDef->quantityPerInterval + NumericConversion_GetBonusQuantity(pEnt,conversionDef, pExtract) - NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef);
}

#include "NumericConversionCommon_h_ast.c"
