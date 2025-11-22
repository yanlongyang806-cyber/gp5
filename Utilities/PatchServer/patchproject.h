#ifndef _PROJECTFILE_H
#define _PROJECTFILE_H

#define MAX_VIEWS_PER_PROJECT 50

typedef struct PatchFile PatchFile;
typedef struct ThrashTableImp* ThrashTable;
typedef struct PatchDB PatchDB;
typedef struct PatchServerDb PatchServerDb;
typedef struct FilespecMap FilespecMap;
typedef struct AllowIp AllowIp;
typedef struct FileVersion FileVersion;
typedef struct StashTableImp* StashTable;

typedef struct ProjectView
{
	PatchFile	*manifest_patch;
	PatchFile	*filespec_patch;	// per-view filespec
	U32			checkout_time;		// for db projects, determines if the view must be regenerated for new checkouts
	time_t		access_time;
} ProjectView;

AUTO_STRUCT;
typedef struct FlagmapConfig
{
//	int		value;		AST(STRUCTPARAM SUBTABLE(parse_FilemapConfig_value))
	char	**files;
} FlagmapConfig;

AUTO_STRUCT;
typedef struct HoggmapConfig
{
	char	*hoggname;	AST(STRUCTPARAM)
	char	*strip;
	char	**files;
	bool	mirror_stripped; AST(NAME("MirrorStripped"))
} HoggmapConfig;

AUTO_STRUCT;
typedef struct PatchProject
{
	// these are loaded from a server's config, then updated with the project's config, the reverse may be more useful
	const char	*name;			AST(UNOWNED) // this is loaded by the server config (parse_config_PatchProject)
	AllowIp		**allow_ips;	NO_AST // this is loaded by the server config (parse_config_PatchProject)
	AllowIp		**deny_ips;		NO_AST // this is loaded by the server config (parse_config_PatchProject)
	AllowIp		**allowFullManifest_ips;	NO_AST // this is loaded by the server config (parse_config_PatchProject)
	AllowIp		**denyFullManifest_ips;		NO_AST // this is loaded by the server config (parse_config_PatchProject)
	bool		useForFullManifest;	NO_AST
	
	bool		allow_checkins;		AST(NAME("AllowCheckins") BOOLFLAG)
	char		**include_config;	AST(NAME("IncludeFiles"))
	FilespecMap	*include_filemap;	NO_AST
	FilespecMap *include_filemap_flat; NO_AST	// Flattened version of include_filemap, to optimize manifest generation
	char		**exclude_config;	AST(NAME("ExcludeFiles"))
	FilespecMap	*exclude_filemap;	NO_AST
	FilespecMap *exclude_filemap_flat; NO_AST	// Flattened version of exclude_filemap, to optimize manifest generation
	bool		strip_prefix;		AST(NAME("StripPrefix") BOOLFLAG)

	// used for generating client filespecs
	HoggmapConfig	**client_hoggspecs;		AST(NAME("Client_HoggFile"))
	FlagmapConfig	*client_mirrored;		AST(NAME("Client_Mirrored"))
	FlagmapConfig	*client_notrequired;	AST(NAME("Client_NotRequired"))

// These used to let you specify a default, but that should probably be managed in the database itself
// For now, I'm making the default 'latest' -GG
// 	int			branch;			AST(NAME("Branch"))
// 	int			time;			AST(NAME("Time"))
// 	char		*sandbox;		AST(NAME("Sandbox"))

AST_STOP
	PatchServerDb	*serverdb;			// null here indicates an un-loaded project
	bool			is_db;				// this project is the database, okay for checkins/outs
	bool			update_me;			// this project exists on the parent server, mirror it
	bool			reload_me;			// this project's .patchproject file has changed (during mirroring)
	StashTable		cached_views;
	PatchFile		*config_patch;		// .patchproject or .serverdb file
	PatchFile		*filespec_patch;
} PatchProject;

PatchProject* patchprojectCreateDbProject(PatchServerDb *serverdb, const char *config_fname);
PatchProject* patchprojectLoad(const char *name, PatchServerDb *serverdb);
void patchprojectReload(PatchProject *project);
void patchprojectClear(PatchProject *project);
void patchprojectHierarchyChanged(PatchProject *project);

bool patchprojectStripPrefix(char *buf, size_t buf_len, const char *path, const char *prefix);

ProjectView* patchprojectFindOrAddView(PatchProject *proj, int branch, int rev, char *sandbox, int incr_from, char *prefix, int client_version,
	const char *creator_token, const char *creator_ip, U64 creator_uid);
bool patchprojectIsPathIncluded(const PatchProject *project, const char *path, const char *prefix);

// CAREFUL! the version returned is NOT necessarily in proj->serverdb
FileVersion* patchprojectFindVersion(PatchProject *proj, char *dir_name, int branch, char *sandbox, int rev, int incr_from, char *prefix, PatchServerDb **pserverdb);

// Make a per-view client filespec.
// Note: view must match the view parameters.
PatchFile* patchprojectGenerateViewClientFilespec(const char *filename, PatchProject *proj, int branch,
												  char *sandbox, int rev, int incr_from, ProjectView *view);

#endif
