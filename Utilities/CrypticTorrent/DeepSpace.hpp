#ifndef DEEPSPACE_HPP
#define DEEPSPACE_HPP

// Set our basic reporting information.
void DeepSpaceInitInfo(const char *product, const char *info_hash32);

// Report that we've started up.
void DeepSpaceReportStartup();

// Report an exit.
void DeepSpaceSyncReportExit(bool started_download, bool got_bytes, int download_percent, bool download_finished, bool installer_ran);

// Big blob of data for DeepSpaceSyncReportPeriodicDownload.
struct DeepSpacePeriodicTorrentBlob {

	// Other information
	int using_fallback;
	int ever_used_fallback;
	unsigned __int64 total_web_download;
	unsigned __int64 total_cryptic_download;

	// libtorrent information
	int paused;
	float progress;
	unsigned __int64 total_download;
	unsigned __int64 total_upload;
	unsigned __int64 total_payload_download;
	unsigned __int64 total_payload_upload;
	unsigned __int64 total_failed_bytes;
	unsigned __int64 total_redundant_bytes;
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
	unsigned __int64 total_done;
	unsigned __int64 total_wanted_done;
	unsigned __int64 total_wanted;
	float distributed_copies;
	int block_size;
	int num_uploads;
	int num_connections;
	int uploads_limit;
	int connections_limit;
	unsigned __int64 all_time_upload;
	unsigned __int64 all_time_download;
	int active_time;
	int seeding_time;
	int seed_rank;
	int last_scrape;
	int has_incoming;
};

// Periodic download status.
void DeepSpaceSyncReportPeriodicDownload(bool final, const struct DeepSpacePeriodicTorrentBlob &blob);

#endif  // DEEPSPACE_HPP
