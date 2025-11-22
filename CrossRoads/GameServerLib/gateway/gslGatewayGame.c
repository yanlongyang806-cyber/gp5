#include "gslGatewayGame.h"

#include "objTransactions.h"
#include "ResourceManager.h"
#include "inventoryCommon.h"
#include "Entity.h"
#include "Reward.h"
#include "itemCommon.h"
#include "GameAccountDataCommon.h"

#include "gslGatewayServer.h"

#include "AutoGen/gslGatewayGame_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/itemCommon_h_ast.h"

typedef struct GatewayGameCBData {
	U32 iContainerID;
}GatewayGameCBData;

static U32 *s_uIDCreateRequests = NULL;


static void GatewayGameDataDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, GatewayGameData* p, void *userData)
{
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER && p)
	{
		gslGatewayServer_ContainerSubscriptionUpdate(eType, GLOBALTYPE_GATEWAYGAMEDATA, p->iContainerID);
	}
}

void gslGatewayGame_SchemaInit(void)
{
	// set up schema and copy dictionary for diary container references
	objRegisterNativeSchema(GLOBALTYPE_GATEWAYGAMEDATA, parse_GatewayGameData, NULL, NULL, NULL, NULL, NULL);

	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GATEWAYGAMEDATA), false, parse_GatewayGameData, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GATEWAYGAMEDATA), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GATEWAYGAMEDATA));
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GATEWAYGAMEDATA), GatewayGameDataDictionaryChangeCB, NULL);

}

void gslGatewayGame_GrantRewardTable(Entity *pEntity, const char *pchRewardTable, U32 iTier, U64 *piCritterIDs)
{
	RewardTable *pRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict,pchRewardTable);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	InventoryBagGroup rewardBags = {0};

	if (pEntity && reward_PowerExec_GenerateBags(pEntity, pRewardTable, entity_CalculateExpLevelSlow(pEntity), 0, iTier, NULL, &rewardBags.eaBags))
	{
		int i;
		ItemIDList sIDs = { 0 };
		ItemChangeReason sReason = {0};

		sReason.pcReason = "GatewayGameGrantRewards";

		for(i=0;i<ea64Size(&piCritterIDs);i++)
		{
			ItemIDContainer *pContainer = StructCreate(parse_ItemIDContainer);

			pContainer->uiID = piCritterIDs[i];

			eaPush(&sIDs.ppIDs,pContainer);
		}

		AutoTrans_GatewayGame_GrantRewards(NULL,GLOBALTYPE_GATEWAYSERVER, GLOBALTYPE_ENTITYPLAYER, pEntity->myContainerID, GLOBALTYPE_GATEWAYGAMEDATA, pEntity->myContainerID, &rewardBags, &sIDs, &sReason, pExtract);

		eaDestroyStruct(&sIDs.ppIDs,parse_ItemIDContainer);

		eaDestroyStruct(&rewardBags.eaBags, parse_InventoryBag);
	}
}

void gslGatewayGame_QueueRewardTable(Entity *pEntity, const char *pchRewardTable, U32 iTier)
{
	RewardTable *pRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict,pchRewardTable);
	InventoryBagGroup rewardBags = {0};

	if (pEntity && reward_PowerExec_GenerateBags(pEntity, pRewardTable, entity_CalculateExpLevelSlow(pEntity), 0, iTier, NULL, &rewardBags.eaBags))
	{
		AutoTrans_GatewayGame_QueueRewardBag(NULL,GLOBALTYPE_GATEWAYSERVER, GLOBALTYPE_GATEWAYGAMEDATA, pEntity->myContainerID, &rewardBags);
	}
}

void gslGatewayGame_ClaimRewards(Entity *pEnt, U64 *piCritterIDs)
{
	int i;
	ItemChangeReason sReason = {0};
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemIDList sIDs = { 0 };

	if(!pEnt)
		return;

	sReason.pcReason = "GatewayGameClaimRewards";

	for(i=0;i<ea64Size(&piCritterIDs);i++)
	{
		ItemIDContainer *pContainer = StructCreate(parse_ItemIDContainer);

		pContainer->uiID = piCritterIDs[i];

		eaPush(&sIDs.ppIDs,pContainer);
	}

	AutoTrans_GatewayGame_ClaimQueuedRewardBag(NULL,GLOBALTYPE_GATEWAYSERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,GLOBALTYPE_GATEWAYGAMEDATA,pEnt->myContainerID,&sReason,pExtract,&sIDs);

	eaDestroyStruct(&sIDs.ppIDs,parse_ItemIDContainer);
}

void gslGatewayGame_DiscardRewards(Entity *pEnt)
{
	if(pEnt)
		AutoTrans_GatewayGame_DiscardQueuedRewardBag(NULL,GLOBALTYPE_GATEWAYSERVER,GLOBALTYPE_GATEWAYGAMEDATA,pEnt->myContainerID);
}

static void CreateGatewayContainer_CB(TransactionReturnVal *pReturn, GatewayGameCBData *cbData)
{
	ea32BFindAndRemove(&s_uIDCreateRequests,cbData->iContainerID);

	free(cbData);
}

void gslGatewayGame_CreateContainerForID(U32 uContainerID)
{
	if(uContainerID && ea32Find(&s_uIDCreateRequests,uContainerID)==-1)
	{
		NOCONST(GatewayGameData) *pData = StructCreateNoConst(parse_GatewayGameData);
		TransactionReturnVal *pReturn;
		GatewayGameCBData *cbData = malloc(sizeof(GatewayGameCBData));
		
		pData->iContainerID = uContainerID;
		 
		cbData->iContainerID = uContainerID;

		pReturn = objCreateManagedReturnVal(CreateGatewayContainer_CB, cbData);

		objRequestContainerVerifyAndSet(pReturn, GLOBALTYPE_GATEWAYGAMEDATA, uContainerID, pData, GLOBALTYPE_OBJECTDB, 0);

		ea32Push(&s_uIDCreateRequests, uContainerID);
	}
}

void gslGatewayGame_SaveState(U32 uContainerID, char *pchState)
{
	AutoTrans_GatewayGame_SaveState(NULL,GLOBALTYPE_GATEWAYSERVER,GLOBALTYPE_GATEWAYGAMEDATA,uContainerID,pchState);
}

#include "AutoGen/gslGatewayGame_h_ast.c"