/***************************************************************************



***************************************************************************/

#include "gslMailNPC.h"
#include "gslmail_old.h"
#include "EntityMailCommon.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "itemTransaction.h"
#include "GameAccountDataCommon.h"
#include "LoggedTransactions.h"
#include "ChatCommonstructs.h"
#include "utilitiesLib.h"
#include "EntityLib.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "entCritter.h"
#include "Player_h_ast.h"
#include "logging.h"
#include "AuctionLot.h"
#include "EntitySavedData.h"
#include "gslLogSettings.h"
#include "NotifyEnum.h"
#include "WebRequests.h"

#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "autogen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/EntityMailCommon_h_ast.h"
#include "autogen/gslMailNPC_c_ast.h"
#include "AutoGen/gslMailNPC_h_ast.h"
#include "autogen/AuctionLot_h_ast.h"
#include "autogen/EntitySavedData_h_ast.h"
#include "AutoGen/WebRequests_h_ast.h"

AUTO_STRUCT;
typedef struct AuctionDeleteCBData
{
	EntityRef erEnt;
	ContainerID iAuctionID;
	
} AuctionDeleteCBData;

// Take items gives items from npc email to the player character's inventory
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pemailv2.Mail, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, .Pplayer.Eaplayernumericthresholds, .Pplayer.Eaastrrecentlyacquiredstickerbookitems")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome gslMailNPC_tr_TakeItems(ATR_ARGS, NOCONST(Entity) *pEnt, 
											   CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
											   U32 uMailIndex,
											   const ItemChangeReason *pReason,
											   GameAccountDataExtract *pExtract)
{
	NOCONST(NPCEMailData)* pMessage;
	ItemChangeReason reason = {0};
	if(ISNULL(pEnt->pPlayer))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity's not a player.");
	}

	if(uMailIndex >= (U32)eaSize(&pEnt->pPlayer->pEmailV2->mail))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Mail index is >= mail entries.");
	}

	pMessage = pEnt->pPlayer->pEmailV2->mail[uMailIndex];

	StructCopyAll(parse_ItemChangeReason, pReason, &reason);

	if (pMessage && pMessage->pReason)
	{
		reason.pcReason = allocAddString(pMessage->pReason);
		reason.pcDetail = allocAddString(pMessage->pDetail);
	}

	while(eaSize(&pMessage->ppItemSlot))
	{
		NOCONST(InventorySlot) *pSlot = eaTail(&pMessage->ppItemSlot);
		Item *pItem = NULL;
		ItemDef *pItemDef;
		InvBagIDs eBagID = InvBagIDs_Inventory;

		ANALYSIS_ASSUME(pSlot != NULL);
		pItem = StructCloneReConst(parse_Item, pSlot->pItem);
		if(ISNULL(pItem))
		{
			break;
		}

		pItemDef = GET_REF(pItem->hItem);

		if (!pItemDef)
		{
			Errorf("Player %s attempted to take item %s from an NPCmail but the def doesn't exist!", pEnt->debugName, REF_STRING_FROM_HANDLE(pItem->hItem));
			TRANSACTION_RETURN_LOG_FAILURE("NPCEmail item transfer failed due to missing def.");
		}

		if (pItemDef && (pItemDef->flags & kItemDefFlag_LockToRestrictBags))
		{
			eBagID = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, CONTAINER_NOCONST(Item, (pItem)), pItem->count, true, pExtract);
		}

		item_trh_ClearPowerIDs(CONTAINER_NOCONST(Item, pItem));
		// note that inv_AddItem no longer destroys pItem (success or fail). If this is moved to co release3 inv_AddItem does
		// destroy pItem therefore be care during merges
		if(inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, eBagID, -1, pItem, pItemDef->pchName, ItemAdd_ClearID, &reason, pExtract) == TRANSACTION_OUTCOME_SUCCESS)
		{
			devassert(eaPop(&pEnt->pPlayer->pEmailV2->mail[uMailIndex]->ppItemSlot) == pSlot);
			StructDestroyNoConst(parse_InventorySlot, pSlot);
			StructDestroy(parse_Item, pItem);
		}
		else
		{
			StructDestroy(parse_Item, pItem);
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail.FailedToTakeItems", kNotifyType_InventoryFull);
			TRANSACTION_RETURN_LOG_FAILURE("NPCEmail item transfer failed during inv_AddItem(). Inventory is likely full.");
			break;
		}
	}
	
	TRANSACTION_RETURN_LOG_SUCCESS("Items transfered.");
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail");
bool gslMailNPC_trh_NPCDeleteMailEntry(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U32 uID)
{
	S32 i, j;
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		return false;
	}

	for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
	{
		if(uID == (U32)pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID)
		{
			NOCONST(NPCEMailData)* pData = pEnt->pPlayer->pEmailV2->mail[i];
			for (j = 0; j < eaSize(&pData->ppItemSlot); j++)
			{
				ItemDef* pDef = SAFE_GET_REF2(pData->ppItemSlot[j], pItem, hItem);
				if (pDef && (pDef->flags & kItemDefFlag_CantDiscard))
				{
					QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, "Mail_CannotDiscard", kNotifyType_MailSendFailed);
					return false;
				}
			}
			StructDestroyNoConst(parse_NPCEMailData, pEnt->pPlayer->pEmailV2->mail[i]);
			eaRemove(&pEnt->pPlayer->pEmailV2->mail, i);
			break;
		}
	}
	
	gslMailNPC_trh_UpdateUnreadMailFlag(ATR_PASS_ARGS, pEnt);

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail");
enumTransactionOutcome gslMailNPC_tr_NPCDeleteMailEntry(ATR_ARGS, NOCONST(Entity)* pEnt, U32 uID)
{
	if(gslMailNPC_trh_NPCDeleteMailEntry(ATR_PASS_ARGS, pEnt, uID))
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"NPC Mail %d deleted", uID);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed to delete NPC mail item %d", uID);
	}
}

// delete npc email from player by id
void gslMailNPC_DeleteMailEntry(Entity *pEntity, U32 uID)
{
	TransactionReturnVal* returnVal;
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("NPCEMail", pEntity, NULL, NULL);
	AutoTrans_gslMailNPC_tr_NPCDeleteMailEntry(returnVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity), uID);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail");
bool gslMailNPC_trh_MarkRead(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U32 uIDNpc, U32 bRead)
{
	S32 i;
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		return false;
	}

	for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
	{
		if(uIDNpc == (U32)pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID)
		{
			pEnt->pPlayer->pEmailV2->mail[i]->bRead = bRead;
			break;
		}
	}

	gslMailNPC_trh_UpdateUnreadMailFlag(ATR_PASS_ARGS, pEnt);

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail");
enumTransactionOutcome gslMailNPC_tr_MarkRead(ATR_ARGS, NOCONST(Entity)* pEnt, U32 uIDNpc, U32 bRead)
{
	if(gslMailNPC_trh_MarkRead(ATR_PASS_ARGS, pEnt, uIDNpc, bRead))
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"NPC Mail %d bRead %d", uIDNpc, bRead);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed to mark read NPC mail item %d", uIDNpc);
	}
}

// mark as read for npc mail
void gslMailNPC_MarkRead(Entity *pEntity, U32 uIDNpc, U32 bRead)
{
	TransactionReturnVal* returnVal;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("NPCEMail", pEntity, NULL, NULL);
	AutoTrans_gslMailNPC_tr_MarkRead(returnVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity), uIDNpc, bRead);
}


bool gslMailNPC_BuildChatMailStruct(Entity *pEntity, S32 iMailIndex, ChatMailStruct *pMail)
{
	bool ret = false;
	if(pEntity && pEntity->pPlayer && iMailIndex < eaSize(&pEntity->pPlayer->pEmailV2->mail) && pMail)
	{

		// first format the string to include the entity
		char *esFormatedSubString = NULL;
		char *esFormatedBodyString = NULL;
		ChatMailStruct mail = {0};

		estrCreate(&esFormatedSubString);
		estrCreate(&esFormatedBodyString);

		langFormatGameString(entGetLanguage(pEntity), &esFormatedSubString, pEntity->pPlayer->pEmailV2->mail[iMailIndex]->subject, STRFMT_ENTITY_KEY("Entity", pEntity), STRFMT_END);
		langFormatGameString(entGetLanguage(pEntity), &esFormatedBodyString, pEntity->pPlayer->pEmailV2->mail[iMailIndex]->body, STRFMT_ENTITY_KEY("Entity", pEntity), STRFMT_END);

		gslMail_InitializeMailEx(pMail, pEntity, esFormatedSubString, esFormatedBodyString, 
			pEntity->pPlayer->pEmailV2->mail[iMailIndex]->fromName, EMAIL_TYPE_NPC_FROM_PLAYER, 
			pEntity->pPlayer->pEmailV2->mail[iMailIndex]->iNPCEMailID, 
			pEntity->pPlayer->pEmailV2->mail[iMailIndex]->sentTime);

		pMail->uID = -pEntity->pPlayer->pEmailV2->mail[iMailIndex]->iNPCEMailID;	// Use upper bits for mail, if a player ever gets more than 2 billion emails this could become an issue ...

		pMail->bRead = pEntity->pPlayer->pEmailV2->mail[iMailIndex]->bRead;

		estrDestroy(&esFormatedBodyString);
		estrDestroy(&esFormatedSubString);

		ret = true;
	}

	return ret;
}

// just send the client the update to date message (for notification and keeping ui up to date)
void gslMailNPC_ActualSend(Entity *pEntity, S32 iMailIndex)
{
	ChatMailStruct mail = {0};
	if(gslMailNPC_BuildChatMailStruct(pEntity, iMailIndex, &mail))
	{
		ClientCmd_gclMailPushNewMail(pEntity, &mail);

		StructDeInit(parse_ChatMailStruct, &mail);
	}
}

AUTO_COMMAND_REMOTE;
void gslMailNPC_GlobalChatSync(U32 uID, ChatMailList *pList)
{
	// Depreciated, no longer used
}

// Make sure all NPC e-mail buffered on the player is synced into their inbox
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr);
void SyncNPCEMail(Entity *pEntity)
{
	// Depreciated, no longer used
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail");
bool gslMailNPC_trh_NPCDeleteMail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	S32 i;
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		return false;
	}

	for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
	{
		StructDestroyNoConst(parse_NPCEMailData, pEnt->pPlayer->pEmailV2->mail[i]);
	}

	eaClear(&pEnt->pPlayer->pEmailV2->mail);

	gslMailNPC_trh_UpdateUnreadMailFlag(ATR_PASS_ARGS, pEnt);

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Mail");
enumTransactionOutcome gslMailNPC_tr_NPCDeleteMail(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	if(gslMailNPC_trh_NPCDeleteMail(ATR_PASS_ARGS, pEnt))
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"NPC all Mail deleted");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed to delete NPC mail");
	}
}

void gslMailNPC_DeleteMail(Entity *pEntity)
{
	TransactionReturnVal* returnVal;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("NPCEMailDelete", pEntity, NULL, NULL);
	AutoTrans_gslMailNPC_tr_NPCDeleteMail(returnVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity));
	
	// make sure mail is synced up
	RemoteCommand_chatCommandRemote_GetMailbox(GLOBALTYPE_CHATSERVER, 0, pEntity->pPlayer->accountID);
	
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Bwipeemail, .Pplayer.Icurrentcharacterids");
enumTransactionOutcome gslMailNPC_tr_WipeEMail(ATR_ARGS, NOCONST(Entity)* pEnt, NON_CONTAINER ChatMailList *pList)
{
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed to wipe NPC mail");
	}
	
	if(!pEnt->pPlayer->bWipeEmail)
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"NPC Email already wiped");
	}
	
	pEnt->pPlayer->bWipeEmail = false;
	eaiDestroy(&pEnt->pPlayer->iCurrentCharacterIDs);

	TRANSACTION_RETURN_LOG_SUCCESS(
		"NPC Email wiped");
	
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void DeleteNPCEMail(Entity *pClientEntity)
{
	gslMailNPC_DeleteMail(pClientEntity);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pemailv2.Unextfuturedelivery, .Pplayer.Pemailv2.Bunreadmail, .Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail");
enumTransactionOutcome gslMailNPC_tr_NPCAddMail(ATR_ARGS, NOCONST(Entity)* pEnt, char *fromName, char *subject, char *body, NON_CONTAINER Item *pItem, U32 uQuantity, U32 uFutureTimeSeconds)
{
	MailCharacterItems *pCharacterItems = NULL;
	
	if(NONNULL(pItem) && uQuantity > 0)
	{
		Item* pItemClone = StructClone(parse_Item, pItem);
		CONTAINER_NOCONST(Item, pItemClone)->count = uQuantity;
		pCharacterItems = CharacterMailAddItem(NULL, pItemClone);
		if(!pCharacterItems)
		{
			StructDestroy(parse_Item, pItemClone);
			TRANSACTION_RETURN_LOG_FAILURE(
				"Failed to add NPC mail due to CharacterMailAddItem failing.");
		}
	}

	// All items MailCharacterItem and item copies will be destroyed by this function regardless of outcome 	
	if(EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pEnt, fromName, subject, body, pCharacterItems, uFutureTimeSeconds, kNPCEmailType_Default, NULL))
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"NPC Mail added");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed to add NPC mail");
	}
}

// Add NPC Email (character mail) with itemdef
void gslMailNPC_AddMailWithItemDef(Entity *pEntity,const char *fromName, const char *subject, const char *body, const char* ItemDefName, U32 uQuantity, U32 uFutureTimeSeconds)
{
	if(pEntity && pEntity->pPlayer)
	{
		TransactionReturnVal* returnVal;
		Item *pItem = item_FromEnt( CONTAINER_NOCONST(Entity, pEntity), ItemDefName, 0, NULL, 0);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("NPCEMail", pEntity, NULL, NULL);

		AutoTrans_gslMailNPC_tr_NPCAddMail(returnVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity), fromName, subject, body, pItem, uQuantity, uFutureTimeSeconds);

		// make sure mail is synced up
//		RemoteCommand_chatCommandRemote_GetMailbox(GLOBALTYPE_CHATSERVER, 0, pEntity->pPlayer->accountID);
	}
}

void gslMailNPC_AddMailRequest(Entity* pEntity, NPCMailRequest* request)
{
	if(pEntity && pEntity->pPlayer)
	{
		TransactionReturnVal* returnVal;
		Item *pItem = item_FromEnt(CONTAINER_NOCONST(Entity, pEntity), request->itemDefName, 0, NULL, 0);
		NPCMailRequest* clonedRequest = StructClone(parse_NPCMailRequest, request);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("NPCEmail", pEntity, SendNPCMailWebRequest_cb, clonedRequest);

		AutoTrans_gslMailNPC_tr_NPCAddMail(returnVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity), request->fromName, request->subject, request->body, pItem, request->itemQuantity, 0); 
	}
}

void gslMailNPC_AddMail(Entity *pEntity,const char *fromName, const char *subject, const char *body)
{
	if(pEntity && pEntity->pPlayer)
	{
		TransactionReturnVal* returnVal;

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("NPCEMail", pEntity, NULL, NULL);
		// 0 item qty indicates no item is being passed in
		AutoTrans_gslMailNPC_tr_NPCAddMail(returnVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity), fromName, subject, body, NULL, 0, 0);


		// make sure mail is synced up
//		RemoteCommand_chatCommandRemote_GetMailbox(GLOBALTYPE_CHATSERVER, 0, pEntity->pPlayer->accountID);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4);
bool SendNPCEMail(Entity *pClientEntity,const char *fromName, const char *subject, const char *body,  ACMD_NAMELIST("ItemDef", REFDICTIONARY) const char* ItemDefName)
{
	if(pClientEntity && fromName && subject)
	{
		gslMailNPC_AddMailWithItemDef(pClientEntity, fromName, subject, body, ItemDefName, 1, 0);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4);
bool FutureSendNPCEMail(Entity *pClientEntity,const char *fromName, const char *subject, const char *body,  ACMD_NAMELIST("ItemDef", REFDICTIONARY) const char* ItemDefName, U32 uFutureTimeSeconds)
{
	if(pClientEntity && fromName && subject)
	{
		gslMailNPC_AddMailWithItemDef(pClientEntity, fromName, subject, body, ItemDefName, 1, uFutureTimeSeconds);
		return true;
	}
	return false;
}

void gslMailNPC_NPCDeleteMail(Entity *pEnt, S32 iNPCEMailID)
{
	if(iNPCEMailID > 0)
	{
		gslMailNPC_DeleteMailEntry(pEnt, iNPCEMailID);
	}
}

// Delete a mail message. Deletes NPC email on characters
AUTO_COMMAND ACMD_NAME(MailDeleteComplete) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail) ACMD_PRIVATE;
void ServerChat_DeleteMailComplete(Entity *pEnt, U32 uMailID, S32 iNPCEMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		if(iNPCEMailID == 0)
		{
			//RemoteCommand_ChatServerDeleteMail(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, uMailID);
			Errorf("Player ID %d attempted to delete a non-NPC email (ID: %d) using ServerChat_DeleteMailComplete(). This shouldn't happen and needs to be fixed.", pEnt->myContainerID, uMailID);
		}
		else
		{
			// delete mail (if of NPC type) that is on the player
			gslMailNPC_NPCDeleteMail(pEnt, iNPCEMailID);
		}
	}
}

static void MailLotDeleted_CB(TransactionReturnVal *pReturn, void *pData) 
{	
	if(pData)
	{
		AuctionDeleteCBData *pAuctionData = (AuctionDeleteCBData *)pData;
		Entity *pEnt = entFromEntityRefAnyPartition(pAuctionData->erEnt);
		if(pEnt)
		{
			if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && pEnt->pPlayer && pEnt->pSaved) 
			{
				if (gbEnableGamePlayDataLogging) 
				{
					char *eMessage = NULL;
					estrStackCreate(&eMessage);
					estrPrintf(&eMessage, "Lot %d",
						pAuctionData->iAuctionID);
					entLog(LOG_MAIL_GSL, pEnt, "Mail_With_Auction_Lot_Deleted", "%s", eMessage);
					estrDestroy(&eMessage);
				}
			}
		}
		StructDestroy(parse_AuctionDeleteCBData, pData);
	}
}

static void MailLotDelete_CB(TransactionReturnVal *pReturn, void *pData) 
{	
	if(pData)
	{
		AuctionDeleteCBData *pAuctionData = (AuctionDeleteCBData *)pData;
		Entity *pEnt = entFromEntityRefAnyPartition(pAuctionData->erEnt);
		if(pEnt)
		{
			if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
			{
				AuctionDeleteCBData *pCallData = StructClone(parse_AuctionDeleteCBData, pAuctionData);
				objRequestContainerDestroy(LoggedTransactions_CreateManagedReturnValEnt("Mail Auction lot deleted", pEnt, MailLotDeleted_CB, pCallData), GLOBALTYPE_AUCTIONLOT,
					pAuctionData->iAuctionID, GLOBALTYPE_AUCTIONSERVER, 0);			
			}
		}

		StructDestroy(parse_AuctionDeleteCBData, pData);

	}
}

AUTO_TRANSACTION
ATR_LOCKS(pDeleter, ".Pplayer.Accountid") ATR_LOCKS(pAuctionLot, ".Recipientid, .State");
enumTransactionOutcome gslMailNPC_tr_DeleteMailedAuction(ATR_ARGS, NOCONST(Entity) *pDeleter, NOCONST(AuctionLot) *pAuctionLot)
{
	if(NONNULL(pDeleter) && NONNULL(pAuctionLot) && NONNULL(pDeleter->pPlayer) && pDeleter->pPlayer->accountID == pAuctionLot->recipientID &&
		pAuctionLot->state == ALS_Mailed)
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Mailed Auction lot cleared to be deleted");
		
	}

	TRANSACTION_RETURN_LOG_FAILURE("Can not delete mailed lot.");
}

// Delete a mailed auction
void gslMailNPC_DeleteMailedAuction(Entity* pEnt, ContainerID iAuctionLotID)
{
	if(!pEnt || pEnt->myEntityType != GLOBALTYPE_ENTITYPLAYER)
	{
		return;
	}
	else
	{
		AuctionDeleteCBData *pData = StructCreate(parse_AuctionDeleteCBData);
		if(pData)
		{
			pData->erEnt = entGetRef(pEnt);
			pData->iAuctionID = iAuctionLotID;
			AutoTrans_gslMailNPC_tr_DeleteMailedAuction(LoggedTransactions_CreateManagedReturnValEnt("Auction_Delete_Mailed", pEnt, MailLotDelete_CB, pData), GLOBALTYPE_GAMESERVER, 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_AUCTIONLOT, iAuctionLotID);
		}
	}
}


// Delete a mail message. Deletes NPC email on characters. Logs email if lot id > 0
AUTO_COMMAND ACMD_NAME(MailDeleteCompleteLog) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void gslMailNPC_DeleteMailCompleteLog(Entity *pEnt, U32 uMailID, S32 iNPCEMailID, U32 uAuctionLotID)
{
	if (pEnt && pEnt->pPlayer)
	{
		ServerChat_DeleteMailComplete(pEnt, uMailID, iNPCEMailID);
		if(uAuctionLotID > 0)
		{
			// delete the auction lot container
			gslMailNPC_DeleteMailedAuction(pEnt, uAuctionLotID);
		}
	}
}

// Sync npc email, was used by game action
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslMailNPCRemoteSync(CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	
	SyncNPCEMail(pEnt);
}

// Create an email message from npc id to send to chat server
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void gslMailNPCRemoteSend(CmdContext* context, S32 iEmailID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	if(pEnt && pEnt->pPlayer)
	{
		S32 i;
		
		for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)		
		{
			if(pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID == iEmailID && timeSecondsSince2000() >= pEnt->pPlayer->pEmailV2->mail[i]->sentTime)
			{
				gslMailNPC_ActualSend(pEnt, i);
				break;
			}
		}
	}
}

#include "autogen/gslMailNPC_c_ast.c"
#include "AutoGen/gslMailNPC_h_ast.c"
