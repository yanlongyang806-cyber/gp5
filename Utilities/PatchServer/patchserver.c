#include "autogen/SVNUtils_h_ast.h"
#include "BlockEarray.h"
#include "ConsoleDebug.h"
#include "cpu_count.h"
#include "crypt.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "HashFunctions.h"
#include "hogutil.h"
#include "logging.h"
#include "LogParsing.h"
#include "netiocp.h"
#include "netipfilter.h"
#include "../net/netpacketutil.h"
#include "piglib_internal.h"
#include "patchcommonutils.h"
#include "patchcompaction.h"
#include "patcher_comm.h"
#include "patcher_comm_h_ast.h"
#include "patchfile.h"
#include "patchfileloading.h"
#include "patchhal.h"
#include "patchhttp.h"
#include "patchhttpdb.h"
#include "patchjournal.h"
#include "patchloadbalance.h"
#include "patchloadbalance_h_ast.h"
#include "patchmirroring.h"
#include "patchproject.h"
#include "patchpruning.h"
#include "patchserver.h"
#include "patchserver_h_ast.h"
#include "patchtracking.h"
#include "patchupdate.h"
#include "rand.h"
#include "ResourceInfo.h"
#include "ServerLib.h"
#include "ScratchStack.h"
#include "sock.h"
#include "statistics.h"
#include "StashSet.h"
#include "StayUp.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "structNet.h"
#include "SVNUtils.h"
#include "sysutil.h"
#include "TimedCallback.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "UnitSpec.h"
#include "utilitiesLib.h"
#include "winutil.h"
#include "zutils.h"
#include "url.h"

extern ParseTable parse_PatchProject[];
#define TYPE_parse_PatchProject PatchProject

#define PATCHSERVER_CONFIGFILE	"./config.txt"
#define PATCHSERVER_DYNAMIC_HTTP_CONFIG "./DynamicHttpConfig.txt"
#define PATCHSERVER_DYNAMIC_HTTP_CONFIG_OLD "./DynamicHttpConfig.old"

#define PATCHSERVER_DYNAMIC_AUTOUP_CONFIG "./DynamicAutoupConfig.txt"
#define PATCHSERVER_DYNAMIC_AUTOUP_CONFIG_OLD "./DynamicAutoupConfig.old"

#ifdef ECHO_log_printfS
#define ERROR_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_ERRORS, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#define INFO_PRINTF(format, ...) {log_printf(LOG_PATCHSERVER_INFO, format, __VA_ARGS__); printf(format, __VA_ARGS__);}
#define CONNECTION_PRINTF(format, ...) log_printf(LOG_PATCHSERVER_CONNECTIONS, format, __VA_ARGS__)
#else
#define ERROR_PRINTF(...) log_printf(LOG_PATCHSERVER_ERRORS, __VA_ARGS__)
#define INFO_PRINTF(...) log_printf(LOG_PATCHSERVER_INFO, __VA_ARGS__)
#define CONNECTION_PRINTF(...) log_printf(LOG_PATCHSERVER_CONNECTIONS, __VA_ARGS__)
#endif

#define DEBUG_DISPLAY_PRINTF(format, ...) {if(s_debug_display) printf(format, __VA_ARGS__);}

#define COMM_MONITOR_WAIT_MSECS 0
#if _WIN64
	#define MAX_PENDING_CHECKINS_BYTES (g_memlimit_checkincache * 1024LL * 1024LL * 1024LL)
#else
	#define MAX_PENDING_CHECKINS_BYTES (128LL * 1024LL * 1024LL)
#endif
#define SECONDS_BETWEEN_MERGES (60.0*15)
#define MAX_REQ_BLOCKS 1000000
#define MAX_REQ_FINGERPRINTS 1000000

// If less than this amount of disk space is available, shut down.
#define EMERGENCY_SHUTDOWN_DISKSPACE (1024ULL*1024*1024*4)

// FIXME: Toggling is disabled because linkCompress() causes crashes inside zlib.
// Although it looks like toggling was on for previous versions, in fast, for all released versions, toggling never actually worked,
// due to a threading bug.
// The long-term plan is to turn off compression for NetLinks entirely.
// #if PATCHER_LINK_COMPRESSION_ON
// #define PATCHER_LINK_COMPRESSION_TOGGLING 1
// #endif

void execMergeProcess(bool time_to_merge);

static NetComm * s_net_comm;
static NetListen ** s_net_listeners;

static U32 s_connections = 0;
static U64 s_sent_bytes_history = 0;
static U64 s_received_bytes_history = 0;
static U64 s_sent_payload = 0;
static U32 s_one_tick_send = 0;
static U32 s_one_tick_messages = 0;
static F32 s_buffer_usage = 0;

U32 g_http_connections = 0;

static U64 s_loopCount = 0;
static F32 s_last_long_tick = 0;
static F32 s_long_tick = 0;
static int s_long_tick_counter = 0;

static int * s_ports = NULL;

static int s_verify_hoggs = 1;
static char **s_verify_project = NULL;			// If verifying, verify only projects in this list.
static int s_verify_no_data = 0;				// Do not load any data when doing verification
static bool s_fatalerror_on_verify_failure = 0;	// Do not load any data when doing verification

static int s_patchserver_fix_hoggs = 0;

static int s_start_down = -1;

static bool s_debug_display = false;

static S64 s_handleMsgTime;

static bool s_server_shutting_down = false;

U64 s_unique_id = 0;

static bool s_periodic_disconnects = false;
static bool s_random_disconnects = false;
static int s_seconds_between_disconnects = 1;
static int s_corruption_freq = 0;

// Server configuration, including all databases and metadata
ServerConfig g_patchserver_config;

static EARRAY_OF(ServerConfig) s_patchserver_config_monitor_array = NULL;

static U32 s_sendMirrorNotify;
static U32 s_lastMirrorNotifyTime;

// Heartbeat timers (name -> timer_id)
static StashTable s_heartbeat_timers = NULL;

// Memory limits (in GB, may not apply in 32-bit mode)
U64 g_memlimit_patchfilecache = 0;
U64 g_memlimit_checkincache = 0;

// All registered child servers: name (string) -> PatchChildServerData *
static StashTable s_child_servers = NULL;

extern bool gbNeverConnectToController;
extern bool gbFixedTokenizerBufferSize;
extern ServerLibState gServerLibState;

// NetLinks that need to be flushed
static StashTable s_links_to_flush = NULL;

// Disable absolute path checking functionality.
bool s_disable_cor_16585 = true;  // TODO: Eliminate this entirely at some point.

int notifyClient(NetLink * link, S32 index, PatchClientLink * client, void * userdata);
void heapDebugDisplay(bool silent);
static void loadConfig(bool merging);

static __forceinline void pktSendTracked(Packet **ppkt, PatchClientLink *client)
{
	if(client->bucket != U32_MAX)
		patchserverGlobalBucketAdd(pktGetSize(*ppkt)*1.05);
	pktSend(ppkt);
}
#define pktSend(ppkt) pktSendTracked((ppkt), client)

AUTO_CMD_INT(s_verify_hoggs, verify);
AUTO_CMD_INT(s_verify_no_data, verify_no_data);
AUTO_CMD_INT(s_fatalerror_on_verify_failure, fatalerror_on_verify_failure);
AUTO_CMD_INT(s_patchserver_fix_hoggs, fix_hoggs);
AUTO_CMD_INT(s_start_down, start_down);
AUTO_CMD_INT(s_corruption_freq, corruption_freq);

// If set to non-zero, log all blocks requested for this connection UID.
static U64 g_log_blocks_uid = 0;
AUTO_CMD_INT(g_log_blocks_uid, log_blocks_uid);

// Control level of verbosity of logging patch commands.
//   0: Log none of them
//   1: Log the ones that not typically generated at high volume (default)
//   2: Log all but the highest volume logs
//   3: Log all of them
static unsigned g_log_patch_cmd_verbosity = 1;
AUTO_CMD_INT(g_log_patch_cmd_verbosity, LogPatchCmdVerbosity);

// There is no config.  This is only supported for running with commands that exit.
static bool g_no_config;
AUTO_CMD_INT(g_no_config, NoConfig) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

StaticDefineInt parse_PruneConfig_value[] =
{
	DEFINE_INT
	{ "*",	-1 },
	DEFINE_END
};

// Allow "DoNotMirror" to be specified for StartAtRevision.
StaticDefineInt parse_BranchToMirror_startAtRevision[] =
{
	DEFINE_INT
	{ "DoNotMirror", INT_MAX },
	{ "*", 0 },
	DEFINE_END
};

// Verify only a specific project, passed once for each project to verify.
AUTO_COMMAND ACMD_COMMANDLINE;
void verify_project(char *project)
{
	eaPush(&s_verify_project, strdup(project));
}

AUTO_COMMAND ACMD_NAME(disconnect);
void turnOnDisconnects(int random, int seconds)
{
	s_seconds_between_disconnects = seconds;
	if(random > 0)
		s_random_disconnects = true;
	else
		s_periodic_disconnects = true;
}

AUTO_COMMAND ACMD_NAME(port);
void addPort(int port)
{
	eaiPush(&s_ports, port);
}

AUTO_COMMAND ACMD_NAME(touchdb);
void touchDb(char *dbname)
{
	PatchDB *db;
	char manifest[MAX_PATH];
	sprintf(manifest, "./%s.manifest", dbname);
	printf("Reserializing %s\nDon't touch anything!\nLoading...\n", manifest);
	db = patchLoadDb(manifest, PATCHDB_POOLED_PATHS, NULL);
	assert(db);
	printf("Writing... (versions of frozen files will be out of order.)\n");
	patchDbWrite(manifest, NULL, db);
	printf("Done.\n");
	exit(0);
}

static void loadEarlyConfig(void);

AUTO_COMMAND ACMD_NAME(merge) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void mergeProjects(int dummy)
{
	int i;
	char title[256];

	s_verify_hoggs = s_patchserver_fix_hoggs = 0;

	setConsoleTitleWithPid("Merger");
	loadEarlyConfig();
	sprintf(title, "Merger - %s", g_patchserver_config.displayName);
	setConsoleTitleWithPid(title);
	loadConfig(true);

	loadstart_printf("Writing manifests");
	for(i = 0; i < eaSize(&g_patchserver_config.serverdbs); i++)
	{
		PatchServerDb *serverdb = g_patchserver_config.serverdbs[i];
		if(serverdb->save_me)
		{
			char manifestFile[MAX_PATH];
			sprintf(manifestFile, "./%s.manifest", serverdb->name);
			loadstart_printf("Writing %s...", manifestFile);
			patchDbWrite(manifestFile, NULL, serverdb->db);
			journalBackup(serverdb->name);
			loadend_printf("");
		}
	}
	loadend_printf("");

	exit(0);
}

AUTO_COMMAND ACMD_NAME(dumpViews) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmd_dumpViews(char *file)
{
	FILE *outf;
	s_verify_hoggs = s_patchserver_fix_hoggs = 0;
	loadConfig(true);

	outf = fopen(file, "w");
	if(!outf)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "Unable to open: %s", file);
		exit(1);
	}

	FOR_EACH_IN_EARRAY(g_patchserver_config.serverdbs, PatchServerDb, serverdb)
		FOR_EACH_IN_EARRAY(serverdb->db->namedviews, NamedView, view)
			fprintf(outf, "%s/%s\n", serverdb->name, view->name);
		FOR_EACH_END
	FOR_EACH_END
	
	fclose(outf);

	exit(0);
}

AUTO_COMMAND ACMD_NAME(setExpires) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmd_setExpires(char *file, U32 expires)
{
	FILE *inf;
	char line[1024];
	
	s_verify_hoggs = s_patchserver_fix_hoggs = 0;
	loadConfig(true);

	inf = fopen(file, "r");
	if(!inf)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "Unable to open: %s", file);
		exit(1);
	}

	while(fgets(line, ARRAY_SIZE_CHECKED(line), inf))
	{
		char *view_name, *tmp, msg[1024];
		PatchProject *proj;

		// Remove trailing newline
		tmp = line + strlen(line) - 1;
		while((*tmp == '\n' || *tmp == '\r') && tmp > line)
		{
			*tmp = '\0';
			tmp -= 1;
		}

		// Extract the view name from the line
		view_name = strchr(line, '/');
		if(!view_name)
			continue;
		*view_name = '\0';
		view_name += 1;

		// Don't set expiration on ignored DBs
		if(g_patchserver_config.prune_config && eaFindString(&g_patchserver_config.prune_config->ignore_projects, line) != -1)
			continue;

		// Find the DB
		proj = patchserverFindProject(line);
		if(!proj || !proj->is_db)
			continue;

		if(!patchserverdbSetExpiration(proj->serverdb, view_name, expires, SAFESTR(msg)))
			printfColor(COLOR_BRIGHT|COLOR_RED, "%s\n", msg);
	}

	fclose(inf);

	exit(0);
}

typedef struct PruneCheckinData { U32 rev; PatchJournal *journal; } PruneCheckinData;
static void pruneCheckinCB(DirEntry *dir, const PruneCheckinData *pcd)
{
	FOR_EACH_IN_EARRAY(dir->versions, FileVersion, ver)
		if(ver->rev == pcd->rev)
		{
			journalAddPrune(pcd->journal, dir->path, ver->rev);
		}
	FOR_EACH_END
}

AUTO_COMMAND ACMD_NAME(pruneCheckin) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmd_pruneCheckin(char *database, U32 rev)
{
	PatchProject *proj;
	PruneCheckinData pcd;

	s_verify_hoggs = s_patchserver_fix_hoggs = 0;
	loadConfig(true);

	// Find the DB
	proj = patchserverFindProject(database);
	if(!proj || !proj->is_db)
		exit(1);

	// Make a journal
	pcd.rev = rev;
	pcd.journal = journalCreate(eaSize(&proj->serverdb->db->checkins) - 1);

	patchForEachDirEntry(proj->serverdb->db, pruneCheckinCB, &pcd);

	journalFlushAndDestroy(&pcd.journal, proj->serverdb->name);

	exit(0);
}

typedef struct PrintChangedFilesData { U32 from; U32 to; int branch; char *path; } PrintChangedFilesData;
static void printChangedFilesCB(DirEntry *dir, const PrintChangedFilesData *pcfd)
{
	if(strStartsWith(dir->path, pcfd->path))
	{
		char *comment = NULL;
		FOR_EACH_IN_EARRAY(dir->versions, FileVersion, ver)
		{
			if(ver->checkin->time >= pcfd->from && ver->checkin->time <= pcfd->to && ver->checkin->branch <= pcfd->branch)
			{
				estrPrintf(&comment, "%s", ver->checkin->comment);
				estrTrimLeadingAndTrailingWhitespace(&comment);
				estrReplaceOccurrences(&comment, "\n", "\n\t\t");
				printf("%s\n\t%d %s \"%s\"\n", ver->parent->path, ver->checkin->rev, ver->checkin->author, comment);
			}
		}
		FOR_EACH_END
		estrDestroy(&comment);
	}
}

AUTO_COMMAND ACMD_NAME(printChangedFiles) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmd_printChangedFiles(char *database, U32 from, U32 to, int branch, char *path)
{
	PatchProject *proj;
	PrintChangedFilesData pcfd;

	s_verify_hoggs = s_patchserver_fix_hoggs = 0;
	loadConfig(true);

	// Find the DB
	proj = patchserverFindProject(database);
	if(!proj || !proj->is_db)
		exit(1);

	pcfd.from = from;
	pcfd.to = to;
	pcfd.path = path;
	pcfd.branch = branch;

	patchForEachDirEntry(proj->serverdb->db, printChangedFilesCB, &pcfd);

	exit(0);
}

// Userdata for dumpBranch_DirEntry()
struct dumpBranch_data
{
  FILE *outf;
  int branch;
};

// Dump FileVersions on branch for entry.
static void dumpBranch_DirEntry(DirEntry *dir, void *userdata)
{
	struct dumpBranch_data *data = userdata;
	FILE *outf = data->outf;
	int branch = data->branch;

	// Print any relevant FileVersions.
	FOR_EACH_IN_EARRAY(dir->versions, FileVersion, version)
	{
		if (version->checkin->branch == branch)
		{
			char *url = NULL;
			estrStackCreate(&url);
			urlEscape(dir->path, &url, true, true);
			fprintf(outf, "%s/?prune=%d&time=%d\n", url, version->checkin->rev, version->checkin->time);
		}
	}
	FOR_EACH_END
}

// Dump all of the FileVersions on a branch, in a way that is easy to use with the web pruning interface.
// This is a one-off command meant for identifying all files on a branch so that they can be blown away,
// but it might be useful for other things as well.
AUTO_COMMAND ACMD_NAME(dumpBranch) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmd_dumpBranch(char *file, char *database, int branch)
{
	FILE *outf;
	bool done = false;

	// Load config.
	s_verify_hoggs = s_patchserver_fix_hoggs = 0;
	loadConfig(true);

	// Open output file.
	outf = fopen(file, "w");
	if(!outf)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "Unable to open: %s", file);
		exit(1);
	}

	// Search for database and dump it.
	FOR_EACH_IN_EARRAY(g_patchserver_config.serverdbs, PatchServerDb, serverdb)
	{
		if (!stricmp_safe(serverdb->name, database))
		{
			struct dumpBranch_data data;
			data.outf = outf;
			data.branch = branch;
			patchForEachDirEntry(serverdb->db, dumpBranch_DirEntry, &data);
			done = true;
			break;
		}
	}
	FOR_EACH_END
	if (!done)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "Unable to find database %s", database);
		exit(2);
	}

	// Close and exit.
	fclose(outf);
	exit(0);
}

// Standard name-value pairs
#define LOG_STANDARD_PAIRS																				\
	("server", "%s", g_patchserver_config.displayName)													\
	("uid", "%"FORM_LL"u", client->UID)																	\
	("ip", "%s", client->ipstr)																			\
	("function", "%s", __FUNCTION__)

// Report that the packet command has something wrong with it that is preventing regular processing.
#define LOG_BAD_PACKET()																				\
	do {																								\
		if (g_log_patch_cmd_verbosity)																	\
		{																								\
			SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "BadPacket",											\
				LOG_STANDARD_PAIRS																		\
			);																							\
		}																								\
	} while (0)

#define LOG_PATCH_START																					\
	do {																								\
		if (g_log_patch_cmd_verbosity >= 1)																\
		{
#define LOG_PATCH_START_VERBOSE2																		\
	do {																								\
		if (g_log_patch_cmd_verbosity >= 2)																\
		{
#define LOG_PATCH_START_VERBOSE3																		\
	do {																								\
		if (g_log_patch_cmd_verbosity >= 3)																\
		{
#define LOG_PATCH_STOP																					\
		}																								\
	} while (0)

// PatchCmd "type" pairs, used below.
// These two are separated out to hide the comma from the __VA_ARGS__ expander.  Normally, if
// LOG_PATCH_COMMAND() is called with no parameters, the __VA_ARGS__ expander will eat a random comma,
// which was inside this pair.  Putting it into a macro prevents this, and seems to prevent any other
// commas from being consumed also.
#define LOG_PATCH_COMMAND_TYPE ("type", "request")
#define LOG_PATCH_RESPONSE_TYPE ("type", "response")

// Log a command packet and its arguments.
#define LOG_PATCH_COMMAND(...)																			\
	LOG_PATCH_START																						\
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "PatchCmd",														\
		LOG_PATCH_COMMAND_TYPE LOG_STANDARD_PAIRS __VA_ARGS__);											\
	LOG_PATCH_STOP
#define LOG_PATCH_COMMAND_VERBOSE2(...)																	\
	LOG_PATCH_START_VERBOSE2																			\
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "PatchCmd",														\
		LOG_PATCH_COMMAND_TYPE LOG_STANDARD_PAIRS __VA_ARGS__);											\
	LOG_PATCH_STOP
#define LOG_PATCH_COMMAND_VERBOSE3(...)																	\
	LOG_PATCH_START_VERBOSE3																			\
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "PatchCmd",														\
		LOG_PATCH_COMMAND_TYPE LOG_STANDARD_PAIRS __VA_ARGS__);											\
	LOG_PATCH_STOP

// Log a command packet and its arguments.
#define LOG_PATCH_RESPONSE(...)																			\
	LOG_PATCH_START																						\
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "PatchCmd",														\
		LOG_PATCH_RESPONSE_TYPE LOG_STANDARD_PAIRS __VA_ARGS__);										\
	LOG_PATCH_STOP
#define LOG_PATCH_RESPONSE_VERBOSE2(...)																\
	LOG_PATCH_START_VERBOSE2																			\
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "PatchCmd",														\
		LOG_PATCH_RESPONSE_TYPE LOG_STANDARD_PAIRS __VA_ARGS__);										\
	LOG_PATCH_STOP
#define LOG_PATCH_RESPONSE_VERBOSE3(...)																\
	LOG_PATCH_START_VERBOSE3																			\
	SERVLOG_PAIRS(LOG_PATCHSERVER_INFO, "PatchCmd",														\
		LOG_PATCH_RESPONSE_TYPE LOG_STANDARD_PAIRS __VA_ARGS__);										\
	LOG_PATCH_STOP

// Create log sequence for some standard project-related data
#define PATCH_PROJECT_CONTEXT																			\
	("context_project", "%s", NULL_TO_EMPTY(SAFE_MEMBER(client->project, name)))						\
	("context_sandbox", "%s", NULL_TO_EMPTY(client->sandbox))											\
	("context_branch", "%d", client->branch)															\
	("context_prefix", "%s", NULL_TO_EMPTY(client->prefix))												\
	("context_rev", "%d", client->rev)																	\
	("context_author", "%s", client->author)

// Handle a bad packet.
static void badPacket(PatchClientLink *client, const char *location, const char *context, int cmd, int extra1, int extra2, int extra3, const char *reason)
{
	Packet *pak;

	// Log error.
	ERROR_PRINTF("Bad Packet: %s (%s) %s\n", NULL_TO_EMPTY(location), NULL_TO_EMPTY(context), NULL_TO_EMPTY(reason));
	LOG_BAD_PACKET();

	// Send back error packet.
	pak = pktCreate(client->link, PATCHSERVER_BAD_PACKET);
	pktSendU32(pak, cmd);
	pktSendU32(pak, extra1);
	pktSendU32(pak, extra2);
	pktSendU32(pak, extra3);
	pktSendString(pak, reason);
	pktSend(&pak);
}

// Like badPacket(), but specific for when common command prerequisites (such as having a valid view) are not met.
static void badPacketPrereqFailed(PatchClientLink *client, const char *location, int cmd)
{
	badPacket(client, location, NULL, cmd, 0, 0, 0, "command prerequisites failed");
}

// Convenience wrapper for badPacketPrereqFailed().
#define BADPACKET_PREREQ_FAILED(cmd) do {badPacketPrereqFailed(client, __FUNCTION__, (cmd));} while(0)

static void dontTalkToMe(PatchClientLink *client, char *msg);

static U32 heartbeatGetTimer(const char *name)
{
	U32 timer_id;
	if(!s_heartbeat_timers)
		s_heartbeat_timers = stashTableCreateWithStringKeys(8, StashDeepCopyKeys_NeverRelease);
	if(!stashFindInt(s_heartbeat_timers, name, &timer_id))
	{
		timer_id = timerAlloc();
		stashAddInt(s_heartbeat_timers, name, timer_id, true);
	}
	return timer_id;
}

void heartbeatReset(const char *name)
{
	timerStart(heartbeatGetTimer(name));
}

F32 heartbeatTime(const char *name)
{
	return timerElapsed(heartbeatGetTimer(name));
}

typedef struct CheckinFile
{
	U8		*data;
	char	*fname;
	int		size_to_receive;
	int		size_uncompressed;
	U32		checksum;
	U32		modified;
	U64		last_used;
} CheckinFile;

typedef struct BlockReq
{
	int start;
	int count;
} BlockReq;

// Get human-readable Patch Server release version string.
const char *patchserverVersion()
{
	static char retString[200] = "";
	ATOMIC_INIT_BEGIN;
	if (!*retString)
	{
		bool bFullDebug = false;
		bool bWin64 = false;
#ifdef _FULLDEBUG
		bFullDebug = true;
#endif
#ifdef _WIN64
		bWin64 = true;
#endif
		sprintf_s(SAFESTR(retString), "Patch Server "
			CRYPTIC_PATCHSERVER_VERSION_SHORT
			" " CRYPTIC_PATCHSERVER_VERSION_TYPE
			", \"%s\" %s %s %s",
			GetUsefulVersionString(),
			bFullDebug ? "(full debug)" : "(not full debug)",
			bWin64 ? "(64-bit)" : "(32-bit)",
			isProductionMode() ? "(production mode)" : "(development mode)");
	}
	ATOMIC_INIT_END;
	return retString;
}

TextParserResult fixupAllowIp(AllowIp *allow, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if(eFixupType == FIXUPTYPE_POST_TEXT_READ)
	{
		int i;
		char *ip_byte_start = allow->ip_str;

		for(i = 0; i < 4; i++)
		{
			if(ip_byte_start)
			{
				allow->ip_bytes[i] = atoi(ip_byte_start);
				allow->ip_match[i] = (*ip_byte_start != '*');
				ip_byte_start = strchr(ip_byte_start, '.');
				if(ip_byte_start)
					ip_byte_start++;
			}
			else
			{
				allow->ip_bytes[i] = 0;
				allow->ip_match[i] = 0;
			}
		}
	}
	return PARSERESULT_SUCCESS;
}

bool allowipCheck(AllowIp **allowips, U32 ip)
{
	int i, j;
	for(i = 0; i < eaSize(&allowips); i++)
	{
		AllowIp *allow = allowips[i];
		for(j = 0; j < 4; j++)
			if( allow->ip_match[j] && // this byte requires a match
				allow->ip_bytes[j] != ((ip >> (8*j)) & 255) ) // the byte doesn't match
				break;
		if(j >= 4) // all 4 bytes match
			return true;
	}
	return false;
}

// Check an IP against an allow and deny list.  The deny list has higher priority, and deny is the default.
bool checkAllowDeny(AllowIp **allowips, AllowIp **denyips, U32 ip)
{
	if (allowipCheck(denyips, ip))
		return false;
	if (allowipCheck(allowips, ip))
		return true;
	return false;
}

ParseTable parse_config_PatchServerDb[] = 
{
	{ "PatchServerDb",	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(PatchServerDb), 0, NULL, 0 },
	{ "name",			TOK_STRUCTPARAM | TOK_POOL_STRING | TOK_STRING(PatchServerDb, name, 0), NULL },
	{ "\n",				TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_config_PatchProject[] = 
{
	{ "PatchProject",			TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(PatchProject), 0, NULL, 0 },
	{ "{",						TOK_START, 0 },
	{ "name",					TOK_STRUCTPARAM | TOK_POOL_STRING | TOK_STRING(PatchProject, name, 0), NULL },
	{ "AllowIp",				TOK_STRUCT(PatchProject, allow_ips, parse_AllowIp) },
	{ "DenyIp",					TOK_STRUCT(PatchProject, deny_ips, parse_AllowIp) },
	{ "AllowFullManifestIp",	TOK_STRUCT(PatchProject, allowFullManifest_ips, parse_AllowIp) },
	{ "DenyFullManifestIp",		TOK_STRUCT(PatchProject, denyFullManifest_ips, parse_AllowIp) },
	{ "UseForFullManifest",		TOK_BOOL(PatchProject, useForFullManifest, false) },
	{ "}",						TOK_END, 0 },
	{ "", 0, 0 }
};

AUTO_RUN;
void patchserverFixupParseTables(void)
{
	ParserSetTableInfo(parse_config_PatchServerDb, sizeof(PatchServerDb), "config_PatchServerDb", NULL, "patchserver.c", SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_config_PatchProject, sizeof(PatchProject), "config_PatchProject", NULL, "patchserver.c", SETTABLEINFO_ALLOW_CRC_CACHING);
}

void patchserverFixupDbHierarchy(void) // done separately so we don't have to force ordering on the user
{
	int i;
	for(i = 0; i < eaSize(&g_patchserver_config.serverdbs); i++)
	{
		PatchServerDb *serverdb = g_patchserver_config.serverdbs[i];
		if(serverdb->basedb_name && !serverdb->basedb)
		{
			PatchProject *project = patchserverFindProject(serverdb->basedb_name);
			if(!project || !project->serverdb)
				FatalErrorf("BaseDatabase %s specified by database %s is not defined", serverdb->basedb_name, serverdb->name);
			else
				serverdb->basedb = project->serverdb;
		}
	}
}

extern ServerLibState gServerLibState;

static void loadEarlyConfig(void)
{
	char * getcwd_ret;
	char curr_dir[MAX_PATH];
	ServerConfig patchserver_config = {0};

	getcwd_ret = fileGetcwd(curr_dir,MAX_PATH);
	assert(getcwd_ret != NULL);
	forwardSlashes(curr_dir);

	if(!fileExists(PATCHSERVER_CONFIGFILE))
		FatalErrorf("Could not find config file %s/%s", curr_dir, PATCHSERVER_CONFIGFILE);

	StructInit(parse_ServerConfig, &patchserver_config);
	if(!ParserReadTextFile(PATCHSERVER_CONFIGFILE, parse_ServerConfig, &patchserver_config, 0))
		FatalErrorf("Could not parse config file %s/%s", curr_dir, PATCHSERVER_CONFIGFILE);

	// Verify config file.
	if (patchserver_config.dynamic_http_config)
		FatalErrorf("Dynamic config not allowed here.");

	if(patchserver_config.log_server)
		sprintf(gServerLibState.logServerHost, "%s", patchserver_config.log_server);

	gServerLibState.iGenericHttpServingPort = patchserver_config.monitor_port
		? patchserver_config.monitor_port : DEFAULT_WEBMONITOR_PATCHSERVER;

	g_memlimit_patchfilecache = patchserver_config.mem_limits.patchfile;
	g_memlimit_checkincache = patchserver_config.mem_limits.checkin;
	if (patchserver_config.max_net_bytes)
		patchupdateSetMaxNetBytes(patchserver_config.max_net_bytes);

	if(patchserver_config.parent.server && patchserver_config.parent.server[0] && patchserver_config.full_mirror_every)
	{
		char **files = NULL;
		loadstart_printf("Deleting journal files...");
		files = fileScanDirNoSubdirRecurse(".");
		FOR_EACH_IN_EARRAY(files, char, file)
			if(strEndsWith(file, ".journal") || strEndsWith(file, ".journal.merge"))
			{
				devassert(unlink(file) == 0);
			}
		FOR_EACH_END
		eaDestroyEx(&files, NULL);
		loadend_printf("");
	}

	g_patchserver_config.displayName = strdup(patchserver_config.displayName);

	StructDeInit(parse_ServerConfig, &patchserver_config);
}

static void loadConfig(bool merging)
{
	int i, count;
	char * getcwd_ret;
	char curr_dir[MAX_PATH];
	int success;

	ipfLoadDefaultFilters();

	getcwd_ret = fileGetcwd(curr_dir,MAX_PATH);
	assert(getcwd_ret != NULL);
	forwardSlashes(curr_dir);

	if(!fileExists(PATCHSERVER_CONFIGFILE))
		FatalErrorf("Could not find config file %s/%s", curr_dir, PATCHSERVER_CONFIGFILE);

	StructInit(parse_ServerConfig, &g_patchserver_config);
	if(!ParserReadTextFile(PATCHSERVER_CONFIGFILE, parse_ServerConfig, &g_patchserver_config, 0))
		FatalErrorf("Could not parse config file %s/%s", curr_dir, PATCHSERVER_CONFIGFILE);

	// Verify config file.
	if (g_patchserver_config.dynamic_http_config)
		FatalErrorf("Dynamic config not allowed here.");

	// Load HTTP patching dynamic config.
	if (fileExists(PATCHSERVER_DYNAMIC_HTTP_CONFIG))
	{
		g_patchserver_config.dynamic_http_config = StructCreate(parse_DynamicHttpConfig);
		success = ParserReadTextFile(PATCHSERVER_DYNAMIC_HTTP_CONFIG, parse_DynamicHttpConfig, g_patchserver_config.dynamic_http_config, 0);
		if (!success)
			FatalErrorFilenamef(PATCHSERVER_DYNAMIC_HTTP_CONFIG, "HTTP config file load failed");
	}

	// Load Autoupdate patching dynamic config.
	if(fileExists(PATCHSERVER_DYNAMIC_AUTOUP_CONFIG))
	{
		g_patchserver_config.dynamic_autoup_config = StructCreate(parse_DynamicAutoupConfig);
		success = ParserReadTextFile(PATCHSERVER_DYNAMIC_AUTOUP_CONFIG, parse_DynamicAutoupConfig, g_patchserver_config.dynamic_autoup_config, 0);
		if(!success)
			FatalErrorFilenamef(PATCHSERVER_DYNAMIC_AUTOUP_CONFIG, "Autoupdate config file load failed");
	}

	if(!g_patchserver_config.notifyMirrorsPeriod)
	{
		g_patchserver_config.notifyMirrorsPeriod = 60;
	}

	count = eaSize(&g_patchserver_config.projects);
	g_patchserver_config.project_stash = stashTableCreateWithStringKeys(count + (count>>1), StashDefault);
	for(i = 0; i < count; i++)
	{
		PatchProject *project = g_patchserver_config.projects[i];
		if(!project->name)
			FatalErrorf("Nameless project in %s", g_patchserver_config.filename);
		if(!stashAddPointer(g_patchserver_config.project_stash, project->name, project, false))
			FatalErrorf("Could not add project %s, it was probably defined twice in %s", project->name, g_patchserver_config.filename);
	}

	count = eaSize(&g_patchserver_config.autoupdates);
	if(g_patchserver_config.autoupdatedb)
	{
		eaInsert(&g_patchserver_config.serverdbs, g_patchserver_config.autoupdatedb, 0);
		g_patchserver_config.autoupdate_stash = stashTableCreateWithStringKeys(count + (count>>1), StashDefault);
		for(i = 0; i < count; i++)
		{
			AutoUpdateFile *autoupdate = g_patchserver_config.autoupdates[i];
			if(!autoupdate->token || !autoupdate->token[0])
				FatalErrorf("Tokenless autoupdate in %s", g_patchserver_config.filename);
			if(!stashAddPointer(g_patchserver_config.autoupdate_stash, autoupdate->token, autoupdate, false))
				FatalErrorf("Could not add autoupdate %s, it was probably defined twice in %s", autoupdate->token, g_patchserver_config.filename);
		}
	}
	else if(count)
	{
		FatalErrorf("AutoUpdates are defined but there is no AutoUpdateDatabase in %s", g_patchserver_config.filename);
	}

	for(i = 0; i < eaSize(&g_patchserver_config.serverdbs); i++)
	{
		PatchServerDb *serverdb = g_patchserver_config.serverdbs[i];
		assert(!patchserverdbLoad(serverdb, s_verify_hoggs, s_verify_project, s_patchserver_fix_hoggs, !s_verify_no_data, s_fatalerror_on_verify_failure, merging)); // not handling config file reloading yet
		if(!serverdb->db) // the serverdb couldn't be loaded. we may need to download it from a parent.
		{
			PatchProject *project = patchserverFindProject(serverdb->name);
			if(project)
				project->is_db = true; // mark this for convenience later
		}
	}
	patchserverFixupDbHierarchy();

	// warn about unused project entries in the config, but leave them in case they get loaded later
	for(i = eaSize(&g_patchserver_config.projects)-1; i >= 0; --i)
		if(!g_patchserver_config.projects[i]->serverdb)
			printf("Warning: project %s specified in %s is not loaded by any database\n", g_patchserver_config.projects[i]->name, g_patchserver_config.filename);

	// Add the Patch Server configuration to the server monitor.
	eaPush(&s_patchserver_config_monitor_array, &g_patchserver_config);
	resRegisterDictionaryForEArray("Configuration", RESCATEGORY_OTHER, 0, &s_patchserver_config_monitor_array,
		parse_ServerConfig);
}

PatchProject* patchserverFindProject(const char *name)
{
	PatchProject *project = NULL;
	if(name)
		stashFindPointer(g_patchserver_config.project_stash, name, &project);
	return project;
}

PatchProject* patchserverFindOrAddProject(const char *name)
{
	PatchProject *project = patchserverFindProject(name);
	if(!project)
	{
		printf("Warning: project %s is not defined in the server config, it won't be accessible by clients\n", name);
		log_printf(LOG_PATCHSERVER_GENERAL, "project %s referenced by a .patchserverdb file, but it isn't declared in the server's config", name);
		project = StructAlloc(parse_PatchProject);
		project->name = allocAddFilename(name); // pooled
	}
	return project;
}

PatchProject* patchserverFindProjectChecked(const char *name, U32 ip)
{
	PatchProject *project = patchserverFindProject(name);
	if(project && (!project->serverdb || !checkAllowDeny(project->allow_ips, project->deny_ips, ip)) )
	{
		log_printf(LOG_PATCHSERVER_GENERAL, "client (ip %x) tried to access project %s, but it's not loaded or the client does not have access", ip, name);
		project = NULL;
	}
	return project;
}

void iterateAllPatchLinks(LinkCallback2 callback, void *userdata)
{
	int i;
	for(i = 0; i < eaSize(&s_net_listeners); i++)
		linkIterate2(s_net_listeners[i], callback, userdata);
}

static char *g_child_status = NULL;

static int concatChildStatus(NetLink *link, S32 index, PatchClientLink *client, void *userdata)
{
	if(client->autoupdate_token && strStartsWith(client->autoupdate_token, "PatchServer"))
		estrConcatf(&g_child_status, "Mirror Child: <b><a href='http://%s/'>%s</a></b> (%s)<br>\n", client->ipstr, client->ipstr,
										client->notify_me ? "waiting" : "updating");
	return 1;
}

const char *patchserverChildStatus(void)
{
	estrPrintf(&g_child_status, "");
	iterateAllPatchLinks(concatChildStatus, NULL);
	return g_child_status;
}



static int patchserverGetClientsCB(NetLink *link, S32 index, PatchClientLink *client, PatchClientLink ***clients)
{
	eaPush(clients, client);
	return 1;
}

void patchserverGetClients(PatchClientLink ***clients)
{
	int i;
	for(i = 0; i < eaSize(&s_net_listeners); i++)
		linkIterate2(s_net_listeners[i], patchserverGetClientsCB, clients);
}

static void clientWaitingRequest(void *userdata, PatchFile *patch, PatchClientLink *client, NetLink *link, int req, int id, int extra, int num_block_reqs, BlockReq *block_reqs);

void sendAutoUpdate(PatchFile *patch, PatchClientLink *client)
{
	if(	patch->load_state >= LOADSTATE_COMPRESSED_ONLY &&
		patch->compressed.len)
	{
		Packet *pak_out = pktCreate(client->link, PATCHSERVER_AUTOUPDATE_FILE);
#if PATCHER_LINK_COMPRESSION_TOGGLING
		linkCompress(client->link, 0);
#endif
		pktSendBool(pak_out, true); // compressed
		pktSendZippedAlready(pak_out, patch->uncompressed.len, patch->compressed.len, patch->compressed.data);
		pktSend(&pak_out);
#if PATCHER_LINK_COMPRESSION_TOGGLING
		linkCompress(client->link, 1);
#endif
		LOG_PATCH_RESPONSE(("filename", "%s", NULL_TO_EMPTY(patch->fileName.name)) ("compressed", "1")
			("uncompressed_len", "%lu", patch->uncompressed.len) ("compressed_len", "%lu", patch->compressed.len));
	}
	else if(patch->load_state >= LOADSTATE_ALL)
	{
		Packet *pak_out = pktCreate(client->link, PATCHSERVER_AUTOUPDATE_FILE);
		pktSendBool(pak_out, false); // not compressed
		pktSendU32(pak_out, patch->uncompressed.len);
		pktSendBytes(pak_out, patch->uncompressed.len, patch->uncompressed.data);
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("filename", "%s", NULL_TO_EMPTY(patch->fileName.name))
			("compressed", "0") ("uncompressed_len", "%lu", patch->uncompressed.len));
	}
	else // Not-loaded file, queue up a request
	{
		WaitingRequest *request = calloc(sizeof(WaitingRequest), 1);
		ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
		request->req = PATCHCLIENT_REQ_AUTOUPDATE;
		request->callback = clientWaitingRequest;
		patchfileRequestLoad(patch, true, request);
	}
}

void sendFileInfo(int id, PatchFile *patch, PatchClientLink *client)
{
	bool slipstream = false;

	assert(client && client->link);

	if(	!patch ||
		patch->load_state == LOADSTATE_ERROR) // Invalid file
	{
		// Should anything get sent back if a requested file doesn't exist?
		U32 pktSize;

		Packet *pak = pktCreate(client->link, PATCHSERVER_FILEINFO);
		pktSendBits(pak, 32, id);
		pktSendBits(pak, 32, -1);
		pktSendBits(pak, 32, 0);
		pktSendBits(pak, 32, 0);
		// Older clients stop reading here (see the normal packet below)
		pktSendBits(pak, 32, 0);
		pktSendBits(pak, 32, 0);
		pktSend(&pak);

		pktSize = 0;
		s_one_tick_send += pktSize;
		s_one_tick_messages++;
		// error message given by handleReqFileInfo
		return;
	}
	
	// Check if we want to slipstream this file
	if(patch->load_state >= LOADSTATE_INFO_ONLY && patch->compressed.len && patch->compressed.len <= g_patchserver_config.slipstream_threshold)
		slipstream = true;
	
	if(patch->load_state >= (slipstream ? LOADSTATE_COMPRESSED_ONLY : LOADSTATE_INFO_ONLY))
	{
		int i;
		U32 pktSize;
		Packet *pak;
		int rev = -1;

		// Compute the packet size
		pktSize = 42;
		for(i=0; i<patch->uncompressed.num_print_sizes; i++)
			pktSize += 4;
		if(patch->compressed.len)
		{
			pktSize += 8;
			for(i=0; i<patch->compressed.num_print_sizes; i++)
				pktSize += 4;
		}
		if(slipstream)
			pktSize += patch->compressed.len;
		if(client->bucket != U32_MAX && patch->ver)
		{
			if(client->bucket < pktSize || patchserverGlobalBucketSizeLeft() < pktSize)
			{
				// Not enough bandwidth to send this packet, queue it up.
				WaitingRequest * request = calloc(sizeof(WaitingRequest), 1);
				ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
				request->req = PATCHCLIENT_REQ_FILEINFO;
				request->callback = clientWaitingRequest;
				request->id = id;
				request->bytes = pktSize;
				request->fname = strdup(patch->fileName.realPath);
				patchserverQueueThrottledRequest(request);
				PERFINFO_AUTO_STOP_FUNC();
				return;
			}
			client->bucket -= pktSize;
		}
		//trackSentData(pktSize);

		pak = pktCreateSize(client->link, pktSize, PATCHSERVER_FILEINFO);
		pktSendBits(pak, 32, id);
		pktSendBits(pak, 32, patch->uncompressed.len);
		pktSendBits(pak, 32, patch->compressed.len);
		pktSendBits(pak, 32, patch->uncompressed.crc);
		pktSendBits(pak, 32, patch->uncompressed.num_print_sizes);
		for(i = 0; i < patch->uncompressed.num_print_sizes; i++)
			pktSendBits(pak, 32, patch->uncompressed.print_sizes[i]);
		// Older patchclients stop reading here. It'd be nice to rearrange this, but I don't want to increment the version. -GG 11/06/07
		if(patch->compressed.len)
		{
			pktSendBits(pak, 32, patch->compressed.crc);
			pktSendBits(pak, 32, patch->compressed.num_print_sizes);
			for(i = 0; i < patch->compressed.num_print_sizes; i++)
				pktSendBits(pak, 32, patch->compressed.print_sizes[i]);
		}
		// Patchclients stop reading here if uncompressed.len == -1 and uncompressed.crc == 0 (i.e. invalid file above)
		pktSendBits(pak, 32, patch->filetime);

		pktSendBool(pak, slipstream);
		// Patchclients skip reading this if the last bool was false
		if(slipstream)
			pktSendBytes(pak, patch->compressed.len, patch->compressed.data);

		// Send actual file revision.
		if (patch->ver)
			rev = patch->ver->rev;
		pktSendBits(pak, 32, rev);

		pktSend(&pak);

		LOG_PATCH_RESPONSE_VERBOSE2(("id", "%d", id)
			("uncompressed_len", "%lu", patch->uncompressed.len)
			("compressed_len", "%lu", patch->compressed.len)
			("uncompressed_crc", "%lu", patch->uncompressed.crc)
			("uncompressed_num_print_sizes", "%d", patch->uncompressed.num_print_sizes)
			("compressed_crc", "%lu", patch->compressed.crc)
			("compressed_num_print_sizes", "%lu", patch->compressed.num_print_sizes)
			("filetime", "%lu", id)
			("slipstream", "%d", !!slipstream)
			("rev", "%d", rev)
		);

		pktSize = 0;
		s_one_tick_send += pktSize;
		s_one_tick_messages++;
		DEBUG_DISPLAY_PRINTF("File info for %s, %i bytes\n", patchFileGetUsedName(patch), pktSize);
	}
	else // Not-loaded file, queue up a request
	{
		WaitingRequest * request = calloc(sizeof(WaitingRequest), 1);
		ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
		request->req = PATCHCLIENT_REQ_FILEINFO;
		request->callback = clientWaitingRequest;
		request->id = id;
		patchfileRequestLoad(patch, false, request);
	}
}

// Check for load errors, and report if there are errors.
static bool areThereLoadErrors(PatchFile *patch, bool compressed, const char *location, const char *context)
{
	if (patch->load_state == LOADSTATE_ERROR)
	{
		char *error = NULL;
		estrStackCreate(&error);
		estrPrintf(&error, "Load error: %s%s (%s) %s load_state %d\n", location, compressed ? ", compressed" : ", uncompressed", context,
			patchFileGetUsedName(patch), (int)patch->load_state);
		ERROR_PRINTF("%s", error);
		AssertOrAlert("PATCHSERVER_LOADERROR", "%s", error);
		estrDestroy(&error);
		return true;
	}
	return false;
}

void sendFingerprints(int id, int print_idx, int num_block_reqs, BlockReq *block_reqs, bool compressed, PatchFile *patch, PatchClientLink *client, char *context)
{
	Packet * pak = NULL;
	int i;
	U32 pktSize = 0;
	int cmd;
	PatchFileData *filedata;

	PERFINFO_AUTO_START_FUNC();
	
	assert(patch && client && client->link);

	// Check for load errors, and report if there are errors.
	if (areThereLoadErrors(patch, compressed, __FUNCTION__, context))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If the file isn't loaded, queue up a request with the loader instead
	if( patch->load_state < LOADSTATE_COMPRESSED_ONLY ||
		patch->load_state < LOADSTATE_ALL && !compressed )
	{
		WaitingRequest * request = calloc(1,sizeof(WaitingRequest)); // freed in respondToLoad
		request->block_reqs = calloc(num_block_reqs,sizeof(BlockReq));
		ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
		request->req = compressed ? PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED : PATCHCLIENT_REQ_FINGERPRINTS;
		request->callback = clientWaitingRequest;
		request->id = id;
		request->print_idx = print_idx;
		request->num_block_reqs = num_block_reqs;
		memcpy(request->block_reqs, block_reqs, num_block_reqs*sizeof(BlockReq));
		patchfileRequestLoad(patch, !compressed, request);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Compute the packet size
	pktSize = 25;
	for(i=0; i<num_block_reqs; i++)
		pktSize += (block_reqs[i].count * 4) + 8;
	if(client->bucket != U32_MAX && patch->ver)
	{
		if(client->bucket < pktSize || patchserverGlobalBucketSizeLeft() < pktSize)
		{
			// Not enough bandwidth to send this packet, queue it up.
			WaitingRequest * request = calloc(1,sizeof(WaitingRequest)); // freed in patchserverHandleWaitingRequest
			request->block_reqs = calloc(num_block_reqs,sizeof(BlockReq));
			ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
			request->req = compressed ? PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED : PATCHCLIENT_REQ_FINGERPRINTS;
			request->callback = clientWaitingRequest;
			request->id = id;
			request->print_idx = print_idx;
			request->num_block_reqs = num_block_reqs;
			memcpy(request->block_reqs, block_reqs, num_block_reqs*sizeof(BlockReq));
			request->bytes = pktSize;
			request->fname = strdup(patch->ver->parent->path);
			patchserverQueueThrottledRequest(request);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		client->bucket -= pktSize;
	}
	//trackSentData(pktSize);

	if(compressed)
	{
		cmd = PATCHSERVER_FINGERPRINTS_COMPRESSED;
		filedata = &patch->compressed;
	}
	else
	{
		cmd = PATCHSERVER_FINGERPRINTS;
		filedata = &patch->uncompressed;
	}

#define QUIT_HANDLE(reason) {pktFree(&pak);\
	badPacket(client, __FUNCTION__, context, cmd, 0, 0, 0, reason);\
	PERFINFO_AUTO_STOP_FUNC();\
	return;}
#define QUIT_HANDLE_RANGE(start_print, print_count, total_prints) {\
	char temp_quit_handle_range[128];\
	sprintf(temp_quit_handle_range, "fingerprints out of range: %i, %i, %i", (start_print), (print_count), (total_prints));\
	QUIT_HANDLE(temp_quit_handle_range);\
	}

	pak = pktCreateSize(client->link, pktSize, cmd);
	pktSendBits(pak, 32, id);
	pktSendBits(pak, 32, num_block_reqs);
	for(i = 0; i < num_block_reqs; i++)
	{
		U32 * printsToSend;
		int j;

		if( block_reqs[i].start < 0 || block_reqs[i].count < 0 ||
			(block_reqs[i].start + block_reqs[i].count) > (int)filedata->num_prints[print_idx] )
		{
			QUIT_HANDLE_RANGE(block_reqs[i].start, block_reqs[i].count, filedata->num_prints[print_idx]);
		}

		pktSendBits(pak, 32, block_reqs[i].start);
		pktSendBits(pak, 32, block_reqs[i].count);
		
		printsToSend = filedata->prints[print_idx] + block_reqs[i].start;
		for(j = 0; j < block_reqs[i].count; j++)
			pktSendBits(pak, 32, printsToSend[j]);
		//pktSize += sizeof(U32) * block_reqs[i].count;
	}
	LOG_PATCH_RESPONSE_VERBOSE3(
		("compressed", "%d", !!compressed)
		("id", "%lu", id)
		("num_block_reqs", "%lu", num_block_reqs));
	s_one_tick_send += pktSize;
	s_one_tick_messages++;
	DEBUG_DISPLAY_PRINTF("Fingerprints for %s, %i bytes\n", patchFileGetUsedName(patch), pktSize);

	pktSend(&pak);

#undef QUIT_HANDLE
#undef QUIT_HANDLE_RANGE

	PERFINFO_AUTO_STOP_FUNC();
}

void sendBlocks(int id, int block_size, int num_block_reqs, BlockReq * block_reqs, bool compressed,
				PatchFile * patch, PatchClientLink * client,  char * context)
{
	Packet * pak = NULL;
	int i;
	U32 pktSize;
	int cmd = (compressed ? PATCHSERVER_BLOCKS_COMPRESSED : PATCHSERVER_BLOCKS);
	PatchFileData *filedata;

	PERFINFO_AUTO_START_FUNC();

	assert(patch && client);

	// Check for load errors, and report if there are errors.
	if (areThereLoadErrors(patch, compressed, __FUNCTION__, context))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If the file isn't loaded, queue up a request with the loader instead
	if( patch->load_state < LOADSTATE_COMPRESSED_ONLY ||
		patch->load_state < LOADSTATE_ALL && !compressed )
	{
		WaitingRequest * request = calloc(1,sizeof(WaitingRequest)); // freed in patchserverHandleWaitingRequest
		request->block_reqs = calloc(num_block_reqs,sizeof(BlockReq));
		ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
		request->req = compressed ? PATCHCLIENT_REQ_BLOCKS_COMPRESSED : PATCHCLIENT_REQ_BLOCKS;
		request->callback = clientWaitingRequest;
		request->id = id;
		request->block_size = block_size;
		request->num_block_reqs = num_block_reqs;
		memcpy(request->block_reqs, block_reqs, num_block_reqs*sizeof(BlockReq));
		if (g_log_blocks_uid && client->UID == g_log_blocks_uid)
			SERVLOG_PAIRS(LOG_TEST, "SendBlocksLoading",
				("ip", "%s", client->ipstr)
				("uid", "%"FORM_LL"u", client->UID)
				("load_state", "%d", patch->load_state));
		patchfileRequestLoad(patch, !compressed, request);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Compute the packet size
	pktSize = 25;
	for(i=0; i<num_block_reqs; i++)
		pktSize += (block_reqs[i].count * block_size) + 8;
	if(client->bucket != U32_MAX && patch->ver)
	{
		if(client->bucket < pktSize || patchserverGlobalBucketSizeLeft() < pktSize)
		{
			// Not enough bandwidth to send this packet, queue it up.
			WaitingRequest * request = calloc(1,sizeof(WaitingRequest)); // freed in patchserverHandleWaitingRequest
			request->block_reqs = calloc(num_block_reqs,sizeof(BlockReq));
			ADD_SIMPLE_POINTER_REFERENCE(request->refto_client, client);
			request->req = compressed ? PATCHCLIENT_REQ_BLOCKS_COMPRESSED : PATCHCLIENT_REQ_BLOCKS;
			request->callback = clientWaitingRequest;
			request->id = id;
			request->block_size = block_size;
			request->num_block_reqs = num_block_reqs;
			memcpy(request->block_reqs, block_reqs, num_block_reqs*sizeof(BlockReq));
			request->bytes = pktSize;
			request->fname = strdup(patch->ver->parent->path);
			if (g_log_blocks_uid && client->UID == g_log_blocks_uid)
				SERVLOG_PAIRS(LOG_TEST, "SendBlocksThrottled",
					("ip", "%s", client->ipstr)
					("uid", "%"FORM_LL"u", client->UID)
					("bucket", "%lu", client->bucket)
					("global_bucket", "%lu", patchserverGlobalBucketSizeLeft()));
			patchserverQueueThrottledRequest(request);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		client->bucket -= pktSize;
	}
	//trackSentData(pktSize);

#if PATCHER_LINK_COMPRESSION_TOGGLING
#define TURN_ON_COMPRESSION linkCompress(client->link, 1)
#else
#define TURN_ON_COMPRESSION ;
#endif

#define QUIT_HANDLE(reason) {pktFree(&pak);\
	TURN_ON_COMPRESSION;\
	badPacket(client, compressed ? "sendBlocks, compressed" : "sendBlocks, uncompressed", context, cmd, 0, 0, 0, reason);\
	PERFINFO_AUTO_STOP_FUNC();\
	return;}
#define QUIT_HANDLE_RANGE(start_block, block_count, total_blocks) {\
	char temp_quit_handle_range[128];\
	sprintf(temp_quit_handle_range, "blocks out of range: %i, %i, %i, %i", start_block, block_count, block_size, total_blocks);\
	QUIT_HANDLE(temp_quit_handle_range);\
	}

	filedata = compressed ? &patch->compressed : &patch->uncompressed;

#if PATCHER_LINK_COMPRESSION_TOGGLING
	if(compressed)
		linkCompress(client->link, 0);
#endif

	pak = pktCreateSize(client->link, pktSize, cmd);
	pktSendBits(pak, 32, id);
	pktSendBits(pak, 32, num_block_reqs);
	for(i = 0; i < num_block_reqs; i++)
	{
		if( block_reqs[i].start < 0 || block_reqs[i].count < 0 ||
			(block_reqs[i].start + block_reqs[i].count)*block_size > (int)filedata->block_len )
		{
			QUIT_HANDLE_RANGE(block_reqs[i].start, block_reqs[i].count, filedata->block_len );
		}
		pktSendBits(pak, 32, block_reqs[i].start);
		pktSendBits(pak, 32, block_reqs[i].count);
		s_sent_payload += block_size * block_reqs[i].count;
		pktSendBytes(pak, block_size * block_reqs[i].count, filedata->data + block_reqs[i].start * block_size);
	}

	if (g_log_blocks_uid && client->UID == g_log_blocks_uid)
		SERVLOG_PAIRS(LOG_TEST, "SendBlocks",
			("ip", "%s", client->ipstr)
			("uid", "%"FORM_LL"u", client->UID)
			("cmd", "%d", cmd)
			("compressed", "%d", !!compressed)
			("fname", "%s", patchFileGetUsedName(patch))
			("id", "%lu", id)
			("num_block_reqs", "%lu", num_block_reqs)
			("pktsize", "%lu", pktSize));
	LOG_PATCH_RESPONSE_VERBOSE3(
		("compressed", "%d", !!compressed)
		("fname", "%s", patchFileGetUsedName(patch))
		("id", "%lu", id)
		("num_block_reqs", "%lu", num_block_reqs)
		("pktsize", "%lu", pktSize));

	pktSize = 0;
	s_one_tick_send += pktSize;
	s_one_tick_messages++;
	DEBUG_DISPLAY_PRINTF("Blocks for %s, %i bytes, %s\n", patchFileGetUsedName(patch), pktSize, compressed ? "Compressed" : "Uncompressed");

	pktSend(&pak);
	TURN_ON_COMPRESSION;

#undef TURN_ON_COMPRESSION
#undef QUIT_HANDLE
#undef QUIT_HANDLE_RANGE
	PERFINFO_AUTO_STOP_FUNC();
}

static void flushLink(NetLink *link);

// Send Autoup information to children.
int sendChildAutoupInfo(NetLink * link, S32 index, PatchClientLink * client, void * userdata)
{
	if (eaSize(&client->servers) && client->patcher_version >= PATCHCLIENT_VERSION_AUTOUPINFO)
	{
		Packet * pak = pktCreate(link, PATCHSERVER_UPDATE_AUTOUPINFO);
		pktSendU32(pak, AUTOUPINFO_VERSION_0);
		pktSendString(pak, userdata);
		LOG_PATCH_RESPONSE(("autoup", "%s", (char *)userdata));
		pktSend(&pak);
		flushLink(client->link);
	}

	return 1;
}

// Delete a dynamic Autoupdate rule.
bool patchserverAutoupConfigDeleteRule(int id)
{
	int index = -1;
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.dynamic_autoup_config->autoup_rules, i, n);
	{
		if (g_patchserver_config.dynamic_autoup_config->autoup_rules[i]->id == id)
		{
			index = i;
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (index == -1)
		return false;
	StructDestroy(parse_AutoupConfigRule, g_patchserver_config.dynamic_autoup_config->autoup_rules[index]);
	eaRemove(&g_patchserver_config.dynamic_autoup_config->autoup_rules, index);
	patchserverSaveDynamicAutoupConfig();
	return true;
}

// Send HTTP information to children.
int sendChildHttpInfo(NetLink * link, S32 index, PatchClientLink * client, void * userdata)
{
	if (eaSize(&client->servers))
	{
		Packet * pak = pktCreate(link, PATCHSERVER_UPDATE_HTTPINFO);
		pktSendString(pak, userdata);
		LOG_PATCH_RESPONSE(("info", "%s", (char *)userdata));
		pktSend(&pak);
		flushLink(client->link);
	}

	return 1;
}

// Write out the dynamic Autoupdate patching configuration.
void patchserverSaveDynamicAutoupConfig()
{
	const char filename[] = PATCHSERVER_DYNAMIC_AUTOUP_CONFIG;
	const char old_filename[] = PATCHSERVER_DYNAMIC_AUTOUP_CONFIG_OLD;
	char error[256];
	char *output = NULL;
	FILE *outfile;
	int result;
	char *tempdata;
	bool do_write = true;

	if (!g_patchserver_config.dynamic_autoup_config)
		return;

	// Generate the new config, with a warning.
	estrAppend2(&output, "# WARNING - Automatically generated and overwritten by the running Patch Server.\r\n"
		"# If you change this file while the Patch Server is running, it will not be reloaded, and it might be overwritten.\r\n\r\n");
	ParserWriteText(&output, parse_DynamicAutoupConfig, g_patchserver_config.dynamic_autoup_config, 0, 0, 0);

	// Check if the config has changed.
	tempdata = fileAlloc(filename, NULL);
	if (tempdata)
	{
		if (!memcmp(tempdata, output, strlen(output)))
			do_write = false;
		fileFree(tempdata);
	}

	// Write out the new file.
	if (do_write)
	{

		// Save backup.
		fileMove(filename, old_filename);

		// Write it to the disk.
		outfile = fopen(filename, "w");
		if (!outfile)
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("Failed to open dynamic Autoupdate config for writing: %s", error);
			return;
		}
		result = (int)fwrite(output, 1, estrLength(&output), outfile);
		if (result != estrLength(&output))
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("Dynamic Autoupdate config write failure: %s", error);
		}
		result = fclose(outfile);
		if (result)
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("Failed to close dynamic Autoupdate config after writing: %s", error);
		}
	}

	// Propagate update to children.
	iterateAllPatchLinks(sendChildAutoupInfo, output);
	estrDestroy(&output);
}

// Write out the dynamic HTTP patching configuration.
void patchserverSaveDynamicHttpConfig()
{
	const char filename[] = PATCHSERVER_DYNAMIC_HTTP_CONFIG;
	const char old_filename[] = PATCHSERVER_DYNAMIC_HTTP_CONFIG_OLD;
	char error[256];
	char *output = NULL;
	FILE *outfile;
	int result;
	char *tempdata;
	bool do_write = true;

	if (!g_patchserver_config.dynamic_http_config)
		return;

	// Generate the new config, with a warning.
	estrAppend2(&output, "# WARNING - Automatically generated and overwritten by the running Patch Server.\r\n"
		"# If you change this file while the Patch Server is running, it will not be reloaded, and it might be overwritten.\r\n\r\n");
	ParserWriteText(&output, parse_DynamicHttpConfig, g_patchserver_config.dynamic_http_config, 0, 0, 0);

	// Check if the config has changed.
	tempdata = fileAlloc(filename, NULL);
	if (tempdata)
	{
		if (!memcmp(tempdata, output, strlen(output)))
			do_write = false;
		fileFree(tempdata);
	}

	// Write out the new file.
	if (do_write)
	{

		// Save backup.
		fileMove(filename, old_filename);
	
		// Write it to the disk.
		outfile = fopen(filename, "w");
		if (!outfile)
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("Failed to open dynamic HTTP config for writing: %s", error);
			return;
		}
		result = (int)fwrite(output, 1, estrLength(&output), outfile);
		if (result != estrLength(&output))
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("Dynamic HTTP config write failure: %s", error);
		}
		result = fclose(outfile);
		if (result)
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("Failed to close dynamic HTTP config after writing: %s", error);
		}
	}

	// Propagate update to children.
	iterateAllPatchLinks(sendChildHttpInfo, output);
	estrDestroy(&output);
}

// Add an Autoupdate rule.
void patchserverAutoupConfigAddRule(AutoupConfigRule *rule)
{
	if (!g_patchserver_config.dynamic_autoup_config)
		g_patchserver_config.dynamic_autoup_config = StructCreate(parse_DynamicAutoupConfig);
	rule->id = ++g_patchserver_config.dynamic_autoup_config->last_id;
	eaPush(&g_patchserver_config.dynamic_autoup_config->autoup_rules, rule);
	patchserverSaveDynamicAutoupConfig();
}

// Add a dynamic named view rule.
void patchserverHttpConfigAddNamedView(HttpConfigNamedView *rule)
{
	if (!g_patchserver_config.dynamic_http_config)
		g_patchserver_config.dynamic_http_config = StructCreate(parse_DynamicHttpConfig);
	rule->id = ++g_patchserver_config.dynamic_http_config->last_id;
	eaPush(&g_patchserver_config.dynamic_http_config->namedviews, rule);
	patchserverSaveDynamicHttpConfig();
}

// Add a dynamic branch rule.
void patchserverHttpConfigAddBranchRule(HttpConfigBranch *rule)
{
	if (!g_patchserver_config.dynamic_http_config)
		g_patchserver_config.dynamic_http_config = StructCreate(parse_DynamicHttpConfig);
	rule->id = ++g_patchserver_config.dynamic_http_config->last_id;
	eaPush(&g_patchserver_config.dynamic_http_config->branches, rule);
	patchserverSaveDynamicHttpConfig();
}

// Delete a dynamic named view rule.
bool patchserverHttpConfigDeleteNamedView(int id)
{
	int index = -1;
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.dynamic_http_config->namedviews, i, n);
	{
		if (g_patchserver_config.dynamic_http_config->namedviews[i]->id == id)
		{
			index = i;
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (index == -1)
		return false;
	StructDestroy(parse_HttpConfigNamedView, g_patchserver_config.dynamic_http_config->namedviews[index]);
	eaRemove(&g_patchserver_config.dynamic_http_config->namedviews, index);
	patchserverSaveDynamicHttpConfig();
	return true;
}

// Delete a dynamic branch rule.
bool patchserverHttpConfigDeleteBranchRule(int id)
{
	int index = -1;
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.dynamic_http_config->branches, i, n);
	{
		if (g_patchserver_config.dynamic_http_config->branches[i]->id == id)
		{
			index = i;
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (index == -1)
		return false;
	StructDestroy(parse_HttpConfigBranch, g_patchserver_config.dynamic_http_config->branches[index]);
	eaRemove(&g_patchserver_config.dynamic_http_config->branches, index);
	patchserverSaveDynamicHttpConfig();
	return true;
}

// Add a client to the list if it has activated child servers.
static int appendChildServer(NetLink* link, S32 index, void *link_user_data, void *func_data)
{
	PatchClientLink *client = link_user_data;
	PatchClientLink ***child_server_list = func_data;
	if (link && client && eaSize(&client->servers))
		eaPush(child_server_list, client);
	return 1;
}

// Assert that our child server information is self-consistent.
static void patchserverVerifyChildServers()
{
	PatchClientLink **child_server_list = NULL;
	StashTableIterator iterator;
	StashElement element;
	StashTable child_servers_stash_copy;
	bool success;

	PERFINFO_AUTO_START_FUNC();

	devassert(stashTableVerifyStringKeys(s_child_servers));

	// Make a copy of child server stash.
	child_servers_stash_copy = stashTableCreateWithStringKeys(stashGetOccupiedSlots(s_child_servers), StashDefault);
	stashGetIterator(s_child_servers, &iterator);
	while (stashGetNextElement(&iterator, &element))
	{
		char *name = stashElementGetStringKey(element);
		PatchChildServerData *server = stashElementGetPointer(element);
		success = stashAddPointer(child_servers_stash_copy, name, server, false);
		devassertmsg(success, "add element to stash copy");
	}

	// Make a list of all child server links.
	iterateAllPatchLinks(appendChildServer, &child_server_list);

	// Loop over each child server on each child server link, and perform some sanity checks.
	EARRAY_CONST_FOREACH_BEGIN(child_server_list, i, n);
	{
		PatchClientLink *client = child_server_list[i];

		// Make sure the link pointer is coherent.
		devassert(linkGetUserData(client->link) == client);

		EARRAY_CONST_FOREACH_BEGIN(client->servers, j, m);
		{
			PatchChildServerData *server = client->servers[j];
			int k;
			bool parent_exists;
			PatchChildServerData *stash_server = NULL;
			
			// Make sure the client pointer is coherent.
			devassert(server->client == client);

			// Check the category.
			devassert(server->category && *server->category);

			// Check that there are no duplicates on this link.
			for (k = 0; k < j; ++k)
				devassert(stricmp(client->servers[k]->name, server->name) != 0);

			// Check that the parent exists and is in order.
			parent_exists = false;
			for (k = 0; !parent_exists && k < j; ++k)
			{
				if (!stricmp(client->servers[k]->name, server->parent))
					parent_exists = true;
			}
			devassert(parent_exists || !stricmp(server->parent, g_patchserver_config.displayName));

			// Check that no other link has this server connected.
			for (k = 0; k < i; ++k)
			{
				PatchClientLink *other_client = child_server_list[k];
				EARRAY_CONST_FOREACH_BEGIN(other_client->servers, l, p);
				{
					PatchChildServerData *other_server = other_client->servers[l];
					devassert(other_server != server);
					devassert(stricmp(other_server->name, server->name));
					devassert(stricmp(other_server->parent, server->name));
				}
				EARRAY_FOREACH_END;
			}

			// Check that the stash element is consistent.
			success = stashFindPointer(s_child_servers, server->name, &stash_server);
			devassert(success);
			devassert(stash_server == server);

			// Remove from stash copy to check for extra stash elements.
			success = stashRemovePointer(child_servers_stash_copy, server->name, &stash_server);
			devassert(success);
			devassert(stash_server == server);

			// Check the stash element key.
			success = stashFindElement(s_child_servers, server->name, &element);
			devassert(success);
			devassert(stashElementGetStringKey(element) == server->name);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	// Check for extra stash elements.
	devassert(stashGetCount(child_servers_stash_copy) == 0);

	// Clean up.
	eaDestroy(&child_server_list);
	stashTableDestroy(child_servers_stash_copy);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send an activation notification for each child.
void patchserverResendChildActivations()
{
	StashTableIterator iterator;
	StashElement element;

	patchserverVerifyChildServers();

	// Send an activation for us.
	patchupdateNotifyActivate(g_patchserver_config.displayName, g_patchserver_config.serverCategory, "");

	// Send an activation for each child.
	stashGetIterator(s_child_servers, &iterator);
	while (stashGetNextElement(&iterator, &element))
	{
		PatchChildServerData *server = stashElementGetPointer(element);
		if (server->client->servers[0] == server)
		{
			int i;
			for (i = 0; i != eaSize(&server->client->servers); ++i)
			{
				PatchChildServerData *child_server = server->client->servers[i];
				patchupdateNotifyActivate(child_server->name, child_server->category, child_server->parent);
				patchupdateNotifyUpdateStatus(child_server->name, child_server->last_update);
			}
		}
	}
	patchTrackingScanForUpdates(false, 0);
}

// Prepare to send notifications to children.
void patchserverMirrorNotifyDirty()
{
	s_sendMirrorNotify = 1;
}

// Send the requested packet to a client, now that patch loading is complete and we're not throttled.
static void clientWaitingRequest(void *userdata, PatchFile *patch, PatchClientLink *client, NetLink *link, int req, int id, int extra, int num_block_reqs, BlockReq *block_reqs)
{
	int block_size = extra;
	int print_idx = extra;

	switch(req)
	{
		xcase PATCHCLIENT_REQ_AUTOUPDATE:
			sendAutoUpdate(patch, client);

		xcase PATCHCLIENT_REQ_FILEINFO:
			sendFileInfo(id, patch, client);

		xcase PATCHCLIENT_REQ_BLOCKS:
		acase PATCHCLIENT_REQ_BLOCKS_COMPRESSED:
			sendBlocks(id, block_size, num_block_reqs,
						block_reqs, req == PATCHCLIENT_REQ_BLOCKS_COMPRESSED,
						patch, client, "respondToLoad");
			SAFE_FREE(block_reqs);

		xcase PATCHCLIENT_REQ_FINGERPRINTS:
		acase PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED:
			sendFingerprints(id, print_idx, num_block_reqs,
								block_reqs, req == PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED,
								patch, client, "respondToLoad");
			SAFE_FREE(block_reqs);

		xdefault:
			assertmsg(0, "No handler for file load request");
	}

	flushLink(client->link);
}

// Get number of client connections.
U32 patchserverConnections()
{
	return s_connections;
}

#define validateAndLoadView(client) validateAndLoadViewEx(client, false, NULL, 0)
bool validateAndLoadViewEx(	PatchClientLink* client,
							bool ignore_time,
							char* err_msg,
							int err_msg_size)
{
	// TODO: loading isn't always necessary (it causes a stall, which will be more frequent on gimme servers)
	PatchServerDb *serverdb;

	PERFINFO_AUTO_START_FUNC();

	if(GET_REF(client->refto_view))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	if(!client->project)
	{
		if(err_msg)
		{
			sprintf_s(SAFESTR2(err_msg), "No project is set");
		}
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	serverdb = client->project->serverdb;

	if(	client->branch < 0 ||
		client->branch == INT_MAX)
	{
		client->branch = serverdb->latest_branch;
	}

	if(	client->branch < serverdb->min_branch ||
		client->branch > serverdb->max_branch)
	{
		if(err_msg)
		{
			sprintf_s(	SAFESTR2(err_msg),
						"Branch %d is out of range for the database",
						client->branch);
		}
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	if(client->sandbox[0])
	{
		Checkin *checkin;

		if(!isalpha(client->sandbox[0]) || strchr(client->sandbox, ' '))
		{
			if(err_msg)
				sprintf_s(SAFESTR2(err_msg), "Sandboxes must start with a letter and cannot contain spaces (sandbox: %s)", client->sandbox);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		checkin = patchGetSandboxCheckin(serverdb->db, client->sandbox);
		if(client->incr_from == PATCHREVISION_NONE) // waiting to be set or non-incremental
		{
			client->incr_from = checkin ? checkin->incr_from : PATCHREVISION_NONE;
			if(client->incr_from != PATCHREVISION_NONE && client->branch != checkin->branch)
			{
				// Our fall through seems to support this now.
				// ensuring the same branch may be overkill, but there's no reason to make an incremental
				//     into a new branch -GG
				if(err_msg)
					sprintf_s(SAFESTR2(err_msg), "A chain of incrementals should be kept in the same branch (you gave %d, sandbox %s is %d)", client->branch, client->sandbox, checkin->branch);
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
		}
		else if(client->incr_from == PATCHREVISION_NEW) // new incremental
		{
			if(checkin)
			{
				if(err_msg)
					sprintf_s(SAFESTR2(err_msg), "Sandbox %s has already been used, and cannot take a new incremental", client->sandbox);
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
		}
		else
		{
			assert(!checkin || client->incr_from == checkin->incr_from);
		}
	}
	else
	{
		if(client->incr_from != PATCHREVISION_NONE) // new incremental
		{
			if(err_msg)
				sprintf_s(SAFESTR2(err_msg), "Incrementals must be checked in to a sandbox");
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
	}

	if(client->rev < 0 || client->rev > serverdb->latest_rev)
		client->rev = serverdb->latest_rev;
	client->rev = patchFindRev(serverdb->db, client->rev, client->branch, client->sandbox);
	if(client->incr_from == PATCHREVISION_NEW) // new incremental
		client->incr_from = client->rev;

	if(client->prefix && patchFindPath(client->project->serverdb->db, client->prefix, false)==NULL)
	{
		if(err_msg)
			sprintf_s(SAFESTR2(err_msg), "Prefix %s does not exist", client->prefix);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

 	//if(!ignore_time && client->rev < (int)eaiGet(&serverdb->db->branch_valid_since, client->branch))
 	//{
 	//	if(err_msg)
 	//		sprintf_s(err_msg, err_msg_size, "The specified view is no longer valid");
 	//	return false;
 	//}

	//{
	//	ProjectView *view;
	//	view = patchprojectFindOrAddView(client->project, client->branch, client->rev, client->sandbox, client->incr_from, client->patcher_version);
	//	assert(view);
	//	ADD_SIMPLE_POINTER_REFERENCE(client->refto_view, view);
	//}

	// Check that server projects can only be patched with patchclient
	if(client->autoupdate_token && client->project->name &&
	   strstri(client->project->name, "Server") != NULL &&
	   strstri(client->autoupdate_token, "PatchClient") == NULL && 
	   strstri(client->autoupdate_token, "PatchServer") == NULL)
	{
		//Aaron has a JIRA to improve this nonsense: http://jira:8080/browse/COR-16088
		char msg[1024], ip[16];
		if(err_msg)
			sprintf_s(SAFESTR2(err_msg), "Only a CLI client can patch the server");
		sprintf(msg, "SECURITY ALERT! Client %s(%s) is trying patch to %s", linkGetIpStr(client->link, SAFESTR(ip)), client->autoupdate_token, client->project->name);
		// TODO: Change this to ALERTCATEGORY_SECURITY once the controllertraker is updated to support that. <NPK 2009-06-02>
		TriggerAlert("SECURITY", msg, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			0, GLOBALTYPE_PATCHSERVER, 0, GLOBALTYPE_PATCHSERVER, 0, g_patchserver_config.displayName, 0);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

static void patchClientDeleteCb(NetLink* link, void *user_data);

static void patchClientCreateCb(NetLink* link, PatchClientLink	* client)
{
	char ipBuf[50];
	bool is_internal = false;

	// Client links are not considered to be trustworthy.
	linkSetIsNotTrustworthy(link, true);

	if(g_patchserver_config.internal)
		is_internal = checkAllowDeny(g_patchserver_config.internal->ips, g_patchserver_config.internal->deny_ips, linkGetIp(link));
	
	client->link = link;
	client->update_pending = true;
	client->notify_me = false;
	client->UID = s_unique_id++;
	client->start_time = (U64)time(NULL);
	strcpy(client->ipstr, linkGetIpStr(link, SAFESTR(ipBuf)));
	if(g_patchserver_config.bandwidth_config)
	{
		// Don't throttle internal IPs
		if(is_internal)
			client->bucket = U32_MAX;
		else
			client->bucket = g_patchserver_config.bandwidth_config->per_user;
	}
	else
		client->bucket = U32_MAX;
	SERVLOG_PAIRS(LOG_PATCHSERVER_CONNECTIONS, "PatchClientConnect",
		("uid", "%"FORM_LL"u", client->UID)
		("ip", "%s", client->ipstr)
	);
	// NOTE: This was previously set to 4 hours. Dropping it to 1 hour to see what breaks. <NPK 2009-05-14>
	linkSetTimeout(link, 60.0f * 60.0f * 1.0f); 
	if(s_corruption_freq)
		linkSetCorruptionFrequency(link, s_corruption_freq);
	CONNECTION_PRINTF("PatchClient connect    %s\n", client->ipstr);

	s_connections++;

	if(g_patchserver_config.max_connections && s_connections > g_patchserver_config.max_connections)
	{
		dontTalkToMe(client, "Server full");
		patchClientDeleteCb(link, client);				// linkRemove in a connect callback doesn't call the disconnect callback
		linkRemove_wReason(&link, "Server full");
	}
	else if(g_patchserver_config.locked && !is_internal)
	{
		dontTalkToMe(client, "Server locked");
		linkFlushAndClose(&link, "Server locked");
	}
}

static void freePendingFile(	PatchClientLink* client,
						CheckinFile* cf)
{
	if(cf->data){
		SAFE_FREE(cf->data);
		
		assert(client->checkinsInMemoryCount);
		client->checkinsInMemoryCount--;

		assert(client->checkinsInMemoryBytes >= cf->size_to_receive);
		client->checkinsInMemoryBytes -= cf->size_to_receive;
	}

	SAFE_FREE(cf->fname);
	free(cf);
}

static void freePendingCheckins(PatchClientLink *client)
{
	char path[MAX_PATH];

	EARRAY_CONST_FOREACH_BEGIN(client->checkin_files, i, isize);
		freePendingFile(client, client->checkin_files[i]);
		client->checkin_files[i] = NULL;
	EARRAY_FOREACH_END;
	
	eaDestroy(&client->checkin_files);

	assert(!client->checkinsInMemoryCount);
	assert(!client->checkinsInMemoryBytes);

	sprintf(path, "pending_checkins/%"FORM_LL"i", client->UID);
	rmdirtreeEx(path, 1);
}

static void patchClientDeleteCb(NetLink* link, void *user_data)
{
	PatchClientLink	*client = user_data;
	const LinkStats * stats = linkStats(link);
	static U64 last_UID = ((U64)0 - 1);
	char buf[50];
	U64 seconds = (U64)time(NULL) - client->start_time;
	F32 rate = (F32) stats->send.real_bytes / (1024 * (seconds ? seconds : 1));
	F32 mbs = (F32) stats->send.real_bytes / (1024 * 1024);
	F32 mb_recv = (F32) stats->recv.real_bytes / (1024 * 1024);
	F32 mins = (F32) seconds / 60.0;
	char *pDisconnectReason = NULL;

	assert(client->UID != last_UID);
	last_UID = client->UID;

	// Remove pending flush request.
	if (s_links_to_flush)
		stashRemovePointer(s_links_to_flush, link, NULL);

	REMOVE_HANDLE(client->refto_view);
	freePendingCheckins(client);
	RefSystem_RemoveReferent(client, true);
	patchfileDestroy(&client->special_manifest_patch);

	// Stop tracking child servers on this link.
	EARRAY_FOREACH_REVERSE_BEGIN(client->servers, i);
	{
		char *name = client->servers[i]->name;
		bool success = stashRemovePointer(s_child_servers, name, NULL);
		devassertmsgf(success, "removing %s", name);
		patchupdateNotifyDeactivate(name);
		patchTrackingRemove(name);
		StructDestroy(parse_PatchChildServerData, client->servers[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&client->servers);
	patchserverVerifyChildServers();

	// The client struct itself is free'd by the net code
	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);
	SERVLOG_PAIRS(LOG_PATCHSERVER_CONNECTIONS, "PatchClientDisconnect",
		("uid", "%"FORM_LL"u", client->UID)
		("ip", "%s", client->ipstr)
		("code", "%u", linkGetDisconnectErrorCode(link))
		("reason", "%s", pDisconnectReason)
	);
	CONNECTION_PRINTF(	"PatchClient disconnect %s (%s, %s, branch %d%s, rev %d)\n",
							client->ipstr,
							client->author,
							client->project ? client->project->name : "no project",
							client->branch,
							client->sandbox,
							client->rev);
	estrDestroy(&pDisconnectReason);
	s_connections--;
	s_sent_bytes_history += stats->send.real_bytes;
	s_received_bytes_history += stats->recv.real_bytes;

	log_printf(LOG_PATCHSERVER_TRANSFER_RATE,
		"IP %20s | MB SENT: %6.2f | KB/S SENT %6.2f | MINUTES %6.2f | MB RECIEVED %6.2f | TOKEN %s | PROJECT %s",
		linkGetIpStr(link, SAFESTR(buf)), mbs, rate, mins, mb_recv, client->autoupdate_token, client->project ? client->project->name : "noproject");

	SAFE_FREE(client->autoupdate_token);
	SAFE_FREE(client->prefix);
	SAFE_FREE(client->host);
	SAFE_FREE(client->referrer);
}

PatchFile* findPatchFileEx(PatchProject *proj, char *fname, int branch, int rev, char *sandbox, int incr_from, char *prefix, PatchClientLink *client)
{
	char special_name[MAX_PATH];
	PatchServerDb *serverdb;
	FileVersion *ver;
	
	PERFINFO_AUTO_START_FUNC();

	ver = patchprojectFindVersion(proj, fname, branch, sandbox, rev, incr_from, prefix, &serverdb);

	if(ver)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return patchfileFromDb(ver, serverdb);
	}

	sprintf(special_name, "%s.manifest", proj->name);
	if (stricmp(special_name, fname) == 0)
	{
		ProjectView *view = client ? GET_REF(client->refto_view) : NULL;
		if(!view)
		{
			view = patchprojectFindOrAddView(proj, branch, rev, sandbox, incr_from, prefix,
				client ? client->patcher_version : PATCHCLIENT_VERSION_CURRENT,
				client ? client->autoupdate_token : "(no client)",
				client ? client->ipstr : "(unknown ip)",
				client ? client->UID : 0);
			if(!view)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return NULL;
			}
			if(client)
				ADD_SIMPLE_POINTER_REFERENCE(client->refto_view, view);
		}
		PERFINFO_AUTO_STOP_FUNC();
		return view->manifest_patch;
	}

	sprintf(special_name, "%s.filespec", proj->name);
	if(stricmp(special_name, fname) == 0)
	{
		ProjectView *view = client ? GET_REF(client->refto_view) : NULL;
		if(!view)
		{
			view = patchprojectFindOrAddView(proj, branch, rev, sandbox, incr_from, prefix,
				client ? client->patcher_version : PATCHCLIENT_VERSION_CURRENT,
				client ? client->autoupdate_token : "(no client)",
				client ? client->ipstr : "(unknown ip)",
				client ? client->UID : 0);
			if(!view)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return NULL;
			}
			if(client)
				ADD_SIMPLE_POINTER_REFERENCE(client->refto_view, view);
		}
		PERFINFO_AUTO_STOP_FUNC();
		return patchprojectGenerateViewClientFilespec(special_name, proj, branch, sandbox, rev, incr_from, view);
	}

	if(proj->is_db)
	{
		sprintf(special_name, "%s.patchserverdb", proj->name);
		if(stricmp(special_name, fname) == 0)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return proj->config_patch;
		}

		sprintf(special_name, "%s.full.manifest", proj->serverdb->name);
		if(stricmp(special_name, fname) == 0)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return patchserverdbGetFullManifestPatch(proj->serverdb, &client->special_manifest_patch);
		}

		if(strStartsWith(fname, proj->serverdb->name) && strEndsWith(fname, ".incremental.manifest"))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return patchserverdbGetIncrementalManifestPatch(proj->serverdb, atoi(fname + strlen(proj->serverdb->name) + 1), &client->special_manifest_patch);
		}
	}
	else
	{
		if(	proj->allowFullManifest_ips &&
			client &&
			checkAllowDeny(proj->allowFullManifest_ips, proj->denyFullManifest_ips, linkGetIp(client->link)))
		{
			sprintf(special_name, "%s.full.manifest", proj->serverdb->name);
			if(stricmp(special_name, fname) == 0)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return patchserverdbGetFullManifestPatch(proj->serverdb, &client->special_manifest_patch);
			}
		}
		
		sprintf(special_name, "%s.patchproject", proj->name);
		if(stricmp(special_name, fname) == 0)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return proj->config_patch;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

PatchFile *findPatchFile(char *fname, PatchClientLink *client)
{
	if(!client->project)
		return NULL;

	return findPatchFileEx(client->project, fname, client->branch, client->rev, client->sandbox, client->incr_from, client->prefix, client);
}

PatchFile* patchserverGetFile(PatchProject *proj, char *fname, int branch, int rev, char *sandbox, int incr_from)
{
	// this was for the web interface to get at various special files, but it's not currently used.
	// it should probably glean incr_from from sandbox.
	return proj ? findPatchFileEx(proj, fname, branch, rev, sandbox, incr_from, NULL, NULL) : NULL;
}

void handleReqFileInfo(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	int			id;
	char		*fname;
	PatchFile	*patch;

#define QUIT_HANDLE(reason) {	badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_FILEINFO, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet not null terminated");
	fname = pktGetStringTemp(pak_in);
	if(!pktCheckRemaining(pak_in, 4))
		QUIT_HANDLE("patch did not have enough bytes");
	id = pktGetBits(pak_in,32);
#undef QUIT_HANDLE

	LOG_PATCH_COMMAND_VERBOSE2(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)) ("id", "%d", id));

	if(!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_FILEINFO);
		return;
	}

	patch = findPatchFile(fname, client);
	if(!patch) // && g_debug_display)
		ERROR_PRINTF("Could not find %s for file info\n", fname);
	sendFileInfo(id, patch, client);
}

void handleReqFileVersionInfo(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char		*fname;
	FileVersion *ver;
	Packet *pak;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_FILEVERSIONINFO, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet not null terminated");
	fname = pktGetStringTemp(pak_in);
#undef QUIT_HANDLE

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)));

	if(!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_FILEVERSIONINFO);
		return;
	}

	pak = pktCreate(link, PATCHSERVER_FILEVERSIONINFO);

	ver = patchprojectFindVersion(client->project, fname, client->branch, client->sandbox, client->rev, client->incr_from, client->prefix, NULL);
	if(!ver)
	{
		if(!strEndsWith(fname, ".ms"))
			ERROR_PRINTF("Could not find %s for fileversion info\n", fname);
		pktSendBool(pak, false);
		pktSend(&pak);
		LOG_PATCH_RESPONSE(("valid", "0"));
		return;
	}

	pktSendBool(pak, true);
	pktSendU32(pak, ver->modified);
	pktSendU32(pak, ver->size);
	pktSendU32(pak, ver->checksum);
	pktSendU32(pak, ver->header_size);
	pktSendU32(pak, ver->header_checksum);
	if(SAFE_MEMBER(client->project, allow_checkins))
	{

		Checkout *checkout = NULL;
		FileVerFlags flags = ver->flags;

		if(client->incr_from == PATCHREVISION_NONE)
		{
			if(!(ver->parent->flags & DIRENTRY_FROZEN))
			{
				FileVersion *newest = patchFindVersionInDir(ver->parent, INT_MAX, client->sandbox, INT_MAX, PATCHREVISION_NONE);
				if(client->branch < newest->checkin->branch)
					flags |= FILEVERSION_LINK_FORWARD_BROKEN;
				if(ver->checkin->branch < client->branch)
					flags |= FILEVERSION_LINK_BACKWARD_SOLID;
			}
			checkout = patchFindCheckoutInDir(ver->parent, client->branch, client->sandbox);
		}

		pktSendU32(pak, flags);
		pktSendString(pak, ver->checkin->author ? ver->checkin->author : "");
		pktSendString(pak, checkout ? checkout->author : "");

		LOG_PATCH_RESPONSE(("valid", "1") ("modified", "%lu", ver->modified) ("size", "%lu", ver->size) ("checksum", "%lu", ver->checksum)
			("header_size", "%lu", ver->header_size) ("header_checksum", "%lu", ver->header_checksum) ("flags", "%lu", flags)
			("lastauthor", "%s", NULL_TO_EMPTY(ver->checkin->author)) ("lockedby", "%s", checkout ? NULL_TO_EMPTY(checkout->author) : ""));
	}
	else
	{
		pktSendU32(pak, 0);
		pktSendString(pak, "");
		pktSendString(pak, "");

		LOG_PATCH_RESPONSE(("valid", "1") ("modified", "%lu", ver->modified) ("size", "%lu", ver->size) ("checksum", "%lu", ver->checksum)
			("header_size", "%lu", ver->header_size) ("header_checksum", "%lu", ver->header_checksum) ("flags", "0")
			("lastauthor", "") ("lockedby", ""));
	}
	pktSend(&pak);
}

void fillBlockReqs(PatchClientLink *client, int cmd, BlockReq ** block_reqs, Packet * pak_in, int num_block_reqs, char * context)
{
	int i = 0;

	PERFINFO_AUTO_START_FUNC();

#define QUIT_HANDLE(reason) {badPacket(client, "handleReqBlocks", context, cmd, 0, 0, 0, reason); PERFINFO_AUTO_STOP_FUNC(); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("not enough bytes remaining in packet");}

	assert(block_reqs);

	if(*block_reqs == NULL)
		*block_reqs = calloc(num_block_reqs, sizeof(BlockReq));
	for(i = 0; i < num_block_reqs; i++)
	{
		CHECK_BYTES(4);
		(*block_reqs)[i].start = pktGetBits(pak_in, 32);
		CHECK_BYTES(4);
		(*block_reqs)[i].count = pktGetBits(pak_in, 32);
	}
#undef QUIT_HANDLE
#undef CHECK_BYTES

	PERFINFO_AUTO_STOP_FUNC();
}

void handleReqFingerprints(Packet *pak_in, NetLink *link, PatchClientLink *client, bool compressed)
{
	U32			id, num_block_reqs;
	int print_idx;
	char		*fname;
	PatchFile	*patch;
	PatchFileData *filedata;
	BlockReq *block_reqs;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, compressed ? PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED : PATCHCLIENT_REQ_FINGERPRINTS, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("not enough bytes in packet");}

	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet not null terminated");
	fname = pktGetStringTemp(pak_in);

	if(!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(compressed ? PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED : PATCHCLIENT_REQ_FINGERPRINTS);
		return;
	}

	patch = findPatchFile(fname, client);
	if(!patch)
		QUIT_HANDLE("patch file did not exist");
	filedata = compressed ? &patch->compressed : &patch->uncompressed;
	CHECK_BYTES(4);
	id				= pktGetBits(pak_in, 32);
	CHECK_BYTES(1);
	print_idx		= pktGetBits(pak_in, 8);
	CHECK_BYTES(4);
	num_block_reqs	= pktGetBits(pak_in, 32);
	if(num_block_reqs > MAX_REQ_FINGERPRINTS)
		QUIT_HANDLE("number of fingerprint blocks requested is too high");
	if(print_idx < 0 || print_idx >= filedata->num_print_sizes)
		QUIT_HANDLE("print_idx out of range");
#undef QUIT_HANDLE
#undef CHECK_BYTES

	LOG_PATCH_COMMAND_VERBOSE3(PATCH_PROJECT_CONTEXT
		("compressed", "%d", !!compressed)
		("fname", "%s", fname)
		("id", "%lu", id)
		("print_idx", "%d", id)
		("num_block_reqs", "%lu", num_block_reqs));

	if(patch)
	{
		block_reqs = ScratchAlloc(num_block_reqs*sizeof(BlockReq));
		fillBlockReqs(client, compressed ? PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED : PATCHCLIENT_REQ_FINGERPRINTS, &block_reqs, pak_in, num_block_reqs, "handleReqFingerprints");
		sendFingerprints(id, print_idx, num_block_reqs, block_reqs, compressed, patch, client, "handleReqFingerprints");
		ScratchFree(block_reqs);
	}
	else
		ERROR_PRINTF("Could not find %s for fingerprinting",fname);
}

void handleReqBlocks(Packet *pak_in, NetLink *link, PatchClientLink *client, bool compressed)
{
	U32			id, num_block_reqs, block_size;
	char		*fname;
	PatchFile	*patch;

	if(!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(compressed ? PATCHCLIENT_REQ_BLOCKS_COMPRESSED : PATCHCLIENT_REQ_BLOCKS);
		return;
	}

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, compressed ? PATCHCLIENT_REQ_BLOCKS_COMPRESSED : PATCHCLIENT_REQ_BLOCKS, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("not enough bytes in packet");}

	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet not null terminated");
	fname = pktGetStringTemp(pak_in);
	patch			= findPatchFile(fname, client);
	if(!patch)
		QUIT_HANDLE("patch file not found");
	CHECK_BYTES(4);
	id				= pktGetBits(pak_in, 32);
	CHECK_BYTES(4);
	block_size		= pktGetBits(pak_in, 32);
	CHECK_BYTES(4);
	num_block_reqs	= pktGetBits(pak_in, 32);

	if (g_log_blocks_uid && client->UID == g_log_blocks_uid)
		SERVLOG_PAIRS(LOG_TEST, "ReqBlocks",
			("ip", "%s", client->ipstr)
			("uid", "%"FORM_LL"u", client->UID)
			("compressed", "%d", !!compressed)
			("fname", "%s", fname)
			("id", "%lu", id)
			("block_size", "%lu", block_size)
			("num_block_reqs", "%lu", num_block_reqs));
	LOG_PATCH_COMMAND_VERBOSE3(PATCH_PROJECT_CONTEXT
		("ip", "%s", client->ipstr)
		("uid", "%"FORM_LL"u", client->UID)
		("compressed", "%d", !!compressed)
		("fname", "%s", fname)
		("id", "%lu", id)
		("block_size", "%lu", block_size)
		("num_block_reqs", "%lu", num_block_reqs));

	if(num_block_reqs > MAX_REQ_BLOCKS)
		QUIT_HANDLE("number of blocks requested is too high");
	if(num_block_reqs <= 0)
		QUIT_HANDLE("number of blocks requested is too low");

#undef QUIT_HANDLE
#undef CHECK_BYTES

	if(patch)
	{
		BlockReq *block_reqs = ScratchAlloc(num_block_reqs*sizeof(BlockReq));
		fillBlockReqs(client, compressed ? PATCHCLIENT_REQ_BLOCKS_COMPRESSED : PATCHCLIENT_REQ_BLOCKS, &block_reqs, pak_in, num_block_reqs,
			compressed ? "handleReqBlocks, compressed" : "handleReqBlocks, uncompressed");
		sendBlocks(id, block_size, num_block_reqs, block_reqs, compressed, patch, client,
			compressed ? "handleReqBlocks, compressed" : "handleReqBlocks, uncompressed");
		ScratchFree(block_reqs);
	}
	else
		ERROR_PRINTF("Could not find %s for file request",fname);
}

bool setProjectView(PatchClientLink* client,
					const char* project,
					int branch,
					const char* sandbox,
					U32 time,
					int rev,
					bool new_incremental,
					bool ignore_time,
					char* err_msg,
					int err_msg_size)
{
	REMOVE_HANDLE(client->refto_view);

	patchfileDestroy(&client->special_manifest_patch);

	client->project = patchserverFindProjectChecked(project, linkGetSAddr(client->link));
	if(!client->project)
	{
		sprintf_s(SAFESTR2(err_msg), "Cannot find project %s in the database", project);
		return false;
	}

	client->branch = branch;
	strcpy(client->sandbox, NULL_TO_EMPTY(sandbox));

	if(branch == -1)
		branch = client->project->serverdb->latest_branch;
	if(rev == PATCHREVISION_NONE)
		rev = MAX(client->project->serverdb->latest_rev, 0);

	if(time)
		client->rev = patchFindRevByTime(client->project->serverdb->db, time, branch, sandbox, client->project->serverdb->latest_rev);
	else if(rev < 0 || rev > MAX(client->project->serverdb->latest_rev, 0))
	{	
		sprintf_s(SAFESTR2(err_msg), "View revision %d invalid (max is %u)", rev, client->project->serverdb->latest_rev);
		return false;
	}
	else
		client->rev = rev;

	client->incr_from = new_incremental ? PATCHREVISION_NEW : PATCHREVISION_NONE;

	return validateAndLoadViewEx(client, ignore_time, err_msg, err_msg_size);
}

// Return true if autoupdate rule token matches, for findAutoupInfoMatch().
static bool findAutoupInfoForTokenMatch(const char *token, AutoupConfigRule *rule)
{
	int index;
	index = eaFindString(&rule->tokens, token);
	return index != -1;
}

// Return true if autoupdate rule category matches, for findAutoupInfoMatch().
static bool findAutoupInfoForCategoryMatch(const char *category, AutoupConfigRule *rule)
{
	int index;
	if (!category || !eaSize(&rule->categories))
		return true;
	index = eaFindString(&rule->categories, category);
	return index != -1;
}

// Return true if named view project name matches, for findHttpInfoForView().
static bool findHttpInfoForViewNamedViewProjectMatch(const char *project, HttpConfigNamedView *namedview)
{
	int index;
	index = eaFindString(&namedview->project, project);
	return index != -1;
}

// Return true if branch project name matches, for findHttpInfoForView().
static bool findHttpInfoForViewBranchProjectMatch(const char *project, HttpConfigBranch *branchconfig)
{
	int index;
	index = eaFindString(&branchconfig->project, project);
	return index != -1;
}

// Return true if named view category name matches, for findHttpInfoForView().
static bool findHttpInfoForViewNamedViewCategoryMatch(const char *category, HttpConfigNamedView *namedview)
{
	int index;
	if (!category || !eaSize(&namedview->categories))
		return true;
	index = eaFindString(&namedview->categories, category);
	return index != -1;
}

// Return true if named view category name matches, for findHttpInfoForView().
static bool findHttpInfoForViewBranchCategoryMatch(const char *category, HttpConfigBranch *branchconfig)
{
	int index;
	if (!eaSize(&branchconfig->categories))
		return true;
	index = eaFindString(&branchconfig->categories, category);
	return index != -1;
}

// Return true if named view autoupdate token matches, for findHttpInfoForView().
static bool findHttpInfoForViewNamedViewTokenMatch(const char *token, HttpConfigNamedView *namedview)
{
	int index;
	if (!token || !eaSize(&namedview->tokens))
		return true;
	index = eaFindString(&namedview->tokens, token);
	return index != -1;
}

// Return true if named view token name matches, for findHttpInfoForView().
static bool findHttpInfoForViewBranchTokenMatch(const char *token, HttpConfigBranch *branchconfig)
{
	int index;
	if (!eaSize(&branchconfig->tokens))
		return true;
	index = eaFindString(&branchconfig->tokens, token);
	return index != -1;
}

// Return true if branch list matches, for findHttpInfoForView().
static bool findHttpInfoForViewBranchBranchMatch(int branch, HttpConfigBranch *branchconfig)
{
	int index;
	index = eaiFind(&branchconfig->branch, branch);
	return index != -1;
}

// Make a ClientHttpInfo struct to send to a client.
static ClientHttpInfo *findHttpInfoForViewMakeInfo(HttpConfigWeightedInfo *info)
{
	ClientHttpInfo *clientinfo = StructCreate(parse_ClientHttpInfo);
	clientinfo->info = strdup(info->info);
	clientinfo->load_balancer = info->load_balancer;
	return clientinfo;
}

// Use weights to pick the appropriate info, for findHttpInfoForView().
static ClientHttpInfo *findHttpInfoForViewReturnInfo(HttpConfigWeightedInfo **info)
{
	float total_weight = 0;
	float cumulative_prob = 0;
	float random;

	// Calculate total weight.
	EARRAY_CONST_FOREACH_BEGIN(info, i, n);
	{
		float weight = info[i]->weight;
		if (weight > 0 && weight == weight)
			total_weight += weight;
	}
	EARRAY_FOREACH_END;

	// Decide which info block should be chosen.
	devassert(total_weight > 0 || eaSize(&info) <= 1);
	random = randomPositiveF32();
	EARRAY_CONST_FOREACH_BEGIN(info, i, n);
	{
		float weight = info[i]->weight;
		float prob;
		if (weight <= 0 || !(weight == weight))
			continue;
		if (i == n - 1)
			return findHttpInfoForViewMakeInfo(info[i]);
		prob = weight/total_weight;
		cumulative_prob += prob;
		if (random < cumulative_prob)
			return findHttpInfoForViewMakeInfo(info[i]);
	}
	EARRAY_FOREACH_END;

	// If there are no info blocks, return null.
	return NULL;
}

// Check for a matching weighted revision list.
static AutoupConfigWeightedRevision **findAutoupInfoMatch(AutoupConfigRule **rules, const char *token, U32 ip)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(rules, i, n);
	{
		AutoupConfigRule *rule = rules[i];
		if(!rule->disabled
			&& findAutoupInfoForCategoryMatch(g_patchserver_config.serverCategory, rule)
			&& findAutoupInfoForTokenMatch(token, rule)
			&& checkAllowDeny(rule->ips, rule->deny_ips, ip)
		)
		{
			PERFINFO_AUTO_STOP_FUNC();

			return rule->autoup_rev;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

// Check for a matching named view.
static ClientHttpInfo *findHttpInfoForViewNamedViewMatch(HttpConfigNamedView **namedviews, const char *token, U32 ip, const char *project, const char *name, int branch, int rev)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(namedviews, i, n);
	{
		HttpConfigNamedView *namedview = namedviews[i];
		if (!namedview->disabled
			&& findHttpInfoForViewNamedViewCategoryMatch(g_patchserver_config.serverCategory, namedview)
			&& findHttpInfoForViewNamedViewTokenMatch(token, namedview)
			&& findHttpInfoForViewNamedViewProjectMatch(project, namedview)
			&& !stricmp_safe(name, namedview->name)
			&& checkAllowDeny(namedview->ips, namedview->deny_ips, ip))
		{
			PERFINFO_AUTO_STOP_FUNC();

			return findHttpInfoForViewReturnInfo(namedview->http_info);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

// Check for a matching branch.
static ClientHttpInfo *findHttpInfoForViewBranchMatch(HttpConfigBranch **branches, const char *token, U32 ip, const char *project, const char *name, int branch, int rev)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(branches, i, n);
	{
		HttpConfigBranch *branchconfig = branches[i];
		if (!branchconfig->disabled
			&& findHttpInfoForViewBranchCategoryMatch(g_patchserver_config.serverCategory, branchconfig)
			&& findHttpInfoForViewBranchTokenMatch(token, branchconfig)
			&& findHttpInfoForViewBranchProjectMatch(project, branchconfig)
			&& findHttpInfoForViewBranchBranchMatch(branch, branchconfig)
			&& (rev < 0 || rev >= branchconfig->min_rev)
			&& rev <= branchconfig->max_rev
			&& checkAllowDeny(branchconfig->ips, branchconfig->deny_ips, ip))
		{
			PERFINFO_AUTO_STOP_FUNC();

			return findHttpInfoForViewReturnInfo(branchconfig->http_info);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

// Find HTTP patching information suitable for a view.
static ClientHttpInfo *findHttpInfoForView(const char *token, U32 ip, const char *project, const char *name, int branch, int rev)
{
	ClientHttpInfo *result = NULL;

	PERFINFO_AUTO_START_FUNC();

	// If no project, there's nothing we can do.
	if (!project || !*project)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	if (g_patchserver_config.http_config && !g_patchserver_config.http_config->allow_http)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Search for a match.
	if (g_patchserver_config.http_config)
		result = findHttpInfoForViewNamedViewMatch(g_patchserver_config.http_config->namedviews, token, ip, project, name, branch, rev);
	if (!result && g_patchserver_config.dynamic_http_config)
		result = findHttpInfoForViewNamedViewMatch(g_patchserver_config.dynamic_http_config->namedviews, token, ip, project, name, branch, rev);
	if (!result && g_patchserver_config.http_config)
		result = findHttpInfoForViewBranchMatch(g_patchserver_config.http_config->branches, token, ip, project, name, branch, rev);
	if (!result && g_patchserver_config.dynamic_http_config)
		result = findHttpInfoForViewBranchMatch(g_patchserver_config.dynamic_http_config->branches, token, ip, project, name, branch, rev);

	PERFINFO_AUTO_STOP_FUNC();

	return result;
}

static void sendViewStatus(PatchClientLink *client, bool view_valid, const char *err_msg, ClientHttpInfo *http_info)
{
	Packet *pak_out;

	// If the request needs to be HTTP load balanced, initiate load balancer request.
	if (view_valid && http_info && http_info->load_balancer)
	{
		patchLoadBalanceRequest(client, http_info);
		return;
	}

	pak_out = pktCreate(client->link, PATCHSERVER_PROJECT_VIEW_STATUS);
	pktSendBits(pak_out, 8, view_valid);
	if(view_valid)
	{
		int branch_id;
		const char *http_info_string = "";

		pktSendBits(pak_out, 32, client->branch);
		pktSendBits(pak_out, 32, client->rev >= 0 ? client->project->serverdb->db->checkins[client->rev]->time : 0);
		pktSendString(pak_out, client->sandbox);
		pktSendBits(pak_out, 32, client->incr_from >= 0? client->project->serverdb->db->checkins[client->incr_from]->time : 0);
		pktSendBits(pak_out, 32, client->rev);
		pktSendBits(pak_out, 32, client->incr_from);

		// Send branch mapping
		{
			PatchBranch *branch = client->project->serverdb->branches[client->branch];
			if(!client->project->serverdb->basedb || branch->parent_branch == PATCHBRANCH_NONE)
				branch_id = PATCHBRANCH_NONE;
			else if(branch->parent_branch == PATCHBRANCH_TIP)
				branch_id = SAFE_MEMBER(client->project->serverdb->basedb,latest_branch);
			else
				branch_id = branch->parent_branch;
			pktSendBits(pak_out, 32, branch_id);
		}

		// Send branch name
		pktSendString(pak_out, client->project->serverdb->branches[client->branch]->name);

		// Send HTTP patching information
		if (http_info)
		{
			devassert(!http_info->load_balancer);
			http_info_string = NULL_TO_EMPTY(http_info->info);
		}
		pktSendString(pak_out, http_info_string);

		LOG_PATCH_RESPONSE(("branch", "%d", client->branch)
			("rev_time", "%lu", client->rev >= 0 ? client->project->serverdb->db->checkins[client->rev]->time : 0)
			("sandbox", "%s", NULL_TO_EMPTY(client->sandbox))
			("incr_time", "%lu", client->incr_from >= 0? client->project->serverdb->db->checkins[client->incr_from]->time : 0)
			("rev", "%d", client->rev)
			("incr_from", "%d", client->incr_from)
			("branch_map", "%d", branch_id)
			("branch_name", "%s", NULL_TO_EMPTY(client->project->serverdb->branches[client->branch]->name))
			("branch_name", "%s", NULL_TO_EMPTY(http_info_string))
		);
		if (http_info)
			StructDestroy(parse_ClientHttpInfo, http_info);
	}
	else
	{
		pktSendString(pak_out, err_msg);
		LOG_PATCH_RESPONSE(("error", "1"));
	}
	pktSend(&pak_out);
}

static void sendViewStatusAndFlush(PatchClientLink *client, bool view_valid, const char *err_msg, ClientHttpInfo *http_info)
{
	sendViewStatus(client, view_valid, err_msg, http_info);
	flushLink(client->link);
}

void handleSetProjectViewByRev(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	bool view_valid;
	char * project;
	char * sandbox;
	int branch, rev;
	char err_msg[1024] = "";
	ClientHttpInfo *http_info = NULL;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PROJECT_VIEW_BY_REV, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("packet did not have enough remaining bytes");}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet not null terminated");}

	CHECK_STR;
	project = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	branch = pktGetBits(pak_in, 32);
	CHECK_STR;
	sandbox = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	rev = pktGetBits(pak_in, 32);

#undef QUIT_HANDLE
#undef CHECK_BYTES
#undef CHECK_STR

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(project))
		("branch", "%d", branch)
		("sandbox", "%s", NULL_TO_EMPTY(sandbox))
		("rev", "%d", rev));

	view_valid = setProjectView(client, project, branch, sandbox, 0, rev, false, false, SAFESTR(err_msg));

	// Look for HTTP patching information.
	if (view_valid)
		http_info = findHttpInfoForView(client->autoupdate_token, linkGetIp(link), project, NULL, branch, rev);

	sendViewStatus(client, view_valid, err_msg, http_info);
}

void handleSetProjectViewByTime(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	bool view_valid;
	char * project;
	char * sandbox;
	int branch;
	U32 time;
	char err_msg[1024] = "";
	ClientHttpInfo *http_info = NULL;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PROJECT_VIEW_BY_TIME, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("packet did not have enough remaining bytes");}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet not null terminated");}

	CHECK_STR;
	project = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	branch = pktGetBits(pak_in, 32);
	CHECK_STR;
	sandbox = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	time = pktGetBits(pak_in, 32);

#undef QUIT_HANDLE
#undef CHECK_BYTES
#undef CHECK_STR

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(project)) ("branch", "%d", branch)
		("sandbox", "%s", NULL_TO_EMPTY(sandbox)));

	view_valid = setProjectView(client, project, branch, sandbox, time, PATCHREVISION_NONE, false, false, SAFESTR(err_msg));

	// Look for HTTP patching information.
	if (view_valid)
		http_info = findHttpInfoForView(client->autoupdate_token, linkGetIp(link), project, NULL, branch, client->rev);

	sendViewStatus(client, view_valid, err_msg, http_info);
}

void handleSetProjectViewDefault(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char * project;
	bool view_valid;
//	PatchProject *proj;
	char err_msg[1024] = "";

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PROJECT_VIEW_DEFAULT, 0, 0, 0, "Missing project");
		return;
	}
	project = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(project)));

// this used to be configurable, get latest instead
//	proj = patchFindProject(project);
//	view_valid = setProjectView(client, project, proj->branch, proj->sandbox, proj->time, false, false, SAFESTR(err_msg));
	view_valid = setProjectView(client, project, -1, "", 0, PATCHREVISION_NONE, false, false, SAFESTR(err_msg));
	sendViewStatus(client, view_valid, err_msg, NULL);
}

void handleSetProjectViewName(Packet *pak_in, NetLink * link, PatchClientLink * client)
{
	char * project, * name;
	bool view_valid;
	PatchProject *proj;
	NamedView *view;
	char err_msg[1024];
	ClientHttpInfo *http_info = NULL;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PROJECT_VIEW_NAME, 0, 0, 0, reason); return;}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet was not null terminated");}

	CHECK_STR;
	project = pktGetStringTemp(pak_in);
	CHECK_STR;
	name = pktGetStringTemp(pak_in);

#undef QUIT_HANDLE
#undef CHECK_STR

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(project)) ("viewname", "%s", NULL_TO_EMPTY(name)));

	proj = patchserverFindProjectChecked(project, linkGetSAddr(client->link)); // TODO: this gets checked again in setProjectView
	if(!proj)
	{
		sprintf(err_msg, "Could not find project name \"%s\"", project);
		view_valid = false;
	}
	else if(!(view = patchFindNamedView(proj->serverdb->db, name)))
	{
		sprintf(err_msg, "Could not find a view for name \"%s\"", name);
		view_valid = false;
	}
	else if(view->expires && view->expires < getCurrentFileTime())
	{
		sprintf(err_msg, "View \"%s\" is expired", name);
		view_valid = false;
	}
	else
	{
		strcpy_trunc(client->view_name, view->name);
		view_valid = setProjectView(client, project, view->branch, view->sandbox, 0, view->rev, false, 
									view->expires >= getCurrentFileTime(), SAFESTR(err_msg));
		if(view_valid)
		{
			char ip_buf[16];
			if(client->rev != view->rev)
				printfColor(COLOR_RED|COLOR_BRIGHT, "VIEW ERROR: Client set to rev %d, view %s is rev %d\n", client->rev, view->name, view->rev);

			view->viewed++;
			patchserverLog(STACK_SPRINTF("view %s", name), "viewed", "ip %s", linkGetIpStr(link, SAFESTR(ip_buf)));
			patchupdateNotifyView(project, name, linkGetIp(link));
			if(!g_patchserver_config.parent.server && view->viewed_external == 0 && g_patchserver_config.internal
				&& !checkAllowDeny(g_patchserver_config.internal->ips, g_patchserver_config.internal->deny_ips, linkGetIp(link)))
			{
				view->viewed_external = 1;
				journalAddViewedExternalFlush(eaSize(&proj->serverdb->db->checkins) - 1, proj->serverdb->name, name);
			}
			if(g_patchserver_config.prune_config && g_patchserver_config.prune_config->view_expires)
			{
				U32 new_expires = getCurrentFileTime();
				ViewExpires *view_expires = proj->serverdb->view_expires_override ? proj->serverdb->view_expires_override : g_patchserver_config.prune_config->view_expires;
				if(view->viewed_external)
					new_expires += (SECONDS_PER_DAY * view_expires->public);
				else
					new_expires += (SECONDS_PER_DAY * view_expires->internal);
				if(new_expires > view->expires)
				{
					view->expires = new_expires;
					view->dirty = true;
					journalAddViewDirtyFlush(eaSize(&proj->serverdb->db->checkins) - 1, proj->serverdb->name, name);
				}
			}
		}
	}

	// Look for HTTP patching information.
	if (view_valid)
		http_info = findHttpInfoForView(client->autoupdate_token, linkGetSAddr(link), project, name, view->branch, view->rev);

	sendViewStatus(client, view_valid, err_msg, http_info);
}

void handleSetProjectViewNewIncremental(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	bool view_valid;
	char * project;
	char * sandbox;
	int branch, rev = -1;
	char err_msg[1024] = "";

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("packet did not have enough remaining bytes");}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet not null terminated");}

	CHECK_STR;
	project = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	branch = pktGetBits(pak_in, 32);
	CHECK_STR;
	sandbox = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	rev = pktGetBits(pak_in, 32);

#undef QUIT_HANDLE
#undef CHECK_BYTES
#undef CHECK_STR

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(project))
		("branch", "%d", branch) ("sandbox", "%s", NULL_TO_EMPTY(sandbox)));

	view_valid = setProjectView(client, project, branch, sandbox, 0, rev, true, false, SAFESTR(err_msg));
	sendViewStatus(client, view_valid, err_msg, NULL);
}

void handleSetProjectViewNewIncrementalName(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	bool view_valid;
	char * project;
	char * sandbox;
	char * name;
	char err_msg[1024] = "";
	PatchProject *proj;
	NamedView *view_from;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL_NAME, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("packet did not have enough remaining bytes");}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet not null terminated");}

	CHECK_STR;
	project = pktGetStringTemp(pak_in);
	CHECK_STR;
	sandbox = pktGetStringTemp(pak_in);
	CHECK_STR;
	name = pktGetStringTemp(pak_in);

#undef QUIT_HANDLE
#undef CHECK_BYTES
#undef CHECK_STR

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(project))
		("sandbox", "%s", NULL_TO_EMPTY(sandbox)) ("viewname", "%s", NULL_TO_EMPTY(name)));

	proj = patchserverFindProjectChecked(project, linkGetSAddr(client->link)); // TODO: this gets checked again in setProjectView
	view_from = proj ? patchFindNamedView(proj->serverdb->db, name) : NULL;

	if(view_from)
		view_valid = setProjectView(client, project, view_from->branch, sandbox, 0, view_from->rev, true, false, SAFESTR(err_msg));
	else
		view_valid = false;
	sendViewStatus(client, view_valid, err_msg, NULL);
}

DirEntry *dirFromProjectAndName(PatchProject * proj, char *fname, char ** err_msg)
{
	DirEntry		*dir;
	if (!proj)
	{
		*err_msg = "can't find project";
		return NULL;
	}
	if (!proj->allow_checkins)
	{
		*err_msg = "project is patch-only";
		return NULL;
	}
	if(!patchprojectIsPathIncluded(proj, fname, NULL))
	{
		*err_msg = "project does not include filename";
		return NULL;
	}
	dir = patchFindPath(proj->serverdb->db, fname, 0);
	if(!dir)
	{
		*err_msg = "can't find filename";
		return NULL;
	}
	return dir;
}

void handleReqFileHistory(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet		*pak;
	char		*fname, *err_msg = "";
	DirEntry	*dir;
	int			i, j;
	PatcherFileHistory history = {0};

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_FILE_HISTORY_STRUCTS, 0, 0, 0, "no filename");
		return;
	}
	fname = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)));

	if(!validateAndLoadView(client))
		return;

	pak = pktCreate(link, PATCHSERVER_FILE_HISTORY_STRUCTS);
	dir = dirFromProjectAndName(client->project, fname, &err_msg);

	// Setup structure to send
	history.dir_entry = dir;
	if (dir)
	{
		history.flags = dir->flags;
		for(i=0;i<eaSize(&dir->versions);i++)
		{
			char		*batch_info=NULL;
			eaPush(&history.checkins, dir->versions[i]->checkin);
			// Batch info
			estrStackCreate(&batch_info);
			for (j=0; j<eaSize(&dir->versions[i]->checkin->versions); j++) {
				estrConcatf(&batch_info, "%s\n", dir->versions[i]->checkin->versions[j]->parent->path);
			}
			eaPush(&history.batch_info, strdup(batch_info));
			estrDestroy(&batch_info);
		}
	}
	ParserSendStructSafe(parse_PatcherFileHistory, pak, &history);
	pktSend(&pak);
	LOG_PATCH_RESPONSE(("versions", "%d", dir ? eaSize(&dir->versions) : 0));

	// Destroy anything we allocated here
	eaDestroy(&history.checkins);
	eaDestroyEx(&history.batch_info, NULL);
}

void handleReqVersionInfo(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet *pak_out;
	char *err_msg = NULL;
	FileVersion *ver;
	PatchServerDb *serverdb;
	char *dir_name;

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_VERSION_INFO);
		return;
	}

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_VERSION_INFO, 0, 0, 0, "no dir");
		return;
	}

	dir_name = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("dir", "%s", NULL_TO_EMPTY(dir_name)));

	ver = patchprojectFindVersion(client->project, dir_name, client->branch, client->sandbox, client->rev, client->incr_from, client->prefix, &serverdb);
	assert(serverdb == client->project->serverdb);

	if(!ver)
	{
		err_msg = "file not found in this database/view";
	}

	pak_out = pktCreate(link, PATCHSERVER_VERSION_INFO);
	if(err_msg)
	{
		pktSendBool(pak_out, false);
		pktSendString(pak_out, err_msg);
		LOG_PATCH_RESPONSE(("success", "1") ("string", "%s", err_msg));
	}
	else
	{
		pktSendBool(pak_out, true);
		pktSendBits(pak_out, 32, (ver->parent->flags & DIRENTRY_FROZEN) ? -1 : ver->checkin->branch);
		pktSendBits(pak_out, 32, ver->checkin->time);
		pktSendString(pak_out, ver->checkin->sandbox);
		pktSendString(pak_out, ver->checkin->author);
		pktSendString(pak_out, ver->checkin->comment);
		LOG_PATCH_RESPONSE(("success", "0") ("branch", "%d", (ver->parent->flags & DIRENTRY_FROZEN) ? -1 : ver->checkin->branch)
			("time", "%lu", ver->checkin->time) ("sandbox", "%s", NULL_TO_EMPTY(ver->checkin->sandbox))
			("author", "%s", NULL_TO_EMPTY(ver->checkin->author)) ("comment", "%s", NULL_TO_EMPTY(ver->checkin->comment)));
	}
	pktSend(&pak_out);
}

void handleReqBranchInfo(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet *pak_out;
	U32 branch_id = -1;
	PatchBranch *branch = NULL;

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_BRANCH_INFO);
		return;
	}

	if(pktCheckRemaining(pak_in, 4))
		branch_id = pktGetBits(pak_in, 32);
	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("branch", "%lu", branch_id));
	if (branch_id != -1)
		branch = eaGet(&client->project->serverdb->branches, branch_id);
	if(!branch)
		branch = client->project->serverdb->branches[client->branch];

	pak_out = pktCreate(link, PATCHSERVER_BRANCH_INFO);
	if(branch)
	{
		int base_branch;
		pktSendString(pak_out, branch->name);
		if(!client->project->serverdb->basedb || branch->parent_branch == PATCHBRANCH_NONE)
			base_branch = PATCHBRANCH_NONE;
		else if(branch->parent_branch == PATCHBRANCH_TIP)
			base_branch = SAFE_MEMBER(client->project->serverdb->basedb,latest_branch);
		else
			base_branch = branch->parent_branch;
		pktSendBits(pak_out, 32, base_branch);
		pktSendString(pak_out, branch->warning);
		LOG_PATCH_RESPONSE(("branch_name", "%s", NULL_TO_EMPTY(branch->name)) ("base_branch", "%d", base_branch)
			("warning", "%s", NULL_TO_EMPTY(branch->warning)));
	}
	else
		LOG_PATCH_RESPONSE(("error", "1"));
	pktSend(&pak_out);
}

static bool timestampsMatch(U32 a, U32 b)
{
	return a == b ||
#ifdef _XBOX // FATX has a resolution of 2 seconds, so GetFileTime may not return the same value given to SetFileTime
		ABS_UNS_DIFF(a,b) == 1 ||
#endif
		ABS_UNS_DIFF(a,b) == 3600;
}


void handleReqLastAuthor(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet *pak_out;
	const char *fname;
	U32 timestamp;
	U32 filesize;
	DirEntry *dir;
	FileVersion *ver;
	char err_buf[MAX_PATH];

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_LASTAUTHOR);
		return;
	}

	pak_out = pktCreate(link, PATCHSERVER_LASTAUTHOR);

	fname = pktGetStringTemp(pak_in);
	timestamp = pktGetBits(pak_in, 32);
	filesize = pktGetBits(pak_in, 32);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)) ("timestamp", "%lu", timestamp) ("filesize", "%lu", filesize));

	if(!client->project)
	{
		pktSendBitsAuto(pak_out, LASTAUTHOR_ERROR);
		sprintf(err_buf, "Invalid project %s", client->project ? client->project->name : "");
		pktSendString(pak_out, err_buf);
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("error", "%s", err_buf));
		return;
	}

	if(strchr(fname, '*') || strchr(fname, ':'))
	{
		pktSendBitsAuto(pak_out, LASTAUTHOR_ERROR);
		sprintf(err_buf, "%s is an invalid file name", fname);
		pktSendString(pak_out, err_buf);
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("error", "%s", err_buf));
		return;
	}

	dir = patchFindPath(client->project->serverdb->db, fname, 0);
	ver = dir ? patchFindVersionInDir(dir, client->branch, client->sandbox, INT_MAX, client->incr_from) : NULL;
	if (!ver)
	{
		pktSendBitsAuto(pak_out, LASTAUTHOR_NOT_IN_DATABASE);
		LOG_PATCH_RESPONSE(("status", "not_in_database"));
	} else {
		Checkout *checkout = patchFindCheckoutInDir(dir, client->branch, client->sandbox);
		if (checkout && stricmp(client->author,checkout->author)==0)
		{
			pktSendBitsAuto(pak_out, LASTAUTHOR_CHECKEDOUT);
			LOG_PATCH_RESPONSE(("status", "checked_out"));
		}
		else 
		{
			// Check if file is up to date
			if (ver->flags & FILEVERSION_DELETED) {
				if (timestamp > 0) {
					pktSendBitsAuto(pak_out, LASTAUTHOR_NOT_LATEST);
					LOG_PATCH_RESPONSE(("status", "not_latest"));
				} else {
					pktSendBitsAuto(pak_out, LASTAUTHOR_GOT_AUTHOR);
					pktSendString(pak_out, ver->checkin->author);
					LOG_PATCH_RESPONSE(("status", "got_author") ("author", "%s", NULL_TO_EMPTY(ver->checkin->author)));
				}
			} else if (ver->size != filesize) {
				pktSendBitsAuto(pak_out, LASTAUTHOR_NOT_LATEST);
				LOG_PATCH_RESPONSE(("status", "not_latest"));
			} else if (!timestampsMatch(ver->modified, timestamp)) {
				pktSendBitsAuto(pak_out, LASTAUTHOR_NOT_LATEST);
				LOG_PATCH_RESPONSE(("status", "not_latest"));
			} else {
				pktSendBitsAuto(pak_out, LASTAUTHOR_GOT_AUTHOR);
				pktSendString(pak_out, ver->checkin->author);
				LOG_PATCH_RESPONSE(("status", "got_author") ("author", "%s", NULL_TO_EMPTY(ver->checkin->author)));
			}
		}
	}
	pktSend(&pak_out);
}

void handleReqLockAuthor(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet *pak_out;
	const char *fname;
	DirEntry *dir;
	FileVersion *ver;

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_LASTAUTHOR);
		return;
	}

	pak_out = pktCreate(link, PATCHSERVER_LOCKAUTHOR);

	fname = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)));

	if(!client->project)
	{
		pktSendBits(pak_out, 1, 0);
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("success", "0"));
		return;
	}

	if(strchr(fname, '*') || strchr(fname, ':'))
	{
		pktSendBits(pak_out, 1, 0);
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("success", "0"));
		return;
	}

	dir = patchFindPath(client->project->serverdb->db, fname, 0);
	ver = dir ? patchFindVersionInDir(dir, client->branch, client->sandbox, INT_MAX, client->incr_from) : NULL;
	if (!ver)
	{
		pktSendBits(pak_out, 1, 0);
		LOG_PATCH_RESPONSE(("success", "0"));
	} else {
		Checkout *checkout = patchFindCheckoutInDir(dir, client->branch, client->sandbox);
		if (checkout)
		{
			pktSendBits(pak_out, 1, 1);
			pktSendString(pak_out, checkout->author);
			LOG_PATCH_RESPONSE(("success", "1") ("author", "%s", checkout->author));
		}
		else 
		{
			pktSendBits(pak_out, 1, 0);
			LOG_PATCH_RESPONSE(("success", "0"));
		}
	}
	pktSend(&pak_out);
}

static void freeAddedDirStashKey(char* stashKey)
{
	SAFE_FREE(stashKey);
}

void handleReqLock(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet			*pak;
	char			*err_msg = "", err_buf[MAX_PATH];
	DirEntry		**dirs = NULL;
	FileVersion		*ver;
	Checkout		*checkout;
	int				success = 0;
	StashTable		stAddedCheckoutDirs;
	U32				*checksums = NULL;
	int				n, i;

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_LOCK);
		return;
	}

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT);
		
	stAddedCheckoutDirs = stashTableCreateWithStringKeys(100, StashDefault);

	while(pktCheckRemaining(pak_in, 1))
	{
		DirEntry*	dir;
		char		stashKey[MAX_PATH * 2];

		if(!pktCheckNullTerm(pak_in))
		{
			badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_LOCK, 0, 0, 0, "no dir");
			sprintf(err_buf, "Bad packet");
			err_msg = err_buf;
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}

		dir = dirFromProjectAndName(client->project, pktGetStringTemp(pak_in), &err_msg);
		if(!dir)
		{
			LOG_PATCH_RESPONSE(("success", "0"));
			goto failed;
		}

		if(client->sandbox && client->sandbox[0] && client->incr_from != PATCHREVISION_NONE)
		{
			sprintf(err_buf, "You can't make checkouts from an incremental sandbox, use Forceins instead of Checkins");
			err_msg = err_buf;
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}

		ver = patchFindVersionInDir(dir, client->branch, client->sandbox, client->rev, client->incr_from);
		if (ver->flags & FILEVERSION_DELETED)
		{
			sprintf(err_buf, "%s is deleted on branch %d", dir->path, client->branch);
			err_msg = err_buf;
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}
		ea32Push(&checksums, ver->checksum);

		sprintf(stashKey,
				"%p:%d%s%s",
				dir,
				client->branch,
				client->sandbox ? ":" : "",
				NULL_TO_EMPTY(client->sandbox));
				
		if(stashFindPointer(stAddedCheckoutDirs, stashKey, NULL))
		{
			// Already added, so just ignore it.
			
			continue;
		}

		checkout = patchFindCheckoutInDir(dir, client->branch, client->sandbox);
		if(checkout)
		{
			if(stricmp(checkout->author, client->author) != 0)
			{
				sprintf(err_buf, "%s already locked on branch %d by %s", dir->path, checkout->branch, checkout->author);
				err_msg = err_buf;
				LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
				goto failed;
			}
			// else it's already locked
		}
		else
		{
			eaPush(&dirs, dir);
			if(!stashAddPointer(stAddedCheckoutDirs, strdup(stashKey), NULL, false))
			{
				assert(0);
			}
		}
	}
	
	printf(	"%s: Attempting checkout of %d files (author %s, branch %d%s%s)\n",
			makeIpStr(linkGetIp(link)),
			eaSize(&dirs),
			client->author,
			client->branch,
			client->sandbox[0] ? ", sandbox " : "",
			client->sandbox[0] ? client->sandbox : "");
			
	// NOTE: Limit this to 10 files at most so avoid huge printf stalls. <NPK 2009-12-21>
	for(i=0; i<eaSize(&dirs); i++)
	{
		printf("  File: %s\n", dirs[i]->path);
		if(i >= 10)
		{		
			printf("  ...\n");
			break;
		}
	}

	success = patchserverdbAddCheckouts(client->project->serverdb,
										dirs,
										client->author,
										client->branch,
										client->sandbox,
										SAFESTR(err_buf));

	if(success)
	{
		if (g_log_patch_cmd_verbosity)
		{
			char *files = NULL;
			estrStackCreate(&files);
			for(i=0; i<eaSize(&dirs); i++)
				estrConcatf(&files, "%s%s", i == 0 ? "" : ", ", dirs[i]->path);
			LOG_PATCH_RESPONSE(("success", "1") ("files", "%s", files));
			estrDestroy(&files);
		}
	}
	else
	{
		err_msg = err_buf;
		LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
	}
	
	printfColor(COLOR_BRIGHT | (success ? COLOR_GREEN : COLOR_RED),
				"Checkout %s!\n",
				success ? "successful" : "failed");

failed:
	stashTableDestroyEx(stAddedCheckoutDirs, freeAddedDirStashKey, NULL);
	stAddedCheckoutDirs = NULL;

	pak = pktCreate(link, PATCHSERVER_LOCK);
	pktSendBits(pak,32,success);
	if (err_msg[0]){
		pktSendString(pak,err_msg);
	}else{
		pktSendString(pak, "The server didn't provide an error message");
	}
	for(n=0; n<ea32Size(&checksums); n++)
		pktSendU32(pak, checksums[n]);
	pktSend(&pak);

	ea32Destroy(&checksums);
	eaDestroy(&dirs);
}

void handleReqUnlock(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet			*pak;
	char			*err_msg = "", err_buf[MAX_PATH];
	DirEntry		**dirs = NULL;
	Checkout		*checkout;
	int				success = 0;

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_UNLOCK);
		return;
	}

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT);

	while(pktCheckRemaining(pak_in, 1))
	{
		DirEntry *dir;

		if(!pktCheckNullTerm(pak_in))
		{
			badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_UNLOCK, 0, 0, 0, "no dir");
			sprintf(err_buf, "Bad packet");
			err_msg = err_buf;
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}

		dir = dirFromProjectAndName(client->project, pktGetStringTemp(pak_in), &err_msg);
		if(!dir)
		{
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}

		checkout = patchFindCheckoutInDir(dir, client->branch, client->sandbox);
		if(!checkout)
		{
			sprintf(err_buf,
					"%s is not checked out in branch %d %s",
					dir->path,
					client->branch,
					NULL_TO_EMPTY(client->sandbox));
			err_msg = err_buf;
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}

		if(stricmp(client->author, checkout->author) != 0)
		{
			sprintf(err_buf, "%s is checked out by another author (%s)", dir->path, checkout->author);
			err_msg = err_buf;
			LOG_PATCH_RESPONSE(("success", "0") ("error", "%s", err_msg));
			goto failed;
		}

		eaPush(&dirs, dir);
	}

	if (g_log_patch_cmd_verbosity)
	{
		int i;
		char *files = NULL;
		estrStackCreate(&files);
		for(i=0; i<eaSize(&dirs); i++)
			estrConcatf(&files, "%s%s", i == 0 ? "" : ", ", dirs[i]->path);
		LOG_PATCH_RESPONSE(("success", "1") ("files", "%s", files));
		estrDestroy(&files);
	}

	patchserverdbRemoveCheckouts(client->project->serverdb, dirs, client->branch, client->sandbox);
	success = 1;

failed:

	pak = pktCreate(link, PATCHSERVER_UNLOCK);
	pktSendBits(pak,32,success);
	if (err_msg[0])
		pktSendString(pak,err_msg);
	pktSend(&pak);

	eaDestroy(&dirs);
}

static int fileCanBeCheckedIn(	PatchClientLink *client,
								PatchProject * proj,
								char *fname,
								int is_delete,
								char * err_msg,
								int err_msg_size,
								bool force)
{
	DirEntry *dir;
	FileVersion *ver;

	if(!proj || !proj->allow_checkins)
	{
		sprintf_s(SAFESTR2(err_msg), "Invalid project %s", proj ? proj->name : "");
		return 0;
	}

	if (strlen(fname) > PATCH_MAX_PATH)		//should match checks in pcl_client.c/checkinSendNames and patchmeui.c/patchmeDialogCheckin
	{
		sprintf_s(SAFESTR2(err_msg), "\"%s\" is over the limit of " STRINGIZE(PATCH_MAX_PATH) " characters.", fname);
		return 0;
	}

	if(strchr(fname, '*') || strchr(fname, ':'))
	{
		sprintf_s(SAFESTR2(err_msg), "%s is an invalid file name", fname);
		return 0;
	}

	dir = patchFindPath(proj->serverdb->db, fname, 0);
	ver = dir ? patchFindVersionInDir(dir, client->branch, client->sandbox, INT_MAX, client->incr_from) : NULL;
	if((!ver || ver->flags & FILEVERSION_DELETED) && is_delete)
	{
		sprintf_s(SAFESTR2(err_msg), "can't delete a file that doesn't exist (%s branch %d)", fname, client->branch);
		return 0;
	}
	if(ver && !(ver->flags & FILEVERSION_DELETED) && !force)
	{
		Checkout *checkout = patchFindCheckoutInDir(dir, client->branch, client->sandbox);
		if(!checkout)
		{
			sprintf_s(SAFESTR2(err_msg), "%s on branch %d is not checked out", fname, client->branch);
			return 0;
		}
		else if(stricmp(client->author,checkout->author)!=0)
		{
			sprintf_s(SAFESTR2(err_msg), "%s on branch %d is locked by another user (%s)", fname, checkout->branch, checkout->author);
			return 0;
		}
	}
	return 1;
}

static void handleReqCheckin(Packet *pak_in, NetLink *link, PatchClientLink *client, bool force)
{
	Packet			*pak = NULL;
	char			err_msg[1024];
	int				i, count, success = 0, cmd;
	CheckinFile		*file = NULL;
	StashSet		filenames;

	if(	patchupdateIsChildServer() ||
		!validateAndLoadView(client) ||
		!client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_CHECKIN);
		return;
	}

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT);

	err_msg[0] = '\0';

	if(force)
		cmd = PATCHSERVER_FORCEIN;
	else
		cmd = PATCHSERVER_CHECKIN;

#define QUIT_HANDLE(reason)\
	{\
		if(file)\
			SAFE_FREE(file->fname);\
		SAFE_FREE(file);\
		freePendingCheckins(client);\
		badPacket(client, __FUNCTION__, NULL, force ? PATCHCLIENT_REQ_FORCEIN : PATCHCLIENT_REQ_CHECKIN, 0, 0, 0, reason);\
		return;\
	}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("packet did not have enough remaining bytes");}

	// Get number of files in checkin.
	CHECK_BYTES(4);
	count = pktGetBits(pak_in, 32);
	filenames = stashSetCreate(count, __FILE__, __LINE__);

	for(i = 0; i < count; i++)
	{
		bool unique;

		file = callocStruct(CheckinFile);

		if(!pktCheckNullTerm(pak_in))
			QUIT_HANDLE("packet was not null terminated");
		file->fname = pktMallocString(pak_in);

		CHECK_BYTES(4);
		file->size_uncompressed = pktGetBits(pak_in,32);
		file->size_to_receive = file->size_uncompressed;

		CHECK_BYTES(4);
		file->checksum = pktGetBits(pak_in,32);

		CHECK_BYTES(4);
		file->modified = pktGetBits(pak_in,32);

		file->data = NULL;

		eaPush(&client->checkin_files, file);
		if (!fileCanBeCheckedIn(client,
								client->project,
								file->fname,
								file->size_uncompressed == -1,
								SAFESTR(err_msg),
								force))
		{
			break;
		}

		// Check for dupes.
		unique = stashSetAdd(filenames, file->fname, false, NULL);
		if(!unique)
		{
			sprintf_s(SAFESTR(err_msg), "Duplicate file: %s", file->fname);
			break;
		}
	}

	stashSetDestroy(filenames);

	// Send response to client.
	
	pak = pktCreate(link, cmd);

	if (i < count)
	{
		freePendingCheckins(client);
		pktSendBits(pak,32,0);
		LOG_PATCH_RESPONSE(("success", "0") ("force", "%d", !!force));
	}
	else
	{
		pktSendBits(pak,32,1);
		if (g_log_patch_cmd_verbosity)
		{
			char *files = NULL;
			estrStackCreate(&files);
			for(i=0; i<eaSize(&client->checkin_files); i++)
				estrConcatf(&files, "%s%s", i == 0 ? "" : ", ", client->checkin_files[i]->fname);
			LOG_PATCH_RESPONSE(("success", "1") ("force", "%d", !!force) ("files", "%s", files));
			estrDestroy(&files);
		}
	}
	pktSendString(pak, err_msg);
	pktSend(&pak);

#undef QUIT_HANDLE
#undef CHECK_BYTES
}

static void handleBlockSend(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	int			file_idx,byte_idx,amt,total=0;
	Packet*		pak;
	S32			isCompressed;

	if(	patchupdateIsChildServer() ||
		!validateAndLoadView(client) ||
		!client->project->allow_checkins)
	{
		return;
	}
	
	isCompressed = client->patcher_version >= PATCHCLIENT_VERSION_CLIENT_CHECKIN_COMPRESSION;

	#define QUIT_HANDLE(reason) {													\
				badPacket(client, "handleBlockSend",								\
								isCompressed ? "compressed" : "uncompressed",		\
								PATCHCLIENT_BLOCK_SEND, 0, 0, 0, reason);			\
				freePendingCheckins(client);										\
				return;																\
			}

	#define CHECK_BYTES(x) {														\
				if(!pktCheckRemaining(pak_in, (x))){								\
					QUIT_HANDLE("Packet does not have enough remaining bytes");		\
				}																	\
			}
	
	for(;;)
	{
		CheckinFile* cf;
		
		CHECK_BYTES(4);		
		file_idx = pktGetBits(pak_in,32);
		
		if(	file_idx < 0 ||
			file_idx >= eaSize(&client->checkin_files))
		{
			break;
		}
		
		cf = client->checkin_files[file_idx];
		
		if(cf->size_uncompressed < 0)
		{
			QUIT_HANDLE("Receiving data for a deleted file");
		}

		CHECK_BYTES(4);
		byte_idx = pktGetBits(pak_in,32);
		
		if(isCompressed){
			if(!byte_idx){
				CHECK_BYTES(4);
				cf->size_to_receive = pktGetBits(pak_in,32);
			}
		}else{
			cf->size_to_receive = cf->size_uncompressed;
		}
		
		CHECK_BYTES(4);
		amt = pktGetBits(pak_in,32);

		if(!cf->data) // FIXME: not safe for monkeys
		{
			char pending_fname[MAX_PATH], cwd[MAX_PATH];
			U32 len;
			FILE * fPending;

			sprintf(pending_fname, "./pending_checkins/%"FORM_LL"i/%s", client->UID, cf->fname);
			if(fileExists(pending_fname))
			{
				cf->data = fileAlloc(pending_fname, &len);
			}
			else
			{
				len = cf->size_to_receive;
				cf->data = malloc(len);
			}
			assert(len == cf->size_to_receive);
			client->checkinsInMemoryCount++;
			client->checkinsInMemoryBytes += cf->size_to_receive;
			cf->last_used = timerCpuTicks64();
			while(	client->checkinsInMemoryBytes > MAX_PENDING_CHECKINS_BYTES)
			{
				CheckinFile*	cfToWrite = NULL;
				U64				curTime = timerCpuTicks64();
				U64				maxDiff = 0;

				EARRAY_FOREACH_REVERSE_BEGIN(client->checkin_files, i);
					CheckinFile* cfCur = client->checkin_files[i];
					if(cfCur->data){
						U64 diff = curTime - cfCur->last_used;
						
						if(diff > maxDiff){
							maxDiff = diff;
							cfToWrite = cfCur;
						}
					}
				EARRAY_FOREACH_END;
				
				assert(SAFE_MEMBER(cfToWrite, data));
				sprintf(pending_fname,
						"%s/pending_checkins/%"FORM_LL"i/%s",
						fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)), // XXX: Remove this one makeDirectories works with rel paths again. <NPK 2009-01-22>
						client->UID,
						cfToWrite->fname);
				makeDirectoriesForFile(pending_fname);
				
				fPending = fopen(pending_fname, "wb");
				assertmsgf(fPending, "Out of disk space writing file: %s", pending_fname);
				fwrite(cfToWrite->data, cfToWrite->size_to_receive, 1, fPending);
				fclose(fPending);
				
				SAFE_FREE(cfToWrite->data);
				
				assert(client->checkinsInMemoryCount);
				client->checkinsInMemoryCount--;

				assert(client->checkinsInMemoryBytes >= cfToWrite->size_to_receive);
				client->checkinsInMemoryBytes -= cfToWrite->size_to_receive;
			}
		}

		CHECK_BYTES(amt);
		if(byte_idx + amt > cf->size_to_receive)
		{
			QUIT_HANDLE("Packet would overflow checkin file data");
		}
		pktGetBytes(pak_in, amt, cf->data + byte_idx);
		
		// Check if the compressed data will decompress and checksum correctly.
		PERFINFO_AUTO_START("CheckCompression", 1);
		if(	isCompressed &&
			byte_idx + amt == cf->size_to_receive)
		{
			U32		size_uncompressed = cf->size_uncompressed;
			void*	data_uncompressed = malloc(cf->size_uncompressed);
			char	errorMsg[1000] = "";

			if(unzipData(data_uncompressed, &size_uncompressed, cf->data, cf->size_to_receive))
			{
				sprintf(errorMsg,
						"File \"%s\" failed to decompress.",
						cf->fname);
			}
			
			if(	!errorMsg[0] &&
				size_uncompressed != cf->size_uncompressed)
			{
				sprintf(errorMsg,
						"File \"%s\" decompressed to %d bytes, but expected %d bytes.",
						cf->fname,
						size_uncompressed,
						cf->size_uncompressed);
			}
			
			if(	!errorMsg[0] &&
				size_uncompressed)
			{
				U32 checksum[4];
				
				pigChecksumData(data_uncompressed, size_uncompressed, checksum);
				
				if(checksum[0] != cf->checksum)
				{
					sprintf(errorMsg,
							"File \"%s\" checksum is 0x%x, expected 0x%x.",
							cf->fname,
							checksum[0],
							cf->checksum);
				}
			}

			SAFE_FREE(data_uncompressed);
			
			if(errorMsg[0]){
				QUIT_HANDLE(errorMsg);
			}
		}
		PERFINFO_AUTO_STOP();

		total += amt;
	}
	pak = pktCreate(link, PATCHSERVER_BLOCK_RECV);
	pktSendBits(pak, 32, total);
	pktSend(&pak);
	LOG_PATCH_RESPONSE_VERBOSE2(("total", "%d", total));

#undef QUIT_HANDLE
#undef CHECK_BYTES
}

static void handleFinishCheckin(Packet *pak_in, NetLink *link, PatchClientLink *client, bool forced)
{
	char				buf[MAX_PATH];
	char*				comment;
	char*				err_msg = "";
	int					success = 0;
	Checkin*			checkin;
	PatchServerDb*		serverdb;
	FileVersion*		version;
	PatchJournal*		journal;
	JournalCheckin*		journalCheckin;
	FileVersion**		new_versions = NULL;
	bool				incremental = false;
	U64					totalBytesToWrite = 0;
	S32					filesToWriteCount = 0;
	S32					filesToDeleteCount = 0;
	S32					isCompressed;
	float				lastPercent = 0;

	if(	patchupdateIsChildServer() ||
		!validateAndLoadView(client) ||
		!client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(forced ? PATCHCLIENT_FINISH_FORCEIN : PATCHCLIENT_FINISH_CHECKIN);
		return;
	}
	
	isCompressed = client->patcher_version >= PATCHCLIENT_VERSION_CLIENT_CHECKIN_COMPRESSION;
	
	serverdb = client->project->serverdb;


	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, forced ? PATCHCLIENT_FINISH_FORCEIN : PATCHCLIENT_FINISH_CHECKIN, 0, 0, 0, "no comment");
		return;
	}
	comment = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("force", "%d", !!forced) ("comment", "%s", NULL_TO_EMPTY(comment)));

	if(eaSize(&serverdb->db->checkins)){
		assert(eaSize(&serverdb->db->checkins) ==
					serverdb->db->checkins[eaSize(&serverdb->db->checkins) - 1]->rev + 1);
	}

	journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);

	INFO_PRINTF("Finishing checkin (%s:%s), comment: %s\n",
				client->author,
				makeIpStr(linkGetIp(link)),
				comment);

	#if 0
		// Write all pending checkins to disk.
		
		EARRAY_CONST_FOREACH_BEGIN(client->checkin_files, i, isize);
			CheckinFile* cf = client->checkin_files[i];
			
			if(cf->data)
			{
				FILE* fPending;
				char pending_fname[MAX_PATH];
				sprintf(pending_fname, "./pending_checkins/%"FORM_LL"i/%s", client->UID, cf->fname);
				makeDirectoriesForFile(pending_fname);

				fPending = fopen(pending_fname, "wb");
				assert(fPending); // TODO: make this more graceful?
				fwrite(cf->data, cf->size_to_receive, 1, fPending);
				fclose(fPending);

				assert(client->checkins_open);
				client->checkins_open--;
				
				SAFE_FREE(cf->data);
			}
			total_size += cf->size_to_receive;
		EARRAY_FOREACH_END;

		assert(client->checkins_open == 0);
	#endif

	checkin = patchAddCheckin(	serverdb->db,
								client->branch,
								client->sandbox,
								client->author,
								comment,
								getCurrentFileTime(),
								client->incr_from);
								
	// Update the client's revision, in case client tries to name this checkin.
	client->rev = checkin->rev;
	
	journalCheckin = journalAddCheckin(	journal,
										checkin->author,
										checkin->sandbox,
										checkin->branch,
										checkin->time,
										checkin->incr_from,
										checkin->comment);

	EARRAY_CONST_FOREACH_BEGIN(client->checkin_files, i, isize);
		const CheckinFile* cf = client->checkin_files[i];
		
		if(cf->size_to_receive >= 0)
		{
			filesToWriteCount++;
			totalBytesToWrite += cf->size_to_receive;
		}
		else
		{
			filesToDeleteCount++;
		}
	EARRAY_FOREACH_END;

	{
		char totalBytesBuffer[100];
		char inMemoryBytesBuffer[100];
		
		friendlyBytesBuf(totalBytesToWrite, totalBytesBuffer);
		friendlyBytesBuf(client->checkinsInMemoryBytes, inMemoryBytesBuffer);
		
		loadstart_printf(	"Writing files to hogg (%spre-compressed):"
							" %d with data (%s),"
							" %d in-memory (%s)..."
							" %d deleted,"
							,
							isCompressed ? "" : "not ",
							filesToWriteCount,
							totalBytesBuffer,
							client->checkinsInMemoryCount,
							inMemoryBytesBuffer,
							filesToDeleteCount);
	}

	printf("  0");	
	EARRAY_CONST_FOREACH_BEGIN(client->checkin_files, i, isize);
		CheckinFile* cf = client->checkin_files[i];
		U32 len;
		U32 header_size = 0, header_checksum = 0;
		U8 * header_data = NULL, * volatile_pig_header = NULL;
		float percent;

		if(!patchserverdbIsFileRevisioned(serverdb, cf->fname))
		{
			ERROR_PRINTF("Warning: Skipping unrevisioned file %s\n", cf->fname);
			continue;
		}

		percent = (100.0 * i) / isize;
		if(percent - lastPercent >= 1)
		{
			lastPercent = percent;
			printf("\b\b\b% 3d", (int)percent);
		}

		if(cf->size_to_receive >= 0)
		{
			if(cf->size_to_receive)
			{
				if(!cf->data)
				{
					char pending_fname[MAX_PATH];
					sprintf(pending_fname, "./pending_checkins/%"FORM_LL"i/%s", client->UID, cf->fname);
					cf->data = fileAlloc(pending_fname, &len);
					assertmsgf(cf->data, "Failed to open file %s.", pending_fname);
					assert(len == cf->size_to_receive);
					
					client->checkinsInMemoryCount++;
					client->checkinsInMemoryBytes += cf->size_to_receive;
				}
			}
			else
			{
				len = 0;
				cf->data = malloc(0);

				client->checkinsInMemoryCount++;
			}

			//printf("pigging file (%d/%d) '%s' ...\n", i+1, eaSize(&client->checkin_files), pending_fname);

			if(pigShouldCacheHeaderData(strrchr(cf->fname, '.')))
			{
				NewPigEntry pig_entry;

				ZeroStruct(&pig_entry);
				pig_entry.data = cf->data;
				pig_entry.size = cf->size_uncompressed;
				pig_entry.fname = cf->fname;
				
				if(isCompressed)
				{
					pig_entry.pack_size = cf->size_to_receive;
				}

				volatile_pig_header = pigGetHeaderData(&pig_entry, &header_size);
				if(volatile_pig_header && header_size > 0)
				{
					header_data = calloc( 1, (header_size + HEADER_BLOCK_SIZE - 1) & ~(HEADER_BLOCK_SIZE - 1) );
					memcpy(header_data, volatile_pig_header, header_size);
					header_checksum = patchChecksum(header_data, header_size);
				}
				else
				{
					header_data = NULL;
					header_size = 0;
					header_checksum = 0;
				}
			}
		}

		if(cf->size_to_receive < 0)
		{
			cf->modified = checkin->time;
		}

		version = patchserverdbAddFile(	serverdb,
										cf->fname,
										cf->data,
										cf->size_uncompressed,
										isCompressed ? cf->size_to_receive : 0,
										cf->checksum,
										cf->modified,
										header_size,
										header_checksum,
										header_data,
										checkin);
		cf->data = NULL;

		if(cf->size_to_receive >= 0)
		{
			assert(client->checkinsInMemoryCount);
			client->checkinsInMemoryCount--;

			assert(client->checkinsInMemoryBytes >= cf->size_to_receive);
			client->checkinsInMemoryBytes -= cf->size_to_receive;
		}
		
		journalAddFile(	journalCheckin,
						cf->fname,
						cf->checksum,
						MAX(cf->size_uncompressed, 0),
						version->modified,
						header_size,
						header_checksum,
						cf->size_uncompressed < 0,
						0);

		//version = patchFindVersion(serverdb->db, file->fname, checkin->branch, checkin->sandbox, checkin->rev, checkin->incr_from);

		eaPush(&new_versions, version);

		if (cf->size_uncompressed > 0 && (cf->checksum != version->checksum))
		{
			// A file failed to checksum, so the whole checkin fails.
			
			sprintf(buf, "checksum failed on %s", cf->fname);
			
			EARRAY_CONST_FOREACH_BEGIN(new_versions, j, jsize);
				FileVersion*		new_version = new_versions[j];
				DirEntry *			dir;

				patchserverdbRemoveNewVersion(serverdb, new_version);

				dir = patchFindPath(serverdb->db, cf->fname, 0);
				eaFindAndRemove(&dir->versions, new_version);
			EARRAY_FOREACH_END;
			
			eaFindAndRemove(&serverdb->db->checkins, checkin);

			eaDestroyEx(&new_versions, fileVersionDestroy);
			checkinFree(&checkin);
			journalDestroy(&journal);

			err_msg = buf;
			goto failed;
		}
	EARRAY_FOREACH_END;

	// NOTE: Pruning is now done only as a recurring task <NPK 2008-12-15>
	//EARRAY_CONST_FOREACH_BEGIN(new_versions, i, isize);
	//	patchserverdbPruneDirEntry(serverdb, new_versions[i]->parent, journal, 0);
	//EARRAY_FOREACH_END;

	printf("\b\b\b");

	success = 1;
	journalFlushAndDestroy(&journal, serverdb->name);

	MAX1(serverdb->latest_rev, checkin->rev);
	MAX1(serverdb->latest_branch, client->branch);

	eaDestroy(&new_versions);
failed:
	freePendingCheckins(client);

	{
		int		cmd = forced ? PATCHSERVER_FINISH_FORCEIN : PATCHSERVER_FINISH_CHECKIN;
		Packet*	pak = pktCreate(link, cmd);
		
		pktSendBits(pak, 32, success);
		pktSendBits(pak, 32, checkin?checkin->time:0);
		pktSendString(pak, err_msg);
		pktSendBits(pak, 32, checkin?checkin->rev:0);
		pktSend(&pak);
		LOG_PATCH_RESPONSE(("success", "%d", success) ("time", "%lu", checkin?checkin->time:0) ("rev", "%d", checkin?checkin->rev:0));
	}
	
	loadend_printf("done.");

	printfColor(COLOR_BRIGHT | (success ? COLOR_GREEN : COLOR_RED),
				"Checkin %s!\n",
				success ? "successful" : "failed");
}

void handleNameView(Packet *pak_in, NetLink * link, PatchClientLink * client)
{
	Packet*			pak;
	char*			view_name;
	char*			comment = NULL;
	U32				days = U32_MAX;
	U32				now;
	U32				expires;
	int				success = 0;
	char			err_msg[1024];

	if(patchupdateIsChildServer() || !validateAndLoadView(client) || !client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_NAME_VIEW);
		return;
	}

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_NAME_VIEW, 0, 0, 0, "no view name");
		return;
	}
	view_name = pktMallocString(pak_in);
	if (!*view_name)
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_NAME_VIEW, 0, 0, 0, "empty view names are not allowed");
		return;
	}
	if(!pktCheckRemaining(pak_in, 4))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_NAME_VIEW, 0, 0, 0, "days missing");
		return;
	}
	days = pktGetBits(pak_in, 32);
	
	
	if(client->patcher_version >= PATCHCLIENT_VERSION_NAMED_VIEW_COMMENTS){
		comment = pktMallocString(pak_in);
	}

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("viewname", "%s", NULL_TO_EMPTY(view_name)) ("comment", "%s", NULL_TO_EMPTY(comment)));

	err_msg[0] = '\0';
	now = getCurrentFileTime();
	expires = CLAMP(days == U32_MAX ? 0 : now + SECONDS_PER_DAY*(U64)days, 0, U32_MAX);
	if(expires == 0 && g_patchserver_config.prune_config && g_patchserver_config.prune_config->view_expires)
	{
		// If the expiry time isn't sent from the client and we have a prune config, set the default expiry.
		ViewExpires *view_expires = (client->project && client->project->serverdb->view_expires_override) ? client->project->serverdb->view_expires_override : g_patchserver_config.prune_config->view_expires;
		expires = getCurrentFileTime() + (SECONDS_PER_DAY * view_expires->internal);
	}
	success = patchserverdbAddViewName(	client->project->serverdb,
										view_name,
										client->branch,
										client->sandbox,
										client->rev,
										comment,
										expires,
										err_msg,
										sizeof(err_msg));

	if(success && eaSize(&client->project->serverdb->db->namedviews) > 1)
	{
		// Mark the last view as dirty
		NamedView **namedviews = client->project->serverdb->db->namedviews;
		namedviews[eaSize(&namedviews)-2]->dirty = true;
		journalAddViewDirtyFlush(eaSize(&client->project->serverdb->db->checkins) - 1,
								 client->project->serverdb->name,
								 view_name);

	}

	SAFE_FREE(view_name);
	SAFE_FREE(comment);

	pak = pktCreate(link, PATCHSERVER_VIEW_NAMED);
	pktSendBits(pak, 32, success);
	pktSendString(pak, err_msg);
	LOG_PATCH_RESPONSE(("success", "%d", success) ("err_msg", "%s", NULL_TO_EMPTY(err_msg)));
	pktSend(&pak);
}

void handleSetExpiration(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet *pak_out;
	PatchProject *proj;
	char *view_name, *proj_name;
	U32 days;
	bool success;
	char msg[1024];

	if(patchupdateIsChildServer())
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_SET_EXPIRATION);
		return;
	}

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_EXPIRATION, 0, 0, 0, "no project");
		return;
	}
	proj_name = pktGetStringTemp(pak_in);
	proj = patchserverFindProjectChecked(proj_name, linkGetSAddr(client->link));

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_EXPIRATION, 0, 0, 0, "no view name");
		return;
	}
	view_name = pktGetStringTemp(pak_in);

	if(!pktCheckRemaining(pak_in, 4))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_EXPIRATION, 0, 0, 0, "no days");
		return;
	}
	days = pktGetBits(pak_in, 32);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("project", "%s", NULL_TO_EMPTY(proj_name))
		("view", "%s", NULL_TO_EMPTY(view_name)) ("days", "%lu", days));

	if(!proj || !proj->allow_checkins)
	{
		sprintf(msg, "Invalid project %s.", proj_name);
		success = false;
	}
	else
	{
		U32 now = getCurrentFileTime();
		U32 expires = CLAMP(days == U32_MAX ? 0 : now + SECONDS_PER_DAY*(U64)days, 0, U32_MAX);
		success = patchserverdbSetExpiration(proj->serverdb, view_name, expires, SAFESTR(msg));
	}

	pak_out = pktCreate(link, PATCHSERVER_EXPIRATION_SET);
	pktSendBool(pak_out, success);
	pktSendString(pak_out, msg);
	LOG_PATCH_RESPONSE(("success", "%d", success) ("msg", "%s", NULL_TO_EMPTY(msg)));
	pktSend(&pak_out);
}

void handleSetFileExpiration(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	Packet *pak_out;
	char *path;
	U32 days;
	bool success;
	U32 now;
	U32 expires;

	// Validate.
	if(	patchupdateIsChildServer() ||
		!validateAndLoadView(client) ||
		!client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_SET_FILE_EXPIRATION);
		return;
	}

	// Parse packet.
#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_FILE_EXPIRATION, 0, 0, 0, reason); return;}
#define CHECK_BYTES(x) {if(!pktCheckRemaining(pak_in, (x))) QUIT_HANDLE("packet did not have enough remaining bytes");}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet not null terminated");}
	CHECK_STR;
	path = pktGetStringTemp(pak_in);
	CHECK_BYTES(4);
	days = pktGetBits(pak_in, 32);
#undef QUIT_HANDLE
#undef CHECK_BYTES
#undef CHECK_STR
	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("path", "%s", NULL_TO_EMPTY(path))
		("days", "%lu", days));

	// Set expiration.
	now = getCurrentFileTime();
	expires = CLAMP(days == U32_MAX ? 0 : now + SECONDS_PER_DAY*(U64)days, 0, U32_MAX);
	success = patchserverdbSetFileExpiration(client->project->serverdb, path, expires);

	// Send response.
	pak_out = pktCreate(link, PATCHSERVER_FILE_EXPIRATION_SET);
	pktSendBool(pak_out, success);
	LOG_PATCH_RESPONSE(("success", "%d", success));
	pktSend(&pak_out);
}

typedef struct UndoCheckinData
{
	DirEntry *dir;
	FileVersion *ver;
} UndoCheckinData;

void handleReqUndoCheckin(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *comment;
	Packet*	pak;
	Checkin *checkin, *new_checkin = NULL;
	char *error_message = NULL;
	bool success = true;
	int i;
	UndoCheckinData **new_files = NULL;
	PatchJournal* journal = NULL;
	PatchServerDb *serverdb = NULL;

	if(	patchupdateIsChildServer() ||
		!validateAndLoadView(client) ||
		!client->project->allow_checkins)
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_UNDO_CHECKIN);
		return;
	}

	serverdb = client->project->serverdb;

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_UNDO_CHECKIN, 0, 0, 0, "no comment");
		return;
	}
	comment = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("comment", "%s", NULL_TO_EMPTY(comment)));

	checkin = serverdb->db->checkins[client->rev];
	for(i=0; i<eaSize(&checkin->versions); i++)
	{
		UndoCheckinData *ucd;
		FileVersion *ver = checkin->versions[i], *ver2;
		DirEntry *dir = ver->parent;
		Checkout *checkout = patchFindCheckoutInDir(dir, checkin->branch, checkin->sandbox);
		if(checkout)
		{
			// If any of these files are locked, bail with an error message
			success = false;
			estrPrintf(&error_message, "File %s locked by %s", dir->path, checkout->author);
			break;
		}
		// Check all future versions, if any are in the same branch/sandbox just skip this file entirely
		ver2 = patchFindVersionInDir(dir, checkin->branch, checkin->sandbox, INT_MAX, PATCHREVISION_NONE);
		if(ver2->checksum != ver->checksum)
		{
			success = false;
			estrPrintf(&error_message, "There is a later version of %s, version %d, in revision %d", dir->path, ver2->version, ver2->rev);
			break;
		}
		if(ver->version == 0)
			// First version, we want to commit a delete
			ver2 = NULL;
		else
			ver2 = patchFindVersionInDir(dir, checkin->branch, checkin->sandbox, checkin->rev-1, PATCHREVISION_NONE);
		
		ucd = calloc(1, sizeof(UndoCheckinData));
		ucd->dir = dir;
		if(ver2 && ~ver2->flags & FILEVERSION_DELETED)
			ucd->ver = ver2;
		eaPush(&new_files, ucd);
	}
	if(success && eaSize(&new_files))
	{
		JournalCheckin* journalCheckin;
		FileVersion *new_ver;

		journal = journalCreate(eaSize(&serverdb->db->checkins) - 1);

		new_checkin = patchAddCheckin(serverdb->db,
									  checkin->branch,
									  checkin->sandbox,
									  client->author,
									  comment,
									  getCurrentFileTime(),
									  checkin->incr_from);
		client->rev = new_checkin->rev;
		journalCheckin = journalAddCheckin(	journal,
											new_checkin->author,
											new_checkin->sandbox,
											new_checkin->branch,
											new_checkin->time,
											new_checkin->incr_from,
											new_checkin->comment);

		for(i=0; i<eaSize(&new_files); i++)
		{
			DirEntry *old_dir = new_files[i]->dir;
			FileVersion *old_ver = new_files[i]->ver;
			if(old_ver)
			{
				char *data;
				FileNameAndOldName name = {0};
				U32 len, len_compressed;
				if(!patchserverdbGetDataForVersion(serverdb, old_ver, &name, &data, &len, &len_compressed))
					assertmsgf(0, "Can't find old file data for undo: %s", name.name);

				new_ver = patchserverdbAddFile(	serverdb,
												old_dir->path,
												data,
												len,
												len_compressed,
												old_ver->checksum,
												old_ver->modified,
												old_ver->header_size,
												old_ver->header_checksum,
												NULL,
												new_checkin);
			}
			else
			{
				new_ver = patchserverdbAddFile(	serverdb,
												old_dir->path,
												NULL,
												-1,
												0,
												0,
												new_checkin->time,
												0,
												0,
												NULL,
												new_checkin);
			}

			journalAddFile(	journalCheckin,
							new_ver->parent->path,
							new_ver->checksum,
							MAX(new_ver->size, 0),
							new_ver->modified,
							new_ver->header_size,
							new_ver->header_checksum,
							new_ver->flags & FILEVERSION_DELETED,
							0);
		}
	}
	else
	{
		// No files to be modified.
		success = false;
	}

	if(success)
	{
		journalFlushAndDestroy(&journal, serverdb->name);
		MAX1(serverdb->latest_rev, new_checkin->rev);
		MAX1(serverdb->latest_branch, new_checkin->branch);
	}
	else
		journalDestroy(&journal);

	pak = pktCreate(link, PATCHSERVER_FINISH_CHECKIN);
	pktSendU32(pak, success);
	pktSendU32(pak, new_checkin?new_checkin->time:0);
	pktSendString(pak, error_message);
	pktSendU32(pak, new_checkin?new_checkin->rev:0);
	LOG_PATCH_RESPONSE(("success", "%d", success) ("err_msg", "%s", NULL_TO_EMPTY(error_message)) ("time", "%lu", new_checkin?new_checkin->time:0)
		("rev", "%d", new_checkin?new_checkin->rev:0));
	pktSend(&pak);

	estrDestroy(&error_message);
	eaDestroyEx(&new_files, NULL);
}

void handleIsCompletelySynced(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *path;
	bool synced;
	Packet*	pak;
	bool exists = false;

	// Make sure we're in a valid state.
	if(	patchupdateIsChildServer() ||
		!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_COMPLETELY_SYNCED);
		return;
	}

	// Parse packet.
	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_COMPLETELY_SYNCED, 0, 0, 0, "no path");
		return;
	}
	path = pktGetStringTemp(pak_in);
	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("path", "%s", NULL_TO_EMPTY(path)));

	// Check if the path exists.
	if (client->project && client->project->serverdb && client->project->serverdb->db)
		exists = !!patchFindPath(client->project->serverdb->db, path, false);

	// Check if the path is completely updated to child servers.
	synced = patchTrackingIsCompletelyUpdatedPath(client->project, path,
		g_patchserver_config.sync_config ? g_patchserver_config.sync_config->include : NULL,
		g_patchserver_config.sync_config ? g_patchserver_config.sync_config->exclude : NULL);

	// Send response.
	pak = pktCreate(link, PATCHSERVER_IS_COMPLETELY_SYNCED_RESPONSE);
	pktSendBool(pak, synced);
	pktSendBool(pak, exists);
	LOG_PATCH_RESPONSE(("synced", "%d", (int)synced)
		("exists", "%d", (int)exists));
	pktSend(&pak);
}

static void dontTalkToMe(PatchClientLink *client, char *msg)
{
	Packet *pak;
	if(!client || !client->link)
		return;

	pak = pktCreate(client->link, PATCHSERVER_DONT_RECONNECT);
	pktSendString(pak, msg);
	pktSend(&pak);
	LOG_PATCH_RESPONSE(("msg", "%s", NULL_TO_EMPTY(msg)));

	flushLink(client->link);
	
	client->hasProtocolError = 1;
}

void handleSetAuthor(Packet *pak_in, NetLink * link, PatchClientLink * client)
{
	Packet			*pak;

	if(patchupdateIsChildServer())
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_SET_AUTHOR);
		dontTalkToMe(client, "Cannot set author on a mirror server, checkins not allowed");
		return;
	}

	if(!pktCheckNullTerm(pak_in))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_AUTHOR, 0, 0, 0, "no author");
		return;
	}
	pktGetString(pak_in, SAFESTR(client->author));
	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("author", "%s", NULL_TO_EMPTY(client->author)));
	removeLeadingAndFollowingSpaces(client->author);
	
	if(!client->author[0]){
		printfColor(COLOR_BRIGHT|COLOR_RED,
					"Blank author name from %s!\n",
					client->ipstr);
					
		dontTalkToMe(client, "Your author name is blank");
	}else{
		pak = pktCreate(link, PATCHSERVER_AUTHOR_RESPONSE);
		pktSendBits(pak, 32, 1);
		pktSend(&pak);
		LOG_PATCH_RESPONSE(("success", "1"));
	}
}

void handleShutdown(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	LOG_PATCH_COMMAND();

	// Check IP is local host
	if (linkGetIp(link) != getHostLocalIp() && linkGetIp(link) != LOCALHOST_ADDR) {
		log_printf(LOG_PATCHSERVER_SHUTDOWN,"Denied shutdown from %s", client->ipstr);

		printfColor(COLOR_BRIGHT|COLOR_RED,
			"Shutdown request denied from %s!\n",
			client->ipstr);

		dontTalkToMe(client, "Not allowed to shutdown from that IP");
	} else {
		log_printf(LOG_PATCHSERVER_SHUTDOWN,"Accepted shutdown from %s", client->ipstr);
		printfColor(COLOR_BRIGHT|COLOR_RED,
			"Shutting down... (requested from %s)\n",
			client->ipstr);
		s_server_shutting_down = true;
	}
}

void handleServerMerge(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	LOG_PATCH_COMMAND();

	// Check IP is local host
	if (linkGetIp(link) != getHostLocalIp() && linkGetIp(link) != LOCALHOST_ADDR) {
		log_printf(LOG_PATCHSERVER_GENERAL,"Denied merge from %s", client->ipstr);

		printfColor(COLOR_BRIGHT|COLOR_RED,
			"Merge request denied from %s!\n",
			client->ipstr);

		dontTalkToMe(client, "Not allowed to merge from that IP");
		return;
	}

	// Report request.
	log_printf(LOG_PATCHSERVER_GENERAL,"Merge request from %s", client->ipstr);
	printfColor(COLOR_BRIGHT|COLOR_RED,
		"Running the merger, if there's something to merge... (requested from %s)\n",
		client->ipstr);

	// Run the merger.
	execMergeProcess(true);
}

void handleNotifyView(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *proj_name, *view_name;
	U32 ip;
	PatchProject *proj;
	NamedView *view;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_NOTIFYVIEW, 0, 0, 0, reason); return;}
#define CHECK_STR {if(!pktCheckNullTerm(pak_in)) QUIT_HANDLE("packet was not null terminated");}

	CHECK_STR;
	proj_name = pktGetStringTemp(pak_in);
	CHECK_STR;
	view_name = pktGetStringTemp(pak_in);
	ip = pktGetU32(pak_in);

#undef QUIT_HANDLE
#undef CHECK_STR

	LOG_PATCH_COMMAND(("project", "%s", NULL_TO_EMPTY(proj_name)) ("viewname", "%s", NULL_TO_EMPTY(view_name)));

	proj = patchserverFindProjectChecked(proj_name, ip);
	if(!proj)
		return;
	view = patchFindNamedView(proj->serverdb->db, view_name);
	if(!view)
		return;

	//log_printf(LOG_PATCHSERVER_NOTIFYVIEW, "%s sent view notification for %s from %d.%d.%d.%d", client->ipstr, view_name, ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255);
	//printfColor(COLOR_BRIGHT|COLOR_GREEN, "%s sent view notification for %s from %d.%d.%d.%d\n", client->ipstr, view_name, ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255);

	patchupdateNotifyView(proj_name, view_name, ip);
	
	if(view->viewed_external == 0 && g_patchserver_config.internal
		&& !checkAllowDeny(g_patchserver_config.internal->ips, g_patchserver_config.internal->deny_ips, ip))
	{
		view->viewed_external = 1;
		journalAddViewedExternalFlush(eaSize(&proj->serverdb->db->checkins) - 1, proj->serverdb->name, view_name);
	}

	if(g_patchserver_config.prune_config && g_patchserver_config.prune_config->view_expires)
	{
		U32 new_expires = getCurrentFileTime();
		ViewExpires *view_expires = proj->serverdb->view_expires_override ? proj->serverdb->view_expires_override : g_patchserver_config.prune_config->view_expires;
		if(view->viewed_external)
			new_expires += (SECONDS_PER_DAY * view_expires->public);
		else
			new_expires += (SECONDS_PER_DAY * view_expires->internal);
		if(new_expires > view->expires)
		{
			view->expires = new_expires;
			view->dirty = true;
			journalAddExpiresFlush(eaSize(&proj->serverdb->db->checkins) - 1, proj->serverdb->name, view_name, new_expires);
			//journalAddViewDirtyFlush(eaSize(&proj->serverdb->db->checkins) - 1, proj->serverdb->name, view_name);
		}
	}

}


void handleReqCheckinsBetweenTimes(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	U32			timeStart = pktGetBits(pak_in, 32);
	U32			timeEnd = pktGetBits(pak_in, 32);
	CheckinList	cl = {0};
	Packet*		pak;

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("start", "%lu", timeStart) ("end", "%lu", timeEnd));
	
	StructInit(parse_CheckinList, &cl);

	if(client->project)
	{
		EARRAY_FOREACH_REVERSE_BEGIN(client->project->serverdb->db->checkins, i);
			Checkin* c = client->project->serverdb->db->checkins[i];
			
			if(c->time > timeEnd)
			{
				continue;
			}
			else if(c->time < timeStart)
			{
				break;
			}
			else if(c->branch == client->branch)
			{
				CheckinInfo* ci = StructAlloc(parse_CheckinInfo);
				
				estrCopy2(&ci->checkinComment, NULL_TO_EMPTY(c->comment));
				ci->iCheckinTimeSS2000 = patchFileTimeToSS2000(c->time);
				ci->iRevNum = c->rev;
				estrCopy2(&ci->userName, NULL_TO_EMPTY(c->author));
				
				eaPush(&cl.checkins, ci);
			}
		EARRAY_FOREACH_END;
	}
	
	pak = pktCreate(link, PATCHSERVER_CHECKINS_BETWEEN_TIMES);
	
	ParserSendStructSafe(parse_CheckinList,
				pak,
				&cl);
	
	pktSend(&pak);

	LOG_PATCH_RESPONSE(("size", "%d", eaSize(&cl.checkins)));

	StructDeInit(parse_CheckinList, &cl);
}


void handleReqCheckinInfo(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	U32 rev = pktGetU32(pak_in);
	Packet *pak = pktCreate(link, PATCHSERVER_CHECKIN_INFO);

	if(!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_REQ_CHECKIN_INFO);
		return;
	}
	devassert(client->project);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("revision", "%lu", rev));

	FOR_EACH_IN_EARRAY(client->project->serverdb->db->checkins, Checkin, c)
		if(c->rev == rev)
		{
			pktSendBits(pak, 8, 1);
			pktSendU32(pak, c->rev);
			pktSendU32(pak, c->branch);
			pktSendU32(pak, c->time);
			pktSendString(pak, NULL_TO_EMPTY(c->sandbox));
			pktSendU32(pak, c->incr_from);
			pktSendString(pak, NULL_TO_EMPTY(c->author));
			pktSendString(pak, NULL_TO_EMPTY(c->comment));
			pktSendU32(pak, eaSize(&c->versions));
			FOR_EACH_IN_EARRAY(c->versions, FileVersion, ver)
				pktSendString(pak, ver->parent->path);
			FOR_EACH_END
			pktSend(&pak);

			LOG_PATCH_RESPONSE(("success", "1") ("rev", "%d", c->rev) ("branch", "%d", c->branch) ("time", "%lu", c->time)
				("sandbox", "%s", NULL_TO_EMPTY(c->sandbox)) ("incr_from", "%d", c->incr_from) ("author", "%s", NULL_TO_EMPTY(c->author))
				("comment", "%s", NULL_TO_EMPTY(c->comment)) ("versions", "%d", eaSize(&c->versions)));

			return;
		}
	FOR_EACH_END

	// Checkin not found
	pktSendBits(pak, 8, 0);
	pktSend(&pak);
	LOG_PATCH_RESPONSE(("success", "0"));
}

void handleCheckDeleted(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *dbname;
	int i, j, count;
	Packet *pak_out;
	DirEntry *dir;

	if(!validateAndLoadView(client))
	{
		BADPACKET_PREREQ_FAILED(PATCHCLIENT_CHECK_DELETED);
		return;
	}

	if(!pktCheckRemaining(pak_in, 4))
	{
		badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_CHECK_DELETED, 0, 0, 0, "no count");
		return;
	}
	count = pktGetBits(pak_in, 32);

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT ("count", "%d", count));

	pak_out = pktCreate(link, PATCHSERVER_CHECK_DELETED);
	pktSendBits(pak_out, 8, 1); // success
	for(i = 0; i < count; i++)
	{
		U32 modified, size, checksum;
		if(!pktCheckNullTerm(pak_in))
		{
			badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_CHECK_DELETED, 0, 0, 0, "no dbname");
			pktFree(&pak_out);
			return;
		}
		dbname = pktGetStringTemp(pak_in);
		dir = patchFindPath(SAFE_MEMBER(client->project, serverdb->db), dbname, 0);
		if(!dir || !eaSize(&dir->versions))
		{
			char err_msg[MAX_PATH];
			pktFree(&pak_out);
			pak_out = pktCreate(link, PATCHSERVER_CHECK_DELETED);
			pktSendBits(pak_out, 8, 0); // success
			sprintf(err_msg, "Could not find %s", dbname);
			pktSendString(pak_out, err_msg);
			pktSend(&pak_out);
			LOG_BAD_PACKET();
			return;
		}

		if(!pktCheckRemaining(pak_in, 12))
		{
			badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_CHECK_DELETED, 0, 0, 0, "no modified");
			pktFree(&pak_out);
			return;
		}
		modified = pktGetBits(pak_in, 32);
		size = pktGetBits(pak_in, 32);
		checksum = pktGetBits(pak_in, 32);

		for(j = 0; j < eaSize(&dir->versions); j++)
		{
			FileVersion *ver = dir->versions[j];
			if( (ver->modified == modified || ABS_UNS_DIFF(ver->modified, modified) == 3600) &&
				ver->size == size && ver->checksum == checksum )
			{
				pktSendBits(pak_out, 8, 1);
				break;
			}
		}
		if(j >= eaSize(&dir->versions))
			pktSendBits(pak_out, 8, 0);
	}
	pktSend(&pak_out);
	LOG_PATCH_RESPONSE();
}

void handleReqNotification(Packet * pak_in, NetLink * link, PatchClientLink * client, bool notify_me)
{
	LOG_PATCH_COMMAND(("notify", "%d", !!notify_me));

	if(notify_me)
	{
		client->once_requested_notify = true;
	}
	
	if(notify_me && client->update_pending)
	{
		Packet * pak = pktCreate(link, PATCHSERVER_UPDATE);

		pktSend(&pak);
		LOG_PATCH_RESPONSE(("update_now", "1"));
		client->update_pending = false;
		notify_me = false;
	}

	client->notify_me = notify_me;

	printf(	"Client %s update notification %s\n",
			makeIpStr(linkGetIp(link)),
			notify_me ? "enabled" : "disabled");
}

int notifyClient(NetLink * link, S32 index, PatchClientLink * client, void * userdata)
{
	if(client->notify_me)
	{
		Packet * pak = pktCreate(link, PATCHSERVER_UPDATE);
		LOG_PATCH_RESPONSE(("update_now", "1"));
		pktSend(&pak);
		client->update_pending = false;
		client->notify_me = false;
		
		printf(	"Sent update notification to %s.\n",
				makeIpStr(linkGetIp(link)));
	}
	else
	{
		// The server will tell client immediately that there's an update if it requests notify.
		
		if(client->once_requested_notify)
		{
			printf(	"Queueing update notification for %s.\n",
					makeIpStr(linkGetIp(link)));
		}
		
		client->update_pending = true;
	}
	return 1;
}

static void loadHeader(FileVersion *ver, PatchServerDb *serverdb)
{
	if(ver->header_data == NULL && ver->header_size > 0)
	{
		patchserverdbLoadHeader(ver, serverdb);
	}
}

void handleReqHeaderInfo(Packet * pak_in, NetLink * link, PatchClientLink * client)
{
	char * fname;
	U32 id;
	FileVersion * ver;
	PatchServerDb *serverdb;
	Packet * pak;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_HEADERINFO, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet was not null terminated");
	fname = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND_VERBOSE2(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)));

	if(!validateAndLoadView(client))
		return;

	ver = patchprojectFindVersion(client->project, fname, client->branch, client->sandbox, client->rev, client->incr_from, client->prefix, &serverdb);
	if(!ver)
		QUIT_HANDLE("no version was found for the requested file");
	loadHeader(ver, serverdb);

	if(!pktCheckRemaining(pak_in, 4))
		QUIT_HANDLE("not enough bytes were reamining in packet");
	id = pktGetBits(pak_in,32);
#undef QUIT_HANDLE

	pak = pktCreate(link, PATCHSERVER_HEADERINFO);
	pktSendBits(pak, 32, id);
	pktSendBits(pak, 32, ver->header_size);
	pktSendBits(pak, 32, ver->header_checksum);
	pktSend(&pak);
	LOG_PATCH_RESPONSE(("id", "%lu", id) ("header_size", "%lu", ver->header_size) ("header_checksum", "%lu", ver->header_checksum));
}

void handleReqHeaderBlocks(Packet * pak_in, NetLink * link, PatchClientLink * client)
{
	char * fname;
	U32 id;
	FileVersion * ver;
	PatchServerDb *serverdb;
	Packet * pak = NULL;
	int num_block_reqs;
	int i;

#define QUIT_HANDLE(reason) {pktFree(&pak); badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_REQ_HEADER_BLOCKS, 0, 0, 0, reason); return;}
#define QUIT_HANDLE_RANGE(block_start, block_count, buffer_size)\
	{\
		char temp_quit_handle_range[128];\
		sprintf(temp_quit_handle_range, "out of range: %i %i %i", (block_start), (block_count), (buffer_size));\
		QUIT_HANDLE(temp_quit_handle_range);\
	}
#define CHECK_BYTES(x)\
	{\
		if(!pktCheckRemaining(pak_in, (x)))\
			QUIT_HANDLE("not enough bytes left in packet");\
	}

	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet not null terminated");
	fname = pktGetStringTemp(pak_in);

	LOG_PATCH_COMMAND_VERBOSE3(PATCH_PROJECT_CONTEXT ("filename", "%s", NULL_TO_EMPTY(fname)));

	if(!validateAndLoadView(client))
		return;

	ver = patchprojectFindVersion(client->project, fname, client->branch, client->sandbox, client->rev, client->incr_from, client->prefix, &serverdb);
	if(!ver)
		QUIT_HANDLE("");
	loadHeader(ver, serverdb);

	CHECK_BYTES(4);
	id				= pktGetBits(pak_in,32);
	CHECK_BYTES(4);
	num_block_reqs	= pktGetBits(pak_in,32);
	if(num_block_reqs > (int)MAX_REQ_BLOCKS)
		QUIT_HANDLE("num_block_reqs is too high");

	pak = pktCreate(link, PATCHSERVER_HEADER_BLOCKS);
	pktSendBits(pak, 32, id);
	pktSendBits(pak, 32, num_block_reqs);
	LOG_PATCH_RESPONSE(("id", "%lu", id) ("num_block_reqs", "%d", num_block_reqs));

	for(i = 0; i < num_block_reqs; i++)
	{
		int start, count;

		CHECK_BYTES(4);
		start = pktGetBits(pak_in, 32);
		CHECK_BYTES(4);
		count = pktGetBits(pak_in, 32);
		if( start < 0 ||
			count < 0 ||
			(start + count) <= 0 ||
			(U32)(start + count) > ((ver->header_size + HEADER_BLOCK_SIZE - 1) / HEADER_BLOCK_SIZE) )
			QUIT_HANDLE_RANGE(start, count, ver->header_size);

		pktSendBits(pak, 32, start);
		pktSendBits(pak, 32, count);
		pktSendBytes(pak, HEADER_BLOCK_SIZE * count, ver->header_data + start * HEADER_BLOCK_SIZE);
	}

	pktSend(&pak);

#undef QUIT_HANDLE_RANGE
#undef QUIT_HANDLE
#undef CHECK_BYTES
}

void handleReqProjectList(Packet * pak_in, NetLink * link, PatchClientLink * client)
{
	int i, count = 0;
	U32 ip = linkGetSAddr(client->link);
	Packet *pak_out = pktCreate(link, PATCHSERVER_PROJECT_LIST);

	LOG_PATCH_COMMAND();

	for(i = 0; i < eaSize(&g_patchserver_config.projects); i++)
	{
		PatchProject *project = g_patchserver_config.projects[i];
		if(project->serverdb && checkAllowDeny(project->allow_ips, project->deny_ips, ip))
			count++;
	}
	pktSendBits(pak_out, 32, count);
	for(i = 0; i < eaSize(&g_patchserver_config.projects); i++)
	{
		PatchProject *project = g_patchserver_config.projects[i];
		if(project->serverdb && checkAllowDeny(project->allow_ips, project->deny_ips, ip))
		{
			pktSendString(pak_out, project->name);
			pktSendBits(pak_out, 32, project->serverdb->latest_branch);
			pktSendBits(pak_out, 1, (patchupdateIsChildServer() || !project->allow_checkins));
		}
	}
	pktSend(&pak_out);
	LOG_PATCH_RESPONSE(("total", "%d", eaSize(&g_patchserver_config.projects)) ("count", "%d", count));
}

void handleReqNameList(Packet * pak_in, NetLink * link, PatchClientLink * client)
{
	int i;
	NamedView ** named_views;
	int count = 0;
	int * views = NULL;
	Packet * pak;

	LOG_PATCH_COMMAND(PATCH_PROJECT_CONTEXT);

	if(!validateAndLoadView(client))
		return;

	devassert(client->project);
	named_views = client->project->serverdb->db->namedviews;

	for(i = 0; i < eaSize(&named_views); i++)
	{
		if(client->branch >= named_views[i]->branch)
		{
			count++;
			eaiPush(&views, i);
		}
	}

	pak = pktCreate(link, PATCHSERVER_NAME_LIST);
	pktSendBits(pak, 32, count);
	for(i = 0; i < count; i++)
	{
		pktSendString(pak, named_views[views[i]]->name);
		pktSendBits(pak, 32, named_views[views[i]]->branch);
		pktSendString(pak, NULL_TO_EMPTY(named_views[views[i]]->sandbox));
		if(client->patcher_version < PATCHCLIENT_VERSION_REVISIONNUMBERS)
			pktSendBits(pak, 32, client->project->serverdb->db->checkins[named_views[views[i]]->rev]->time);
		else
			pktSendBits(pak, 32, named_views[views[i]]->rev);
		pktSendString(pak, named_views[views[i]]->comment);
		pktSendU32(pak, named_views[views[i]]->expires);
	}
	pktSend(&pak);
	LOG_PATCH_RESPONSE(("count", "%d", count));

	eaiDestroy(&views);
}

static int patchserverGetAutoUpdateRevision(const char *token, U32 ip)
{
	AutoupConfigWeightedRevision **revisions = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (g_patchserver_config.dynamic_autoup_config)
		revisions = findAutoupInfoMatch(g_patchserver_config.dynamic_autoup_config->autoup_rules, token, ip);
	if(eaSize(&revisions))
	{
		float total_weight = 0;
		float cumulative_prob = 0;

		// produces [0.0, 1.0) and is fairly sticky even for machines getting different IPs from same DHCP server over small time period
		U32 host_ip = ntohl(ip);
		U32 ip_masked = host_ip & 0x00FFFFFF;
		U32 ip_hash = MurmurHash2Int(ip_masked);
		float ip_value = (float)ip_hash / 0xFFFFFFFF;

		// Calculate total weight.
		EARRAY_CONST_FOREACH_BEGIN(revisions, i, n);
		{
			float weight = revisions[i]->weight;
			if(weight > 0 && weight == weight)
				total_weight += weight;
		}
		EARRAY_FOREACH_END;

		// Decide which rev should be chosen.
		devassert(total_weight > 0 || eaSize(&revisions) == 1);
		EARRAY_CONST_FOREACH_BEGIN(revisions, i, n);
		{
			float weight = revisions[i]->weight;
			float prob;
			if(weight <= 0 || !(weight == weight))
				continue;
			if(i == n - 1)
			{
				PERFINFO_AUTO_STOP_FUNC();

				return revisions[i]->rev;
			}
			prob = weight / total_weight;
			cumulative_prob += prob;
			if(ip_value < cumulative_prob)
			{
				PERFINFO_AUTO_STOP_FUNC();

				return revisions[i]->rev;
			}
		}
		EARRAY_FOREACH_END;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return INT_MAX;
}

FileVersion* patchserverGetAutoUpdateFile(const char *token, U32 ip)
{
	AutoUpdateFile *autoupdate;

	PERFINFO_AUTO_START_FUNC();

	if(token && token[0] && g_patchserver_config.autoupdatedb && g_patchserver_config.autoupdatedb->db &&
		stashFindPointer(g_patchserver_config.autoupdate_stash, token, &autoupdate) &&
		checkAllowDeny(autoupdate->ips, autoupdate->deny_ips, ip))
	{
		int revision = patchserverGetAutoUpdateRevision(token, ip);
		FileVersion *ver = patchFindVersion(g_patchserver_config.autoupdatedb->db, token, INT_MAX, NULL, revision, PATCHREVISION_NONE);
		if(ver && !(ver->flags & FILEVERSION_DELETED))
		{
			PERFINFO_AUTO_STOP_FUNC();

			return ver;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

int handleConnectPacket(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	ServerConfig *config = &g_patchserver_config;
	FileVersion *update_file = NULL;

	int i;
	U32 ip = linkGetSAddr(client->link), listen_ip;
	char *redirect_server = NULL; // no redirect
	int redirect_port = DEFAULT_PATCHSERVER_PORT;

	client->patcher_version_fake = pktCheckRemaining(pak_in, 4) ? pktGetU32(pak_in) : -1;

	if (s_server_shutting_down) {
		// Perhaps shouldn't respond, instead, so that the client just hangs
		//   until the restart is done?
		LOG_PATCH_COMMAND(("patcher_version_fake", "%d", client->patcher_version_fake));
		LOG_PATCH_RESPONSE(("status", "dontTalkToMe") ("details", "server_shutting_down"));
		dontTalkToMe(client, "Server is shutting down");
		return 0;
	}

	// Check the legacy version.
	// The old versionchecking 
	if (client->patcher_version_fake < PATCHCLIENT_VERSION_SUPPORTED)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		estrConcatf(&estr, "This PatchServer requires a client version of at least %d, your client ",
			PATCHCLIENT_VERSION_SUPPORTED);
		if(client->patcher_version_fake < 0)
			estrConcatf(&estr, "didn't report a version, which is typical of versions before 2.");
		else
			estrConcatf(&estr, "reported version %d.", client->patcher_version_fake);
		LOG_PATCH_COMMAND(("patcher_version_fake", "%d", client->patcher_version));
		LOG_PATCH_RESPONSE(("status", "dontTalkToMe") ("details", "patcher_version_fake too old"));
		dontTalkToMe(client, estr);
		estrDestroy(&estr);
		return 0; // close the connection
	}

	client->autoupdate_token = pktCheckNullTerm(pak_in) ? pktMallocString(pak_in) : NULL;
	client->host = pktCheckNullTerm(pak_in) ? pktMallocString(pak_in) : NULL;
	client->referrer = pktCheckNullTerm(pak_in) ? pktMallocString(pak_in) : NULL;
	client->patcher_version = pktCheckRemaining(pak_in, 4) ? pktGetU32(pak_in) : client->patcher_version_fake;

	LOG_PATCH_COMMAND(("patcher_version", "%d", client->patcher_version)
		("patcher_version_fake", "%d", client->patcher_version_fake)
		("autoupdate_token", "%s", NULL_TO_EMPTY(client->autoupdate_token))
		("host", "%s", NULL_TO_EMPTY(client->host))
		("referrer", "%s", NULL_TO_EMPTY(client->referrer)));

	// Make sure the real version number makes sense.
	if (client->patcher_version < client->patcher_version_fake)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		estrConcatf(&estr, "The second client version number %d is less than the first %d, which doesn't make any sense.",
			client->patcher_version, client->patcher_version_fake);
		LOG_PATCH_RESPONSE(("status", "dontTalkToMe") ("details", "patcher_version_fake insane"));
		dontTalkToMe(client, estr);
		estrDestroy(&estr);
		return 0; // close the connection
	}

	for(i = 0; i < eaSize(&config->redirects); ++i)
	{
		ServerRedirect *redirect = config->redirects[i];
		if(!redirect->disabled && checkAllowDeny(redirect->ips, redirect->exclude, ip))
		{
			AddressAndPort *redir_to = &redirect->direct_to;
			bool redir_disabled = redirect->disabled;
			bool found_match;
			
			// Check the host and referrer matches
			if(eaSize(&redirect->host_match))
			{
				found_match = false;
				FOR_EACH_IN_EARRAY(redirect->host_match, const char, host)
					if(stricmp(NULL_TO_EMPTY(host), NULL_TO_EMPTY(client->host))==0)
					{
						found_match = true;
						break;
					}
				FOR_EACH_END
				if(!found_match)
					continue; // Don't apply this redirect block
			}

			if(eaSize(&redirect->referrer_match))
			{
				found_match = false;
				FOR_EACH_IN_EARRAY(redirect->referrer_match, const char, referrer)
					if(stricmp(NULL_TO_EMPTY(referrer), NULL_TO_EMPTY(client->referrer))==0)
					{
						found_match = true;
						break;
					}
				FOR_EACH_END
				if(!found_match)
					continue; // Don't apply this redirect block
			}

			// Skip this redirect if it is does not match token rules.
			if (eaSize(&redirect->token_match) && eaFindString(&redirect->token_match, client->autoupdate_token) == -1)
				continue;
			if (eaFindString(&redirect->no_token_match, client->autoupdate_token) != -1)
				continue;

			if(eaSize(&redirect->alts))
			{
				do 
				{
					if(redirect->alt_index)
					{
						redir_to = redirect->alts[redirect->alt_index-1]->address;
						redir_disabled = redirect->alts[redirect->alt_index-1]->disabled;
					}
					else
					{
						redir_to = &redirect->direct_to;
						redir_disabled = redirect->disabled;
					}
					redirect->alt_index = (redirect->alt_index + 1) % (eaSize(&redirect->alts) + 1);
				} while (redir_disabled);
				
			}

			redirect_server = redir_to->server;
			if(redir_to->port)
				redirect_port = redir_to->port;
			break;
		}
	}

	// Token redirect that just means not to do anything
	if(stricmp(redirect_server, "me")==0)
		redirect_server = NULL;

	// If you were about to redirect to the current server again, don't
	listen_ip = linkGetListenIp(link);
	if(listen_ip && stricmp(redirect_server, makeIpStr(listen_ip))==0)
		redirect_server = NULL;
	
	if(redirect_server && client->patcher_version >= PATCHCLIENT_VERSION_AUTOUPDATEFIX)
	{
		Packet *pak_out = pktCreate(link, PATCHSERVER_CONNECT_OK);
		pktSendBool(pak_out, true);
		pktSendString(pak_out, redirect_server);
		pktSendU32(pak_out, redirect_port);
		pktSendU32(pak_out, getCurrentFileTime());	// current server time
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("status", "redirect") ("redirect_server", "%s", NULL_TO_EMPTY(redirect_server)) ("redirect_port", "%d", redirect_port));
		return 0; // PATCHFIXME: let the client close the connection?
	}

	update_file = patchserverGetAutoUpdateFile(client->autoupdate_token, ip);

	if(update_file)
	{
		Packet *pak_out = pktCreate(link, PATCHSERVER_CONNECT_OK);
		pktSendBool(pak_out, false); // don't redirect
		pktSendBool(pak_out, true); // update info
		pktSendU32(pak_out, update_file->checksum);
		pktSendU32(pak_out, update_file->size);
		pktSendU32(pak_out, update_file->modified);
		pktSendU32(pak_out, getCurrentFileTime());	// current server time
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("status", "update") ("checksum", "%lu", update_file->checksum) ("size", "%lu", update_file->size)
			("modified", "%lu", update_file->modified));
		return 1;
	}
	else
	{
		Packet *pak_out = pktCreate(link, PATCHSERVER_CONNECT_OK);
		pktSendBool(pak_out, false); // don't redirect
		pktSendBool(pak_out, false); // no update info
		pktSendU32(pak_out, getCurrentFileTime());	// current server time
		pktSend(&pak_out);
		LOG_PATCH_RESPONSE(("status", "normal"));
		return 1;
	}
}

int handleReqAutoUpdate(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	FileVersion *update_file = patchserverGetAutoUpdateFile(client->autoupdate_token, linkGetSAddr(link));

	LOG_PATCH_COMMAND(("token", "%s", NULL_TO_EMPTY(client->autoupdate_token)));

	if(update_file)
	{
		sendAutoUpdate(patchfileFromDb(update_file, g_patchserver_config.autoupdatedb), client);
		return 1;
	}
	else
	{
		dontTalkToMe(client, "Your client requested an autoupdate that doesn't exist");
		return 0;
	}
}

int handleLog(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *action = pktGetStringTemp(pak_in);
	char *buf = pktGetStringTemp(pak_in), ip_buf[16];
	enumLogCategory category = LOG_PATCHCLIENT;
	const char *logline = buf;
	char *scratch = NULL;

	// Put ClientThroughput logs into a separate category.
	if (!stricmp_safe(action, "ClientThroughput"))
	{
		category = LOG_THROUGHPUT;
		estrStackCreate(&scratch);
		estrPrintf(&scratch, "uid %"FORM_LL"u %s", client->UID, buf);
		logline = scratch;
	}

	objLog(category, GLOBALTYPE_PATCHSERVER, linkGetSAddr(link), 1, linkGetIpStr(link, SAFESTR(ip_buf)), NULL, g_patchserver_config.displayName, action, NULL, "%s", NULL_TO_EMPTY(logline));

	estrDestroy(&scratch);

	return 1;
}

int handlePing(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *data = pktGetStringTemp(pak_in);
	Packet *pak = pktCreate(link, PATCHSERVER_PONG);
	pktSendString(pak, data);
	pktSend(&pak);
	return 1;
}

void handleHeartbeat(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *name, ip[64];

	linkGetIpStr(link, SAFESTR(ip));
	if(stricmp(ip, "127.0.0.1")!=0)
	{
		dontTalkToMe(client, "Invalid source for heartbeat");
		printfColor(COLOR_RED|COLOR_BRIGHT, "Bad heartbeat from %s\n", ip);
		return;
	}

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_HEARTBEAT, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet was not null terminated");
	name = pktGetStringTemp(pak_in);
#undef QUIT_HANDLE

	printf("Got heartbeat for %s\n", name);
	heartbeatReset(name);
	
	return;
}

void handleSetPrefix(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *prefix;

#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_SET_PREFIX, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet was not null terminated");
	prefix = pktGetStringTemp(pak_in);
#undef QUIT_HANDLE

	LOG_PATCH_COMMAND(("prefix", "%s", NULL_TO_EMPTY(prefix)));

	if(prefix && !prefix[0])
		prefix = NULL;
	if(prefix)
		client->prefix = strdup(prefix);
	else
		client->prefix = NULL;

	return;
}

// Handle PATCHCLIENT_PATCHSERVER_ACTIVATED.
void handleActivated(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *name;
	char *category;
	char *parent;
	bool found_parent;
	PatchChildServerData *data;
	PatchChildServerData *old = NULL;
	bool success;

	// Parse the packet.
#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_PATCHSERVER_ACTIVATED, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet was not null terminated");
	name = pktGetStringTemp(pak_in);
	category = pktGetStringTemp(pak_in);
	parent = pktGetStringTemp(pak_in);
	if (!name || !*name || !category || !*category || !parent)
		QUIT_HANDLE("name or category missing");
#undef QUIT_HANDLE
	LOG_PATCH_COMMAND(("name", "%s", NULL_TO_EMPTY(name))
		("category", "%s", NULL_TO_EMPTY(category))
		("parent", "%s", NULL_TO_EMPTY(parent)));

	// Check that the parent exists.
	if (parent && *parent)
	{
		found_parent = false;
		EARRAY_CONST_FOREACH_BEGIN(client->servers, i, n);
		{
			if (!stricmp(client->servers[i]->name, parent))
			{
				found_parent = true;
				break;
			}
		}
		EARRAY_FOREACH_END;
		if (!found_parent)
		{
			AssertOrAlert("UNKNOWN_PARENT", "Link %"FORM_LL"u (%s) sent a server \"%s\" with an unknown parent \"%s\"",
				client->UID, client->ipstr, name, parent);
			linkRemove_wReason(&old->client->link, "Unknown parent");
			patchserverVerifyChildServers();
			return;
		}
	}

	// Check if this link has already tried to activate this server.
	EARRAY_CONST_FOREACH_BEGIN(client->servers, i, n);
	{
		if (!stricmp(client->servers[i]->name, name))
		{
			AssertOrAlert("DUPLICATE_SERVER_LIST", "Link %"FORM_LL"u (%s) sent a duplicate server \"%s\"",
				client->UID, client->ipstr, name);
			linkRemove_wReason(&old->client->link, "Duplicate server");
			patchserverVerifyChildServers();
			return;
		}
	}
	EARRAY_FOREACH_END;

	// Remove any existing server with this name.
	if (stashFindPointer(s_child_servers, name, &old))
	{
		devassert(old->client);
		log_printf(LOG_PATCHSERVER_GENERAL, "ActivateDuplicate(name %s category %s parent %s old_category %s old_parent %s)",
			 NULL_TO_EMPTY(name),
			 NULL_TO_EMPTY(category),
			 NULL_TO_EMPTY(parent),
			 NULL_TO_EMPTY(old->category),
			 NULL_TO_EMPTY(old->parent));
		devassert(old->client->link != link);
		if (old->client->link != link)
			linkRemove_wReason(&old->client->link, "Another server tried to register the same name");
		if (stashFindPointer(s_child_servers, name, &old))
		{
			patchTrackingRemove(name);
			devassertmsgf(0, "Link %"FORM_LL"u (%s) sent a server that was already registered \"%s\", and closing the old link didn't fix it",
				client->UID, client->ipstr, name);
			success = stashRemovePointer(s_child_servers, name, &old);
			devassert(success);
		}
		patchserverVerifyChildServers();
	}

	// Add server data on client.
	data = StructCreate(parse_PatchChildServerData);
	data->name = strdup(name);
	data->category = strdup(category);
	data->parent = strdup((parent && *parent) ? parent : g_patchserver_config.displayName);
	data->client = client;
	eaPush(&client->servers, data);

	// Store to global stash.
	success = stashAddPointer(s_child_servers, data->name, data, true);
	devassert(success);
	patchserverVerifyChildServers();

	// Notify the parent.
	if (g_patchserver_config.parent.server && g_patchserver_config.serverCategory)
		patchupdateNotifyActivate(data->name, data->category, data->parent);

	// Add tracking.
	patchTrackingAdd(data->name, data->category, data->parent);

	// Propagate HTTP patching configuration.
	patchserverSaveDynamicHttpConfig();

	// Propagate Autoup patching configuration.
	patchserverSaveDynamicAutoupConfig();
}

// Handle PATCHCLIENT_PATCHSERVER_DEACTIVATED.
void handleDeactivated(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *name;
	PatchChildServerData *server = NULL;
	bool success;

	// Parse the packet.
#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_PATCHSERVER_DEACTIVATED, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet was not null terminated");
	name = pktGetStringTemp(pak_in);
	if (!name || !*name)
		QUIT_HANDLE("name missing");
#undef QUIT_HANDLE
	LOG_PATCH_COMMAND(("name", "%s", NULL_TO_EMPTY(name)));

	// Make sure we know the server being deactivated.
	success = stashFindPointer(s_child_servers, name, &server);
	if (!success)
	{
		AssertOrAlert("DEACTIVATE_UNKNOWN", "Link %"FORM_LL"u (%s) tried to deactivate a server \"%s\" that we don't know about",
			client->UID, client->ipstr, name);
		linkRemove_wReason(&link, "Deactivate unknown");
		patchserverVerifyChildServers();
		return;
	}

	// Make sure the server was activated on this link.
	if (server->client != client)
	{
		AssertOrAlert("DEACTIVATE_UNOWNED", "Link %"FORM_LL"u (%s) tried to deactivate a server \"%s\" that is owned by another link",
			client->UID, client->ipstr, name);
		linkRemove_wReason(&link, "Deactivate unowned");
		patchserverVerifyChildServers();
		return;
	}

	// Look for this server, and remove it from the tracked list if we can find it.
	EARRAY_CONST_FOREACH_BEGIN(client->servers, i, n);
	{
		if (!stricmp(client->servers[i]->name, name))
		{
			int j;

			// Make sure this won't mess up parenting.
			for (j = i; j < eaSize(&client->servers); ++j)
			{
				if (!stricmp(client->servers[j]->parent, name))
				{
					AssertOrAlert("DEACTIVATE_UNOWNED", "Link %"FORM_LL"u (%s) tried to deactivate a server \"%s\" that is a parent of an active server \"%s\"",
						client->UID, client->ipstr, name, client->servers[j]->parent);
					linkRemove_wReason(&link, "Deactivate parent");
					patchserverVerifyChildServers();
					return;
				}
			}

			// Notify the parent.
			if (g_patchserver_config.parent.server && g_patchserver_config.serverCategory)
				patchupdateNotifyDeactivate(name);

			// Remove it.
			success = stashRemovePointer(s_child_servers, client->servers[i]->name, NULL);
			devassert(success);
			patchTrackingRemove(client->servers[i]->name);
			StructDestroy(parse_PatchChildServerData, client->servers[i]);
			eaRemove(&client->servers, i);

			// Verify.
			patchserverVerifyChildServers();

			return;
		}
	}
	EARRAY_FOREACH_END;

	// Alert if we don't know anything about this server.
	AssertOrAlert("DEACTIVATED_MYSTERY_SERVER", "Unknown server \"%s\" deactivated.", name);
}

// Handle PATCHCLIENT_PATCHSERVER_UPDATE_STATUS.
void handleUpdateStatus(Packet *pak_in, NetLink *link, PatchClientLink *client)
{
	char *name;
	char *status;
	bool found;

	// Parse the packet.
#define QUIT_HANDLE(reason) {badPacket(client, __FUNCTION__, NULL, PATCHCLIENT_PATCHSERVER_UPDATE_STATUS, 0, 0, 0, reason); return;}
	if(!pktCheckNullTerm(pak_in))
		QUIT_HANDLE("packet was not null terminated");
	name = pktGetStringTemp(pak_in);
	status = pktGetStringTemp(pak_in);
	if (!name || !*name || !status || !*status)
		QUIT_HANDLE("name or status missing");
#undef QUIT_HANDLE
	LOG_PATCH_COMMAND(("name", "%s", NULL_TO_EMPTY(name))
		("status", "%s", NULL_TO_EMPTY(status)));

	// Save last update.
	found = false;
	EARRAY_CONST_FOREACH_BEGIN(client->servers, i, n);
	{
		PatchChildServerData *server = client->servers[i];
		devassert(server->client == client);
		if (!stricmp_safe(server->name, name))
		{
			free(server->last_update);
			server->last_update = strdup(status);
			found = true;
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (!found)
	{
		AssertOrAlert("UPDATE_UNKNOWN_SERVER", "Link %"FORM_LL"u (%s) tried to send an update for a server \"%s\" that we don't know about",
			client->UID, client->ipstr, name);
		linkRemove_wReason(&link, "Update unknown");
		patchserverVerifyChildServers();
		return;
	}

	// Relay this to the parent.
	if (g_patchserver_config.parent.server)
		patchupdateNotifyUpdateStatus(name, status);
	else
		patchTrackingUpdate(client, name, status);
}

// Report some packet handling performance statistics.
static void ReportClientMsgPerformance(int cmd, const char *ip, S64 start_ticks, S64 stop_ticks)
{
	S64 duration;
	static S64 last_record_ticks = 0;

	static U64 stat_count[PATCHCLIENT_CMD_COUNT];
	static S64 stat_duration_total[PATCHCLIENT_CMD_COUNT];
	static S64 stat_duration_min[PATCHCLIENT_CMD_COUNT];
	static S64 stat_duration_max[PATCHCLIENT_CMD_COUNT];
	static bool initialized = false;

	PERFINFO_AUTO_START_FUNC();

	duration = stop_ticks - start_ticks;
	devassert(duration >= 0);
	s_handleMsgTime += duration;

	// Record packet stalls.
	if (duration > timerCpuSpeed64()/8)
		servLog(LOG_FRAMEPERF, "PatchCmdStall", "server %s cmd %s ip %s duration %f", g_patchserver_config.displayName,
			StaticDefineIntRevLookup(PatchClientCmdEnum, cmd), ip, timerSeconds64(duration*1000));

	// Initialize per-packet performance statistics.
	if (!initialized)
	{
		unsigned i;
		for (i = 0; i < PATCHCLIENT_CMD_COUNT; ++i)
			stat_duration_min[i] = LLONG_MAX;
		initialized = true;
	}

	// Record per-packet performance statistics.
	if (cmd < PATCHCLIENT_CMD_COUNT)
	{
		++stat_count[cmd];
		stat_duration_total[cmd] += duration;
		stat_duration_min[cmd] = MIN(stat_duration_min[cmd], duration);
		stat_duration_max[cmd] = MAX(stat_duration_max[cmd], duration);
	}

	// Report statistics every second.
	if (stop_ticks > last_record_ticks + timerCpuSpeed64())
	{
		unsigned i;
		U64 total_count = 0;
		S64 total_duration_total = 0;
		S64 total_duration_min = LLONG_MAX;
		S64 total_duration_max = 0;
		static PatchMsgPerfInfos info;
		static PatchMsgPerfInfo info_storage[PATCHCLIENT_CMD_COUNT];

		// Initialize the reporting struct if necessary.
		if (!info.server)
		{
			info.server = g_patchserver_config.displayName;
			devassert(info.server);
			eaSetCapacity(&info.cmds, PATCHCLIENT_CMD_COUNT);
			for (i = 0; i < PATCHCLIENT_CMD_COUNT; ++i)
				info_storage[i].cmd = StaticDefineIntRevLookup(PatchClientCmdEnum, i);
		}

		// Look for data to report.
		for (i = 0; i < PATCHCLIENT_CMD_COUNT; ++i)
		{
			if (stat_count[i])
			{
				const char *cmd_name = StaticDefineIntRevLookup(PatchClientCmdEnum, i);
				total_count += stat_count[i];
				total_duration_total += stat_duration_total[i];
				total_duration_min = MIN(total_duration_min, stat_duration_min[i]);
				total_duration_max = MAX(total_duration_max, stat_duration_max[i]);
				info_storage[i].count = stat_count[i];
				info_storage[i].duration = timerSeconds64(stat_duration_total[i]*1000);
				info_storage[i].minDuration = timerSeconds64(stat_duration_min[i]*1000);
				info_storage[i].maxDuration = timerSeconds64(stat_duration_max[i]*1000);
				stat_count[i] = 0;
				stat_duration_total[i] = 0;
				stat_duration_min[i] = LLONG_MAX;
				stat_duration_max[i] = 0;
				eaPush(&info.cmds, &info_storage[i]);
			}
		}
		info.totalCount = total_count;
		info.totalDuration = timerSeconds64(total_duration_total*1000);
		info.totalMinDuration = timerSeconds64(total_duration_min*1000);
		info.totalMaxDuration = timerSeconds64(total_duration_max*1000);
		servLogWithStruct(LOG_FRAMEPERF, "PatchCmdPerf", &info, parse_PatchMsgPerfInfos);
		eaClear(&info.cmds);
		last_record_ticks = stop_ticks;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void patchHandleClientMsg(Packet * pak_in, int cmd, NetLink * link, void *user_data)
{
	static StaticCmdPerf cmdPerf[PATCHCLIENT_CMD_COUNT];
	
	PatchClientLink	* client = user_data;
	S64 start_ticks;
	U32 notHandled = 0;
	char ip[16];

	start_ticks = timerCpuTicks64();

	if(cmd >= 0 && cmd < ARRAY_SIZE(cmdPerf)){
		if(!cmdPerf[cmd].name){
			char buffer[100];
			sprintf(buffer, "Cmd:%s (%d)", StaticDefineIntRevLookup(PatchClientCmdEnum, cmd), cmd);
			cmdPerf[cmd].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[cmd].name, &cmdPerf[cmd].pi, 1);
	}else{
		PERFINFO_AUTO_START("Cmd:Unknown", 1);
	}

	if(client->hasProtocolError)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(!client->patcher_version && cmd != PATCHCLIENT_CONNECT)
	{
		ERROR_PRINTF("patchHandleClientMsg: client did not send a connect packet (sent %d)\n",cmd);
		dontTalkToMe(client, "Your client didn't send a connect packet.");
		PERFINFO_AUTO_STOP();
		return;
	}

	// Handle universal (parent and child) commands
	switch(cmd)
	{
		xcase PATCHCLIENT_CONNECT:
			if(!handleConnectPacket(pak_in, link, client)){
				flushLink(link);
				PERFINFO_AUTO_STOP();
				return;
			}
		xcase PATCHCLIENT_REQ_AUTOUPDATE:
			if(!handleReqAutoUpdate(pak_in, link, client)){
				flushLink(link);
				PERFINFO_AUTO_STOP();
				return;
			}
		xcase PATCHCLIENT_REQ_FILEINFO:
			handleReqFileInfo(pak_in, link, client);
		xcase PATCHCLIENT_REQ_FINGERPRINTS:
			handleReqFingerprints(pak_in, link, client, false);
		xcase PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED:
			handleReqFingerprints(pak_in, link, client, true);
		xcase PATCHCLIENT_REQ_BLOCKS:
			handleReqBlocks(pak_in, link, client, false);
		xcase PATCHCLIENT_REQ_BLOCKS_COMPRESSED:
			handleReqBlocks(pak_in, link, client, true);
		xcase PATCHCLIENT_SET_PROJECT_VIEW_BY_REV:
			handleSetProjectViewByRev(pak_in, link, client);
		xcase PATCHCLIENT_SET_PROJECT_VIEW_BY_TIME:
			handleSetProjectViewByTime(pak_in, link, client);
		xcase PATCHCLIENT_SET_PROJECT_VIEW_DEFAULT:
			handleSetProjectViewDefault(pak_in, link, client);
		xcase PATCHCLIENT_SET_PROJECT_VIEW_NAME:
			handleSetProjectViewName(pak_in, link, client);
		xcase PATCHCLIENT_REQ_NOTIFICATION:
			handleReqNotification(pak_in, link, client, true);
		xcase PATCHCLIENT_REQ_NOTIFICATION_OFF:
			handleReqNotification(pak_in, link, client, false);
		xcase PATCHCLIENT_REQ_HEADERINFO:
			handleReqHeaderInfo(pak_in, link, client);
		xcase PATCHCLIENT_REQ_HEADER_BLOCKS:
			handleReqHeaderBlocks(pak_in, link, client);
		xcase PATCHCLIENT_REQ_PROJECT_LIST:
			handleReqProjectList(pak_in, link, client);
		xcase PATCHCLIENT_REQ_NAME_LIST:
			handleReqNameList(pak_in, link, client);
		xcase PATCHCLIENT_SET_AUTHOR:
			handleSetAuthor(pak_in, link, client);
		xcase PATCHCLIENT_SHUTDOWN:
			handleShutdown(pak_in, link, client);
		xcase PATCHCLIENT_SERVER_MERGE:
			handleServerMerge(pak_in, link, client);
		xcase PATCHCLIENT_NOTIFYVIEW:
			handleNotifyView(pak_in, link, client);
		xcase PATCHCLIENT_REQ_CHECKINS_BETWEEN_TIMES:
			handleReqCheckinsBetweenTimes(pak_in, link, client);
		xcase PATCHCLIENT_REQ_CHECKIN_INFO:
			handleReqCheckinInfo(pak_in, link, client);
		xcase PATCHCLIENT_REQ_FILEVERSIONINFO:
			handleReqFileVersionInfo(pak_in, link, client);
		xcase PATCHCLIENT_LOG:
			handleLog(pak_in, link, client);
		xcase PATCHCLIENT_PING:
			handlePing(pak_in, link, client);
		xcase PATCHCLIENT_HEARTBEAT:
			handleHeartbeat(pak_in, link, client);
		xcase PATCHCLIENT_SET_PREFIX:
			handleSetPrefix(pak_in, link, client);
		xcase PATCHCLIENT_PATCHSERVER_ACTIVATED:
			handleActivated(pak_in, link, client);
		xcase PATCHCLIENT_PATCHSERVER_DEACTIVATED:
			handleDeactivated(pak_in, link, client);
		xcase PATCHCLIENT_PATCHSERVER_UPDATE_STATUS:
			handleUpdateStatus(pak_in, link, client);
		xcase PATCHCLIENT_REQ_COMPLETELY_SYNCED:
			handleIsCompletelySynced(pak_in, link, client);
		xdefault:
			if(g_patchserver_config.parent.server)
			{
				char ip_str[16];
				ERROR_PRINTF("patchHandleClientMsg: Invalid child patchserver command or unknown command %d\n",cmd);
				servLog(LOG_PATCHSERVER_GENERAL, "BadPacket", "server %s ip %s id %d", g_patchserver_config.displayName,
					linkGetIpStr(client->link, SAFESTR(ip_str)), cmd);
				dontTalkToMe(client, "This PatchServer doesn't recognize your command (it doesn't support checkins).");
				PERFINFO_AUTO_STOP();
				return;
			}
			notHandled = 1;
	}

	// Handle parent-only commands, these are also database-project-only commands
	if(notHandled)
	{
		if(cmd != PATCHCLIENT_SET_EXPIRATION && cmd != PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL && cmd != PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL_NAME)
		{
			if(!client->project || !client->project->allow_checkins)
			{
				// originally, this was left to be handled by the individual handlers, which ignore these requests
				ERROR_PRINTF("patchHandleClientMsg: Invalid project (%s) for command %s\n", client->project?client->project->name:"NULL", StaticDefineIntRevLookup(PatchClientCmdEnum, cmd));
				dontTalkToMe(client, "The project you reported is for patching only (it doesn't support checkins).");
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP_FUNC();
				return;
			}
		}

		switch(cmd)
		{
			xcase PATCHCLIENT_REQ_FILE_HISTORY_STRUCTS:
				handleReqFileHistory(pak_in,link,client);
			xcase PATCHCLIENT_REQ_VERSION_INFO:
				handleReqVersionInfo(pak_in, link, client);
			xcase PATCHCLIENT_REQ_BRANCH_INFO:
				handleReqBranchInfo(pak_in, link, client);
			xcase PATCHCLIENT_REQ_LOCK:
				handleReqLock(pak_in,link,client);
			xcase PATCHCLIENT_REQ_UNLOCK:
				handleReqUnlock(pak_in, link, client);
			xcase PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL:
				handleSetProjectViewNewIncremental(pak_in, link, client);
			xcase PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL_NAME:
				handleSetProjectViewNewIncrementalName(pak_in, link, client);
			xcase PATCHCLIENT_REQ_CHECKIN:
				handleReqCheckin(pak_in, link, client, false);
			xcase PATCHCLIENT_REQ_FORCEIN:
				handleReqCheckin(pak_in, link, client, true);
			xcase PATCHCLIENT_BLOCK_SEND:
				handleBlockSend(pak_in, link, client);
			xcase PATCHCLIENT_FINISH_CHECKIN:
				handleFinishCheckin(pak_in, link, client, false);
			xcase PATCHCLIENT_FINISH_FORCEIN:
				handleFinishCheckin(pak_in, link, client, true);
			xcase PATCHCLIENT_NAME_VIEW:
				handleNameView(pak_in, link, client);
			xcase PATCHCLIENT_SET_EXPIRATION:
				handleSetExpiration(pak_in, link, client);
			xcase PATCHCLIENT_SET_FILE_EXPIRATION:
				handleSetFileExpiration(pak_in, link, client);
			xcase PATCHCLIENT_CHECK_DELETED:
				handleCheckDeleted(pak_in, link, client);
			xcase PATCHCLIENT_REQ_LASTAUTHOR:
				handleReqLastAuthor(pak_in, link, client);
			xcase PATCHCLIENT_REQ_LOCKAUTHOR:
				handleReqLockAuthor(pak_in, link, client);
			xcase PATCHCLIENT_REQ_UNDO_CHECKIN:
				handleReqUndoCheckin(pak_in, link, client);
			xdefault:
				ERROR_PRINTF("patchHandleClientMsg: Unknown command %d\n",cmd);
				dontTalkToMe(client, "Unknown command");
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP_FUNC();
				return;
		}
	}

	PERFINFO_AUTO_START("Finishing", 1);

	flushLink(link);

 	if( cmd == PATCHCLIENT_FINISH_CHECKIN ||
 		cmd == PATCHCLIENT_FINISH_FORCEIN ||
 		cmd == PATCHCLIENT_REQ_LOCK ||
 		cmd == PATCHCLIENT_REQ_UNLOCK ||
 		cmd == PATCHCLIENT_NAME_VIEW)
	{
		patchserverMirrorNotifyDirty();

		if(SAFE_MEMBER2(client, project, serverdb))
		{
			client->project->serverdb->destroy_full_manifest_on_notify = 1;
		}
	}

	// Log if processing this command took longer than 125 ms.
	ReportClientMsgPerformance(cmd, linkGetIpStr(client->link, SAFESTR(ip)),
		start_ticks, timerCpuTicks64());

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

void patchNetInit()
{
	ServerConfig *config = &g_patchserver_config;
	int i;
	NetListen * listener;
	int * ports = NULL;

	// Force disable packet verify data
	commDefaultVerify(0);
	s_net_comm = commCreate(COMM_MONITOR_WAIT_MSECS, getNumVirtualCpus());

	if(s_periodic_disconnects)
		commTimedDisconnect(s_net_comm, s_seconds_between_disconnects);
	if(s_random_disconnects)
		commRandomDisconnect(s_net_comm, s_seconds_between_disconnects);

	for(i = 0; i < eaiSize(&config->client_ports); i++)
		eaiPushUnique(&ports, config->client_ports[i]);

	for(i = 0; i < eaiSize(&s_ports); i++)
		eaiPushUnique(&ports, s_ports[i]);

	if(eaiSize(&ports) == 0)
		eaiPush(&ports, DEFAULT_PATCHSERVER_PORT);

	for(i = 0; i < eaiSize(&ports); i++)
	{
		U32 ip = INADDR_ANY;
		if (config->client_host && *config->client_host)
		{
			ip = ipFromString(config->client_host);
			if (!ip)
				FatalErrorf("Unable to resolve client listen host \"%s\"", config->client_host);
		}
		listener = commListenIp(s_net_comm,
			LINKTYPE_TOUNTRUSTED_10MEG,
			(PATCHER_LINK_COMPRESSION_ON?LINK_COMPRESS:LINK_NO_COMPRESS) | LINK_ENCRYPT | LINK_ENCRYPT_OPTIONAL | LINK_FLUSH_PING_ACKS,
			ports[i],
			patchHandleClientMsg, patchClientCreateCb, patchClientDeleteCb,
			sizeof(PatchClientLink), ip);
		assertmsgf(listener, "Unable to listen on port %d", ports[i]);
		eaPush(&s_net_listeners, listener);

		log_printf(LOG_PATCHSERVER_CONNECTIONS,"Listening on 0.0.0.0:%i", ports[i]);
	}
	eaiDestroy(&ports);

	patchHttpInit();

	patchupdateInit(config);

	// Initialize throttling.
	if(g_patchserver_config.bandwidth_config)
		patchserverThrottleInit();
}

typedef struct TitleBarInfo
{
	U64 total_sent;
	U64 loop_count;
	F32 cpu_seconds;
} TitleBarInfo;

static U64 g_total_sent;
static TitleBarInfo ** g_titlebar_info = NULL;
static F32 * g_sent_history = NULL;

int totalUpSentUncompressed(NetLink * link, S32 index, PatchClientLink * client, U64 * user_data)
{
	const LinkStats * stats = linkStats(link);
	*user_data += stats->send.bytes;
	return 1;
}

U64 totalBytesSentUncompressed(void)
{
	U64 bytes = 0;
	iterateAllPatchLinks(totalUpSentUncompressed, &bytes);
	return bytes + s_sent_bytes_history;
}

int totalUpSent(NetLink * link, S32 index, PatchClientLink * client, U64 * user_data)
{
	const LinkStats * stats = linkStats(link);
	*user_data += stats->send.real_bytes;
	return 1;
}

U64 totalBytesSent(void)
{
	U64 bytes = 0;
	iterateAllPatchLinks(totalUpSent, &bytes);
	return bytes + s_sent_bytes_history;
}

int totalUpReceived(NetLink * link, S32 index, PatchClientLink * client, U64 * user_data)
{
	const LinkStats * stats = linkStats(link);
	*user_data += stats->recv.real_bytes;
	return 1;
}

U64 totalBytesReceived(void)
{
	U64 bytes = 0;
	iterateAllPatchLinks(totalUpReceived, &bytes);
	return bytes + s_received_bytes_history;
}

void checkExecMergeProcess(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	execMergeProcess(timeSinceLastCallback >= 0);
}

void execMergeProcess(bool time_to_merge)
{
	static int pid = 0;
	static time_t start = 0;
	int i;
	char arg[] = "-merge";
	char * exe = getExecutableName();
	char cmdLine[MAX_PATH];
	bool merge_needed = false;

	if(pid)
	{
		HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
		DWORD code;
		if(process)
		{
			GetExitCodeProcess(process, &code);
			CloseHandle(process);
			if(code == STILL_ACTIVE)
			{
				time_t delta = time(NULL) - start;
				ERROR_PRINTF("Attempted to merge, but the previous merge is still going\n");
				if(delta > 1200)
				{
					TriggerAlert("MERGETIMEOUT", STACK_SPRINTF("Journal merge process has been running for %"FORM_LL"d seconds", delta), ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
						         0, GLOBALTYPE_PATCHSERVER, 0, GLOBALTYPE_PATCHSERVER, 0, g_patchserver_config.displayName, 0);
				}
				return;
			}
		}
	}

	if(time_to_merge)
	{
		for(i = 0; i < eaSize(&g_patchserver_config.serverdbs); i++)
		{
			merge_needed |= journalRename(g_patchserver_config.serverdbs[i]->name);
		}
	}
	else
	{
		for(i = 0; i < eaSize(&g_patchserver_config.serverdbs); i++)
		{
			merge_needed |= g_patchserver_config.serverdbs[i]->save_me;
		}
	}

	if(!merge_needed)
		return;

	sprintf(cmdLine, "\"%s\" %s", exe, arg);
	pid = system_detach(cmdLine, 1, false);
	if(pid)
	{
		INFO_PRINTF("Started merge process ID %i\n", pid);
		start = time(NULL);
	}
	else
	{
		ERROR_PRINTF("Failed to start merge\n");
	}
}

void patchserverGetTitleBarText(char* outBuffer,
								S32 outBuffer_size)
{
	F32 sending_rate = 0.0, loop_rate = 0.0, sent_per_loop = 0.0, buffer_usage = 0.0;
	U64 served_bytes = patchfileCacheSize();
	S32 filesToWrite;
	U64 bytesToWrite;
	
	g_total_sent = 0;
	iterateAllPatchLinks(totalUpSent, &g_total_sent);

	if(eaSize(&g_titlebar_info) > 5)
	{
		int i;
		U64 loops;
		U64 sent;
		F32 seconds;

		free(g_titlebar_info[0]);
		eaRemove(&g_titlebar_info, 0);

		loops = g_titlebar_info[4]->loop_count - g_titlebar_info[0]->loop_count;
		sent = g_titlebar_info[4]->total_sent - g_titlebar_info[0]->total_sent;
		seconds = 0;
		for(i = 0; i < 5; i++)
			seconds += g_titlebar_info[i]->cpu_seconds;

		sending_rate = (F32) sent / ((F32) seconds * (F32)(1024));
		if(loops > 0)
		{
			loop_rate = (F32) loops / (F32) seconds;
			sent_per_loop = (F32) sent / (F32) loops;
			if(s_connections > 0)
				buffer_usage = sent_per_loop * 100.0f / (s_connections * patchupdateGetMaxNetBytes());
		}

		eafPush(&g_sent_history, sending_rate);
		if(eafSize(&g_sent_history) > 120)
			eafRemove(&g_sent_history, 0);
	}
	
	filesToWrite = patchserverdbGetHoggMirrorQueueSize(&bytesToWrite);

	s_buffer_usage = buffer_usage;
	sprintf_s(	SAFESTR2(outBuffer),
				"%sPatchServer |"
				" %s |"
				" Conns: %u"
				" HTTP: %u"
				"  HoggOps: %d"
				"  Mirrors: %d"
				"%s%s%s"
				"  Loads: %d"
				"  Processing: %d"
				"  Sent: %.1fMB"
				"  Send: %.2fKB/s"
				"  Cache: %.1fMB"
				"  FPS: %.1f"
				"  LongTick: %.0fms"
				"  Buffer: %.1f%%",
				g_patchserver_config.reportdown ? "[DOWN]" : "",
				g_patchserver_config.displayName,
				s_connections,
				g_http_connections,
				hogTotalPendingOperationCount(),
				filesToWrite,
				filesToWrite ? "  (" : "",
				filesToWrite ? getCommaSeparatedInt(bytesToWrite) : "",
				filesToWrite ? " bytes)" : "",
				fileLoaderLoadsPending(),
				patchserverLoadProcessRequestsPending(),
				(F32)(g_total_sent + s_sent_bytes_history) / (F32)(1024*1024),
				sending_rate,
				(F32)served_bytes / (F32)(1024*1024),
				loop_rate,
				s_last_long_tick*1000,
				buffer_usage);
}

void updateTitleBar(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char title[500];
	TitleBarInfo * info;

	info = malloc(sizeof(TitleBarInfo));
	info->cpu_seconds = timeSinceLastCallback;
	info->total_sent = g_total_sent + s_sent_bytes_history;
	info->loop_count = s_loopCount;
	eaPush(&g_titlebar_info, info);

	patchserverGetTitleBarText(SAFESTR(title));
	setConsoleTitleWithPid(title);
	
	s_long_tick_counter++;
	if (s_long_tick_counter >= 10) { // 5 seconds
		s_last_long_tick = s_long_tick;
		s_long_tick = 0;
		s_long_tick_counter = 0;
	}
}

void viewCachePrint(void * key, void * value, U32 rank, S64 last_used, S64 score, void * userData)
{
	PatchFile *patch = value;

	if(rank < 50)
	{
		printf("%4i %16"FORM_LL"i | %16"FORM_LL"i | %4.0fs | %s\n", rank + 1, score, last_used, (F32)(score - last_used) / (F32)timerCpuSpeed64(),
				patchFileGetUsedName(patch));
	}
}

// Mark a link to be flushed.
static void flushLink(NetLink *link)
{
	if (!s_links_to_flush)
		s_links_to_flush = stashTableCreateAddress(0);
	stashAddInt(s_links_to_flush, link, 1, false);
};

// Flush all pending data to patch links.
static void flushLinks()
{
	PERFINFO_AUTO_START_FUNC();

	// Flush each link.
	FOR_EACH_IN_STASHTABLE2(s_links_to_flush, elem)
	{
		NetLink *link = stashElementGetKey(elem);
		linkFlush(link);
	}
	FOR_EACH_END;

	// Clear the table.
	stashTableClear(s_links_to_flush);

	PERFINFO_AUTO_STOP_FUNC();
}

void patchserverShutDown(void)
{
	if(patchupdateIsConnectedToParent() || patchupdateIsConnectingToParent())
	{
		loadstart_printf("Disconnecting from master server...");
		patchupdateDisconnect();
		loadend_printf("done");
	}

	if (patchserverdbAsyncIsRunning())
	{
		loadstart_printf("Aborting pruning and compaction...");
		patchserverdbAsyncAbort();
		loadend_printf("done");
	}
	
	loadstart_printf("Closing all comms...");
	{
		commFlushAndCloseAllComms(1);
	}
	loadend_printf("done");

	loadstart_printf("Flushing hogg files...");
	{
		FOR_EACH_IN_EARRAY(g_patchserver_config.serverdbs, PatchServerDb, serverdb)
		{
			patchHALCloseAllHogs(serverdb);
		}
		FOR_EACH_END;
	}
	loadend_printf("done");
	
	loadstart_printf("Flushing local mirror writes...");
	{
		while(patchserverdbGetHoggMirrorQueueSize(NULL)){
			Sleep(10);
		}
	}
	loadend_printf("done");

	exit(0);
}

void patchserverNotifyMirrors()
{
	PERFINFO_AUTO_START_FUNC();
	if(	s_sendMirrorNotify &&
		time(NULL) - s_lastMirrorNotifyTime >= g_patchserver_config.notifyMirrorsPeriod)
	{
		s_sendMirrorNotify = 0;
		
		EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.serverdbs, i, isize);
			PatchServerDb* serverdb = g_patchserver_config.serverdbs[i];
			
			if(TRUE_THEN_RESET(serverdb->destroy_full_manifest_on_notify))
			{
				int j;
				for(j=0; j<eaSize(&serverdb->incremental_manifest_patch); j++)
				{
					patchfileDestroy(&serverdb->incremental_manifest_patch[j]);
				}
				eaClear(&serverdb->incremental_manifest_patch);
				eaiClear(&serverdb->incremental_manifest_revs);
			}
		EARRAY_FOREACH_END;
		
		iterateAllPatchLinks(notifyClient, NULL); // Notifying clients that want to be notified
		
		s_lastMirrorNotifyTime = time(NULL);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// Add this client to the appropriate shard's bucket.
static int clientConnectionStatsCB(NetLink *link, S32 index, PatchClientLink *client, void *data)
{
	StashTable projects = data;
	const char *project;
	const char *token;
	char *name = NULL;
	bool found;
	int value;
	bool success;

	// Determine project name.
	if (!client->project || !client->project->name)
		project = "Unspecified";
	else
		project = client->project->name;

	// Determine token.
	if (!client->autoupdate_token || !*client->autoupdate_token)
		token = "Unknown";
	else
		token = client->autoupdate_token;

	// Format name.
	estrStackCreate(&name);
	estrPrintf(&name, "%s:%s", token, project);

	// Get the appropriate project bucket.
	found = stashFindInt(projects, name, &value);

	// Increment the value.
	if (found)
		++value;
	else
		value = 1;
	success = stashAddInt(projects, name, value, true);
	devassert(success);
	estrDestroy(&name);

	return 1;
}

// Log how many clients are patching to each project.
static void logClientConnectionStats()
{
	int i;
	static StashTable projects = NULL;
	GenericArray **array = NULL;
	GenericArray log = {0};

	PERFINFO_AUTO_START_FUNC();

	if (!projects)
		projects = stashTableCreateWithStringKeys(s_connections * 2, StashDeepCopyKeys_NeverRelease);
	stashTableClear(projects);

	// Count clients in each project.
	for(i = 0; i < eaSize(&s_net_listeners); i++)
		linkIterate2(s_net_listeners[i], clientConnectionStatsCB, projects);

	// Create log data array.
	FOR_EACH_IN_STASHTABLE2(projects, project)
	{
		const char *project_name = stashElementGetStringKey(project);
		int count = stashElementGetInt(project);
		GenericArray *entry = StructCreate(parse_GenericArray);
		entry->string = strdup(project_name);
		entry->integer = count;
		eaPush(&array, entry);
	}
	FOR_EACH_END;

	// Format log line.
	log.array = array;
	servLogWithStruct(LOG_THROUGHPUT, "ProjectConnections", &log, parse_GenericArray);
	eaDestroy(&array);

	PERFINFO_AUTO_STOP_FUNC();
}

// Sample statistics
U32 *s_vec_connections;
U32 *s_vec_http_connections;
U64 *s_vec_cache_size;
F32 *s_vec_buffer_usage;
U32 *s_vec_hog_ops;
U32 *s_vec_loads_pending;

// Differenced accumulation sample statistics
U64 *s_vec_pcl_bytes_received;
U64 s_last_pcl_bytes_received;
U64 *s_vec_pcl_bytes_sent;
U64 s_last_pcl_bytes_sent;
U64 *s_vec_pcl_bytes_sent_payload;
U64 s_last_pcl_bytes_sent_payload;
U64 *s_vec_pcl_bytes_sent_uncompressed;
U64 s_last_pcl_bytes_sent_uncompressed;
U64 *s_vec_http_bytes_sent;
U64 s_last_http_bytes_sent;
U64 *s_vec_http_bytes_sent_overhead;
U64 s_last_http_bytes_sent_overhead;
U64 *s_vec_http_bytes_received;
U64 s_last_http_bytes_received;
U64 *s_vec_loaded_bytes;
U64 s_last_loaded_bytes;
U64 *s_vec_decompressed_bytes;
U64 s_last_decompressed_bytes;

// Empty the statistics vectors.
static void zeroStats()
{
	beaSetSize(&s_vec_connections, 0);
	beaSetSize(&s_vec_http_connections, 0);
	beaSetSize(&s_vec_cache_size, 0);
	beaSetSize(&s_vec_buffer_usage, 0);
	beaSetSize(&s_vec_hog_ops, 0);
	beaSetSize(&s_vec_loads_pending, 0);
	beaSetSize(&s_vec_pcl_bytes_received, 0);
	beaSetSize(&s_vec_pcl_bytes_sent, 0);
	beaSetSize(&s_vec_pcl_bytes_sent_payload, 0);
	beaSetSize(&s_vec_pcl_bytes_sent_uncompressed, 0);
	beaSetSize(&s_vec_http_bytes_sent, 0);
	beaSetSize(&s_vec_http_bytes_sent_overhead, 0);
	beaSetSize(&s_vec_http_bytes_received, 0);
	beaSetSize(&s_vec_loaded_bytes, 0);
	beaSetSize(&s_vec_decompressed_bytes, 0);
}

// Record one sample of statistics.
static void recordStatsSample()
{
	U32 connections = s_connections;
	U32 http_connections = g_http_connections;
	U64 cache_size = patchfileCacheSize();
	F32 buffer_usage = s_buffer_usage;
	U32 hog_ops = hogTotalPendingOperationCount();
	U32 loads_pending = fileLoaderLoadsPending();
	U64 pcl_bytes_received = totalBytesReceived();
	U64 pcl_bytes_sent = totalBytesSent();
	U64 pcl_bytes_sent_payload = s_sent_payload;
	U64 pcl_bytes_sent_uncompressed = totalBytesSentUncompressed();
	U64 http_bytes_sent = patchHttpDbBytesSent();
	U64 http_bytes_sent_overhead = patchHttpDbBytesSentOverhead();
	U64 http_bytes_received = patchHttpDbBytesReceived();
	U64 loaded_bytes = patchfileLoadedBytes();
	U64 decompressed_bytes = patchfileDecompressedBytes();

	*(U32 *)beaPushEmpty(&s_vec_connections) = connections;
	*(U32 *)beaPushEmpty(&s_vec_http_connections) = http_connections;
	*(U64 *)beaPushEmpty(&s_vec_cache_size) = cache_size;
	*(F32 *)beaPushEmpty(&s_vec_buffer_usage) = buffer_usage;
	*(U32 *)beaPushEmpty(&s_vec_hog_ops) = hog_ops;
	*(U32 *)beaPushEmpty(&s_vec_loads_pending) = loads_pending;
	*(U64 *)beaPushEmpty(&s_vec_pcl_bytes_received) = pcl_bytes_received - s_last_pcl_bytes_received;
	*(U64 *)beaPushEmpty(&s_vec_pcl_bytes_sent) = pcl_bytes_sent - s_last_pcl_bytes_sent;
	*(U64 *)beaPushEmpty(&s_vec_pcl_bytes_sent_payload) = pcl_bytes_sent_payload - s_last_pcl_bytes_sent_payload;
	*(U64 *)beaPushEmpty(&s_vec_pcl_bytes_sent_uncompressed) = pcl_bytes_sent_uncompressed - s_last_pcl_bytes_sent_uncompressed;
	*(U64 *)beaPushEmpty(&s_vec_http_bytes_sent) = http_bytes_sent - s_last_http_bytes_sent;
	*(U64 *)beaPushEmpty(&s_vec_http_bytes_sent_overhead) = http_bytes_sent_overhead - s_last_http_bytes_sent_overhead;
	*(U64 *)beaPushEmpty(&s_vec_http_bytes_received) = http_bytes_received - s_last_http_bytes_received;
	*(U64 *)beaPushEmpty(&s_vec_loaded_bytes) = loaded_bytes - s_last_loaded_bytes;
	*(U64 *)beaPushEmpty(&s_vec_decompressed_bytes) = decompressed_bytes - s_last_decompressed_bytes;

	s_last_pcl_bytes_received = pcl_bytes_received;
	s_last_pcl_bytes_sent = pcl_bytes_sent;
	s_last_pcl_bytes_sent_payload = pcl_bytes_sent_payload;
	s_last_pcl_bytes_sent_uncompressed = pcl_bytes_sent_uncompressed;
	s_last_http_bytes_sent = http_bytes_sent;
	s_last_http_bytes_sent_overhead = http_bytes_sent_overhead;
	s_last_http_bytes_received = http_bytes_received;
	s_last_loaded_bytes = loaded_bytes;
	s_last_decompressed_bytes = decompressed_bytes;
}

// Log server-side patching statistics.
static void reportStats()
{
	U32 now;
	const U32 record_period = 1;			// Collect instantaneous stats this often
	const U32 report_period = 10;			// Report stats this often
	static U32 last_net_stats_record = 0;
	static U32 last_net_stats_report = 0;
	U32 elapsed;
	char *logline = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Only report once per period.
	now = timeSecondsSince2000();
	if (now < last_net_stats_record + record_period)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	last_net_stats_record = now;
	recordStatsSample();

	// Only record once per period.
	if (now < last_net_stats_report + report_period)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	elapsed = last_net_stats_report ? now - last_net_stats_report : 0;
	last_net_stats_report = now;

	// Log statistics.
	estrPrintf(&logline, "elapsed %lu", elapsed);
	statisticsLogStatsU32(&logline, s_vec_connections, "connections");
	statisticsLogStatsU32(&logline, s_vec_http_connections, "http_connections");
	statisticsLogStatsU64(&logline, s_vec_cache_size, "cache_size");
	statisticsLogStatsF32(&logline, s_vec_buffer_usage, "buffer_usage");
	statisticsLogStatsU32(&logline, s_vec_hog_ops, "hog_ops");
	statisticsLogStatsU32(&logline, s_vec_loads_pending, "loads_pending");
	statisticsLogStatsU64(&logline, s_vec_pcl_bytes_received, "pcl_bytes_received");
	statisticsLogStatsU64(&logline, s_vec_pcl_bytes_sent, "pcl_bytes_sent");
	statisticsLogStatsU64(&logline, s_vec_pcl_bytes_sent_payload, "pcl_bytes_sent_payload");
	statisticsLogStatsU64(&logline, s_vec_pcl_bytes_sent_uncompressed, "pcl_bytes_sent_uncompressed");
	statisticsLogStatsU64(&logline, s_vec_http_bytes_sent, "http_bytes_sent");
	statisticsLogStatsU64(&logline, s_vec_http_bytes_sent_overhead, "http_bytes_sent_overhead");
	statisticsLogStatsU64(&logline, s_vec_http_bytes_received, "http_bytes_received");
	statisticsLogStatsU64(&logline, s_vec_loaded_bytes, "loaded_bytes");
	statisticsLogStatsU64(&logline, s_vec_decompressed_bytes, "decompressed_bytes");
	logClientConnectionStats();

	// Reset sample buffers.
	zeroStats();

	// Send log line.
	servLog(LOG_THROUGHPUT, "ServerThroughput", "%s", logline);
	estrDestroy(&logline);

	PERFINFO_AUTO_STOP_FUNC();
}

// Bursty, high-volume NetLink traffic requires a large commandqueue.
int OVERRIDE_LATELINK_comm_commandqueue_size(void)
{
	return (1<<16);
}

// Report relative path usage outside of obvious Gimme/patch data lookups.
// [COR-16585] This is a transitional measure for porting to use a standard filesystem mode, instead of
// filesystem-only, all-absolute.
void OVERRIDE_LATELINK_fileHandleRelativePath(const char *path)
{
	int *recurse;
	STATIC_THREAD_ALLOC(recurse);

	if (s_disable_cor_16585)
		return;

	// Whitelist.
	if (strStartsWith(path, "piggs/")
		|| strStartsWith(path, "server/")
		|| strStartsWith(path, "templates/"))
		return;

	// Suspect relative path usage.
	if (*recurse)
		return;
	++*recurse;
	assertmsgf(0, "[COR-16585] Invalid relative path usage: %s", path);
	//filelog_printf("relative.log", "%s", path);
	--*recurse;
}

void serverLoop(void)
{
	U32 frame_timer = timerAlloc();
	//U32 last_cpu_seconds = 0, cpu_seconds = 0;
	bool step_allowed = true;
	int kbHitTimer = timerAlloc();
	S32 timeClearedCache = time(NULL);
	time_t now = time(NULL);
	struct tm now_tm = {0};
	int last_timestamp_min = 65;

	s_debug_display = false;
	for(;;)
	{
		autoTimerThreadFrameBegin("main");

		ScratchVerifyNoOutstanding();

		s_one_tick_send = 0;
		s_one_tick_messages = 0;

		if(step_allowed)
		{
			F32 frame_time = timerElapsedAndStart(frame_timer);

			PERFINFO_AUTO_START("OncePerFrameStuff", 1);
			DEBUG_DISPLAY_PRINTF("Performing one tick\n");
			commMonitor(s_net_comm);
			commMonitor(commDefault());
			patchHttpTick();
			patchupdateProcess();
			utilitiesLibOncePerFrame(frame_time, 1.0);
			serverLibOncePerFrame();
			patchserverInitPatchFileTick();
			reportStats();
			patchLoadBalanceProcess(sendViewStatusAndFlush);
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_START("Flushing", 1);
			flushLinks();
			if(time(NULL) - timeClearedCache > 10)
			{
				patchfileClearOldCachedFiles();
				timeClearedCache = time(NULL);
			}
			patchserverNotifyMirrors();
			PERFINFO_AUTO_STOP();

			if(g_patchserver_config.bandwidth_config)
				patchserverThrottle();

			PERFINFO_AUTO_START("Pruning", 1);
			now = time(NULL);
			localtime_s(&now_tm, &now);
			if(!patchserverdbAsyncIsRunning() && // Don't prune if we are still pruning or compacting
				g_patchserver_config.prune_config && // Only prune if there is a config
			   now - g_patchserver_config.prune_config->last_prune >= 3600 && // and if it was more than an hour since the last prune
               (now_tm.tm_hour == g_patchserver_config.prune_config->after_time || // and the current hour matches the prune time
			    g_patchserver_config.prune_config->after_time == -1 || // or the prune time is -1 
				g_patchserver_config.prune_config->prune_requested)) // or a prune was manually requested
			{
				bool do_prune = false;
				g_patchserver_config.prune_config->prune_requested = false;
				g_patchserver_config.prune_config->last_prune = now;

				if(g_patchserver_config.prune_config->view_expires)
					do_prune = true;

				FOR_EACH_IN_EARRAY(g_patchserver_config.projects, PatchProject, proj)
					if(proj->is_db && proj->serverdb && (proj->serverdb->keepdays_config.files || proj->serverdb->keepvers_config.files))
					{
						do_prune = true;
						break;
					}
				FOR_EACH_END

				if(do_prune && g_patchserver_config.prune_config->enable)
				{
					loadstart_printf("Running a prune cycle...");
					SERVLOG_PAIRS(LOG_PATCHSERVER_PRUNE, "PruneStart", ("server" "%s", g_patchserver_config.displayName));
					FOR_EACH_IN_EARRAY(g_patchserver_config.serverdbs, PatchServerDb, serverdb)
						// Check if this is an ignored DB
						if(g_patchserver_config.prune_config && eaFindString(&g_patchserver_config.prune_config->ignore_projects, serverdb->name) != -1)
							continue;
						loadstart_printf("Pruning %s...", serverdb->name);
						SERVLOG_PAIRS(LOG_PATCHSERVER_GENERAL, "PruneDatabase", ("filename", "%s", serverdb->name));
						patchpruningPruneAsyncStart(serverdb);
						loadend_printf("");
					FOR_EACH_END
					loadend_printf("");
				}
				
				loadstart_printf("Initiating hogg compaction...");
				patchcompactionCompactHogsAsyncStart();
				loadend_printf("");
			}
			PERFINFO_AUTO_STOP();

			patchserverdbAsyncTick();

			PERFINFO_AUTO_START("Debug", 1);
			if(now_tm.tm_min % 5 == 0 && now_tm.tm_min != last_timestamp_min)
			{
				// Display a timestamp in the console every 5 minutes
				char timebuf[100];
				last_timestamp_min = now_tm.tm_min;
				strftime(SAFESTR(timebuf), "%Y-%m-%d %H:%M:%S", &now_tm);
				printf("Timestamp: %s\n", timebuf);
			}
			s_loopCount++;
			DEBUG_DISPLAY_PRINTF("Sent this tick: %u messages / %u bytes\n\n\n", s_one_tick_messages, s_one_tick_send);
			if(s_debug_display)
				step_allowed = false;
			if (frame_time > s_long_tick)
				s_long_tick = frame_time;

			// Performance counters
			if (PERFINFO_RUN_CONDITIONS)
			{
				ADD_MISC_COUNT(hogTotalPendingOperationCount()*100000, "hogops");
				ADD_MISC_COUNT(fileLoaderLoadsPending()*100000, "loads");
				ADD_MISC_COUNT(patchserverLoadProcessRequestsPending()*100000, "processing");
			}

			PERFINFO_AUTO_STOP();
		}
		else
		{
			//PATCHTODO: Try to avoid disconnects while the server is paused
			Sleep(20);
		}

		if (timerElapsed(kbHitTimer) > 0.2)
		timerStart(kbHitTimer);

		if (s_server_shutting_down)
		{
			if (hogTotalPendingOperationCount()==0) // Anything else to check?
			{
				// Ready to shut down
				patchserverShutDown();
			}
		}

		autoTimerThreadFrameEnd();
	}
	timerFree(frame_timer);
	timerFree(kbHitTimer);
}

void debugRequestShutdown(void) {
	s_server_shutting_down = 1;
}

void debugRequestPrune(void) {
	if(g_patchserver_config.prune_config)
	{
		g_patchserver_config.prune_config->last_prune = 0;
		g_patchserver_config.prune_config->prune_requested = true;
	}
}
void debugRequestFullUpdate(void) {
	patchmirroringNextMirrorForceFull();
}

void debugRequestUpdate(void) {
	patchmirroringRequestMirror();
}

void debugRequestReconnect(void) {
	patchupdateDisconnect();
}

void debugAddAcceptSock(void)
{ 
	int i; 
	for(i=0; i<eaSize(&s_net_listeners); i++)
	{
		addListenAcceptSock(s_net_listeners[i]);
	}
	printf("Adding %u new accept socket%s\n", eaSize(&s_net_listeners), eaSize(&s_net_listeners)>1?"s":"");
	log_printf(LOG_PATCHSERVER_CONNECTIONS, "Adding %u new accept socket%s\n", eaSize(&s_net_listeners), eaSize(&s_net_listeners)>1?"s":"");
}

static ConsoleDebugMenu patchserver_debug_shutdown[] = {
	{'s', "Shutdown PatchServer", debugRequestShutdown},
	{'p', "Initiate prune", debugRequestPrune},
	{'x', "Show transfer stats", cmd_showXferStats},
	{'c', "Show file cache statistics", patchfileCachePrint},
	{'f', "Make the next update a full update", debugRequestFullUpdate},
	{'a', "Add a new accept socket", debugAddAcceptSock},
	{'u', "Force an update from the parent server", debugRequestUpdate},
	{'o', "Abort running prune or compact", patchserverdbAsyncAbort},
	{'r', "Reconnect to parent", debugRequestReconnect},
	{0}
};

// Cleanup handling
static void consoleCloseHandler(DWORD fdwCtrlType)
{
	printf("Shutting down...\n");
	debugRequestShutdown();
}

// We're low on disk space.
static void patchDiskSpaceCheckCallBack(U64 uFreeBytes, void *userdata)
{
	if (!s_server_shutting_down && uFreeBytes < EMERGENCY_SHUTDOWN_DISKSPACE)
	{
		AssertOrAlert("EMERGENCY_SHUTDOWN", "Emergency shutdown commencing: There is insufficient disk space to safely continue operation.  Free bytes: %" FORM_LL "u", uFreeBytes);
		s_server_shutting_down = 1;
	}
}

AUTO_RUN_SECOND;
void initCaches(void)
{
#ifdef _WIN64
	ScratchStackSetThreadSize(8*1024*1024);
	stringCacheSetInitialSize(16*1024*1024);
#else
	ScratchStackSetThreadSize(2*1024*1024);
	stringCacheSetInitialSize(2*1024*1024);
#endif
	hogSyncSetMemoryCap(512*1024*1024);
}

int wmain(int argc, WCHAR** argv_wide)
{
	char containerIdString[9];
	char cwd[MAX_PATH];
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	setDefaultProductionMode(1);

	DO_AUTO_RUNS
	setDefaultAssertMode();
	gbNeverConnectToController = true;

	// Run stay up code instead of normal code if -StayUp was given
	if (StayUp(argc, argv, NULL, NULL, NULL, NULL))
		return 0;

	SetAppGlobalType(GLOBALTYPE_PATCHSERVER);
	FolderCacheChooseMode();
	fileSetFixedCwd(true);  // Makes mutex locking go faster
	gimmeDLLDisable(1);

	logSetDir("PatchServer");
	logAutoRotateLogFiles(1);

	fileGetcwd(SAFESTR(cwd));
	BeginPeriodicFreeDiskSpaceCheck(cwd[0], 60, 600, 3, EMERGENCY_SHUTDOWN_DISKSPACE, patchDiskSpaceCheckCallBack, NULL);

	loadstart_printf("");

	if (!g_no_config)
	{
		loadstart_printf("Load early config");
		loadEarlyConfig();
		loadend_printf("");
	}

	loadstart_printf("First Misc. Init...");
	setSafeCloseAction(consoleCloseHandler);
	useSafeCloseHandler();
	disableConsoleCloseButton();
	memCheckInit();
	autoTimerInit();
	sharedMemorySetMode(SMM_DISABLED);
	loadstart_printf("Setup logging...");
	loadend_printf("");
	utilitiesLibStartup();
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'P', 0x2f9f2f);
	loadend_printf("");

	// This forces textparser to not read the entirety of files in RAM before parsing.
	gbFixedTokenizerBufferSize = true;

	loadstart_printf("Parsing command line...\n");
	printf("%s\n", argv[0]);

	serverLibStartup(argc, argv);
	gServerLibState.bAllowErrorDialog = false;
	if (isProductionMode())
		ErrorfPushCallback(serverErrorAlertCallbackProgrammer, NULL);

	// We should never get this far if -NoConfig has been given.
	if (g_no_config)
		FatalErrorf("-NoConfig is only valid with exiting command-line options.");

	// If -verify is set, -verify_all_checkins is the default for the next update.
	if (g_sync_verify_all_checkins == -1)
		g_sync_verify_all_checkins = s_verify_hoggs;

	printf(        "                          ");
	loadend_printf("");

	// Print version before main load.
	printf("Patch Server Version: %s\n", patchserverVersion());

	// these have load timers in them
	patchfileCacheInit();
	loadConfig(false);
	if (g_patchserver_config.displayName)
		gServerLibState.containerID = hashString(g_patchserver_config.displayName, false) % 0x7ffffffd + 2;
	if (s_start_down == 1)
		g_patchserver_config.reportdown = true;
	else if (s_start_down == 0)
		g_patchserver_config.reportdown = false;

	// Keep track of child servers.
	s_child_servers = stashTableCreateWithStringKeys(20, StashDefault);
	resRegisterDictionaryForStashTable("Child Servers", RESCATEGORY_OTHER, 0, s_child_servers, parse_PatchChildServerData);

	// Log version.
	sprintf_s(SAFESTR(containerIdString), "%d", gServerLibState.containerID);
	servLogWithPairs(LOG_PATCHSERVER_GENERAL, "Version", "server", g_patchserver_config.displayName, "containerId", containerIdString,
		"version", patchserverVersion(), NULL);

	execMergeProcess(true);

	loadstart_printf("Deleting unfinished checkins...");
	if(dirExists("./pending_checkins"))
		rmdirtreeEx("./pending_checkins", 1);
	loadend_printf("");

	loadstart_printf("Last Misc. Init...\n");
	TimedCallback_Add(updateTitleBar, NULL, 0.5);
	if(!g_patchserver_config.parent.server)
		TimedCallback_Add(checkExecMergeProcess, argv[0], SECONDS_BETWEEN_MERGES);
	patchNetInit();
	patchTrackingInit();

	eaPush(GetDefaultConsoleDebugMenu(), patchserver_debug_shutdown);
	printf(        "                     ");
	loadend_printf("");

	loadend_printf("Patchserver ready");
	printf("\n\n");

	log_printf(LOG_PATCHSERVER_CONNECTIONS,"PatchServer running");
	serverLoop();
	EXCEPTION_HANDLER_END 
	exit(0);
}

#include "patchserver_h_ast.c"
