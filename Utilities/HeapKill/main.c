#include "utilitiesLib.h"
#include "cmdparse.h"
#include "timing_profiler_interface.h"
#include "winutil.h"
#include "sysutil.h"
#include "file.h"
#include "earray.h"
#include "ThreadManager.h"
#include "MemTrack.h"
#include "MemAlloc.h"
#include "osdependent.h"
#include "utils.h"

// Amount of memory to keep allocated (in MB)
static size_t giMaxAllocSize = 512;
AUTO_CMD_INT(giMaxAllocSize, MaxAllocSize) ACMD_CMDLINE;

// If non-zero, always allocate this size (in KB)
static size_t giFixedAllocSize = 0;
AUTO_CMD_INT(giFixedAllocSize, FixedAllocSize) ACMD_CMDLINE;

// If true, use uniform distribution
static bool gbUniformDistribution = false;
AUTO_CMD_INT(gbUniformDistribution, UniformDistribution) ACMD_CMDLINE;

// If true, try to fragment memory
static bool gbFragment = false;
AUTO_CMD_INT(gbFragment, Fragment) ACMD_CMDLINE;

// If true, initialize 1 byte every 2K
static bool gbTouchMemory = true;
AUTO_CMD_INT(gbTouchMemory, TouchMemory) ACMD_CMDLINE;

// Frequency to do an allocation greater than 1KB
static int giLargeChance = 1;
AUTO_CMD_INT(giLargeChance, LargeChance) ACMD_CMDLINE;

// Number of allocations to do per frame
static int giNumAllocAtTime = 10;
AUTO_CMD_INT(giNumAllocAtTime, NumAllocAtTime) ACMD_CMDLINE;

// Percent of 256-byte allocations
static int giSmallAllocPercent = 0;
AUTO_CMD_INT(giSmallAllocPercent, SmallAllocPercent) ACMD_CMDLINE;

// Number of threads
static int giNumThreads = 1;
AUTO_CMD_INT(giNumThreads, NumThreads) ACMD_CMDLINE;

// If true, set the working set size (1GB more)
static bool gbSetWorkingSet = false;
AUTO_CMD_INT(gbSetWorkingSet, SetWorkingSet) ACMD_CMDLINE;

// If true, allow freeing of memory
static bool gbAllowFree = true;
AUTO_CMD_INT(gbAllowFree, AllowFree) ACMD_CMDLINE;

static bool gbHitMax = false;

static size_t sizeToAllocNext(void)
{
	static size_t last_amount = 0;
	size_t amount = 0;
	if (giFixedAllocSize)
	{
		amount = giFixedAllocSize*1024;
	}
	else if (gbUniformDistribution)
	{
		amount = (rand()%5024)*1024;
	}
	else if (gbFragment)
	{
		if (gbHitMax || !last_amount || last_amount == 1024 * 16)
		{
			amount = 1024 * 18;
		}
		else
		{
			amount = 1024 * 16;
		}
	}
	else if (rand()%100 < giSmallAllocPercent)
	{
		amount = (rand()%(1024-128))+128;
	}
	else if (!(rand()%giLargeChance))
	{
		amount = rand()%64;
		amount = amount * amount * 1024;
	}
	else
	{
		amount = rand()%1024;
	}
	last_amount = amount;
	return amount;
}

static void touchMemory(void * pData, size_t iSize)
{
	size_t i = 0;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < iSize; i+= 2 * 1024)
	{
		char * pBytes = (char *)pData + i;
		pBytes[0] = 1;
	}

	PERFINFO_AUTO_STOP();
}

static void killHeap(U64 * * eaAllocs)
{
	void * pData = NULL;
	size_t iToAlloc = 0;

	PERFINFO_AUTO_START_FUNC();

	iToAlloc = sizeToAllocNext();
	if (!iToAlloc) iToAlloc = 1;
	pData = malloc(iToAlloc);

	if (gbHitMax && gbFragment)
	{
		free(pData);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (gbAllowFree)
	{
		if (!gbFragment || iToAlloc == 1024 * 16) {
			eai64Push(eaAllocs, (U64)pData);
		}
	}
	
	if (gbTouchMemory) touchMemory(pData, iToAlloc);

	PERFINFO_AUTO_STOP();
}

static bool shouldFreeSome(bool bHasSome)
{
	if (gbFragment && !gbHitMax) return false;
	if (bHasSome && (gbHitMax || (!rand()%2))) return true;
	return false;
}

static void freeSome(U64 * * eaAllocs)
{
	PERFINFO_AUTO_START_FUNC();

	if (shouldFreeSome(eai64Size(eaAllocs) > 0))
	{
		int iToFree = rand()%eai64Size(eaAllocs);

		free((void*)(*eaAllocs)[iToFree]);
		eai64RemoveFast(eaAllocs, iToFree);
	}

	PERFINFO_AUTO_STOP();
}

DWORD WINAPI heapKillThread(LPVOID lpParam)
{
	U64 * eaAllocs = NULL;

	while (true)
	{
		int i = 0;
		int iCurUsed = 0;

		for (i = 0; i < giNumAllocAtTime; i++)
		{
			timerTickBegin();
			killHeap(&eaAllocs);
			timerTickEnd();
		}

		if (gbAllowFree)
		{
			for (i = 0; i < giNumAllocAtTime; i++)
			{
				timerTickBegin();
				freeSome(&eaAllocs);
				timerTickEnd();
			}
		}
	}
}

int main(int argc, char * argv[])
{
	int i = 0;
	U32 elapsedTimerID = 0;
	size_t iMinWorkingSet = 0;
	size_t iMaxWorkingSet = 0;
	bool bSetWorkingSetSize = false;

	WAIT_FOR_DEBUGGER
	EXCEPTION_HANDLER_BEGIN

	disableRtlHeapChecking(NULL);

	setCavemanMode();
	setDefaultProductionMode(true);

	DO_AUTO_RUNS;

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'K', 0xff0000);
	SetConsoleTitle("HeapKill");
	newConsoleWindow();
	showConsoleWindow();
	printf("HeapKill: Initial internal heap segments: %d\n", memTrackCountInternalHeapSegments(CRYPTIC_FIRST_HEAP));

	utilitiesLibStartup_Lightweight();
	cmdParseCommandLine(argc, argv);

	if (gbSetWorkingSet)
	{
		iMinWorkingSet = (giMaxAllocSize + 1000) * 1024 * 1024;
		iMaxWorkingSet = iMinWorkingSet + 100 * 1024 * 1024;
		bSetWorkingSetSize = SetProcessWorkingSetSizeEx(GetCurrentProcess(), iMinWorkingSet, iMaxWorkingSet, QUOTA_LIMITS_HARDWS_MIN_ENABLE);
		if (!bSetWorkingSetSize)
		{
			DWORD dwLastError = GetLastError();
			printf("Error setting working set size: %i\n", dwLastError);
		}
	}
	GetProcessWorkingSetSize(GetCurrentProcess(), &iMinWorkingSet, &iMaxWorkingSet);
	printf("Min working set: %6.2fMB\nMax working set: %6.2fMB\n", iMinWorkingSet / (1024*1024.0), iMaxWorkingSet / (1024*1024.0));

	printf("Trying to ruin your heap. You should connect with a profiler to see what's up.\n");

	srand(42);

	for (i = 0; i < giNumThreads; i++)
	{
		(void)tmCreateThread(heapKillThread, NULL);
	}

	elapsedTimerID = timerAlloc();
	timerStart(elapsedTimerID);
	while (true)
	{
		F32 elapsed = 0;
		size_t iCurUsed = 0;

		timerTickBegin();

		elapsed = timerElapsedAndStart(elapsedTimerID);
		utilitiesLibOncePerFrame(elapsed, 1);

		if (!gbHitMax) iCurUsed = getProcessPageFileUsage();
		
		if (iCurUsed > giMaxAllocSize * 1024 * 1024 && !gbHitMax)
		{
			gbHitMax = true;
			printf("Reached steady memory usage of ~%6.2fMB\n", iCurUsed / (1024*1024.0));
			printf("Internal heap segments: %d\n", memTrackCountInternalHeapSegments(CRYPTIC_FIRST_HEAP));
			if (!gbAllowFree)
			{
				printf("Closing because free is not allowed.\n");
				timerTickEnd();
				break;
			}
		}

		timerTickEnd();
	}

	EXCEPTION_HANDLER_END

	return EXIT_SUCCESS;
}
