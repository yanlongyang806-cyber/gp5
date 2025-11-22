#pragma once
GCC_SYSTEM

#include "ReferenceSystem.h"
#include "ShardIntervalTimingCommon.h"

typedef struct ItemDef ItemDef;
typedef struct GameAccountData GameAccountData;

AUTO_STRUCT;
typedef struct UGCTipsConfig
{
	// If this is zero, it indicates the other fields should be ignored and tipping is not enabled.
	U32 uTippingEnabled;
	
    // Which numeric to use for tips
    REF_TO(ItemDef) hTipsNumeric;

    // Time time interval over which we limit tipping.
	// We allow 1 tip per author over this period, and "allowedTipsPerTimeInterval" tips overall.
	
	// To figure out the interval blocks, first look at the IntervalMode.
	////  If it is not "Off" use the appropriate ShardInterval timing. 

		// Shard-clock-based interval timing. Uses uTimeOfTip to determine availability.
		ShardIntervalTimingMode eUGCTipsIntervalMode;		AST(NAME(IntervalMode))

	////  If it IS "off" fall back to the old-style cooldowns
	    U32 timeIntervalSeconds;
	
	// Number of overall tips allowed per time interval
    U32 allowedTipsPerTimeInterval;

	// List of valid tip amounts.
	S32 *pTipAmounts;
	
} UGCTipsConfig;

extern UGCTipsConfig gUGCTipsConfig;


bool UGCTipsAlreadyTippedAuthor(GameAccountData *pTipperAccountData, U32 uAuthorAccountID);
bool UGCTipsAlreadyTippedMaxTimes(GameAccountData *pTipperAccountData);
int UGCTipsAllowedTipsRemaining(GameAccountData *pTipperAccountData);

bool UGCTipsInCurrentTimeBlock(U32 uTipTime);

bool UGCTipsEnabled();
