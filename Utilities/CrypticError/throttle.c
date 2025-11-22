#include "sysutil.h"
#include "throttle.h"
#include "timing.h"
#include "ui.h"

// How long do we run at full speed before pulling back?
#define THROTTLE_ESTIMATE_TIME_SEC (3)

// What percentage of the estimated outbound pipe do want to use?
#define THROTTLE_UTILIZATION_PERCENT (50)

// What is the minimum amount of bytes/sec we'll allow?
#define THROTTLE_MINIMUM_BYTESPERSEC (5 * 1024) 

typedef enum ThrottleState
{
	TS_DISABLED = 0,
	TS_ESTIMATING,
	TS_THROTTLING,

	TS_COUNT
} ThrottleState;

static ThrottleState sThrottleState = TS_DISABLED;
static size_t suTotalBytesSentDuringEstimate = 0;
static int sTimerHandle = 0;

static F32 sfThrottledBytesPerSec = 0;
static size_t suTotalBytesToSend = 0;
static F32 sfLastProgressUpdateDurationSec = 0;

// -----------------------------------------------------------------

void throttleReset(size_t uTotalBytesToSend)
{
	if(!sTimerHandle)
		sTimerHandle = timerAlloc();

	timerStart(sTimerHandle);
	sfLastProgressUpdateDurationSec = 0;

	suTotalBytesToSend = uTotalBytesToSend;
	suTotalBytesSentDuringEstimate = 0;

	if(sfThrottledBytesPerSec == 0)
	{
		sThrottleState = TS_ESTIMATING;
		LogStatusBar("Estimating available bandwidth...");
	}
	else
	{
		sThrottleState = TS_THROTTLING;
	}
}

// This function calculates how much data we're going to send every second,
// based on the perceived maximum outbound rate seen over the first few
// seconds of the data send.
void throttleRecalc(size_t uTotalSentBytes, F32 fDurationSec)
{
//	char wtf[512];
	F32 fBytesPerSec = ((F32)uTotalSentBytes / (F32)fDurationSec);
	sfThrottledBytesPerSec = fBytesPerSec * (THROTTLE_UTILIZATION_PERCENT / 100.0);
	if(sfThrottledBytesPerSec < THROTTLE_MINIMUM_BYTESPERSEC)
	{
		// Just so it isn't ridiculously slow.
		sfThrottledBytesPerSec = THROTTLE_MINIMUM_BYTESPERSEC;
	}

	//sprintf(wtf, "Estimated %2.2f Kbytes/s", fBytesPerSec / 1024.0);
	//LogNote(wtf);
	//sprintf(wtf, "Locking to %2.2f Kbytes/s", sfThrottledBytesPerSec / 1024.0);
	//LogNote(wtf);

	suTotalBytesSentDuringEstimate = uTotalSentBytes;
	timerStart(sTimerHandle);
}

void throttleUpdateProgressText(size_t uTotalSentBytes)
{
	F32 fDurationSec = timerElapsed(sTimerHandle);

	if( !sfLastProgressUpdateDurationSec 
	||  ((fDurationSec - sfLastProgressUpdateDurationSec) > 1) )
	{
		F32 fBytesPerSec = ((F32)(uTotalSentBytes - suTotalBytesSentDuringEstimate) / (F32)fDurationSec);

		LogTransferProgress(
			suTotalBytesToSend - uTotalSentBytes,
			suTotalBytesToSend,
			(fBytesPerSec / 1024.0));

		sfLastProgressUpdateDurationSec = fDurationSec;
	}
}

void throttleSleep(size_t uTotalSentBytes)
{
	uTotalSentBytes -= suTotalBytesSentDuringEstimate;

	{
		// Length of time since the beginning of the upload
		F32 fDurationSec = timerElapsed(sTimerHandle);
		F32 fExpectedSec = (F32)uTotalSentBytes / (F32)(sfThrottledBytesPerSec);

		// We're going too fast, make up for the time we wanted to take
		if(fExpectedSec > fDurationSec)
		{
			F32 fTimeToSleepSec = fExpectedSec - fDurationSec;
			U32 uTimeToSleepMS  = (U32)(fTimeToSleepSec * 1000.0f);
			delay(uTimeToSleepMS);
		}
	}
}

void throttleProgress(size_t uTotalSentBytes)
{
	switch(sThrottleState)
	{
	case TS_ESTIMATING:
		{
			F32 fDurationSec = timerElapsed(sTimerHandle);
			if(fDurationSec > THROTTLE_ESTIMATE_TIME_SEC)
			{
				throttleRecalc(uTotalSentBytes, fDurationSec);
				sThrottleState = TS_THROTTLING;
			}

			throttleUpdateProgressText(uTotalSentBytes);			
			break;
		}

	case TS_THROTTLING:
		{
			throttleSleep(uTotalSentBytes);
			throttleUpdateProgressText(uTotalSentBytes);			
			break;
		}

	case TS_DISABLED:
	default:
		{
			return;
		}
	}
}
