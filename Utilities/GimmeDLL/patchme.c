#include "patchme.h"

#include "net/net.h"
#include "error.h"
#include "logging.h"
#include "trivia.h"
#include "utils.h"
#include "StashTable.h"
#include "earray.h"
#include "file.h"
#include "cmdparse.h"
#include "winutil.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "sysutil.h"
#include "mathutil.h"
#include "StringCache.h"
#include "UnitSpec.h"
#include "textparser.h"
#include "ScratchStack.h"
#include "SVNUtils.h"
#include "autogen/SVNUtils_h_ast.h"
#include "RegistryReader.h"
#include "FilespecMap.h"
#include "timing_profiler_interface.h"
#include "hoglib.h"
#include "qsortG.h"

#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "patchcommonutils.h"
#include "patchmeui.h"
#include "patchmeStat.h"
#include "patchdb.h"
#include "patchtrivia.h"

#include "gimmeDLLPrivateInterface.h"
#include "gimme.h"
#include "UTF8.h"

STATIC_ASSERT(GIMME_BRANCH_UNKNOWN == PATCHBRANCH_NONE)

#ifdef WIN32
#	define AUTOUPDATE_TOKEN "GimmeWin32"
#else
#	error "Please define an AUTOUPDATE_TOKEN for this platform"
#endif
#define GIMME_SERVER			gimme_server
#define GIMME_SERVER_AND_PORT	GIMME_SERVER, gimme_port
#define MANIFEST_CACHE_TIME		1 // 60

static char gimme_server[64] = "AssetMaster";
#if 1
static int gimme_port = 7255;
#else
static int gimme_port = 9427;
#endif
// Sets the gimme server to connect to
AUTO_CMD_STRING(gimme_server, gimme_server);
// Sets the gimme port to connect to
AUTO_CMD_INT(gimme_port, gimme_port);

U32 gimme_timeout_seconds = 20;
// Sets the number of seconds before timing out
AUTO_CMD_INT(gimme_timeout_seconds, gimme_timeout);

U32 verifyAllFiles;
AUTO_CMD_INT(verifyAllFiles, verify_all_files);

U32 fileOverlay;
AUTO_CMD_INT(fileOverlay, file_overlay);

bool gimme_force_manifest = false;
AUTO_CMD_INT(gimme_force_manifest, forceManifest);

bool gimme_verbose = false;
AUTO_CMD_INT(gimme_verbose, gimme_verbose);

static bool isStandaloneGimme(void)
{
	return strEndsWith(getExecutableName(), "/gimme.exe");
}

AUTO_RUN;
void patchmeInitTimeout(void)
{
	gimme_timeout_seconds = isStandaloneGimme() ? 0 : 20;
}

typedef struct GimmeClient // one per root directory
{
	char root[MAX_PATH];
	PCL_Client *client;
	U32 created;
	bool dirty;
	char **diffnames;
	PCL_DiffType *difftypes;
	int overridebranch;
	int overriderev;
	U32 overridetime;
	PCLFileSpec fileSpec;
	RegReader regReader;
	unsigned error_report_count;	// Number of errors that have been reported to the Patch Server
	GIMMEOperation debug_last_op;	// Last operation, for debugging purposes only
} GimmeClient;

typedef struct BranchCached
{
	// currently this can't change while the server is up, put an expiration on the cache if it can
	char *name;
	int parent_branch;
	char *warning;
} BranchCached;

static void destroyBranchInfo(BranchCached *cached);
static void finishQueue(void);

static GimmeClient **s_clients;
static StashTable s_branchcache;
static char **s_tempfiles;
static bool s_didwork;
static GimmeErrorValue s_error=0;
static U32 s_overridetime;
static bool s_launchEditor;
static const char *s_editor;
static PCLFileSpec s_filespec;
static char **s_extra;
static bool s_use_more_memory = false;
static bool s_doStartFileWatcher = false;  // If true, start FileWatcher.

static int s_overridebranch=-1;	AUTO_CMD_INT(s_overridebranch,	overridebranch) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
static int s_overriderev;		AUTO_CMD_INT(s_overriderev,		revision) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
static int s_ignoreerrors;
AUTO_COMMAND ACMD_NAME(ignoreerrors) ACMD_CMDLINE ACMD_ACCESSLEVEL(0); void cmd_ignoreerrors(int i) {gimme_state.ignore_errors = !!i; s_ignoreerrors = !!i;}
static int s_pause;				AUTO_CMD_INT(s_pause,			pause) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
								AUTO_COMMAND ACMD_NAME(nopause) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);	void cmd_nopause(int i) { s_pause = !i; }
static int s_delaypause;		AUTO_CMD_INT(s_delaypause,		delayPause) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
static int s_dodelaypause;
static int s_nowarn;			AUTO_CMD_INT(s_nowarn,			nowarn) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
static int s_quiet;				AUTO_COMMAND ACMD_NAME(quiet) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);		void cmd_quiet(int i) { s_quiet = GIMME_QUIET; }
static int s_noskipbins=-1;		AUTO_CMD_INT(s_noskipbins,		noskipbins) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
static bool s_nogimmeauthor=false; AUTO_CMD_INT(s_nogimmeauthor, nogimmeauthor) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

AUTO_COMMAND ACMD_NAME(filespec);
void cmd_filespec(char *str)
{
	PCLFileSpecEntry* e = callocStruct(PCLFileSpecEntry);
	
	e->doInclude = 1;
	e->filespec = strdup(str);
	
	eaPush(&s_filespec.entriesSecond, e);
}

AUTO_COMMAND ACMD_NAME(exfilespec);
void cmd_exfilespec(char *str)
{
	PCLFileSpecEntry* e = callocStruct(PCLFileSpecEntry);
	
	e->filespec = strdup(str);
	
	eaPush(&s_filespec.entriesSecond, e);
}

AUTO_COMMAND ACMD_NAME(extra);
void cmd_extra(char *str)
{
	eaPush(&s_extra, strdup(str));
}

static bool s_queue_checkins;
static GIMMEOperation s_checkin_op;
int g_patchme_simulate;	 AUTO_COMMAND ACMD_NAME(simulate);			void cmd_simulate(int i)	{ finishQueue(); g_patchme_simulate = i; }
int g_patchme_notestwarn; AUTO_COMMAND ACMD_NAME(notestwarn);		void cmd_notestwarn(int i)	{ finishQueue(); g_patchme_notestwarn = i; }
static int s_checkpoint; AUTO_COMMAND ACMD_NAME(leavecheckedout);	void cmd_checkpoint(int i)	{ finishQueue(); s_checkpoint = i; }
static int s_nocomments; AUTO_COMMAND ACMD_NAME(nocomments);		void cmd_nocomments(int i)	{ finishQueue(); s_nocomments = i; }
static char *s_defaultcomment = NULL; AUTO_COMMAND ACMD_NAME(defaultcomment); void cmd_defaultcomment(char *s) { s_defaultcomment = strdup(s); }

static char s_output_stats_to_file[MAX_PATH];
AUTO_CMD_STRING(s_output_stats_to_file, output_stats_to_file) ACMD_CMDLINE;

// Data received from the last progress callback
static S64 s_last_glv_received;
static S64 s_last_glv_total;
static U32 s_last_glv_received_files;
static U32 s_last_glv_total_files;
static int s_last_glv_xfers;
static int s_last_glv_buffered;
static S64 s_last_glv_actual_transferred;
static U32 s_last_glv_seconds;
static U64 s_last_glv_loops;
static F32 s_last_glv_elapsed;
static S64 s_first_glv_mirror_cpu_ticks;
static S64 s_last_glv_mirror_cpu_ticks;

extern int g_ExactTimestamps;
AUTO_COMMAND ACMD_NAME(exactTimestamps);		void cmd_exactTimestamps(int i)	{ g_ExactTimestamps= i; }

#define PCL_DO(funccall)		handleError(client, funccall)
#define PCL_DO_WAIT(funccall)	(handleError(client, funccall) && handleError(client, pclWait(client)))

void patchmeCheckCommandLine(void)
{
	// Hack for overriding server address/port in non-standalone apps, e.g. Launcer, GameClient, etc
	char *cmdline = strdup(GetCommandLineWithoutExecutable());
	char *s;
	const char *check[] = {"gimme_server ", "gimme_port ", "gimme_verbose "};
	int i;
	for (i=0; i<ARRAY_SIZE(check); i++) {
		if (s = strstri(cmdline, check[i])) {
			char *s2;
			s2 = strchr(s+strlen(check[i]), ' ');
			if (s2)
				*s2 = '\0';
			printfColor(COLOR_GREEN|COLOR_BRIGHT, "GimmeDLL: Found gimme argument: %s\n", s);
			globCmdParse(s);
			if (s2)
				*s2 = ' ';
		}
	}
	free(cmdline);

	g_patcher_encrypt_connections = false; // Disable encryption for the integrated gimme

	if(fileExists("C:/gimme_verbose.txt"))
	{
		gimme_verbose = true;
		printfColor(COLOR_GREEN|COLOR_BRIGHT, "GimmeDLL: gimme_verbose.txt found, enabling verbose mode\n");
	}
}

static NetComm *threadLocalComm(void)
{
	NetComm **comm=0;
	STATIC_THREAD_ALLOC_TYPE(comm,NetComm **);

	if (!*comm)
		*comm = commCreate(0, 1);
	return *comm;
}

#define patchmelog(level, fmt, ...) patchmelog_dbg(level, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

static void patchmelog_dbg(PCL_LogLevel level, FORMAT_STR const char *fmt, ...)
{
	char *str=NULL;
	estrStackCreate(&str);
	VA_START(args, fmt);
	estrConcatfv(&str, fmt, args);
	VA_END();
	
	switch(level)
	{
		xcase PCLLOG_ERROR:
			gimmeLog(LOG_WARN_HEAVY, "Error: %s", str);
			s_pause = 1;
		xcase PCLLOG_WARNING:
			gimmeLog(LOG_WARN_LIGHT, "Warning: %s", str);
		xdefault: // PCLLOG_INFO
			gimmeLog(LOG_STAGE, "%s", str);
		xcase PCLLOG_LINK:
		acase PCLLOG_SPAM:
			gimmeLog(LOG_TOSCREENONLY, "%s\n", str);
		xcase PCLLOG_FILEONLY:
			gimmeLog(LOG_TOFILEONLY, "%s", str);
		xcase PCLLOG_TITLE:
			setConsoleTitle(str);
	}
	estrDestroy(&str);
}

static void patchmelogCallback(PCL_LogLevel level, const char *str, void *userdata)
{
	patchmelog(level, "%s", str);
}

static const char *GimmeClientDebugString(GimmeClient *gc)
{
	static char buffer[1024];
	sprintf(buffer, "root %s", gc->root);
	return buffer;
}

static const char* getAuthor(void);

static S32 handleError(PCL_Client *client, PCL_ErrorCode error)
{
	// GGFIXME: map errors to gimme errors
	if(error == PCL_SUCCESS && client)
	{
		error = client->error;
	}
	if(client)
		pclClearError(client);

	if(error != PCL_SUCCESS)
	{
		char buf[MAX_PATH];
		GimmeClient *gc = NULL;


		pclGetErrorString(error, SAFESTR(buf));
		patchmelog(PCLLOG_ERROR, "PatchClientLib reporting: %s", buf);
		
		switch(error){
			#define CASE(x, y) xcase x: s_error = y
			CASE(PCL_LOST_CONNECTION,				GIMME_ERROR_NO_SC);
			CASE(PCL_XFERS_FULL,					GIMME_ERROR_NO_SC);
			CASE(PCL_FILE_NOT_FOUND,				GIMME_ERROR_FILENOTFOUND);
			CASE(PCL_UNEXPECTED_RESPONSE,			GIMME_ERROR_UNKNOWN);
			CASE(PCL_INVALID_VIEW,					GIMME_ERROR_NOT_IN_DB);
			CASE(PCL_FILESPEC_NOT_LOADED,			GIMME_ERROR_NO_SC);
			CASE(PCL_MANIFEST_NOT_LOADED,			GIMME_ERROR_NO_SC);
			CASE(PCL_NOT_IN_FILESPEC,				GIMME_ERROR_NOT_IN_DB);
			CASE(PCL_NULL_HOGG_IN_FILESPEC,			GIMME_ERROR_UNKNOWN);
			CASE(PCL_NAMING_FAILED,					GIMME_ERROR_UNKNOWN);
			CASE(PCL_SET_EXPIRATION_FAILED,			GIMME_ERROR_UNKNOWN);
			CASE(PCL_AUTHOR_FAILED,					GIMME_ERROR_UNKNOWN);
			CASE(PCL_LOCK_FAILED,					GIMME_ERROR_ALREADY_CHECKEDOUT);
			#if 0
			CASE(PCL_CHECKIN_FAILED,				GIMME_ERROR_);
			CASE(PCL_LOST_CONNECTION,);
			CASE(PCL_WAITING,);
			CASE(PCL_TIMED_OUT,);
			CASE(PCL_NO_VIEW_FILE,);
			CASE(PCL_DLL_NOT_LOADED,);
			CASE(PCL_NO_CONNECTION_FILE,);
			CASE(PCL_DIALOG_MESSAGE_ERROR,);
			CASE(PCL_HEADER_NOT_UP_TO_DATE,);
			CASE(PCL_HEADER_HOGG_NOT_LOADED,);
			CASE(PCL_NOT_IN_ROOT_FOLDER,);
			CASE(PCL_CLIENT_PTR_IS_NULL,);
			CASE(PCL_NO_ERROR_STRING,);
			CASE(PCL_NULL_PARAMETER,);
			CASE(PCL_STRING_BUFFER_SIZE_TOO_SMALL,);
			CASE(PCL_DLL_FUNCTION_NOT_LOADED,);
			CASE(PCL_COMM_LINK_FAILURE,);
			CASE(PCL_NO_FILES_LOADED,);
			CASE(PCL_HOGG_READ_ERROR,);
			CASE(PCL_COULD_NOT_START_LOCK_XFER,);
			CASE(PCL_NO_RESPONSE_FUNCTION,);
			CASE(PCL_INVALID_PARAMETER,);
			CASE(PCL_INTERNAL_LOGIC_BUG,);
			CASE(PCL_COULD_NOT_WRITE_LOCKED_FILE,);
			CASE(PCL_NOT_WAITING_FOR_NOTIFICATION,);
			#endif
			#undef CASE
			
			xdefault:
				s_error = GIMME_ERROR_UNKNOWN;
		}

		// Figure out which Gimme client this is.
		EARRAY_CONST_FOREACH_BEGIN(s_clients, i, n);
		{
			if (s_clients[i]->client == client)
			{
				gc = s_clients[i];
				break;
			}
		}
		EARRAY_FOREACH_END;

		// Send an error report.
		if (client && gc && gc->error_report_count++ < 10)
			pclSendLog(client, "GimmeErrorReport", "%s pcl_error %d pcl_error_string \"%s\" gimme_error %d last_op %d author %s",
				GimmeClientDebugString(gc), error, buf, s_error, gc->debug_last_op, getAuthor());
		
		return 0;
	}
	
	return 1;
}

static U32 parseDate(const char *s)
{
	if(s){
		U32 parsedTime = timeGetSecondsSince2000FromGimmeString(s);
		
		if(parsedTime){
			return patchSS2000ToFileTime(parsedTime-timeLocalOffsetFromUTC());
		}else{
			return 0;
		}
	}else{
		return 0;
	}
}

const char *patchmeGetTempFileName(const char *orig_name, int uid)
{
	char tempfile[MAX_PATH];
	char buf[MAX_PATH];
	const char *filepart;
	char *pTempDir = NULL;
	char *ret;
	static int internal_counter=0;

	strcpy(buf, orig_name);
	forwardSlashes(buf);
	filepart = strrchr(buf, '/');

	if(filepart)
		filepart++;
	else
		filepart = orig_name;

	if(!GetEnvironmentVariable_UTF8("TEMP", &pTempDir)){
		estrCopy2(&pTempDir, "c:/temp");
	}
	if (uid==-1) {
		uid = timeSecondsSince2000()+internal_counter;
		internal_counter++;
	}
	sprintf(tempfile, "%s/%u.%s", pTempDir, uid, filepart);
	eaPush(&s_tempfiles, ret = strdup(tempfile)); // FIXME: not really a safe path for deletion, but the name's unique
	
	estrDestroy(&pTempDir);
	return ret;
}

static void diffCurrent(PCL_Client *client, const char *fname, const char *dbname)
{
	const char *tempfile;

	if(!fileExists(fname))
		return;

	tempfile = patchmeGetTempFileName(fname, -1);

	PCL_DO_WAIT(pclGetFileTo(client, dbname, tempfile, NULL, 0, NULL, NULL));

	fileLaunchDiffProgram(tempfile, fname);
}

static const char* getAuthor(void)
{
	// GGFIXME: the gimme version does more...
	static char name[1024];
	static bool got_name=false;
	if(!got_name)
	{
		char *env;
		_dupenv_s(&env, NULL, "GIMME_USERNAME");
		if(env)
			strcpy(name, env);
		else
			strcpy(name, getUserName());
		got_name=true;
	}
	return name;
}

static bool displayProgressCB(PatchProcessStats *stats, GimmeClient *gc)
{
	static U32 last_reg_write = 0;

	// Save GLV statistics.
	s_last_glv_received = stats->received;
	s_last_glv_total = stats->total;
	s_last_glv_received_files = stats->received;
	s_last_glv_total_files = stats->total_files;
	s_last_glv_xfers = stats->xfers;
	s_last_glv_buffered = stats->buffered;
	s_last_glv_actual_transferred = stats->actual_transferred;
	s_last_glv_seconds = stats->seconds;
	s_last_glv_loops = stats->loops;
	s_last_glv_elapsed = stats->elapsed;

	if(stats->received && stats->total)
	{
		char title[1000];
		char buf1[64], buf2[64];
		U32 percent = stats->received * 100 / stats->total;
//		U32 compression = actual_transferred * 100 / received;
		F32 compression = (F32)stats->received / stats->actual_transferred;
		F32 total_time = stats->elapsed * stats->total / stats->received;

//		printf("%3d%%  Time: %d:%.2d/%d:%.2d  Files: %d/%d  Data: %4d%s/%d%s  Compression: %3d%%    \r",
		sprintf(title,
				"%3d%%  Time: %d:%.2d/%d:%.2d  Files: %d/%d  Data: %s/%s  Compression: %.1fx",
				percent,
				(U32)stats->elapsed / 60,
				(U32)stats->elapsed % 60,
				(U32)total_time / 60,
				(U32)total_time % 60,
				stats->received_files,
				stats->total_files,
				friendlyBytesBuf(stats->received, buf1),
				friendlyBytesBuf(stats->total, buf2),
				compression);
				
		pcllog(gc->client, PCLLOG_TITLE_DISCARDABLE, "%s", title);

		if(stats->total_files > 5)
		{
			U32 now = timeSecondsSince2000();
			const U32 minimum_delta = 5;
			if (now > last_reg_write + minimum_delta)
			{
				rrWriteInt(gc->regReader, "GlvTimestamp", now);
				last_reg_write = now;
			}
		}
	}
	return false;
}

static bool displayFolderScanCB(const char *fileName, int number, int count, bool addedToList, F32 elapsed,
								PCL_ErrorCode error, const char *error_details, GimmeClient *gc)
{
	if(count && (elapsed >= 0.2 || number == count))
	{
		char title[1000];
		sprintf(title, "%3d%%  (%d/%d) %-200.200s", (number * 100 / count), number, count, fileName+strlen(gc->root));
		setConsoleTitle(title);
		return true;
	}
	return false;
}

static PCL_ErrorCode patchMeDisconnectSafe(PCL_Client** clientInOut)
{
	PCL_Client* client = SAFE_DEREF(clientInOut);
	
	if(client){
		*clientInOut = NULL;
		return pclDisconnectAndDestroy(client);
	}
	
	return PCL_SUCCESS;
}

static bool patchMeMirrorCallback(	PCL_Client* client,
									void* userData,
									F32 elapsed,
									const char* curFileName,
									ProgressMeter* progress)
{
	static U32 lastTime;
	s_last_glv_mirror_cpu_ticks = timerCpuTicks64();
	if (!s_first_glv_mirror_cpu_ticks)
		s_first_glv_mirror_cpu_ticks = s_last_glv_mirror_cpu_ticks;
	
	if(	timeGetTime() - lastTime > 250 ||
		progress->files_so_far == progress->files_total)
	{
		char title[500];
		
		lastTime = timeGetTime();
	
		//char bytesString[100];
		//char totalBytesString[100];
		
		sprintf(title,
				"Mirroring %d/%d: %s",
				progress->files_so_far,
				progress->files_total,
				//friendlyBytesBuf(progress->bytes_so_far, bytesString),
				//friendlyBytesBuf(progress->bytes_total, totalBytesString),
				curFileName);
				
		setConsoleTitle(title);
	}
		
	return true;
}

static void gimmeClientDisconnect(GimmeClient **pgc)
{
	GimmeClient *gc;

	if(!pgc || !*pgc)
		return;

	gc = *pgc;

	if(gc->client)
		pclDisconnectAndDestroy(gc->client);

	eaFindAndRemove(&s_clients, gc);
	free(gc);
	*pgc = NULL;
}

static void getConnected(GimmeClient *gc)
{
	PCL_ErrorCode error = PCL_SUCCESS;
	PCL_Client *client = NULL;
	int attempts = 0;
	U32 connectTimer = timerAlloc();
	NetComm *comm = threadLocalComm();

	do {
		S32 didPrintTimeoutWarning = 0;
		
		// TODO: add autoupdate, it should be safe
		if(!PCL_DO(pclConnectAndCreate(	&client,
										GIMME_SERVER_AND_PORT,
										(4*60*60),
										comm,
										NULL,
										AUTOUPDATE_TOKEN,
										NULL,
										NULL,
										NULL)))
		{
			break;
		}

		if (s_use_more_memory)
		{
			PCL_DO(pclSetMaxMemUsage(client, 512*1024*1024));
		}
		
		pclSetMirrorCallback(client, patchMeMirrorCallback, NULL);
									
		while(1){
			Sleep(1);
			error = pclProcessTracked(client);
			commMonitor(comm);
			
			if(	error != PCL_WAITING
				||
				gimme_timeout_seconds &&
				timerElapsed(connectTimer) >= gimme_timeout_seconds)
			{
				break;
			}
			
			if(	!didPrintTimeoutWarning &&
				timerElapsed(connectTimer) >= 5)
			{
				didPrintTimeoutWarning = 1;
				if(gimme_timeout_seconds){
					printf(	"This connection attempt will timeout in %d seconds.\n",
							gimme_timeout_seconds);
				}else{
					printf("This connection attempt will not timeout.\n");
				}
			}
		}
					
		if(error == PCL_WAITING)
		{
			patchmelog(PCLLOG_WARNING, "Timeout while connecting to Gimme server %s:%d", GIMME_SERVER_AND_PORT);
			patchMeDisconnectSafe(&client);
			s_didwork = true;
			s_pause = true;
			break;
		}
		else if (error != PCL_SUCCESS)
		{
			if (error != PCL_LOST_CONNECTION)
				handleError(client, error);
			patchmelog(PCLLOG_SPAM, "The client was disconnected during a connect attempt", NULL);
			patchMeDisconnectSafe(&client);
			attempts++;
			if (attempts >= 5) {
				patchmelog(PCLLOG_WARNING, "Unable to successfully connect to Gimme server %s:%d", GIMME_SERVER_AND_PORT);
				s_didwork = true;
				s_pause = true;
				break;
			}
		}
		else
		{
			handleError(client, error);
		}
	} while(error != PCL_SUCCESS);

	if(client)
	{
		const char noahs_special_place_for_broken_things[] = "N:/users/noah";
		stashTableClearEx(s_branchcache, NULL, destroyBranchInfo); // FIXME: this needs to happen after *every* disconnect (the patchserver may have had its config changed during that time)

		if(	!PCL_DO(pclVerifyAllFiles(client, verifyAllFiles)) ||
			!PCL_DO(pclUseFileOverlay(client, fileOverlay)) ||
			!PCL_DO(pclSetProcessCallback(client, displayProgressCB, gc)) ||
			!PCL_DO(pclSetForceInScanCallback(client, displayFolderScanCB, gc)) ||
			!PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE|PCL_XFER_COMPRESSED)) ||
			!PCL_DO(pclSetLoggingFunction(client, patchmelogCallback, NULL)))
		{
			// There was an error.
		}

		if (dirExists(noahs_special_place_for_broken_things))
			PCL_DO(pclSetBadFilesDirectory(client, noahs_special_place_for_broken_things));

		if(gimme_verbose)
		{
			printfColor(COLOR_GREEN|COLOR_BRIGHT, "GimmeDLL: Verbose mode enabled\n");
			PCL_DO(pclVerboseLogging(client, 1));
		}
	}

	gc->client = client;
	timerFree(connectTimer);
}

// Check that `path` is entirely within `root.
//  ex. isPathPartOfRoot("C:\FCCore\path", "C:\FCCore") == true
//      isPathPartOfRoot("C:\FCCore\path", "C:\FC") == false
static bool isPathPartOfRoot(const char *path, const char *root)
{
	char buf[MAX_PATH];
	if (!strStartsWith(path, root))
		return false;
	// Same path
	if (stricmp(path, root)==0)
		return true;
	strcpy(buf, root);
	strcat(buf, "/");
	if (strStartsWith(path, buf))
		return true;
	return false; // Starts with the same, but not in the root (e.g. C:\FCCore\blah is not in C:\FC)
}

static const char* createFullPath(const char* path, char* buffer, S32 bufferLen)
{
	makefullpath_s(path, buffer, bufferLen);
	if(is_pigged_path(buffer)){
		buffer[0] = 0;
	}
/*
	if(!fileIsAbsolutePath(path)){
		size_t len;
		
		fileGetcwd(buffer, bufferLen);
		len = strlen(buffer);
		forwardSlashes(buffer);
		sprintf_s(	buffer + len,
					bufferLen - len,
					"%s%s",
					buffer[len - 1] == '/' ? "" : "/",
					path);
		forwardSlashes(buffer + len);

		printf("Found full path: %s\n", buffer);
	}else{
		strcpy_s(buffer, bufferLen, path);
		forwardSlashes(buffer);
	}
*/
	return buffer;
}

// Make sure that this root is acceptable for Gimme, and error if it is not.
// Gimme has additional safety constraints that PCL does not.  In particular, there must only
// be one candidate PATCH_DIR folder in the parent directory hierarchy; Nested Gimme
// installations are not allowed.
bool validateGimmeRoot(const char *fname, const char *root)
{
	char path[MAX_PATH];
	size_t len;
	char root_candidate[MAX_PATH];

	// Skip validation if this is not a real filename.
	if (!fname || !*fname)
		return true;

	// Copy the filename path and trim trailing path separators.
	devassert(isPathPartOfRoot(fname, root));
	strcpy_s(SAFESTR(path), fname);
	len = strlen(path);
	while (len > 0 && path[len - 1] == '/')
	{
		path[len - 1] = 0;
		--len;
	}
	devassert(*path);

	// Search for nested roots.
	root_candidate[0] = 0;
	for(;;)
	{
		char patchdir_candidate[MAX_PATH];
		char *next;

		// Check for a .patch folder in this directory.
		strcpy(patchdir_candidate, path);
		strcat(patchdir_candidate, "/" PATCH_DIR);
		if (dirExists(patchdir_candidate) || fileExists(patchdir_candidate))
		{
			if (root_candidate[0])
			{
				patchmelog(PCLLOG_ERROR, "Multiple candidate .patch directories found; Gimme does not allow nested roots: \"%s\" and \"%s\"",
					root_candidate, patchdir_candidate);
				return false;
			}
			strcpy(root_candidate, path);
		}

		// Go to parent directory.
		next = strrchr(path, '/');
		if (!next)
			break;																			// Exit
		*next = 0;
	}

	return true;
}

static S32 createAndConnectGimmeClientNoView(GimmeClient** gcInOut)
{
	bool first = false;
	GimmeClient*	gc = *gcInOut;
	PCL_Client* client;

	// Check if the connection was lost.
	if(	SAFE_MEMBER(gc, client) &&
		pclWait(gc->client) == PCL_LOST_CONNECTION)
	{
		patchMeDisconnectSafe(&gc->client);
	}

	if(!gc)
	{
		gc = callocStruct(GimmeClient);
		strcpy(gc->root, "NoRoot");
		eaPush(&s_clients, gc);
		gc->regReader = createRegReader();
		initRegReader(gc->regReader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
		*gcInOut = gc;
	}

	if(!gc->client)
	{
		getConnected(gc);
		if(!gc->client)
		{
			return 0;
		}
		first = true;
	}

	client = gc->client;

	pclResetStateToIdle(gc->client);

	if(first)
	{
		//PCL_DO_WAIT(pclSetAuthor(gc->client, getAuthor(), NULL, NULL));
	}

	gc->dirty = false;
	gc->created = timeSecondsSince2000();

	return 1;
}

static void gcLoadLocalFileSpec(GimmeClient* gc){
	char	filespecPath[MAX_PATH];
	FILE*	f;

	sprintf(filespecPath,
			"%s/%s/LocalFilespec.txt",
			gc->root,
			PATCH_DIR);
			
	f = fopen(filespecPath, "rt");
	
	if(f){
		char	line[1000];
		S32		foundInclude = 0;
		S32		lastWasIncludeAll = 0;
		bool	bVerbose = false;

		if (fileLastChanged(filespecPath) > time(NULL) - 30*60)
			bVerbose = true; // Verbose if modified in the last 30 minutes
		
		while(fgets(line, sizeof(line), f)){
			PCLFileSpecEntry* e = NULL;
			
			removeLeadingAndFollowingSpaces(line);

			while(	strEndsWith(line, "\n") ||
					strEndsWith(line, "\r"))
			{
				line[strlen(line) - 1] = 0;
			}
			
			if(	!line[0] ||
				strStartsWith(line, "#") ||
				strStartsWith(line, "//"))
			{
				continue;
			}
			
			if (stricmp(line, "Verbose")==0) {
				bVerbose = true;
			}
			else if(strStartsWith(line, "Include:")){
				e = callocStruct(PCLFileSpecEntry);
				e->doInclude = 1;
				e->filespec = strdup(line + strlen("Include:"));
				removeLeadingAndFollowingSpaces(e->filespec);
				forwardSlashes(e->filespec);
				if (bVerbose)
					printfColor(COLOR_BRIGHT|COLOR_GREEN,
							"Including: %s/%s\n",
							gc->root,
							e->filespec);
				lastWasIncludeAll = !strcmp(e->filespec, "*");
				foundInclude = 1;
			}
			else if(strStartsWith(line, "Exclude:")){
				e = callocStruct(PCLFileSpecEntry);
				e->filespec = strdup(line + strlen("Exclude:"));
				removeLeadingAndFollowingSpaces(e->filespec);
				forwardSlashes(e->filespec);
				if (bVerbose)
					printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
							"Excluding: %s/%s\n",
							gc->root,
							e->filespec);
				lastWasIncludeAll = 0;
			}else{
				printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
							"Bad line in %s: %s\n",
							filespecPath,
							line);
			}
			
			if(e){
				eaPush(&gc->fileSpec.entriesFirst, e);
				e->isAbsolute = 1;
			}
		}
		
		fclose(f);
		
		if(	foundInclude &&
			!lastWasIncludeAll &&
			bVerbose)
		{
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"Defaulting to excluding anything that doesn't match one of the above.\n"
						"Did you want \"Include: *\" as the last local filespec entry?\n");
		}
	}

	eaPushEArray(&gc->fileSpec.entriesSecond, &s_filespec.entriesSecond);
}

static S32 createAndConnectGimmeClient(	GimmeClient** gcInOut,
										const char* fname,
										bool require_manifest)
{
	PCL_Client*		client;
	GimmeClient*	gc = *gcInOut;
	char			root[MAX_PATH];
	const char*		project;
	const char*		sandbox;
	const char*		branch_str;
	const char*		server;
	const char*				port;
	int				branch;
	bool			first = false;
	TriviaList*		trivia = triviaListGetPatchTriviaForFile(fname, SAFESTR(root));
	char*			functionLog = NULL;
	bool success;
	bool bAlreadyConnected;
	
	PERFINFO_AUTO_START_FUNC();

	// Make sure that the root that PCL found is acceptable, and error if it is not.
	if (trivia)
	{
		success = validateGimmeRoot(fname, root);
		if (!success)
		{
			triviaListDestroy(&trivia);
			trivia = 0;
		}
	}

	if(!trivia)
	{
		//FatalErrorf("file %s is not in a patcher db", fname);
		estrDestroy(&functionLog);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}
	assert(strStartsWith(fname, root));

	project = triviaListGetValue(trivia,"PatchProject");
	branch_str = triviaListGetValue(trivia,"PatchBranch");
	//sandbox = triviaListGetValue(trivia,"PatchSandbox");
	sandbox = NULL;
	server = triviaListGetValue(trivia,"PatchServer");
	if(server)
		strcpy(gimme_server, server);
	port = triviaListGetValue(trivia,"PatchPort");
	if(port)
		gimme_port = strtoul(port, NULL, 10);

	if(!project)
	{
		FatalErrorf("Gimme folder %s has no project information!", root);
		triviaListDestroy(&trivia);
		estrDestroy(&functionLog);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	// Check if the connection was lost.
	if(	SAFE_MEMBER(gc, client) &&
		pclWait(gc->client) == PCL_LOST_CONNECTION)
	{
		estrConcatf(&functionLog, "patchMeDisconnectSafe\n");
		patchMeDisconnectSafe(&gc->client);
	}

	if (gc && gc->client)
		bAlreadyConnected = true;
	else
		bAlreadyConnected = false;

	if (!bAlreadyConnected)
		loadstart_printf(""); // "Connecting to ..." printed later

	if(!gc)
	{
		estrConcatf(&functionLog, "created gimme client\n");
		gc = callocStruct(GimmeClient);
		strcpy(gc->root, root);
		eaPush(&s_clients, gc);
		gc->regReader = createRegReader();
		initRegReader(gc->regReader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
		*gcInOut = gc;
		
		gcLoadLocalFileSpec(gc);
	}
	
	if(!gc->client)
	{
		estrConcatf(&functionLog, "getConnected\n");
		getConnected(gc);
		if(!gc->client)
		{
			triviaListDestroy(&trivia);
			estrDestroy(&functionLog);
			loadend_printf("failed to connect.");
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
		first = true;
	}

	branch = branch_str ? atoi(branch_str) : INT_MAX;
	if (s_overridebranch!=-1)
		branch = s_overridebranch;

	gc->overriderev = s_overriderev;
	gc->overridebranch = s_overridebranch;
	gc->overridetime = s_overridetime;

	client = gc->client;

	estrConcatf(&functionLog, "pclResetStateToIdle\n");
	pclResetStateToIdle(gc->client);

	if(first)
	{
		estrConcatf(&functionLog, "pclResetRoot\n");
		PCL_DO(pclResetRoot(gc->client, gc->root));
	}
	
	if (require_manifest)
		printf("Updating manifest...\n");
		
	if(s_overridetime)
	{
		bool saveTrivia = require_manifest && (s_overridebranch == -1);
		estrConcatf(&functionLog, "pclSetViewByTime\n");
		PCL_DO_WAIT(pclSetViewByTime(gc->client, project, branch, sandbox, s_overridetime, require_manifest, saveTrivia, NULL, NULL));
	}
	else if(s_overriderev)
	{
		bool saveTrivia = require_manifest && (s_overridebranch == -1);
		estrConcatf(&functionLog, "pclSetViewByRev\n");
		PCL_DO_WAIT(pclSetViewByRev(gc->client, project, branch, sandbox, s_overriderev, require_manifest, saveTrivia, NULL, NULL));
	}
	else
	{
		bool saveTrivia = require_manifest && (s_overridebranch == -1);
		estrConcatf(&functionLog, "pclSetViewLatest\n");
		PCL_DO_WAIT(pclSetViewLatest(gc->client, project, branch, sandbox, require_manifest, saveTrivia, NULL, NULL));
	}

	// Set this always, we may load hoggs even if not loading the manifest
	estrConcatf(&functionLog, "pclSetHoggsSingleAppMode\n");
	PCL_DO(pclSetHoggsSingleAppMode(gc->client, false));

	if(first && !s_nogimmeauthor)
	{
		estrConcatf(&functionLog, "pclSetAuthor\n");
		PCL_DO_WAIT(pclSetAuthor(gc->client, getAuthor(), NULL, NULL));
	}

	if(first)
	{
		EARRAY_CONST_FOREACH_BEGIN(s_extra, i, n);
		{
			pclAddExtraFolder(gc->client, s_extra[i], HOG_NOCREATE|HOG_READONLY);
		}
		EARRAY_FOREACH_END;
	}
	
	gc->dirty = false;
	gc->created = timeSecondsSince2000();

	triviaListDestroy(&trivia);
	estrDestroy(&functionLog);

	if (!bAlreadyConnected)
		loadend_printf("Connected successfully.");

	PERFINFO_AUTO_STOP_FUNC();
	return 1;
}

#define FIX_FULL_PATH(x)														\
	char fullPathBuffer[MAX_PATH];												\
	S32 ignored = (x = createFullPath(x, SAFESTR(fullPathBuffer))) == NULL

static PCL_Client* setViewToFile(	const char *fullpath,
									const char **dbname,
									GimmeClient **pgimmeclient,
									bool never_cache,
									bool require_manifest,
									bool require_new_client)
{
	GimmeClient*	gc = NULL;
	char			fname[MAX_PATH];
	NetComm*		comm = threadLocalComm();
	PERFINFO_AUTO_START_FUNC();

	assert(fullpath);
	strcpy(fname, fullpath);
	forwardSlashes(fname);

	if( strstri(fname, "CoreData") || 
		strstri(fname, "CoreSrc") ||
		strstri(fname, "CoreTools") )
	{
		// TOTAL HACK!
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Flush disconnects
	if (comm) {
		commMonitor(comm);
	}

	if (!require_new_client)
	{
		EARRAY_CONST_FOREACH_BEGIN(s_clients, i, isize);
			if(isPathPartOfRoot(fname, s_clients[i]->root) &&
				s_clients[i]->overridetime == s_overridetime &&
				s_clients[i]->overriderev == s_overriderev &&
				s_clients[i]->overridebranch == s_overridebranch)
			{
				gc = s_clients[i];
				break;
			}
		EARRAY_FOREACH_END;
	}

	// Check if the connection was lost.
	if(	SAFE_MEMBER(gc, client) &&
		pclWait(gc->client) == PCL_LOST_CONNECTION)
	{
		patchMeDisconnectSafe(&gc->client);
	}

	if( !SAFE_MEMBER(gc, client)
		||
		s_overridetime
		||
		s_overriderev
		||
		gc->dirty
		||
		require_manifest && !SAFE_MEMBER(gc->client, db) // If the connection is cached but wasn't loaded with the DB last time
		||
		never_cache)
	{
		if (stricmp(fullpath, "NoRoot")==0) {
			if(!createAndConnectGimmeClientNoView(&gc)){
				PERFINFO_AUTO_STOP_FUNC();
				return NULL;
			}
		} else {
			if(!createAndConnectGimmeClient(&gc, fname, require_manifest)){
				PERFINFO_AUTO_STOP_FUNC();
				return NULL;
			}
		}
	}

	if(dbname)
	{
		*dbname = fullpath + strlen(gc->root);
		if(isSlash(**dbname))
			++*dbname;
	}
	if(pgimmeclient)
		*pgimmeclient = gc;
	PERFINFO_AUTO_STOP_FUNC();
	return gc->client;
}

bool patchmeForceDirtyBit(const char *fullpath)
{
	char			fname[MAX_PATH];

	assert(fullpath);
	strcpy(fname, fullpath);
	forwardSlashes(fname);

	EARRAY_CONST_FOREACH_BEGIN(s_clients, i, isize);
		if(isPathPartOfRoot(fname, s_clients[i]->root) &&
			s_clients[i]->overridetime == s_overridetime &&
			s_clients[i]->overriderev == s_overriderev &&
			s_clients[i]->overridebranch == s_overridebranch)
		{
			s_clients[i]->dirty = true;
			return true;
		}
	EARRAY_FOREACH_END;

	return false;
}

// Tell patchme to use more memory, but go faster.
void patchmeUseMoreMemory()
{
	s_use_more_memory = true;
}

bool patchmeVerifyServerTimeDifference(PCL_Client *client, U32 *client_time, U32 *server_time)
{
	bool should_warn = false;

	if(PCL_DO(pclVerifyServerTimeDifference(client, &should_warn, client_time, server_time)))
	{
		return should_warn;
	}

	// doesn't make sense to check this if a different error came up
	return false;
}

static void cacheBranchInfo(const char *name, int parent_branch, const char *warning, PCL_ErrorCode error, const char *error_details, BranchCached **pcached)
{
// 	handleError(error);
	assert(error == PCL_SUCCESS);

	*pcached = calloc(1, sizeof(BranchCached));
	(*pcached)->name = strdup(name);
	(*pcached)->parent_branch = parent_branch;
	(*pcached)->warning = strdup(warning);
}

static void destroyBranchInfo(BranchCached *cached)
{
	SAFE_FREE(cached->name);
	SAFE_FREE(cached->warning);
	free(cached);
}

static BranchCached* getBranchInfo(PCL_Client *client, int branch)
{
	char key[256], project[256];
	BranchCached *cached = NULL;

	assert(client);
	if(PCL_DO(pclGetView(client, SAFESTR(project), NULL, branch < 0 ? &branch : NULL, NULL, 0)))
	{
		sprintf(key, "%s %d", project, branch);

		if(!stashFindPointer(s_branchcache, key, &cached))
		{
			if(PCL_DO_WAIT(pclGetBranchInfo(client, branch, cacheBranchInfo, &cached)))
			{
				if(!s_branchcache)
					s_branchcache = stashTableCreateWithStringKeys(0, StashDeepCopyKeys_NeverRelease);
				assert(stashAddPointer(s_branchcache, key, cached, false));
			}
		}
	}

	return cached;
}

bool patchmeGetPreviousBranchInfo(PCL_Client *client, int *branchOut, char **name)
{
	S32 branch;
	
	if(PCL_DO(pclGetView(client, NULL, 0, NULL, &branch, NULL, 0)))
	{
		BranchCached* cached = getBranchInfo(client, --branch);
		
		if(cached){
			*branchOut = branch;
			*name = SAFE_MEMBER(cached, name);
			
			return true;
		}
	}
	
	return false;
}

static void patchmeOutputStatsToFile(void)
{
	TriviaList trivia = {0};
	static int runningtotal_numfiles;
	static U64 runningtotal_size;
	static F32 runningtotal_elapsed;
	// Combine multiple calls if they're getting latest on multiple folders
	runningtotal_numfiles+=s_last_glv_total_files;
	runningtotal_size+=s_last_glv_total;
	runningtotal_elapsed+=s_last_glv_elapsed;
	triviaListPrintf(&trivia, "NumFiles", "%d", runningtotal_numfiles);
	triviaListPrintf(&trivia, "DataSize", "%"FORM_LL"d", runningtotal_size);
	triviaListPrintf(&trivia, "Elapsed", "%1.1f", runningtotal_elapsed);
	triviaListWriteToFile(&trivia, s_output_stats_to_file);
	triviaListClear(&trivia);
}

// Send our GLV stats to the server to be logged.
static void patchmeLogStatsToServer(PCL_Client *client, const char *project, const char *root, const char *path,
									S64 gimme_init, S64 patch_init, S64 transferring, S64 mirroring, S64 finishing)
{
	if (gimme_init < 0 || patch_init < 0 || transferring < 0 || mirroring < 0 || finishing < 0)
		return;
	pclSendLog(client, "GlvStats", "author \"%s\" project \"%s\" root \"%s\" path \"%s\""
		" duration_total %f duration_gimme_init %f duration_patch_init %f"
		" duration_transferring %f duration_mirroring %f duration_finishing %f"
		" patch_received %"FORM_LL"d patch_total %"FORM_LL"d patch_received_files %lu"
		" patch_total_files %lu patch_xfers %d patch_buffered %d patch_actual_transferred %"FORM_LL"d"
		" patch_seconds %lu patch_loops %"FORM_LL"u patch_elapsed %f",
		NULL_TO_EMPTY(getAuthor()), NULL_TO_EMPTY(project), NULL_TO_EMPTY(root), NULL_TO_EMPTY(path),
		timerSeconds64(gimme_init + patch_init + transferring + mirroring + finishing),
		timerSeconds64(gimme_init), timerSeconds64(patch_init),	timerSeconds64(transferring), timerSeconds64(mirroring), timerSeconds64(finishing),
		s_last_glv_received, s_last_glv_total,
		s_last_glv_received_files, s_last_glv_total_files, s_last_glv_xfers, s_last_glv_buffered, s_last_glv_actual_transferred,
		s_last_glv_seconds, s_last_glv_loops, s_last_glv_elapsed);
}

// Log an operation on a list of files.
void logFileList(const char *operation, const char *root, char **diffnames, PCL_DiffType *difftypes)
{
	char *files = NULL;
	int i;

	// Get file list.
	PERFINFO_AUTO_START_FUNC();
	estrStackCreate(&files);
	for (i = 0; i < eaSize(&diffnames); ++i)
	{
		if (i)
			estrConcatChar(&files, ',');
		estrConcatf(&files, "%s:%u", diffnames[i], (unsigned)difftypes[i]);
	}

	// Log file list.
	patchmelog(PCLLOG_INFO, "%s, \"%s\": %s", operation, root, files);
	estrDestroy(&files);
	PERFINFO_AUTO_STOP_FUNC();
}

char *patchmeBlockFileRegPath = "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme\\BlockedFiles";

// Check the registry to mark files as blocked
static void markBlockedFiles(const char* root, const char ** diffnames, PCL_DiffType *difftypes, bool show_popup)
{
	RegReader reader;
	char *blocked_files = NULL;		//list of blocked files
	char *blocked_reasons = NULL;	//list of unique blocking reasons
	int blocked_files_count = 0, blocked_reasons_count = 0;
	char buf[1024], fullpath[MAX_PATH];
	int i;

	PERFINFO_AUTO_START_FUNC();

	reader = createRegReader();
	initRegReader(reader, patchmeBlockFileRegPath);

	for (i = 0; i < eaSize(&diffnames); i++)
	{
		sprintf(fullpath, "%s/%s", root, diffnames[i]);

		buf[0] = 0;
		if (rrReadString(reader, fullpath, SAFESTR(buf)))
		{
			difftypes[i] |= PCLDIFF_BLOCKED;
			if ((difftypes[i] & PCLDIFFMASK_ACTION) != PCLDIFF_DELETED && (difftypes[i] & PCLDIFFMASK_ACTION) != PCLDIFF_NOCHANGE)
			{
				// 6 and 8 chosen as a arbitrary display lengths to keep popup from getting too large.  reasons are more important to see than files, so more can be shown.
				if (blocked_files_count == 6)
					estrConcat(&blocked_files, "\n...", 4);
				else if (blocked_files_count < 6)
					estrConcatf(&blocked_files, "\n%s", fullpath);
				++blocked_files_count;

				if (buf[0] && (!blocked_reasons || !strstr(blocked_reasons, buf)))
				{
					if (blocked_reasons_count == 8)
						estrConcat(&blocked_reasons, "\n...", 4);
					else if (blocked_reasons_count < 8)
						estrConcatf(&blocked_reasons, "\n%s", buf);
					++blocked_reasons_count;
				}
			}
		}
	}

	destroyRegReader(reader);

	if (blocked_reasons_count)
	{
		char* warning = NULL;
		estrConcatf(&warning, "Some of the included files have been blocked for the reason(s):%s\n\nThis involves the file(s):%s\n\n", blocked_reasons, blocked_files);

		if (show_popup)
		{
			estrConcatf(&warning, "These items will be unchecked by default.  The checkin will fail if any of them are included.");
			patchmelog(PCLLOG_WARNING, "%s", warning);
			MessageBox_UTF8(NULL, warning, "Warning", MB_OK);
		}
		else
		{
			estrConcatf(&warning, "These blocked files will cause this checkin to fail.");
			patchmelog(PCLLOG_WARNING, "%s", warning);
		}

		estrDestroy(&warning);
	}

	estrDestroy(&blocked_files);
	estrDestroy(&blocked_reasons);

	PERFINFO_AUTO_STOP_FUNC();
}

// Check diff types for blocked files. Return true if safe to continue, otherwise return false and set error.
static bool checkBlockedFiles(const char* root, const char ** diffnames, PCL_DiffType *difftypes, bool show_popup)
{
	char *blocked_files = NULL;		//list of blocked files
	int blocked_files_count = 0, i;

	PERFINFO_AUTO_START_FUNC();

	// Check for blocked files
	for (i = 0; i < eaSize(&diffnames); i++)
	{
		if ((difftypes[i] & PCLDIFF_BLOCKED) && (difftypes[i] & PCLDIFFMASK_ACTION) != PCLDIFF_DELETED && (difftypes[i] & PCLDIFFMASK_ACTION) != PCLDIFF_NOCHANGE)
		{
			// 14 chosen as an arbitrary display length to keep popup from getting too large
			if (blocked_files_count == 14)
				estrConcat(&blocked_files, "\n...", 4);
			else if (blocked_files_count < 14)
				estrConcatf(&blocked_files, "\n%s/%s", root, diffnames[i]);
			++blocked_files_count;
		}
	}

	if (blocked_files_count)
	{
		char* warning = NULL;
		estrConcatf(&warning, "Checkin failed because it included the following blocked file(s):%s\n\nEither run the operation again or check previous console output for the reasons that these files were blocked.", blocked_files);

		s_error = GIMME_ERROR_BLOCKED;
		patchmelog(PCLLOG_WARNING, "%s", warning);
		if (show_popup)
		{
			MessageBox_UTF8(NULL, warning, "Warning", MB_OK);
		}

		estrDestroy(&warning);
	}

	estrDestroy(&blocked_files);

	PERFINFO_AUTO_STOP_FUNC();

	return !blocked_files_count;
}

// Update registry if blocked files are being deleted or reverted
static void updateBlockedFiles(const char* root, const char ** diffnames, PCL_DiffType *difftypes)
{
	RegReader reader;
	char buf[1024], fullpath[MAX_PATH];
	int i;

	PERFINFO_AUTO_START_FUNC();

	reader = createRegReader();
	initRegReader(reader, patchmeBlockFileRegPath);

	for (i = 0; i < eaSize(&diffnames); i++)
	{
		if (difftypes[i] & PCLDIFF_BLOCKED)
		{
			if ((difftypes[i] & PCLDIFFMASK_ACTION) == PCLDIFF_DELETED || (difftypes[i] & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE)
			{
				sprintf(fullpath, "%s/%s", root, diffnames[i]);
				if (rrReadString(reader, fullpath, SAFESTR(buf)))
					rrDelete(reader, fullpath);
			}
			else
				assertmsg(0, "Somehow a blocked file was able to be checked in?");
		}
	}

	destroyRegReader(reader);

	PERFINFO_AUTO_STOP_FUNC();
}

static void finishQueue(void)
{
	int i;
	for(i = s_queue_checkins ? 0 : eaSize(&s_clients)-1; i < eaSize(&s_clients); i++)
	{
		GimmeClient *gc = s_clients[i];
		PCL_Client *client = gc->client;
		U32 client_time, server_time;
		if(!client)
			continue;

		assert(eaSize(&gc->diffnames) == eaiSize(&gc->difftypes));
		if(!eaSize(&gc->diffnames))
			continue;

		if(patchmeVerifyServerTimeDifference(client, &client_time, &server_time))
		{
			U32 time_diff = ABS_UNS_DIFF(client_time, server_time);
			Errorf("Client time out of sync with server time, client is %ds %s", time_diff, (client_time > server_time) ? "ahead" : "behind");
			
			if( !s_nocomments )
			{
				char *time_warning = NULL;
				estrStackCreate(&time_warning);
				estrPrintf(&time_warning, "Your system clock appears to be %d seconds %s the server time.  If this warning persists, please check with IT to synchronize your system clock.", time_diff, (client_time > server_time) ? "ahead of" : "behind");
				MessageBox_UTF8(NULL, time_warning, "Warning", MB_OK);
				estrDestroy(&time_warning);
			}
		}

		patchmeGetTestInfo(gc->root, gc->diffnames, gc->difftypes); // mark the files accordingly, even if they aren't commenting
		markBlockedFiles(gc->root, gc->diffnames, gc->difftypes, !s_nocomments);

		logFileList("Considering", gc->root, gc->diffnames, gc->difftypes);

		if(	(	s_nocomments ||
				patchmeDialogCheckin(	s_checkin_op,
										gc->client,
										gc->root,
										&gc->diffnames,
										&gc->difftypes))
			&&
			!g_patchme_simulate)
		{
			if(checkBlockedFiles(gc->root, gc->diffnames, gc->difftypes, !s_nocomments))
			{
				const char *comment = s_nocomments ? s_defaultcomment : patchmeDialogCheckinGetComments();

				logFileList("Checkin", gc->root, gc->diffnames, gc->difftypes);

				PCL_DO_WAIT(pclCheckinFileList(	client,
												gc->diffnames,
												gc->difftypes,
												eaSize(&gc->diffnames),
												comment,
												s_checkin_op == GIMME_FORCECHECKIN,
												NULL,
												NULL));

				if(!s_error)
					updateBlockedFiles(gc->root, gc->diffnames, gc->difftypes);
				
				if(s_checkpoint || s_checkin_op == GIMME_CHECKIN_LEAVE_CHECKEDOUT)
				{
					// If we have any new files, we need to update the manifest in order to check them out,
					//  otherwise they just get set readonly like a checkout on a non-versioned file.
					int j;
					for (j=0; j<eaiSize(&gc->difftypes); j++) 
					{
						if ((gc->difftypes[j] & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED)
						{
							gc->dirty = true;
						}
					}
				
					if (gc->dirty)
					{
						createAndConnectGimmeClient(&gc, gc->root, true);
					}
				
					PCL_DO_WAIT(pclLockFiles(	client,
												gc->diffnames,
												eaSize(&gc->diffnames),
												NULL,
												NULL,
												NULL,
												NULL,
												NULL));
				}
			}
		}

		eaDestroyEx(&gc->diffnames, NULL);
		eaiDestroy(&gc->difftypes);
		gc->dirty = true;
	}
}

static bool patchMePrelockCallback(	PCL_Client* client,
									void *userdata,
									const char*const* fileNames,
									U32 count)
{
	GimmeQuietBits* quiet = userdata;
	if(count >= 100 && !(quiet && (*quiet & GIMME_QUIET_LARGE_CHECKOUT))){
		char msg[1000];
		
		sprintf(msg, "You are trying to checkout %d files.  Are you sure about this?", count);
		if(MessageBox_UTF8(NULL, msg, "Woah, that's a lot of files!", MB_YESNO) != IDYES){
			printf("Canceling checkout of %d files!\n", count);
			return false;
		}

		sprintf(msg, "Seriously, checking out %d files is not cool (unless you meant to).  Are you REALLY sure?", count);
		if(MessageBox_UTF8(NULL, msg, "No joke, that is a whole bunch of files!", MB_YESNO) != IDYES){
			printf("Canceling checkout of %d files!\n", count);
			return false;
		}
	}
	return true;
}

bool patchmeDoOperationEx(	const char **file_paths_orig,
							int file_count,
							GIMMEOperation op,
							GimmeQuietBits quiet,
							bool is_single_file, // Through the single-op API, might actually be multiple files, but *not* a whole directory
							GimmeErrorValue *ret)
{
	// GGFIXME: none of the patchme functions use quiet bits
	PCL_ErrorCode error = PCL_SUCCESS;

	char *dbname;
	char **paired_paths=NULL;
	char **paired_names=NULL;
	int *paired_recurse=NULL;
	int i;

	GimmeClient *gc = NULL;
	PCL_Client *client = NULL;
	char **file_paths=NULL;
	bool never_cache = false;
	char path_buffer[MAX_PATH];

	S64 gimme_op_start = timerCpuTicks64();
	S64 patch_op_start, patch_wait_start;
	S32 success;
	
	gimmeQueryClearCaches();

	if (file_count<=0) // Something wrong with the caller?
		return false;

	// Remove duplicates
	for (i=0; i<file_count; i++) {
		int j;
		bool bGood;
		strcpy(path_buffer, file_paths_orig[i]);
		if (path_buffer[0] && path_buffer[strlen(path_buffer)-1] == '/')
			path_buffer[strlen(path_buffer)-1] = '\0';
		bGood = path_buffer[0];
		for (j=0; bGood && j<eaSize(&file_paths); j++) {
			if (stricmp(path_buffer, file_paths[j])==0) {
				bGood = false;
			}
		}
		if (bGood) {
			eaPush(&file_paths, strdup(path_buffer));
		}
	}
	file_count = eaSize(&file_paths);

#if 0
	// TODO: Implement optimized/non-manifest single-file versions
	//  Or, would it be simpler to implement downloading just a fraction of
	//  the manifest and keeping the rest of the code the same?
	// Also free file_paths
	if (is_single_file)
	{
		switch (op)
		{
			xcase GIMME_CHECKOUT:
				return patchmeCheckoutFast(fullpath, ret);
			xcase GIMME_CHECKIN_LEAVE_CHECKEDOUT:
			acase GIMME_CHECKIN:
			acase GIMME_FORCECHECKIN:
			acase GIMME_ACTUALLY_DELETE:
				return patchmeCheckinFast(fullpath, op, ret);
			xcase GIMME_DELETE:
				return patchmeDeleteFast(fullpath, ret);
			xcase GIMME_UNDO_CHECKOUT:
				return patchmeUndoCheckoutFast(fullpath, ret);
			xcase GIMME_GLV:
				return patchmeGetLatestFast(fullpath, ret);
		}
	}
#endif

	// Early out
	if (op == GIMME_CHECKOUT && is_single_file && file_count==1)
	{
		if (GimmeQueryIsFileLockedByMeOrNew(file_paths[0])) {
			char ms_path[MAX_PATH];
			strcpy(ms_path, file_paths[0]);
			strcat(ms_path, ".ms");
			if (!fileExists(ms_path) || GimmeQueryIsFileLockedByMeOrNew(ms_path))
			{
				s_didwork = true;
				fwChmod(file_paths[0], _S_IREAD|_S_IWRITE);
				if (fileExists(ms_path))
					fwChmod(ms_path, _S_IREAD|_S_IWRITE);
				if (ret)
					*ret = GIMME_NO_ERROR;
				eaDestroy(&file_paths);
				return true;
			}
		}
	}

	// Checkouts are not allowed to use the cached connection/view since that might result in data loss
	if(op == GIMME_CHECKOUT)
		never_cache = true;

	// Try to open a client without the manifest.
	if(!gimme_force_manifest && is_single_file && file_count)
	{
		client = setViewToFile(file_paths[0], &dbname, &gc, never_cache, false, !s_queue_checkins);
		if(client)
		{
			if(client->filespec)
			{
				// Got a client, load file data from the server directly.
				char trimmed_path[MAX_PATH];
				FOR_EACH_IN_EARRAY(file_paths, const char, path)
					strcpy(trimmed_path, path + strlen(client->root_folder) + 1);
					PCL_DO_WAIT(pclInjectFileVersionFromServer(client, trimmed_path));

					if(strEndsWith(trimmed_path, ".ms") && strlen(trimmed_path) > 3)
						trimmed_path[strlen(trimmed_path)-3] = '\0';
					else if(strlen(path) + 3 <= MAX_PATH) // check if the original path is long enough for a .ms
						strcat(trimmed_path, ".ms");
					else
						continue;	//didn't change the path here, don't inject again

					PCL_DO_WAIT(pclInjectFileVersionFromServer(client, trimmed_path));
				FOR_EACH_END
			}
			else
			{
				// No filespec, fall back to a normal client.
				gimmeClientDisconnect(&gc);
				client = NULL;
			}
		}
	}
	
	// Couldn't do a fast-mode client, fall back to normal.
	if(!client)
		client = setViewToFile(file_paths[0], &dbname, &gc, never_cache, true, !s_queue_checkins);

	// Still couldn't make a client, error out.
	if(!client)
		return false;

	// Record this as the last operation.
	gc->debug_last_op = op;
		
	// Check for doing operations on files excluded from being mirrored
	if (client->mirrorFilespec)
	{
		for (i=0; i<file_count; i++)
		{
			const char *file_path = file_paths[i];
			size_t root_len;
			const char *file_name;
			root_len = strlen(gc->root);
			if (strlen(file_path) < root_len || !strStartsWith(file_path, gc->root)
				|| file_path[root_len] && file_path[root_len] != '/')
			{
				patchmelog(PCLLOG_ERROR, "MirrorFilespec: %s not within %s", file_path, gc->root);
				if (ret)
					*ret = GIMME_ERROR_LOCKFILE;
				eaDestroy(&file_paths);
				s_didwork = true;
				return true;
			}
			if (strlen(file_path) == strlen(gc->root))
				continue;
			file_name = file_path + strlen(gc->root) + 1;
			if (simpleFileSpecExcludesFile(file_name, client->mirrorFilespec))
			{
				patchmelog(PCLLOG_ERROR, "Cannot perform operation on file which is excluded from being mirrored: %s\n", file_path);
				if (ret)
					*ret = GIMME_ERROR_LOCKFILE;
				eaDestroy(&file_paths);
				s_didwork = true;
				return true;
			}
		}
	}

	{
		S32		branch;
		char	project[MAX_PATH];
		
		if(	pclGetCurrentBranch(client, &branch) == PCL_SUCCESS &&
			pclGetCurrentProject(client, SAFESTR(project)) == PCL_SUCCESS)
		{
			static char last_str[128];
			char str[128];
			sprintf(str, "Gimme folder \"%s\", Project \"%s\", Branch %d.",
					gc->root,
					project,
					branch);
			if (stricmp(str, last_str)!=0) {
				printf("%s\n", str);
				strcpy(last_str, str);
			}
		}
	}

	s_error = GIMME_NO_ERROR;

	// create the list of unique paths to work from
	for (i=0; i<file_count; i++) 
	{
		char *file_path;
		if (eaFindString(&paired_paths, file_paths[i]) == -1)
		{
			char temp_buf[MAX_PATH];
			char *ms_path;

			//add the absolute and relative path of the file
			eaPush(&paired_paths, file_path=strdup(file_paths[i]));
			if (stricmp(file_path, gc->root)==0)
				eaPush(&paired_names, "");
			else
				eaPush(&paired_names, &file_path[strlen(gc->root)+1]);
			eaiPush(&paired_recurse, 1);

			//assume it has a .ms file, and add that as well
			strcpy(temp_buf, file_path);
			if(strEndsWith(file_path, ".ms"))
				temp_buf[strlen(temp_buf)-3] = '\0';
			else if(strlen(temp_buf) + 3 <= MAX_PATH)
				strcat(temp_buf, ".ms");
			if (eaFindString(&paired_paths, temp_buf) == -1)
			{
				eaPush(&paired_paths, ms_path = strdup(temp_buf));
				if (stricmp(ms_path, gc->root)==0)
					eaPush(&paired_names, "");
				else
					eaPush(&paired_names, &ms_path[strlen(gc->root)+1]);
				eaiPush(&paired_recurse, 1);
			}
		}
	}

	assert(!g_patchme_simulate || op == GIMME_CHECKIN_LEAVE_CHECKEDOUT || op == GIMME_CHECKIN || op == GIMME_FORCECHECKIN || op == GIMME_ACTUALLY_DELETE);

	switch(op)
	{
		xcase GIMME_CHECKOUT:
		{
			loadstart_printf("Checking out ");
			consoleSetFGColor(COLOR_BLUE|COLOR_GREEN|COLOR_BRIGHT);
			loadupdate_printf("%s", paired_paths[0]);
			if (file_count>1)
				loadupdate_printf(", (%d files)", file_count);
			consoleSetFGColor(COLOR_RED|COLOR_GREEN|COLOR_BLUE);
			loadupdate_printf("...");
			error = pclLockFiles(	client,
									paired_names,
									eaSize(&paired_names),
									NULL,
									NULL,
									patchMePrelockCallback,
									&quiet,
									&gc->fileSpec); // this will also get latest
			if(error == PCL_SUCCESS)
				error = pclWait(client);
			else if (error == PCL_LOCK_FAILED)
				pclWait(client); // Still might get latest on some files in the batch
			if(error == PCL_LOCK_FAILED)
			{
				s_error = GIMME_ERROR_ALREADY_CHECKEDOUT;
			} else {
				// Success
				handleError(client, error);
				gc->dirty = true;
			}

			// TODO: Checking out a folder, we should have set any new files, which are not in the manifest, as writeable

			if (s_launchEditor && !s_error) {
				for (i=0; i<file_count; i++) 
				if(fileExists(file_paths[i]))
				{
					if(!s_editor)
					{
						fileOpenWithEditor(file_paths[i]);
					}
					else
					{
						char buf[MAX_PATH];
						sprintf(buf, "\"%s\" \"%s\"", s_editor, file_paths[i]);
						_flushall();
						system_detach(buf, 0, false);
					}
				}			
			}
			loadend_printf("Done checking out file.");
		}
		xcase GIMME_CHECKIN_LEAVE_CHECKEDOUT:
		acase GIMME_CHECKIN:
		acase GIMME_FORCECHECKIN:
		acase GIMME_ACTUALLY_DELETE:
		acase GIMME_UNDO_CHECKOUT:
		{
			int old_size;

			if(s_queue_checkins)	//only start queue if this op is queued
			{
				if(s_checkin_op != op)
					finishQueue();
			}
			s_checkin_op = op;

			old_size = eaSize(&gc->diffnames);
			for (i=0; i<eaSize(&paired_names); i++)
			{
				PCL_DO(pclDiffFolder(	client,
										paired_names[i],
										op == GIMME_FORCECHECKIN,
										op == GIMME_UNDO_CHECKOUT,
										is_single_file,
										&gc->diffnames,
										&gc->difftypes));
			}
			if(s_checkpoint || op == GIMME_CHECKIN_LEAVE_CHECKEDOUT)
			{
				// remove any 'undo checkout' actions
				for(i = eaSize(&gc->diffnames)-1; i >= old_size; --i)
				{
					if((gc->difftypes[i] & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE)
					{
						free(eaRemove(&gc->diffnames, i));
						eaiRemove(&gc->difftypes, i);
					}
				}
			}
			if(op == GIMME_UNDO_CHECKOUT)
			{
				for(i = eaSize(&gc->diffnames)-1; i >= old_size; --i) {
					if ((gc->difftypes[i]&PCLDIFFMASK_ACTION) == PCLDIFF_CREATED) {
						gc->difftypes[i] = PCLDIFF_NOCHANGE | PCLDIFF_NEWFILE;
					} else {
						gc->difftypes[i] = PCLDIFF_NOCHANGE;
					}
				}
			}
			// Remove files that don't match the filespec.
			for(i = eaSize(&gc->diffnames)-1; i >= old_size; --i)
			{
				const char* subPath = NULL;
				EARRAY_CONST_FOREACH_BEGIN(paired_names, j, jsize);
					if(strStartsWith(gc->diffnames[i], paired_names[j])){
						subPath = gc->diffnames[i] + strlen(paired_names[j]);
						if(	paired_names[j][0] &&
							subPath[0] &&
							subPath[0] != '/')
						{
							subPath = NULL;
						}else{
							break;
						}
					}
				EARRAY_FOREACH_END;
				assert(subPath);
				if(!pclFileIsIncludedByFileSpec(subPath, gc->diffnames[i], &gc->fileSpec))
				{
					free(eaRemove(&gc->diffnames, i));
					eaiRemove(&gc->difftypes, i);
				}
			}
			if(!s_queue_checkins)
			{
				int prev_s_nocomments = s_nocomments;
				s_nocomments = !!(quiet & GIMME_QUIET);
				finishQueue();
				s_nocomments = prev_s_nocomments;
			}
		}

		xcase GIMME_DELETE:
			for(i = 0; i < eaSize(&paired_paths); i++)
			{
				if(	s_nowarn
					||
					quiet & GIMME_QUIET
					||
					fileExists(paired_paths[i]) &&
					patchmeDialogDelete(paired_paths[i], false)
					||
					dirExists(paired_paths[i]) &&
					patchmeDialogDelete(paired_paths[i], true))
				{
					bool exists;
					PCL_DO(pclExistsInDb(client, paired_names[i], &exists));
					if(exists)
					{
						PCL_ErrorCode error = pclLockFiles(	client,
															&paired_names[i],
															1,
															NULL,
															NULL,
															patchMePrelockCallback,
															&quiet,
															&gc->fileSpec); // this will also get latest

						if(error == PCL_SUCCESS) {
							error = pclWait(client);
							fileMoveToRecycleBin(paired_paths[i]);
						}
						gc->dirty = true;
					} else {
						fileMoveToRecycleBin(paired_paths[i]);
					}
				}
			}

		xcase GIMME_GLV:
			if(s_overridetime)
			{
				PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE & ~(PCL_BACKUP_TIMESTAMP_CHANGES|PCL_KEEP_RECREATED_DELETED)));
			}
			if(s_noskipbins == 0 || (s_noskipbins == -1 && !s_overridetime))
			{
				PCL_DO(pclAddFileFlags(client, PCL_SKIP_BINS));
			}

			// Remove auto-added .ms entries which don't exist
			for (i=eaSize(&paired_paths)-1; i>=0; i--) {
				bool exists;
				if (strEndsWith(paired_paths[i], ".ms"))
				{
					PCL_DO(pclExistsInDb(client, paired_names[i], &exists));
					if (!exists)
					{
						eaRemove(&paired_names, i);
						free(eaRemove(&paired_paths, i));
						eaiRemove(&paired_recurse, i);
					}
				}
			}

			loadstart_printf("Getting latest on ");
			consoleSetFGColor(COLOR_BLUE|COLOR_GREEN|COLOR_BRIGHT);
			loadupdate_printf("%s", paired_paths[0]);
			consoleSetFGColor(COLOR_RED|COLOR_GREEN|COLOR_BLUE);
			loadupdate_printf("...");
			patch_op_start = timerCpuTicks64();
			success = PCL_DO(pclGetFileList(client,
											paired_paths,
											paired_recurse,
											is_single_file?true:false,
											eaSize(&paired_paths),
											NULL,
											NULL,
											&gc->fileSpec));
			patch_wait_start = timerCpuTicks64();
			s_last_glv_mirror_cpu_ticks = 0;
			if (success)
				PCL_DO(pclWaitFrames(client, true));
			
			{
				char buf[64];
				S64 stop = timerCpuTicks64();
				loadend_printf("Done getting latest, %d files, %s, %1.1fs, average %s/s", s_last_glv_total_files, friendlyBytes(s_last_glv_total),
					s_last_glv_elapsed, friendlyBytesBuf((U64)(s_last_glv_total / (s_last_glv_elapsed?s_last_glv_elapsed:1)), buf));
				if (s_output_stats_to_file[0])
					patchmeOutputStatsToFile();
				if (s_error == GIMME_NO_ERROR && client->root_folder && file_count == 1 && s_last_glv_total_files > 5)
					patchmeLogStatsToServer(client, client->project, client->root_folder, paired_paths[0], patch_op_start - gimme_op_start,
						patch_wait_start - patch_op_start, s_first_glv_mirror_cpu_ticks - patch_wait_start,
						s_last_glv_mirror_cpu_ticks - s_first_glv_mirror_cpu_ticks,	stop - s_last_glv_mirror_cpu_ticks);
			}

#if !PLATFORM_CONSOLE
			if(s_error == GIMME_NO_ERROR)
			{
				// Record the time in the glv log
				char glvlog[MAX_PATH], line[100];

				sprintf(glvlog, "%s/" PATCH_DIR "/glv.log", client->root_folder);
				sprintf(line, "%u\r\n", s_overridetime ? s_overridetime : getCurrentFileTime());
				InsertLineAtBeginningOfFileAndTrunc(glvlog, line, 25);
			}

			if(client && client->root_folder)
			{
				// Execute the runevery script if present
				char buf[MAX_PATH];
				const char *val;
				TriviaList *trivia;
				TriviaMutex trivia_mutex;
				sprintf(buf, "%s/tools/scripts/RunEvery.bat", client->root_folder);
				if(fileExists(buf))
				{
					backSlashes(buf);
					loadstart_printf("Running RunEvery script...");
					system(buf);
					loadend_printf("done");
				}

				// Add the root folder to filewatcher if needed
				// FIXME: This belongs in patchtrivia.c.
				sprintf(buf, "%s/" PATCH_DIR "/patch_trivia.txt", client->root_folder);
				trivia_mutex = triviaAcquireDumbMutex(buf);
				trivia = triviaListCreateFromFile(buf);
				assert(trivia);
				val = triviaListGetValue(trivia, "AddedToFilewatcher");
				if(!val || stricmp(val, "0")==0 || stricmp(val, "1")==0 || stricmp(val, "2")==0 || stricmp(val, "3")==0)
				{
					char **fw_config = NULL, *data, root_folder[MAX_PATH];
					int i;
					FILE *fw_config_file;
					strcpy(root_folder, client->root_folder);
					forwardSlashes(root_folder);
					loadstart_printf("Adding %s to filewatcher config", root_folder);
					data = fileAlloc("C:/CrypticSettings/fileWatch.txt", NULL);
					DivideString(data, "\n", &fw_config, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_FORWARDSLASHES);
					free(data);
					for(i=eaSize(&fw_config)-1; i >= 0; i--)
					{
						if(strStartsWith(fw_config[i], root_folder))
							eaRemove(&fw_config, i);
						else if(strStartsWith(fw_config[i], "C:/game"))
							eaRemove(&fw_config, i);
					}
					eaPush(&fw_config, strdupf("%s/data", root_folder));
					eaPush(&fw_config, strdupf("%s/src", root_folder));
					eaPush(&fw_config, strdupf("%s/tools", root_folder));
					makeDirectoriesForFile("C:/CrypticSettings/fileWatch.txt.new");
					fw_config_file = fopen("C:/CrypticSettings/fileWatch.txt.new", "wb");
					if(fw_config_file)
					{
						for(i=0; i<eaSize(&fw_config); i++)
						{
							backSlashes(fw_config[i]);
							fprintf(fw_config_file, "%s\r\n", fw_config[i]);
						}
						fclose(fw_config_file);
						unlink("C:/CrypticSettings/fileWatch.txt.old");
						rename("C:/CrypticSettings/fileWatch.txt", "C:/CrypticSettings/fileWatch.txt.old");
						rename("C:/CrypticSettings/fileWatch.txt.new", "C:/CrypticSettings/fileWatch.txt");
						triviaListPrintf(trivia, "AddedToFilewatcher", "4");
						triviaListWriteToFile(trivia, buf);
					}
					eaDestroyEx(&fw_config, NULL);
					loadend_printf("");
				}
				triviaListDestroy(&trivia);
				triviaReleaseDumbMutex(trivia_mutex);
			}
#endif

			if(s_overridetime)
			{
				PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE));
			}


// 		xcase GIMME_LABEL:

		xdefault:
			FatalErrorf("Unsupported Gimme Operation %d", op);
	}

	pclFlush(client); // GGFIXME: is this necessary?

	if(!s_queue_checkins)		//make sure this client doesn't stay in the queue for any subsequent queued operation
	{
		gimmeClientDisconnect(&gc);
	}

	eaDestroyEx(&file_paths, NULL);
	eaDestroy(&paired_names);
	eaDestroyEx(&paired_paths, NULL);
	eaiDestroy(&paired_recurse);

	if(ret)
		*ret = s_error;
	s_didwork = true;
	
	return true;
}

bool patchmeDoOperation(const char *fullpath,
						GIMMEOperation op,
						GimmeQuietBits quiet,
						bool is_single_file,
						GimmeErrorValue *ret)
{
	if (strchr(fullpath, ';')) {
		// Multiple files
		char **files=NULL;
		char *context=NULL;
		const char *tok;
		size_t temp_size = strlen(fullpath)+1;
		char *temp = ScratchAlloc(temp_size);
		bool final_ret;
		strcpy_s(SAFESTR2(temp), fullpath);
		tok = strtok_s(temp, ";", &context);
		do {
			FIX_FULL_PATH(tok);
			eaPush(&files, strdup(tok));
		} while (tok = strtok_s(NULL, ";", &context));
		final_ret = patchmeDoOperationEx(files, eaSize(&files), op, quiet, is_single_file, ret);
		eaDestroyEx(&files, NULL);
		ScratchFree(temp);
		return final_ret;
	} else {
		FIX_FULL_PATH(fullpath);
		return patchmeDoOperationEx(&fullpath, 1, op, quiet, is_single_file, ret);
	}
}

// This is used in the undo operation from the context menu in the checkin window.  Because the full checkin operation is still
//   queued, the undo needs to skip the queue.  This function is saving state of all the statics involved in a queued operation,
//   running an operation, and then resetting the statics to return to the queue.
bool patchmeDoUnqueuedOperation(const char *fullpath, GIMMEOperation op, GimmeQuietBits quiet, GimmeErrorValue *ret, bool setDirty)
{
	GimmeErrorValue prev_error = s_error;			//restore error state of previous op
	GIMMEOperation prev_op = s_checkin_op;			//restore op type to not interrupt the queue
	bool prev_queue = s_queue_checkins;				//restore queue state
	bool prev_simulate = g_patchme_simulate;		//simulate is used to diff folders, but needs to be false for this op in order for it to do anything
	bool prev_notestwarn = g_patchme_notestwarn;	//this should be an actionable op, so it should warn for no testing (although this should probably never be a checkin, so this won't do anything)
	bool prev_didwork = s_didwork;					//keep track of current work state of previous op
	bool cur_didwork;								//temp to store work state of the unqueued op for return
	int client_length = eaSize(&s_clients);			//this number is expected not to change by the end of the operation

	if (s_error != GIMME_NO_ERROR)
	{
		patchmelog(PCLLOG_ERROR, "Unqueued operation attempted while in error state %d, aborting unqueued operation", s_error);
		*ret = s_error;
		return false;
	}

	s_queue_checkins = false;
	g_patchme_simulate = false;
	g_patchme_notestwarn = false;
	s_didwork = false;

	patchmelog(PCLLOG_INFO, "-- Starting unqueued operation type %d", op);
	*ret = GimmeDoOperation(fullpath, op, quiet);
	patchmelog(PCLLOG_INFO, "-- Finished unqueued operation type %d with error code %d", op, *ret);

	s_checkin_op = prev_op;
	s_queue_checkins = prev_queue;
	g_patchme_simulate = prev_simulate;
	g_patchme_notestwarn = prev_notestwarn;

	cur_didwork = s_didwork;
	s_didwork = prev_didwork;

	if (s_error != GIMME_NO_ERROR)
	{
		patchmelog(PCLLOG_ERROR, "Unqueued operation exited with error %d, forwarding error on to normal operation");
	}

	if (eaSize(&s_clients) != client_length)
	{
		patchmelog(PCLLOG_ERROR, "Unexpected client list length after unqueued operation.");
		setDirty = true;
		s_error = (s_error == GIMME_NO_ERROR) ? GIMME_ERROR_UNKNOWN : s_error;
		*ret = s_error;
	}

	// If this operation may interfere with the validity of the manifest of other clients, make sure 
	// to mark them as dirty.
	if (setDirty)
	{
		int i;
		for (i = 0; i < eaSize(&s_clients); ++i)
			s_clients[i]->dirty = true;
	}

	return cur_didwork;
}

// Set the default checkin comment, when doing a GIMME_CHECKIN with GIMME_QUIET.
void patchmeSetDefaultCheckinComment(const char *comment)
{
	SAFE_FREE(s_defaultcomment);
	s_defaultcomment = strdup(comment);
}

void patchmeGetSpecificVersion(const char *fullpath, int branch, int revision)
{
	int old_overridebranch = s_overridebranch;
	int old_overriderev = s_overriderev;
	s_overridebranch = branch;
	s_overriderev = revision;
	patchmeDoOperation(fullpath, GIMME_GLV, 0, true, NULL);
	s_overridebranch = old_overridebranch;
	s_overriderev = old_overriderev;
}

void patchmeGetSpecificVersionTo(const char *fullpath, const char *destpath, int branch, int revision)
{
	FIX_FULL_PATH(fullpath);
	char *dbname;
	PCL_Client *client;
	int old_overridebranch = s_overridebranch;
	int old_overriderev = s_overriderev;
	s_overridebranch = branch;
	s_overriderev = revision;
	client = setViewToFile(fullpath, &dbname, NULL, false, false, false);
	if(client)
	{
		PCL_DO_WAIT(pclGetFileTo(client, dbname, destpath, NULL, 0, NULL, NULL));
	}
	s_overridebranch = old_overridebranch;
	s_overriderev = old_overriderev;
}

bool patchmeQueryIsFileLocked(const char *fullpath, const char **ret)
{
	const char *dbname;
	const char *author_name;
	PCL_Client *client;
	
	// If the path is the same as the last time we called this and it has been less than 5 seconds, return the cached value.
	if (gimme_state.is_file_locked_cached_path[0] && (timerCpuSeconds() - gimme_state.is_file_locked_cached_time) < 5 && stricmp(fullpath, gimme_state.is_file_locked_cached_path)==0) {
		*ret = gimme_state.is_file_locked_cached_result[0]?gimme_state.is_file_locked_cached_result:NULL;
		return true;
	}

	client = setViewToFile(fullpath, &dbname, NULL, false, gimme_force_manifest, false);
	if(!client) {
		*ret = GIMME_GLA_ERROR_NO_SC;
		return false;
	}

	if (client->db && gimme_force_manifest && pclGetLockAuthorUnsafe(client, dbname, ret)==PCL_SUCCESS)
	{
		return true;
	}

	if(PCL_DO_WAIT(pclGetLockAuthorQuery(client, dbname, &author_name)))
	{
		*ret = author_name;

		gimme_state.is_file_locked_cached_time = timerCpuSeconds();
		strcpy(gimme_state.is_file_locked_cached_path, fullpath);
		strcpy(gimme_state.is_file_locked_cached_result, *ret?*ret:"");

		return true;
	}

	return false;
/*
	// If we had the manifest locally already, we could do this:

	if (!PCL_DO(pclGetLockAuthorUnsafe(client, dbname, ret))) {
		*ret = GIMME_GLA_ERROR_NO_SC;
		return true;
	}
	return true;

	*/
}

void patchmeShutdownServer(void)
{
	PCL_Client *client;
	client = setViewToFile("NoRoot", NULL, NULL, false, false, false);
	if (!client)
		return;
	PCL_DO_WAIT(pclShutdown(client));
}

bool patchmeQueryLastAuthor(const char *fullpath, const char **ret)
{
	FIX_FULL_PATH(fullpath);
	
	const char *last_author;
	PatchServerLastAuthorResponse response;
	const char *dbname;
	PCL_Client *client;

	if(!fullpath)
	{
		*ret = GIMME_GLA_ERROR;
		return true;
	}

	if (gimme_state.last_author_cached_result[0] &&
		(timerCpuSeconds() - gimme_state.last_author_cached_time) < 5 &&
		stricmp(fullpath, gimme_state.last_author_cached_path)==0)
	{
		*ret = gimme_state.last_author_cached_result;
		return true;
	}

	client = setViewToFile(fullpath, &dbname, NULL, false, gimme_force_manifest, false);
	if(!client)
	{
		*ret = GIMME_GLA_ERROR_NO_SC "(no conn)" ;
		return false;
	}

	if(client->db && gimme_force_manifest && pclGetAuthorUnsafe(client, dbname, ret)==PCL_SUCCESS)
	{
		return true;
	}

	if(PCL_DO_WAIT(pclGetAuthorQuery(client, dbname, fullpath, &response, &last_author)))
	{
		switch (response) {
			xcase LASTAUTHOR_GOT_AUTHOR:
			acase LASTAUTHOR_ERROR:
				*ret = last_author;
			xcase LASTAUTHOR_NOT_IN_DATABASE:
				*ret = GIMME_GLA_ERROR;
			xcase LASTAUTHOR_CHECKEDOUT:
				*ret = GIMME_GLA_CHECKEDOUT;
			xcase LASTAUTHOR_NOT_LATEST:
				*ret = GIMME_GLA_NOT_LATEST;
			xdefault: // Shouldn't ever happen
				assert(0);
				*ret = GIMME_GLA_ERROR_NO_SC;
				return false;
		}

		gimme_state.last_author_cached_time = timerCpuSeconds();
		strcpy(gimme_state.last_author_cached_path, fullpath);
		strcpy(gimme_state.last_author_cached_result, *ret?*ret:"");

		return true;
	}

	/*
	// If we had the manifest locally already, we could do this:
		if(	PCL_DO(pclIsUnderSourceControl(client, dbname, &btemp)) &&
		!btemp)
	{
		*ret = GIMME_GLA_ERROR_NO_SC;
		return true;
	}

	if(	PCL_DO(pclExistsInDb(client, dbname, &btemp)) &&
		!btemp)
	{
		*ret = GIMME_GLA_ERROR;
		return true;
	}

	if(	PCL_DO(pclGetLockAuthorUnsafe(client, dbname, &locked_by)) &&
		locked_by &&
		!stricmp(locked_by, getAuthor()))
	{
		*ret = GIMME_GLA_CHECKEDOUT;
		return true;
	}

	if(	PCL_DO(pclIsFileUpToDate(client, dbname, &btemp)) &&
		!btemp)
	{
		*ret = GIMME_GLA_NOT_LATEST;
		return true;
	}

	if(	PCL_DO(pclIsDeleted(client, dbname, &btemp)) &&
		btemp)
	{
		*ret = GIMME_GLA_ERROR;
		return true;
	}

	if(PCL_DO_WAIT(pclGetAuthorUnsafe(client, dbname, &last_author)))
	{
		*ret = last_author;
		return true;
	}

	*/

	// Default error return if all else fails
	*ret = GIMME_GLA_ERROR_NO_SC;
	return true;
}

bool patchmeQueryStat(const char *fullpath, bool graphical)
{
	const char *dbname;
	PCL_Client *client;
	bool bSuccess=false;
	bool bRepeat=false;
	bool saved_queue_checkins = s_queue_checkins;

	if(!fullpath)
		return true;

	s_queue_checkins = false;

	do
	{
		PatcherFileHistory history = {0};

		client = setViewToFile(fullpath, &dbname, NULL, false, false, false);
		if(!client)
			return false;

		bRepeat = false;

		if(PCL_DO_WAIT(pclFileHistory(client, dbname, NULL, NULL, &history)))
		{
			// Got data
			bSuccess = true;

			// Don't use cached file lock values, as this could have just changed the lock status
			gimme_state.is_file_locked_cached_time = 0;

			if (patchmeStatShow(fullpath, graphical, &history))
				bRepeat = true;

			if (!graphical)
				s_pause = 1;
		}

		StructDeInit(parse_PatcherFileHistory, &history);
	} while (bRepeat);

	s_queue_checkins = saved_queue_checkins;
	return bSuccess;
}

int patchmeQueryAvailable()
{
	int i;
	GimmeClient gc = {0};

	for(i = 0; i < eaSize(&s_clients); i++)
		if(s_clients[i]->client)
			return 1;

	getConnected(&gc);
	if(gc.client)
	{
		patchMeDisconnectSafe(&gc.client);
		return 1;
	}
	return 0;
}

bool patchmeQueryBranchName(const char *localpath, const char **ret)
{
	char			root[MAX_PATH];
	TriviaList*		trivia = triviaListGetPatchTriviaForFile(localpath, SAFESTR(root));
	bool bRet = false;
	*ret = NULL;

	if (trivia)
	{
		const char *s;
		if (s = triviaListGetValue(trivia, "PatchBranchName")) {
			*ret = allocAddString(s);
			bRet = true;
		} else {
			BranchCached *cached;

			PCL_Client *client = setViewToFile(localpath, NULL, NULL, false, false, false);
			if(!client) {
				// Folder is controlled by patchme, but failed to connect
				*ret = GIMME_GLA_ERROR_NO_SC;
				bRet = true;
			} else {

				cached = getBranchInfo(client, -1);
				if (cached) {
					char triviapath[MAX_PATH];
					*ret = cached->name;
					bRet = true;
					triviaListPrintf(trivia, "PatchBranchName", "%s", cached->name);
					sprintf(triviapath, "%s/%s/%s", root, PATCH_DIR, TRIVIAFILE_PATCH);
					// Assert to make sure we're not writing a trivia file with a core mapping into a non-core folder
					assert(!triviaListGetValue(trivia, "PatchProject") || !strstri(triviapath, "Core") == !strstri(triviaListGetValue(trivia, "PatchProject"), "Core"));
					triviaListWritePatchTriviaToFile(trivia, triviapath);
				} else {
					// Folder is controlled by patchme, but failed to connect
					*ret = GIMME_GLA_ERROR_NO_SC;
					bRet = true;
				}
			}
		}
	}
	triviaListDestroy(&trivia);
	return bRet;
}

bool patchmeQueryBranchNumber(const char *localpath, int *ret)
{
	char buf[100];
	if( !triviaGetPatchTriviaForFile(SAFESTR(buf), localpath, "PatchBranch") &&
		( !setViewToFile(localpath, NULL, NULL, false, false, false) ||
		  !triviaGetPatchTriviaForFile(SAFESTR(buf), localpath, "PatchBranch") ) )
		return false;

	*ret = atoi(buf);
	return true;
}

bool patchmeQueryCoreBranchNumForDir(const char *localpath, int *ret)
{
	char buf[100];
	if( !triviaGetPatchTriviaForFile(SAFESTR(buf), localpath, "CoreBranchMapping") &&
		( !setViewToFile(localpath, NULL, NULL, false, false, false) ||
		!triviaGetPatchTriviaForFile(SAFESTR(buf), localpath, "CoreBranchMapping") ) )
		return false;

	*ret = atoi(buf);
	return true;
}

void patchmeDiffFile(const char *fullpath)
{
	FIX_FULL_PATH(fullpath);
	char *dbname;
	PCL_Client *client = setViewToFile(fullpath, &dbname, NULL, false, true, false);
	if(client)
	{
		diffCurrent(client, fullpath, dbname);
		s_didwork = true;
	}
}


AUTO_COMMAND ACMD_NAME(doDelayPause);
void cmd_dodelaypause(int i)
{
	if(gimmeGetOption("DelayPause"))
	{
		s_pause = 1;
		gimmeSetOption("DelayPause", 0);
	}
}

int gimmeGetBranchNumber(const char *localpath);
static void setBranchIfHigher(int *branch, char *root, char *dir)
{
	char gimmedir[MAX_PATH];
	int gimmebranch;
	sprintf(gimmedir, "%s/%s", root, dir);
	gimmebranch = gimmeGetBranchNumber(gimmedir);
	if(gimmebranch)
	{
		patchmelog(PCLLOG_INFO, "%s at branch %d", gimmedir, gimmebranch);
		MAX1(*branch, gimmebranch);
	}
}

static bool moveCoreDir(char *root, char *coreroot, char *dir)
{
	char olddir[MAX_PATH], newdir[MAX_PATH];
	sprintf(olddir, "%s/Core%s", root, dir);
	if(dirExists(olddir))
	{
		sprintf(newdir, "%s/%s", coreroot, dir);
		patchmelog(PCLLOG_INFO, "Moving %s to %s", olddir, newdir);
		makeDirectories(coreroot);
		assert(fileMove(olddir, newdir) == 0);
		return true;
	}
	return false;
}

static void stampTrivia(char *root, char *project, char *usertype, int branch)
{
	char fullproject[MAX_PATH], triviapath[MAX_PATH], patch_dir[MAX_PATH];
	TriviaList trivia = {0};

	sprintf(patch_dir, "%s/%s", root, PATCH_DIR);
	makeDirectories(patch_dir);

	if(usertype)
		sprintf(fullproject, "%sGimme%s", project, usertype);
	else
		strcpy(fullproject, project);
	sprintf(triviapath, "%s/%s/%s", root, PATCH_DIR, TRIVIAFILE_PATCH);
	patchmelog(PCLLOG_INFO, "Setting %s to project %s, branch %d", root, fullproject, branch);
	// GGFIXME: wrap this in a mutex
	triviaListPrintf(&trivia, "PatchProject", "%s", fullproject);
	triviaListPrintf(&trivia, "PatchBranch", "%d", branch);

	// Assert to make sure we're not writing a trivia file with a core mapping into a non-core folder
	assert(!triviaListGetValue(&trivia, "PatchProject") || !strstri(triviapath, "Core") == !strstri(triviaListGetValue(&trivia, "PatchProject"), "Core"));

	triviaListWritePatchTriviaToFile(&trivia, triviapath);
	triviaListClear(&trivia);
}

void patcherizeDir(char *project, char *suffix, char *usertype, bool coreonly)
{
	char root[MAX_PATH], coreroot[MAX_PATH];
	TriviaList *trivia;

	sprintf(root, "C:/%s%s", project, suffix);
	if(!dirExists(root))
		return;

	if(!coreonly)
	{
		int branch = 0;
		trivia = triviaListGetPatchTriviaForFile(root, NULL, 0);
		if(trivia)
		{
			branch = atoi(triviaListGetValue(trivia, "PatchBranch"));
			triviaListDestroy(&trivia);
			patchmelog(PCLLOG_INFO, "%s at branch %d", root, branch);
		}
		else
		{
			setBranchIfHigher(&branch, root, "data");
			setBranchIfHigher(&branch, root, "src");
			setBranchIfHigher(&branch, root, "tools");
		}
		stampTrivia(root, stricmp(suffix, "Core") == 0 ? suffix : project, usertype, branch);
	}

	if(stricmp(project, "Core") != 0 && stricmp(suffix, "Core") != 0)
	{
		int branch = 0;
		bool moved_core = false;
		sprintf(coreroot, "%sCore", root);
		trivia = triviaListGetPatchTriviaForFile(coreroot, NULL, 0);
		if(trivia)
		{
			branch = atoi(triviaListGetValue(trivia, "PatchBranch"));
			triviaListDestroy(&trivia);
			patchmelog(PCLLOG_INFO, "%s at branch %d", coreroot, branch);
			moved_core = true;
		}
		else
		{
			setBranchIfHigher(&branch, root, "coredata");
			setBranchIfHigher(&branch, root, "coresrc");
			setBranchIfHigher(&branch, root, "coretools");
		}
		moved_core |= moveCoreDir(root, coreroot, "data");
		moved_core |= moveCoreDir(root, coreroot, "src");
		moved_core |= moveCoreDir(root, coreroot, "tools");
		if(moved_core)
			stampTrivia(coreroot, "Core", usertype, branch);
	}
}

AUTO_COMMAND ACMD_NAME(patcherize);
void cmd_patcherize(char *usertype)
{
	s_didwork = true;
#if 0
	patcherizeDir("Core", "", usertype, false);
	patcherizeDir("FightClub", "Core", usertype, false);
	patcherizeDir("FightClub", "", usertype, false);
	patcherizeDir("FightClubFix", "Core", usertype, false);
	patcherizeDir("FightClub", "Fix", usertype, false);
	patcherizeDir("PrimalAge", "Core", usertype, false);
	patcherizeDir("PrimalAge", "", usertype, false);
#endif
	patcherizeDir("Cryptic", "", NULL, false);
}

// Alias for patcherize, to avoid running the old patcherize.
AUTO_COMMAND ACMD_NAME(patcherize2);
void cmd_patcherize2(char *usertype)
{
	cmd_patcherize(usertype);
}

AUTO_COMMAND ACMD_NAME(setproject);
void cmd_setproject(char *root, char *project)
{
	char foundroot[MAX_PATH];
	TriviaList *trivia;
	forwardSlashes(root);
	if(root[0] && root[strlen(root)-1] == '/')
		root[strlen(root)-1] = '\0';
	trivia = triviaListGetPatchTriviaForFile(root, SAFESTR(foundroot));
	if(trivia)
	{
		if(stricmp(root, foundroot) == 0)
		{
			const char *branch_str = triviaListGetValue(trivia, "PatchBranch");
			int branch = branch_str ? atoi(branch_str) : 0;
			stampTrivia(root, project, NULL, branch);
		}
		else
		{
			patchmelog(PCLLOG_ERROR, "%s is not a gimme root directory", root);
		}

		triviaListDestroy(&trivia);
		s_didwork = true;
	}
}

char **g_install_overlays = NULL;
AUTO_COMMAND ACMD_NAME(installoverlay);
void cmd_installoverlay(char *root)
{
	eaPush(&g_install_overlays, strdup(root));
}

AUTO_COMMAND ACMD_NAME(installproject, install);
void cmd_installproject(const char *root, const char *project, const char *branch)
{
	TriviaList* trivia;
	char		triviaRoot[MAX_PATH], normalizedRoot[MAX_PATH], *normalizedRootTmp = normalizedRoot;
	S32			doInstall = 1;
	
	s_didwork = true;
	strcpy(normalizedRoot, root);
	forwardSlashes(normalizedRoot);
	while(strEndsWith(normalizedRoot, "/"))
	{
		normalizedRoot[strlen(normalizedRoot)-1] = '\0';
	}
	
	// Check if "root" is in an existing patch folder.
	
	trivia = triviaListGetPatchTriviaForFile(root, SAFESTR(triviaRoot));

	if(	trivia &&
		stricmp(triviaRoot, normalizedRoot))
	{
		patchmelog(PCLLOG_ERROR,
					"You can't install gimme to \"%s\", it is already installed to \"%s\"!",
					normalizedRoot,
					triviaRoot);
					
		doInstall = 0;
	}

	triviaListDestroy(&trivia);
	
	// Continue with the install.

	if(doInstall)
	{
		GimmeClient gc = {0};
		strcpy(gc.root, normalizedRoot);
		gc.regReader = createRegReader();
		initRegReader(gc.regReader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
		gcLoadLocalFileSpec(&gc);
		getConnected(&gc);
		if(gc.client)
		{
			int recurse = 1;
			PCL_Client *client = gc.client;
			FOR_EACH_IN_EARRAY(g_install_overlays, char, overlay)
				PCL_DO(pclAddExtraFolder(client, overlay, HOG_READONLY|HOG_NOCREATE));
			FOR_EACH_END
			makeDirectories(normalizedRoot);
			PCL_DO(pclResetRoot(client, gc.root));
			PCL_DO_WAIT(pclSetViewLatest(client, project, stricmp(branch, "tip") == 0 ? INT_MAX : atoi(branch), NULL, true, true, NULL, NULL));
			PCL_DO(pclSetHoggsSingleAppMode(client, false));
			PCL_DO_WAIT(pclGetFileList(client, &normalizedRootTmp, &recurse, false, 1, NULL, NULL, &gc.fileSpec));
			PCL_DO(patchMeDisconnectSafe(&client));
		}
		else
		{
			patchmelog(PCLLOG_ERROR, "Could not connect to gimme server %s:%d!", GIMME_SERVER_AND_PORT);
		}
	}
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(installfix);
void cmd_installfix(const char *root, char *original, const char *branch)
{
	TriviaList* trivia;
	char		triviaRoot[MAX_PATH];

	trivia = triviaListGetPatchTriviaForFile(original, SAFESTR(triviaRoot));

	if(trivia)
	{
		const char *project = triviaListGetValue(trivia, "PatchProject");
		if(project)
		{
			cmd_installoverlay(original);
			cmd_installproject(root, project, branch);
		}
	}

	triviaListDestroy(&trivia);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(whoami);
void cmd_whoami(void)
{
	patchmelog(PCLLOG_ERROR, "Gimme User Name : %s", getAuthor());
	s_pause = 1;
//	s_didwork = true;				// not *real* work, we may need to defer to gimme
}

AUTO_COMMAND ACMD_NAME(getbydate);
void cmd_getbydate(const char *datestr, const char *fullpath)
{
	
	if(strcmpi(datestr, "dialog")==0)
	{
		bool sync_core;
		s_overridetime = patchmeDialogGetDate(fullpath, &sync_core);
		if(!s_overridetime)
		{
			s_didwork = true;
			return;
		}
		if(sync_core)
		{
			char *core_folder;
			if(strEndsWith(fullpath, "fix"))
				core_folder = strdupf("%sCore", fullpath);
			else
				core_folder = strdup("C:/Core");
			patchmeDoOperation(core_folder, GIMME_GLV, 0, false, NULL);
			free(core_folder);
		}
	}
	else
		s_overridetime = parseDate(datestr);
	patchmeDoOperation(fullpath, GIMME_GLV, 0, false, NULL);
	s_overridetime = 0;
	s_doStartFileWatcher = true;
}

// Same as -getbydate, but take a seconds-since-2000 number. Added for use in the incremental builders
AUTO_COMMAND ACMD_NAME(getbydateSS2000);
void cmd_getbydatess2000(U32 ss200, const char *fullpath)
{
	s_overridetime = patchSS2000ToFileTime(ss200);
	patchmeDoOperation(fullpath, GIMME_GLV, 0, false, NULL);
	s_overridetime = 0;
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(matchdate);
void cmd_matchdate(const char *matchpath, const char *fullpath)
{
	char *tmp, *glvlog = fileAlloc(STACK_SPRINTF("%s/" PATCH_DIR "/glv.log", matchpath), NULL);
	if(!glvlog)
	{
		patchmelog(PCLLOG_ERROR, "Cannot find glv log to match");
		s_didwork = true;
		return;
	}
	s_overridetime = strtol(glvlog, &tmp, 10);
	if(glvlog == tmp)
	{
		patchmelog(PCLLOG_ERROR, "No time to match in glv log");
		s_didwork = true;
		free(glvlog);
		return;
	}
	free(glvlog);
	patchmeDoOperation(fullpath, GIMME_GLV, 0, false, NULL);
	s_overridetime = 0;
}

static U32 getbyrev_branch, getbyrev_rev;
static void getbyrevCallback(PCL_Client* client, U32 rev, U32 branch, U32 time, char *sandbox, U32 incr_from, char *author, char *comment, char **files, void *userdata)
{
	getbyrev_rev = rev;
	getbyrev_branch = branch;
}

// Do a get-latest using the branch/rev/(sandbox?) from a given rev.
AUTO_COMMAND ACMD_NAME(getbyrev);
void cmd_getbyrev(U32 rev, const char *fullpath)
{
	int old_overridebranch = s_overridebranch;
	int old_overriderev = s_overriderev;

	PCL_Client *client = setViewToFile(fullpath, NULL, NULL, false, false, false);
	assertmsgf(client, "Can't create view for %s. Did you remember to use an absolute path?", fullpath);

	PCL_DO_WAIT(pclGetCheckinInfo(client, rev, getbyrevCallback, NULL));

	s_overridebranch = getbyrev_branch;
	s_overriderev = getbyrev_rev;
	patchmeDoOperation(fullpath, GIMME_GLV, 0, false, NULL);
	s_overridebranch = old_overridebranch;
	s_overriderev = old_overriderev;

	s_doStartFileWatcher = true;
}

static bool mergerev_checkrev = false;
AUTO_CMD_INT(mergerev_checkrev, checkrev);

static char **mergerev_files = NULL;

static void mergerevCallback(PCL_Client* client, U32 rev, U32 branch, U32 time, char *sandbox, U32 incr_from, char *author, char *comment, char **files, void *userdata)
{
	getbyrev_rev = rev;
	getbyrev_branch = branch;

	FOR_EACH_IN_EARRAY(files, char, file)
	{
		char *fullpath = NULL;
		estrPrintf(&fullpath, "%s/%s", client->root_folder, file);
		eaPush(&mergerev_files, fullpath);
	}
	FOR_EACH_END
}

// Get all files in a specific revision. NOTE: Does no actual file-level merging and never will.
AUTO_COMMAND ACMD_NAME(mergerev);
void cmd_mergerev(U32 rev, const char *fullpath)
{
	int old_overridebranch = s_overridebranch;
	int old_overriderev = s_overriderev;
	bool rev_good = true;
	PatcherFileHistory *file_history;

	PCL_Client *client = setViewToFile(fullpath, NULL, NULL, false, false, false);
	assert(client);

	PCL_DO_WAIT(pclGetCheckinInfo(client, rev, mergerevCallback, NULL));

	if(mergerev_checkrev)
	{
		char *dbname;
		U32 file_checksum, *possible_missed_checkins=NULL;
		Checkin *file_checkin;
		loadstart_printf("Checking revision dependencies...");
		
		FOR_EACH_IN_EARRAY(mergerev_files, char, file)
		{
			dbname = file + strlen(client->root_folder) + 1;
			file_history = StructCreate(parse_PatcherFileHistory);

			PCL_DO_WAIT(pclFileHistory(client, dbname, NULL, NULL, file_history));
			if(!file_history->dir_entry)
				continue;
			file_checksum = patchChecksumFile(file);
			if(!file_checksum)
				continue;
			FOR_EACH_IN_EARRAY(file_history->dir_entry->versions, FileVersion, ver)
				Checkin *checkin = file_history->checkins[FOR_EACH_IDX(file_history->dir_entry->versions, ver)];
				if(ver->checksum == file_checksum && checkin->branch <= (int)getbyrev_branch && checkin->rev < (int)getbyrev_rev)
				{
					file_checkin = checkin;
					break;
				}
			FOR_EACH_END
			FOR_EACH_IN_EARRAY(file_history->dir_entry->versions, FileVersion, ver)
				Checkin *checkin = file_history->checkins[FOR_EACH_IDX(file_history->dir_entry->versions, ver)];
				if(checkin->branch <= (int)getbyrev_branch && checkin->rev < (int)getbyrev_rev && checkin->rev > file_checkin->rev)
					eaiPushUnique(&possible_missed_checkins, checkin->rev);
			FOR_EACH_END

			StructDestroy(parse_PatcherFileHistory, file_history);
		}
		FOR_EACH_END

		if(eaiSize(&possible_missed_checkins) > 0)
			rev_good = false;
		
		if(rev_good)
			loadend_printf("pass");
		else
		{
			int i;
			loadend_printf("fail");
			s_didwork = true;
			s_error = GIMME_ERROR_CANCELED;
			eaiQSortG(possible_missed_checkins, cmpU32);
			printfColor(COLOR_RED|COLOR_BRIGHT, "Possible missed checkins: %d", possible_missed_checkins[0]);
			for(i=1; i<eaiSize(&possible_missed_checkins); i++)
				printfColor(COLOR_RED|COLOR_BRIGHT, ", %d", possible_missed_checkins[i]);
			printf("\n");
		}
		eaiDestroy(&possible_missed_checkins);
	}

	// Skip files which are not included in this project.
	EARRAY_FOREACH_REVERSE_BEGIN(mergerev_files, i);
	{
		bool is_included = false;
		char *file = mergerev_files[i];
		const char *dbname = file + strlen(client->root_folder) + 1;
		pclIsIncluded(client, dbname, &is_included);
		if (!is_included)
		{
			estrDestroy(&file);
			eaRemoveFast(&mergerev_files, i);
		}
	}
	EARRAY_FOREACH_END;

	if(rev_good)
	{
		if(gimme_verbose)
		{
			filelog_printf("patchxfer", "cmd_mergerev: branch=%d rev=%u nfiles=%u", getbyrev_branch, getbyrev_rev, eaSize(&mergerev_files));
			FOR_EACH_IN_EARRAY(mergerev_files, char, file)
				filelog_printf("patchxfer", "cmd_mergerev:   file=%s", file);
			FOR_EACH_END
		}

		s_overridebranch = getbyrev_branch;
		s_overriderev = getbyrev_rev;
		patchmeDoOperationEx(mergerev_files, eaSize(&mergerev_files), GIMME_GLV, 0, true, NULL);
		s_overridebranch = old_overridebranch;
		s_overriderev = old_overriderev;
	}

	FOR_EACH_IN_EARRAY(mergerev_files, char, file)
		estrDestroy(&file);
	FOR_EACH_END
	eaDestroy(&mergerev_files);
}

AUTO_COMMAND ACMD_NAME(glvignorewriteable);
void cmd_glvignorewriteable(const char *fullpath)
{
	FIX_FULL_PATH(fullpath);
	GimmeClient* gc;
	PCL_Client *client = setViewToFile(fullpath, NULL, &gc, false, true, false);
	if(client)
	{
		int recurse = 1;
		PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE & ~PCL_BACKUP_WRITEABLES));
		PCL_DO_WAIT(pclGetFileList(client, &fullpath, &recurse, false, 1, NULL, NULL, &gc->fileSpec));
		PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE));
		s_didwork = true;
	}
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(glvfile);
void cmd_glvfile(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_GLV, 0, true, NULL);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(glvfold);
void cmd_glvfold(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_GLV, 0, false, NULL);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(editor);
void cmd_checkout_editor(char *editor, const char *fullpath)
{
	if (editor && stricmp(editor, "null")==0) {
		s_launchEditor = false;
	} else {
		s_launchEditor = true;
		s_editor = editor;
	}
	patchmeDoOperation(fullpath, GIMME_CHECKOUT, 0, true, NULL);
	s_launchEditor = false;
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(checkout);
void cmd_checkout(const char *fullpath)
{
	cmd_checkout_editor(NULL, fullpath);
}

AUTO_COMMAND ACMD_NAME(checkoutfold);
void cmd_checkoutfold(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_CHECKOUT, 0, false, NULL);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(remove);
void cmd_remove(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_DELETE, 0, true, NULL);
}

AUTO_COMMAND ACMD_NAME(rmfold);
void cmd_rmfold(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_DELETE, 0, false, NULL);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(put, checkin);
void cmd_checkin(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_CHECKIN, 0, true, NULL);
}

AUTO_COMMAND ACMD_NAME(checkinfold);
void cmd_checkinfold(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_CHECKIN, 0, false, NULL);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(forceput);
void cmd_forcein(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_FORCECHECKIN, 0, true, NULL);
}

AUTO_COMMAND ACMD_NAME(forceputfold);
void cmd_forceinfold(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_FORCECHECKIN, 0, false, NULL);
	s_doStartFileWatcher = true;
}

AUTO_COMMAND ACMD_NAME(undo);
void cmd_undo(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_UNDO_CHECKOUT, 0, true, NULL);
}

AUTO_COMMAND ACMD_NAME(undofold);
void cmd_undofold(const char *fullpath)
{
	patchmeDoOperation(fullpath, GIMME_UNDO_CHECKOUT, 0, false, NULL);
}

AUTO_COMMAND ACMD_NAME(diff);
void cmd_diff(const char *fullpath)
{
	patchmeDiffFile(fullpath);
}

AUTO_COMMAND ACMD_NAME(webstat);
void cmd_webstat(const char *fullpath)
{
	FIX_FULL_PATH(fullpath);
	char root[MAX_PATH];
	TriviaList *trivia = triviaListGetPatchTriviaForFile(fullpath, SAFESTR(root));
	if(trivia)
	{
		char url[MAX_PATH];
		const char *project = triviaListGetValue(trivia,"PatchProject");
		assert(strStartsWith(fullpath, root));
		sprintf(url, "http://%s/%s/file%s/", GIMME_SERVER, project, fullpath + strlen(root));
		openURL(url);
		triviaListDestroy(&trivia);

		s_didwork = true;
	}
}

AUTO_COMMAND ACMD_NAME(cstat);
void cmd_cstat(const char *fullpath)
{
	FIX_FULL_PATH(fullpath);
	if (patchmeQueryStat(fullpath, false)) {
		s_didwork = true;
	} else {
		cmd_webstat(fullpath); // Perhaps server which does not support the stat query
	}
}

AUTO_COMMAND ACMD_NAME(stat);
void cmd_stat(const char *fullpath)
{
	FIX_FULL_PATH(fullpath);
#ifdef _XBOX
	cmd_cstat(fullpath);
#else
	if (patchmeQueryStat(fullpath, true)) {
		s_didwork = true;
	} else {
		cmd_webstat(fullpath); // Perhaps server which does not support the stat query
	}
#endif
}

AUTO_COMMAND ACMD_NAME(author);
void cmd_author(const char* fullpath)
{
	FIX_FULL_PATH(fullpath);
	const char* author = "no error returned";
	
	if(patchmeQueryLastAuthor(fullpath, &author)){
		printf("File: %s\n", fullpath);
		printf("Author: %s\n", author);
		s_didwork = true;
	}else{
		printf("File: %s\n", fullpath);
		printf("Failed to get last author: %s!\n", author);
	}
}

AUTO_COMMAND ACMD_NAME(islocked);
void cmd_islocked(const char* fullpath)
{
	FIX_FULL_PATH(fullpath);
	const char* author;

	if(patchmeQueryIsFileLocked(fullpath, &author)){
		printf("File: %s\n", fullpath);
		printf("Checked out by: %s\n", author);
	}else{
		printf("File: %s\n", fullpath);
		printf("Failed query!\n");
	}

	s_didwork = true;
}

// for testing, feel free to remove
AUTO_COMMAND ACMD_NAME(branchName);
void cmd_branchName(const char* fullpath)
{
	FIX_FULL_PATH(fullpath);
	const char* ret;

	if(patchmeQueryBranchName(fullpath, &ret)){
		printf("File: %s\n", fullpath);
		printf("Branch Name: %s\n", ret);
	}else{
		printf("File: %s\n", fullpath);
		printf("Failed to query branch name!\n");
	}

	s_didwork = true;
}

// for testing, feel free to remove
AUTO_COMMAND ACMD_NAME(branchNumber);
void cmd_branchNumber(const char* fullpath)
{
	FIX_FULL_PATH(fullpath);
	int ret;

	if(patchmeQueryBranchNumber(fullpath, &ret)){
		printf("File: %s\n", fullpath);
		printf("Branch Number: %d\n", ret);
	}else{
		printf("File: %s\n", fullpath);
		printf("Failed to query branch number!\n");
	}

	s_didwork = true;
}

AUTO_COMMAND ACMD_NAME(shutdown);
void cmd_shutdown(int dummy)
{
	patchmeShutdownServer();
	s_didwork = true;
}

AUTO_COMMAND ACMD_NAME(undocheckin);
void cmd_undocheckin(U32 rev, const char* fullpath)
{
	FIX_FULL_PATH(fullpath);
	GimmeClient* gc;
	PCL_Client *client = setViewToFile(fullpath, NULL, &gc, false, true, false);

	// Undo checkin.
	PCL_DO_WAIT(pclUndoCheckin(client, *s_defaultcomment ? s_defaultcomment : "Reverting checkin", NULL, NULL));

	s_didwork = true;
}

// Dummy for commands handled by gimmeMain for now
AUTO_COMMAND ACMD_NAME(register);
void cmd_dummy(int dummy)
{
}

static void getCheckinsBetweenTimes(const char* outFileName,
									time_t		timeStart,
									time_t		timeEnd,
									const char* foldersConcatted)
{
	char**		folders = NULL;
	const char*	start = foldersConcatted;
	CheckinList	cl = {0};


	// Split foldersConcatted separated by plus-signs.
	
	while(1){
		const char*	plus = strchr(start, '+');
		char		path[MAX_PATH];
		
		if(plus){
			strncpy(path, start, plus - start);
			start = plus + 1;
			eaPush(&folders, strdup(path));
		}else{
			eaPush(&folders, strdup(start));
			break;
		}
	}
	
	EARRAY_CONST_FOREACH_BEGIN(folders, i, isize);
		PCL_Client* client = setViewToFile(folders[i], NULL, NULL, false, false, false);
		CheckinList	clTemp = {0};
		
		if (!client)
		{
			patchmelog(PCLLOG_ERROR, "Could not find patch folder for : %s", folders[i]);
		} else {
			if(PCL_DO_WAIT(pclGetCheckinsBetweenTimes(client, timeStart, timeEnd, &clTemp)))
			{
				eaPushEArray(	&cl.checkins,
								&clTemp.checkins);
			}
		}
	EARRAY_FOREACH_END;
	
	eaDestroyEx(&folders, NULL);
	
	printf("Writing %d checkins to %s\n", eaSize(&cl.checkins), outFileName);
	
	ParserWriteTextFile(outFileName,
						parse_CheckinList,
						&cl,
						0,
						0);

	s_didwork = true;
}

AUTO_COMMAND ACMD_NAME(getCheckinsBetweenTimes);
void cmd_getCheckinsBetweenTimes(const char* outFileName,
								 const char* timeStartString,
								 const char* timeEndString,
								 const char* foldersConcatted)
{
	time_t timeStart = parseDate(timeStartString);
	time_t   timeEnd = parseDate(timeEndString);

	if(	!timeStart ||
		!timeEnd)
	{
		printf("Can't parse time strings.\n");
		return;
	}

	getCheckinsBetweenTimes(outFileName, timeStart, timeEnd, foldersConcatted);
}

AUTO_COMMAND ACMD_NAME(getCheckinsBetweenTimesSS2000);
void cmd_getCheckinsBetweenTimesSS2000(	const char* outFileName,
										U32 timeStartSS2000,
										U32 timeEndSS2000,
										const char* foldersConcatted)
{
	time_t timeStart = patchSS2000ToFileTime(timeStartSS2000);
	time_t   timeEnd = patchSS2000ToFileTime(timeEndSS2000);

	getCheckinsBetweenTimes(outFileName, timeStart, timeEnd, foldersConcatted);
}

// Userdata for switchbranchCB()
struct switchbranchCB_data
{
	GimmeClient *gc;
	const char *backup_path;
};

static FileScanAction switchbranchCB(char *dir_name, struct _finddata32_t *data, void *userdata)
{
	struct switchbranchCB_data *cbdata = userdata;
	GimmeClient *gc = cbdata->gc;
	const char *backup_path = cbdata->backup_path;

	if(!(data->attrib & _A_SUBDIR))
	{
		PCL_Client *client = gc->client;
		char srcpath[MAX_PATH], destpath[MAX_PATH], *dbname;
		bool exists;
		sprintf(srcpath, "%s/%s", dir_name, data->name);
		dbname = srcpath + strlen(backup_path) + 1;

		PCL_DO(pclExistsInDb(client, dbname, &exists));
		if(!exists)
		{
			sprintf(destpath, "%s/%s", gc->root, dbname);
			forwardSlashes(destpath);
			mkdirtree(destpath);
			backSlashes(srcpath);
			backSlashes(destpath);
			patchmelog(PCLLOG_INFO, "Moving from %s to %s", srcpath, destpath);
			if(fileMove(srcpath, destpath)!=0)
			{
				patchmelog(PCLLOG_ERROR, "Error restoring backing up file (%s)!", destpath);
				s_pause = 1;
				s_error = GIMME_ERROR_COPY;
			}
			rmdirtree(srcpath); // prune the directory
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

AUTO_COMMAND ACMD_NAME(switchbranch);
void cmd_switchbranch(const char *localpath, int newbranch)
{
	char path[MAX_PATH], project[MAX_PATH], sandbox[MAX_PATH], backup_path[MAX_PATH], srcpath[MAX_PATH], destpath[MAX_PATH];
	int oldbranch, i;
	BranchCached *oldinfo, *newinfo;
	char **diffnames = NULL, **checkout_names = NULL;
	PCL_DiffType *difftypes = NULL;
	GimmeClient *gc;
	PCL_Client *client;
	struct switchbranchCB_data cbdata;

	// Set view.
	client = setViewToFile(localpath, NULL, &gc, false, true, false);
	if(!client)
	{
// 		patchmelog(PCLLOG_ERROR, "Error: the root specified (%s) is not a gimme dir.", path);
		s_error = GIMME_ERROR_NODIR;
		return;
	}
	gcLoadLocalFileSpec(gc);

	s_didwork = true;
	finishQueue();

	if(g_patchme_simulate)
	{
		patchmelog(PCLLOG_ERROR, "-simulate and -switchbranch are not supported together");
		s_error = GIMME_ERROR_CANCELED;
		s_pause = 1;
		return;
	}

	strcpy(path, gc->root);

// GGFIXME: implement this
// 	if(newbranch > gimmeGetMaxBranchNumber())
// 	{
// 		patchmelog(PCLLOG_ERROR, "%s: You cannot switch to a branch (%d) newer than the maximum (%d)!", path, newbranch, gimmeGetMaxBranchNumber());
// 		s_error = GIMME_ERROR_CANCELED;
//		s_pause = 1;
// 		return;
// 	}

	PCL_DO(pclGetView(client, SAFESTR(project), NULL, &oldbranch, SAFESTR(sandbox)));
	oldinfo = getBranchInfo(client, oldbranch);
	newinfo = getBranchInfo(client, newbranch);
	if(newbranch == oldbranch)
	{
		patchmelog(PCLLOG_INFO,
					"%s: Already on branch %d (%s)",
					path,
					newbranch,
					SAFE_MEMBER(oldinfo, name));
					
		s_error = GIMME_NO_ERROR;
		return;
	}

	patchmelog(PCLLOG_INFO,
				"%s: Moving from branch %d (%s) to %d (%s) for user %s",
				path,
				oldbranch,
				SAFE_MEMBER(oldinfo, name),
				newbranch,
				SAFE_MEMBER(newinfo, name),
				getAuthor());

	// Get list of modified checked out files, new files, and unchanged checked out files
	PCL_DO(pclDiffFolder(client, "", false, false, false, &diffnames, &difftypes));
	if(eaSize(&diffnames))
	{
		char **undo_names = NULL;

		if(!patchmeDialogConfirm(gc->root, &diffnames, &difftypes))
		{
			patchmelog(PCLLOG_ERROR, "Branch switching canceled by user request");
			s_error = GIMME_ERROR_CANCELED;
			s_pause = 1;
			return;
		}

		for(i = eaSize(&diffnames)-1; i >= 0; --i)
		{
			if(difftypes[i] == PCLDIFF_NOCHANGE)
			{
				eaPush(&undo_names, diffnames[i]);
				eaRemove(&diffnames, i);
				eaiRemove(&difftypes, i);
			}
		}
		if(eaSize(&undo_names))
		{
			patchmelog(PCLLOG_INFO, "Undoing checkouts on unchanged files...");
			PCL_DO_WAIT(pclUnlockFiles(client, undo_names, eaSize(&undo_names), NULL, NULL));
			eaDestroyEx(&undo_names, NULL);
		}

		// Backup checked out files
		patchmelog(PCLLOG_INFO, "Backing up checked out files...");
		sprintf(backup_path, "C:/game/branch.Backups/%d/%s", oldbranch, project);
		for(i = 0; i < eaSize(&diffnames); i++)
		{
			// Move to backup folder
			sprintf(srcpath, "%s/%s", gc->root, diffnames[i]);
			sprintf(destpath, "%s/%s", backup_path, diffnames[i]);
			forwardSlashes(destpath);
			mkdirtree(destpath);
			backSlashes(srcpath);
			backSlashes(destpath);
			patchmelog(PCLLOG_INFO, "Moving from %s to %s", srcpath, destpath);
			// Doing a move here might be more efficient, but if one fails in the middle, I'm afraid to do the cleanup work
			if(fileMove(srcpath, destpath)!=0)
			{
				patchmelog(PCLLOG_ERROR, "Error backing up file (%s), branch switching cannot complete, your local files may be inconsistent!", srcpath);
				s_error = GIMME_ERROR_COPY;
				s_pause = 1;
				eaDestroyEx(&diffnames, NULL);
				eaiDestroy(&difftypes);
				return;
			}
			rmdirtree(srcpath); // prune the directory
		}

		eaDestroyEx(&diffnames, NULL);
		eaiDestroy(&difftypes);
	}

	// get latest with the old branch to back up appropriately, then the new branch with no backups
	{
		char *root = gc->root;
		int recurse = 1;
		PCL_DO_WAIT(pclGetFileList(client, &root, &recurse, false, 1, NULL, NULL, &gc->fileSpec));
		PCL_DO_WAIT(pclSetViewLatest(client, project, newbranch, sandbox, true, true, NULL, NULL));
		PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE & ~(PCL_BACKUP_TIMESTAMP_CHANGES|PCL_BACKUP_WRITEABLES|PCL_KEEP_RECREATED_DELETED)));
		PCL_DO_WAIT(pclGetFileList(client, &root, &recurse, false, 1, NULL, NULL, &gc->fileSpec));
		PCL_DO(pclSetFileFlags(client, PCL_SET_GIMME_STYLE));
		gc->dirty = false;
		gc->created = timeSecondsSince2000();
	}

	// Check for checked out files that were backed up, and new files that were backed up
	// Any checked out file must either be a) restored from backup, or b) forced to get latest
	// Check for local files that are not in the current branch to be removed (this was done above when making backups?)
	// Get list of modified checked out files, new files, and unchanged checked out files

	// Restore checked out files from backup.
	sprintf(backup_path, "C:/game/branch.Backups/%d/%s", newbranch, project);
	PCL_DO(pclGetCheckouts(client, &checkout_names));
	for(i = eaSize(&checkout_names)-1; i >= 0; --i)
	{
		sprintf(srcpath, "%s/%s", backup_path, checkout_names[i]);
		if(fileExists(srcpath))
		{
			// Move from backup folder
			sprintf(destpath, "%s/%s", gc->root, checkout_names[i]);
			forwardSlashes(destpath);
			mkdirtree(destpath);
			backSlashes(srcpath);
			backSlashes(destpath);
			patchmelog(PCLLOG_INFO, "Moving from %s to %s", srcpath, destpath);
			if(fileMove(srcpath, destpath)!=0)
			{
				patchmelog(PCLLOG_ERROR, "Error restoring backing up file (%s)!", destpath);
				s_error = GIMME_ERROR_COPY;
				s_pause = 1;
			}
			rmdirtree(srcpath); // prune the directory

			free(eaRemove(&checkout_names, i));
		}
	}

	// Get Latest on checked out files.
	if(eaSize(&checkout_names))
	{
		// These files are checked out, but don't exist in the backups/ folder, so we want to 
		// force a get latest version, since the getLatestVersionFolder won't have overwritten
		// them (because they're checked out)
		PCL_DO_WAIT(pclForceGetFiles(client, checkout_names, eaSize(&checkout_names), NULL, NULL));
	}
	eaDestroyEx(&checkout_names, NULL);
	
	// Restore new files from backup.
	cbdata.gc = gc;
	cbdata.backup_path = backup_path;
	fileScanAllDataDirs(backup_path, switchbranchCB, &cbdata); // move new files

	return;
}

// Make a backup of files changed in the local working copy.
AUTO_COMMAND ACMD_NAME(backup);
void cmd_backup(const char *localpath)
{
	GimmeClient *gc;
	PCL_Client *client;
	char **diffnames = NULL;
	PCL_DiffType *difftypes = NULL;
	char backup_path[MAX_PATH];
	char project[MAX_PATH];
	int branch;
	int i;

	// Initialize and connect to Gimme server.
	client = setViewToFile(localpath, NULL, &gc, false, true, false);
	if (!client)
	{
		s_error = GIMME_ERROR_NODIR;
		return;
	}
	gcLoadLocalFileSpec(gc);
	s_didwork = true;
	finishQueue();

	// Disallow -simulate.
	if (g_patchme_simulate)
	{
		patchmelog(PCLLOG_ERROR, "-simulate and -backup are not supported together");
		s_error = GIMME_ERROR_CANCELED;
		s_pause = 1;
		return;
	}

	// Perform diff.
	PCL_DO(pclDiffFolder(client, "", false, false, false, &diffnames, &difftypes));

	// Remove files which have not changed from our list.
	for (i = eaSize(&diffnames)-1; i >= 0; --i)
	{
		if((PCLDIFFMASK_ACTION & difftypes[i]) == PCLDIFF_NOCHANGE)
		{
			eaRemove(&diffnames, i);
			eaiRemove(&difftypes, i);
		}
	}

	// Make sure there are some files to back up, to guard against user error.
	if (!eaSize(&diffnames))
	{
		patchmelog(PCLLOG_ERROR, "No files found to back up!");
		s_error = GIMME_ERROR_FILENOTFOUND;
		s_pause = 1;
		return;
	}

	// Create default backup path.
	PCL_DO(pclGetView(gc->client, SAFESTR(project), NULL, &branch, NULL, 0));
	sprintf(backup_path, "%s/backups/%s-%s", gc->root, project, timeGetFilenameDateStringFromSecondsSince2000(timeSecondsSince2000()));

	// Prompt user for backup confirmation.
	if (!s_quiet && !patchmeDialogBackup(gc->root, &diffnames, &difftypes, SAFESTR(backup_path)))
	{
		patchmelog(PCLLOG_ERROR, "Backup canceled by user request");
		s_error = GIMME_ERROR_CANCELED;
		s_pause = 1;
		eaDestroyEx(&diffnames, NULL);
		eaiDestroy(&difftypes);
		return;
	}

	// Remove deleted files from our list.
	for (i = eaSize(&diffnames)-1; i >= 0; --i)
	{
		if((PCLDIFFMASK_ACTION & difftypes[i]) == PCLDIFF_DELETED)
		{
			eaRemove(&diffnames, i);
			eaiRemove(&difftypes, i);
		}
	}

	// Make sure path is reasonable.
	if (!*backup_path)
	{
		patchmelog(PCLLOG_ERROR, "No backup path specified");
		s_error = GIMME_ERROR_FILENOTFOUND;
		s_pause = 1;
		eaDestroyEx(&diffnames, NULL);
		eaiDestroy(&difftypes);
		return;
	}

	// Backup the new or changed files.
	patchmelog(PCLLOG_INFO, "Backing up new or changed files to %s...", backup_path);
	for (i = 0; i < eaSize(&diffnames); i++)
	{
		char srcpath[MAX_PATH], destpath[MAX_PATH];

		// Copy to backup folder
		sprintf(srcpath, "%s/%s", gc->root, diffnames[i]);
		sprintf(destpath, "%s/%s", backup_path, diffnames[i]);
		forwardSlashes(destpath);
		mkdirtree(destpath);
		backSlashes(srcpath);
		backSlashes(destpath);
		patchmelog(PCLLOG_INFO, "Copying from %s to %s", srcpath, destpath);
		if(fileCopy(srcpath, destpath)!=0)
		{
			patchmelog(PCLLOG_ERROR, "Error backing up file (%s)!", srcpath);
			s_error = GIMME_ERROR_COPY;
			s_pause = 1;
		}
	}
	eaDestroyEx(&diffnames, NULL);
	eaiDestroy(&difftypes);

	// Open up the backup.
	// Display special warning if there was a failure.
	if (!s_quiet)
	{
		if (s_error)
		{
			// Report failure to server, for tracking.
			pclSendLog(gc->client, "GimmeBackupFailed", "author %s project %s branch %d backup_path \"%s\" root \"%s\" localpath \"%s\"",
				getAuthor(), project, branch, backup_path, gc->root, localpath);

			// Report failure to user with a message box so they don't miss it.
			MessageBox_UTF8(NULL, "There was an error during the backup process, and not all files have been successfully backed up.  Read the console for details.", "Gimme Backup Failed!", MB_ICONEXCLAMATION);
		}
		else
		{
			// Report success to server, for tracking.
			pclSendLog(gc->client, "GimmeBackupSuccess", "author %s project %s branch %d backup_path \"%s\" root \"%s\" localpath \"%s\"",
				getAuthor(), project, branch, backup_path, gc->root, localpath);

			// Save last backup path.
			rrWriteString(gc->regReader, "LastBackupPath", backup_path);

			// Announce success.
			patchmelog(PCLLOG_INFO, "Backup successful!");

			// Open up directory.
			fileOpenWithEditor(backup_path);
		}
	}
}

// Userdata for getRestoreListCB()
struct getRestoreListCB_data
{
	GimmeClient *gc;
	const char *backup_path;
	char ***diff_names;
	PCL_DiffType **diff_types;
};

// Handle each backup file for getRestoreList().
static FileScanAction getRestoreListCB(char *dir_name, struct _finddata32_t *data, void *userdata)
{
	if(!(data->attrib & _A_SUBDIR))
	{
		struct getRestoreListCB_data *cbdata = userdata;
		GimmeClient *gc = cbdata->gc;
		const char *backup_path = cbdata->backup_path;
		char ***diff_names = cbdata->diff_names;
		PCL_DiffType **diff_types = cbdata->diff_types;
		PCL_Client *client = gc->client;
		char srcpath[MAX_PATH], destpath[MAX_PATH], *dbname;
		bool under_source_control;
		const char *author;
		PCL_ErrorCode error;
		PCL_DiffType type;

		// Find the name in the database.
		sprintf(srcpath, "%s/%s", dir_name, data->name);
		dbname = srcpath + strlen(backup_path) + 1;

		// Make sure this is actually under source control.
		PCL_DO(pclIsUnderSourceControl(client, dbname, &under_source_control));
		if(!under_source_control)
		{
			patchmelog(PCLLOG_ERROR, "File %s is not under source control!", destpath);
			s_error = GIMME_ERROR_NO_SC;
			return FSA_STOP;
		}

		// Get destination name.
		sprintf(destpath, "%s/%s", gc->root, dbname);

		// Classify the file.
		if (!fileCompare(srcpath, destpath))
		{
			// The files are the same.
			type = PCLDIFF_NOCHANGE;
		}
		else if ((error = pclGetLockAuthorUnsafe(gc->client, dbname, &author)) == PCL_FILE_NOT_FOUND)
		{
			// The backup file is new.
			type = PCLDIFF_CREATED;

		}
		else if (error == PCL_SUCCESS && author && !stricmp(author, getAuthor()))
		{
			bool up_to_date = false;
			pclIsFileUpToDate(client, dbname, &up_to_date);
			if (up_to_date || !fileExists(destpath))
			{
				// The file is checked out.
				type = PCLDIFF_CHANGED;
			}
			else
			{
				// This file has local modifications.
				type = PCLDIFF_CHANGED|PCLDIFF_CONFLICT;
			}
		}
		else
		{
			// This file needs to be checked out.
			type = PCLDIFF_CHANGED|PCLDIFF_NEEDCHECKOUT;
		}

		eaPush(cbdata->diff_names, strdup(dbname));
		eaiPush(cbdata->diff_types, type);
	}
	return FSA_EXPLORE_DIRECTORY;
}


// Get list of files to restore.
static void getRestoreList(GimmeClient *gc, const char *backup_path, char ***diff_names, PCL_DiffType **diff_types)
{
	PCL_Client *client = gc->client;
	U32 file_flags;
	struct getRestoreListCB_data cbdata;

	// Add file flag to check checksums.
	pclGetFileFlags(client, &file_flags);
	pclAddFileFlags(client, PCL_VERIFY_CHECKSUM);

	// Scan.
	cbdata.gc = gc;
	cbdata.backup_path = backup_path;
	cbdata.diff_names = diff_names;
	cbdata.diff_types = diff_types;
	fileScanAllDataDirs(backup_path, getRestoreListCB, &cbdata);

	// Restore previous file flags.
	pclSetFileFlags(client, file_flags);
}

// Restore the local working copy from a previous backup.
AUTO_COMMAND ACMD_NAME(restore_backup);
void cmd_restore_backup(const char *localpath)
{
	GimmeClient *gc;
	PCL_Client *client;
	char **diffnames = NULL;
	PCL_DiffType *difftypes = NULL;
	char backup_path[MAX_PATH];
	PCL_ErrorCode error;
	int i;

	// Initialize and connect to Gimme server.
	client = setViewToFile(localpath, NULL, &gc, false, true, false);
	if(!client)
	{
		s_error = GIMME_ERROR_NODIR;
		return;
	}
	gcLoadLocalFileSpec(gc);
	s_didwork = true;
	finishQueue();

	// Disallow -simulate.
	if(g_patchme_simulate)
	{
		patchmelog(PCLLOG_ERROR, "-simulate and -restore_backup are not supported together");
		s_error = GIMME_ERROR_CANCELED;
		s_pause = 1;
		return;
	}

	// Get last backup path, if any.
	backup_path[0] = 0;
	rrReadString(gc->regReader, "LastBackupPath", SAFESTR(backup_path));

	// Ask user to confirm backup path.
	if (!patchmeDialogRestorePath(SAFESTR(backup_path)))
	{
		patchmelog(PCLLOG_ERROR, "Restore canceled by user request");
		s_error = GIMME_ERROR_CANCELED;
		s_pause = 1;
		return;
	}

	// Make sure path is reasonable.
	if (!*backup_path)
	{
		patchmelog(PCLLOG_ERROR, "No restore path specified");
		s_error = GIMME_ERROR_FILENOTFOUND;
		s_pause = 1;
		return;
	}

	// Get list of files to restore.
	getRestoreList(gc, backup_path, &diffnames, &difftypes);

	// Make sure there are some files to restore.
	if (!eaSize(&diffnames))
	{
		patchmelog(PCLLOG_ERROR, "No files found to restore");
		s_error = GIMME_ERROR_FILENOTFOUND;
		s_pause = 1;
		return;
	}

	// Display dialog.
	if (!patchmeDialogRestore(gc->root, backup_path, &diffnames, &difftypes))
	{
		patchmelog(PCLLOG_ERROR, "Backup canceled by user request");
		s_error = GIMME_ERROR_CANCELED;
		s_pause = 1;
		return;
	}

	// Make sure each file to be restored is checked out.
	PCL_DO_WAIT(error = pclLockFiles(client, diffnames, eaSize(&diffnames), NULL, NULL, NULL, NULL, &gc->fileSpec));
	if (error != PCL_SUCCESS)
	{
		patchmelog(PCLLOG_ERROR, "Checkout failed");
		s_error = GIMME_ERROR_ALREADY_CHECKEDOUT;
		s_pause = 1;
	}

	// Restore files.
	if (!s_error)
	{
		for (i = 0; i != eaSize(&diffnames); ++i)
		{
			const char *dbname = diffnames[i];
			char srcpath[MAX_PATH], destpath[MAX_PATH];
			sprintf(srcpath, "%s/%s", backup_path, dbname);
			sprintf(destpath, "%s/%s", gc->root, dbname);
			forwardSlashes(destpath);
			mkdirtree(destpath);
			backSlashes(srcpath);
			backSlashes(destpath);
			patchmelog(PCLLOG_INFO, "Copying from %s to %s", srcpath, destpath);
			if(fileCopy(srcpath, destpath)!=0)
			{
				patchmelog(PCLLOG_ERROR, "Error restoring back up file (%s)!", destpath);
				s_pause = 1;
				s_error = GIMME_ERROR_COPY;
			}
		}
	}

	// Check for error.
	if (s_error)
	{
		// Report failure to user with a message box so they don't miss it.
		MessageBox_UTF8(NULL, "There was an error during the restore process, and not all files have been restored.  Read the console for details.", "Gimme Backup Failed!", MB_ICONEXCLAMATION);
	}
}

bool patchmeDoCommandWrapperInternal(int argc, char **argv, GimmeErrorValue *ret)
{
	s_didwork = false;
	s_error = GIMME_NO_ERROR;

	consoleUpSize(220, 500);

	if(0)while(1){
		Sleep(1);
	}

	if (argc == 1)
	{
		printf("Usage: gimme [-checkout] <filename>\n");
		printf("Key: b=branch and optionally sandbox (e.g. 6garthbox), [] means optional\n");
		printf("       gimme -put <filename>\n");
		printf("       gimme -undo <filename>     - undoes a checkout\n");
		printf("       gimme -editor \"notepad.exe\" <filename>\n");
		printf("       gimme -remove <filename>   - remove a file from the database\n");
		printf("       gimme -getbydate MMDDYYHH[:mm[:ss]] <filename> - get based on date\n");
		printf("       gimme -glvfile <filename>  - get the latest version of a single file\n");
		printf("       gimme -diff <filename>     - diff a local file with the latest revision\n");
		printf("       gimme -stat <filename>     - open Stat window with details on a file\n");
		printf("       gimme -author <filename>   - show the last author of a file\n");
		printf("       gimme -whoami              - displays the name Gimme uses to recognize you\n");
		printf("                        This is either your username or defined by GIMME_USERNAME\n");
		printf("       gimme -getCheckinsBetweenTimes <filename> <starttime> <endtime> \"c:/fightclub+c:/core\"\n");
		printf("                        Writes checkins to <filename>.  Times are MMDDYYHH[:mm[:ss]]\n");
		printf("       gimme -install <folder> <project> <branch> - install a new project\n");
		printf("       gimme -installfix <folder> <original> <branch> - install a new fix branch based on another project\n");
		printf("       gimme -backup <folder>     - back up a local working copy\n");
		printf("       gimme -restore <folder>    - restore local working copy from a backup\n");
		printf("   Most commands work on folders recursively, the following are aliases\n");
		printf("       -glvfold, -checkoutfold, -checkinfold, -undofold, -rmfold\n");
		printf("\n");
		printf("   Any checkin command may be prefixed with -leavecheckedout in order to leave a\n");
		printf("    file checkedout after updating the system with the local version.\n");
		printf("   Any command may be prefixed with any of the following:\n");
		printf("     -simulate        - \"simulates\" an operation (use \"gimme -simulate -getlatest\"\n");
		printf("                        to see a list of all files that differ between your local\n");
		printf("                        version and the database.\n");
		printf("     -notestwarn      - don't warn about testing failures when simulating\n");
		printf("     -ignoreerrors    - ignores errors when deciding whether to pause/continue\n");
		printf("     -filespec spec   - limit operations to files that match a filespec (e.g. *.lnk)\n");
		printf("     -exfilespec spec - limit operations to files that do not match a filespec (e.g. server/*)\n");
		printf("     -extra path      - use patch overlay folder, for glvfold and similar operations\n");
		printf("     -fileOverlay     - enables overlay mode for files on disk\n");
		printf("     -nocomments      - do not ask for any checkin comments\n");
		printf("     -overriderev <rev> - work with the specified revision\n");
 		printf("     -overridebranch <n> - override the current branch number for all subsequent commands\n");
 		printf("     -verifyAllFiles  - Forces a re-checksum of all local files\n");
		s_didwork = true;
	}
	else if(argc == 2 && argv[1][0] != '-') // no command defaults to checkout
	{
		cmd_checkout(argv[1]);
	}
	else
	{
		s_queue_checkins = true;
		if (0) // Time running all the commands.
			loadstart_printf("doing command");
		cmdParseCommandLine(argc, argv);
		finishQueue();
		s_queue_checkins = false;
		if (0)
			loadend_printf("done");
	}

	// Start FileWatcher, if we did an operation that is marked as triggering FileWatcher startup.
	if (s_doStartFileWatcher && !fileWatcherIsRunning())
		startFileWatcher();

	if(s_didwork)
	{
		int i;
		char *estr = NULL;
		estrStackCreate(&estr);
		for(i=1;i<argc;i++)
			estrConcatf(&estr, "%s ", argv[i]);
		gimmeUserLog(estr);
		estrDestroy(&estr);

		logWaitForQueueToEmpty();
		if(s_pause)
		{
			size_t ret_size;
			getenv_s(&ret_size, NULL, 0, "GIMME_NO_PAUSE");

			_flushall();
			s_nowarn = 0;

			if(!gimme_state.ignore_errors && ret_size <= 1)
			{
				if(s_delaypause)
				{
					gimmeSetOption("DelayPause", 1);
				}
				else
				{
					patchmelog(PCLLOG_INFO, "Press any key to continue");
					setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'G', 0xff5050);
					_getch();
				}
			}

			_chdir("C:\\"); // seems to help windows out if we were doing deletions, etc =)
		}
	}

	*ret = s_error;
	return s_didwork;
}
