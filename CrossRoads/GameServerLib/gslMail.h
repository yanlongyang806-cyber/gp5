#pragma once

#include "MailCommon.h"

AUTO_STRUCT;
typedef struct EmailV3NewMessageWrapper
{
	EmailV3SenderItem** eaItemsFromPlayer;
	Item** eaItemsFromNPC;
	EmailV3Message* pMessage;
	U32 uSenderContainerID;
	const char* pchRecipientHandle;
	U32 uRecipientAccountID;
} EmailV3NewMessageWrapper;

AUTO_STRUCT;
typedef struct EmailV3ReturnToSenderWrapper
{
	U32 uiSrcAccountID;
	U32 uiDstAccountID;
	U32 uiMailID;
	U32 uiRecipientContainerID;
} EmailV3ReturnToSenderWrapper;

void EmailV3_SendPlayerEmail(const char* pchSubject,
	const char* pchBody, 
	Entity* pSender,
	const char* pchRecipientHandle, 
	EmailV3SenderItemsWrapper* pWrapper);

void EmailV3_DeleteMessage(Entity* pEnt, int iMailID);

void gsl_TransferMail(Entity *pEnt);

void gsl_SetMailMessageCount(Entity *pEnt);