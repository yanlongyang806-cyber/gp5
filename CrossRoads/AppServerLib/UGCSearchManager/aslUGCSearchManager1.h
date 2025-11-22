#pragma once

typedef enum SSSTreeSearchType SSSTreeSearchType;
typedef enum UGCProjectSearchFilterComparison UGCProjectSearchFilterComparison;
typedef struct UGCContentInfo UGCContentInfo;
typedef struct FeaturedContentList FeaturedContentList;
typedef struct GameProgressionNodeRef GameProgressionNodeRef;
typedef struct QueueDefRef QueueDefRef;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectSearchFilter UGCProjectSearchFilter;
typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCSearchResult UGCSearchResult;
typedef enum enumResourceEventType enumResourceEventType;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef UGCIDList UGCIDList;

int aslUGCSearchManager1_Init(void);

void aslUGCSearchManager1_ProjectResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProject *pProject, void *pUserData);
void aslUGCSearchManager1_SeriesResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProjectSeries *pProjectSeries, void *pUserData);

void aslUGCSearchManager1_GetAuthorAllowsFeaturedList(UGCIDList *id_list, bool bIncludeAlreadyFeatured);

UGCSearchResult *aslUGCSearchManager1_FindUGCMapsForPlaying(UGCProjectSearchInfo *pSearchInfo);

S32 aslUGCSearchManager1_OncePerFrame(F32 fElapsed);

S32 UGCSearch_GetTotalSearchResultCount(const UGCContentInfo *const *const eaResults,
								const UGCProject *const *const eaUGCProjects,
								const UGCProjectSeries *const *const eaUGCSeries,
								const GameProgressionNodeRef *const *const eaProgressionNodes,
								const QueueDefRef *const *const eaQueues);

bool UGCSearch_ShouldEarlyOut(const UGCContentInfo *const *const eaResults,
								const UGCProject *const *const eaUGCProjects,
								const UGCProjectSeries *const *const eaUGCSeries,
								const GameProgressionNodeRef *const *const eaProgressionNodes,
								const QueueDefRef *const *const eaQueues);

SSSTreeSearchType SSSSearchTypeFromUGCComparison(UGCProjectSearchFilterComparison eComparison);
bool RunStringFilter(UGCProjectSearchFilter *pFilter, const char *pString);
bool RunNumberFilter(UGCProjectSearchFilter *pFilter, float fValue);
