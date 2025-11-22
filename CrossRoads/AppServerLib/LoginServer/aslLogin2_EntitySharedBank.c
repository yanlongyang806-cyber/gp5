#include "aslLogin2_EntitySharedBank.h"
#include "aslLogin2_Error.h"
#include "ServerLib.h"
#include "AppServerLib.h"
#include "UtilitiesLib.h"
#include "textparser.h"
#include "stdtypes.h"
#include "objTransactions.h"
#include "GameAccountDataCommon.h"
#include "aslLoginCharacterSelect.h"
#include "accountnet.h"
#include "MicroTransactions.h"
#include "ShardCluster.h"
#include "ShardCommon.h"
#include "Player.h"
#include "Entity.h"
#include "GamePermissionsCommon.h"
#include "itemCommon.h"
#include "LoggedTransactions.h"
#include "AccountProxyCommon.h"
#include "MailCommon.h"
#include "inventoryCommon.h"


#include "AutoGen/aslLogin2_EntitySharedBank_c_ast.h"
#include "Autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "autogen/MailCommon_h_ast.h"

extern ParseTable parse_EmailV3[];
extern ParseTable parse_EmailV3Message[];

AUTO_STRUCT;
typedef struct EntitySharedBankState
{
	ContainerID accountID;

	bool containerCheckComplete;
	bool failed;

	U32 timeStarted;

	// String used to record errors for logging.
	STRING_MODIFIABLE errorString;          AST(ESTRING)

	// Completion callback data.
	EntitySharedBankCB cbFunc;				NO_AST
	void *userData;							NO_AST
} EntitySharedBankState;

static Entity *EntitySharedBankCreateInit(void)
{
	static NOCONST(Entity) *prototypeEntityData = NULL;

	// comment this out until we decide exactly how we want to flag the game account data
	if(!prototypeEntityData)
	{
		prototypeEntityData = StructCreateWithComment(parse_Entity, "Entity Shared Bank creation init");

		prototypeEntityData->pEmailV3 = StructCreateNoConst(parse_EmailV3);
		prototypeEntityData->pEmailV3->eaMessages = NULL;
		prototypeEntityData->pEmailV3->iMessageCount = 0;
		prototypeEntityData->pEmailV3->iAttachmentsCount = 0;
		eaIndexedEnableNoConst(&prototypeEntityData->pEmailV3->eaMessages, parse_EmailV3Message);

		prototypeEntityData->myEntityType = GLOBALTYPE_ENTITYSHAREDBANK;

		gameSpecific_EntitySharedBankExtraData(prototypeEntityData);

	}

	return (Entity *)prototypeEntityData;

}

// Handle completion of GameAccountData container creation/verification.  Start the actual refresh if other data is ready.
static void
	EntitySharedBankContainerExistsCB(TransactionReturnVal *returnVal, EntitySharedBankState *refreshState)
{
	refreshState->containerCheckComplete = true;
	if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
	{
		estrConcatf(&refreshState->errorString, "Failed to create or verify EntitySharedBank container %d.", refreshState->accountID);
		refreshState->failed = true;
	}

	if ( refreshState->failed )
	{
		aslLogin2_Log("EntitySharedBank Container refresh failed for accountID %d. %s", refreshState->accountID, NULL_TO_EMPTY(refreshState->errorString));
	}
	else
	{
		aslLogin2_Log("EntitySharedBank Container refresh succeeded for accountID %d", refreshState->accountID);
	}

	// Notify the caller that we are done.
	if ( refreshState->cbFunc )
	{
		(* refreshState->cbFunc)(refreshState->accountID, 
			!refreshState->failed, 
			refreshState->userData);
	}

	// Clean up state.
	StructDestroy(parse_EntitySharedBankState, refreshState);

}

// This creates the EntiySharedBank container if it doesn't already exist.
static void EnsureEntitySharedBankExists(ContainerID accountID, TransactionReturnCallback cbFunc, void *userData)
{
	if( accountID > 0 )
	{
		static char *diffString = NULL;
		TransactionRequest *request = objCreateTransactionRequest();
		Entity *pEntSharedBank;

		estrClear(&diffString);

		// Call game specific function to create initial game account data.
		pEntSharedBank = EntitySharedBankCreateInit();

		if(pEntSharedBank)
		{
			StructTextDiffWithNull_Verify(&diffString, parse_Entity, pEntSharedBank, NULL, 0, TOK_PERSIST, 0, 0);
		}

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"VerifyOrCreateAndInitContainer containerIDVar %s %d %s",
			GlobalTypeToName(GLOBALTYPE_ENTITYSHAREDBANK),
			accountID,
			diffString);

		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
			objCreateManagedReturnVal(cbFunc, userData), "EnsureGameContainerExists", request);

		objDestroyTransactionRequest(request);
	}
}

void aslLogin2_CheckEntitySharedBank(ContainerID accountID, EntitySharedBankCB cbFunc, void *userData)
{
	EntitySharedBankState *refreshState = StructCreate(parse_EntitySharedBankState);

	refreshState->accountID = accountID;
	refreshState->timeStarted = timeSecondsSince2000();
	refreshState->cbFunc = cbFunc;
	refreshState->userData = userData;

	// If the EntitySharedBank container doesn't exist, then create it.
	EnsureEntitySharedBankExists(accountID, EntitySharedBankContainerExistsCB, refreshState);
}

#include "AutoGen/aslLogin2_EntitySharedBank_c_ast.c"