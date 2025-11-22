#include "bindiff.h"
#include "cmdparse.h"
#include "crypt.h"
#include "earray.h"
#include "error.h"
#include "errornet.h"
#include "file.h"
#include "fileutil.h"
#include "GlobalComm.h"
#include "hoglib.h"
#include "logging.h"
#include "MemoryMonitor.h"
#include "MemReport.h"
#include "../net/net.h"
#include "Organization.h"
#include "patchclientmain_c_ast.h"
#include "patchcommonutils.h" // machinePath() TODO: move this to utilitieslib?
#include "patchdb.h"
#include "patchtrivia.h"
#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "piglib.h"
#include "rand.h"
#include "ScratchStack.h"
#include "SharedMemory.h"
#include "sock.h"
#include "sysutil.h"
#include "textparser.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "gimmeDLLWrapper.h"
#include "utilitiesLib.h"
#include "utils.h"
#include "ThreadManager.h"
#include "trivia.h"
#include "wininclude.h"

#ifdef _XBOX
// Just trying to compile - some texture function isn't linking
#pragma comment(lib, "xgraphics.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "Xonline.lib")
#pragma comment(lib, "xboxkrnl.lib")
#pragma comment(lib, "xbdm.lib")
#endif

#define COMM_MONITOR_WAIT_MSECS 1

#define DEFAULT_PATCHCLIENT_TIMEOUT		(3600.0 * 4)
#define DEFAULT_PATCHCLIENT_CONFIG_FILE	"client.config"
#define DEFAULT_PATCHCLIENT_GLOBAL_CONFIG_FILE	"C:\\patchclient.config"

#ifdef _XBOX
	#define AUTOUPDATE_TOKEN "PatchClientXBOX"
#elif defined(_M_X64)
	#define AUTOUPDATE_TOKEN "PatchClientX64"
#elif defined(WIN32)
	#define AUTOUPDATE_TOKEN "PatchClientWin32"
#else
	#error Please define PATCHCLIENT_EXECUTABLE for this platform.
#endif

#define HANDLE_ERROR(error) handleError(error)

#define PCL_DO(funccall)						\
	do {										\
		if (g_client)							\
		{										\
			error = funccall;					\
			HANDLE_ERROR(error);				\
		}										\
	} while(0)

#define PCL_DO_WAIT(funccall)					\
	do {										\
		if (g_client) {							\
			PCL_DO(funccall);					\
			error = pclWait(g_client);			\
			HANDLE_ERROR(error);				\
		}										\
	} while(0)

#define PCL_DO_WAIT_FRAMES(funccall)			\
	do {										\
		if (g_client) {							\
			PCL_DO(funccall);					\
			error = pclWaitFrames(g_client, true);\
			HANDLE_ERROR(error);				\
		}										\
	} while(0)

typedef struct DownloadStats
{
	U32 seconds;
	U32 transferred;
	U64 loops;
} DownloadStats;

typedef struct ProjectName
{
	char *view_name;
	char *project;
	bool check_exists;
} ProjectName;

static PCL_Client * g_client;
static NetComm * g_net_comm;
U32 upload_start;

// If true, shut down.
static bool g_shutdown = false;

// When the client is shutdown, it is placed here for final cleanup.
static PCL_Client * g_client_shutdown = NULL;

// Global error reporting
static PCL_ErrorCode patch_error = 0;
static char patch_error_string[2048] = {0};

AUTO_STRUCT;
typedef struct PatchClientConfig 
{
	char *command; AST(NAME(Command))

	char *patchserver;		AST(NAME(PatchServer) ADDNAMES(Server) DEFAULT(DEFAULT_PATCHSERVER))
	char *view_name;		AST(NAME(Name))
	char *project;			AST(NAME(Project))
	int branch;				AST(NAME(Branch))
	U32 view_time;			AST(NAME(Time))
	int rev;				AST(NAME(Rev) DEFAULT(PATCHREVISION_NONE))
	char *sandbox;			AST(NAME(Sandbox))
	char *author;			AST(NAME(Author))
	char **files;			AST(NAME(FileMap, FolderMap, Folder))
	char **counts_as;		AST(NAME(MapTo))
	char **hide_paths;		AST(NAME(Hide))
	int *recurse;			AST(NAME(FolderRecurse))
	char *clone_name;		AST(NAME(CloneName))
	char *launch;			AST(NAME(Launch) ADDNAMES(Executable))
	char *launchin;			AST(NAME(LaunchIn) ADDNAMES(ExecutableDir))
	char **failexecute;		AST(NAME(FailExecute))
	bool doexecuteevenonfail; AST(NAME(DoExecuteEvenOnFail))
	char *comment;			AST(NAME(Comment))
	char *root;				AST(NAME(Root))
	int days;				AST(NAME(DaysToExpire) DEFAULT(-1))
	int count;				AST(NAME(Count))
	int port;				AST(NAME(Port) DEFAULT(DEFAULT_PATCHSERVER_PORT))
	int clean;				AST(NAME(Clean))
	int patch_all;			AST(NAME(PatchAll))
	int skip_self_patch;	AST(NAME(SkipSelfPatch, NoAutoUpdate))
	int waitforkey;			AST(NAME(WaitForKey))
	int matchesonly;		AST(NAME(MatchesOnly))
	bool incremental;		AST(NAME(Incremental) BOOLFLAG)
	int forcepatch;			AST(NAME(ForcePatch) DEFAULT(1))
	int changeroot;			AST(NAME(ChangeRootFolder))
	F32 timeout;			AST(NAME(Timeout) DEFAULT(DEFAULT_PATCHCLIENT_TIMEOUT))
	int verifyAllFiles;		AST(NAME(VerifyAllFiles))
	int fileOverlay;        AST(NAME(FileOverlay))
	int cleanup;            AST(NAME(Cleanup))
	char **extra_paths;		AST(NAME(Extra))
	bool no_write;			AST(NAME(NoWrite))
	U32 max_net_bytes;		AST(NAME(MaxNetBytes))
	U32 pause;				AST(NAME(Pause))
	char *prefix;			AST(NAME(Prefix))
	char *proxy_host;		AST(NAME(ProxyHost))
	U32 proxy_port;			AST(NAME(ProxyPort))
	char *incr_from;		AST(NAME(IncrFrom))
	bool save_trivia;		AST(NAME(SaveTrivia) DEFAULT(1))
	bool in_memory;			AST(NAME(InMemory))
	bool metadata_in_memory; AST(NAME(MetadataInMemory))
	char *http_server;		AST(NAME(HttpServer))
	int http_port;			AST(NAME(HttpPort))
	char *http_prefix;		AST(NAME(HttpPrefix))
	bool verbose_stats;		AST(NAME(VerboseStats))
	bool skip_mirroring;	AST(NAME(SkipMirroring))
	bool no_delete;			AST(NAME(NoDelete))
	char *write_folder;		AST(NAME(WriteFolder))
	bool skip_deleteme;		AST(NAME(SkipDeleteme))
	bool final_hogg_reload;	AST(NAME(FinalHoggReload))
	bool init_pigset;		AST(NAME(InitPigSet)) // For debugging pigset-patcher interaction bugs
	bool crash_exit;		AST(NAME(CrashExit))
	bool memCheckDumpAllocs; AST(NAME(memCheckDumpAllocs))
	bool mmpl;				AST(NAME(mmpl))
	bool mmplShort;			AST(NAME(mmplShort))
	bool mmdsShort;			AST(NAME(mmdsShort))
	bool dump_scratchstack;	AST(NAME(DumpScratchStack))
	bool share_hoggs;		AST(NAME(ShareHoggs))
	bool no_bindiff;		AST(NAME(NoBinDiff))
	bool xfer_compressed;	AST(NAME(XferCompressed))
	bool profile_timeout;	AST(NAME(ProfileTimeout))
	bool no_reconnect;		AST(NAME(NoReconnect))
	bool ultra_fast;		AST(NAME(UltraFast))
	bool clean_hogs;		AST(NAME(CleanHogs))
	char **reportee_id;		AST(NAME(ReporteeId))
	char **reportee_host;	AST(NAME(ReporteeHost))
	U32 *reportee_port;		AST(NAME(ReporteePort))
	U32 *reportee_critical; AST(NAME(ReporteeCritical))
} PatchClientConfig;

char g_config_file[MAX_PATH];
PatchClientConfig g_config; // this could be an earray at some point. main(), connectToServer(), and command line assume there's one
DownloadStats ** g_download_stats;
U32 g_last_display;
static char s_profile_output[CRYPTIC_MAX_PATH];
static bool s_delete_profile = true;

static void handleError(PCL_ErrorCode error);
void connectToServer(void);
void syncByConfig(PatchClientConfig *config);
void testLoop(PatchClientConfig *config);
void nameView(	const char* view_name,
				const char* project,
				int branch,
				const char* comment,
				const char* sandbox,
				U32 checkout_time,
				int rev,
				int days);
void cloneView(char *view_name, char *project, char *clone_name, int days, const char* comment);
void setExpiration(const char *project, const char *view_name, int days); // days from now, -1 for never
void setFileExpiration(PatchClientConfig *config, const char **files, int days);
void fileHistory(const char *fname, char * project, int branch, char * sandbox, U32 checkout_time);
void connectAndLockFile(char * fname, char * project, int branch, char * sandbox, U32 checkout_time, char * author);
void checkinFiles(StringArray fnames, int count, char *comment, char * project, int branch, char * sandbox, char * author);
void forceinDir(const char*const* dirNames,
				const char*const* counts_as,
				const int* recurse,
				int count,
				const char*const* hide_paths,
				int hide_count,
				const char* project,
				int branch,
				const char* sandbox,
				const char* author,
				const char* comment,
				const char* view_name,
				int days,
				bool matchesonly,
				bool incremental,
				const char *incr_from,
				bool changeroot);
void corruptHogg(char * fname, U32 corruptions);
void shutdownServer(void);
void mergeServer(void);
void readCommandLine(int argc, char ** argv);
void readExecutableName(char *executable_name, PatchClientConfig *client_config);
void readClientConfig(char * config_file, PatchClientConfig *client_config);
void readAndFixupGlobals(PatchClientConfig **client_config);
static bool displayDownloadProgress(PatchProcessStats *stats, void *userdata);
static bool displayMirrorProgress(PCL_Client* client, void* userData, F32 elapsed, const char* curFileName, ProgressMeter* progress);
void checkName(char *project, char *view_name, int branch, bool check_exists);
void listNames(char *project);
void displayHelp(void);
void getFile(PCL_Client *client, const char *fname, const char *outfname);
void isCompletelySynced(char *project, int branch, char *sandbox);
void ListFiles(char *project, int branch, char *sandbox);
static int LoadTest(int connections);

// Handle PCL errors.
static void handleError(PCL_ErrorCode error)
{
	const char *error_details = NULL;

	// If no error, do nothing.
	if (!error)
		return;

	// If there was a timeout error, don't delete the saved profile.
	if (error == PCL_TIMED_OUT)
		s_delete_profile = false;

	// Get error message.
	patch_error = error;
	pclGetErrorString(error, patch_error_string, sizeof(patch_error_string));

	// Get error details, if any.
	if (g_client)
		pclGetErrorDetails(g_client, &error_details);

	// Report error.
#ifdef PLATFORM_CONSOLE
	FatalErrorf("Patch Command Line Client Error %s%s%s", patch_error_string,
		error_details ? ": " : "",
		NULL_TO_EMPTY(error_details));
#else
	Errorf("Patch Command Line Client Error %s%s%s", patch_error_string,
		error_details ? ": " : "",
		NULL_TO_EMPTY(error_details));
#endif

	// Don't allow any further PCL operations.
	g_client = NULL;
}

static void s_StructStringReplace(char **phaystack, const char *needle, const char *hay)
{
	char *haystack, *p;

	if(!phaystack)
		return;

	haystack = *phaystack;
	if( haystack && needle && needle[0] && (p=strstri(haystack,needle)) )
	{
		size_t needle_length = strlen(needle), buff_size;
		char *buff, *haystack2 = p+needle_length;

		if(!hay || !hay[0])
			hay = "\"\"";

		buff_size = strlen(haystack) + strlen(hay) - needle_length + 1;
		buff = alloca(buff_size);
		*p = '\0';
		sprintf_s(SAFESTR2(buff),"%s%s%s",haystack,hay,haystack2);
		StructFreeString(haystack);
		*phaystack = StructAllocString(buff);
	}
}

#ifdef _XBOX
static char **g_root_dirs_self;

static void XboxAddSelfRoot(const char *dir)
{
	if(!dir || !dir[0])
		return;

	if(!g_root_dirs_self)
	{
		// add a couple to get us started
		eaPush(&g_root_dirs_self,estrCreateFromStr("DEVKIT:"));
		eaPush(&g_root_dirs_self,estrCreateFromStr("DEVKIT:\\PatchClient"));
	}

	if(strchr(dir,':'))
		eaPush(&g_root_dirs_self,estrCreateFromStr(dir));
	else
	{
		char *full_dir = NULL;
		estrConcatf(&full_dir,"DEVKIT:\\%s",dir);
		eaPush(&g_root_dirs_self,full_dir);
	}
}

static char* XboxFindSelf(void)
{
	static char devkit_path[MAX_PATH];

	int i;
	char exe[MAX_PATH];
	char *executable = getExecutableName(); // "GAME:\\PatchClient.exe"
	char *pch = strrchr(executable,'\\');
	U32 exe_crc = cryptAdlerFile(executable);

	if(!exe_crc)
		return NULL;

	if(pch)
		strcpy(exe,pch+1);
	else
		strcpy(exe,executable);

	for(i = eaSize(&g_root_dirs_self)-1; i >= 0; --i)
	{
		U32 crc;
		sprintf(devkit_path, "%s\\%s", g_root_dirs_self[i], exe);
		crc = cryptAdlerFile(devkit_path);
		if(crc == exe_crc)
			return devkit_path;
	}
	return NULL;
}
#endif

// Make various small speedups focused on startup and similar, so the execution time of the process is as short as possible.
void enableUltraFast()
{
	// Avoid calls to HyperThreadingEnabled() which stall in SetProcessAffinityMask() while probing for CPU properties.
	tmDisableSetThreadProcessorIdx(true);

	// Logging can slow some things down because of waiting on the disk.
	logDisableLogging(true);

	// Connect on startup scans the entire process list, which can take a while.
	autoTimerDisableConnectAtStartup(true);

	// The localhost conversion is fairly resource-intensive since it does gethostbyname().
	sockDontConvertLocalhost(true);
}

static int configureClient(PatchClientConfig *client_config, int argc, char **argv)
{
	char path[MAX_PATH] = {0}, default_path[MAX_PATH] = {0};
#if !PLATFORM_CONSOLE
	char *tmp;
#endif
	// Defaults
	StructInit(parse_PatchClientConfig, client_config);

#if !PLATFORM_CONSOLE
	tmp = fileGetcwd(default_path, ARRAY_SIZE_CHECKED(default_path));
	strcat(default_path, "/");
#endif
	strcat(default_path, DEFAULT_PATCHCLIENT_CONFIG_FILE);

	// Executable name (e.g. LastDrop.FightClubPatcher.exe)
	readExecutableName(getExecutableName(), client_config);

	// Config file
	if(g_config_file[0]) // set by an early command line argument
	{
#if !PLATFORM_CONSOLE
		tmp = fileGetcwd(path, ARRAY_SIZE_CHECKED(path));
		strcat(path, "/");
#endif
		strcat(path, g_config_file);
		if(!fileExists(path))
		{
			printf("Config file %s not found.\n", g_config_file);
			return 1;
		}
	}
	else if(fileExists(default_path))
	{
		strcpy(path, default_path);
	}

	if(path[0] && !ParserReadTextFile(path, parse_PatchClientConfig, client_config, 0))
	{
		printf("Config file %s could not be parsed.\n", path);
		return 1;
	}
#if !PLATFORM_CONSOLE
	if(fileExists(DEFAULT_PATCHCLIENT_GLOBAL_CONFIG_FILE) && !ParserReadTextFile(DEFAULT_PATCHCLIENT_GLOBAL_CONFIG_FILE, parse_PatchClientConfig, client_config, 0))
	{
		printf("Config file %s could not be parsed.\n", DEFAULT_PATCHCLIENT_GLOBAL_CONFIG_FILE);
		return 1;
	}
#endif

	// Command line
	cmdParseCommandLine(argc, argv);

	// XBox autopatching fixup
#ifdef _XBOX
	XboxAddSelfRoot(client_config->project);
	XboxAddSelfRoot(client_config->root);
#endif

	// Zero means 'latest'
	if(client_config->view_time == 0)
		client_config->view_time = INT_MAX;
	// branch == 0 will be adjusted to the latest branch on the server // TODO: time and branch should be handled the same
	if(client_config->sandbox == NULL)
		client_config->sandbox = StructAllocString("");

	return 0;
}

// Try to clean up when appropriate.
static BOOL	consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType) 
	{ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 

			if (g_client)
				pclAbort(g_client);
			if (g_client_shutdown)
				pclAbort(g_client_shutdown);
			g_client_shutdown = g_client;
			g_client = NULL;
			g_shutdown = true;
			return TRUE;
	}

	// Pass signals to the next handler.
	return FALSE; 
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv_in;
	extern 	xferFile();
	PatchClientConfig * client_config = &g_config;
	char cmdline[10240] = "";
	int argc_in, error = 0;


	char ** argv;

	ARGV_WIDE_TO_ARGV
	EXCEPTION_HANDLER_BEGIN

	argv_in = argv;
	argc_in = argc;

	// Before we do anything, turn on ultrafast mode, if requested.
	if (strstri((char*)GetCommandLineA(), "-ultrafast") && !strstri((char*)GetCommandLineA(), "-ultrafast 0"))
		enableUltraFast();

	// As a tool, patchclient should always run in prod mode by default.
	setDefaultProductionMode(1);

	DO_AUTO_RUNS
	setDefaultAssertMode();
	gimmeDLLDisable(1);
	hogSetAllowUpgrade(true);
	printf("Initializing...\n");

#ifdef _XBOX
	{
		int i;
		char * args[1024];

		strcpy(cmdline, GetCommandLine());

		printf("Command line from argv: ");
		for(i = 0; i < argc_in; i++)
			printf("%s ", argv_in[i]);
		printf("\n");

		printf("Command line from GetCommandLine(): %s\n", cmdline);

		argc = tokenize_line_safe(cmdline, args, ARRAY_SIZE(args), NULL);
		argv = args;
	}
#else
	argc = argc_in;
	argv = argv_in;
#endif

#ifndef _XBOX
	consoleSetSize(120, 9999, 60);
#endif

	errorTrackerEnableErrorThreading(false);
	fileDisableAutoDataDir();
	memMonitorInit();
	//gimmeDisable();
	utilitiesLibStartup();
	sharedMemorySetMode(SMM_DISABLED);
	logSetDir("patchclient");
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	error = configureClient(client_config, argc, argv);

	autoTimerInit();
	filelog_printf("connections","patchclient starting");

	// Start profiling, if requested.
	// This profile will be deleted unless there is a timeout error that clears s_delete_profile.
	if (client_config->profile_timeout)
	{
		char cwd[CRYPTIC_MAX_PATH];
		char output[CRYPTIC_MAX_PATH];
		const char *date = timeGetFilenameDateStringFromSecondsSince2000(timeSecondsSince2000());
		fileGetcwd(cwd, sizeof(output));
		sprintf(s_profile_output, "%s/localdata/patchclient/patchclient-timeout.%d.%s.pf", cwd, getpid(), date);
		fixDoubleSlashes(s_profile_output);
		timerRecordStart(s_profile_output);
	}

	if (g_config.init_pigset)
		PigSetInit();

	if(!error)
		g_net_comm = commCreate(COMM_MONITOR_WAIT_MSECS, 1);

	if(client_config->proxy_host)
		commSetProxy(g_net_comm, client_config->proxy_host, client_config->proxy_port);

	if(error)
	{
		// do nothing
	}
	else if(!client_config->command)
	{
		displayHelp();
	}
	else if(stricmp(client_config->command, "sync") == 0 ||
			stricmp(client_config->command, "gamesync") == 0 ||
			stricmp(client_config->command, "namedsync") == 0 )
	{
		char		trivia_path[MAX_PATH] = "";
		const char* root = client_config->root ? client_config->root : client_config->project;
		
		if(root)
		{
			machinePath(trivia_path, root);
		}

		// TODO: do more sanity checking
		if(!SAFE_DEREF(client_config->project))
		{
			printf("You must provide a project to sync.\n");
			error = 1;
		}
		else if(!client_config->forcepatch && triviaCheckPatchCompletion(trivia_path, client_config->view_name, client_config->branch, client_config->view_time, client_config->sandbox, !client_config->patch_all))
		{
			printf("Skipping patch. The patch was already applied successfully. Use -forcepatch if you want to patch anyway.\n");
		}
		else
		{
			syncByConfig(client_config);
		}
	}
	else if (stricmp(client_config->command, "testloop") == 0)
	{
		testLoop(client_config);
	}
	else if(stricmp(client_config->command, "nameview")==0)
	{
		nameView(	client_config->view_name,
					client_config->project,
					client_config->branch,
					client_config->comment,
					client_config->sandbox,
					client_config->view_time,
					client_config->rev,
					client_config->days);
	}
	else if(stricmp(client_config->command, "cloneview")==0)
	{
		cloneView(	client_config->view_name,
					client_config->project,
					client_config->clone_name,
					client_config->days,
					client_config->comment);
	}
	else if(stricmp(client_config->command, "setexpiration")==0)
	{
		setExpiration(client_config->project, client_config->view_name, client_config->days);
	}
	else if(stricmp(client_config->command, "setfileexpiration")==0)
	{
		setFileExpiration(client_config, client_config->files, client_config->days);
	}
	else if (stricmp(client_config->command,"filehistory")==0)
	{
		char * filename = (eaSize(&client_config->files) ? client_config->files[0] : NULL);

		fileHistory(filename, client_config->project, client_config->branch, client_config->sandbox, client_config->view_time);
	}
	else if (stricmp(client_config->command,"checkout")==0)
	{
		char * filename = (eaSize(&client_config->files) ? client_config->files[0] : NULL);

		connectAndLockFile(filename, client_config->project, client_config->branch, client_config->sandbox, client_config->view_time, client_config->author);
	}
	else if (stricmp(client_config->command,"checkin")==0)
	{
		checkinFiles(client_config->files, eaSize(&client_config->files), client_config->comment, client_config->project,
			client_config->branch, client_config->sandbox, client_config->author);
	}
	else if (stricmp(client_config->command, "forceindir") == 0)
	{
		if(client_config->incremental && !client_config->sandbox)
		{
			printf("Incremental patches must be made in a sandbox.\n");
		}
		else if(client_config->incremental && !client_config->view_name)
		{
			printf("You can't make an incremental patch without a -name.\n");
		}
		else
		{
			forceinDir( client_config->files,
						client_config->counts_as,
						client_config->recurse,
						eaSize(&client_config->files),
						client_config->hide_paths,
						eaSize(&client_config->hide_paths),
						client_config->project,
						client_config->branch,
						client_config->sandbox,
						client_config->author,
						client_config->comment,
						client_config->view_name,
						client_config->days,
						!!client_config->matchesonly,
						client_config->incremental,
						client_config->incr_from,
						client_config->changeroot);
		}
	}
	else if (stricmp(client_config->command, "corrupthogg") == 0)
	{
		char * filename = (eaSize(&client_config->files) ? client_config->files[0] : NULL);

		corruptHogg(filename, 100);
	}
	else if (stricmp(client_config->command, "shutdown") == 0)
	{
		shutdownServer();
	}
	else if (stricmp(client_config->command, "merge") == 0)
	{
		mergeServer();
	}
	else if(stricmp(client_config->command, "checkname") == 0 ||
			stricmp(client_config->command, "namefree") == 0 )
	{
		checkName(client_config->project, client_config->view_name, client_config->branch, false);
	}
	else if (stricmp(client_config->command, "nameexists") == 0)
	{
		checkName(client_config->project, client_config->view_name, client_config->branch, true);
	}
	else if (stricmp(client_config->command, "listnames") == 0)
	{
		listNames(client_config->project);
	}
#ifndef _XBOX
	else if(stricmp(client_config->command, "linkdialog") == 0)
	{
		char cwd[MAX_PATH];

		if(client_config->root && client_config->root[0])
			strcpy(cwd, client_config->root);
		else
			(void)fileGetcwd(cwd, MAX_PATH);
		pclLinkDialog(cwd, true);
	}
	else if(stricmp(client_config->command, "syncdialog") == 0)
	{
		char cwd[MAX_PATH];

		if(client_config->root && client_config->root[0])
			strcpy(cwd, client_config->root);
		else
			(void)fileGetcwd(cwd, MAX_PATH);
		pclSyncDialog(cwd, true);
	}
	else if(stricmp(client_config->command, "getlatestdialog") == 0)
	{
		char cwd[MAX_PATH];
		char ** dirs = NULL;

		if(client_config->root && client_config->root[0])
			strcpy(cwd, client_config->root);
		else
			(void)fileGetcwd(cwd, MAX_PATH);
		eaPush(&dirs, cwd);

		pclGetLatestDialog(dirs, eaSize(&dirs), true);

		eaDestroy(&dirs);
	}
#endif
	else if(stricmp(client_config->command, "getfile") == 0)
	{
		int i;

		connectToServer();
		pclSetProcessCallback(g_client, NULL, NULL);
		if(client_config->view_name)
		{
			PCL_DO_WAIT(pclSetNamedView(g_client, client_config->project, client_config->view_name, false, client_config->save_trivia, NULL, NULL));
		}
		else if(client_config->rev != PATCHREVISION_NONE)
		{
			PCL_DO_WAIT(pclSetViewByRev(g_client, client_config->project, client_config->branch, client_config->sandbox, client_config->rev, false, client_config->save_trivia, NULL, NULL));
		}
		else
		{
			PCL_DO_WAIT(pclSetViewByTime(g_client, client_config->project, client_config->branch, client_config->sandbox, client_config->view_time, false, client_config->save_trivia, NULL, NULL));
		}

		for(i=0; i<eaSize(&client_config->files); i++)
		{
			getFile(g_client, client_config->files[i], client_config->counts_as[i]);
		}
	}
	else if(stricmp(client_config->command, "undocheckin") == 0)
	{
		connectToServer();
		PCL_DO_WAIT(pclSetAuthor(g_client, client_config->author, NULL, NULL));
		if(client_config->view_name)
		{
			PCL_DO_WAIT(pclSetNamedView(g_client, client_config->project, client_config->view_name, false, client_config->save_trivia, NULL, NULL));
		}
		else if(client_config->rev != PATCHREVISION_NONE)
		{
			PCL_DO_WAIT(pclSetViewByRev(g_client, client_config->project, client_config->branch, client_config->sandbox, client_config->rev, false, client_config->save_trivia, NULL, NULL));
		}
		else
		{
			PCL_DO_WAIT(pclSetViewByTime(g_client, client_config->project, client_config->branch, client_config->sandbox, client_config->view_time, false, client_config->save_trivia, NULL, NULL));
		}

		PCL_DO_WAIT(pclUndoCheckin(g_client, client_config->comment ? client_config->comment : "Reverting checkin", NULL, NULL));
	}
	else if(stricmp(client_config->command, "ping") == 0)
	{
		connectToServer();
		PCL_DO_WAIT(pclPing(g_client, NULL, NULL, NULL));
	}
	else if (stricmp(client_config->command,"iscompletelysynced")==0)
	{
		isCompletelySynced(client_config->project, client_config->branch, client_config->sandbox);
	}
	else if (stricmp(client_config->command,"list")==0)
	{
		ListFiles(client_config->project, client_config->branch, client_config->sandbox);
	}
	else if (stricmp(client_config->command,"loadtest")==0)
	{
		error = LoadTest(client_config->count);
	}
	else
	{
		printf("Unknown patchclient command: %s\n", client_config->command);
		error = true;
	}

	// Exit immediately if someone hit Ctrl-C.
	if (g_shutdown)
	{
		if (g_client_shutdown)
			pclDisconnectAndDestroy(g_client_shutdown);
		exit(13);
	}

	logWaitForQueueToEmpty();

	if(g_config.cleanup)
	{
#ifndef _XBOX
		rmdirtreeEx(STACK_SPRINTF("%s/.patch", client_config->root), 1);
#else
		Errorf("-cleanup option is not valid on the XBox");
#endif
	}

	// Disconnect client link.
	if (g_client)
	{
		PCL_DO(pclDisconnectAndDestroy(g_client));
		g_client = NULL;
	}

	// If there was an error, run any requested failure command.
#ifndef PLATFORM_CONSOLE
	if(!error && patch_error && eaSize(&client_config->failexecute))
	{
		static const char workingdir_command[] = "WORKINGDIR(";
		EARRAY_CONST_FOREACH_BEGIN(client_config->failexecute, i, n);
		{
			char cwd[MAX_PATH];
			char workingDir[MAX_PATH] = {0};
			char *failexecute = client_config->failexecute[i];
			char *escapedError = NULL;

			// Perform substitutions.
			s_StructStringReplace(&failexecute,"{Root}",client_config->root);
			s_StructStringReplace(&failexecute,"{PatchServer}",client_config->patchserver);
			s_StructStringReplace(&failexecute,"{Project}",client_config->project);
			estrStackCreate(&escapedError);
			estrAppendEscaped(&escapedError, patch_error_string);
			s_StructStringReplace(&failexecute,"{ErrorString}",escapedError);
			estrDestroy(&escapedError);
	
			// Extract WORKINGDIR if present; uses the same syntax as Sentry.
			if (strStartsWith(failexecute, workingdir_command))
			{
				char *pFirstRightParens = strchr(failexecute, ')');
				if (pFirstRightParens)
				{
					char *pCommandBegin = pFirstRightParens + 1;
					while (*pCommandBegin == ' ')
						pCommandBegin++;
					*pFirstRightParens = 0;
					strcpy(workingDir, failexecute+ strlen(workingdir_command));
				}
				failexecute = pFirstRightParens + 1;
			}
	
			// Execute failure handler.
			if (workingDir[0])
			{
				fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)-1);
				assert(chdir(workingDir) == 0);
				system_detach(failexecute, 0, false);
				assert(chdir(cwd) == 0);
			}
			else
			{
				system_detach(failexecute, 0, false);
			}
		}
		EARRAY_FOREACH_END;
	}
#endif  // PLATFORM_CONSOLE

	// Run any requested -launch command.
	if(!error && (!patch_error || client_config->doexecuteevenonfail) && client_config->launch)
	{
		char cwd[MAX_PATH];
		s_StructStringReplace(&client_config->launchin,"{Root}",client_config->root);
		s_StructStringReplace(&client_config->launchin,"{Project}",client_config->project);
		s_StructStringReplace(&client_config->launch,"{Root}",client_config->root);
		s_StructStringReplace(&client_config->launch,"{PatchServer}",client_config->patchserver);
		s_StructStringReplace(&client_config->launch,"{Project}",client_config->project);
#ifdef _XBOX
		// TODO: utilize executableDir or root
		printf("About to restart with command line <<%s>>\n", client_config->launch);
		debugXboxRestart(client_config->launch);
		UNUSED(cwd);
#else
		if (client_config->launchin && client_config->launchin[0])
		{
			fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)-1);
			assert(chdir(client_config->launchin) == 0);
			system_detach(client_config->launch, 0, false);
			assert(chdir(cwd) == 0);
		}
		else
		{
			system_detach(client_config->launch, 0, false);
		}
#endif
	}

	// Stop profiling, and delete the profile, if there was no timeout error.
	if (client_config->profile_timeout)
	{

		// Force a final frame.
		if (!s_delete_profile)
			Sleep(5);
		autoTimerThreadFrameEnd();

		// Stop recording, and delete the profile, if necessary.
		if (s_delete_profile)
		{
			U32 now;
			int result;
			timerRecordEnd();
			Sleep(1);
			now = timeSecondsSince2000_ForceRecalc();
			do {
				result = remove(s_profile_output);
				Sleep(1000);
			} while (result && timeSecondsSince2000_ForceRecalc() < now + 3);
		}
		else
		{
			Sleep(1000);
			timerRecordEnd();
		}
	}

	StructDeInit(parse_PatchClientConfig, client_config);

	// Dump debugging memory state, if required.
	if (g_config.memCheckDumpAllocs)
		memCheckDumpAllocs();
	if (g_config.mmpl)
		mmpl();
	if (g_config.mmplShort)
		mmplShort();
	if (g_config.mmdsShort)
		mmdsShort();
	if (g_config.dump_scratchstack)
		ScratchStackDumpStats();

	if(client_config->waitforkey)
	{
#ifndef _XBOX
		printf("Press a key to exit.\n");
		(void)_getch();
#endif
	}

	if(client_config->pause)
	{
		printf("Pausing for %u seconds...\n", client_config->pause);
		Sleep(client_config->pause * 1000);
	}

	if (g_config.crash_exit)
	{
		extern int *g_NULLPTR;
		setAssertMode(ASSERTMODE_FULLDUMP|ASSERTMODE_MINIDUMP|ASSERTMODE_DATEDMINIDUMPS|ASSERTMODE_NOERRORTRACKER);
		((*g_NULLPTR)++);
	}

	EXCEPTION_HANDLER_END 
	
#ifndef _XBOX
	if(patch_error)
		exit(100+patch_error);
	else
		exit(0);
#endif
}

#define SETCFGCMD(cmd)									\
	{													\
		StructFreeString(g_config.command);				\
		g_config.command = StructAllocString(#cmd);		\
	}

#define SETCFGSTR(var)									\
	{													\
		StructFreeString(g_config.var);					\
		g_config.var = StructAllocString(str);			\
	}

#define PUSHCFGSTR(var)									\
	{													\
		eaPush(&g_config.var, StructAllocString(str));	\
	}


AUTO_CMD_STRING(g_config_file, config) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

AUTO_COMMAND ACMD_NAME(sync);			void setcfgcmd_sync(int dum)			SETCFGCMD(sync)
AUTO_COMMAND ACMD_NAME(namedsync);		void setcfgcmd_namedsync(int dum)		SETCFGCMD(namedsync)	// redundant
AUTO_COMMAND ACMD_NAME(gamesync);		void setcfgcmd_gamesync(int dum)		SETCFGCMD(gamesync)		// redundant
AUTO_COMMAND ACMD_NAME(filehistory);	void setcfgcmd_filehistory(int dum)		SETCFGCMD(filehistory)
AUTO_COMMAND ACMD_NAME(nameview);		void setcfgcmd_nameview(int dum)		SETCFGCMD(nameview)
AUTO_COMMAND ACMD_NAME(cloneview);		void setcfgcmd_cloneview(int dum)		SETCFGCMD(cloneview)
AUTO_COMMAND ACMD_NAME(setexpiration);	void setcfgcmd_setexpiration(int dum)	SETCFGCMD(setexpiration)
AUTO_COMMAND ACMD_NAME(setfileexpiration); void setcfgcmd_setfileexpiration(int dum) SETCFGCMD(setfileexpiration)
AUTO_COMMAND ACMD_NAME(checkin);		void setcfgcmd_checkin(int dum)			SETCFGCMD(checkin)
AUTO_COMMAND ACMD_NAME(checkout);		void setcfgcmd_checkout(int dum)		SETCFGCMD(checkout)
AUTO_COMMAND ACMD_NAME(forceindir);		void setcfgcmd_forceindir(int dum)		SETCFGCMD(forceindir)
AUTO_COMMAND ACMD_NAME(corrupthogg);	void setcfgcmd_corrupthogg(int dum)		SETCFGCMD(corrupthogg)
AUTO_COMMAND ACMD_NAME(shutdown);		void setcfgcmd_shutdown(int dum)		SETCFGCMD(shutdown)
AUTO_COMMAND ACMD_NAME(merge);			void setcfgcmd_merge(int dum)			SETCFGCMD(merge)
AUTO_COMMAND ACMD_NAME(testloop);		void setcfgcmd_testloop(int dum)		SETCFGCMD(testloop)
AUTO_COMMAND ACMD_NAME(linkdialog);		void setcfgcmd_linkdialog(int dum)		SETCFGCMD(linkdialog)
AUTO_COMMAND ACMD_NAME(syncdialog);		void setcfgcmd_syncdialog(int dum)		SETCFGCMD(syncdialog)
AUTO_COMMAND ACMD_NAME(getlatestdialog);void setcfgcmd_getlatestdialog(int dum)	SETCFGCMD(getlatestdialog)
AUTO_COMMAND ACMD_NAME(namefree);		void setcfgcmd_namefree(int dum)		SETCFGCMD(namefree)
AUTO_COMMAND ACMD_NAME(checkname);		void setcfgcmd_checkname(int dum)		SETCFGCMD(checkname)
AUTO_COMMAND ACMD_NAME(nameexists);		void setcfgcmd_nameexists(int dum)		SETCFGCMD(nameexists)
AUTO_COMMAND ACMD_NAME(listnames);		void setcfgcmd_listnames(int dum)		SETCFGCMD(listnames)
AUTO_COMMAND ACMD_NAME(getfile);		void setcfgcmd_getfile(int dum)			SETCFGCMD(getfile)
AUTO_COMMAND ACMD_NAME(undo_checkin);	void setcfgcmd_undocheckin(int dum)		SETCFGCMD(undocheckin)
AUTO_COMMAND ACMD_NAME(ping);			void setcfgcmd_ping(int dum)			SETCFGCMD(ping)
AUTO_COMMAND ACMD_NAME(iscompletelysynced); void setcfgcmd_iscompletelysynced(int dum) SETCFGCMD(iscompletelysynced)
AUTO_COMMAND ACMD_NAME(list);			void setcfgcmd_list(int dum)			SETCFGCMD(list)
AUTO_COMMAND ACMD_NAME(loadtest);		void setcfgcmd_loadtest(int dum)		SETCFGCMD(loadtest)

AUTO_COMMAND ACMD_NAME(author);			void setcfgstr_author(char *str)		SETCFGSTR(author)
AUTO_COMMAND ACMD_NAME(comment);		void setcfgstr_comment(ACMD_SENTENCE str) SETCFGSTR(comment)

AUTO_COMMAND ACMD_NAME(launch);			void setcfgstr_launch(char *str)		SETCFGSTR(launch)
AUTO_COMMAND ACMD_NAME(executable);		void setcfgstr_executable(char *str)	SETCFGSTR(launch)		// redundant
AUTO_COMMAND ACMD_NAME(launchin);		void setcfgstr_launchin(char *str)		SETCFGSTR(launchin)
AUTO_COMMAND ACMD_NAME(executableDir);	void setcfgstr_executableDir(char *str)	SETCFGSTR(launchin)		// redundant
AUTO_COMMAND ACMD_NAME(failexecute);	void setcfgstr_failexecute(char *str)	PUSHCFGSTR(failexecute)

AUTO_COMMAND ACMD_NAME(patchserver);	void setcfgstr_patchserver(char *str)	SETCFGSTR(patchserver)
AUTO_COMMAND ACMD_NAME(server);			void setcfgstr_server(char *str)		SETCFGSTR(patchserver)	// redundant
AUTO_COMMAND ACMD_NAME(project);		void setcfgstr_project(char *str)		SETCFGSTR(project)
AUTO_COMMAND ACMD_NAME(sandbox);		void setcfgstr_sandbox(char *str)		SETCFGSTR(sandbox)
AUTO_COMMAND ACMD_NAME(name);			void setcfgstr_name(char *str)			SETCFGSTR(view_name)
AUTO_COMMAND ACMD_NAME(clonename);		void setcfgstr_clonename(char *str)		SETCFGSTR(clone_name)
AUTO_COMMAND ACMD_NAME(root);			void setcfgstr_root(char *str)			SETCFGSTR(root)
AUTO_COMMAND ACMD_NAME(extra);			void setcfgstr_extra(char *str)			PUSHCFGSTR(extra_paths)
AUTO_COMMAND ACMD_NAME(prefix);			void setcfgstr_prefix(char *str)		SETCFGSTR(prefix)
AUTO_COMMAND ACMD_NAME(incr_from);		void setcfgstr_incr_from(char *str)		SETCFGSTR(incr_from)
AUTO_COMMAND ACMD_NAME(httpserver);		void setcfgstr_http_server(char *str)	SETCFGSTR(http_server)
AUTO_COMMAND ACMD_NAME(httpprefix);		void setcfgstr_http_prefix(char *str)	SETCFGSTR(http_prefix)
AUTO_COMMAND ACMD_NAME(writefolder);	void setcfgstr_write_folder(char *str)	SETCFGSTR(write_folder)


AUTO_CMD_FLOAT(g_config.timeout, timeout);

AUTO_CMD_INT(g_config.days, days);
AUTO_CMD_INT(g_config.count, count);
AUTO_CMD_INT(g_config.port, port);
AUTO_CMD_INT(g_config.branch, branch);
AUTO_CMD_INT(g_config.view_time, time);
AUTO_CMD_INT(g_config.rev, rev);
AUTO_CMD_INT(g_config.changeroot, changeroot);
AUTO_CMD_INT(g_config.clean, clean);
AUTO_CMD_INT(g_config.patch_all, patchall);
AUTO_CMD_INT(g_config.forcepatch, forcepatch);
AUTO_CMD_INT(g_config.skip_self_patch, skipselfpatch);
AUTO_CMD_INT(g_config.waitforkey, waitforkey); // make this one its own global?
AUTO_CMD_INT(g_config.matchesonly, matchesonly);
AUTO_CMD_INT(g_config.incremental, incremental);
AUTO_CMD_INT(g_config.skip_self_patch, noAutoUpdate);	// deprecated synonym for skipselfpatch
AUTO_CMD_INT(g_config.verifyAllFiles, verifyAllFiles);
AUTO_CMD_INT(g_config.fileOverlay, fileOverlay);
AUTO_CMD_INT(g_config.cleanup, cleanup);
AUTO_CMD_INT(g_config.no_write, nowrite);
AUTO_CMD_INT(g_config.max_net_bytes, maxnetbytes);
AUTO_CMD_INT(g_config.pause, pause);
AUTO_CMD_INT(g_config.save_trivia, save_trivia);
AUTO_CMD_INT(g_config.in_memory, inmemory);
AUTO_CMD_INT(g_config.metadata_in_memory, metadatainmemory);
AUTO_CMD_INT(g_config.doexecuteevenonfail, doexecuteevenonfail);
AUTO_CMD_INT(g_config.http_port, httpport);
AUTO_CMD_INT(g_config.verbose_stats, verbosestats);
AUTO_CMD_INT(g_config.skip_mirroring, skipmirroring);
AUTO_CMD_INT(g_config.no_delete, nodelete);
AUTO_CMD_INT(g_config.skip_deleteme, skipdeleteme);
AUTO_CMD_INT(g_config.final_hogg_reload, finalhoggreload);
AUTO_CMD_INT(g_config.init_pigset, initpigset);
AUTO_CMD_INT(g_config.crash_exit, crashexit);
AUTO_CMD_INT(g_config.memCheckDumpAllocs, dumpmemCheckDumpAllocs);
AUTO_CMD_INT(g_config.mmpl, dumpmmpl);
AUTO_CMD_INT(g_config.mmplShort, dumpmmplShort);
AUTO_CMD_INT(g_config.mmdsShort, dumpmmdsShort);
AUTO_CMD_INT(g_config.dump_scratchstack, dumpScratchStack);
AUTO_CMD_INT(g_config.share_hoggs, sharehoggs);
AUTO_CMD_INT(g_config.no_bindiff, nobindiff);
AUTO_CMD_INT(g_config.xfer_compressed, xfercompressed);
AUTO_CMD_INT(g_config.profile_timeout, profiletimeout);
AUTO_CMD_INT(g_config.no_reconnect, noreconnect);
AUTO_CMD_INT(g_config.ultra_fast, ultrafast);
AUTO_CMD_INT(g_config.clean_hogs, cleanhogs);

extern bool g_ExactTimestamps;
AUTO_CMD_INT(g_ExactTimestamps, exactTimestamps);

extern bool g_AllFilesInWrongHoggs; // Simulates a bug we had that might have exposed other bugs
AUTO_CMD_INT(g_AllFilesInWrongHoggs, AllFilesInWrongHoggs);


AUTO_COMMAND ACMD_NAME(hide);
void hideFileOrFolder(char * hide_path)
{
	eaPush(&g_config.hide_paths, StructAllocString(hide_path));
}

AUTO_COMMAND ACMD_NAME(file, folder);
void addFileOrFolder(char * name)
{
	char counts_as[MAX_PATH], * str;

	strcpy(counts_as, name);
	str = strchr(counts_as, '*');
	if(str)
		str[0] = '\0';

	eaPush(&g_config.files, StructAllocString(name));
	eaPush(&g_config.counts_as, StructAllocString(counts_as));
	eaiPush(&g_config.recurse, 1);
};

AUTO_COMMAND ACMD_NAME(foldermap);
void addFolderWithAlias(char * folder, char * counts_as)
{
	eaPush(&g_config.files, StructAllocString(folder));
	eaPush(&g_config.counts_as, StructAllocString(counts_as));
	eaiPush(&g_config.recurse, 1);
};

AUTO_COMMAND ACMD_NAME(foldermap_nr);
void addFolderWithAliasNR(char * folder, char * counts_as)
{
	eaPush(&g_config.files, StructAllocString(folder));
	eaPush(&g_config.counts_as, StructAllocString(counts_as));
	eaiPush(&g_config.recurse, 0);
};

AUTO_COMMAND ACMD_NAME(beginpatchstatusreporting);
void beginpatchstatusreporting(char *identifier, char *host, U16 port)
{
	eaPush(&g_config.reportee_id, StructAllocString(identifier));
	eaPush(&g_config.reportee_host, StructAllocString(host));
	eaiPush(&g_config.reportee_port, port);
	eaiPush(&g_config.reportee_critical, 0);
};

AUTO_COMMAND ACMD_NAME(beginpatchstatusreporting_critical);
void beginpatchstatusreporting_critical(char *identifier, char *host, U16 port)
{
	eaPush(&g_config.reportee_id, StructAllocString(identifier));
	eaPush(&g_config.reportee_host, StructAllocString(host));
	eaiPush(&g_config.reportee_port, port);
	eaiPush(&g_config.reportee_critical, 1);
};


extern int g_force_sockbsd;
AUTO_COMMAND ACMD_NAME(proxy);
void setcfg_proxy(char *host, U32 port)
{
	// This must be done early
	g_force_sockbsd = 1;

	StructFreeString(g_config.proxy_host);
	g_config.proxy_host = StructAllocString(host);
	g_config.proxy_port = port;
};

AUTO_COMMAND;
void NoRedirect(bool enable)
{
	pclNoRedirect(!!enable);
}

AUTO_COMMAND;
void NoHttp(bool enable)
{
	pclNoHttp(!!enable);
}

void displayHelp(void)
{
	printf(	"\n\n"
			"PatchClient Commands (only the last specified is used)\n"

			// Basic commands
			"  -help            | display this\n"
			"  -sync            | you must specify -project, will sync to -name if specified, otherwise\n"
			"                   | -branch (default latest), -time (default latest), and\n"
			"                   | -sandbox (default none)\n"
			"  -getfile         | sync a particular file, specified by -file\n"
			"  -nameview        | assign a name to a combination of branch, time, and\n"
			"                   | sandbox\n"
			"  -checkout        | ask for the lock for a specific file in a project\n"
			"  -checkin         | upload a file to a project\n"
			"  -forceindir      | make the project match the user's directory, ignoring\n"
			"                   | what's checked out\n"

			// More advanced commands
			"  -cloneview       | make a copy of -name view called -clonename\n"
			"  -setexpiration   | set -name view to expire in -days (-1 for no expiration)\n"
			"  -setfileexpiration | set a path and all its children versions to expire in -days (-1 for no expiration)\n"
			"  -namefree        | checks the uniqueness of a -name, exits with 0 if free\n"
			"  -nameexists      | checks if a patch is on the server, exits with 0 if so\n"
			"  -iscompletelysynced | check if a path is completely synced\n"
			"  -list            | list the files in a directory\n"

			// Testing commands
			"  -testloop        | like sync, but in an infinite loop, implies -clean\n"
			"  -loadtest        | perform load test, specify -count\n"
			"  -corrupthogg     | corrupt a hogg file\n"

			// Weird commands
			//"  -linkdialog      | opens the Link dialog box\n"
			//"  -syncdialog      | opens the Sync dialog box\n"
			//"  -getlatestdialog | opens the Get Latest dialog box\n"

			"\nPatchClient Arguments\n"

			// Basic
			"  -author          | author who's uploading / checking-out\n"
			"  -comment         | the comment to add to a checkin\n"
			"  -file            | the file/folder to work on / corrupt / upload\n"
			"  -folder          | an alternative to -file\n"
			"  -foldermap       | takes 2 parameters: the folder on the local machine, and the\n"
			"                   | patch folder to map it to\n"
			"  -days            | days from now (used with -setexpiration and new views)\n"

			"  -project         | the project the client is using\n"
			"  -patchserver     | the patchserver to connect to (also -server)\n");
	printf(	"                   | (default %s)\n", DEFAULT_PATCHSERVER);
	printf(	"  -port            | which port to use (default %d)\n", DEFAULT_PATCHSERVER_PORT);
	printf(	"  -name            | the name of a view to use\n"
			"  -branch          | branch to upload / checkout / name (default latest)\n"
			"  -time            | the time for the view you're using (ignored for upload)\n"
			"  -sandbox         | part of a view: the sandbox you're working in\n"
			"  -prefix          | operate on only this subset of the project\n"

	// More advanced
			"  -config          | a config file to load (which can still be overridden)\n"
			"  -count           | number of connections (used with -loadtest)\n"
			"  -clean           | deletes patch directory before patching\n"
			"  -patchall        | get all files (default gets required files)\n"
			"  -forcepatch      | normally, patching will be skipped if the patch has already\n"
			"                   | been successfully applied\n"
			"  -skipselfpatch   | disables updating the patchclient\n"
			"  -root            | the root folder to use for patching\n"
			"  -extra           | add an extra folder to search for patch data\n"
			"  -proxy           | patch through a given SOCKS proxy server\n"
			"  -noredirect      | ignore redirection requests from the server\n"
			"  -skipmirroring   | don't perform mirroring\n"
			"  -nodelete        | don't delete files when writing the patch to the disk\n"
			"  -writefolder     | specify a folder other than the root for write operations\n"
			"  -matchesonly     | with forcein, only upload matches to foldermaps\n"
			"  -incremental     | with forcein, this is an incremental patch, requires sandbox\n"
			"  -clonename       | the name of the new view with -cloneview\n"
			"  -beginpatchstatusreporting <id> <host> [port] | report patching status\n"
			"  -launch          | the executable to launch when finished (see launch tags)\n"
			"  -launchin        | the working directory for -launch (see launch tags)\n"
			"  -failexecute     | like -launch, but for failure\n"
			"  -doexecuteevenonfail | run the -launch command even after patch failure\n"

			"\nAdvanced Tuning and Debugging\n"
			"  -finalhoggreload | reload all hoggs when done syncing\n"
			"  -nobindiff       | don't use bindiffing\n"
			"  -xfercompressed  | try to use compressed xfers whenever possible\n"
			"  -profiletimeout  | save a profile if command times out\n"
			"  -skipdeleteme    | don't scan for .deleteme files to delete\n"
			"  -sharehoggs      | don't block other processes from accessing hoggs during the patch\n"
			"  -cleanhogs       | remove \"rogue\" hog files which don't belong\n"
			"  -noreconnect     | don't reconnect if the link to the server drops\n"
			"  -ultrafast       | skip some operations to speed up patchclient by several ms\n"


			"\nPatch Server control, diagnostics, and testing \n"
			"  -shutdown        | shut down Patch Server\n"
			"  -merge           | force Patch Server to merge\n"

			"\nThese tags will be replaced appropriately in launch/launchin arguments\n"
			"  launch and launchin: {Root}, {Project}\n"
			"  launch only: {PatchServer}\n"
			"\n\n");
}

void OVERRIDE_LATELINK_help(char *dummy)
{
	displayHelp();
}

static void preconnectCallback(PCL_Client * client, void * userData)
{
	// Start reporting to all hosts requested.
	EARRAY_CONST_FOREACH_BEGIN(g_config.reportee_host, i, n);
	{
		pclStartStatusReporting(client, g_config.reportee_id[i], g_config.reportee_host[i], g_config.reportee_port[i], !!g_config.reportee_critical[i]);
	}
	EARRAY_FOREACH_END;
}

static void connectCallback(PCL_Client *client, bool updated, PCL_ErrorCode error, const char *error_details, const char *exe_name)
{
	if(updated)
	{
		char *cmd=NULL;
		assert(estrPrintf(&cmd, "\"%s\" %s", exe_name, GetCommandLineWithoutExecutable()) >= 0);
		_flushall();
		printf("Running auto-patched executable: %s\n", cmd);
#ifdef _XBOX
		debugXboxRestart(cmd);
#else
		exit(system(cmd));
#endif
	}
}

void connectToServer(void)
{
	PCL_ErrorCode error;
	PatchClientConfig *config = &g_config;
	int attempts = 0;
	char autoupdate_buf[MAX_PATH] = "";
	char *autoupdate_path = NULL;

	if(g_config.skip_self_patch)
	{
		printf("Skipping self-path because of configuration\n");
	}
#ifdef _XBOX
	else
	{
		autoupdate_path = XboxFindSelf();
		if(!autoupdate_path)
			printf("Skipping self-patch because the client executable could not be found in DEVKIT:\\.\n");
	}
#else
	else
	{
		strcpy(autoupdate_buf, getExecutableName());
		forwardSlashes(autoupdate_buf);

		if( strstri(autoupdate_buf, "/Utilities/bin/") ||
			strstri(autoupdate_buf, "/Core/tools/bin/")||
			strstri(autoupdate_buf, "/Night/tools/bin/") )
		{
			printf("Skipping self-patch because the client is in Utilities/bin or Core/tools/bin.\n");
		}
		else
		{
			autoupdate_path = autoupdate_buf;
		}
	}
#endif

	assert(g_client == NULL);
	do {
		if(g_client == NULL)
		{
			printf("Connecting to %s:%i\n", config->patchserver, config->port);
		}
		else
		{
			printf("The client was disconnected during a connect attempt\n");
			pclDisconnectAndDestroy(g_client);
			g_client = NULL;
			attempts++;
		}

		error = pclConnectAndCreateEx(&g_client,
									config->patchserver,
									config->port,
									config->timeout,
									g_net_comm,
									config->root,
									AUTOUPDATE_TOKEN,
									autoupdate_path,
									connectCallback,
									preconnectCallback,
									autoupdate_path,
									NULL,
									0);
		if(!error && g_client && g_config.max_net_bytes)
			error = pclSetMaxNetBytes(g_client, g_config.max_net_bytes * 1024);
		if(!error && g_client)
#if _WIN64
			pclSetMaxMemUsage(g_client, 2*1024*1024*1024LL);
#else
			pclSetMaxMemUsage(g_client, 512*1024*1024);
#endif
		if(error || !g_client)
		{
			#ifdef _XBOX
				assert(0);
			#else
				exit(attempts ? 2 : 1);
			#endif
		}
		error = pclWait(g_client);

	} while(error == PCL_LOST_CONNECTION);
	HANDLE_ERROR(error);
	if(!g_client)
		return;
	assert(linkConnected(g_client->link) && !linkDisconnected(g_client->link));

	pclVerifyAllFiles(g_client, g_config.verifyAllFiles);
	pclUseFileOverlay(g_client, g_config.fileOverlay);

	FOR_EACH_IN_EARRAY(g_config.extra_paths, char, path)
		pclAddExtraFolder(g_client, path, HOG_NOCREATE|HOG_READONLY);
	FOR_EACH_END

	g_last_display = 0;
	error = pclSetProcessCallback(g_client, displayDownloadProgress, NULL);
	HANDLE_ERROR(error);
	error = pclSetMirrorCallback(g_client, displayMirrorProgress, NULL);
	HANDLE_ERROR(error);

	if(config->prefix)
	{
		error = pclSetPrefix(g_client, config->prefix);
		HANDLE_ERROR(error);
	}

	// Set PCL flags.
	if(config->in_memory)
		pclAddFileFlags(g_client, PCL_IN_MEMORY);
	if(config->metadata_in_memory)
		pclAddFileFlags(g_client, PCL_METADATA_IN_MEMORY);
	if(config->skip_mirroring)
		pclAddFileFlags(g_client, PCL_NO_MIRROR);
	if(config->no_delete)
		pclAddFileFlags(g_client, PCL_NO_DELETE);
	if(config->skip_deleteme)
		pclAddFileFlags(g_client, PCL_NO_DELETEME_CLEANUP);
	if(config->share_hoggs)
		pclSetHoggsSingleAppMode(g_client, false);
	if(config->no_bindiff)
		pclAddFileFlags(g_client, PCL_DISABLE_BINDIFF);
	if(config->xfer_compressed)
		pclAddFileFlags(g_client, PCL_XFER_COMPRESSED);
	// TODO: Fix reconnection
	//if(!config->no_reconnect)
	//	pclAddFileFlags(g_client, PCL_RECONNECT);
	if(config->clean_hogs)
		pclAddFileFlags(g_client, PCL_CLEAN_HOGGS);

	// Set badfiles folder.
	pclSetBadFilesDirectory(g_client, fileTempDir());

	// Set write folder, if specified.
	if(config->write_folder)
		pclSetWriteFolder(g_client, config->write_folder);

	// Enable HTTP patching if requested.
	if (config->http_server)
	{
		const U16 default_port = 80;
		U16 port = default_port;
		if (config->http_port > USHRT_MAX || config->http_port < 0)
		{
			Errorf("Invalid port %d, using %u instead", config->http_port, default_port);
		}
		else if (config->http_port)
			port = config->http_port;
		pclSetHttp(g_client, config->http_server, port, config->http_prefix);
	}
}

// TODO: put game version checking in PatchClientLib
static PatchClientConfig *s_gamesync_config;
static bool s_FoundVersion = false;

static void getStructString(SA_PARAM_NN_NN_STR char **pstr, SA_PARAM_NN_VALID Packet *pak)
{
	StructFreeString(*pstr);
	*pstr = pktGetStringTemp(pak);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**pstr'"
	*pstr = StructAllocString(*pstr);
}

static void handleVersionMsg(SA_PARAM_NN_VALID Packet * pak, int cmd, NetLink * link, void * userData)
{
	if( cmd == FROMVERSION_VERSION	||	// business as usual
		cmd == VERSION_TO_CLIENT	||	// compatibility with some old loginservers
		cmd == VERSION_TO_CLIENT2	  )	// compatibility with some old loginservers
	{
		// If this changes, change gclPatching.c:patchingHandleVersionMessage
		getStructString(&s_gamesync_config->view_name, pak);
		s_gamesync_config->view_time = pktGetBits(pak, 32);
		s_gamesync_config->branch = pktGetBits(pak, 32);
		getStructString(&s_gamesync_config->sandbox, pak);

		printf("\nLoginServer Version: %s %d %d %s\n",
				(s_gamesync_config->view_name?s_gamesync_config->view_name:"\"\""),
				s_gamesync_config->view_time,
				s_gamesync_config->branch,
				(s_gamesync_config->sandbox?s_gamesync_config->sandbox:"\"\"") );
		s_FoundVersion = true;
	}
}

static void handleVersionLink(NetLink * link, void * userData)
{
	// if this changes, change gclPatching.c:patchingGetVersion
	Packet * pak = pktCreate(link, TOVERSION_REQ);
	pktSend(&pak);
}


void xferDoneCallback(PCL_Client * client, char * filename, int status, void * userData)
{
	int * done = userData;
	*done = 1;
}

bool strTrimEndsWith(char *str, char *ending)
{
	if(str && ending)
	{
		size_t str_len = strlen(str);
		size_t end_len = strlen(ending);

		if(stricmp(ending, str+str_len-end_len)==0)
		{
			str[str_len-end_len] = '\0';
			return true;
		}
	}
	return false;
}

void setStructString(char **str, ...)
{
	size_t size = 1;
	char *out, *cat, *pch;

	// get the size
	VA_START(va, str);
	while(cat = va_arg(va,char*))
		size += strlen(cat);
	VA_END();

	// copy it over
	pch = out = alloca(size);
	VA_START(va, str);
	while(cat = va_arg(va,char*))
		for(; *cat; ++cat, ++pch)
			*pch = *cat;
	VA_END();
	*pch = '\0';

	// set the string
	if(*str)
		StructFreeString(*str);
	*str = StructAllocString(out);
}

void readExecutableName(char *executable_name, PatchClientConfig *client_config)
{
	char *exe_name, *pch, *tok, *tok_last = NULL, *tok_project = NULL;

	if(!executable_name || !client_config)
		return;

	strdup_alloca(exe_name,executable_name);
	forwardSlashes(exe_name);
	pch = strrchr(exe_name,'/');
	if(pch)
		exe_name = pch+1;
	pch = NULL;

	for(tok = strtok_s(exe_name,".",&pch); tok; tok = strtok_s(NULL,".",&pch))
	{
#ifdef _XBOX
		if(!tok_last)
			XboxAddSelfRoot(tok); // hack to help find the patcher's location on xbox
#endif
		// TODO: this stuff should be put into a per-project table
		if(strTrimEndsWith(tok,"Patcher") && !tok_project)
		{
			strTrimEndsWith(tok,"Client");
#ifdef _XBOX
			
			strTrimEndsWith(tok,"XBOX");	// make sure we remove both XboxClient
			strTrimEndsWith(tok,"Client");	// and ClientXbox
			setStructString(&client_config->root,"DEVKIT:\\",tok,NULL); // xbox devkit hack
			setStructString(&client_config->project,tok,"XboxClient",NULL);
			XboxAddSelfRoot(client_config->project);
			XboxAddSelfRoot(client_config->root);
#else
			setStructString(&client_config->project,tok,"Client",NULL);
			setStructString(&client_config->root,"./",client_config->project,NULL);
#endif
			setStructString(&client_config->command,"sync",NULL);
			setStructString(&client_config->patchserver,ORGANIZATION_DOMAIN,NULL);
			client_config->port = DEFAULT_PATCHSERVER_PORT;


			tok_project = tok;
		}

		tok_last = tok;
	}
}

void syncByConfig(PatchClientConfig *config)
{
	PCL_ErrorCode error;
	PCLFileSpec filespec = {0};
	PCLFileSpec *use_filespec = NULL;

	// Create g_client and connect to the patchserver
	eaDestroyEx(&g_download_stats, NULL);
	connectToServer();

	// Set the no_write flag if needed
	if(config->no_write)
		pclAddFileFlags(g_client, PCL_NO_WRITE);

	// Set the view // TODO: should use setView()
	if(SAFE_DEREF(config->view_name))
		PCL_DO_WAIT(pclSetNamedView(g_client, config->project, config->view_name, true, config->save_trivia, NULL, NULL));
	else if(config->rev != PATCHREVISION_NONE)
		PCL_DO_WAIT(pclSetViewByRev(g_client, config->project, config->branch, config->sandbox, config->rev, true, config->save_trivia, NULL, NULL));
	else
		PCL_DO_WAIT(pclSetViewByTime(g_client, config->project, config->branch, config->sandbox, config->view_time, true, config->save_trivia, NULL, NULL));

#ifndef _XBOX
	if(g_client && config->clean)
	{
		char root[MAX_PATH];
		pclRootDir(g_client, SAFESTR(root));
		rmdirtreeEx(root, 1);
	}
#endif

	// Convert any hide paths into PCLFileSpec.
	if (eaSize(&config->hide_paths))
	{
		EARRAY_CONST_FOREACH_BEGIN(config->hide_paths, i, n);
		{
			PCLFileSpecEntry* e = callocStruct(PCLFileSpecEntry);
			e->filespec = strdup(config->hide_paths[i]);
			eaPush(&filespec.entriesSecond, e);
		}
		EARRAY_FOREACH_END;
		use_filespec = &filespec;
	}

	// Get the specified files
	if(config->patch_all)
		PCL_DO_WAIT_FRAMES(pclGetAllFiles(g_client, NULL, NULL, use_filespec));
	else
		PCL_DO_WAIT_FRAMES(pclGetRequiredFiles(g_client, true, false, NULL, NULL, use_filespec));
	printf("\n");

	if (config->final_hogg_reload)
	{
		loadstart_printf("Reloading hoggs...");
		PCL_DO(pclUnloadHoggs(g_client));
		PCL_DO(pclLoadHoggs(g_client));
		loadend_printf("done.");
	}

	ANALYSIS_ASSUME(g_client);
	if (pclErrorState(g_client) == PCL_SUCCESS)
		printf("\nPatching was successful!\n");
	else
		printf("\rPatching has FAILED.\n");

	// Clean up.
	eaDestroyEx(&g_download_stats, NULL);
}

int cmpXferInfo(const XferStateInfo * pLeft, const XferStateInfo * pRight)
{
	if(pLeft->start_ticks < pRight->start_ticks)
		return -1;
	else if(pLeft->start_ticks > pRight->start_ticks)
		return 1;
	else
		return 0;
}

static bool displayDownloadProgress(PatchProcessStats *stats, void *userdata)
{
	static F32 elapsed_last;
#ifndef _XBOX
	static U32 percent_last;
	if(stats->received && (stats->elapsed - elapsed_last > (g_config.verbose_stats ? 2 : 0.1) || stats->elapsed <= 0))
#else
	if(stats->received && stats->elapsed - elapsed_last > 1)
#endif
	{
		F32 rec_num, tot_num, trans_num;
		char *rec_units, *tot_units, *trans_units;
		U32 rec_prec, tot_prec, trans_prec;
		U32 percent = stats->received * 100 / stats->total;
		//U32 compression = stats->actual_transferred * 100 / stats->received;
		F32 total_time = stats->elapsed * stats->total / stats->received;
		int percent_http = stats->actual_transferred ? 100*stats->http_actual_transferred/stats->actual_transferred : 0;
		char percent_http_string[256];
		char http_error_string[256];

		if (g_config.verbose_stats)
		{
			const max_files = 5;
			int i;
			char error_string[256];
			PCL_ErrorCode error_error = pclGetErrorString(stats->error, SAFESTR(error_string));
			printf("\n\nVerbose Statistics:\n"
				"received %"FORM_LL"d\n"
				"total %"FORM_LL"d\n"
				"received_files %lu\n"
				"total_files %lu\n"
				"xfers %d\n"
				"buffered %d\n"
				"actual_transferred %"FORM_LL"d\n"
				"overlay_bytes %"FORM_LL"u\n"
				"http_actual_transferred %"FORM_LL"d\n"
				"http_errors %"FORM_LL"d\n"
				"http_header_bytes %"FORM_LL"u\n"
				"http_mime_bytes %"FORM_LL"u\n"
				"http_body_bytes %"FORM_LL"u\n"
				"http_extra_bytes %"FORM_LL"u\n"
				"seconds %lu\n"
				"loops %"FORM_LL"d\n"
				"elapsed %f\n"
				"error %d (\"%s\" [%d])\n",
				stats->received,
				stats->total,
				stats->received_files,
				stats->total_files,
				stats->xfers,
				stats->buffered,
				stats->actual_transferred,
				stats->overlay_bytes,
				stats->http_actual_transferred,
				stats->http_errors,
				stats->http_header_bytes,
				stats->http_mime_bytes,
				stats->http_body_bytes,
				stats->http_extra_bytes,
				stats->seconds,
				stats->loops,
				stats->elapsed,
				stats->error, error_string, error_error);

			printf("Files (%d total, %d not shown)\n", eaSize(&stats->state_info),
				eaSize(&stats->state_info) > max_files ? eaSize(&stats->state_info) - max_files : 0);
			for (i = 0; i != max_files; ++i)
			{
				if (i < eaSize(&stats->state_info))
				{
					XferStateInfo *state = stats->state_info[i];
					printf("  filename %s"
						" state %s"
						" bytes_requested %lu"
						" start_ticks %"FORM_LL"u"
						" blocks_so_far %lu"
						" blocks_total %lu"
						" block_size %lu\n",
						state->filename,
						state->state,
						state->bytes_requested,
						state->start_ticks,
						state->blocks_so_far,
						state->blocks_total,
						state->block_size);
				}
				else
					printf("\n");
			}
			if (eaSize(&stats->state_info) > max_files)
				printf("  ...\n");
			else
				printf("\n");
			printf("\n\n");
		}

		humanBytes(stats->received, &rec_num, &rec_units, &rec_prec);
		humanBytes(stats->total, &tot_num, &tot_units, &tot_prec);
		humanBytes(stats->actual_transferred, &trans_num, &trans_units, &trans_prec);

		percent_http_string[0] = 0;
		if (stats->http_actual_transferred > 0)
			sprintf(percent_http_string, " HTTP: %d%%", percent_http);
		http_error_string[0] = 0;
		if (stats->http_errors > 0)
			sprintf(http_error_string, " (%"FORM_LL"u errors)", stats->http_errors);

		printf("%3d%%  Time: %d:%.2d/%d:%.2d  Files: %d/%d  Data: %.*f%s/%.*f%s  Data transferred: %.*f%s%s%s   \r",
			percent, (U32)stats->elapsed / 60, (U32)stats->elapsed % 60, (U32)total_time / 60, (U32)total_time % 60,
			stats->received_files, stats->total_files, rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, trans_prec, trans_num, trans_units,
			percent_http_string, http_error_string);
#ifndef _XBOX
		if(percent != percent_last)
		{
			char title[MAX_PATH];
			sprintf(title, "Downloaded %d%%", percent);
			setConsoleTitle(title);
			percent_last = percent;
		}
#endif
		elapsed_last = stats->elapsed;
	}
	return false;
}

// Print the mirroring status.
static bool displayMirrorProgress(	PCL_Client* client,
									void* userData,
									F32 elapsed,
									const char* curFileName,
									ProgressMeter* progress)
{
	static U32 lastTime;
	
	if(	timeGetTime() - lastTime > 250 ||
		progress->files_so_far == progress->files_total)
	{
		char title[500];
		
		lastTime = timeGetTime();
		
		sprintf(title,
				"Mirroring %d/%d: %s",
				progress->files_so_far,
				progress->files_total,
				curFileName);
				
		setConsoleTitle(title);

		printf("                                                                           \r%s\r", title);
	}
		
	return true;
}

void testLoop(PatchClientConfig *config)
{
	config->clean = true;

	for(;;)
	{
		memMonitorDisplayStats();
		if(!heapValidateAllReturn())
		{
#ifdef _XBOX
			assert(0);
#else
			exit(3);
#endif
		}

		// PATCHTODO: Make a test loop that runs on the XBox (cleaning is turned off currently)
		syncByConfig(config);
	}
}

void historyCallback(PatcherFileHistory *history, PCL_ErrorCode error, const char *error_details, void * userData)
{
	int i;

	// TODO: Make this call the code in patchmeStat.c if this is ever used

	if(!error)
	{
		for(i = 0; i < eaSize(&history->dir_entry->versions); i++)
			printf("%d %s\n", history->dir_entry->versions[i]->rev, history->checkins[i]->author);
	}
	else
	{
		char msg[MAX_PATH];
		error = pclGetErrorString(error, SAFESTR(msg));
		printf("History request failed: %s\n", msg);
	}
}

void fileHistory(const char *fname, char * project, int branch, char * sandbox, U32 checkout_time)
{
	int error;

	connectToServer();

	PCL_DO_WAIT(pclSetViewByTime(g_client, project, branch, sandbox, checkout_time, true, false, NULL, NULL));
	PCL_DO_WAIT(pclFileHistory(g_client, fname, historyCallback, NULL, NULL));
	g_client = NULL;
}

void connectAndLockFile(char * fname, char * project, int branch, char * sandbox, U32 checkout_time, char * author)
{
	int error;

	connectToServer();

	PCL_DO_WAIT(pclSetViewByTime(g_client, project, branch, sandbox, checkout_time, true, false, NULL, NULL));
	PCL_DO_WAIT(pclSetAuthor(g_client, author, NULL, NULL));
	PCL_DO_WAIT(pclLockFiles(g_client, &fname, 1, NULL, NULL, NULL, NULL, NULL));
	g_client = NULL;
}

void checkinFiles(StringArray fnames, int count, char *comment, char * project, int branch, char * sandbox, char * author)
{
	int error;
	int i;
	int * recurse = NULL;
	for(i = 0; i < eaSize(&fnames); i++)
	{
		eaiPush(&recurse, 1);
	}

	connectToServer();

	PCL_DO_WAIT(pclSetViewLatest(g_client, project, branch, sandbox, true, true, NULL, NULL));
	PCL_DO_WAIT(pclSetAuthor(g_client, author, NULL, NULL));
	PCL_DO_WAIT(pclCheckInFiles(g_client, fnames, fnames, recurse, count, NULL, 0, comment, NULL, NULL));
	g_client = NULL;
}

bool forceinScanDisplay(const char * fileName, int number, int count, bool addedToList, F32 elapsed, PCL_ErrorCode error, const char *error_details, void * userData)
{
	if(elapsed >= 0.1)
	{
		//PATCHTODO: XBox Output
#ifndef _XBOX
		{
			char title[MAX_PATH];
			sprintf(title, "Scanning file %i out of %i", number, count);
			if(error)
			{
				char err[MAX_PATH];
				error = pclGetErrorString(error, SAFESTR(err));
				strcat(title, " - Error: ");
				strcat(title, err);
			}
			setConsoleTitle(title);
		}
#endif
		return true;
	}
	return false;
}

bool uploadDisplay(S64 sent, S64 total, F32 elapsed, PCL_ErrorCode error, const char *error_details, void * userData)
{
	int time_diff;
	U32 speed = 0;
	
	if(elapsed >= 0.1 || sent >= total)
	{
		if(sent == 0)
		{
			upload_start = time(NULL);
		}

		time_diff = time(NULL) - upload_start;
		if(time_diff > 0)
		{
			speed = sent / time_diff;
		}
		// PATCHTODO: XBox Output
#ifndef _XBOX
		{
			char title[MAX_PATH];
			char err[MAX_PATH];
			if(sent < total)
				sprintf(title, "Uploading %0.3f of %0.3f MB; %0.3f MB/s", (F32)sent / (1024*1024),
				(F32)total / (1024*1024), (F32)speed / (1024*1024));
			else
				sprintf(title, "Uploaded %0.3f MB in %i seconds", (F32)sent / (1024*1024), time_diff);
			if(error)
			{
				strcat(title, " - Error: ");
				error = pclGetErrorString(error, SAFESTR(err));
				strcat(title, err);
			}
			setConsoleTitle(title);
		}
#endif
		return true;
	}
	return false;
}

void forceinCallback(int rev, U32 timestamp, PCL_ErrorCode error, const char * error_details, void * userData)
{
	U32 * t = userData;

	if(!error)
		*t = timestamp;
}

void forceinDir(const char*const* dirNames,
				const char*const* counts_as,
				const int* recurse,
				int count,
				const char*const* hide_paths,
				int hide_count,
				const char* project,
				int branch,
				const char* sandbox,
				const char* author,
				const char* comment,
				const char* view_name,
				int days,
				bool matchesonly,
				bool incremental,
				const char *incr_from,
				bool changeroot)
{
	PCL_ErrorCode error;
	U32 timestamp;
// 	char root_dir[MAX_PATH];
// 	int i, root_dir_size;

	connectToServer();

	pclSetForceInScanCallback(g_client, forceinScanDisplay, NULL);
	pclSetUploadCallback(g_client, uploadDisplay, NULL);

	if(incremental)
	{
		int i = incr_from ? atoi(incr_from) : PATCHREVISION_NONE;
		if(i != 0)
			PCL_DO_WAIT(pclSetViewNewIncremental(g_client, project, branch, sandbox, i, true, NULL, NULL));
		else
			PCL_DO_WAIT(pclSetViewNewIncrementalName(g_client, project, sandbox, incr_from, true, NULL, NULL));
	}
	else
		PCL_DO_WAIT(pclSetViewLatest(g_client, project, branch, sandbox, true, true, NULL, NULL));

	if(!comment){
		comment = "forceinfiles";
	}

	PCL_DO_WAIT(pclSetAuthor(g_client, author, NULL, NULL));
	PCL_DO_WAIT(pclForceInFiles(g_client,
								dirNames,
								counts_as,
								recurse,
								count,
								hide_paths,
								hide_count,
								comment,
								!!matchesonly,
								forceinCallback,
								&timestamp));
	if(view_name)
	{
		PCL_DO_WAIT(pclNameCurrentView(g_client, view_name, days, comment, NULL, NULL));
	}
}

bool scanHoggIndices(HogFile * hogg, HogFileIndex index, const char * filename, void * userData)
{
	U32 i = 0;
	HogFileIndex * indices = userData;

	while(indices[i] != HOG_INVALID_INDEX)
		i++;

	indices[i] = index;
	return true;
}

void corruptHogg(char * fname, U32 corruptions)
{
	char * file_mem;
	char * hog_file_mem;
	U32 size, i, j, files, spot;
	U32 hogfilesize;
	U32 timestamp;
	FILE * fout;
	HogFileIndex * indices;
	HogFileIndex hogfile;
	HogFile * hogg;
	int ret=0;

	initRand();
	hogg = hogFileRead(fname, NULL, PIGERR_QUIET, NULL, HOG_DEFAULT);
	if(!hogg)
	{
#ifdef _XBOX
		assert(0);
#else
		exit(6);
#endif
	}

	printf("corruptions: %i\n", corruptions);
	spot = 0;
	for(i = 0; i < corruptions; i++)
	{
		printf("iteration %i\n", i);
		if( randomU32() % 100 == 0 )
		{
			file_mem = fileAlloc(fname, &size);
			spot = (spot + randomU32()) % size;
			file_mem[spot] = (U8)(randomU32() % 256);
			fout = fopen(fname, "wb");
			if(!fout)
			{
#ifdef _XBOX
				assert(0);
#else
				exit(7);
#endif
			}
			fwrite(file_mem, size, 1, fout);
			free(file_mem);
			fclose(fout);
		}
		else
		{
			printf("corrupting hog file\n");
			if(!ret)
			{
				files = hogFileGetNumFiles(hogg);
				indices = _malloca(files * sizeof(HogFileIndex));
				for(j = 0; j < files; j++)
				{
					indices[j] = HOG_INVALID_INDEX;
				}
				hogScanAllFiles(hogg, scanHoggIndices, indices);
				for(j = 0; indices[j] != HOG_INVALID_INDEX; j++);
				hogfile = indices[randomU32() % j];
				hog_file_mem = hogFileExtract(hogg, hogfile, &hogfilesize, NULL);
				while(randomU32() % 100 > 0)
				{
					spot = (spot + randomU32()) % hogfilesize;
					hog_file_mem[spot] = (U8)(randomU32() % 256);
				}
				timestamp = hogFileGetFileTimestamp(hogg, hogfile);
				hogFileModifyUpdateNamed(hogg, hogFileGetFileName(hogg, hogfile), hog_file_mem, hogfilesize, timestamp, NULL);
				//hogFileModifyDelete(hogg, hogfile); free(file_mem);
			}
		}
	}
	hogFileDestroy(hogg, true);
}

// Request that a Patch Server shut down.
void shutdownServer()
{
	PCL_ErrorCode error;

	connectToServer();
	PCL_DO_WAIT(pclShutdown(g_client));
}

// Request that a Patch Server merge its PatchDB.
void mergeServer()
{
	PCL_ErrorCode error;

	connectToServer();
	PCL_DO_WAIT(pclMergeServer(g_client));
}


void nameView(	const char* view_name,
				const char* project,
				int branch,
				const char* comment,
				const char* sandbox,
				U32 checkout_time,
				int rev,
				int days)
{
	PCL_ErrorCode error;

	if(checkout_time == 0)
		checkout_time = INT_MAX;

	connectToServer();
	if (rev == PATCHREVISION_NONE)
		PCL_DO_WAIT(pclSetViewByTime(g_client, project, branch, sandbox, checkout_time, true, false, NULL, NULL));
	else
		PCL_DO_WAIT(pclSetViewByRev(g_client, project, branch, sandbox, rev, true, false, NULL, NULL));
	PCL_DO_WAIT(pclNameCurrentView(g_client, view_name, days, comment, NULL, NULL));
}

void cloneView(char * view_name, char * project, char *clone_name, int days, const char* comment)
{
	PCL_ErrorCode error;
	connectToServer();
	PCL_DO_WAIT(pclSetNamedView(g_client, project, view_name, true, true, NULL, NULL));
	PCL_DO_WAIT(pclNameCurrentView(g_client, clone_name, days, comment, NULL, NULL));
}

void setExpirationCB(PCL_ErrorCode error, const char *error_details, const char *msg, void *userdata)
{
	if(error == PCL_SUCCESS && msg && msg[0])
		printf("Warning: %s\n", msg);
}

void setExpiration(const char *project, const char *view_name, int days)
{
	PCL_ErrorCode error;
	connectToServer();
	PCL_DO_WAIT(pclSetExpiration(g_client, project, view_name, days, setExpirationCB, NULL));
}

void setFileExpirationCB(PCL_ErrorCode error, const char *error_details, void *userdata)
{
	char *string = userdata;
	printf("%s: %s!\n", string, error == PCL_SUCCESS ? "Success" : "Failed");
}

// Set a view on the basis of the options that the user has specified.
static void setView(PatchClientConfig *config, bool load_manifest)
{
	PCL_ErrorCode error;
	devassert(config->project);
	if(config->view_name)
		PCL_DO_WAIT(pclSetNamedView(g_client, config->project, config->view_name, load_manifest, config->save_trivia, NULL, NULL));
	else if(config->rev != PATCHREVISION_NONE)
		PCL_DO_WAIT(pclSetViewByRev(g_client, config->project, config->branch, config->sandbox, config->rev, load_manifest, config->save_trivia, NULL, NULL));
	else if(config->view_time)
		PCL_DO_WAIT(pclSetViewByTime(g_client, config->project, config->branch, config->sandbox, config->view_time, load_manifest, config->save_trivia, NULL, NULL));
	else
		PCL_DO_WAIT(pclSetDefaultView(g_client, config->project, false, NULL, NULL));
}

void setFileExpiration(PatchClientConfig *config, const char **files, int days)
{
	PCL_ErrorCode error;

	connectToServer();

	setView(config, false);

	// Set for each file.
	EARRAY_CONST_FOREACH_BEGIN(files, i, n);
	{
		PCL_DO_WAIT(pclSetFileExpiration(g_client, files[i], days, setFileExpirationCB, (void *)files[i]));
	}
	EARRAY_FOREACH_END;
}

void checkNameCallback(char ** names, int * branches, char ** sandboxes, U32 * revs, char ** comments, U32 * expires, int count,
	PCL_ErrorCode error, const char * error_details, void * userData)
{
	ProjectName * proj_name = userData;
	int i;

	assert(proj_name);

	if(error)
	{
		char err[MAX_PATH];

		error = pclGetErrorString(error, SAFESTR(err));
		printf("There has been some error checking the name: %s\n", err);
#ifdef _XBOX
		assert(0);
#else
		exit(8);
#endif
		return;
	}

	for(i = 0; i < count; i++)
		if(stricmp(proj_name->view_name, names[i]) == 0)
			break;

	printf("Name %s is %sused for project %s\n", proj_name->view_name, (i<count ? "" : "not "), proj_name->project);

	if( i < count && !proj_name->check_exists ||
		i >= count && proj_name->check_exists ) // where's logical xor when you need it?
	{
#ifdef _XBOX
		assert(0);
#else
		exit(9);
#endif
	}
}

void checkName(char *project, char *view_name, int branch, bool check_exists)
{
	PCL_ErrorCode error;
	ProjectName proj_name;

	if(!view_name || !view_name[0])
	{
		printf("No name was entered\n");
#ifdef _XBOX
		assert(0);
#else
		exit(10);
#endif
	}
	if(!project || !project[0])
	{
		printf("No project was entered\n");
#ifdef _XBOX
		assert(0);
#else
		exit(11);
#endif
	}

	connectToServer();

	PCL_DO_WAIT(pclSetViewLatest(g_client, project, branch, "", false, false, NULL, NULL));

	proj_name.view_name = view_name;
	proj_name.project = project;
	proj_name.check_exists = check_exists;

	PCL_DO_WAIT(pclGetNameList(g_client, checkNameCallback, &proj_name));
}

void listNamesCallback(char ** names, int * branches, char ** sandboxes, U32 * revs, char ** comments, U32 * expires, int count,
	PCL_ErrorCode error, const char * error_details, void * userData)
{
	int i;

	if (error)
		return;

	for (i = 0; i != count; ++i)
	{
		printf("%s: branch %d sandbox %s rev %u expires %u comment %s\n",
			names[i],
			branches[i],
			NULL_TO_EMPTY(sandboxes[i]),
			revs[i],
			expires[i],
			comments[i]);
	}
}

void listNames(char *project)
{
	PCL_ErrorCode error;

	connectToServer();
	PCL_DO_WAIT(pclSetDefaultView(g_client, project, false, NULL, NULL));
	PCL_DO_WAIT(pclGetNameList(g_client, listNamesCallback, NULL));
}

void getFile(PCL_Client *client, const char *fname, const char *outfname)
{
	PCL_ErrorCode error;
	bool show = false;

	if(stricmp(outfname, "stdout")==0)
	{
		show = true;
		outfname = "C:/temp/getfile.tmp";
	}

	PCL_DO_WAIT(pclGetFileTo(client, fname, outfname, NULL, 1, NULL, NULL));

	if(show)
	{
		U32 len;
		char *data = fileAlloc(outfname, &len);
		fwrite(data, 1, len, fileGetStdout());
		free(data);
	}
}

// Is completely synced?
static void isCompletelySyncedCallback(PCL_Client* client, bool synced, bool exists, void *userdata)
{
	const char *file = userdata;
	printf("%s: Synced %s, Exists %s\n", file, synced ? "Yes" : "No", exists ? "Yes" : "No");
}

// Check if a path is completely synced.
void isCompletelySynced(char *project, int branch, char *sandbox)
{
	int i;
	PCL_ErrorCode error;

	// Check parameters.
	if (!eaSize(&g_config.files))
	{
		Errorf("No files specified for -iscompletelysynced!");
		exit(12);
	}

	// Connect to server and set view.
	connectToServer();
	setView(&g_config, false);

	// Check each file.
	for(i=0; i<eaSize(&g_config.files); i++)
		PCL_DO_WAIT(pclIsCompletelySynced(g_client, g_config.files[i], isCompletelySyncedCallback,
			g_config.files[i]));
}

// List the files in a directory.
void ListFiles(char *project, int branch, char *sandbox)
{
	int i;
	PCL_ErrorCode error;

	// Check parameters.
	if (!eaSize(&g_config.files))
	{
		Errorf("No files specified for -list!");
		exit(12);
	}

	// Connect to server and set view.
	connectToServer();
	setView(&g_config, true);

	// Check each file.
	for(i=0; i<eaSize(&g_config.files); i++)
	{
		char **children = NULL;
		PCL_DO_WAIT(pclListFilesInDir(g_client, g_config.files[i], &children, NULL, false));
		printf("%s: ", g_config.files[i]);
		EARRAY_CONST_FOREACH_BEGIN(children, j, m);
		{
			if (j)
				printf(", ");
			printf("%s", children[j]);
			free(children[j]);
		}
		EARRAY_FOREACH_END;
		printf("\n");
		eaDestroy(&children);
	}
}

// Load test mode
static int LoadTest(int connections)
{
	int i;

	if (connections < 1)
	{
		printf("You must specify a positive -count.\n");
		return 1;
	}

	// Create connections.
	for (i = 0; i != connections; ++i)
	{
		char status[256];
		autoTimerThreadFrameBegin("pclWait");

		sprintf(status, "Initiating connection %d/%d...\n", i + 1, connections);
		printf("%s", status);
		if (!(i % 10))
			setConsoleTitle(status);

		connectToServer();
		setView(&g_config, false);
		g_client = NULL;

		autoTimerThreadFrameEnd();
	}

	// Print memory report.
	mmplShort();

	// Report that we're done.
	setConsoleTitle("patchclient loadtest waiting");
	printf("\n\n%d connections to server opened, and waiting.  Press Ctrl-C to exit.\n", connections);

	// Wait forever.
	for(;;)
		commMonitor(g_net_comm);

	return 0;
}

#include "patchclientmain_c_ast.c"
