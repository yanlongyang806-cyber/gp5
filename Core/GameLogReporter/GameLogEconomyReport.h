#include "GameLogReporter.h"

#define GAMEECON_NUM_SUMMARIES_TO_KEEP 30

AUTO_STRUCT;
typedef struct ItemDailyTransactionData
{
	const char* pchItemDefName;	AST(KEY POOL_STRING)
	U64 uAmountEarned;
	U64 uAmountSpent;
	U64 uNumEarners;
	U64 uNumSpenders;
} ItemDailyTransactionData;

AUTO_STRUCT;
typedef struct ItemDailySummary
{
	U32 uDaysSince2000;
	ItemDailyTransactionData** eaItemTransactions;
} ItemDailySummary;

AUTO_STRUCT;
typedef struct ItemDailySummaries
{
	ItemDailySummary** eaSummaries;
} ItemDailySummaries;


AUTO_STRUCT;
typedef struct PlayerItemGainLoss
{
	const char* pchPlayerDebugName;	AST(KEY POOL_STRING)
	char* pchClassName;	AST(POOL_STRING)
	int iMinLevel;
	int iMaxLevel;
	U64 uGain;
	U64 uLoss;
} PlayerItemGainLoss;

AUTO_STRUCT;
typedef struct ReasonItemGainLoss
{
	const char* pchReasonDisplayName;	AST(KEY POOL_STRING)
	int iGainCount;
	U64 uGain;
	int iLossCount;
	U64 uLoss;
} ReasonItemGainLoss;

AUTO_STRUCT;
typedef struct ItemGainRecords
{
	PlayerItemGainLoss** eaPlayerGainLoss;
	ReasonItemGainLoss** eaReasonGainLoss;
} ItemGainRecords;

void EconomyReport_Init(void);
void EconomyReport_AnalyzeParsedLog(ParsedLog* pLog);
void EconomyReport_GenerateReport(void);

#include "GameLogEconomyReport_h_ast.h"