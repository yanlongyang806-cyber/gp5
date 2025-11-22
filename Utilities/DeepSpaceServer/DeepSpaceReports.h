/*
 * DeepSpaceServer Reporting
 */

#ifndef CRYPTIC_DEEPSPACEREPORTS_H
#define CRYPTIC_DEEPSPACEREPORTS_H

typedef struct StashTableImp *StashTable;

// Parameters for a report run
typedef struct ReportParameters
{
	// Time parameters
	U32 begin_time;
	U32 end_time;
	U32 granularity;			// Finest granularity of data, eg hours
	U32 super_granularity;		// Coarser granularity of unique data, eg days (not merely sum because of uniques)

	// Product-related parameters
	char *product;
	bool use_info_hash;
	U32 info_hash[8];

	bool breakdowns;			// If true, do extended breakdowns of results.

} ReportParameters;

// Breakdown for some stratum of machines
typedef struct ReportBreakdown
{
	U32 started;
	U32 finished;
	U32 installed;
	U32 resumed;
	U32 canceled;
	U32 activity;
} ReportBreakdown;

// Time interval in ReportTimeSeries
typedef struct ReportResults
{
	// Time range
	U32 begin_time;		// Beginning of interval (inclusive)
	U32 end_time;		// Ending of interval (exclusive)

	// Basic discrete statistics
	U32 started;
	U32 finished;
	U32 installed;
	U32 resumed;
	U32 canceled;

	// Statistics relating to the basic classification of the UniqueMachine
	// These aren't necessarily useful to business or ops, but they're good to get an idea of what's
	// going on with the data mining.
	U32 examined;				// We examined this UniqueMachine at all
	U32 activity;				// Interval has some activity (not including a deduced cancel)
	U32 previously_finished;	// Early out: This UniqueMachine has already finished.
	U32 non_interesting;		// Early out: Nothing happened during this interval for this machine
	U32 accumulated;			// Machine was ultimately accumulated (not early-outed for one reason or another)

	// Extended statistics
	U32 short_spread;
	U32 ever_used_fallback;
	U64 total_download;
	U64 total_web_download;
	U64 total_cryptic_download;

} ReportResults;

// Results of a report run
typedef struct ReportTimeSeries
{
	// Time series itself
	ReportResults *pResults;
	int periods;

	// Super-series
	ReportResults *pResultsSuper;
	int super_periods;

	// Lists of statistics.
	F32 *download_rate;
	F32 *dropout_percent;
	F32 *total_download_time;

	// Breakdowns
	StashTable country_breakdown;	// Country (char *) -> ReportBreakdown *
	StashTable lcid_breakdown;		// LCID (int) --> ReportBreakdown *
} ReportTimeSeries;

// Call this function periodically to process any pending report runs.
void ProcessReports(void);

#endif  // CRYPTIC_DEEPSPACEREPORTS_H
