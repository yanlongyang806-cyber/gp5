/*
 * DeepSpaceServer
 */

#ifndef CRYPTIC_DEEPSPACESERVER_H
#define CRYPTIC_DEEPSPACESERVER_H

typedef struct StashTableImp *StashTable;

AUTO_ENUM;
typedef enum DsnClientType {
	DsnClientType_Unknown = 1,
	DsnClientType_CrypticTorrent,
} DsnClientType;

AUTO_STRUCT;
typedef struct BasicEvent {
	U32 when;					// From origin server
	U64 packet_id;				// From client (randomly generated); this is here for sorting purposes
	U32 uIp;					// From origin server
	int origin_server;			// From origin server

	// All of the following are from the client
	DsnClientType client_type;
	U32 info_hash[8];
	int lcid;
	int client_version;
	const char *product;			AST(POOL_STRING)
} BasicEvent;

AUTO_STRUCT;
typedef struct StartedEvents {
	BasicEvent event;
} StartedEvents;

AUTO_STRUCT;
typedef struct ExitEvents {
	BasicEvent event;

	bool bStartedDownload;
	bool bGotBytes;
	int iDownloadPercent;
	bool bDownloadFinished;
	bool bInstallerRan;
} ExitEvents;

// Big blob of data for DeepSpaceSyncReportPeriodicDownload.
AUTO_STRUCT;
typedef struct PeriodicTorrentBlob {

	int using_fallback;
	int ever_used_fallback;
	U64 total_web_download;
	U64 total_cryptic_download;

	int paused;
	float progress;
	U64 total_download;
	U64 total_upload;
	U64 total_payload_download;
	U64 total_payload_upload;
	U64 total_failed_bytes;
	U64 total_redundant_bytes;
	float download_rate;
	float upload_rate;
	float download_payload_rate;
	float upload_payload_rate;
	int num_seeds;
	int num_peers;
	int num_complete;
	int num_incomplete;
	int list_seeds;
	int list_peers;
	int connect_candidates;
	int num_pieces;
	U64 total_done;
	U64 total_wanted_done;
	U64 total_wanted;
	float distributed_copies;
	int block_size;
	int num_uploads;
	int num_connections;
	int uploads_limit;
	int connections_limit;
	U64 all_time_upload;
	U64 all_time_download;
	int active_time;
	int seeding_time;
	int seed_rank;
	int last_scrape;
	int has_incoming;
} PeriodicTorrentBlob;

AUTO_STRUCT;
typedef struct PeriodicEvents {
	BasicEvent event;

	PeriodicTorrentBlob blob;
} PeriodicEvents;

AUTO_STRUCT;
typedef struct UniqueMachine {
	U32 machine_id[8];
	StartedEvents *started_events;		AST(BLOCK_EARRAY)
	ExitEvents *exit_events;			AST(BLOCK_EARRAY)
	PeriodicEvents *periodic_events;	AST(BLOCK_EARRAY)
} UniqueMachine;

AUTO_STRUCT;
typedef struct DSNDatabase {
	UniqueMachine *machines;			AST(BLOCK_EARRAY)	// List of all machines we know about
	UniqueMachine *old_machines;		NO_AST				// Old pointer, to see if we need to rehash
} DSNDatabase;

// All data
extern DSNDatabase sDatabase;

// Look up table of machines in the database
extern StashTable machine_stash;  // machine_id[8] (fixed) -> index (int)

#endif  // CRYPTIC_DEEPSPACESERVER_H
