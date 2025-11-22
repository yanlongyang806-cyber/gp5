/* File StringTable.c
 *	Currently, the string table is implemented as an array of memory chunks.
 *	The first element of this array, however, is an "index table".  This is
 *	used when the StringTable's mode is set to "indexable".
 *
 *	Implementation notes:
 *	How large is each MemoryChunk in the StringTable?
 *		All the memory chunks are of the same size.  The size is specified
 *		when initStringTable is called.  However, this size is not stored
 *		explicitly in one of the members of StringTableImp.  Instead, the
 *		size is stored implicitly within each MemoryChunk.  So, to retrieve
 *		the size of any of the MemoryChunks in the StringTable, it is possible
 *		to just use the maximum size of the first MemoryChunk.
 *
 *
 *	
 *			
 */


#pragma warning(push)
#pragma warning(disable:4028) // parameter differs from declaration
#pragma warning(disable:4047)

#include "StringTable.h"
#include "Array.h"
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "Common.h"

typedef struct{
	Array indexTable;	// array of string pointers that points into the chunkTable
	Array chunkTable;	// array of memory chunks
	unsigned int indexable	: 1;
	unsigned int wideString	: 1;
} StringTableImp;

/* Typedef MemoryChunk
 *	A memory chunk is compatible with an Array at the binary level.  However,
 *	a memory chunk cannot grow dynamically since it will have all the memory it 
 *	should hold at the time of creation.  Also, an Array only allocates memory
 *	in multiples of 4 bytes, whereas the memory chunk will allocate the exact
 *	number of bytes requested.
 *
 *	Note that it is a bad idea to opearte on a MemoryChunk using Array function.  
 *	Quite a few Array functions assumes that the array can resize itself 
 *	dynamically.  In addition, the storage member of the MemoryChunk marks the 
 *	beginning of a piece of usable memory and should not be used like a real 
 *	pointer as done in Arrays.
 *
 *	Since a MemoryChunk not functionally compatible with an Array, maybe it
 *	ought to have its own structure?
 */
typedef Array MemoryChunk;


/*****************************************
 * Begin MemoryChunk specific functions *
 *****************************************/
static MemoryChunk* createMemoryChunk(unsigned int chunkSize){
	MemoryChunk* chunk = calloc(1, chunkSize + sizeof(MemoryChunk) - 4);
	chunk->maxSize = chunkSize;
	return chunk;
}

static void destroyMemoryChunk(MemoryChunk* chunk){
	free(chunk);
}

/**************************************
 * End MemoryChunk specific functions *
 **************************************/


int strTableStrMemoryLength(StringTableImp* table, void* str){
	if(table->wideString)
		return wcslen(str);
	else
		return strlen(str);
}

/***********************************
 * Begin module specific functions *
 ***********************************/
typedef enum{
	PREV = -1,
	CURR = 0,
	NEXT = 1
} MemoryChunkSpecifier;

/* Function strTableGetChunk
 *	Simplified method of grabbing memory chunks from a string table.  
 *	
 *	Requesting for the "current" chunk
 *		always returns the most recently created memory chunk.  
 *
 *	Requesting for the "previous" chunk 
 *		always returns the 2nd most recently created chunk.
 *
 *	Requesting for the "next" chunk
 *		always allocates a new memory chunk, which is automatically added to the string table.
 *
 */
static MemoryChunk* strTableGetChunk(StringTableImp* table, MemoryChunkSpecifier cursorModifier){
	int cursor;

	// Since the "current" chunk is always the last chunk in the string table, a new MemoryChunk
	// needs to be created and added to the string table if the "next" chunk is requested.
	if(NEXT == cursorModifier){
		MemoryChunk* initialChunk = table->chunkTable.storage[0];
		MemoryChunk* newChunk = createMemoryChunk(initialChunk->maxSize);
		arrayPushBack(&table->chunkTable, newChunk);
		return newChunk;
	}

	// Either getting the "current" or the "previous" chunk...
	cursor = table->chunkTable.size - 1 + cursorModifier;
	
	// Don't let the cursor point to crazy places
	if(cursor < 0)
		cursor = 0;
	else if(cursor >= table->chunkTable.maxSize)
		cursor = table->chunkTable.maxSize;

	// Return the chunk indexed by the cursor.
	return table->chunkTable.storage[cursor];
}
/**************************************
 * End MemoryChunk specific functions *
 **************************************/



// Constructor + Destructor
StringTable createStringTable(){
	return calloc(1, sizeof(StringTableImp));
}

/* Function initStringTable
 *	Initializes a string table. =)
 *	
 *	The specified chunk size is stored implicitly in the first MemoryChunk's
 *	maxSize member.  Since each MemoryChunk is already storing this same
 *	size, there is really no point to storing it again in the StringTable
 *	itself.  
 *
 */
void initStringTable(StringTableImp* table, unsigned int chunkSize){
	// Create enough space to hold a few chunks of memory
	initArray(&table->chunkTable, 16);

	// Create and store an initial chunk of memory
	arrayPushBack(&table->chunkTable, createMemoryChunk(chunkSize));
}

void destroyStringTable(StringTableImp* table){

	destroyArrayPartialEx(&table->chunkTable, NULL);
	destroyArrayPartial(&table->indexTable);
	
	// Destroy the string table itself
	free(table);
}


/* Function strTableAddString
 *	Adds the given string into the given string table.
 *
 *	The function only checks the current and previous MemoryChunk to see
 *	if they are capable of holding the incoming string.  While it is possible
 *	to perform a linear search through all the memory chunks to make full use of
 *	allocated memory, this will make the function slower the more strings 
 *	(or memory chunks) there are.  This is particularly undesirable when dealing 
 *	with a large number of strings.  In such a case, we probably want to spend
 *	time searching the memory chunk table linearly everytime a string is being 
 *	inserted.
 *
 *	Alternatively, it is possible to use larger chunks of memory to make
 *	the linear search faster.
 *
 *	Parameters:
 *		table - the string table into which to add the given string\
 *		str - the string to add to the string table
 *
 *	Returns:
 *		char* - copy of the str that's stored in the table
 */
const void* strTableAddString(StringTableImp* table, const void* str){
	MemoryChunk* chunk;
	char* newStringStartLocation;
	int strSize;
	int strMemSize;
	

	// Calculate how much memory the new string is going to need.
	if(table->wideString){
		strSize = wcslen(str) + 1;  // strict string size plus null terminating char
		strMemSize = strSize << 1;  // strict string size plus null terminating char
	}
	else{
		strMemSize = strSize = strlen(str) + 1;
	}

	// If the string is a wide string, it will need twice the normal amount of memory to store it.
	

	// Find a MemoryChunk that is capable of holding the given string
	//	Can the previous chunk of memory hold this string?
	chunk = strTableGetChunk(table, PREV);
	if(chunk->size + strMemSize >= chunk->maxSize){
		// The previous chunk does not have enough memory to hold the string.
		// Can the current chunk of memory hold this string?
		chunk = strTableGetChunk(table, CURR);

		if(chunk->size + strMemSize >= chunk->maxSize){
			// The current chunk also cannot hold the string

			chunk = strTableGetChunk(table, NEXT);
			if(chunk->size + strMemSize >= chunk->maxSize){
				// Still can't insert the string?
				// The given string must be larger than the chunk size.
				// The string table will break down and cry now...
				assert(0);
			}
		}
	}

	// A chunk that's large enough was found.
	// Calculate the location where the new string should be copied to.
	newStringStartLocation = (char*)&chunk->storage + chunk->size;

	// Copy the string into the memory chunk.
	if(table->wideString)
		wcsncpy((wchar_t*)newStringStartLocation, str, strSize);
	else
		strncpy(newStringStartLocation, str, strSize);
	chunk->size += strMemSize;

	// Store the address to the new string in the index table if the table
	// is supposed to be indexable.
	if(table->indexable)
		arrayPushBack(&table->indexTable, newStringStartLocation);

	return newStringStartLocation;
}

/* Function strTableClear
 *	Clears the entire string table without altering the table size in any way.
 *	
 *	Remember to throw aways any old pointers into the string table after clearing
 *	a string table.  This function actually only resets the size of all memory 
 *	chunks to zero.  Therefore, any existing pointers into the string table would 
 *	still be "legal" even though they are invalidated logically.  The contents of 
 *	the string pointed to by the old pointers will change when new strings are inserted.
 *
 */
void strTableClear(StringTableImp* table){
	int i;
	MemoryChunk* chunk;

	for(i = 0; i < table->chunkTable.size; i++){
		chunk = table->chunkTable.storage[i];
		chunk->size = 0;
	}
}

/* Function strTableGetString
 *	Returns a string given it's index in the string table.  This function
 *	will always return NULL unless the string table is indexable.
 *
 */
const char* strTableGetString(StringTableImp* table, int index){
	// If the table is not indexable, this operation cannot be completed because
	// no string addresses were stored.
	if(!table->indexable)
		return NULL;
	
	// If the requested index is invalid, return a dummy value.
	if(index < 0 || index >= table->indexTable.size)
		return NULL;

	return table->indexTable.storage[index];
}


/* Function strForEachString
 *	Invokes the given processor function for each of the string held in the string
 *	table.  This function is provided so that it is possible to perform some operation
 *	using all the strings stored in the string table even if the table is not indexable.
 *
 *	Expected StringProcessor return values:
 *		0 - stop examining any more strings
 *		1 - continue examining strings
 *
 */
void strTableForEachString(StringTableImp* table, StringProcessor processor){
	int chunkIndex;
	int strIndex;

	MemoryChunk* chunkCursor;
	void* strCursor;

	// Walk through every memory chunk in the string table.
	for(chunkIndex = 0; chunkIndex < table->chunkTable.size; chunkIndex++){
		chunkCursor = table->chunkTable.storage[chunkIndex];

		// Walk through every string in the current chunk.
		for(strIndex = 0; strIndex < chunkCursor->size; strIndex += strTableStrMemoryLength(table, strCursor) + 1){
			strCursor = (char*)&chunkCursor->storage + strIndex;
			if(!processor(strCursor))
				return;
		}
	}
}

StrinTableMode strTableGetMode(StringTableImp* table){
	StrinTableMode mode = 0;
	if(table->indexable)
		mode |= Indexable;
	if(table->wideString)
		mode |= WideString;
	
	return mode;
}

/* Function strTableSetMode
 *	Sets the operation mode of the string table.
 *
 *	Returns:
 *		0 - mode set failed
 *		1 - mode set successfully
 */
int strTableSetMode(StringTableImp* table, StrinTableMode mode){
	MemoryChunk* initialChunk = table->chunkTable.storage[0];

	// The following options can only be set before the table has not been used yet.
	if(0 == initialChunk->size){
		if(Indexable & mode){
			table->indexable = 1;
		}

		if(WideString & mode){
			table->wideString = 1;
		}
	}

	// FIXME!!!
	// Always saying the mode was set successfully is not correct.
	return 1;
}


/********************************************************************
 * Begin StringTable Test related functions
 */
#include <crtdbg.h>
#include <stdio.h>
int testProcessor(char* str){
	static i = 0;
	printf("Index %i assigned to string \"%s\"\n", i, str);
	i++;
	return 1;
}

void testStringTable(){
	StringTable strTable;
	int i;
	
	// Strings stolen from HashTable test function. =)
	int keysSize = 23;
	char* keys[] = {"one", "two", "three", "four", "five", "somekey", "this", "is", "blaa", "test",
		"of course", "the sun", "hashbrown", "tablesalt", "which", "capable", "really", "fastfood",
		"insertions", "android", "look up and down", "POWER PUNCH", "ENERGY BLAST"};

	// Create and initialize a string table
	strTable = createStringTable();
	initStringTable(strTable, 32);
	strTableSetMode(strTable, Indexable);
	/*	Usually, the chunk size should be a large number.  The small
	 *	size specified here is for testing purposes only.
	 */
	
	
	// Insert some strings
	printf("Inserting strings...\n");
	for(i = 0; i < keysSize; i++){
		const char* copiedStr;

		// Upon insertion, we get back an exact copy of the string that's stored in the string
		// table.
		copiedStr = strTableAddString(strTable, keys[i]);

		// Print out the string for verification.
		printf("Index %i assigned to string \"%s\"\n", i, copiedStr);
	}

	// Try to get the string via the strTableGetString function.
	//	Try to break the function by giving it bad indices.
	printf("\nAccessing strings through indexing...\n");
	for(i = -1; i < keysSize + 1; i++){
		printf("Index %i assigned to string \"%s\"\n", i, strTableGetString(strTable, i));
	}

	// Print all the strings using the strTableForEachString function
	printf("\nAccessing strings through callback function...\n");
	strTableForEachString(strTable, testProcessor);

	destroyStringTable(strTable);
	assert(_CrtCheckMemory());
	_CrtDumpMemoryLeaks();	
}
/*
 * Begin StringTable Test related functions
 ********************************************************************/
#pragma warning(pop)