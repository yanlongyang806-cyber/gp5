
	
#include "ActivityCalendar.h"
#include "ActivityCommon.h"
#include "Expression.h"
#include "GameClientLib.h"
#include "gclActivity.h"
#include "gclEntity.h"
#include "Player.h"
#include "Timing.h"
#include "UIGen.h"
#include "fileutil.h"
#include "Guild.h"

#include "AutoGen/gclCalendarUI_c_ast.h"
#include "AutoGen/ActivityCalendar_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct tm TimeStruct;

// Get the timezone offset for the server.
extern S32 gclUIGen_GetServerUTCOffset(U32 uTimestamp);

extern U32 gclActivity_EventClock_GetSecondsSince2000();  // gclActivity.c

AUTO_STRUCT;
typedef struct CalendarEntry
{
	CalendarEvent* pEvent;				AST(UNOWNED)
	CalendarTiming* pTiming;			AST(UNOWNED)
	CalendarTiming* pOwnedTiming;
	S32 iSortOrder;
	U32 bEventActive : 1;				AST(NAME(EventActive))
} CalendarEntry;

AUTO_STRUCT;
typedef struct CalendarDay
{
	U32 uTimestampBegin; AST(NAME(TimestampBegin))
	U32 uTimestampEnd; AST(NAME(TimestampEnd))

	U16 uYear; AST(NAME(Year))
	U8 uMonth; AST(NAME(month))
	U8 uDay; AST(NAME(Day))
	U8 uDayOfWeek; AST(NAME(DayOfWeek))
	U8 uWeekOfMonth; AST(NAME(WeekOfMonth))

	// Event information for this day, sorted into a spanning list display order
	S32 iEventCount; AST(NAME(EventCount))
	CalendarEntry **eaEvents; AST(NAME(Events))

	// Events on this day that aren't sorted into a spanning list display order
	S32 iBackgroundEventCount; AST(NAME(BackgroundEventCount))
	CalendarEntry **eaBackgroundEvents; AST(NAME(BackgroundEvents))
} CalendarDay;

AUTO_STRUCT;
typedef struct CalendarEntryColumn
{
	CalendarEntry *pEntry; AST(UNOWNED)
	U32 iColumn;
} CalendarEntryColumn;

AUTO_STRUCT;
typedef struct CalendarEventDebug
{
	const char *pchEventName; AST(POOL_STRING)
	const char *pchStartDate;
	const char *pchEndDate;
	U32 uStartDate; AST(NO_TEXT_SAVE)
	U32 uEndDate; AST(NO_TEXT_SAVE)
} CalendarEventDebug;

AUTO_STRUCT;
typedef struct CalendarEventDebugData
{
	CalendarEventDebug** eaEvents;
} CalendarEventDebugData;

static CalendarEvent** s_eaGuildEvents;

__forceinline static void gclCalendarSetDay(CalendarDay *pDay, U32 uDayTimestamp, TimeStruct *pTime, U32 uPeriod)
{
	int iDayOfFirst;
	pDay->uTimestampBegin = uDayTimestamp;
	pDay->uTimestampEnd = uDayTimestamp + uPeriod - SECONDS(1);

	pDay->uDay = pTime->tm_mday;
	pDay->uDayOfWeek = pTime->tm_wday;
	pDay->uMonth = pTime->tm_mon;
	pDay->uYear = pTime->tm_year + 1900;

	iDayOfFirst = pTime->tm_wday - (pTime->tm_mday - 1) % 7;
	iDayOfFirst += iDayOfFirst < 0 ? 7 : 0;
	pDay->uWeekOfMonth = (pTime->tm_mday - 1 + iDayOfFirst) / 7 + ((iDayOfFirst != 0) ? 1 : 0);
}

static int CalendarEntrySorter(const CalendarEntry **ppEventL, const CalendarEntry **ppEventR, const void *pUserData)
{
	// No timing should always come before anything with timing.
	if ((*ppEventL)->pTiming==NULL)
	{
		if ((*ppEventR)->pTiming==NULL)
		{
			return(0);
		}
		return(-1);
	}

	if ((*ppEventR)->pTiming==NULL)
	{
		return(1);
	}
		
	if ((*ppEventL)->pTiming->uStartDate < (*ppEventR)->pTiming->uStartDate)
		return -1;
	if ((*ppEventL)->pTiming->uStartDate > (*ppEventR)->pTiming->uStartDate)
		return 1;
	return 0;
}

static void gclCalendarFillEntry(CalendarEntry* pEntry, CalendarEvent* pEvent, CalendarTiming* pTiming, ActivityDisplayTags **eaiSortOrder)
{
	U32 uCurrentServerTime = gclActivity_EventClock_GetSecondsSince2000();
	pEntry->pEvent = pEvent;
	pEntry->pTiming = pTiming;

	if (pTiming==NULL)
	{
		pEntry->bEventActive = true;
	}
	else
	{
		pEntry->bEventActive = pEvent->bEventActiveOnServer && (pTiming->uStartDate <= uCurrentServerTime) && (pTiming->uEndDate > uCurrentServerTime);
	}

	if (eaiSortOrder)
	{
		S32 i, iIndex, iOrder = eaiSize(eaiSortOrder);
		for (i = ea32Size(&pEvent->uDisplayTags) - 1; i >= 0; i--)
		{
			if ((iIndex = eaiFind(eaiSortOrder, pEvent->uDisplayTags[i])) >= 0)
				iOrder = MIN(iOrder, iIndex);
		}
		pEntry->iSortOrder = iOrder;
	}
	else
	{
		pEntry->iSortOrder = 0;
	}
}

static void ActivityParseTags(U32** ppiDestTags, const char* pchTagsString)
{
	char *pchData;
	char *pchFind;
	char *pchContext;

	strdup_alloca(pchData, pchTagsString);
	ea32ClearFast(ppiDestTags);
	if (pchFind = strtok_r(pchData," ,\r\n\t",&pchContext))
	{
		do
		{
			U32 uFind = StaticDefineIntGetInt(ActivityDisplayTagsEnum,pchFind);
			if(uFind != -1)
				ea32Push(ppiDestTags, uFind);
		} while (pchFind = strtok_r(NULL," ,\r\n\t",&pchContext));
	}
}


static int ActivityFillCalendarEntries(CalendarEntry ***peaCalendarEntries, CalendarEvent** eaCalendarEvents,
									   U32 uDateStart, U32 uDateEnd,
									   const char *pchIncludeTags, const char *pchExcludeTags,
									   bool bIncludeOtherMaps,
									   bool bIncludeManualOn,
									   bool bExcludeManualOff)
{
	int iCount = 0;
	int i, j;
	U32 *eaIncludeTags = NULL;
	U32 *eaExcludeTags = NULL;

	ea32Create(&eaIncludeTags);
	ea32Create(&eaExcludeTags);

	ActivityParseTags(&eaIncludeTags, pchIncludeTags);
	ActivityParseTags(&eaExcludeTags, pchExcludeTags);

	for (i = 0; i < eaSize(&eaCalendarEvents); i++)
	{
		CalendarEvent* pCalendarEvent = eaCalendarEvents[i];

		if (pCalendarEvent->bEventAffectsCurrentMap || bIncludeOtherMaps)
		{
			if (!ActivityCalendarFilterByTag(pCalendarEvent->uDisplayTags, eaIncludeTags, eaExcludeTags))
			{
				// eaTiming could be NULL. Which indicates a manually on event. It could also be manually on even if we have an eaTiming entry.
				//  In that case, pretend we DON'T have one but only if we are paying attention to ManualOn.

				if (bIncludeManualOn && (eaSize(&pCalendarEvent->eaTiming)<=0 || pCalendarEvent->iEventRunMode == kEventRunMode_ForceOn))
				{
					if (peaCalendarEntries==NULL)
					{
						// Count only
						iCount++;
					}
					else
					{
						CalendarEntry* pEntry = eaGetStruct(peaCalendarEntries, parse_CalendarEntry, iCount++);
						gclCalendarFillEntry(pEntry, pCalendarEvent, NULL, NULL);
					}
				}

				// Exclude Foreced Off events if specified
				else if (!(bExcludeManualOff && pCalendarEvent->iEventRunMode == kEventRunMode_ForceOff))
				{
					for (j = 0; j < eaSize(&pCalendarEvent->eaTiming); j++)
					{
						CalendarTiming* pCalendarTiming = pCalendarEvent->eaTiming[j];
						if ((uDateStart==0 && uDateEnd==0) || MAX(pCalendarTiming->uStartDate,uDateStart)<=MIN(pCalendarTiming->uEndDate,uDateEnd))
						{
							if (peaCalendarEntries==NULL)
							{
								// Count only
								iCount++;
							}
							else
							{
								CalendarEntry* pEntry = eaGetStruct(peaCalendarEntries, parse_CalendarEntry, iCount++);
								gclCalendarFillEntry(pEntry, pCalendarEvent, pCalendarTiming, NULL);
							}
						}
					}
				}
			}
		}
	}
	
	ea32Destroy(&eaIncludeTags);
	ea32Destroy(&eaExcludeTags);

	return iCount;
}

typedef enum ActivityEventQuery
{
	ActivityEventQuery_Requested,
	ActivityEventQuery_Active,
	ActivityEventQuery_Server,
	
} ActivityEventQuery;

static int ActivityGetCalendarEntries(CalendarEntry ***peaCalendarEntries,
									  U32 uDateStart, U32 uDateEnd,
									  const char *pchTagsToInclude, const char *pchTagsToExclude,
										bool bIncludeOtherMaps,
										bool bIncludeManualOn,
										bool bExcludeManualOff,
										ActivityEventQuery eQueryType)
{
	Entity* pEnt = entActivePlayerPtr();
	int iCount = 0;

	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pEventInfo)
	{
		CalendarEvent** peaCalendarEvents=NULL;

		if (eQueryType == ActivityEventQuery_Requested)
		{
			peaCalendarEvents = pEnt->pPlayer->pEventInfo->eaRequestedEvents;
		}
		else if (eQueryType == ActivityEventQuery_Active)
		{
			peaCalendarEvents = pEnt->pPlayer->pEventInfo->eaActiveEvents;
		}
		else if (eQueryType == ActivityEventQuery_Server)
		{
			if (pEnt->pPlayer->pEventInfo->pServerCalendar)
			{
				peaCalendarEvents = pEnt->pPlayer->pEventInfo->pServerCalendar->eaEvents;
			}
		}

		if (peaCalendarEvents!=NULL)
		{
			iCount = ActivityFillCalendarEntries(peaCalendarEntries,peaCalendarEvents,
												 uDateStart,uDateEnd,
												 pchTagsToInclude, pchTagsToExclude,
												 bIncludeOtherMaps,
												 bIncludeManualOn,
												 bExcludeManualOff);
		}
	}

	// peaCalendarEntries will be NULL if we are just counting
	if (peaCalendarEntries!=NULL)
	{
		eaSetSizeStruct(peaCalendarEntries, parse_CalendarEntry, iCount);
		eaStableSort(*peaCalendarEntries, NULL, CalendarEntrySorter);
	}

	return iCount;
}


static void gclCalendarCreateGuildEvents(Entity *pEnt)
{
	S32 i, j, iEventCount = 0;
	Guild *pGuild = guild_GetGuild(pEnt);
	U32 uNow = gclActivity_EventClock_GetSecondsSince2000();

	if (pEnt && pGuild)
	{
		for (i = 0; i < eaSize(&pGuild->eaEvents); i++)
		{
			GuildEvent *pGuildEvent = pGuild->eaEvents[i];
			CalendarEvent *pCalendarEvent;
			char achBuffer[32];
			const char *pchEventName;
			S32 iCur, iMax = 0;

			sprintf(achBuffer, "%u", pGuildEvent->uiID);
			pchEventName = allocAddString(achBuffer);

			// Find an existing structure
			for (j = iEventCount; j < eaSize(&s_eaGuildEvents); j++)
			{
				if (s_eaGuildEvents[j]->pchEventName == pchEventName)
				{
					if (j != iEventCount)
						eaMove(&s_eaGuildEvents, iEventCount, j);
					break;
				}
			}
			pCalendarEvent = eaGetStruct(&s_eaGuildEvents, parse_CalendarEvent, iEventCount++);

			if (!pCalendarEvent->pchDisplayName || stricmp(pCalendarEvent->pchDisplayName, pGuildEvent->pcTitle))
				StructCopyString(&pCalendarEvent->pchDisplayName, pGuildEvent->pcTitle);
			if (!pCalendarEvent->pchDisplayDescLong || stricmp(pCalendarEvent->pchDisplayDescLong, pGuildEvent->pcDescription))
				StructCopyString(&pCalendarEvent->pchDisplayDescLong, pGuildEvent->pcDescription);

			if (pGuildEvent->eRecurType)
				iMax = (uNow - pGuildEvent->iStartTimeTime) / DAYS(pGuildEvent->eRecurType);
			iCur = MIN(iMax, MAX(0, pGuildEvent->iRecurrenceCount));
			eaSetSizeStruct(&pCalendarEvent->eaTiming, parse_CalendarTiming, iCur);
			for (j = 0; j <= iCur; j++)
			{
				CalendarTiming *pTiming = eaGetStruct(&pCalendarEvent->eaTiming, parse_CalendarTiming, j);
				pTiming->uStartDate = pGuildEvent->iStartTimeTime + DAYS(pGuildEvent->eRecurType) * j;
				pTiming->uEndDate = pTiming->uStartDate + pGuildEvent->iDuration;
			}
		}
	}

	eaSetSizeStruct(&s_eaGuildEvents, parse_CalendarEvent, iEventCount);
}

// Default to server time
static bool s_bCalendarLocalMode = false;
static S32 s_iCalendarFirstDay = 0;

// A helper global entry (reduce some processing overhead)
// a.k.a. a really cheap hack
static CalendarEntry s_LastCalendarEntry;

// Toggle local/server time for the calendar.
AUTO_CMD_INT(s_bCalendarLocalMode, CalendarLocalMode) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Get the local/server time flag for the calendar.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarGetLocalMode");
bool gclExprCalendarUI_GetLocalMode(void)
{
	return s_bCalendarLocalMode;
}

// Toggle local/server time for the calendar.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarSetLocalMode");
void gclExprCalendarUI_SetLocalMode(bool bLocalMode)
{
	s_bCalendarLocalMode = bLocalMode;
}

// Set the day of week to be considered the "first day". 0 = Sunday, 6 = Saturday
AUTO_CMD_INT(s_iCalendarFirstDay, CalendarFirstDay) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Get the day of week to be considered the "first day". 0 = Sunday, 6 = Saturday
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarGetFirstDay");
S32 gclExprCalendarUI_GetFirstDay(void)
{
	return s_iCalendarFirstDay;
}

// Set the day of week to be considered the "first day". 0 = Sunday, 6 = Saturday
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarSetFirstDay");
void gclExprCalendarUI_SetFirstDay(S32 iFirstDay)
{
	s_iCalendarFirstDay = iFirstDay;
}

// Get the last calendar entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarLastEntry");
SA_RET_NN_VALID CalendarEntry *gclExprCalendarUI_GetLastCalendarEntry(void)
{
	return &s_LastCalendarEntry;
}

// Set the last calendar entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarSetLastEntry");
SA_RET_OP_VALID CalendarEntry *gclExprCalendarUI_SetLastCalendarEntry(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry)
		StructCopyAll(parse_CalendarEntry, pEntry, &s_LastCalendarEntry);
	else
		StructReset(parse_CalendarEntry, &s_LastCalendarEntry);
	return pEntry;
}

static void MakeTimeStruct(bool bLocalMode, U32 uTime, TimeStruct *pTimeStruct)
{
	if (bLocalMode)
	{
		timeMakeLocalTimeStructFromSecondsSince2000(uTime, pTimeStruct);
		return;
	}

	// Adjust for UTC offset
	uTime += gclUIGen_GetServerUTCOffset(uTime) * HOURS(1);
	timeMakeTimeStructFromSecondsSince2000(uTime, pTimeStruct);
}

static U32 MakeTimeFromStruct(bool bLocalMode, const TimeStruct *pTimeStruct)
{
	TimeStruct tm = *pTimeStruct;
	U32 uTime;

	if (bLocalMode)
	{
		// normalize time
		if (tm.tm_sec < 0)
		{
			tm.tm_min -= (59 - tm.tm_sec) / 60;
			tm.tm_sec = (60 - (-tm.tm_sec % 60)) % 60;
		}
		else if (tm.tm_sec >= 24)
		{
			tm.tm_min += tm.tm_sec / 60;
			tm.tm_sec = tm.tm_sec % 60;
		}

		if (tm.tm_min < 0)
		{
			tm.tm_hour -= (59 - tm.tm_min) / 60;
			tm.tm_min = (60 - (-tm.tm_min % 60)) % 60;
		}
		else if (tm.tm_min >= 24)
		{
			tm.tm_hour += tm.tm_min / 60;
			tm.tm_min = tm.tm_min % 60;
		}

		if (tm.tm_hour < 0)
		{
			tm.tm_mday -= (23 - tm.tm_hour) / 24;
			tm.tm_hour = (24 - (-tm.tm_hour % 24)) % 24;
		}
		else if (tm.tm_hour >= 24)
		{
			tm.tm_mday += tm.tm_hour / 24;
			tm.tm_hour = tm.tm_hour % 24;
		}

		if (tm.tm_mon < 0)
		{
			tm.tm_year -= (11 - tm.tm_mon) / 12;
			tm.tm_mon = (12 - (-tm.tm_mon % 12)) % 12;
		}
		else if (pTimeStruct->tm_mon >= 12)
		{
			tm.tm_year += tm.tm_mon / 12;
			tm.tm_mon = tm.tm_mon % 12;
		}

		// TODO: normalize the day without requiring a loop
		while (tm.tm_mday <= 0)
		{
			tm.tm_mon--;
			if (tm.tm_mon < 0)
			{
				tm.tm_year--;
				tm.tm_mon = 11;
			}
			tm.tm_mday += timeDaysInMonth(tm.tm_mon, tm.tm_year + 1900);
		}
		while (tm.tm_mday > timeDaysInMonth(tm.tm_mon, tm.tm_year + 1900))
		{
			tm.tm_mday -= timeDaysInMonth(tm.tm_mon, tm.tm_year + 1900);
			tm.tm_mon++;
			if (tm.tm_mon >= 12)
			{
				tm.tm_mon = 0;
				tm.tm_year++;
			}
		}

		return timeGetSecondsSince2000FromLocalTimeStruct(&tm);
	}

	// Adjust for UTC offset
	uTime = timeGetSecondsSince2000FromTimeStruct(&tm);
	uTime -= gclUIGen_GetServerUTCOffset(uTime) * HOURS(1);
	return uTime;
}

static CalendarDay s_Now;
static U32 s_uNowFrame;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampYear");
U32 gclExprCalendarUI_GetTimestampYear(U32 uTimestamp)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	return tm.tm_year + 1900;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSetYear");
U32 gclExprCalendarUI_GetTimestampSetYear(U32 uTimestamp, U32 uYear)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	tm.tm_year = uYear - 1900;
	return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampMonth");
U32 gclExprCalendarUI_GetTimestampMonth(U32 uTimestamp)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	return tm.tm_mon;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSetMonth");
U32 gclExprCalendarUI_GetTimestampSetMonth(U32 uTimestamp, U32 uMonth)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	tm.tm_mon = uMonth;
	return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampDay");
U32 gclExprCalendarUI_GetTimestampDay(U32 uTimestamp)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	return tm.tm_mday;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSetDay");
U32 gclExprCalendarUI_GetTimestampSetDay(U32 uTimestamp, U32 uDay)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	tm.tm_mday = uDay;
	return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampHours");
U32 gclExprCalendarUI_GetTimestampHours(U32 uTimestamp)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	return tm.tm_hour;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSetHours");
U32 gclExprCalendarUI_GetTimestampSetHours(U32 uTimestamp, U32 uHours)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	tm.tm_hour = uHours;
	return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampMinutes");
U32 gclExprCalendarUI_GetTimestampMinutes(U32 uTimestamp)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	return tm.tm_min;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSetMinutes");
U32 gclExprCalendarUI_GetTimestampSetMinutes(U32 uTimestamp, U32 uMinutes)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	tm.tm_min = uMinutes;
	return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSeconds");
U32 gclExprCalendarUI_GetTimestampSeconds(U32 uTimestamp)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	return tm.tm_sec;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampSetSeconds");
U32 gclExprCalendarUI_GetTimestampSetSeconds(U32 uTimestamp, U32 uSeconds)
{
	TimeStruct tm = {0};
	MakeTimeStruct(s_bCalendarLocalMode, uTimestamp, &tm);
	tm.tm_sec = uSeconds;
	return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
}

void gclCalendar_OncePerFrame(void)
{
	if (gGCLState.totalElapsedTimeMs != s_uNowFrame)
	{
		// U32 uNow = timeServerSecondsSince2000();
		U32 uNow = gclActivity_EventClock_GetSecondsSince2000();
		TimeStruct tm = {0};

		MakeTimeStruct(s_bCalendarLocalMode, uNow, &tm);
		uNow -= HOURS(tm.tm_hour) + MINUTES(tm.tm_min) + SECONDS(tm.tm_sec);
		gclCalendarSetDay(&s_Now, uNow, &tm, DAYS(1));

		s_uNowFrame = gGCLState.totalElapsedTimeMs;

		gclCalendarCreateGuildEvents(entActivePlayerPtr());
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarCurrentDay");
U32 gclExprCalendarUI_GetCurrentDay(void)
{
	gclCalendar_OncePerFrame();
	return s_Now.uDay;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarCurrentDayOfWeek");
U32 gclExprCalendarUI_GetCurrentDayOfWeek(void)
{
	gclCalendar_OncePerFrame();
	return s_Now.uDayOfWeek;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarCurrentWeekOfMonth");
U32 gclExprCalendarUI_GetCurrentWeekOfMonth(void)
{
	gclCalendar_OncePerFrame();
	return s_Now.uWeekOfMonth;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarCurrentMonth");
U32 gclExprCalendarUI_GetCurrentMonth(void)
{
	gclCalendar_OncePerFrame();
	return s_Now.uMonth;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarCurrentYear");
U32 gclExprCalendarUI_GetCurrentYear(void)
{
	gclCalendar_OncePerFrame();
	return s_Now.uYear;
}

// Calculate a timestamp for a given day
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampForDay");
U32 gclExprCalendarUI_GetTimestamp(S32 iYear, S32 iMonth, S32 iDay)
{
	TimeStruct tm = {0};

	// Normalize month
	if (iMonth < 0)
	{
		iYear -= (11 - iMonth) / 12;
		iMonth = -iMonth % 12;
	}
	else if (iMonth >= 12)
	{
		iYear += iMonth / 12;
		iMonth = iMonth % 12;
	}

	// Ensure that the timestamp will be reasonable...
	if (iYear >= 2000 && iYear < 2136)
	{
		tm.tm_year = iYear - 1900;
		tm.tm_mon = iMonth;
		tm.tm_mday = iDay; // Remember: days are one-based for some reason
		return MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
	}

	return 0;
}

// Deprecated GenGetCalendarEntryColumns (was a FightClub thing)
//AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCalendarEntryColumns);
//int expr_ActivityGetCalendarEntryColumns(SA_PARAM_NN_VALID UIGen *pGen, U32 uDateStart, U32 uDateEnd, const char *pchTagsToInclude, const char *pchTagsToExclude)

// Calculate a timestamp for a given month
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarTimestampForMonth");
U32 gclExprCalendarUI_GetTimestampForMonth(S32 iYear, S32 iMonth)
{
	return gclExprCalendarUI_GetTimestamp(iYear, iMonth, 1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCalendarGetTimeSegments");
void gclExprCalendarUI_GetTimeSegments(SA_PARAM_NN_VALID UIGen *pGen, U32 uStartTime, U32 uEndTime, U32 uPeriod, bool bLocalMode)
{
	CalendarDay ***peaCalendarDays = ui_GenGetManagedListSafe(pGen, CalendarDay);
	TimeStruct tm = {0};
	S32 iCount = 0;
	U32 uTime = uStartTime;

	while (uTime < uEndTime)
	{
		CalendarDay *pDay = eaGetStruct(peaCalendarDays, parse_CalendarDay, iCount++);

		MakeTimeStruct(bLocalMode, uTime, &tm);
		gclCalendarSetDay(pDay, uTime, &tm, uPeriod);

		// Reset event information
		pDay->iEventCount = 0;

		uTime += uPeriod;
	}

	eaSetSizeStruct(peaCalendarDays, parse_CalendarDay, iCount);
	ui_GenSetManagedListSafe(pGen, peaCalendarDays, CalendarDay, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCalendarGetMonth");
void gclExprCalendarUI_GetMonth(SA_PARAM_NN_VALID UIGen *pGen, S32 iYear, S32 iMonth)
{
	// Normalize month
	if (iMonth < 0)
	{
		iYear -= -iMonth / 12;
		iMonth = -iMonth % 12;
	}
	else if (iMonth >= 12)
	{
		iYear += iMonth / 12;
		iMonth = iMonth % 12;
	}

	// Ensure that the timestamp will be reasonable...
	if (iYear >= 2000 && iYear < 2136)
	{
		U32 uMonthStartTime, uMonthEndTime;
		TimeStruct MonthStart = {0}, MonthEnd = {0}, tm = {0};

		tm.tm_year = iYear - 1900;
		tm.tm_mon = iMonth;
		tm.tm_mday = 1;
		uMonthStartTime = MakeTimeFromStruct(s_bCalendarLocalMode, &tm);
		MakeTimeStruct(s_bCalendarLocalMode, uMonthStartTime, &MonthStart);

		if (++tm.tm_mon >= 12)
		{
			tm.tm_mon = 0;
			++tm.tm_year;
		}
		tm.tm_mday = 1;
		uMonthEndTime = MakeTimeFromStruct(s_bCalendarLocalMode, &tm) - 1;
		MakeTimeStruct(s_bCalendarLocalMode, uMonthEndTime, &MonthEnd);

		// Align times to show full weeks (this supports arbitrary starting day of weeks).
		MonthStart.tm_wday = (14 + MonthStart.tm_wday - (s_iCalendarFirstDay % 7)) % 7;
		MonthEnd.tm_wday = (14 + MonthEnd.tm_wday - (s_iCalendarFirstDay % 7)) % 7;

		uMonthStartTime -= DAYS(MonthStart.tm_wday);
		uMonthEndTime += DAYS(6 - MonthEnd.tm_wday);

		gclExprCalendarUI_GetTimeSegments(pGen, uMonthStartTime, uMonthEndTime, DAYS(1), s_bCalendarLocalMode);
	}
	else
	{
		gclExprCalendarUI_GetTimeSegments(pGen, 0, 0, DAYS(1), s_bCalendarLocalMode);
	}
}

static int CalendarEventTimeSorter(const CalendarEntry **ppEventL, const CalendarEntry **ppEventR, const void *pUserData)
{
	if ((*ppEventL)->iSortOrder < (*ppEventR)->iSortOrder)
		return -1;
	if ((*ppEventL)->iSortOrder > (*ppEventR)->iSortOrder)
		return 1;

	// No timing should always come before anything with timing.
	if ((*ppEventL)->pTiming==NULL)
	{
		if ((*ppEventR)->pTiming==NULL)
		{
			return(0);
		}
		return(-1);
	}

	if ((*ppEventR)->pTiming==NULL)
	{
		return(1);
	}
	
	if ((*ppEventL)->pTiming->uStartDate < (*ppEventR)->pTiming->uStartDate)
		return -1;
	if ((*ppEventL)->pTiming->uStartDate > (*ppEventR)->pTiming->uStartDate)
		return 1;
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCalendarAddEvents");
void gclExprCalendarUI_AddEvents(SA_PARAM_NN_VALID UIGen *pGen, const char *pchIncludeTags, const char *pchExcludeTags)
{	
	static U32 *s_peIncludeTags = NULL;
	static U32 *s_peExcludeTags = NULL;
	CalendarDay ***peaCalendarDays = ui_GenGetManagedListSafe(pGen, CalendarDay);
	Entity* pEnt = entActivePlayerPtr();
	S32 i, j, k, t;

	if (!pEnt || !pEnt->pPlayer || !pEnt->pPlayer->pEventInfo)
	{
		for (i = 0; i < eaSize(peaCalendarDays); i++)
		{
			eaClearFast(&(*peaCalendarDays)[i]->eaEvents);
			eaClearFast(&(*peaCalendarDays)[i]->eaBackgroundEvents);
			(*peaCalendarDays)[i]->iEventCount = 0;
			(*peaCalendarDays)[i]->iBackgroundEventCount = 0;
		}
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eaiClearFast(&s_peIncludeTags);
	eaiClearFast(&s_peExcludeTags);

	// Parse tags
	{
		char *pchBuffer;
		char *pchToken;
		char *pchContext;

		strdup_alloca(pchBuffer, pchIncludeTags);

		if (pchToken = strtok_r(pchBuffer, " ,\r\n\t", &pchContext))
		{
			do
			{
				ActivityDisplayTags eIncludeTag = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchToken);
				if (eIncludeTag != -1)
					eaiPush(&s_peIncludeTags, eIncludeTag);
			} while (pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext));
		}

		strdup_alloca(pchBuffer, pchExcludeTags);

		if (pchToken = strtok_r(pchBuffer, " ,\r\n\t", &pchContext))
		{
			do
			{
				ActivityDisplayTags eExcludeTag = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchToken);
				if (eExcludeTag != -1)
					eaiPush(&s_peExcludeTags, eExcludeTag);
			} while (pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext));
		}
	}

	for (i = 0; i < eaSize(peaCalendarDays); i++)
	{
		CalendarDay *pLastDay = i > 0 ? (*peaCalendarDays)[i-1] : NULL;
		CalendarDay *pDay = (*peaCalendarDays)[i];
		U32 uIntersectStart, uIntersectEnd;

		// Add events
		uIntersectStart = MAX(pEnt->pPlayer->pEventInfo->uRequestStartDate, pDay->uTimestampBegin);
		uIntersectEnd = MIN(pEnt->pPlayer->pEventInfo->uRequestEndDate, pDay->uTimestampEnd);
		if (uIntersectStart <= uIntersectEnd)
		{
			static CalendarEntry **s_eaEntries;
			int iCount = 0;

			for (j = eaSize(&pDay->eaEvents) - 1; j >= 0; j--)
			{
				if (pDay->eaEvents[j])
					eaPush(&s_eaEntries, pDay->eaEvents[j]);
			}
			eaClearFast(&pDay->eaEvents);

			// There may be events on this day
			for (j = 0; j < eaSize(&pEnt->pPlayer->pEventInfo->eaRequestedEvents); j++)
			{
				CalendarEvent* pCalendarEvent = pEnt->pPlayer->pEventInfo->eaRequestedEvents[j];

				if (!ActivityCalendarFilterByTag(pCalendarEvent->uDisplayTags, s_peIncludeTags, s_peExcludeTags))
				{
					for (t = 0; t < eaSize(&pCalendarEvent->eaTiming); t++)
					{
						CalendarTiming* pCalendarTiming = pCalendarEvent->eaTiming[t];
						uIntersectStart = MAX(pCalendarTiming->uStartDate, pDay->uTimestampBegin);
						uIntersectEnd = MIN(pCalendarTiming->uEndDate, pDay->uTimestampEnd);
						if (uIntersectStart <= uIntersectEnd)
						{
							CalendarEntry* pEntry = eaGetStruct(&s_eaEntries, parse_CalendarEntry, iCount++);
							gclCalendarFillEntry(pEntry, pCalendarEvent, pCalendarTiming, NULL);
						}
					}
				}
			}

			eaSetSizeStruct(&s_eaEntries, parse_CalendarEntry, iCount);
			eaStableSort(s_eaEntries, NULL, CalendarEventTimeSorter);

			// Keep event indices consistent across multiple days
			eaSetSize(&pDay->eaEvents, eaSize(&s_eaEntries));
			for (j = 0; pLastDay && j < eaSize(&pLastDay->eaEvents); j++)
			{
				CalendarEntry *pEntry = pLastDay->eaEvents[j];
				k = -1;
				if (pEntry)
				{
					for (k = eaSize(&s_eaEntries)-1; k >= 0; k--)
					{
						if (s_eaEntries[k]->pEvent == pEntry->pEvent &&
							s_eaEntries[k]->pTiming == pEntry->pTiming)
						{
							break;
						}
					}
				}
				if (k >= 0)
				{
					CalendarEntry* pRemovedEntry = eaRemove(&s_eaEntries, k);
					if (j >= eaSize(&pDay->eaEvents))
						eaSetSize(&pDay->eaEvents, j + 1);
					pDay->eaEvents[j] = pRemovedEntry;
				}
			}

			// Fill in new events
			for (j = 0; j < eaSize(&pDay->eaEvents); j++)
			{
				if (!pDay->eaEvents[j])
					pDay->eaEvents[j] = eaRemove(&s_eaEntries, 0);
			}

			// Theoretically there should be enough empty spaces from the first eaSetSize(),
			// as there are spaces for entries, but if something goes wrong... this should catch it.
			if (!devassert(eaSize(&s_eaEntries) <= 0))
			{
				while (eaSize(&s_eaEntries) > 0)
					eaPush(&pDay->eaEvents, eaRemove(&s_eaEntries, 0));
			}

			pDay->iEventCount = eaSize(&pDay->eaEvents);
		}
		else
		{
			eaClearFast(&pDay->eaEvents);
			pDay->iEventCount = 0;
		}
		eaClearFast(&pDay->eaBackgroundEvents);
		pDay->iBackgroundEventCount = 0;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCalendarDayGetEvents");
void gclExprCalendarUI_GetDayEvents(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CalendarDay *pDay)
{
	if (pDay)
	{
		ui_GenSetListSafe(pGen, &pDay->eaEvents, CalendarEntry);
	}
	else
	{
		ui_GenSetListSafe(pGen, NULL, CalendarEntry);
	}
}

// Get the event with specific criteria
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarDayFindEntry");
SA_RET_OP_VALID CalendarEntry *gclExprCalendarUI_FindDayEntry(SA_PARAM_OP_VALID CalendarDay *pDay, const char *pchEvent, S32 iOptions)
{
	CalendarEntry *pResult = NULL;

	if (pDay)
	{
		bool bNoFlags = (iOptions == 0);
		bool bDefaultActivityFilter = bNoFlags && pchEvent && *pchEvent;
		bool bActivityFilter = bDefaultActivityFilter || (iOptions & 1) != 0;
		bool bStartTodayFilter = (iOptions & 2) != 0;
		bool bEventName = (iOptions & 4) != 0;
		S32 i, iTag = -1;

		if (bActivityFilter)
			iTag = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchEvent);

		for (i = eaSize(&pDay->eaBackgroundEvents) - 1; i >= 0; --i)
		{
			CalendarEntry *pEntry = pDay->eaBackgroundEvents[i];

			if (!pEntry)
				continue;

			if (bActivityFilter && (iTag == -1 || ea32Find(&pEntry->pEvent->uDisplayTags, (U32)iTag) < 0))
				continue;

			if (bEventName && pchEvent && stricmp(pEntry->pEvent->pchEventName, pchEvent))
				continue;

			if (bStartTodayFilter && (pEntry->pTiming==NULL || pEntry->pTiming->uStartDate < pDay->uTimestampBegin || pDay->uTimestampEnd <= pEntry->pTiming->uStartDate))
				continue;

			// Return the first matching event
			pResult = pEntry;
		}

		for (i = eaSize(&pDay->eaEvents) - 1; i >= 0; --i)
		{
			CalendarEntry *pEntry = pDay->eaEvents[i];

			if (!pEntry)
				continue;

			if (bActivityFilter && (iTag == -1 || ea32Find(&pEntry->pEvent->uDisplayTags, (U32)iTag) < 0))
				continue;

			if (bEventName && pchEvent && stricmp(pEntry->pEvent->pchEventName, pchEvent))
				continue;

			if (bStartTodayFilter && (pEntry->pTiming==NULL || pEntry->pTiming->uStartDate < pDay->uTimestampBegin || pDay->uTimestampEnd <= pEntry->pTiming->uStartDate))
				continue;

			// Return the first matching event
			pResult = pEntry;
		}
	}

	if (pResult)
		StructCopyAll(parse_CalendarEntry, pResult, &s_LastCalendarEntry);
	else
		StructReset(parse_CalendarEntry, &s_LastCalendarEntry);

	return pResult;
}

// Same as CalendarDayGetEvent, except it will also set the GenData variable.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCalendarDayFindEntry");
SA_RET_OP_VALID CalendarEntry *gclExprCalendarUI_GenFindDayEntry(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CalendarDay *pDay, const char *pchEvent, S32 iOptions)
{
	CalendarEntry *pResult = gclExprCalendarUI_FindDayEntry(pDay, pchEvent, iOptions);
	ui_GenSetPointer(pGen, pResult, parse_CalendarEntry);
	return pResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("DaysInMonth");
int gclExprCalendarUI_DaysInMonth(int iMonth, int iYear)
{
	// Normalize month
	if (iMonth < 0)
	{
		iYear -= -iMonth / 12;
		iMonth = -iMonth % 12;
	}
	else if (iMonth >= 12)
	{
		iYear += iMonth / 12;
		iMonth = iMonth % 12;
	}

	return timeDaysInMonth(iMonth, iYear);
}

// Determine if a CalendarEntry falls inside a CalendarDay.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryIsInDay");
bool gclExprCalendarUI_CalendarEntryIsInDay(SA_PARAM_OP_VALID CalendarEntry *pEntry, SA_PARAM_OP_VALID CalendarDay *pDay, U32 uOptions)
{
	// By default:
	//	Start time is inclusive of day start time
	//	End time is exclusive of day start time
	if (pEntry && pDay && (uOptions & ~7) == 0 && pEntry->pTiming!=NULL)
	{
		return pEntry->pTiming->uStartDate <= pDay->uTimestampEnd && pEntry->pTiming->uEndDate > pDay->uTimestampBegin;
	}
	return false;
}

// Determine if the CalendarEntry should backspan multiple days.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryIsBackspanDay");
bool gclExprCalendarUI_CalendarEntryIsBackspanDay(SA_PARAM_OP_VALID CalendarEntry *pEntry, SA_PARAM_OP_VALID CalendarDay *pDay, U32 uOptions)
{
	if (gclExprCalendarUI_CalendarEntryIsInDay(pEntry, pDay, uOptions))
	{
		U32 uColumn = uOptions & 7;
		return uColumn == 6 || (pEntry->pTiming->uEndDate <= pDay->uTimestampEnd + 1 && pDay->uTimestampBegin <= pEntry->pTiming->uEndDate);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryStartsInDay");
bool gclExprCalendarUI_CalendarEntryStartsInDay(SA_PARAM_OP_VALID CalendarEntry *pEntry, SA_PARAM_OP_VALID CalendarDay *pDay, U32 uOptions)
{
	// By default:
	//	Start time is between day start and end time inclusive
	if (pEntry && pEntry->pTiming && pDay)
		return pDay->uTimestampBegin <= pEntry->pTiming->uStartDate && pEntry->pTiming->uStartDate <= pDay->uTimestampEnd;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryEndsInDay");
bool gclExprCalendarUI_CalendarEntryEndsInDay(SA_PARAM_OP_VALID CalendarEntry *pEntry, SA_PARAM_OP_VALID CalendarDay *pDay, U32 uOptions)
{
	// By default:
	//	End time is between day start and end time inclusive
	if (pEntry && pEntry->pTiming && pDay)
		return pDay->uTimestampBegin <= pEntry->pTiming->uEndDate && pEntry->pTiming->uEndDate <= pDay->uTimestampEnd;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryMiddleInDay");
bool gclExprCalendarUI_CalendarEntryMiddleInDay(SA_PARAM_OP_VALID CalendarEntry *pEntry, SA_PARAM_OP_VALID CalendarDay *pDay, U32 uOptions)
{
	// By default:
	//	Start time is before day start exclusive
	//	End time is after day end exclusive
	if (pEntry && pEntry->pTiming && pDay)
		return pEntry->pTiming->uStartDate < pDay->uTimestampBegin && pDay->uTimestampEnd < pEntry->pTiming->uEndDate;
	return false;
}

typedef struct ActivityDisplayTagMask
{
	S32 iBitFieldSize;
	S32 nIncludeAny, nExcludeAny;
	S32 nIncludeAll, nExcludeAll;
	U32 *bfIncludeAny, *bfExcludeAny;
	U32 *bfIncludeAll, *bfExcludeAll;
} ActivityDisplayTagMask;

static void ParseTagMask(const char *pchMask, const char **eapchTagNames, S32 *eaiValues, ActivityDisplayTagMask *pMask)
{
	static const char *s_pchDelimiters = " ,\r\n\t";
	S32 iBitFieldSize = pMask->iBitFieldSize;
	char *pchBuffer, *pchToken, *pchContext;
	S32 i;

	strdup_alloca(pchBuffer, pchMask);

	if (!pMask->bfIncludeAny)
		pMask->bfIncludeAny = (U32 *)malloc(iBitFieldSize);
	if (!pMask->bfExcludeAny)
		pMask->bfExcludeAny = (U32 *)malloc(iBitFieldSize);
	if (!pMask->bfIncludeAll)
		pMask->bfIncludeAll = (U32 *)malloc(iBitFieldSize);
	if (!pMask->bfExcludeAll)
		pMask->bfExcludeAll = (U32 *)malloc(iBitFieldSize);

	if (pMask->bfIncludeAny)
		memset(pMask->bfIncludeAny, 0, iBitFieldSize);
	if (pMask->bfExcludeAny)
		memset(pMask->bfExcludeAny, 0, iBitFieldSize);
	if (pMask->bfIncludeAll)
		memset(pMask->bfIncludeAll, 0, iBitFieldSize);
	if (pMask->bfExcludeAll)
		memset(pMask->bfExcludeAll, 0, iBitFieldSize);

	pMask->nIncludeAny = 0;
	pMask->nExcludeAny = 0;
	pMask->nIncludeAll = 0;
	pMask->nExcludeAll = 0;

	if (pchToken = strtok_r(pchBuffer, s_pchDelimiters, &pchContext))
	{
		do
		{
			U32 *pbf = pMask->bfIncludeAny;
			S32 *pn = &pMask->nIncludeAny;

			for (; pchToken[0] && strchr("+-", pchToken[0]); pchToken++)
			{
				if (pchToken[0] == '+')
				{
					pbf = pMask->bfIncludeAll;
					pn = &pMask->nIncludeAll;
				}
				else if (pchToken[0] == '-')
				{
					pbf = pMask->bfExcludeAny;
					pn = &pMask->nExcludeAny;
				}
			}

			if (!pbf)
				continue;

			if (strchr(pchToken, '*') || strchr(pchToken, '?'))
			{
				for (i = eaSize(&eapchTagNames) - 1; i >= 0; i--)
				{
					if (isWildcardMatch(pchToken, eapchTagNames[i], false, true))
					{
						if (!TSTB(pbf, eaiValues[i]))
						{
							SETB(pbf, eaiValues[i]);
							(*pn)++;
						}
					}
				}
			}
			else
			{
				ActivityDisplayTags eTag = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchToken);
				if (eTag != -1)
				{
					SETB(pbf, eTag);
					(*pn)++;
				}
			}
		} while (pchToken = strtok_r(NULL, s_pchDelimiters, &pchContext));
	}
}

static bool AcceptTagMask(U32 *ea32Tags, ActivityDisplayTagMask *pMask)
{
	S32 i;
	S32 iBitFieldSize = pMask->iBitFieldSize;
	U32 *pbfIncludeAnyTags = pMask->nIncludeAny ? pMask->bfIncludeAny : NULL;
	U32 *pbfExcludeAnyTags = pMask->nExcludeAny ? pMask->bfExcludeAny : NULL;
	U32 *pbfIncludeAllTags = pMask->nIncludeAll ? pMask->bfIncludeAll : NULL;
	U32 *pbfExcludeAllTags = pMask->nExcludeAll ? pMask->bfExcludeAll : NULL;
	bool bIncluded = !pbfIncludeAnyTags;
	bool bExcluded = false;
	U32 *pbfMatchedIncludeAll = NULL;
	U32 *pbfMatchedExcludeAll = NULL;

	if (pbfIncludeAllTags)
		pbfMatchedIncludeAll = (U32 *)memset(alloca(iBitFieldSize), 0, iBitFieldSize);
	if (pbfExcludeAllTags)
		pbfMatchedExcludeAll = (U32 *)memset(alloca(iBitFieldSize), 0, iBitFieldSize);

	for (i = ea32Size(&ea32Tags) - 1; i >= 0; i--)
	{
		U32 iTag = ea32Tags[i];

		if (pbfIncludeAllTags && TSTB(pbfIncludeAllTags, iTag))
			SETB(pbfMatchedIncludeAll, iTag);
		if (pbfExcludeAllTags && TSTB(pbfExcludeAllTags, iTag))
			SETB(pbfMatchedExcludeAll, iTag);

		if (pbfIncludeAnyTags && TSTB(pbfIncludeAnyTags, iTag))
			bIncluded = true;
		if (pbfExcludeAnyTags && TSTB(pbfExcludeAnyTags, iTag))
			bExcluded = true;
	}

	return bIncluded && !bExcluded
		&& (!pbfMatchedIncludeAll || memcmp(pbfMatchedIncludeAll, pbfIncludeAllTags, iBitFieldSize) == 0)
		&& (!pbfMatchedExcludeAll || memcmp(pbfMatchedExcludeAll, pbfExcludeAllTags, iBitFieldSize) == 0);
}

static CalendarEntry *CalendarEntrySetOwnedTiming(CalendarEntry *pEntry)
{
	if (pEntry->pTiming != pEntry->pOwnedTiming)
	{
		if (!pEntry->pOwnedTiming)
			pEntry->pOwnedTiming = StructClone(parse_CalendarTiming, pEntry->pTiming);
		else
			*pEntry->pOwnedTiming = *pEntry->pTiming;
		pEntry->pTiming = pEntry->pOwnedTiming;
	}
	return pEntry;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Calendar Fill requests

// These are all rather complicated.
// There are 3 sets of data that can be used to generate calendar entries. All 3 end up on the pPlayer->pEventInfo struct
//   - RequestedEvents
//		When the client requests info for a particular date range the result is placed here. The Server may take some time to fill the request, of course.
//	 - ServerCalendar->eaEvents
//		These are automaticall updated on the server periodically according to a the EventConfig.def file. There is a Lookahead time and include/exclude flags.
//   - eaActiveEvents
//		This is a full list of all currently active events. Including those that are manually on.


////////////////////////////
// Commands to fill the RequestedEvents list
// These can be very inefficient

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FillCalendarRequest);
void expr_ActivityFillCalendarRequest(U32 uDateStart, U32 uDateEnd, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	ServerCmd_gslActivity_FillActivityCalendar(uDateStart,uDateEnd,pchTagsToInclude,pchTagsToExclude);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FillCalendarRequest_Month);
void expr_ActivityFillCalendarRequest_Month(int iYear, int iMonth, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	U32 uDateStart;
	U32 uDateEnd;
	TimeStruct sTime = {0};

	sTime.tm_year = iYear;
	sTime.tm_mon = iMonth;
	sTime.tm_mday = 1;

	uDateStart = timeGetSecondsSince2000FromTimeStruct(&sTime);

	sTime.tm_mon = iMonth + 1;

	if(sTime.tm_mon == 13)
	{
		sTime.tm_year = iYear + 1;
		sTime.tm_mon = 1;
	}

	uDateEnd = timeGetSecondsSince2000FromTimeStruct(&sTime) - 1;

	ServerCmd_gslActivity_FillActivityCalendar(uDateStart,uDateEnd,pchTagsToInclude,pchTagsToExclude);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FillCalendarRequest_Day);
void expr_ActivityFillCalendarRequest_Day(int iYear, int iMonth, int iDay, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	U32 uDateStart;
	U32 uDateEnd;
	TimeStruct sTime = {0};

	sTime.tm_year = iYear;
	sTime.tm_mon = iMonth;
	sTime.tm_mday = iDay;

	uDateStart = timeGetSecondsSince2000FromTimeStruct(&sTime);

	uDateEnd = uDateStart + 86399; //24 hours minus 1 second

	ServerCmd_gslActivity_FillActivityCalendar(uDateStart,uDateEnd,pchTagsToInclude,pchTagsToExclude);
}



////////////////////////////
// Commands that operate on already received data.

// Performs a very complicated event fill in, specifically designed for Star Trek's calendar, though NW uses it as well.
// It is primarily designed to cluster events on a given tag.
// Operates on RequestedEvents
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCalendarDaysFillEventsMagical");
void gclExprCalendarUI_CalendarDaysFillEventsMagical(SA_PARAM_NN_VALID UIGen *pGen, const char *pchEvents, const char *pchEventOrder, const char *pchBackgroundEvents, U32 uOptions)
{
	static bool s_once = false;
	static const char **s_eapchTagNames = NULL;
	static S32 *s_eaiValues = NULL;
	static ActivityDisplayTagMask s_EventMask;
	static ActivityDisplayTagMask s_BackgroundEventMask;
	static ActivityDisplayTags *s_eaiEventOrder;

	ParseTable *pParseTable;
	CalendarDay ***peaCalendarDays = (CalendarDay ***)ui_GenGetList(pGen, NULL, &pParseTable);
	Entity* pEnt = entActivePlayerPtr();
	CalendarEvent ***peaEvents = pEnt && pEnt->pPlayer && pEnt->pPlayer->pEventInfo ? &pEnt->pPlayer->pEventInfo->eaRequestedEvents : NULL;
	S32 i, j, k, t;
	bool bPadToEventOrderMinimal = !!(uOptions & 1);
	bool bUnionEventTimes = !!(uOptions & 2);
	bool bUnionBackgroundEventTimes = !!(uOptions & 4);

	// If event times are unioned, then there should only be one of a specific event
	// on a given day, so align the events by just the event information. Also give
	// in and grant the events raises, benefits, and let them drive the calendar out
	// of business and use by the outrageous demands.
	bool bAlignByEvent = !!(uOptions & 8) && bUnionEventTimes;

	if (pParseTable != parse_CalendarDay)
		return;

	if (uOptions & 16)
	{
		gclCalendar_OncePerFrame();
		peaEvents = &s_eaGuildEvents;
	}

	if (!peaEvents)
	{
		for (i = 0; i < eaSize(peaCalendarDays); i++)
		{
			eaClearFast(&(*peaCalendarDays)[i]->eaEvents);
			eaClearFast(&(*peaCalendarDays)[i]->eaBackgroundEvents);
			(*peaCalendarDays)[i]->iEventCount = 0;
			(*peaCalendarDays)[i]->iBackgroundEventCount = 0;
		}
		return;
	}

	if (!s_once)
	{
		S32 iMaxValue = 1;
		DefineFillAllKeysAndValues(ActivityDisplayTagsEnum, &s_eapchTagNames, &s_eaiValues);
		for (i = eaiSize(&s_eaiValues) - 1; i >= 0; i--)
			iMaxValue = MAX(iMaxValue, s_eaiValues[i] + 1);
		s_EventMask.iBitFieldSize = ((iMaxValue + 31) / 32) * sizeof(U32);
		s_BackgroundEventMask.iBitFieldSize = s_EventMask.iBitFieldSize;
		s_once = true;
	}

	PERFINFO_AUTO_START_FUNC();

	ParseTagMask(pchEvents, s_eapchTagNames, s_eaiValues, &s_EventMask);
	ParseTagMask(pchBackgroundEvents, s_eapchTagNames, s_eaiValues, &s_BackgroundEventMask);

	// Parse tag display order
	{
		static const char *s_pchDelimiters = " ,\r\n\t";
		char *pchBuffer, *pchToken, *pchContext;
		eaiClearFast(&s_eaiEventOrder);
		strdup_alloca(pchBuffer, pchEventOrder);
		if (pchToken = strtok_r(pchBuffer, s_pchDelimiters, &pchContext))
		{
			do
			{
				ActivityDisplayTags eTag = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchToken);
				if (eTag != -1)
					eaiPushUnique(&s_eaiEventOrder, eTag);
			} while (pchToken = strtok_r(NULL, s_pchDelimiters, &pchContext));
		}
	}

	for (i = 0; i < eaSize(peaCalendarDays); i++)
	{
		CalendarDay *pLastDay = i > 0 ? (*peaCalendarDays)[i-1] : NULL;
		CalendarDay *pDay = (*peaCalendarDays)[i];
		U32 uIntersectStart, uIntersectEnd;

		if (pEnt)
		{
			uIntersectStart = MAX(pEnt->pPlayer->pEventInfo->uRequestStartDate, pDay->uTimestampBegin);
			uIntersectEnd = MIN(pEnt->pPlayer->pEventInfo->uRequestEndDate, pDay->uTimestampEnd);
		}
		else
		{
			uIntersectStart = uIntersectEnd = 0;
		}

		// Add events
		if (uIntersectStart <= uIntersectEnd)
		{
			static CalendarEntry **s_eaEntries;
			int iCount = 0;

			for (j = eaSize(&pDay->eaEvents) - 1; j >= 0; j--)
			{
				if (pDay->eaEvents[j])
					eaPush(&s_eaEntries, pDay->eaEvents[j]);
			}
			eaClearFast(&pDay->eaEvents);

			pDay->iBackgroundEventCount = 0;

			// Find all the event entries that occur on this day.
			for (j = 0; j < eaSize(peaEvents); j++)
			{
				CalendarEvent* pCalendarEvent = (*peaEvents)[j];
				bool bEvent = AcceptTagMask(pCalendarEvent->uDisplayTags, &s_EventMask);
				bool bBackgroundEvent = AcceptTagMask(pCalendarEvent->uDisplayTags, &s_BackgroundEventMask);
				CalendarEntry* pUnionEntry = NULL;
				CalendarEntry* pUnionBackgroundEntry = NULL;

				// Ignore manually on and manually off events as they aren't scheduled as such and should not appear in the calendar.
				if ((bEvent || bBackgroundEvent) && pCalendarEvent->iEventRunMode == kEventRunMode_Auto)
				{
					for (t = 0; t < eaSize(&pCalendarEvent->eaTiming); t++)
					{
						CalendarTiming* pCalendarTiming = pCalendarEvent->eaTiming[t];
						uIntersectStart = MAX(pCalendarTiming->uStartDate, pDay->uTimestampBegin);
						uIntersectEnd = MIN(pCalendarTiming->uEndDate, pDay->uTimestampEnd);
						if (uIntersectStart <= uIntersectEnd)
						{
							CalendarEntry* pEntry;
							if (bEvent)
							{
								if (!pUnionEntry)
								{
									pEntry = eaGetStruct(&s_eaEntries, parse_CalendarEntry, iCount++);
									gclCalendarFillEntry(pEntry, pCalendarEvent, pCalendarTiming, &s_eaiEventOrder);
									if (bUnionEventTimes)
										pUnionEntry = CalendarEntrySetOwnedTiming(pEntry);
								}
								else
								{
									MIN1(pUnionEntry->pOwnedTiming->uStartDate, pCalendarTiming->uStartDate);
									MAX1(pUnionEntry->pOwnedTiming->uEndDate, pCalendarTiming->uEndDate);
								}
							}
							if (bBackgroundEvent)
							{
								if (!pUnionBackgroundEntry)
								{
									pEntry = eaGetStruct(&pDay->eaBackgroundEvents, parse_CalendarEntry, pDay->iBackgroundEventCount++);
									gclCalendarFillEntry(pEntry, pCalendarEvent, pCalendarTiming, &s_eaiEventOrder);
									if (bUnionBackgroundEventTimes)
										pUnionBackgroundEntry = CalendarEntrySetOwnedTiming(pEntry);
								}
								else
								{
									MIN1(pUnionBackgroundEntry->pOwnedTiming->uStartDate, pCalendarTiming->uStartDate);
									MAX1(pUnionBackgroundEntry->pOwnedTiming->uEndDate, pCalendarTiming->uEndDate);
								}
							}
						}
					}
				}
			}

			eaSetSizeStruct(&pDay->eaBackgroundEvents, parse_CalendarEntry, pDay->iBackgroundEventCount);
			eaSetSizeStruct(&s_eaEntries, parse_CalendarEntry, iCount);
			eaStableSort(s_eaEntries, NULL, CalendarEventTimeSorter);

			// Keep events that span multiple days at a consistent index for all
			// spanned days.
			eaSetSize(&pDay->eaEvents, eaSize(&s_eaEntries));
			for (j = 0; pLastDay && j < eaSize(&pLastDay->eaEvents); j++)
			{
				CalendarEntry *pEntry = pLastDay->eaEvents[j];
				k = -1;
				if (pEntry)
				{
					for (k = eaSize(&s_eaEntries)-1; k >= 0; k--)
					{
						if (s_eaEntries[k]->pEvent == pEntry->pEvent &&
							(bAlignByEvent || s_eaEntries[k]->pTiming == pEntry->pTiming))
						{
							break;
						}
					}
				}
				if (k >= 0)
				{
					eaSet(&pDay->eaEvents, eaRemove(&s_eaEntries, k), j);
				}
			}

			if (eaSize(&s_eaEntries) > 0)
			{
				// Add new events after the last event with the highest sort order
				// lower than the sort order of the first new event.
				for (j = eaSize(&pDay->eaEvents) - 1; j > 0; j--)
				{
					if (pDay->eaEvents[j] && pDay->eaEvents[j]->iSortOrder < s_eaEntries[0]->iSortOrder)
						break;
				}
				MAX1(j, 0);

				// If filling in from the beginning of the list, and bPadToEventOrderMinimal
				// is set. And if the number of events on this day is less than the number
				// of tags in the event order. Then ensure the first N events spaces are empty.
				// Where N is the sort order of the event.
				//
				// The primary goal of this padding is to add empty space at the beginning of
				// the entry list. So it will stop padding as soon as it encounters an existing
				// event.
				if (j == 0 && (bPadToEventOrderMinimal && iCount < eaiSize(&s_eaiEventOrder)))
				{
					for (k = 0; k < s_eaEntries[0]->iSortOrder; k++)
					{
						if (eaGet(&pDay->eaEvents, k))
							break;
						j++;
					}
				}

				// Add all remaining events to the list of events
				for (; eaSize(&s_eaEntries) > 0; j++)
				{
					if (!eaGet(&pDay->eaEvents, j))
						eaSet(&pDay->eaEvents, eaRemove(&s_eaEntries, 0), j);
				}
			}

			pDay->iEventCount = eaSize(&pDay->eaEvents);
		}
		else
		{
			eaClearFast(&pDay->eaEvents);
			eaClearFast(&pDay->eaBackgroundEvents);
			pDay->iEventCount = 0;
			pDay->iBackgroundEventCount = 0;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Generic STO query. Uses Request system. Does not deal with Manually On/Off events. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCalendarEntries);
void expr_ActivityGetCalendarEntries(SA_PARAM_NN_VALID UIGen *pGen, U32 uDateStart, U32 uDateEnd, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	CalendarEntry ***peaCalendarEntries = ui_GenGetManagedListSafe(pGen, CalendarEntry);
	bool bIncludeOtherMaps = true;
	bool bIncludeManualOn = false;		// STO doesn't know how to deal with Manual stuff yet.
	bool bExcludeManualOff = false;
	
	ActivityGetCalendarEntries(peaCalendarEntries, uDateStart, uDateEnd, pchTagsToInclude, pchTagsToExclude,
							   bIncludeOtherMaps, bIncludeManualOn, bExcludeManualOff,ActivityEventQuery_Requested);
	ui_GenSetManagedListSafe(pGen, peaCalendarEntries, CalendarEntry, true);
}

// STO specific function. Should really be called somthing else or converted to true Active query.
//   It just needs to know if there are any currently active events that match the tags.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetActiveCalendarEntryCount);
int expr_ActivityGetActiveCalendarEntryCount(const char *pchTagsToInclude, const char *pchTagsToExclude, bool bExcludeOtherMaps, bool bIgnored)
{
	bool bIncludeOtherMaps = !bExcludeOtherMaps;	// This oddity brought you by legacy implementation and the fact that the include stuff made not much sense.
	bool bIncludeManualOn = false;		// STO doesn't know how to deal with Manual stuff yet.
	bool bExcludeManualOff = false;
	
	return ActivityGetCalendarEntries(NULL, gclActivity_EventClock_GetSecondsSince2000(), gclActivity_EventClock_GetSecondsSince2000(),
									  pchTagsToInclude, pchTagsToExclude, bIncludeOtherMaps, bIncludeManualOn, bExcludeManualOff, ActivityEventQuery_Requested);
}


// NW. Used for slideout events. Uses the Server array. Excludes Manually Off events
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetServerCalendarEntries);
void expr_ActivityGetServerCalendarEntries(SA_PARAM_NN_VALID UIGen *pGen, S32 iMaxEntries, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	CalendarEntry ***peaCalendarEntries = ui_GenGetManagedListSafe(pGen, CalendarEntry);
	bool bIncludeOtherMaps = true;
	bool bIncludeManualOn = false;	// The sliders currently don't want to pay attention to  ManualOn events.
	bool bExcludeManualOff = true;

	// 0,0 for dates indicates to ignore the dates for purposes of filtering
	ActivityGetCalendarEntries(peaCalendarEntries, 0, 0, pchTagsToInclude, pchTagsToExclude,
							   bIncludeOtherMaps, bIncludeManualOn, bExcludeManualOff,ActivityEventQuery_Server);
	if (eaSize(peaCalendarEntries) > iMaxEntries)
		eaSetSizeStruct(peaCalendarEntries, parse_CalendarEntry, iMaxEntries);
	ui_GenSetManagedListSafe(pGen, peaCalendarEntries, CalendarEntry, true);
}


// TODO: Deprecate this and make a new function

// NW. Used for landing page strip. Uses the Requested Array.
// Uses the requested field and returns soon upcoming events and currently active.
//  NOTE: Even though we say "Active" we still use the requested array. This is historical as it differentiates
//  this function from a no longer existing one that returned only events that were explicitly in the future.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetUpcomingAndActiveCalendarEntries);
void expr_ActivityGetUpcomingAndActiveCalendarEntries(SA_PARAM_NN_VALID UIGen *pGen, S32 iMaxEntries, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	CalendarEntry ***peaCalendarEntries = ui_GenGetManagedListSafe(pGen, CalendarEntry);
	bool bIncludeOtherMaps = true;
	bool bIncludeManualOn = false;
	bool bExcludeManualOff = true;		// Make sure we exclude things that have been disabled
	
	ActivityGetCalendarEntries(peaCalendarEntries, gclActivity_EventClock_GetSecondsSince2000(), gclActivity_EventClock_GetSecondsSince2000() + 86400,
							   pchTagsToInclude, pchTagsToExclude, bIncludeOtherMaps,
								bIncludeManualOn, bExcludeManualOff, ActivityEventQuery_Requested);							   

	if (eaSize(peaCalendarEntries) > iMaxEntries)
		eaSetSizeStruct(peaCalendarEntries, parse_CalendarEntry, iMaxEntries);
	ui_GenSetManagedListSafe(pGen, peaCalendarEntries, CalendarEntry, true);
}

// TODO: Deprecate this and make a new function.

// NW. Used for Mission Helper and PVP Mission Helper. Uses the Active Array.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetActiveCalendarEntries);
void expr_ActivityGetActiveCalendarEntries(SA_PARAM_NN_VALID UIGen *pGen, const char *pchTagsToInclude, const char *pchTagsToExclude, bool bExcludeOtherMaps, bool bIgnored)
{
	CalendarEntry ***peaCalendarEntries = ui_GenGetManagedListSafe(pGen, CalendarEntry);
	bool bIncludeOtherMaps = !bExcludeOtherMaps;	// This oddity brought you by legacy implementation and the fact that the include stuff made not much sense.
	bool bIncludeManualOn = true;	
	bool bExcludeManualOff = false;  // Not relevant since we are looking at the Active list.
	
	ActivityGetCalendarEntries(peaCalendarEntries, gclActivity_EventClock_GetSecondsSince2000(), gclActivity_EventClock_GetSecondsSince2000(),
							   pchTagsToInclude, pchTagsToExclude,
								bIncludeOtherMaps,
								bIncludeManualOn, bExcludeManualOff, ActivityEventQuery_Active);							   							   
	ui_GenSetManagedListSafe(pGen, peaCalendarEntries, CalendarEntry, true);
}



// NW. Used for Gauntlgrym, Crafting recipe event. And some other contact info. Contact_ImageMenu.uigen.
//  Also event details window from landing page zen panel.
static void ActivityGetActiveEventTimeInfoByName(const char *pchEventName, U32 *puStartTime, U32 *puEndTime)
{
	static CalendarEntry **eaCalendarEntries = NULL;
	S32 i = 0;
	S32 iCount = 0;

	bool bIncludeOtherMaps = false;
	bool bIncludeManualOn = false;	// Can't get a time remaining for manual
	bool bExcludeManualOff = false; // Not relevant since we are looking at the Active list.
	
	iCount = ActivityGetCalendarEntries(&eaCalendarEntries, gclActivity_EventClock_GetSecondsSince2000(), gclActivity_EventClock_GetSecondsSince2000(), "", "",
										bIncludeOtherMaps,
										bIncludeManualOn, bExcludeManualOff, ActivityEventQuery_Active);							   							   

	// We Ignore the Run Mode here.
	// If the event was started manually, this should return 0
	if (puStartTime!=NULL)
	{
		*puStartTime = 0;
	}
	if (puEndTime!=NULL)
	{
		*puEndTime = 0;
	}
	for (i = 0; i < iCount; ++i)
	{
		CalendarEntry *pEntry = eaCalendarEntries[i];
		if (pEntry && pEntry->pTiming && pEntry->pEvent && stricmp(pEntry->pEvent->pchEventName, pchEventName) == 0)
		{
			if (puStartTime!=NULL)
			{
				*puStartTime = pEntry->pTiming->uStartDate;
			}
			if (puEndTime!=NULL)
			{
				*puEndTime = pEntry->pTiming->uEndDate;
			}
		}
	}
}

// NW. Used for Gauntlgrym, Crafting recipe event. And some other contact info. Contact_ImageMenu.uigen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetEventTimeRemainingByName);
S32 expr_ActivityGetEventTimeRemainingByName(const char *pchEventName)
{
	U32 uStartTime, uEndTime;

	ActivityGetActiveEventTimeInfoByName(pchEventName, &uStartTime, &uEndTime);
	if (uEndTime > 0)
	{
		return MAX(uEndTime - gclActivity_EventClock_GetSecondsSince2000(), 0);
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetEventStartTimeByName);
S32 expr_ActivityGetEventStartTimeByName(const char *pchEventName)
{
	U32 uStartTime, uEndTime;
	ActivityGetActiveEventTimeInfoByName(pchEventName, &uStartTime, &uEndTime);
	return(uStartTime);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetEventEndTimeByName);
S32 expr_ActivityGetEventEndTimeByName(const char *pchEventName)
{
	U32 uStartTime, uEndTime;
	ActivityGetActiveEventTimeInfoByName(pchEventName, &uStartTime, &uEndTime);
	return(uEndTime);
}



//"Upcoming" meant events that aren't active yet or events that are active on other maps.
// DEPRECATED as unused
//AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetUpcomingCalendarEntries);
//void expr_ActivityGetUpcomingCalendarEntries(SA_PARAM_NN_VALID UIGen *pGen, S32 iMaxEntries, const char *pchTagsToInclude, const char *pchTagsToExclude)

// DEPRECATED as unused
//AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCurrentEntry);
//void expr_ActivityGetCurrentEntry(SA_PARAM_NN_VALID UIGen *pGen, const char *pchTagsToInclude, const char *pchTagsToExclude)


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queries for individual CalendarEntries

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryFindTag");
const char *expr_ActivityFindEntryTag(ExprContext *pContext, SA_PARAM_OP_VALID CalendarEntry *pEntry, const char *pchTagPrefix)
{
	const char *pchResult = NULL;

	if (pEntry)
	{
		S32 i;
		for (i = ea32Size(&pEntry->pEvent->uDisplayTags) - 1; i >= 0; --i)
		{
			const char *pchTagName = StaticDefineIntRevLookup(ActivityDisplayTagsEnum, pEntry->pEvent->uDisplayTags[i]);
			if (pchTagName && *pchTagName && strStartsWith(pchTagName, pchTagPrefix))
			{
				pchResult = pchTagName;
			}
		}
	}

	return pchResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventName");
const char* expr_CalendarEntryGetEventName(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return NULL_TO_EMPTY(pEntry->pEvent->pchEventName);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventQueueName");
const char* expr_CalendarEntryGetEventQueueName(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return NULL_TO_EMPTY(pEntry->pEvent->pchQueue);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventIcon");
const char* expr_CalendarEntryGetEventIcon(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return NULL_TO_EMPTY(pEntry->pEvent->pchIcon);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventBackground");
const char* expr_CalendarEntryGetEventBackground(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return NULL_TO_EMPTY(pEntry->pEvent->pchBackground);
	}
	return "";
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventParentName");
const char* expr_CalendarEntryGetEventParentName(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return NULL_TO_EMPTY(pEntry->pEvent->pchParent);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventDisplayName");
const char* expr_CalendarEntryGetEventDisplayName(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return pEntry->pEvent->pchDisplayName ? pEntry->pEvent->pchDisplayName : NULL_TO_EMPTY(TranslateDisplayMessage(pEntry->pEvent->msgDisplayName));
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventDescShort");
const char* expr_CalendarEntryGetEventDescShort(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return pEntry->pEvent->pchDisplayDescShort ? pEntry->pEvent->pchDisplayDescShort : NULL_TO_EMPTY(TranslateDisplayMessage(pEntry->pEvent->msgDisplayDescShort));
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventDescLong");
const char* expr_CalendarEntryGetEventDescLong(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return pEntry->pEvent->pchDisplayDescLong ? pEntry->pEvent->pchDisplayDescLong : NULL_TO_EMPTY(TranslateDisplayMessage(pEntry->pEvent->msgDisplayDescLong));
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryHasEventMapMove");
bool expr_CalendarEntryHasEventMapMove(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return pEntry->pEvent->bEventMapMove;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryHasEventContact");
bool expr_CalendarEntryHasEventContact(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pEvent)
	{
		return pEntry->pEvent->bEventContact;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventStartTime");
U32 expr_CalendarEntryGetEventStartTime(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pTiming)
	{
		return pEntry->pTiming->uStartDate;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CalendarEntryGetEventEndTime");
U32 expr_CalendarEntryGetEventEndTime(SA_PARAM_OP_VALID CalendarEntry *pEntry)
{
	if (pEntry && pEntry->pTiming)
	{
		return pEntry->pTiming->uEndDate;
	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Print Debug (USES Requested events)

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_NAME(EventCalendarPrintRequest); 
void cmd_EventCalendarPrintRequest(const char *pchDateStart, const char *pchDateEnd, const char *pchFileName)
{
	U32 uDateStart = timeGetSecondsSince2000FromDateString(pchDateStart);
	U32 uDateEnd = timeGetSecondsSince2000FromDateString(pchDateEnd);

	ServerCmd_gslActivity_FillActivityCalendarForPrint(uDateStart,uDateEnd,pchFileName);
}

static int sortCalendarEventDebugByStartDate(const CalendarEventDebug **entry1, const CalendarEventDebug **entry2)
{
	return (*entry1)->uStartDate - (*entry2)->uStartDate;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(7);
void gclPrintCalendarInfo(CalendarRequest* pRequest, const char* pchFileName)
{
	if (pRequest)
	{
		CalendarEventDebugData DebugData = {0};
		int i, j;
		for (i = 0; i < eaSize(&pRequest->eaEvents); i++)
		{
			CalendarEvent* pCalendarEvent = pRequest->eaEvents[i];
			for (j = 0; j < eaSize(&pCalendarEvent->eaTiming); j++)
			{
				CalendarTiming* pCalendarTiming = pCalendarEvent->eaTiming[j];
				CalendarEventDebug* pDebugEvent = StructCreate(parse_CalendarEventDebug);
				pDebugEvent->pchEventName = pCalendarEvent->pchEventName;
				pDebugEvent->uStartDate = pCalendarTiming->uStartDate;
				pDebugEvent->uEndDate = pCalendarTiming->uEndDate;

				if(pDebugEvent->uStartDate)
					pDebugEvent->pchStartDate = StructAllocString(timeGetLocalDateStringFromSecondsSince2000(pDebugEvent->uStartDate));
				if(pDebugEvent->uEndDate)
					pDebugEvent->pchEndDate = StructAllocString(timeGetLocalDateStringFromSecondsSince2000(pDebugEvent->uEndDate));

				eaPush(&DebugData.eaEvents, pDebugEvent);
			}
		}

		eaQSort(DebugData.eaEvents,sortCalendarEventDebugByStartDate);
		ParserWriteTextFile(pchFileName,parse_CalendarEventDebugData,&DebugData,0,0);
		StructDeInit(parse_CalendarEventDebugData, &DebugData);
	}
}

#include "AutoGen/gclCalendarUI_c_ast.c"
