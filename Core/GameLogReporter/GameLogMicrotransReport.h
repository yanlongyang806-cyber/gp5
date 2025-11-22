#include "GameLogReporter.h"

AUTO_STRUCT;
typedef struct MicrotransSummary
{
	U32 uStartTimeSS2000;
	U32 uEndTimeSS2000;
	U64 uTransactionAmount;
} MicrotransSummary;

AUTO_STRUCT;
typedef struct MicrotransSummaries
{
	MicrotransSummary** eaSummaries;
} MicrotransSummaries;

AUTO_STRUCT;
typedef struct MicrotransAmountSpent
{
	char* pchKey;
	U64 uAmount;
} MicrotransAmountSpent;

void MicrotransReport_Init(void);
void MicrotransReport_AnalyzeParsedLog(ParsedLog* pLog);
void MicrotransReport_GenerateReport(void);

#include "GameLogMicrotransReport_h_ast.h"