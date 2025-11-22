#include "UGCTipsCommon.h"
#include "ResourceManager.h"
#include "GameBranch.h"
#include "file.h"
#include "itemCommon.h"
#include "timing.h"
#include "shardClock.h"
#include "GameAccountData/GameAccountData.h"

#include "AutoGen/UGCTipsCommon_h_ast.h"

#include "error.h"

#include "Autogen/ShardIntervalTimingCommon_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

UGCTipsConfig gUGCTipsConfig;


////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Load and Validate

void UGCTips_ConfigValidate(const char* filename, UGCTipsConfig* pConfig)
{
	if (pConfig==NULL || pConfig->uTippingEnabled==0)
	{
		return;
	}
	
    if (!GET_REF(pConfig->hTipsNumeric)) {
        ErrorFilenamef(filename, "UGCTips references non-existent tip numeric item '%s'", REF_STRING_FROM_HANDLE(pConfig->hTipsNumeric));
    }
    else
    {
        ItemDef *tipsItemDef = GET_REF(pConfig->hTipsNumeric);
        if ( tipsItemDef->eType != kItemType_Numeric )
        {
            ErrorFilenamef(filename, "UGCTips references tip item that is not a numeric '%s'", REF_STRING_FROM_HANDLE(pConfig->hTipsNumeric));
        }
    }

	if (gUGCTipsConfig.eUGCTipsIntervalMode==kShardIntervalTimingMode_Off && pConfig->timeIntervalSeconds == 0 )
    {
        ErrorFilenamef(filename, "UGCTips time interval must be non-zero if the IntervalMode is Off");
    }

    if ( pConfig->allowedTipsPerTimeInterval <= 0 )
    {
        ErrorFilenamef(filename, "UGCTips tips per interval must be greater than zero");
    }
}


AUTO_STARTUP(UGCTips) ASTRT_DEPS(Items);
void UGCTips_Load(void)
{
	char *estrSharedMemory = NULL;
	char *pcBinFile = NULL;
	char *pcBuffer = NULL;
	char *estrFile = NULL;

    loadstart_printf("Loading UGCTips config...");

    // initialize with default values in case the config file doesn't exist
    StructReset(parse_UGCTipsConfig, &gUGCTipsConfig);

	MakeSharedMemoryName(GameBranch_GetFilename(&pcBinFile, "FoundryTips.bin"),&estrSharedMemory);
	estrPrintf(&estrFile, "defs/config/%s", GameBranch_GetFilename(&pcBuffer, "FoundryTips.def"));
	ParserLoadFilesShared(estrSharedMemory, NULL, estrFile, pcBinFile, PARSER_OPTIONALFLAG, parse_UGCTipsConfig, &gUGCTipsConfig);

	// Not entirely sure about this, but NumericConversion effectively does the same thing and only validates on a real server
    if (IsGameServerSpecificallly_NotRelatedTypes())
	{
		UGCTips_ConfigValidate(estrFile, &gUGCTipsConfig);
	}

	estrDestroy(&estrFile);

	loadend_printf("Done.");

	estrDestroy(&estrFile);
	estrDestroy(&pcBuffer);
	estrDestroy(&pcBinFile);
	estrDestroy(&estrSharedMemory);
}




////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Utility functions

bool UGCTipsInCurrentTimeBlock(U32 uTipTime)
{
	if (gUGCTipsConfig.eUGCTipsIntervalMode==kShardIntervalTimingMode_Off)
	{
		// Old style computation
		U32 startOfCurrentInterval;
		U32 curTime = ShardClock_SecondsSince2000();
		
	    // compute the start time of the current time interval
	    startOfCurrentInterval = ( curTime / gUGCTipsConfig.timeIntervalSeconds ) * gUGCTipsConfig.timeIntervalSeconds;
	
		// If the recorded tip is in the current interval
		if (uTipTime >= startOfCurrentInterval)
		{
			return(true);
		}
	}
	else
	{
		return(ShardIntervalTiming_TimeIsInCurrentInterval(uTipTime, gUGCTipsConfig.eUGCTipsIntervalMode));
	}
	return(false);
}

bool UGCTipsAlreadyTippedAuthor(GameAccountData *pTipperAccountData, U32 uAuthorAccountID)
{
	int i;

	if (gUGCTipsConfig.uTippingEnabled==0)
	{
		return(false);
	}
	for(i = eaSize(&(pTipperAccountData->eaTipRecords))-1; i>=0; i--)
	{
		// If the recorded tip is for the author specified
		if (pTipperAccountData->eaTipRecords[i]->uTipAuthorAccountID == uAuthorAccountID)
		{
			// And If the recorded tip is in the current interval
			return(UGCTipsInCurrentTimeBlock(pTipperAccountData->eaTipRecords[i]->uTimeOfTip));
		}
	}
	return(false);
}

bool UGCTipsAlreadyTippedMaxTimes(GameAccountData *pTipperAccountData)
{
	int iMaxTipsPerPeriod=gUGCTipsConfig.allowedTipsPerTimeInterval;
	int iTipCount=0;
	int i;

	if (gUGCTipsConfig.uTippingEnabled==0)
	{
		return(false);
	}

	for(i = eaSize(&(pTipperAccountData->eaTipRecords))-1; i>=0; i--)
	{
		if (UGCTipsInCurrentTimeBlock(pTipperAccountData->eaTipRecords[i]->uTimeOfTip))
		{
			iTipCount++;
		}
	}

	if (iTipCount>=iMaxTipsPerPeriod)
	{
		return(true);
	}
	return(false);
}

int UGCTipsAllowedTipsRemaining(GameAccountData *pTipperAccountData)
{
	int iMaxTipsPerPeriod=gUGCTipsConfig.allowedTipsPerTimeInterval;
	int iTipCount=0;
	int i;

	if (gUGCTipsConfig.uTippingEnabled==0)
	{
		return(0);
	}

	for(i = eaSize(&(pTipperAccountData->eaTipRecords))-1; i>=0; i--)
	{
		if (UGCTipsInCurrentTimeBlock(pTipperAccountData->eaTipRecords[i]->uTimeOfTip))
		{
			iTipCount++;
		}
	}

	return(iMaxTipsPerPeriod-iTipCount);
}

bool UGCTipsEnabled()
{
	return(gUGCTipsConfig.uTippingEnabled!=0);
}



#include "UGCTipsCommon_h_ast.c"
