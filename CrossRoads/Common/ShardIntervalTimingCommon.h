#include "stdtypes.h"

#ifndef SHARDINTERVALTIMING_COMMON_H
#define SHARDINTERVALTIMING_COMMON_H

AUTO_ENUM;
typedef enum ShardIntervalTimingMode
{
	kShardIntervalTimingMode_Off = 0,
	kShardIntervalTimingMode_Daily,
	kShardIntervalTimingMode_Weekly,
		
} ShardIntervalTimingMode;

bool ShardIntervalTiming_TimeIsInCurrentInterval(U32 uUTCSecondsSince2000, ShardIntervalTimingMode eIntervalMode);
U32 ShardIntervalTiming_TimeOfNextInterval(ShardIntervalTimingMode eIntervalMode);
U32 ShardIntervalTiming_IntervalNumOfTime(U32 uTime, ShardIntervalTimingMode eIntervalMode);

#endif


