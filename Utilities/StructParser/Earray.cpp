#include "earray.h"

#define ABS(a)		(((a)<0) ? (-(a)) : (a))

void eaDestroy_x(cEArrayHandle* handle) // free list
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle))
		return;

	free(pArray);
	
	
	*handle = NULL;
}

void eaCreateInternal(cEArrayHandle* handle)
{
	EArray* pArray;
	pArray = (EArray*)malloc(sizeof(EArray));
	pArray->count = 0;
	pArray->size = 1;
	*handle = (cEArrayHandle)HandleFromEArray(pArray);
}


void eaSetCapacity_dbg(cEArrayHandle* handle, int capacity) // grows or shrinks capacity, limits size if required
{
	EArray* pArray;

	if (!(*handle))
		eaCreateInternal(handle);	// Auto-create pArray.
	pArray = EArrayFromHandle(*handle);

	pArray->size = capacity>1? capacity: 1;
	
	pArray = (EArray*)realloc(pArray, EARRAY_HEADER_SIZE + sizeof(pArray->structptrs[0])*(pArray->size));
	if (pArray->count > capacity) 
		pArray->count = capacity;

	*handle = (cEArrayHandle)HandleFromEArray(pArray);
}


int eaPush_dbg(cEArrayHandle* handle, const void* structptr) // add to the end of the list, returns the index it was added at (the new size)
{
	EArray* pArray = *handle ? EArrayFromHandle(*handle) : NULL;

	
	if (!(*handle))
		eaCreateInternal(handle);	// Auto-create pArray.
	pArray = EArrayFromHandle(*handle);

	if (pArray->count == ABS(pArray->size)) 
	{
		eaSetCapacity_dbg(handle, ABS(pArray->size)*2);
		pArray = EArrayFromHandle(*handle);
	}

	pArray->structptrs[pArray->count++] = (void*)structptr;
	return pArray->count-1;
}
	


void eaClearEx(EArrayHandle* handle, EArrayItemCallback destructor)
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle))
		return;

	// Do not change the order: This goes from last to first when clearing
	// so that an item can safely remove itself from the array on destruction
	for (i = pArray->count-1; i >= 0; --i)
	{
		if (pArray->structptrs[i])
		{
			if (destructor)
				destructor(pArray->structptrs[i]);
			else
				free(pArray->structptrs[i]);	

			pArray->structptrs[i] = NULL;
		}
	}

	pArray->count = 0;
}

void eaDestroyEx_x(EArrayHandle *handle,EArrayItemCallback destructor)
{
	eaClearEx(handle,destructor);
	eaDestroy((cEArrayHandle*)handle);
}

static void* twoNULLs[2];
#define PTR_ARRAY_OFFSET_TO_NULL(s) ((size_t)((intptr_t)twoNULLs + sizeof(void*) - 1 - (intptr_t)s) / sizeof(void*))

#define CopyStructs(dest,src,count)					memmove((dest),(src),sizeof((dest)[0]) * (count))
#define CopyStructsFromOffset(dest,offset,count)	memmove((dest),(dest)+(offset),sizeof((dest)[0]) * (count))
#define DupStructs(src,count)						((count && src)?memmove(calloc((count), sizeof((src)[0])),(src),sizeof((src)[0]) * (count)):NULL)


size_t eaGetIndexToRemovedOrOffsetToNULL(cEArrayHandle* handle, U32 index)
{
	void* structptr;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count){
		return PTR_ARRAY_OFFSET_TO_NULL(pArray->structptrs);
	}
	structptr = pArray->structptrs[index];
	pArray->count--;
	if(index != (U32)pArray->count){
		CopyStructsFromOffset(pArray->structptrs + index, 1, pArray->count - index);
		pArray->structptrs[pArray->count] = structptr;
	}
	return pArray->count;
}