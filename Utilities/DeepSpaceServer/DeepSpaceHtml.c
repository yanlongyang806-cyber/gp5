/*
 * HTML-formatting for DSN reports
 */

#include "clearsilver.h"
#include "DeepSpaceHtml.h"
#include "DeepSpaceHtml_c_ast.h"
#include "DeepSpaceReports.h"
#include "DeepSpaceUtils.h"
#include "file.h"
#include "fileutil.h"
#include "qsortG.h"
#include "StashTable.h"
#include "statistics.h"
#include "StringCache.h"
#include "timing.h"

// Maximum number of rows in a breakdown table
#define MAX_BREAKDOWN_TABLE_ROWS 10

// HTML table entry for download statistics
AUTO_STRUCT;
typedef struct HtmlReportTableEntry {
	int row;
	char *label;						AST(ESTRING)

	char *started;						AST(ESTRING)
	char *finished;						AST(ESTRING)
	char *installs;						AST(ESTRING)
	char *canceled;						AST(ESTRING)
	char *resumed;						AST(ESTRING)

	// See ReportResults for explanation
	char *examined;						AST(ESTRING)
	char *activity;						AST(ESTRING)
	char *previously_finished;			AST(ESTRING)
	char *non_interesting;				AST(ESTRING)
	char *accumulated;					AST(ESTRING)

	char *completionpercent;			AST(ESTRING)
	char *completionpercentadjusted;	AST(ESTRING)

	//char *averagespeed;				AST(ESTRING)
	char *gbcryptic;					AST(ESTRING)
	char *gbweb;						AST(ESTRING)
	char *gbpeers;						AST(ESTRING)
	char *efficiency;					AST(ESTRING)
	char *fallbackpercent;				AST(ESTRING)
} HtmlReportTableEntry;

// Fully-processed report data, ready to be formatted into HTML.
AUTO_STRUCT;
typedef struct DeepSpaceHtmlReport {
	char *titletext;				AST(ESTRING)
	int periods;
	EARRAY_OF(HtmlReportTableEntry) days;
	HtmlReportTableEntry *days_total;
	EARRAY_OF(HtmlReportTableEntry) countries;
	EARRAY_OF(HtmlReportTableEntry) locales;
	char filename_downloadsperhour[MAX_PATH];
	char filename_downloaddurationhist[MAX_PATH];
//	char filename_efficiency[MAX_PATH];
	char filename_dropout[MAX_PATH];
	char filename_downloadrate[MAX_PATH];
} DeepSpaceHtmlReport;

// If true, write HDF files for debugging.
static bool s_write_hdf = false;
AUTO_CMD_INT(s_write_hdf, write_hdf);

// Write HTML file for struct.
// Optionally, write an HDF file for debugging.
static void RenderHtml(const char *basename, const char *template_file, ParseTable *tpi, DeepSpaceHtmlReport *struct_mem)
{
	char filename[MAX_PATH];
	FILE *outfile;
	char *data;

	// Write HTML data.
	data = renderTemplate(template_file, tpi, struct_mem, false);
	sprintf(filename, "%s/download_report.html", basename);
	outfile = fopen(filename, "w");
	if (!outfile)
	{
		Errorf("Opening %s failed", filename);
		return;
	}
	fwrite(data, 1, estrLength(&data), outfile);
	fclose(outfile);
	estrDestroy(&data);
	
	// Write HDF data, for debugging HTML.
	if (s_write_hdf)
	{
		sprintf(filename, "%s/download_report.hdf", basename);
		data = renderTemplate(template_file, tpi, struct_mem, true);
		outfile = fopen(filename, "w");
		if (!outfile)
		{
			Errorf("Opening %s failed", filename);
			return;
		}
		fwrite(data, 1, estrLength(&data), outfile);
		fclose(outfile);
		estrDestroy(&data);
	}
}

// Get the day of the week from a SS2000.
static const char *weekdayFromSs2000(U32 ss2000)
{
	struct tm t;
	timeMakeLocalTimeStructFromSecondsSince2000(ss2000, &t);
	switch (t.tm_wday) {
	case 0:
		return "Sunday";
	case 1:
		return "Monday";
	case 2:
		return "Tuesday";
	case 3:
		return "Wednesday";
	case 4:
		return "Thursday";
	case 5:
		return "Friday";
	case 6:
		return "Saturday";
	default:
		devassert(0);
	}
	return "alpaca";
}

// Get the day of the week from a SS2000.
static const char *shortWeekdayFromSs2000(U32 ss2000)
{
	struct tm t;
	timeMakeLocalTimeStructFromSecondsSince2000(ss2000, &t);
	switch (t.tm_wday) {
	case 0:
		return "Sun";
	case 1:
		return "Mon";
	case 2:
		return "Tue";
	case 3:
		return "Wed";
	case 4:
		return "Thu";
	case 5:
		return "Fri";
	case 6:
		return "Sat";
	default:
		devassert(0);
	}
	return "alpaca";
}

// Get the month from a SS2000.
static const char *monthFromSs2000(U32 ss2000)
{
	struct tm t;
	timeMakeLocalTimeStructFromSecondsSince2000(ss2000, &t);
	switch (t.tm_mon) {
		case 0:
			return "January";
		case 1:
			return "February";
		case 2:
			return "March";
		case 3:
			return "April";
		case 4:
			return "May";
		case 5:
			return "June";
		case 6:
			return "July";
		case 7:
			return "August";
		case 8:
			return "September";
		case 9:
			return "October";
		case 10:
			return "November";
		case 11:
			return "December";
		default:
			devassert(0);
	}
	return "llama";
}

// Return day of month.
int dayFromSs2000(U32 ss2000)
{
	struct tm t;
	timeMakeLocalTimeStructFromSecondsSince2000(ss2000, &t);
	return t.tm_mday;
}

// Get the completion percent from a period.
static int reportCompletionPercent(const ReportResults *period)
{
	int ended = period->finished + period->canceled;
	return ended ? 100*period->finished/ended : 0;
}

// Get the completion percent from a period.
static int reportCompletionPercentAdjusted(const ReportResults *period)
{
	int ended = period->finished + (period->canceled - period->short_spread);
	return ended ? 100*period->finished/ended : 0;
}

// Write out the gnuplot data for a particular field.
static void writeRenderLineDataU32(FILE *out, const ReportTimeSeries *series, size_t field_offset)
{
	int i;
	for (i = 0; i != series->periods; ++i)
	{
		const ReportResults *report_hour = &series->pResults[i];
		const U32 *value_ptr = (const U32 *)((const char *)report_hour + field_offset);
		fprintf(out, "%u %u\n", report_hour->begin_time, *value_ptr);
	}
	fprintf(out, "\n\n");
}

// Render downloads per hour graph.
static void RenderGraphDownloadsPerHour(DeepSpaceHtmlReport *report, const char *basename, const ReportParameters *parameters,
	const ReportTimeSeries *results)
{
	int i;
	char output_filename[MAX_PATH];
	char graph_data_filename[MAX_PATH];
	char graph_script_filename[MAX_PATH];
	FILE *graph_data;
	FILE *graph_script;
	char *command = NULL;
	U32 last;

	// Get filenames.
	sprintf(report->filename_downloadsperhour, "downloadsperhour.png");
	sprintf(graph_data_filename, "%s/downloadsperhour_data.txt", basename);
	sprintf(graph_script_filename, "%s/downloadsperhour_script.txt", basename);
	
	// Open files.
	sprintf(output_filename, "%s/downloadsperhour.png", basename);
	graph_data = fopen(graph_data_filename, "wt");
	if (!graph_data)
	{
		Errorf("Unable to open graph data file");
		return;
	}
	graph_script = fopen(graph_script_filename, "wt");
	if (!graph_script)
	{
		Errorf("Unable to open graph script file");
		fclose(graph_data);
		return;
	}

	// Loop over each hour.
	writeRenderLineDataU32(graph_data, results, offsetof(ReportResults, started));
	writeRenderLineDataU32(graph_data, results, offsetof(ReportResults, finished));
	writeRenderLineDataU32(graph_data, results, offsetof(ReportResults, resumed));
	writeRenderLineDataU32(graph_data, results, offsetof(ReportResults, canceled));
	fclose(graph_data);

	// Set up basic script.
	fprintf(graph_script, "set terminal pngcairo transparent enhanced font \"arial,10\" fontscale 1.0 size 485, 150\n"
		"set output \"%s\"\n"
		"set label \"Downloads, hourly\" at graph 0.5,0.85 center\n"
		"set yrange [0:]\n"
		"set grid\n",
		output_filename);

	// Make labels.
	fprintf(graph_script, "set xtics (");
	for (i = 0; i != results->super_periods; ++i)
	{
		const ReportResults *report_hour = &results->pResultsSuper[i];
		fprintf(graph_script, "%s\"%s\" %u", i ? ", " : "", shortWeekdayFromSs2000(report_hour->begin_time), report_hour->begin_time);
	}
	last = results->pResultsSuper[results->super_periods - 1].end_time;
	fprintf(graph_script, ", \"%s\" %u", shortWeekdayFromSs2000(last), last);
	fprintf(graph_script, ")\n");

	// Write plot command.
	fprintf(graph_script, "plot \"%s\""
			   " index 0 smooth unique title \"Started\""	" with lines linewidth 1"
		" , \"\" index 1 smooth unique title \"Finished\""	" with lines linewidth 1"
		" , \"\" index 2 smooth unique title \"Resumed\""	" with lines linewidth 1"
		" , \"\" index 3 smooth unique title \"Canceled\""	" with lines linewidth 1\n",
		graph_data_filename);
	fclose(graph_script);

	// Execute gnuplot.
	estrStackCreate(&command);
	estrPrintf(&command, "wgnuplot %s", graph_script_filename);
	system(command);
	estrDestroy(&command);
}

// Render dropouts graph.
static void RenderGraphDropouts(DeepSpaceHtmlReport *report, const char *basename, const ReportParameters *parameters, const ReportTimeSeries *results)
{
	int i;
	char output_filename[MAX_PATH];
	char graph_data_filename[MAX_PATH];
	char graph_script_filename[MAX_PATH];
	FILE *graph_data;
	FILE *graph_script;
	char *command = NULL;
	F32 *centers = NULL;
	U32 *counts = NULL;

	// Get filenames.
	sprintf(report->filename_dropout, "dropout.png");
	sprintf(graph_data_filename, "%s/dropout_data.txt", basename);
	sprintf(graph_script_filename, "%s/dropout_script.txt", basename);

	// Open files.
	sprintf(output_filename, "%s/dropout.png", basename);
	graph_data = fopen(graph_data_filename, "wt");
	if (!graph_data)
	{
		Errorf("Unable to open graph data file");
		return;
	}
	graph_script = fopen(graph_script_filename, "wt");
	if (!graph_script)
	{
		Errorf("Unable to open graph script file");
		fclose(graph_data);
		return;
	}

	// Create histogram.
	statisticsHistogramF32(&centers, &counts, results->dropout_percent, 20, 0);
	for (i = 0; i != ea32Size(&centers); ++i)
		fprintf(graph_data, "%f %u\n", centers[i]*100, counts[i]);
	fclose(graph_data);

	// Set up basic script.
	fprintf(graph_script, "set terminal pngcairo transparent enhanced font \"arial,10\" fontscale 1.0 size 485, 150\n"
		"set output \"%s\"\n"
		"set label \"Download Progress %% at Cancel\" at graph 0.5,0.85 center\n"
		//"set xlabel \"Rate\"\n"
		"set ylabel \"Downloads\"\n"
		"set boxwidth 0.95 relative\n"
		"set style fill transparent solid 0.5 noborder\n"
		"set xrange [0:100]\n"
		"set grid ytics\n"
		"set xtics rotate\n",
		output_filename);

	// Write plot command.
	fprintf(graph_script, "plot \"%s\""
		" u 1:2 w boxes lc rgb\"green\" notitle\n",
		graph_data_filename);
	fclose(graph_script);

	// Execute gnuplot.
	estrStackCreate(&command);
	estrPrintf(&command, "wgnuplot %s", graph_script_filename);
	system(command);
	estrDestroy(&command);
}

// Render download rate graph.
static void RenderGraphDownloadRate(DeepSpaceHtmlReport *report, const char *basename, const ReportParameters *parameters, const ReportTimeSeries *results)
{
	int i;
	char output_filename[MAX_PATH];
	char graph_data_filename[MAX_PATH];
	char graph_script_filename[MAX_PATH];
	FILE *graph_data;
	FILE *graph_script;
	char *command = NULL;
	F32 *centers = NULL;
	U32 *counts = NULL;

	// Get filenames.
	sprintf(report->filename_downloadrate, "downloadrate.png");
	sprintf(graph_data_filename, "%s/downloadrate_data.txt", basename);
	sprintf(graph_script_filename, "%s/downloadrate_script.txt", basename);

	// Open files.
	sprintf(output_filename, "%s/downloadrate.png", basename);
	graph_data = fopen(graph_data_filename, "wt");
	if (!graph_data)
	{
		Errorf("Unable to open graph data file");
		return;
	}
	graph_script = fopen(graph_script_filename, "wt");
	if (!graph_script)
	{
		Errorf("Unable to open graph script file");
		fclose(graph_data);
		return;
	}

	// Create histogram.
	qsort(results->download_rate, eafSize(&results->download_rate), sizeof(F32), cmpF32);
	statisticsHistogramF32(&centers, &counts, results->download_rate, 25, statisticsSortedPercentile(results->download_rate, eafSize(&results->download_rate), 0.95));
	for (i = 0; i != ea32Size(&centers); ++i)
	{
		F32 bytes_num;
		char *bytes_units;
		U32 bytes_prec;
		humanBytes(centers[i], &bytes_num, &bytes_units, &bytes_prec);
		fprintf(graph_data, "%.*f%s/s", bytes_prec, bytes_num, bytes_units);
		fprintf(graph_data, " %u\n", counts[i]);
	}
	fclose(graph_data);

	// Set up basic script.
	fprintf(graph_script, "set terminal pngcairo transparent enhanced font \"arial,8\" fontscale 1.0 size 485, 150\n"
		"set output \"%s\"\n"
		"set xlabel \" \"\n"
		"set ylabel \"Downloads\"\n"
		"set label \"Average Download Speed\" at graph 0.5,0.85 center\n"
		"set boxwidth 0.95 relative\n"
		"set style fill transparent solid 0.5 noborder\n"
		"set grid ytics\n"
		"set xtics rotate\n",
		output_filename);

	// Write plot command.
	fprintf(graph_script, "plot \"%s\""
		" u 2:xticlabels(1) w boxes lc rgb\"green\" notitle\n",
		graph_data_filename);
	fclose(graph_script);

	// Execute gnuplot.
	estrStackCreate(&command);
	estrPrintf(&command, "wgnuplot %s", graph_script_filename);
	system(command);
	estrDestroy(&command); 
}

// Render download duration histogram.
static void RenderGraphDownloadDurationHist(DeepSpaceHtmlReport *report, const char *basename, const ReportParameters *parameters,
	const ReportTimeSeries *results)
{
	int i;
	char output_filename[MAX_PATH];
	char graph_data_filename[MAX_PATH];
	char graph_script_filename[MAX_PATH];
	FILE *graph_data;
	FILE *graph_script;
	char *command = NULL;
	F32 *centers = NULL;
	U32 *counts = NULL;

	// Get filenames.
	sprintf(report->filename_downloaddurationhist, "downloaddurationhist.png");
	sprintf(graph_data_filename, "%s/downloaddurationhist_data.txt", basename);
	sprintf(graph_script_filename, "%s/downloaddurationhist_script.txt", basename);

	// Open files.
	sprintf(output_filename, "%s/downloaddurationhist.png", basename);
	graph_data = fopen(graph_data_filename, "wt");
	if (!graph_data)
	{
		Errorf("Unable to open graph data file");
		return;
	}
	graph_script = fopen(graph_script_filename, "wt");
	if (!graph_script)
	{
		Errorf("Unable to open graph script file");
		fclose(graph_data);
		return;
	}

	// Create histogram.
	qsort(results->total_download_time, eafSize(&results->total_download_time), sizeof(F32), cmpF32);
	statisticsHistogramF32(&centers, &counts, results->total_download_time, 25, statisticsSortedPercentile(results->total_download_time, eafSize(&results->total_download_time), 0.95));
	for (i = 0; i != ea32Size(&centers); ++i)
		fprintf(graph_data, "%f %u\n", centers[i]/60, counts[i]);
	fclose(graph_data);

	// Set up basic script.
	fprintf(graph_script, "set terminal pngcairo transparent enhanced font \"arial,10\" fontscale 1.0 size 485, 150\n"
		"set output \"%s\"\n"
		"set label \"Download Time for Completed Downloads\" at graph 0.5,0.85 center\n"
		"set xlabel \"Minutes\"\n"
		"set ylabel \"Downloads\"\n"
		"set xrange [0:]\n"
		"set boxwidth 0.95 relative\n"
		"set style fill transparent solid 0.5 noborder\n"
		"set grid ytics\n"
		"set xtics rotate\n",
		output_filename);

	// Write plot command.
	fprintf(graph_script, "plot \"%s\""
		" u 1:2 w boxes lc rgb\"green\" notitle\n",
		graph_data_filename);
	fclose(graph_script);

	// Execute gnuplot.
	estrStackCreate(&command);
	estrPrintf(&command, "wgnuplot %s", graph_script_filename);
	system(command);
	estrDestroy(&command); 
}

// Render graphs.
static void RenderGraphs(DeepSpaceHtmlReport *report, const char *basename, const ReportParameters *parameters, const ReportTimeSeries *results)
{
	RenderGraphDownloadsPerHour(report, basename, parameters, results);
	RenderGraphDropouts(report, basename, parameters, results);
	RenderGraphDownloadRate(report, basename, parameters, results);
	RenderGraphDownloadDurationHist(report, basename, parameters, results);
}

// Fill in a HtmlReportTableEntry from a ReportResults
static void PopulateReportResults(HtmlReportTableEntry *html_day, const ReportResults *report_day)
{
	F32 bytes_num;
	char *bytes_units;
	U32 bytes_prec;

	estrPrintf(&html_day->started, "%s", getCommaSeparatedInt(report_day->started));
	estrPrintf(&html_day->finished, "%s", getCommaSeparatedInt(report_day->finished));
	estrPrintf(&html_day->installs, "%s", getCommaSeparatedInt(report_day->installed));
	estrPrintf(&html_day->canceled, "%s", getCommaSeparatedInt(report_day->canceled));
	estrPrintf(&html_day->resumed, "%s", getCommaSeparatedInt(report_day->resumed));

	estrPrintf(&html_day->examined, "%s", getCommaSeparatedInt(report_day->examined));
	estrPrintf(&html_day->activity, "%s", getCommaSeparatedInt(report_day->activity));
	estrPrintf(&html_day->previously_finished, "%s", getCommaSeparatedInt(report_day->previously_finished));
	estrPrintf(&html_day->non_interesting, "%s", getCommaSeparatedInt(report_day->non_interesting));
	estrPrintf(&html_day->accumulated, "%s", getCommaSeparatedInt(report_day->accumulated));

	estrPrintf(&html_day->completionpercent, "%d", reportCompletionPercent(report_day));
	estrPrintf(&html_day->completionpercentadjusted, "%d", reportCompletionPercentAdjusted(report_day));

	humanBytes(report_day->total_cryptic_download, &bytes_num, &bytes_units, &bytes_prec);
	estrPrintf(&html_day->gbcryptic,"%.*f%s", bytes_prec, bytes_num, bytes_units);

	humanBytes(report_day->total_web_download, &bytes_num, &bytes_units, &bytes_prec);
	estrPrintf(&html_day->gbweb,"%.*f%s", bytes_prec, bytes_num, bytes_units);

	humanBytes(report_day->total_download - report_day->total_web_download - report_day->total_cryptic_download,
		&bytes_num, &bytes_units, &bytes_prec);
	estrPrintf(&html_day->gbpeers,"%.*f%s", bytes_prec, bytes_num, bytes_units);

	estrPrintf(&html_day->efficiency, "%d",
		report_day->total_download ? (int)(100*(report_day->total_download - report_day->total_web_download - report_day->total_cryptic_download)/report_day->total_download) : 0);
	estrPrintf(&html_day->fallbackpercent, "%d", report_day->finished ? 100*report_day->ever_used_fallback/report_day->finished : 0);
}

// Fill in a HtmlReportTableEntry from a ReportBreakdown
static void PopulateReportBreakdownLine(HtmlReportTableEntry *html_day, const ReportBreakdown *breakdown)
{
	estrPrintf(&html_day->started, "%s", getCommaSeparatedInt(breakdown->started));
	estrPrintf(&html_day->finished, "%s", getCommaSeparatedInt(breakdown->finished));
	estrPrintf(&html_day->installs, "%s", getCommaSeparatedInt(breakdown->installed));
	estrPrintf(&html_day->canceled, "%s", getCommaSeparatedInt(breakdown->canceled));
	estrPrintf(&html_day->resumed, "%s", getCommaSeparatedInt(breakdown->resumed));
}

// Reverse compare StashElement * with ReportBreakdown * values by the started member.
static int compareBreakdownElemsByStarted(const void *lhs_ptr, const void *rhs_ptr)
{
	StashElement *lhs_elem_ptr = (StashElement *)lhs_ptr;
	StashElement *rhs_elem_ptr = (StashElement *)rhs_ptr;
	StashElement lhs_elem = *lhs_elem_ptr;
	StashElement rhs_elem = *rhs_elem_ptr;
	ReportBreakdown *lhs_breakdown = stashElementGetPointer(lhs_elem);
	ReportBreakdown *rhs_breakdown = stashElementGetPointer(rhs_elem);
	return rhs_breakdown->started - lhs_breakdown->started;
}

typedef char *(*reportElemToLabelEstrFunc)(StashElement elem);

// Print a breakdown table.
static void PopulateReportBreakdown(HtmlReportTableEntry ***table, StashTable breakdown, reportElemToLabelEstrFunc make_label)
{
	StashElement *sorted = NULL;
	ReportBreakdown report_total = {0};
	HtmlReportTableEntry *total_entry;

	// Populate sorted from stash.
	FOR_EACH_IN_STASHTABLE2(breakdown, elem);
	{
		eaPush(&sorted, elem);
	}
	FOR_EACH_END;

	// Sort it.
	eaQSort(sorted, compareBreakdownElemsByStarted);

	// Take the top ten entries.
	eaSetSize(&sorted, MIN(eaSize(&sorted), MAX_BREAKDOWN_TABLE_ROWS));

	// Format each table line.
	EARRAY_CONST_FOREACH_BEGIN(sorted, i, n);
	{
		StashElement elem = sorted[i];
		ReportBreakdown *report_breakdown = stashElementGetPointer(elem);
		HtmlReportTableEntry *entry = StructCreate(parse_HtmlReportTableEntry);
		eaPush(table, entry);
		entry->row = i & 1;
		entry->label = make_label(elem);
		PopulateReportBreakdownLine(entry, report_breakdown);

		// Accumulate table total.
		report_total.started += report_breakdown->started;
		report_total.finished += report_breakdown->finished;
		report_total.installed += report_breakdown->installed;
		report_total.resumed += report_breakdown->resumed;
		report_total.canceled += report_breakdown->canceled;
		report_total.activity += report_breakdown->activity;
	}
	EARRAY_FOREACH_END;

	// Format total.
	total_entry = StructCreate(parse_HtmlReportTableEntry);
	eaPush(table, total_entry);
	total_entry->label = estrDup("Total");
	PopulateReportBreakdownLine(total_entry, &report_total);
}

// Get a label string for a country.
static char *labelCountryToEstr(StashElement elem)
{
	const char *label = stashElementGetKey(elem);
	return estrDup(label);
}

// Get a label string for an LCID.
static char *labelLcidToEstr(StashElement elem)
{
	int lcid = stashElementGetIntKey(elem);
	char *estr = NULL;
	getLcidName(&estr, lcid);
	return estr;
}

// Fill the report up with the results data.
static void PopulateReport(DeepSpaceHtmlReport *report, const ReportParameters *parameters, const ReportTimeSeries *results)
{
	ReportResults report_total = {0};
	//char from[20];
	//char to[20];
	int i;
	ReportBreakdown *removed;
	bool success;

	// Title
	//timeMakePACDateStringFromSecondsSince2000(from, parameters->begin_time);
	//timeMakePACDateStringFromSecondsSince2000(to, parameters->end_time);
	estrPrintf(&report->titletext, "Summary for %s<br />Period starting %s %d", parameters->product, monthFromSs2000(parameters->begin_time), dayFromSs2000(parameters->begin_time));

	// Create days array.
	for (i = 0; i != results->super_periods; ++i)
	{
		const ReportResults *report_day = &results->pResultsSuper[i];
		HtmlReportTableEntry *html_day = StructCreate(parse_HtmlReportTableEntry);

		eaPush(&report->days, html_day);
		html_day->row = i & 1;
		estrPrintf(&html_day->label, "%s %s %d", weekdayFromSs2000(report_day->begin_time), monthFromSs2000(report_day->begin_time), dayFromSs2000(report_day->begin_time));
		PopulateReportResults(html_day, report_day);

		report_total.started += report_day->started;
		report_total.finished += report_day->finished;
		report_total.installed += report_day->installed;
		report_total.resumed += report_day->resumed;
		report_total.canceled += report_day->canceled;

		report_total.examined += report_day->examined;
		report_total.activity += report_day->activity;
		report_total.previously_finished += report_day->previously_finished;
		report_total.non_interesting += report_day->non_interesting;
		report_total.accumulated += report_day->accumulated;

		report_total.short_spread += report_day->short_spread;
		report_total.ever_used_fallback += report_day->ever_used_fallback;
		report_total.total_download += report_day->total_download;
		report_total.total_web_download += report_day->total_web_download;
		report_total.total_cryptic_download += report_day->total_cryptic_download;
	}

	// Create days total.
	report->days_total = StructCreate(parse_HtmlReportTableEntry);
	report->days_total->label = estrDup("Total");
	PopulateReportResults(report->days_total, &report_total);

	// Create countries table.
	success = stashRemovePointer(results->country_breakdown, allocFindString("UNKNOWN"), &removed);
	if (success)
		free(removed);
	success = stashRemovePointer(results->country_breakdown, allocFindString("-"), &removed);
	if (success)
		free(removed);
	PopulateReportBreakdown(&report->countries, results->country_breakdown, labelCountryToEstr);

	// Create countries table.
	PopulateReportBreakdown(&report->locales, results->lcid_breakdown, labelLcidToEstr);
}

// Format results into an HTML report.
void CreateHtmlReport(const char *basename, const ReportParameters *parameters, const ReportTimeSeries *results)
{
	DeepSpaceHtmlReport report = {0};

	PERFINFO_AUTO_START_FUNC();

	// Create directory for report.
	(void)mkdir(basename);

	// Put interesting data into DeepSpaceHtmlReport.
	PopulateReport(&report, parameters, results);

	// Create graphs.
	RenderGraphs(&report, basename, parameters, results);

	// Render it to HTML.
	RenderHtml(basename, "html_report.cs", parse_DeepSpaceHtmlReport, &report);

	StructDeInit(parse_DeepSpaceHtmlReport, &report);
	PERFINFO_AUTO_STOP_FUNC();
}

#include "DeepSpaceHtml_c_ast.c"
