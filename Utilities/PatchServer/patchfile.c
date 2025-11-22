#include "bindiff.h"
#include "crypt.h"
#include "earray.h"
#include "EString.h"
#include "file.h"
#include "fileLoader.h"
#include "hoglib.h"
#include "MemoryMonitor.h"
#include "patchcommonutils.h"
#include "patchdb.h"
#include "patchfile.h"
#include "patchfileloading.h"
#include "patchhal.h"
#include "patchserver.h"
#include "patchserverdb.h"
#include "sysutil.h"
#include "timing.h"
#include "thrashtable.h"
#include "utils.h"
#include "wininclude.h"

#define INITIAL_PATCH_FILE_COUNT 1000

#if 1
static U32 print_size[] = { 64*SMALLEST_DATA_BLOCK,
							16*SMALLEST_DATA_BLOCK,
							 4*SMALLEST_DATA_BLOCK,
							   SMALLEST_DATA_BLOCK };
#else
static U32 print_size[] = { 128*SMALLEST_DATA_BLOCK,
							 32*SMALLEST_DATA_BLOCK,
							  8*SMALLEST_DATA_BLOCK,
							  4*SMALLEST_DATA_BLOCK,
							  2*SMALLEST_DATA_BLOCK,
							    SMALLEST_DATA_BLOCK };
#endif
STATIC_ASSERT(ARRAY_SIZE(print_size)==MAX_PRINT_SIZES)

static ThrashTable	g_patch_table;
static U64			s_loaded_bytes = 0;
static U64			s_decompressed_bytes = 0;

AUTO_CMD_INT(g_patchserver_config.printFileCacheUpdates, printFileCacheUpdates);

const char* patchFileGetUsedName(PatchFile* patch)
{
	return patch->usingOldName ?
				patch->fileName.oldName :
				patch->fileName.name;
}

static void s_freePatchFileData(PatchFile *patch)
{
	int i;
	
	assert(patch->load_state > LOADSTATE_LOADING);

	if(g_patchserver_config.printFileCacheUpdates){
		printfColor(COLOR_RED,
					"FileCache: Freeing \"%s\" (%db comp, %db uncomp, currently %"FORM_LL"db/%"FORM_LL"db)...",
					patchFileGetUsedName(patch),
					patch->compressed.block_len,
					patch->uncompressed.block_len,
					thrashSize(g_patch_table),
					thrashSizeLimit(g_patch_table));
	}

	if(patch->estring)
	{
		SAFE_FREE(patch->compressed.data);
		estrDestroy(&patch->uncompressed.data);
	}

	SAFE_FREE(patch->uncompressed.data);
	SAFE_FREE(patch->compressed.data);

	for(i = 0; i < MAX_PRINT_SIZES; i++)
	{
		SAFE_FREE(patch->uncompressed.prints[i]);
		SAFE_FREE(patch->compressed.prints[i]);
	}

	patch->load_state = LOADSTATE_INFO_ONLY;
	
	if(g_patchserver_config.printFileCacheUpdates){
		printf("done.\n");
	}
}

void patchfileDestroy(PatchFile **patchInOut)
{
	PatchFile* patch;

	PERFINFO_AUTO_START_FUNC();

	patch = SAFE_DEREF(patchInOut);
	if(patch)
	{
		if(patch->load_state != LOADSTATE_LOADING)
		{
			assertmsg(	!eaSize(&patch->requests),
						"Patchfile has waiting requests, but isn't loading!");
			devassert(!eaSize(&patch->special_files));
			// It is conceivable that all child patchfiles could have removed themselves from the array, leaving the array intact.
			eaDestroy(&patch->special_files);

			// Remove references to us from a parent.
			if (patch->special_parent)
			{
				EARRAY_FOREACH_REVERSE_BEGIN(patch->special_parent->special_files, i);
				{
					eaRemoveFast(&patch->special_parent->special_files, i);
				}
				EARRAY_FOREACH_END;
			}

			if(!thrashRemove(g_patch_table, patchInOut)) 
			{
				// thrashRemove calls s_freePatchFileData if the patch is in there.
				
				s_freePatchFileData(patch);
			}
			if(patch->requests)
				eaDestroy(&patch->requests);
			SAFE_FREE(patch->prepend);

			if(patch->halhog)
			{
				patchHALHogFileDestroy(patch->halhog, true);
				patch->halhog = NULL;
			}

			free(patch);
		}
		else
		{
			assertmsg(!patch->estring || patch->nonhogfile, "EString-based patchfile marked 'LOADING' and comes from hogfile!");
			assertmsg(!patch->delete_me, "Attempting to delete a patch multiple times!");
			patch->delete_me = true;
		}
		*patchInOut = NULL;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

#define GIGABYTE 1024*1024*1024
#define GIGABYTELL 1024LL*1024LL*1024LL

extern U64 g_memlimit_patchfilecache;
extern U64 g_memlimit_checkincache;

void patchfileCacheInit(void)
{
	U64 size_limit;
#ifdef _M_X64
	MEMORYSTATUSEX ms;
	ZeroStruct(&ms);
	ms.dwLength = sizeof(ms);

	GlobalMemoryStatusEx(&ms);
	assert(ms.ullTotalPhys >= (U64)2*GIGABYTE);
	if (ms.ullTotalPhys > 9LL*GIGABYTELL)
		size_limit = ms.ullTotalPhys - 7LL*GIGABYTELL;
	else
		size_limit = ms.ullTotalPhys;
	if(g_memlimit_patchfilecache)
		size_limit = MIN(size_limit, g_memlimit_patchfilecache * GIGABYTELL);
	size_limit -= g_memlimit_checkincache * GIGABYTELL;
	loadstart_printf("Setting file cache to %fGB (of %f)...",
			(float)(size_limit / (1024*1024)) / 1024,
			(float)(ms.ullTotalPhys / (1024*1024)) / 1024 );
			
	if(!SetProcessWorkingSetSizeEx(	GetCurrentProcess(),
									size_limit,
									ms.ullTotalPhys,
									QUOTA_LIMITS_HARDWS_MIN_ENABLE |
										QUOTA_LIMITS_HARDWS_MAX_DISABLE))
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "Failed to set minimum working set.\n");
	}
	
#else
	size_limit = ((U64)800 * 1024 * 1024);
	loadstart_printf("Not running in x64. Setting file cache to 800MB...\n");
#endif

	g_patch_table = thrashCreate(	INITIAL_PATCH_FILE_COUNT,
									StashDefault,
									StashKeyTypeFixedSize,
									sizeof(PatchFile*),
									size_limit,
									s_freePatchFileData);
	loadend_printf("");

	printProcessWorkingSetSize();
}

U64 patchfileCacheSize(void)
{
	return thrashSize(g_patch_table);
}

U64 patchfileLoadedBytes(void)
{
	return s_loaded_bytes;
}

U64 patchfileDecompressedBytes(void)
{
	return s_decompressed_bytes;
}

void printOneCacheItem(void * key, void * value, U32 rank, S64 last_used, S64 score, void * userData)
{
	PatchFile * patch = value;
	char buffer[MAX_PATH], *filename;

	if(rank < 50)
	{
		strcpy(buffer, patchFileGetUsedName(patch));
		filename = strrchr(buffer, '/');
		if(filename)
		{
			*filename = 0;
			filename = strrchr(buffer, '/');
			if(filename)
				++filename;
			else
				filename = buffer;
		}
		else
		{
			filename = buffer;
		}

		printf("%4i %16"FORM_LL"i | %16"FORM_LL"i | %4.0fs | %7.3fMB | %s\n", rank + 1, score, last_used, (F32)(score - last_used) / (F32)timerCpuSpeed64(),
			(F32)(patch->uncompressed.block_len + patch->compressed.block_len) / (F32)(1024*1024), filename);
	}
}

void patchfileCachePrint(void)
{
	thrashSortedScan(g_patch_table, printOneCacheItem, NULL);
}

// may run in a background thread
static void s_patchfiledataInitSizes(PatchFileData *filedata, U32 len)
{
	if(len)
	{
		int i;
		filedata->len = len;
		for(i = 0; i < MAX_PRINT_SIZES-1; i++)
			if(len > print_size[i])
				break;
		filedata->num_print_sizes = MAX_PRINT_SIZES - i;
		filedata->print_sizes = print_size + i;
		filedata->block_len = (len + filedata->print_sizes[0]-1) & ~(filedata->print_sizes[0]-1);
	}
	else
	{
		filedata->len = 0;
		filedata->num_print_sizes = 0;
		filedata->print_sizes = NULL;
		filedata->block_len = 0;
	}
}

// may run in a background thread
void patchfiledataInitForXfer(PatchFileData *filedata, U8 *data, U32 len, bool estring)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	assert(!filedata->data);
	filedata->data = data;
	s_patchfiledataInitSizes(filedata, len);

	if(filedata->data && filedata->block_len != filedata->len)
	{
		assert(filedata->block_len);

		if(estring)
			estrForceSize(&filedata->data, filedata->block_len);
		else
			filedata->data = realloc(filedata->data, filedata->block_len);
		memset(filedata->data + filedata->len, 0, filedata->block_len - filedata->len);
	}

	for(i = 0; i < filedata->num_print_sizes; i++)
		filedata->prints[i] = bindiffMakeFingerprints(filedata->data, filedata->block_len, filedata->print_sizes[i], &filedata->num_prints[i]);

	if(filedata->data)
		filedata->crc = patchChecksum(filedata->data, filedata->len);

	PERFINFO_AUTO_STOP_FUNC();
}

// Duplicate PatchFileData.
static void s_patchfiledataDup(PatchFileData *filedata, const PatchFileData *original)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (!original->data)
		return;

	// Copy general filedata fields.
	filedata->crc = original->crc;
	filedata->len = original->len;
	filedata->block_len = original->block_len;

	// Copy fingerprints.
	filedata->num_print_sizes = original->num_print_sizes;
	filedata->print_sizes = original->print_sizes;
	memcpy(filedata->num_prints, original->num_prints, filedata->num_print_sizes * sizeof(filedata->num_prints[0]));
	for(i = 0; i < filedata->num_print_sizes; i++)
	{
		size_t print_size_bytes = filedata->num_prints[i] * sizeof(filedata->prints[i][0]);
		filedata->prints[i] = malloc(print_size_bytes);
		memcpy(filedata->prints[i], original->prints[i], print_size_bytes);
	}

	// Copy file data.
	filedata->data = malloc(original->block_len);
	memcpy(filedata->data, original->data, original->block_len);

	PERFINFO_AUTO_STOP();
}

// runs in a background thread
static void s_patchfileInitForXfer( const char *filename,
									void *uncompressed,
									int uncompressed_len,
									void *compressed,
									int compressed_len,
									U32 crc,
									PatchFile *patch)
{
	// The background work that used to be here was been moved to patchserverProcessPatchFile() so that it can be spread
	// over more than one thread, and generalized for PatchFiles that aren't loaded from hogs.
}

// runs in the foreground thread in a tick function once a file has been loaded, after s_patchfileInitForXfer() has been called
static void s_patchfileFinishLoad(	const char *filename,
									void *uncompressed,
									int uncompressedSize,
									void *compressed,
									int compressedSize,
									U32 crc,
									PatchFile *patch)
{
	U32 old_len = patch->uncompressed.len, old_crc = patch->uncompressed.crc;
	bool fileNotFoundInHogg = !compressed && !uncompressed;

	PERFINFO_AUTO_START_FUNC();
	
	// Make sure our state is consistent.
	assert(patch->load_state == LOADSTATE_LOADING);
	assert(!patch->estring);
	assertmsgf(eaSize(&patch->requests) || eaSize(&patch->special_files), "No requests queued for load of %s", patch->fileName.name);

	// Make sure the file was found.
	if(fileNotFoundInHogg)
	{
		if(patch->usingOldName){
			printfColor(COLOR_BRIGHT|COLOR_RED,
				"File not found in hogg: %s/%s\n",
				patch->serverdb->name,
				filename);
			ErrorOrAlertDeferred(true, "PATCHDB_FILE_MISSING", "PatchDB corruption: File not found in hogg: %s/%s", patch->serverdb->name, filename);
		}
		patch->load_error = true;
	}

	// Make sure basic file data matches.
	if (!patch->load_error &&
		(old_crc != patch->uncompressed.crc ||
		old_len != patch->uncompressed.len))
	{
		ErrorOrAlertDeferred(true, "PATCHDB_FILE_DATA_MISMATCH", "PatchFile data mismatch while loading  %s %s/%s, ignoring: crc %u len %u old_crc %u old_len %u\n",
			patch->serverdb->name,
			patch->serverdb->name,
			patchFileGetUsedName(patch),
			crc,
			uncompressedSize,
			old_crc,
			old_len);
		patch->load_error = true;
	}

	// Handle load errors.
	if(patch->load_error)
	{
		patch->load_state = LOADSTATE_ERROR;
		
		if(	!patch->ignoreOldName &&
			FALSE_THEN_SET(patch->usingOldName))
		{
			patch->load_state = LOADSTATE_LOADING;
			patch->load_error = false;

			assert(patchserverdbRequestAsyncLoadFromHogg(patch, patch->serverdb, patch->checkin_time,
													patch->fileName.oldName, 
													FILE_LOW_PRIORITY,
													patch->loadingUncompressed,
													s_patchfileInitForXfer,
													s_patchfileFinishLoad));
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		AssertOrAlert("PATCHDB_CORRUPTION", "PatchDB corruption: Unable to load file \"%s\"", filename);

		if(patch->halhog)
		{
			patchHALHogFileDestroy(patch->halhog, true);
			patch->halhog = NULL;
		}

		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if(patch->halhog)
	{
		patchHALHogFileDestroy(patch->halhog, true);
		patch->halhog = NULL;
	}

	// Save load data for background thread.
	devassert(!patch->loadinfo);
	patch->loadinfo = malloc(sizeof(*patch->loadinfo));
	patch->loadinfo->uncompressed = uncompressed;
	patch->loadinfo->uncompressed_len = uncompressedSize;
	patch->loadinfo->compressed = compressed;
	patch->loadinfo->compressed_len = compressedSize;
	patch->loadinfo->crc = crc;

	// Begin background initialization of the PatchFile.
	patchserverRequestPatchFileInit(patch);

	PERFINFO_AUTO_STOP_FUNC();
}

void patchfileRequestLoad(PatchFile *patch, bool uncompressed, WaitingRequest *request)
{
	// Note: Files that are only loaded to generate dependent special files don't need a request.
	assert(patch && (patch->serverdb || patch->nonhogfile) && (request || eaSize(&patch->special_files)));
	assert(patch->load_state < (uncompressed ? LOADSTATE_ALL : LOADSTATE_COMPRESSED_ONLY));

	if (request)
		eaPush(&patch->requests, request);

	if(patch->load_state != LOADSTATE_LOADING && !patch->special)
	{
		// TODO: handle loading compressed and uncompressed individually, this'll blow away the history for the file
		thrashRemove(g_patch_table, &patch);

		patch->loadingUncompressed = !!uncompressed;
		patch->load_state = LOADSTATE_LOADING;
		patch->ignoreOldName = false;
		patch->usingOldName = false;
		assertmsgf(eaSize(&patch->requests) || eaSize(&patch->special_files), "No requests queued for load of %s", patch->fileName.name);

		if(!patchserverdbRequestAsyncLoadFromHogg(patch, patch->serverdb, patch->checkin_time, patch->fileName.name, FILE_LOW_PRIORITY, uncompressed, s_patchfileInitForXfer, s_patchfileFinishLoad))
		{
			printf(	"Can't load hogg for \"%s\", time %d\n",
					patchFileGetUsedName(patch),
					patch->filetime);
			AssertOrAlert("PATCHDB_HOGG_MISSING", "PatchDB corruption: Can't load hogg for \"%s\", time %d",
				patchFileGetUsedName(patch),
				patch->filetime);

			patch->load_state = LOADSTATE_LOADING;
			patch->load_error = true;
			patch->ignoreOldName = true;

			s_patchfileFinishLoad(	patchFileGetUsedName(patch),
									NULL,
									0,
									NULL,
									0,
									0,
									patch);
		}
	}
}

PatchFile* patchfileFromDb(FileVersion *ver, PatchServerDb *serverdb)
{
	PatchFile* patch = SAFE_MEMBER(ver, patch);
	
	assert(ver && serverdb);

	if(!patch)
	{
		patch = ver->patch = callocStruct(PatchFile);
		
		patchserverdbNameInHogg(serverdb, ver, &patch->fileName);
		strcpy(patch->fileName.realName, ver->parent->name);
		strcpy(patch->fileName.realPath, ver->parent->path);
		patch->checkin_time = ver->checkin->time;
		patch->filetime = ver->modified;
		patch->serverdb = serverdb;
		patch->uncompressed.data = NULL;
		patch->compressed.data = NULL;
		patch->halhog = NULL;
		s_patchfiledataInitSizes(&patch->uncompressed, ver->size); // we need these for file info if we don't load uncompressed data
		// compressed sizes will be inited with the first load
		patch->uncompressed.crc = ver->checksum;
		patch->ver = ver;
		patch->load_state = LOADSTATE_NONE;
	}

	if(patch->load_state >= LOADSTATE_COMPRESSED_ONLY)
	{
		// Hit the cache, though we don't use the lookup.
		
		assertmsg(	thrashFind(g_patch_table, &patch, NULL),
					"Patchfile had loaded data, but wasn't in the cache!");
	}

	return ver->patch;
}

// Special files are patch files that do not actually exist on disk.  Normally, if the special file can be created
// entirely from data that is already loaded, such as configuration files or metadata, patchfileFromEString() can be used.
// However, if the special file needs data from a file in the PatchDB itself, the PatchDB file needs to be loaded first.
// After the PatchDB file is loaded, the special file will be built, using the data stored about it in the "Special file"
// section of its PatchFile.
PatchFile* patchfileSpecialFromDb(PatchFile *base, PatchServerDb *serverdb, const char *prepend, U32 filetime, const char *filename)
{
	PatchFile *patch;

	// If the base patch file is already loaded, create the special patch file.
	if (base->load_state >= LOADSTATE_ALL)
	{
		char *estr = NULL;
		if (prepend)
			estrCopy2(&estr, prepend);
		estrConcat(&estr, base->uncompressed.data, base->uncompressed.len);
		patch = patchfileFromEStringEx(&estr, filetime, filename);
		patch->special = true;
		return patch;
	}

	// Otherwise, create a dependent load file, to be filled-in later.
	patch = calloc(1, sizeof(PatchFile));
	strcpy(patch->fileName.name, filename);
	strcpy(patch->fileName.realName, filename);
	patch->ignoreOldName = true;
	patch->estring = false;
	patch->filetime = filetime;
	patch->serverdb = serverdb;
	patch->load_state = LOADSTATE_LOADING;
	patch->special = true;
	patch->special_parent = base;
	if (prepend)
		patch->prepend = strdup(prepend);
	eaPush(&base->special_files, patch);
	patchfileRequestLoad(base, true, NULL);

	return patch;
}

PatchFile* patchfileFromFile(const char *fname)
{
	U32 len = 0;
	U8 *data = NULL;

	PatchFile *patch = calloc(1, sizeof(PatchFile));
	strcpy(patch->fileName.name,fname);
	strcpy(patch->fileName.realName, fname);
	patch->ignoreOldName = true;
	data = fileAlloc(patch->fileName.name, &len);
	if(data)
		patch->filetime = fileLastChanged(patch->fileName.name);

	patch->loadinfo = malloc(sizeof(*patch->loadinfo));
	patch->loadinfo->uncompressed = data;
	patch->loadinfo->uncompressed_len = len;
	patch->loadinfo->compressed = NULL;
	patch->loadinfo->compressed_len = 0;
	patch->loadinfo->crc = 0;
	patch->nonhogfile = true;
	
	patch->load_state = LOADSTATE_LOADING;

	if(data)
		patchserverRequestPatchFileInit(patch);

	return patch;
}

PatchFile* patchfileFromEStringEx(char **estr, U32 filetime, const char *fname)
{
	PatchFile *patch;
	PERFINFO_AUTO_START_FUNC();
	patch = calloc(1, sizeof(PatchFile));
	strcpy(patch->fileName.name, fname);
	strcpy(patch->fileName.realName, fname);
	patch->ignoreOldName = true;
	patch->estring = true;

	patch->loadinfo = malloc(sizeof(*patch->loadinfo));
	patch->loadinfo->uncompressed = *estr;
	patch->loadinfo->uncompressed_len = estrLength(estr);
	patch->loadinfo->compressed = NULL;
	patch->loadinfo->compressed_len = 0;
	patch->loadinfo->crc = 0;
	patch->nonhogfile = true;

	*estr = NULL; // can't trust this pointer anymore

	patch->filetime = filetime;
	patch->load_state = LOADSTATE_LOADING;

	patchserverRequestPatchFileInit(patch);

	PERFINFO_AUTO_STOP_FUNC();
	return patch;
}

// Like patchfileDup(), but write to an existing PatchFile.
// This is used in special circumstances with special files to "fill in" a file from another.
// Note that it requires the source to be completely loaded.
void patchfileDupOverwrite_dbg(PatchFile *target, PatchFile *source MEM_DBG_PARMS)
{
	PERFINFO_AUTO_START_FUNC();

	devassert(target->nonhogfile);
	devassert(source->load_state >= LOADSTATE_ALL);
	devassert(!target->compressed.data && !target->uncompressed.data);

	s_patchfiledataDup(&target->uncompressed, &source->uncompressed);
	s_patchfiledataDup(&target->compressed, &source->compressed);

	if(source->halhog)
	{
		target->halhog = patchHALHogFileAddRef(source->halhog);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

PatchFile* patchfileDup_dbg(PatchFile *original MEM_DBG_PARMS)
{
	PatchFile *patch;
	PERFINFO_AUTO_START_FUNC();

	// Create basic PatchFile information.
	patch = calloc(1, sizeof(PatchFile));
	strcpy(patch->fileName.name, original->fileName.name);
	strcpy(patch->fileName.realName, original->fileName.realName);
	patch->ignoreOldName = true;
	patch->filetime = original->filetime;
	patch->nonhogfile = true;

	// If the original PatchFile is not actually loaded, set this up as a special file instead, so it will be duplicated once the original is loaded.
	if (original->load_state < LOADSTATE_ALL)
	{
		patch->serverdb = original->serverdb;
		patch->load_state = LOADSTATE_LOADING;
		patch->special = true;
		patch->special_parent = original;
		eaPush(&original->special_files, patch);
		patchfileRequestLoad(original, true, NULL);;
		PERFINFO_AUTO_STOP_FUNC();
		return patch;
	}

	// Copy PatchFile data.
	patchfileDupOverwrite(patch, original);

	patch->load_state = LOADSTATE_ALL;

	PERFINFO_AUTO_STOP_FUNC();
	return patch;
}

void patchfileClearOldCachedFiles(void)
{
	PERFINFO_AUTO_START_FUNC();
	thrashRemoveOld(g_patch_table, 60.f * 10.f, 1.f);
	PERFINFO_AUTO_STOP_FUNC();
}

// Add a prepared PatchFile to the cache.
void patchfileAddToCache(PatchFile *patch)
{
	U32 size;

	// Calculate PatchFile size.
	size = 0;
	if(patch->uncompressed.data)
	{
		size += patch->uncompressed.block_len;
		s_decompressed_bytes += patch->uncompressed.len;
	}
	if(patch->compressed.data)
	{
		size += patch->compressed.block_len;
		s_loaded_bytes += patch->compressed.len;
	}

	// Print debugging information, if requested.
	if(g_patchserver_config.printFileCacheUpdates){
		printfColor(COLOR_GREEN,
			"FileCache: Adding \"%s\" (%db comp, %db uncomp, currently %"FORM_LL"db/%"FORM_LL"db)\n",
			patch->fileName.name,
			patch->compressed.block_len,
			patch->uncompressed.block_len,
			thrashSize(g_patch_table),
			thrashSizeLimit(g_patch_table));
	}

	// Add PatchFile to cache.
	assert(thrashAdd(g_patch_table, &patch, patch, size, false));
}
