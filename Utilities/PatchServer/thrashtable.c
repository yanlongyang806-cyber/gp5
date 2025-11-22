#include "assert.h"
#include "earray.h"
#include "thrashtable.h"
#include "timing.h"

#define MAXIMUM_INTERARRIVAL_SECONDS 120
#define MINIMUM_INTERARRIVAL_SECONDS (1.0 / 500.0)
#define INTERARRIVALS_TO_TRACK 8
#define NEWBIE UINT_MAX
#define NEWBIE_SECONDS 10

typedef struct Popularity
{
	U32 ref_count;
	S64 last_used;
	U8 interarrivals[INTERARRIVALS_TO_TRACK];
	U8 counters[INTERARRIVALS_TO_TRACK];
	S64 score;
} Popularity;

typedef struct Thrasher
{
	void * key;
	void * value;
	Popularity popularity;
	U32 size;
	U32 position;
} Thrasher;

typedef struct ThrashTableImp
{
	StashTable stash_table;
	Thrasher ** heap;
	Thrasher ** newbies;
	U64 size;
	U64 size_limit;
	Destructor destructor;
	U8 interarrival_max;
	U8 interarrival_min;
	S64 bonus_increment;
	S64 newbie_ticks;
} ThrashTableImp;

void thrashRemoveSkipFind(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID const void* pKey);
bool thrashMakeRoom(SA_PARAM_NN_VALID ThrashTable table, U32 room);
void thrashCheckNewbies(SA_PARAM_NN_VALID ThrashTable table);

void thrasherFree(SA_PRE_NN_VALID SA_POST_P_FREE Thrasher * thrasher, Destructor destructor);
int thrasherCompare(SA_PARAM_NN_VALID const Thrasher ** pLeft, SA_PARAM_NN_VALID const Thrasher ** pRight);

int popularityCompare(SA_PARAM_NN_VALID const Popularity * popLeft, SA_PARAM_NN_VALID const Popularity * popRight);
void popularityInit(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID Popularity * pop);
void popularityHit(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID Popularity * pop);
void popularityRelease(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID Popularity * pop);

void thrashHeapCreate(SA_PARAM_NN_VALID Thrasher *** handle, U32 initialSize);
void thrashHeapRemove(SA_PARAM_NN_VALID Thrasher *** handle, U32 index);
void thrashHeapSwap(SA_PARAM_NN_VALID Thrasher *** handle, U32 i, U32 j);
void thrashHeapPush(SA_PARAM_NN_VALID Thrasher *** handle, SA_PARAM_NN_VALID Thrasher * elem);
bool thrashHeapPop(SA_PARAM_NN_VALID Thrasher *** handle, Thrasher ** elem);
void thrashHeapPercolate(SA_PARAM_NN_VALID Thrasher *** handle, U32 index);

ThrashTable thrashCreateEx(U32 initialCount, StashTableMode stashMode, StashKeyType keyType,
						   U32 keyLength, U64 sizeLimit, ThrashDestructor destructor,
						   F32 maximum_interarrival_seconds, F32 minimum_interarrival_seconds, F32 newbie_seconds)
{
	ThrashTable table;
	S64 cpu, i, iaMin, iaMax;

	// Not all stashtable features are available
	assert( !(stashMode & StashSharedHeap) && !(keyType == StashKeyTypeInts) && !(keyType == StashKeyTypeAddress) &&
		!(keyType == StashKeyTypeExternalFunctions));

	table = calloc(1,sizeof(ThrashTableImp));
	assert(table);

	// The thrashtable deep copies keys outside of the stashtable
	stashMode = stashMode & ~StashDeepCopyKeys_NeverRelease;

	table->stash_table = stashTableCreate(initialCount, stashMode, keyType, keyLength);
	thrashHeapCreate(&table->heap, initialCount);
	table->size = 0;
	table->size_limit = sizeLimit;
	table->destructor = destructor;

	iaMax = 0;
	iaMin = 0;
	cpu = timerCpuSpeed64();
	for(i = (S64)(cpu * maximum_interarrival_seconds); i > 0; i >>= 1)
		iaMax++;
	for(i = (S64)(cpu * minimum_interarrival_seconds); i > 0; i >>= 1)
		iaMin++;
	table->bonus_increment = cpu * maximum_interarrival_seconds / (iaMax - iaMin) / INTERARRIVALS_TO_TRACK;
	table->interarrival_max = iaMax;
	table->interarrival_min = iaMin;
	table->newbie_ticks = (S64)(cpu * newbie_seconds);

	return table;
}

ThrashTable thrashCreate(U32 initialCount, StashTableMode stashMode, StashKeyType keyType,
						 U32 keyLength, U64 sizeLimit, ThrashDestructor destructor)
{
	return thrashCreateEx(initialCount, stashMode, keyType, keyLength, sizeLimit, destructor,
						  MAXIMUM_INTERARRIVAL_SECONDS, MINIMUM_INTERARRIVAL_SECONDS, NEWBIE_SECONDS);
}

void thrashDestroy(ThrashTable table)
{
	int i;
	stashTableDestroy(table->stash_table);
	for(i = 1; i < eaSize(&table->heap); i++)
		thrasherFree(table->heap[i], table->destructor);
	eaDestroy(&table->newbies);
	eaDestroy(&table->heap);
	free(table);
}

void thrashClear(ThrashTable table)
{
	int i;
	stashTableClear(table->stash_table);
	for(i = 1; i < eaSize(&table->heap); i++)
		thrasherFree(table->heap[i], table->destructor);
	eaSetSize(&table->heap, 1);
	eaDestroy(&table->newbies);
	table->size = 0;
}

U64 thrashSize(ThrashTable table)
{
	return table->size;
}

U64 thrashSizeLimit(ThrashTable table)
{
	return table->size_limit;
}

bool thrashAdd(ThrashTable table, const void* pKey, void* pValue, U32 size, bool bOverwriteIfFound)
{
	Thrasher * elem;
	U32 key_len;

	if(stashFindPointer(table->stash_table, pKey, &elem))
	{
		if(!bOverwriteIfFound)
			return false;
		else
			thrashRemoveSkipFind(table, pKey);
	}

	thrashCheckNewbies(table);

	if(size + table->size > table->size_limit)
	{
		static U32 last_alerted = 0;
		const U32 warn_duration = 60*60*24*7;
		U32 now = timeSecondsSince2000();
		if (now - last_alerted >= warn_duration)
		{
			ErrorOrAlert("THRASHTABLE_FULL", "Allocation of size %lu and existing cache of size %"FORM_LL"u exceeds cache size limit %"FORM_LL"u.  (warn duration %lu seconds)",
				size, table->size, table->size_limit, warn_duration);
			last_alerted = now;
		}
		if(!thrashMakeRoom(table, size + table->size - table->size_limit))
		{
			return false;
		}
	}

	elem = malloc(sizeof(Thrasher));

	// For some reason, maybe a good one, the stash key length uses strlen without an addition for the null terminator.
	// To avoid trouble, 2 extra bytes (it can use wide char strings) are added on.
	key_len = stashGetKeyLength(table->stash_table, pKey);
	elem->key = malloc(key_len + 2);
	memset((U8*)elem->key + key_len, 0, 2);
	memcpy(elem->key, pKey, key_len);

	elem->value = pValue;
	elem->size = size;
	popularityInit(table, &elem->popularity);

	table->size += size;
	stashAddPointer(table->stash_table, elem->key, elem, false);
	thrashHeapPush(&table->heap, elem);
	eaPush(&table->newbies, elem);

	return true;
}

bool thrashRelease(ThrashTable table, const void* pKey)
{
	Thrasher * found;

	if(stashFindPointer(table->stash_table, pKey, &found))
	{
		popularityRelease(table, &found->popularity);
		thrashHeapPercolate(&table->heap, found->position);
		return true;
	}
	else
	{
		return false;
	}
}

bool thrashFind(ThrashTable table, const void* pKey, void** ppValue)
{
	Thrasher * found;

	if(stashFindPointer(table->stash_table, pKey, &found))
	{
		if(found->popularity.ref_count == NEWBIE)
			eaFindAndRemove(&table->newbies, found);
		popularityHit(table, &found->popularity);
		thrashHeapPercolate(&table->heap, found->position);
		if(ppValue)
			*ppValue = found->value;
		return true;
	}
	else
	{
		return false;
	}
}

bool thrashRemove(ThrashTable table, const void* pKey)
{
	Thrasher * found;

	if(stashFindPointer(table->stash_table, pKey, &found))
	{
		thrashRemoveSkipFind(table, pKey);
		return true;
	}
	else
	{
		return false;
	}
}

void thrashSortedScan(ThrashTable table, ThrashSortedScanner scanner, void * userData)
{
	Thrasher ** heap = NULL;
	int i;

	eaCopy(&heap, &table->heap);
	eaRemoveFast(&heap, 0);
	eaQSort(heap, thrasherCompare);
	for(i = 0; i < eaSize(&heap); i++)
		scanner(heap[i]->key, heap[i]->value, i, heap[i]->popularity.last_used, heap[i]->popularity.score, userData);
	eaDestroy(&heap);
}

static void thrashCheckNewbies(ThrashTable table)
{
	S64 ticks = timerCpuTicks64();
	int i;

	for(i = 0; i < eaSize(&table->newbies); i++)
	{
		Thrasher * newb = table->newbies[i];

		if(ticks - newb->popularity.last_used > table->newbie_ticks)
		{
			if(newb->popularity.ref_count == NEWBIE)
			{
				newb->popularity.ref_count = 0;
				thrashHeapPercolate(&table->heap, newb->position);
			}
		}
		else
			break;
	}
	while(i)
		eaRemove(&table->newbies, --i);
}

static void thrashRemoveSkipFind(ThrashTable table, const void* pKey)
{
	bool stash_ret;
	Thrasher * found;

	stash_ret = stashRemovePointer(table->stash_table, pKey, &found);
	assert(stash_ret);

	table->size -= found->size;
	if(found->position > 0) // Might have been popped already
		thrashHeapRemove(&table->heap, found->position);

	if(found->popularity.ref_count == NEWBIE)
		eaFindAndRemove(&table->newbies, found);

	thrasherFree(found, table->destructor);
}

static bool thrashMakeRoom(ThrashTable table, U32 room)
{
	Thrasher * popped;
	Thrasher ** newbies = NULL;
	U32 freed = 0;

	while(room > freed)
	{
		if(thrashHeapPop(&table->heap, &popped))
		{
			freed += popped->size;
			thrashRemoveSkipFind(table, popped->key);
		}
		else
		{
			return false;
		}
	}

	return true;
}

static void popularityInit(ThrashTable table, Popularity * pop)
{
	int i;

	for(i = 0; i < INTERARRIVALS_TO_TRACK; i++)
	{
		pop->counters[i] = 1 << i;
		pop->interarrivals[i] = table->interarrival_max;
	}
	pop->last_used = timerCpuTicks64();
	pop->score = pop->last_used;
	pop->ref_count = NEWBIE;
}

static void popularityHit(ThrashTable table, Popularity * pop)
{
	int i;
	U8 new_interarrival = 0;
	S64 curr = timerCpuTicks64();
	S64 diff = curr - pop->last_used;
	S64 bonus;

	pop->last_used = curr;

	for(; diff > 0 && new_interarrival < table->interarrival_max; diff = diff >> 1)
		new_interarrival++;

	if(new_interarrival >= table->interarrival_min)
	{
		for(i = INTERARRIVALS_TO_TRACK - 1; i >= 0; i--)
		{
			if(--(pop->counters[i]) == 0)
			{
				pop->counters[i] = 1 << i;
				if(i != 0)
				{
					ANALYSIS_ASSUME(i > 0);
#pragma warning(suppress:6201) // /analyze not paying attention to ANALYSIS_ASSUME
					pop->interarrivals[i] = pop->interarrivals[i-1];
				}
				else
					pop->interarrivals[0] = new_interarrival;
			}
		}
	}

	pop->score = pop->last_used;
	bonus = table->bonus_increment * table->interarrival_max * INTERARRIVALS_TO_TRACK;
	for(i = 0; i < INTERARRIVALS_TO_TRACK; i++)
		bonus -= table->bonus_increment * pop->interarrivals[i];
	pop->score += bonus;
	if(pop->ref_count == NEWBIE)
	{
		pop->ref_count = 1;
	}
	else
		pop->ref_count = (pop->ref_count + 1 == NEWBIE) ? pop->ref_count : (pop->ref_count + 1);
}

void popularityRelease(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID Popularity * pop)
{
	if(pop->ref_count == NEWBIE || pop->ref_count == 0)
		pop->ref_count = 0;
	else
		pop->ref_count -= 1;
}

static int popularityCompare(const Popularity * popLeft, const Popularity * popRight)
{
	int refLeft  = (popLeft->ref_count  == 0) ? -1 : ( (popLeft->ref_count  == NEWBIE) ? 0 : 1 );
	int refRight = (popRight->ref_count == 0) ? -1 : ( (popRight->ref_count == NEWBIE) ? 0 : 1 );

	if(refLeft < refRight)
		return 1;
	else if(refLeft > refRight)
		return -1;
	else if(popLeft->score < popRight->score)
		return 1;
	else if(popLeft->score > popRight->score)
		return -1;
	else
		return 0;
}

static int thrasherCompare(const Thrasher ** tLeft, const Thrasher ** tRight)
{
	return popularityCompare(&(*tLeft)->popularity, &(*tRight)->popularity);
}

static void thrasherFree(Thrasher * thrasher, Destructor destructor)
{
	free(thrasher->key);
	if(destructor)
		destructor(thrasher->value);
	else
		free(thrasher->value);
	free(thrasher);
}

static void thrashHeapCreate(Thrasher *** handle, U32 initialSize)
{
	assert(*handle == NULL);
	eaSetCapacity(handle, initialSize + 1);
	// dummy on the front of the heap
	eaPush(handle, NULL);
}

static void thrashHeapSwap(Thrasher *** handle, U32 i, U32 j)
{
	((*handle)[i])->position = j;
	((*handle)[j])->position = i;
	eaSwap(handle, i, j);
}

static void thrashHeapRemove(Thrasher *** handle, U32 index)
{
	U32 last;
	if(index > 0)
	{
		last = eaSize(handle) - 1;
		if(last > index)
		{
			thrashHeapSwap(handle, index, last);
			eaRemoveFast(handle, last);
			thrashHeapPercolate(handle, index);
		}
		else
		{
			eaRemoveFast(handle, index);
		}
	}
}

static bool thrashHeapPop(Thrasher *** handle, Thrasher ** elem)
{
	U32 size = eaSize(handle);

	if(size > 1)
	{
		*elem = (*handle)[1];
		thrashHeapRemove(handle, 1);
		(*elem)->position = 0;
		return true;
	}
	else
	{
		*elem = NULL;
		return false;
	}
}

static void thrashHeapPush(Thrasher *** handle, Thrasher * elem)
{
	U32 index = eaPush(handle, elem);
	elem->position = index;
	thrashHeapPercolate(handle, index);
}

static void thrashHeapPercolate(Thrasher *** handle, U32 index)
{
	U32 parent, lchild, rchild, size, bigchild;
	Thrasher ** heap = *handle;
	bool movingUp = false;
	bool movingDown = false;

	assert(heap);
	size = eaSize(handle);
	parent = index >> 1;
	lchild = index << 1;
	rchild = lchild + 1;
	
	if(parent > 0 && popularityCompare(&(heap[parent]->popularity), &(heap[index]->popularity)) < 0)
	{
		movingUp = true;
	}
	else if(lchild < size)
	{
		bigchild = lchild;
		if(rchild < size && popularityCompare(&(heap[lchild]->popularity), &(heap[rchild]->popularity)) < 0)
			bigchild = rchild;
		if(popularityCompare(&(heap[index]->popularity), &(heap[bigchild]->popularity)) < 0)
			movingDown = true;
	}

	while(movingUp)
	{
		thrashHeapSwap(handle, index, parent);
		index = parent;
		parent = index >> 1;
		if(parent > 0 && popularityCompare(&(heap[parent]->popularity), &(heap[index]->popularity)) < 0)
			continue;

		return;
	}

	while(movingDown)
	{
		thrashHeapSwap(handle, index, bigchild);
		index = bigchild;
		lchild = index << 1;
		rchild = lchild + 1;
		if(lchild < size)
		{
			bigchild = lchild;
			if(rchild < size && popularityCompare(&(heap[lchild]->popularity), &(heap[rchild]->popularity)) < 0)
				bigchild = rchild;
			if(popularityCompare(&(heap[index]->popularity), &(heap[bigchild]->popularity)) < 0)
				continue;
		}

		return;
	}
}

S32 thrashRemoveOld(SA_PARAM_NN_VALID ThrashTable table,
					F32 maxSecondsToKeep,
					F32 maxSecondsToSpend)
{
	U32 timer = timerAlloc();
	S64 curTicks = timerCpuTicks64();
	S64 maxTicks = maxSecondsToKeep * timerCpuSpeed64();
	S32 removedSomething = 0;
	
	while(1)
	{
		S32 lastUsedDiff = 0;
		U32 lastUsedIndex;
		
		EARRAY_CONST_FOREACH_BEGIN_FROM(table->heap, i, isize, 1);
			Thrasher*	t = table->heap[i];
			S64			diff = curTicks - t->popularity.last_used;
			
			if(	diff >= maxTicks &&
				diff > lastUsedDiff)
			{
				lastUsedDiff = diff;
				lastUsedIndex = i;
			}
		EARRAY_FOREACH_END;
		
		if(lastUsedDiff){
			Thrasher* t = table->heap[lastUsedIndex];
			
			thrashHeapRemove(&table->heap, lastUsedIndex);
			t->position = 0;
			thrashRemoveSkipFind(table, t->key);
			
			removedSomething = 1;
		}else{
			break;
		}
		
		if(	maxSecondsToSpend > 0.f &&
			timerElapsed(timer) >= maxSecondsToSpend)
		{
			break;
		}
	}
	
	timerFree(timer);
	
	return removedSomething;
}
