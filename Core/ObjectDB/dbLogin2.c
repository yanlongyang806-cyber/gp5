/***************************************************************************



***************************************************************************/

#include "AccountStub.h"
#include "dbContainerRestore.h"
#include "dbGenericDatabaseThreads.h"
#include "dbLogin2.h"
#include "earray.h"
#include "error.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "Login2Common.h"
#include "Login2ServerCommon.h"
#include "MemAlloc.h"
#include "objContainer.h"
#include "ObjectDB.h"
#include "objIndex.h"
#include "objTransactions.h"
#include "stdtypes.h"
#include "StringCache.h"
#include "textparser.h"
#include "ThreadSafeMemoryPool.h"
#include "UtilitiesLib.h"
#include "VirtualShard.h"

#include "GlobalTypes_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/dbLogin2_h_ast.h"
#include "AutoGen/ObjectDB_autogen_SlowFuncs.h"

extern ObjectIndex *gAccountID_idx;
extern ObjectIndex *gAccountIDDeleted_idx;
extern bool dbPopulateDeletedChoiceLists;

// This starts false and is set when the ObjectDB reaches the HandleRequests state.
static bool gAcceptLoginRemoteCommands = false;

TSMP_DEFINE(DBGetCharacterDetailState);
TSMP_DEFINE(Login2CharacterDetailDBReturn);
TSMP_DEFINE(Login2CharacterChoice);
TSMP_DEFINE(Login2CharacterChoices);
TSMP_DEFINE(DBOnlineCharacterState);

AUTO_RUN;
void InitDBLogin2Types(void)
{
	TSMP_SMART_CREATE(DBGetCharacterDetailState, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_DBGetCharacterDetailState, &TSMP_NAME(DBGetCharacterDetailState));

	TSMP_SMART_CREATE(Login2CharacterDetailDBReturn, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_Login2CharacterDetailDBReturn, &TSMP_NAME(Login2CharacterDetailDBReturn));

	TSMP_SMART_CREATE(Login2CharacterChoice, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_Login2CharacterChoice, &TSMP_NAME(Login2CharacterChoice));

	TSMP_SMART_CREATE(Login2CharacterChoices, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_Login2CharacterChoices, &TSMP_NAME(Login2CharacterChoices));

	TSMP_SMART_CREATE(DBOnlineCharacterState, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_DBOnlineCharacterState, &TSMP_NAME(DBOnlineCharacterState));
}

static bool
IsUGCAllowedForVirtualShard(U32 virtualShardID)
{
    Container *con;
    VirtualShard *virtualShard;

    con = objGetContainer(GLOBALTYPE_VIRTUALSHARD, virtualShardID);
    if ( con && con->containerData )
    {
        virtualShard = con->containerData;

        return ( virtualShard->bUGCShard && !virtualShard->bDisabled );
    }

    return false;
}

// Build a Login2CharacterChoice struct from a Container representing an online or deleted character.
// (We only use the header here, but the container is made available for convenience. Don't try reading the container data!)
static Login2CharacterChoice *
CharacterChoiceFromContainer(Container *con, bool deleted)
{
    Login2CharacterChoice *characterChoice = StructCreate(parse_Login2CharacterChoice);
	ObjectIndexHeader *header = con->header;

    characterChoice->shardName = Login2_GetPooledShardName();
    characterChoice->isDeleted = deleted;
    characterChoice->isOffline = false;
    characterChoice->isUGCEditAllowed = IsUGCAllowedForVirtualShard(header->virtualShardId);

    // Fill in which server owns the container.
    characterChoice->ownerType = con->meta.containerOwnerType;
    characterChoice->ownerID = con->meta.containerOwnerID;

    // Copy data from header.
    characterChoice->accountID = header->accountId;
    characterChoice->containerID = header->containerId;
    characterChoice->createdTime = header->createdTime;
    characterChoice->level = header->level;
    characterChoice->fixupVersion = header->fixupVersion;
    characterChoice->lastPlayedTime = header->lastPlayedTime;
    characterChoice->virtualShardID = header->virtualShardId;
    characterChoice->pubAccountName = strdup_special(header->pubAccountName, CRYPTIC_CHURN_HEAP);
    characterChoice->privAccountName = strdup_special(header->privAccountName, CRYPTIC_CHURN_HEAP);
    characterChoice->savedName = strdup_special(header->savedName, CRYPTIC_CHURN_HEAP);
    characterChoice->extraData1 = strdup_special(header->extraData1, CRYPTIC_CHURN_HEAP);
    characterChoice->extraData2 = strdup_special(header->extraData2, CRYPTIC_CHURN_HEAP);
    characterChoice->extraData3 = strdup_special(header->extraData3, CRYPTIC_CHURN_HEAP);
    characterChoice->extraData4 = strdup_special(header->extraData4, CRYPTIC_CHURN_HEAP);
    characterChoice->extraData5 = strdup_special(header->extraData5, CRYPTIC_CHURN_HEAP);

    return characterChoice;
}

// Build a Login2CharacterChoice struct from a CharacterStub, representing an offlined character.
static Login2CharacterChoice *
CharacterChoiceFromStub(CharacterStub *characterStub, ContainerID accountID)
{
    Login2CharacterChoice *characterChoice = StructCreate(parse_Login2CharacterChoice);

    characterChoice->shardName = Login2_GetPooledShardName();
    characterChoice->isDeleted = false;
    characterChoice->isOffline = true;
    characterChoice->isUGCEditAllowed = IsUGCAllowedForVirtualShard(characterStub->virtualShardId);

    // This character is offline, so it is owned by the ObjectDB.
    characterChoice->ownerType = objServerType();
    characterChoice->ownerID = objServerID();

    // Copy data from stub.
    characterChoice->accountID = accountID;
    characterChoice->containerID = characterStub->iContainerID;
    characterChoice->savedName = strdup_special(characterStub->savedName, CRYPTIC_CHURN_HEAP);

    characterChoice->createdTime = characterStub->createdTime;
    characterChoice->level = characterStub->level;
    characterChoice->fixupVersion = characterStub->fixupVersion;
    characterChoice->lastPlayedTime = characterStub->lastPlayedTime;
    characterChoice->virtualShardID = characterStub->virtualShardId;
    characterChoice->pubAccountName = characterStub->pubAccountName ? strdup_special(characterStub->pubAccountName, CRYPTIC_CHURN_HEAP) : NULL;
    characterChoice->privAccountName = characterStub->privAccountName ? strdup_special(characterStub->privAccountName, CRYPTIC_CHURN_HEAP) : NULL;
    characterChoice->extraData1 = characterStub->extraData1 ? strdup_special(characterStub->extraData1, CRYPTIC_CHURN_HEAP) : NULL;
    characterChoice->extraData2 = characterStub->extraData2 ? strdup_special(characterStub->extraData2, CRYPTIC_CHURN_HEAP) : NULL;
    characterChoice->extraData3 = characterStub->extraData3 ? strdup_special(characterStub->extraData3, CRYPTIC_CHURN_HEAP) : NULL;
    characterChoice->extraData4 = characterStub->extraData4 ? strdup_special(characterStub->extraData4, CRYPTIC_CHURN_HEAP) : NULL;
    characterChoice->extraData5 = characterStub->extraData5 ? strdup_special(characterStub->extraData5, CRYPTIC_CHURN_HEAP) : NULL;

    return characterChoice;
}

// This remote command returns basic information on the player's characters.
// It only uses readily available information so it can return immediately (does not need to be a slow remote command).
// If does not unpack containers or do other CPU intensive operations.
AUTO_COMMAND_REMOTE ACMD_CATEGORY(ObjectDB) ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
dbLogin2_GetCharacterChoices(Login2InterShardDestination *returnDestination, U32 accountID, bool forcePopulateDeletedChoiceLists)
{
    Login2CharacterChoices *characterChoices;
    ContainerID *eaIDs = NULL;
    int i;
    Container *con = NULL;

	// If the ObjectDB is not ready, just drop the command
	if (!gAcceptLoginRemoteCommands)
		return;

    // If we don't know where to send the response, then just give up.
    if ( returnDestination == NULL || returnDestination->shardName == NULL || accountID == 0 )
    {
        return;
    }

    characterChoices = StructCreate(parse_Login2CharacterChoices);
    characterChoices->accountID = accountID;
    eaIndexedEnable(&characterChoices->characterChoices, parse_Login2CharacterChoice);

	// Get an array of the container headers for the characters belonging to this account.
	eaIDs = GetContainerIDsFromAccountID(accountID);
    for ( i = ea32Size(&eaIDs) - 1; i >= 0; i-- )
    {
		con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true);

		if (con)
		{
			eaPush(&characterChoices->characterChoices, CharacterChoiceFromContainer(con, false));
			objUnlockContainer(&con);
		}
    }
	ea32ClearFast(&eaIDs);

    // Get the AccountStub container for this account.
    con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

    if (con)
	{
		AccountStub *accountStub = con->containerData;

        if(accountStub)
        {
            // Add any offline characters that have not already been restored to the character choices list.
            for ( i = eaSize(&accountStub->eaOfflineCharacters) - 1; i >= 0; i-- )
            {
                CharacterStub *characterStub = accountStub->eaOfflineCharacters[i];

                if ( characterStub && !characterStub->restored )
                {
                    // Only add the deleted character to the character choices if it is not already there.  This ensures
                    //  that if there are both deleted and non-deleted versions we take the non-deleted one.
                    if ( eaIndexedGetUsingInt(&characterChoices->characterChoices, characterStub->iContainerID ) == NULL )
                    {
                        eaPush(&characterChoices->characterChoices, CharacterChoiceFromStub(characterStub, accountID));
                    }
                    else
                    {
                        Errorf("Offline character with duplicate ID of online character.");
                        // XXX - ask Jon if we should alert here, or if the above check is even necessary.
                    }
                }
            }
        }
		
		objUnlockContainer(&con);
    }

    if ( forcePopulateDeletedChoiceLists || dbPopulateDeletedChoiceLists )
    {
        // Get an array of the container headers for the deleted characters belonging to this account.
        eaIDs = GetDeletedContainerIDsFromAccountID(accountID);

        for ( i = ea32Size(&eaIDs) - 1; i >= 0; i-- )
        {
			con = objGetDeletedContainerEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true);

			if (con)
			{
				// Only add the deleted character to the character choices if it is not already there.  This ensures
				//  that if there are both deleted and non-deleted versions we take the non-deleted one.
				if ( eaIndexedGetUsingInt(&characterChoices->characterChoices, con->header->containerId) == NULL )
				{
					eaPush(&characterChoices->characterChoices, CharacterChoiceFromContainer(con, true));
				}
				else
				{
					Errorf("Deleted character with duplicate ID of online or offline character.");
					// XXX - ask Jon if we should alert here, or if the above check is even necessary.
				}

				objUnlockContainer(&con);
			}
        }
	}
	ea32Destroy(&eaIDs);

    // Return the results.
    RemoteCommand_Intershard_aslLogin2_ReturnCharacterChoices(returnDestination->shardName, returnDestination->serverType, returnDestination->serverID, accountID, returnDestination->requestToken, characterChoices);

    StructDestroy(parse_Login2CharacterChoices, characterChoices);

    return;
}

static void
DBGetCharacterDetailComplete(DBGetCharacterDetailState *getDetailState)
{
    if ( getDetailState->failed )
    {
        log_printf(LOG_LOGIN, "Failed to get character detail for %u@%u. %s", getDetailState->playerID, getDetailState->accountID, getDetailState->errorString);
    }

    // Return results to the server tha called us.
    RemoteCommand_Intershard_aslLogin2_ReturnCharacterDetail(getDetailState->returnDestination->shardName, getDetailState->returnDestination->serverType,
        getDetailState->returnDestination->serverID, getDetailState->playerID, getDetailState->detailReturn, getDetailState->returnDestination->requestToken, getDetailState->failed, getDetailState->errorString);

    // Clean up state.
    StructDestroy(parse_DBGetCharacterDetailState, getDetailState);
}

void
dbLogin2_UnpackCharacter(GWTCmdPacket *packet, DBGetCharacterDetailState *getDetailState)
{
    Container *con;
	int i, size;
    static char *s_value;

    getDetailState->detailReturn = StructCreate(parse_Login2CharacterDetailDBReturn);
    // Get the player container.  Note - does not handle deleted containers.
    con = objGetContainer(GLOBALTYPE_ENTITYPLAYER, getDetailState->playerID);
    if ( con == NULL || con->containerData == NULL )
    {
        getDetailState->failed = true;
        estrConcatf(&getDetailState->errorString, "dbLogin2_GetCharacterDetail: unable to find EntityPlayer containerID %u\n", getDetailState->playerID);

        DBGetCharacterDetailComplete(getDetailState);
        return;
    }

	estrHeapCreate(&getDetailState->detailReturn->playerCharacterString, con->fileSizeHint ? con->fileSizeHint : 1024*1024, CRYPTIC_CHURN_HEAP);
    // Write the container as a string.
    if ( !ParserWriteText(&getDetailState->detailReturn->playerCharacterString, con->containerSchema->classParse, con->containerData, 0, TOK_SUBSCRIBE | TOK_PERSIST, 0) )
    {
        getDetailState->failed = true;
        estrConcatf(&getDetailState->errorString, "dbLogin2_GetCharacterDetail: failed to parser write EntityPlayer containerID %u\n", getDetailState->playerID);

        DBGetCharacterDetailComplete(getDetailState);
        return;
    }

	if ( getDetailState->returnActivePuppets )
	{
		ParseTable *puppetParseTable;
		void **puppetArray;

		if ( !objPathGetEArray(".pSaved.pPuppetMaster.ppPuppets", con->containerSchema->classParse, con->containerData, &puppetArray, &puppetParseTable) )
		{
			getDetailState->failed = true;
			estrConcatf(&getDetailState->errorString, "dbLogin2_GetCharacterDetail: failed to get puppet array for EntityPlayer containerID %u\n", getDetailState->playerID);

			DBGetCharacterDetailComplete(getDetailState);
			return;
		}

		for ( i = 0, size = eaSize(&puppetArray); i < size; i++ )
		{
			bool isActive;
			GlobalType puppetType;
			ContainerID puppetID;

			estrClear(&s_value);

			if ( !objPathGetEString(".eState", puppetParseTable, puppetArray[i], &s_value) )
			{
				// Failed, but keep looking for other puppets.
				continue;
			}
			isActive = (stricmp(s_value, "Active") == 0);

			if ( isActive )
			{
				char *puppetDetailString = NULL;
				ContainerRef *puppetRef;

				// Get the puppet container ID.
				estrClear(&s_value);
				if ( !objPathGetEString(".curId", puppetParseTable, puppetArray[i], &s_value) )
				{
					// Failed, but keep looking for other puppets.
					continue;
				}
				puppetID = atoi(s_value);

				// Get the puppet container type.
				estrClear(&s_value);
				if ( !objPathGetEString(".curType", puppetParseTable, puppetArray[i], &s_value) )
				{
					// Failed, but keep looking for other puppets.
					continue;
				}
				puppetType = NameToGlobalType(s_value);

				// Get the puppet container.
				if ( !objDoesContainerExist(puppetType, puppetID) )
				{
					// Failed, but keep looking for other puppets.
					continue;
				}

				puppetRef = StructCreate(parse_ContainerRef);
				puppetRef->containerType = puppetType;
				puppetRef->containerID = puppetID;

				eaPush(&getDetailState->puppetRefs, puppetRef);
			}
		}
    }

	if ( getDetailState->returnTeamRequestPets )
	{
		int *petIDArray;

		if ( !objPathGetEArrayInt(".pSaved.ppPreferredPetIDs", con->containerSchema->classParse, con->containerData, &petIDArray) )
		{
			getDetailState->failed = true;
			estrConcatf(&getDetailState->errorString, "dbLogin2_GetCharacterDetail: failed to get PreferredPetIDs for EntityPlayer containerID %u\n", getDetailState->playerID);

			DBGetCharacterDetailComplete(getDetailState);
			return;
		}

		for ( i = 0, size = eaiSize(&petIDArray); i < size; i++ )
		{
			// Pets are implicitly GLOBALTYPE_ENTITYSAVEDPET
			GlobalType petType = GLOBALTYPE_ENTITYSAVEDPET;
			ContainerID petID = petIDArray[i];
			ContainerRef *petRef;

			// Get the pet container.
			if ( !objDoesContainerExist(petType, petID) )
			{
				// Failed, but keep looking for other pets.
				continue;
			}

			petRef = StructCreate(parse_ContainerRef);
			petRef->containerType = petType;
			petRef->containerID = petID;

			eaPush(&getDetailState->petRefs, petRef);
		}
	}

	if(eaSize(&getDetailState->puppetRefs) || eaSize(&getDetailState->petRefs))
	{
		if(GenericDatabaseThreadIsActive())
			QueueDBLogin2UnpackPetsOnMainThread(packet, getDetailState);
		else
			dbLogin2_UnpackPets(getDetailState);
	}
	else
	{
	    DBGetCharacterDetailComplete(getDetailState);
	}
}

void dbLogin2_UnpackPetsInternal( ContainerRef ***peaRefs, STRING_EARRAY *peaOut ) 
{
	int i;
	int numPuppets = eaSize(peaRefs);
	for(i = 0; i < numPuppets; ++i)
	{
		char *puppetDetailString = NULL;
		ContainerRef *puppetRef = (*peaRefs)[i];
		Container *puppet = objGetContainer(puppetRef->containerType, puppetRef->containerID);

		if ( puppet == NULL || puppet->containerData == NULL )
		{
			// Failed, but keep looking for other puppets.
			continue;
		}

		estrHeapCreate(&puppetDetailString, puppet->fileSizeHint ? puppet->fileSizeHint : 1024*1024, CRYPTIC_CHURN_HEAP);
		// Write the active puppet container as a string.
		if ( !ParserWriteText(&puppetDetailString, puppet->containerSchema->classParse, puppet->containerData, 0, TOK_SUBSCRIBE | TOK_PERSIST, 0) )
		{
			// Failed to write the puppet, but keep looking for other puppets.
			estrDestroy(&puppetDetailString);
			puppetDetailString = NULL;
			continue;
		}

		if ( puppetDetailString )
		{
			// Save the encoded puppet string for return to the login server.
			eaPush(peaOut, puppetDetailString);
		}
	}
}

// This happens in the background thread. The GWT gives us a double pointer to the detail state
void
dbLogin2_UnpackPets(DBGetCharacterDetailState *getDetailState)
{
	dbLogin2_UnpackPetsInternal(&getDetailState->puppetRefs, &getDetailState->detailReturn->activePuppetStrings);
	dbLogin2_UnpackPetsInternal(&getDetailState->petRefs, &getDetailState->detailReturn->teamRequestPetStrings);
    DBGetCharacterDetailComplete(getDetailState);
}

void dbLogin2_QueueUnpackPets(DBGetCharacterDetailState *getDetailState)
{
	QueueDBLogin2UnpackPetsOnGenericDatabaseThreads(getDetailState, getDetailState->puppetRefs, getDetailState->petRefs);
}

DBGetCharacterDetailState **characterDetailStatePending;

static void
CharacterRestoreCB(TransactionReturnVal *returnVal, ContainerRestoreRequest *containerRequest)
{
    DBGetCharacterDetailState *getDetailState;

    if ( containerRequest == NULL || containerRequest->callbackData.appData == NULL )
    {
        // If we can't find the state data there is nothing for us to do.
        return;
    }

	// getDetailState is destroyed by dbLogin2_UnpackCharacter or DBGetCharacterDetailComplete
    getDetailState = containerRequest->callbackData.appData;
	containerRequest->callbackData.appData = NULL;

    // XXX - need to clean up containerRequest here.

    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
		// Add the detail state to an array, so that we can wait until the dbUpdateContainer command is finished.
		eaPush(&characterDetailStatePending, getDetailState);
    }
    else
    {
        getDetailState->failed = true;
        estrConcatf(&getDetailState->errorString, "dbLogin2_GetCharacterDetail: container restore failed for EntityPlayer containerID %u\n", getDetailState->playerID);
        DBGetCharacterDetailComplete(getDetailState);
    }
	CleanUpContainerRestoreRequest(&containerRequest);
}

void LoginCharacterRestoreTick(void)
{
	PERFINFO_AUTO_START_FUNC();
	// Walking backwards so that we can use eaRemoveFast
	FOR_EACH_IN_EARRAY(characterDetailStatePending, DBGetCharacterDetailState, getDetailState);
	{
		Container *con = objGetContainer(GLOBALTYPE_ENTITYPLAYER, getDetailState->playerID);
		if(con && con->containerData)
		{
			if(GenericDatabaseThreadIsActive())
				QueueDBLogin2UnpackCharacterOnGenericDatabaseThreads(getDetailState, getDetailState->playerID);
			else
				dbLogin2_UnpackCharacter(NULL, getDetailState);
			eaRemoveFast(&characterDetailStatePending, igetDetailStateIndex);
		}
	}
	FOR_EACH_END;
	PERFINFO_AUTO_STOP();
}

// This function will online the requested player if it is currently offline.
// Returns true if the character is being onlined.
// Returns false if the character is already online or cannot be found.
static bool
OnlineCharacter(U32 accountID, U32 playerID, TransactionReturnCallback cbFunc, void *userData)
{
    Container *con = NULL;

    // Get the AccountStub container for this account.
    con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

    if(con)
    {
	    AccountStub *accountStub = con->containerData;
        if ( accountStub )
        {
			int index;
			CharacterStub *characterStub;
			if((index = GetCharacterStubIndex(accountStub, playerID)) == -1)
			{ 
				objUnlockContainer(&con);
				return false;
			}
			
			// Found the character.
            characterStub = accountStub->eaOfflineCharacters[index];
            if ( !characterStub->restored )
            {
                // Set up the restore request.
				RestoreCallbackData callbackData = {0};
                callbackData.cbFunc = cbFunc;
                callbackData.appData = userData;

                // Character has not been previously restored, so we need to restore it now.
				AutoRestoreEntityPlayerEx(accountID, playerID, &callbackData);
				RestoreAccountWideContainersForAccountStub(accountStub, false);

				objUnlockContainer(&con);
                return true;
            }
        }
		objUnlockContainer(&con);
    }

    return false;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
dbLogin2_GetCharacterDetail(ACMD_OWNABLE(Login2InterShardDestination) ppReturnDestination, U32 accountID, U32 playerID, bool returnActivePuppets, bool returnTeamRequestPets)
{
    DBGetCharacterDetailState *getDetailState;

	// If the ObjectDB is not ready to accept logins, drop the remote command.
	if (!gAcceptLoginRemoteCommands)
		return;

    getDetailState = StructCreate(parse_DBGetCharacterDetailState);
    getDetailState->accountID = accountID;
    getDetailState->playerID = playerID;
    getDetailState->returnActivePuppets = returnActivePuppets;
	getDetailState->returnTeamRequestPets = returnTeamRequestPets;

    // Take ownership of the destination struct.
    getDetailState->returnDestination = *ppReturnDestination;
    *ppReturnDestination = NULL;

    if ( !OnlineCharacter(accountID, playerID, CharacterRestoreCB, getDetailState) )
    {
        // The character is not being onlined, so it should be present and we can continue immediately.
		if(GenericDatabaseThreadIsActive())
			QueueDBLogin2UnpackCharacterOnGenericDatabaseThreads(getDetailState, getDetailState->playerID);
		else
			dbLogin2_UnpackCharacter(NULL, getDetailState);
    }
}

static void
OnlineCharacterCB(TransactionReturnVal *returnVal, ContainerRestoreRequest *containerRequest)
{
    DBOnlineCharacterState *onlineState;

    if ( containerRequest == NULL || containerRequest->callbackData.appData == NULL )
    {
        // Can't find callback data.
        return;
    }

    onlineState = containerRequest->callbackData.appData;

    SlowRemoteCommandReturn_dbLogin2_OnlineCharacter(onlineState->commandID, returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);
    StructDestroy(parse_DBOnlineCharacterState, onlineState);
}

// This command can be used to online a player character.
// Will return true if the character is not offlined, or is onlined successfully.
AUTO_COMMAND_REMOTE_SLOW(bool) ACMD_IFDEF(APPSERVER);
void
dbLogin2_OnlineCharacter(SlowRemoteCommandID commandID, U32 accountID, U32 playerID)
{
    DBOnlineCharacterState *onlineState = StructCreate(parse_DBOnlineCharacterState);
    onlineState->commandID = commandID;

    if ( !OnlineCharacter(accountID, playerID, OnlineCharacterCB, onlineState) )
    {
        // The character is not being onlined, so we can return immediately.
        SlowRemoteCommandReturn_dbLogin2_OnlineCharacter(commandID, true);
        StructDestroy(parse_DBOnlineCharacterState, onlineState);
    }
}

void dbLogin2_EnableLoginRemoteCommands(bool enable)
{
	gAcceptLoginRemoteCommands = enable;
}

#include "AutoGen/dbLogin2_h_ast.c"