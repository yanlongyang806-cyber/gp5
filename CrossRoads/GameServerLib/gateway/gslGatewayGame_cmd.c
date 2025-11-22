#include "gslGatewayGame.h"
#include "stdtypes.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "GlobalTypeEnum.h"
#include "rewardCommon.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(Gateway_GiveMeSomeRewards) ACMD_LIST(gGatewayCmdList);
void GatewayGame_GiveMeSomeRewards(Entity *pEnt, const char *pchRewardTable)
{
	U64 *piCritterIDs = NULL;

	ea64Push(&piCritterIDs,17179869256);
	ea64Push(&piCritterIDs,17179869252);

	//gslGatewayGame_GrantRewardTable(pEnt,pchRewardTable,piCritterIDs);

	ea64Destroy(&piCritterIDs);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(Gateway_ClaimAllRewards) ACMD_LIST(gGatewayCmdList);
void GatewayGame_ClaimAllRewards(Entity *pEnt)
{
	ItemChangeReason sReason = {0};
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	sReason.pcReason = "GatewayGameClaimRewards";

	//AutoTrans_GatewayGame_ClaimRewardBag(NULL,GLOBALTYPE_GATEWAYSERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,GLOBALTYPE_GATEWAYGAMEDATA,pEnt->myContainerID,&sReason,pExtract);
}