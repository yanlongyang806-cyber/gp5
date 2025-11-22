#include "Win2003x64.h"
#include "StashTable.h"

#pragma pack(push, heap, 8)
typedef struct Win2003x64_HeapEntry // _HEAP_ENTRY
{
	U64 uPrevBlockPrivate;
	U16 uSize;
	U16 uPreviousSize;
	U8 uSmallTagIndex;
	U8 uFlags;
	U8 uUnusedBytes;
	U8 uSegmentIndex;
} Win2003x64_HeapEntry;
typedef struct Win2003x64_VirtualAlloc // This may be wrong (gotten off the intertubes)
{
	U64 uNext;
	U64 uPrev;
	U64 uCommitSize;
	U64 uReserveSize;
	Win2003x64_HeapEntry BusyBlock;
} Win2003x64_VirtualAlloc;
typedef struct Win2003x64_HeapSegment // _HEAP_SEGMENT
{
	Win2003x64_HeapEntry entry;
	char signature[4];
	U32 uFlags;
	U64 uHeap;
	U64 uLargestUnCommittedRange;
	U64 uBaseAddress;
	U32 uNumberOfPages;
	U64 uFirstEntry;
	U64 uLastValidEntry;
	U32 uNumberOfUnCommittedPages;
	U32 uNumberOfUnCommittedRanges;
	U64 uUncommittedRanges;
	U16 uAllocatorBackTraceIndex;
	U16 uReserved;
	U64 uLastEntryInSegment;
} Win2003x64_HeapSegment;
typedef struct Win2003x64_UncommittedRange // _HEAP_UNCOMMMTTED_RANGE
{
	U64 uNext;
	U64 uAddress;
	U64 uSize;
	U32 uFiller;
} Win2003x64_UncommittedRange;
#pragma pack(pop, heap)

typedef struct Win2003x64_CrypticHeapSegmentWrapper
{
	CrypticHeapSegment segment;
	StashTable stUncommittedRanges;
	HeapAddress uFirstEntry;
	HeapAddress uLastEntry;
} Win2003x64_CrypticHeapSegmentWrapper;

static CrypticHeapIterator Win2003x64_SegmentBegin(CrypticHeap * pHeap)
{
	return 0;
}

static CrypticHeapIterator Win2003x64_SegmentNext(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	return iter + 1;
}

static CrypticHeapIterator Win2003x64_SegmentEnd(CrypticHeap * pHeap)
{
	HeapAddress uLastSegmentOffset = 0xAE3;
	U8 uLastIndex = 0;

	verify(pHeap->pAccessor(pHeap->uBaseAddress + uLastSegmentOffset,
		&uLastIndex, 1, pHeap->pAccessorUserData));

	return uLastIndex + 1;
}

static CrypticHeapSegment * Win2003x64_SegmentGet(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	HeapAddress uSegmentOffset = 0xA0;
	Win2003x64_HeapSegment segment = {0};
	Win2003x64_CrypticHeapSegmentWrapper * pSegment = callocStruct(pSegment, Win2003x64_CrypticHeapSegmentWrapper);
	HeapAddress uCurRange = 0;
	unsigned int uCurRangeIdx = 0;

	verify(pHeap->pAccessor(pHeap->uBaseAddress + uSegmentOffset + iter * 8,
		&pSegment->segment.uBaseAddress, 8, pHeap->pAccessorUserData));

	if (!pSegment->segment.uBaseAddress) return (CrypticHeapSegment*)pSegment;

	verify(pHeap->pAccessor(pSegment->segment.uBaseAddress, &segment, sizeof(Win2003x64_HeapSegment), pHeap->pAccessorUserData));

	pSegment->segment.uLargestUnCommittedRange = segment.uLargestUnCommittedRange;
	pSegment->segment.uNumberOfPages = segment.uNumberOfPages;
	pSegment->segment.uNumberOfUnCommittedPages = segment.uNumberOfUnCommittedPages;
	pSegment->segment.uNumberOfUnCommittedRanges = segment.uNumberOfUnCommittedRanges;
	pSegment->uFirstEntry = segment.uFirstEntry;
	pSegment->uLastEntry = segment.uLastValidEntry;

	pSegment->stUncommittedRanges = stashTableCreateFixedSize(segment.uNumberOfUnCommittedRanges, sizeof(HeapAddress));

	uCurRange = segment.uUncommittedRanges;
	for (uCurRangeIdx = 0; uCurRangeIdx < segment.uNumberOfUnCommittedRanges; uCurRangeIdx++)
	{
		Win2003x64_UncommittedRange range = {0};
		HeapAddress * pAddress = malloc(sizeof(HeapAddress));
		HeapSize * pSize = malloc(sizeof(HeapSize));

		verify(pHeap->pAccessor(uCurRange, &range, sizeof(Win2003x64_UncommittedRange), pHeap->pAccessorUserData));

		*pAddress = range.uAddress;
		*pSize = range.uSize;

		stashAddPointer(pSegment->stUncommittedRanges, pAddress, pSize, false);

		uCurRange = range.uNext;
	}

	return (CrypticHeapSegment*)pSegment;
}

static void Win2003x64_StashTableFreeFunc(void * data)
{
	free(data);
}

static void Win2003x64_SegmentFree(CrypticHeap * pHeap, CrypticHeapSegment * pSegment)
{
	Win2003x64_CrypticHeapSegmentWrapper * pSegmentWrapper = (Win2003x64_CrypticHeapSegmentWrapper *)pSegment;
	if (pSegmentWrapper->stUncommittedRanges)
	{
		stashTableDestroyEx(pSegmentWrapper->stUncommittedRanges, Win2003x64_StashTableFreeFunc, Win2003x64_StashTableFreeFunc);
	}
	free(pSegment);
}

static CrypticHeapIterator Win2003x64_VirtualAllocBegin(CrypticHeap * pHeap)
{
	HeapAddress uVirtAllocOffset = 0x90;
	HeapAddress uVirtAlloc = 0;

	verify(pHeap->pAccessor(pHeap->uBaseAddress + uVirtAllocOffset, &uVirtAlloc, 8, pHeap->pAccessorUserData));

	if (uVirtAlloc == pHeap->uBaseAddress + uVirtAllocOffset) return 0;

	return uVirtAlloc;
}

static CrypticHeapIterator Win2003x64_VirtualAllocNext(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	Win2003x64_VirtualAlloc virtualAlloc = {0};

	verify(pHeap->pAccessor(iter, &virtualAlloc, sizeof(Win2003x64_VirtualAlloc), pHeap->pAccessorUserData));

	return virtualAlloc.uNext;
}

static CrypticHeapIterator Win2003x64_VirtualAllocEnd(CrypticHeap * pHeap)
{
	HeapAddress uVirtAllocOffset = 0x98;
	HeapAddress uVirtAlloc = 0;

	if (Win2003x64_VirtualAllocBegin(pHeap) == 0) return 0;

	verify(pHeap->pAccessor(pHeap->uBaseAddress + uVirtAllocOffset, &uVirtAlloc, 8, pHeap->pAccessorUserData));

	return uVirtAlloc;
}

static CrypticHeapVirtualAlloc * Win2003x64_VirtualAllocGet(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	CrypticHeapVirtualAlloc * pVirtualAlloc = callocStruct(pVirtualAlloc, CrypticHeapVirtualAlloc);
	pVirtualAlloc->uBaseAddress = iter;
	return pVirtualAlloc;
}

static void Win2003x64_VirtualAllocFree(CrypticHeap * pHeap, CrypticHeapVirtualAlloc * pVirtualAlloc)
{
	free(pVirtualAlloc);
}

static CrypticHeapIterator Win2003x64_FreeListBegin(CrypticHeap * pHeap)
{
	return 0;
}

static CrypticHeapIterator Win2003x64_FreeListNext(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	return iter + 1;
}

static CrypticHeapIterator Win2003x64_FreeListEnd(CrypticHeap * pHeap)
{
	return 128;
}

static CrypticHeapFreeList * Win2003x64_FreeListGet(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	CrypticHeapFreeList * pFreeList = callocStruct(pFreeList, CrypticHeapFreeList);
	HeapAddress uFreeListOffset = 0x2c8;

	pFreeList->uBaseAddress = pHeap->uBaseAddress + uFreeListOffset + iter * 8 * 2;

	verify(pHeap->pAccessor(pFreeList->uBaseAddress,
		&pFreeList->uFirstEntry, 8, pHeap->pAccessorUserData));

	if (pFreeList->uFirstEntry == pFreeList->uBaseAddress)
	{
		pFreeList->uFirstEntry = 0;
		pFreeList->uLastEntry = 0;
	}
	else
	{
		verify(pHeap->pAccessor(pFreeList->uBaseAddress + 8,
			&pFreeList->uLastEntry, 8, pHeap->pAccessorUserData));
	}

	return pFreeList;
}

static void Win2003x64_FreeListFree(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList)
{
	free(pFreeList);
}

static CrypticHeapIterator Win2003x64_CacheBegin(CrypticHeap * pHeap)
{
	return 0;
}

static CrypticHeapIterator Win2003x64_CacheNext(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	return iter + 1;
}

static CrypticHeapIterator Win2003x64_CacheEnd(CrypticHeap * pHeap)
{
	return 0x480;
}

static CrypticHeapCache * Win2003x64_CacheGet(CrypticHeap * pHeap, CrypticHeapIterator iter)
{
	const HeapAddress uCacheAddressOffset = 0x2b8;
	const HeapAddress uCacheOffset = 0x70;
	Win2003x64_HeapEntry block = {0};
	CrypticHeapCache * pCache = callocStruct(pCache, CrypticHeapCache);
	HeapAddress uCacheAddress = 0;

	verify(pHeap->pAccessor(pHeap->uBaseAddress + uCacheAddressOffset, &uCacheAddress, 8, pHeap->pAccessorUserData));

	if (!uCacheAddress) return pCache;

	verify(pHeap->pAccessor(uCacheAddress + uCacheOffset + iter * 8, &pCache->uBaseAddress, 8, pHeap->pAccessorUserData));

	if (!pCache->uBaseAddress) return pCache;

	verify(pHeap->pAccessor(pCache->uBaseAddress,
		&block, sizeof(Win2003x64_HeapEntry), pHeap->pAccessorUserData));

	pCache->uBaseAddress += sizeof(Win2003x64_HeapEntry);
	pCache->uSize = block.uSize * sizeof(Win2003x64_HeapEntry);

	return pCache;
}

static void Win2003x64_CacheFree(CrypticHeap * pHeap, CrypticHeapCache * pCache)
{
	free(pCache);
}

static CrypticHeapIterator Win2003x64_BlockBegin(CrypticHeap * pHeap, CrypticHeapSegment * pSegment)
{
	Win2003x64_CrypticHeapSegmentWrapper * pSegmentWrapper = (Win2003x64_CrypticHeapSegmentWrapper *)pSegment;
	return pSegmentWrapper->uFirstEntry;
}

static CrypticHeapIterator Win2003x64_BlockNext(CrypticHeap * pHeap, CrypticHeapSegment * pSegment, CrypticHeapIterator iter)
{
	Win2003x64_CrypticHeapSegmentWrapper * pSegmentWrapper = (Win2003x64_CrypticHeapSegmentWrapper *)pSegment;
	Win2003x64_HeapEntry block = {0};
	HeapSize * pSize = NULL;

	if (stashFindPointer(pSegmentWrapper->stUncommittedRanges, &iter, &pSize))
	{
		return iter + *pSize;
	}

	verify(pHeap->pAccessor(iter, &block, sizeof(Win2003x64_HeapEntry), pHeap->pAccessorUserData));

	return iter + block.uSize * sizeof(Win2003x64_HeapEntry);
}

static CrypticHeapIterator Win2003x64_BlockEnd(CrypticHeap * pHeap, CrypticHeapSegment * pSegment)
{
	Win2003x64_CrypticHeapSegmentWrapper * pSegmentWrapper = (Win2003x64_CrypticHeapSegmentWrapper *)pSegment;
	return pSegmentWrapper->uLastEntry;
}

static CrypticHeapBlock * Win2003x64_BlockGet(CrypticHeap * pHeap, CrypticHeapSegment * pSegment, CrypticHeapIterator iter)
{
	Win2003x64_CrypticHeapSegmentWrapper * pSegmentWrapper = (Win2003x64_CrypticHeapSegmentWrapper *)pSegment;
	CrypticHeapBlock * pBlock = callocStruct(pBlock, CrypticHeapBlock);
	Win2003x64_HeapEntry block = {0};
	HeapSize * pSize = NULL;

	if (stashFindPointer(pSegmentWrapper->stUncommittedRanges, &iter, &pSize))
	{
		pBlock->uSize = *pSize * sizeof(Win2003x64_HeapEntry);
		pBlock->uBaseAddress = iter;
		pBlock->bCommitted = false;
		pBlock->bBusy = false;
		return pBlock;
	}

	verify(pHeap->pAccessor(iter, &block, sizeof(Win2003x64_HeapEntry), pHeap->pAccessorUserData));

	pBlock->uSize = block.uSize * sizeof(Win2003x64_HeapEntry) - sizeof(Win2003x64_HeapEntry);
	pBlock->uBaseAddress = iter + sizeof(Win2003x64_HeapEntry);
	pBlock->bCommitted = true;
	pBlock->bBusy = block.uFlags & 0x1;

	return pBlock;
}

static void Win2003x64_BlockFree(CrypticHeap * pHeap, CrypticHeapSegment * pSegment, CrypticHeapBlock * pBlock)
{
	free(pBlock);
}

static CrypticHeapIterator Win2003x64_FreeEntryBegin(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList)
{
	return pFreeList->uFirstEntry;
}

static CrypticHeapIterator Win2003x64_FreeEntryNext(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList, CrypticHeapIterator iter)
{
	verify(pHeap->pAccessor(iter, &iter, 8, pHeap->pAccessorUserData));
	return iter;
}

static CrypticHeapIterator Win2003x64_FreeEntryEnd(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList)
{
	if (!pFreeList->uFirstEntry) return 0;
	return pFreeList->uBaseAddress;
}

static CrypticHeapFreeEntry * Win2003x64_FreeEntryGet(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList, CrypticHeapIterator iter)
{
	Win2003x64_HeapEntry block = {0};
	CrypticHeapFreeEntry * pFreeEntry = callocStruct(pFreeEntry, CrypticHeapFreeEntry);

	verify(pHeap->pAccessor(iter - sizeof(Win2003x64_HeapEntry), &block, sizeof(Win2003x64_HeapEntry), pHeap->pAccessorUserData));

	pFreeEntry->uBaseAddress = iter;
	pFreeEntry->uSize = block.uSize * sizeof(Win2003x64_HeapEntry);
	return pFreeEntry;
}

static void Win2003x64_FreeEntryFree(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList, CrypticHeapFreeEntry * pFreeEntry)
{
	free(pFreeEntry);
}

HOSI * HOSI_Win2003x64(void)
{
	static HOSI hosi = {0};
	hosi.segment_begin = Win2003x64_SegmentBegin;
	hosi.segment_next = Win2003x64_SegmentNext;
	hosi.segment_end = Win2003x64_SegmentEnd;
	hosi.segment_get = Win2003x64_SegmentGet;
	hosi.segment_free = Win2003x64_SegmentFree;

	hosi.virtualalloc_begin = Win2003x64_VirtualAllocBegin;
	hosi.virtualalloc_next = Win2003x64_VirtualAllocNext;
	hosi.virtualalloc_end = Win2003x64_VirtualAllocEnd;
	hosi.virtualalloc_get = Win2003x64_VirtualAllocGet;
	hosi.virtualalloc_free = Win2003x64_VirtualAllocFree;

	hosi.freelist_begin = Win2003x64_FreeListBegin;
	hosi.freelist_next = Win2003x64_FreeListNext;
	hosi.freelist_end = Win2003x64_FreeListEnd;
	hosi.freelist_get = Win2003x64_FreeListGet;
	hosi.freelist_free = Win2003x64_FreeListFree;

	hosi.cache_begin = Win2003x64_CacheBegin;
	hosi.cache_next = Win2003x64_CacheNext;
	hosi.cache_end = Win2003x64_CacheEnd;
	hosi.cache_get = Win2003x64_CacheGet;
	hosi.cache_free = Win2003x64_CacheFree;

	hosi.block_begin = Win2003x64_BlockBegin;
	hosi.block_next = Win2003x64_BlockNext;
	hosi.block_end = Win2003x64_BlockEnd;
	hosi.block_get = Win2003x64_BlockGet;
	hosi.block_free = Win2003x64_BlockFree;

	hosi.freeentry_begin = Win2003x64_FreeEntryBegin;
	hosi.freeentry_next = Win2003x64_FreeEntryNext;
	hosi.freeentry_end = Win2003x64_FreeEntryEnd;
	hosi.freeentry_get = Win2003x64_FreeEntryGet;
	hosi.freeentry_free = Win2003x64_FreeEntryFree;

	return &hosi;
}