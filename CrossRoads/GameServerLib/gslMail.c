#include "MailCommon.h"
#include "MailCommon_h_ast.h"
#include "gslmail.h"
#include "gslmail_h_ast.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntityLib.h"
#include "SharedBankCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "AutoTransDefs.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "NotifyEnum.h"
#include "LoggedTransactions.h"
#include "gslSharedBank.h"
#include "objTransactions.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "guild.h"
#include "gslLogSettings.h"
#include "GamePermissionsCommon.h"
#include "gslMail_old.h"
#include "loggingEnums.h"
#include "chatCommonStructs.h"
#include "AuctionLot.h"

#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "autogen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/AuctionLot_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//__CATEGORY Mail related settings
// The time that players must wait after sending an email before sending another one.
U32 s_MailCooldownTime = 5;
AUTO_CMD_INT(s_MailCooldownTime, MailCooldownTime) ACMD_AUTO_SETTING(Mail, GAMESERVER);

enumTransactionOutcome EmailV3_tr_DeleteMessage(ATR_ARGS, NOCONST(Entity)* pSharedBank, int iOwnerContainerID, int iMailID);

AUTO_TRANS_HELPER;
enumTransactionOutcome EmailV3_trh_PlayerCanReceiveMessage(ATR_ARGS, ContainerID iSenderID, ATH_ARG NOCONST(Entity)* pRecipientAccountSharedBank, ATH_ARG NOCONST(EmailV3Message)* pMessage, bool bSendErrors)
{
	if (ISNULL(pRecipientAccountSharedBank) || ISNULL(pMessage))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		if (NONNULL(pRecipientAccountSharedBank->pEmailV3))
		{
			S32 iInboxSize = pRecipientAccountSharedBank->pEmailV3->iMessageCount;
			S32 iAttachments = pRecipientAccountSharedBank->pEmailV3->iAttachmentsCount;

			if ((U32)iInboxSize >= g_SharedBankConfig.uMaxInboxSize || iInboxSize < 0)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iSenderID, "Mail_Error_RecipientInboxFull", kNotifyType_MailSendFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}
			if ((U32)iAttachments >= g_SharedBankConfig.uMaxInboxAttachments || iAttachments < 0)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iSenderID, "Mail_Error_RecipientAttachmentsFull", kNotifyType_MailSendFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
		else
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iSenderID, "Mail_Error_RecipientInboxFull", kNotifyType_MailSendFailed);
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION 
	ATR_LOCKS(pSender, ".Pplayer.Eaplayernumericthresholds, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(pRecipientAccountSharedBank, ".Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount, .Pemailv3.Bunreadmail, pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_SendMessageFromPlayer(ATR_ARGS, NOCONST(Entity)* pSender, NOCONST(Entity)* pRecipientAccountSharedBank, EmailV3NewMessageWrapper* pWrapper, int iMailID, const ItemChangeReason *pReason, GameAccountDataExtract* pSenderExtract)
{
	char* estrItems = NULL;
	int i;
	NOCONST(EmailV3Message)* pNCMessage;
	
	if (ISNULL(pWrapper) || ISNULL(pWrapper->pMessage) || ISNULL(pRecipientAccountSharedBank))
		return TRANSACTION_OUTCOME_FAILURE;

	pNCMessage = StructCloneDeConst(parse_EmailV3Message, pWrapper->pMessage);
		
	if (EmailV3_trh_PlayerCanReceiveMessage(ATR_PASS_ARGS, pWrapper->uSenderContainerID, pRecipientAccountSharedBank, pNCMessage, true) == TRANSACTION_OUTCOME_FAILURE)
		return TRANSACTION_OUTCOME_FAILURE;
		
	if (eaSize(&pWrapper->eaItemsFromPlayer) > 0)
	{
		// Sender is needed if there are items to transfer.  It can be NULL if no items to transfer.
		if (ISNULL(pSender))
			return TRANSACTION_OUTCOME_FAILURE;

		if (EmailV3_trh_AddItemsToMessageFromEntInventory(ATR_PASS_ARGS, pNCMessage, pWrapper->eaItemsFromPlayer, pSender, pReason, pSenderExtract) == TRANSACTION_OUTCOME_FAILURE)
			return TRANSACTION_OUTCOME_FAILURE;
	}

	if(EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pRecipientAccountSharedBank, pNCMessage, iMailID) == TRANSACTION_OUTCOME_FAILURE)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
		

	estrCreate(&estrItems);
	for (i = 0; i < eaSize(&pNCMessage->ppItems); i++)
	{
		if (i != 0)
			estrAppend2(&estrItems, ", ");
		estrConcatf(&estrItems, "%d %s", pNCMessage->ppItems[i]->count, REF_STRING_FROM_HANDLE(pNCMessage->ppItems[i]->hItem));
	}

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_MAIL_GSL, "Player_Mail", "RecipientAccount %s, ID %d, Subject <&%s&>, Body <&%s&>, Items(%s)", pWrapper->pchRecipientHandle, pNCMessage->uID, pNCMessage->pchSubject, pNCMessage->pchBody, estrItems);

	estrDestroy(&estrItems);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION 
	ATR_LOCKS(pRecipientAccountSharedBank, ".Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount, .Pemailv3.Bunreadmail, pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_SendMessageFromPlayerNoItems(ATR_ARGS, NOCONST(Entity)* pRecipientAccountSharedBank, EmailV3NewMessageWrapper* pWrapper, int iMailID, const ItemChangeReason *pReason, GameAccountDataExtract* pSenderExtract)
{
	return EmailV3_tr_SendMessageFromPlayer(ATR_PASS_ARGS, NULL, pRecipientAccountSharedBank, pWrapper, iMailID, pReason, pSenderExtract);
}

AUTO_TRANSACTION 
	ATR_LOCKS(pRecipientAccountSharedBank, ".Pemailv3.bUnreadMail");
enumTransactionOutcome EmailV3_tr_SetUnreadMailBit(ATR_ARGS, NOCONST(Entity)* pRecipientAccountSharedBank, S32 bSet)
{
	if (ISNULL(pRecipientAccountSharedBank) || ISNULL(pRecipientAccountSharedBank->pEmailV3))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		pRecipientAccountSharedBank->pEmailV3->bUnreadMail = bSet;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pRecipientAccountSharedBank, ".Pemailv3.Bunreadmail, .Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount, pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_SendMessageFromNPC(ATR_ARGS, NOCONST(Entity)* pRecipientAccountSharedBank, EmailV3NewMessageWrapper* pWrapper, int iMailID)
{
	if (ISNULL(pWrapper) || ISNULL(pWrapper->pMessage) || ISNULL(pRecipientAccountSharedBank))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		NOCONST(EmailV3Message)* pNCMessage = StructCloneDeConst(parse_EmailV3Message, pWrapper->pMessage);
		char* estrItems = NULL;
		int i;

		EmailV3_trh_AddItemsToMessage(ATR_PASS_ARGS, pNCMessage, (NOCONST(Item)**)pWrapper->eaItemsFromNPC);
	
		if(EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pRecipientAccountSharedBank, pNCMessage, iMailID) == TRANSACTION_OUTCOME_FAILURE)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}

		estrCreate(&estrItems);
		for (i = 0; i < eaSize(&pNCMessage->ppItems); i++)
		{
			if (i != 0)
				estrAppend2(&estrItems, ", ");
			estrConcatf(&estrItems, "%d %s", pNCMessage->ppItems[i]->count, REF_STRING_FROM_HANDLE(pNCMessage->ppItems[i]->hItem));
		}

		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_MAIL_GSL, "NPC_Mail", "Sender %s, ID %d, Subject <&%s&>, Body <&%s&>, Items(%s)", pNCMessage->pchSenderHandle, pNCMessage->uID, pNCMessage->pchSubject, pNCMessage->pchBody, estrItems);

		estrDestroy(&estrItems);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

static void EmailV3_RecipientAccountSharedBankExists_CB(TransactionReturnVal *pReturn, EmailV3NewMessageWrapper* pWrapper)
{
	if (pWrapper && pReturn && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pSender = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID);

		if(!pSender && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
			pSender = entForClientCmd(pWrapper->uSenderContainerID,pSender);

		if (pSender)
		{
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pSender);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pSender, "Email:SendFromPlayer", NULL);

			// Only pass in the sender entity if there are items involved.
			if (eaSize(&pWrapper->eaItemsFromPlayer) > 0)
			{
				AutoTrans_EmailV3_tr_SendMessageFromPlayer(LoggedTransactions_CreateManagedReturnValEnt("EmailV3SendMessage", pSender, NULL, NULL), 
					GetAppGlobalType(), 
					GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID, 
					GLOBALTYPE_ENTITYSHAREDBANK, pWrapper->uRecipientAccountID, 
					pWrapper, timeSecondsSince2000(), &reason, pExtract);
			}
			else
			{
				AutoTrans_EmailV3_tr_SendMessageFromPlayerNoItems(LoggedTransactions_CreateManagedReturnValEnt("EmailV3SendMessage", pSender, NULL, NULL), 
					GetAppGlobalType(), 
					GLOBALTYPE_ENTITYSHAREDBANK, pWrapper->uRecipientAccountID, 
					pWrapper, timeSecondsSince2000(), &reason, pExtract);
			}
		}
	}

	if (pWrapper)
	{
		StructDestroy(parse_EmailV3NewMessageWrapper, pWrapper);
	}
}

static void EmailV3_ReturnToSenderAccountSharedBankExists_CB(TransactionReturnVal *pReturn, EmailV3ReturnToSenderWrapper* pWrapper)
{
	if (pWrapper && pReturn && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pLoggingEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pWrapper->uiRecipientContainerID);
		AutoTrans_EmailV3_tr_ReturnToSender(LoggedTransactions_CreateManagedReturnValEnt("EmailV3ReturnToSender", pLoggingEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pWrapper->uiSrcAccountID, GLOBALTYPE_ENTITYSHAREDBANK, pWrapper->uiDstAccountID, timeSecondsSince2000());
	}

	if (pWrapper)
	{
		StructDestroy(parse_EmailV3ReturnToSenderWrapper, pWrapper);
	}
}

static void EmailV3_GetRecipientAccountID_CB(TransactionReturnVal *pReturn, EmailV3NewMessageWrapper* pWrapper)
{
	U32 uiAccountID;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(pReturn, &uiAccountID);

	if (pWrapper && pWrapper->pMessage)
	{
		if (uiAccountID == 0)
		{
			Entity* pSender = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID);
			//account doesn't exist
			notify_NotifySend(pSender, kNotifyType_MailSendFailed, entTranslateMessageKey(pSender, "Mail_SendFailure_AccountDoesNotExist"), NULL, NULL);
		}
		else
		{
			if (pWrapper->pMessage->eSenderType == kEmailV3Type_Player)
			{
				SharedBankCBData *cbData = NULL;
				cbData = calloc(sizeof(SharedBankCBData), 1);
				cbData->ownerType = 0;
				cbData->ownerID = 0;
				cbData->accountID = uiAccountID;
				cbData->pUserData = pWrapper;
				cbData->pFunc = EmailV3_RecipientAccountSharedBankExists_CB;

				pWrapper->uRecipientAccountID = uiAccountID;
				RemoteCommand_DBCheckAccountWideContainerExistsWithRestore(objCreateManagedReturnVal(RestoreSharedBank_CB, cbData), GLOBALTYPE_OBJECTDB, 0, uiAccountID, GLOBALTYPE_ENTITYSHAREDBANK);
				return;
			}
			else if (pWrapper->pMessage->eSenderType == kEmailV3Type_NPC)
			{
				// Do not use this function with mail that must be received
				AutoTrans_EmailV3_tr_SendMessageFromNPC(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, uiAccountID, pWrapper, timeSecondsSince2000());
			}
		}
	}

	if (pWrapper)
	{
		StructDestroy(parse_EmailV3NewMessageWrapper, pWrapper);
	}
}

static void EmailV3_ReturnToSenderGetDstAccountID_CB(TransactionReturnVal *pReturn, EmailV3ReturnToSenderWrapper* pWrapper)
{
	U32 uiAccountID;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(pReturn, &uiAccountID);

	if (pWrapper)
	{
		if (uiAccountID)
		{
			SharedBankCBData *cbData = NULL;
			cbData = calloc(sizeof(SharedBankCBData), 1);
			cbData->ownerType = 0;
			cbData->ownerID = 0;
			cbData->accountID = uiAccountID;
			cbData->pUserData = pWrapper;
			cbData->pFunc = EmailV3_ReturnToSenderAccountSharedBankExists_CB;

			pWrapper->uiDstAccountID = uiAccountID;
			RemoteCommand_DBCheckAccountWideContainerExistsWithRestore(objCreateManagedReturnVal(RestoreSharedBank_CB, cbData), GLOBALTYPE_OBJECTDB, 0, uiAccountID, GLOBALTYPE_ENTITYSHAREDBANK);
			return;
		}
	}

	if (pWrapper)
	{
		StructDestroy(parse_EmailV3ReturnToSenderWrapper, pWrapper);
	}
}

void EmailV3_PlayerToPlayerMailChatServerSignoff_CB(TransactionReturnVal *pReturn, EmailV3NewMessageWrapper* pWrapper)
{
	int iResult;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_ChatServer_OutgoingMailCanBeSent(pReturn, &iResult);

	if (iResult == 1 && pWrapper && pWrapper->pchRecipientHandle)
	{
		RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
			objCreateManagedReturnVal(EmailV3_GetRecipientAccountID_CB, pWrapper),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountGetHandle(pWrapper->pchRecipientHandle));
	}
	else
	{
		Entity* pSender = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID);

		if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
			pSender = entForClientCmd(pWrapper->uSenderContainerID, pSender);

		if (pSender)
			notify_NotifySend(pSender, kNotifyType_MailSendFailed, entTranslateMessageKey(pSender, "Mail_SendFailure_ChatServerDeclined"), NULL, NULL);

		StructDestroy(parse_EmailV3NewMessageWrapper, pWrapper);
	}
}

// Function used by newer shards
void EmailV3_SendMessageInternal(EmailV3Message* pMessage,
								Entity* pSender, 
								const char* pchRecipientHandle, 
								EmailV3SenderItem** eaItemsFromPlayer, 
								Item** eaItemsFromNPC)
{
	NOCONST(EmailV3Message)* pNCMessage = CONTAINER_NOCONST(EmailV3Message, pMessage);
	EmailV3NewMessageWrapper* pWrap = StructCreate(parse_EmailV3NewMessageWrapper);

	eaCopyStructs(&eaItemsFromPlayer, &pWrap->eaItemsFromPlayer, parse_EmailV3SenderItem);
	//does NOT duplicate items
	eaCopy(&pWrap->eaItemsFromNPC, &eaItemsFromNPC);

	pWrap->pMessage = pMessage;
	pWrap->uSenderContainerID = pSender ? pSender->myContainerID : 0;
	pWrap->pchRecipientHandle = StructAllocString(pchRecipientHandle);

	if (pSender)
	{
		//messages from players need an extra step to ask the chatserver if this is okay.


		RemoteCommand_ChatServer_OutgoingMailCanBeSent(
			objCreateManagedReturnVal(EmailV3_PlayerToPlayerMailChatServerSignoff_CB, pWrap),
			GLOBALTYPE_CHATSERVER, 0, entGetAccountOrLocalName(pSender), pMessage->pchBody, pMessage->pchSubject);
	}
	else
	{
		RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
			objCreateManagedReturnVal(EmailV3_GetRecipientAccountID_CB, pWrap),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountGetHandle(pchRecipientHandle));
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_SendPlayerEmail(const char* pchSubject,
	const char* pchBody, 
	Entity* pSender,
	const char* pchRecipientHandle, 
	EmailV3SenderItemsWrapper* pWrapper)
{
	if (pSender && pSender->pPlayer && pchRecipientHandle && *pchRecipientHandle)
	{
        EmailV3Message* pMessage;
        U32 lastMailSendTime = pSender->pPlayer->lastMailSendTime;
        bool hasMailPermission;

        if ( lastMailSendTime == 0 )
        {
            lastMailSendTime = pSender->pPlayer->uiContainerArrivalTime;
        }

        hasMailPermission = (!gamePermission_Enabled() || GamePermission_EntHasToken(pSender, GAME_PERMISSION_CAN_SEND_MAIL) || !stricmp(accountGetHandle(NULL_TO_EMPTY(pchRecipientHandle)), pSender->pPlayer->publicAccountName));

        // If the mail send cooldown has not passed, then fail.
        if ( ( lastMailSendTime + s_MailCooldownTime ) > timeSecondsSince2000() )
        {
            ClientCmd_gclMailSentConfirm(pSender, false, "You must wait briefly before sending mail again");
            //ClientCmd_gclMailSentConfirm(pSender, false, entTranslateMessageKey(pSender, "Chat_NoTrialMail"));
            return;
        }

        // If player doesn't have the mail permission token, they can't mail non-friends
        if (!hasMailPermission) 
        {
            S32 i = 0;
            if (SAFE_MEMBER4(pSender, pPlayer, pUI, pChatState, eaFriends)) 
            {
                for (i = 0; i < eaSize(&pSender->pPlayer->pUI->pChatState->eaFriends); i++) 
                {
                    ChatPlayerStruct *pFriend = pSender->pPlayer->pUI->pChatState->eaFriends[i];
                    if (!ChatFlagIsFriend(pFriend->flags))
                    {
                        continue;
                    }
                    if (!stricmp(NULL_TO_EMPTY(pFriend->chatHandle), accountGetHandle(NULL_TO_EMPTY(pchRecipientHandle)))) 
                    {
                        break;
                    }
                }
            }
            if (!SAFE_MEMBER4(pSender, pPlayer, pUI, pChatState, eaFriends) || i == eaSize(&pSender->pPlayer->pUI->pChatState->eaFriends)) 
            {
                ClientCmd_gclMailSentConfirm(pSender, false, entTranslateMessageKey(pSender, "Chat_NoTrialMail"));
                return;
            }
        }

        pSender->pPlayer->lastMailSendTime = timeSecondsSince2000();
		pMessage = EmailV3_CreateNewMessage(pchSubject, pchBody, pSender, entGetLocalName(pSender), pSender->pPlayer ? pSender->pPlayer->publicAccountName : NULL);
		EmailV3_SendMessageInternal(pMessage, pSender, pchRecipientHandle, pWrapper ? pWrapper->eaItemsFromPlayer : NULL, NULL);
	}
}

void EmailV3_SendNPCEmail(const char* pchSubject,
	const char* pchBody, 
	const char* pchSenderName, 
	const char* pchRecipientHandle, 
	Item** eaNPCItems)
{
	EmailV3Message* pMessage = EmailV3_CreateNewMessage(pchSubject, pchBody, NULL, pchSenderName, NULL);
	EmailV3_SendMessageInternal(pMessage, NULL, pchRecipientHandle, NULL, eaNPCItems);
}

AUTO_TRANS_HELPER;
NOCONST(EmailV3Message)* EmailV3_trh_GetMessageByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pSharedBank, U32 id)
{
	if (NONNULL(pSharedBank->pEmailV3))
	{
		return (NOCONST(EmailV3Message)*)eaIndexedGetUsingInt(&pSharedBank->pEmailV3->eaMessages, id);
	}
	return NULL;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Eaplayernumericthresholds, .Pplayer.Eaastrrecentlyacquiredstickerbookitems")
	ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[], .Pemailv3.Iattachmentscount");
enumTransactionOutcome EmailV3_tr_TakeAttachedItem(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pSharedBank, int iMailID, int iItem, GameAccountDataExtract* pExtract)
{
	NOCONST(EmailV3Message)* pMessage =  EmailV3_trh_GetMessageByID(ATR_PASS_ARGS, pSharedBank, iMailID);
	if (pMessage && pMessage->ppItems)
	{
		if (iItem >= 0 && iItem < eaSize(&pMessage->ppItems))
		{
			Item* pItem = CONTAINER_RECONST(Item, pMessage->ppItems[iItem]);
			ItemDef* pDef = GET_REF(pItem->hItem);
			InvBagIDs eBag = inv_trh_GetBestBagForItemDef(pEnt, pDef, CONTAINER_NOCONST(Item, (pItem)), pItem->count, true, pExtract);

			if (!pDef)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail.InvalidItem", kNotifyType_MailFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}

			item_trh_ClearPowerIDs(pMessage->ppItems[iItem]);

			if (inv_AddItem(ATR_PASS_ARGS, pEnt, NULL, eBag, -1, pItem, pDef->pchName, ItemAdd_ClearID, NULL, pExtract) == TRANSACTION_OUTCOME_FAILURE)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail.FailedToTakeItems", kNotifyType_InventoryFull);
				return TRANSACTION_OUTCOME_FAILURE;
			}

			StructDestroyNoConst(parse_Item, pMessage->ppItems[iItem]);
			eaRemove(&pMessage->ppItems, iItem);

			if (eaSize(&pMessage->ppItems) <= 0)
			{
				if (pMessage->eSenderType == kEmailV3Type_Player && pSharedBank->pEmailV3->iAttachmentsCount > 0)
				{
					pSharedBank->pEmailV3->iAttachmentsCount--;
				}
				eaDestroy(&pMessage->ppItems);
				pMessage->ppItems = NULL;
			}

			return TRANSACTION_OUTCOME_SUCCESS;
		}
		else
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail.InvalidItem", kNotifyType_MailFailed);
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_TakeAttachedItem(Entity* pEnt, int iMailID, int iItem)
{
	if (pEnt && pEnt->pPlayer)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		AutoTrans_EmailV3_tr_TakeAttachedItem(LoggedTransactions_CreateManagedReturnValEnt("EmailV3TakeItem", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID, iItem, pExtract);
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Eaplayernumericthresholds, .Pplayer.Eaastrrecentlyacquiredstickerbookitems")
	ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[], .Pemailv3.Iattachmentscount, .Pemailv3.Imessagecount");
enumTransactionOutcome EmailV3_tr_TakeAllAttachedItems(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pSharedBank, int iMailID, GameAccountDataExtract* pExtract, int bDeleteOnSuccess)
{
	NOCONST(EmailV3Message)* pMessage =  EmailV3_trh_GetMessageByID(ATR_PASS_ARGS, pSharedBank, iMailID);
	if (pMessage && pMessage->ppItems)
	{
		int i;
		bool bFoundItem = false;
		for (i = 0; i < eaSize(&pMessage->ppItems); i++)
		{
			Item* pItem = CONTAINER_RECONST(Item, pMessage->ppItems[i]);
			ItemDef* pDef = GET_REF(pItem->hItem);
			InvBagIDs eBag = inv_trh_GetBestBagForItemDef(pEnt, pDef, CONTAINER_NOCONST(Item, (pItem)), pItem->count, true, pExtract);

			item_trh_ClearPowerIDs(pMessage->ppItems[i]);

			if (pDef)
			{
				if (inv_AddItem(ATR_PASS_ARGS, pEnt, NULL, eBag, -1, pItem, pDef->pchName, ItemAdd_ClearID, NULL, pExtract) == TRANSACTION_OUTCOME_FAILURE)
				{
					QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail_FailedToTakeItems", kNotifyType_InventoryFull);
					return TRANSACTION_OUTCOME_FAILURE;
				}

				StructDestroyNoConst(parse_Item, eaRemove(&pMessage->ppItems, i));
				i--;
				bFoundItem = true;
			}
		}

		if (eaSize(&pMessage->ppItems) <= 0)
		{
			eaDestroy(&pMessage->ppItems);
			pMessage->ppItems = NULL;

			if (bFoundItem && pMessage->eSenderType == kEmailV3Type_Player && pSharedBank->pEmailV3->iAttachmentsCount > 0)
			{
				pSharedBank->pEmailV3->iAttachmentsCount--;
			}

			if (bDeleteOnSuccess)
			{
				if (EmailV3_tr_DeleteMessage(ATR_PASS_ARGS, pSharedBank, pEnt->myContainerID, iMailID) == TRANSACTION_OUTCOME_FAILURE)
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}
			}
		}

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_TakeAllAttachedItems(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		AutoTrans_EmailV3_tr_TakeAllAttachedItems(LoggedTransactions_CreateManagedReturnValEnt("EmailV3TakeAll", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID, pExtract, false);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_TakeAllAttachedItemsAndDelete(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		AutoTrans_EmailV3_tr_TakeAllAttachedItems(LoggedTransactions_CreateManagedReturnValEnt("EmailV3TakeAll", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID, pExtract, true);
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pSrcSharedBank, ".pEmailV3.eaMessages[]")
	ATR_LOCKS(pDstSharedBank, ".Pemailv3.Bunreadmail, .Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount, pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_ReturnToSender(ATR_ARGS, NOCONST(Entity)* pSrcSharedBank, NOCONST(Entity)* pDstSharedBank, int iMailID)
{
	if (NONNULL(pSrcSharedBank->pEmailV3) && NONNULL(pDstSharedBank->pEmailV3))
	{
		NOCONST(EmailV3Message)* pMessage = eaIndexedRemoveUsingInt(&pSrcSharedBank->pEmailV3->eaMessages, iMailID);
		char* estrItems = NULL;
		int i;
		
		if (!pMessage)
			return TRANSACTION_OUTCOME_FAILURE;

		pMessage->bRead = false;
		pMessage->eContentType = kEmailV3Contents_ReturnToSender;
		if(EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pDstSharedBank, pMessage, iMailID) == TRANSACTION_OUTCOME_FAILURE)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}

		estrCreate(&estrItems);
		for (i = 0; i < eaSize(&pMessage->ppItems); i++)
		{
			if (i != 0)
				estrAppend2(&estrItems, ", ");
			estrConcatf(&estrItems, "%d %s", pMessage->ppItems[i]->count, REF_STRING_FROM_HANDLE(pMessage->ppItems[i]->hItem));
		}

		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_MAIL_GSL, "Return_To_Sender", "SenderAccount @%s, OldID %d, NewID %d, Subject <&%s&>, Body <&%s&>, Items(%s)", pMessage->pchSenderHandle, iMailID, pMessage->uID, pMessage->pchSubject, pMessage->pchBody, estrItems);

		estrDestroy(&estrItems);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[], .Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount");
enumTransactionOutcome EmailV3_tr_DeleteMessage(ATR_ARGS, NOCONST(Entity)* pSharedBank, int iOwnerContainerID, int iMailID)
{
	if (NONNULL(pSharedBank->pEmailV3))
	{
		int i;
		char* estrItems = NULL;
		
		NOCONST(EmailV3Message)* pMessage = eaIndexedRemoveUsingInt(&pSharedBank->pEmailV3->eaMessages, iMailID);
		if(ISNULL(pMessage))
		{
			// its already deleted
			return TRANSACTION_OUTCOME_SUCCESS;
		}

		if (pMessage->eSenderType == kEmailV3Type_Player)
		{
			if(pSharedBank->pEmailV3->iMessageCount > 0)
			{
				pSharedBank->pEmailV3->iMessageCount--;
			}
			
			if (eaSize(&pMessage->ppItems) > 0 && pSharedBank->pEmailV3->iAttachmentsCount > 0)
			{
				pSharedBank->pEmailV3->iAttachmentsCount--;
			}
		}

		for (i = 0; i < eaSize(&pMessage->ppItems); i++)
		{
			ItemDef* pDef = SAFE_GET_REF(pMessage->ppItems[i], hItem);
			if (pDef && (pDef->flags & kItemDefFlag_CantDiscard))
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iOwnerContainerID, "Mail_CannotDiscard", kNotifyType_MailSendFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}

		estrCreate(&estrItems);
		for (i = 0; i < eaSize(&pMessage->ppItems); i++)
		{
			if (i != 0)
				estrAppend2(&estrItems, ", ");
			estrConcatf(&estrItems, "%d %s", pMessage->ppItems[i]->count, REF_STRING_FROM_HANDLE(pMessage->ppItems[i]->hItem));
		}

		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_MAIL_GSL, "Message_Deleted", "SenderAccount @%s, ID %d, Subject <&%s&>, Body <&%s&>, Items(%s)", pMessage->pchSenderHandle, pMessage->uID, pMessage->pchSubject, pMessage->pchBody, estrItems);

		StructDestroyNoConst(parse_EmailV3Message, pMessage);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_DeleteMessage(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		AutoTrans_EmailV3_tr_DeleteMessage(LoggedTransactions_CreateManagedReturnValEnt("EmailV3DeleteMessage", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, pEnt->myContainerID, iMailID);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_ReturnToSender(Entity* pEnt, int iMailID, const char* pchSenderAccountHandle)
{
	if (pEnt && pEnt->pPlayer)
	{
		EmailV3ReturnToSenderWrapper* pWrapper = StructCreate(parse_EmailV3ReturnToSenderWrapper);
		pWrapper->uiSrcAccountID = entGetAccountID(pEnt);
		pWrapper->uiMailID = iMailID;
		pWrapper->uiRecipientContainerID = pEnt->myContainerID;
		RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
			objCreateManagedReturnVal(EmailV3_ReturnToSenderGetDstAccountID_CB, pWrapper),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pchSenderAccountHandle);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_MarkMessageAsRead(ATR_ARGS, NOCONST(Entity)* pSharedBank, int iMailID)
{
	NOCONST(EmailV3Message)* pMessage =  EmailV3_trh_GetMessageByID(ATR_PASS_ARGS, pSharedBank, iMailID);
	
	if (!pMessage)
		return TRANSACTION_OUTCOME_FAILURE;

	pMessage->bRead = true;

	return TRANSACTION_OUTCOME_SUCCESS;
}


void EmailV3_MarkMessageAsRead_CB(TransactionReturnVal *pReturn, ContainerID* cbData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		bool bHasUnread = false;
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, *cbData);
		if(pEnt && pEnt->pPlayer)
		{
			Entity* pBank = GET_REF(pEnt->pPlayer->hSharedBank);
			int i;
			if (pBank)
			{
				EmailV3* pEmail = EmailV3_GetSharedBankMail(pBank);
				if(pEmail)
				{
					for (i = 0; i < eaSize(&pEmail->eaMessages); i++)
					{
						if (!pEmail->eaMessages[i]->bRead)
							bHasUnread = true;
					}
					if (bHasUnread != pEmail->bUnreadMail)
						AutoTrans_EmailV3_tr_SetUnreadMailBit(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pBank->myContainerID, bHasUnread);
				}
			}
		}
	}
	if (cbData)
		free(cbData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_MarkMessageAsRead(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		ContainerID* pID = calloc(1, sizeof(ContainerID));
		*pID = pEnt->myContainerID;
		AutoTrans_EmailV3_tr_MarkMessageAsRead(LoggedTransactions_CreateManagedReturnValEnt("EmailV3MarkMessageAsRead", pEnt, EmailV3_MarkMessageAsRead_CB, pID), GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID);
	}
}
// This could potentially cause a SHITLOAD of transactions all at once when used by members of a large guild. Leaving it alone on orders from Jeff W.
AUTO_COMMAND ACMD_PRIVATE ACMD_NAME(guild_SendMail) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void EmailV3_SendGuildMail(Entity *pEnt, const char *pcSubject, const ACMD_SENTENCE pcBody)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildMember *pMember = pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
	if (pMember) {
		// If the player is on a trial account, they can't send guild mail
		if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_SEND_MAIL)) {
			ClientCmd_gclMailSentConfirm(pEnt, false, entTranslateMessageKey(pEnt, "Chat_NoTrialGuildMail"));
			return;
		}

		if (!guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildMail)) {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			entFormatGameMessageKey(pEnt, &estrBuffer, "ChatServer_NoGuildMailPermission", STRFMT_END);
			ClientCmd_gclMailSentConfirm(pEnt, false, estrBuffer);
			estrDestroy(&estrBuffer);
			return;
		}

		if (timeSecondsSince2000() - pGuild->iLastGuildMailTime < GUILD_MAIL_TIME) {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			entFormatGameMessageKey(pEnt, &estrBuffer, "ChatServer_GuildMailTooRecent", STRFMT_END);
			ClientCmd_gclMailSentConfirm(pEnt, false, estrBuffer);
			estrDestroy(&estrBuffer);
			return;
		}
		else 
		{
			EntityRef *pEntRef = NULL;
			int i;
			EmailV3NewMessageWrapper wrapper = {0};
			StashTable stAccountIDs = stashTableCreateInt(eaSize(&pGuild->eaMembers));
			
			wrapper.pMessage = EmailV3_CreateNewMessage(pcSubject, pcBody, pEnt, entGetLocalName(pEnt), pEnt->pPlayer ? pEnt->pPlayer->publicAccountName : NULL);
			wrapper.uSenderContainerID = pEnt->myContainerID;
			for (i = 0; i < eaSize(&pGuild->eaMembers); i++) {
				//Only send an email to each accountID once, regardless of how many of their characters are in the guild.
				// Added check to protect against ID zero which will assert in stash system
				if (pGuild->eaMembers[i]->iAccountID && stashIntAddInt(stAccountIDs, pGuild->eaMembers[i]->iAccountID, pGuild->eaMembers[i]->iAccountID, false))
				{
					if (gbEnableGamePlayDataLogging)
					{
						char *estrLog = NULL;
						gslMailLogTo(&estrLog, pGuild->eaMembers[i]->pcAccount, pcSubject);
						entLog(LOG_MAIL_GSL, pEnt, "Mail_Guild", "%s", estrLog);
						estrDestroy(&estrLog);
					}
					wrapper.uRecipientAccountID = pGuild->eaMembers[i]->iAccountID;
					AutoTrans_EmailV3_tr_SendMessageFromPlayerNoItems(LoggedTransactions_CreateManagedReturnValEnt("EmailV3SendMessage", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, wrapper.uRecipientAccountID, &wrapper, timeSecondsSince2000(), NULL, NULL);
				}
			}

			stashTableDestroy(stAccountIDs);

			AutoTrans_trUpdateGuildMailTimer(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, pGuild->iContainerID);

			StructDestroy(parse_EmailV3Message, wrapper.pMessage);
		}
	} else {
		ClientCmd_gclMailSentConfirm(pEnt, false, entTranslateMessageKey(pEnt, "ChatServer_MalformedMail"));
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pSharedBank, ".Pemailv3.Ulastusedid, .Pemailv3.Bunreadmail, .Pemailv3.Imessagecount, .Pemailv3.Iattachmentscount, .Pemailv3.Eamessages")
	ATR_LOCKS(pEnt, ".bNeedsChatMailFixup, .pPlayer.bDoneMailAuctionItemsFixup")
	ATR_LOCKS(eaAuctionLots, ".Icontainerid, .Ppitemsv2, .state");
enumTransactionOutcome EmailV3_tr_ConvertFromChatMailToSharedBank(ATR_ARGS, NOCONST(Entity)* pSharedBank, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(AuctionLot)) eaAuctionLots, EmailWrappers *pEmailWrappers)
{
	int i, j, k;
	int iEmailListSize, iAuctionListSize;

	if (ISNULL(pSharedBank) || ISNULL(pEnt))
		return TRANSACTION_OUTCOME_FAILURE;

	if (ISNULL(pEmailWrappers))
		return TRANSACTION_OUTCOME_FAILURE;

	if(ISNULL(pSharedBank->pEmailV3))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	iEmailListSize = eaSize(&pEmailWrappers->eaEmailWrappers);
	iAuctionListSize = eaSize(&eaAuctionLots);

	for(i = 0; i < iEmailListSize; i++)
	{
		EmailWrapper *pEmail = pEmailWrappers->eaEmailWrappers[i];
		if (pEmail)
		{
			NOCONST(EmailV3Message)* pEmailMessage = StructCreateNoConst(parse_EmailV3Message);

			pEmailMessage->uSent = pEmail->uSent;
			pEmailMessage->uExpireTime = pEmail->uExpireTime;
			pEmailMessage->pchSubject = StructAllocString(pEmail->pchSubject);
			pEmailMessage->pchBody = StructAllocString(pEmail->pchBody);
			pEmailMessage->pchSenderName = StructAllocString(pEmail->pchSenderName);
			pEmailMessage->eSenderType = pEmail->eTypeOfEmail;
			pEmailMessage->bRead = pEmail->bRead;

			if (pEmail->uLotID && NONNULL(eaAuctionLots))
			{
				for (j = 0; j < iAuctionListSize; j++)
				{
					if (NONNULL(eaAuctionLots[j]) && eaAuctionLots[j]->iContainerID == pEmail->uLotID)
					{
						for (k = 0; k < eaSize(&eaAuctionLots[j]->ppItemsV2); k++)
						{
							eaPush(&pEmailMessage->ppItems, StructCloneNoConst(parse_Item, eaAuctionLots[j]->ppItemsV2[k]->slot.pItem));
						}

						eaAuctionLots[j]->state = ALS_Closed;
						break;
					}
				}
			}

			if(EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pSharedBank, pEmailMessage, ++pSharedBank->pEmailV3->uLastUsedID) == TRANSACTION_OUTCOME_FAILURE)
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	pEnt->bNeedsChatMailFixup = false;

	if (NONNULL(pEnt->pPlayer))
	{
		pEnt->pPlayer->bDoneMailAuctionItemsFixup = true;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void TransferMailPartial_CB(TransactionReturnVal *pReturn, EmailWrappers *pEmailWrappers)
{
	if(pEmailWrappers)
	{
		int i, iSize;
		int iAccountID = pEmailWrappers->accountID;

		// We're done, so clean up the wrapper memory.
		iSize = eaSize(&pEmailWrappers->eaEmailWrappers);
		for (i = iSize - 1; i >= 0; i--)
		{
			EmailWrapper *pEmailWrapper = eaRemove(&pEmailWrappers->eaEmailWrappers, i);
			if (pEmailWrapper)
				StructDestroy(parse_EmailWrapper, pEmailWrapper);
		}

		if (pEmailWrappers)
		{
			StructDestroy(parse_EmailWrappers, pEmailWrappers);
		}
	}
}

static void TransferMail_CB(TransactionReturnVal *pReturn, EmailWrappers *pEmailWrappers)
{
	if(pEmailWrappers)
	{
		int i, iSize;
		int iAccountID = pEmailWrappers->accountID;

		// We're done, so clean up the wrapper memory.
		iSize = eaSize(&pEmailWrappers->eaEmailWrappers);
		for (i = iSize - 1; i >= 0; i--)
		{
			EmailWrapper *pEmailWrapper = eaRemove(&pEmailWrappers->eaEmailWrappers, i);
			if (pEmailWrapper)
				StructDestroy(parse_EmailWrapper, pEmailWrapper);
		}

		if (pEmailWrappers)
		{
			StructDestroy(parse_EmailWrappers, pEmailWrappers);
		}

		if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			RemoteCommand_chatCommandRemote_DeleteAllShardMail(GLOBALTYPE_CHATSERVER, 0, iAccountID);
		}
	}
}

#define MAIL_TRANSFER_LIMIT 40
#define MAIL_SIZE_LIMIT 200
static void EmailV3_ChatMailTransferSharedBankExists_CB(TransactionReturnVal *pReturn, EmailWrappers *pEmailWrappers)
{
	if (pEmailWrappers && pReturn && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		EmailWrappers *pTransactWrappers = StructCreate(parse_EmailWrappers);
		U32* eaAuctionLots = NULL;
		int i, s, count;

		pTransactWrappers->accountID = pEmailWrappers->accountID;
		pTransactWrappers->containerID = pEmailWrappers->containerID;
		ea32Create(&eaAuctionLots);
		s = eaSize(&pEmailWrappers->eaEmailWrappers);
		count = 0;

		for(i = 0; i < s; i++)
		{
			if (pEmailWrappers->eaEmailWrappers[i] && pEmailWrappers->eaEmailWrappers[i]->uLotID)
			{
				ea32Push(&eaAuctionLots, pEmailWrappers->eaEmailWrappers[i]->uLotID);
				count++;
			}

			eaPush(&pTransactWrappers->eaEmailWrappers, StructClone(parse_EmailWrapper, pEmailWrappers->eaEmailWrappers[i]));

			if (count >= MAIL_TRANSFER_LIMIT)
			{
				count = 0;

				AutoTrans_EmailV3_tr_ConvertFromChatMailToSharedBank(LoggedTransactions_CreateManagedReturnVal("EmailV3TransferMail", TransferMailPartial_CB, pTransactWrappers),
					GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pEmailWrappers->accountID,
					GLOBALTYPE_ENTITYPLAYER, pEmailWrappers->containerID, GLOBALTYPE_AUCTIONLOT, &eaAuctionLots, pTransactWrappers);

				pTransactWrappers = StructCreate(parse_EmailWrappers);
				pTransactWrappers->accountID = pEmailWrappers->accountID;
				pTransactWrappers->containerID = pEmailWrappers->containerID;

				ea32Destroy(&eaAuctionLots);
				eaAuctionLots = NULL;
				ea32Create(&eaAuctionLots);
			}
		}

		AutoTrans_EmailV3_tr_ConvertFromChatMailToSharedBank(LoggedTransactions_CreateManagedReturnVal("EmailV3TransferMail", TransferMail_CB, pTransactWrappers),
			GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pEmailWrappers->accountID,
			GLOBALTYPE_ENTITYPLAYER, pEmailWrappers->containerID, GLOBALTYPE_AUCTIONLOT, &eaAuctionLots, pTransactWrappers);

		ea32Destroy(&eaAuctionLots);

		// We're done, so clean up the wrapper memory.
		s = eaSize(&pEmailWrappers->eaEmailWrappers);
		for (i = s - 1; i >= 0; i--)
		{
			EmailWrapper *pEmailWrapper = eaRemove(&pEmailWrappers->eaEmailWrappers, i);
			if (pEmailWrapper)
				StructDestroy(parse_EmailWrapper, pEmailWrapper);
		}

		if (pEmailWrappers)
		{
			StructDestroy(parse_EmailWrappers, pEmailWrappers);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void gslMail_ReceiveMailWrapperForTransfer(int iAccountID, int iContainerID, EmailWrappers *pEmailWrappers)
{
	SharedBankCBData *cbData = NULL;
	cbData = calloc(sizeof(SharedBankCBData), 1);
	cbData->ownerType = 0;
	cbData->ownerID = 0;
	cbData->accountID = iAccountID;

	pEmailWrappers->accountID = iAccountID;
	pEmailWrappers->containerID = iContainerID;

	cbData->pUserData = StructClone(parse_EmailWrappers, pEmailWrappers);
	cbData->pFunc = EmailV3_ChatMailTransferSharedBankExists_CB;

	RemoteCommand_DBCheckAccountWideContainerExistsWithRestore(objCreateManagedReturnVal(RestoreSharedBank_CB, cbData), GLOBALTYPE_OBJECTDB, 0, iAccountID, GLOBALTYPE_ENTITYSHAREDBANK);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void gsl_TransferMail(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		Entity *pSharedBank = GET_REF(pEnt->pPlayer->hSharedBank);
		int iAccountID = pEnt->pPlayer->accountID;

		if (pSharedBank && pSharedBank->pEmailV3 && eaSize(&pSharedBank->pEmailV3->eaMessages) >= MAIL_SIZE_LIMIT)
			return;

		RemoteCommand_chatCommandRemote_TransferAndDeleteShardMail(GLOBALTYPE_CHATSERVER, 0, iAccountID);
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pSharedBank, ".pEmailV3.iMessageCount, .pEmailV3.iAttachmentsCount");
enumTransactionOutcome EmailV3_tr_SetMessageCount(ATR_ARGS, NOCONST(Entity)* pSharedBank, int iMessageCount, int iAttachmentsCount)
{
	if (ISNULL(pSharedBank) || ISNULL(pSharedBank->pEmailV3))
		return TRANSACTION_OUTCOME_FAILURE;

	if (iMessageCount < 0 || iAttachmentsCount < 0)
		return TRANSACTION_OUTCOME_FAILURE;

	pSharedBank->pEmailV3->iMessageCount = iMessageCount;
	pSharedBank->pEmailV3->iAttachmentsCount = iAttachmentsCount;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pSharedBank, ".Pemailv3");
enumTransactionOutcome EmailV3_tr_CreateSharedBankMail(ATR_ARGS, NOCONST(Entity) *pSharedBank)
{
	(void)EmailV3_trh_GetOrCreateSharedBankMail(ATR_PASS_ARGS, pSharedBank, true);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void gsl_SetMailMessageCount(Entity *pEnt)
{
	Entity* pSharedBank;
	int iMessageCount = 0;
	int iAttachmentsCount = 0;
	int i;

	if (!pEnt->pPlayer || !IS_HANDLE_ACTIVE(pEnt->pPlayer->hSharedBank))
		return;

	pSharedBank = GET_REF(pEnt->pPlayer->hSharedBank);

	if (!pSharedBank)
		return;

	if(!pSharedBank->pEmailV3)
	{
		// do the create of the struct (also sets counts)
		AutoTrans_EmailV3_tr_CreateSharedBankMail(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYSHAREDBANK, pSharedBank->myContainerID);
		return;
	}

	if (pSharedBank->pEmailV3->iMessageCount >= 0 &&
		pSharedBank->pEmailV3->iAttachmentsCount >= 0)
		return;

	for (i = 0; i < eaSize(&pSharedBank->pEmailV3->eaMessages); i++)
	{
		if (pSharedBank->pEmailV3->eaMessages[i]->eSenderType == kEmailV3Type_Player)
		{
			iMessageCount++;
			if (eaSize(&pSharedBank->pEmailV3->eaMessages[i]->ppItems) > 0)
				iAttachmentsCount++;
		}
	}

	AutoTrans_EmailV3_tr_SetMessageCount(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYSHAREDBANK, pSharedBank->myContainerID, iMessageCount, iAttachmentsCount);
}

#include "AutoGen/gslmail_h_ast.c"
