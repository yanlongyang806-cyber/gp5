#pragma once

#include "heap.h"

typedef struct HOSI HOSI;

typedef struct CrypticHeap
{
	HeapAddress uBaseAddress;
	HeapFlags flags;
	HeapAccessFunction pAccessor;
	void * pAccessorUserData;
	HOSI * hosi;
} CrypticHeap;

typedef struct CrypticHeapSegment
{
	HeapAddress uBaseAddress;
	HeapSize uLargestUnCommittedRange;
	unsigned int uNumberOfPages;
	unsigned int uNumberOfUnCommittedPages;
	unsigned int uNumberOfUnCommittedRanges;
} CrypticHeapSegment;

typedef struct CrypticHeapVirtualAlloc
{
	HeapAddress uBaseAddress;
} CrypticHeapVirtualAlloc;

typedef struct CrypticHeapFreeList
{
	HeapAddress uBaseAddress;
	HeapAddress uFirstEntry;
	HeapAddress uLastEntry;
} CrypticHeapFreeList;

typedef struct CrypticHeapCache
{
	HeapAddress uBaseAddress;
	HeapSize uSize;
} CrypticHeapCache;

typedef struct CrypticHeapBlock
{
	HeapAddress uBaseAddress;
	HeapSize uSize;
	bool bBusy;
	bool bCommitted;
} CrypticHeapBlock;

typedef struct CrypticHeapFreeEntry
{
	HeapAddress uBaseAddress;
	HeapSize uSize;
} CrypticHeapFreeEntry;

typedef U64 CrypticHeapIterator;

typedef CrypticHeapIterator (*HOSI_IteratorBegin)(CrypticHeap * pHeap);
typedef CrypticHeapIterator (*HOSI_IteratorNext)(CrypticHeap * pHeap, CrypticHeapIterator iter);
typedef CrypticHeapIterator (*HOSI_IteratorEnd)(CrypticHeap * pHeap);

typedef CrypticHeapSegment * (*HOSI_SegmentGet)(CrypticHeap * pHeap, CrypticHeapIterator iter);
typedef void (*HOSI_SegmentFree)(CrypticHeap * pHeap, CrypticHeapSegment * pSegment);

typedef CrypticHeapVirtualAlloc * (*HOSI_VirtualAllocGet)(CrypticHeap * pHeap, CrypticHeapIterator iter);
typedef void (*HOSI_VirtualAllocFree)(CrypticHeap * pHeap, CrypticHeapVirtualAlloc * pVirtualAlloc);

typedef CrypticHeapFreeList * (*HOSI_FreeListGet)(CrypticHeap * pHeap, CrypticHeapIterator iter);
typedef void (*HOSI_FreeListFree)(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList);

typedef CrypticHeapCache * (*HOSI_CacheGet)(CrypticHeap * pHeap, CrypticHeapIterator iter);
typedef void (*HOSI_CacheFree)(CrypticHeap * pHeap, CrypticHeapCache * pCache);

typedef CrypticHeapIterator (*HOSI_SegmentIteratorBegin)(CrypticHeap * pHeap, CrypticHeapSegment * pSegment);
typedef CrypticHeapIterator (*HOSI_SegmentIteratorNext)(CrypticHeap * pHeap, CrypticHeapSegment * pSegment, CrypticHeapIterator iter);
typedef CrypticHeapIterator (*HOSI_SegmentIteratorEnd)(CrypticHeap * pHeap, CrypticHeapSegment * pSegment);

typedef CrypticHeapBlock * (*HOSI_BlockGet)(CrypticHeap * pHeap, CrypticHeapSegment * pSegment, CrypticHeapIterator iter);
typedef void (*HOSI_BlockFree)(CrypticHeap * pHeap, CrypticHeapSegment * pSegment, CrypticHeapBlock * pBlock);

typedef CrypticHeapIterator (*HOSI_FreeListIteratorBegin)(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList);
typedef CrypticHeapIterator (*HOSI_FreeListIteratorNext)(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList, CrypticHeapIterator iter);
typedef CrypticHeapIterator (*HOSI_FreeListIteratorEnd)(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList);

typedef CrypticHeapFreeEntry * (*HOSI_FreeEntryGet)(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList, CrypticHeapIterator iter);
typedef void (*HOSI_FreeEntryFree)(CrypticHeap * pHeap, CrypticHeapFreeList * pFreeList, CrypticHeapFreeEntry * pFreeEntry);

typedef struct HOSI
{
	HOSI_IteratorBegin segment_begin;
	HOSI_IteratorNext segment_next;
	HOSI_IteratorEnd segment_end;
	HOSI_SegmentGet segment_get;
	HOSI_SegmentFree segment_free;

	HOSI_IteratorBegin virtualalloc_begin;
	HOSI_IteratorNext virtualalloc_next;
	HOSI_IteratorEnd virtualalloc_end;
	HOSI_VirtualAllocGet virtualalloc_get;
	HOSI_VirtualAllocFree virtualalloc_free;

	HOSI_IteratorBegin freelist_begin;
	HOSI_IteratorNext freelist_next;
	HOSI_IteratorEnd freelist_end;
	HOSI_FreeListGet freelist_get;
	HOSI_FreeListFree freelist_free;

	HOSI_IteratorBegin cache_begin;
	HOSI_IteratorNext cache_next;
	HOSI_IteratorEnd cache_end;
	HOSI_CacheGet cache_get;
	HOSI_CacheFree cache_free;

	HOSI_SegmentIteratorBegin block_begin;
	HOSI_SegmentIteratorNext block_next;
	HOSI_SegmentIteratorEnd block_end;
	HOSI_BlockGet block_get;
	HOSI_BlockFree block_free;

	HOSI_FreeListIteratorBegin freeentry_begin;
	HOSI_FreeListIteratorNext freeentry_next;
	HOSI_FreeListIteratorEnd freeentry_end;
	HOSI_FreeEntryGet freeentry_get;
	HOSI_FreeEntryFree freeentry_free;
} HOSI;