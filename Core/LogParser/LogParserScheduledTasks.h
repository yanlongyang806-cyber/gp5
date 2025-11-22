#include "timing.h"
#include "qsortG.h"
#include "error.h"
#include "fileUtil2.h"
#include "StringUtil.h"

AUTO_ENUM;
typedef enum ScheduledTaskRepeatType
{
	kScheduledTaskRepeatType_Invalid = 0,
	kScheduledTaskRepeatType_Hourly,
	kScheduledTaskRepeatType_Daily,
	kScheduledTaskRepeatType_Weekly,
	kScheduledTaskRepeatType_Specified,
} ScheduledTaskRepeatType;

AUTO_STRUCT;
typedef struct ScheduledTask
{
	const char* pchName;		AST(STRUCTPARAM POOL_STRING)
	S64 uiNextRunSS2000;	AST(NO_TEXT_SAVE)	//S64 so indexed sorting works right.
	char* estrNextRunLocalDateString; AST(ESTRING) //for debugging

	const char* pchExecutable;	//The actual command we're going to run
	STRING_EARRAY eaArgs;	AST(NAME(Arg))	//Args to pass to the command

	const char* pchFirstRunDateString;	AST(NAME(FirstRun))
	U32 uiFirstRunSS2000;	AST(NO_TEXT_SAVE)	//Calculated on load from the designer-entered date string.
	
	S32 iTaskTimestampOffsetInSeconds;	//Offset in seconds from the time we start our task to the time we tell the executable we started. Allows you to, for example, start a task at 12:30 AM that analyzes logs from 12:00 to 12:00 (a -1800 second offset)
	
	ScheduledTaskRepeatType eScheduleType;
	U32 uiSpecifiedIntervalSeconds; AST(NAME(SpecifiedIntervalSeconds))
	U32 uiLastRunAdjustment;	AST(NAME(LastRunAdjustment)) //For things like the microtrans report which want to run daily but analyze 60 days of logs.

	const char* pchFilename;	AST(FILENAME)

	bool bAddMailserverArg;	//If set, includes an additional arg of the format ("-Mailserver %s", s_pchMailServer)
	bool bAddShardInfoArgs;	//If set, includes a additional args for ControllerTracker and ShardName

	bool bDisable;				NO_AST
	U32 uiLastRunStartSS2000;	NO_AST
	U32 uiLastRunEndSS2000;		NO_AST
	U32 uiNumRunsSinceStartup;	NO_AST
	S64 iRunDurationsSum;		NO_AST
} ScheduledTask;

AUTO_STRUCT;
typedef struct ScheduledTasksDef
{
	ScheduledTask** eaTasks; AST(NAME(ScheduledTask))//Indexing is enabled post-load because the keys haven't been generated yet.
	const char* pchDefaultMailServer; AST(NAME(DefaultMailServer))
	const char* pchDownloadDir;		  AST(NAME(DownloadDirectory))
	const char* pchWorkingDir;		  AST(NAME(WorkingDirectory))
} ScheduledTasksDef;

AUTO_STRUCT;
typedef struct ScheduledTaskInfo
{
	const char* pchName;			AST(POOL_STRING FORMATSTRING(HTML=1))
	U32 uiLastRun;					AST(NAME(LastRun) FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U32 uiNextRun;					AST(NAME(NextRun) FORMATSTRING(HTML_SECS = 1))
	U32 uiAvgDuration;				AST(NAME(AverageRuntime) FORMATSTRING(HTML_SECS_DURATION = 1))
	bool bDisabled;
	char* pchEnableTask;			AST(ESTRING FORMATSTRING(command=1))
	char* pchDisableTask;			AST(ESTRING FORMATSTRING(command=1))
}ScheduledTaskInfo;

AUTO_STRUCT;
typedef struct ScheduledTaskDownloadLink
{
	char* pchName;					AST(ESTRING FORMATSTRING(HTML=1))
	char* pchLink;					AST(ESTRING FORMATSTRING(HTML=1))
	U32 uiLastModified;				AST(NAME(TimeModified) FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U64 uiSizeInBytes;				AST(NAME(Filesize) FORMATSTRING(HTML_BYTES=1))
}ScheduledTaskDownloadLink;

AUTO_STRUCT;
typedef struct ScheduledTasksMonitorInfoStruct
{
	char* pchStatus;				AST(ESTRING FORMATSTRING(HTML=1))
	ScheduledTaskInfo** eaTasks;
	ScheduledTaskDownloadLink** eaDownloads;
	char* pchDisableScheduler;			AST(ESTRING FORMATSTRING(command=1))
	char* pchEnableScheduler;			AST(ESTRING FORMATSTRING(command=1))
} ScheduledTasksMonitorInfoStruct;

void LogParserScheduledTasks_OncePerFrame();
void LogParserScheduledTasks_Init();
void LogParserScheduledTasks_ValidateTasks();
void LogParserScheduledTasks_RunTask();

#include "LogParserScheduledTasks_h_ast.h"