#include "timing.h"

#if defined(GAMESERVER)
	#include "gslActivity.h"
#elif defined(GAMECLIENT)
	#include "gclActivity.h"
#endif

// Wrapper for "Now" seconds so we can distinguish client from server conveniently.
// In reality, the EventClock functionality should be folded into this file and we
//   should retire the gslActivity/gclActivity functions.
// We also need to figure out how to make this work on AppServers, etc.
U32 ShardClock_SecondsSince2000()
{
// Use the event clock so we have better control.
#if defined(GAMESERVER)
	return(gslActivity_GetEventClockSecondsSince2000());
#elif defined(GAMECLIENT)
	return(gclActivity_EventClock_GetSecondsSince2000());
#else
	devassert(0 && "Shard Clock is not implemented on this server. Returning timeSecondsSince2000");
	return(timeSecondsSince2000());
#endif
}

