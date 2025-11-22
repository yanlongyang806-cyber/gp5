#include "FilespecMap.h"
#include "patchdb.h"
#include "patchproject.h"
#include "patchproject_h_ast.h"
#include "patchproject_opt.h"
#include "timing_profiler.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "timing.h"

bool patchprojectStripPrefix(char *buf, size_t buf_len, const char *path, const char *prefix)
{
	size_t len;
	if(prefix && prefix[0] == '/')
		prefix += 1; // path (coming from a DirEntry) will never start with /, but prefixes usually do.
	if(prefix && strStartsWith(path, prefix))
	{
		const char *c = path + strlen(prefix);
		if(*c == '/')
			c++;
		len = MIN(buf_len, strlen(c));
		memcpy(buf, c, len);
		buf[len] = 0;
		return true;
	}
	else
	{
		len = MIN(buf_len, strlen(path));
		memcpy(buf, path, len);
		buf[len] = 0;
		return prefix==NULL;
	}
}

bool patchprojectIsPathIncluded(const PatchProject *project, const char *path, const char *prefix)
{
	char buf[MAX_PATH];
	PERFINFO_AUTO_START_FUNC();

	if(!patchprojectStripPrefix(SAFESTR(buf), path, prefix))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(project->include_filemap)
	{
		// patchFindPath may do some name fixup
		//int dummy;
		//DirEntry *dir = SAFE_MEMBER2(project, serverdb, db) ? patchFindPath(project->serverdb->db, path, 0) : NULL;
		if(filespecMapGetInt(project->include_filemap, buf, NULL))
		{
			bool ret = !filespecMapGetInt(project->exclude_filemap, buf, NULL);
			PERFINFO_AUTO_STOP();
			return ret;
		}
		PERFINFO_AUTO_STOP();
		return false;
	}
	PERFINFO_AUTO_STOP();
	return true;
}

// Return true if this file should be included in the manifest.
static __inline bool fileMatchesFilespecForManifest(const DirEntry *dir, const GetManifestData *gmd)
{
	char path[MAX_PATH];

	// If no versions, do nothing.
	if (!eaSize(&dir->versions))
		return false;

	// Check if we're within the strip prefix.
	if (gmd->project->strip_prefix &&
		!patchprojectStripPrefix(SAFESTR(path), dir->path, gmd->prefix))
		return false;

	// If we don't match the include filemap, we're not included.
	if (gmd->project->include_filemap_flat &&
		!filespecMapCheckFlat(gmd->project->include_filemap_flat, gmd->project->strip_prefix?path:dir->path))
		return false;

	// If we match the exclude filemap, we're not included.
	if (gmd->project->exclude_filemap_flat &&
		filespecMapCheckFlat(gmd->project->exclude_filemap_flat, gmd->project->strip_prefix?path:dir->path))
		return false;

	return true;
}

void getManifestCBRevs(DirEntry *dir, GetManifestData *gmd)
{
	FileVersion *ver;

	PERFINFO_AUTO_START_FUNC_L2();

	if(!fileMatchesFilespecForManifest(dir, gmd))
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	ver = patchFindVersionInDir(dir, gmd->branch, gmd->sandbox, gmd->rev, gmd->incr_from);
	if( gmd->project->allow_checkins ||
		(ver && !(ver->flags & FILEVERSION_DELETED)) )
	{
		if(!gmd->walk_heirarchy || stashAddPointer(gmd->result_stash, dir->path, match, 0)) // multiple dbs are applied from most specific to least
		{
			if(ver)
			{
				estrConcatf(&gmd->result_estr, "%s\t%d\t%d\t%d\t%d\t%d",
													dir->path, ver->modified,
													ver->size, ver->checksum,
													ver->header_size, ver->header_checksum);

				if(gmd->project->allow_checkins)
				{
					Checkout *checkout = NULL;
					FileVerFlags flags = ver->flags;

					if(!gmd->incremental)
					{
						if(!(dir->flags & DIRENTRY_FROZEN))
						{
							FileVersion *newest = patchFindVersionInDir(dir, INT_MAX, gmd->sandbox, INT_MAX, PATCHREVISION_NONE);
							if(gmd->branch < newest->checkin->branch)
								flags |= FILEVERSION_LINK_FORWARD_BROKEN;
							if(ver->checkin->branch < gmd->branch)
								flags |= FILEVERSION_LINK_BACKWARD_SOLID;
						}
						checkout = patchFindCheckoutInDir(dir, gmd->branch, gmd->sandbox);
					}

					estrConcatf(&gmd->result_estr, "\t%d\t%s\t%s", flags, 
														ver->checkin->author ? ver->checkin->author : "",
														checkout ? checkout->author : "");
				}

				estrConcatf(&gmd->result_estr, "\n");
			}
			else // fake a deletion
			{
				FileVerFlags flags = FILEVERSION_DELETED;

				if(!gmd->incremental)
				{
					FileVersion *newest = patchFindVersionInDir(dir, INT_MAX, gmd->sandbox, INT_MAX, PATCHREVISION_NONE);
					if(newest && gmd->branch < newest->checkin->branch)
						flags |= FILEVERSION_LINK_FORWARD_BROKEN;
				}
				estrConcatf(&gmd->result_estr, "%s\t0\t0\t0\t0\t0\t%d\t\t\n", dir->path, flags);
			}
		}
		else
		{
			assertmsg(!gmd->project->allow_checkins || gmd->incremental, "multiple matching files in a db project?!");
		}
	}

	PERFINFO_AUTO_STOP_L2();
}
