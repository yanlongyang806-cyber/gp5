/*
 * HTML-formatting for DSN reports
 */

#ifndef CRYPTIC_DEEPSPACEHTML_H
#define CRYPTIC_DEEPSPACEHTML_H

typedef struct ReportParameters ReportParameters;
typedef struct ReportTimeSeries ReportTimeSeries;

// Format results into an HTML report.
void CreateHtmlReport(const char *basename, const ReportParameters *parameters, const ReportTimeSeries *results);

#endif  // CRYPTIC_DEEPSPACEHTML_H


