// Performance-sensitive internal routines of PatchProject that require optimization.

#ifndef CRYPTIC_PATCHPROJECT_OPT_H
#define CRYPTIC_PATCHPROJECT_OPT_H

typedef struct DirEntry DirEntry;
typedef struct DirEntry DirEntry;

typedef struct GetManifestData
{
	PatchProject	*project;
	int				branch;
	char			*sandbox;
	int				rev;
	int				incr_from;
	bool			incremental;

	// old system
	PatchDB			*result;

	// new system
	char *result_estr;
	StashTable result_stash;
	const char *prefix;
	bool walk_heirarchy;
} GetManifestData;

void getManifestCBRevs(DirEntry *dir, GetManifestData *gmd);

#endif  // CRYPTIC_PATCHPROJECT_OPT_H
