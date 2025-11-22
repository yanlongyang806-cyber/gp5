/* File MemoryPool.h
 *	A memory pool manages a large array of structures of a fixed size.  
 *	The main advantage of are
 *		- Low memory management overhead
 *			It manages large pieces of memory without the need for additional memory for
 *			management purposes, unlike malloc/free.
 *		- Fast at serving mpAlloc/mpFree requests.
 *			Very few operations are needed before it can complete both operations.
 *		- Suitable for managing both statically and dynamically allocated arrays.
 *
 *
 */

#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H


typedef void (*Destructor)(void*);

// MemoryPool internals are hidden.  Accidental changes are *bad* for memory management.
typedef long MemoryPool;


#define ZERO_MEMORY_BIT				1

typedef enum{
	TurnOffAllFeatures =		0,
	Default =					ZERO_MEMORY_BIT,
	//ZeroMemory =				ZERO_MEMORY_BIT,
} MemoryPoolMode;


// HashTable mode query/alteration
MemoryPoolMode mpGetMode(MemoryPool pool);
int mpSetMode(MemoryPool pool, MemoryPoolMode mode);


/************************************************************************
 * Normal Memory Pool
 */

// constructor/destructors
MemoryPool createMemoryPool();
void initMemoryPool(MemoryPool pool, int structSize, int structCount);
void initMemoryPool_dbg(MemoryPool pool, int structSize, int structCount, char* callerName, int line);

MemoryPool initMemoryPoolLazy(MemoryPool pool, int structSize, int structCount);
MemoryPool initMemoryPoolLazy_dbg(MemoryPool pool, int structSize, int structCount, char* callerName, int line);

void destroyMemoryPool(MemoryPool pool);
void destroyMemoryPoolGfxNode(MemoryPool pool);

// Allocate a piece of memory.
void* mpAlloc(MemoryPool pool);
void* mpAlloc_dbg(MemoryPool pool, char* callerName, int line);

// Retains all allocated memory.
int mpFree(MemoryPool pool, void* memory);

// Frees all allocated memory.
void mpFreeAll(MemoryPool pool);

int mpVerifyFreelist(MemoryPool pool);
int mpFindMemory(MemoryPool pool, void* memory);

// Chech the structure size of this memory pool.
unsigned int mpStructSize(MemoryPool pool);

// Check if a piece of memory has been returned into a memory pool.
int mpReclaimed(void* memory);

int mpCheckPool(MemoryPool pool);

void testMemoryPool();
/*
 * Normal Memory Pool
 ************************************************************************/


#if defined(INCLUDE_MEMCHECK) || defined(_CRTDBG_MAP_ALLOC)
#define TRACKED_MEMPOOL
/************************************************************************
 * Tracked Memory Pool
 *	Allocation request origin is tracked properly.
 */


#define initMemoryPool(pool,structSize,structCount) initMemoryPool_dbg(pool,structSize,structCount, __FILE__, __LINE__);
#define initMemoryPoolLazy(pool,structSize,structCount) initMemoryPoolLazy_dbg(pool,structSize,structCount, __FILE__, __LINE__);
#define mpAlloc(pool) mpAlloc_dbg(pool, __FILE__, __LINE__)

/*
 * Tracked Memory Pool
 ************************************************************************/
#endif

#endif