#include "GameLogReporter.h"

AUTO_STRUCT;
typedef struct NumericGainLossData
{
	S64 uGain;
	S64 uLoss;
} NumericGainLossData;

AUTO_STRUCT;
typedef struct LevelNumericData
{
	S32 iLevel;	AST(KEY)
	U32 uiLevelStart;
	U32 uiLevelEnd;
	StashTable stGainLossByItemName;
} LevelNumericData;

AUTO_STRUCT;
typedef struct CharacterNumericData
{
	U32 containerID;
	U32 accountID;
	const char* pchClassname;
	LevelNumericData** eaLevels;
} CharacterNumericData;

void PerCharacterNumericCSV_Init(void);
void PerCharacterNumericCSV_AnalyzeParsedLog(ParsedLog* pLog);
void PerCharacterNumericCSV_GenerateReport(void);
void PerCharacterNumericCSV_SendResultEmail(void);

#include "GameLogPerCharacterNumericCSV_h_ast.h"