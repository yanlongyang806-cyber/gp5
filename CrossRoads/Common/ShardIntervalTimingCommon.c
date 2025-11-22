#include "ShardIntervalTimingCommon.h"

#include "HashFunctions.h"
#include "rand.h"
#include "timing.h"
#include "textparser.h"
#include "shardClock.h"

#include "autogen/ShardIntervalTimingCommon_h_ast.h"
#include "autogen/ShardIntervalTimingCommon_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


AUTO_ENUM;
typedef enum ShardIntervalTimingTimeZoneMode
{
	kShardIntervalTimingTimeZoneMode_UTC = 0,
	kShardIntervalTimingTimeZoneMode_PST,
	kShardIntervalTimingTimeZoneMode_PDT,

} ShardIntervalTimingTimeZoneMode;

AUTO_STRUCT ;
typedef struct ShardIntervalTimingConfig
{
	ShardIntervalTimingTimeZoneMode	uTimeZoneMode;		AST(NAME(TimeZoneMode))
	U32								uDailyResetHour;	AST(NAME(DailyResetHour) DEFAULT(3)) // 0 is midnight, 23 is 11pm. This is relative to the time zone mode.
	U32								uWeeklyResetDay;	AST(NAME(WeeklyResetDay) DEFAULT(4)) // 0 is Sunday, 6 is Saturday.
} ShardIntervalTimingConfig;


ShardIntervalTimingConfig g_ShardIntervalTimingConfig;

U32 g_uAddableIntervalOffsetSeconds=0;	// We can add this to a given time and then do a divide to see if we are in a particular time block.


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

//
// By our convention 0 seconds is currently 01Jan2000 00:00:00  01Jan is a Saturday.
// 

//  This is an offset that we can add to a time so that we can modulo the result to get either day or week blocks.
//  If we wanted the block to start at Hour 0 on Saturday, we would return zero since that already the zero mark of
//    our conventional time.
//  If we wanted the block to start at Hour 1 on Sunday, we would return (7*24*60*60) -  (24*60*60 + 1 * 60). (We want to be able to add it)
//  This deals with time zone conversions as well.
U32 ShardIntervalTiming_GetIntervalBoundaryOffsetInSeconds()
{
 	U32 uDaysFromSaturday = (g_ShardIntervalTimingConfig.uWeeklyResetDay + 1) % 7;
 	U32 uHoursFromMidnight = g_ShardIntervalTimingConfig.uDailyResetHour;

	U32 uForwardOffsetHours = uDaysFromSaturday * 24 + uHoursFromMidnight;
	U32 uAddableOffsetHours;

	// Adjust for time zone stuff.
	// If we want to do some sort of automatic daylight savings adjustment we need to change how we calculate.

	switch (g_ShardIntervalTimingConfig.uTimeZoneMode)
	{
		case kShardIntervalTimingTimeZoneMode_UTC: uForwardOffsetHours += 0; break;
		case kShardIntervalTimingTimeZoneMode_PST: uForwardOffsetHours += 8; break;
		case kShardIntervalTimingTimeZoneMode_PDT: uForwardOffsetHours += 7; break;
	}

	uForwardOffsetHours = uForwardOffsetHours % (24*7);		// in case we were pushed over one-week.
	
	// We now have a forward offset from zero. If we were to subtract this amount, we could use it for our calculations. Just to keep things all positive,
	//  we'll make a value we can Add to calculate stuff.

	uAddableOffsetHours = (24*7) - uForwardOffsetHours;

	// And convert to seconds

	return(uAddableOffsetHours * 60 * 60);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////


static U32 ShardIntervalTiming_GetTimeSecondsFromInterval(U32 uInterval, U32 uIntervalSeconds)
{
	U32 uOffsetSeconds = uInterval * uIntervalSeconds;
	U32 uUTCSecondsSince2000;
	
	if (uOffsetSeconds  > g_uAddableIntervalOffsetSeconds)
	{
		uUTCSecondsSince2000 = uOffsetSeconds - g_uAddableIntervalOffsetSeconds;
	}
	else
	{
		uUTCSecondsSince2000 = 0;
	}

	return(uUTCSecondsSince2000);
}

static U32 ShardIntervalTiming_GetIntervalFromTimeSeconds(U32 uUTCSecondsSince2000, U32 uIntervalSeconds)
{
	U32 uOffsetSeconds = uUTCSecondsSince2000 + g_uAddableIntervalOffsetSeconds;
	U32 uInterval = uOffsetSeconds / uIntervalSeconds;

	return(uInterval);
}

static bool ShardIntervalTiming_TimeIsInCurrentIntervalSeconds(U32 uUTCSecondsSince2000, U32 uIntervalSeconds)
{
	U32 uNowSeconds = ShardClock_SecondsSince2000();
	U32 uNowInterval = ShardIntervalTiming_GetIntervalFromTimeSeconds(uNowSeconds,uIntervalSeconds); 
	U32 uQueryInterval = ShardIntervalTiming_GetIntervalFromTimeSeconds(uUTCSecondsSince2000,uIntervalSeconds);
	
	return (uNowInterval==uQueryInterval);
}

////////////////////////////////////
////////////////////////////////////

bool ShardIntervalTiming_TimeIsInCurrentInterval(U32 uUTCSecondsSince2000, ShardIntervalTimingMode eIntervalMode)
{
	U32 uIntervalSeconds = 0;

	switch (eIntervalMode)
	{
		case kShardIntervalTimingMode_Off: return(false); break;	// We shouldn't really be calling this function with the mode Off
		case kShardIntervalTimingMode_Daily: uIntervalSeconds = 24*60*60; break;
		case kShardIntervalTimingMode_Weekly: uIntervalSeconds = 7*24*60*60; break;
	}
	
	return(ShardIntervalTiming_TimeIsInCurrentIntervalSeconds(uUTCSecondsSince2000, uIntervalSeconds));
}

U32 ShardIntervalTiming_TimeOfNextInterval(ShardIntervalTimingMode eIntervalMode)
{
	U32 uNowSeconds = ShardClock_SecondsSince2000();
	U32 uNowInterval = 0;
	U32 uIntervalSeconds = 0;

	switch (eIntervalMode)
	{
		case kShardIntervalTimingMode_Off: return(0); break;	// We shouldn't really be calling this function with the mode Off
		case kShardIntervalTimingMode_Daily: uIntervalSeconds = 24*60*60; break;
		case kShardIntervalTimingMode_Weekly: uIntervalSeconds = 7*24*60*60; break;
	}
	
	uNowInterval = ShardIntervalTiming_GetIntervalFromTimeSeconds(uNowSeconds,uIntervalSeconds);
	return(ShardIntervalTiming_GetTimeSecondsFromInterval(uNowInterval+1, uIntervalSeconds));
}

U32 ShardIntervalTiming_IntervalNumOfTime(U32 uTime, ShardIntervalTimingMode eIntervalMode)
{
	U32 uIntervalSeconds = 0;

	switch (eIntervalMode)
	{
		case kShardIntervalTimingMode_Off: return(0); break;	// We shouldn't really be calling this function with the mode Off
		case kShardIntervalTimingMode_Daily: uIntervalSeconds = 24*60*60; break;
		case kShardIntervalTimingMode_Weekly: uIntervalSeconds = 7*24*60*60; break;
	}

	return(ShardIntervalTiming_GetIntervalFromTimeSeconds(uTime,uIntervalSeconds));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ShardIntervalTiming_LoadConfig(void)
{
	StructReset(parse_ShardIntervalTimingConfig, &g_ShardIntervalTimingConfig);

	// Load the config file
	ParserLoadFiles(NULL, 
		"defs/config/ShardIntervalTimingConfig.def", 
		"ShardIntervalTimingConfig.bin", 
		PARSER_OPTIONALFLAG,
		parse_ShardIntervalTimingConfig, 
		&g_ShardIntervalTimingConfig);
}


AUTO_STARTUP(AS_ShardIntervalTiming);
void ShardIntervalTiming_LoadData(void)
{
	loadstart_printf("Loading ShardIntervalTiming data...");
	ShardIntervalTiming_LoadConfig();
	loadend_printf(" done.");

	// For now do this once at load-up.
	g_uAddableIntervalOffsetSeconds=ShardIntervalTiming_GetIntervalBoundaryOffsetInSeconds();
}


#include "autogen/ShardIntervalTimingCommon_h_ast.c"
#include "autogen/ShardIntervalTimingCommon_c_ast.c"
