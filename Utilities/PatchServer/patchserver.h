#ifndef _PATCHSERVER_H
#define _PATCHSERVER_H

#include "TextParserEnums.h"
#include "ReferenceSystem.h"

typedef struct PatchProject PatchProject;
typedef struct PatchServerDb PatchServerDb;
typedef struct PatchFile PatchFile;
typedef struct FileVersion FileVersion;
typedef struct StashTableImp* StashTable;
typedef struct WaitingRequest WaitingRequest;
typedef struct NetLink NetLink;
typedef struct CheckinFile CheckinFile;
typedef struct ProjectView ProjectView;

#define CRYPTIC_PATCHSERVER_VERSION_SHORT "1.8.0"
#define CRYPTIC_PATCHSERVER_VERSION_TYPE "(testing)"

#define ECHO_log_printfS 1

AUTO_STRUCT AST_FIXUPFUNC(fixupAllowIp);
typedef struct AllowIp
{
	char	*ip_str; AST(STRUCTPARAM)
	U8		ip_bytes[4]; NO_AST
	bool	ip_match[4]; NO_AST
} AllowIp;

AUTO_STRUCT;
typedef struct AddressAndPort
{
	char	*server;	AST(STRUCTPARAM)
	int		port;		AST(STRUCTPARAM)
} AddressAndPort;

AUTO_STRUCT;
typedef struct RedirectAlternatives
{
	AddressAndPort *address; AST(STRUCTPARAM)
	bool disabled; 
} RedirectAlternatives;

AUTO_STRUCT;
typedef struct ServerRedirect
{
	AddressAndPort direct_to;		AST(STRUCTPARAM)
	AllowIp	**ips;					AST(NAME(Ip, AllowIp))
	RedirectAlternatives **alts;	AST(NAME(Alternative))
	U32 alt_index;					NO_AST
	bool disabled;
	AllowIp **exclude;				AST(NAME(Exclude, DenyIp))
	const char **host_match;		AST(NAME(HostMatch))
	const char **referrer_match;	AST(NAME(ReferrerMatch))
	const char **token_match;		AST(NAME(TokenMatch))
	const char **no_token_match;	AST(NAME(NoTokenMatch))
} ServerRedirect;

AUTO_STRUCT;
typedef struct AutoUpdateFile
{
	char		*token;			AST(STRUCTPARAM)
	AllowIp		**ips;			AST(NAME(AllowIp))
	AllowIp		**deny_ips;		AST(NAME(DenyIp))
} AutoUpdateFile;

AUTO_STRUCT;
typedef struct BranchToMirror {
	S32					branch;					AST(NAME("Branch"), STRUCTPARAM)
	S32					startAtRevision;		AST(NAME("StartAtRevision") STRUCTPARAM SUBTABLE(parse_BranchToMirror_startAtRevision))
	S32					firstBranchRevAfterStart;
} BranchToMirror;

AUTO_STRUCT;
typedef struct MirrorConfig {
	char*				db;						AST(NAME("Db"), STRUCTPARAM)
	BranchToMirror**	branchToMirror;			AST(NAME("BranchToMirror"))
} MirrorConfig;

AUTO_STRUCT;
typedef struct InternalIPs {
	AllowIp		**ips;		AST(NAME(Ip, AllowIp))
	AllowIp		**deny_ips;	AST(NAME(DenyIp))
} InternalIPs;

AUTO_STRUCT;
typedef struct ViewExpires {
	int public;
	int internal;
} ViewExpires;

AUTO_STRUCT;
typedef struct PruneConfig {
	ViewExpires*		view_expires;			AST(NAME("ViewExpires"))
	int					after_time;				AST(NAME("After") SUBTABLE(parse_PruneConfig_value))
	time_t				last_prune;				NO_AST
	bool				prune_requested;		NO_AST
	char**				ignore_projects;		AST(NAME("Ignore"))
	bool				enable;					AST(NAME("Enable"))
} PruneConfig;

AUTO_STRUCT;
typedef struct BandwidthConfig {
	U32					total;					AST(NAME("Total") DEFAULT(1024 * 1024 * 1024 / 8))
	U32					per_user;				AST(NAME("PerUser") DEFAULT(U32_MAX))
	U32					time_slice;				AST(NAME("TimeSlice") DEFAULT(1000))
} BandwidthConfig;

AUTO_STRUCT;
typedef struct MemoryLimits {
	U32					patchfile;				AST(NAME("PatchFile"))
	U32					checkin;				AST(NAME("Checkin") DEFAULT(1))
} MemoryLimits;

AUTO_STRUCT;
typedef struct AutoupConfigWeightedRevision
{
	float weight;	AST(STRUCTPARAM DEFAULT(1))
	int rev;		AST(NAME("Rev") DEFAULT(INT_MAX))
} AutoupConfigWeightedRevision;

AUTO_STRUCT;
typedef struct AutoupConfigRule
{
	int					id;								AST(NAME("Id"))

	bool				disabled;						AST(NAME("Disabled"))

	STRING_EARRAY		tokens;							AST(NAME("Tokens"))
	STRING_EARRAY		categories;						AST(NAME("Categories"))
	AllowIp**			ips;							AST(NAME("IP", "AllowIp"))
	AllowIp**			deny_ips;						AST(NAME("DenyIp"))

	EARRAY_OF(AutoupConfigWeightedRevision) autoup_rev;	AST(NAME("WeightedRev"))
} AutoupConfigRule;

// List of Autoupdate config rules
AUTO_STRUCT;
typedef struct DynamicAutoupConfig {
	int last_id;								AST(NAME("LastId"))
	EARRAY_OF(AutoupConfigRule) autoup_rules;	AST(NAME("AutoupRule"))
} DynamicAutoupConfig;

AUTO_STRUCT;
typedef struct HttpConfigWeightedInfo
{
	char *info;									AST(STRUCTPARAM)
	float weight;								AST(STRUCTPARAM DEFAULT(1))
	bool load_balancer;							AST(STRUCTPARAM)					// True if this is a load balancer
} HttpConfigWeightedInfo;

AUTO_STRUCT;
typedef struct HttpConfigNamedView
{
	bool				disabled;				AST(NAME("Disabled"))
	int					id;						AST(NAME("Id"))
	STRING_EARRAY		categories;				AST(NAME("Categories"))
	STRING_EARRAY		tokens;					AST(NAME("Tokens"))
	AllowIp**			ips;					AST(NAME("AllowIp"))
	AllowIp**			deny_ips;				AST(NAME("DenyIp"))
	STRING_EARRAY		project;				AST(NAME("Project"))
	char*				name;					AST(NAME("Name"))
	EARRAY_OF(HttpConfigWeightedInfo) http_info; AST(NAME("HttpInfo"))
} HttpConfigNamedView;

AUTO_STRUCT;
typedef struct HttpConfigBranch
{
	bool				disabled;				AST(NAME("Disabled"))
	int					id;						AST(NAME("Id"))
	STRING_EARRAY		categories;				AST(NAME("Categories"))
	STRING_EARRAY		tokens;					AST(NAME("Tokens"))
	AllowIp**			ips;					AST(NAME("AllowIp"))
	AllowIp**			deny_ips;				AST(NAME("DenyIp"))
	STRING_EARRAY		project;				AST(NAME("Project"))
	INT_EARRAY			branch;					AST(NAME("Branch"))
	int					min_rev;				AST(NAME("MinRev") DEFAULT(0))
	int					max_rev;				AST(NAME("MaxRev") DEFAULT(INT_MAX))
	EARRAY_OF(HttpConfigWeightedInfo) http_info; AST(NAME("HttpInfo"))
} HttpConfigBranch;

// List of HTTP locations suitable for specific views.
AUTO_STRUCT;
typedef struct HttpConfig {
	bool allow_http;							AST(NAME("AllowHttp") DEFAULT(true))
	EARRAY_OF(HttpConfigNamedView) namedviews;	AST(NAME("NamedView"))
	EARRAY_OF(HttpConfigBranch) branches;		AST(NAME("Branch"))
} HttpConfig;

// List of HTTP locations suitable for specific views.
AUTO_STRUCT;
typedef struct DynamicHttpConfig {
	int last_id;								AST(NAME("LastId"))
	EARRAY_OF(HttpConfigNamedView) namedviews;	AST(NAME("NamedView"))
	EARRAY_OF(HttpConfigBranch) branches;		AST(NAME("Branch"))
} DynamicHttpConfig;

// List of categories to include and exclude for IsCompletelySynced requests
AUTO_STRUCT;
typedef struct SyncConfig {
	STRING_EARRAY include;						AST(NAME("Incl", "Include"))
	STRING_EARRAY exclude;						AST(NAME("Exclude"))
} SyncConfig;

AUTO_STRUCT;
typedef struct ServerConfig
{
	char*				displayName;			AST(NAME("ServerDisplayName") KEY)
	AddressAndPort		parent;					AST(NAME("ParentServer"))
	int					parentTimeout;			AST(NAME("ParentServerTimeout"))
	char*				serverCategory;			AST(NAME("ServerCategory"))
	bool				low_bandwidth;			AST(NAME("LowBandwidthMirroring") BOOLFLAG DEFAULT(false))
	char*				client_host;			AST(NAME("ClientHost"))
	int*				client_ports;			AST(NAME("ClientPort"))
	char*				http_host;				AST(NAME("HttpHost"))
	int					http_port;				AST(NAME("HttpPort"))
	int					monitor_port;			AST(NAME("ServerMonitorPort"))
	AllowIp**			http_allow;				AST(NAME("AllowHttp"))
	AllowIp**			http_deny;				AST(NAME("DenyHttp"))
	ServerRedirect**	redirects;				AST(NAME("RedirectTo"))
	PatchServerDb**		serverdbs;				AST(NAME("Database") STRUCT(parse_config_PatchServerDb))
	PatchProject**		projects;				AST(NAME("Project") STRUCT(parse_config_PatchProject))
	StashTable			project_stash;			NO_AST
	AutoUpdateFile**	autoupdates;			AST(NAME("AutoUpdate"))
	PatchServerDb*		autoupdatedb;			AST(NAME("AutoUpdateDatabase") STRUCT(parse_config_PatchServerDb))
	StashTable			autoupdate_stash;		NO_AST
	S32					printFileCacheUpdates;	AST(NAME("PrintFileCacheUpdates"))
	char*				filename;				AST(CURRENTFILE)
	U32					notifyMirrorsPeriod;	AST(NAME("NotifyMirrorsPeriod"))
	MirrorConfig**		mirrorConfig;			AST(NAME("MirrorConfig"))
	InternalIPs*		internal;				AST(NAME("InternalIPs"))
	char*				log_server;				AST(NAME("LogServer"))
	PruneConfig*		prune_config;			AST(NAME("PruneConfig"))
	U32					full_mirror_every;		AST(NAME("FullMirrorEvery"))
	BandwidthConfig*	bandwidth_config;		AST(NAME("BandwidthConfig"))
	U32					max_connections;		AST(NAME("MaxConnections") DEFAULT(500000))  // Default to 500,000
	bool				locked;					AST(NAME("Locked"))
	bool				reportdown;				AST(NAME("ReportAsDown"))
	MemoryLimits		mem_limits;				AST(NAME("MemoryLimits"))
	U32					max_net_bytes;			AST(NAME("SyncMaxNetBytes"))
	HttpConfig			*http_config;			AST(NAME("HttpConfig"))
	DynamicHttpConfig	*dynamic_http_config;
	DynamicAutoupConfig	*dynamic_autoup_config;
	SyncConfig			*sync_config;			AST(NAME("PatchTrackingConfig"))
	U32					slipstream_threshold;	AST(NAME("SlipstreamThreshold") DEFAULT(PCL_SLIPSTREAM_THRESHOLD))
	char**				automaton_author;		AST(NAME("AutomatonAuthor"))
} ServerConfig;

typedef struct PatchClientLink PatchClientLink;

// Data for attached child Patch Servers
AUTO_STRUCT;
typedef struct PatchChildServerData
{
	char *name;
	char *category;
	char *parent;
	char *last_update;
	PatchClientLink *client;					NO_AST
} PatchChildServerData;

AUTO_STRUCT;
typedef struct PatchClientLink
{
	PatchProject	*project;
	char			sandbox[MAX_PATH];
	char			author[64];
	int				branch;
	int				rev;
	int				incr_from; // TODO: it's probably safer to remove this and have patchdb.h always look it up
	int				patcher_version_fake;
	int				patcher_version;
	char			*autoupdate_token;
	NetLink			*link; NO_AST
	char			ipstr[MAX_PATH];
	REF_TO(ProjectView) refto_view; NO_AST
	bool			update_pending;
	bool			notify_me;
	bool			once_requested_notify;
	U32				hasProtocolError : 1;
	char			view_name[100];

	CheckinFile**	checkin_files; NO_AST
	U32				checkinsInMemoryCount;
	U64				checkinsInMemoryBytes;
	U64				UID;
	U64				start_time;
	U32				bucket;
	char			*prefix;
	char			*host; // server name the client thinks it connected to initially
	char			*referrer; // server that last referred the client
	PatchFile		*special_manifest_patch; NO_AST // the last full manifest requested by this client
	EARRAY_OF(PatchChildServerData) servers; // Server names associated with this client.
} PatchClientLink;

extern ServerConfig g_patchserver_config;

extern int g_sync_verify_all_checkins;

extern bool s_disable_cor_16585;

void patchserverFixupDbHierarchy(void); // done separately so we don't have to force ordering on the user

void patchserverNotifyMirrors(void);

// Get human-readable Patch Server release version string.
const char *patchserverVersion(void);

TextParserResult fixupAllowIp(AllowIp *allowip, enumTextParserFixupType eFixupType, void *pExtraData);
bool allowipCheck(AllowIp **allowips, U32 ip);
bool checkAllowDeny(AllowIp **allowips, AllowIp **denyips, U32 ip);

PatchProject* patchserverFindProjectChecked(const char *name, U32 ip);
PatchProject* patchserverFindProject(const char *name);
PatchProject* patchserverFindOrAddProject(const char *name);

PatchFile* patchserverGetFile(PatchProject *project, char *fname, int branch, int rev, char *sandbox, int incr_from);
FileVersion* patchserverGetAutoUpdateFile(const char *token, U32 ip);
const char* patchserverChildStatus(void);
void patchserverGetClients(PatchClientLink ***clients);

void patchserverGetTitleBarText(char* outBuffer,
								S32 outBufferSize);

#define patchserverLog(objname, action, ...) objLog(LOG_PATCHSERVER_GENERAL, 0, 0, 0, objname, NULL, g_patchserver_config.displayName, action, NULL,  __VA_ARGS__);

void heartbeatReset(const char *name);
F32 heartbeatTime(const char *name);

// for iterateAllPatchLinks
typedef int LinkCallback2(NetLink* link, S32 index, void *link_user_data, void *func_data);

void iterateAllPatchLinks(LinkCallback2 callback, void *userdata);

PatchFile* findPatchFileEx(PatchProject *proj, char *fname, int branch, int rev, char *sandbox, int incr_from, char *prefix, PatchClientLink *client);
PatchFile *findPatchFile(char *fname, PatchClientLink *client);

// Send update status to the master server.
void patchupdateSendUpdateStatus(const char *status);

void patchserverAutoupConfigAddRule(AutoupConfigRule *rule);

// Delete a dynamic Autoupdate rule.  Return false if not found.
bool patchserverAutoupConfigDeleteRule(int id);

// Write out the dynamic Autoupdate patching configuration.
void patchserverSaveDynamicAutoupConfig();

// Write out the dynamic HTTP patching configuration.
void patchserverSaveDynamicHttpConfig(void);

// Add a dynamic named view rule.
void patchserverHttpConfigAddNamedView(HttpConfigNamedView *rule);

// Add a dynamic branch rule.
void patchserverHttpConfigAddBranchRule(HttpConfigBranch *rule);

// Delete a dynamic named view rule.  Return false if not found.
bool patchserverHttpConfigDeleteNamedView(int id);

// Delete a dynamic branch rule.  Return false if not found.
bool patchserverHttpConfigDeleteBranchRule(int id);

// Send an activation notification for each child.
void patchserverResendChildActivations(void);

// Prepare to send notifications to children.
void patchserverMirrorNotifyDirty(void);

// Get number of client connections.
U32 patchserverConnections(void);

#endif
