#ifndef _PATCHFILE_H
#define _PATCHFILE_H

#include "patchserverdb.h"

#define SMALLEST_DATA_BLOCK 256
#define MAX_PRINT_SIZES 4

typedef struct PatchServerDb PatchServerDb;
typedef struct FileVersion FileVersion;
typedef struct WaitingRequest WaitingRequest;
typedef struct HogFile HogFile;
typedef struct HALHogFile HALHogFile;

typedef enum LoadState
{
	LOADSTATE_LOADING = -2,
	LOADSTATE_ERROR = -1,
	LOADSTATE_NONE,
	LOADSTATE_INFO_ONLY,
	LOADSTATE_COMPRESSED_ONLY,
	LOADSTATE_ALL,
} LoadState;

typedef struct PatchFileData
{
	U8		*data;
	U32		len;		// length of data
	U32		block_len;	// len rounded up to a multiple of of the biggest fingerprint size
	U32		crc;

	int		num_print_sizes;
	U32		*print_sizes;
	U32		num_prints[MAX_PRINT_SIZES];
	U32		*prints[MAX_PRINT_SIZES];
} PatchFileData;

// Information from fileLoader
typedef struct PatchFileLoadInfo {
	void *uncompressed;
	int uncompressed_len;
	void *compressed;
	int compressed_len;
	U32 crc;
} PatchFileLoadInfo;

typedef struct PatchFile
{
	FileNameAndOldName			fileName;
	U32							checkin_time;
	U32							filetime;			// TODO: keep a pointer to the associated version instead
	PatchFileData				uncompressed;
	PatchFileData				compressed;
	HALHogFile*					halhog;				// Used to hold the handle open when initiating fileLoader calls
	PatchFileLoadInfo			*loadinfo;			// Temporary information from fileLoader used by patchserverInitPatchFile()

	PatchFile**					special_files;		// if non-empty, this a regular db file, but it has several dependent special files
	PatchFile*					special_parent;		// if non-empty, this is a special file, and this is a pointer to the base patch

	// Special files
	
	char*						prepend;			// prepend this string immediately after loading

	PatchServerDb*				serverdb;
	LoadState					load_state;
	WaitingRequest**			requests; // this could be userdata/callbacks, which would get rid of the hack for http requests. if we add a third system, make the switch.
	FileVersion*				ver; // optional - pointer to the fileversion used to generate this

	// Boolean flags
	U32							loadingUncompressed	:1;
	U32							usingOldName		:1;
	U32							ignoreOldName		:1;
	U32							delete_me			:1;
	U32							estring				:1;
	U32							load_error			:1;
	U32							special				:1;	// if true, this file is a special file created by patchfileSpecialFromDb()
	U32							nonhogfile			:1;	// if true, this file is not loaded from hogs, and so will bypass fileloader, but may still need the init step
} PatchFile;

const char* patchFileGetUsedName(PatchFile* patch);

void patchfileCacheInit(void);
U64 patchfileCacheSize(void);
U64 patchfileLoadedBytes(void);
U64 patchfileDecompressedBytes(void);
void patchfileCachePrint(void);

void patchfiledataInitForXfer(PatchFileData *filedata, U8 *data, U32 len, bool estring);

PatchFile* patchfileFromDb(FileVersion *ver, PatchServerDb *serverdb);
PatchFile* patchfileSpecialFromDb(PatchFile *base, PatchServerDb *serverdb, const char *prepend, U32 filetime, const char *filename);
PatchFile* patchfileFromFile(const char *fname);
PatchFile* patchfileFromEStringEx(char **estr, U32 filetime, const char *fname); // takes control of the estring
#define patchfileFromEString(estr, filetime) patchfileFromEStringEx(estr,filetime,"") // takes control of the estring
#define patchfileDup(patch) patchfileDup_dbg(patch MEM_DBG_PARMS_INIT)
PatchFile* patchfileDup_dbg(PatchFile *patch MEM_DBG_PARMS);
#define patchfileDupOverwrite(target, source) patchfileDupOverwrite_dbg(target, source MEM_DBG_PARMS_INIT)
void patchfileDupOverwrite_dbg(PatchFile *target, PatchFile *source MEM_DBG_PARMS);
void patchfileDestroy(PatchFile **patch);

void patchfileRequestLoad(PatchFile *patch, bool uncompressed, WaitingRequest *request);

void patchfileClearOldCachedFiles(void);

// Add a prepared PatchFile to the cache.
void patchfileAddToCache(PatchFile *patch);

#endif
