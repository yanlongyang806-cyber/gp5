#include "EString.h"
#include "FilespecMap.h"
#include "earray.h"
#include "error.h"
#include "fileutil.h"
#include "logging.h"
#include "memlog.h"
#include "patchcommonutils.h"
#include "patchdb.h"
#include "patcher_comm.h"
#include "patchfile.h"
#include "patchjournal.h"
#include "patchproject.h"
#include "patchproject_h_ast.h"
#include "patchproject_opt.h"
#include "patchserver.h"
#include "patchserverdb.h"
#include "pcl_typedefs.h"
#include "referencesystem.h"
#include "textparser.h"
#include "thrashtable.h"
#include "timing.h"

extern ParseTable parse_FileVersion[];
#define TYPE_parse_FileVersion FileVersion
extern ParseTable parse_Checkout[];
#define TYPE_parse_Checkout Checkout

// The last time a project was loaded from disk.
static time_t g_project_load_time = 0;

// Initial size of manifest building stash table.
static U32 s_manifest_stash_init_size = 2*1024*1024;
AUTO_CMD_INT(s_manifest_stash_init_size, ManifestStashInitSize) ACMD_CMDLINE;

// OLD PATCHDB PARSETABLES ///////////////////////////////////////////////////

// These versions are no longer supported (as of adding revision numbers, they won't work at all),
// but I'm leaving these tables as an example. Feel free to delete them -GG

// Older than PATCHCLIENT_VERSION 3

StaticDefineInt parse_pre3_FileVerFlags[3] =
{
	DEFINE_INT
	{ "Deleted",		FILEVERSION_DELETED },
	DEFINE_END
};

ParseTable parse_pre3_FileVersion[] =
{
	{ "FileVersion", 		TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(FileVersion), 0, NULL, 0 },
	{ "{",					TOK_START, 0 },
	{ "version",			TOK_AUTOINT(FileVersion, version, 0), NULL },
	{ "checksum",			TOK_AUTOINT(FileVersion, checksum, 0), NULL },
	{ "size",				TOK_AUTOINT(FileVersion, size, 0), NULL },
	{ "modified",			TOK_AUTOINT(FileVersion, modified, 0), NULL },
	{ "checkin_idx",		TOK_AUTOINT(FileVersion, rev, 0), NULL },
	{ "flags",				TOK_AUTOINT(FileVersion, flags, 0), parse_pre3_FileVerFlags },
	{ "header_size",		TOK_AUTOINT(FileVersion, header_size, 0), NULL },
	{ "header_checksum",	TOK_AUTOINT(FileVersion, header_checksum, 0), NULL },
	{ "}",					TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre3_Checkout[] =
{
	{ "Checkout", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(Checkout), 0, NULL, 0 },
	{ "{",			TOK_START, 0 },
	{ "author",		TOK_STRING(Checkout, author, 0), NULL },
	{ "time",		TOK_AUTOINT(Checkout, time, 0), NULL },
	{ "sandbox",	TOK_STRING(Checkout, sandbox, 0), NULL },
	{ "branch",		TOK_AUTOINT(Checkout, branch, 0), NULL },
	{ "}",			TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre3_DirEntry[] =
{
	{ "DirEntry", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(DirEntry), 0, NULL, 0 },
	{ "{",			TOK_START, 0 },
	{ "name",		TOK_STRING(DirEntry, name, 0), NULL },
	{ "versions",	TOK_STRUCT(DirEntry, versions, parse_pre3_FileVersion) },
	{ "children",	TOK_STRUCT(DirEntry, children, parse_pre3_DirEntry) },
	{ "checkouts",	TOK_STRUCT(DirEntry, checkouts, parse_pre3_Checkout) },
	{ "}",			TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre3_Checkin[] =
{
	{ "Checkin", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(Checkin), 0, NULL, 0 },
	{ "{",			TOK_START, 0 },
	{ "author",		TOK_STRING(Checkin, author, 0), NULL },
	{ "comment",	TOK_STRING(Checkin, comment, 0), NULL },
	{ "sandbox",	TOK_STRING(Checkin, sandbox, 0), NULL },
	{ "idx",		TOK_AUTOINT(Checkin, rev, 0), NULL },
	{ "branch",		TOK_AUTOINT(Checkin, branch, 0), NULL },
	{ "time",		TOK_AUTOINT(Checkin, time, 0), NULL },
	{ "}",			TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre3_NamedView[] =
{
	{ "NamedView", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(NamedView), 0, NULL, 0 },
	{ "{",			TOK_START, 0 },
	{ "name",		TOK_STRING(NamedView, name, 0), NULL },
	{ "branch",		TOK_AUTOINT(NamedView, branch, 0), NULL },
	{ "sandbox",	TOK_STRING(NamedView, sandbox, 0), NULL },
//	{ "time",		TOK_AUTOINT(NamedView, time, 0), NULL },
	{ "}",			TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre3_PatchDB[] =
{
	{ "PatchDB", 			TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(PatchDB), 0, NULL, 0 },
	{ "root",				TOK_EMBEDDEDSTRUCT(PatchDB, root, parse_pre3_DirEntry) },
	{ "checkins",			TOK_STRUCT(PatchDB, checkins, parse_pre3_Checkin) },
	{ "namedviews",			TOK_STRUCT(PatchDB, namedviews, parse_pre3_NamedView) },
//	{ "patcher_version",	TOK_AUTOINT(PatchDB, patcher_version, 0), NULL },
	{ "", 0, 0 }
};

// Older than PATCHCLIENT_VERSION 1

ParseTable parse_pre1_FileVersion[] =
{
	{ "FileVersion", 		TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(FileVersion), 0, NULL, 0 },
	{ "{",					TOK_START, 0 },
	{ "version",			TOK_AUTOINT(FileVersion, version, 0), NULL },
	{ "checksum",			TOK_AUTOINT(FileVersion, checksum, 0), NULL },
	{ "size",				TOK_AUTOINT(FileVersion, size, 0), NULL },
	{ "modified",			TOK_AUTOINT(FileVersion, modified, 0), NULL },
	{ "checkin_idx",		TOK_AUTOINT(FileVersion, rev, 0), NULL },
	{ "flags",				TOK_AUTOINT(FileVersion, flags, 0), parse_pre3_FileVerFlags },
	{ "}",					TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre1_DirEntry[] =
{
	{ "DirEntry", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(DirEntry), 0, NULL, 0 },
	{ "{",			TOK_START, 0 },
	{ "name",		TOK_STRING(DirEntry, name, 0), NULL },
	{ "versions",	TOK_STRUCT(DirEntry, versions, parse_pre1_FileVersion) },
	{ "children",	TOK_STRUCT(DirEntry, children, parse_pre3_DirEntry) },
	{ "checkouts",	TOK_STRUCT(DirEntry, checkouts, parse_pre3_Checkout) },
	{ "}",			TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_pre1_PatchDB[] = 
{
	{ "PatchDB", 			TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(PatchDB), 0, NULL, 0 },
	{ "root",				TOK_EMBEDDEDSTRUCT(PatchDB, root, parse_pre1_DirEntry) },
	{ "checkins",			TOK_STRUCT(PatchDB, checkins, parse_pre3_Checkin) },
	{ "namedviews",			TOK_STRUCT(PatchDB, namedviews, parse_pre3_NamedView) },
//	{ "patcher_version",	TOK_AUTOINT(PatchDB, patcher_version, 0), NULL },
	{ "", 0, 0 }
};

// Handler

// returns the appropriate parse table, and a unique 4-character identifier in token
static ParseTable* clientManifestParseTable(U32 client_version, char *token, U32 token_size)
{
	extern ParseTable parse_PatchDB[];
	assert(token);
// See the note on old versions above the parse tables
// 	if(client_version < 1)
// 	{
// 		strcpy_s(token, token_size, "PRE1");
// 		return parse_pre1_PatchDB;
// 	}
// 	if(client_version < 3)
// 	{
// 		strcpy_s(token, token_size, "PRE3");
// 		return parse_pre3_PatchDB;
// 	}
	strcpy_s(token, token_size, "CURR");
	return parse_PatchDB;
}

//////////////////////////////////////////////////////////////////////////////

void viewFree(ProjectView *view)
{
	patchfileDestroy(&view->manifest_patch);
	patchfileDestroy(&view->filespec_patch);
	RefSystem_RemoveReferent(view, true);
}

static void getManifestCBOld(DirEntry *match_dir, GetManifestData* gmd)
{
	int dummy;
	FileVersion *match;

	PERFINFO_AUTO_START_FUNC();

	if( !eaSize(&match_dir->versions)
		||
		gmd->project->include_filemap &&
		!filespecMapGetInt(gmd->project->include_filemap, match_dir->path, &dummy) ||
		gmd->project->exclude_filemap &&
		filespecMapGetInt(gmd->project->exclude_filemap, match_dir->path, &dummy))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	match = patchFindVersionInDir(match_dir, gmd->branch, gmd->sandbox, gmd->rev, gmd->incr_from);
	if( match &&
		(	gmd->project->allow_checkins ||
			!(match->flags & FILEVERSION_DELETED))
		)
	{
		DirEntry *dir_entry = patchFindPath(gmd->result, match->parent->path, 1);
		if(!eaSize(&dir_entry->versions)) // multiple dbs are applied from most specific to least
		{
			FileVersion *ver = StructAlloc(parse_FileVersion);
			ver->checksum = match->checksum;
			ver->size = match->size;
			ver->modified = match->modified;
			ver->header_checksum = match->header_checksum;
			ver->header_size = match->header_size;
			ver->flags = match->flags;

			if(	gmd->project->allow_checkins &&
				!gmd->incremental &&
				!(match_dir->flags & DIRENTRY_FROZEN))
			{
				FileVersion *newest = patchFindVersionInDir(match->parent, INT_MAX, gmd->sandbox, INT_MAX, PATCHREVISION_NONE);
				if(gmd->branch < newest->checkin->branch)
					ver->flags |= FILEVERSION_LINK_FORWARD_BROKEN;
				if(match->checkin->branch < gmd->branch)
					ver->flags |= FILEVERSION_LINK_BACKWARD_SOLID;
			}
			eaPush(&dir_entry->versions, ver);

			if(gmd->project->allow_checkins && !gmd->incremental)
			{
				Checkout *checkout = patchFindCheckoutInDir(match->parent, gmd->branch, gmd->sandbox);
				if(checkout)
				{
					// StructClone does a copyall, but checkouts have no special info
					eaPush(&dir_entry->checkouts, StructClone(parse_Checkout, checkout));
				}
			}
		}
		else
		{
			assertmsg(!gmd->project->allow_checkins || gmd->incremental, "multiple matching files in a db project?!");
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

typedef struct FindOldestViewCallbackData {
	char*			key;
	ProjectView*	view;
} FindOldestViewCallbackData;

static S32 findOldestViewCallback(FindOldestViewCallbackData* data, StashElement element)
{
	ProjectView* view = stashElementGetPointer(element);
	
	if(	!data->view ||
		view->access_time < data->view->access_time)
	{
		data->key = stashElementGetKey(element);
		data->view = view;
	}
	
	return 1;
}

static S32 freeOldestView(	PatchProject* proj,
							S32 removeIfThisOld)
{
	FindOldestViewCallbackData data = {0};
	
	stashForEachElementEx(proj->cached_views, findOldestViewCallback, &data);
	
	if(!data.view){
		return 0;
	}else{
		S32 diff = time(NULL) - data.view->access_time;
		bool forced = diff >= removeIfThisOld;
		
		if(!forced){
			return 0;
		}
		
		log_printf(LOG_PATCHSERVER_INFO,
				"Freeing old view %s:\"%s\", not accessed for %d seconds%s.\n",
				proj->name,
				data.key,
				diff,
				forced ? " (forced)" : "");
				
		{
			ProjectView* viewCheck;
			if(!stashRemovePointer(proj->cached_views, data.key, &viewCheck)){
				assert(0);
			}
			assert(viewCheck == data.view);
		}
		
		viewFree(data.view);
		
		return 1;
	}
}

ProjectView* patchprojectFindOrAddView(PatchProject *proj, int branch, int rev, char *sandbox, int incr_from, char *prefix, int client_version,
	const char *creator_token, const char *creator_ip, U64 creator_uid)
{
	ParseTable *parse_table;
	char parse_key[5];
	char view_key[MAX_PATH];
	GetManifestData gmd = {0};
	void (*callback)(DirEntry*,GetManifestData*);
	ProjectView *view;
	bool use_hierarchy = !proj->allow_checkins || incr_from != PATCHREVISION_NONE;
	U32 checkout_time = ea32Get(&proj->serverdb->db->checkout_time, branch);
	U32 rev_time;

	PERFINFO_AUTO_START_FUNC();

	assert(proj && proj->serverdb);

	// See if we already have it
	PERFINFO_AUTO_START("CheckForView", 1);
	if(client_version < PATCHCLIENT_VERSION_REVISIONNUMBERS)
		parse_table = clientManifestParseTable(client_version, SAFESTR(parse_key));
	else
		sprintf(parse_key, "REVS");
	sprintf(view_key, "%d%s %d %d %s %s", branch, sandbox ? sandbox : "", rev, incr_from, parse_key, NULL_TO_EMPTY(prefix));
	
	if(stashFindPointer(proj->cached_views, view_key, &view))
	{
		if(view->checkout_time != checkout_time)
		{
			log_printf(LOG_PATCHSERVER_INFO,
					"Freeing view %s:\"%s\", because the checkout time changed.\n",
					proj->name,
					view_key);
					
			viewFree(view);
			stashRemovePointer(proj->cached_views, view_key, NULL);
		}
		else
		{
			view->access_time = time(NULL);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP_FUNC();
			return view;
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("CheckForView");

	PERFINFO_AUTO_START("FreeViews", 1);
	if(stashGetCount(proj->cached_views) >= 20000){
		freeOldestView(proj, 0);
	}
	
	// Free anything older than ten minutes.
	
	while(stashGetCount(proj->cached_views) >= 10){
		if(!freeOldestView(proj, 60 * 10)){
			break;
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("FreeViews");

	PERFINFO_AUTO_START("InitManifestGet", 1);

	// Log that we're creating a new view.
	// Print it too, unless it's a prefixed view, because there tend to be lots of prefixed views.
	log_printf(LOG_PATCHSERVER_INFO,
		"Creating view: %s:\"%s\"\n",
		proj->name,
		view_key);
	if (!prefix)
		printf("Creating view: %s:\"%s\"\n",
			proj->name,
			view_key);

	//assert(heapValidateAll());
	SERVLOG_PAIRS(LOG_PATCHSERVER_GENERAL, "CreatingView",
		("name", "%s", proj->name)
		("branch", "%u", branch)
		("rev", "%u", rev)
		("sandbox", "%s", sandbox)
		("parse_key", "%s", parse_key)
		("prefix", "%s", prefix?prefix:"(no prefix)")
		("client_version", "%d", client_version)
		("creator_token", "%s", NULL_TO_EMPTY(creator_token))
		("creator_ip", "%s", creator_ip)
		("creator_uid", "%"FORM_LL"u", creator_uid)
	);

	view = calloc(1, sizeof(*view));
	view->checkout_time = checkout_time;
	view->access_time = time(NULL);

	gmd.project = proj;
	gmd.branch = branch;
	gmd.rev = rev;
	gmd.sandbox = sandbox;
	gmd.incr_from = incr_from;
	gmd.incremental = incr_from != PATCHREVISION_NONE;
	gmd.prefix = prefix;

	if(client_version < PATCHCLIENT_VERSION_REVISIONNUMBERS)
	{
		callback = getManifestCBOld;
		gmd.result = patchCreateDb(PATCHDB_POOLED_PATHS, NULL);
		gmd.result->root.name = StructAllocString(proj->serverdb->db->root.name ? proj->serverdb->db->root.name : proj->name);
	}
	else
	{
		// Manifest parsed by patchLoadDbClient()
		callback = getManifestCBRevs;
		estrPrintf(&gmd.result_estr, "# Project: %s  Branch: %d", proj->name, branch);
		if(sandbox && sandbox[0])
		{
			estrConcatf(&gmd.result_estr, " (%s", sandbox);
			if(incr_from != PATCHREVISION_NONE)
				estrConcatf(&gmd.result_estr, " [incremental %d]", incr_from);
			estrConcatf(&gmd.result_estr, ")");
		}
		estrConcatf(&gmd.result_estr, "  Revision: %d", rev);
		if(prefix)
			estrConcatf(&gmd.result_estr, "  Prefix: %s", prefix);
		estrConcatf(&gmd.result_estr, "\n");
		gmd.result_stash = stashTableCreateWithStringKeys(s_manifest_stash_init_size, StashDefault);
	}
	PERFINFO_AUTO_STOP_CHECKED("InitManifestGet");

	// If you touch this, change patchprojectFindVersion() below as well
	PERFINFO_AUTO_START("BuildManifest", 1);
	gmd.walk_heirarchy = proj->serverdb->basedb && use_hierarchy && proj->serverdb->branches[gmd.branch]->parent_branch != PATCHBRANCH_NONE;
	patchForEachDirEntryPrefix(proj->serverdb->db, prefix, callback, &gmd);
	if(gmd.walk_heirarchy)
	{
		// i may be being paranoid, but decrementing time will prevent the view from changing if time
		// is now and another checkin finishes in this second.
		// also note that incrementals travel up the hierarchy as non-incrementals in no sandbox,
		// but from the point the incremental was created
		PatchServerDb *serverdb = proj->serverdb;
		U32 time;
		
		if(incr_from != PATCHREVISION_NONE)
		{
			time = proj->serverdb->db->checkins[incr_from]->time - 1;
			gmd.sandbox = NULL;
			gmd.incr_from = PATCHREVISION_NONE;
		}
		else
			time = proj->serverdb->db->checkins[rev]->time - 1;

		while(serverdb->basedb)
		{
			if(serverdb->branches[gmd.branch]->parent_branch == PATCHBRANCH_TIP)
			{
				serverdb = serverdb->basedb;
				gmd.branch = serverdb->latest_branch;
			}
			else if(serverdb->branches[gmd.branch]->parent_branch == PATCHBRANCH_NONE)
			{
				break;
			}
			else
			{
				gmd.branch = serverdb->branches[gmd.branch]->parent_branch;
				serverdb = serverdb->basedb;
			}
			gmd.rev = patchFindRevByTime(serverdb->db, time, gmd.branch, gmd.sandbox, serverdb->latest_rev);
			patchForEachDirEntryPrefix(serverdb->db, prefix, callback, &gmd);
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("BuildManifest");

	PERFINFO_AUTO_START("WriteManifest", 1);
	rev_time = rev >= 0 ? MAX(proj->serverdb->db->checkins[rev]->time, checkout_time) : 0;
	if(client_version < PATCHCLIENT_VERSION_REVISIONNUMBERS)
	{
		char *manifest_estr = NULL;
		gmd.result->version = 0;
		ParserWriteText(&manifest_estr, parse_table, gmd.result, 0, 0, 0); // note that we've only included what the client can handle
		view->manifest_patch = patchfileFromEStringEx(&manifest_estr, rev_time, view_key);
		patchDbDestroy(&gmd.result);
	}
	else
	{
		view->manifest_patch = patchfileFromEStringEx(&gmd.result_estr, rev_time, view_key);
		stashTableDestroy(gmd.result_stash);
	}
	PERFINFO_AUTO_STOP_CHECKED("WriteManifest");

	if(!stashAddPointer(proj->cached_views, view_key, view, false))
	{
		assertmsg(0, "Failed to add view to cache");
	}

	PERFINFO_AUTO_STOP_FUNC();

	return view;
}

FileVersion* patchprojectFindVersion(PatchProject *proj, char *dir_name, int branch, char *sandbox, int rev, int incr_from, char *prefix,
									 PatchServerDb **pserverdb)
{
	bool use_hierarchy = !proj->allow_checkins || incr_from != PATCHREVISION_NONE;
	PatchServerDb *serverdb = proj->serverdb;
	FileVersion *ver;

	PERFINFO_AUTO_START_FUNC();
	
	//if(!patchprojectIsPathIncluded(proj, dir_name))
	//{
	//	PERFINFO_AUTO_STOP_FUNC();
	//	return NULL;
	//}

	// If you touch this, change patchprojectFindOrAddView() above as well
	ver = patchFindVersion(serverdb->db, dir_name, branch, sandbox, rev, incr_from);
	if(!ver && serverdb->basedb && use_hierarchy) // use the hierarchy
	{
		U32 time;
		
		if(incr_from != PATCHREVISION_NONE)
		{
			time = incr_from >= 0 ? proj->serverdb->db->checkins[incr_from]->time - 1 : 0;
			sandbox = NULL;
		}
		else
			time = rev >= 0 ? proj->serverdb->db->checkins[rev]->time - 1 : 0;

		while(!ver && serverdb->basedb)
		{
			if(serverdb->branches[branch]->parent_branch == PATCHBRANCH_TIP)
			{
				serverdb = serverdb->basedb;
				branch = serverdb->latest_branch;
			}
			else if(serverdb->branches[branch]->parent_branch == PATCHBRANCH_NONE)
			{
				break;
			}
			else
			{
				branch = serverdb->branches[branch]->parent_branch;
				serverdb = serverdb->basedb;
			}
			rev = patchFindRevByTime(serverdb->db, time, branch, sandbox, serverdb->latest_rev);
			ver = patchFindVersion(serverdb->db, dir_name, branch, sandbox, rev, PATCHREVISION_NONE);
		}
	}

	if(ver && ver->parent)
	{
		if(ver->parent->project_cache_time <= g_project_load_time)
			eaClear(&ver->parent->project_cache_allowed);
		if(eaFind(&ver->parent->project_cache_allowed, proj)==-1)
		{
			if(!patchprojectIsPathIncluded(proj, dir_name, prefix))
			{
				PERFINFO_AUTO_STOP_FUNC();
				return NULL;
			}
			eaPush(&ver->parent->project_cache_allowed, proj);
			ver->parent->project_cache_time = time(NULL);
		}
	}

	if(pserverdb)
		*pserverdb = ver ? serverdb : NULL;
	PERFINFO_AUTO_STOP_FUNC();
	return ver;
}

static void generateClientFilespec(char **estr, PatchProject *proj)
{
	int i, j;

	// this is pretty cheesy.  it'll break if there are any quotes in filenames, etc.,
	// but i'd rather do this than build something for the parser
	estrClear(estr);

	if(proj->is_db)
	{
		// this is a database project (i.e. gimme). nothing should be stored in hoggs and everything's required
		// TODO: the client needs to know which files are unrevisioned, so it can skip uploading them
		estrConcatf(estr, "hoggspecs\n{\n\tfilespecs\n\t{\n\t\tspec *\n\t}\n}\n\n");
//		return;
	}
	else
	{
		for(i = 0; i < eaSize(&proj->client_hoggspecs); ++i)
		{
			HoggmapConfig *hoggmap = proj->client_hoggspecs[i];
			estrConcatf(estr, "hoggspecs\n{\n");
			if(hoggmap->hoggname)
				estrConcatf(estr, "\tfilename \"%s\"\n", hoggmap->hoggname);
			if(hoggmap->strip)
				estrConcatf(estr, "\tstrip \"%s\"\n", hoggmap->strip);
			if(hoggmap->mirror_stripped)
				estrConcatf(estr, "\tmirror_stripped 1\n");
			for(j = 0; j < eaSize(&hoggmap->files); ++j)
				estrConcatf(estr, "\tfilespecs\n\t{\n\t\tspec \"%s\"\n\t}\n", hoggmap->files[j]);
			estrConcatf(estr, "}\n\n");
		}
		if(proj->client_mirrored)
		{
			estrConcatf(estr, "flagspecs\n{\n\tflag Mirrored\n");
			for(j = 0; j < eaSize(&proj->client_mirrored->files); ++j)
				estrConcatf(estr, "\tfilespecs\n\t{\n\t\tspec \"%s\"\n\t}\n", proj->client_mirrored->files[j]);
			estrConcatf(estr, "}\n\n");
		}
		if(proj->client_notrequired)
		{
			estrConcatf(estr, "flagspecs\n{\n\tflag NotRequired\n");
			for(j = 0; j < eaSize(&proj->client_notrequired->files); ++j)
				estrConcatf(estr, "\tfilespecs\n\t{\n\t\tspec \"%s\"\n\t}\n", proj->client_notrequired->files[j]);
			estrConcatf(estr, "}\n\n");
		}
	}

	if(proj->allow_checkins)
	{
		if(proj->serverdb->client_nowarn)
		{
			estrConcatf(estr, "flagspecs\n{\n\tflag NoWarn\n");
			for(j = 0; j < eaSize(&proj->serverdb->client_nowarn->files); ++j)
				estrConcatf(estr, "\tfilespecs\n\t{\n\t\tspec \"%s\"\n\t}\n", proj->serverdb->client_nowarn->files[j]);
			estrConcatf(estr, "}\n\n");
		}

		estrConcatf(estr, "flagspecs\n{\n\tflag Included\n");
		if(proj->is_db)
			estrConcatf(estr, "\tfilespecs\n\t{\n\t\tspec *\n\t}\n");
		else for(j = 0; j < eaSize(&proj->include_config); ++j)
			estrConcatf(estr, "\tfilespecs\n\t{\n\t\tspec \"%s\"\n\t}\n", proj->include_config[j]);
		estrConcatf(estr, "}\n\n");

		estrConcatf(estr, "controlspecs\n{\n");
		for(j = 0; j < eaSize(&proj->serverdb->keepvers_config.files); ++j)
		{
			FilemapConfigLine *spec = proj->serverdb->keepvers_config.files[j];
			estrConcatf(estr, "\tspec \"%s\" %s\n", spec->spec, spec->value == PATCHSERVER_KEEP_BINS ? "Bins" : spec->value ? "Incl" : "Excl");
		}
		estrConcatf(estr, "}\n\n");
	}
}

static void lastInitProject(PatchProject *project)
{
	char *filespec = NULL;
	project->cached_views = stashTableCreateWithStringKeys(200, StashDefault|StashDeepCopyKeys_NeverRelease);

	if(!eaSize(&project->allow_ips))
		printf("Warning: project %s does not allow any client ips! (change %s)\n", project->name, g_patchserver_config.filename);

	estrCreate(&filespec);
	generateClientFilespec(&filespec, project);
	project->filespec_patch = patchfileFromEString(&filespec, project->config_patch->filetime);

//	projectviewFindOrAdd(proj, 0, 0, "", PATCHCLIENT_VERSION); // default view
}

PatchProject* patchprojectCreateDbProject(PatchServerDb *serverdb, const char *config_fname)
{
	PatchProject *project = patchserverFindOrAddProject(serverdb->name);
	if(project->serverdb)
		FatalErrorf("Trying to load project %s twice (it was already specified in %s.patchserverdb)", project->name, project->serverdb->name);
	project->is_db = true;
	project->allow_checkins = true;
	project->serverdb = serverdb;
	project->config_patch = patchfileFromFile(config_fname);
	lastInitProject(project);
	return project;
}

PatchProject* patchprojectLoad(const char *name, PatchServerDb *serverdb)
{
	int i;
	char fname[MAX_PATH];

	PatchProject *project = patchserverFindOrAddProject(name);
	if(project->serverdb)
		FatalErrorf("Trying to load project %s twice (specified in %s.patchserverdb and %s.patchserverdb)", project->name, project->serverdb->name, serverdb->name);
	if(project->is_db)
		FatalErrorf("Name conflict! Project %s is a subproject of %s, but shares a name with a database.", project->name, serverdb->name);

	sprintf(fname, "./%s.patchproject", project->name);
	loadstart_printf("Loading %s...", fname);

	ParserReadTextFile(fname, parse_PatchProject, project, 0);

	project->is_db = false;
	project->serverdb = serverdb;

	project->include_filemap = filespecMapCreate();
	if(!eaSize(&project->include_config))
		printf("Warning: No files included in project %s!\n", project->name);
	else for(i = 0; i < eaSize(&project->include_config); i++)
	filespecMapAddInt(project->include_filemap, project->include_config[i], 1);
	project->include_filemap_flat = filespecMapFlatten(project->include_filemap, 0);

	project->exclude_filemap = filespecMapCreate();
	for(i = 0; i < eaSize(&project->exclude_config); i++)
		filespecMapAddInt(project->exclude_filemap, project->exclude_config[i], 1);
	project->exclude_filemap_flat = filespecMapFlatten(project->exclude_filemap, 0);

	project->config_patch = patchfileFromFile(fname);
	lastInitProject(project);

	g_project_load_time = time(NULL);
	loadend_printf(""); 
	return project;
}

void patchprojectReload(PatchProject *project)
{
	int i;
	char fname[MAX_PATH];
	PatchProject loaded = {0};

	assert(project && project->serverdb && !project->is_db); // currently, there's nothing in the db project that changes. if there is, patchserverdbReload should handle it

	sprintf(fname, "./%s.patchproject", project->name);
	ParserReadTextFile(fname, parse_PatchProject, &loaded, 0);

	SWAPP(project->include_config, loaded.include_config);
	filespecMapDestroy(project->include_filemap);
	filespecMapDestroy(project->include_filemap_flat);
	project->include_filemap = filespecMapCreate();
	if(!eaSize(&project->include_config))
		printf("Warning: No files included in project %s!\n", project->name);
	else for(i = 0; i < eaSize(&project->include_config); i++)
		filespecMapAddInt(project->include_filemap, project->include_config[i], 1);
	project->include_filemap_flat = filespecMapFlatten(project->include_filemap, 0);

	SWAPP(project->exclude_config, loaded.exclude_config);
	filespecMapDestroy(project->exclude_filemap);
	filespecMapDestroy(project->exclude_filemap_flat);
	project->exclude_filemap = filespecMapCreate();
	for(i = 0; i < eaSize(&project->exclude_config); i++)
		filespecMapAddInt(project->exclude_filemap, project->exclude_config[i], 1);
	project->exclude_filemap_flat = filespecMapFlatten(project->exclude_filemap, 0);

	patchfileDestroy(&project->config_patch);
	project->config_patch = patchfileFromFile(fname);

	stashTableDestroyEx(project->cached_views, NULL, viewFree);
	project->cached_views = NULL;
	SWAPP(project->client_hoggspecs, loaded.client_hoggspecs); // swap to let textparser destroy the old stuff
	SWAPP(project->client_mirrored, loaded.client_mirrored);
	SWAPP(project->client_notrequired, loaded.client_notrequired);
	patchfileDestroy(&project->filespec_patch);
	lastInitProject(project);

	StructDeInit(parse_PatchProject, &loaded);
	project->reload_me = false;
	g_project_load_time = time(NULL);
}

void patchprojectClear(PatchProject *project)
{
	assert(project && !project->is_db); // db projects shouldn't be getting cleared. if they need to be, project->is_db may need to be cleared...
	if(project->serverdb) // otherwise, it's already clear
	{
		StructDeInit(parse_PatchProject, project);
		filespecMapDestroy(project->include_filemap);
		filespecMapDestroy(project->include_filemap_flat);
		filespecMapDestroy(project->exclude_filemap);
		filespecMapDestroy(project->exclude_filemap_flat);
		stashTableDestroyEx(project->cached_views, NULL, viewFree);
		project->cached_views = NULL;
		patchfileDestroy(&project->config_patch);
		patchfileDestroy(&project->filespec_patch);
		project->serverdb = NULL; // indicates that the project is not loaded
	}
}

void patchprojectHierarchyChanged(PatchProject *project)
{
	stashTableClearEx(project->cached_views, NULL, viewFree);
}

// Make a per-view client filespec.
PatchFile* patchprojectGenerateViewClientFilespec(const char *filename, PatchProject *proj, int branch,
												  char *sandbox, int rev, int incr_from, ProjectView *view)
{
	// If this view has a filespec patch already, return that one.
	if (view->filespec_patch)
		return view->filespec_patch;

	// Check for a per-view filespecoverride, and make a special filespec patch if found.
	if (!proj->is_db)
	{
		char filespecoverride[PATCH_MAX_PATH];
		FileVersion *ver;
		sprintf(filespecoverride, "control/%s.filespecoverride", proj->name);
		ver = patchFindVersion(proj->serverdb->db, filespecoverride, branch, sandbox, rev, incr_from);
		if (ver)
		{
			PatchFile *base = patchfileFromDb(ver, proj->serverdb);
			char *prepend = NULL;
			estrStackCreate(&prepend);
			generateClientFilespec(&prepend, proj);
			view->filespec_patch = patchfileSpecialFromDb(base, proj->serverdb, prepend, ver->modified, filename);
			estrDestroy(&prepend);
			return view->filespec_patch;
		}
	}

	return proj->filespec_patch;
}

#include "patchproject_h_ast.c"
