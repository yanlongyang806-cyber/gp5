#ifndef GSLGATEWAYGAME_H__
#define GSLGATEWAYGAME_H__

#include "stdtypes.h"

typedef struct InventoryBag InventoryBag;
typedef struct Entity Entity;
typedef struct RewardTable RewardTable;
typedef U32 ContainerID;

AUTO_STRUCT AST_CONTAINER;
typedef struct GatewayGameData
{
	const ContainerID iContainerID;								AST(PERSIST SUBSCRIBE KEY)

	CONST_OPTIONAL_STRUCT(InventoryBag) pRewardBag;				AST(PERSIST SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(InventoryBag) pLastRewardBag;			AST(PERSIST SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(InventoryBag) pLastQueuedRewardBag;	AST(PERSIST SUBSCRIBE)

	CONST_STRING_MODIFIABLE pchSaveState;						AST(PERSIST SUBSCRIBE)
}GatewayGameData;

void gslGatewayGame_SchemaInit(void);

void gslGatewayGame_CreateContainerForID(U32 uContainerID);

void gslGatewayGame_GrantRewardTable(Entity *pEntity, const char *pchRewardTable, U32 iTier, U64 *piCritterIDs);
void gslGatewayGame_QueueRewardTable(Entity *pEntity, const char *pchRewardTable, U32 iTier);
void gslGatewayGame_ClaimRewards(Entity *pEnt, U64 *piCritterIDs);
void gslGatewayGame_DiscardRewards(Entity *pEnt);

void gslGatewayGame_SaveState(U32 uContainerID, char *pchState);

#endif
