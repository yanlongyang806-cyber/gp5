#pragma once

// Is there an easy way around having this include?
#include "StashTable.h"

//-------------------------------------------------------------------------------
// A ThrashTable is a StashTable with a size limit that frees stuff automatically
//
//
// ... and a bunch of stuff to maintain that limit
//-------------------------------------------------------------------------------
typedef struct ThrashTableImp* ThrashTable;
typedef void (*ThrashDestructor)(void * value);
typedef void (*ThrashSortedScanner)(void * key, void * value, U32 rank, S64 last_used, S64 score, void * userData);

// Minimal set of functions for now

// Make a ThrashTable!  It makes a stashtable, so it needs some stash creation stuff.  It automatically destroys its
// entries, so a destructor is good (it uses free if you don't enter one).  A size limit is required--how else would
// it know when to destroy things?
ThrashTable thrashCreate(U32 initialCount, StashTableMode stashMode, StashKeyType keyType,
						 U32 keyLength, U64 sizeLimit, ThrashDestructor destructor);

// Make a ThrashTable!  The options to tune its auto-removal will be available with this function.
ThrashTable thrashCreateEx(U32 initialCount, StashTableMode stashMode, StashKeyType keyType,
						   U32 keyLength, U64 sizeLimit, ThrashDestructor destructor,
						   F32 maximum_interarrival_seconds, F32 minimum_interarrival_seconds, F32 newbie_seconds);

// There's plenty of stuff for it to destroy, including whatever you left in there.
void thrashDestroy(SA_PARAM_NN_VALID ThrashTable table);
void thrashClear(SA_PARAM_NN_VALID ThrashTable table);

// Just like the add function for a stashtable, except there's also a size parameter.  If you're operating at full capacity,
// which is kind of the point, an add is also a call or two to the destructor in disguise.
bool thrashAdd(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID void* pKey,
			   SA_PARAM_NN_VALID const void* pValue, U32 size, bool bOverwriteIfFound);

// Like the find functions for a stashtable.  When searching for something to delete, the record of calls to this function is used.
// Not much of that record is actually stored, but it's the thought that counts.
bool thrashFind(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID const void* pKey, void** ppValue);

// If you don't want to wait for the ThrashTable to remove something, go ahead and do it yourself.
bool thrashRemove(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID const void* pKey);

// Sometime after a find, it might be nice to give the ThrashTable a little hint that you're done with what you found
bool thrashRelease(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID const void* pKey);

// It's nice to know how much it is storing
U64 thrashSize(SA_PARAM_NN_VALID ThrashTable table);

U64 thrashSizeLimit(SA_PARAM_NN_VALID ThrashTable table);

// Mostly for debugging purposes, you just might want to go through the whole table from the safest value down to the value
// that's next in line for removal.
void thrashSortedScan(SA_PARAM_NN_VALID ThrashTable table, SA_PARAM_NN_VALID ThrashSortedScanner scanner, void * userData);

S32 thrashRemoveOld(SA_PARAM_NN_VALID ThrashTable table,
					F32 maxSecondsToKeep,
					F32 maxSecondsToSpend);