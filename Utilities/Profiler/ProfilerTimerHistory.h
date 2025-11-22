
#pragma once

typedef struct TimerHistoryChunk TimerHistoryChunk;

typedef struct TimerHistoryFrame {
	U32										frameIndex;

	U32										count;
	
	struct {
		U64									begin;
		U64									active;
		U64									blocking;
		U64									other;
		U64									os;
	} cycles;
	
	struct {
		U64									user;
		U64									kernel;
	} osTicks;
} TimerHistoryFrame;

typedef struct IterateTimerHistoryData {
	const TimerHistoryFrame*				hf;
	U32										frameIndex;
	S32										x0;
	S32										x1;

	U64										frameCyclesBegin;
	U64										frameCyclesDelta;
} IterateTimerHistoryData;

typedef struct TimerHistoryChunkListGroup TimerHistoryChunkListGroup;

typedef struct TimerHistoryChunkListGroup {
	TimerHistoryChunkListGroup*				parent;
	U32										byteCount;
} TimerHistoryChunkListGroup;

typedef struct TimerHistoryChunkList {
	TimerHistoryChunkListGroup*				group;
	TimerHistoryChunk*						head;
	TimerHistoryChunk*						tail;
	U32										count;
	
	struct {
		U32									hasCount			: 1;
		U32									hasCyclesActive		: 1;
		U32									hasCyclesBlocking	: 1;
		U32									hasCyclesOther		: 1;
		U32									hasCyclesBegin		: 1;
		U32									hasOSCycles			: 1;
		U32									hasOSTicksUser		: 1;
		U32									hasOSTicksKernel	: 1;
	} flags;
} TimerHistoryChunkList;

typedef S32 (*TimerHistoryIterateCallback)(	const IterateTimerHistoryData* ithd,
											void* userPointer);

void	timerHistoryIterate(TimerHistoryIterateCallback callback,
							void* callbackUserPointer,
							const TimerHistoryChunkList* clCycles,
							const TimerHistoryChunkList* clFrames,
							S32 sx,
							U64 cyclesMin,
							U64 cyclesMax);

void	timerHistoryChunkListClear(TimerHistoryChunkList* cl);

void	timerHistoryFrameAddValues(	TimerHistoryChunkList* cl,
									const U32 frameIndex,
									U32 count,
									U64 cyclesActive,
									U64 cyclesBlocking,
									U64 cyclesBegin,
									U64 cyclesOther,
									U64 osCycles,
									U64 osTicksUser,
									U64 osTicksKernel);

S32 timerHistoryGetExistingFrame(	const TimerHistoryChunkList* cl,
									U32 frameIndex,
									TimerHistoryFrame* hfOut);
