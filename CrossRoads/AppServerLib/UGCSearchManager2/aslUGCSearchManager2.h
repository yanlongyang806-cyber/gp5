#pragma once

typedef struct UGCSearchResult UGCSearchResult;
typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;
typedef struct UGCIDList UGCIDList;
typedef struct UGCIntershardData UGCIntershardData;
typedef enum enumResourceEventType enumResourceEventType;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectSeries UGCProjectSeries;

int aslUGCSearchManager2_Init(void);

void aslUGCSearchManager2_ProjectResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProject *pProject, void *pUserData);
void aslUGCSearchManager2_SeriesResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProjectSeries *pProjectSeries, void *pUserData);

void aslUGCSearchManager2_GetAuthorAllowsFeaturedList(UGCIDList *id_list, bool bIncludeAlreadyFeatured);

UGCSearchResult *aslUGCSearchManager2_FindUGCMapsForPlaying(UGCProjectSearchInfo *pSearchInfo);

S32 aslUGCSearchManager2_OncePerFrame(F32 fElapsed);
