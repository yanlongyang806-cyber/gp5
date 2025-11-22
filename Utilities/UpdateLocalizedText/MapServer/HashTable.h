/* File HashTable
 *	Provides an interface to manipulate HashTables that has a [supposed] average
 *	lookup time of O(1).  The table makes use of a hash function found in
 *	Dr. Dobb's journal.
 *
 *	The actual HashTable structure can be found in the .c file.  It has been
 *	intentionally hidden to avoid direct access.
 *
 *	Creating a HashTable:
 *		First, call createHashTable() to get a HashTable descriptor.  The descriptor
 *		represents one instance of the HashTable.  Then, call initHashTable() with
 *		the expected # of elements to be stored.  Note that the a slightly larger
 *		table than the requested size will be created.
 *
 *	Adding a key/value pair:
 *		Use the hashAddElement() function.  Details can be found in the implementation
 *		file.
 *
 *	Looking up a key/value pair:
 *		Use the hashFindElement() or the hashFindValue() function.  Note that although
 *		hashFindValue seems more convenient, it is not possible to differentiate
 *		from the returned value whether they key cannot be found or the associated
 *		value is 0.  See implementation file for details.
 *
 *	Table traversal:
 *		To look through all entries in a hash table, use the hashForEachElement() function
 *		This function will traverse the hash table and pass each valid element to the
 *		callback function for processing.
 *
 *	
 */

#ifndef HASHTABLE_H
#define HASHTABLE_H

typedef long HashTable;
typedef long HashElement;
typedef void (*Destructor)(void* value);

#ifndef __cplusplus
	#ifndef bool
		typedef int bool;
		#define true 1
		#define false 0
	#endif
#endif

/* Enum HashTableMode
 *	Defines several possible HashTable operation modes.
 *	
 *	Default:
 *		If no alternate modes are set after a HashTable has been initialized,
 *		it is in this mode.  All functionality mentioned in other modes
 *		are inactive.  This makes the HashTable use less memory and have
 *		a slightly faster insertion/lookup time.  But the HashTable might
 *		become "corrupted" if it encounters two strings with the same hash
 *		value.  If the keys are known ahead of time not to cause such
 *		an conflict, this mode is the best mode to be in.
 *	
 *	CopyKeyNames:
 *		Specifies whether the HashTable should copy the key string when
 *		a new element is inserted.  Note that when this mode is activated,
 *		the HashTable only keeps a shallow copy of the key string by default.
 *		It is possible to cause the HashTable to crash the program if the
 *		HashTable attempts to operate on the key strings when the string(s)
 *		have been free/altered by another part of the program.
 *
 *	DeepCopyKeyNames:
 *		Specifies that the HashTable should make a deep copy of the key
 *		name, instead of the default shallow copy.  This will avoid the
 *		possible crash bug as mentioned above.  This feature implies
 *		that the CopyKeyNames mode is activated on and will activate it
 *		if not already.
 *
 *	AllowDuplicateHashVal:
 *		In the unlikely event that different key string generate the
 *		same hash value, this mode will allow the hash table to cope
 *		with the condition at the expense of some insertion/lookup
 *		speed.  This feature implies that the VerifyHashValueUniqueness
 *		and CopyKeyNames modes are activated and will activate them if
 *		not already.
 *
 *	VerifyHashValueUniqueness:
 *		Check if hash values are unique during insertion and lookups by
 *		comparing the stored key string and some specified key string.
 *		If non-unique hash values are detected, a short error message
 *		will be printed into the console noting the strings that
 *		generated the duplicate hash value.  This feature implies that the
 *		CopyKeyNames mode is activated and will activate it if not already
 *
 *	FullyAutomatic:
 *		Turns on all available modes.  This makes sure that the HashTable
 *		cannot be crashed/corrupted by an external module through normal
 *		means.  All insertions and lookups will be successful.
 *		
 *
 *	See .c file for implementation notes.
 */
#define COPY_KEY_NAMES_BIT				(1 << 0)
#define DEEP_COPY_KEY_NAMES_BIT			(1 << 1)
#define ALLOW_DUPICATE_HASH_VAL_BIT		(1 << 2)
#define VERIFY_HASH_VAL_UNIQUENESS_BIT	(1 << 3)
#define CASE_INSENSITIVE_BIT			(1 << 4)
#define OVERWRITE_EXISTING_VALUE_BIT	(1 << 5)
#define TRACK_PERFORMANCE_BIT			(1 << 6)

typedef enum{
	CopyKeyNames =				COPY_KEY_NAMES_BIT,
	DeepCopyKeyNames =			COPY_KEY_NAMES_BIT | DEEP_COPY_KEY_NAMES_BIT,
	AllowDupicateHashVal =		ALLOW_DUPICATE_HASH_VAL_BIT | VERIFY_HASH_VAL_UNIQUENESS_BIT | COPY_KEY_NAMES_BIT,
	VerifyHashValUniqueness =	COPY_KEY_NAMES_BIT | VERIFY_HASH_VAL_UNIQUENESS_BIT,
	FullyAutomatic =			CopyKeyNames | DeepCopyKeyNames | AllowDupicateHashVal | VerifyHashValUniqueness,		
	CaseInsensitive =			CASE_INSENSITIVE_BIT,
	OverWriteExistingValue =	OVERWRITE_EXISTING_VALUE_BIT,
	TrackPerformance =			TRACK_PERFORMANCE_BIT,
} HashTableMode;

// constructor/destructors
HashTable createHashTable();
HashTable hashTableCreate(int initial_size,HashTableMode mode); // BR I added this one because I was tired of calling 3 functions to get a hashtable
void initHashTable(HashTable table, unsigned int size);
void clearHashTable(HashTable table);
void clearHashTableEx(HashTable table, Destructor func);
void destroyHashTable(HashTable table);


// element insertion/lookup
HashElement hashFindElement(HashTable table, const char* key);
HashElement hashAddElement(HashTable table, const char* key, void* value);
HashElement hashAddInt(HashTable table, const char* key, int value);
void* hashRemoveElement(HashTable table, const char* key);
void* hashFindValue(HashTable table, const char* key);
bool hashFindInt(HashTable table, const char* key,int *index);
bool hashResize(HashTable table, unsigned int newSize);
HashTable hashMerge(HashTable oldTable, HashTable newTable);

typedef void (*hashCopyElement)(HashElement source, HashElement target);
HashTable copyHashTable(HashTable source, HashTable target, hashCopyElement copyElement);


// hash table traversal
/* Function type HashElementProcessor
 *	hashForEachElement expects a function that matches the HashElementProcessor's
 *	signature.
 *
 *	Returns:
 *		0 - Do not continue traversing the hash table.
 *		~0 - Continue traversing the hash table.
 */
typedef int (*HashElementProcessor)(HashElement);
void hashForEachElement(HashTable table, HashElementProcessor processor);

// hash table info
int hashGetSize(HashTable table);
int hashGetMaxSize(HashTable table);

// HashTable mode query/alteration
HashTableMode hashGetMode(HashTable table);
int hashSetMode(HashTable table, HashTableMode mode);

// Hash element info
int hashGetHashValue(HashElement element);
void* hashGetValue(HashElement element);
void hashSetValue(HashElement element, void* value);
char* hashGetKey(HashElement element);


// HashTable debug utilities
void hashDumpTable(HashTable table);

// short hash table test
void testHashTable();

// the actual hash function
unsigned int hashCalc(const char* k, int length, unsigned int initval);






#endif
