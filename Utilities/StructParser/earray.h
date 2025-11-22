#pragma once

#include "utils.h"

#define OFFSETOF(typename, n)					(uintptr_t)&(((typename*)(0x0))->n)

typedef struct EArray
{
	int count;
	int size; // if negative this is a stack earray
	intptr_t tableandflags; // a parse table pointer, and some flags packed in
	void* structptrs[1];
} EArray;

#define EARRAY_HEADER_SIZE	OFFSETOF(EArray,structptrs)	

typedef struct EArray32
{
	int count;
	int size;
	U32 values[1];
} EArray32;

#define EARRAY32_HEADER_SIZE	OFFSETOF(EArray32,values)

#define EArrayFromHandle(handle) ((EArray*)(((char*)handle) - EARRAY_HEADER_SIZE))
#define HandleFromEArray(array) ((EArrayHandle)(((char*)array) + EARRAY_HEADER_SIZE))

#define EArrayTypeFromHandle(t, handle)					((EArray##t*)(((char*)handle) - EARRAY##t##_HEADER_SIZE))
#define HandleFromEArrayType(t, array)					((U##t*)(((char*)array) + EARRAY##t##_HEADER_SIZE))
#define eaTypeSize(t, handle)							(*(handle)? EArray##t##FromHandle(*(handle))->count : 0)

__forceinline static int EAPtrTest(const void * const * const *ptr) { return 0; }


#define eaSize(handle)			((*(handle)? EArrayFromHandle(*(handle))->count : 0))
#define eaUSize(handle)			((U32)eaSize(handle))

// This should only be used when you have more or less levels of indirection than void *** (e.g, an earray of void *s).
#define eaSizeUnsafe(handle)	(*(handle)? EArrayFromHandle(*(handle))->count : 0)


typedef void** EArrayHandle;									// pointer to a list of pointers
typedef const void ** cEArrayHandle;							// non-const pointer to a non-const list of const pointers
typedef const void * const * const ccEArrayHandle;				// const pointer to a const list of const pointers

typedef U32* EArray32Handle;									// pointer to a list of ints
typedef const U32* cEArray32Handle;								// non-const pointer to a non-const list of const ints
typedef const U32* const ccEArray32Handle;						// const pointer to a const list of const ints

typedef void (*EArrayItemCallback)(void*);




void	eaDestroy_x(cEArrayHandle* handle);								// free array
void	eaDestroyEx_x(EArrayHandle *handle,EArrayItemCallback destructor); // calls destroy contents, then destroy; if destructor is null, uses free()

#define eaDestroy(handle) eaDestroy_x((cEArrayHandle*)handle)
#define eaDestroyEx(handle, destructor) eaDestroyEx_x((EArrayHandle*)handle, destructor)


int		eaPush_dbg(cEArrayHandle* handle, const void* structptr);		// add to the end of the list, returns the index it was added at (the old size)
#define eaVerifyType(handle,structptr)		(1?0:(**(handle)=(structptr),*(handle)=*(handle),0))
#define eaVerifyTypeConst(handle,structptr)	(1?0:(**(handle)==(structptr),0))
#define eaPush(handle,structptr) ((void)eaVerifyType(handle,structptr),eaPush_dbg((cEArrayHandle*)(handle),structptr))

size_t eaGetIndexToRemovedOrOffsetToNULL(cEArrayHandle* handle, U32 index);
#define eaRemove(handle, index) ((*(handle))[eaGetIndexToRemovedOrOffsetToNULL((cEArrayHandle*)(handle),(index))])
