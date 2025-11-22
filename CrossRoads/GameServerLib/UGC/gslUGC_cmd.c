//// Container for all Client -> Server communication for UGC.
////
//// Let's have a real API here!
#include "gslUGC_cmd.h"

#include "Character.h"
#include "CharacterRespecServer.h"
#include "CostumeCommonEntity.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "GlobalEnums.h"
#include "JobManagerSupport.h"
#include "LoggedTransactions.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "ReferenceSystem_Internal.h"
#include "Regex.h"
#include "RemoteCommandGroup.h"
#include "ResourceDBUtils.h"
#include "ResourceInfo.h"
#include "ResourceSearch.h"
#include "ResourceSystem_internal.h"
#include "Reward.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "TextFilter.h"
#include "TokenStore.h"
#include "UGCAchievements.h"
#include "UGCAchievements_h_ast.h"
#include "UGCCommon.h"
#include "UGCError.h"
#include "UGCProjectCommon.h"
#include "UGCProjectUtils.h"
#include "WorldGrid.h"
#include "accountCommon.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "alerts.h"
#include "cmdServerCharacter.h"
#include "cmdServerCombat.h"
#include "continuousBuilderSupport.h"
#include "crypt.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "file.h"
#include "gslAccountProxy.h"
#include "gslContact.h"
#include "gslEditor.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslGameAccountData.h"
#include "gslInteraction.h"
#include "gslLogSettings.h"
#include "gslMechanics.h"
#include "gslMission_transact.h"
#include "gslPetCommand.h"
#include "gslPowerTransactions.h"
#include "gslQueue.h"
#include "gslSpawnPoint.h"
#include "gslUGC.h"
#include "gslUGCSearchCache.h"
#include "gslUGCTransactions.h"
#include "itemServer.h"
#include "logging.h"
#include "mission_common.h"
#include "rand.h"
#include "rewardCommon.h"
#include "stringUtil.h"
#include "sysutil.h"
#include "utilitiesLib.h"

#include "../../crossroads/appServerLib/UGCDataManager/aslUGCDataManager.h"
#include "../../crossroads/appServerLib/pub/aslJobManagerPub.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/UGCProjectCommon_h_ast.h"

#include "AutoGen/gslUGC_cmd_c_ast.h"

AUTO_STRUCT;
typedef struct UGCAccountIDLookupData
{
	char* pchAccountName;
	GlobalType eEntType;
	ContainerID iEntID;
} UGCAccountIDLookupData;
extern ParseTable parse_UGCAccountIDLookupData[];
#define TYPE_parse_UGCAccountIDLookupData UGCAccountIDLookupData

static bool s_bUGCAccountSubscriptions = false;
AUTO_CMD_INT(s_bUGCAccountSubscriptions, UGCAccountSubscriptions) ACMD_AUTO_SETTING(UGC, GAMESERVER);

void QueryUGCProjectStatus(Entity *pEntity);
void gslUGC_Search(Entity *pEntity, UGCProjectSearchInfo *pSearch);

//set by map manager when it launches this gameserver if the player is banned from publishing
static bool sbNoPublishing = false;
AUTO_CMD_INT(sbNoPublishing, NoPublishing) ACMD_COMMANDLINE;

static U32 s_ugcPlayDialogTreeID;
static int s_ugcPlayDialogTreePromptID;

bool gslUGC_PlayerIsReviewer(SA_PARAM_NN_VALID Entity *pEntity)
{
	GameAccountData* pData = entity_GetGameAccount(pEntity);
	if (gConf.bDontAllowGADModification)
		return gad_GetAccountValueInt(pData, GetAccountUgcReviewerKey());
	else
		return gad_GetAttribInt(pData, GetAccountUgcReviewerKey());
}

//returns whether the user has requested to search for UGC since this was last called.
//polled each frame by 
AUTO_EXPR_FUNC(clickable) ACMD_NAME(ShowUGCFinder);
void gslUGC_ShowUGCFinderOnClient(ExprContext *pContext){
	Entity *pInteractor = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ClientCmd_gclUGC_ShowUGCFinder(pInteractor);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGCSearch_Return(UGCProjectSearchInfo* pSearch)
{
	if(!pSearch->pUGCSearchResult)
	{
		pSearch->pUGCSearchResult = StructCreate(parse_UGCSearchResult);
		SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pSearch->pUGCSearchResult->hErrorMessage);
	}

	ugcSearchCacheReceive(pSearch);
}

static void CollectSubscribedToAuthors(ContainerID **peaiAuthorsOut, UGCPlayer *ugcPlayer)
{
	if(ugcPlayer && ugcPlayer->pSubscription)
	{
		int it;
		for(it = 0; it != eaSize(&ugcPlayer->pSubscription->eaAuthors); ++it)
			eaiPush(peaiAuthorsOut, ugcPlayer->pSubscription->eaAuthors[it]->authorID);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_Search(Entity *pEntity, UGCProjectSearchInfo *pSearch)
{
	if(!gslUGC_PlayingIsEnabled())
	{
		pSearch->pUGCSearchResult = StructCreate(parse_UGCSearchResult); // this is not leaked because it is tagging along an input parameter which textparser will destroy
		SET_HANDLE_FROM_STRING("Message", "UGCSearchError_PlayingDisabled", pSearch->pUGCSearchResult->hErrorMessage);
		ClientCmd_gclUGC_ReceiveSearchResult(pEntity, pSearch->pUGCSearchResult);
		return;
	}

	UTF8NormalizeAllStringsInStruct( parse_UGCProjectSearchInfo, pSearch );

	pSearch->entContainerID = entGetContainerID(pEntity);
	pSearch->iOwnerAccountID = entGetAccountID(pEntity);
	if(0 == stricmp(GetProductName(), "StarTrek")) // Star Trek will always get AccessLevel features in their search if player has sufficient AccessLevel
		pSearch->iAccessLevel = entGetAccessLevel(pEntity);
	else // Neverwinter will only get AccessLevel features once the player opts in via a checkbox, which will set AccessLevel to non-zero. Need to not give it if they should not get it.
		pSearch->iAccessLevel = pSearch->iAccessLevel ? entGetAccessLevel(pEntity) : 0;
	pSearch->pchPlayerAllegiance = allocAddString(REF_STRING_FROM_HANDLE(pEntity->hAllegiance));
	pSearch->iPlayerLevel = entity_GetSavedExpLevel(pEntity);
	pSearch->pcShardName = StructAllocString(GetShardNameFromShardInfoString());

	if(pSearch->eSpecialType == SPECIALSEARCH_SUBCRIBED && pEntity->pPlayer && entGetUGCAccount(pEntity))
	{
		UGCAccount *ugcAccount = entGetUGCAccount(pEntity);
		if(ugcAccount)
		{
			pSearch->pSubscription = StructCreate(parse_UGCSubscriptionSearchInfo);

			if(s_bUGCAccountSubscriptions)
			{
				FOR_EACH_IN_EARRAY(ugcAccount->eaPlayers, UGCPlayer, pUGCPlayer)
				{
					CollectSubscribedToAuthors(&pSearch->pSubscription->eaiAuthors, pUGCPlayer);
				}
				FOR_EACH_END;
			}
			else
			{
				UGCPlayer *pUGCPlayer = eaIndexedGetUsingInt(&ugcAccount->eaPlayers, entGetContainerID(pEntity));
				CollectSubscribedToAuthors(&pSearch->pSubscription->eaiAuthors, pUGCPlayer);
			}
		}
	}

	ugcSearchCacheRequest(pSearch);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_CacheSearchByID_Return(UGCProjectList *pResult, ContainerID entContainerID, bool bCache)
{
	if(bCache)
		ugcSearchCacheReceiveHeaders(pResult);

	if(entContainerID)
	{
		Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
		if(!pEntity)
			return;

		ClientCmd_gclUGC_CacheReceiveSearchResult(pEntity, pResult);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_CacheSearchByID(Entity *pEntity, UGCIDList* pIDs)
{
	UTF8NormalizeAllStringsInStruct( parse_UGCIDList, pIDs );

	ugcSearchCacheRequestHeaders(pEntity, pIDs);

	if(eaiSize(&pIDs->eaProjectIDs) || eaiSize(&pIDs->eaProjectSeriesIDs))
	{
		UGCIntershardData ugcIntershardData;

		StructInit(parse_UGCIntershardData, &ugcIntershardData);

		ugcIntershardData.pcShardName = GetShardNameFromShardInfoString();
		ugcIntershardData.entContainerID = entGetContainerID(pEntity);

		RemoteCommand_Intershard_aslUGCSearchManager_SearchByID(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM, pIDs, &ugcIntershardData);
	}
}

static void UGCProjectSave_CB(TransactionReturnVal *pReturn, UserData rawEntRef)
{
	Entity *pEntity = entFromEntityRefAnyPartition((EntityRef)(intptr_t)rawEntRef);
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		printf("UGC project saved to ObjectDB.\n");
		if (pEntity)
		{
			ClientCmd_UGCEditorUpdateSaveStatus(pEntity, true, "");
			QueryUGCProjectStatus(pEntity); // Update save time, etc.
		}
	} 
	else 
	{
		printf("UGC project save failed: %s", GetTransactionFailureString(pReturn));
		if (pEntity)
			ClientCmd_UGCEditorUpdateSaveStatus(pEntity, false, GetTransactionFailureString(pReturn));
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void SaveUGCProject(Entity *pEntity, UGCProjectData *data)
{
	InfoForUGCProjectSaveOrPublish infoForSave = {0};
	ContainerID projectID;
	char *pcError = NULL;

	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, data );

	if (!data)
	{
		AssertOrAlert("UGC_NO_PROJECT", "Can't save project when there is no UGCProjectData");
		ClientCmd_UGCEditorUpdateSaveStatus(pEntity, false, "There is no project data");
		return;
	}

	gslUGC_SetUGCProjectCopy(data);

	if (!gslUGC_DoSave(data, &infoForSave, &projectID, false, &pcError, __FUNCTION__))
	{
		ClientCmd_UGCEditorUpdateSaveStatus(pEntity, false, pcError); 
		estrDestroy(&pcError);
		return;
	}

	AutoTrans_trSaveUgcProject(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCProjectSave_CB, (void *)(intptr_t)entGetRef(pEntity)), GLOBALTYPE_GAMESERVER,
		GLOBALTYPE_UGCPROJECT, projectID,
		&infoForSave,
		"SaveUGCProject",
		pEntity->pPlayer->publicAccountName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void UpdateUGCProjectServerCopy(Entity *pEntity, UGCProjectData *data)
{
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, data );
	gslUGC_SetUGCProjectCopy(data);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void DeleteAutosaveUGCProject(Entity *pEntity, const char *ns_name_unnormalized, int type)
{
	char filename[MAX_PATH];
	UGCProjectAutosaveData pWriteData = { 0 };
	bool succeeded;
	char *pcError = NULL;
	char* ns_name = NULL;
	UTF8NormalizeString( ns_name_unnormalized, &ns_name );

	// TomY TODO - validate namespace

	TellControllerWeMayBeStallyForNSeconds(giUGCSaveStallySeconds, "UGCAutosaveDelete");

	// Write autosave file
	sprintf(filename, "ns/%s/autosave/autosave.ugcproject", ns_name);
	succeeded = ParserWriteTextFile(filename, parse_UGCProjectAutosaveData, &pWriteData, 0, 0);

	if (!gslUGC_UploadAutosave(ns_name, &pcError))
	{
		AssertOrAlert("UGC_AUTOSAVE_DELETE_FAILED", "Autosave delete failed. May be a problem with ugcmaster: %s", pcError);
	}

	// Clear files from disk
	gslUGC_DeleteNamespaceDataFiles(ns_name);

	ClientCmd_UGCEditorAutosaveDeletionCompleted(pEntity, type);

	estrDestroy( &ns_name );
}

void SaveAndPublishSuccess( Entity* pEntity )
{
	log_printf( LOG_UGC, "Publish: Success!" );
	if( pEntity ) {
		ClientCmd_UGCEditorUpdatePublishStatus( pEntity, true, "" );
	}
	if(   g_UGCPublishAndQuit && g_strUGCPublishAndQuitNotityEntityShardName
		  && g_UGCPublishAndQuitNotityEntityContainerID ) {
		UGCShardReturnAndErrorString ret = { 0 };
		ret.bSucceeded = true;
		estrConcatf( &ret.estrDetails, "Publish: Success!" );
		RemoteCommand_Intershard_UGCFeaturedCopyProject_Return( g_strUGCPublishAndQuitNotityEntityShardName, GLOBALTYPE_ENTITYPLAYER, g_UGCPublishAndQuitNotityEntityContainerID, g_UGCPublishAndQuitNotityEntityContainerID, &ret );
		StructDeInit( parse_UGCShardReturnAndErrorString, &ret );
	}
}

void SaveAndPublishFail( Entity* pEntity, const char* strReason, bool alert )
{
	log_printf( LOG_UGC, "Publish: Publish failed because %s", strReason );
	
	if( g_isContinuousBuilder ) {
		assertmsgf( 0, "Can't Publish: %s", strReason );
	}
	if( pEntity ) {
		ClientCmd_UGCEditorUpdatePublishStatus( pEntity, false, strReason );
	}
	if(   g_UGCPublishAndQuit && g_strUGCPublishAndQuitNotityEntityShardName
		  && g_UGCPublishAndQuitNotityEntityContainerID ) {
		UGCShardReturnAndErrorString ret = { 0 };
		ret.bSucceeded = false;
		estrConcatf( &ret.estrDetails, "Can't Publish: %s", strReason );
		RemoteCommand_Intershard_UGCFeaturedCopyProject_Return( g_strUGCPublishAndQuitNotityEntityShardName, GLOBALTYPE_ENTITYPLAYER, g_UGCPublishAndQuitNotityEntityContainerID, g_UGCPublishAndQuitNotityEntityContainerID, &ret );
		StructDeInit( parse_UGCShardReturnAndErrorString, &ret );
	}

	if( alert ) {
		AssertOrAlertWarning( "UGC_PUBLISH_FAILED", "Publish failed because %s", strReason );
	}
}

static void UGCProjectPublish_CB(TransactionReturnVal *pReturn, InfoForUGCProjectSaveOrPublish *pInfoForPublish)
{
	Entity *pEntity = pInfoForPublish->entContainerID ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pInfoForPublish->entContainerID) : NULL;
	gGSLState.bAtomicPartsOfUGCPublishHappening = false;
	
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		gslUGC_DoProjectPublishPostTransaction(pEntity, pInfoForPublish);
	}
	else 
	{
		char *pFailureString = GetTransactionFailureString(pReturn);

		StructDestroy(parse_InfoForUGCProjectSaveOrPublish, pInfoForPublish);
		SaveAndPublishFail( pEntity, pFailureString, !strstri( pFailureString, PUBLISH_FAIL_NOALERT_STRING ));
	}
}

static void DoSaveAndPublishUGCProject(Entity *pEntity, UGCProjectData *data)
{
	devassert(data);

	if(data)
	{
		InfoForUGCProjectSaveOrPublish *pInfoForPublish = NULL;
		ContainerID projectID;

		TellControllerWeMayBeStallyForNSeconds(giUGCPublishStallySeconds, "UGCPublish");
		gslUGC_AddTriviaData( data );

		gslUGC_SetUGCProjectCopy(data);

		// Do validate of the project
		{
			UGCRuntimeStatus* status = StructCreate( parse_UGCRuntimeStatus );

			ugcSetStageAndAdd( status, "UGC Validate" );
			ugcValidateProject( data );

			if( ugcValidateErrorfIfStatusHasErrors( status ))
			{
				StructDestroySafe( parse_UGCRuntimeStatus, &status );
				SaveAndPublishFail( pEntity, "Project has errors.", false );
				return;
			}

			StructDestroySafe( parse_UGCRuntimeStatus, &status );
		}

		pInfoForPublish = StructCreate(parse_InfoForUGCProjectSaveOrPublish);
		if (!gslUGC_DoSave(data, pInfoForPublish, &projectID, true, NULL, __FUNCTION__))
		{
			SaveAndPublishFail( pEntity, "gslUGC_DoSave failed", true );
			return;
		}

		// Rename this map and save the new resources
		if (!gslUGC_ForkNamespace(data, pInfoForPublish, true))
		{
			SaveAndPublishFail( pEntity, "gslUGC_ForkNamespace failed", true );
			return;
		}

		pInfoForPublish->pPublishJobName = strdup(GetUniqueJobGroupName("Publish UGC Map"));
		pInfoForPublish->entContainerID = pEntity ? entGetContainerID(pEntity) : 0;

		gGSLState.bAtomicPartsOfUGCPublishHappening = true;
		AutoTrans_trPublishUgcProject(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCProjectPublish_CB, pInfoForPublish), GLOBALTYPE_GAMESERVER,
			GLOBALTYPE_UGCPROJECT, projectID,
			pInfoForPublish,
			"PublishUGCProject",
			pEntity ? pEntity->pPlayer->publicAccountName : "");

		gslUGC_RemoveTriviaData();
	}
	else
	{
		SaveAndPublishFail( pEntity, "a NULL project got into DoSaveAndPublish", true );
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void SaveAndPublishUGCProject(Entity *pEntity, UGCProjectData *data)
{
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, data );
	FixupStructLeafFirst( parse_UGCProjectData, data, FIXUPTYPE_POST_TEXT_READ, NULL );

	log_printf( LOG_UGC, "Publish: Starting process for namespace %s",
				data ? ugcProjectDataGetNamespace( data ) : NULL );

	// bAtomicPartsOfUGCPublishHappening tracks if a UGC publish is in the background.  If
	// the server could ever be in its tick while a publish is happening you MUST set
	// bAtomicPartsOfUGCPublishHappening!
	if(gGSLState.bAtomicPartsOfUGCPublishHappening)
	{
		SaveAndPublishFail( pEntity, "Another publish just starting up", false );
		return;
	}

	if(sbNoPublishing)
	{
		SaveAndPublishFail( pEntity, "sbNoPublishing is set", false );
		return;
	}

	if (!data)
	{
		SaveAndPublishFail( pEntity, "no UGCProjectData", true );
		return;
	}

	if(ugcIsPublishEnabled())
	{
		DoSaveAndPublishUGCProject(pEntity, data);
	}
	else
	{
		SaveAndPublishFail( pEntity, "UGC publishing is disabled", false );
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_SetAuthorAllowsFeatured_Return(ContainerID entContainerID, bool bSuccess)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
		ClientCmd_ugcEditorAuthorAllowsFeaturedChanged(pEnt, bSuccess);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslUGC_SetAuthorAllowsFeatured( Entity* pEnt, bool bAuthorAllowsFeatured )
{
	UGCProject *pUGCProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	if(pUGCProject)
	{
		RemoteCommand_Intershard_aslUGCDataManager_SetAuthorAllowsFeatured(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			pUGCProject->id, bAuthorAllowsFeatured, GetShardNameFromShardInfoString(), entGetContainerID(pEnt));
	}
}

static void UGCProjectWithdraw_CB(TransactionReturnVal *pReturn, void *pUserData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		Entity *pEnt = entFromEntityRefAnyPartition((intptr_t)pUserData);
		QueryUGCProjectStatus(pEnt);

	}
	else
	{
	}

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void gslWithdrawUGCProject(Entity *pEnt)
{
	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	if (!pProject)
	{
		return;
	}

	if (!UGCProject_CanBeWithdrawn(CONTAINER_NOCONST(UGCProject, pProject)))
	{
		return;
	}

	AutoTrans_trCancelUGCPublish(NULL,
		GLOBALTYPE_GAMESERVER, true, GLOBALTYPE_UGCPROJECT, pProject->id, GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID, "gslWithdrawUGCProject");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void gslCancelUGCProjectPublish(Entity *pEnt)
{
	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	UGCProjectStatusQueryInfo *pInfo;
	const char* strJobName = NULL;
	bool bCurrentlyPublishing = false;

	if (!pProject) {
		return;
	}

	pInfo = UGCProject_GetStatusFromProject(pProject, &strJobName, &bCurrentlyPublishing);
	if (!pInfo) {
		return;
	}
	StructDestroySafe( parse_UGCProjectStatusQueryInfo, &pInfo );

	if (bCurrentlyPublishing && strJobName) {
		AutoTrans_trCancelUGCPublish(LoggedTransactions_CreateManagedReturnVal("CancelUGCPublish", UGCProjectWithdraw_CB, (void*)((intptr_t)(entGetRef(pEnt)))),
			GLOBALTYPE_GAMESERVER, false, GLOBALTYPE_UGCPROJECT, pProject->id, GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID, "gslCancelUGCProjectPublish");
	}
}

static bool sbCurrentlyQueryingJobGroupStatus = false;

static void UGCQueryStatusCB(TransactionReturnVal *returnVal, UGCProjectStatusQueryInfo *pInfo)
{
	JobManagerGroupResult *pResult = NULL;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_RequestJobGroupStatus(returnVal, &pResult);
	Entity *pEntity = entFromEntityRefAnyPartition(pInfo->iEntityRef);

	sbCurrentlyQueryingJobGroupStatus = false;

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		F32 percentage = 0;
		int i;
#ifdef UGC_QUERY_DEBUG_OUTPUT
		char *pStatusString = NULL;
		ParserWriteText(&pStatusString, parse_JobManagerGroupResult, pResult, 0, 0, 0);
		printf("Status:\n%s\n\n", pStatusString);
		estrDestroy(&pStatusString);
#endif
		pInfo->bCurrentlyPublishing = true;
		switch (pResult->eResult)
		{
		case JMR_ONGOING:
			for (i = 0; i < eaSize(&pResult->ppJobStatuses); i++)
				if (pResult->ppJobStatuses[i]->eResult == JMR_SUCCEEDED)
					percentage += 100.f / (F32)eaSize(&pResult->ppJobStatuses);
				else
					percentage += ((F32)pResult->ppJobStatuses[i]->iPercentDone) / (F32)eaSize(&pResult->ppJobStatuses);
			break;
		case JMR_SUCCEEDED:
			percentage = 100.f;
			break;
		case JMR_UNKNOWN:
		case JMR_FAILED:
			pInfo->bCurrentlyPublishing = false;
			break;
		case JMR_QUEUED:
			pInfo->iCurPlaceInQueue = pResult->iPlaceInQueue;
			percentage = 0.f;
			break;
		default:
			percentage = 0.f;
		};
		if (pEntity)
		{
			pInfo->fPublishPercentage = percentage;
			ClientCmd_UGCProjectJobStatus(pEntity, pInfo);
		}
		StructDestroy(parse_JobManagerGroupResult, pResult);
	}
	else if (pEntity)
	{
		// Assume there is some error
		ClientCmd_UGCProjectJobStatus(pEntity, pInfo);
	}
	StructDestroySafe( parse_UGCProjectStatusQueryInfo, &pInfo );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void QueryUGCProjectStatus(Entity *pEntity)
{
	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	UGCProjectStatusQueryInfo *pInfo;
	const char* pcJobName = NULL;
	bool bCurrentlyPublishing = false;

	if (!pProject)
		return;

	pInfo = UGCProject_GetStatusFromProject(pProject, &pcJobName, &bCurrentlyPublishing);

	if(!pInfo)
		return;


	if (bCurrentlyPublishing)
	{
		// Get publish status from job server
		pInfo->iEntityRef = entGetRef(pEntity);

		//want to make sure this remote command can't get spammed
		if (!sbCurrentlyQueryingJobGroupStatus)
		{
			sbCurrentlyQueryingJobGroupStatus = true;
			RemoteCommand_RequestJobGroupStatus(objCreateManagedReturnVal(UGCQueryStatusCB, pInfo), 
				GLOBALTYPE_JOBMANAGER, 0, pcJobName);
		}
		else
		{
			StructDestroySafe( parse_UGCProjectStatusQueryInfo, &pInfo );
		}
	}
	else
	{
		ClientCmd_UGCProjectJobStatus(pEntity, pInfo);
		StructDestroySafe( parse_UGCProjectStatusQueryInfo, &pInfo );
	}
}



////////////////////////////////////////////////////////////////////////////
///  Review request

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_RequestReviewsForPageReturn(ContainerID entContainerID, UGCDetails *pDetails, S32 iPageNumber)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
		ClientCmd_gclUGC_ReceiveReviewsForPage(pEnt, SAFE_MEMBER2(pDetails, pProject, id), SAFE_MEMBER2(pDetails, pSeries, id), iPageNumber,
			pDetails->pProject ? &pDetails->pProject->ugcReviews : &pDetails->pSeries->ugcReviews);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslUGC_RequestReviewsForPage(Entity *pEnt, ContainerID uProjectID, ContainerID uSeriesID, S32 iPageNumber)
{
	RemoteCommand_Intershard_aslUGCDataManager_RequestReviewsForPage(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(), entGetContainerID(pEnt), uProjectID, uSeriesID, iPageNumber);
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(9);
void TestReviewUGCProject(Entity *pEnt, float fRating, const char *pComment_unnormalized)
{
	UGCReviewData ugcReviewData = {0};
	const char **eaKeys = NULL;
	S32 *eaiValues = NULL;

	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	char* pComment;
	UTF8NormalizeString( pComment_unnormalized, &pComment );

	if (!pProject)
	{
		printf("no project to rate\n");
		estrDestroy( &pComment );
		return;
	}

	if (!pEnt)
	{
		printf("No ent to do rating with\n");
		estrDestroy( &pComment );
		return;
	}

	if (!gslUGC_ReviewingIsEnabled())
	{
		printf("Reviewing is currently disabled by AutoSetting\n");
		estrDestroy( &pComment );
		return;
	}

	UGCTagFillAllKeysAndValues(&eaKeys, &eaiValues);

	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		if(randomIntRange(0, 100) <= 20) // 20%
			eaiPushUnique(&ugcReviewData.eaiUGCTags, value);
	}
	FOR_EACH_END;

	ugcReviewData.fRating = fRating;
	ugcReviewData.pComment = pComment;

	RemoteCommand_Intershard_aslUGCDataManager_ReviewProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		/*entContainerID=*/0, // no return
		NULL, pProject->id, entGetAccountID(pEnt), SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), /*bBetaReviewing=*/false, &ugcReviewData);

	eaiDestroy(&ugcReviewData.eaiUGCTags);

	eaDestroy(&eaKeys);
	eaiDestroy(&eaiValues);
	estrDestroy( &pComment );
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(9);
void TestAddReviewsUGCProjectByID(Entity *pEnt, ContainerID iProjectID, S32 iNumReviews)
{
	S32 i;
	char pAccount[256];
	char pComment[256];

	UGCReviewData ugcReviewData = {0};

	const char **eaKeys = NULL;
	S32 *eaiValues = NULL;

	if (!pEnt)
	{
		printf("No ent to do rating with\n");
		return;
	}

	if (!gslUGC_ReviewingIsEnabled())
	{
		printf("Reviewing is currently disabled by AutoSetting\n");
		return;
	}

	UGCTagFillAllKeysAndValues(&eaKeys, &eaiValues);

	for (i = 1; i <= iNumReviews; i++)
	{
		F32 fRating = randomF32();

		sprintf(pAccount, "TestAccount%d", i);
		sprintf(pComment, "This is a review by account %s, who gave the project a rating of %f.", pAccount, fRating);

		if(randomIntRange(0, 100) <= 20) // 20%
		{
			FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
			{
				if(randomIntRange(0, 100) <= 20) // 20%
					eaiPushUnique(&ugcReviewData.eaiUGCTags, value);
			}
			FOR_EACH_END;
		}

		ugcReviewData.fRating = fRating;
		ugcReviewData.pComment = pComment;

		RemoteCommand_Intershard_aslUGCDataManager_ReviewProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			/*entContainerID=*/0, // no return
			NULL, iProjectID, i, pAccount, /*bBetaReviewing=*/false, &ugcReviewData);

		eaiDestroy(&ugcReviewData.eaiUGCTags);
	}

	eaDestroy(&eaKeys);
	eaiDestroy(&eaiValues);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_RequestDetails(Entity *pEntity, U32 iProjectID, U32 iSeriesID, S32 iRequesterID)
{
	gslUGC_DoRequestDetails(pEntity, iProjectID, iSeriesID, iRequesterID);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_ProjectReviewed_Return(ContainerID entContainerID, bool bSuccess, UGCShardReturnProjectReviewed *pUGCShardReturnProjectReviewed)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
	{
		char* estrMessage = NULL;
		const char* pchMessageKey;
		NotifyType eNotifyType;

		estrStackCreate(&estrMessage);

		if(bSuccess)
		{
			pchMessageKey = "UGC.ReviewFeedback";
			eNotifyType = kNotifyType_UGCFeedback;

			ClientCmd_gclUGC_ReviewsChanged(pEnt, pUGCShardReturnProjectReviewed->iProjectID, 0);
		}
		else
		{
			pchMessageKey = "UGC.ReviewError";
			eNotifyType = kNotifyType_UGCError;
		}
		entFormatGameMessageKey(pEnt, &estrMessage, pchMessageKey,
			STRFMT_STRING("ProjectName", pUGCShardReturnProjectReviewed->pchProjName),
			STRFMT_INT("ProjectID", pUGCShardReturnProjectReviewed->iProjectID),
			STRFMT_END);

		if(estrMessage && estrMessage[0])
			ClientCmd_NotifySend(pEnt, eNotifyType, estrMessage, NULL, NULL);

		estrDestroy(&estrMessage);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_AddReview(Entity *pEnt, U32 iProjectID, const char* pchProjName_unnormalized, UGCReviewData *pUGCReviewData)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	char* pchProjName = NULL;
	UTF8NormalizeString( pchProjName_unnormalized, &pchProjName );
	UTF8NormalizeAllStringsInStruct( parse_UGCReviewData, pUGCReviewData );

	if (gbEnableUgcDataLogging) 
	{
		entLog(LOG_UGC, pEnt, "UGCReviewAttempt", "Proj %s ID %d reviewed. Rating: %f. Comment: %s",
			pchProjName, iProjectID, pUGCReviewData->fRating, pUGCReviewData->pComment);
	}

	if(!iProjectID) {
		estrDestroy( &pchProjName );
		return;
	}

	// Check for the global AUTO_SETTING that disables reviewing.

	if(!gslUGC_ReviewingIsEnabled())
	{
		//  In actuality we should never get here since the client blocks
		//  the review UI buttons from activating when reviewing is
		//  disabled. However, there are theoretically
		//  cases where the setting has changed after the review dialog
		//  has appeared, in which case this serves as a second layer
		//  of prevention.

		if(pEnt)
		{
			char* estrMessage = NULL;
			const char* pchMessageKey;
			NotifyType eNotifyType;

			estrStackCreate(&estrMessage);

			pchMessageKey = "UGC.ReviewsDisabled";
			eNotifyType = kNotifyType_UGCError;

			entFormatGameMessageKey(pEnt, &estrMessage, pchMessageKey,
				STRFMT_END);

			if(estrMessage && estrMessage[0])
				ClientCmd_NotifySend(pEnt, eNotifyType, estrMessage, NULL, NULL);

			estrDestroy(&estrMessage);
		}
		estrDestroy( &pchProjName );
		return;
	}


	if((pInfo && pInfo->uLastMissionRatingRequestID == iProjectID)
		|| mission_FindMissionFromUGCProjectID(pInfo, iProjectID)
		|| entity_HasCompletedUGCProject(pEnt, iProjectID))
	{
		UGCShardReturnProjectReviewed *pUGCShardReturnProjectReviewed = StructCreate(parse_UGCShardReturnProjectReviewed);
		char* estrComment = NULL;

		if (gbEnableUgcDataLogging) 
		{
			entLog(LOG_UGC, pEnt, "UGCReviewSucceeded", "Transaction submitted");
		}

		pUGCShardReturnProjectReviewed->iProjectID = iProjectID;
		pUGCShardReturnProjectReviewed->pchProjName = StructAllocString(pchProjName);
		pUGCShardReturnProjectReviewed->entContainerID = entGetContainerID(pEnt);

		estrStackCreate(&estrComment);
		estrCopy2(&estrComment, pUGCReviewData->pComment);

		StructCopyString(&pUGCReviewData->pComment, estrComment);
		estrDestroy(&estrComment);

		RemoteCommand_Intershard_aslUGCDataManager_ReviewProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			entGetContainerID(pEnt),
			pUGCShardReturnProjectReviewed, iProjectID, entGetAccountID(pEnt), SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), pInfo->bLastMissionPlayingAsBetaReviewer, pUGCReviewData);

		StructDestroy(parse_UGCShardReturnProjectReviewed, pUGCShardReturnProjectReviewed);
	}

	estrDestroy( &pchProjName );
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_ProjectSeriesReviewed_Return(ContainerID entContainerID, bool bSuccess, UGCShardReturnProjectSeriesReviewed *pUGCShardReturnProjectSeriesReviewed)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
	{
		char* estrMessage = NULL;
		const char* pchMessageKey;
		NotifyType eNotifyType;

		estrStackCreate(&estrMessage);

		if(bSuccess)
		{
			pchMessageKey = "UGC.SeriesReviewFeedback";
			eNotifyType = kNotifyType_UGCFeedback;

			ClientCmd_gclUGC_ReviewsChanged(pEnt, 0, pUGCShardReturnProjectSeriesReviewed->iSeriesID);
		}
		else
		{
			pchMessageKey = "UGC.SeriesReviewError";
			eNotifyType = kNotifyType_UGCError;
		}
		entFormatGameMessageKey(pEnt, &estrMessage, pchMessageKey,
			STRFMT_STRING("SeriesName", pUGCShardReturnProjectSeriesReviewed->pchSeriesName),
			STRFMT_INT("SeriesID", pUGCShardReturnProjectSeriesReviewed->iSeriesID),
			STRFMT_END);

		if (estrMessage && estrMessage[0])
			ClientCmd_NotifySend(pEnt, eNotifyType, estrMessage, NULL, NULL);

		estrDestroy(&estrMessage);
	}
}

static bool gslUGC_CanReviewProjectID( Entity* pEnt, ContainerID projectID )
{
	MissionInfo* pInfo = mission_GetInfoFromPlayer( pEnt );

	if(   (pInfo && pInfo->uLastMissionRatingRequestID == projectID)
		|| mission_FindMissionFromUGCProjectID( pInfo, projectID )
		|| entity_HasCompletedUGCProject( pEnt, projectID )) {
			return true;
	} else {
		return false;
	}
}

static int gslUGC_CanReviewSeriesNodeCount( Entity* pEnt, CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes )
{
	int accum = 0;

	int it;
	for( it = 0; it != eaSize( &eaNodes ); ++it ) {
		const UGCProjectSeriesNode* node = eaNodes[ it ];
		if( node->iProjectID && gslUGC_CanReviewProjectID( pEnt, node->iProjectID )) {
			++accum;
		} else {
			accum += gslUGC_CanReviewSeriesNodeCount( pEnt, node->eaChildNodes );
		}
	}

	return accum;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_SearchBySeriesID_ForReviewing_Return(UGCProjectSeries *pUGCProjectSeries, UGCShardReturnProjectSeriesReviewed* pUGCShardReturnProjectSeriesReviewed)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pUGCShardReturnProjectSeriesReviewed->entContainerID);

	// User has disconnected, bail
	if(!pEnt)
		return;

	if(!pUGCProjectSeries) {
		char* estrMessage = NULL;
		entFormatGameMessageKey( pEnt, &estrMessage, "UGC.SeriesReviewError",
			STRFMT_STRING( "SeriesName", pUGCShardReturnProjectSeriesReviewed->pchSeriesName ),
			STRFMT_INT( "SeriesID", pUGCShardReturnProjectSeriesReviewed->iSeriesID ),
			STRFMT_END );
		if( !nullStr( estrMessage )) {
			ClientCmd_NotifySend( pEnt, kNotifyType_UGCError, estrMessage, NULL, NULL );
		}
		estrDestroy( &estrMessage );
	}
	else
	{
		// DO THE VALIDATION HERE
		const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pUGCProjectSeries);
		if( pVersion && gslUGC_CanReviewSeriesNodeCount( pEnt, pVersion->eaChildNodes ) >= 1 )
		{
			UGCReviewData ugcReviewData;

			char* estrComment = NULL;
			if (gbEnableUgcDataLogging)
			{
				entLog(LOG_UGC, pEnt, "UGCReviewSucceeded", "Transaction submitted");
			}

			estrStackCreate( &estrComment );
			estrCopy2( &estrComment, pUGCShardReturnProjectSeriesReviewed->pchComment );

			ugcReviewData.fRating = pUGCShardReturnProjectSeriesReviewed->fRating;
			ugcReviewData.eaiUGCTags = pUGCShardReturnProjectSeriesReviewed->eaiUGCTags;
			ugcReviewData.pComment = estrComment;

			RemoteCommand_Intershard_aslUGCDataManager_ReviewProjectSeries(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
				GetShardNameFromShardInfoString(),
				entGetContainerID(pEnt), pUGCShardReturnProjectSeriesReviewed,
				pUGCShardReturnProjectSeriesReviewed->iSeriesID, entGetAccountID(pEnt), SAFE_MEMBER2( pEnt, pPlayer, publicAccountName ), &ugcReviewData);

			estrDestroy( &estrComment );
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_AddReviewSeries( Entity *pEntity, U32 iSeriesID, const char* pchSeriesName_unnormalized, UGCReviewData *pUGCReviewData)
{
	char* pchSeriesName = NULL;
	UTF8NormalizeString( pchSeriesName_unnormalized, &pchSeriesName );
	UTF8NormalizeAllStringsInStruct( parse_UGCReviewData, pUGCReviewData );
	
	if (gbEnableUgcDataLogging)
	{
		entLog( LOG_UGC, pEntity, "UGCReviewAttempt", "Series %s ID %d reviewed. Rating: %f. Comment: %s",
			pchSeriesName, iSeriesID, pUGCReviewData->fRating, pUGCReviewData->pComment );
	}

	if( !iSeriesID ) {
		estrDestroy( &pchSeriesName );
		return;
	}

	// Check for the global AUTO_SETTING that disables reviewing.

	if( !gslUGC_ReviewingIsEnabled() ) {
		//  In actuality we should never get here since the client blocks
		//  the review UI buttons from activating when reviewing is
		//  disabled. However, there are theoretically
		//  cases where the setting has changed after the review dialog
		//  has appeared, in which case this serves as a second layer
		//  of prevention.

		if (pEntity) {
			char* estrMessage = NULL;
			const char* pchMessageKey;
			NotifyType eNotifyType;

			estrStackCreate(&estrMessage);

			pchMessageKey = "UGC.ReviewsDisabled";
			eNotifyType = kNotifyType_UGCError;

			entFormatGameMessageKey(pEntity, &estrMessage, pchMessageKey,
				STRFMT_END);

			if (estrMessage && estrMessage[0])
				ClientCmd_NotifySend(pEntity, eNotifyType, estrMessage, NULL, NULL);

			estrDestroy(&estrMessage);
		}
		estrDestroy( &pchSeriesName );
		return;
	}

	// Need to get the series container now to verify a player can
	// review it.
	{
		UGCShardReturnProjectSeriesReviewed* pUGCShardReturnProjectSeriesReviewed = StructCreate(parse_UGCShardReturnProjectSeriesReviewed);
		pUGCShardReturnProjectSeriesReviewed->iSeriesID = iSeriesID;
		pUGCShardReturnProjectSeriesReviewed->pchSeriesName = StructAllocString(pchSeriesName);
		pUGCShardReturnProjectSeriesReviewed->fRating = pUGCReviewData->fRating;
		eaiCopy(&pUGCShardReturnProjectSeriesReviewed->eaiUGCTags, &pUGCReviewData->eaiUGCTags);
		pUGCShardReturnProjectSeriesReviewed->pchComment = StructAllocString(pUGCReviewData->pComment);
		pUGCShardReturnProjectSeriesReviewed->pcShardName = StructAllocString(GetShardNameFromShardInfoString());
		pUGCShardReturnProjectSeriesReviewed->entContainerID = entGetContainerID(pEntity);

		RemoteCommand_Intershard_aslUGCSearchManager_SearchBySeriesID_ForReviewing(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
			pUGCShardReturnProjectSeriesReviewed);
	}

	estrDestroy( &pchSeriesName );
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_SubscribeToAuthor_Return(ContainerID entContainerID, bool bSuccess, UGCSubscriptionData *pUGCSubscriptionData)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	UGCAccount *pUGCAccount = NULL;
	char* estrMessage = NULL;

	// User has disconnected (or was already sent a client message), bail
	if(!pEntity)
		return;

	pUGCAccount = entGetUGCAccount(pEntity);

	if( bSuccess ) {
		// This may be the first time a player gets a UGC Account. Request it.
		if(!pUGCAccount && !entity_IsUGCCharacter(pEntity))
			RemoteCommand_Intershard_aslUGCDataManager_RequestAccount(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(), entGetContainerID(pEntity), entGetAccountID(pEntity));

		entFormatGameMessageKey( pEntity, &estrMessage, "UGC.SubscribeToAuthorFeedback",
			STRFMT_STRING("AuthorName", pUGCSubscriptionData->strSubscribedToName),
			STRFMT_END );
		if( !nullStr( estrMessage )) {
			ClientCmd_NotifySend( pEntity, kNotifyType_UGCFeedback, estrMessage, NULL, NULL );
		}
	} else {
		if(pUGCAccount && !entity_IsUGCCharacter(pEntity))
		{
			ugc_trh_UnsubscribeFromAuthor(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCAccount, pUGCAccount), pUGCSubscriptionData->entContainerID, pUGCSubscriptionData->iAuthorID);

			pEntity->pPlayer->dirtyBit = 1;
		}

		entFormatGameMessageKey( pEntity, &estrMessage, "UGC.SubscribeToAuthorError",
			STRFMT_STRING("AuthorName", pUGCSubscriptionData->strSubscribedToName),
			STRFMT_END );
		if( !nullStr( estrMessage )) {
			ClientCmd_NotifySend( pEntity, kNotifyType_UGCError, estrMessage, NULL, NULL );
		}
	}

	estrDestroy( &estrMessage );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_SubscribeToAuthor( Entity* pEntity, ContainerID iAuthorID, char* strAuthorName_unnormalized )
{
	UGCSubscriptionData* pUGCSubscriptionData = StructCreate(parse_UGCSubscriptionData);
	char* strAuthorName = NULL;
	UTF8NormalizeString( strAuthorName_unnormalized, &strAuthorName );

	pUGCSubscriptionData->entContainerID = entGetContainerID(pEntity);
	pUGCSubscriptionData->strSubscribedToName = StructAllocString(strAuthorName);
	pUGCSubscriptionData->iAuthorID = iAuthorID;

	RemoteCommand_Intershard_aslUGCDataManager_SubscribeToAuthor(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetAccountID(pEntity),
		pUGCSubscriptionData
		);

	{
		// So that we do not need the UGC shard to poke us back with a new UGCAccount structure on the Entity->Player, we will just fix it up locally.
		// This has the benefit of being able to immediately search subscribed UGC author content from the client.
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount && !entity_IsUGCCharacter(pEntity))
		{
			ugc_trh_SubscribeToAuthor(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCAccount, pUGCAccount), pUGCSubscriptionData->entContainerID, iAuthorID);

			pEntity->pPlayer->dirtyBit = 1;
		}
	}

	StructDestroy(parse_UGCSubscriptionData, pUGCSubscriptionData);
	estrDestroy( &strAuthorName );
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_UnsubscribeFromAuthor_Return(ContainerID entContainerID, bool bSuccess, UGCSubscriptionData *pUGCSubscriptionData)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	char* estrMessage = NULL;

	// User has disconnected, bail
	if( !pEntity )
		return;

	if( bSuccess ) {
		entFormatGameMessageKey( pEntity, &estrMessage, "UGC.UnsubscribeFromAuthorFeedback",
			STRFMT_STRING("AuthorName", pUGCSubscriptionData->strSubscribedToName),
			STRFMT_END );
		if( !nullStr( estrMessage )) {
			ClientCmd_NotifySend( pEntity, kNotifyType_UGCFeedback, estrMessage, NULL, NULL );
		}
	} else {
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount && !entity_IsUGCCharacter(pEntity))
		{
			ugc_trh_SubscribeToAuthor(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCAccount, pUGCAccount), pUGCSubscriptionData->entContainerID, pUGCSubscriptionData->iAuthorID);

			pEntity->pPlayer->dirtyBit = 1;
		}

		entFormatGameMessageKey( pEntity, &estrMessage, "UGC.UnsubscribeFromAuthorError",
			STRFMT_STRING("AuthorName", pUGCSubscriptionData->strSubscribedToName),
			STRFMT_END );
		if( !nullStr( estrMessage )) {
			ClientCmd_NotifySend( pEntity, kNotifyType_UGCError, estrMessage, NULL, NULL );
		}
	}

	estrDestroy( &estrMessage );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGC_UnsubscribeFromAuthor( Entity* pEntity, ContainerID iAuthorID, const char* strAuthorName_unnormalized )
{
	UGCSubscriptionData* pUGCSubscriptionData = StructCreate(parse_UGCSubscriptionData);
	char* strAuthorName = NULL;
	UTF8NormalizeString( strAuthorName_unnormalized, &strAuthorName );

	pUGCSubscriptionData->entContainerID = entGetContainerID(pEntity);
	pUGCSubscriptionData->strSubscribedToName = StructAllocString(strAuthorName);
	pUGCSubscriptionData->iAuthorID = iAuthorID;

	RemoteCommand_Intershard_aslUGCDataManager_UnsubscribeFromAuthor(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetAccountID(pEntity),
		pUGCSubscriptionData
		);

	{
		// So that we do not need the UGC shard to poke us back with a new UGCAccount structure on the Entity->Player, we will just fix it up locally.
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount && !entity_IsUGCCharacter(pEntity))
		{
			ugc_trh_UnsubscribeFromAuthor(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCAccount, pUGCAccount), pUGCSubscriptionData->entContainerID, iAuthorID);

			pEntity->pPlayer->dirtyBit = 1;
		}
	}

	StructDestroy(parse_UGCSubscriptionData, pUGCSubscriptionData);
	estrDestroy( &strAuthorName );
}

// Called when we're leaving Play mode
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslUGCEnterEditor( Entity *pEntity )
{
	char ns[RESOURCE_NAME_MAX_SIZE];
	bool delete_files = false;
	Quat estimateQuat;

	if (gGSLState.bCurrentlyInUGCPreviewMode)
	{
		TellControllerWeMayBeStallyForNSeconds(giUGCLeavePreviewStallySeconds, "LeavePreview");
	}

	// Dismiss any interacts that are in progress so they do not carry over to the next map we reload
	interaction_EndInteractionAndDialog(entGetPartitionIdx(pEntity), pEntity, false, true, true);

	// Despawn pets (if any) before load and otherwise consider the player to have left the previous map
	gslPlayerLeftMap(pEntity, true);

	if (resExtractNameSpace_s(zmapInfoGetPublicName(NULL), SAFESTR(ns), NULL, 0))
		delete_files = true;

	// Move the entity to an estimate spawn point to prevent crashing on bad coordinates during worldLoadZoneMapByName
	PYRToQuat(zerovec3, estimateQuat);
	entSetPosRotFace(pEntity, zerovec3, estimateQuat, zerovec3, true, false, "estimateSpawnLoc");

	// Load the empty map
	worldLoadZoneMapByName("Ugc_Editing_Map");

	spawnpoint_MovePlayerToStartSpawn(pEntity, NULL, true);

	if (delete_files)
		gslUGC_DeleteNamespaceDataFiles(ns);

	gGSLState.bCurrentlyInUGCPreviewMode = false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_PRIVATE;
void gslIsUGCPublishDisabled(Entity *pEntity)
{
	ClientCmd_gclUGCPublishEnabled(pEntity, ugcIsPublishEnabled());
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void gslUGCPlay( Entity *pEntity, UGCProjectData* ugc_proj, const char *map_name_unnormalized, U32 objective_id, Vec3 pos, Vec3 rot )
{
	char* map_name = NULL;
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, ugc_proj );
	UTF8NormalizeString( map_name_unnormalized, &map_name );
	
	gslUGC_SetUGCProjectCopy(ugc_proj);

	s_ugcPlayDialogTreeID = 0;
	s_ugcPlayDialogTreePromptID = 0;
	gslUGC_DoPlay( pEntity, ugc_proj, map_name, objective_id, pos, rot, false );
	aiDisableForUGC = false;
	estrDestroy( &map_name );
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void gslUGCPlayDialogTree( Entity* pEntity, UGCProjectData* ugc_proj, U32 dialog_tree_id, int prompt_id )
{
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, ugc_proj );
	gslUGC_SetUGCProjectCopy(ugc_proj);

	s_ugcPlayDialogTreeID = dialog_tree_id;
	s_ugcPlayDialogTreePromptID = prompt_id;
	gslUGC_DoPlayDialogTree( pEntity, ugc_proj, dialog_tree_id, prompt_id );
	aiDisableForUGC = false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD;
void gslUGCResetMap(Entity *pEntity, UGCProjectData* ugc_proj)
{
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, ugc_proj );
	
	if (isProductionEditMode() && pEntity)
	{
		// Dismiss any interacts that are in progress so they do not stay.
		interaction_EndInteractionAndDialog(entGetPartitionIdx(pEntity), pEntity, false, true, true);

		game_MapReInit();
		if( s_ugcPlayDialogTreeID ) {
			gslUGC_DoPlayDialogTree( pEntity, ugc_proj, s_ugcPlayDialogTreeID, s_ugcPlayDialogTreePromptID );
		} else {
			gslUGC_DoPlay( pEntity, ugc_proj, zmapInfoGetPublicName( NULL ), 0, NULL, NULL, true );
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD;
void ugc_KillTarget(Entity *pEntity)
{
	if(!isProductionEditMode()) return;

	KillTarget(pEntity);
}

AUTO_STRUCT;
typedef struct AddMissionByNameData
{
	ContainerID entContainerID;
	char *pcNamespace;
	bool bPlayingAsBetaReviewer;
} AddMissionByNameData;

static void missioninfo_AddMissionByName_CB(TransactionReturnVal *returnVal, AddMissionByNameData *pAddMissionByNameData)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
		AutoTrans_mission_tr_SetUGCPlayingAsBetaReviewer(NULL, GetAppGlobalType(),
		GLOBALTYPE_ENTITYPLAYER, pAddMissionByNameData->entContainerID,
		pAddMissionByNameData->pcNamespace,
		pAddMissionByNameData->bPlayingAsBetaReviewer);

	StructDestroy(parse_AddMissionByNameData, pAddMissionByNameData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslUGC_PlayProjectNonEditor(Entity *pEntity, 
	const char *pcNamespace_unnormalized, 
	const char *pcCostumeOverride_unnormalized,
	const char *pcPetOverride_unnormalized,
	const char *pcBodyText_unnormalized,
	bool bPlayingAsBetaReviewer)
{
	char* pcNamespace = NULL;
	char* pcCostumeOverride = NULL;
	char* pcPetOverride = NULL;
	char* pcBodyText = NULL;

	if(!gslUGC_PlayingIsEnabled())
		return;

	UTF8NormalizeString( pcNamespace_unnormalized, &pcNamespace );
	UTF8NormalizeString( pcCostumeOverride_unnormalized, &pcCostumeOverride );
	UTF8NormalizeString( pcPetOverride_unnormalized, &pcPetOverride );
	UTF8NormalizeString( pcBodyText_unnormalized, &pcBodyText );

	if(pEntity && !nullStr(pcNamespace))
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
		if(pInfo)
		{
			AllegianceDef *pAllegiance = GET_REF(pEntity->hAllegiance);
			if(!pAllegiance || !pAllegiance->bCannotPlayUGC)
			{
				PlayerCostume* pCostume = costumeEntity_CostumeFromName(pcCostumeOverride);
				PetContactList* pPetContactList = RefSystem_ReferentFromString("PetContactList", pcPetOverride);
				ContainerID iProjectID = UGCProject_GetContainerIDFromUGCNamespace(pcNamespace);
				char pcMissionName[RESOURCE_NAME_MAX_SIZE];
				sprintf(pcMissionName, "%s:Mission", pcNamespace);

				if(iProjectID)
					RemoteCommand_Intershard_aslUGCDataManager_ReportUGCProjectWasPlayedForWhatsHot(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, iProjectID);

				if(g_isContinuousBuilder || !contactdialog_CreateNamespaceMissionGrantContact(pEntity, pcMissionName, pCostume, pPetContactList, pcBodyText))
				{
					AddMissionByNameData *pAddMissionByNameData = StructCreate(parse_AddMissionByNameData);
					pAddMissionByNameData->entContainerID = entGetContainerID(pEntity);
					pAddMissionByNameData->pcNamespace = StructAllocString(pcNamespace);
					pAddMissionByNameData->bPlayingAsBetaReviewer = bPlayingAsBetaReviewer;

					missioninfo_AddMissionByName(entGetPartitionIdx(pEntity), pInfo, pcMissionName, missioninfo_AddMissionByName_CB, pAddMissionByNameData);
				}
			}
		}
	}

	estrDestroy( &pcNamespace );
	estrDestroy( &pcCostumeOverride );
	estrDestroy( &pcPetOverride );
	estrDestroy( &pcBodyText );
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslUGC_SetPlayerIsReviewer(Entity *pEntity, bool bSet)
{
	if( !GamePermission_EntHasToken( pEntity, GAME_PERMISSION_UGC_CAN_REPORT_PROJECT )) {
		return;
	}
	
	if (gConf.bDontAllowGADModification)
		gslAPSetKeyValueCmdNoCallback(pEntity, GetAccountUgcReviewerKey(), bSet ? 1 : 0);
	else
		gslGAD_SetAttrib(pEntity, GetAccountUgcReviewerKey(), bSet ? "1" : "0");

	{
		UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
		event->uUGCAuthorID = entGetAccountID(pEntity);
		event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
		event->ugcAchievementServerEvent->ugcPlayerReviewerEvent = StructCreate(parse_UGCPlayerReviewerEvent);
		event->ugcAchievementServerEvent->ugcPlayerReviewerEvent->bPlayerIsReviewer = bSet;
		RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
		StructDestroy(parse_UGCAchievementEvent, event);
	}
}

AUTO_COMMAND ACMD_CATEGORY(UGC);
void dbgUpdateAllUGCStatus(void)
{
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		QueryUGCProjectStatus(currEnt);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslUGC_SetUGCProjectSearchEULAAccepted(Entity *pEntity, bool bAccepted)
{
	if (gConf.bDontAllowGADModification)
		gslAPSetKeyValueCmdNoCallback(pEntity, GetAccountUgcProjectSearchEULAKey(), bAccepted ? 1 : 0);
	else
		gslGAD_SetAttrib(pEntity, GetAccountUgcProjectSearchEULAKey(), bAccepted ? "1" : "0");
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_ReportProject_Return(ContainerID entContainerID, bool bSuccess, UGCShardReturnProjectReviewed *pUGCShardReturnProjectReviewed)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
	{
		char* estrMessage = NULL;
		const char* pchMessageKey;
		NotifyType eNotifyType;

		estrStackCreate(&estrMessage);

		if(bSuccess)
		{
			pchMessageKey = "UGC.ReportFeedback";
			eNotifyType = kNotifyType_UGCFeedback;
		}
		else
		{
			pchMessageKey = "UGC.ReportError";
			eNotifyType = kNotifyType_UGCError;
		}

		entFormatGameMessageKey(pEnt, &estrMessage, pchMessageKey,
			STRFMT_STRING("ProjectName", pUGCShardReturnProjectReviewed->pchProjName),
			STRFMT_INT("ProjectID", pUGCShardReturnProjectReviewed->iProjectID),
			STRFMT_END);

		if (estrMessage && estrMessage[0])
			ClientCmd_NotifySend(pEnt, eNotifyType, estrMessage, NULL, NULL);

		estrDestroy(&estrMessage);
	}
}

static void gslUGC_ReportInternal(Entity *pEnt, ContainerID iProjectID, const char* pchProjName,
	U32 eReason, const char* pchDetails)
{
	UGCShardReturnProjectReviewed *pUGCShardReturnProjectReviewed = calloc(1, sizeof(UGCShardReturnProjectReviewed));
	pUGCShardReturnProjectReviewed->iProjectID = iProjectID;
	pUGCShardReturnProjectReviewed->entContainerID = entGetContainerID(pEnt);
	pUGCShardReturnProjectReviewed->pchProjName = StructAllocString(pchProjName);

	RemoteCommand_Intershard_aslUGCDataManager_ReportProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetContainerID(pEnt),
		pUGCShardReturnProjectReviewed,
		iProjectID, pEnt->pPlayer->accountID, pEnt->pPlayer->publicAccountName, eReason, pchDetails);

	StructDestroy(parse_UGCShardReturnProjectReviewed, pUGCShardReturnProjectReviewed);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslUGC_Report(Entity *pEntity, U32 iProjectID, const char* pchProjName_unnormalized, U32 eReason, const char* pchDetails_unnormalized)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	char* pchProjName = NULL;
	char* pchDetails = NULL;
	
	UTF8NormalizeString( pchProjName_unnormalized, &pchProjName );
	UTF8NormalizeString( pchDetails_unnormalized, &pchDetails );

	if (!pInfo || !iProjectID)
	{
		estrDestroy( &pchProjName );
		estrDestroy( &pchDetails );
		return;
	}
	if( !GamePermission_EntHasToken( pEntity, GAME_PERMISSION_UGC_CAN_REPORT_PROJECT ))
	{
		estrDestroy( &pchProjName );
		estrDestroy( &pchDetails );
		return;
	}
	if (pInfo->uLastMissionRatingRequestID != iProjectID &&
		!mission_FindMissionFromUGCProjectID(pInfo, iProjectID) &&
		!entity_HasCompletedUGCProject(pEntity, iProjectID))
	{
		estrDestroy( &pchProjName );
		estrDestroy( &pchDetails );
		return;
	}
	if (UGCProject_CanMakeReport(NULL, 0, eReason, pchDetails))
	{
		gslUGC_ReportInternal(pEntity, iProjectID, pchProjName, eReason, pchDetails);
	}

	estrDestroy( &pchProjName );
	estrDestroy( &pchDetails );
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_HIDE;
void gslUGC_GodMode(Entity *pEntity, bool enable)
{
	Entity** pets = NULL;

	if(!isProductionEditMode()) return;

	PetCommands_GetAllPets( pEntity, &pets );

	gslEntityGodMode(pEntity, enable);
	FOR_EACH_IN_EARRAY( pets, Entity, pet ) {
		gslEntityGodMode( pet, enable );
	} FOR_EACH_END;

	eaDestroy(&pets);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_HIDE;
void gslUGC_UntargetableMode(Entity *pEntity, bool enable)
{
	Entity** pets = NULL;

	if(!isProductionEditMode()) return;

	PetCommands_GetAllPets( pEntity, &pets );

	gslEntityUntargetableMode(pEntity, enable);
	FOR_EACH_IN_EARRAY( pets, Entity, pet ) {
		gslEntityUntargetableMode( pet, enable );
	} FOR_EACH_END;

	eaDestroy(&pets);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_HIDE;
void gslUGC_FreecamMode(Entity* pEntity, bool enable)
{
	PowerDef* powerDef;
	Power* power;
	if( !isProductionEditMode() ) {
		return;
	}

	powerDef = powerdef_Find( "UGC_Flight_Power" );
	if( !powerDef ) {
		return;
	}
	power = character_FindPowerByDefPersonal( entGetChar( pEntity ), powerDef );

	if( enable ) {
		if( !power ) {
			character_AddPowerPersonal( entGetPartitionIdx( pEntity ), entGetChar( pEntity ), powerDef, 0, false, entity_GetCachedGameAccountDataExtract( pEntity ));
		}
	} else {
		if( power ) {
			character_RemovePowerPersonal( entGetChar( pEntity ), power->uiID );
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_HIDE;
void gslUGC_RespawnAtFullHealth(Entity* pEntity)
{
	if(!isProductionEditMode()) return;

	gslUGC_DoRespawnAtFullHealth( pEntity );
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_HIDE ACMD_NAME(ugc_ReportTest);
void gslUGC_ReportTest(Entity *pEntity, const char* pchNamespace_unnormalized, U32 eReason, const char* pchDetails_unnormalized)
{
	ContainerID iProjectID;
	char* pchNamespace = NULL;
	char* pchDetails = NULL;
	UTF8NormalizeString( pchNamespace_unnormalized, &pchNamespace );
	UTF8NormalizeString( pchDetails_unnormalized, &pchDetails );

	iProjectID = UGCProject_GetContainerIDFromUGCNamespace(pchNamespace);
	if (iProjectID)
	{
		gslUGC_ReportInternal(pEntity, iProjectID, NULL, eReason, pchDetails);
	}

	estrDestroy( &pchNamespace );
	estrDestroy( &pchDetails );
}

void gslUGC_WaitForResourcesCB(U32 uFenceID, UserData pData)
{
	EntityRef ref = (EntityRef)(intptr_t)pData;
	Entity *pEntity = entFromEntityRefAnyPartition(ref);
	if (pEntity)
	{
		ClientCmd_UGCEditorWaitForResourcesComplete(pEntity);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslUGC_WaitForResources(Entity *pEntity)
{
	EntityRef ref = entGetRef(pEntity);
	resServerRequestFenceInstruction(entGetClientLink(pEntity)->pResourceCache, gslUGC_WaitForResourcesCB, (void*)(intptr_t)ref);
}

AUTO_COMMAND_REMOTE;
void gslUGC_SendMailForChange( const UGCProject* pProject, const UGCProjectVersion* pVersion, UGCChangeReason reason )
{
	const char* pHeaderMessageKey = NULL;
	const char* pBodyMessageKey = NULL;
	char *pHeaderString = NULL;
	char *pBodyString = NULL;

	switch( reason ) {
		xcase UGC_CHANGE_AUTOWITHDRAW:
	pHeaderMessageKey = "UGC.ProjectAutoWithdrawnHeader";
	pBodyMessageKey = "UGC.ProjectAutoWithdrawnBody";
	xcase UGC_CHANGE_CSR_BAN:
	pHeaderMessageKey = "UGC.ProjectCSRBanHeader";
	pBodyMessageKey = "UGC.ProjectCSRBanBody";
	xcase UGC_CHANGE_TEMP_AUTOBAN:
	pHeaderMessageKey = "UGC.ProjectTempAutobanHeader";
	pBodyMessageKey = "UGC.ProjectTempAutobanBody";
	xcase UGC_CHANGE_PERMANENT_AUTOBAN:
	pHeaderMessageKey = "UGC.ProjectPermanentAutobanHeader";
	pBodyMessageKey = "UGC.ProjectPermanentAutobanBody";
xdefault:
	AssertOrAlert("UGC_CHANGE_NOT_INFORMING_PLAYER", "Project (%d) changed without informing the player.  This should never happen.",
		pProject->id );
	return;
	}

	langFormatGameMessageKey( pProject->iOwnerLangID, (&pHeaderString), pHeaderMessageKey,
		STRFMT_STRING("ProjName", UGCProject_GetVersionName(pProject, pVersion)),
		STRFMT_STRING("ProjID", pProject->pIDString),
		STRFMT_INT("DaysOld", gConf.iUGCProjectNoPlayDaysBeforeNoRepublish),
		STRFMT_END );

	langFormatGameMessageKey( pProject->iOwnerLangID, (&pBodyString), pBodyMessageKey,
		STRFMT_STRING("ProjName", UGCProject_GetVersionName(pProject, pVersion)),
		STRFMT_STRING("ProjID", pProject->pIDString),
		STRFMT_INT("DaysOld", gConf.iUGCProjectNoPlayDaysBeforeNoRepublish),
		STRFMT_END );

	RemoteCommand_ChatServerSendNPCEmail_Simple(
		GLOBALTYPE_CHATSERVER, 0, pProject->iOwnerAccountID, 0, pProject->pOwnerAccountName,
		TranslateMessageKeyDefault("UGC.TheFoundry", "The Foundry"),
		pHeaderString, pBodyString );

	estrDestroy( &pHeaderString );
	estrDestroy( &pBodyString );
}

// These commands are no longer useful.  Everything tagged is in UGCResInfos now.
// 
// static const WorldUGCProperties *ugcResourceGetUGCPropertiesLocal(const char *dictName, const char *objName)
// {
// 	ParseTable* parse_table = RefSystem_GetDictionaryParseTable(dictName);
// 	void* data = RefSystem_ReferentFromString(dictName, objName);
// 	int column;

// 	if (data && parse_table && ParserFindColumn(parse_table, "UGCProperties", &column))
// 	{
// 		WorldUGCProperties *ugc_properties = (WorldUGCProperties*)TokenStoreGetPointer(parse_table, column, data, 0, NULL);
// 		return ugc_properties;
// 	}
// 	return NULL;
// }

// // AUTO_COMMAND;
// void gslUGC_ListExplicitlyTaggedAssets( void )
// {
// 	// NOTE: this list of dictionaries needs to be the same as the one
// 	// in UGCCommon's ugcResourceInfoPopulateDictionary.
// 	char* dictionaries[] = {
// 		"AIAnimList", "Cutscene", "FSM", "UGCGenesisBackdropDef", "ObjectLibrary",
// 		"PetContactList", "PlayerCostume", "RewardTable", "UGCSound",
// 		"ZoneMap",
// 		NULL };
// 	int dictIt;

// 	for( dictIt = 0; dictionaries[ dictIt ]; ++dictIt ) {
// 		ResourceSearchRequest request = { 0 };
// 		ResourceSearchResult* result = NULL;

// 		request.eSearchMode = SEARCH_MODE_TAG_SEARCH;
// 		request.pcSearchDetails = "UGC";
// 		request.pcType = dictionaries[ dictIt ];
// 		request.iRequest = 1;

// 		result = handleResourceSearchRequest( &request );
// 		if( result ) {
// 			FILE* outFile = NULL;
// 			char filenameBuffer[ 256 ];
// 			int rowIt;

// 			sprintf( filenameBuffer, "c:/Autogen_%s.preugcresinfo", dictionaries[ dictIt ]);
// 			outFile = fopen( filenameBuffer, "wt" );
// 			fprintf( outFile, "// Autogenerated preugcresinfo from dictionary\n" );

// 			for( rowIt = 0; rowIt != eaSize( &result->eaRows ); ++rowIt ) {
// 				ResourceSearchResultRow* row = result->eaRows[ rowIt ];
// 				ResourceInfo* info = resGetInfo( row->pcType, row->pcName );
// 				const WorldUGCProperties* ugcProps = ugcResourceGetUGCPropertiesLocal( row->pcType, row->pcName );
// 				const char* name = info->resourceName;

// 				if( stricmp( info->resourceDict, "ObjectLibrary" ) == 0 ) {
// 					GroupDef* def = objectLibraryGetGroupDefByName( info->resourceName, false );
// 					if( SAFE_MEMBER( def, name_str )) {
// 						name = def->name_str;
// 					}
// 				}
// 				fprintf( outFile, "\n"
// 						 "Resource %s\n"
// 						 "{\n"
// 						 "\tDictionary %s\n"
// 						 "\tTags \"%s\"\n",
// 						 name, info->resourceDict, info->resourceTags );
// 				if( ugcProps ) {
// 					if( TranslateDisplayMessage( ugcProps->dVisibleName )) {
// 						fprintf( outFile, "\tVisibleNameString %s\n", TranslateDisplayMessage( ugcProps->dVisibleName ));
// 					}
// 					if( TranslateDisplayMessage( ugcProps->dDescription )) {
// 						fprintf( outFile, "\tDescriptionString %s\n", TranslateDisplayMessage(( ugcProps->dDescription )));
// 					}
// 					if( !nullStr( ugcProps->pchImageOverride )) {
// 						fprintf( outFile, "\tImageOverride \"%s\"\n", ugcProps->pchImageOverride );
// 					}
// 					if( ugcProps->bNoDescription ) {
// 						fprintf( outFile, "\tNoDescription 1\n" );
// 					}
// 					if( ugcProps->fMapDefaultHeight ) {
// 						fprintf( outFile, "\tMapDefaultHeight %f\n", ugcProps->fMapDefaultHeight );
// 					}
// 					if( ugcProps->bMapOnlyPlatformsAreLegal ) {
// 						fprintf( outFile, "\tMapOnlyPlatformsAreLegal 1\n" );
// 					}
// 					if( ugcProps->groupDefProps.bRoomDoorsEverywhere ) {
// 						fprintf( outFile, "\tRoomDoorsEverywhere 1\n" );
// 					}
// 					if( !nullStr( ugcProps->groupDefProps.strClickableName )) {
// 						fprintf( outFile, "\tClickableName \"%s\"\n", ugcProps->groupDefProps.strClickableName );
// 					}

// 					if( ugcProps->restrictionProps.iMinLevel ) {
// 						fprintf( outFile, "\tMinLevel %d\n", ugcProps->restrictionProps.iMinLevel );
// 					}
// 					if( ugcProps->restrictionProps.iMaxLevel ) {
// 						fprintf( outFile, "\tMaxLevel %d\n", ugcProps->restrictionProps.iMaxLevel );
// 					}
// 					{
// 						int it;
// 						for( it = 0; it != eaSize( &ugcProps->restrictionProps.eaFactions ); ++it ) {
// 							fprintf( outFile, "\tFaction %s\n", ugcProps->restrictionProps.eaFactions[ it ]->pcFaction );
// 						}
// 					}
// 				}
// 				fprintf( outFile, "}\n" );
// 			}

// 			fclose( outFile );
// 		}
// 		StructDestroySafe( parse_ResourceSearchResult, &result );
// 	}
// }

// AUTO_COMMAND;
// void gslUGC_ListAllEncounterTemplates( void )
// {
// 	FILE* outFile = fopen( "c:/EncounterTemplates.csv", "wt" );
// 	ResourceSearchRequest req = { 0 };
// 	ResourceSearchResult* result = NULL;

// 	req.eSearchMode = SEARCH_MODE_TAG_SEARCH;
// 	req.pcSearchDetails = "UGC,Encounter";
// 	req.pcType = "ObjectLibrary";
// 	req.iRequest = 1;
// 	result = ugcResourceSearchRequest( &req );

// 	fprintf( outFile, "// Autogenerated\n" );
// 	fprintf( outFile, "OBJLIB NAME,ENCOUNTER NAME,ENCOUNTER PATH\n" );
// 	{
// 		int it;
// 		for( it = 0; it != eaSize( &result->eaRows ); ++it ) {
// 			char* defName = result->eaRows[ it ]->pcName;
// 			GroupDef* def = objectLibraryGetGroupDefByName( defName, false );
// 			if( !def ) {
// 				fprintf( outFile, "%s,<ERROR DEF NOT FOUND>\n", defName );
// 			} else if( !def->property_structs.encounter_properties ) {
// 				fprintf( outFile, "%s,<ERROR ENCOUNTER PROPS NOT FOUND>\n", def->name_str );
// 			} else {
// 				const char* encName = REF_STRING_FROM_HANDLE( def->property_structs.encounter_properties->hTemplate );
// 				const EncounterTemplate* encTemplate = GET_REF( def->property_structs.encounter_properties->hTemplate );
// 				if( !encTemplate ) {
// 					fprintf( outFile, "%s,%s,<ERROR ENCOUNTER TEMPLATE NOT FOUND>\n",
// 							 def->name_str, encName );
// 				} else {
// 					fprintf( outFile, "%s,%s,%s\n",
// 							 def->name_str, encName, encTemplate->pcFilename );
// 				}
// 			}
// 		}
// 	}
// 	fclose( outFile );
// }

//////////////////////////////////////////////////////////////////////
// Featured content interface

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void UGCFeaturedCopyProject_Return(ContainerID entContainerID, UGCShardReturnAndErrorString *pUGCShardReturnAndErrorString)
{
	Entity* ent = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(!ent)
		return;

	if(pUGCShardReturnAndErrorString->bSucceeded)
		ClientCmd_NotifySend( ent, kNotifyType_Default, pUGCShardReturnAndErrorString->estrDetails, NULL, NULL);
	else
		ClientCmd_NotifySend( ent, kNotifyType_Failed, pUGCShardReturnAndErrorString->estrDetails, NULL, NULL);
}

U32 gslUGC_FeaturedTimeFromString( const char* timeStr )
{
	if( stricmp( timeStr, "now" ) == 0 ) {
		return timeSecondsSince2000();
	} else {
		return timeGetSecondsSince2000FromGenericString( timeStr );
	}
}

//////////////////////////////////////////////////////////////////////
/// Copy PROJECT-ID into a new project (saving rating, reviews, etc.)
/// and mark the copy as featured.
///
/// Other parameters are the same as ugcFeatured_AddProject.
AUTO_COMMAND ACMD_NAME(ugcFeatured_CopyProject) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedCopyProject( Entity* ent, ContainerID projectId, const char* strDetails_unnormalized, const char* startTime_unnormalized, const char* endTime_unnormalized )
{
	NOCONST(UGCFeaturedData) featuredData = { 0 };
	char* strDetails = NULL;
	char* startTime = NULL;
	char* endTime = NULL; 
	UTF8NormalizeString( strDetails_unnormalized, &strDetails );
	UTF8NormalizeString( startTime_unnormalized, &startTime );
	UTF8NormalizeString( endTime_unnormalized, &endTime );
	
	featuredData.iStartTimestamp = gslUGC_FeaturedTimeFromString( startTime );
	featuredData.iEndTimestamp = gslUGC_FeaturedTimeFromString( endTime );
	StructCopyString( &featuredData.strDetails, strDetails );

	RemoteCommand_Intershard_aslUGCDataManager_FeaturedCopyProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		projectId, CONTAINER_RECONST(UGCFeaturedData, &featuredData), GetShardNameFromShardInfoString(), entGetContainerID(ent));

	StructResetNoConst( parse_UGCFeaturedData, &featuredData );
	estrDestroy( &strDetails );
	estrDestroy( &startTime );
	estrDestroy( &endTime );
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_Featured_SaveState_Return(ContainerID entContainerID, UGCFeaturedContentInfoList* pFeatured)
{
	Entity* ent = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(!ent)
		return;

	if(pFeatured)
		ClientCmd_gclUGC_FeaturedSaveState(ent, pFeatured);
	else
		ClientCmd_NotifySend(ent, kNotifyType_Failed, "Command Failed", NULL, NULL);
}

//////////////////////////////////////////////////////////////////////
/// Save the state of Featured/Featured Archives into a file (on the
/// client).
AUTO_COMMAND ACMD_NAME(ugcFeatured_SaveState) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedSaveState( Entity* ent )
{
	RemoteCommand_Intershard_aslUGCSearchManager_Featured_SaveState(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
		GetShardNameFromShardInfoString(), entGetContainerID(ent));
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_Featured_LoadState_Return(UGCFeaturedContentInfoList* pOldFeatured, UGCFeaturedContentInfoList* pNewFeatured)
{
	if(pOldFeatured && pNewFeatured)
	{
		int it;

		// Remove all old featured projects
		for( it = 0; it != eaSize( &pOldFeatured->eaFeaturedContent ); ++it ) {
			UGCFeaturedContentInfo* pFeatured = pOldFeatured->eaFeaturedContent[ it ];
			if( pFeatured->sContentInfo.iUGCProjectID ) {
				RemoteCommand_Intershard_aslUGCDataManager_FeaturedRemoveProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
					GetShardNameFromShardInfoString(),
					/*entContainerID=*/0, // no return
					pFeatured->sContentInfo.iUGCProjectID,
					"unknown_csr");
			} else {
				AssertOrAlert( "UGC_FEATURED_UNSUPPORTED_TYPE", "Got a request for a timestamp from an unsupported ContentInfo." );
			}
		}

		// Add the current list
		for( it = 0; it != eaSize( &pNewFeatured->eaFeaturedContent ); ++it ) {
			UGCFeaturedContentInfo* pFeatured = pNewFeatured->eaFeaturedContent[ it ];
			if( pFeatured->sContentInfo.iUGCProjectID ) {
				RemoteCommand_Intershard_aslUGCDataManager_FeaturedAddProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
					GetShardNameFromShardInfoString(),
					/*entContainerID=*/0, // no return
					pFeatured->sContentInfo.iUGCProjectID,
					"unknown_csr",
					"unspecified_details",
					pFeatured->sFeaturedData.iStartTimestamp,
					pFeatured->sFeaturedData.iEndTimestamp);
			} else {
				AssertOrAlert( "UGC_FEATURED_UNSUPPORTED_TYPE", "Got a request for a timestamp from an unsupported ContentInfo." );
			}
		}
	}
} 

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_PRIVATE;
void gslUGC_FeaturedLoadState(Entity *pEntity, UGCFeaturedContentInfoList* pFeaturedContent)
{
	UTF8NormalizeAllStringsInStruct( parse_UGCFeaturedContentInfoList, pFeaturedContent );
	RemoteCommand_Intershard_aslUGCSearchManager_Featured_LoadState(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
		GetShardNameFromShardInfoString(), entGetContainerID(pEntity), pFeaturedContent);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_Featured_GetAuthorAllowsFeaturedList_Return(ContainerID entContainerID, UGCProjectList* pList, bool bShow)
{
	Entity *ent = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(!ent)
		return;

	if(pList)
	{
		if(bShow)
			ClientCmd_gclUGC_FeaturedShowAuthorAllowsFeaturedList(ent, pList);
		else
			ClientCmd_gclUGC_FeaturedSaveAuthorAllowsFeaturedList(ent, pList);
	}
	else
		ClientCmd_NotifySend(ent, kNotifyType_Failed, "Command Failed", NULL, NULL);
}

//////////////////////////////////////////////////////////////////////
/// Save a list of projects that the author has allowed to be
/// featured.
AUTO_COMMAND ACMD_NAME(ugcFeatured_SaveAuthorAllowsFeaturedList) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedSaveAuthorAllowsFeaturedList( Entity* ent )
{
	RemoteCommand_Intershard_aslUGCSearchManager_Featured_GetAuthorAllowsFeaturedList(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
		GetShardNameFromShardInfoString(), entGetContainerID(ent), /*bShow=*/false, /*bIncludeAlreadyFeatured=*/false);
}

//////////////////////////////////////////////////////////////////////
/// Show a list of projects that the author has allowed to be
/// featured.
AUTO_COMMAND ACMD_NAME(ugcFeatured_ShowAuthorAllowsFeaturedList) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedShowAuthorAllowsFeaturedList( Entity* ent )
{
	RemoteCommand_Intershard_aslUGCSearchManager_Featured_GetAuthorAllowsFeaturedList(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
		GetShardNameFromShardInfoString(), entGetContainerID(ent), /*bShow=*/true, /*bIncludeAlreadyFeatured=*/true);
}

//////////////////////////////////////////////////////////////////////
// "Safe" import/export of UGC projects

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void GetCryptKeyForSafeProjectExport(char *pString, int iID, Entity *pEnt)
{
	char sig[128];
	if (!strStartsWith(pString, UGCProject_GetTimestampPlusShardNameStringEscaped()))
	{
		ClientCmd_ReceiveCryptKeyForSafeProjectExport(pEnt, "", iID);
		return;
	}


	cryptHMACSHA1Create("J39Dl2duUr1", pString, SAFESTR(sig));
	ClientCmd_ReceiveCryptKeyForSafeProjectExport(pEnt, sig, iID);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void CheckBufferForSafeImport(Entity *pEnt, char *pString, int iID)
{
	char sig[128];
	char *pFirstNewLine = strchr(pString, '\n');
	UGCTimeStampPlusShardName timeStampPlusShardName = {0};
	bool bReadResult;
	bool bValidityResult=false;
	char *pReadLocation;

	if (!pFirstNewLine)
	{
		ClientCmd_SafeImportBufferResult(pEnt, iID, 0);
		return;
	}
	*pFirstNewLine = 0;
	cryptHMACSHA1Create("J39Dl2duUr1", pFirstNewLine + 1, SAFESTR(sig));
	if (strcmp(sig, pString) != 0)
	{
		ClientCmd_SafeImportBufferResult(pEnt, iID, 0);
		return;
	}

	StructInit(parse_UGCTimeStampPlusShardName, &timeStampPlusShardName);
	pReadLocation = pFirstNewLine + 1;
	bReadResult = ParserReadTextEscaped(&pReadLocation, parse_UGCTimeStampPlusShardName, &timeStampPlusShardName, 0);
	if (bReadResult)
	{
		bValidityResult = UGCProject_TimeStampPlusShardNameIsValidForSafeImport(&timeStampPlusShardName);
	}
	StructDeInit(parse_UGCTimeStampPlusShardName, &timeStampPlusShardName);


	ClientCmd_SafeImportBufferResult(pEnt, iID, bReadResult && bValidityResult);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(2);
void gslUGC_RespecCharacter( Entity* ent, int allegianceDefaultsIndex, const char* className_unnormalized, int levelValue )
{
	char* className = NULL;

	if(!isProductionEditMode()) return;
	UTF8NormalizeString( className_unnormalized, &className );
	
	gslUGC_DoRespecCharacter( ent, allegianceDefaultsIndex, className, levelValue );

	estrDestroy( &className );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_FORCEWRITECURFILE;
void FreezeUGCProject(Entity *pEntity, UGCProjectData *data, UGCFreezeProjectInfo *pInfo)
{
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, data );
	UTF8NormalizeAllStringsInStruct( parse_UGCFreezeProjectInfo, pInfo );
	DoFreezeUGCProject( pEntity, data, pInfo );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(2);
void gslUGC_PlayingEditorHideComponent( Entity* ent, int componentID )
{
	if( !isProductionEditMode() ) {
		return;
	}

	gslUGC_DoPlayingEditorHideComponent( ent, componentID );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(2);
void gslUGC_PlayingEditorSetActive( Entity* ent )
{
	EntityIterator* it;
	Entity* entCritter;
	if( !isProductionEditMode() ) {
		return;
	}

	aiDisableForUGC = true;

	it = entGetIteratorSingleType( entGetPartitionIdx( ent ), 0, 0, GLOBALTYPE_ENTITYCRITTER );
	while( entCritter = EntityIteratorGetNext( it )) {
		GameEncounter* gameEncounter = SAFE_MEMBER( entCritter->pCritter, encounterData.pGameEncounter );
		int actorIndex = SAFE_MEMBER( entCritter->pCritter, encounterData.iActorIndex );
		if( !gameEncounter || !entCritter->aibase ) {
			continue;
		}

		// Cancel existing behavior
		aiMovementResetPath( entCritter, entCritter->aibase );
		if( entGetChar( entCritter )) {
			character_ActAllCancel( entGetPartitionIdx( entCritter ), entGetChar( entCritter ), true );
		}

		// Reset position
		{
			Vec3 spawnPos;
			Quat spawnRot;
			encounter_GetEncounterActorPosition( gameEncounter, actorIndex, spawnPos, spawnRot );
			entSetPos( entCritter, spawnPos, true, __FUNCTION__ );
			entSetRot( entCritter, spawnRot, true, __FUNCTION__ );
		}
	}
	EntityIteratorRelease( it );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(2);
void gslUGC_UpdateAutosave( UGCProjectData* data )
{
	if( !isProductionEditMode() ) {
		return;
	}
	UTF8NormalizeAllStringsInStruct( parse_UGCProjectData, data );

	gslUGC_SetUGCProjectCopy( data );
}


///////////////////////////////////////////////////////////////////////////////////////////////
// BEGIN GOTHIC EXPORT COMMANDS
///////////////////////////////////////////////////////////////////////////////////////////////

static bool gothicFilename_s(Entity *entity, const char *filename, char *out_filename, size_t out_filename_size)
{
	char csv_filename[CRYPTIC_MAX_PATH];

	if(!isProductionEditMode())
	{
		ClientCmd_clientConPrint(entity, "Must be connected to a Foundry editing GameServer to run gothic export commands.");
		return false;
	}

	if(strEndsWith(filename, ".csv"))
		strcpy(csv_filename, filename);
	else
		changeFileExt(filename, ".csv", csv_filename);

	fileLocateWrite_s(csv_filename, out_filename, out_filename_size);

	return true;
}
#define gothicFilename(entity, filename, csv_filename) gothicFilename_s(entity, filename, SAFESTR(csv_filename))

static FILE *gothicFile(Entity *entity, const char *filename)
{
	char msg[4096];
	char out_filename[CRYPTIC_MAX_PATH];
	FILE *file = NULL;

	if(!gothicFilename(entity, filename, out_filename))
		return NULL;

	file = fopen(out_filename, "wt");
	if(!file)
	{
		sprintf(msg, "Cannot open gothic output file %s.", out_filename);
		ClientCmd_clientConPrint(entity, msg);
		return NULL;
	}

	sprintf(msg, "Writing to gothic: %s ...", out_filename);
	ClientCmd_clientConPrint(entity, msg);

	return file;
}

#define FOR_EACH_INFO_IN_REFDICT(hDict, name) { RefDictIterator i##name##Iter; ReferentInfoStruct *p; RefSystem_InitRefDictIterator(hDict, &i##name##Iter); while ((p = RefSystem_GetNextReferentInfoFromIterator(&i##name##Iter))) { const char *name = p->pStringRefData;

#define FOR_GOTHIC_FILE(entity, filename, file)	{ FILE *file = gothicFile(entity, filename); if(file) {
#define END_GOTHIC_FILE(entity, file)			ClientCmd_clientConPrint(entity, "...Done."); fclose(file); } }

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(UGC);
void gslUGC_ExportGothicForEncounterTemplates(Entity *entity, const char *filename_unnormalized, const char *filename_regex_unnormalized, bool exclude_resinfos)
{
	char* filename = NULL;
	char* filename_regex = NULL;
	UTF8NormalizeString( filename_unnormalized, &filename );
	UTF8NormalizeString( filename_regex_unnormalized, &filename_regex );
	
	FOR_GOTHIC_FILE(entity, filename, file)
	{
		fprintf(file, "EncounterTemplate,Filename\n");
		FOR_EACH_IN_REFDICT("EncounterTemplate", EncounterTemplate, encounter)
		{
			if(RegExSimpleMatch(encounter->pcFilename, filename_regex))
				if(!exclude_resinfos || !ugcResourceGetInfo("EncounterTemplate", encounter->pcName))
					fprintf(file, "%s,%s\n", encounter->pcName, encounter->pcFilename);
		}
		FOR_EACH_END
	}
	END_GOTHIC_FILE(entity, file);

	estrDestroy( &filename );
	estrDestroy( &filename_regex );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(UGC);
void gslUGC_ExportGothicForPlayerCostumes(Entity *entity, const char *filename_unnormalized, const char *filename_regex_unnormalized, bool exclude_resinfos)
{
	char* filename = NULL;
	char* filename_regex = NULL;
	UTF8NormalizeString( filename_unnormalized, &filename );
	UTF8NormalizeString( filename_regex_unnormalized, &filename_regex );

	FOR_GOTHIC_FILE(entity, filename, file)
	{
		fprintf(file, "PlayerCostume,Filename\n");
		FOR_EACH_IN_REFDICT("PlayerCostume", PlayerCostume, costume)
		{
			if(RegExSimpleMatch(costume->pcFileName, filename_regex))
				if(!exclude_resinfos || !ugcResourceGetInfo("PlayerCostume", costume->pcName))
					fprintf(file, "%s,%s\n", costume->pcName, costume->pcFileName);
		}
		FOR_EACH_END
	}
	END_GOTHIC_FILE(entity, file);

	estrDestroy( &filename );
	estrDestroy( &filename_regex );
}

///////////////////////////////////////////////////////////////////////////////////////////////
// END GOTHIC EXPORT COMMANDS
///////////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_HIDE ACMD_CLIENTONLY;
void gslUGC_RequestAccountThrottled(Entity *pEntity)
{
	if(pEntity && pEntity->pPlayer && !entity_IsUGCCharacter(pEntity))
	{
		U32 now = timeSecondsSince2000();
		if(now >= pEntity->pPlayer->iLastUGCAccountRequestTimestamp + 60) // only allow every 60 seconds
		{
			pEntity->pPlayer->iLastUGCAccountRequestTimestamp = now - 1; // subtract 1 to ensure it does not happen again within 1 second

			RemoteCommand_Intershard_aslUGCDataManager_RequestAccount(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
				GetShardNameFromShardInfoString(), entGetContainerID(pEntity), entGetAccountID(pEntity));
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void gslUGC_ProvideAccount(ContainerID entContainerID, UGCAccount *pUGCAccount)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEntity)
	{
		StructDestroySafe(parse_UGCAccount, &pEntity->pPlayer->pUGCAccount);
		pEntity->pPlayer->pUGCAccount = StructClone(parse_UGCAccount, pUGCAccount);

		pEntity->pPlayer->dirtyBit = 1;

		if(isDevelopmentMode()) ClientCmd_achDebugAccountChangeCB(pEntity);

		RemoteCommand_gslUGC_AchievementsNotify(GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEntity), entGetContainerID(pEntity));

		eventsend_RecordUGCAccountChanged(entGetPartitionIdx(pEntity), pEntity);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(4);
void gslUGC_TransferProjectOwnershipToUserByIDWithName(ContainerID uUGCAccountID, const char *pcAccountName_unnormalized)
{
	char* pcAccountName = NULL;
	UTF8NormalizeString( pcAccountName_unnormalized, &pcAccountName );

	if(uUGCAccountID)
	{
		UGCProject *pUGCProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
		if(pUGCProject)
			RemoteCommand_ugcTransferProjectOwnershipToUserByIDWithName(GLOBALTYPE_UGCDATAMANAGER, 0, pUGCProject->id, uUGCAccountID, pcAccountName);
	}

	estrDestroy( &pcAccountName );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(7);
void gslUGC_FlagProjectAsCryptic(ContainerID uUGCProjectID, bool bFlaggedAsCryptic)
{
	if(uUGCProjectID)
		RemoteCommand_Intershard_ugcFlagProjectAsCryptic(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, uUGCProjectID, bFlaggedAsCryptic);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(7);
void gslUGC_FlagProjectSeriesAsCryptic(ContainerID uUGCProjectSeriesID, bool bFlaggedAsCryptic)
{
	if(uUGCProjectSeriesID)
		RemoteCommand_Intershard_ugcFlagProjectSeriesAsCryptic(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, uUGCProjectSeriesID, bFlaggedAsCryptic);
}

char *UGCImport_Save(UGCProject *pUGCProject, UGCProjectData *pUGCProjectData, bool bPublish, TransactionReturnVal *pReturnVal)
{
	InfoForUGCProjectSaveOrPublish infoForSave = {0};
	ContainerID projectID;
	char *estrError = NULL;

	if(!pUGCProjectData)
		return estrCreateFromStr("No UGC Project passed to UGCImport_Save!");

	ugcProjectDataNameSpaceChange(pUGCProjectData, UGCProject_GetMostRecentNamespace(pUGCProject));

	if(!gslUGC_DoSaveForProject(pUGCProject, pUGCProjectData, &infoForSave, &projectID, bPublish, &estrError, __FUNCTION__))
		return estrError;

	if(bPublish)
	{
		AutoTrans_trSaveUgcProjectForNeedsFirstPublish(pReturnVal, GetAppGlobalType(),
			GLOBALTYPE_UGCPROJECT, projectID,
			/*bNeedsFirstPublishState=*/true,
			&infoForSave,
			"UGCImport_Save (needs first publish)");
	}
	else
	{
		AutoTrans_trSaveUgcProjectForNeedsFirstPublish(pReturnVal, GetAppGlobalType(),
			GLOBALTYPE_UGCPROJECT, projectID,
			/*bNeedsFirstPublishState=*/false,
			&infoForSave,
			"UGCImport_Save");
	}

	return NULL;
}

#include "AutoGen/gslUGC_cmd_c_ast.c"
