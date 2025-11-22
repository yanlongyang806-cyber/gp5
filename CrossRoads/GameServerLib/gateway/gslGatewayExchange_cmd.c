
#include "Entity.h"
#include "Player.h"
#include "EntitySavedData.h"
#include "stdtypes.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "cmdparse.h"
#include "CurrencyExchangeCommon.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayExchange_WithdrawOrder) ACMD_LIST(gGatewayCmdList);
void GatewayExchange_WithdrawOrder(Entity *pEnt, U32 orderID)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
		RemoteCommand_aslCurrencyExchange_WithdrawOrder(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, GetAppGlobalID(), pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, orderID, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayExchange_ClaimTC) ACMD_LIST(gGatewayCmdList);
void GatewayExchange_ClaimTC(Entity *pEnt, U32 quantity)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
		RemoteCommand_aslCurrencyExchange_ClaimTC(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, GetAppGlobalID(), pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, quantity, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayExchange_ClaimMTC) ACMD_LIST(gGatewayCmdList);
void GatewayExchange_ClaimMTC(Entity *pEnt, U32 quantity)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
		RemoteCommand_aslCurrencyExchange_ClaimMTC(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, GetAppGlobalID(), pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, quantity, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayExchange_CreateBuyOrder) ACMD_LIST(gGatewayCmdList);
void GatewayExchange_CreateBuyOrder(Entity *pEnt, U32 quantity, U32 price)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
		RemoteCommand_aslCurrencyExchange_CreateOrder(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, GetAppGlobalID(), pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, OrderType_Buy, quantity, price, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayExchange_CreateSellOrder) ACMD_LIST(gGatewayCmdList);
void GatewayExchange_CreateSellOrder(Entity *pEnt, U32 quantity, U32 price)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
		RemoteCommand_aslCurrencyExchange_CreateOrder(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, GetAppGlobalID(), pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, OrderType_Sell, quantity, price, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}