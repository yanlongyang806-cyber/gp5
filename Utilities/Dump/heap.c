#include "heap.h"
#include "heapOSInterface.h"
#include "OS/Win2003x64.h"
#include "OS/Win7x64.h"

const unsigned int CrypticAllocMinSize = 6;
const unsigned int CrypticGuard = 0x5D379DF5;

static bool gbCheckFreeList = true;
AUTO_CMD_INT(gbCheckFreeList, CheckFreeList) ACMD_CMDLINE;

static void heap_PrintAddress(CrypticHeap * pHeap, HeapAddress uAddress)
{
	if (pHeap->flags & HF_64_BIT) printf("%016llx", uAddress);
	else printf("%08x", uAddress);
}

static void heap_WalkVirtualAllocations(CrypticHeap * pHeap)
{
	CrypticHeapIterator virtualAllocIter = 0;

	printf("\n\nVirtual Allocations\nBase Address\n");
	for (virtualAllocIter = pHeap->hosi->virtualalloc_begin(pHeap);
		virtualAllocIter != pHeap->hosi->virtualalloc_end(pHeap);
		virtualAllocIter = pHeap->hosi->virtualalloc_next(pHeap, virtualAllocIter))
	{
		CrypticHeapVirtualAlloc * pVirtualAlloc = pHeap->hosi->virtualalloc_get(pHeap, virtualAllocIter);
		heap_PrintAddress(pHeap, pVirtualAlloc->uBaseAddress);
		printf("\n");
		pHeap->hosi->virtualalloc_free(pHeap, pVirtualAlloc);
	}
}

static void heap_WalkFreeLists(CrypticHeap * pHeap)
{
	CrypticHeapIterator freeListIter = 0;
	CrypticHeapIterator cacheIter = pHeap->hosi->cache_begin(pHeap);
	CrypticHeapCache * pCache = pHeap->hosi->cache_get(pHeap, cacheIter);

	while (pCache && !pCache->uBaseAddress)
	{
		pHeap->hosi->cache_free(pHeap, pCache);
		pCache = NULL;
		cacheIter = pHeap->hosi->cache_next(pHeap, cacheIter);
		if (cacheIter == pHeap->hosi->cache_end(pHeap)) break;
		pCache = pHeap->hosi->cache_get(pHeap, cacheIter);
	}

	printf("\n\nFree Lists\nNumber of Entries\n");
	for (freeListIter = pHeap->hosi->freelist_begin(pHeap);
		freeListIter != pHeap->hosi->freelist_end(pHeap);
		freeListIter = pHeap->hosi->freelist_next(pHeap, freeListIter))
	{
		CrypticHeapFreeList * pFreeList = pHeap->hosi->freelist_get(pHeap, freeListIter);
		CrypticHeapIterator freeEntryIter = 0;
		unsigned int uNumEntries = 0;

		for (freeEntryIter = pHeap->hosi->freeentry_begin(pHeap, pFreeList);
			freeEntryIter != pHeap->hosi->freeentry_end(pHeap, pFreeList);
			freeEntryIter = pHeap->hosi->freeentry_next(pHeap, pFreeList, freeEntryIter))
		{
			CrypticHeapFreeEntry * pFreeEntry = pHeap->hosi->freeentry_get(pHeap, pFreeList, freeEntryIter);
			uNumEntries++;
			printf("\t%llu\n", pFreeEntry->uSize);

			if (pCache && pFreeEntry->uBaseAddress == pCache->uBaseAddress)
			{
				pHeap->hosi->cache_free(pHeap, pCache);
				pCache = NULL;
				cacheIter = pHeap->hosi->cache_next(pHeap, cacheIter);
				if (cacheIter != pHeap->hosi->cache_end(pHeap))
				{
					pCache = pHeap->hosi->cache_get(pHeap, cacheIter);

					while (!pCache->uBaseAddress)
					{
						pHeap->hosi->cache_free(pHeap, pCache);
						pCache = NULL;
						cacheIter = pHeap->hosi->cache_next(pHeap, cacheIter);
						if (cacheIter == pHeap->hosi->cache_end(pHeap)) break;
						pCache = pHeap->hosi->cache_get(pHeap, cacheIter);
					}
				}
			}

			pHeap->hosi->freeentry_free(pHeap, pFreeList, pFreeEntry);
		}

		printf("%u\n", uNumEntries);

		pHeap->hosi->freelist_free(pHeap, pFreeList);
	}

	assert(!pCache);
}

static bool heap_VerifyCrypticAllocation(CrypticHeap * pHeap, HeapAddress uAddress, HeapSize uEntrySize)
{
	U8 pData[8] = {0};
	HeapSize uSize = 0;
	HeapSize uExtra = 6;
	bool bSuccess = false;

	bSuccess = pHeap->pAccessor(uAddress, pData, 8, pHeap->pAccessorUserData);
	assertmsg(bSuccess, "Could not get heap data; dump or heap invalid.");

	if (!(pHeap->flags & HF_64_BIT) && pData[3] == (CrypticGuard & 0xFF) && pData[0] < 0xFF)
	{
		uSize = pData[0];
	}
	else if (uEntrySize >= CrypticAllocMinSize + 4 && 
		pData[7] == (CrypticGuard & 0xFF))
	{
		if (pHeap->flags & HF_64_BIT)
		{
			uSize = (*((U32*)pData)) << 8 | pData[4];
		}
		else if (pData[4] == 0xFF)
		{
			uSize = *((U32*)pData);
		}

		uExtra += 4;
	}

	if (uSize)
	{
		U16 uEndGuardband = 0;

		assertmsg(uSize + uExtra <= uEntrySize,
			"Invalid MemTrackHeader size (too large); POSSIBLY invalid heap.\n");

		bSuccess = pHeap->pAccessor(uAddress + uSize + uExtra - 2, &uEndGuardband, 2, pHeap->pAccessorUserData);
		assertmsg(bSuccess, "Could not get heap data; dump or heap invalid.");

		if (uEndGuardband != (CrypticGuard & 0xFFFF))
		{
			printf("\tEntry missing tail guardband: ");
			heap_PrintAddress(pHeap, uAddress);
			printf("\n");
		}
		return true;
	}
	return false;
}

static void heap_WalkSegments(CrypticHeap * pHeap)
{
	CrypticHeapIterator segmentIter = 0;

	printf("\n\nSegments\nPages, Uncommitted Ranges, Largest Uncommitted Range, Cryptic Allocs, Other Allocs\n");
	for (segmentIter = pHeap->hosi->segment_begin(pHeap);
		 segmentIter != pHeap->hosi->segment_end(pHeap);
		 segmentIter = pHeap->hosi->segment_next(pHeap, segmentIter))
	{
		CrypticHeapSegment * pSegment = pHeap->hosi->segment_get(pHeap, segmentIter);
		CrypticHeapIterator blockIter = 0;
		unsigned int uCrypticAllocs = 0;
		unsigned int uOtherAllocs = 0;

		for (blockIter = pHeap->hosi->block_begin(pHeap, pSegment);
			 blockIter != pHeap->hosi->block_end(pHeap, pSegment);
			 blockIter = pHeap->hosi->block_next(pHeap, pSegment, blockIter))
		{
			CrypticHeapBlock * pBlock = pHeap->hosi->block_get(pHeap, pSegment, blockIter);
			if (pBlock->bBusy && pBlock->bCommitted)
			{
				if (heap_VerifyCrypticAllocation(pHeap, pBlock->uBaseAddress, pBlock->uSize))
				{
					uCrypticAllocs++;
				}
				else
				{
					uOtherAllocs++;
				}
			}
			pHeap->hosi->block_free(pHeap, pSegment, pBlock);
		}

		printf("%u, %u, %llu, %u, %u\n", pSegment->uNumberOfPages, pSegment->uNumberOfUnCommittedRanges, pSegment->uLargestUnCommittedRange,
			uCrypticAllocs, uOtherAllocs);

		pHeap->hosi->segment_free(pHeap, pSegment);
	}
}

void HeapVerify(HeapAddress uHeapAddr, WindowsVersion eVersion, HeapFlags flags, HeapAccessFunction pAccessor, void * pAccessorUserData)
{
	CrypticHeap heap = {0};

	heap.flags = flags;
	heap.pAccessor = pAccessor;
	heap.pAccessorUserData = pAccessorUserData;
	heap.uBaseAddress = uHeapAddr;

	if (eVersion == Version_WindowsServer2003 && flags & HF_64_BIT)
	{
		heap.hosi = HOSI_Win2003x64();
	}
	else if (eVersion == Version_Windows7 && flags & HF_64_BIT)
	{
		heap.hosi = HOSI_Win7x64();
	}
	else
	{
		assertmsg(0, "Unsupported version of Windows.");
	}

	heap_WalkVirtualAllocations(&heap);

	if (gbCheckFreeList)
	{
		heap_WalkFreeLists(&heap);
	}

	heap_WalkSegments(&heap);
}