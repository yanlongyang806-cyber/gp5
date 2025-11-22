#pragma warning(push)
#pragma warning(disable:4024)
#pragma warning(disable:4028) // parameter differs from declaration
#pragma warning(disable:4047)


#include "MemoryPool.h"
#include "Array.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <crtdbg.h>

typedef struct MemoryPoolNode MemoryPoolNode;
struct MemoryPoolNode{
	MemoryPoolNode* address;	// stores it's own address
	MemoryPoolNode* next;		// stores the next node's address
};

typedef struct{
	unsigned int	structSize;
	unsigned int	structCount;
	Array			chunkTable;	// A list of memory chunks
	MemoryPoolNode*	freelist;	// points to the next free MemoryPoolNode
	unsigned int	zeroMemory : 1;
}MemoryPoolImp;

MemoryPoolMode mpGetMode(MemoryPoolImp* pool){
	MemoryPoolMode mode = TurnOffAllFeatures;
	if(pool->zeroMemory)
		mode |= ZERO_MEMORY_BIT;
	return mode;
}

int mpSetMode(MemoryPoolImp* pool, MemoryPoolMode mode){
	if(mode & ZERO_MEMORY_BIT){
		pool->zeroMemory = 1;
	}else
		pool->zeroMemory = 0;

	return 1;
}

MemoryPool createMemoryPool(){
	return calloc(1, sizeof(MemoryPoolImp));
}


/* Function mpInitMemoryChunk()
 *  Cuts up given memory chunk into the proper size for the given memory pool and
 *	adds the cut up pieces to the free-list in the memory pool.
 *
 *	Note that this function does *not* add the memory chunk to the memory pool's
 *	memory chunk table.  Remember to do this or else the memory chunk will be leaked
 *	when the pool is destroyed.
 *
 */
static void mpInitMemoryChunk(MemoryPoolImp* pool, void* memoryChunk){
	MemoryPoolNode* frontNode;
	MemoryPoolNode* backNode;
	MemoryPoolNode* lastNode;
	int structSize = pool->structSize;
	int structCount = pool->structCount;
	
	// Cannot add a memory chunk to a memory pool if there is no pool.
	assert(pool);
	
	// At least 8 bytes are needed to store some integrity check value + a next element link.
	assert(structSize >= sizeof(MemoryPoolNode));
	
	// Build a linked list of free MemoryPool nodes in their array element order by
	// constructing the list in reverse order.
	//	Get the the beginning of the last structure (allocation unit) in the array and use it as the beginning
	//	of a MemoryPool node.
	//		Cast memory to char* to perform arithmetic in unit of 1 byte instead of 4 bytes for
	//		normal pointers.
	lastNode = frontNode = backNode = (MemoryPoolNode*)((char*)memoryChunk + structSize * (structCount - 1));
	
	//	Initialize the last element.
	backNode->address = backNode;
	backNode->next = NULL;
	
	//  Keep constructing memory pool nodes in the reverse order until we've reached where
	//  "memory" is pointing.
	while((void*)backNode > memoryChunk){
		// Deduce the starting address of the structure that's right before the one being
		// held by the back node.  Call it the front node.
		frontNode = (MemoryPoolNode*)((char*)backNode - structSize);
		
		// Initialize the front node
		frontNode->address = frontNode;
		frontNode->next = backNode;
		
		// The current front node becomes the back node for the next iteration.
		backNode = frontNode;
	}
	
	// Add the linked list of free MemoryPool nodes to the memory pool
	//	If the memroy pool is not completely empty, add the new elements to the front of the
	//	linked list.  The only reason the list is added to the front instead of the back of the
	//	list is because it is easier to get to the front of the existing freelist.
	if(pool->freelist){
		lastNode->next = pool->freelist;
	}
	
	pool->freelist = frontNode;
}


/* Function mpAddMemoryChunk()
 *	Adds a new chunk of memory into the memory pool based on the existing structSize and structCount
 *	settings.  The memory is then initialized then added to the pool's memory chunk table.
 *
 *
 */
static void mpAddMemoryChunk_dbg(MemoryPoolImp* pool, char* callerName, int line){
	void* memoryChunk;
	
	// Cannot add a memory chunk to a memory pool if there is no pool.
	assert(pool);
	
	// Create the memory chunk to be added.
	memoryChunk = _calloc_dbg(pool->structCount, pool->structSize, _NORMAL_BLOCK, callerName, line);

	mpInitMemoryChunk(pool, memoryChunk);

	// Add the memory chunk to a list in the pool.
	arrayPushBack(&pool->chunkTable, memoryChunk);
}

static void mpAddMemoryChunk(MemoryPoolImp* pool){
	void* memoryChunk;
	
	// Cannot add a memory chunk to a memory pool if there is no pool.
	assert(pool);
	
	// Create the memory chunk to be added.
	memoryChunk = calloc(pool->structCount, pool->structSize);

	mpInitMemoryChunk(pool, memoryChunk);

	// Add the memory chunk to a list in the pool.
	arrayPushBack(&pool->chunkTable, memoryChunk);
}

/* Function initMemoryPool()
 *	Initializes the given memory pool.
 *
 *	Parameters:
 *		pool - The memory pool to be initialized.
 *		structSize - The size of an "allocation unit".  A piece of memory of this size will be returned everytime
 *					 mpAlloc() is called.
 *		structCount - The number of "allocation units" to put in the pool initially.  Together with structSize,
 *					  this number also dictates the amount of "allocation units" to grow by when the pool runs
 *					  dry.
 *
 *
 */
void initMemoryPool_dbg(MemoryPoolImp* pool, int structSize, int structCount, char* callerName, int line){
	pool->structSize = structSize;
	pool->structCount = structCount;
	mpAddMemoryChunk_dbg(pool, callerName, line);
	mpSetMode(pool, Default);
}

#ifndef TRACKED_MEMPOOL
void initMemoryPool(MemoryPoolImp* pool, int structSize, int structCount){
	pool->structSize = structSize;
	pool->structCount = structCount;
	mpAddMemoryChunk(pool);
	mpSetMode(pool, Default);
}
#endif

MemoryPool initMemoryPoolLazy_dbg(MemoryPool pool, int structSize, int structCount, char* callerName, int line){
	if(!pool){
		pool = createMemoryPool();
		initMemoryPool_dbg(pool, structSize, structCount, callerName, line);
	}else
		mpFreeAll(pool);

	return pool;
}

#ifndef TRACKED_MEMPOOL
MemoryPool initMemoryPoolLazy(MemoryPool pool, int structSize, int structCount){
	if(!pool){
		pool = createMemoryPool();
		initMemoryPool(pool, structSize, structCount);
	}else
		mpFreeAll(pool);

	return pool;
}
#endif


/* Function destroyMemoryPool()
 *	Frees all memory allocated by memory pool, including the pool itself.
 *
 *	After this call, all memory allocated out of the given pool is invalidated.
 *
 */
void destroyMemoryPool(MemoryPoolImp* pool){
	destroyArrayPartialEx(&pool->chunkTable, NULL); // BR - changed destroyArrayEx to destroyArrayPartialEx
	free(pool);
}

/* Special for the gfxtree for debug.  sets every node to -1 before freeing
*/
void destroyMemoryPoolGfxNode(MemoryPoolImp* pool){
	int i;
	for(i = 0 ; i < pool->chunkTable.size ; i++ )
		memset(pool->chunkTable.storage[i], -1, pool->structSize* pool->structCount );
	destroyArrayPartialEx(&pool->chunkTable, NULL); // BR - changed destroyArrayEx to destroyArrayPartialEx
	free(pool);
}


/* Function mpAlloc
 *	Allocates a piece of memory from the memory pool.  The size of the allocation will
 *	match the structSize given to the memory pool during initialization (initMemoryPool).
 *	
 *	Returns:
 *		NULL - cannot allocate memory because the memory pool ran dry and
 *			   it is not possible to get more memory.
 *		otherwise - valid memory pointer of size mpStructSize(pool)
 */
void* mpAlloc_dbg(MemoryPoolImp* pool, char* callerName, int line){
	void* memory;

	// If there is no memory pool, no memory can be allocated.
	assert(pool);

	// If there are no more free memory left in the pool, try to
	// allocate some more memory for the pool.
	if(!pool->freelist){
		mpAddMemoryChunk_dbg(pool, callerName, line);
	}

	memory = pool->freelist;
	
	// If we successfully got something from the freelist...
	if(memory){
		// Remove the allocated node from the freelist by
		// advancing the freelist pointer to the next node.
		pool->freelist = pool->freelist->next;

		// Make sure other parts of the program have been playing nice
		// with the memory allocated from the mem pool.
		assert(((MemoryPoolNode*)memory)->address == memory);

		// Initialize the returned memory to zero
		if(pool->zeroMemory)
			memset(memory, 0, pool->structSize);
	}
	
	return memory;
}

#ifndef TRACKED_MEMPOOL
void* mpAlloc(MemoryPoolImp* pool){
	void* memory;

	// If there is no memory pool, no memory can be allocated.
	assert(pool);

	// If there are no more free memory left in the pool, try to
	// allocate some more memory for the pool.
	if(!pool->freelist){
		mpAddMemoryChunk(pool);
	}

	memory = pool->freelist;
	
	// If we successfully got something from the freelist...
	if(memory){
//		if(memory == 0x00592040)
//			 __asm int 3;

		assert(pool->freelist->address == pool->freelist);

		// Remove the allocated node from the freelist by
		// advancing the freelist pointer to the next node.
		pool->freelist = pool->freelist->next;

		// Make sure other parts of the program have been playing nice
		// with the memory allocated from the mem pool.
		assert(((MemoryPoolNode*)memory)->address == memory);

		// Initialize the returned memory to zero
		if(pool->zeroMemory)
			memset(memory, 0, pool->structSize);
	}
	
	return memory;
}
#endif

/* Function mpFree
*	Returns a piece of memory back into the pool.
*	
*	Parameters:
*		pool - a valid memory pool
*		memory - the piece of memory to return to the pool.  The size
*				 is implied by mpStructSize() of the given pool.
*
*	Returns:
*		1 - memory returned successfully
*		0 - some error encountered while returning memory
*
*	Warning:
*		This function does not know anything about the incoming memory chunk.  
*		Currently, it will accept any non-zero value and add it back into
*		the memory pool happily.  This will most likely cause an invalid memory
*		access somewhere down the line.  Since it will be hard to track the
*		source of this kind of bug, it will be a good idea to put some error
*		checking here, even a bad one.
*
*
*/
int mpFree(MemoryPoolImp* pool, void* memory){
	MemoryPoolNode* node;
	assert(pool);
	
//	if(memory == 0x00592040)
//		__asm int 3;


	// Take the memory and overlay a memory pool node on top of it.
	node = memory;
	
	// Insert the node as the head of the freelist.
	node->address = node;
	node->next = pool->freelist;
	pool->freelist = node;
	

	return 1;
}

int mpVerifyFreelist(MemoryPoolImp* pool){

	if (!pool)
		return 1;

	// Make sure each node in the memory pool still looks valid.
	//	Each node is supposed to store its own address, then the next pointer.
	//	Use this information to walk and verify the node.
	// Make sure the list is not circular.
	//	Sometimes a piece of code might free the same piece of memory twice, resulting
	//	in a circular list.
	//	The algorithm here is to move one cursor faster than the other.  If the list is
	//	circular, the fast one will eventually overtake the slow one.
	{
		MemoryPoolNode* slowCursor;
		MemoryPoolNode* fastCursor;

		slowCursor = pool->freelist;

		// If the memory pool is empty, the list is not circular and the pool is valid.
		if(!slowCursor)
			return 1;

		fastCursor = pool->freelist->next;

		// If there is only one node in the list, the list is not circular.
		if(!fastCursor)
			return 1;

		while(1){

			// Make sure each node in the memory pool still looks valid.
			//	Each node is supposed to store its own address, then the next pointer.
			//	Use this information to walk and verify the node.
			if(slowCursor->address != slowCursor)
				return 0;

			// Check if the list ended.
			if(!fastCursor)
				return 1;

			// Did the fast cursor go in a loop and catch up to the fast cursor?
			if(slowCursor == fastCursor)
				return 0;

			// Advance the fast again.
			fastCursor = fastCursor->next;
			if(!fastCursor)
				return 1;

			if(slowCursor == fastCursor)
				return 0;

			// Advance both cursors once.
			slowCursor = slowCursor->next;
			fastCursor = fastCursor->next;
		}
	}
	return 1;
}

int mpFindMemory(MemoryPoolImp* pool, void* memory){
	MemoryPoolNode* node;

	if (!pool)
		return 0;

	// Make sure each node in the memory pool still looks valid.
	//	Each node is supposed to store its own address, then the next pointer.
	//	Use this information to walk and verify the node.
	for(node = pool->freelist; node; node = node->next){
		if(node == memory){
			return 1;
		}
	}

	return 0;
}

/* Function mpFreeAll
 *	Return all memory controlled by the memory pool back into the pool.  This effectively
 *	destroys/invalidates all structure held by the memory pool.
 *
 *	Note that this function does not actually free any of the memory that is currently
 *	held by the pool.
 *
 */
void mpFreeAll(MemoryPoolImp* pool){
	int i;
	pool->freelist = NULL;
	for(i = pool->chunkTable.size - 1; i >= 0; i--)
		mpInitMemoryChunk(pool, pool->chunkTable.storage[i]);
}

unsigned int mpStructSize(MemoryPoolImp* pool){
	return pool->structSize;
}


/* Function mpReclaimed()
 *	Answers if a piece of memory has been reclaimed by a memory pool.  Note that this function
 *	is not fool-proof.  It does not know if the given memory is allocated out of a memory pool.
 *	This function merely checks if the piece of memory holds a valid integrity value.  It will 
 *	fail when a piece of memory that is not reclaimed just happens to be storing the expected 
 *	integrity value at the expected location.
 *
 *	Returns:
 *		0 - memory not reclaimed.
 *		1 - memory reclaimed.
 */
int mpReclaimed(void* memory){
	MemoryPoolNode* node;
	assert(memory);

	node = memory;
	if(node->address == node)
		return 1;
	else
		return 0;
}


void testMemoryPool(){
	MemoryPool pool;
	void* memory;

	pool = createMemoryPool();
	initMemoryPool(pool, 16, 16);


	printf("MemoryPool created\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

	memory = mpAlloc(pool);

	printf("\n");
	printf("Memory allocated\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

	mpFree(pool, memory);
	printf("\n");
	printf("Memory freed\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

	mpFree(pool, memory);
	printf("\n");
	printf("Memory freed again\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

}

#pragma warning(pop)