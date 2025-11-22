/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "gslGatewaySession.h"

#include "entity.h"
#include "player.h"

#include "NumericConversionCommon.h"
#include "inventoryCommon.h"
#include "GameAccountDataCommon.h"
#include "LoggedTransactions.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(gateway_SetHidden) ACMD_LIST(gGatewayCmdList) ACMD_GLOBAL;
void gateway_SetHidden(Entity *e, U32 bHidden)
{
	if(e && e->pPlayer && e->pPlayer->pGatewayInfo)
	{
		AutoTrans_gateway_tr_SetHidden(NULL, GetAppGlobalType(), entGetType(e), entGetContainerID(e), bHidden);
	}
}