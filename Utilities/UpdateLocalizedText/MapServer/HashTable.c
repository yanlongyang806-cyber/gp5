/* File HashTable.c
 *
 *  Note on pragma directives:
 *		The function definitions has a different signature than the function declaration.
 *		HashTables are replaced by HashTableImp* and HashElements are replaced by
 *		HashElementImp*.  This is done because a descriptor is basically a struct pointer
 *		in disguise.  There is really no need for an explicit cast inside every function
 *		except for to stop the compiler from complaining.  The pragma directives simply
 *		disables those related warnings.
 */ 
#pragma warning(push)
#pragma warning(disable:4028) // parameter differs from declaration
#pragma warning(disable:4024) // related
#pragma warning(disable:4022) // related

#pragma warning(disable:4047) // return type differs in lvl of indirection warning
#pragma warning(disable:4002) // too manay macro parameters warning


#include "HashTable.h"
#include "StringTable.h"
#include <stdlib.h>
#include <memory.h> 
#include <assert.h>
#include <string.h>
#include <stdio.h>
//#include "Common.h"


#ifdef _DEBUG_HASHTABLE
#define ifDebugHash(x) x
#else
#define ifDebugHash()
#endif

#ifdef _DEBUG
#define ifDebug(x) x
#else
#define ifDebug()
#endif



/* Structure HashElementImp
 *	Used to hold a hash value + value pair to be stored in
 *	hash tables.
 *
 *	Variables:
 *		stringName - available only when _DEBUG_HASHTABLE is defined.  
 *					 This variable is used to debugging purposes since
 *					 hashvalue does not tell what the paired value
 *					 means.
 *		hashvalue - used during a table resize to generate a new
 *					element index.
 *		value - stores a "thing" that corresponds to the hash value.
 *
 */
typedef struct HashElementImp{
	char* stringName;	
	int hashValue;
	void* value;
	unsigned int deleted	: 1;
} HashElementImp;


typedef struct{
	// Performance statistics
	unsigned int elementsAdded;
	unsigned int elementsLookedup;
	unsigned int elementsRemoved;

	// slotConflicts/findIndexCount gives an number of hashes that must be performed for each
	// hashtable operation (i.e. addElement, findElement, removeElement).
	unsigned int hashCount;			// How many hashes were performed during findIndex()?
	unsigned int findIndexCount;	// How many times was the hash table asked to findIndex()?
} HashPerformanceImp;

/* Structure HashTableImp
 *	Variables:
 *		size	-	current table size
 *		maxSize	-	maximum table size.  Indicates how many HashElementImp
 *					structures are being held by the storage pointer.
 *		storage	-	pointer to an array of HashElementImp structures that
 *					stores the hashvalue + value pairs.
 */
typedef struct HashTableImp{
	int size;
	int maxSize;
	HashElementImp* storage;
	StringTable strTable;		// string storage space when deep copy is requested.

	unsigned int copyKeyNames			: 1;
	unsigned int deepCopyKeyNames		: 1;
	unsigned int allowDuplicateHashVal	: 1;
	unsigned int verifyHashValUniquenessOnInsertion: 1; // @_@ long painful var names...
	unsigned int verifyHashValUniquenessOnRetrieval: 1;
	unsigned int caseInsensitive		: 1;
	unsigned int overwriteExistingValue : 1;

	HashPerformanceImp* performance;
} HashTableImp;


// Special hash value used to mark unused HashElements.
// Make sure that nothing else uses this hash value or else
// the element will be considered "emtpy".
#define INVALID_HASHVAL 0
#define INVALID_HASHIDX -1


/***********************************************
 * Begin Hash function by Bob Jenkins from DDJ *
 ***********************************************/
typedef  unsigned long  int  ub4;   /* unsigned 4-byte quantities */
typedef  unsigned       char ub1;   /* unsigned 1-byte quantities */

#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (n-1)

/* mix -- mix 3 32-bit values reversibly.
 *	For every delta with one or two bits set, and the deltas of all three
 *	high bits or all three low bits, whether the original value of a,b,c
 *	is almost all zero or is uniformly distributed,
 *	If mix() is run forward or backward, at least 32 bits in a,b,c
 *	have at least 1/4 probability of changing.
 *	If mix() is run forward, every bit of c will change between 1/3 and
 *	2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
 *	mix() takes 36 machine instructions, but only 18 cycles on a superscalar
 *	machine (like a Pentium or a Sparc).  No faster mixer seems to work,
 *	that's the result of my brute-force search.  There were about 2^68
 *	hashes to choose from.  I only tested about a billion of those.
 */

#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

/* hash() -- hash a variable-length key into a 32-bit value
  k       : the key (the unaligned variable-length array of bytes)
  len     : the length of the key, counting by bytes
  initval : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Every 1-bit and 2-bit delta achieves avalanche.
About 6*len+35 instructions.
The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.
If you are hashing n strings (ub1 **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hash( k[i], len[i], h);
By Bob Jenkins, 1996.  bob_jenkins@compuserve.com.  You may use this
code any way you wish, private, educational, or commercial.  It's free.
See http://ourworld.compuserve.com/homepages/bob_jenkins/evahash.htm
Use for hash table lookup, or anything where one collision in 2^^32 is
acceptable.  Do NOT use for cryptographic purposes.
*/

unsigned int hashCalc(const char* k, int length, unsigned int initval)
{
   register ub4 a,b,c,len;

   /* Set up the internal state */
   len = length;
   a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
   c = initval;         /* the previous hash value */

   /*---------------------------------------- handle most of the key */
   while (len >= 12)
   {
      a += (k[0] +((ub4)k[1]<<8) +((ub4)k[2]<<16) +((ub4)k[3]<<24));
      b += (k[4] +((ub4)k[5]<<8) +((ub4)k[6]<<16) +((ub4)k[7]<<24));
      c += (k[8] +((ub4)k[9]<<8) +((ub4)k[10]<<16)+((ub4)k[11]<<24));
      mix(a,b,c);
      k += 12; len -= 12;
   }
   /*------------------------------------- handle the last 11 bytes */
   c += length;
   switch(len)              /* all the case statements fall through */
   {
   case 11: c+=((ub4)k[10]<<24);
   case 10: c+=((ub4)k[9]<<16);
   case 9 : c+=((ub4)k[8]<<8);
      /* the first byte of c is reserved for the length */
   case 8 : b+=((ub4)k[7]<<24);
   case 7 : b+=((ub4)k[6]<<16);
   case 6 : b+=((ub4)k[5]<<8);
   case 5 : b+=k[4];
   case 4 : a+=((ub4)k[3]<<24);
   case 3 : a+=((ub4)k[2]<<16);
   case 2 : a+=((ub4)k[1]<<8);
   case 1 : a+=k[0];
     /* case 0: nothing left to add */
   }
   mix(a,b,c);
   /*-------------------------------------------- report the result */
   return c;
}
/*********************************************
 * End Hash function by Bob Jenkins from DDJ *
 *********************************************/




/***********************************
 * Begin Module specific functions *
 ***********************************/

/* Function highestOnBit
 *	This function goes through all "target" bits and returns
 *	the index of the highest bit that is "on" or a "1".  This
 *	is used by the functions that need to determine the actual
 *	table size from any given size.
 *
 *	Returns:
 *		> 0	- Index of the highest bit that is "on" or "1".
 *		-1	- No "on" bits found
 */
static int getHighestOnBit(unsigned int target){
	int i = 0;
	int lastSeenBit = 0;
	unsigned int bitmask = 1;

	if(0 == target)
		return -1;

	// examine all bits
	while(bitmask){
		// if the current bit is a 1, save the bit position in numeric form
		if(target & bitmask)
			lastSeenBit = i;
		
		// examine next bit during the next loop
		bitmask = bitmask << 1;
		i++;
	}

	return lastSeenBit;
}

/* Function getRealTableSize
 *	The hash tables always allocates enough memory to hold a specified
 *	size, although it also makes sure that the actual table size
 *	is some power of 2.  This function is used to calculate such
 *	size since the calculation is used in more than one function.
 *
 *	To find this particular value, all bits of the specified
 *	size is examined.  A variable (lastSeenBit) keeps track of 
 *	the highest bit where a "1" is seen.
 *
 *	When all bits have been processed, the lastSeenBit variable 
 *	can be used to figure out what (power of 2) size table may be able
 *	to hold the specified size.  A table size of 2^lastSeenBit makes 
 *	sure that the table can hold almost the specified table size.
 *	If the specified size is some of power 2, 2^lastSeenBit will fit
 *	perfectly.  However, if the specified size is not some power
 *	of 2, then a 2^lastSeenBit will not hold all possible elements,
 *	but 2^(lastSeenBit+1) will.
 *
 *	Returns:
 *		> 0	- Index of the highest bit that is "on" or "1".
 *		-1	- No "on" bits found
 */
static unsigned int getRealTableSize(unsigned int size){
	int i = 0;
	int lastSeenBit = 0;
	unsigned int bitmask = 1;
	int realSize;
	
	if(size == 0){
		return 0;
	}
	
	lastSeenBit = getHighestOnBit(size);
	
	// use 2^lastSeenBit as the preliminary table size
	realSize = 1 << lastSeenBit;
	
	// if the specified size is still larger than the bit counted
	// size, use the 2^(lastSeenBit+1) as the table size
	if(size - realSize){
		return realSize << 1;
	}else
		return realSize;
}

static void clearHashTableImp(HashTableImp* table, Destructor func){
	
	// Free memory used by whatever pointed to by the value pointer 
	// in the HashElementImp array
	int i;
	for(i = 0; i < table->maxSize; i++){
		if(func)
			func(table->storage[i].value);

		if(table->copyKeyNames)
			table->storage[i].stringName = NULL;
	}

	// Clear all strings in the string table
	if(table->strTable)
		strTableClear(table->strTable);

	if(table->performance)
		memset(&table->performance, 0, sizeof(HashPerformanceImp));
}

static void destroyHashTableImp(HashTableImp* table, Destructor func, int partialOnly){
	// free all stored values if neccessary
	clearHashTableImp(table, func);

	// free memory being used to hold all key/value pairs
	free(table->storage);
	
	// Do not destroy the string table if the hash table is only being partially destroyed.
	if(table->strTable && !partialOnly)
		destroyStringTable(table->strTable);
	
	if(table->performance){
		free(table->performance);
	}

	if(partialOnly)
		memset(table, 0, sizeof(HashTableImp));
	else
		free(table);
}
/*********************************
 * End Module specific functions *
 *********************************/


HashTable createHashTable(){
	HashTableImp* table = calloc(1, sizeof(HashTableImp));
	return (HashTable)table;
}

/* Function initHashTable
 *	Allocates memory for hash table stroage and initializes
 *	all other hash table variables to appropriate values.
 *
 *	Specified size and actual table size:
 *	This function always allocates a table with a number of elements
 *	that is some power of 2.  It also ensures that it can hold at least 
 *	the number of elements specified by "size".
 *
 *	Parameters:
 *		table - a valid hash table to operate on
 *		size - the # of elements the table is expected to hold
 */
void initHashTable(HashTableImp* table, unsigned int size){
	assert(table);	

	if(size == 0){
		// FIXME!! destroy table to be on the safe side
		memset(table, 0, sizeof(HashTableImp));
		return;
	}

	table->maxSize = getRealTableSize(size);
	
	// allocate memory for the table
	table->storage = malloc(table->maxSize * sizeof(HashElementImp));
	memset(table->storage, 0, table->maxSize * sizeof(HashElementImp));
	
	// init other hash table variables
	table->size = 0;
}

HashTable hashTableCreate(int initialSize, HashTableMode mode){
	HashTable	table;

	table = createHashTable();
	initHashTable(table, initialSize);
	hashSetMode(table, mode);

	return table;
}

// hashFindIndex return codes
#define	HASHFIND_FOUND_EMTPY_ENTRY 0
#define HASHFIND_FOUND_EXISTING_ENTRY 1
#define HASHFIND_ERROR 2

static int hashFindIndex(HashTableImp* table, const char* key, int* finalStorageIndex, int* finalHashValue, int countDeletedAsEmpty){
	int hashvalue;
	int storageIndex;
	int hashCount = 0;
	int returnCode;
	const char* operationKey;
	char upperCasedKey[4096];	// WARNING! case insensitive strings cannot be longer than this!
	
	if(table->caseInsensitive){
		strcpy(upperCasedKey, key);
		strupr(upperCasedKey);
		operationKey = upperCasedKey;
	}else
		operationKey = key;

	if(table->performance)
		table->performance->findIndexCount++;

	// try to find a slot that is not being used
	hashvalue = 0;
	while(1){
		hashvalue = hashCalc(operationKey, strlen(operationKey), hashvalue);
		storageIndex = hashvalue & hashmask(table->maxSize);

		if(table->performance)
			table->performance->hashCount++;
		
		// Make sure the hash value is valid.
		//	The hash value should never become 0.  If it does become 0 through some magical
		//	means, this will stop the search from going into an infinite loop, since 0 is the
		//	the number the "hashing" processing begins with.
		if(INVALID_HASHVAL == hashvalue){
			printf("Invalid HashValue %i generated while looking for index of key \"%s\"\n", 
				INVALID_HASHVAL, key);
			returnCode = HASHFIND_ERROR;
			goto exit;
		}
		
		// Found emtpy slot?
		// Used slots are not emtpy slots by definition.
		if(INVALID_HASHVAL == table->storage[storageIndex].hashValue){
			returnCode = HASHFIND_FOUND_EMTPY_ENTRY;
			goto exit;
		}

		// Deleted slots should be considered emtpy slots only during insertion.
		if(countDeletedAsEmpty && table->storage[storageIndex].deleted){
			returnCode = HASHFIND_FOUND_EMTPY_ENTRY;
			goto exit;
		}
		

		// Identical hash values?  This most likely indicates the given key
		// already exists in the table.
		if(hashvalue == table->storage[storageIndex].hashValue && !table->storage[storageIndex].deleted){

			// Check that the hash value generated is unique for any key if requested...
			//	This cannot be done unless the hash table has a copy of all the key names.
			//	This does not need to be done if the feature is not requested.
			if(table->copyKeyNames && table->verifyHashValUniquenessOnInsertion){
				// If the strings are the same, we've found the correct index.
				if(table->caseInsensitive){
					if(stricmp(table->storage[storageIndex].stringName, key) == 0){
						returnCode = HASHFIND_FOUND_EXISTING_ENTRY;
						goto exit;
					}
				}else{
					if(strcmp(table->storage[storageIndex].stringName, key) == 0){
						returnCode = HASHFIND_FOUND_EXISTING_ENTRY;
						goto exit;
					}
				}
				
					
				// If duplicate hash values are allowed, try to generate another hash value
				if(table->allowDuplicateHashVal)
					continue;
				
				// otherwise, the hash value confilct is impossible to resolve.  Do no do anything else.
				// We already know the stored element and the given key name has the same 
				// hash value.  If key strings are not the same, more than one keyname can 
				// generate the same hash value.  We know for sure then that the hash value 
				// is not unique.
				// If this should ever happen, it would be possible to have the hash table
				// continue functioning normally by generating it's hash value again.
				printf("Hash value conflict!  Both \"%s\" and \"%s\" produced a hash value of %i\n",
					table->storage[storageIndex].stringName, key, table->storage[storageIndex].hashValue);
				returnCode = HASHFIND_ERROR;
				goto exit;
			}
			
			returnCode = HASHFIND_FOUND_EXISTING_ENTRY;
			goto exit;
		}
	}

exit:
	if(finalHashValue)
		*finalHashValue = hashvalue;
	if(finalStorageIndex)
		*finalStorageIndex = storageIndex;
	return returnCode;
}

/* Function hashAddElement
 *	Attempts to add a key/value pair to the specified table.
 *
 *	Parameters:
 *		table	-	the hash table to insert the pair
 *		key		-	the key to be used for retrival of the specified value later
 *		value	-	the value to be stored in association to the key
 *
 *	Returns:
 *		hash element handle	-	the handle to the hash element that represents
 *								the added hash table entry
 *
 *		NULL	-	operation failed
 *		Possible reasons:
 *			- Invalid hash value generated
 *			- Given key already exists
 */
HashElement hashAddElement(HashTableImp* table, const char* key, void* value){
	int hashCount = 0;
	int indexIncCount = 0;
	int hashvalue;
	int storageIndex;
	int hashFindResult;
	assert(key);

	// Table needs to be resized if it is over 75% full to:
	//	1. Maintain reasonable hash table performance
	//	2. Ensure that bad searches can hit an invalid entry and end the search quickly.
	if(table->size >= ((table->maxSize >> 1) + (table->maxSize >> 2)) || table->size >= table->maxSize - 1){
		// Attempt to resize the hashtable
		if(!hashResize(table, table->maxSize * 2))
			// If the table can't be resized, there is no way to add
			// another element into the table.  Operation failed.
			return NULL;
	}

	if(table->performance)
		table->performance->elementsAdded++;

	// Try to find an appropriate location to place the new element.
	hashFindResult = hashFindIndex(table, key, &storageIndex, &hashvalue, 1);

	// If an empty slot wasn't found...
	if(HASHFIND_FOUND_EMTPY_ENTRY != hashFindResult){

		// because an entry with the specified key already exists...
		if(HASHFIND_FOUND_EXISTING_ENTRY == hashFindResult){
			// and the table is supposed to overwrite existing entries...
			if(table->overwriteExistingValue){
				// overwrite the existing entry by replacing the old value with the new value.
				table->storage[storageIndex].value = value;
				return &(table->storage[storageIndex]);
			}
		}

		return NULL;
	}
	
	// Set hash element variables
	table->storage[storageIndex].deleted = 0;
	if(table->copyKeyNames){
		if(table->deepCopyKeyNames)
			// Add a copy of the key into the string table.
			// Store the pointer to the copy with the hash element.
			table->storage[storageIndex].stringName = (const)strTableAddString(table->strTable, key);
		else
			table->storage[storageIndex].stringName = (const)key;
	}

	table->storage[storageIndex].value = value;
	table->storage[storageIndex].hashValue = hashvalue;
	table->size++;

	//printf("Adding \"%s\" took %i hashes + %i index increments\n", key, hashCount, indexIncCount);
	//printf("Adding \"%s\" took %i hashes\n", key, hashCount);

	return &(table->storage[storageIndex]);
}

HashElement hashAddInt(HashTableImp* table, const char* key, int value){

	return hashAddElement(table,key,(void *)value);
}

/* Function hashRemoveElement
 *	Attempts to remove a key/value pair to the specified table.
 *
 *	Returns:
 *		some value - associated value of deleted entry.
 *		NULL - Specified entry not found or deleted entry has associated value of NULL.
 */
void* hashRemoveElement(HashTableImp* table, const char* key){
	HashElementImp* element = hashFindElement(table, key);

	if(table->performance)
		table->performance->elementsRemoved++;

	if(element){
		table->size--;
		element->deleted = 1;
		return element->value;
	}else
		return NULL;
}

/* Function hashFindValue
 *	Returns the value that's associated with the given key in the
 *	given table.
 *
 *	Returns:
 *		some value - associated value retrived
 *		NULL - key not found in table
 *		Note that it will be impossible to differenciate between NULL and
 *		a stored value of 0.
 */
void* hashFindValue(HashTableImp* table, const char* key){
	int storageIndex;
	assert(table && key);

	if(table->performance)
		table->performance->elementsLookedup++;

	// Is it possible to find a valid index for the given key?
	if(HASHFIND_FOUND_EXISTING_ENTRY == hashFindIndex(table, key, &storageIndex, NULL, 0))
		return table->storage[storageIndex].value;
	else
		return NULL;
}

/* Function hashFindIndex
 *	Returns the integer value that's associated with the given key in the
 *	given table.
 *
 *	Returns:
 *		success or failure
 *		if success, returns value in index
 */
bool hashFindInt(HashTableImp* table, const char* key,int *index){
	int storageIndex;
	assert(table && key);

	if(table->performance)
		table->performance->elementsLookedup++;

	// Is it possible to find a valid index for the given key?
	if(HASHFIND_FOUND_EXISTING_ENTRY == hashFindIndex(table, key, &storageIndex, NULL, 0))
	{
		*index = (int)table->storage[storageIndex].value;
		return 1;
	}
	return 0;
}

/*
// Although extremely unlikely, it is still possible that two
// different keys produces the same hash value.  To be on the
// safe side, optionally verify that the key is really the one
// being looked for using a string compare.
 */
/* Function hashFindElement
 *	Returns a hash element handle which has, related to it, a hash value,
 *	a value, and possible a string name.
 *
 */

HashElement hashFindElement(HashTableImp* table, const char* key){
	int storageIndex;
	assert(table && key);

	if(table->performance)
		table->performance->elementsLookedup++;

	// Is it possible to find a valid index for the given key?
	if(HASHFIND_FOUND_EXISTING_ENTRY == hashFindIndex(table, key, &storageIndex, NULL, 0))
		return &table->storage[storageIndex];
	else
		return NULL;
}

/* Function hashResize
 *	This function "resizes" a hash table by creating a new table
 *	then reinserting all the elements using the element's stored
 *	hashvalue.
 *
 *	doesn't handle hash collisions during element insertions yet.
 */
bool hashResize(HashTableImp* table, unsigned int newSize){
	int i;
	HashTableImp newTable;
	HashElementImp* element;
	int hashvalue;
	int storageIndex;
	assert(table);

	// Check if it is okay to proceed with resizing
	//	If the new size is 0, destroy just about everything except
	//	for the memory used by the table itself.
	if(0 == newSize)
		destroyHashTableImp(table, NULL, 1);

	//	Can't resize the table if it's impossible to recalcuate hash values.
	if(!table->copyKeyNames)
		return false;

	// Make a shallow copy of the table to retain all modes of the old table.
	memcpy(&newTable, table, sizeof(HashTableImp));

	// Make it an emtpy table with the requested size.
	initHashTable(&newTable, newSize);

	// re-insert all valid hash elements
	for(i = 0; i < table->maxSize; i++){
		element = &table->storage[i];

		// skip invalid and deleted elements
		if(INVALID_HASHVAL != element->hashValue && !element->deleted){

			// Find a place to put this element in the new table.
			if(HASHFIND_ERROR == hashFindIndex(&newTable, element->stringName, &storageIndex, &hashvalue, 1))
				// The new table should be larger than the old one.  It should always be possible to insert the
				// entry.
				assert(0);
			

			// Update the various fields of the new element
			memcpy(&newTable.storage[storageIndex], element, sizeof(HashElementImp));
			newTable.storage[storageIndex].hashValue = hashvalue;
			newTable.size++;
		}
	}

	// Free the memory used to hold the old hash elements
	free(table->storage);

	// Make the old table a shallow copy of the new table
	memcpy(table, &newTable, sizeof(HashTableImp));

	return true;
}

static HashTableImp* hashMergeOldTable;
static int hashMergeRetainOldValues;
static int insertNewElements(HashElementImp* element){
	hashAddElement(hashMergeOldTable, element->stringName, element->value);
	return 1;
}

HashTable hashMerge(HashTableImp* oldTable, HashTableImp* newTable){
	// The tables can't be merged if the new table doesn't keep a copy of the
	// key names anywhere.
	if(!newTable->copyKeyNames && !newTable->deepCopyKeyNames)
		return 0;

	if(oldTable == NULL){
		oldTable = createHashTable();
		hashSetMode(oldTable, FullyAutomatic);
		initHashTable(oldTable, 16);
	}

	hashMergeOldTable = oldTable;
	hashForEachElement(newTable, insertNewElements);
	return oldTable;
}


static HashTable hashCopyTarget;
static hashCopyElement hashCopyElementCB;

// This function is called for every valid element in the source hash table.
static void copyHashTableHelper(HashElementImp* sourceElement){
	HashElementImp* targetElement;

	// Add an entry with the matching key into the hash table.
	targetElement = hashAddElement(hashCopyTarget, sourceElement->stringName, NULL);
	if(hashCopyElementCB){
		hashCopyElementCB(sourceElement, targetElement);	
	}else{
		targetElement->value = sourceElement->value;
	}
}

HashTable copyHashTable(HashTableImp* source, HashTableImp* target, hashCopyElement copyElement){
	// The tables can't be merged if the new table doesn't keep a copy of the
	// key names anywhere.
	if(!source->copyKeyNames && !target->deepCopyKeyNames)
		return 0;

	if(target == NULL){
		target = createHashTable();
		hashSetMode(target, FullyAutomatic);
		initHashTable(target, 16);
	}

	hashCopyTarget = target;
	hashCopyElementCB = copyElement;
	hashForEachElement(source, (HashElementProcessor)copyHashTableHelper);
	return target;
}
/* Function hashForEachElement()
 *	
 *
 *
 *
 *
 */
void hashForEachElement(HashTableImp* table, HashElementProcessor processor){
	int i;
	HashElementImp* element;

	// Examine all entries in the hash table.
	for(i = 0; i < table->maxSize; i++){
		 element = &table->storage[i];

		 // Send in-use elements to the callback function.
		 if(element->hashValue != INVALID_HASHVAL && !element->deleted){
			processor(element);
		 }
	}
}

int hashGetSize(HashTableImp* table){
	assert(table);
	return table->size;
}

int hashGetMaxSize(HashTableImp* table){
	assert(table);
	return table->maxSize;
}


/*************************************************
 * Begin HashTable mode query/alteration functions
 *	HashTableMode implementation notes:
 *		Although almost all values available in the HashTableMode enumeration has
 *		a corresponding bit member in the HashTableImp structure, it is decided
 *		that the two will not have a strict one-to-one binding where the user
 *		would directly specify which bits are set in the implemetation structure.
 *		The reason is that some modes has interdepencies.  It is undesirable for
 *		the user to put the HashTable into a mode where it will most likely
 *		perform some illegal operation, like having the allowDuplicateHashVal bit
 *		on while having the copyKeyNames bit off.
 *
 *		Currently, each HashTableMode bit represents one or more bits in the
 *		HashTableImp structure.  These are strict one-to-one bindings in a way.
 *		Each valid HashTableMode value is the added/or'ed result of the different
 *		bits.  The actual implementation of the mode setting/querying functions
 *		can then only translate between the strict one-to-one bindings mentioned
 *		above.  It trusts that the valid HashTableModes would set the correct bits.
 *
 *		All this may seem overly complicted for the few modes that the HashTable
 *		supports.  But when the alternative is to have a bunch of mode validity
 *		checks, this seems less painful.
 *
 */
HashTableMode hashGetMode(HashTableImp* table){
	HashTableMode mode;

	if(table->copyKeyNames)
		mode |= COPY_KEY_NAMES_BIT;

	if(table->deepCopyKeyNames)
		mode |= DEEP_COPY_KEY_NAMES_BIT;

	if(table->allowDuplicateHashVal)
		mode |= ALLOW_DUPICATE_HASH_VAL_BIT;

	if(table->verifyHashValUniquenessOnInsertion && table->verifyHashValUniquenessOnRetrieval)
		mode |= VERIFY_HASH_VAL_UNIQUENESS_BIT;

	if(table->caseInsensitive)
		mode |= CASE_INSENSITIVE_BIT;
	
	if(table->overwriteExistingValue)
		mode |= OVERWRITE_EXISTING_VALUE_BIT;

	if(table->performance)
		mode |= TRACK_PERFORMANCE_BIT;
	return mode;
}


int hashSetMode(HashTableImp* table, HashTableMode mode){
	if(table->size != 0)
		return false;

	if(mode & COPY_KEY_NAMES_BIT){
		table->copyKeyNames = 1;
	}

	// Once deep copy gets turned on, it cannot be turned off.
	if(mode & DEEP_COPY_KEY_NAMES_BIT){
		table->deepCopyKeyNames = 1;
		
		// create and initialize the string table member to hold all the stirngs
		table->strTable = createStringTable();
		initStringTable(table->strTable, 4096);	// have the string table allocate 4k at a time... overkill?
	}
	
	if(mode & ALLOW_DUPICATE_HASH_VAL_BIT){
		table->allowDuplicateHashVal = 1;
	}
	
	if(mode & VERIFY_HASH_VAL_UNIQUENESS_BIT){
		table->verifyHashValUniquenessOnInsertion = 1;
		table->verifyHashValUniquenessOnRetrieval = 1;		
	}

	if(mode & CASE_INSENSITIVE_BIT){
		table->caseInsensitive = 1;
	}
	
	if(mode & OVERWRITE_EXISTING_VALUE_BIT){
		table->overwriteExistingValue = 1;
	}

	// Performance tracking can be turned on/off.
	if(mode & TRACK_PERFORMANCE_BIT){
		table->performance = calloc(1, sizeof(HashPerformanceImp));
	}else{
		if(table->performance)
			free(table->performance);
		table->performance = NULL;
	}

	return true;
}
/*
* End HashTable mode query/alteration functions
************************************************/

void destroyHashTable(HashTableImp* table){
	assert(table);
	destroyHashTableImp(table, NULL, 0);
}

void clearHashTable(HashTableImp* table){
	memset(table->storage, 0, table->maxSize * sizeof(HashElementImp));
	table->size = 0;
	if(table->strTable)
		strTableClear(table->strTable);
}

void clearHashTableEx(HashTableImp* table, Destructor func){
	int i;
	for(i = 0; i < table->size; i++){
		if(!table->storage[i].deleted && table->storage[i].hashValue){
			if(func)
				func(table->storage[i].value);
			else
				free(table->storage[i].value);
		}
	}
	clearHashTable(table);
}

void hashDumpTable(HashTableImp* table){
	HashElementImp* element;
	int i;

	for(i = 0; i < table->maxSize; i++){
		element = &table->storage[i];
		
		// skip invalid and deleted elements
		if(INVALID_HASHVAL == element->hashValue){
			printf("Element %i is empty\n", i);
		}else if(element->deleted){
			printf("Element %i is empty\n", i);
		}else{
			printf("Element %i has key: \"%s\" value: \"%i\"\n", i, element->stringName, element->value);
		}
	}
}

void testHashTable(){
	HashTable table;
	int i;
	bool result;
	int keysSize = 23;
	char* keys[] = {"one", "two", "three", "four", "five", "somekey", "this", "is", "blaa", "test",
			"of course", "the sun", "hashbrown", "tablesalt", "which", "capable", "really", "fastfood",
			"insertions", "android", "look up and down", "POWER PUNCH", "ENERGY BLAST"};

	/***************************************************************
	 * Normal operation test
	 */
	// create valid table for use
	table = createHashTable();
	initHashTable(table, keysSize);

	// insert some key/value pairs
	for(i = 0; i < keysSize; i++){
		printf("Inserting key:\"%s\" Value: %i\n", keys[i], i);
		result = hashAddElement(table, keys[i], i);
		if(result)
			printf("Key added\n");
		else
			printf("Error adding key\n");
	}

	// try to find the key/value pairs
	for(i = 0; i < keysSize; i++){
		printf("Finding key \"%s\"\n", keys[i]);
		printf("Found Value: %i\n", hashFindValue(table, keys[i]));
	}

	destroyHashTable(table);
	/*
	 * Normal operation test
	 ***************************************************************/

	/***************************************************************
	 * Entry Deletion test
	 */
	// create valid table for use
	table = createHashTable();
	hashSetMode(table, FullyAutomatic);
	initHashTable(table, 4);
	
	printf("\n\nTesting Hash Element deletion\n");

	// Insert key 1
	printf("Inserting key 1\n");
	hashAddElement(table, keys[1], 1);

	// Insert key 4
	printf("Inserting key 4\n");
	hashAddElement(table, keys[4], 4);	
	
	// Insert key 12 which results in collisions with key 1 and key 4
	printf("Inserting key 12\n");
	hashAddElement(table, keys[12], 12);

	printf("\nLooking up key 1 results in: %i\n", hashFindValue(table, keys[1]));
	printf("Looking up key 4 results in: %i\n", hashFindValue(table, keys[4]));
	printf("Looking up key 12 results in: %i\n", hashFindValue(table, keys[12]));
	
	// Remove key 1 and key 4 and try to lookup key 12
	printf("\nRemoving key 1\n");
	hashRemoveElement(table, keys[1]);
	printf("Removing key 4\n");
	hashRemoveElement(table, keys[4]);

	printf("Looking up key 1 results in: %i\n", hashFindValue(table, keys[1]));
	printf("Looking up key 4 results in: %i\n", hashFindValue(table, keys[4]));
	printf("Looking up key 12 results in: %i\n", hashFindValue(table, keys[12]));  
	/* Key 12 should return 12.  This verifies that key lookup is done
	 * correctly after deletion of a collision causing entries.
	 */

	printf("\nAdding back key 4\n");
	hashAddElement(table, keys[4], 4);
	printf("Adding back key 1\n");
	hashAddElement(table, keys[1], 1);

	printf("Looking up key 1 results in: %i\n", hashFindValue(table, keys[1]));	
	printf("Looking up key 4 results in: %i\n", hashFindValue(table, keys[4]));
	printf("Looking up key 12 results in: %i\n", hashFindValue(table, keys[12]));
	clearHashTable(table);
	/* Key 1 results in: 1, Key 4 results in: 4, Key 12 results in: 12.
	 * This verifies that the deleted entries can be re-populated they become
	 * deleted.
	 *
	 */
	/*
	 * Entry Deletion test
	 ***************************************************************/

	/***************************************************************
	 * Resize test
	 */
	// The hash table created in entry deletion test is 4.
	// Try to a more elements than it can hold.
	// insert some key/value pairs
	printf("\n\nHashTable resize test\n");
	printf("Initial HashTable size is: %i\n", ((HashTableImp*)table)->size);
	printf("Initial HashTable max size is: %i\n", ((HashTableImp*)table)->maxSize);

	for(i = 0; i < keysSize; i++){
		printf("Inserting key:\"%s\" Value: %i\n", keys[i], i);
		result = hashAddElement(table, keys[i], i);
		
		if(result)
			printf("Key added\n");
		else
			printf("Error adding key\n");
	}


	// try to find the key/value pairs
	for(i = 0; i < keysSize; i++){
		printf("Finding key \"%s\"\n", keys[i]);
		printf("Found Value: %i\n", hashFindValue(table, keys[i]));
	}

	printf("Final HashTable size is: %i\n", ((HashTableImp*)table)->size);
	printf("Final HashTable max size is: %i\n", ((HashTableImp*)table)->maxSize);
	destroyHashTable(table);
	/*
	 * Resize test
	 ***************************************************************/
}

int hashGetHashValue(HashElementImp* element){
	assert(element);
	return element->hashValue;
}

void* hashGetValue(HashElementImp* element){
	assert(element);
	return element->value;
}

void hashSetValue(HashElementImp* element, void* value){
	assert(element);
	element->value = value;
}

char* hashGetKey(HashElementImp* element){
	return element->stringName;
}
#pragma warning(pop)