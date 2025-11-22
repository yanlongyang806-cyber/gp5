#pragma once
#include "alerts.h"

#define MAX_ALERTTRACKER_THROTTLE_HOWMANY 1000

typedef struct EventCounter EventCounter;
typedef struct Alert Alert;
typedef struct CriticalSystem_Status CriticalSystem_Status;
typedef struct SimpleEventCounter SimpleEventCounter;

AUTO_STRUCT;
typedef struct AlertCounts_SingleCritSys
{
	char *pCritSysName; AST(KEY)
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
	int iTotalCount;
	int iLast15Minutes;
	int iLastHour;
	int iLast6Hours;
	int iLastDay;
	EventCounter *pCounter; NO_AST
} AlertCounts_SingleCritSys;

AUTO_STRUCT;
typedef struct AlertTextsPerSystem
{
	char *pCritSysName; AST(KEY)
	char **ppRecentFullTexts; AST(FORMATSTRING(HTML_PREFORMATTED = 1))
} AlertTextsPerSystem;

AUTO_ENUM;
typedef enum AlertTrackerNetopsCommentType
{
	COMMENTTYPE_PERMANENT,
	COMMENTTYPE_TIMED,
	COMMENTTYPE_TIMED_RESETTING,
} AlertTrackerNetopsCommentType;

AUTO_STRUCT;
typedef struct AlertTrackerNetopsComment
{
	const char *pParentKey; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	int iID; AST(FORMATSTRING(HTML_SKIP=1)) //unique to this alerttracker
	AlertTrackerNetopsCommentType eType;
	U32 iLifespan; AST(FORMATSTRING(HTML_SECS_DURATION=1))
	U32 iCommentCreationTime; AST(FORMATSTRING(HTML_SECS_AGO=1)) //resets every time the comment happens if type is COMMENTTYPE_TIMED_RESETTING
	char *pText;
	char **ppSystemOrCategoryNames;
	AST_COMMAND("RemoveComment", "RemoveComment $FIELD(ParentKey) $FIELD(ID) $CONFIRM(Really remove this comment?)")
} AlertTrackerNetopsComment;

AUTO_STRUCT;
typedef struct AlertTrackerThrottle_PerSystem
{
	char *pSystemName; AST(KEY)
	SimpleEventCounter *pCounter; NO_AST
} AlertTrackerThrottle_PerSystem;

AUTO_STRUCT;
typedef struct AlertTrackerThrottle
{
	const char *pParentKey; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	int iID; AST(FORMATSTRING(HTML_SKIP=1)) //unique to this alerttracker
	char **ppSystems; //each one is a product name, system name, or "all"
	int iHowMany; //how many times it has to happen
	int iWithin; AST(FORMATSTRING(HTML_SECS_DURATION=1)) //within how many seconds
	int iResetLength; AST(FORMATSTRING(HTML_SECS_DURATION=1)) //then don't show it again for this many seconds
	U32 iTurnoffTime; AST(FORMATSTRING(HTML_SECS=1))

	AlertTrackerThrottle_PerSystem **ppPerSystemThrottles; AST(FORMATSTRING(HTML_SKIP=1) NO_TEXT_SAVE)

	AST_COMMAND("RemoveThrottle", "RemoveThrottle $FIELD(ParentKey) $FIELD(ID) $STRING(Type yes to remove this)")
} AlertTrackerThrottle;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "SetRedirect, AddComment, AddThrottle, Throttles, Comment, CommentType, Systems, Level, TotalCount, RedirectAddress");
typedef struct AlertTracker
{
	const char *pAlertKey; AST(POOL_STRING KEY)
	enumAlertCategory eCategory;
	enumAlertLevel eLevel;
	int iTotalCount; AST(NO_TEXT_SAVE)
	int iLast15Minutes; AST(NO_TEXT_SAVE)
	int iLastHour; AST(NO_TEXT_SAVE)
	int iLast6Hours; AST(NO_TEXT_SAVE)
	int iLastDay; AST(NO_TEXT_SAVE)
	EventCounter *pCounter; NO_AST
	AlertCounts_SingleCritSys **ppCountsPerSystem; AST(NO_TEXT_SAVE)
	AlertTextsPerSystem **ppTextsPerSystem; AST(NO_TEXT_SAVE)
	AlertTrackerNetopsComment **ppNetopsComments;
	AlertTrackerThrottle **ppThrottles;

	char *pComment_ForServerMonitoring; AST(NAME(Comment), NO_TEXT_SAVE, ESTRING)
	char *pCommentType_ForServerMonitoring; AST(NAME(CommentType), NO_TEXT_SAVE, ESTRING)
	char *pSystemsString_ForServerMonitoring; AST(NAME(Systems), NO_TEXT_SAVE, ESTRING)

	//if set, then redirect any email about this alert to only this address
	char *pRedirectAddress; AST(ESTRING)

	AST_COMMAND("SetRedirect", "SetRedirectForAlertTracker $FIELD(AlertKey) $STRING(Redirect these alerts to... or empty for none)")
	AST_COMMAND("AddComment", "AddCommentForAlertTracker $FIELD(AlertKey) $SELECT(Comment type|NAMELIST_AlertTrackerNetopsCommentType) $INT(Days duration) \"$STRING(System or category names, or all)\" \"$STRING(Comment text)\"")
	AST_COMMAND("AddThrottle", "AddThrottleForAlertTracker $FIELD(AlertKey) \"$STRING(To which systems or products or all)\" $INT(How many) $INT(Within how many seconds) $INT(After alerting, can't alert again for this many minutes, default 5) $INT(Turn off entirely after how many hours, or 0 for perm)")
} AlertTracker;

AUTO_STRUCT;
typedef struct AlertTrackerList
{
	AlertTracker **ppTrackers;
} AlertTrackerList;



//CONFUSING TERMINOLOGY ALERT... there are two different alert throttling systems in place on the controller Tracker. There's one
//that lives on alert trackers and allows netops to say "only page me if I get X of these within Y seconds". Then there's
//the function DoAlertThrottling and its associated functions and stuff, which group emails together so that when
//an alert comes in a bunch of times quickly, the 2nd-through-nth of them get stuck together into one long email



//when you track an alert, it tells you whether or not it's being throttled by the alert tracker throttling system,
//which affects whether or not it shoudl be sent to the short addresses, ie, pagers
typedef enum enumAlertTrackerThrottlingState
{
	ALERTTHROTTLING_NOT_THROTTLED, 
	//throttling not on for this alert at all, do nothing
	
	ALERTTHROTTLING_THROTTLING_ON_NOT_MET, 
	//throttling is on for this alert, and the throttling condition was not met, so send only to long addresses

	ALERTTHROTTLING_THROTTLING_ON_AND_MET, 
	//throttling is on for this alert, the condition was met, send this to short addresses, with a prefix
	//explaining the throttling
} enumAlertTrackerThrottlingState; 	




enumAlertTrackerThrottlingState AlertTrackers_TrackAlert(Alert *pAlert, CriticalSystem_Status *pSystem, char **ppThrottlingComment);
void AlertTrackers_InitSystem(void);

//ppSystemOrCategoryNames is an earray of strings, will get all comments that match any system or category
//in that list. If bExpandList is true, then it does one pass of expanding the list by adding in all categories 
//belonging to systems in the list, and all systems belonging to categories in the list
char *AlertTrackers_GetCommentsForAlert(const char *pAlertKey, char **ppSystemOrCategoryNames, bool bExpandList);
char *AlertTrackers_GetCommentsForAlert_OneSystemOrCategory(const char *pAlertKey, char *pSystemOrCategoryName, bool bExpandList);

char *AlertTrackers_GetRedirectAddressForAlert(const char *pAlertKey);