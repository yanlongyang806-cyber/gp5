#include "GameLogReporter.h"

#define EXAMPLE_NUM_SUMMARIES_TO_KEEP 30

AUTO_STRUCT;
typedef struct ExampleDailySummaryData
{
	const char* pchKey;	AST(KEY POOL_STRING)
	U64 uValue;
} ExampleDailySummaryData;

AUTO_STRUCT;
typedef struct ExampleDailySummary
{
	U32 uDaysSince2000;
	ExampleDailySummaryData** eaData;
} ExampleDailySummary;

AUTO_STRUCT;
typedef struct ExampleDailySummaries
{
	ExampleDailySummary** eaSummaries;
} ExampleDailySummaries;

void NewReportTemplate_Init(void);
void NewReportTemplate_AnalyzeParsedLog(ParsedLog* pLog);
void NewReportTemplate_GenerateReport(void);
void NewReportTemplate_SendResultEmail(void);

#include "GameLogNewReportTemplate_h_ast.h"