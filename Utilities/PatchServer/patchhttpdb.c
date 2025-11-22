#include "autogen/patchhttpdb_c_ast.h"
#include "BlockEarray.h"
#include "FilespecMap.h"
#include "httplib.h"
#include "httputil.h"
#include "logging.h"
#include "net.h"
#include "patchcommonutils.h"
#include "patchdb.h"
#include "patchfile.h"
#include "patchhttp.h"
#include "patchhttpdb.h"
#include "patchproject.h"
#include "patchserver.h"
#include "pcl_client.h"
#include "StringUtil.h"
#include "timing.h"
#include "timing_profiler.h"
#include "url.h"

// HTTP statistics.
static U64 s_http_sent = 0;
static U64 s_http_sent_overhead = 0;
static U64 s_http_received = 0;

// Duration in seconds for Expires header.
#define PATCHHTTPDB_EXPIRY_DURATION			31556952	// Approximately one year (60*60*24*365.2425)

// A specific range in an entity.
AUTO_STRUCT;
typedef struct PatchHttpRange
{
	U64 begin;
	U64 end;
	bool beginset;
	bool endset;
} PatchHttpRange;

// An unfulfilled HTTP request
AUTO_STRUCT;
typedef struct PatchHttpRequest
{
	// Connection options
	bool persist;								// The value of the connection's 'persist' at the time
												// this request was made

	// Header
	char *headers;			AST(ESTRING)		// Result header block
	char *content_type;							// Content-Type header, stored separately because it may need to be recomposed in a multipart response

	// Entity: one of the following is set.
	PatchFile *patch;		NO_AST				// Patch file associated with this request, if any.
	char *body;				AST(ESTRING)		// Raw entity body

	// Progress
	bool header_sent;							// True if the header has been sent to the client
	int sent;									// How many bytes have been sent to the client so far

	// Range requests
	PatchHttpRange *ranges; AST(BLOCK_EARRAY)	// Range requests
} PatchHttpRequest;

// HTTP connection, possibly persistent
AUTO_STRUCT;
typedef struct PatchHttpConnection
{
	NetLink *link;							AST(UNOWNED LATEBIND)	// NetLink associated with this connection
	EARRAY_OF(PatchHttpRequest) requests;							// List of pending requests, in order.
	bool persist;							AST(DEFAULT(true))		// If true, this connection should be persisted.
} PatchHttpConnection;

// Connections being handled by patchHttpDb (unowned).
EARRAY_OF(PatchHttpConnection) connections;

// Get a PatchHttpConnection from a HttpRequest.
static PatchHttpConnection *patchHttpDbGetConnection(HttpRequest *request)
{
	void **userdata = hrGetLinkUserDataPtr(request);
	PatchHttpConnection *connection;
	if (!*userdata)
	{
		*userdata = StructCreate(parse_PatchHttpConnection);
		connection = *userdata;
		connection->link = request->link;
		eaPush(&connections, connection);
	}
	connection = *userdata;
	devassert(connection->link == request->link);
	return *userdata;
}

// Concatenate basic headers to an estring.
static void patchHttpDbConcatBasicHeaders(char **estrHeaders, bool persist)
{
	char date[256];
	estrAppend2(estrHeaders, "Server: CrypticPatchServer/" CRYPTIC_PATCHSERVER_VERSION_SHORT "\r\n");
	timeMakeRFC822StringFromSecondsSince2000(date, timeSecondsSince2000());
	estrConcatf(estrHeaders, "Date: %s\r\n", date);
	estrAppend2(estrHeaders, "Accept-Ranges: bytes\r\n");
	if (!persist)
		estrAppend2(estrHeaders, "Connection: close\r\n");
}

// Concatenate basic headers to an estring.
static void patchHttpDbConcatEntity(char **estrHeaders, const char *etag, U32 ss2000time)
{
	if (etag)
		estrConcatf(estrHeaders, "ETag: %s\r\n", etag);

	if (ss2000time)
	{
		char date[256];
		timeMakeRFC822StringFromSecondsSince2000(date, ss2000time);
		estrConcatf(estrHeaders, "Last-Modified: %s\r\n", date);
		timeMakeRFC822StringFromSecondsSince2000(date, timeSecondsSince2000() + PATCHHTTPDB_EXPIRY_DURATION);
		estrConcatf(estrHeaders, "Expires: %s\r\n", date);
	}
}

// Always pass in a fresh response.
void patchHttpDbFormatDynamicResponse(PatchHttpRequest *response, bool persist, const char *status,
								const char *body, const char *contentType)
{
	if (body)
		estrCopy2(&response->body, body);

	// Send status line.
	estrPrintf(&response->headers, "HTTP/1.1 %s\r\n", status);

	// Send headers.
	patchHttpDbConcatBasicHeaders(&response->headers, persist);
	response->content_type = strdup(contentType);
}

// Queue a response to a request.
static void patchHttpDbQueueResponse(PatchHttpConnection *connection, PatchHttpRequest *response)
{
	// Note persist state.
	response->persist = connection->persist;

	// Append to response list.
	eaPush(&connection->requests, response);
}

// Send an HTTP response.
static void patchHttpDbDynamicResponse(PatchHttpConnection *connection, const char *status,
								const char *body, const char *contentType)
{
	PatchHttpRequest *response;

	PERFINFO_AUTO_START_FUNC();

	response = StructCreate(parse_PatchHttpRequest);

	patchHttpDbFormatDynamicResponse(response, connection->persist, status, body, contentType);
	patchHttpDbQueueResponse(connection, response);

	PERFINFO_AUTO_STOP_FUNC();
}

// Based on this request, should the HTTP connection be persisted?
static bool patchHttpDbShouldPersistConnection(HttpRequest *request)
{
	const char *connection;

	// Pre-HTTP 1.1 clients.
	if (request->major_version == 1 && request->minor_version < 1 || request->major_version < 1)
	{
		connection = hrFindHeader(request, "Connection");
		if (!stricmp_safe(connection, "Keep-Alive"))
			return true;
		return false;
	}

	// HTTP 1.1 or better clients.
	connection = hrFindHeader(request, "Connection");
	if (!stricmp_safe(connection, "close"))
		return false;
	return true;
}

// Handle a request for "/database"
void patchHttpDbHandleRequestIndex(HttpRequest *request)
{
	PatchHttpConnection *connection;
	char *body = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Check if this connection should be persisted.
	connection = patchHttpDbGetConnection(request);
	if (connection->persist)
		connection->persist = patchHttpDbShouldPersistConnection(request);

	// Don't allow multiple requests if this is connection is not persisted.
	if (!connection->persist && eaSize(&connection->requests))
		return;

	// Send header.
	estrPrintf(&body,
		"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
		"<html xmlns=\"http://www.w3.org/1999/xhtml\">\r\n"
		"  <head><meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\" />\r\n"
		"  <title>%s</title></head>\r\n"
		"<body><h1>%s</h1>\r\n", g_patchserver_config.displayName, g_patchserver_config.displayName);

	// Send all projects.
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.serverdbs, i, n);
	{
		PatchServerDb *serverdb = g_patchserver_config.serverdbs[i];
		estrConcatf(&body, "<h2>%s</h2>\r\n<div>\r\n", serverdb->name);
		EARRAY_CONST_FOREACH_BEGIN(serverdb->projects, j, m);
		{
			PatchProject *project = serverdb->projects[j];
			estrConcatf(&body, "<a href='database/projects/%s'>%s</a><br></br>\r\n", project->name, project->name);
		}
		EARRAY_FOREACH_END;
		estrAppend2(&body, "</div>\r\n");
	}
	EARRAY_FOREACH_END;

	// Send footer.
	estrAppend2(&body, "</body></html>\r\n");

	// Send response.
	patchHttpDbDynamicResponse(connection, "200 OK", body, "text/html");

	PERFINFO_AUTO_STOP_FUNC();
}

// Create an etag from a checksum.
static char *patchHttpDbEtag(char *etag, size_t etag_size, U32 checksum)
{
	sprintf_s(SAFESTR2(etag), "\"%lu\"", checksum);
	return etag;
}

// Send an HTTP response, with a PatchFile.
static void patchHttpDbResponsePatch(PatchHttpConnection *connection, const char *status,
                                     PatchFile *patch, U32 checksum, U32 modified, const char *contentType, PatchHttpRange *ranges)
{
	PatchHttpRequest *response;
	char etag[25];

	PERFINFO_AUTO_START_FUNC();

	// Save PatchFile.
	response = StructCreate(parse_PatchHttpRequest);
	devassert(patch);
	response->patch = patch;

	// Send status line.
	estrPrintf(&response->headers, "HTTP/1.1 %s\r\n", status);

	// Send headers.
	patchHttpDbConcatBasicHeaders(&response->headers, connection->persist);
	if (checksum)
		patchHttpDbConcatEntity(&response->headers, patchHttpDbEtag(SAFESTR(etag), checksum), patchFileTimeToSS2000(modified));
	response->content_type = strdup(contentType);

	// Save ranges, if any.
	response->ranges = ranges;

	// Append to response list.
	patchHttpDbQueueResponse(connection, response);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send a 304 "not modified" response.
static void patchHttpDbResponseNotModified(PatchHttpConnection *connection)
{
	patchHttpDbDynamicResponse(connection, "304 Not Modified", NULL, NULL);
}


// Send a 404 "not found" response.
static void patchHttpDbResponseNotFound(PatchHttpConnection *connection)
{
	patchHttpDbDynamicResponse(connection, "404 Not Found", "File not found.", "text/plain");
}

// Send a 412 "precondition failed" response.
static void patchHttpDbResponsePreConditionFailed(PatchHttpConnection *connection, const char *reason)
{
	patchHttpDbDynamicResponse(connection, "412 Precondition Failed", reason, "text/plain");
}

// Find a specific revision.  Return NULL if not found.
static Checkin *patchHttpDbFindRev(PatchDB *db, int rev)
{
	if(db && eaSize(&db->checkins) > rev)
		return db->checkins[rev];

	return NULL;
}

// Return true if the tag matches this FileVersion.
static bool patchHttpDbEtagMatch(const char *etag, FileVersion *version, bool *error)
{
	char current_etag[25];

	*error = false;

	if (!version->checksum)
		return false;

	patchHttpDbEtag(SAFESTR(current_etag), version->checksum);
	return !stricmp(etag, current_etag);
}

static bool patchHttpDbDateLess(const char *httpdate, FileVersion *version, bool *error)
{
	U32 lhs = timeGetSecondsSince2000FromHttpDateString(httpdate);
	U32 rhs = patchFileTimeToSS2000(version->modified);
	if (!lhs)
	{
		*error = true;
		return false;
	}
	*error = false;
	return lhs < rhs;
}

static bool patchHttpDbDateGreater(const char *httpdate, FileVersion *version, bool equal, bool *error)
{
	U32 lhs = timeGetSecondsSince2000FromHttpDateString(httpdate);
	U32 rhs = patchFileTimeToSS2000(version->modified);
	if (!lhs)
	{
		*error = true;
		return false;
	}
	*error = false;
	return lhs > rhs || equal && lhs == rhs;
}

static bool patchHttpDbEtagRangeMatch(const char *value, FileVersion *version, bool *error)
{
	if (value[0] == '"' && !strchr(value,','))
		return patchHttpDbEtagMatch(value, version, error);
	return patchHttpDbDateGreater(value, version, true, error);
}

// If the header has conditions that allow us to not deliver the entity, send the appropriate response and return true.
// If this function is not able to make anything happen, it returns false, indicating that the request should be handled normally.
static bool patchHttpDbProcessConditionals(PatchHttpConnection *connection, HttpRequest *request, FileVersion *version)
{
	const char *if_match, *if_modified_since, *if_none_match, *if_unmodified_since;
	bool if_match_condition = false, if_modified_since_condition = false, if_none_match_condition = false, if_unmodified_since_condition = false;
	bool error;

	// Check for conditional headers.
	if_match = hrFindHeader(request, "If-Match");
	if_modified_since = hrFindHeader(request, "If-Modified-Since");
	if_none_match = hrFindHeader(request, "If-None-Match");
	if_unmodified_since = hrFindHeader(request, "If-Unmodified-Since");

	// If there are no conditional headers, there's nothing to do.
	if (!if_match && !if_modified_since && !if_none_match && !if_unmodified_since)
		return false;

	// Reject undefined combinations of conditional headers.
	if ((if_match && if_unmodified_since) + (if_none_match && if_modified_since) > 1)
		return false;

	// Check all conditions.
	if (if_match)
	{
		if_match_condition = patchHttpDbEtagMatch(if_match, version, &error);
		if (error)
			if_match = NULL;
	}
	if (if_modified_since)
	{
		if_modified_since_condition = patchHttpDbDateLess(if_modified_since, version, &error);
		if (error)
			if_modified_since = NULL;
	}
	if (if_none_match)
	{
		if_none_match_condition = !patchHttpDbEtagMatch(if_none_match, version, &error);
		if (error)
			if_none_match = NULL;
	}
	if (if_unmodified_since)
	{
		if_unmodified_since_condition = patchHttpDbDateGreater(if_unmodified_since, version, false, &error);
		if (error)
			if_unmodified_since = NULL;
	}

	// If a condition fails, respond with an error.
	if (if_match && !if_match_condition)
	{
		patchHttpDbResponsePreConditionFailed(connection, "If-Match failed");
		return true;
	}
	if (if_modified_since && !if_modified_since_condition)
	{
		patchHttpDbResponseNotModified(connection);
		return true;
	}
	if (if_none_match && !if_none_match_condition)
	{
		patchHttpDbResponsePreConditionFailed(connection, "If-None-Match failed");
		return true;
	}
	if (if_unmodified_since && !if_unmodified_since_condition)
	{
		patchHttpDbResponsePreConditionFailed(connection, "If-Unmodified-Since failed");
		return true;
	}

	return false;
}

// Process any range requests.
static void patchHttpDbProcessRanges(PatchHttpConnection *connection, HttpRequest *request, FileVersion *version, PatchHttpRange **ranges)
{
	const char *range_header, *if_range;
	bool if_range_condition = false;
	bool error;
	const char range_prefix[] = "bytes=";
	const char *i;
	char *next;

	// Check for Range header.
	range_header = hrFindHeader(request, "Range");
	if (!range_header)
		return;

	// Verify If-Range header.
	if_range = hrFindHeader(request, "If-Range");
	if (if_range)
	{
		if_range_condition = patchHttpDbEtagRangeMatch(if_range, version, &error);
		if (error || !if_range_condition)
			return;
	}

	// Parse Range header.
	if (!strStartsWith(range_header, range_prefix))
		return;
	errno = 0;
	for(i = range_header + sizeof(range_prefix) - 1; *i;)
	{
		U64 begin = -1, end = -1;
		bool beginset = false, endset = false;
		PatchHttpRange *range;

		// Parse the range beginning position.
		if (*i != '-')
		{
			begin = _strtoui64(i, &next, 10);
			if (errno || next == i)
			{
				beaDestroy(ranges);
				return;
			}
			beginset = true;
			i = next;
		}

		// Parse the ending position.
		if (*i && *i == '-')
		{
			++i;
			end = _strtoui64(i, &next, 10);
			if (!errno && next != i)
				endset = true;
			i = next;
		}

		// Check for continuation.
		if (*i)
		{
			if (*i == ',')
				++i;
			else
			{
				beaDestroy(ranges);
				return;
			}
		}

		// Make sure it's a syntactically-correct range.
		if (beginset && endset && end < begin)
		{
			beaDestroy(ranges);
			return;
		}

		// Add the range to the list.
		range = beaPushEmpty(ranges);
		range->begin = begin;
		range->end = end;
		range->beginset = beginset;
		range->endset = endset;
	}
	if (!beaSize(ranges))
		return;
}

// Handle a request for a file.
static void patchHttpDbHandleFileRequest(PatchHttpConnection *connection, HttpRequest *request, const char *path)
{
	const char *filename;
	const char *fileversion;
	char filepath[CRYPTIC_MAX_PATH] = {0};
	char *bare_fileversion = NULL;
	int matches;
	char *projectname = NULL;
	unsigned revision;
	char extra;
	Checkin *checkin;
	U32 ip;
	PatchProject *project;
	Checkin *sandbox_checkin = NULL;
	int incr_from = PATCHREVISION_NONE;
	FileVersion *version;
	PatchFile *patch;
	PatchHttpRange *ranges = NULL;
	const char *response_line;

	PERFINFO_AUTO_START_FUNC();

	// Parse out the project and filename.
	filename = strchr(path, '/');
	if (!filename || !filename[1])
	{
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	estrStackCreate(&projectname);
	estrConcat(&projectname, path, filename - path);
	++filename;

	// Break out fileversion.
	fileversion = strrchr(filename, '/');
	if (!fileversion || !fileversion[1])
	{
		estrDestroy(&projectname);
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	++fileversion;

	// Break out file path.
	if (fileversion - filename <= 1 || fileversion - filename - 1 >= CRYPTIC_MAX_PATH)
	{
		estrDestroy(&projectname);
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	memcpy(filepath, filename, fileversion - filename - 1);

	// Only serve compressed versions of files.
	if (!strEndsWith(fileversion, ".hz"))
	{
		estrDestroy(&projectname);
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	estrStackCreate(&bare_fileversion);
	estrConcat(&bare_fileversion, fileversion, (int)strlen(fileversion) - 3);

	// Parse fileversion.
	matches = sscanf_s(bare_fileversion, "r%u%c",
		&revision, &extra);
	estrDestroy(&bare_fileversion);
	if (matches != 1)
	{
		estrDestroy(&projectname);
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Look up project.
	ip = linkGetSAddr(request->link);
	project = patchserverFindProjectChecked(projectname, ip);
	estrDestroy(&projectname);
	if(!project || !project->serverdb || !project->serverdb->db || !project->is_db)
	{
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Look up revision checkin.
	checkin = patchHttpDbFindRev(project->serverdb->db, revision);
	if (!checkin)
	{
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Look up file version.
	version = patchFindVersion(project->serverdb->db, filepath, checkin->branch, checkin->sandbox, revision,
		checkin->incr_from);
	if (!version || !version->checkin || version->checkin->rev != revision)
	{
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If this is a conditional request, attempt to avoid sending the entity.
	if (patchHttpDbProcessConditionals(connection, request, version))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Load patch.
	patch = patchfileFromDb(version, project->serverdb);
	if (patch->load_state < LOADSTATE_COMPRESSED_ONLY)
		patchserverLoadForHttp(request->link, patch, false);

	// Process range requests, if any.
	patchHttpDbProcessRanges(connection, request, version, &ranges);

	// Queue response.
	response_line = beaSize(&ranges) ? "206 Partial Content" : "200 OK";
	patchHttpDbResponsePatch(connection, response_line, patch, version->checksum, version->modified,
		"application/octet-stream", ranges);

	PERFINFO_AUTO_STOP_FUNC();
}

// Data for patchHttpDbCheckVersion()
struct CheckVersionData
{
	// View parameters
	PatchProject *project;
	int branch;
	char *sandbox;
	int rev;
	int incr_from;

	// Output
	char *output;
};

// Add a link to the output for this file in the requested view, if present.
static void patchHttpDbCheckVersion(DirEntry *dir, void *userdata)
{
	struct CheckVersionData *data = userdata;
	int dummy;
	FileVersion *ver;
	//PatchFile *patch;
	char path[MAX_PATH];
	char *httppath = NULL;
	char *httppath_escaped = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Check if this file is included in this project.
	if(!eaSize(&dir->versions)
		||
		data->project->strip_prefix &&
		!patchprojectStripPrefix(SAFESTR(path), dir->path, NULL)
		||
		data->project->include_filemap &&
		!filespecMapGetInt(data->project->include_filemap, data->project->strip_prefix?path:dir->path, &dummy) ||
		data->project->exclude_filemap &&
		filespecMapGetInt(data->project->exclude_filemap, data->project->strip_prefix?path:dir->path, &dummy))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Find version.
	ver = patchFindVersionInDir(dir, data->branch, data->sandbox, data->rev, data->incr_from);
	if (!ver || (ver->flags & FILEVERSION_DELETED))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	/* TODO: Don't include slipstreamed files
	patch = patchfileFromDb(ver, data->project->serverdb);
	if (patch->load_state < LOADSTATE_INFO_ONLY)
	{
		patchserverLoadForHttp(link, patch, false);
	} */

	// Add the proper version of this file to the links list.
	estrStackCreate(&httppath);
	estrPrintf(&httppath,
		"%s/%s/r%u.hz",
		data->project->serverdb->name,
		ver->parent->path,
		ver->checkin->rev);
	string_tolower(httppath);
	estrStackCreate(&httppath_escaped);
	urlEscape(httppath, &httppath_escaped, false, true);
	estrConcatf(&data->output, "<a href='../../files/%s'>%s</a><br></br>\r\n", httppath_escaped, httppath);
	estrDestroy(&httppath_escaped);
	estrDestroy(&httppath);

	PERFINFO_AUTO_STOP_FUNC();
}

// Handle a request for a named view.
static void patchHttpDbHandleNamedViewRequest(PatchHttpConnection *connection, HttpRequest *request, const char *path)
{
	const char *name;
	char *projectname = NULL;
	U32 ip;
	PatchProject *project;
	NamedView *view;
	Checkin *sandbox_checkin = NULL;
	int incr_from = PATCHREVISION_NONE;
	struct CheckVersionData data;


	// Find view name.
	name = strchr(path, '/');
	if (!name || !name[1] || name-1 == path)
	{
		patchHttpDbResponseNotFound(connection);
		return;
	}
	++name;

	// Copy out project name.
	estrStackCreate(&projectname);
	estrConcat(&projectname, path, name - path - 1);

	// Look up project.
	ip = linkGetSAddr(request->link);
	project = patchserverFindProjectChecked(projectname, ip);
	estrDestroy(&projectname);
	if(!project || !project->serverdb)
	{
		patchHttpDbResponseNotFound(connection);
		return;
	}

	// Look up view.
	view = patchFindNamedView(project->serverdb->db, name);
	if (!view)
	{
		patchHttpDbResponseNotFound(connection);
		return;
	}

	// Look up sandbox.
	if (view->sandbox)
		sandbox_checkin = patchGetSandboxCheckin(project->serverdb->db, view->sandbox);
	if (sandbox_checkin)
		incr_from = sandbox_checkin->incr_from;

	// Record view parameters for scan.
	data.project = project;
	data.branch = view->branch;
	data.rev = view->rev;
	data.sandbox = view->sandbox;
	data.incr_from = incr_from;
	data.output = NULL;
	estrPrintf(&data.output,
		"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
		"<html xmlns=\"http://www.w3.org/1999/xhtml\">\r\n"
		"  <head><meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\" />\r\n"
		"  <title>%s</title></head>\r\n"
		"<body><h1>%s</h1><div>\r\n", view->name, view->name);

	// Scan all files in database to create view link list.
	patchForEachDirEntryPrefix(project->serverdb->db, NULL, patchHttpDbCheckVersion, &data);

	// Send the links list to the client.
	estrConcatf(&data.output, "</div></body></html>\r\n");
	patchHttpDbDynamicResponse(connection, "200 OK", data.output, "text/html");
	estrDestroy(&data.output);
}

// Handle a request for project views.
static void patchHttpDbHandleProjectRequest(PatchHttpConnection *connection, HttpRequest *request, const char *path)
{
	U32 ip;
	PatchProject *project;
	char *output = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Look up project.
	ip = linkGetSAddr(request->link);
	project = patchserverFindProjectChecked(path, ip);
	if(!project || !project->serverdb)
	{
		patchHttpDbResponseNotFound(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Send header.
	estrPrintf(&output,
		"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
		"<html xmlns=\"http://www.w3.org/1999/xhtml\">\r\n"
		"  <head><meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\" />\r\n"
		"  <title>%s</title></head>\r\n"
		"<body><h1>%s</h1><div>\r\n", project->name, project->name);

	// Make list of views.
	EARRAY_CONST_FOREACH_BEGIN(project->serverdb->db->namedviews, i, n);
	{
		NamedView *view = project->serverdb->db->namedviews[i];
		estrConcatf(&output, "<a href='../namedviews/%s/%s'>%s</a><br></br>\r\n", project->name, view->name, view->name);
	}
	EARRAY_FOREACH_END;

	// Send view list to the client.
	estrConcatf(&output, "</div></body></html>\r\n");
	patchHttpDbDynamicResponse(connection, "200 OK", output, "text/html");

	PERFINFO_AUTO_STOP_FUNC();
}

// Handle a request for "/database/*"
void patchHttpDbHandleRequest(HttpRequest *request)
{

#define REQUEST_FILES_NAME "files"
#define REQUEST_NAMEDVIEWS_NAME "namedviews"
#define REQUEST_PROJECTS_NAME "projects"

	const char *path = request->path + sizeof(PATCHHTTPDATABASE_ROOT);
	PatchHttpConnection *connection;

	PERFINFO_AUTO_START_FUNC();

	// Check if this connection should be persisted.
	connection = patchHttpDbGetConnection(request);
	if (connection->persist)
		connection->persist = patchHttpDbShouldPersistConnection(request);

	// Don't allow multiple requests if this is connection is not persisted.
	if (!connection->persist && eaSize(&connection->requests))
		return;

	if (strStartsWith(path, REQUEST_FILES_NAME "/") && path[sizeof(REQUEST_FILES_NAME)])
		patchHttpDbHandleFileRequest(connection, request, path + sizeof(REQUEST_FILES_NAME));
	else if (strStartsWith(path, REQUEST_NAMEDVIEWS_NAME "/") && path[sizeof(REQUEST_NAMEDVIEWS_NAME)])
		patchHttpDbHandleNamedViewRequest(connection, request, path + sizeof(REQUEST_NAMEDVIEWS_NAME));
	else if (strStartsWith(path, REQUEST_PROJECTS_NAME "/") && path[sizeof(REQUEST_PROJECTS_NAME)])
		patchHttpDbHandleProjectRequest(connection, request, path + sizeof(REQUEST_PROJECTS_NAME));
	else
	{
		patchHttpDbResponseNotFound(connection);
	}

	PERFINFO_AUTO_STOP_FUNC();
#undef REQUEST_FILES_NAME
#undef REQUEST_NAMEDVIEWS_NAME
}

// Make a boundary name that won't conflict.
static void patchHttpDbNameBoundary(char **estrBoundaryName, const char *file, size_t entity_size)
{
	char boundary[2 + 8 + 10 + 1 + 1];
	U64 value;

	PERFINFO_AUTO_START_FUNC();

	// Keep looping until we find an acceptable boundary.
	for(value = 0;;++value)
	{
		char number[10 + 1 + 1] = "";
		if (value)
			sprintf(number, "-%"FORM_LL"u", value);
		sprintf(boundary, "\r\n--boundary%s", number);
		if (!memstr(file, boundary, entity_size))
		{
			estrCopy2(estrBoundaryName, boundary + 4);
			PERFINFO_AUTO_STOP();
			return;
		}
		++value;
	}

	// Never get here
	devassert(0);
	PERFINFO_AUTO_STOP();
}

static void patchHttpDbProcessConnectionRanges(PatchHttpConnection *connection, int *size)
{
	static const char perfname_single[] = "SingleRange";
	static const char perfname_multi[] = "MultiRange";
	static PERFINFO_TYPE *perfinfo_single;
	static PERFINFO_TYPE *perfinfo_multi;
	PatchHttpRequest *request = connection->requests[0];
	const char *data = request->patch ? request->patch->compressed.data : request->body;
	int entity_size = *size;
	char *new_body = NULL;
	int i;

	if (!request->ranges)
		return;
	else if (beaSize(&request->ranges) == 1)
	{
		PERFINFO_AUTO_START_STATIC(perfname_single, &perfinfo_single, 1);
	}
	else
	{
		PERFINFO_AUTO_START_STATIC(perfname_multi, &perfinfo_multi, 1);
	}

	// Make sure range requests are satisfiable, and remove invalid ranges.
	if (request->ranges)
	{
		for (i = 0; i != beaSize(&request->ranges); ++i)
		{
			PatchHttpRange *range = request->ranges + i;
			if ((!range->beginset || range->begin >= *size) && (range->beginset || !range->endset || !range->end))
			{
				beaRemove(&request->ranges, i);
				--i;
			}
		}
		if (!beaSize(&request->ranges))
		{
			PatchHttpRequest *newResponse = StructCreate(parse_PatchHttpRequest);
			patchHttpDbFormatDynamicResponse(newResponse, request->persist, "416 Requested Range Not Satisfiable", "Requested Range Not Satisfiable", "text/plain");
			newResponse->persist = request->persist;
			*size = estrLength(&newResponse->body);
			StructDestroy(parse_PatchHttpRequest, request);
			connection->requests[0] = newResponse;
			request = newResponse;
			estrConcatf(&request->headers, "Content-Range: bytes */%d\r\n", entity_size);
		}
	}

	// Canonicalize each range.
	for (i = 0; i != beaSize(&request->ranges); ++i)
	{
		PatchHttpRange *range = request->ranges + i;
		if (!range->beginset)
		{
			if (range->endset)
			{
				range->begin = *size - range->end;
				range->end = *size - 1;
			}
			else
			{
				range->begin = 0;
				range->end = *size - 1;
			}
		}
		else if (!range->endset)
			range->end = *size - 1;
	}

	// Merge ranges.
	// Note that ranges aren't necessarily in order.  If the client decided to request ranges out-of-order, we don't attempt to reorder them, which
	// may result in some merging not happening.
	for(i = 1; i < beaSize(&request->ranges); ++i)
	{
		PatchHttpRange *next_range = request->ranges + i;
		PatchHttpRange *last_range = request->ranges + i - 1;
		if (next_range->begin <= last_range->end)
		{
			last_range->end = next_range->end;
			beaRemove(&request->ranges, i);
			--i;
		}
	}

	// Generate the response dynamically.
	// Note: It is possible to stream multi-part byteranges, but it's somewhat more complicated, because the Content-Length
	// still needs to be in the main header, and we need to know which part we're in as we stream, which is slightly more tricky because
	// the size of each part's header is variable because the Content-Range field's length depends on where we are in the file.
	// So, in interests of keeping the entity sending loop simple, we generate the entire response immediately.
	// If this is found to be a performance problem, this can be written to stream the multipart section without pregenerating it.
	if (beaSize(&request->ranges) == 1)
	{

		// Generate single range response.
		PatchHttpRange *range = request->ranges;
		int new_size = range->end - range->begin + 1;

		// Add Content-Range header.
		estrConcatf(&request->headers, "Content-Range: bytes %"FORM_LL"u-%"FORM_LL"u/%d\r\n", range->begin, range->end, entity_size);

		// Create body range.
		estrConcat(&new_body, data + range->begin, new_size);

		// Replace existing body with this one.
		request->patch = NULL;
		estrDestroy(&request->body);
		request->body = new_body;
		*size = estrLength(&request->body);
	}
	else if (beaSize(&request->ranges))
	{

		// Generate multiple range response.
		char *boundary = NULL;

		// Find a suitable boundary marker.
		estrStackCreate(&boundary);
		patchHttpDbNameBoundary(&boundary, data, entity_size);

		// Generate each part.
		for (i = 0; i != beaSize(&request->ranges); ++i)
		{
			PatchHttpRange *range = request->ranges + i;
			int new_size = range->end - range->begin + 1;
			estrConcatf(&new_body, "\r\n--%s\r\n"
				"Content-Type: %s\r\n"
				"Content-Range: bytes %"FORM_LL"u-%"FORM_LL"u/%d\r\n\r\n",
				boundary, request->content_type, range->begin, range->end, entity_size);
			estrConcat(&new_body, data + range->begin, new_size);
		}

		// Generate final boundary.
		estrConcatf(&new_body, "\r\n--%s--\r\n", boundary);

		// Replace existing body with this one.
		request->patch = NULL;
		estrDestroy(&request->body);
		request->body = new_body;
		*size = estrLength(&request->body);
		free(request->content_type);
		request->content_type = strdupf("multipart/byteranges; boundary=%s", boundary);
		estrDestroy(&boundary);
	}

	// Now that the ranges have been processed, treat this as a normal response.
	beaDestroy(&request->ranges);

	PERFINFO_AUTO_STOP();
}

// Process any pending requests on single HTTP connection.
// This function uses linkSendBufWasFull()-style flow control based on updateLinkStates() from HttpLib.c.
// It will send as much data as is loaded-for-send until the link is full.
static void patchHttpDbProcessConnection(PatchHttpConnection *connection)
{
	NetLink *link = connection->link;

	// Process any pending requests.
	while (eaSize(&connection->requests))
	{
		bool persist;
		const int block_size = 1024;
		PatchHttpRequest *request = connection->requests[0];
		int size = 0;
		int send_size;

		// If the link is full, wait.
		if(linkSendBufWasFull(link))
		{
			linkClearSendBufWasFull(link);
			return;
		}

		// Get the size of the body.
		PERFINFO_AUTO_START("CheckLoadState", 1);
		if (request->patch)
		{
			if (request->patch->load_state == LOADSTATE_ERROR)
			{
				PatchHttpRequest *newResponse = StructCreate(parse_PatchHttpRequest);
				AssertOrAlert("PATCHSERVER_HTTPDB_LOADERROR", "Load error: HTTPLoad %s load_state %d\n", patchFileGetUsedName(request->patch), (int)request->patch->load_state);
				patchHttpDbFormatDynamicResponse(newResponse, request->persist, "500 Internal Server Error", "Unable to load file", "text/plain");
				newResponse->persist = request->persist;
				size = estrLength(&newResponse->body);
				StructDestroy(parse_PatchHttpRequest, request);
				connection->requests[0] = newResponse;
				request = newResponse;
			}
			else if (request->patch->load_state == LOADSTATE_LOADING)
			{
				PERFINFO_AUTO_STOP();
				return;
			}
			// If the file isn't loaded, queue up a request with the loader instead
			else if (request->patch->load_state < LOADSTATE_COMPRESSED_ONLY)
			{
				patchserverLoadForHttp(link, request->patch, false);
				PERFINFO_AUTO_STOP();
				return;
			}
			else
			{
				size = request->patch->compressed.len;
			}
		}
		else if (request->body)
			size = estrLength(&request->body);
		PERFINFO_AUTO_STOP();

		// Process range requests, if any.
		patchHttpDbProcessConnectionRanges(connection, &size);
		request = connection->requests[0];

		// Send the header.
		// Wait if this fills up the link.
		PERFINFO_AUTO_START("SendHeader", 1);
		if (!request->header_sent)
		{
			Packet *pak = pktCreateRaw(link);
			if (request->content_type)
				estrConcatf(&request->headers, "Content-Type: %s\r\n", request->content_type);
			if (request->patch || request->body)
				estrConcatf(&request->headers, "Content-Length: %d\r\n", size);
			estrAppend2(&request->headers, "\r\n");
			pktSendBytesRaw(pak, request->headers, estrLength(&request->headers));
			pktSendRaw(&pak);
			s_http_sent_overhead += estrLength(&request->headers);
			s_http_sent += estrLength(&request->headers);
			request->header_sent = true;
			if(linkSendBufWasFull(link))
			{
				linkClearSendBufWasFull(link);
				PERFINFO_AUTO_STOP();
				return;
			}
		}
		PERFINFO_AUTO_STOP();

		// Send the body data in blocks.
		// Wait when the link is full.
		PERFINFO_AUTO_START("SendEntity", 1);
		devassert(!size || request->sent < size);
		for(send_size = MIN(size - request->sent, block_size);
			send_size;
			send_size = MIN(size - request->sent, block_size))
		{
			Packet *pak = pktCreateRaw(link);
			char *data = request->patch ? request->patch->compressed.data : request->body;
			devassert(request->patch || request->body);
			data += request->sent;
			pktSendBytesRaw(pak, data, send_size);
			pktSendRaw(&pak);
			request->sent += send_size;
			s_http_sent += send_size;
			if(request->sent < size && linkSendBufWasFull(link))
			{
				linkClearSendBufWasFull(link);
				PERFINFO_AUTO_STOP();
				return;
			}
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("FinishSending", 1);
		persist = request->persist;
		StructDestroy(parse_PatchHttpRequest, request);
		eaRemove(&connection->requests, 0);

		// If this is not a persistent connection, close it.
		if (!persist)
		{
			linkFlushAndClose(&link, "Non-persistent connection finished.");
			PERFINFO_AUTO_STOP();
			return;
		}
		PERFINFO_AUTO_STOP();
	}
}

// Process pending HTTP requests.
void patchHttpDbTick()
{
	PERFINFO_AUTO_START_FUNC();

	// Process each connection.
	EARRAY_CONST_FOREACH_BEGIN(connections, i, n);
	{
		patchHttpDbProcessConnection(connections[i]);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

// Notify patchHttpDb code that a NetLink has disconnected.
void patchHttpDbDisconnect(NetLink *link, void *pLinkUserData)
{
	PatchHttpConnection *connection = pLinkUserData;

	PERFINFO_AUTO_START_FUNC();

	eaFindAndRemove(&connections, connection);
	StructDestroy(parse_PatchHttpConnection, connection);

	PERFINFO_AUTO_STOP_FUNC();
}

// Return true if this HTTP NetLink is being processed by patchHttpDb.
bool patchHttpDbIsOurNetLink(NetLink *link)
{
	HttpClientStateDefault *client = linkGetUserData(link);
	return !!client->pLinkUserData;
}

// Total number of bytes sent
U64 patchHttpDbBytesSent()
{
	return s_http_sent;
}

// Total number of header bytes sent (total - payload)
U64 patchHttpDbBytesSentOverhead()
{
	return s_http_sent_overhead;
}

// Number of HTTP request bytes received from clients
U64 patchHttpDbBytesReceived()
{
	return s_http_received;
}

#include "autogen/patchhttpdb_c_ast.c"
