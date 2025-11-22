// Common includes
#include "AutoStartupSupport.h"
#include "cmdparse.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "GlobalTypes.h"
#include "hoglib.h"
#include "logging.h"
#include "objContainerIO.h"
#include "sysutil.h"
#include "textparser.h"
#include "utilitiesLib.h"
#include "wininclude.h"

// Shard Merge specific includes
#include "AccountStubMerging.h"
#include "CurrencyExchangeMerging.h"
#include "EntitySharedBankMerging.h"
#include "ShardMerge.h"

// AutoGen
#include "autogen/objContainerIO_h_ast.h"
#include "autogen/ShardMerge_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "ShardMerge"););

AUTO_ENUM;
typedef enum ShardMergeActionType
{
	ShardMergeActionType_Ignore = 0,
	ShardMergeActionType_KeepOne, // For each id, keep the most recent version
	ShardMergeActionType_Merge, // Combine the data from all version of a container. This needs a special callback function per type to know how things should be merged
} ShardMergeActionType;

AUTO_STRUCT;
typedef struct ShardMergeTypeConfig
{
	GlobalType containerType;		AST(KEY)
	ShardMergeActionType actionType; 
} ShardMergeTypeConfig;

AUTO_STRUCT;
typedef struct ShardMergeConfig
{
	ShardMergeTypeConfig **ppTypeConfigs;
	char pSourceDirectory[MAX_PATH];
	char pDestinationDirectory[MAX_PATH];
} ShardMergeConfig;

static char gpShardMergeConfigFile[MAX_PATH] = "./ShardMergeConfig.txt";
AUTO_CMD_STRING(gpShardMergeConfigFile, ShardMergeConfigFile);

ShardMergeConfig gpShardMergeConfig = {0};

bool LoadShardMergeConfig(void)
{
	bool success = false;

	if(strlen(gpShardMergeConfigFile) == 0)
	{
		printf("Empty config file name\n");
		return success;
	}

	if(!fileExists(gpShardMergeConfigFile))
	{
		printf("Config file %s does not exist\n", gpShardMergeConfigFile);
		return success;
	}

	success = ParserReadTextFile(gpShardMergeConfigFile, parse_ShardMergeConfig, &gpShardMergeConfig, 0);

	if(!success)
	{
		printf("Could not parse config file %s\n", gpShardMergeConfigFile);
	}

	if(!dirExists(gpShardMergeConfig.pSourceDirectory))
	{
		printf("Invalid source directory %s\n", gpShardMergeConfig.pSourceDirectory);
		success = false;
	}

	if(!dirExists(gpShardMergeConfig.pDestinationDirectory))
	{
		printf("Invalid destination directory %s\n", gpShardMergeConfig.pDestinationDirectory);
		success = false;
	}

	return success;
}

typedef struct ShardMergeHogFileData
{
	char filename[MAX_PATH];
	HogFile *hogFile;
} ShardMergeHogFileData;

typedef struct ShardMergeData
{
	ShardMergeHogFileData **snapshots;
	ShardMergeHogFileData **offlines;
} ShardMergeData;

static FileScanAction FindAllHogsToMergeCallback(char *dir, struct _finddata32_t* data, ShardMergeData *shardMergeData)
{
	const char *current = "";
	char fdir[MAX_PATH];
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
	{
		return retval;
	}

	strcpy(fdir, dir);

	strcat(fdir, "/");
	strcat(fdir, data->name);

	//Check for snapshots
	if(objStringIsSnapshotFilename(data->name))
	{
		if (objHogHasValidContainerSourceInfo(fdir))
		{
			ShardMergeHogFileData *fileData = callocStruct(ShardMergeHogFileData);
			strcpy(fileData->filename, fdir);
			eaPush(&shardMergeData->snapshots, fileData);
		}
	}
	else if(objStringIsOfflineFilename(data->name))
	{
		ShardMergeHogFileData *fileData = callocStruct(ShardMergeHogFileData);
		strcpy(fileData->filename, fdir);
		eaPush(&shardMergeData->offlines, fileData);
	}

	return retval;
}

static int sortShardMergeRequests(const ContainerLoadRequest **pRequest1, const ContainerLoadRequest **pRequest2)
{
	S32 diff = (*pRequest1)->containerType - (*pRequest2)->containerType;
	int stringCompareResult;
	if (diff)
		return diff;

	diff = (S32)((S64)(*pRequest1)->containerID - (S64)(*pRequest2)->containerID);
	if (diff)
		return diff;

	stringCompareResult = stricmp(hogFileGetArchiveFileName((*pRequest1)->the_hog), hogFileGetArchiveFileName((*pRequest2)->the_hog));
	if (stringCompareResult != 0)
		return stringCompareResult;

	if ((*pRequest1)->sequenceNumber > (*pRequest2)->sequenceNumber)
		return -1;
	else if ((*pRequest1)->sequenceNumber < (*pRequest2)->sequenceNumber)
		return 1;
	else
		return (*pRequest2)->deleted - (*pRequest1)->deleted;
}

// This returns the next index at which to resume walking
static int ShardMergeChooser_IgnoreType(ContainerLoadRequest **ppRequests, int start)
{
	int i;
	ContainerLoadRequest *pRequest = ppRequests[start];
	GlobalType currentType = pRequest->containerType;
	ContainerID lastID = 0;
	loadstart_printf("\t\tIgnoring all containers for type %s...\n", GlobalTypeToName(ppRequests[start]->containerType));
	for(i = start ; i < eaSize(&ppRequests); ++i)
	{
		pRequest = ppRequests[i];
		if(pRequest->containerType != currentType)
			break;
	}
	loadend_printf("\t\tdone ignoring.");
	return i;
}

// Does not compute headers, so do not use for EntityPlayers
Container *ShardMerge_LoadContainerFromDisk(ContainerLoadRequest *request)
{
	ContainerSchema *schema = objFindContainerSchema(request->containerType);
	Container *newContainer;
	assertmsgf(schema, "Unable to find schema for type %s.", GlobalTypeToName(request->containerType));
	newContainer = objCreateContainer(schema);
	objLoadFileData(request);
	newContainer->containerID = request->containerID;

	newContainer->fileData = request->fileData;
	request->fileData = NULL; // now owned by container
	newContainer->fileSize = request->fileSize;
	newContainer->fileSizeHint = newContainer->fileSize;
	newContainer->bytesCompressed = request->bytesCompressed;
	newContainer->checksum = hogFileGetFileChecksum(request->the_hog, request->hog_index);
	newContainer->oldFileNoDevassert = hogFileGetFileTimestamp(request->the_hog, request->hog_index) < 333961199;

	objUnpackContainer(schema, newContainer, true, false, false);

	return newContainer;
}

static void MergeContainers(HogFile *outputSnapshot, ContainerLoadRequest ***pppContainersToMerge, ContainerLoadRequest ***pppUpdateContainers)
{
	Container *accumulator = NULL;
	bool writeSuccess = false;
	U64 sequenceNumber = 0;

	assert(pppContainersToMerge);
	if(!eaSize(pppContainersToMerge))
		return;

	if(eaSize(pppContainersToMerge) == 1)
	{
		// No merging needs to happen.
		// Just add the one container to the update list for copying
		eaPush(pppUpdateContainers, (*pppContainersToMerge)[0]);
		return;
	}

	// Walk the containers in pppContainersToMerge
	//		- Load them into RAM
	//		- Accumulate all data into one and then write directly to the output.
	FOR_EACH_IN_EARRAY_FORWARDS(*pppContainersToMerge, ContainerLoadRequest, request);
	{
		Container *newContainer = ShardMerge_LoadContainerFromDisk(request);
		if(!accumulator)
		{
			accumulator = newContainer;
			sequenceNumber = request->sequenceNumber;
			// We need to have a different merge per type so we know the internals
			switch((*pppContainersToMerge)[0]->containerType)
			{
				case GLOBALTYPE_ACCOUNTSTUB:
				{
					InitializeAccountStub(accumulator->containerData);
				}
				break;
				case GLOBALTYPE_CURRENCYEXCHANGE:
				{
					InitializeCurrencyExchangeContainer(accumulator->containerData);
				}
				break;
				case GLOBALTYPE_ENTITYSHAREDBANK:
				{
					InitializeEntitySharedBank(accumulator->containerData);
				}
				break;
				default:
					assert(0); // Not implemented
			}
			continue;
		}

		// We need to have a different merge per type so we know the internals
		switch((*pppContainersToMerge)[0]->containerType)
		{
			case GLOBALTYPE_ACCOUNTSTUB:
			{
				MergeTwoAccountStubs(accumulator->containerData, newContainer->containerData);
			}
			break;
			case GLOBALTYPE_CURRENCYEXCHANGE:
			{
				MergeTwoCurrencyExchangeContainers(accumulator->containerData, newContainer->containerData);
			}
			break;
			case GLOBALTYPE_ENTITYSHAREDBANK:
			{
				MergeTwoEntitySharedBanks(accumulator->containerData, newContainer->containerData);
			}
			break;
			default:
				assert(0); // Not implemented
		}
		
		objDestroyContainer(newContainer);
	}
	FOR_EACH_END;

	objWriteContainerToSnapshot(outputSnapshot, accumulator, sequenceNumber);
	objDestroyContainer(accumulator);
}

static int ShardMergeChooser_MergeType(HogFile *outputSnapshot, ContainerLoadRequest **ppRequests, int start, ContainerLoadRequest ***pppUpdateContainers)
{
	int i;
	int total;
	ContainerLoadRequest *pRequest = ppRequests[start];
	GlobalType currentType = pRequest->containerType;
	ContainerID lastID = 0;
	GlobalType lastDeleteType = 0;
	ContainerID lastDeleteID = 0;
	bool skipNext = false;
	ContainerLoadRequest **ppContainersToMerge = NULL;

	loadstart_printf("\t\tMerging type %s...\n", GlobalTypeToName(ppRequests[start]->containerType));

	// Finding the end so we can have progress reporting
	for(i = start; i < eaSize(&ppRequests) && ppRequests[i]->containerType == currentType; ++i);

	total = i - start;

	for(i = start ; i < eaSize(&ppRequests); ++i)
	{
		if((i - start) % 1000 == 0)
			printf("\r\t\tProcessing: %d/%d", i-start, total);

		pRequest = ppRequests[i];
		if(pRequest->containerType != currentType || pRequest->containerID != lastID)
		{
			MergeContainers(outputSnapshot, &ppContainersToMerge, pppUpdateContainers);
			eaClear(&ppContainersToMerge);
		}

		if(pRequest->containerType != currentType)
		{
			break;
		}

		// if pRequest->deleted && skipNext it means we have two delete markers in a row, which should not happen
		devassert(!(pRequest->deleted && skipNext));

		if(pRequest->deleted)
		{
			lastDeleteID = pRequest->containerID;
			skipNext = true;
			continue;
		}

		if(skipNext)
		{
			// Every delete marker should be followed by a full container with the same id
			devassert(lastDeleteID == pRequest->containerID);
			skipNext = false;
			continue;
		}

		lastID = pRequest->containerID;

		eaPush(&ppContainersToMerge, pRequest);
	}

	// Catch the case of reaching the end of ppRequests
	MergeContainers(outputSnapshot, &ppContainersToMerge, pppUpdateContainers);
	eaDestroy(&ppContainersToMerge);
	printf("\r\t\tFinished Processing: %d/%d\n", i-start, total);
	loadend_printf("\t\tdone merging.");
	return i;
}

// This returns the next index at which to resume walking
static int ShardMergeChooser_KeepOne(ContainerLoadRequest **ppRequests, int start, ContainerLoadRequest ***pppUpdateContainers)
{
	int i;
	ContainerLoadRequest *pRequest = ppRequests[start];
	GlobalType currentType = pRequest->containerType;
	ContainerID lastID = 0;
	GlobalType lastDeleteType = 0;
	ContainerID lastDeleteID = 0;

	loadstart_printf("\t\tChoosing one to keep for type %s...\n", GlobalTypeToName(ppRequests[start]->containerType));
	for(i = start ; i < eaSize(&ppRequests); ++i)
	{
		pRequest = ppRequests[i];
		if(pRequest->containerType != currentType)
			break;

		if(pRequest->containerID == lastID)
		{
			//fprintf(fileGetStderr(), "Ignoring container %d:%d version %d\n", pRequest->containerType, pRequest->containerID, pRequest->sequenceNumber);
			// We already loaded a newer one
			continue;
		}

		if (pRequest->deleted)
		{
			lastDeleteID = pRequest->containerID;
			// Skip deleted markers
			continue;
		}

		lastID = pRequest->containerID;

		if (lastDeleteID == pRequest->containerID)
		{
			pRequest->snapshotMarkDeleted = true;
		}
		eaPush(pppUpdateContainers, pRequest);
	}
	loadend_printf("\t\tdone.");

	return i;
}

void ShardMergeWriter_KeepOne(HogFile *outputSnapshot, ContainerLoadRequest *pRequest, ContainerLoadThreadState *threadState, bool offline)
{
	ContainerHogMarkLatest(outputSnapshot,
		pRequest->containerType,
		pRequest->containerID,
		pRequest->sequenceNumber);

	assert(pRequest->the_hog != outputSnapshot);
	ContainerHogCopyToSnapshot(outputSnapshot, threadState, pRequest, !offline);
	if(pRequest->snapshotMarkDeleted)
	{
		// If we found a delete marker, save that to the snapshot
		ContainerHogMarkDeleted(outputSnapshot, pRequest->containerType, pRequest->containerID, 1);
	}
}

void MergeShardHogFilesInternal(ShardMergeHogFileData **snapshots, HogFile* outputSnapshot, bool offline)
{
	IncrementalLoadState loadState = {0, 0, 0, NULL, NULL, GLOBALTYPE_NONE};
	ContainerLoadThreadState *threadState;
	// Do the actual merge
	int i;
	GlobalType lastType = 0;
	ContainerID lastID = 0;
	GlobalType lastDeleteType = 0;
	ContainerID lastDeleteID = 0;
	int numRemoved = 0;
	ContainerLoadRequest **ppUpdateContainers = NULL;
	int timer = timerAlloc();
	threadState = callocStruct(ContainerLoadThreadState);
	loadState.loadThreadState = threadState;

	loadstart_printf("\tGenerating request list...");
	for(i = 0; i < eaSize(&snapshots); ++i)
	{
		if(offline)
			AddOfflineRequestsToThreadState(snapshots[i]->filename, threadState);
		else
			AddRequestsToThreadState(snapshots[i]->filename, threadState);
	}
	loadend_printf("done");

	// Now that all of the files are in request array, sort then walk the array doing work
	// sort order:
	//	type
	//	id
	//	file
	//	sequence number
	//	deleted

	loadstart_printf("\tSorting...");
	eaQSort(threadState->ppRequests, sortShardMergeRequests);
	loadend_printf("done");

	// Read the first one. Based on its type, call a function to deal with that type of action. That
	// internal function will walk down the list of all requests with that container type. When it 
	// reaches the next type, we will go around the loop again. Incrementing will be taken care of 
	// inside the loop

	// This just figures out, for each type and id, which files to load.
	// Later, we are going to walk the list of files to load and act on them.
	// For KeepOne, the first loop will add a single file to the list and we will write that file to the output
	// For Merge, we will add all of the needed files (one per snapshot), to the update list, and the update process will 
	// load each one and do the actual combination.
	loadstart_printf("\tWalking the request list...\n");
	for (i = 0; i < eaSize(&threadState->ppRequests);)
	{
		ContainerLoadRequest *pRequest = threadState->ppRequests[i];
		ShardMergeTypeConfig *typeConfig = eaIndexedGetUsingInt(&gpShardMergeConfig.ppTypeConfigs, pRequest->containerType);
		if(typeConfig)
		{
			switch(typeConfig->actionType)
			{
				case ShardMergeActionType_KeepOne:
					i = ShardMergeChooser_KeepOne(threadState->ppRequests, i, &ppUpdateContainers);
					break;
				case ShardMergeActionType_Ignore:
					i = ShardMergeChooser_IgnoreType(threadState->ppRequests, i);
					break;
				case ShardMergeActionType_Merge:
					// This will do some writing to the output file in the case of merges
					i = ShardMergeChooser_MergeType(outputSnapshot, threadState->ppRequests, i, &ppUpdateContainers);
					break;
				default:
					assertmsgf(0, "Invalid actionType specified in config file: %u", typeConfig->actionType);
			}
		}
		else
		{
			i = ShardMergeChooser_IgnoreType(threadState->ppRequests, i);
		}
	}
	loadend_printf("\tdone walking request list.");

	lastType = 0;
	lastID = 0;

	CONTAINER_LOGPRINT("Found %d unique, updated containers, %d of which are to be deleted\n", eaSize(&ppUpdateContainers), numRemoved);

	numRemoved = 0;

	loadstart_printf("\tSorting copy requests by file location... ");
	eaQSort(ppUpdateContainers, sortContainerLoadRequestsByFile);
	loadend_printf("done.");

	loadstart_printf("\tPerforming copies...\n\n");
	// Write out the actual file data
	for (i = 0; i < eaSize(&ppUpdateContainers); i++)
	{
		ContainerLoadRequest *pRequest = ppUpdateContainers[i];

		ShardMergeTypeConfig *typeConfig = eaIndexedGetUsingInt(&gpShardMergeConfig.ppTypeConfigs, pRequest->containerType);
		if(typeConfig)
		{
			switch(typeConfig->actionType)
			{
				case ShardMergeActionType_KeepOne:
				case ShardMergeActionType_Merge:
					// There might be some merge type files to write out here if there was only one for a particular id
					ShardMergeWriter_KeepOne(outputSnapshot, pRequest, threadState, offline);
					break;
				default:
					assert(0); // Not implemented
			}
		}
		//fprintf(fileGetStderr(), "Writing container %d:%d version %d\n", pRequest->containerType, pRequest->containerID, pRequest->sequenceNumber);

		// MB/s display
		if (timerElapsed(timer) > 1 || i == eaSize(&ppUpdateContainers)-1)
		{
			F32 write, read;
			F32 write_avg, read_avg;
			timerStart(timer);
			hogGetGlobalStats(&read, &write, &read_avg, &write_avg);
			fprintf(fileGetStderr(), "%d/%d   Disk I/O: Read: %1.1fKB/s (avg:%1.1fKB/s)  Write:%1.1fKB/s (avg:%1.1fKB/s)      \r",
				i+1, eaSize(&ppUpdateContainers),
				read/1024.f, read_avg/1024.f, write/1024.f, write_avg/1024.f);
		}
	}
	loadend_printf("\n\tdone performing copies.");
}

static void MergeShardHogFiles(ShardMergeConfig *config, ShardMergeData *shardMergeData)
{
	HogFile *outputSnapshot = NULL;
	HogFile *outputOffline = NULL;
	char outputSnapshotName[MAX_PATH];
	char outputOfflineName[MAX_PATH];
	char *infoString = NULL;
	//This should block all container persist updates until we're done merging.
	EnterCriticalSection(&gContainerSource.hog_access);

	loadstart_printf("Finding hog files in %s...", config->pSourceDirectory);
	fileScanAllDataDirs(config->pSourceDirectory, FindAllHogsToMergeCallback, shardMergeData);
	loadend_printf("done.");

	// Set up a load state

	if(eaSize(&shardMergeData->snapshots))
	{
		loadstart_printf("Merging snapshots...\n");
		snprintf(outputSnapshotName, MAX_PATH, "%s\\outputSnapshot.hogg", config->pDestinationDirectory);
		outputSnapshot = hogFileReadEx(outputSnapshotName,NULL,PIGERR_ASSERT,NULL,HOG_MUST_BE_WRITABLE|HOG_NO_STRING_CACHE|HOG_APPEND_ONLY, (32*1024*1024));
		MergeShardHogFilesInternal(shardMergeData->snapshots, outputSnapshot, false);
		loadend_printf("done merging snapshots.");
	}

	loadstart_printf("Writing out container info file... ");
	// Write out the new container info
	ParserWriteText(&infoString,parse_ContainerRepositoryInfo, gContainerSource.sourceInfo, 0, 0, 0);
	hogFileModifyUpdateNamed(outputSnapshot, CONTAINER_INFO_FILE, infoString, estrLength(&infoString)+1, objContainerGetSystemTimestamp(), EStringFreeCallback);
	loadend_printf("done");

	loadstart_printf("Flushing output snapshot... ");
	hogFileModifyFlush(outputSnapshot);
	loadend_printf("done");

	if(eaSize(&shardMergeData->offlines))
	{
		// Do the actual merge
		loadstart_printf("Merging offline hogs...\n");
		snprintf(outputOfflineName, MAX_PATH, "%s\\outputOffline.hogg", config->pDestinationDirectory);
		outputOffline = hogFileReadEx(outputOfflineName,NULL,PIGERR_ASSERT,NULL,HOG_MUST_BE_WRITABLE|HOG_NO_STRING_CACHE|HOG_APPEND_ONLY, (32*1024*1024));
		MergeShardHogFilesInternal(shardMergeData->offlines, outputOffline, true);
		loadend_printf("done merging offline hogs.");
	}

	loadstart_printf("Flushing output offline hog... ");
	hogFileModifyFlush(outputOffline);
	loadend_printf("done");
	LeaveCriticalSection(&gContainerSource.hog_access);
}

void PerformShardMerge(ShardMergeConfig *config)
{
	ShardMergeData shardMergeData = {0};
	// Merge snapshots
	MergeShardHogFiles(config, &shardMergeData);
}

int main(int argc, char **argv)
{
	EXCEPTION_HANDLER_BEGIN;
	WAIT_FOR_DEBUGGER;
	DO_AUTO_RUNS;

#if PARALLEL_MODE
	timerSetMaxTimers(32768);
#endif

	SetAppGlobalType(GLOBALTYPE_SHARDMERGE);

	gimmeDLLDisable(1);
	FolderCacheChooseMode();
	cmdParseCommandLine(argc, argv);
	utilitiesLibStartup();

	srand(clock());

	AutoStartup_SetTaskIsOn("ShardMerge", 1);
	DoAutoStartup();
	loadstart_printf("Loading configuration... ");
	if(LoadShardMergeConfig())
	{
		char dummyHogFile[MAX_PATH];
		snprintf(dummyHogFile, MAX_PATH, "%s\\db.hogg", gpShardMergeConfig.pSourceDirectory);
		loadend_printf("done.");
		objSetContainerSourceToHogFile(dummyHogFile, 1, NULL, NULL);
		PerformShardMerge(&gpShardMergeConfig);
	}
	else
	{
		loadend_printf("Configuration not loaded. Exiting.");
	}

	EXCEPTION_HANDLER_END;

	printf("Done performing shard merge. Press any key to exit.");
	getchFast();
	return 0;
}

// GameServer dependencies
/*ASTRT_DEPS(GameServerNoData,
									Combat,
									EntityCostumes,
									Tray,
									Items,
									ItemCostumeClones,
									ItemGen,
									ItemVars,
									Critters,
									Missions,
									MissionSets,
									AS_GameProgression,
									Contacts,
									EncounterLayerInit,
									Encounters,
									AI,
									AICivilian,
									InventoryBags,
									Stores,
									PowerStores,
									Officers,
									PetContactLists,
									PetRestrictionsValidate,
									ContactAudioPhrases,
									ContactConfig,
									AlgoPet,
									AS_ControlSchemes,
									HUDOptionsDefaults,
									RegionRules,
									Guilds,
									LayerFSMInit,
									PlayerFSMStartup,
									AS_TextFilter,
									Emotes,
									Chat,
									PetCommandConfig,
									Species,
									PVP,
									PVPGame,
									Queues,
									AttribStatsPresets,
									PlayerStats,
									ItemTagInfo,
									ProjectGameServerConfig,
									ChoiceTable,
									DoorTransitionSequence,
									Interaction,
									GlobalExpressions,
									ShardVariables,
									DiaryDefs,
									AS_ActivityLogConfig,
									GameAccount,
									GameAccountNumericPurchase,
									RewardsServer,
									RequiredPowersAtCreation,
									WarpRestrictions,
									TimeControlConfig,
									WorldLibZone,
									PlayerDifficulty,
									GAMESPECIFIC,
									AS_GuildRecruitParam,
									MicroTransactions,
									Bulletins,
									LogoffConfig,
									UGCServer,
									LeaderboardServer,
									AS_GuildStats,
									AS_GuildThemes,
									GamePermissions,
									GslAuctionLoadConfig,
									MissionConfig,
									MissionWarpCostsServer,
									ItemAssignments,
									TeamTransferConfig,
									CharacterCombat,
									GlobalWorldVars,
									GameStringFormat,
									Activities,
                                    NumericConversion,
                                    CurrencyExchangeConfig,
									UGC,
                                    UGCTips,
									GameContentNodes,
									SuggestedContent,
									MovementManagerConfig,
									NotifySettings,
									MapNotificationsLoad,
									ItemUpgrade,
									AutoStart_AuctionBroker,
									SuperCritterPet,
                                    GroupProjects,
									UGCAchievements,
									AS_ExtraHeaderDataConfig,
									GroupProjectLevelTreeDef,
									AS_ShardIntervalTiming
									AS_ItemSalvageDef
									AutoStart_ZoneRewards,
									);
									*/

AUTO_STARTUP(AnimLists);
void aslLoginServerFakeAnimListStartup(void)
{
	//fake auto startup here since inventory does validation on anim lists
}

AUTO_STARTUP(ShardMerge) ASTRT_DEPS(Combat,EntityCostumes,Items,ItemCostumeClones,ItemGen,ItemVars,InventoryBags,Stores,PowerStores,AlgoPet,RegionRules,Species,ItemTagInfo,ItemAssignments,ItemUpgrade);
void ShardMergeStartup(void)
{
}

#include "autogen/ShardMerge_c_ast.c"
