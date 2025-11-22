#include "stdtypes.h"

#ifndef SHARDCLOCK_H
#define SHARDCLOCK_H

///////////////////////////////////////////////////////////////////
//
//    A ShardClock which can be used for any game systems that need
// a time. It's value can be adjusted via console commands which should
// make testing of timed systems easier.
// 

U32 ShardClock_SecondsSince2000();

#endif


