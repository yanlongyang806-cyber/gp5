#include "BlockEarray.h"
#include "earray.h"
#include "memcheck.h"
#include "patchserverdb.h"
#include "StringTable.h"
#include "TextParserEnums.h"

// The minimum alignment guaranteed by the allocator
#define ALLOCATOR_ALIGNMENT 16

// Block allocator
static void *FastCopyAllocator(void *data, size_t size)
{
	void *ptr;
	if (data)
	{
		char **memory = data;
		char *index = *memory;
		uintptr_t next_index = (uintptr_t)index;
		size_t aligned_size = ALIGNUP(size, ALLOCATOR_ALIGNMENT);
		next_index += size;
		ptr = index;
		*memory = (char *)next_index;
	}
	else
		ptr = malloc(size);
	return ptr;
}

// Determine how much memory FastCopyAllocator() will need to allocate block EArrays.
static void FastCopyAllocateSimulate(size_t count, size_t size, size_t *bytes)
{
	if (!count)
		return;
	*bytes += BLOCK_EARRAY_INTERNAL_OVERHEAD;
	*bytes += count * size;
	*bytes = ALIGNUP(*bytes, ALLOCATOR_ALIGNMENT);
}

// Duplicate a string.
static char *FastStrDup(StringTable table, const char *string)
{
	if (!string)
		return NULL;

	return strTableAddString(table, string);
}

// Recursively calculate DirEntry resource usage.
static void CountDirEntrys(size_t *dir_bytes, size_t *version_bytes, size_t *checkout_bytes, DirEntry *dir)
{
	FastCopyAllocateSimulate(eaSize(&dir->versions), sizeof(FastFileVersion), version_bytes);
	FastCopyAllocateSimulate(eaSize(&dir->checkouts), sizeof(FastCheckout), checkout_bytes);
	FastCopyAllocateSimulate(eaSize(&dir->children), sizeof(FastDirEntry), dir_bytes);
	EARRAY_CONST_FOREACH_BEGIN(dir->children, i, n);
	{
		CountDirEntrys(dir_bytes, version_bytes, checkout_bytes, dir->children[i]);
	}
	EARRAY_FOREACH_END;
}

// Copy a DirEntry, recursively.
static void FastCopyManifestDirEntry(FastDirEntry *dir, FastPatchDB *db, DirEntry *src)
{
	int version_count;
	int checkout_count;
	int dir_count;

	// Copy name (pooled)
	dir->name = src->name;

	// Copy versions.
	dir->versions = NULL;
	version_count = eaSize(&src->versions);
	if (version_count)
	{
		beaSetSizeEx(&dir->versions, sizeof(*dir->versions), NULL, version_count, BEAFLAG_NO_ZERO, FastCopyAllocator, db->version_memory, true MEM_DBG_PARMS_INIT);
		EARRAY_CONST_FOREACH_BEGIN(src->versions, i, n);
		{
			FileVersion *src_version = src->versions[i];
			FastFileVersion *version = dir->versions + i;
			memcpy(version, src_version, sizeof(*version));
		}
		EARRAY_FOREACH_END;
	}

	// Copy checkouts.
	dir->checkouts = NULL;
	checkout_count = eaSize(&src->checkouts);
	if (checkout_count)
	{
		beaSetSizeEx(&dir->checkouts, sizeof(*dir->checkouts), NULL, checkout_count, BEAFLAG_NO_ZERO, FastCopyAllocator, db->checkout_memory, true MEM_DBG_PARMS_INIT);
		EARRAY_CONST_FOREACH_BEGIN(src->checkouts, i, n);
		{
			Checkout *src_checkout = src->checkouts[i];
			FastCheckout *checkout = dir->checkouts + i;
			checkout->author = FastStrDup(db->strings, src_checkout->author);
			checkout->time = src_checkout->time;
			checkout->sandbox = FastStrDup(db->strings, src_checkout->sandbox);
			checkout->branch = src_checkout->branch;
		}
		EARRAY_FOREACH_END;
	}

	// Copy children.
	dir->children = NULL;
	dir_count = eaSize(&src->children);
	if (dir_count)
	{
		beaSetSizeEx(&dir->children, sizeof(*dir->children), NULL, dir_count, BEAFLAG_NO_ZERO, FastCopyAllocator, db->dir_memory, true MEM_DBG_PARMS_INIT);
		EARRAY_CONST_FOREACH_BEGIN(src->children, i, n);
		{
			FastCopyManifestDirEntry(dir->children + i, db, src->children[i]);
		}
		EARRAY_FOREACH_END;
	}
}

// Copy all DirEntrys.
static void patchserverdbFastCopyDirEntries(FastPatchDB *db, PatchDB *src)
{
	size_t dir_bytes = 0;
	size_t version_bytes = 0;
	size_t checkout_bytes = 0;

	// Count memory required, recursively.
	CountDirEntrys(&dir_bytes, &version_bytes, &checkout_bytes, &src->root);

	// Allocate memory.
#define ALLOCATE(type)															\
	db->type ## _memory_base = FastCopyAllocator(NULL, type ## _bytes);			\
	db->type ## _memory_index = db->type ## _memory_base;			\
	db->type ## _memory = &db->type ## _memory_index;							\
	db->type ## _memory_bound = db->type ## _memory_index + type ## _bytes;
	ALLOCATE(dir);
	ALLOCATE(version);
	ALLOCATE(checkout);
#undef ALLOCATE

	// Copy recursively.
	FastCopyManifestDirEntry(&db->root, db, &src->root);

	// Check memory usage.
	assert(db->dir_memory_index <= db->dir_memory_bound);
	assert(db->version_memory_index <= db->version_memory_bound);
	assert(db->checkout_memory_index <= db->checkout_memory_bound);
}

// Copy all checkins.
static void patchserverdbFastCopyCheckins(FastPatchDB *db, PatchDB *src)
{
	int checkin_count;

	db->checkins = NULL;
	checkin_count = eaSize(&src->checkins);
	if (checkin_count)
	{
		beaSetSizeEx(&db->checkins, sizeof(*db->checkins), NULL, checkin_count, BEAFLAG_NO_ZERO, FastCopyAllocator, NULL, true MEM_DBG_PARMS_INIT);
		EARRAY_CONST_FOREACH_BEGIN(src->checkins, i, n);
		{
			Checkin *src_checkin = src->checkins[i];
			FastCheckin *checkin = db->checkins + i;
			checkin->rev = src_checkin->rev;
			checkin->branch = src_checkin->branch;
			checkin->time = src_checkin->time;
			checkin->sandbox = FastStrDup(db->strings, src_checkin->sandbox);
			checkin->incr_from = src_checkin->incr_from;
			checkin->author = FastStrDup(db->strings, src_checkin->author);
			checkin->comment = FastStrDup(db->strings, src_checkin->comment);
		}
		EARRAY_FOREACH_END;
	}
}

// Copy all NamedViews.
static void patchserverdbFastCopyNamedViews(FastPatchDB *db, PatchDB *src)
{
	int view_count;

	db->namedviews = NULL;
	view_count = eaSize(&src->namedviews);
	if (view_count)
	{
		beaSetSizeEx(&db->namedviews, sizeof(*db->namedviews), NULL, view_count, BEAFLAG_NO_ZERO, FastCopyAllocator, NULL, true MEM_DBG_PARMS_INIT);
		EARRAY_CONST_FOREACH_BEGIN(src->namedviews, i, n);
		{
			NamedView *src_namedview = src->namedviews[i];
			FastNamedView *namedview = db->namedviews + i;
			namedview->name = FastStrDup(db->strings, src_namedview->name);
			namedview->branch = src_namedview->branch;
			namedview->rev = src_namedview->rev;
			namedview->comment = FastStrDup(db->strings, src_namedview->comment);
			namedview->sandbox = FastStrDup(db->strings, src_namedview->sandbox);
			namedview->expires = src_namedview->expires;
			namedview->viewed = src_namedview->viewed;
			namedview->viewed_external = src_namedview->viewed_external;
			namedview->dirty = src_namedview->dirty;
		}
		EARRAY_FOREACH_END;
	}
}

// Make a fast copy of the server PatchDB.
//
// This routine uses the following strategies to copy faster than StructCopy():
// -Struct packing (most structs have trailing fields that are NO_AST)
// -Block EArrays instead of regular EArrays, where appropriate
// -Special-purpose memory pooling for DirEntry components
// -String tables for duplicating strings
// -Takes advantage of DirEntry names being pooled
// Currently, all manifests used in the PatchDB and AssetDB are copied in under 500 ms
// using this code.  A typical time is 300-400 ms for a large StarTrek manifest.
FastPatchDB *patchserverdbFastCopy(PatchDB *src, size_t string_size_hint)
{
	FastPatchDB *db = FastCopyAllocator(NULL, sizeof(*db));

	// Copy top-level data.
	db->version = src->version;
	db->branch_valid_since = NULL;
	eaiCopy(&db->branch_valid_since, &src->branch_valid_since);
	db->latest_rev = src->latest_rev;

	// Create string table.
	db->strings = strTableCreate(StrTableDefault, (unsigned)string_size_hint);

	// Copy all DirEntrys.
	patchserverdbFastCopyDirEntries(db, src);

	// Copy checkins.
	patchserverdbFastCopyCheckins(db, src);

	// Copy named views.
	patchserverdbFastCopyNamedViews(db, src);

	return db;
}

// Destructor
AUTO_FIXUPFUNC;
TextParserResult FastPatchDBFixup(FastPatchDB *db, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_DESTRUCTOR:
			free(db->dir_memory_base);
			free(db->version_memory_base);
			free(db->checkout_memory_base);
			db->root.children = NULL;
			db->root.versions = NULL;
			db->root.checkouts = NULL;
			beaDestroy(&db->checkins);
			beaDestroy(&db->namedviews);
			destroyStringTable(db->strings);
			break;
	}
	return PARSERESULT_SUCCESS;
}
