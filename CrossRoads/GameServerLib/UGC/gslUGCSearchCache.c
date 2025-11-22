#include "gslUGCSearchCache.h"

#include "timing.h"
#include "error.h"
#include "textparser.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringUtil.h"
#include "TimedCallback.h"
#include "EntityLib.h"
#include "Alerts.h"

#include "ugcprojectcommon.h"

#include "gslUGCSearchCache_c_ast.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct UGCSearchCacheConfig UGCSearchCacheConfig;
typedef struct UGCSearchCacheEntry UGCSearchCacheEntry;
typedef enum UGCSearchCacheCriteria UGCSearchCacheCriteria;

// UGC Search Cache Config is loaded from a file and contains all game-specific configuration on the
// searches that should be cached and for how long they should be cached.
AUTO_STRUCT;
typedef struct UGCSearchCacheConfig
{
	// Individual search cache configs
	UGCSearchCacheEntry **eaUGCSearchCacheEntries;	AST(NAME(UGCSearchCacheEntry))
} UGCSearchCacheConfig;
extern ParseTable parse_UGCSearchCacheConfig[];
#define TYPE_parse_UGCSearchCacheConfig UGCSearchCacheConfig

// UGC Search Cache Config is loaded from a file and contains all game-specific configuration on the
// searches that should be cached and for how long they should be cached.
AUTO_STRUCT;
typedef struct UGCSearchCacheResult
{
	U32 uLastRequestSeconds;						AST(NAME(LastRequestSeconds))

	U32 uLastUpdateSeconds;							AST(NAME(LastUpdateSeconds))

	UGCProjectSearchInfo *pUGCProjectSearchInfo;	AST(NAME(UGCProjectSearchInfo))

	ContainerID *eaEntContainerIDs;					AST(NAME(EntContainerIDs))
} UGCSearchCacheResult;
extern ParseTable parse_UGCSearchCacheResult[];
#define TYPE_parse_UGCSearchCacheResult UGCSearchCacheResult

// UGC Search Cache Entry defines specific search criteria that should have a cached result
AUTO_STRUCT;
typedef struct UGCSearchCacheEntry
{
	UGCSearchCacheCriteria AccessLevel;						AST(NAME(AccessLevel))
	UGCSearchCacheCriteria Lang;							AST(NAME(Lang))
	UGCSearchCacheCriteria Location;						AST(NAME(Location))
	UGCSearchCacheCriteria SpecialType;						AST(NAME(SpecialType))

	UGCProjectSearchInfo *pUGCProjectSearchInfo;			AST(NAME(UGCProjectSearchInfo))

	UGCSearchCacheResult **eaUGCSearchCacheResults;			AST(NO_WRITE)
} UGCSearchCacheEntry;
extern ParseTable parse_UGCSearchCacheEntry[];
#define TYPE_parse_UGCSearchCacheEntry UGCSearchCacheEntry

// UGC Search Cache Criteria indicates how to match a particular field in UGCProjectSearchInfo against UGCSearchCacheEntry
AUTO_ENUM;
typedef enum UGCSearchCacheCriteria {
	// Ignore means don't even match the field. This will actually assume the fields match.
	// Used for fields like PlayerLevel in the case of NW where all UGC content is level-less.
	// Setting PlayerLevel to Ignore allows any player of any level to get the same search results from the cache as anyone else.
	UGCSearchCacheCriteria_Ignore		= 0,

	// Exact means this field must be an exact match for even considering to cache the incoming UGCProjectSearchInfo against the entry.
	// Used for fields like Simple_Raw, where we never want to cache incoming UGCProjectSearchInfo requests with this field being non-empty.
	UGCSearchCacheCriteria_Exact		= 1,

	// Separate means to actually cache separately every instance of this field for incoming UGCProjectSearchInfo requests
	// Used for fields whose difference matters. For example, in a multi-language game (STO and NW) the Language field must have this criteria.
	UGCSearchCacheCriteria_Separate		= 2
} UGCSearchCacheCriteria;
extern StaticDefineInt UGCSearchCacheCriteriaEnum[];

static UGCSearchCacheConfig *s_pUGCSearchCacheConfig = NULL;

static StashTable s_UGCProjectHeaderCacheStash = NULL;
static StashTable s_UGCSeriesHeaderCacheStash = NULL;

static void ProjectDtor(UGCProject *pProject)
{
	StructDestroy(parse_UGCProject, pProject);
}

static void SeriesDtor(UGCProjectSeries *pSeries)
{
	StructDestroy(parse_UGCProjectSeries, pSeries);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_HIDE ACMD_ACCESSLEVEL(4);
void ugcSearchCacheClear(void)
{
	if(s_pUGCSearchCacheConfig)
	{
		FOR_EACH_IN_EARRAY(s_pUGCSearchCacheConfig->eaUGCSearchCacheEntries, UGCSearchCacheEntry, pUGCSearchCacheEntry)
		{
			eaDestroyStruct(&pUGCSearchCacheEntry->eaUGCSearchCacheResults, parse_UGCSearchCacheResult);
		}
		FOR_EACH_END;
	}

	stashTableClearEx(s_UGCProjectHeaderCacheStash, NULL, ProjectDtor);
	stashTableClearEx(s_UGCSeriesHeaderCacheStash, NULL, SeriesDtor);
}

// If non-zero, then the search cache is enabled for all GameServers
static int s_bUGCEnableSearchCache = 1;
AUTO_CMD_INT(s_bUGCEnableSearchCache, UGCEnableSearchCache) ACMD_AUTO_SETTING(Ugc, GAMESERVER) ACMD_CALLBACK(UGCEnableSearchCacheChanged);
void UGCEnableSearchCacheChanged(CMDARGS)
{
	if(s_bUGCEnableSearchCache)
	{
		if(!s_UGCProjectHeaderCacheStash) s_UGCProjectHeaderCacheStash = stashTableCreateInt(1);
		if(!s_UGCSeriesHeaderCacheStash) s_UGCSeriesHeaderCacheStash = stashTableCreateInt(1);
	}
	else
		ugcSearchCacheClear();
}

// If non-zero, maximum number of waiting entities for a cached UGC search. After this number, we start returning empty results for the default search.
// This only ever occurs during a login burst when GameServers are starting up for the first time. Once there exists cached search results,
// they will always be returned instead of timing out any entities.
static int s_iUGCSearchCacheMaxWaitingEntities = 500;
AUTO_CMD_INT(s_iUGCSearchCacheMaxWaitingEntities, UGCSearchCacheMaxWaitingEntities) ACMD_AUTO_SETTING(Ugc, GAMESERVER);

// If non-zero, then the search cache will have a timeout on it where any waiting Entities are sent an empty search result
static U32 s_uUGCSearchCacheTimeoutInSeconds = 15;
AUTO_CMD_INT(s_uUGCSearchCacheTimeoutInSeconds, UGCSearchCacheTimeoutInSeconds) ACMD_AUTO_SETTING(Ugc, GAMESERVER);

// The duration in seconds that we will return a cached result
static U32 s_uUGCSearchCacheDurationInSeconds = 300;
AUTO_CMD_INT(s_uUGCSearchCacheDurationInSeconds, UGCSearchCacheDurationInSeconds) ACMD_AUTO_SETTING(Ugc, GAMESERVER);

#if 0
#define UGC_SEARCH_CACHE_PRINTF(fmt, ...) printf(fmt "\n", __VA_ARGS__)
#else
#define UGC_SEARCH_CACHE_PRINTF(fmt, ...)
#endif

static void ugcSearchCacheConfigLoad()
{
	loadstart_printf("Loading UGC Search Cache Config...");

	if(s_pUGCSearchCacheConfig)
		StructReset(parse_UGCSearchCacheConfig, s_pUGCSearchCacheConfig);
	else
		s_pUGCSearchCacheConfig = StructCreate(parse_UGCSearchCacheConfig);

	if(!ParserLoadFiles(NULL, "genesis/ugc_search_cache_config.txt", "UGCSearchCacheConfig.bin", PARSER_OPTIONALFLAG, parse_UGCSearchCacheConfig, s_pUGCSearchCacheConfig))
	{
		Errorf("Error loading UGC Search Cache Config... disabling functionality.");
		StructReset(parse_UGCSearchCacheConfig, s_pUGCSearchCacheConfig);
	}

	loadend_printf(" done");
}

static void ugcSearchCacheConfigReload(const char *pchRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ugcSearchCacheConfigLoad();
}

AUTO_STARTUP(UGCSearchCache);
void ugcSearchCacheStartup(void)
{
	ugcSearchCacheConfigLoad();

	if(isDevelopmentMode())
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "genesis/ugc_search_cache_config.txt", ugcSearchCacheConfigReload);
}

static bool ugcSearchMatch(UGCProjectSearchInfo *pUGCProjectSearchInfo1, UGCProjectSearchInfo *pUGCProjectSearchInfo2, UGCSearchCacheEntry *pUGCSearchCacheEntry,
	bool bTreatSeparateAsExact)
{
#define RETURN_FALSE_UNLESS_MATCH_BASIC(criteria, field) \
	do { \
		if(UGCSearchCacheCriteria_Exact == pUGCSearchCacheEntry->criteria || (bTreatSeparateAsExact && UGCSearchCacheCriteria_Separate == pUGCSearchCacheEntry->criteria)) \
			if(pUGCProjectSearchInfo1->field != pUGCProjectSearchInfo2->field) \
				return false; \
	} while(0)

	if(!pUGCProjectSearchInfo1 || !pUGCProjectSearchInfo2 || !pUGCSearchCacheEntry)
		return false;
#define RETURN_FALSE_UNLESS_MATCH_STRING(criteria, field) \
	do { \
		if(UGCSearchCacheCriteria_Exact == pUGCSearchCacheEntry->criteria || (bTreatSeparateAsExact && UGCSearchCacheCriteria_Separate == pUGCSearchCacheEntry->criteria)) \
			if(0 != stricmp(pUGCProjectSearchInfo1->field, pUGCProjectSearchInfo2->field)) \
				return false; \
	} while(0)

	if(!pUGCProjectSearchInfo1 || !pUGCProjectSearchInfo2 || !pUGCSearchCacheEntry)
		return false;

	RETURN_FALSE_UNLESS_MATCH_BASIC(AccessLevel, iAccessLevel);
	RETURN_FALSE_UNLESS_MATCH_BASIC(Lang, eLang);
	RETURN_FALSE_UNLESS_MATCH_STRING(Location, pchLocation);
	RETURN_FALSE_UNLESS_MATCH_BASIC(SpecialType, eSpecialType);

	if(eaSize(&pUGCProjectSearchInfo1->ppFilters) || eaSize(&pUGCProjectSearchInfo2->ppFilters)) // not really supporting match on EArrays yet
		return false;
	if(eaiSize(&pUGCProjectSearchInfo1->eaiIncludeAllTags) || eaiSize(&pUGCProjectSearchInfo2->eaiIncludeAllTags)) // not really supporting match on EArrays yet
		return false;
	if(eaiSize(&pUGCProjectSearchInfo1->eaiIncludeAnyTags) || eaiSize(&pUGCProjectSearchInfo2->eaiIncludeAnyTags)) // not really supporting match on EArrays yet
		return false;
	if(eaiSize(&pUGCProjectSearchInfo1->eaiIncludeNoneTags) || eaiSize(&pUGCProjectSearchInfo2->eaiIncludeNoneTags)) // not really supporting match on EArrays yet
		return false;
	if(!nullStr(pUGCProjectSearchInfo1->pchPlayerAllegiance) || !nullStr(pUGCProjectSearchInfo2->pchPlayerAllegiance)) // not really supporting match on PlayerAllegiance yet
		return false;
	if(!nullStr(pUGCProjectSearchInfo1->pSimple_Raw) || !nullStr(pUGCProjectSearchInfo2->pSimple_Raw)) // unsupported
		return false;
	if(!nullStr(pUGCProjectSearchInfo1->pSimple_SSSTree) || !nullStr(pUGCProjectSearchInfo2->pSimple_SSSTree)) // not really supporting match on text yet
		return false;
	if(pUGCProjectSearchInfo1->pSubscription || pUGCProjectSearchInfo2->pSubscription) // not really supporting match on pointers yet
		return false;

	return true;
}

typedef struct UGCSearchCacheStats
{
	int iTimeOut;
	int iEntitiesTimedOut;

	int iCacheRequests;

	int iCacheHit;
	int iCacheHit_OutOfDate;

	int iCacheMiss;
	int iCacheMiss_TooManyEntities;
	int iCacheMiss_EntitySpams;
	int iCacheMiss_EntityWaitListed;
	int iCacheMiss_Seed;

	int iNotCached;
} UGCSearchCacheStats;
UGCSearchCacheStats s_UGCSearchCacheStats = {0};

AUTO_COMMAND;
void ugcSearchCacheDumpStats(CmdContext *cmd_context)
{
	estrPrintf(cmd_context->output_msg, "TimeOut=%d, EntitiesTimedOut=%d, CacheRequests=%d, CacheHit=%d, CacheHit_OutOfDate=%d, CacheMiss=%d, CacheMiss_TooManyEntities=%d, CacheMiss_EntitySpams=%d, " \
		"CacheMiss_EntityWaitListed=%d, CacheMiss_Seed=%d, NotCached=%d\n",
		s_UGCSearchCacheStats.iTimeOut, s_UGCSearchCacheStats.iEntitiesTimedOut, s_UGCSearchCacheStats.iCacheRequests, s_UGCSearchCacheStats.iCacheHit, s_UGCSearchCacheStats.iCacheHit_OutOfDate,
		s_UGCSearchCacheStats.iCacheMiss, s_UGCSearchCacheStats.iCacheMiss_TooManyEntities, s_UGCSearchCacheStats.iCacheMiss_EntitySpams, s_UGCSearchCacheStats.iCacheMiss_EntityWaitListed,
		s_UGCSearchCacheStats.iCacheMiss_Seed, s_UGCSearchCacheStats.iNotCached);
}

static UGCSearchCacheEntry *ugcSearchCacheFindEntry(UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	FOR_EACH_IN_EARRAY(s_pUGCSearchCacheConfig->eaUGCSearchCacheEntries, UGCSearchCacheEntry, pUGCSearchCacheEntry)
	{
		if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheEntry->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/false))
			return pUGCSearchCacheEntry;
	}
	FOR_EACH_END;

	return NULL;
}

static void ugcSearchCacheReceiveForEntContainerID(UGCProjectSearchInfo* pSearch, ContainerID entContainerID)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);

	UGC_SEARCH_CACHE_PRINTF("%s (%u): %u", __FUNCTION__, timeSecondsSince2000(), entContainerID);

	if(pSearch && pEntity)
		ClientCmd_gclUGC_ReceiveSearchResult(pEntity, pSearch->pUGCSearchResult);
}

static void ugcSearchCacheTimedCallback(TimedCallback *callback, F32 timeSinceLastCallback, UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	// Does this search request match one we will cache? It better!
	UGCSearchCacheEntry *pUGCSearchCacheEntry = ugcSearchCacheFindEntry(pUGCProjectSearchInfo);

	UGC_SEARCH_CACHE_PRINTF("%s", __FUNCTION__);

	if(pUGCSearchCacheEntry)
	{
		UGC_SEARCH_CACHE_PRINTF("\t%s", "would cache");

		// See if we have already cached this exact search result
		FOR_EACH_IN_EARRAY(pUGCSearchCacheEntry->eaUGCSearchCacheResults, UGCSearchCacheResult, pUGCSearchCacheResult)
		{
			if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/true))
			{
				UGC_SEARCH_CACHE_PRINTF("\t%s", "previously requested");

				if(!pUGCSearchCacheResult->pUGCProjectSearchInfo->pUGCSearchResult)
				{
					TriggerAutoGroupingAlert("UGC_SEARCH_CACHE_TIMEOUT", ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER, 60*60, "The GameServer (%u) UGC Search Cache timed out for a default search. Entity count waiting for result: %d",
						GetAppGlobalID(), eaiSize(&pUGCSearchCacheResult->eaEntContainerIDs));

					s_UGCSearchCacheStats.iTimeOut++;

					if(eaiSize(&pUGCSearchCacheResult->eaEntContainerIDs))
					{
						// Iterate over every waiting entity and inform them of load error. This will only ever happen upon the first time a cached search is requested from a GameServer,
						// typically during login bursts, but also any time a new GameServer is starting up.
						pUGCProjectSearchInfo->pUGCSearchResult = StructCreate(parse_UGCSearchResult);
						SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pUGCProjectSearchInfo->pUGCSearchResult->hErrorMessage);

						FOR_EACH_IN_EARRAY_INT(pUGCSearchCacheResult->eaEntContainerIDs, ContainerID, entContainerID)
						{
							ugcSearchCacheReceiveForEntContainerID(pUGCProjectSearchInfo, entContainerID);
						}
						FOR_EACH_END;

						s_UGCSearchCacheStats.iEntitiesTimedOut += eaiSize(&pUGCSearchCacheResult->eaEntContainerIDs);
					}

					eaRemoveFast(&pUGCSearchCacheEntry->eaUGCSearchCacheResults, FOR_EACH_IDX(pUGCSearchCacheEntry->eaUGCSearchCacheResults, pUGCSearchCacheResult));
					StructDestroy(parse_UGCSearchCacheResult, pUGCSearchCacheResult);
				}
				else
				{
					UGC_SEARCH_CACHE_PRINTF("\t%s", "cache exists");
				}

				break;
			}
		}
		FOR_EACH_END;
	}

	StructDestroy(parse_UGCProjectSearchInfo, pUGCProjectSearchInfo);
}

static void ugcSearchCache_FindUGCMapsForPlaying(UGCProjectSearchInfo *pSearch)
{
	UGC_SEARCH_CACHE_PRINTF("%s (%u)", __FUNCTION__, timeSecondsSince2000());

	// IMPORTANT!
	// If we are caching, we need to tell UGCSearchManager the GameServer that is making the request so that GameServer will get the return call!
	pSearch->entContainerID = 0;
	pSearch->gameServerID = GetAppGlobalID();

	s_UGCSearchCacheStats.iCacheRequests++;

	RemoteCommand_Intershard_FindUGCMapsForPlaying(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM, pSearch);
}

void ugcSearchCacheRequest(UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	UGC_SEARCH_CACHE_PRINTF("%s (%u)", __FUNCTION__, pUGCProjectSearchInfo->entContainerID);

	if(s_bUGCEnableSearchCache)
	{
		// Does this search request match one we will cache?
		UGCSearchCacheEntry *pUGCSearchCacheEntry = ugcSearchCacheFindEntry(pUGCProjectSearchInfo);

		UGC_SEARCH_CACHE_PRINTF("\t%s", "cache enabled");

		if(pUGCSearchCacheEntry)
		{
			UGC_SEARCH_CACHE_PRINTF("\t%s", "would cache");

			// See if we have already cached this exact search result
			FOR_EACH_IN_EARRAY(pUGCSearchCacheEntry->eaUGCSearchCacheResults, UGCSearchCacheResult, pUGCSearchCacheResult)
			{
				if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/true))
				{
					UGC_SEARCH_CACHE_PRINTF("\t%s", "previously requested");

					// We have at least requested this cached search before
					if(pUGCSearchCacheResult->pUGCProjectSearchInfo->pUGCSearchResult)
					{
						UGC_SEARCH_CACHE_PRINTF("\t%s", "cache exists, return it");

						s_UGCSearchCacheStats.iCacheHit++;

						// Return the cached result
						ugcSearchCacheReceiveForEntContainerID(pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCProjectSearchInfo->entContainerID);

						if(timeSecondsSince2000() > pUGCSearchCacheResult->uLastUpdateSeconds + s_uUGCSearchCacheDurationInSeconds
							&& s_uUGCSearchCacheTimeoutInSeconds > 0 && timeSecondsSince2000() > pUGCSearchCacheResult->uLastRequestSeconds + s_uUGCSearchCacheTimeoutInSeconds)
						{
							UGC_SEARCH_CACHE_PRINTF("\t%s", "out of date, re-request it");

							s_UGCSearchCacheStats.iCacheHit_OutOfDate++;

							pUGCSearchCacheResult->uLastRequestSeconds = timeSecondsSince2000();

							// The cached search result is out of date, use the callback to request an update
							ugcSearchCache_FindUGCMapsForPlaying(pUGCProjectSearchInfo);
						}
					}
					else
					{
						UGC_SEARCH_CACHE_PRINTF("\t%s", "cache does not exist");

						s_UGCSearchCacheStats.iCacheMiss++;

						if(s_iUGCSearchCacheMaxWaitingEntities > 0 && eaiSize(&pUGCSearchCacheResult->eaEntContainerIDs) >= s_iUGCSearchCacheMaxWaitingEntities)
						{
							UGC_SEARCH_CACHE_PRINTF("\t%s", "too many waiting, return empty");

							pUGCProjectSearchInfo->pUGCSearchResult = StructCreate(parse_UGCSearchResult);
							SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pUGCProjectSearchInfo->pUGCSearchResult->hErrorMessage);

							ugcSearchCacheReceiveForEntContainerID(pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCProjectSearchInfo->entContainerID);

							s_UGCSearchCacheStats.iCacheMiss_TooManyEntities++;
						}
						else
						{
							UGC_SEARCH_CACHE_PRINTF("\t%s", "wait list");

							// The cached search result is either not yet available or is out of date, record that this entity wants the result and return NULL
							if(pUGCProjectSearchInfo->entContainerID)
							{
								int index = eaiPushUnique(&pUGCSearchCacheResult->eaEntContainerIDs, pUGCProjectSearchInfo->entContainerID);
								if(index < eaiSize(&pUGCSearchCacheResult->eaEntContainerIDs) - 1)
									s_UGCSearchCacheStats.iCacheMiss_EntitySpams++;

								s_UGCSearchCacheStats.iCacheMiss_EntityWaitListed++;
							}
						}
					}

					return; // we have taken care of this entity in some fashion, bail
				}
			}
			FOR_EACH_END;

			UGC_SEARCH_CACHE_PRINTF("\t%s", "first request");

			s_UGCSearchCacheStats.iCacheMiss_Seed++;

			// We have never requested this exact cached search (or every request has timed out) so create the entry for it
			// and record the first entity waiting for this result.

			{
				UGCSearchCacheResult *pUGCSearchCacheResult = StructCreate(parse_UGCSearchCacheResult);
				pUGCSearchCacheResult->pUGCProjectSearchInfo = StructClone(parse_UGCProjectSearchInfo, pUGCProjectSearchInfo);
				pUGCSearchCacheResult->uLastRequestSeconds = timeSecondsSince2000();
				eaPush(&pUGCSearchCacheEntry->eaUGCSearchCacheResults, pUGCSearchCacheResult);
				if(pUGCProjectSearchInfo->entContainerID)
					eaiPush(&pUGCSearchCacheResult->eaEntContainerIDs, pUGCProjectSearchInfo->entContainerID);
			}

			ugcSearchCache_FindUGCMapsForPlaying(pUGCProjectSearchInfo);

			// Use a timed callback to flush out any entities that queue up waiting for the response
			TimedCallback_Run(ugcSearchCacheTimedCallback, StructClone(parse_UGCProjectSearchInfo, pUGCProjectSearchInfo), s_uUGCSearchCacheTimeoutInSeconds);

			return; // we have taken care of this first entity requesting this search from the GameServer, bail
		}
	}

	// Here, we either have caching disabled or the request is not cachable (e.g. custom search)
	UGC_SEARCH_CACHE_PRINTF("\t%s", "requesting uncached");

	s_UGCSearchCacheStats.iNotCached++;

	// we are performing an uncached search
	RemoteCommand_Intershard_FindUGCMapsForPlaying(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM, pUGCProjectSearchInfo);
}

void ugcSearchCacheReceive(UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	if(s_bUGCEnableSearchCache)
	{
		// Does this search request match one we will cache?
		UGCSearchCacheEntry *pUGCSearchCacheEntry = ugcSearchCacheFindEntry(pUGCProjectSearchInfo);

		UGC_SEARCH_CACHE_PRINTF("%s", __FUNCTION__);

		if(pUGCSearchCacheEntry)
		{
			UGC_SEARCH_CACHE_PRINTF("\t%s", "would cache");

			// See if we have already cached this exact search result
			FOR_EACH_IN_EARRAY(pUGCSearchCacheEntry->eaUGCSearchCacheResults, UGCSearchCacheResult, pUGCSearchCacheResult)
			{
				if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/true))
				{
					UGC_SEARCH_CACHE_PRINTF("\t%s", "exact match");

					// We have at least requested this cached search before, update the result
					StructDestroySafe(parse_UGCSearchResult, &pUGCSearchCacheResult->pUGCProjectSearchInfo->pUGCSearchResult);

					pUGCSearchCacheResult->uLastUpdateSeconds = timeSecondsSince2000();
					pUGCSearchCacheResult->pUGCProjectSearchInfo->pUGCSearchResult = StructClone(parse_UGCSearchResult, pUGCProjectSearchInfo->pUGCSearchResult);

					FOR_EACH_IN_EARRAY_INT(pUGCSearchCacheResult->eaEntContainerIDs, ContainerID, entContainerID)
					{
						ugcSearchCacheReceiveForEntContainerID(pUGCProjectSearchInfo, entContainerID);
					}
					FOR_EACH_END;

					eaiDestroy(&pUGCSearchCacheResult->eaEntContainerIDs);

					return;
				}
			}
			FOR_EACH_END;
		}
	}
	
	ugcSearchCacheReceiveForEntContainerID(pUGCProjectSearchInfo, pUGCProjectSearchInfo->entContainerID);
}

typedef struct UGCProjectHeaderCache
{
	UGCProject *pProject;
	U32 uTimeLastUpdated;
} UGCProjectHeaderCache;

typedef struct UGCSeriesHeaderCache
{
	UGCProjectSeries *pSeries;
	U32 uTimeLastUpdated;
} UGCSeriesHeaderCache;

void ugcSearchCacheRequestHeaders(Entity *pEntity, UGCIDList* pUGCIDList)
{
	UGC_SEARCH_CACHE_PRINTF("%s (%u): %u", __FUNCTION__, timeSecondsSince2000(), entGetContainerID(pEntity));

	if(s_bUGCEnableSearchCache)
	{
		UGCProjectList result = {0};

		FOR_EACH_IN_EARRAY_INT(pUGCIDList->eaProjectIDs, ContainerID, uUGCProjectID)
		{
			UGCProject *pProject = NULL;
			if(stashIntFindPointer(s_UGCProjectHeaderCacheStash, uUGCProjectID, &pProject) && pProject)
			{
				eaPush(&result.eaProjects, pProject);
				ea32RemoveFast(&pUGCIDList->eaProjectIDs, FOR_EACH_IDX(pIDs->eaProjectIDs, uUGCProjectID));
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_INT(pUGCIDList->eaProjectSeriesIDs, ContainerID, uUGCSeriesID)
		{
			UGCProjectSeries *pSeries = NULL;
			if(stashIntFindPointer(s_UGCSeriesHeaderCacheStash, uUGCSeriesID, &pSeries) && pSeries)
			{
				eaPush(&result.eaProjectSeries, pSeries);
				ea32RemoveFast(&pUGCIDList->eaProjectSeriesIDs, FOR_EACH_IDX(pIDs->eaProjectSeriesIDs, uUGCSeriesID));
			}
		}
		FOR_EACH_END;

		if(eaSize(&result.eaProjects) + eaSize(&result.eaProjectSeries))
		{
			ClientCmd_gclUGC_CacheReceiveSearchResult(pEntity, &result);

			eaDestroy(&result.eaProjects);
			eaDestroy(&result.eaProjectSeries);
		}
	}
}

void ugcSearchCacheReceiveHeaders(UGCProjectList *pUGCProjectList)
{
	UGC_SEARCH_CACHE_PRINTF("%s (%u)", __FUNCTION__, timeSecondsSince2000());

	if(s_bUGCEnableSearchCache)
	{
		if(!s_UGCProjectHeaderCacheStash) s_UGCProjectHeaderCacheStash = stashTableCreateInt(1);
		if(!s_UGCSeriesHeaderCacheStash) s_UGCSeriesHeaderCacheStash = stashTableCreateInt(1);

		FOR_EACH_IN_EARRAY(pUGCProjectList->eaProjects, UGCProject, pProject)
		{
			UGCProject *pNewProject = StructClone(parse_UGCProject, pProject);
			StashElement stashElement;
			if(!stashIntAddPointerAndGetElement(s_UGCProjectHeaderCacheStash, pNewProject->id, pNewProject, /*bOverwriteIfFound=*/false, &stashElement))
			{
				UGCProject *pOldProject = stashElementGetPointer(stashElement);

				StructDestroySafe(parse_UGCProject, &pOldProject);

				stashElementSetPointer(stashElement, pNewProject);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pUGCProjectList->eaProjectSeries, UGCProjectSeries, pSeries)
		{
			UGCProjectSeries *pNewSeries = StructClone(parse_UGCProjectSeries, pSeries);
			StashElement stashElement;
			if(!stashIntAddPointerAndGetElement(s_UGCSeriesHeaderCacheStash, pNewSeries->id, pNewSeries, /*bOverwriteIfFound=*/false, &stashElement))
			{
				UGCProjectSeries *pOldSeries = stashElementGetPointer(stashElement);

				StructDestroySafe(parse_UGCProjectSeries, &pOldSeries);

				stashElementSetPointer(stashElement, pNewSeries);
			}
		}
		FOR_EACH_END;
	}
}

#include "gslUGCSearchCache_c_ast.c"
