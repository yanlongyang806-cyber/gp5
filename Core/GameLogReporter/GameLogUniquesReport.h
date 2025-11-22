#include "GameLogReporter.h"

AUTO_STRUCT;
typedef struct LoginAttempt
{
	U32 uAccountID;
	bool bSuccessful;
	Language eLang;
} LoginAttempt;

AUTO_STRUCT;
typedef struct DailyUniquesData
{
	U32 uDaysSince2000; AST(KEY)
	StashTable stLoginAttemptsByAccountID; NO_AST
	U32 uTotalLogins;
	U32 u7DaySum;
	U32 u30DaySum;
	StashTable stSuccessfulLoginsByLang; NO_AST
} DailyUniquesData;

void UniquesReport_Init(void);
void UniquesReport_AnalyzeParsedLog(ParsedLog* pLog);
void UniquesReport_GenerateReport(void);

#include "GameLogUniquesReport_h_ast.h"