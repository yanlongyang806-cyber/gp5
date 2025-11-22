#ifndef GSL_ENTITYMAILCOMMON_H
#define GSL_ENTITYMAILCOMMON_H

typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Item) NOCONST(Item);
typedef struct NOCONST(AuctionLot) NOCONST(AuctionLot);
typedef struct Item Item;
typedef struct ItemChangeReason ItemChangeReason;
typedef enum NPCEmailType NPCEmailType;
typedef U32 ContainerID;

AUTO_STRUCT;
typedef struct MailCharacterItems
{
	EARRAY_OF(Item) eaMailItems;
}MailCharacterItems;

void EntityMail_AddItemCleanupFromError(ATR_ARGS, ContainerID iContainerID, ATH_ARG MailCharacterItems *pItems);
MailCharacterItems *CharacterMailAddItem(MailCharacterItems *pItems, Item *pItem);
MailCharacterItems *CharacterMailAddItemsFromAuctionLot(MailCharacterItems *pItems, NOCONST(AuctionLot) *pLot);
bool EntityMail_trh_NPCAddMail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char *fromName, const char *subject, const char *body,
	MailCharacterItems *pCharacterItems, U32 uFutureTimeSeconds, NPCEmailType eType, const ItemChangeReason* pReason);

void EntityMail_trh_CheckCurrencySum(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pchCurrencyName);
void gslMailNPC_trh_UpdateUnreadMailFlag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEntity);

extern S64 g_NPCMailCurrencyAlertThreshold;

#endif	// GSL_ENTITYMAILCOMMON_H
