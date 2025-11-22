#pragma once

typedef U32 ContainerID;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCProjectList UGCProjectList;
typedef struct UGCIDList UGCIDList;
typedef struct UGCProjectVersion UGCProjectVersion;
typedef struct UGCProjectSeriesVersion UGCProjectSeriesVersion;
typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;

extern bool g_bShowUnpublishedProjectsForDebugging;
extern int g_iMaximumDaysOldForNewContentList;
extern F32 g_fMinimumTagPercentageForSearch;

UGCProject *aslUGCSearchManager_GetProject(ContainerID id);
UGCProjectSeries *aslUGCSearchManager_GetProjectSeries(ContainerID id);

UGCProjectList *aslUGCSearchManager_SearchByID_Internal(UGCIDList* pList);

const UGCProject *aslUGCSearchManager_GetFirstPublishedProject(const UGCProjectSeriesVersion *pUGCProjectSeriesVersion);

float aslUGCSearchManager_SeriesAverageDurationInMinutes(const UGCProjectSeriesVersion *pUGCProjectSeriesVersion);

bool ProjectIsSearchable(const UGCProject *pProject);
bool ProjectMeetsBasicCriteria(UGCProjectSearchInfo *pSearchInfo, UGCProject *pProject);

AUTO_ENUM;
typedef enum eSearchType
{
	SEARCH_UNKNOWN,
	SEARCH_SIMPLE,
	SEARCH_W_PRIMARY_FILTER,
	SEARCH_SORTED_BY_RATINGS,
	SEARCH_WHATSHOT,
	SEARCH_REVIEWER,
	SEARCH_NEW,
	SEARCH_BROWSE,
	SEARCH_FEATURED,
	SEARCH_SUBSCRIBED,
	SEARCH_FEATURED_AND_FILTER,
	SEARCH_FEATURED_NO_SERIES,
	SEARCH_LOCATION_NO_SERIES,

	SEARCH_TOTAL,

	SEARCH_LAST,
} eSearchType;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Name, Count_2, TotalMicroSecs_2, AverageMicroSecs_2, TotalMilliSecs_2, AverageMilliSecs_2, SlowQueryCount, RecentSlowQueries");
typedef struct SearchTypeReport
{
	char *pName;

	int iCount_1;
	U64 iTotalMicroSecs_1;
	U64 iTotalMilliSecs_1;
	int iAverageMilliSecs_1;

	int iCount_2;
	U64 iTotalMicroSecs_2;
	U64 iAverageMicroSecs_2;
	U64 iTotalMilliSecs_2;
	int iAverageMilliSecs_2;

	int iSlowQueryCount;
	STRING_EARRAY eaEstrRecentSlowQueries;	AST(NAME(RecentSlowQueries) ESTRING)
} SearchTypeReport;

extern SearchTypeReport sSearchTypeReports[SEARCH_LAST];
