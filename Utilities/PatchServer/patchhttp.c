#include "patchhttp.h"
#include "patchhttpdb.h"
#include "patchserver.h"
#include "patchdb.h"
#include "patchserverdb.h"
#include "patchproject.h"
#include "patchcommonutils.h"
#include "patchcompaction.h"
#include "patchfile.h"
#include "patchfileloading.h"
#include "patcher_comm.h"
#include "patchpruning.h"
#include "patchmirroring.h"
#include "patchupdate.h"

#include "net/net.h"
#include "HttpLib.h"
#include "httputil.h"
#include "earray.h"
#include "EString.h"
#include "timing.h"
#include "strings_opt.h"
#include "FilespecMap.h"
#include "mathutil.h"
#include "sock.h"
#include "sysutil.h"
#include "hoglib.h"
#include "stashtable.h"
#include "logging.h"
#include "clearsilver.h"
#include "referencesystem.h"
#include "utilitiesLib.h"
#include "ServerLib.h"
#include "GenericHttpServing.h"
#include "file.h"
#include "Regex.h"

#include "autogen/patchhttp_c_ast.h"
#include "autogen/patchserver_h_ast.h"

// Comm used by all HTTP patching.
static NetComm *g_http_comm = NULL;

extern U32 g_http_connections;

// HTTP config named view rules that have been verified
HttpConfigNamedView **verified_namedview_rules = NULL;

// HTTP config branch rules that have been verified
HttpConfigBranch **verified_branch_rules = NULL;

// Autoupdate config rules that have been verified
AutoupConfigRule **verified_autoup_rules = NULL;

#define render(template_file, type) renderEx(req->link, (template_file), parse_##type, data, req)
static void renderEx(NetLink *link, const char *template_file, ParseTable *tpi, void *struct_mem, HttpRequest *req)
{
	bool hdf = hrFindBool(req, "hdfdump");
	char *out = renderTemplate(template_file, tpi, struct_mem, hdf);
	httpSendHeader(link, estrLength(&out), "Content-Type", hdf ? "text/plain; charset=utf-8" : "text/html; charset=utf-8", "Cache-Control", "no-cache", NULL);
	httpSendBytesRaw(link, out, estrLength(&out));
	httpSendComplete(link);
	StructDestroyVoid(tpi, struct_mem);
	estrDestroy(&out);
}

static void patchHttpSendFile(NetLink *link, PatchFile *patch);

// Called when a file requested for HTTP use is loaded.
static void patchserverLoadForHttpCallback(void *userdata, PatchFile *patch, PatchClientLink *client, NetLink *link, int req, int id, int extra, int num_block_reqs, BlockReq *block_reqs)
{
	if(patch->load_state != LOADSTATE_ERROR)
		patchHttpSendFile(link, patch);
	else
		httpSendFileNotFoundError(link, "ERROR: File not in HOGG.");
}

void patchserverLoadForHttp(NetLink *link, PatchFile *patch, bool uncompressed)
{
	WaitingRequest *request;

	PERFINFO_AUTO_START_FUNC();

	request = calloc(sizeof(WaitingRequest), 1);
	request->callback = patchserverLoadForHttpCallback;

	// no client and no req
	ADD_SIMPLE_POINTER_REFERENCE(request->refto_link, link);
	patchfileRequestLoad(patch, !!uncompressed, request);

	PERFINFO_AUTO_STOP();
}

static void patchHttpSendFile(NetLink *link, PatchFile *patch)
{

	// patchHttpDb NetLinks are handled separately.
	if (patchHttpDbIsOurNetLink(link))
		return;

	if(patch->load_state >= LOADSTATE_ALL)
	{
		if(patch->serverdb == g_patchserver_config.autoupdatedb) // TOTAL HACK
		{
			char fname[MAX_PATH];
			sprintf(fname, "%s.exe", patch->fileName.realName);
			httpSendAttachment(link, fname, patch->uncompressed.data, patch->uncompressed.len);
		}
		else
		{
			httpSendAttachment(link, patch->fileName.realName, patch->uncompressed.data, patch->uncompressed.len);
		}
	}
	else
	{
		// Not-loaded, queue up a request
		patchserverLoadForHttp(link, patch, true);
	}
}

static void s_patchHttpSendFile(NetLink *link, FileVersion *file, PatchServerDb *serverdb)
{
	patchHttpSendFile(link, patchfileFromDb(file, serverdb));
}

static const char* fixEmptyString(const char *str)
{
	return str && str[0] ? str : "&nbsp;";
}

static const char* formatTime(U32 time, bool full)
{
	static char formatted[MAX_PATH];
	char timestr[32], timestr_full[32];
	if(time == U32_MAX)
		return "<span title=\"U32_MAX\">Never</span>";
	timeMakeLocalDateStringFromSecondsSince2000(timestr_full, patchFileTimeToSS2000(time));
	timeMakeLocalDateNoTimeStringFromSecondsSince2000(timestr, patchFileTimeToSS2000(time));
	sprintf(formatted, "<span class=\"time\" title=\"%s\">%s</span>", timestr_full, full?timestr_full:timestr);
	return formatted;
}

static const char* formatBranch(int branch, const char *sandbox)
{
	static char *estr = NULL;
	estrPrintf(&estr, "%d", branch);
	if(SAFE_DEREF(sandbox))
	{
		estrConcatf(&estr, " (%s)", sandbox);
	}
	return estr;
}

static const char* linkFromDb(const PatchServerDb *serverdb)
{
	static char *estr = NULL;
	estrPrintf(&estr, "<a href='/%s/'>%s</a>", serverdb->name, serverdb->name);
	return estr;
}

static const char* linkFromCheckin(const PatchServerDb *serverdb, const Checkin *checkin, bool show_author, const char *style)
{
	static char *estr = NULL;
	estrPrintf(&estr, "<a%s href='/%s/checkin/%d/'>#%d (", NULL_TO_EMPTY(style), serverdb->name, checkin->rev, checkin->rev);
	if(show_author)
		estrConcatf(&estr, "%s ", checkin->author ? checkin->author : "[none]");
	estrConcatf(&estr, "%s)</a>", formatTime(checkin->time, false));
	return estr;
}

static const char* linkFromView(const PatchServerDb *serverdb, const NamedView *view)
{
	static char *estr = NULL;
	estrPrintf(&estr, "<a href='/%s/view/%s/'>%s</a>", serverdb->name, view->name, view->name);
	return estr;
}

static const char* linkFromDir(const PatchServerDb *serverdb, const DirEntry *dir, const char *queryStr)
{
	static char *estr = NULL;
	char *q = (queryStr && queryStr[0]) ? "?" : "";
	char *esc = NULL;
	assert(dir->parent);
	urlEscape(dir->path, &esc, true, false);
	estrReplaceOccurrences(&esc, "%2F", "/");
	if(eaSize(&dir->children))
		estrPrintf(&estr, "<b>&lt;<a href='/%s/file/%s/%s%s'>%s</a>&gt;</b>", serverdb->name, esc, q, (queryStr && queryStr[0]) ? queryStr : "", dir->name);
	else
		estrPrintf(&estr, "<a href='/%s/file/%s/%s%s'>%s</a>", serverdb->name, esc, q, (queryStr && queryStr[0]) ? queryStr : "", dir->name);
	estrDestroy(&esc);
	return estr;
	
}

static const char* linkFromVer(const PatchServerDb *serverdb, const FileVersion *ver)
{
	static char *estr = NULL;
	char *esc = NULL;
	urlEscape(ver->parent->path, &esc, true, false);
	estrReplaceOccurrences(&esc, "%2F", "/");
	if(ver->flags & FILEVERSION_DELETED)
		estrPrintf(&estr, "&nbsp;");
	else
		estrPrintf(	&estr,
					"<a href='/%s/file/%s/?get=%d'>Get</a>",
					serverdb->name,
					esc,
					ver->rev);
	estrDestroy(&esc);
	return estr;
}

static const char* pruneLinkFromVer(const PatchServerDb *serverdb, const FileVersion *ver)
{
	static char *estr = NULL;
	char *esc = NULL;
	urlEscape(ver->parent->path, &esc, true, false);
	estrReplaceOccurrences(&esc, "%2F", "/");
	estrPrintf(	&estr,
				"<a href='/%s/file/%s/?prune=%d&time=%d'>Prune</a>",
				serverdb->name,
				esc,
				ver->rev,
				ver->checkin->time);
	estrDestroy(&esc);
	return estr;
}

static int cmpDirEntryName(const DirEntry **left, const DirEntry **right)
{
	return stricmp((*left)->name, (*right)->name);
}

static int cmpDirEntryPath(const DirEntry **left, const DirEntry **right)
{
	return stricmp((*left)->path, (*right)->path);
}


static int shouldShowDirEntryHelper(DirEntry *entry, const char *username)
{
	int i;

	if(!eaSize(&entry->children))
	{
		for(i=eaSize(&entry->checkouts)-1; i>=0; i--)
		{
			Checkout *c = entry->checkouts[i];

			if(!stricmp(username, c->author) || !stricmp("all", username))
				return 1;
		}

		return 0;
	}
	else
	{
		for(i=eaSize(&entry->children)-1; i>=0; i--)
		{
			if(shouldShowDirEntryHelper(entry->children[i], username))
				return 1;
		}

		return 0;
	}

	return 0;
}

static int shouldShowDirEntry(HttpRequest *req, DirEntry *entry)
{
	const char* username = hrFindValue(req, "filterCheckout");

	if(!username || !username[0])
		return 1;

	return shouldShowDirEntryHelper(entry, username);
}

static void concatChildTable(HttpRequest *req, char **estr, PatchServerDb *serverdb, DirEntry *dir, bool shortlist)
{
	char *estr2;
	int i, row1 = 0, row2 = 0, dirs = 0, files = 0;
	char *querystr = NULL;
	static DirEntry **tempArray = NULL;

	if(!eaSize(&dir->children))
		return;

	estrStackCreate(&querystr);
	hrBuildQueryString(req, &querystr);

	estrStackCreate(&estr2);
	estrConcatf(estr, "<table class=table>\n");
	estrConcatf(estr, "<tr class=rowh><td>Filename\n");
	eaCopy(&tempArray, &dir->children);
	eaQSort(tempArray, cmpDirEntryName);
	for(i = 0; i < eaSize(&tempArray); i++)
	{
		DirEntry *child = tempArray[i];
		FileVersion *latest = eaTail(&child->versions);
		bool latest_deleted = latest && latest->flags & FILEVERSION_DELETED;
		if(!shouldShowDirEntry(req, child))
			continue;
		if(eaSize(&child->children))
		{
			estrConcatf(estr, "<tr class=row%s%d><td>%s\n", latest_deleted ? "deleted" : "", row1, linkFromDir(serverdb, child, querystr));
			row1 = !row1;
			dirs++;
		}
		else
		{
			estrConcatf(&estr2, "<tr class=row%s%d><td>%s\n", latest_deleted ? "deleted" : "", row2, linkFromDir(serverdb, child, querystr));
			row2 = !row2;
			files++;
		}
	}
	if(shortlist && dirs && dirs + files > 10)
	{
		assertmsg(!dir->parent, "short concatChildTable not implemented for folders other than root");
		estrConcatf(estr, "<tr class=rowf><td><a href='/%s/file/'>More...</a>\n", serverdb->name);
	}
	else
		estrConcat(estr, estr2, estrLength(&estr2));
	estrConcatf(estr, "</table>\n");
	estrDestroy(&estr2);
	estrDestroy(&querystr);
}

static void concatViewTable(char **estr, PatchServerDb *serverdb, int count, int page, bool morebypage, U32 now)
{
	int i, row, first, last;

	if(!eaSize(&serverdb->db->namedviews))
		return;

	count = ABS(count);
	page = ABS(page);
	first = eaSize(&serverdb->db->namedviews)-1 - page*count;
	last = count ? MAX(first-count+1, 0) : 0;

	estrConcatf(estr, "<table class=\"table\">\n");
	estrConcatf(estr, "<tr class=\"rowh\"><th>View</th><th>Branch</th><th>Revision</th><th>Expires</th><th>Viewed</th><th>External</th><th>Dirty</th><th>HTTP Patching</th></tr>\n");
	for(i = first, row = 0; i >= last; --i, row = !row)
	{
		NamedView *view = serverdb->db->namedviews[i];
		bool expired = view->expires && view->expires < now;
		const char *expire_timestr = formatTime(view->expires, false);
		char *expire_str = NULL;
		if(expired)
			estrPrintf(&expire_str, "<font color=\"red\">Expired %s</font>", expire_timestr);
		else if(view->expires)
			estrPrintf(&expire_str, "%s", expire_timestr);
		else
			estrPrintf(&expire_str, "Never");
		
		estrConcatf(estr, "<tr class=\"row%d\"><td>", row);
		if(expired)
			estrConcatf(estr, "%s", view->name);
		else
			estrConcatf(estr, "<a href='/%s/view/%s/'>%s</a>", serverdb->name, view->name, view->name);
		estrConcatf(estr,
					"</td><td>%s</td><td>%s</td><td>%s</td><td>%d</td><td>%s</td><td>%s</td>"
					"<td><a href=\"/httpconfig?viewprojects=%s&viewname=%s#add_namedview_rule\">Add Rule...</a></td></tr>\n",
					formatBranch(view->branch, view->sandbox),
					linkFromCheckin(serverdb, serverdb->db->checkins[view->rev], false, ""),
					expire_str,
					view->viewed,
					view->viewed_external ? "Yes" : "No",
					view->dirty ? "Yes" : "No",
					serverdb->name, view->name);
		estrDestroy(&expire_str);
	}
	if(count && count < eaSize(&serverdb->db->namedviews))
	{
		estrConcatf(estr, "<tr class=\"rowf\"><td colspan=\"5\">");
		if(i >= 0)
		{
			estrConcatf(estr, "<a href='/%s/view/", serverdb->name);
			if(morebypage)
				estrConcatf(estr, "?c=%d&p=%d", count, page+2); // +1 for next, +1 for humans
			estrConcatf(estr, "'>Show More...</a> ");
		}
		estrConcatf(estr, "<a href='/%s/view/?c=0'>Show All %d...</a>", serverdb->name, eaSize(&serverdb->db->namedviews));
		estrConcatf(estr, "</td></tr>\n");
	}
	estrConcatf(estr, "</table>\n");
}

static U32 findSizeInHogg(	PatchServerDb* serverdb,
							FileVersion* v,
							S32* foundInHoggOut)
{
	if(FALSE_THEN_SET(v->foundSizeInHogg)){
		patchserverdbSetVersionSize(serverdb, v);
	}
	
	if(foundInHoggOut){
		*foundInHoggOut = v->foundInHogg;
	}
	
	return v->sizeInHogg;
}

typedef struct SizeFromHereData {
	PatchServerDb*				serverdb;
	
	S32							minRev;
	S32							maxRev;
	
	S32*						branchMinRev;
	S32*						branchMaxRev;
	
	FileVersion**				verBeforeBranchMinRev;
	
	S32 						showDBSizeInHoggsFromHere;
	const PatchProject*const*	projects;
	
	struct {
		S32						oldestRev;
		U32						fileCount;
		U64						dbSizeFromHere;
		U64						dbSizeInHoggsFromHere;
	} out;
} SizeFromHereData;

static void findSizeFromHere(	SizeFromHereData* sfh,
								DirEntry* de)
{
	S32 useThisDirEntry;

	useThisDirEntry = !eaSize(&sfh->projects);
	
	assert(sfh->branchMinRev);

	if(!useThisDirEntry){
		EARRAY_CONST_FOREACH_BEGIN(sfh->projects, i, isize);
			if(patchprojectIsPathIncluded(sfh->projects[i], de->path, NULL)){
				useThisDirEntry = 1;
				break;
			}
		EARRAY_FOREACH_END;
	}
	
	if(useThisDirEntry){
		U64 size = 0;
		U64 sizeInHoggs = 0;
		
		EARRAY_INT_CONST_FOREACH_BEGIN(sfh->branchMinRev, i, isize);
			sfh->verBeforeBranchMinRev[i] = NULL;
		EARRAY_FOREACH_END;
		
		EARRAY_CONST_FOREACH_BEGIN(de->versions, i, isize);
			FileVersion*	v = de->versions[i];
			S32				branch = v->checkin->branch;

			EARRAY_INT_CONST_FOREACH_BEGIN_FROM(sfh->branchMinRev, j, jsize, branch);
				if(	sfh->branchMaxRev[j] >= 0 &&
					v->rev <= sfh->branchMinRev[j])
				{
					FileVersion* vBest = sfh->verBeforeBranchMinRev[j];
					
					if(	!vBest
						||
						branch > vBest->checkin->branch
						||
						branch == vBest->checkin->branch &&
						v->rev > vBest->rev)
					{
						sfh->verBeforeBranchMinRev[j] = v;
					}
				}
			EARRAY_FOREACH_END;
		EARRAY_FOREACH_END;

		EARRAY_CONST_FOREACH_BEGIN(de->versions, i, isize);
			FileVersion*	v = de->versions[i];
			S32				branch = v->checkin->branch;
			S32				includeVersion = 0;
			
			if(	branch < eaiSize(&sfh->branchMinRev) &&
				v->rev >= sfh->branchMinRev[branch] &&
				v->rev <= sfh->branchMaxRev[branch])
			{
				includeVersion = 1;
			}else{
				EARRAY_CONST_FOREACH_BEGIN(sfh->verBeforeBranchMinRev, j, jsize);
					if(v == sfh->verBeforeBranchMinRev[j]){
						includeVersion = 1;
						break;
					}
				EARRAY_FOREACH_END;
			}
			
			if(!includeVersion){
				continue;
			}
			
			if(	sfh->out.oldestRev < 0 ||
				v->rev < sfh->out.oldestRev)
			{
				sfh->out.oldestRev = v->rev;
			}

			size += v->size;

			if(sfh->showDBSizeInHoggsFromHere){
				sizeInHoggs += findSizeInHogg(	sfh->serverdb,
												v,
												NULL);
			}
		EARRAY_FOREACH_END;

		sfh->out.dbSizeFromHere += size;
		sfh->out.dbSizeInHoggsFromHere += sizeInHoggs;
	}
		
	EARRAY_CONST_FOREACH_BEGIN(de->children, i, isize);
		findSizeFromHere(sfh, de->children[i]);
	EARRAY_FOREACH_END;
}

static U32 s_checkin_http_timeout = 15;
static U32 s_checkin_http_timeout_iterations = 200;

// timeout in seconds for appending the checkin list to a http response
AUTO_CMD_INT(s_checkin_http_timeout, checkin_http_timeout);

// loop count between checks for checkin_http_timeout
AUTO_CMD_INT(s_checkin_http_timeout_iterations, checkin_http_timeout_iterations);

static void concatCheckinTable(	HttpRequest* req,
								char **estr,
								PatchServerDb *serverdb,
								int count,
								int page,
								bool morebypage,
								S32 showDBSizeFromHere,
								S32 showDBSizeInHoggsFromHere,
								const PatchProject*const*const projects,
								const S32*const branches,
								char **include_authors,
								char **exclude_authors)
{
	int i, row, first, last;
	unsigned int tableStart = estrLength(estr);
	U32 start = getCurrentFileTime();

	if(!eaSize(&serverdb->db->checkins))
		return;

	count = ABS(count);
	page = ABS(page);
	first = eaSize(&serverdb->db->checkins)-1 - page*count;
	last = count ? MAX(first-count+1, 0) : 0;

	if(eaSize(&projects)){
		estrConcatf(estr, "Projects: ");
		
		EARRAY_CONST_FOREACH_BEGIN(projects, j, jsize);
			estrConcatf(estr,
						"%s%s",
						j ? ", " : "",
						projects[j]->name);
		EARRAY_FOREACH_END;

		estrConcatf(estr, "<br>");
	}
	
	if(eaiSize(&branches)){
		estrConcatf(estr, "Branches: ");
		
		EARRAY_INT_CONST_FOREACH_BEGIN(branches, j, jsize);
			estrConcatf(estr,
						"%s%d",
						j ? ", " : "",
						branches[j]);
		EARRAY_FOREACH_END;

		estrConcatf(estr, "<br>");
	}

	if(eaSize(&include_authors)){
		estrConcatf(estr, "Including only authors: ");

		EARRAY_CONST_FOREACH_BEGIN(include_authors, j, jsize);
		estrConcatf(estr,
			"%s%s",
			j ? ", " : "",
			include_authors[j]);
		EARRAY_FOREACH_END;

		estrConcatf(estr, "<br>");
	}

	if(eaSize(&exclude_authors)){
		estrConcatf(estr, "Excluding authors: ");

		EARRAY_CONST_FOREACH_BEGIN(exclude_authors, j, jsize);
		estrConcatf(estr,
			"%s%s",
			j ? ", " : "",
			exclude_authors[j]);
		EARRAY_FOREACH_END;

		estrConcatf(estr, "<br>");
	}

	estrConcatf(estr, "<table class=table>\n");
	estrConcatf(estr,
				"<tr class=rowh>"
				"<td>Checkin"
				"<td>Branch"
				"<td>Time"
				"<td>Author"
				"<td>Size Delta"
				"<td>Adds"
				"<td>+Size"
				"<td>Deletes"
				"<td>-Size"
				"%s"
				"%s"
				"%s"
				"<td>Comment\n",
				showDBSizeFromHere ? "<td>DB Size" : "",
				showDBSizeInHoggsFromHere ? "<td>DB Size In Hoggs" : "",
				showDBSizeFromHere || showDBSizeInHoggsFromHere ? "<td>Oldest Rev" : "");
				
	for(i = first, row = 0; i >= last; --i)
	{
		const Checkin*		checkin = serverdb->db->checkins[i];
		S32					totalFilesAdded = 0;
		S32					totalFilesDeleted = 0;
		S64					totalBytesAdded = 0;
		S64					totalBytesDeleted = 0;
		S64					totalBytesDelta = 0;
		SizeFromHereData	sfh = {0};
		
		// check to timeout the operation
		if(first != i && !((first-i) % s_checkin_http_timeout_iterations))
		{
			timeSecondsSince2000Update();
			if(getCurrentFileTime() - start >= s_checkin_http_timeout)
			{
				char ipBuf[32];
				SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "CheckinHttpTimeout", ("time", "%d", getCurrentFileTime() - start) ("count", "%d", count) ("request", "%s", req->path) ("client", "%s", linkGetIpStr(req->link, SAFESTR(ipBuf))));
				estrInsertf(estr, tableStart, "Operation timed out after %d entries.\n", first - i);

				//set up the variables here so that 'next page' acts mostly as expected
				count = MAX(s_checkin_http_timeout_iterations, first-i-s_checkin_http_timeout_iterations);
				page = (eaSize(&serverdb->db->checkins)-1 - first) / count;
				break;
			}
		}

		if (eaiSize(&branches) && eaiFind(&branches, checkin->branch) == -1)
			continue;

		if(checkin->author)
		{
			bool skipcheckin = false;
			char** author_array = NULL;
			eaPush(&author_array, checkin->author);
			if (eaSize(&include_authors) && eaFindRegex(&author_array, &include_authors) == -1)
				skipcheckin = true;
			if (eaSize(&exclude_authors) && eaFindRegex(&author_array, &exclude_authors) != -1)
				skipcheckin = true;
			eaDestroy(&author_array);
			if(skipcheckin)
				continue;
		}
		else
		{
			if (eaSize(&include_authors))
				continue;
		}
		
		if(	showDBSizeFromHere ||
			showDBSizeInHoggsFromHere)
		{
			PERFINFO_AUTO_START("showDBSizeFromHere", 1);
			sfh.showDBSizeInHoggsFromHere = showDBSizeInHoggsFromHere;
			sfh.projects = projects;
			sfh.serverdb = serverdb;
			sfh.out.oldestRev = -1;

			EARRAY_CONST_FOREACH_BEGIN_FROM(serverdb->db->checkins, j, jsize, i);
				Checkin* c = serverdb->db->checkins[j];
				
				devassert(c->branch >= 0);
				
				if(	eaiSize(&branches) &&
					eaiFind(&branches, c->branch) < 0)
				{
					continue;
				}
				
				while(eaiSize(&sfh.branchMaxRev) <= c->branch){
					eaiPush(&sfh.branchMaxRev, -1);
					eaiPush(&sfh.branchMinRev, S32_MAX);
					eaPush(&sfh.verBeforeBranchMinRev, NULL);
				}
				
				MAX1(sfh.branchMaxRev[c->branch], c->rev);
				MIN1(sfh.branchMinRev[c->branch], c->rev);
			EARRAY_FOREACH_END;			
			
			findSizeFromHere(	&sfh,
								&serverdb->db->root);
								
			eaiDestroy(&sfh.branchMaxRev);
			eaiDestroy(&sfh.branchMinRev);
			eaDestroy(&sfh.verBeforeBranchMinRev);

			PERFINFO_AUTO_STOP_CHECKED("showDBSizeFromHere");
		}
		
		EARRAY_CONST_FOREACH_BEGIN(checkin->versions, j, jsize);
			FileVersion* v = checkin->versions[j];
			FileVersion* vOld = patchPreviousVersion(v);
			
			if(v->flags & FILEVERSION_DELETED){
				totalFilesDeleted++;
				
				if(vOld){
					totalBytesDeleted += vOld->size;
					totalBytesDelta -= vOld->size;
				}
			}else{
				totalFilesAdded++;
				totalBytesAdded += v->size;
				
				totalBytesDelta += v->size;
				
				if(vOld){
					totalBytesDelta -= vOld->size;
				}
			}
		EARRAY_FOREACH_END;
		
		row = !row;
		estrConcatf(estr,
					"<tr class=row%d>"
					"<td><a href='/%s/checkin/%d'>%d</a>"
					"<td>%s"	// Branch.
					"<td>%s"	// Time.
					"<td>%s"	// Author.
					"<td>%s"	// Added.
					"<td>%s"	// Added.
					"<td>%s"	// +Size.
					"<td>%s"	// Deleted.
					"<td>%s"	// -Size.
					"%s%s"		// <td>DB Size
					"%s%s"		// <td>DB Size In Hoggs
					"%s%s"		// <td>Oldest Rev
					"<td class=comment>%s"	// Comment.
					"\n"
					,
					row,
					serverdb->name,
					checkin->rev,
					checkin->rev,
					formatBranch(checkin->branch, checkin->sandbox),
					formatTime(checkin->time, true),
					checkin->author,
					totalBytesDelta ? getCommaSeparatedInt(totalBytesDelta) : "",
					totalFilesAdded ? getCommaSeparatedInt(totalFilesAdded) : "",
					totalFilesAdded ? getCommaSeparatedInt(totalBytesAdded) : "",
					totalFilesDeleted ? getCommaSeparatedInt(-totalFilesDeleted) : "",
					totalFilesDeleted ? getCommaSeparatedInt(-totalBytesDeleted) : "",
					showDBSizeFromHere ? "<td>" : "",
					showDBSizeFromHere ? getCommaSeparatedInt(sfh.out.dbSizeFromHere) : "",
					showDBSizeInHoggsFromHere ? "<td>" : "",
					showDBSizeInHoggsFromHere ? getCommaSeparatedInt(sfh.out.dbSizeInHoggsFromHere) : "",
					showDBSizeFromHere || showDBSizeInHoggsFromHere ? "<td>" : "",
					showDBSizeFromHere || showDBSizeInHoggsFromHere ? getCommaSeparatedInt(sfh.out.oldestRev) : "",
					NULL_TO_EMPTY(checkin->comment));
	}
	if(count && count < eaSize(&serverdb->db->checkins))
	{
		estrConcatf(estr, "<tr class=rowf><td colspan=5>");
		if(i >= 0)
		{
			estrConcatf(estr, "<a href='/%s/checkin/", serverdb->name);
			if(morebypage)
				estrConcatf(estr, "?c=%d&p=%d", count, page+2); // +1 for next, +1 for humans
			estrConcatf(estr, "'>Show More...</a> ");
		}
		estrConcatf(estr, "<a href='/%s/checkin/?c=0'>Show All %d...</a>\n", serverdb->name, eaSize(&serverdb->db->checkins));
	}
	estrConcatf(estr, "</table>\n");
}

static int compareCheckinFile(const FileVersion **a, const FileVersion **b)
{
	return stricmp((*a)->parent->path, (*b)->parent->path);
}

static void concatCheckinFilesTable(char **estr, PatchServerDb *serverdb, Checkin *checkin, int count, int page)
{
	int i, row, first, last;

	if(!eaSize(&checkin->versions))
	{
		estrConcatf(estr, "No files found for this checkin, they've probably all been pruned.<br>\n<br>\n");
		return;
	}

	count = ABS(count);
	page = ABS(page);
	first = page*count;
	last = eaSize(&checkin->versions)-1;
	if(count)
		MIN1(last, first+count-1);
	eaQSort(checkin->versions, compareCheckinFile);

	estrConcatf(estr, "<table class=table>\n");
	estrConcatf(estr, "<tr class=rowh><td>Filename<td>Size<td>Modified<td>&nbsp;\n");
	for(i = first, row = 0; i <= last; ++i, row = !row)
	{
		FileVersion *ver = checkin->versions[i];
		FileVersion *prev = patchPreviousVersion(ver);
		const char *style = ver->flags&FILEVERSION_DELETED ? " class='del'" :
							!prev || prev->flags&FILEVERSION_DELETED ? " class='new'" : "";
		estrConcatf(estr, "<tr class=row%d><td><span style=\"font-size:80%%\"><a%s href='/%s/file/%s/'>%s</a></span>", row, style, serverdb->name,
							ver->parent->path, ver->parent->path);
		if(ver->flags & FILEVERSION_DELETED)
			estrConcatf(estr, "<td colspan=3><i>Deleted</i>\n");
		else
			estrConcatf(estr, "<td>%s<td>%s<td>%s\n", getCommaSeparatedInt(ver->size), formatTime(ver->modified, true), linkFromVer(serverdb, ver));
	}
	if(count && count < eaSize(&checkin->versions))
	{
		estrConcatf(estr, "<tr class=rowf><td colspan=4>");
		if(i < eaSize(&checkin->versions))
			estrConcatf(estr, "<a href='/%s/checkin/%d/?c=%d&p=%d'>Show More...</a> ",
								serverdb->name, checkin->rev, count, page+2); // +1 for next, +1 for humans
		estrConcatf(estr, "<a href='/%s/checkin/%d/?c=0'>Show All %d...</a>\n",
							serverdb->name, checkin->rev, eaSize(&checkin->versions));
	}
	estrConcatf(estr, "</table>\n");
}

static void patchHttpAddHeader(	char** estrOut,
								const char* format,
								...)
{
	const char *html_header_1 =
		"<html>\n"
		"<head>\n"
		"<title>";
	const char *html_header_2 =
		"</title>\n"
		"<style type='text/css'>\n"
		"body { background: #eee; color: black; font-family: Verdana, sans-serif; }\n"
		"ul { margin: 0 auto 0.5em 2em; padding: 0; } \n"
		"li { padding-bottom: 0.3em; } \n"
		"table { vertical-align: middle; border-spacing: 0; border-collapse: collapse; margin: 0 auto 1em 0; }\n"
		"tr { border: 0; }\n"
		"td, th { padding: 2px 6px; vertical-align: top; white-space: nowrap; }\n"
		"td.comment { padding: 2px 6px; vertical-align: top; white-space: normal; }\n"
		"form { margin: 0; padding: 0; border: 0; }\n"
		"a:link    { color: #00f; text-decoration: none; }\n"
		"a:visited { color: #00a; text-decoration: none; }\n"
		"a:hover   { color: #00f; text-decoration: underline; }\n"
		"a.new:link    { color: #0c0; text-decoration: none; }\n"
		"a.new:visited { color: #080; text-decoration: none; }\n"
		"a.new:hover   { color: #0c0; text-decoration: underline; }\n"
		"a.del:link    { color: #f00; text-decoration: none; }\n"
		"a.del:visited { color: #a00; text-decoration: none; }\n"
		"a.del:hover   { color: #f00; text-decoration: underline; }\n"
		".table { border: solid #000 1px; border-spacing: 0; }\n"
		".rowh { background: #fff; font-weight: bold; text-align: center; }\n"
		".row0 { background: #eee; }\n"
		".row1 { background: #ddd; }\n"
		".rowdeleted0 { background: #fdd; }\n"
		".rowdeleted1 { background: #ecc; }\n"
		".rowf { background: #fff; }\n"
		".time { font-family: monospace; font-size: smaller; vertical-align: middle; white-space: nowrap; }\n"
		"</style>\n"
		"</head>\n"
		"<body><div>\n";

	estrConcatf(estrOut,
				"%s%s",
				html_header_1,
				g_patchserver_config.displayName ?
					g_patchserver_config.displayName :
					getComputerName());

	if(format)
	{
		VA_START(va, format);
			estrConcatfv(estrOut, format, va);
		VA_END();
	}
		
	estrConcatf(estrOut,
				"%s",
				html_header_2);
}

AUTO_STRUCT;
typedef struct CompareRevisionsFileVersion {
	const FileVersion*				v;			AST(UNOWNED)
} CompareRevisionsFileVersion;

AUTO_STRUCT;
typedef struct CompareRevisionsDirEntry {
	const DirEntry*					de;			AST(UNOWNED)
	CompareRevisionsFileVersion**	crfvs;
} CompareRevisionsDirEntry;

AUTO_STRUCT;
typedef struct CompareRevisionsInput {
	U32								rev;
	U32								branch;
	char*							sandbox;
	
	U64								totalFileSize;
	U64								diffFileSize;
} CompareRevisionsInput;

AUTO_STRUCT;
typedef struct CompareRevisions {
	const PatchProject*				project;	AST(UNOWNED)
	CompareRevisionsInput**			cris;
	CompareRevisionsDirEntry**		crdes;
} CompareRevisions;

static void gatherCompareRevisions(	DirEntry* de,
									CompareRevisions* cr)
{
	if(	de->versions
		&&
		(	!SAFE_MEMBER(cr->project, include_filemap) ||
			filespecMapGetInt(cr->project->include_filemap, de->path, NULL))
		)
	{
		CompareRevisionsDirEntry* crde = NULL;

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
			CompareRevisionsInput*	cri = cr->cris[i];
			const FileVersion*		v = patchFindVersionInDir(	de,
																cri->branch,
																cri->sandbox,
																cri->rev,
																PATCHREVISION_NONE);

			if(	v &&
				!(v->flags & FILEVERSION_DELETED))
			{
				CompareRevisionsFileVersion* crfv = StructAlloc(parse_CompareRevisionsFileVersion);

				if(!crde){
					crde = StructAlloc(parse_CompareRevisionsDirEntry);
					crde->de = de;

					eaPush(&cr->crdes, crde);
				}

				crfv->v = v;

				while(eaSize(&crde->crfvs) < i){
					eaPush(&crde->crfvs, NULL);
				}

				eaPush(&crde->crfvs, crfv);
			}
		EARRAY_FOREACH_END;
	}

	EARRAY_CONST_FOREACH_BEGIN(de->children, i, isize);
		gatherCompareRevisions(de->children[i], cr);
	EARRAY_FOREACH_END;
}

static S32 sortCompareRevisionsDirEntryBySize(	void* context,
												const CompareRevisionsDirEntry** d1Ptr,
												const CompareRevisionsDirEntry** d2Ptr)
{
	const CompareRevisionsDirEntry* d1 = *d1Ptr;
	const CompareRevisionsDirEntry* d2 = *d2Ptr;
	const S32						i = (intptr_t)context;
	S32								sizeDiff;
	
	if(!eaGet(&d1->crfvs, i)){
		if(eaGet(&d2->crfvs, i)){
			return 1;
		}
		return stricmp(d1->de->path, d2->de->path);
	}
	if(!eaGet(&d2->crfvs, i)){
		return -1;
	}
	
	sizeDiff = d2->crfvs[i]->v->size - d1->crfvs[i]->v->size;
	
	if(!sizeDiff){
		return stricmp(d1->de->path, d2->de->path);
	}
	
	return sizeDiff;
}

AUTO_STRUCT;
typedef struct CompareRevsData
{
	ServerConfig *config; AST(UNOWNED)
	PatchServerDb *serverdb; AST(UNOWNED)
	char *error; AST(ESTRING)
	char *url_base; AST(ESTRING)
	//CompareRevisions *cr; AST(UNOWNED)
	const PatchProject*	project; AST(UNOWNED)
} CompareRevsData;

static void CompareRevsHandle(NetLink* link,
							  PatchServerDb* serverdb,
							  const char** args,
							  const char** values,
							  S32 count)
{
	CompareRevsData*    data = StructCreate(parse_CompareRevsData);
	CompareRevisions*	cr;
	S32					badParams = 0;
	S32					allFiles = 0;
	S32					countToDisplay = -1;
	S32					startCount = 0;
	char				sortBy[100] = "";
	char				projectName[100] = "";
	char *estr=NULL;

	data->config = &g_patchserver_config;
	data->serverdb = serverdb;

	if(count <= 1)
	{
		estrConcatf(&data->error, "No params!");
		badParams = 1;
	}

	cr = StructAlloc(parse_CompareRevisions);
	eaCreate(&cr->crdes);

	FOR_BEGIN_FROM(i, 1, count);
	{
		if(!strcmp(args[i], "r") && values[i]){
			CompareRevisionsInput*	cri;
			U32						rev = atoi(values[i]);

			if(!rev)
			{
				NamedView *view = patchFindNamedView(serverdb->db, values[i]);
				if(!view)
				{
					estrConcatf(&data->error, "Can't parse value %s as a revision!", values[i]);
					badParams = 1;
					break;
				}
				rev = view->rev;
			}

			if(rev >= (U32)eaSize(&serverdb->db->checkins)){
				estrConcatf(&data->error, "Revision out of range: %d!", rev);
				badParams = 1;
				break;
			}
			cri = StructAlloc(parse_CompareRevisionsInput);
			eaPush(&cr->cris, cri);
			cri->rev = rev;
			cri->branch = serverdb->db->checkins[rev]->branch;
			cri->sandbox = StructAllocString(serverdb->db->checkins[rev]->sandbox);
		}
		else if(!stricmp(args[i], "allfiles") && values[i]){
			allFiles = !!atoi(values[i]);
		}
		else if(!stricmp(args[i], "count") && values[i]){
			countToDisplay = atoi(values[i]);
		}
		else if(!stricmp(args[i], "start") && values[i]){
			startCount = atoi(values[i]);
		}
		else if(!stricmp(args[i], "sort") && values[i]){
			strcpy(sortBy, values[i]);
		}
		else if(!stricmp(args[i], "project") && values[i]){
			strcpy(projectName, values[i]);

			cr->project = NULL;

			EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, j, jsize);
			{
				if(!stricmp(projectName, serverdb->projects[j]->name)){
					cr->project = serverdb->projects[j];
					break;
				}
			}
			EARRAY_FOREACH_END;

			if(!cr->project){
				projectName[0] = 0;
			}
		}
	}
	FOR_END;

	if(	!badParams &&
		!eaSize(&cr->cris))
	{
		estrConcatf(&data->error, "No revisions specified!");
		badParams = 1;
	}

	if(!badParams){
		S32		row = 0;
		//char	urlBase[1000];

		estrPrintf(&data->url_base, "/%s/CompareRevs?", serverdb->name);

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
		{
			estrConcatf(&data->url_base, "%sr=%d", i ? "&" : "", cr->cris[i]->rev);
		}
		EARRAY_FOREACH_END;

		if(allFiles){
			estrConcatf(&data->url_base, "&allFiles=1");
		}

		if(countToDisplay >= 0){
			estrConcatf(&data->url_base, "&count=%d", countToDisplay);
		}

		if(startCount >= 0){
			estrConcatf(&data->url_base, "&start=%d", startCount);
		}

		if(sortBy[0]){
			estrConcatf(&data->url_base, "&sort=%s", sortBy);
		}

		if(projectName[0]){
			estrConcatf(&data->url_base, "&project=%s", projectName);
		}

		gatherCompareRevisions(&serverdb->db->root, cr);

		// Show the project selector.

		EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, i, isize);
		{
			PatchProject* p = serverdb->projects[i];
			if(p->is_db){
				continue;
			}
			if(!stricmp(projectName, p->name)){
				estrConcatf(&estr,
					" | <strong>%s</strong>",
					p->name);
			}else{
				estrConcatf(&estr,
					" | <a href=\"%s&project=%s\">%s</a>",
					data->url_base,
					p->name,
					p->name);
			}
		}
		EARRAY_FOREACH_END;

		estrConcatf(&estr, "</span><br>\n");

		// Show the selector for all/different files.

		estrConcatf(&estr,
			"Show within project: "
			"<span style=\"font-size:80%%\">"
			"<a href=\"%s&allfiles=%d\">%s files</a>"
			"</span><br>",
			data->url_base,
			!allFiles,
			allFiles ? "Different" : "All");

		estrConcatf(&estr, "<p><table class=table>\n");

		// Output the table header.

		estrConcatf(&estr,
			"<tr class=rowh>"
			"<td>"
			"<td><a href=\"%s&sort=file\">File</a><br><span style=\"font-size:50%%\">%s total files</span>",
			data->url_base,
			getCommaSeparatedInt(eaSize(&cr->crdes)));

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
		{
			CompareRevisionsInput*	cri = cr->cris[i];
			const Checkin*			c = serverdb->db->checkins[cri->rev];

			estrConcatf(&estr,
				"<td><a href=\"%s&sort=r%d\">%d</a><br>"
				"<span style=\"font-size:50%%\">Branch: %d%s%s</span>",
				data->url_base,
				i,
				cri->rev,
				c->branch,
				SAFE_DEREF(c->sandbox) ? "<br>Sandbox: " : "",
				NULL_TO_EMPTY(c->sandbox));
		}
		EARRAY_FOREACH_END;

		// Sort.

		if(!stricmp(sortBy, "file")){
			// Do nothing.
		}else{
			EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
			{
				char sortKey[100];
				sprintf(sortKey, "r%d", i);
				if(!stricmp(sortKey, sortBy)){
					eaQSort_s(cr->crdes, sortCompareRevisionsDirEntryBySize, (void*)(intptr_t)i);
				}
			}
			EARRAY_FOREACH_END;
		}

		// Output the files.

		EARRAY_CONST_FOREACH_BEGIN(cr->crdes, i, isize);
		{
			CompareRevisionsDirEntry*	crde = cr->crdes[i];
			S32							showMe = 1;

			EARRAY_CONST_FOREACH_BEGIN(crde->crfvs, j, jsize);
			{
				if(crde->crfvs[j]){
					cr->cris[j]->totalFileSize += crde->crfvs[j]->v->size;
				}
			}
			EARRAY_FOREACH_END;

			if(!allFiles){
				showMe = eaSize(&crde->crfvs) != eaSize(&cr->cris);

				if(!showMe){
					EARRAY_CONST_FOREACH_BEGIN_FROM(crde->crfvs, j, jsize, 1);
					{
						if(	!crde->crfvs[0] ||
							!crde->crfvs[j] ||
							crde->crfvs[0]->v->size != crde->crfvs[j]->v->size ||
							crde->crfvs[0]->v->checksum != crde->crfvs[j]->v->checksum)
						{
							showMe = 1;
							break;
						}
					}
					EARRAY_FOREACH_END;
				}
			}

			if(showMe){
				EARRAY_CONST_FOREACH_BEGIN(crde->crfvs, j, jsize);
				{
					if(crde->crfvs[j]){
						cr->cris[j]->diffFileSize += crde->crfvs[j]->v->size;
					}
				}
				EARRAY_FOREACH_END;

				if(startCount > 0){
					startCount--;
				}
				else if(countToDisplay < 0 ||
					countToDisplay > 0)
				{
					if(countToDisplay > 0){
						countToDisplay--;
					}

					estrConcatf(&estr,
						"<tr class=row%d><td>%d<td><span style=\"font-size:80%%\">%s</span>",
						row = !row,
						i + 1,
						crde->de->path);

					EARRAY_CONST_FOREACH_BEGIN(cr->cris, j, jsize);
					CompareRevisionsFileVersion* crfv = eaGet(&crde->crfvs, j);

					if(!crfv){
						estrConcatf(&estr, "<td>&nbsp;");
					}else{
						estrConcatf(&estr,
							"<td><a href=\"/%s/Checkin/%d\">%s</a>",
							serverdb->name,
							crfv->v->rev,
							getCommaSeparatedInt(crfv->v->size));
					}
					EARRAY_FOREACH_END;
				}
			}
		}
		EARRAY_FOREACH_END;

		// Output the total revision size.

		estrConcatf(&estr,
			"<tr class=row%d><td><td><strong>Total Revision Size</strong>",
			row = !row);

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
		{
			estrConcatf(&estr,
				"<td><strong>%s</strong>",
				getCommaSeparatedInt(cr->cris[i]->totalFileSize));
		}
		EARRAY_FOREACH_END;

		// Output the diff revision size.

		estrConcatf(&estr,
			"<tr class=row%d><td><td><strong>Diff Revision Size</strong>",
			row = !row);

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
		estrConcatf(&estr,
			"<td><strong>%s</strong>",
			getCommaSeparatedInt(cr->cris[i]->diffFileSize));
		EARRAY_FOREACH_END;

		// Done.

		estrConcatf(&estr, "</table>");
	}
	
	//data->cr = cr;
	data->project = cr->project;
	//render("compare_revs.cs", CompareRevsData);
	//httpSendStr(link,estr);
	estrDestroy(&estr);

	StructDestroySafe(parse_CompareRevisions, &cr);
}

static void sendCompareRevs(NetLink* link,
							PatchServerDb* serverdb,
							const char** args,
							const char** values,
							S32 argCount)
{
	CompareRevisions*	cr;
	S32					badParams = 0;
	S32					allFiles = 0;
	S32					countToDisplay = -1;
	S32					startCount = 0;
	char				sortBy[100] = "";
	char				projectName[100] = "";
	char*				estr = NULL;
	const char*			helpText = 	"<p>Use /CompareRevs?r=100&r=101<br>\n"
									"Other params:<br>\n"
									"allFiles=1 (show all files, not just diffs)<br>\n"
									"count=# (how many files to show)<br>\n"
									"start=# (which file to start at)<br>\n"
									"sort=[file/r#] (sort by file or one of the revisions by size)<br>\n"
									;

	estrStackCreate(&estr);
	patchHttpAddHeader(&estr, ": %s: Compare Revisions", serverdb->name);

	if(argCount <= 1)
	{
		estrConcatf(&estr, "No params!%s", helpText);
		badParams = 1;
	}
	
	cr = StructAlloc(parse_CompareRevisions);
	eaCreate(&cr->crdes);
		
	FOR_BEGIN_FROM(i, 1, argCount);
		if(!strcmp(args[i], "r") && values[i]){
			CompareRevisionsInput*	cri;
			U32						rev = atoi(values[i]);

			if(!rev)
			{
				NamedView *view = patchFindNamedView(serverdb->db, values[i]);
				if(!view)
				{
					estrConcatf(&estr, "Can't parse value %s as a revision!%s", values[i], helpText);
					badParams = 1;
					break;
				}
				rev = view->rev;
			}
			
			if(rev >= (U32)eaSize(&serverdb->db->checkins)){
				estrConcatf(&estr, "Revision out of range: %d!%s", rev, helpText);
				badParams = 1;
				break;
			}
			cri = StructAlloc(parse_CompareRevisionsInput);
			eaPush(&cr->cris, cri);
			cri->rev = rev;
			cri->branch = serverdb->db->checkins[rev]->branch;
			cri->sandbox = StructAllocString(serverdb->db->checkins[rev]->sandbox);
		}
		else if(!stricmp(args[i], "allfiles") && values[i]){
			allFiles = !!atoi(values[i]);
		}
		else if(!stricmp(args[i], "count") && values[i]){
			countToDisplay = atoi(values[i]);
		}
		else if(!stricmp(args[i], "start") && values[i]){
			startCount = atoi(values[i]);
		}
		else if(!stricmp(args[i], "sort") && values[i]){
			strcpy(sortBy, values[i]);
		}
		else if(!stricmp(args[i], "project") && values[i]){
			strcpy(projectName, values[i]);
			
			cr->project = NULL;
			
			EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, j, jsize);
				if(!stricmp(projectName, serverdb->projects[j]->name)){
					cr->project = serverdb->projects[j];
					break;
				}
			EARRAY_FOREACH_END;
			
			if(!cr->project){
				projectName[0] = 0;
			}
		}
	FOR_END;
	
	if(	!badParams &&
		!eaSize(&cr->cris))
	{
		estrConcatf(&estr, "No revisions specified!%s", helpText);
		badParams = 1;
	}
	
	if(!badParams){
		S32		row = 0;
		char	urlBase[1000];
		
		sprintf(urlBase, "/%s/CompareRevs?", serverdb->name);
		
		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
			strcatf(urlBase, "%sr=%d", i ? "&" : "", cr->cris[i]->rev);
		EARRAY_FOREACH_END;
		
		if(allFiles){
			strcat(urlBase, "&allFiles=1");
		}
		
		if(countToDisplay >= 0){
			strcatf(urlBase, "&count=%d", countToDisplay);
		}
		
		if(startCount >= 0){
			strcatf(urlBase, "&start=%d", startCount);
		}
		
		if(sortBy[0]){
			strcatf(urlBase, "&sort=%s", sortBy);
		}
		
		if(projectName[0]){
			strcatf(urlBase, "&project=%s", projectName);
		}
		
		gatherCompareRevisions(&serverdb->db->root, cr);

		estrConcatf(&estr,
					"<a href=\"/\">PatchServer</a> - "
					"<a href=\"/%s/\">%s</a> - "
					"Compare Revisions<p>\n",
					serverdb->name,
					serverdb->name);

		// Show the project selector.
		
		estrConcatf(&estr,
					"Project: <span style=\"font-size:80%%\">");
		
		if(cr->project){
			estrConcatf(&estr,
						"<a href=\"%s&project=\">All Files</a>",
						urlBase);
		}else{
			estrConcatf(&estr,
						"<strong>All Files</strong>");
		}
					
		EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, i, isize);
			PatchProject* p = serverdb->projects[i];
			if(p->is_db){
				continue;
			}
			if(!stricmp(projectName, p->name)){
				estrConcatf(&estr,
							" | <strong>%s</strong>",
							p->name);
			}else{
				estrConcatf(&estr,
							" | <a href=\"%s&project=%s\">%s</a>",
							urlBase,
							p->name,
							p->name);
			}
		EARRAY_FOREACH_END;

		estrConcatf(&estr, "</span><br>\n");
		
		// Show the selector for all/different files.
		
		estrConcatf(&estr,
					"Show within project: "
					"<span style=\"font-size:80%%\">"
					"<a href=\"%s&allfiles=%d\">%s files</a>"
					"</span><br>",
					urlBase,
					!allFiles,
					allFiles ? "Different" : "All");
		
		estrConcatf(&estr, "<p><table class=table>\n");
		
		// Output the table header.
		
		estrConcatf(&estr,
					"<tr class=rowh>"
					"<td>"
					"<td><a href=\"%s&sort=file\">File</a><br><span style=\"font-size:50%%\">%s total files</span>",
					urlBase,
					getCommaSeparatedInt(eaSize(&cr->crdes)));
					
		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
			CompareRevisionsInput*	cri = cr->cris[i];
			const Checkin*			c = serverdb->db->checkins[cri->rev];
			
			estrConcatf(&estr,
						"<td><a href=\"%s&sort=r%d\">%d</a><br>"
						"<span style=\"font-size:50%%\">Branch: %d%s%s</span>",
						urlBase,
						i,
						cri->rev,
						c->branch,
						SAFE_DEREF(c->sandbox) ? "<br>Sandbox: " : "",
						NULL_TO_EMPTY(c->sandbox));
		EARRAY_FOREACH_END;
		
		// Sort.
		
		if(!stricmp(sortBy, "file")){
			// Do nothing.
		}else{
			EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
				char sortKey[100];
				sprintf(sortKey, "r%d", i);
				if(!stricmp(sortKey, sortBy)){
					eaQSort_s(cr->crdes, sortCompareRevisionsDirEntryBySize, (void*)(intptr_t)i);
				}
			EARRAY_FOREACH_END;
		}
		
		// Output the files.
		
		EARRAY_CONST_FOREACH_BEGIN(cr->crdes, i, isize);
			CompareRevisionsDirEntry*	crde = cr->crdes[i];
			S32							showMe = 1;
			
			EARRAY_CONST_FOREACH_BEGIN(crde->crfvs, j, jsize);
				if(crde->crfvs[j]){
					cr->cris[j]->totalFileSize += crde->crfvs[j]->v->size;
				}
			EARRAY_FOREACH_END;

			if(!allFiles){
				showMe = eaSize(&crde->crfvs) != eaSize(&cr->cris);
				
				if(!showMe){
					EARRAY_CONST_FOREACH_BEGIN_FROM(crde->crfvs, j, jsize, 1);
						if(	!crde->crfvs[0] ||
							!crde->crfvs[j] ||
							crde->crfvs[0]->v->size != crde->crfvs[j]->v->size ||
							crde->crfvs[0]->v->checksum != crde->crfvs[j]->v->checksum)
						{
							showMe = 1;
							break;
						}
					EARRAY_FOREACH_END;
				}
			}
			
			if(showMe){
				EARRAY_CONST_FOREACH_BEGIN(crde->crfvs, j, jsize);
					if(crde->crfvs[j]){
						cr->cris[j]->diffFileSize += crde->crfvs[j]->v->size;
					}
				EARRAY_FOREACH_END;
				
				if(startCount > 0){
					startCount--;
				}
				else if(countToDisplay < 0 ||
						countToDisplay > 0)
				{
					if(countToDisplay > 0){
						countToDisplay--;
					}
					
					estrConcatf(&estr,
								"<tr class=row%d><td>%d<td><span style=\"font-size:80%%\">%s</span>",
								row = !row,
								i + 1,
								crde->de->path);
					
					EARRAY_CONST_FOREACH_BEGIN(cr->cris, j, jsize);
						CompareRevisionsFileVersion* crfv = eaGet(&crde->crfvs, j);
						
						if(!crfv){
							estrConcatf(&estr, "<td>&nbsp;");
						}else{
							estrConcatf(&estr,
										"<td><a href=\"/%s/Checkin/%d\">%s</a>",
										serverdb->name,
										crfv->v->rev,
										getCommaSeparatedInt(crfv->v->size));
						}
					EARRAY_FOREACH_END;
				}
			}
		EARRAY_FOREACH_END;
		
		// Output the total revision size.
		
		estrConcatf(&estr,
					"<tr class=row%d><td><td><strong>Total Revision Size</strong>",
					row = !row);

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
			estrConcatf(&estr,
						"<td><strong>%s</strong>",
						getCommaSeparatedInt(cr->cris[i]->totalFileSize));
		EARRAY_FOREACH_END;
				
		// Output the diff revision size.

		estrConcatf(&estr,
					"<tr class=row%d><td><td><strong>Diff Revision Size</strong>",
					row = !row);

		EARRAY_CONST_FOREACH_BEGIN(cr->cris, i, isize);
			estrConcatf(&estr,
						"<td><strong>%s</strong>",
						getCommaSeparatedInt(cr->cris[i]->diffFileSize));
		EARRAY_FOREACH_END;

		// Done.
		
		estrConcatf(&estr, "</table>");
	}
	
	httpSendStr(link,estr);
	estrDestroy(&estr);

	StructDestroySafe(parse_CompareRevisions, &cr);
}

typedef struct DBStats {
	PatchServerDb*			serverdb;
	const PatchProject**	projects;
	S32						getFileSizeInHogg;

	struct {
		U32					totalFiles;
		S64					totalFileBytes;
		
		U32					inHoggFiles;
		U32					inHoggFilesDeleted;
		S64					inHoggFileBytes;
	} out;
} DBStats;

static void gatherDBStats(	DBStats* dbStats,
							DirEntry* de)
{
	EARRAY_CONST_FOREACH_BEGIN(dbStats->projects, i, isize);
		if(patchprojectIsPathIncluded(dbStats->projects[i], de->path, NULL)){
			EARRAY_CONST_FOREACH_BEGIN(de->versions, j, jsize);
				FileVersion* v = de->versions[j];
				
				dbStats->out.totalFiles++;
				dbStats->out.totalFileBytes += v->size;
				
				if(dbStats->getFileSizeInHogg){
					S32 foundInHogg;
					U32 sizeInHogg = findSizeInHogg(dbStats->serverdb, v, &foundInHogg);
					
					if(foundInHogg){
						if(v->flags & FILEVERSION_DELETED){
							dbStats->out.inHoggFilesDeleted++;
						}else{
							dbStats->out.inHoggFiles++;
							dbStats->out.inHoggFileBytes += v->sizeInHogg;
						}
					}
				}
			EARRAY_FOREACH_END;
			break;
		}
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(de->children, i, isize);
		gatherDBStats(dbStats, de->children[i]);
	EARRAY_FOREACH_END;
}

static void sendDBStats(NetLink* link,
						PatchServerDb* serverdb,
						const char** args,
						const char** values,
						S32 argCount)
{
	DBStats		dbStats = {0};
	char*		estr = NULL;
	
	FOR_BEGIN_FROM(i, 1, argCount);
		if(!strcmp(args[i], "p") && values[i]){
			EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.projects, j, jsize);
				PatchProject* p = g_patchserver_config.projects[j];

				if(!stricmp(p->name, values[i])){
					eaPushUnique(&dbStats.projects, p);
				}
			EARRAY_FOREACH_END;
		}
		else if(!stricmp(args[i], "hoggSize")){
			dbStats.getFileSizeInHogg = 1;
		}
	FOR_END;
	
	dbStats.serverdb = serverdb;
	
	gatherDBStats(	&dbStats,
					&serverdb->db->root);

	estrStackCreate(&estr);
	patchHttpAddHeader(&estr, ": %s DB Stats", serverdb->name);
	
	estrConcatf(&estr,
				"<a href=\"/\">PatchServer</a> - "
				"<a href=\"/%s/\">%s</a> - "
				"DB Stats<p>\n",
				serverdb->name,
				serverdb->name);

	// Print the projects list.
	
	estrConcatf(&estr, "Projects: ");
	
	EARRAY_CONST_FOREACH_BEGIN(dbStats.projects, i, isize);
		estrConcatf(&estr,
					"%s%s",
					i ? ", " : "",
					dbStats.projects[i]->name);
	EARRAY_FOREACH_END;
	
	estrConcatf(&estr, "<br>");
	
	// Print the stats.

	estrConcatf(&estr,
				"Total files: %s<br>",
				getCommaSeparatedInt(dbStats.out.totalFiles));

	estrConcatf(&estr,
				"Total bytes uncompressed: %s<br>",
				getCommaSeparatedInt(dbStats.out.totalFileBytes));

	if(dbStats.getFileSizeInHogg){
		estrConcatf(&estr,
					"Total non-delete files found in hoggs: %s<br>",
					getCommaSeparatedInt(dbStats.out.inHoggFiles));
					
		estrConcatf(&estr,
					"Total delete files found in hoggs: %s<br>",
					getCommaSeparatedInt(dbStats.out.inHoggFilesDeleted));

		estrConcatf(&estr,
					"Total bytes in hoggs: %s<br>",
					getCommaSeparatedInt(dbStats.out.inHoggFileBytes));
	}

	httpSendStr(link,estr);
	estrDestroy(&estr);
	
	eaDestroy(&dbStats.projects);
}

AUTO_STRUCT;
typedef struct RedirectsData
{
	ServerConfig *config; AST(UNOWNED)
	char *title; AST(ESTRING)
} RedirectsData;

static void RedirectsHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	RedirectsData* data;
	
	if(req->method == HTTPMETHOD_POST)
	{
		int i, j;
		char argbuf[100];
		for(i=0; i<eaSize(&g_patchserver_config.redirects); i++)
		{
			ServerRedirect *redir = g_patchserver_config.redirects[i];
			sprintf(argbuf, "disable_%d", i);
			redir->disabled = hrFindBool(req, argbuf);
			for(j=0; j<eaSize(&redir->alts); j++)
			{
				sprintf(argbuf, "disable_%d_%d", i, j);
				redir->alts[j]->disabled = hrFindBool(req, argbuf);
			}
		}
		httpRedirect(req->link, "/redirects");
		return;
	}

	data = StructCreate(parse_RedirectsData);
	data->config = &g_patchserver_config;
	estrPrintf(&data->title, "Redirects");
	render("redirects.cs", RedirectsData);
}

AUTO_STRUCT;
typedef struct ConfigData
{
	ServerConfig *config; AST(UNOWNED)
	char *title; AST(ESTRING)
} ConfigData;

static void ConfigHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	ConfigData* data;

	if(req->method == HTTPMETHOD_POST)
	{
		if(g_patchserver_config.bandwidth_config)
		{
			g_patchserver_config.bandwidth_config->total = hrFindInt(req, "bandwidth_total", U32_MAX);
			g_patchserver_config.bandwidth_config->per_user = hrFindInt(req, "bandwidth_peruser", U32_MAX);
			g_patchserver_config.bandwidth_config->time_slice = hrFindInt(req, "bandwidth_timeslice", 1000);
		}

		g_patchserver_config.locked = hrFindBool(req, "locked");
		g_patchserver_config.reportdown = hrFindBool(req, "down");

		httpRedirect(req->link, "/config");
		return;
	}

	data = StructCreate(parse_ConfigData);
	data->config = &g_patchserver_config;
	estrPrintf(&data->title, "Configuration");
	render("config.cs", ConfigData);
}

static void MonitorHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	if (g_patchserver_config.reportdown)
		httpSendServiceUnavailable(req->link, "The server is down.\n");
	else
		httpSendStr(req->link, "The server is up.\n");
}

// HttpConfig web output
AUTO_STRUCT;
typedef struct WebHttpConfig
{
	char *server_display_name;							AST(UNOWNED)
	char *title;										AST(UNOWNED)
	EARRAY_OF(HttpConfigNamedView) namedview_rules;		AST(UNOWNED)
	EARRAY_OF(HttpConfigBranch) branch_rules;			AST(UNOWNED)
	const char *viewprojects;							AST(UNOWNED)
	const char *viewname;								AST(UNOWNED)
	const char *branchprojects;							AST(UNOWNED)
	const char *branchnumbers;							AST(UNOWNED)
	bool controls;
} WebHttpConfig;

// Check the method.
#define VALIDATE_METHOD(METHOD)												\
	if(req->method != METHOD)												\
	{																		\
		httpSendMethodNotAllowedError(req->link, "Method Not Allowed");		\
		return;																\
	}

// Web-based dynamic configuration of child HttpConfigs.
static void HttpConfigHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	WebHttpConfig *data;

	VALIDATE_METHOD(HTTPMETHOD_GET);

	data = StructCreate(parse_WebHttpConfig);
	data->title = "HTTP Patching Dynamic Configuration";
	data->server_display_name = g_patchserver_config.displayName;
	data->controls = true;
	data->namedview_rules = g_patchserver_config.dynamic_http_config ? g_patchserver_config.dynamic_http_config->namedviews : NULL;
	data->branch_rules = g_patchserver_config.dynamic_http_config ? g_patchserver_config.dynamic_http_config->branches : NULL;

	data->viewprojects = hrFindValue(req, "viewprojects");
	data->viewname = hrFindValue(req, "viewname");
	data->branchprojects = hrFindValue(req, "branchprojects");
	data->branchnumbers = hrFindValue(req, "branchnumbers");

	render("httpconfig.cs", WebHttpConfig);
}

// Copy a list from an HTTP form, performing the following:
//   -trim whitespace
//   -split on commas
//   -omit empty elements
//   -merge duplicates
static void FormCopyList(HttpRequest *req, const char *name, char ***processed, bool remove_duplicates)
{
	char **input = hrFindList(req, name);
	EARRAY_CONST_FOREACH_BEGIN(input, i, n);
	{
		DivideString(input[i], ",;", processed,
			DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			|(remove_duplicates ? DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE : 0));
	}
	EARRAY_FOREACH_END;
	eaDestroy(&input);
	eaReverse(processed);
}

// Add rule web output
AUTO_STRUCT;
typedef struct AddRuleData
{
	char *server_display_name;							AST(UNOWNED)
	char *title;										AST(UNOWNED)
	STRING_EARRAY errors;								AST(ESTRING)
	EARRAY_OF(HttpConfigNamedView) namedview_rules;		AST(UNOWNED)
	EARRAY_OF(HttpConfigBranch) branch_rules;			AST(UNOWNED)
	int id;
} AddRuleData;

AUTO_FIXUPFUNC;
TextParserResult fixupXMLAccountResponse(AddRuleData *p, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		eaDestroy(&p->namedview_rules);
		eaDestroy(&p->branch_rules);
	}
	return PARSERESULT_SUCCESS;
}

// Verify a new named view rule.
static void HttpConfigHandleAddNamedViewRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	AddRuleData *data;
	char **errors = NULL;
	HttpConfigNamedView *rule;
	char **iplist = NULL, **hostnamelist = NULL, **portlist = NULL, **prefixlist = NULL, **weightlist = NULL;
	int size;

	VALIDATE_METHOD(HTTPMETHOD_POST);

	// Retrieve form data.
	rule = StructCreate(parse_HttpConfigNamedView);
	rule->disabled = 1;
	FormCopyList(req, "viewcategories", &rule->categories, true);
	FormCopyList(req, "viewips", &iplist, true);
	EARRAY_CONST_FOREACH_BEGIN(iplist, i, n);
	{
		AllowIp *ip = StructCreate(parse_AllowIp);
		int success = ParserReadText(iplist[i], parse_AllowIp, ip, 0);
		if (!success)
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "\"%s\" is not a valid IP", iplist[i]);
			StructDestroy(parse_AllowIp, ip);
		}
		eaPush(&rule->ips, ip);
	}
	EARRAY_FOREACH_END;
	FormCopyList(req, "viewprojects", &rule->project, true);
	rule->name = strdup(hrFindValue(req, "viewname"));
	FormCopyList(req, "viewhostname", &hostnamelist, false);
	FormCopyList(req, "viewport", &portlist, false);
	FormCopyList(req, "viewprefix", &prefixlist, false);
	FormCopyList(req, "viewweight", &weightlist, false);

	// Validate IP list.
	if (!eaSize(&rule->ips))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one allow IP is required.");
	}

	// Validate project names.
	if (!eaSize(&rule->project))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one project name is required.");
	}

	// Validate view name.
	if (!*rule->name)
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "View name required.");
	}

	// Validate server list.
	size = eaSize(&hostnamelist);
	if (size > eaSize(&portlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a port");
	}
	if (size > eaSize(&prefixlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a prefix");
	}
	if (size > eaSize(&weightlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a weight");
	}
	if (size < MAX(eaSize(&portlist), MAX(eaSize(&prefixlist), eaSize(&weightlist))))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a hostname");
	}
	if (size == eaSize(&portlist) && size == eaSize(&prefixlist) && size == eaSize(&weightlist))
	{
		devassert(portlist);
		EARRAY_CONST_FOREACH_BEGIN(hostnamelist, i, n);
		{
			HttpConfigWeightedInfo *info = StructCreate(parse_HttpConfigWeightedInfo);
			char *end;
			unsigned long port;
			char *string = NULL;
			char lbname[4 + 12 + 5 + 1];
			errno = 0;
			info->weight = strtod(weightlist[i], &end);
			if (errno || *end || info->weight <= 0 || info->weight != info->weight)
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "\"%s\" is not a valid weight number", weightlist[i]);
			}
			port = strtoul(portlist[i], &end, 10);
			if (errno || *end || port == 0 || port > 0xffff)
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "\"%s\" is not a valid port", portlist[i]);
			}
			sprintf(lbname, "viewloadbalancer%d", i);
			info->load_balancer = !!hrFindValue(req, lbname);
			estrStackCreate(&string);
			estrPrintf(&string, "%s:%lu/%s", hostnamelist[i], port, prefixlist[i]);
			info->info = strdup(string);
			estrDestroy(&string);
			eaPush(&rule->http_info, info);
		}
		EARRAY_FOREACH_END;
	}

	// Create result page.
	data = StructCreate(parse_AddRuleData);
	data->title = "Verify New Rule";
	data->server_display_name = g_patchserver_config.displayName;
	eaPush(&data->namedview_rules, rule);
	eaDestroy(&iplist);
	eaDestroy(&hostnamelist);
	eaDestroy(&portlist);
	eaDestroy(&prefixlist);
	eaDestroy(&weightlist);

	// If there are errors, render error page.
	if (eaSize(&errors))
	{
		data->title = "Rule Errors";
		data->errors = errors;
		render("add_rule_errors.cs", AddRuleData);
		StructDestroy(parse_HttpConfigNamedView, rule);
		return;
	}

	// Save this rule.
	data->id = eaPush(&verified_namedview_rules, rule);

	// Render verify page.
	render("add_namedview_rule_verify.cs", AddRuleData);
}

// Verify a new branch rule.
static void HttpConfigHandleAddBranchRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	AddRuleData *data;
	char **errors = NULL;
	HttpConfigBranch *rule;
	char **iplist = NULL, **branchlist = NULL, **hostnamelist = NULL, **portlist = NULL, **prefixlist = NULL, **weightlist = NULL;
	int size;

	VALIDATE_METHOD(HTTPMETHOD_POST);

	// Retrieve form data.
	rule = StructCreate(parse_HttpConfigBranch);
	rule->disabled = 1;
	FormCopyList(req, "branchcategories", &rule->categories, true);
	FormCopyList(req, "branchips", &iplist, true);
	EARRAY_CONST_FOREACH_BEGIN(iplist, i, n);
	{
		AllowIp *ip = StructCreate(parse_AllowIp);
		int success = ParserReadText(iplist[i], parse_AllowIp, ip, 0);
		if (!success)
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "\"%s\" is not a valid IP", iplist[i]);
			StructDestroy(parse_AllowIp, ip);
		}
		eaPush(&rule->ips, ip);
	}
	EARRAY_FOREACH_END;
	FormCopyList(req, "branchprojects", &rule->project, true);
	FormCopyList(req, "branchnumbers", &branchlist, true);
	EARRAY_CONST_FOREACH_BEGIN(branchlist, i, n);
	{
		char *end;
		unsigned long branch;
		errno = 0;
		branch = strtoul(branchlist[i], &end, 10);
		if (errno || *end)
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "\"%s\" is not a valid branch number", branchlist[i]);
		}
		eaiPush(&rule->branch, branch);
	}
	EARRAY_FOREACH_END;
	FormCopyList(req, "branchhostname", &hostnamelist, false);
	FormCopyList(req, "branchport", &portlist, false);
	FormCopyList(req, "branchprefix", &prefixlist, false);
	FormCopyList(req, "branchweight", &weightlist, false);

	// Validate IP list.
	if (!eaSize(&rule->ips))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one allow IP is required.");
	}

	// Validate project names.
	if (!eaSize(&rule->project))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one project name is required.");
	}

	// Validate branch list.
	if (!eaiSize(&rule->branch))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one branch number is required.");
	}

	// Validate server list.
	size = eaSize(&hostnamelist);
	if (size > eaSize(&portlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a port");
	}
	if (size > eaSize(&prefixlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a prefix");
	}
	if (size > eaSize(&weightlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a weight");
	}
	if (size < MAX(eaSize(&portlist), MAX(eaSize(&prefixlist), eaSize(&weightlist))))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One source server is missing a hostname");
	}
	if (size == eaSize(&portlist) && size == eaSize(&prefixlist) && eaSize(&weightlist))
	{
		devassert(portlist);
		EARRAY_CONST_FOREACH_BEGIN(hostnamelist, i, n);
		{
			HttpConfigWeightedInfo *info = StructCreate(parse_HttpConfigWeightedInfo);
			char *end;
			unsigned long port;
			char lbname[6 + 12 + 5 + 1];
			char *string = NULL;
			errno = 0;
			info->weight = strtod(weightlist[i], &end);
			if (errno || *end || info->weight <= 0 || info->weight != info->weight)
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "\"%s\" is not a valid weight number", weightlist[i]);
			}
			port = strtoul(portlist[i], &end, 10);
			if (errno || *end || port == 0 || port > 0xffff)
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "\"%s\" is not a valid port", portlist[i]);
			}
			sprintf(lbname, "branchloadbalancer%d", i);
			info->load_balancer = !!hrFindValue(req, lbname);
			estrStackCreate(&string);
			estrPrintf(&string, "%s:%lu/%s", hostnamelist[i], port, prefixlist[i]);
			info->info = strdup(string);
			estrDestroy(&string);
			eaInsert(&rule->http_info, info, 0);
		}
		EARRAY_FOREACH_END;
	}

	// Create result page.
	data = StructCreate(parse_AddRuleData);
	data->title = "Verify New Rule";
	data->server_display_name = g_patchserver_config.displayName;
	eaPush(&data->branch_rules, rule);
	eaDestroy(&iplist);
	eaDestroy(&hostnamelist);
	eaDestroy(&portlist);
	eaDestroy(&prefixlist);
	eaDestroy(&weightlist);

	// If there are errors, render error page.
	if (eaSize(&errors))
	{
		data->title = "Rule Errors";
		data->errors = errors;
		render("add_rule_errors.cs", AddRuleData);
		StructDestroy(parse_HttpConfigBranch, rule);
		return;
	}

	// Save this rule.
	data->id = eaPush(&verified_branch_rules, rule);

	// Render verify page.
	render("add_branch_rule_verify.cs", AddRuleData);
}

// Add a new named view rule.
static void HttpConfigHandleAddNamedViewRuleVerified(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;

	// Get ID.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);
	if (id < 0 || id >= eaSize(&verified_namedview_rules))
	{
		httpSendClientError(req->link, "Internal error: Bad ID");
		return;
	}

	// Add rule.
	patchserverHttpConfigAddNamedView(verified_namedview_rules[id]);
	verified_namedview_rules[id] = 0;

	// Redirect.
	httpRedirect(req->link, "/httpconfig");
}

// Add a new branch rule.
static void HttpConfigHandleAddBranchRuleVerified(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;

	// Get ID.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);
	if (id < 0 || id >= eaSize(&verified_branch_rules))
	{
		httpSendClientError(req->link, "Internal error: Bad ID");
		return;
	}

	// Add rule.
	patchserverHttpConfigAddBranchRule(verified_branch_rules[id]);
	verified_branch_rules[id] = 0;

	// Redirect.
	httpRedirect(req->link, "/httpconfig");
}

// HttpConfigHandleDeleteNamedViewRule() web output
AUTO_STRUCT;
typedef struct DeleteRuleData
{
	char *server_display_name;							AST(UNOWNED)
	char *title;										AST(UNOWNED)
	char *type;											AST(UNOWNED)
	STRING_EARRAY errors;								AST(ESTRING)
	EARRAY_OF(HttpConfigNamedView) namedview_rules;		AST(UNOWNED)
	EARRAY_OF(HttpConfigBranch) branch_rules;			AST(UNOWNED)
	int id;
} DeleteRuleData;

// Handle actions to modify a named view rule.
static void HttpConfigHandleEditNamedViewRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;
	int index;
	HttpConfigNamedView *rule = NULL;

	// Make sure the configuration exists at all.
	if (!g_patchserver_config.dynamic_http_config)
	{
		httpSendClientError(req->link, "Internal error: no HTTP config");
		return;
	}

	// Find rule.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.dynamic_http_config->namedviews, i, n);
	{
		if (g_patchserver_config.dynamic_http_config->namedviews[i]->id == id)
		{
			index = i;
			rule = g_patchserver_config.dynamic_http_config->namedviews[i];
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (!rule)
	{
		httpSendClientError(req->link, "Internal error: Bad ID");
		return;
	}

	// Process action.
	if (hrFindValue(req, "enable"))
	{
		rule->disabled = false;
		patchserverSaveDynamicHttpConfig();
		httpRedirect(req->link, "/httpconfig");
	}
	else if (hrFindValue(req, "disable"))
	{
		rule->disabled = true;
		patchserverSaveDynamicHttpConfig();
		httpRedirect(req->link, "/httpconfig");
	}
	else if (hrFindValue(req, "delete"))
	{
		// Create page variables.
		DeleteRuleData *data = StructCreate(parse_DeleteRuleData);
		data->title = "Confirm Deletion";
		data->server_display_name = g_patchserver_config.displayName;
		data->type = "namedview";
		data->id = id;
		eaPush(&data->namedview_rules, rule);

		// Render page.
		render("confirm_delete.cs", DeleteRuleData);
	}
	else
		httpSendClientError(req->link, "No action");
}

// Delete a rule after confirmation.
static void HttpConfigHandleDeleteNamedViewRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;
	bool success;

	// Make sure the configuration exists at all.
	if (!g_patchserver_config.dynamic_http_config)
	{
		httpSendClientError(req->link, "Internal error: no HTTP config");
		return;
	}

	// Get ID.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);

	// Delete rule.
	success = patchserverHttpConfigDeleteNamedView(id);

	// Process the deletion.
	if (!success)
	{
		httpSendClientError(req->link, "Unable to delete");
		return;
	}

	// Redirect on success.
	httpRedirect(req->link, "/httpconfig");
}

// Edit a branch rule.
static void HttpConfigHandleEditBranchRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;
	int index;
	HttpConfigBranch *rule = NULL;

	// Make sure the configuration exists at all.
	if (!g_patchserver_config.dynamic_http_config)
	{
		httpSendClientError(req->link, "Internal error: no HTTP config");
		return;
	}

	// Find rule.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.dynamic_http_config->branches, i, n);
	{
		if (g_patchserver_config.dynamic_http_config->branches[i]->id == id)
		{
			index = i;
			rule = g_patchserver_config.dynamic_http_config->branches[i];
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (!rule)
	{
		httpSendClientError(req->link, "Internal error: Bad ID");
		return;
	}

	// Process action.
	if (hrFindValue(req, "enable"))
	{
		rule->disabled = false;
		patchserverSaveDynamicHttpConfig();
		httpRedirect(req->link, "/httpconfig");
	}
	else if (hrFindValue(req, "disable"))
	{
		rule->disabled = true;
		patchserverSaveDynamicHttpConfig();
		httpRedirect(req->link, "/httpconfig");
	}
	else if (hrFindValue(req, "delete"))
	{
		// Create page variables.
		DeleteRuleData *data = StructCreate(parse_DeleteRuleData);
		data->title = "Confirm Deletion";
		data->server_display_name = g_patchserver_config.displayName;
		data->type = "branch";
		data->id = id;
		eaPush(&data->branch_rules, rule);

		// Render page.
		render("confirm_delete.cs", DeleteRuleData);
	}
	else
		httpSendClientError(req->link, "No action");
}

//
static void HttpConfigHandleDeleteBranchRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;
	bool success;

	// Make sure the configuration exists at all.
	if (!g_patchserver_config.dynamic_http_config)
	{
		httpSendClientError(req->link, "Internal error: no HTTP config");
		return;
	}

	// Get ID.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);

	// Delete rule.
	success = patchserverHttpConfigDeleteBranchRule(id);

	// Process the deletion.
	if (!success)
	{
		httpSendClientError(req->link, "Unable to delete");
		return;
	}

	// Redirect on success.
	httpRedirect(req->link, "/httpconfig");
}

// HttpConfig web output
AUTO_STRUCT;
typedef struct WebAutoupConfig
{
	char *server_display_name;					AST(UNOWNED)
	char *title;								AST(UNOWNED)
	EARRAY_OF(AutoupConfigRule) autoup_rules;	AST(UNOWNED)
	bool controls;
} WebAutoupConfig;

// Web-based dynamic configuration of Autoupdate configs.
static void AutoupConfigHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	WebAutoupConfig *data;

	VALIDATE_METHOD(HTTPMETHOD_GET);

	data = StructCreate(parse_WebAutoupConfig);
	data->title = "Autoupdate Dynamic Configuration";
	data->server_display_name = g_patchserver_config.displayName;
	data->autoup_rules = g_patchserver_config.dynamic_autoup_config ? g_patchserver_config.dynamic_autoup_config->autoup_rules : NULL;
	data->controls = true;

	render("autoupconf.cs", WebAutoupConfig);
}

// Add Autoupdate web output
AUTO_STRUCT;
typedef struct AddAutoupData
{
	char *server_display_name;							AST(UNOWNED)
	char *title;										AST(UNOWNED)
	STRING_EARRAY errors;								AST(ESTRING)
	EARRAY_OF(AutoupConfigRule) autoup_rules;			AST(UNOWNED)
	int id;
} AddAutoupData;

// Verify a new Autoupdate rule.
static void AutoupConfigHandleAddRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	AddAutoupData *data;
	char **errors = NULL;
	AutoupConfigRule *rule = NULL;
	char **iplist = NULL, **revlist = NULL, **weightlist = NULL;
	int size;

	VALIDATE_METHOD(HTTPMETHOD_POST);

	// Retrieve form data.
	rule = StructCreate(parse_AutoupConfigRule);
	rule->disabled = 1;
	FormCopyList(req, "autoupToken", &rule->tokens, false);
	FormCopyList(req, "autoupCategory", &rule->categories, false);
	FormCopyList(req, "autoupIP", &iplist, true);
	EARRAY_CONST_FOREACH_BEGIN(iplist, i, n);
	{
		AllowIp *ip = StructCreate(parse_AllowIp);
		int success = ParserReadText(iplist[i], parse_AllowIp, ip, 0);
		if (!success)
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "\"%s\" is not a valid IP", iplist[i]);
			StructDestroy(parse_AllowIp, ip);
		}
		eaPush(&rule->ips, ip);
	}
	EARRAY_FOREACH_END;
	FormCopyList(req, "revisionRev", &revlist, false);
	FormCopyList(req, "revisionWeight", &weightlist, false);

	// Validate token names.
	if(!eaSize(&rule->tokens))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one token name is required.");
	}

	// Validate IP list.
	if(!eaSize(&rule->ips))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "At least one allow IP is required.");
	}

	// Validate revision list. This validation code has been copy/pasted.
	size = eaSize(&revlist);
	if(size == 0)
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "Must have at least one revision");
	}
	else if(size > eaSize(&weightlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One revision is missing a weight");
	}
	else if(size < eaSize(&weightlist))
	{
		int index = eaPush(&errors, 0);
		estrPrintf(&errors[index], "One revision is missing a revision number");
	}
	else
	{
		EARRAY_CONST_FOREACH_BEGIN(revlist, i, n);
		{
			AutoupConfigWeightedRevision *revision = StructCreate(parse_AutoupConfigWeightedRevision);
			char *end;
			errno = 0;
			revision->weight = strtod(weightlist[i], &end);
			if(errno || *end || revision->weight <= 0 || revision->weight != revision->weight)
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "\"%s\" is not a valid weight number", weightlist[i]);
			}
			revision->rev = strtol(revlist[i], &end, 10);
			if(errno || *end || revision->rev < 0)
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "\"%s\" is not a valid revision number", revlist[i]);
			}
			eaPush(&rule->autoup_rev, revision);
		}
		EARRAY_FOREACH_END;
	}

	// Validate that each token in the rule has each revision in the rule. This validation code has been copy/pasted.
	EARRAY_CONST_FOREACH_BEGIN(rule->tokens, i, n);
	{
		EARRAY_CONST_FOREACH_BEGIN(rule->autoup_rev, j, m);
		{
			if(!patchFindVersion(g_patchserver_config.autoupdatedb->db, rule->tokens[i], INT_MAX, NULL, rule->autoup_rev[j]->rev, PATCHREVISION_NONE))
			{
				int index = eaPush(&errors, 0);
				estrPrintf(&errors[index], "There is no Autoupdate revision \"%d\" for token \"%s\"", rule->autoup_rev[j]->rev, rule->tokens[i]);
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	// Create result page.
	data = StructCreate(parse_AddAutoupData);
	data->title = "Verify New Rule";
	data->server_display_name = g_patchserver_config.displayName;
	eaPush(&data->autoup_rules, rule);
	eaDestroy(&revlist);
	eaDestroy(&weightlist);

	// If there are errors, render error page.
	if(eaSize(&errors))
	{
		data->title = "Rule Errors";
		data->errors = errors;
		render("add_autoup_errors.cs", AddAutoupData);
		StructDestroy(parse_AutoupConfigRule, rule);
		return;
	}

	// Save this rule.
	data->id = eaPush(&verified_autoup_rules, rule);

	// Render verify page.
	render("add_autoup_rule_verify.cs", AddAutoupData);
}

// Add a new Autoupdate rule.
static void AutoupConfigHandleAddRuleVerified(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;

	// Get ID.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);
	if (id < 0 || id >= eaSize(&verified_autoup_rules))
	{
		httpSendClientError(req->link, "Internal error: no Autoupdate config");
		return;
	}

	// Add rule.
	patchserverAutoupConfigAddRule(verified_autoup_rules[id]);
	verified_autoup_rules[id] = 0;

	// Redirect.
	httpRedirect(req->link, "/autoupconf");
}

// AutoupConfigHandleDeleteRule() web output
AUTO_STRUCT;
typedef struct DeleteAutoupRuleData
{
	char *server_display_name;							AST(UNOWNED)
	char *title;										AST(UNOWNED)
	STRING_EARRAY errors;								AST(ESTRING)
	EARRAY_OF(AutoupConfigRule) autoup_rules;			AST(UNOWNED)
	int id;
} DeleteAutoupRuleData;

// Handle actions to modify an Autoupdate rule.
static void AutoupConfigHandleEditRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;
	AutoupConfigRule *rule = NULL;

	// Make sure the configuration exists at all.
	if (!g_patchserver_config.dynamic_autoup_config)
	{
		httpSendClientError(req->link, "Internal error: no Autoupdate config");
		return;
	}

	// Find rule.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.dynamic_autoup_config->autoup_rules, i, n);
	{
		if (g_patchserver_config.dynamic_autoup_config->autoup_rules[i]->id == id)
		{
			rule = g_patchserver_config.dynamic_autoup_config->autoup_rules[i];
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (!rule)
	{
		httpSendClientError(req->link, "Internal error: Bad ID");
		return;
	}

	// Process action.
	if(hrFindValue(req, "Revisions"))
	{
		AutoupConfigRule *ruleCopy = StructClone(parse_AutoupConfigRule, rule);
		char **revlist = NULL, **weightlist = NULL;
		char **errors = NULL;
		int size;
		FormCopyList(req, "revisionRev", &revlist, false);
		FormCopyList(req, "revisionWeight", &weightlist, false);

		// Validate revision list. This validation code has been copy/pasted.
		size = eaSize(&revlist);
		if(size == 0)
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "Must have at least one revision");
		}
		else if(size > eaSize(&weightlist))
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "One revision is missing a weight");
		}
		else if(size < eaSize(&weightlist))
		{
			int index = eaPush(&errors, 0);
			estrPrintf(&errors[index], "One revision is missing a revision number");
		}
		else
		{
			eaDestroyStruct(&ruleCopy->autoup_rev, parse_AutoupConfigWeightedRevision);
			EARRAY_CONST_FOREACH_BEGIN(revlist, i, n);
			{
				AutoupConfigWeightedRevision *revision = StructCreate(parse_AutoupConfigWeightedRevision);
				char *end;
				errno = 0;
				revision->weight = strtod(weightlist[i], &end);
				if(errno || *end || revision->weight <= 0 || revision->weight != revision->weight)
				{
					int index = eaPush(&errors, 0);
					estrPrintf(&errors[index], "\"%s\" is not a valid weight number", weightlist[i]);
				}
				revision->rev = strtol(revlist[i], &end, 10);
				if(errno || *end || revision->rev < 0)
				{
					int index = eaPush(&errors, 0);
					estrPrintf(&errors[index], "\"%s\" is not a valid revision number", revlist[i]);
				}
				eaPush(&ruleCopy->autoup_rev, revision);
			}
			EARRAY_FOREACH_END;
		}

		// Validate that each token in the rule has each revision in the rule. This validation code has been copy/pasted.
		EARRAY_CONST_FOREACH_BEGIN(ruleCopy->tokens, i, n);
		{
			EARRAY_CONST_FOREACH_BEGIN(ruleCopy->autoup_rev, j, m);
			{
				if(!patchFindVersion(g_patchserver_config.autoupdatedb->db, ruleCopy->tokens[i], INT_MAX, NULL, ruleCopy->autoup_rev[j]->rev, PATCHREVISION_NONE))
				{
					int index = eaPush(&errors, 0);
					estrPrintf(&errors[index], "There is no Autoupdate revision \"%d\" for token \"%s\"", ruleCopy->autoup_rev[j]->rev, ruleCopy->tokens[i]);
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		// If there are errors, render error page.
		if(eaSize(&errors))
		{
			// Create error page.
			AddAutoupData *data = StructCreate(parse_AddAutoupData);
			data->title = "Rule Errors";
			data->server_display_name = g_patchserver_config.displayName;
			eaPush(&data->autoup_rules, rule);
			data->errors = errors;
			render("add_autoup_errors.cs", AddAutoupData);
		}
		else
		{
			StructCopyFields(parse_AutoupConfigRule, ruleCopy, rule, 0, 0);
			patchserverSaveDynamicAutoupConfig();
			httpRedirect(req->link, "/autoupconf");
		}
		StructDestroy(parse_AutoupConfigRule, ruleCopy);
		eaDestroy(&revlist);
		eaDestroy(&weightlist);
	}
	else if(hrFindValue(req, "enable"))
	{
		rule->disabled = false;
		patchserverSaveDynamicAutoupConfig();
		httpRedirect(req->link, "/autoupconf");
	}
	else if(hrFindValue(req, "disable"))
	{
		rule->disabled = true;
		patchserverSaveDynamicAutoupConfig();
		httpRedirect(req->link, "/autoupconf");
	}
	else if(hrFindValue(req, "delete"))
	{
		// Create page variables.
		DeleteAutoupRuleData *data = StructCreate(parse_DeleteAutoupRuleData);
		data->title = "Confirm Deletion";
		data->server_display_name = g_patchserver_config.displayName;
		data->id = id;
		eaPush(&data->autoup_rules, rule);

		// Render page.
		render("confirm_autoup_delete.cs", DeleteAutoupRuleData);
	}
	else
		httpSendClientError(req->link, "No action");
}

// Delete a rule after confirmation.
static void AutoupConfigHandleDeleteRule(SA_PARAM_NN_VALID HttpRequest *req)
{
	int id;
	bool success;

	// Make sure the configuration exists at all.
	if (!g_patchserver_config.dynamic_autoup_config)
	{
		httpSendClientError(req->link, "Internal error: no autoupdate config");
		return;
	}

	// Get ID.
	VALIDATE_METHOD(HTTPMETHOD_POST);
	id = hrFindInt(req, "id", -1);

	// Delete rule.
	success = patchserverAutoupConfigDeleteRule(id);

	// Process the deletion.
	if(!success)
	{
		httpSendClientError(req->link, "Unable to delete");
		return;
	}

	// Redirect on success.
	httpRedirect(req->link, "/autoupconf");
}

AUTO_COMMAND ACMD_NAME(AddRedirect) ACMD_CATEGORY(XMLRPC);
void XmlRpcAddRedirect(const char *direct_to, char *ips)
{
	ServerRedirect *new_redirect = StructCreate(parse_ServerRedirect);
	new_redirect->direct_to.server = StructAllocString(direct_to);
	//eaPush(&new_redirect->ips, StructAllocString(ips));
	eaPush(&g_patchserver_config.redirects, new_redirect);
}

AUTO_STRUCT;
typedef struct ConnectionsData
{
	ServerConfig *config; AST(UNOWNED)
	char *title; AST(ESTRING)
	PatchClientLink **clients; AST(UNOWNED)
	const LinkStats **link_stats; AST(UNOWNED)
	U64 current_time;
	U32 global_bucket;
	char global_percent[16];
	U32 throttled_requests;
	U32 avg_speed;
} ConnectionsData;

AUTO_FIXUPFUNC;
TextParserResult ConnectionsDataFixup(ConnectionsData *data, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&data->clients);
		eaDestroy(&data->link_stats);
		break;
	}
	return PARSERESULT_SUCCESS;
}

static void ConnectionsHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	int i;
	ConnectionsData* data;
	const char *format = hrFindValue(req, "format");
	const char *template = "connections.cs";

	data = StructCreate(parse_ConnectionsData);
	data->config = &g_patchserver_config;
	estrPrintf(&data->title, "Connections");
	patchserverGetClients(&data->clients);
	FOR_EACH_IN_EARRAY_FORWARDS(data->clients, PatchClientLink, client)
		eaPush(&data->link_stats, linkStats(client->link));
	FOR_EACH_END
	data->current_time = (U64)time(NULL);
	if(g_patchserver_config.bandwidth_config)
	{
		data->global_bucket = patchserverThrottleLastGlobalBucket();
		if(g_patchserver_config.bandwidth_config->total)
			sprintf(data->global_percent, "%.2f", 100 - (patchserverThrottleLastGlobalBucket() * 100.0 / g_patchserver_config.bandwidth_config->total));
	}
	data->throttled_requests = patchserverThrottledRequestCount();
	if(eaSize(&data->clients))
	{
		for(i=eaSize(&data->clients)-1; i>=0; i--)
		{
			U32 time_running = data->current_time - data->clients[i]->start_time;
			if(!time_running)
				continue;
			data->avg_speed += (data->link_stats[i]->send.real_bytes / (time_running * eaSize(&data->clients)));
		}
	}
	if(format && stricmp(format, "json")==0)
		template = "connections_json.cs";
	render(template, ConnectionsData);
}

static void concatProjectTable(char **estr, PatchServerDb *serverdb)
{
	int i, row;
	bool any_allow_checkins = false;

	// Only display projects table if there are multiple projects.
	// first project is the db project
	if (eaSize(&serverdb->projects) <= 1)
		return;

	// Check if any projects allow checkins, so we can add the link break column.
	for(i = 0; i < eaSize(&serverdb->projects); i++)
	{
		PatchProject *project = serverdb->projects[i];
		if (!project->is_db && project->allow_checkins)
		{
			any_allow_checkins = true;
			break;
		}
	}

	// Create table.
	estrConcatf(estr, "<table class=table>\n");
	estrConcatf(estr, "<tr class=rowh><td>Project<td>Type");
	if (any_allow_checkins)
		estrConcatf(estr, "<td>Broken Links?");
	estrConcatf(estr, "\n");
	for(i = 0, row = 0; i < eaSize(&serverdb->projects); i++, row = !row)
	{
		PatchProject *project = serverdb->projects[i];
		estrConcatf(estr, "<tr class=row%d><td>%s<td>%s", row, project->name, project->is_db ? "Database" : project->allow_checkins ? "Gimme" : "Patch");
		if (any_allow_checkins && !project->is_db && project->allow_checkins)
			estrConcatf(estr, "<td><a href='/linkbreaks?project=%s'>Check...</a>", project->name);
		else
			estrConcatf(estr, "<td>");
		estrConcatf(estr, "\n");
	}
	estrConcatf(estr, "</table>\n");
}

// Web data: An entry in the broken links list for LinkBreaksData.
AUTO_STRUCT;
typedef struct LinkBreaksDataBrokenLink
{
	const char *path;									AST(UNOWNED)
	const FileVersion *ver1;							AST(UNOWNED)
	const Checkin *checkin1;							AST(UNOWNED)
	const FileVersion *ver2;							AST(UNOWNED)
	const Checkin *checkin2;							AST(UNOWNED)
	bool lost_checkins;
} LinkBreaksDataBrokenLink;

// Web data for /link_breaks
AUTO_STRUCT;
typedef struct LinkBreaksData
{
	// Generic data
	ServerConfig *config;								AST(UNOWNED)
	char *title;										AST(ESTRING)

	// Configuration
	char *project;
	int branch1;
	int branch2;
	char *prefix;
	bool exclude_automations;
	bool lost_checkins;

	// List of broken links
	EARRAY_OF(LinkBreaksDataBrokenLink) broken_links;	AST(UNOWNED)
} LinkBreaksData;

AUTO_FIXUPFUNC;
TextParserResult LinkBreaksDataFixup(LinkBreaksData *data, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&data->broken_links);
		break;
	}
	return PARSERESULT_SUCCESS;
}

// Internal state of FindLinkBreaks() passed to FindLinkBreaksInDirEntry().
struct FindLinkBreaksData {

	// Parameters
	PatchProject *project;
	int branch1;
	int branch2;
	char **exclude_authors;
	bool lost_checkins;

	// Temporary
	char **author_array;

	// Discovered broken links
	EARRAY_OF(LinkBreaksDataBrokenLink) *broken_links;
};

// Find link breaks for a particular DirEntry.
static void FindLinkBreaksInDirEntry(DirEntry *dir, void *userdata)
{
	struct FindLinkBreaksData *data = userdata;

	if (eaSize(&dir->versions) > 1)
	{

		// Check that we're included by the project.
		if (patchprojectIsPathIncluded(data->project, dir->path, NULL))
		{

			// Check that the link is broken.
			FileVersion *ver1 = patchFindVersionInDir(dir, data->branch1, NULL, data->project->serverdb->latest_rev, PATCHREVISION_NONE);
			FileVersion *ver2 = patchFindVersionInDir(dir, data->branch2, NULL, data->project->serverdb->latest_rev, PATCHREVISION_NONE);
			if (ver1 && ver1 != ver2 && ver1->checkin->branch >= data->branch1)
			{
				//FileVersion *lost_version;
				bool lost_checkins = false;

				// Check for checkins "lost" because they happened after a higher branch checkin.
				//lost_version = patchFindVersionInDir(dir, data->branch2, NULL, ver1->rev, PATCHREVISION_NONE);
				//lost_checkins = lost_version->checkin->branch > data->branch2;
				lost_checkins = ver1->rev > ver2->rev  // FIXME TODO what about intermediate branches?
				  && ver1->checksum != ver2->checksum;  // FIXME or other revs?  also check more completely

				// Check that we're not excluded by author.
				data->author_array[0] = ver1->checkin->author;
				data->author_array[1] = ver2->checkin->author;
				// data->author_array[2] = lost_version ? lost_version->checkin->author : "";
				data->author_array[2] = "xxxxxxxxxxx";  // FIXME
				if (!eaSize(&data->exclude_authors) || eaFindRegex(&data->author_array, &data->exclude_authors) == -1)
				{

					// Add to list.
					if (!data->lost_checkins || lost_checkins)
					{
						LinkBreaksDataBrokenLink *broken = StructCreate(parse_LinkBreaksDataBrokenLink);
						broken->path = dir->path;
						broken->ver1 = ver1;
						broken->checkin1 = ver1->checkin;
						broken->ver2 = ver2;
						broken->checkin2 = ver2->checkin;
						broken->lost_checkins = lost_checkins;
						eaPush(data->broken_links, broken);
					}
				}
			}
		}
	}
}

// Find links broken between two branches.
static void FindLinkBreaks(EARRAY_OF(LinkBreaksDataBrokenLink) *broken_links, PatchProject *project, int branch1, int branch2, const char *prefix,
	char **exclude_authors, bool lost_checkins)
{
	struct FindLinkBreaksData data;

	PERFINFO_AUTO_START_FUNC();

	data.project = project;
	data.branch1 = branch1;
	data.branch2 = branch2;
	data.exclude_authors = exclude_authors;
	data.lost_checkins = lost_checkins;
	data.author_array = NULL;
	eaSetSize(&data.author_array, 3);
	data.broken_links = broken_links;
	patchForEachDirEntryPrefix(project->serverdb->db, prefix, FindLinkBreaksInDirEntry, &data);
	eaDestroy(&data.author_array);

	PERFINFO_AUTO_STOP_FUNC();
}

// /linkbreaks
static void LinkBreaksHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	LinkBreaksData* data;
	const char *template = "link_breaks.cs";
	const char *project_name;
	const char *prefix;
	int branch1, branch2;
	int exclude_automations, lost_checkins;

	// Fill in the basics.
	data = StructCreate(parse_LinkBreaksData);
	data->config = &g_patchserver_config;
	estrPrintf(&data->title, "Link Breaks");

	// Get parameters.
	project_name = hrFindValue(req, "project");
	branch1 = hrFindInt(req, "branch1", -1);
	branch2 = hrFindInt(req, "branch2", -1);
	prefix = hrFindValue(req, "prefix");
	exclude_automations = hrFindInt(req, "exclude_automations", 0);
	lost_checkins = hrFindInt(req, "lost_checkins", 0);

	// Fill in the HTML parameters.
	data->project = strdup(project_name);
	if (branch1 > branch2)
		SWAP32(branch1, branch2);
	if (branch1 != -1 && branch2 != -1)
	{
		data->branch1 = branch1;
		data->branch2 = branch2;
	}
	if (prefix)
		data->prefix = strdup(prefix);
	data->exclude_automations = !!exclude_automations;
	data->lost_checkins = !!lost_checkins;

	// If everything looks OK, search for link breaks.
	if (project_name && branch1 != -1 && branch2 != -1 && branch1 != branch2)
	{
		PatchProject *project;
		project = patchserverFindProjectChecked(project_name, linkGetSAddr(req->link));
		if (project)
		{
			free(data->project);
			data->project = strdup(project->name);
			if (branch1 != -1 && branch2 != -1 && branch1 != branch2
				&& INRANGE(branch1, project->serverdb->min_branch, project->serverdb->max_branch) && INRANGE(branch2, project->serverdb->min_branch, project->serverdb->max_branch))

				FindLinkBreaks(&data->broken_links, project, branch1, branch2, prefix, exclude_automations ? g_patchserver_config.automaton_author : NULL, !!lost_checkins);
		}
	}

	// Render the page.
	render(template, LinkBreaksData);
}

static void ServerDBHandle(SA_PARAM_NN_VALID HttpRequest *req, PatchServerDb *serverdb, char *path)
{
	char *estr = NULL;
	U32 now = getCurrentFileTime();
	const char *html_footer =
		"</div></body>\n"
		"</html>\n";

	estrStackCreate(&estr);
	patchHttpAddHeader(&estr, ": %s", serverdb->name);
	estrConcatf(&estr, "<b><a href='/'>PatchServer</a> - %s<br></b>\n", serverdb->name);
	estrConcatf(&estr, "<br>\n");
	concatProjectTable(&estr, serverdb);
	concatChildTable(req, &estr, serverdb, &serverdb->db->root, true);
	concatViewTable(&estr, serverdb, 20, 0, false, now);
	concatCheckinTable(req, &estr, serverdb, 20, 0, false, 0, 0, NULL, NULL, NULL, NULL);
	estrAppend2(&estr, html_footer);
	httpSendStr(req->link,estr);
	estrDestroy(&estr);
}

static void ViewHandle(SA_PARAM_NN_VALID HttpRequest *req, PatchServerDb *serverdb, char *path)
{
	char *estr = NULL;
	U32 now = getCurrentFileTime();
	const char *html_footer =
		"</div></body>\n"
		"</html>\n";

	if(!path[0])
	{
		int linecount = hrFindInt(req, "c", 100);
		int page = hrFindInt(req, "p", 1) - 1;

		estrStackCreate(&estr);
		patchHttpAddHeader(&estr, ": %s: Views", serverdb->name);
		estrConcatf(&estr, "<b><a href='/'>PatchServer</a> - %s - Views</b><br>\n", linkFromDb(serverdb));
		estrConcatf(&estr, "<br>\n");
		concatViewTable(&estr, serverdb, linecount, page, true, now);
		estrAppend2(&estr, html_footer);
		httpSendStr(req->link, estr);
		estrDestroy(&estr);
	}
	else
	{
		bool expired;
		NamedView *named;

		char *view_name = path, *slash;
		slash = strchr(view_name, '/');
		if(slash)
			*slash = '\0';

		named = patchFindNamedView(serverdb->db, view_name);
		if(!named)
		{
			httpSendFileNotFoundError(req->link, "Invalid view name");
			return;
		}

		expired = named->expires && named->expires < now;

		// TODO: put this control back in, allow the user to specify a date, implement accesslevels for web users
		// 			msg[0] = '\0';
		// 			if(count > 1 && !g_patchserver_config.parent.server && !expired) // updating expiration
		// 			{
		// 				int days = INT_MAX;
		// 				bool from_viewtime = 0;
		// 				for(i = 1; i < count; i++)
		// 				{
		// 					if(!strcmp(args[i],"days") && values[i] && strlen(values[i]))
		// 						days = atoi(values[i]);
		// 					else if(!strcmp(args[i],"from") && values[i] && atoi(values[i]))
		// 						from_viewtime = true;
		// 				}
		// 				if(days != INT_MAX)
		// 				{
		// 					patchserverdbSetExpiration(serverdb, named->name, days < 0 ? U32_MAX : days, from_viewtime, SAFESTR(msg));
		// 					if(!msg[0])
		// 						sprintf(msg, "Expiration for view %s successfully updated", named->name);
		// 					expired = named->expires && named->expires < now;
		// 				}
		// 			}

		estrStackCreate(&estr);
		patchHttpAddHeader(&estr, ": %s: %s", serverdb->name, named->name);
		// 			if(msg[0])
		// 				estrConcatf(&estr, "<div style='background:#fff;border:solid black 1px;padding:3px'>%s</div><br><br>\n", msg);
		estrConcatf(&estr, "<b><a href='/'>PatchServer</a> - %s - %s<br></b>\n", linkFromDb(serverdb), named->name);
		estrConcatf(&estr, "Branch <b>%d</b><br>\n", named->branch);
		if(SAFE_DEREF(named->sandbox))
		{
			const Checkin *checkin = patchGetSandboxCheckin(serverdb->db, named->sandbox);
			if(checkin)
				estrConcatf(&estr, "Sandbox <b>%s</b>%s<br>\n", named->sandbox, checkin->incr_from == PATCHREVISION_NONE ? "" : " (incremental)");
			else
				estrConcatf(&estr, "Sandbox <span style=\"color:red; font-weight: bold;\">Unable to find a checkin for sandbox %s</span><br />", named->sandbox);
		}
		estrConcatf(&estr, "Revision: <b>%s</b><br>\n", linkFromCheckin(serverdb, serverdb->db->checkins[named->rev], false, ""));
		estrConcatf(&estr, "Expires <b>%s</b><br>\n", expired ? "<font color=red>Expired</font>" : named->expires ? formatTime(named->expires, true) : "Never");

		if(SAFE_DEREF(named->comment))
		{
			estrConcatf(&estr, "Comment: <br><pre>\n%s\n</pre><br>\n", named->comment);
		}

		estrConcatf(&estr, "<div><a href=\"/httpconfig?viewprojects=%s&viewname=%s#add_namedview_rule\">Add HTTP Patching Rule...</a><br /></div>\n",
			serverdb->name, named->name);

		// 			if(!g_patchserver_config.parent.server && !expired)
		// 				estrConcatf(&estr, "<form>Set expiration <input name=days size=3> days from <select name=from><option value=0>Now<option value=1>View Time</select> <input type=submit value=Set></form><br>\n");
		estrAppend2(&estr, html_footer);
		httpSendStr(req->link, estr);
		estrDestroy(&estr);
	}
}

static void CheckinHandle(SA_PARAM_NN_VALID HttpRequest *req, PatchServerDb *serverdb, char *path)
{
	char *estr = NULL;
	const char *html_footer =
		"</div></body>\n"
		"</html>\n";


	if(!path[0])
	{
		bool showDBSizeFromHere = hrFindBool(req, "dbSizeFromHere");
		bool showDBSizeInHoggsFromHere = hrFindBool(req, "dbSizeInHoggsFromHere");
		int linecount = hrFindInt(req, "c", 100);
		int page = hrFindInt(req, "p", 1)-1;
		const PatchProject** projects = NULL;
		S32* branches = NULL;
		char **args;
		char **include_authors = NULL;
		char **exclude_authors = NULL;

		args = hrFindList(req, "project");
		FOR_EACH_IN_EARRAY(args, char, arg)
		{
			EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.projects, k, ksize);
				const PatchProject* p = g_patchserver_config.projects[k];
				if(stricmp(p->name, arg)==0)
					eaPushUnique(&projects, p);
			EARRAY_FOREACH_END;
		}
		FOR_EACH_END
		eaDestroy(&args);

		args = hrFindList(req, "branch");
		FOR_EACH_IN_EARRAY(args, char, arg)
		{
			eaiPushUnique(&branches, atoi(arg));
		}
		FOR_EACH_END
		eaDestroy(&args);

		// Get authors to include.
		args = hrFindList(req, "includeauthor");
		FOR_EACH_IN_EARRAY(args, char, arg)
		{
			char *author = arg;
			if (eaFindString(&include_authors, author) == -1)
				eaPush(&include_authors, estrDup(strdup(author)));
		}
		FOR_EACH_END
		eaDestroy(&args);

		// Get authors to exclude.
		args = hrFindList(req, "excludeauthor");
		FOR_EACH_IN_EARRAY(args, char, arg)
		{
			char *author = arg;
			if (eaFindString(&exclude_authors, author) == -1)
				eaPush(&exclude_authors, estrDup(author));
		}
		FOR_EACH_END
		eaDestroy(&args);

		//for(i = 1; i < count; i++)
		//{
		//	if(!strcmp(args[i], "c") && values[i]){
		//		linecount = atoi(values[i]);
		//	}
		//	else if(!strcmp(args[i], "p") && values[i]){
		//		page = atoi(values[i])-1;
		//	}
		//	else if(!stricmp(args[i], "dbSizeFromHere")){
		//		showDBSizeFromHere = 1;
		//	}
		//	else if(!stricmp(args[i], "dbSizeInHoggsFromHere")){
		//		showDBSizeInHoggsFromHere = 1;
		//	}
		//	else if(!stricmp(args[i], "project") && values[i]){
		//		EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.projects, k, ksize);
		//		const PatchProject* p = g_patchserver_config.projects[k];
		//		if(!stricmp(p->name, values[i])){
		//			eaPushUnique(&projects, p);
		//		}
		//		EARRAY_FOREACH_END;
		//	}
		//	else if(!stricmp(args[i], "branch") && values[i]){
		//		eaiPushUnique(&branches, atoi(values[i]));
		//	}
		//}

		estrStackCreate(&estr);
		patchHttpAddHeader(&estr, ": %s: Checkins", serverdb->name);
		estrConcatf(&estr, "<b><a href='/'>PatchServer</a> - %s - Checkins (%d)<br></b>\n", linkFromDb(serverdb), eaSize(&serverdb->db->checkins));
		estrConcatf(&estr, "<br>Append &amp;dbSizeFromHere or &amp;dbSizeInHoggsFromHere to see DB size if all previous revisions were pruned.\n");
		estrConcatf(&estr, "<br>Append &amp;project=FightClubClient&amp;project=FightClubXboxClient, for example, to filter db size.\n");
		estrConcatf(&estr, "<br>\n");
		estrConcatf(&estr, "<br>\n");
		concatCheckinTable(	req,
			&estr,
			serverdb,
			linecount,
			page,
			true,
			showDBSizeFromHere,
			showDBSizeInHoggsFromHere,
			projects,
			branches,
			include_authors,
			exclude_authors);
		estrAppend2(&estr, html_footer);
		httpSendStr(req->link, estr);
		estrDestroy(&estr);
		eaDestroy(&projects);
		eaiDestroy(&branches);
		eaDestroyEString(&include_authors);
		eaDestroyEString(&exclude_authors);
	}
	else
	{
		int i;
		int linecount = hrFindInt(req, "c", 100);
		int page = hrFindInt(req, "p", 1)-1;
		U64 totalBytes = 0;
		S32 rev = atoi(path);
		Checkin *checkin = eaGet(&serverdb->db->checkins, rev);
		if(!checkin)
		{
			httpSendFileNotFoundError(req->link, "Invalid checkin");
			return;
		}

		estrStackCreate(&estr);
		patchHttpAddHeader(&estr, ": %s: Checkin %d", serverdb->name, checkin->rev);
		estrConcatf(&estr, "<b><a href='/'>PatchServer</a> - %s - Checkin %d<br></b>\n", linkFromDb(serverdb), checkin->rev);
		if(rev > 0){
			S32 revPrev = rev - 1;
			while(	revPrev &&
				serverdb->db->checkins[revPrev]->branch != checkin->branch)
			{
				revPrev--;
			}
			if(revPrev < 0){
				revPrev = rev - 1;
			}
			estrConcatf(&estr,
				"<a href=\"/%s/CompareRevs?r=%d&r=%d\">Compare to previous revision (%d, branch %d)</a><br>",
				serverdb->name,
				revPrev,
				rev,
				revPrev,
				serverdb->db->checkins[revPrev]->branch);
		}
		estrConcatf(&estr, "Branch <b>%d</b><br>\n", checkin->branch);
		if(checkin->sandbox && checkin->sandbox[0])
		{
			char tag[64] = {0};
			if(checkin->incr_from != PATCHREVISION_NONE)
				sprintf(tag, " (incremental from <a href=\"/%s/checkin/%d\">%d</a>)", serverdb->name, checkin->incr_from, checkin->incr_from);
			estrConcatf(&estr, "Sandbox <b>%s</b>%s<br>\n", checkin->sandbox, tag);
		}
		estrConcatf(&estr, "Time <b>%s</b> (%d)<br>\n", formatTime(checkin->time, true), checkin->time);
		if(checkin->author)
			estrConcatf(&estr, "Author <b>%s</b><br>\n", checkin->author);
		estrConcatf(&estr, "Files <b>%d</b><br>\n", eaSize(&checkin->versions));
		for(i = eaSize(&checkin->versions)-1; i >= 0; --i)
			totalBytes += checkin->versions[i]->size;
		estrConcatf(&estr, "Size <b>%s</b><br>\n", getCommaSeparatedInt(totalBytes));
		if(checkin->comment)
			estrConcatf(&estr, "Comment <i>%s</i><br>\n", checkin->comment); // TODO: formatting
		estrConcatf(&estr, "<br>\n");
		concatCheckinFilesTable(&estr, serverdb, checkin, linecount, page);
		estrAppend2(&estr, html_footer);
		httpSendStr(req->link, estr);
		estrDestroy(&estr);
	}
}

static void FileHandle(SA_PARAM_NN_VALID HttpRequest *req, PatchServerDb *serverdb, char *path)
{
	DirEntry *dir;
	char *c;
	S32 showPruneLink = 0;
	const char *arg;
	int argi, i, row;
	bool write_access = !g_patchserver_config.parent.server;
	char *estr = NULL, *slash;
	const char *html_footer =
		"</div></body>\n"
		"</html>\n";

	if(!path[0])
	{
		path = "/";
		dir = &serverdb->db->root;
	}
	else
	{
		size_t len = strlen(path);
		// XXX: This should redirect to the cannonical URL. <NPK 2009-04-27>
		if(len && path[len-1] == '/')
			path[len-1] = '\0';
		dir = patchFindPath(serverdb->db, path, false);
	}
	if(!dir)
	{
		httpSendFileNotFoundError(req->link, "Invalid directory entry");
		return;
	}

	// downloading file
	argi = hrFindInt(req, "get", -1);
	if(argi != -1)
	{
		for(i = 0; i < eaSize(&dir->versions); i++)
		{
			if(dir->versions[i]->rev == argi)
			{
				s_patchHttpSendFile(req->link, dir->versions[i], serverdb);
				return;
			}
		}

		httpSendFileNotFoundError(req->link, "Invalid file version");
		return;
	}

	// removing lock
	arg = hrFindValue(req, "unlock");
	if(arg)
	{
		DirEntry **dirs = NULL;
		int branch = atoi(arg);
		const char *sandbox = arg;
		char ipBuffer[50];

		if(!write_access)
		{
			httpSendServerError(req->link, "Cannot unlock files on a mirror server");
			return;
		}

		while(isdigit(*sandbox))
			++sandbox;

		if(!patchFindCheckoutInDir(dir, branch, sandbox))
		{
			httpSendFileNotFoundError(req->link, "Invalid checkout");
			return;
		}

		filelog_printf("patchserver_forceunlock.txt",
			"File force unlocked from web interface (client=%s), branch %d%s: %s",
			linkGetIpStr(req->link, SAFESTR(ipBuffer)),
			branch,
			sandbox,
			dir->path);
		
		SERVLOG_PAIRS(LOG_PATCHSERVER_FORCEUNLOCK, "ForceUnlockWebInterface",
			("client", "%s", linkGetIpStr(req->link, SAFESTR(ipBuffer)))
			("branch", "%d", branch)
			("sandbox", "%s", sandbox)
			("path", "%s", dir->path)
		);

		eaPush(&dirs, dir);
		patchserverdbRemoveCheckouts(serverdb, dirs, branch, sandbox);
		eaDestroy(&dirs);
	}

	argi = hrFindInt(req, "prune", -1);
	if(argi != -1)
	{
		S32	checkinTime = hrFindInt(req, "time", -1);
		if(checkinTime != -1)
		{
			S32	rev = argi;

			EARRAY_CONST_FOREACH_BEGIN(dir->versions, k, ksize);
				FileVersion* ver = dir->versions[k];

				if(	ver->rev == rev &&
					ver->checkin->time == checkinTime)
				{
					char pruneReason[200];
					char ipBuffer[50];

					sprintf(pruneReason,
						"Pruned from web interface (client=%s)",
						linkGetIpStr(req->link, SAFESTR(ipBuffer)));

					patchpruningPruneFileVersion(serverdb, ver, NULL, pruneReason, 1);

					// The DirEntry might be gone now, so re-find it.

					dir = patchFindPath(serverdb->db, path, false);

					if(!dir)
					{
						httpSendFileNotFoundError(req->link, "You pruned the last version of this file.");
						return;
					}
					break;
				}
			EARRAY_FOREACH_END;
		}
		else
		{
			showPruneLink = 1;
		}
	}

	estrStackCreate(&estr);
	patchHttpAddHeader(&estr, ": %s: %s", serverdb->name, path);
	estrConcatf(&estr, "<b><a href='/'>PatchServer</a> - %s - ", linkFromDb(serverdb));
	for(c = path; ; c = slash+1)
	{
		char *esc = NULL;
		slash = strchr(c, '/');
		if(!slash || slash == c)
		{
			estrConcatf(&estr, "%s", c);
			break;
		}

		*slash = '\0';
		urlEscape(path, &esc, true, false);
		estrReplaceOccurrences(&esc, "%2F", "/");
		estrConcatf(&estr, "<a href='/%s/file/%s/'>%s</a>/", serverdb->name, esc, c);
		estrDestroy(&esc);
		*slash = '/';
	}
	estrConcatf(&estr, "</b><br>\n");
	if(eaSize(&dir->versions))
	{
		bool shown = false;
		estrConcatf(&estr, "Included in Patch Projects: ");
		for(i = 1; i < eaSize(&serverdb->projects); i++)
		{
			int dummy;
			if( !serverdb->projects[i]->allow_checkins &&
				filespecMapGetInt(serverdb->projects[i]->include_filemap, path, &dummy))
			{
				estrConcatf(&estr, "%s<b>%s</b>", shown ? ", " : "", serverdb->projects[i]->name);
				shown = true;
			}
		}
		if(!shown)
			estrConcatf(&estr, "<i>none</i>");
		estrConcatf(&estr, "<br>\n");
	}
	if(dir->flags & DIRENTRY_FROZEN){
		estrConcatf(&estr, "This file is frozen (i.e. is the same in all branches).<br>\n");
	}
	estrConcatf(&estr, "<br>\n");
	if(eaSize(&dir->checkouts))
	{
		estrConcatf(&estr, "<table class=table>\n");
		estrConcatf(&estr, "<tr class=rowh><td>Checked Out<td>Branch<td>Time%s\n", write_access ? "<td>&nbsp;" : "");
		for(i = 0, row = 0; i < eaSize(&dir->checkouts); i++, row = !row)
		{
			const Checkout *checkout = dir->checkouts[i];
			estrConcatf(&estr, "<tr class=row%d><td>%s<td>%s<td>%s", row, checkout->author,
				formatBranch(checkout->branch, checkout->sandbox), formatTime(checkout->time, true));
			if(write_access)
				estrConcatf(&estr, "<td><a class='del' href='?unlock=%d%s'>Force Unlock</a>",
				checkout->branch, checkout->sandbox ? checkout->sandbox : "");
			estrConcatf	(&estr, "\n");
		}
		estrConcatf(&estr, "</table>\n");
	}
	if(!write_access && showPruneLink)
	{
		estrConcatf(&estr, "<p><b>You are pruning files from a non-master server, files will be re-patched</b></p>");
	}
	if(eaSize(&dir->versions))
	{
		estrConcatf(&estr, "<table class=table>\n");
		estrConcatf(&estr,
			"<tr class=\"rowh\">"
			"<td>Checkin</td>"
			"<td>Branch</td>"
			"<td>Size</td>"
			"<td>Modified</td>"
			"<td>Get</td>"
			"<td>Expires</td>"
			"%s"
			"<td>Comment</td>"
			"\n",
			showPruneLink ? "<td>Prune</td>" : "");
		for(i = eaSize(&dir->versions)-1, row = 0; i >= 0; --i, row = !row)
		{
			const FileVersion *ver = dir->versions[i];
			const FileVersion *prev = patchPreviousVersion(ver);
			const char *style = ver->flags&FILEVERSION_DELETED ? " class=\"del\"" :
				!prev || prev->flags&FILEVERSION_DELETED ? " class=\"new\"" : "";
			bool expired = ver->expires && ver->expires < getCurrentFileTime();
			const char *expire_timestr = formatTime(ver->expires, false);

			char *expire_str = NULL;
			if(expired)
				estrPrintf(&expire_str, "<font color=\"red\">Expired %s</font>", expire_timestr);
			else if(ver->expires)
				estrPrintf(&expire_str, "%s", expire_timestr);
			else
				estrPrintf(&expire_str, "Never");
			estrConcatf(&estr,
				"<tr class=\"row%d\"><td>%s</td><td>%s</td>",
				row,
				linkFromCheckin(serverdb, ver->checkin, true, style),
				formatBranch(ver->checkin->branch, ver->checkin->sandbox));

			if(ver->flags & FILEVERSION_DELETED)
				estrConcatf(&estr, "<td colspan=\"2\"><i>Deleted</i></td>\n");
			else
				estrConcatf(&estr, "<td>%s</td><td>%s</td>", getCommaSeparatedInt(ver->size), formatTime(ver->modified, true));
			estrConcatf(&estr,
				"<td>%s</td><td>%s</td>%s%s%s<td class=comment>%s</td>\n",
				linkFromVer(serverdb, ver),
				expire_str,
				showPruneLink ? "<td>&nbsp;" : "",
				showPruneLink ? pruneLinkFromVer(serverdb, ver) : "",
				showPruneLink ? "</td>" : "",
				fixEmptyString(ver->checkin->comment));
			estrDestroy(&expire_str);
		}
		estrConcatf(&estr, "</table>\n");
	}
	concatChildTable(req, &estr, serverdb, dir, false);
	estrAppend2(&estr, html_footer);
	httpSendStr(req->link, estr);
	estrDestroy(&estr);
}

static const char* getMirrorConfigString(void)
{
	static char* estr;
	
	estrSetSize(&estr, 0);
	
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.mirrorConfig, i, isize);
		const MirrorConfig* mc = g_patchserver_config.mirrorConfig[i];
		
		estrConcatf(&estr, "Mirroring: %s (", mc->db);
		
		EARRAY_CONST_FOREACH_BEGIN(mc->branchToMirror, j, jsize);
			const BranchToMirror* b = mc->branchToMirror[j];
			if (b->startAtRevision == 0)
			{
				// Print nothing.
			}
			else if (b->startAtRevision == INT_MAX)
			{
				estrConcatf(&estr,
					"%sBranch %d excluded",
					j ? ", " : "",
					b->branch);
			}
			else
				estrConcatf(&estr,
					"%sBranch %d from rev %d",
					j ? ", " : "",
					b->branch,
					b->startAtRevision);
		EARRAY_FOREACH_END;
		
		estrConcatf(&estr, ")<br>");
	EARRAY_FOREACH_END;
	
	return estr;
}

AUTO_STRUCT;
typedef struct IndexData {
	bool hdfdump; // Don't render the template, show raw HDF instead
	ServerConfig *config; AST(UNOWNED)
	char *machine_name; AST(UNOWNED)
	char *public_ip; 
	char *local_ip;
	char status[500];
	const char *update_status; AST(UNOWNED)
	PatchClientLink **child_links;
} IndexData;

static void IndexHandle(SA_PARAM_NN_VALID HttpRequest *req)
{
	const char *arg;
	IndexData *data;

	// Special handler for /?get=<autoupdate token>
	arg = hrFindValue(req, "get");
	if(arg)
	{
		FileVersion *update_file = patchserverGetAutoUpdateFile(arg, linkGetSAddr(req->link));
		if(update_file)
			s_patchHttpSendFile(req->link, update_file, g_patchserver_config.autoupdatedb);
		else
			httpSendFileNotFoundError(req->link, "Invalid autoupdate file");
		return;
	}

	// Fill in data for template
	data = StructCreate(parse_IndexData);
	data->config = &g_patchserver_config;
	data->machine_name = getComputerName();
	data->local_ip = StructAllocString(makeIpStr(getHostLocalIp()));
	data->public_ip = StructAllocString(makeIpStr(getHostPublicIp()));\
	patchserverGetTitleBarText(SAFESTR(data->status));
	if(g_patchserver_config.parent.server)
		data->update_status = patchupdateUpdateStatus();
	//patchserverChildStatusEx(&data->child_links);
	render("index.cs", IndexData);
}

static void IndexHandleLegacy(SA_PARAM_NN_VALID HttpRequest *req)
{
	static char *estr = NULL;
	int i, j, row;
	PatchServerDb *serverdb;
	const char *html_footer =
		"</div></body>\n"
		"</html>\n";

	PERFINFO_AUTO_START_FUNC();

	estrClear(&estr);
	patchHttpAddHeader(&estr, NULL);
	estrConcatf(&estr,
		"<b>PatchServer[%d]: %s</b><br>\n"
		"Machine: %s (%s / %s)<br>\n"
		"Version: %s<br>\n",
		gServerLibState.containerID,
		g_patchserver_config.displayName ? g_patchserver_config.displayName : getComputerName(),
		getComputerName(),
		makeIpStr(getHostLocalIp()),
		makeIpStr(getHostPublicIp()),
		patchserverVersion());

	{
		char title[500];
		patchserverGetTitleBarText(SAFESTR(title));

		estrConcatf(&estr, "Stats: %s<br>\n", title);
	}

	if(g_patchserver_config.parent.server)
	{
		estrConcatf(&estr, "Mirror Parent: <b><a href='http://%s/'>%s</a>:%d</b><br>\n", g_patchserver_config.parent.server, g_patchserver_config.parent.server, g_patchserver_config.parent.port ? g_patchserver_config.parent.port : DEFAULT_PATCHSERVER_PORT);
		estrConcatf(&estr, "%s", getMirrorConfigString());
		estrConcatf(&estr, "Mirroring Status: %s<br>\n", patchupdateUpdateStatus());
	}

	if (g_patchserver_config.prune_config)
	{
		const char *status;
		if (patchpruningAsyncIsRunning())
			status = patchpruningStatus();
		else if (patchcompactionCompactHogsAsyncIsRunning())
			status = patchcompactionStatus();
		else
			status = "Waiting for next prune/compact cycle";
		estrConcatf(&estr, "Pruning Status: %s<br>\n", status);
	}

	estrConcatf(&estr, "%s", patchserverChildStatus());

	// Links to other pages.
	estrConcatf(&estr, "<br />\n");
	if(g_patchserver_config.redirects)
		estrConcatf(&estr, "[<a href=\"/redirects\">Redirects</a>] ");
	if (!g_patchserver_config.parent.server)
	{
		estrConcatf(&estr, "[<a href=\"/httpconfig\">HTTP Patching</a>] ");
		estrConcatf(&estr, "[<a href=\"/autoupconf\">Autoupdate Config</a>] ");
	}
	estrConcatf(&estr, "[<a href=\"/config\">Miscellaneous Config</a>] ");
	estrConcatf(&estr, "[<a href=\"/connections\">Connections</a>]<br />\n");
	estrConcatf(&estr, "<br>\n");

	estrConcatf(&estr, "<table class=table>\n");
	estrConcatf(&estr, "<tr class=rowh><td>Database<td>Recent Checkins<td>Recent Views\n");
	for(i = 0, row = 0; i < eaSize(&g_patchserver_config.serverdbs); i++, row = !row)
	{
		PatchProject *project;
		serverdb = g_patchserver_config.serverdbs[i];
		project = patchserverFindProject(serverdb->name);
		if(project && checkAllowDeny(project->allow_ips, project->deny_ips, linkGetSAddr(req->link)))
		{
			if(project->serverdb) // the db has been loaded
			{
				estrConcatf(&estr, "<tr class=row%d><td style='vertical-align:top'>%s<td>", row, linkFromDb(serverdb));
				if(!eaSize(&serverdb->db->checkins))
					estrConcatf(&estr, "&nbsp;");
				else
				{
					for(j = eaSize(&serverdb->db->checkins)-1; j >= 0 && j >= eaSize(&serverdb->db->checkins)-5; --j)
					{
						estrConcatf(&estr,
							"%s (%d files)<br>",
							linkFromCheckin(serverdb, serverdb->db->checkins[j], true, ""),
							eaSize(&serverdb->db->checkins[j]->versions));
					}

					if(j >= 0)
					{
						if(eaSize(&serverdb->db->checkins) > 100)
							estrConcatf(&estr, "<a href='/%s/checkin/'>More...</a> ", serverdb->name);
						estrConcatf(&estr, "<a href='/%s/checkin/?c=0'>All %d...</a>", serverdb->name, eaSize(&serverdb->db->checkins));
					}
				}
				estrConcatf(&estr, "<td>");
				if(!eaSize(&serverdb->db->namedviews))
					estrConcatf(&estr, "&nbsp;");
				else
				{
					for(j = eaSize(&serverdb->db->namedviews)-1; j >= 0 && j >= eaSize(&serverdb->db->namedviews)-5; --j)
						estrConcatf(&estr, "%s<br>", linkFromView(serverdb, serverdb->db->namedviews[j]));
					if(j >= 0)
					{
						if(eaSize(&serverdb->db->namedviews) > 100)
							estrConcatf(&estr, "<a href='/%s/view/'>More...</a> ", serverdb->name);
						estrConcatf(&estr, "<a href='/%s/view/?c=0'>All %d...</a>", serverdb->name, eaSize(&serverdb->db->namedviews));
					}
				}
			}
			else
				estrConcatf(&estr, "<tr class=row%d><td colspan=3>%s", row, serverdb->name);
		}
	}
	estrConcatf(&estr, "</table>\n");
	estrConcatf(&estr, "<table class=\"table\">\n");
	if(g_patchserver_config.autoupdatedb)
	{
		estrConcatf(&estr, "<tr class=rowh><td>AutoUpdate</td><td>Size</td><td>Modifed</td>\n");
		for(i = 0, row = 0; i < eaSize(&g_patchserver_config.autoupdates); i++, row = !row)
		{
			FileVersion *update_file = patchserverGetAutoUpdateFile(g_patchserver_config.autoupdates[i]->token, linkGetSAddr(req->link));
			if(update_file)
			{
				estrConcatf(&estr, "<tr class=\"row%d\"><td><a href=\"/?get=%s\">%s</a></td><td>%s</td><td>%s</td>\n", row,
					update_file->parent->name,
					update_file->parent->name,
					getCommaSeparatedInt(update_file->size),
					formatTime(update_file->modified, true));
			}
		}
		estrConcatf(&estr, "</table>\n");
	}
	estrConcatf(&estr, "<table class=table><tr><td class=rowf><form><input name=t> <input type=submit value='Check Timestamp'><br>");
	i = hrFindInt(req, "t", -1);
	if(i != -1)
		estrConcatf(&estr, "%d is %s<br>", i, formatTime(i, true));
	estrConcatf(&estr, "</form></table>\n");
	estrConcatf(&estr, "%s", html_footer);
	httpSendStr(req->link, estr);

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_STRUCT;
typedef struct PatchUserCheckout
{
	char *path;
	Checkout *co; AST(UNOWNED)
} PatchUserCheckout;

AUTO_STRUCT;
typedef struct PatchUserData {
	ServerConfig *config; AST(UNOWNED)
	char *title; AST(ESTRING)
	PatchServerDb *serverdb; AST(UNOWNED)
	char *user;
	PatchUserCheckout **checkouts;
} PatchUserData;

static int cmpPatchUserCheckout(const PatchUserCheckout **left, const PatchUserCheckout **right)
{
	int ret = stricmp((*left)->path, (*right)->path);
	if(!ret)
		ret = (*left)->co->branch - (*right)->co->branch;
	return ret;
}

static void UserCheckoutCB(DirEntry *dir, PatchUserData *data)
{
	FOR_EACH_IN_EARRAY(dir->checkouts, Checkout, checkout)
		if(stricmp(checkout->author, data->user)==0)
		{
			PatchUserCheckout *co = StructAlloc(parse_PatchUserCheckout);
			co->path = StructAllocString(dir->path);
			co->co = checkout;
			eaPush(&data->checkouts, co);
			return;
		}
	FOR_EACH_END
}

static void UserHandle(SA_PARAM_NN_VALID HttpRequest *req, PatchServerDb *serverdb, const char *path)
{
	PatchUserData* data;

	data = StructCreate(parse_PatchUserData);
	data->config = &g_patchserver_config;
	estrPrintf(&data->title, "User %s", path);
	data->serverdb = serverdb;
	data->user = strdup(path);
	patchForEachDirEntry(serverdb->db, UserCheckoutCB, data);
	eaQSort(data->checkouts, cmpPatchUserCheckout);
	render("user.cs", PatchUserData);
}


static void patchHttpHandle(HttpRequest *req, void *userdata)
{
	PatchServerDb *serverdb = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!req || !req->path)
	{
		httpSendClientError(req->link, "The server did not understand your request.");
		return;
	}

	if (!checkAllowDeny(g_patchserver_config.http_allow, g_patchserver_config.http_deny, linkGetIp(req->link)))
	{
		char ip[MAX_IP_STR];
		char *error = NULL;
		estrStackCreate(&error);
		estrPrintf(&error, "Permission denied, IP %s\r\n", linkGetIpStr(req->link, SAFESTR(ip)));
		httpSendPermissionDeniedError(req->link, error);
		estrDestroy(&error);
	}

	// Global URL handlers. These aren't specific to any serverdb.
	if(stricmp(req->path, "/")==0)
		IndexHandleLegacy(req);
	else if(stricmp(req->path, PATCHHTTPDATABASE_ROOT)==0)
		patchHttpDbHandleRequestIndex(req);
	else if(strStartsWith(req->path, PATCHHTTPDATABASE_ROOT "/"))
	{
		if (req->path[sizeof(PATCHHTTPDATABASE_ROOT) - 1])
			patchHttpDbHandleRequest(req);
		else
			patchHttpDbHandleRequestIndex(req);
	}
	else if(stricmp(req->path, "/redirects")==0)
		RedirectsHandle(req);
	else if(stricmp(req->path, "/config")==0)
		ConfigHandle(req);
	else if(stricmp(req->path, "/monitor")==0)
		MonitorHandle(req);
	else if(stricmp(req->path, "/httpconfig")==0)
		HttpConfigHandle(req);
	else if(stricmp(req->path, "/httpconfig/add_namedview_rule")==0)
		HttpConfigHandleAddNamedViewRule(req);
	else if(stricmp(req->path, "/httpconfig/add_branch_rule")==0)
		HttpConfigHandleAddBranchRule(req);
	else if(stricmp(req->path, "/httpconfig/add_namedview_rule_verified")==0)
		HttpConfigHandleAddNamedViewRuleVerified(req);
	else if(stricmp(req->path, "/httpconfig/add_branch_rule_verified")==0)
		HttpConfigHandleAddBranchRuleVerified(req);
	else if(stricmp(req->path, "/httpconfig/edit_namedview_rule")==0)
		HttpConfigHandleEditNamedViewRule(req);
	else if(stricmp(req->path, "/httpconfig/delete_namedview_rule")==0)
		HttpConfigHandleDeleteNamedViewRule(req);
	else if(stricmp(req->path, "/httpconfig/edit_branch_rule")==0)
		HttpConfigHandleEditBranchRule(req);
	else if(stricmp(req->path, "/httpconfig/delete_branch_rule")==0)
		HttpConfigHandleDeleteBranchRule(req);
	else if(stricmp(req->path, "/autoupconf")==0)
		AutoupConfigHandle(req);
	else if(stricmp(req->path, "/autoupconf/add_autoup_rule")==0)
		AutoupConfigHandleAddRule(req);
	else if(stricmp(req->path, "/autoupconf/add_autoup_rule_verified")==0)
		AutoupConfigHandleAddRuleVerified(req);
	else if(stricmp(req->path, "/autoupconf/edit_autoup_rule")==0)
		AutoupConfigHandleEditRule(req);
	else if(stricmp(req->path, "/autoupconf/delete_autoup_rule")==0)
		AutoupConfigHandleDeleteRule(req);
	else if(stricmp(req->path, "/connections")==0)
		ConnectionsHandle(req);
	else if(stricmp(req->path, "/linkbreaks")==0)
		LinkBreaksHandle(req);
	else if (strStartsWith(req->path, "/static"))
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		estrCopy2(&estr, req->path);
		estrReplaceOccurrences(&estr, "/static", getGenericServingStaticDir());
		if (fileIsAbsolutePathInternal(estr))
		{
			estrPrintf(&estr, "Could not find static file (absolute path not allowed): %s\n", req->path);
			httpSendFileNotFoundError(req->link, estr);
		}
		else
		{
			if (fileExists(estr))
				httpSendFile(req->link, estr, NULL);
			else
			{
				estrPrintf(&estr, "Could not find static file: %s\n", req->path);
				httpSendFileNotFoundError(req->link, estr);
			}
		}
		estrDestroy(&estr);
	}
	else
	{
		// A serverdb-specific page. Locate the serverdb and then process.
		PatchProject *project;
		char *path, *op;
		U32 ip = linkGetSAddr(req->link);
		char *serverdb_name = req->path+1;
		char *slash = strchr(serverdb_name, '/');
		if(slash)
		{
			*slash = '\0';
			path = slash + 1;
		}
		else
			path = "";

		project = patchserverFindProjectChecked(serverdb_name, ip);
		if(project && project->serverdb && !project->is_db) // give them a second chance
			project = patchserverFindProjectChecked(project->serverdb->name, ip);
		if(!project || !project->serverdb)
		{
			// technically, this may be a 403 error, but lets not reveal anything about what we have
			httpSendFileNotFoundError(req->link, "Invalid database");
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		serverdb = project->serverdb;

		// What sub-page is this?
		op = path;
		slash = strchr(path, '/');
		if(slash)
		{
			*slash = '\0';
			path = slash + 1;
		}
		else
			path = "";

		if(stricmp(op, "")==0)
			ServerDBHandle(req, serverdb, path);
		else if(stricmp(op, "view")==0)
			ViewHandle(req, serverdb, path);
		else if(stricmp(op, "checkin")==0)
			CheckinHandle(req, serverdb, path);
		else if(stricmp(op, "file")==0)
			FileHandle(req, serverdb, path);
		else if(stricmp(op, "CompareRevs")==0)
		{
			char **args=NULL;
			char **values=NULL;
			U32 count = 1;
			
			eaPush(&args, path);
			eaPush(&values, "");

			if(req->vars)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(req->vars->ppUrlArgList, UrlArgument, urlarg)
					eaPush(&args, urlarg->arg);
					eaPush(&values, urlarg->value);
				FOR_EACH_END
				count += eaSize(&req->vars->ppUrlArgList);
			}
			sendCompareRevs(req->link, serverdb, args, values, count);
			eaDestroy(&args);
			eaDestroy(&values);
		}
		else if(stricmp(op, "user")==0)
			UserHandle(req, serverdb, path);
		else
			httpSendFileNotFoundError(req->link, "Page does not exist");
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static int patchHttpDefaultConnectHandler(NetLink* link, HttpClientStateDefault *pClientState)
{
	g_http_connections++;
	return httpDefaultConnectHandler(link, pClientState);
}

static int patchHttpDefaultDisconnectHandler(NetLink* link, HttpClientStateDefault *pClientState)
{
	g_http_connections--;
	patchHttpDbDisconnect(link, pClientState->pLinkUserData);
	RefSystem_RemoveReferent(link, true);
	return httpDefaultDisconnectHandler(link, pClientState);
}

void patchHttpInit()
{
	NetListen *listen;
	U32 ip = INADDR_ANY;

	if(!eaSize(&g_patchserver_config.http_allow))
	{
		printf("No AllowHttp lines in config. Not initializing web interface.\n");
		return;
	}

	// Create a comm for HTTP serving.
	// FIXME: The below comment is inaccurate, and in fact, extra net send threads were not helping in
	// the slightest.  The pumping is, in fact, stalling the main thread.  This needs to be reworked.
	// Note: Currently, the pumping algorithm will cause send threads to stall.  It is necessary to use
	// a sufficient number of send threads to prevent overall performance degradation.
	g_http_comm = commCreate(0, 1);

	hrSetHandler(patchHttpHandle, NULL);
	if (g_patchserver_config.client_host && *g_patchserver_config.client_host)
	{
		ip = ipFromString(g_patchserver_config.client_host);
		if (!ip)
			FatalErrorf("Unable to resolve client listen host \"%s\"", g_patchserver_config.client_host);
	}
	listen = commListenIp(g_http_comm, LINKTYPE_HTTP_SERVER, LINK_HTTP,
		g_patchserver_config.http_port ? g_patchserver_config.http_port : 80,
		httpDefaultMsgHandler, patchHttpDefaultConnectHandler, patchHttpDefaultDisconnectHandler,
		sizeof(HttpClientStateDefault), ip);
	assertmsgf(listen, "Warning: Unable to bind to port %d. Not initializing web interface.", g_patchserver_config.http_port ? g_patchserver_config.http_port : 80);
	hrEnableXMLRPC(true);

	// Load ClearSilver templates from "templates" in the filesystem or patch.
	csSetCustomLoadPath("templates");
}

// Process HTTP events.
void patchHttpTick()
{
	commMonitor(g_http_comm);
	patchHttpDbTick();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
const char *xmlrpc_test(void)
{
	static char out[100];
	sprintf(out, "test");
	return out;
}

// Views for NamedViewListResponse
AUTO_STRUCT;
typedef struct XmlrpcNamedView
{
	char *name;		AST(UNOWNED)	// Name of view
	int branch;						// Branch that view is in
} XmlrpcNamedView;

// Response for NamedViewList()
AUTO_STRUCT;
typedef struct NamedViewListResponse
{
	char *status;						AST(UNOWNED)
	EARRAY_OF(XmlrpcNamedView) views;
} NamedViewListResponse;

// Get list of named views for a project
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
NamedViewListResponse *NamedViewList(const char *project_name)
{
	PatchProject *project;
	PatchDB *db;
	NamedViewListResponse *response;

	PERFINFO_AUTO_START_FUNC();

	// Check for proper usage.
	response = StructCreate(parse_NamedViewListResponse);
	if (!project_name || !*project_name)
	{
		response->status = "bad_usage";
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	
	// Look up database.
	project = patchserverFindProject(project_name);
	if(!project || !project->serverdb || !project->serverdb->db)
	{
		response->status = "no_such_project";
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	db = project->serverdb->db;

	// Populate named view list.
	EARRAY_CONST_FOREACH_BEGIN(db->namedviews, i, n);
	{
		NamedView *view = db->namedviews[i];
		XmlrpcNamedView *responseView = StructCreate(parse_XmlrpcNamedView);
		responseView->name = view->name;
		responseView->branch = view->branch;
		eaPush(&response->views, responseView);
	}
	EARRAY_FOREACH_END;

	// Return success.
	response->status = "success";
	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

#include "autogen/patchhttp_c_ast.c"
