
#include "wininclude.h"
#include "ProfilerTimerHistory.h"
#include "timing_profiler_interface.h"
#include "FragmentedBuffer.h"

typedef struct TimerHistoryChunk TimerHistoryChunk;

typedef struct TimerHistoryChunk {
	TimerHistoryChunk*						next;
	TimerHistoryChunk*						prev;

	U64										cyclesMin;
	U64										cyclesMax;

	U32										frameIndexMin;
	U32										frameIndexMax;
	U32										bitsLastFrameBegin;
	U32										bitsTotal;

	U8										buffer[100];
} TimerHistoryChunk;

static struct {
	HANDLE									hThread;
	HANDLE									eventWake;
	TimerHistoryChunkList					cl;
	CRITICAL_SECTION						cs;
} clFreeingThread;

static U32 __stdcall clFreeingThreadMain(void* unused){
	EXCEPTION_HANDLER_BEGIN

	SetThreadPriority(GetCurrentThread(), BELOW_NORMAL_PRIORITY_CLASS);

	while(1){
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		WaitForSingleObject(clFreeingThread.eventWake, INFINITE);
		
		while(1){
			TimerHistoryChunk* c;
			
			EnterCriticalSection(&clFreeingThread.cs);
			
			c = clFreeingThread.cl.head;
			
			if(c){
				clFreeingThread.cl.head = c->next;
				
				if(!clFreeingThread.cl.head){
					assert(clFreeingThread.cl.tail == c);
					clFreeingThread.cl.tail = NULL;
				}
			}
			
			LeaveCriticalSection(&clFreeingThread.cs);
			
			if(!c){
				break;
			}
			
			SAFE_FREE(c);
		}
		
		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END
}

void timerHistoryChunkListClear(TimerHistoryChunkList* cl){
	#if 1
	{
		if(!cl->count){
			assert(!cl->head);
			assert(!cl->tail);
			return;
		}

		ATOMIC_INIT_BEGIN;
		{
			if(!clFreeingThread.hThread){
				InitializeCriticalSection(&clFreeingThread.cs);
				clFreeingThread.eventWake = CreateEvent(NULL, FALSE, FALSE, NULL);

				clFreeingThread.hThread = (HANDLE)_beginthreadex(	NULL,
																	0,
																	clFreeingThreadMain,
																	NULL,
																	0,
																	NULL);
			}
		}
		ATOMIC_INIT_END;
		
		EnterCriticalSection(&clFreeingThread.cs);
		
		if(clFreeingThread.cl.tail){
			clFreeingThread.cl.tail->next = cl->head;
		}else{
			clFreeingThread.cl.head = cl->head;
		}
		
		clFreeingThread.cl.tail = cl->tail;
		
		LeaveCriticalSection(&clFreeingThread.cs);
		
		SetEvent(clFreeingThread.eventWake);
		
		cl->head = NULL;
		cl->tail = NULL;
		cl->count = 0;
	}
	#else
	{
		while(cl->head){
			TimerHistoryChunk* next = cl->head->next;

			SAFE_FREE(cl->head);

			cl->head = next;
		}
		
		cl->tail = NULL;
		cl->count = 0;
	}
	#endif
}

static void timerHistoryChunkCreate(TimerHistoryChunkList* cl,
									TimerHistoryChunk** cOut)
{
	const U32			size = sizeof(**cOut);
	TimerHistoryChunk*	c;

	// Nothing over budget, so just allocate it.

	*cOut = c = callocStruct(TimerHistoryChunk);
}

static U32 countEncodingBitCounts[] = {3, 8, 10, 16};

static S32 timerHistoryWriteCount(	BitBufferWriter* bb,
									U32 count)
{
	ARRAY_FOREACH_BEGIN(countEncodingBitCounts, i);
	{
		const U32 bitCount = countEncodingBitCounts[i];

		if(count < BIT(bitCount)){
			return	bbWriteBit(bb, 1) &&
					bbWriteU32(bb, bitCount, count, NULL);
		}
		
		if(!bbWriteBit(bb, 0)){
			return 0;
		}
	}
	ARRAY_FOREACH_END;

	return bbWriteU32(bb, 32, count, NULL);
}

static S32 timerHistoryReadCount(	BitBufferReader* bb,
									U32* countOut)
{
	ARRAY_FOREACH_BEGIN(countEncodingBitCounts, i);
	{
		U32 bitCount = countEncodingBitCounts[i];
		U32 flags;
		
		if(!bbReadBit(bb, &flags)){
			return 0;
		}
		
		if(flags){
			return bbReadU32(bb, bitCount, countOut, NULL);
		}
	}
	ARRAY_FOREACH_END;

	return bbReadU32(bb, 32, countOut, NULL);
}

static S32 timerHistoryWriteCycles(	BitBufferWriter* bb,
									U64 cycles)
{
	if(cycles <= (U64)0xffffffff){
		U32 cycles32 = cycles;
		
		if(cycles32 <= BIT_RANGE(0, 15)){
			if(cycles32 <= BIT_RANGE(0, 7)){
				return	bbWriteU32(bb, 3, 0, NULL) &&
						bbWriteU32(bb, BITS_PER_BYTE, cycles32, NULL);
			}else{
				return	bbWriteU32(bb, 3, 1, NULL) &&
						bbWriteU32(bb, BITS_PER_BYTE * 2, cycles32, NULL);
			}
		}
		else if(cycles32 <= BIT_RANGE(0, 23)){
			return	bbWriteU32(bb, 3, 2, NULL) &&
					bbWriteU32(bb, BITS_PER_BYTE * 3, cycles32, NULL);
		}else{
			return	bbWriteU32(bb, 3, 3, NULL) &&
					bbWriteU32(bb, BITS_PER_BYTE * 4, cycles32, NULL);
		}
	}else{
		U32 cycles32 = cycles >> 32;
		
		if(cycles32 <= BIT_RANGE(0, 15)){
			if(cycles32 <= BIT_RANGE(0, 7)){
				return	bbWriteU32(bb, 3, 4, NULL) &&
						bbWriteU64(bb, BITS_PER_BYTE * 5, cycles, NULL);
			}else{
				return	bbWriteU32(bb, 3, 5, NULL) &&
						bbWriteU64(bb, BITS_PER_BYTE * 6, cycles, NULL);
			}
		}
		else if(cycles32 <= BIT_RANGE(0, 23)){
			return	bbWriteU32(bb, 3, 6, NULL) &&
					bbWriteU64(bb, BITS_PER_BYTE * 7, cycles, NULL);
		}else{
			return	bbWriteU32(bb, 3, 7, NULL) &&
					bbWriteU64(bb, BITS_PER_BYTE * 8, cycles, NULL);
		}
	}
}

static S32 timerHistoryReadCycles(	BitBufferReader* bb,
									U64* cyclesOut)
{
	U32 flags;
	U64 cycles;
	S32 success;
	U32 bitCount;

	if(!bbReadU32(bb, 3, &flags, NULL)){
		return 0;
	}
	
	bitCount = BITS_PER_BYTE * (flags + 1);

	if(bitCount <= 32){
		U32 cycles32;
		success = bbReadU32(bb, bitCount, &cycles32, NULL);
		cycles = cycles32;
	}else{
		success = bbReadU64(bb, bitCount, &cycles, NULL);
	}
	
	if(!success){
		return 0;
	}
	
	if(cyclesOut){
		*cyclesOut = cycles;
	}

	return 1;
}

static S32 timerHistoryReadDeltaToNextFrameIndex(	BitBufferReader* bb,
													U32* deltaToNextFrameIndexOut)
{
	S32 success = 1;
	U32 hasDelta = 0;

	success &= bbReadBit(bb, &hasDelta);
	assert(success);		
	
	if(hasDelta){
		success &= timerHistoryReadCount(bb, deltaToNextFrameIndexOut);
		deltaToNextFrameIndexOut[0]++;
		assert(success);
	}else{
		deltaToNextFrameIndexOut[0] = 1;
	}
	
	return success;
}

static void timerHistoryReadFrame(	const TimerHistoryChunkList* cl,
									const TimerHistoryChunk* c,
									BitBufferReader* bb,
									TimerHistoryFrame* hfOut)
{
	S32 success = 1;
	
	if(cl->flags.hasCyclesBegin){
		PERFINFO_AUTO_START("timerHistoryReadFrame:thread", 1);
	}else{
		PERFINFO_AUTO_START("timerHistoryReadFrame:other", 1);
	}
	
	if(	bb->pos.byte ||
		bb->pos.bit)
	{
		U32 frameIndexDelta = 0;
		
		success &= timerHistoryReadDeltaToNextFrameIndex(bb, &frameIndexDelta);
		assert(success);		

		hfOut->frameIndex += frameIndexDelta;
	}else{
		hfOut->frameIndex = c->frameIndexMin;
	}

	if(cl->flags.hasCount){
		success &= timerHistoryReadCount(bb, &hfOut->count);
		assert(success);		
	}
	
	if(cl->flags.hasCyclesActive){
		success &= timerHistoryReadCycles(bb, &hfOut->cycles.active);
		assert(success);		
	}
	
	if(cl->flags.hasCyclesBlocking){
		success &= timerHistoryReadCycles(bb, &hfOut->cycles.blocking);
		assert(success);		
	}

	if(cl->flags.hasCyclesOther){
		success &= timerHistoryReadCycles(bb, &hfOut->cycles.other);
		assert(success);		
	}

	if(cl->flags.hasCyclesBegin){
		success &= timerHistoryReadCycles(bb, &hfOut->cycles.begin);
		assert(success);		
	}

	if(cl->flags.hasOSCycles){
		success &= timerHistoryReadCycles(bb, &hfOut->cycles.os);
		assert(success);		
	}
	
	if(cl->flags.hasOSTicksUser){
		success &= timerHistoryReadCycles(bb, &hfOut->osTicks.user);
		assert(success);		
	}
	
	if(cl->flags.hasOSTicksKernel){
		success &= timerHistoryReadCycles(bb, &hfOut->osTicks.kernel);
		assert(success);		
	}
	
	assert(success);
	
	PERFINFO_AUTO_STOP();
}

S32 timerHistoryGetExistingFrame(	const TimerHistoryChunkList* cl,
									U32 frameIndex,
									TimerHistoryFrame* hfOut)
{
	TimerHistoryChunk* c;
	
	if(!hfOut){
		return 0;
	}
	
	ZeroStruct(hfOut);

	for(c = cl->tail; c; c = c->prev){
		if(frameIndex >= c->frameIndexMin){
			if(frameIndex <= c->frameIndexMax){
				BitBufferReader bb;
				
				BB_READER_INIT(bb, c->buffer, c->bitsTotal, 0);
				
				while(1){
					timerHistoryReadFrame(cl, c, &bb, hfOut);
					
					if(hfOut->frameIndex == frameIndex){
						return 1;
					}
					else if(hfOut->frameIndex > frameIndex){
						break;
					}
				}
			}
			
			break;
		}
	}
	
	ZeroStruct(hfOut);

	return 0;
}

void timerHistoryFrameAddValues(TimerHistoryChunkList* cl,
								const U32 frameIndex,
								U32 count,
								U64 cyclesActive,
								U64 cyclesBlocking,
								U64 cyclesBegin,
								U64 cyclesOther,
								U64 osCycles,
								U64 osTicksUser,
								U64 osTicksKernel)
{
	//TimerHistoryFrame*		hf;
	TimerHistoryChunk*		cTail = cl->tail;
	BitBufferWriter			bb = {0};
	
	while(1){
		BitBufferPos	posBeforeWrite = {0};
		S32				success = 1;

		if(cTail){
			if(cTail->bitsTotal){
				if(cTail->frameIndexMax == frameIndex){
					BitBufferReader		bbr = {0};
					TimerHistoryFrame	hf = {0};
					
					assert(cTail->bitsLastFrameBegin != cTail->bitsTotal);

					BB_READER_INIT(	bbr,
									cTail->buffer,
									cTail->bitsTotal,
									cTail->bitsLastFrameBegin);

					// Decode last frame, move bitsTotal back, add values, rerun the loop to re-encode.
					
					timerHistoryReadFrame(	cl,
											cTail,
											&bbr,
											&hf);

					cTail->bitsTotal = cTail->bitsLastFrameBegin;

					cTail->frameIndexMax = frameIndex - hf.frameIndex;
					if(cTail->frameIndexMax){
						assert(cTail->frameIndexMax >= cTail->frameIndexMin);
					}
					
					cTail->cyclesMax -= hf.cycles.active + hf.cycles.blocking;

					count += hf.count;
					cyclesActive += hf.cycles.active;
					cyclesBlocking += hf.cycles.blocking;
					cyclesBegin += hf.cycles.begin;
					cyclesOther += hf.cycles.other;
					osCycles += hf.cycles.os;
					osTicksUser += hf.osTicks.user;
					osTicksKernel += hf.osTicks.kernel;

					continue;
				}else{
					// Write frame into existing chunk.

					const U32 frameIndexDelta = frameIndex - cTail->frameIndexMax;

					assert(frameIndex > cTail->frameIndexMax);
					
					BB_WRITER_INIT(bb, cTail->buffer, cTail->bitsTotal);
					
					posBeforeWrite = bb.pos;
					
					if(frameIndexDelta == 1){
						success &= bbWriteBit(&bb, 0);
					}else{
						success &=	bbWriteBit(&bb, 1) &&
									timerHistoryWriteCount(&bb, frameIndexDelta - 1);
					}
				}
			}

			if(!cTail->bitsTotal){
				BB_WRITER_INIT(bb, cTail->buffer, 0);
			}
			
			if(cl->flags.hasCount){
				success &= timerHistoryWriteCount(&bb, count);
			}
			
			if(cl->flags.hasCyclesActive){
				success &= timerHistoryWriteCycles(&bb, cyclesActive);
			}
			
			if(cl->flags.hasCyclesBlocking){
				success &= timerHistoryWriteCycles(&bb, cyclesBlocking);
			}

			if(cl->flags.hasCyclesOther){
				success &= timerHistoryWriteCycles(&bb, cyclesOther);
			}

			if(cl->flags.hasCyclesBegin){
				success &= timerHistoryWriteCycles(&bb, cyclesBegin);
			}

			if(cl->flags.hasOSCycles){
				success &= timerHistoryWriteCycles(&bb, osCycles);
			}
			
			if(cl->flags.hasOSTicksUser){
				success &= timerHistoryWriteCycles(&bb, osTicksUser);
			}
			
			if(cl->flags.hasOSTicksKernel){
				success &= timerHistoryWriteCycles(&bb, osTicksKernel);
			}

			if(!success){
				// An empty chunk is too small the fit a single frame, that's just dumb.
				
				assert(cTail->bitsTotal);
			}else{
				// Update cTail and we're done.

				cTail->bitsTotal = bb.pos.byte * BITS_PER_BYTE + bb.pos.bit;

				if(!cTail->frameIndexMin){
					cTail->frameIndexMin = frameIndex;
					cTail->cyclesMin = cyclesBegin;
					cTail->cyclesMax = cyclesBegin + cyclesActive + cyclesBlocking;
				}else{
					MIN1(cTail->cyclesMin, cyclesBegin);
					MAX1(cTail->cyclesMax, cyclesBegin + cyclesActive + cyclesBlocking);
				}

				cTail->frameIndexMax = frameIndex;
				cTail->bitsLastFrameBegin = posBeforeWrite.byte * BITS_PER_BYTE + posBeforeWrite.bit;

				#if VERIFY_ENCODED_FRAMES_AFTER_WRITING
				{
					// Make sure the frame is readable.
					
					BitBufferReader		bbr;
					TimerHistoryFrame	hf = {0};
				
					BB_READER_INIT(	bbr,
									cTail->buffer,
									cTail->bitsTotal,
									posBeforeWrite.byte * BITS_PER_BYTE + posBeforeWrite.bit);
					
					timerHistoryReadFrame(cl, cTail, &bbr, &hf);
				}
				#endif
				
				break;
			}
		}
		
		// Create a new tail.
		
		{
			TimerHistoryChunkListGroup* group = cl->group;
			
			for(; group; group = group->parent){
				group->byteCount += sizeof(*cTail);
			}
		}
		
		if(cTail){
			cTail->next = callocStruct(TimerHistoryChunk);

			cTail->next->prev = cTail;
			cTail = cTail->next;
		}else{
			cTail = callocStruct(TimerHistoryChunk);

			cl->head = cTail;
		}

		cl->tail = cTail;
		cl->count++;
	}
}

void timerHistoryIterate(	TimerHistoryIterateCallback callback,
							void* callbackUserPointer,
							const TimerHistoryChunkList* clCycles,
							const TimerHistoryChunkList* clFrames,
							S32 sx,
							U64 cyclesMin,
							U64 cyclesMax)
{
	const S64					cyclesMinToMaxDelta = cyclesMax - cyclesMin;
	const TimerHistoryChunk*	cCycles = clCycles->tail;
	const TimerHistoryChunk*	cFrames = clFrames->tail;
	BitBufferReader				bbCycles;
	BitBufferReader				bbFrames;
	TimerHistoryFrame			hfCycles = {0};
	TimerHistoryFrame			hfFrame = {0};

	if(	!callback ||
		!cCycles ||
		!clCycles ||
		!cFrames ||
		!clFrames ||
		cyclesMax <= cyclesMin)
	{
		return;
	}

	// Find the earliest frame contains or is later than cyclesMin.

	while(	cyclesMin < cFrames->cyclesMin &&
			cFrames->prev &&
			cyclesMin <= cFrames->prev->cyclesMax)
	{
		cFrames = cFrames->prev;
	}
	
	if(	cFrames->cyclesMax < cyclesMin ||
		cFrames->cyclesMin >= cyclesMax)
	{
		return;
	}

	// Find the first frame that ends after cyclesMin.
	
	BB_READER_INIT(bbFrames, cFrames->buffer, cFrames->bitsTotal, 0);
	
	while(1){
		timerHistoryReadFrame(	clFrames,
								cFrames,
								&bbFrames,
								&hfFrame);

		if(	hfFrame.cycles.begin +
			hfFrame.cycles.active +
			hfFrame.cycles.blocking >
			cyclesMin)
		{
			break;
		}
	}

	// Find the starting cycles chunk.

	while(	hfFrame.frameIndex < cCycles->frameIndexMin &&
			cCycles->prev &&
			hfFrame.frameIndex <= cCycles->prev->frameIndexMax)
	{
		cCycles = cCycles->prev;
	}
	
	if(cCycles->frameIndexMax < hfFrame.frameIndex){
		return;
	}

	BB_READER_INIT(bbCycles, cCycles->buffer, cCycles->bitsTotal, 0);
	
	while(cCycles){
		S32							x0;
		S32							x1;
		IterateTimerHistoryData		ithd = {0};

		do{
			timerHistoryReadFrame(	clCycles,
									cCycles,
									&bbCycles,
									&hfCycles);
		}while(hfCycles.frameIndex < hfFrame.frameIndex);

		// Find the correct frame chunk.

		if(hfCycles.frameIndex > cFrames->frameIndexMax){
			while(	cFrames &&
					hfCycles.frameIndex > cFrames->frameIndexMax)
			{
				cFrames = cFrames->next;
			}
			
			if(!cFrames){
				break;
			}
			
			// Find this frame in the frame chunk.
			
			BB_READER_INIT(bbFrames, cFrames->buffer, cFrames->bitsTotal, 0);

			// Read the first frame.

			timerHistoryReadFrame(	clFrames,
									cFrames,
									&bbFrames,
									&hfFrame);
		}
		
		if(!cFrames){
			break;
		}

		// Find the correct frame index.

		while(hfFrame.frameIndex != hfCycles.frameIndex){
			timerHistoryReadFrame(	clFrames,
									cFrames,
									&bbFrames,
									&hfFrame);
		}

		x1 =	sx *
				(S64)(	hfFrame.cycles.begin +
						hfFrame.cycles.active +
						hfFrame.cycles.blocking -
						cyclesMin) /
				cyclesMinToMaxDelta;

		MINMAX1(x1, 0, sx - 1);

		if(hfFrame.cycles.begin >= cyclesMax){
			break;
		}

		x0 =	sx *
				(S64)(hfFrame.cycles.begin - cyclesMin) /
				cyclesMinToMaxDelta;

		if(x0 < 0){
			x0 = 0;
		}
		else if(x0 >= sx){
			break;
		}

		//ithd.hc = cCycles;
		ithd.hf = &hfCycles;
		ithd.frameIndex = hfCycles.frameIndex;
		ithd.x0 = x0;
		ithd.x1 = x1;
		ithd.frameCyclesBegin = hfFrame.cycles.begin;
		ithd.frameCyclesDelta = hfFrame.cycles.active +
								hfFrame.cycles.blocking;

		if(!callback(&ithd, callbackUserPointer)){
			break;
		}
		
		if(hfCycles.frameIndex == cCycles->frameIndexMax){
			cCycles = cCycles->next;
			
			if(!cCycles){
				break;
			}
			
			BB_READER_INIT(bbCycles, cCycles->buffer, cCycles->bitsTotal, 0);
		}
	}
}

