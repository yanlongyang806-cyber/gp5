#include "LogParsing.h"
#include "LogParsingFileBuckets.h"
#include "GlobalTypeEnum.h"
#include "fileutil2.h"
#include "sysutil.h"
#include "gimmeDLLWrapper.h"
#include "FolderCache.h"
#include "wincon.h"
#include "cmdparse.h"
#include "stashtable.h"

#ifndef GAMELOGREPORTER_H
#define GAMELOGREPORTER_H

AUTO_ENUM;
typedef enum GameLogReportType{
	kGameLogReportType_Invalid = -1,
	kGameLogReportType_Economy = 0,
	kGameLogReportType_TrackedItemCSVs,
	kGameLogReportType_Microtransaction,
	kGameLogReportType_DailyUniques,
	kGameLogReportType_ExampleReport,



	kGameLogReportType_Count,	//must be last
} GameLogReportType;

typedef struct ParsedLog ParsedLog;

typedef void (*GameLogReportInitFunc)(void);
typedef void (*GameLogReportProcessFunc)(ParsedLog*);
typedef void (*GameLogReportFinishFunc)(void);
typedef void (*GameLogReportEmailFunc)(void);

typedef struct GameLogReportTypeMap{
	GameLogReportType eType;
	GameLogReportInitFunc pInitFunc;
	GameLogReportProcessFunc pProcessFunc;
	GameLogReportFinishFunc pFinishFunc;
	GameLogReportEmailFunc pEmailFunc;
} GameLogReportTypeMap;

bool WriteOutputFile(const char* pchFilename, const char* pchData, bool bGZip);

void SendDownloadLinkEmail(char* pchFilter);
void SendReportEmail(char* attachment);

#define STRING_STARTS_WITH(a, b) (strncmp(a, b, ARRAY_SIZE_CHECKED(b)-1)==0)
#include "GameLogReporter_h_ast.h"
#endif