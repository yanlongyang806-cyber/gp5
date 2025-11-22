#ifndef _PATCHSERVERDB_H
#define _PATCHSERVERDB_H

#include "patchdb.h"
#include "fileLoader.h"
#include "MemoryPool.h"

typedef struct PatchFile PatchFile;
typedef struct FilespecMap FilespecMap;
typedef struct PatchJournal PatchJournal;
typedef struct HogFile HogFile;
typedef struct PatchProject PatchProject;
typedef struct PatchServerDb PatchServerDb;
typedef struct FlagmapConfig FlagmapConfig; // patchproject.h
typedef struct ViewExpires ViewExpires;
typedef struct StringTableImp StringTableImp;
typedef StringTableImp *StringTable;

#define PATCHSERVER_KEEP_EXCLUDE 0
#define PATCHSERVER_KEEP_UNLIMITED -1
#define PATCHSERVER_KEEP_BINS -3

#define HOG_TIMESTAMP_DIVISION 100000

AUTO_STRUCT;
typedef struct FilemapConfigLine
{
	char	*spec;		AST(STRUCTPARAM)
	int		value;		AST(STRUCTPARAM SUBTABLE(parse_FilemapConfig_value))
} FilemapConfigLine;

AUTO_STRUCT;
typedef struct FilemapConfig
{
	FilemapConfigLine	**files;
} FilemapConfig;

AUTO_STRUCT;
typedef struct PatchBranch
{
	int				branch;			AST(STRUCTPARAM)
	char			*name;			AST(NAME("Name"))
	int				parent_branch;	AST(NAME("BaseDatabaseBranch", "CoreBranch") SUBTABLE(parse_PatchBranch_parent_branch))
	char			*warning;		AST(NAME("CheckinWarning"))
} PatchBranch;

typedef struct MirrorConfig MirrorConfig;

AUTO_STRUCT;
typedef struct PatchServerDb
{
	const char*		name;				AST(UNOWNED) // pooled, loaded by the server config (parse_config_PatchServerDb)
AST_STOP
	PatchDB*		db;					// should always be valid
	PatchServerDb*	basedb;				// parent db for hierarchical dbs
	int				latest_branch;
	int				latest_rev;
	PatchFile**		incremental_manifest_patch;
	int*			incremental_manifest_revs;
	bool			destroy_full_manifest_on_notify; // Also clears the incremental manifests
	PatchDB*		db_new;				// temporary storage for updates on child patchservers
	FilespecMap*	keepdays;
	FilespecMap*	keepvers;
	FilespecMap*	frozenmap;
	StashTable		hogg_stash;				// U32 checkin_time -> HALHoggTracker
AST_START
	PatchProject**	projects;               AST(UNOWNED)
AST_STOP
	MirrorConfig*	mirrorConfig;
	bool			update_me;				// this serverdb exists on the parent, mirror it
	bool			load_me;				// this serverdb's .patchserverdb file is new or has changed (during mirroring)
	bool			save_me;				// this serverdb had journals merged into it, write it out if we're a merge process
	unsigned		update_duration;		// total time needed to update this database
	bool			updated;				// true, if this checkin has been updated

AST_START
	char			*basedb_name;			AST(NAME("BaseDatabase") POOL_STRING) // TODO: check for loops
	char			**project_names;		AST(NAME("Project"))
	char			*persist_prefix;		AST(NAME("InternalPrefix"))
	FilemapConfig	keepdays_config;		AST(NAME("DaysToKeep"))
	FilemapConfig	keepvers_config;		AST(NAME("VersionsToKeepPerBranch"))
	FilemapConfig	frozenmap_config;		AST(NAME("FrozenFiles"))
	bool			expire_immediately;		AST(NAME("ExpireImmediately"))
	int				min_branch;				AST(NAME("MinBranchNum"))
	int				max_branch;				AST(NAME("MaxBranchNum"))
	PatchBranch		**branches;				AST(NAME("Branch"))
	FlagmapConfig	*client_nowarn;			AST(NAME("Client_NoWarn")) // for .filespec for gimme projects
	S32				mirrorHoggsLocally;		AST(NAME("MirrorHoggsLocally"))
	S32				mirrorHoggsOnStartup;	AST(NAME("MirrorHoggsOnStartup"))
	ViewExpires		*view_expires_override;	AST(NAME("ViewExpires"))
} PatchServerDb;

typedef struct FileNameAndOldName {
	char			name[MAX_PATH];
	char			oldName[MAX_PATH];
	char			realName[MAX_PATH];
	char			realPath[MAX_PATH];
} FileNameAndOldName;

// These "Fast" versions of PatchDB structs from patchdb.h are designed to generate the same output with ParserWriteText, but have
// a representation that is faster to populate.  When the live PatchDB needs to be serialized to a full manifest, the entire
// PatchDB needs to be walked in a single operation.  To make this faster, the PatchDB is copied to the Fast form, and then sent
// to a background thread to do the actual serialization.

// Must be kept in synch with FileVersion in patchdb.h
AUTO_STRUCT;
typedef struct FastFileVersion
{
	AST_PREFIX(STRUCTPARAM)
	int				version;			AST(NAME(Version))
	int				rev;
	U32				checksum;
	U32				size;
	U32				modified;
	U32				header_size;
	U32				header_checksum;
	FileVerFlags	flags;				AST(INT)
	U32				expires;			AST(NAME(Expires))
	AST_PREFIX()
} FastFileVersion;

// Must be kept in synch with Checkout in patchdb.h
AUTO_STRUCT;
typedef struct FastCheckout
{
	char	*author;	AST(NAME(Author))
	U32		time;		AST(NAME(Time))
	char	*sandbox;	AST(NAME(Sandbox))
	int		branch;		AST(NAME(Branch))
} FastCheckout;

// Must be kept in synch with DirEntry in patchdb.h
typedef struct FastDirEntry FastDirEntry;
AUTO_STRUCT;
typedef struct FastDirEntry
{
	char*				name;			AST(POOL_STRING STRUCTPARAM)
	FastFileVersion*	versions;		AST(NAME(Version) BLOCK_EARRAY)
	FastDirEntry*		children;		AST(NAME(File) BLOCK_EARRAY)
	FastCheckout*		checkouts;		AST(NAME(Checkout) BLOCK_EARRAY)
} FastDirEntry;

// Must be kept in synch with Checkin in patchdb.h
AUTO_STRUCT;
typedef struct FastCheckin
{
	int		rev;		AST(STRUCTPARAM)
	int		branch;		AST(NAME(Branch))
	U32		time;		AST(NAME(Time))
	char	*sandbox;	AST(NAME(Sandbox))
	int		incr_from;	AST(NAME(IncrementalFrom) DEFAULT(PATCHREVISION_NONE))
	char	*author;	AST(NAME(Author))
	char	*comment;	AST(NAME(Comment))
} FastCheckin;

// Must be kept in synch with NamedView in patchdb.h
AUTO_STRUCT;
typedef struct FastNamedView
{
	char*	name;				AST(STRUCTPARAM)
	int		branch;				AST(NAME(Branch))
	int		rev;				AST(NAME(Revision))
	char*	comment;			AST(NAME(Comment))
	char*	sandbox;			AST(NAME(Sandbox))
	U32		expires;			AST(NAME(Expires))
	U32		viewed;				AST(NAME(Viewed))
	U32		viewed_external;	AST(NAME(ViewedExternal))
	bool	dirty;				AST(NAME(Dirty))
} FastNamedView;

// Must be kept in synch with PatchDB in patchdb.h
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct FastPatchDB
{
	int				version;				AST(NAME(DBVersion))
	FastDirEntry	root;					AST(NAME(Root))
	FastCheckin		*checkins;				AST(NAME(Checkin) BLOCK_EARRAY)
	FastNamedView	*namedviews;			AST(NAME(View) BLOCK_EARRAY)
	int				*branch_valid_since;	AST(NAME(BranchValidity))
	int				latest_rev;				AST(NAME(LatestRev) DEFAULT(-1))

	AST_STOP

	// String table, for non-pooled strings
	StringTable		strings;

	// Memory blocks for FastDirEntry
	char			**dir_memory;
	char			**version_memory;
	char			**checkout_memory;
	char			*dir_memory_base;
	char			*dir_memory_index;
	char			*dir_memory_bound;
	char			*version_memory_base;
	char			*version_memory_index;
	char			*version_memory_bound;
	char			*checkout_memory_base;
	char			*checkout_memory_index;
	char			*checkout_memory_bound;
} FastPatchDB;

bool patchserverdbLoad(PatchServerDb *serverdb, bool verify_hoggs, char **verify_projects, bool fix_hoggs, bool verify_hoggs_load_data,
					   bool fatalerror_on_verify_failure, bool merging); // returns true if db hierarchy changes
void patchserverdbUnloadFiles(PatchServerDb *serverdb);

void patchserverdbNameInHogg(	PatchServerDb *serverdb,
								FileVersion *ver,
								FileNameAndOldName* nameOut);

void patchserverdbUpdateTimeline(PatchServerDb *serverdb, U32 t);
U32 patchserverdbFindOnTimeline(PatchServerDb *serverdb, U32 t);

FileVersion* patchserverdbAddFile(	PatchServerDb *serverdb,
									const char *dir_name,
									void *data,
									int size_uncompressed,
									int size_compressed,
									U32 checksum,
									U32 modified_time,
									U32 header_size,
									U32 header_checksum,
									U8 *header_data,
									Checkin *checkin);

void patchserverdbGetHoggMirrorFilePath(char* filePathOut,
										size_t filePathOutSize,
										const char* dbName,
										const char* pathInHogg,
										U32 timeCheckin);
									
S32 patchserverdbGetHoggMirrorQueueSize(U64* bytesToWriteOut);

void patchserverdbQueueWriteToDisk(const char* dbName,
										  const char* pathInHogg,
										  char* data,
										  U32 timeCheckin,
										  U32 timeModified,
										  U32 sizeCompressed,
										  U32 sizeUncompressed,
										  S32 doWaitForHoggToFinish);

void patchserverdbQueueDeleteToDisk(const char* dbName,
									const char* filePath,
									U32 timeCheckin);

void patchserverdbWriteFile(const char* dbName,
							HogFile* hogg,
							const char* pathInHogg,
							void* data,
							int size_uncompressed,
							int size_compressed,
							U32 checksum,
							U32 timeModified,
							const Checkin* checkin,
							S32 mirrorHoggsLocally);
int patchserverdbAddCheckouts(PatchServerDb *serverdb, DirEntry **dirs, const char *author, int branch, const char *sandbox, char* errMsg, S32 errMsgSize);
void patchserverdbRemoveCheckouts(PatchServerDb *serverdb, DirEntry **dirs, int branch, const char *sandbox);

FilespecMap* patchserverdbCreateFilemapFromConfig(FilemapConfig *config);

void patchserverdbAsyncTick(void);

bool patchserverdbIsFileRevisioned(PatchServerDb *serverdb, const char *filename);
bool patchserverdbAddViewName(	PatchServerDb* db,
								const char* view_name,
								int branch,
								const char* sandbox,
								int rev,
								const char* comment,
								U32 expires,
								char* err_msg,
								int err_msg_size);
bool patchserverdbSetExpiration(PatchServerDb *serverdb, const char *view_name, U32 expires, char *msg, int msg_size);
bool patchserverdbSetFileExpiration(PatchServerDb *serverdb, const char *path, U32 expires);
PatchFile* patchserverdbGetFullManifestPatch(PatchServerDb *serverdb, PatchFile **patch);
PatchFile* patchserverdbGetIncrementalManifestPatch(PatchServerDb *serverdb, int from_rev, PatchFile **patch);

// Delete a file from a hog, and log it.
void patchserverdbHogDelete(PatchServerDb *serverdb, HogFile *hogfile, const char *filename, const char *reason);

bool patchserverdbAsyncIsRunning(void);
void patchserverdbAsyncAbort(void);

bool patchserverdbRequestAsyncLoadFromHogg(PatchFile *patch, PatchServerDb *serverdb, U32 checkin_time, const char *filename, U32 priority, bool uncompressed,
										FileLoaderHoggFunc background, FileLoaderHoggFunc foreground);
void patchserverdbSetVersionSize(PatchServerDb *serverdb, FileVersion *v);
bool patchserverdbGetDataForVersion(PatchServerDb *serverdb, FileVersion *version, FileNameAndOldName *name, char **data, U32 *len, U32 *len_compressed);
void patchserverdbRemoveVersion(PatchServerDb *serverdb, const Checkin *checkin, FileVersion *ver);
void patchserverdbLoadHeader(FileVersion *ver, PatchServerDb *serverdb);
void patchserverdbRemoveNewVersion(PatchServerDb *serverdb, FileVersion *new_version);

// Make a fast copy of the server PatchDB.
FastPatchDB *patchserverdbFastCopy(PatchDB *src, size_t string_size_hint);

#endif
