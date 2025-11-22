/***************************************************************************



***************************************************************************/

#ifndef GSL_MAILNPC_H
#define GSL_MAILNPC_H

typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Item) NOCONST(Item);
typedef struct Item Item;
typedef struct ChatMailStruct ChatMailStruct;
typedef struct NPCMailRequest NPCMailRequest;
typedef U32 ContainerID;

AUTO_STRUCT;
typedef struct NpcMailCB
{
	ContainerID entId;

}NpcMailCB;

void SyncNPCEMail(Entity *pEntity);
void gslMailNPC_AddMail(Entity *pEntity,const char *fromName, const char *subject, const char *body);
void gslMailNPC_AddMailWithItemDef(Entity *pEntity,const char *fromName, const char *subject, const char *body, const char* ItemDefName, U32 uQuantity, U32 uFutureTimeSeconds);
void gslMailNPC_AddMailRequest(Entity* pEntity, NPCMailRequest* request);
bool gslMailNPC_BuildChatMailStruct(Entity *pEntity, S32 iMailIndex, ChatMailStruct *pMail);
void gslMailNPC_MarkRead(Entity *pEntity, U32 uIDNpc, U32 bRead);

void gslMailNPC_DeleteMailCompleteLog(Entity *pEnt, U32 uMailID, S32 iNPCEMailID, U32 uAuctionLotID);


#endif	// GSL_MAILNPC_H
