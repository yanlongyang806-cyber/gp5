#pragma once
#include "stringUtil.h"
#include "NameValuePair.h"
#include "winInclude.h"
#include "ContinuousBuilder_Pub.h"

#define GIMMEINTS 0




AUTO_STRUCT;
typedef struct CBCompileCommand
{
	char *pCommand;
	bool bIgnoreResult;
} CBCompileCommand;




AUTO_STRUCT;
typedef struct CBRevision
{
	U32 iSVNRev;
	U32 iGimmeTime; AST(NAME(GimmeTime, GimmeRev))
} CBRevision;


//function used in suspectList.c
typedef struct ErrorTrackerEntryList ErrorTrackerEntryList;
bool ErrorListIsErrorsOnly(ErrorTrackerEntryList *pList);

typedef struct ErrorTrackerContext ErrorTrackerContext;
extern ErrorTrackerContext *pCurErrorContext;




typedef enum enumEmailFlags
{
	EMAILFLAG_NO_DEFAULT_RECIPIENTS = 1 << 0,
	EMAILFLAG_HIGH_PRIORITY = 1 << 1,
	EMAILFLAG_HTML = 1 << 2,
	EMAILFLAG_HIGHLIGHT_RECIPIENT_NAMES = 1 << 3,
} enumEmailFlags;


void SendEmailToList_Internal(enumEmailFlags eFlags, char ***pppList, char *pFileName, char *pSubjectLine);
void SendEmailToList_WithAttachment_Internal(enumEmailFlags eFlags, char ***pppList, char *pFileName, char *pSubjectLine, char* pAttachmentFile, char* pAttachmentName, char* pMimeType);

extern bool gbFastTestingMode;
extern ParseTable parse_NameValuePairList[];
#define TYPE_parse_NameValuePairList NameValuePairList
extern ParseTable parse_NameValuePair[];
#define TYPE_parse_NameValuePair NameValuePair
typedef struct ErrorEntry ErrorEntry;
typedef struct CBConfig CBConfig;
typedef struct CBProduct CBProduct;
typedef struct CBType CBType;
typedef struct BuildScriptingContext BuildScriptingContext;

AUTO_STRUCT;
typedef struct CBRunSummary
{
	CBProduct *pProduct;
	CBType *pType;
	CBConfig *pConfig;

	U32 iBuildStartTime;
	U32 iBuildFinishTime;
	enumCBResult eResult;
	CBRevision revision;
	ErrorEntry **ppErrors; AST(LATEBIND)
	NameValuePairList *pScriptingVariables; AST(LATEBIND)
} CBRunSummary;

AUTO_STRUCT;
typedef struct CBRunSummaryList
{
	CBRunSummary **ppSummaries;
} CBRunSummaryList;

void PackageLogsForPublishing( char* packageName );
void PublishLogs( void );

void RelocateLogs( void );

void DumpCBSummary(void);

void WriteCurrentStateToXML(char* pXMLFileName);


bool PickerWithDescriptions(char *pPickerName, char *pPickerLabel, char ***pppInChoices, char ***pppInDescriptions, char **ppOutChoice, char *pStartingChoice);

char *GetCBLogDirectoryName(BuildScriptingContext *pContext);

char *GetCBLogDirectoryNameFromTime(U32 iTime);

extern bool gbIsOnceADayBuild;

extern HWND ghCBDlg;

extern U32 giLastTimeStarted;

const char *GetLastResultStateString(void);
const char *GetLastNonAbortResultStateString(void);

AUTO_ENUM;
typedef enum CommentCategory
{
	COMMENT_STATE,
	COMMENT_SUBSTATE,
	COMMENT_SUBSUBSTATE,
	COMMENT_COMMENT,
	COMMENT_SCRIPTING,
} CommentCategory;
void AddComment(CommentCategory eCategory, char *pString, ...);

//the "key" that identifies individual builds for log dir names and on the cb monitor
extern char *gpLastTimeStartedString;

void CB_GetDescriptiveStateString(char **ppOutString);

void CB_AddBuildTypeSpecificStartingVariables(void);

char *GetBuilderComment(void);

bool CB_IsWaitingBetweenBuilds(void);


char *CBGetPresumedGimmeProjectAndBranch(void);
char *CBGetPresumedCoreGimmeBranch(void);
char *CBGetPresumedSVNBranch(void);


typedef struct BuildScriptingContext BuildScriptingContext;

BuildScriptingContext *CBGetRootScriptingContext(void);