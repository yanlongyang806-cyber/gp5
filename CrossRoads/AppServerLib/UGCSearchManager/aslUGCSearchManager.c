#include "aslUGCSearchManager.h"
#include "aslUGCSearchManager1.h"
#include "aslUGCSearchManager2.h"

#include "AppServerLib.h"
#include "ServerLib.h"
#include "ugcprojectcommon.h"
#include "UGCCommon.h"
#include "ReferenceSystem.h"
#include "StringUtil.h"
#include "ResourceManager.h"
#include "AutoStartupSupport.h"
#include "error.h"
#include "sock.h"
#include "file.h"
#include "objTransactions.h"
#include "ChoiceTable_common.h"
#include "queue_common.h"
#include "ShardVariableCommon.h"
#include "ContinuousBuilderSupport.h"

#include "AutoGen\Controller_autogen_RemoteFuncs.h"
#include "AutoGen\GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen\AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen\ugcprojectcommon_h_ast.h"

#include "aslUGCSearchManager_h_ast.h"

static bool g_bUGCSearchVersion1 = 0;
AUTO_CMD_INT(g_bUGCSearchVersion1, UGCSearchVersion1);

static bool g_bUGCSearchVersion2 = 1;
AUTO_CMD_INT(g_bUGCSearchVersion2, UGCSearchVersion2);

static bool g_iUGCSearchVersionPreferredResult = 2;
AUTO_CMD_INT(g_iUGCSearchVersionPreferredResult, UGCSearchVersionPreferredResult);

int g_iMaximumDaysOldForNewContentList = 21;
AUTO_CMD_INT(g_iMaximumDaysOldForNewContentList, MaximumDaysOldForNewContentList);

bool g_bShowUnpublishedProjectsForDebugging;
AUTO_CMD_INT(g_bShowUnpublishedProjectsForDebugging, ShowUnpublishedProjectsForDebugging) ACMD_CMDLINE;

// The tag percentage of all reviews for the content to show up in a tag search
F32 g_fMinimumTagPercentageForSearch = 0.05;
AUTO_CMD_FLOAT(g_fMinimumTagPercentageForSearch, MinimumTagPercentageForSearch) ACMD_AUTO_SETTING(Ugc, UGCSEARCHMANAGER);

DictionaryHandle hUGCProjectDictionary = NULL;
DictionaryHandle hUGCProjectSeriesDictionary = NULL;

UGCProject *aslUGCSearchManager_GetProject(ContainerID id)
{
	char pchTemp[16];
	sprintf(pchTemp, "%u", id);

	return RefSystem_ReferentFromString(hUGCProjectDictionary, pchTemp);
}

UGCProjectSeries *aslUGCSearchManager_GetProjectSeries(ContainerID id)
{
	char pchTemp[16];
	sprintf(pchTemp, "%u", id);

	return RefSystem_ReferentFromString(hUGCProjectSeriesDictionary, pchTemp);
}

static void UGCProjectResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProject *pProject, void *pUserData)
{
	CONTAINER_NOCONST(UGCProject, pProject)->ugcReviews.iNumRatingsCached = ugcReviews_GetRatingCount( CONTAINER_NOCONST( UGCProjectReviews, &pProject->ugcReviews ));

	if(g_bUGCSearchVersion1)
		aslUGCSearchManager1_ProjectResCB(eType, pDictName, pResourceName, pProject, pUserData);
	if(g_bUGCSearchVersion2)
		aslUGCSearchManager2_ProjectResCB(eType, pDictName, pResourceName, pProject, pUserData);
}

static void UGCSeriesResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProjectSeries *pProjectSeries, void *pUserData)
{
	CONTAINER_NOCONST(UGCProjectSeries, pProjectSeries)->ugcReviews.iNumRatingsCached = ugcReviews_GetRatingCount( CONTAINER_NOCONST( UGCProjectReviews, &pProjectSeries->ugcReviews ));

	if(g_bUGCSearchVersion1)
		aslUGCSearchManager1_SeriesResCB(eType, pDictName, pResourceName, pProjectSeries, pUserData);
	if(g_bUGCSearchVersion2)
		aslUGCSearchManager2_SeriesResCB(eType, pDictName, pResourceName, pProjectSeries, pUserData);
}

AUTO_STARTUP(UGCSearchManager) 
ASTRT_DEPS(AS_GameProgression, AS_Messages, AS_UGCSearchManagerQueueLoad, ShardVariables, UGCReporting, UGCTags, UGCSearchConfigStartup);
void aslUGCSearchManagerStartup(void)
{
}

AUTO_STARTUP(AS_UGCSearchManagerQueueLoad) ASTRT_DEPS(QueueCategories, CharacterClasses);
void UGCSearchManagerQueueLoad(void)
{
	choice_Load();
	Queues_Load(NULL, Queues_ReloadQueues);	// Do not call the aslQueue version here. Bad container things will happen
	Queues_LoadConfig();
}

S32 UGCSearchManagerOncePerFrame(F32 fElapsed)
{
	int result = 1;

	shardvariable_OncePerFrame();

	if(g_bUGCSearchVersion1)
		result = aslUGCSearchManager1_OncePerFrame(fElapsed) && result;
	if(g_bUGCSearchVersion2)
		result = aslUGCSearchManager2_OncePerFrame(fElapsed) && result;

	return result;
}

int UGCSearchManagerLibInit(void)
{
	AutoStartup_SetTaskIsOn("UGCSearchManager", 1);

	objLoadAllGenericSchemas();
		
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_UGCSEARCHMANAGER, "UGC Search Manager type not set");

	loadstart_printf("Connecting UGCSearchManager to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");

	gAppServer->oncePerFrame = UGCSearchManagerOncePerFrame;

	hUGCProjectDictionary = RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), false, parse_UGCProject, false, false, NULL);
	resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), RES_DICT_KEEP_ALL);
	objSubscribeToOnlineContainers(GLOBALTYPE_UGCPROJECT);
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), UGCProjectResCB, NULL);

	hUGCProjectSeriesDictionary = RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECTSERIES), false, parse_UGCProjectSeries, false, false, NULL);
	resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECTSERIES), RES_DICT_KEEP_ALL);
	objSubscribeToOnlineContainers(GLOBALTYPE_UGCPROJECTSERIES);
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECTSERIES), UGCSeriesResCB, NULL);

	assert(g_bUGCSearchVersion1 || g_bUGCSearchVersion2);

	if(g_bUGCSearchVersion1 && !aslUGCSearchManager1_Init())
		return 0;

	if(g_bUGCSearchVersion2 && !aslUGCSearchManager2_Init())
		return 0;

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");

	return 1;
}

AUTO_RUN;
void UGCSearchManagerRegister(void)
{
	aslRegisterApp(GLOBALTYPE_UGCSEARCHMANAGER, UGCSearchManagerLibInit, 0);
}

SearchTypeReport sSearchTypeReports[SEARCH_LAST] = {0};
static StashTable hSearchTypeReports = NULL;

AUTO_RUN;
void InitSearchTypeReports(void)
{
	int i;

	hSearchTypeReports = stashTableCreateWithStringKeys(SEARCH_LAST, StashDefault);

	for (i=0; i < SEARCH_LAST; i++)
	{
		sSearchTypeReports[i].pName = strdup(StaticDefineIntRevLookup(eSearchTypeEnum, i));
		stashAddPointer(hSearchTypeReports, sSearchTypeReports[i].pName, &sSearchTypeReports[i], false);
	}

	resRegisterDictionaryForStashTable("SearchReports", RESCATEGORY_OTHER, 0, hSearchTypeReports, parse_SearchTypeReport);
}

static UGCSearchResult *FindUGCMaps_Internal(UGCProjectSearchInfo *pSearchInfo)
{
	UGCSearchResult *pUGCSearchResult = NULL;

	// this function modifies pSearchInfo! so we clone it first so our caching on the GameServer works.
	UGCProjectSearchInfo *pInputSearchInfo = StructClone(parse_UGCProjectSearchInfo, pSearchInfo);

	if(g_bUGCSearchVersion1)
	{
		UGCSearchResult *pUGCSearchResult1 = aslUGCSearchManager1_FindUGCMapsForPlaying(pSearchInfo);
		if(!g_bUGCSearchVersion2 || 2 != g_iUGCSearchVersionPreferredResult)
			pUGCSearchResult = pUGCSearchResult1;
		else
			StructDestroy(parse_UGCSearchResult, pUGCSearchResult1);
	}

	if(g_bUGCSearchVersion2)
	{
		UGCSearchResult *pUGCSearchResult2 = aslUGCSearchManager2_FindUGCMapsForPlaying(pSearchInfo);
		if(!g_bUGCSearchVersion1 || 2 == g_iUGCSearchVersionPreferredResult)
			pUGCSearchResult = pUGCSearchResult2;
		else
			StructDestroy(parse_UGCSearchResult, pUGCSearchResult2);
	}

	// remove any included projects that are in included series:
	// it would be nice to do this earlier, but the different searches work in different ways
	// so it's cleaner to deal with all cases here.
	{
		int i, j;
		for (i=eaSize(&pUGCSearchResult->eaResults)-1; i >= 0; i--)
		{
			UGCProject* project = NULL;
			if (!pUGCSearchResult->eaResults[i]->iUGCProjectID){
				continue;	//iterate over only the projects in the outside loop.
			}
			project = aslUGCSearchManager_GetProject(pUGCSearchResult->eaResults[i]->iUGCProjectID);
			if (project && project->seriesID){
				for (j=eaSize(&pUGCSearchResult->eaResults)-1; j >= 0; j--){
					if (!pUGCSearchResult->eaResults[j]->iUGCProjectSeriesID){
						continue;	//iterate over only the series in the inside loop.
					}
					if (pUGCSearchResult->eaResults[j]->iUGCProjectSeriesID == project->seriesID){
						eaRemove(&pUGCSearchResult->eaResults, i);
						break;
					}
				}
			}
		}
	}

	// Add series' projects to a separate, included list
	{
		int it;
		int projIt;
		for( it = 0; it != eaSize( &pUGCSearchResult->eaResults ); ++it ) {
			if( pUGCSearchResult->eaResults[ it ]->iUGCProjectSeriesID ) {
				UGCProjectSeries* series = aslUGCSearchManager_GetProjectSeries( pUGCSearchResult->eaResults[ it ]->iUGCProjectSeriesID );
				if( series ) {
					for( projIt = 0; projIt != ea32Size( &series->ugcSearchCache.eaPublishedProjectIDs ); ++projIt ) {
						ea32PushUnique( &pUGCSearchResult->eaProjectSeriesProjectIDs,
							series->ugcSearchCache.eaPublishedProjectIDs[ projIt ]);
					}
				}
			}
		}
	}

	StructDestroy(parse_UGCProjectSearchInfo, pInputSearchInfo);

	return pUGCSearchResult;
}

static void aslUGCSearchManager_ReturnHeaderCache(const char *pcShardName, ContainerID gameServerID, UGCSearchResult *pUGCSearchResult)
{
	UGCProjectList *accum = NULL;
	UGCIDList list;
	StructInit(parse_UGCIDList, &list);

	FOR_EACH_IN_EARRAY(pUGCSearchResult->eaResults, UGCContentInfo, pUGCContentInfo)
	{
		if(pUGCContentInfo->iUGCProjectID)
			ea32Push(&list.eaProjectIDs, pUGCContentInfo->iUGCProjectID);
		else if(pUGCContentInfo->iUGCProjectSeriesID)
			ea32Push(&list.eaProjectSeriesIDs, pUGCContentInfo->iUGCProjectSeriesID);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY_INT(pUGCSearchResult->eaProjectSeriesProjectIDs, ContainerID, uUGCProjectID)
	{
		ea32Push(&list.eaProjectIDs, uUGCProjectID);
	}
	FOR_EACH_END;

	accum = aslUGCSearchManager_SearchByID_Internal(&list);

	StructDeInit(parse_UGCIDList, &list);

	RemoteCommand_Intershard_gslUGC_CacheSearchByID_Return(pcShardName, GLOBALTYPE_GAMESERVER, gameServerID,
		accum, /*entContainerID=*/0, /*bCache=*/true);

	StructDestroy(parse_UGCProjectList, accum);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void FindUGCMapsForPlaying(UGCProjectSearchInfo *pSearchInfo)
{
	pSearchInfo->pUGCSearchResult = FindUGCMaps_Internal(pSearchInfo);

	if(pSearchInfo->loginCookie) // login server requested
	{
		RemoteCommand_Intershard_aslLoginUGCSearch_Return(pSearchInfo->pcShardName, GLOBALTYPE_LOGINSERVER, 0, pSearchInfo);
	}
	else if(pSearchInfo->entContainerID) // entity requested
	{
		RemoteCommand_Intershard_gslUGCSearch_Return(pSearchInfo->pcShardName, GLOBALTYPE_ENTITYPLAYER, pSearchInfo->entContainerID, pSearchInfo);
	}
	else if(pSearchInfo->gameServerID) // game server requested
	{
		aslUGCSearchManager_ReturnHeaderCache(pSearchInfo->pcShardName, pSearchInfo->gameServerID, pSearchInfo->pUGCSearchResult);

		RemoteCommand_Intershard_gslUGCSearch_Return(pSearchInfo->pcShardName, GLOBALTYPE_GAMESERVER, pSearchInfo->gameServerID, pSearchInfo);
	}
}

UGCProjectList *aslUGCSearchManager_SearchByID_Internal(UGCIDList* pList)
{
	UGCProjectList *accum = StructCreate( parse_UGCProjectList );
	int it;
	for( it = 0; it != ea32Size( &pList->eaProjectIDs ); ++it ) {
		UGCProject* proj = aslUGCSearchManager_GetProject( pList->eaProjectIDs[ it ]);
		if( proj && UGCProject_GetMostRecentPublishedVersion( proj )) {
			eaPush( &accum->eaProjects, UGCProject_CreateHeaderCopy( proj, true ));
		}
	}
	for( it = 0; it != ea32Size( &pList->eaProjectSeriesIDs ); ++it ) {
		UGCProjectSeries* series = aslUGCSearchManager_GetProjectSeries( pList->eaProjectSeriesIDs[ it ]);
		if( series ) {
			eaPush( &accum->eaProjectSeries, UGCProjectSeries_CreateHeaderCopy( series ));
		}
	}

	return accum;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_SearchByID(UGCIDList* pList, UGCIntershardData *pUGCIntershardData)
{
	UGCProjectList *accum = NULL;

	accum = aslUGCSearchManager_SearchByID_Internal(pList);

	if(pUGCIntershardData->entContainerID)
		RemoteCommand_Intershard_gslUGC_CacheSearchByID_Return(pUGCIntershardData->pcShardName, GLOBALTYPE_ENTITYPLAYER, pUGCIntershardData->entContainerID,
			accum, pUGCIntershardData->entContainerID, /*bCache=*/false);
	else if(pUGCIntershardData->loginCookie)
		RemoteCommand_Intershard_aslLogin_UGCSearchByID_Return(pUGCIntershardData->pcShardName, GLOBALTYPE_LOGINSERVER, 0, accum, pUGCIntershardData->loginCookie);

	StructDestroy(parse_UGCProjectList, accum);
}

const UGCProject *aslUGCSearchManager_GetFirstPublishedProject(const UGCProjectSeriesVersion *pUGCProjectSeriesVersion)
{
	if(!pUGCProjectSeriesVersion)
		return NULL;

	{
		const UGCProjectVersion* pFirstProjectVersion = NULL;
		const UGCProjectSeriesNode* node = eaGet(&pUGCProjectSeriesVersion->eaChildNodes, 0);
		while(node && !node->iProjectID)
			node = eaGet(&node->eaChildNodes, 0);
		if(node)
			return aslUGCSearchManager_GetProject(node->iProjectID);
	}

	return NULL;
}

float aslUGCSearchManager_SeriesAverageDurationInMinutes(const UGCProjectSeriesVersion *pUGCProjectSeriesVersion)
{
	float fValue = 0.0f;
	if(pUGCProjectSeriesVersion)
	{
		int it;
		for(it = 0; it != eaSize(&pUGCProjectSeriesVersion->eaChildNodes); ++it)
		{
			const UGCProjectSeriesNode *node = pUGCProjectSeriesVersion->eaChildNodes[it];
			if(node->iProjectID)
			{
				UGCProject* project = aslUGCSearchManager_GetProject(node->iProjectID);
				if(project)
					fValue += UGCProject_AverageDurationInMinutes(project);
			}
		}
	}
	return fValue;
}

bool ProjectIsSearchable(const UGCProject *pProject)
{
	return UGCProject_IsPublishedAndNotDeleted(pProject);
}

bool ProjectMeetsBasicCriteria(UGCProjectSearchInfo *pSearchInfo, UGCProject *pProject)
{
	const UGCProjectVersion *pPublishedVersion;

	if(!g_bShowUnpublishedProjectsForDebugging && !ProjectIsSearchable(pProject))
		return false; // PROJECT AND GLOBAL

	pPublishedVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

	// StarTrek uses AL to find banned projects, future featured projects, projects outside player level and allegiance. NW does not want to do this at the moment.
	// UGC builders want to use default search to find the recently published project.
	if((0 == stricmp(GetProductName(), "StarTrek") && pSearchInfo->iAccessLevel > ACCESS_UGC)
		|| g_isContinuousBuilder)
	{
		return true; // SEARCH or STARTUP
	}

	// If this is a featured copy, and it has not yet been featured, we don't want to show it to the masses
	if ( pProject->uUGCFeaturedOrigProjectID && !UGCProject_IsFeatured( CONTAINER_NOCONST( UGCProject, pProject ),
		/*bIncludePreviouslyFeatured=*/true, /*bIncludeFutureFeatured=*/false )) {
			return false; // PROJECT and TIME
	}

	if (ugcDefaultsSearchFiltersByPlayerLevel())
	{
		if ( pPublishedVersion->pRestrictions && pPublishedVersion->pRestrictions->iMinLevel && ( pSearchInfo->iPlayerLevel < pPublishedVersion->pRestrictions->iMinLevel ) )
		{
			return false; // PROJECT and SEARCH
		}
		if ( pPublishedVersion->pRestrictions && pPublishedVersion->pRestrictions->iMaxLevel && ( pSearchInfo->iPlayerLevel > pPublishedVersion->pRestrictions->iMaxLevel ) )
		{
			return false; // PROJECT and SEARCH
		}
	}

	if (pSearchInfo->pchPlayerAllegiance && pSearchInfo->pchPlayerAllegiance[0] && pPublishedVersion->pRestrictions && eaSize(&pPublishedVersion->pRestrictions->eaFactions))
	{
		bool bMatched = false;
		int i;

		for (i = 0;i < eaSize(&pPublishedVersion->pRestrictions->eaFactions); i++)
		{
			if (stricmp_safe(pSearchInfo->pchPlayerAllegiance, pPublishedVersion->pRestrictions->eaFactions[i]->pcFaction) == 0)
			{
				bMatched = true;
				break;
			}
		}

		if (!bMatched)
		{
			return false; // PROJECT and SEARCH
		}
	}

	if (pProject->ugcReporting.uTemporaryBanExpireTime > 0)
	{
		return false; // PROJECT AND TIME
	}

	if(!pSearchInfo->bIsIDString)
	{
		bool bStillInReview = (pProject->ugcReviews.iNumRatingsCached < ugc_NumReviewsBeforeNonReviewerCanPlay);
		bool bLookingForInReview = (pSearchInfo->eSpecialType == SPECIALSEARCH_REVIEWER);

		if(bStillInReview != bLookingForInReview)
			return false; // PROJECT AND SEARCH
	}

	return true;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_Featured_GetAuthorAllowsFeaturedList(const char *pcShardName, ContainerID entContainerID, bool bShow, bool bIncludeAlreadyFeatured)
{
	UGCIDList idList = { 0 };
	UGCProjectList* accum = NULL;

	if(g_bUGCSearchVersion1 && (!g_bUGCSearchVersion2 || 2 != g_iUGCSearchVersionPreferredResult))
		aslUGCSearchManager1_GetAuthorAllowsFeaturedList(&idList, bIncludeAlreadyFeatured);
	if(g_bUGCSearchVersion2 && (!g_bUGCSearchVersion1 || 2 == g_iUGCSearchVersionPreferredResult))
		aslUGCSearchManager2_GetAuthorAllowsFeaturedList(&idList, bIncludeAlreadyFeatured);

	accum = aslUGCSearchManager_SearchByID_Internal(&idList);
	StructReset( parse_UGCIDList, &idList );

	RemoteCommand_Intershard_gslUGC_Featured_GetAuthorAllowsFeaturedList_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, accum, bShow);

	StructDestroy(parse_UGCProjectList, accum);
}

#include "aslUGCSearchManager_h_ast.c"
