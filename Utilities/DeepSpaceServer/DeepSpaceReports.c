/*
 * DeepSpaceServer Reporting
 */

#include "BlockEarray.h"
#include "clearsilver.h"
#include "DeepSpaceHtml.h"
#include "DeepSpaceServer.h"
#include "DeepSpaceReports.h"
#include "DeepSpaceUtils.h"
#include "earray.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "timing.h"
#include "utils.h"

// Number of seconds to run before timing out report process job
#define REPORT_TICK_PROCESS_TIMEOUT .02f  // 20 ms

// Number of seconds until we consider a download to be canceled
#define REPORT_CANCEL_TIMEOUT 60*60  // one hour

// For adjusted completion percent, ignore downloads with a total time spread of less than this many seconds
#define ADJUSTED_COMPLETION_THRESHOLD 60

typedef void (*pfnReportCallback)(ReportTimeSeries *results, const ReportParameters *parameters, void *userdata);

// One instance of a currently-running report
typedef struct RunningReport {
	ReportTimeSeries results;				// Results of report run so far
	ReportParameters parameters;			// Parameters of report run
	pfnReportCallback callback;				// Callback to run once report is complete
	void *userdata;							// Userdata for callback
	int current_machine;					// Current progress
} RunningReport;

// All currently-running reports.
static RunningReport **sppRunningReports = NULL;

// Iterate over all events of a type.
#define WALK_EVENTS(EVENTS, TYPE, EVENT)										\
	do {																		\
		TYPE *walk_events_ptr_##EVENT;											\
		for (walk_events_ptr_##EVENT = (EVENTS);								\
			(walk_events_ptr_##EVENT) < (EVENTS) + beaSize(&(EVENTS));			\
			++walk_events_ptr_##EVENT)											\
		{																		\
			TYPE *EVENT = walk_events_ptr_##EVENT;								\
			{
#define WALK_EVENTS_END }}} while (0)

typedef bool (*process_event)(BasicEvent *event, void *userdata);

// Walk all events of all types for a UniqueMachine.
static void WalkAllEvents(const UniqueMachine *machine, process_event process, void *userdata)
{
	bool proceed;

	WALK_EVENTS(machine->started_events, StartedEvents, event)
	{
		proceed = process(&event->event, userdata);
		if (!proceed)
			return;
	}
	WALK_EVENTS_END;
	WALK_EVENTS(machine->exit_events, ExitEvents, event)
	{
		proceed = process(&event->event, userdata);
		if (!proceed)
			return;
	}
	WALK_EVENTS_END;
	WALK_EVENTS(machine->periodic_events, PeriodicEvents, event)
	{
		proceed = process(&event->event, userdata);
		if (!proceed)
			return;
	}
	WALK_EVENTS_END;
}

// Check if this event meets all of our criteria.
static bool reportFilterEventByParameters(const BasicEvent *event, const ReportParameters *parameters)
{
	// Check product.
	if (parameters->product && stricmp_safe(event->product, parameters->product))
		return false;

	// Check info hash.
	if (parameters->use_info_hash)
		if (memcmp(event->info_hash, parameters->info_hash, sizeof(event->info_hash)))
			return false;

	// Passed all of the above, so it passes.
	return true;
}

// Check if this event is in our time period.
static bool reportFilterEventByPeriod(const BasicEvent *event, const ReportResults *period)
{
	return event->when >= period->begin_time && event->when < period->end_time;
}

// Local variables for ProcessReportMachineTimeInitiatedDownloads().
typedef struct ProcessEnv {
	// Parameters
	ReportResults *period;
	ReportTimeSeries *results;
	const UniqueMachine *machine;
	const ReportParameters *parameters;

	// Local variables
	U32 absolute_first_activity;	// The first indication of any relevant activity at all

	// First step: figure out if there's good stuff to do here
	bool activity;					// There is some activity on this stream within the reporting window.
	U32 first_activity;				// The timestamp of the first indication of activity in the window
	BasicEvent *any_event;			// arbitrary event

	// Second step: get overall status
	bool started;					// If we've started within this window; note: this includes resumes
	bool resumed;					// true if it looks like we've resumed
	bool activity_last_period_near_end; // For cancellation detection: see reportCheckCanceledInPeriod()
	bool activity_this_period_near_end; // For cancellation detection: see reportCheckCanceledInPeriod()
	bool canceled;					// true if it looks like we've canceled

	// Third step: more statistics
	BasicEvent *finished_event;		// Set by main function: the reference finished event
	BasicEvent *start_event;		// Reference absolute start event: can be used to determine absolute overall length of download
	U32 spread_earliest;			// Earliest event timestamp for this filter
	U32 spread_latest;				// Latest event timestamp for this filter
} ProcessEnv;

// Find out whether there's any activity at all in the period, and if so, if the download started in this period.
static bool reportFindActivity(BasicEvent *event, void *userdata)
{
	ProcessEnv *env = userdata;
	
	// Reduce consideration only to events that meet our parameters, excepting time range.
	if (!reportFilterEventByParameters(event, env->parameters))
		return true;

	// Find the last incidence of activity in the period.
	if (!env->absolute_first_activity)
		env->absolute_first_activity = event->when;
	else
		env->absolute_first_activity = MIN(env->absolute_first_activity, event->when);
	
	// Only consider it further if it's in our period.
	if (!reportFilterEventByPeriod(event, env->period))
		return true;

	// Find the first incidence of activity in the period.
	env->activity = true;
	env->any_event = event;
	if (!env->first_activity)
		env->first_activity = event->when;
	else
		env->first_activity = MIN(env->first_activity, event->when);

	return true;
}

// Check if we've started in this period because there was no recent previous activity.
// env->started is initialized to true
static bool reportCheckStarted(BasicEvent *event, void *userdata)
{
	ProcessEnv *env = userdata;

	// Reduce consideration only to events that meet our parameters, excepting time range.
	if (!reportFilterEventByParameters(event, env->parameters))
		return true;

	// If we had some activity within REPORT_CANCEL_TIMEOUT, this isn't a start.
	if (event->when < env->first_activity && env->first_activity - event->when < REPORT_CANCEL_TIMEOUT)
	{
		env->started = false;
		return false;
	}

	return true;
}

// Check if the fact that there's no activity in this period means that we should cancel.
static bool reportCheckCanceledInPeriod(BasicEvent *event, void *userdata)
{
	ProcessEnv *env = userdata;

	// Reduce consideration only to events that meet our parameters, excepting time range.
	if (!reportFilterEventByParameters(event, env->parameters))
		return true;

	// Two cases are cancellation:
	//   1) There is no activity in this period, but there was some in the last, within REPORT_CANCEL_TIMEOUT of the end (which is why it wasn't canceled then)
	//   2) There is activity in this period, but not within REPORT_CANCEL_TIMEOUT of the end
	//        Note that if it turns out that we actually finished, rather than canceling, we'll clear this later in the step.
	if (event->when < env->period->begin_time && env->period->begin_time - event->when < REPORT_CANCEL_TIMEOUT)
	{
		env->activity_last_period_near_end = true;
		env->any_event = event;
	}
	if (reportFilterEventByPeriod(event, env->period) && env->period->end_time - event->when < REPORT_CANCEL_TIMEOUT)
	{
		env->activity_this_period_near_end = true;
		env->any_event = event;
		return false;
	}

	return true;
}

// Find the earliest plausible start event.
static bool reportFindEarliestMatchingInfo(BasicEvent *event, void *userdata)
{
	ProcessEnv *env = userdata;

	// Reduce consideration only to events that meet our parameters, excepting time range.
	if (!reportFilterEventByParameters(event, env->parameters))
		return true;

	// Only look for downloads from the same info hash.
	if (memcmp(event->info_hash, env->finished_event->info_hash, sizeof(event->info_hash)))
		return true;

	// Make sure we save at least one relevant event.
	env->any_event = event;

	// Only look at older stuff for this.
	if (event->when > env->finished_event->when)
		return true;

	// Save this, if it's the earliest.
	if (!env->start_event || event->when < env->start_event->when)
		env->start_event = event;

	return true;
}

// Get the total spread for these filter parameters.
static bool reportGetSpread(BasicEvent *event, void *userdata)
{
	ProcessEnv *env = userdata;

	// Reduce consideration only to events that meet our parameters, excepting time range.
	if (!reportFilterEventByParameters(event, env->parameters))
		return true;

	// Get the min and max values.
	env->spread_earliest = MIN(env->spread_earliest, event->when);
	env->spread_latest  = MAX(env->spread_latest, event->when);

	return true;
}

// Extract statistics from this machine for this period, and accumulate them into the results.
static void ProcessReportMachineTimePeriod(ReportResults *period, ReportTimeSeries *results, const UniqueMachine *machine,
	const ReportParameters *parameters)
{
	ProcessEnv env = {0};
	bool early_abort = false;
	bool finished = false;
	bool installed = false;
	float dropout_percent = 0;
	U32 total_info_hash_time = 0;			// Result: absolute overall download time
	double download_rate_sample_sum = 0;
	int download_rate_sample_num = 0;
	float average_download_rate = 0;		// Result: Average download rate
	bool ever_used_fallback = false;		// Result: If we've ever used the fallback
	U64 total_download = 0;					// Result: Total amount downloaded from any source
	U64 total_web_download = 0;				// Result: Total amount downloaded from HTTP seeding
	U64 total_cryptic_download = 0;			// Result: total amount downloaded from Cryptic

	// Step 0: Don't do statistics gathering if this machine is already finished.

	// Exception: If we didn't record it being installed before, but it's being installed now, report that.

	++period->examined;

	// Check if the download was ever previously finished, and if so, ignore this machine for this period.
	WALK_EVENTS(machine->exit_events, ExitEvents, event)
	{
		if (!reportFilterEventByParameters(&event->event, parameters))
			continue;
		if (event->event.when >= period->begin_time)
			continue;
		if (event->bDownloadFinished)
		{
			early_abort = true;
			break;
		}
	}
	WALK_EVENTS_END;
	
	// Also check for being previously finished by checking for 100% completion.
	if (!early_abort)
	{
		WALK_EVENTS(machine->periodic_events, PeriodicEvents, event)
		{
			if (!reportFilterEventByParameters(&event->event, parameters))
				continue;
			if (event->event.when >= period->begin_time)
				continue;
			if (event->blob.progress == 1)
			{
				early_abort = true;
				break;
			}
		}
		WALK_EVENTS_END;
	}

	// If we have to abort early, check if we need to record an install, then return.
	if (early_abort)
	{
		bool previously_installed = false;

		++period->previously_finished;

		// Make sure the installer hasn't already been run previously.
		WALK_EVENTS(machine->exit_events, ExitEvents, event2)
		{
			if (!reportFilterEventByParameters(&event2->event, parameters))
				continue;
			if (event2->event.when >= period->begin_time)
				continue;
			if (event2->bInstallerRan)
			{
				previously_installed = true;
				break;
			}
		}
		WALK_EVENTS_END;

		// If it hasn't, check if it's been run in this period.
		if (!previously_installed)
		{
			WALK_EVENTS(machine->exit_events, ExitEvents, event2)
			{
				if (!reportFilterEventByParameters(&event2->event, parameters))
					continue;
				if (!reportFilterEventByPeriod(&event2->event, period))
					continue;
				if (event2->bInstallerRan)
				{
					previously_installed = true;
					break;
				}
			}
			WALK_EVENTS_END;
		}

		return;																		// Return.
	}

	// Initialize environment.
	env.period = period;
	env.results = results;
	env.machine = machine;
	env.parameters = parameters;

	// Step 1: Figure out if there's any activity here.

	// Check for any activity within this period.
	WalkAllEvents(machine, reportFindActivity, &env);

	// Step 2: Figure out our general state.

	// Check if this is a start, or a cancel, or both.
	if (env.activity)
	{
		env.started = true;
		WalkAllEvents(machine, reportCheckStarted, &env);
	}
	WalkAllEvents(machine, reportCheckCanceledInPeriod, &env);
	if (!env.activity && env.activity_last_period_near_end
		|| env.activity && !env.activity_this_period_near_end)
		env.canceled = true;

	// Check if we've resumed from some previous download.
	if (env.started && env.absolute_first_activity < env.first_activity)
		env.resumed = true;

	// Check if the downloader reported being finished.
	WALK_EVENTS(machine->exit_events, ExitEvents, event)
	{
		if (!reportFilterEventByParameters(&event->event, parameters))
			continue;
		if (!reportFilterEventByPeriod(&event->event, period))
			continue;
		if (event->bDownloadFinished)
		{
			finished = true;
			env.canceled = false;
			if (!env.finished_event || event->event.when < env.finished_event->when)
				env.finished_event = &event->event;
		}
	}
	WALK_EVENTS_END;

	// Check if we saw 100% completion, which would also mean finished.
	WALK_EVENTS(machine->periodic_events, PeriodicEvents, event)
	{
		if (!reportFilterEventByParameters(&event->event, parameters))
			continue;
		if (!reportFilterEventByPeriod(&event->event, period))
			continue;
		if (event->blob.progress == 1)
		{
			finished = true;
			env.canceled = false;
			if (!env.finished_event || event->event.when < env.finished_event->when)
				env.finished_event = &event->event;
		}
	}
	WALK_EVENTS_END;

	// Check if the installer has been run.
	if (finished)
	{
		WALK_EVENTS(machine->exit_events, ExitEvents, event)
		{
			if (!reportFilterEventByParameters(&event->event, parameters))
				continue;
			if (!reportFilterEventByPeriod(&event->event, period))
				continue;
			if (event->bInstallerRan)
			{
				installed = true;
				// Make sure the installer hasn't already been run previously.
				WALK_EVENTS(machine->exit_events, ExitEvents, event2)
				{
					if (!reportFilterEventByParameters(&event2->event, parameters))
						continue;
					if (event2->event.when >= event->event.when)
						continue;
					if (event2->bInstallerRan)
					{
						installed = false;
						break;
					}
				}
				WALK_EVENTS_END;
				break;
			}
		}
		WALK_EVENTS_END;
	}

	// If nothing interesting is happening here, skip the rest.
	if (!env.activity && !env.canceled)
	{
		++period->non_interesting;
		return;																			// Return.
	}

	// Step 3: Pull some additional statistics about the completion.

	// Get the dropout percent, if canceled.
	if (env.canceled)
	{
		WALK_EVENTS(machine->periodic_events, PeriodicEvents, event)
		{
			if (!reportFilterEventByParameters(&event->event, parameters))
				continue;
			if (event->event.when >= period->end_time)
				continue;
			dropout_percent = MAX(dropout_percent, event->blob.progress);
		}
		WALK_EVENTS_END;
	}

	// Scan for some other statistics.
	if (finished)
	{
		// Get reference starting event.
		WalkAllEvents(machine, reportFindEarliestMatchingInfo, &env);

		// Scan periodic data for interesting stuff.
		WALK_EVENTS(machine->periodic_events, PeriodicEvents, event)
		{
			// Only look at relevant events.
			if (!reportFilterEventByParameters(&event->event, parameters))
				continue;

			// Only look for downloads from the same info hash.
			if (memcmp(event->event.info_hash, env.finished_event->info_hash, sizeof(event->event.info_hash)))
				continue;

			// Get download rate data.
			download_rate_sample_sum += event->blob.download_payload_rate;
			++download_rate_sample_num;

			// Check if we've used the fallback.
			if (event->blob.ever_used_fallback)
				ever_used_fallback = true;

			// Check for download amounts.
			// FIXME: This is not an accurate calculation if they've restarted the download.
			total_download = MAX(total_download, event->blob.total_download);
			total_web_download = MAX(total_web_download, event->blob.total_web_download);
			total_cryptic_download = MAX(total_cryptic_download, event->blob.total_cryptic_download);
		}
		WALK_EVENTS_END;

		// Calculate average download rate.
		if (download_rate_sample_num)
			average_download_rate = download_rate_sample_sum / download_rate_sample_num;
	}

	// Check if this was just a brief download.
	env.spread_earliest = U32_MAX;
	WalkAllEvents(machine, reportGetSpread, &env);
	if (env.canceled && env.spread_latest - env.spread_earliest < ADJUSTED_COMPLETION_THRESHOLD)
		++period->short_spread;

	// Step 4: Do breakdowns, if requested.
	if (parameters->breakdowns)
	{
		const char *country;
		ReportBreakdown *breakdown = NULL;
		int lcid;

		// Country
		country = ipToCountryName(env.any_event->uIp);
		stashFindPointer(results->country_breakdown, country, &breakdown);
		if (!breakdown)
			breakdown = calloc(1, sizeof(*breakdown));
		if (env.resumed)
			++breakdown->resumed;
		else if (env.started)
			++breakdown->started;
		if (finished)
			++breakdown->finished;
		if (installed)
			++breakdown->installed;
		if (env.canceled)
			++breakdown->canceled;
		if (env.activity)
			++breakdown->activity;
		stashAddPointer(results->country_breakdown, country, breakdown, true);

		// LCID
		lcid = env.any_event->lcid;
		breakdown = NULL;
		stashIntFindPointer(results->lcid_breakdown, lcid, &breakdown);
		if (!breakdown)
			breakdown = calloc(1, sizeof(*breakdown));
		if (env.resumed)
			++breakdown->resumed;
		else if (env.started)
			++breakdown->started;
		if (finished)
			++breakdown->finished;
		if (installed)
			++breakdown->installed;
		if (env.canceled)
			++breakdown->canceled;
		if (env.activity)
			++breakdown->activity;
		stashIntAddPointer(results->lcid_breakdown, lcid, breakdown, true);

		// Non-breakdown aggregates
		if (average_download_rate)
			eafPush(&results->download_rate, average_download_rate);
		if (env.canceled)
		{
			eafPush(&results->dropout_percent, dropout_percent);
		}
		if (env.finished_event && env.start_event)
			eafPush(&results->total_download_time, (float)(env.finished_event->when - env.start_event->when));
	}

	// Step 5: Accumulate results.
	++period->accumulated;
	if (env.resumed)
		++period->resumed;
	else if (env.started)
		++period->started;
	if (finished)
		++period->finished;
	if (installed)
		++period->installed;
	if (env.canceled)
		++period->canceled;
	if (env.activity)
		++period->activity;

	if (ever_used_fallback)
		++period->ever_used_fallback;
	period->total_download += total_download;
	period->total_web_download += total_web_download;
	period->total_cryptic_download += total_cryptic_download;
}

// Gather statistics for each time interval.
static void ProcessReportMachine(ReportTimeSeries *results, const UniqueMachine *machine, ReportParameters *parameters)
{
	int i;

	// Process at fine granularity.
	parameters->breakdowns = false;
	for (i = 0; i != results->periods; ++i)
		ProcessReportMachineTimePeriod(results->pResults + i, results, machine, parameters);

	// Process at coarse granularity.
	parameters->breakdowns = true;
	for (i = 0; i != results->super_periods; ++i)
		ProcessReportMachineTimePeriod(results->pResultsSuper + i, results, machine, parameters);
}

// Process any pending reports until the timeout.
void ProcessReports()
{
	int timer;
	unsigned count = 0;		// Used to reduce the number of times we call timerElapsed()

	PERFINFO_AUTO_START_FUNC();

	// Start timer.
	timer = timerAlloc();

	// Loop over each running report.
	EARRAY_FOREACH_BEGIN(sppRunningReports, i);
	{
		RunningReport *report = sppRunningReports[i];
		int *current = &report->current_machine;

		// Process each machine in report.
		for (; *current < beaSize(&sDatabase.machines); ++*current)
		{
			ProcessReportMachine(&report->results, sDatabase.machines + *current, &report->parameters);
			if ((++count & 0xff) == 0 && timerElapsed(timer) > REPORT_TICK_PROCESS_TIMEOUT)
			{
				timerFree(timer);
				PERFINFO_AUTO_STOP_FUNC();
				return;
			}
		}

		// Report finished: call report callback.
		report->callback(&report->results, &report->parameters, report->userdata);
		free(report->parameters.product);
		free(report);
		eaRemove(&sppRunningReports, 0);
		--i;
	}
	EARRAY_FOREACH_END;

	timerFree(timer);

	PERFINFO_AUTO_STOP_FUNC();
}

// Initiate a report run.
static void StartReport(const ReportParameters *parameters, pfnReportCallback callback, void *userdata)
{
	U32 i;
	int index = 0;
	U32 total_duration;
	U32 periods;
	U32 super_periods;
	RunningReport *report = calloc(1, sizeof(*report));

	// Don't run if there is nothing to do.
	if (parameters->end_time <= parameters->begin_time)
		return;

	// Set up basic parameters.
	report->parameters.begin_time = parameters->begin_time;
	report->parameters.end_time = parameters->end_time;
	report->parameters.granularity = parameters->granularity;
	report->parameters.super_granularity = parameters->super_granularity;
	report->parameters.product = strdup(parameters->product);
	report->parameters.use_info_hash = report->parameters.use_info_hash;
	memcpy(report->parameters.info_hash, parameters->info_hash, sizeof(report->parameters.info_hash));
	report->callback = callback;
	report->userdata = userdata;

	// Set up time series.
	total_duration = report->parameters.end_time - report->parameters.begin_time;
	periods = total_duration / report->parameters.granularity;
	if (total_duration % report->parameters.granularity)
		++periods;
	report->results.periods = periods;
	report->results.pResults = calloc(periods, sizeof(*report->results.pResults));
	for (i = report->parameters.begin_time; i < report->parameters.end_time; i += report->parameters.granularity)
	{
		report->results.pResults[index].begin_time = i;
		report->results.pResults[index].end_time = MIN(i + report->parameters.granularity, report->parameters.end_time);
		++index;
	}

	// Set up time super-series.
	super_periods = total_duration / report->parameters.super_granularity;
	if (total_duration % report->parameters.super_granularity)
		++super_periods;
	report->results.super_periods = super_periods;
	report->results.pResultsSuper = calloc(super_periods, sizeof(*report->results.pResultsSuper));
	index = 0;
	for (i = report->parameters.begin_time; i < report->parameters.end_time; i += report->parameters.super_granularity)
	{
		report->results.pResultsSuper[index].begin_time = i;
		report->results.pResultsSuper[index].end_time = MIN(i + report->parameters.super_granularity, report->parameters.end_time);
		++index;
	}

	// Set up breakdown stashes.
	report->results.country_breakdown = stashTableCreateAddress(0);
	report->results.lcid_breakdown = stashTableCreateInt(0);

	// Add report to active reports list.
	eaPush(&sppRunningReports, report);
}

// A report from RunReport() has completed.
static void RunReportHtmlCallback(ReportTimeSeries *results, const ReportParameters *parameters, void *userdata)
{
	const char *basename = userdata;
	CreateHtmlReport(basename, parameters, results);
}

// Run a manual HTML report, using SecsSince2000 times.
void RunReportHtml(char *basename, U32 begin, U32 end, U32 granularity, U32 divisor, const char *product, U32 *info_hash)
{
	ReportParameters parameters = {0};

	// Initiate report run with these parameters.
	parameters.begin_time = begin;
	parameters.end_time = end;
	parameters.granularity = granularity;
	parameters.super_granularity = granularity*divisor;
	parameters.product = strdup(product);
	if (info_hash)
	{
		parameters.use_info_hash = true;
		memcpy(parameters.info_hash, info_hash, sizeof(info_hash));
	}

	StartReport(&parameters, RunReportHtmlCallback, basename);
}

// Run a manual HTML report, with a friendly date and time.
AUTO_COMMAND;
void RunReportHtmlDateTime(char *basename, char *begin_datetime, char *end_datetime, U32 granularity, U32 divisor, const char *product, const char *info_hash_string)
{
	U32 begin, end;
	U32 info_hash_array[8];
	U32 *info_hash = NULL;

	// Parse time range.
	begin = timeGetSecondsSince2000FromPACDateString(begin_datetime);
	if (!begin)
	{
		Errorf("Not a valid begin datetime");
		return;
	}
	end = timeGetSecondsSince2000FromPACDateString(end_datetime);
	if (!end)
	{
		Errorf("Not a valid begin datetime");
		return;
	}

	// Parse info hash.
	if (info_hash_string && *info_hash_string)
	{
		bool success = parseInfoHashString(info_hash_array, info_hash_string);
		if (!success)
		{
			Errorf("Not a valid info hash string");
			return;
		}
		info_hash = info_hash_array;
	}

	// Call the main report-running function.
	RunReportHtml(basename, begin, end, granularity, divisor, product, info_hash);
}

// Run a manual HTML report, with a friendly date and time.
AUTO_COMMAND;
void RunReportHtmlDateTimeRegularWeekly(char *basename, char *begin_datetime, const char *product, const char *info_hash_string)
{
	U32 begin, end;
	U32 info_hash_array[8];
	U32 *info_hash = NULL;

	// Parse time range.
	begin = timeGetSecondsSince2000FromPACDateString(begin_datetime);
	if (!begin)
	{
		Errorf("Not a valid begin datetime");
		return;
	}

	// One week
	end = begin + 60*60*24*7;

	// Parse info hash.
	if (info_hash_string && *info_hash_string)
	{
		bool success = parseInfoHashString(info_hash_array, info_hash_string);
		if (!success)
		{
			Errorf("Not a valid info hash string");
			return;
		}
		info_hash = info_hash_array;
	}

	// Call the main report-running function.
	RunReportHtml(basename, begin, end, 60*60, 24, product, info_hash);
}
