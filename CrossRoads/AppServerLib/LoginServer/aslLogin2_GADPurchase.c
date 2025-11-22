#include "aslLogin2_GADPurchase.h"
#include "aslLogin2_IntershardCommands.h"
#include "aslLogin2_Error.h"
#include "aslLogin2_Util.h"
#include "aslLoginServer.h"
#include "aslLogin2_StateMachine.h"

#include "TransactionOutcomes.h"
#include "objTransactions.h"
#include "GlobalTypeEnum.h"
#include "StringCache.h"
#include "timing.h"
#include "accountnet.h"
#include "AccountProxyCommon.h"
#include "microtransactions_common.h"
#include "LoggedTransactions.h"
#include "inventoryCommon.h"

#include "AutoGen/aslLogin2_IntershardCommands_h_ast.h"
//#include "AutoGen/aslLogin2_RenameCharacter_c_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

typedef struct GADMakeNumericPurchaseData
{
	U64 userData;
	U32 uEntID;
	char pchPurchaseDefName[MAX_PATH];
	GADPurchaseCB cbFunc;

} GADMakeNumericPurchaseData;

static void GADMakeNumericPurchase_CB(TransactionReturnVal* pReturn, GADMakeNumericPurchaseData* cbData)
{
	Packet* pClientPack;
	Login2State *loginState;
	bool success = false;

	loginState = aslLogin2_GetActiveLoginState(cbData->userData);

	if ( !aslLogin2_ValidateLoginState(loginState) )
	{
		aslLogin2_Log("Command %s returned with invalid loginState.  loginCookie=%p", __FUNCTION__, cbData->userData);
		return;
	}

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		success = true;
	}

	pClientPack = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_GAME_ACCOUNT_NUMERIC_PURCHASE_RESULT);
	pktSendBits(pClientPack, 1, success);
	pktSendU32(pClientPack, cbData->uEntID);
	pktSendString(pClientPack, cbData->pchPurchaseDefName);
	pktSend(&pClientPack);

	// Notify the caller that we are done.
	if( cbData->cbFunc )
	{
		(* cbData->cbFunc)(success, cbData->userData);
	}

	SAFE_FREE(cbData);

}

void aslLogin2_GADPurchase(ContainerID playerID, ContainerID accountID, const char *gadPurchaseDef, GADPurchaseCB cbFunc, U64 userData)
{
	
	if (gadPurchaseDef)
	{
		GADMakeNumericPurchaseData* cbData = calloc(1, sizeof(GADMakeNumericPurchaseData));
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(GADMakeNumericPurchase_CB, cbData);
		ItemChangeReason reason = {0};

		cbData->userData = userData;
		cbData->uEntID = playerID;
		cbData->cbFunc = cbFunc;
		strcpy(cbData->pchPurchaseDefName, gadPurchaseDef);
		inv_FillItemChangeReason(&reason, NULL, "GAD:PurchaseWithNumerics", gadPurchaseDef);
		AutoTrans_GameAccount_tr_EntNumericPurchase(pReturn, 
			GLOBALTYPE_LOGINSERVER, 
			GLOBALTYPE_ENTITYPLAYER, 
			playerID, 
			GLOBALTYPE_GAMEACCOUNTDATA, 
			accountID,
			gadPurchaseDef,
			&reason);
	}
}

