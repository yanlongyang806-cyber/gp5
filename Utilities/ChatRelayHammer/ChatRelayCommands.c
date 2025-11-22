#include "chatRelayCommands.h"

#include "chatCommonFake.h"
#include "cmdparse.h"
#include "GlobalComm.h"
#include "net.h"

static void crh_SendPrivateCommand(NetLink *link, U32 uAccountID, const char *cmdString)
{
	Packet *pak = pktCreate(link, TOSERVER_GAME_MSG);
	pktSendU32(pak, CHATRELAY_CMD_PRIVATE);
	pktSendU32(pak, uAccountID);
	cmdParsePutStructListIntoPacket(pak, NULL, NULL);
	pktSendString(pak, cmdString);
	pktSendBits(pak, 32, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED);
	pktSendBits(pak, 32, CMD_CONTEXT_HOWCALLED_SERVERWRAPPER);
	pktSend(&pak);
}

static void crh_SendPublicCommand(NetLink *link, U32 uAccountID, const char *cmdString)
{
	Packet *pak = pktCreate(link, TOSERVER_GAME_MSG);
	pktSendU32(pak, CHATRELAY_CMD_PUBLIC);
	pktSendU32(pak, uAccountID);
	cmdParsePutStructListIntoPacket(pak, NULL, NULL);
	pktSendString(pak, cmdString);
	pktSendBits(pak, 32, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED);
	pktSendBits(pak, 32, CMD_CONTEXT_HOWCALLED_SERVERWRAPPER);
	pktSend(&pak);
}

static void crh_SendMessage(NetLink *link, U32 uAccountID, ChatMessage *pMsg)
{
	char* cmdString = NULL;

	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crSendMessage ");
	estrConcatf(&cmdString, " ");
	if (pMsg)
		ParserWriteTextEscaped(&cmdString, parse_ChatMessage, pMsg, 0, 0, 0);
	else
		estrConcatf(&cmdString, "<& __NULL__ &>");

	crh_SendPrivateCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_SendPrivateMessage(NetLink *link, U32 uFromID, U32 uToID, const char *msg)
{
	ChatUserInfo toInfo = {0};
	ChatMessage *pChatMsg;

	toInfo.accountID = uToID;
	pChatMsg = ChatCommon_CreateMsg(NULL, &toInfo, kChatLogEntryType_Private, NULL, msg, NULL);

	crh_SendMessage(link, uFromID, pChatMsg);
	pChatMsg->pTo = NULL;
	StructDestroy(parse_ChatMessage, pChatMsg);
}

void crh_SendChannelMessage(NetLink *link, U32 uFromID, const char *channel_name, const char *msg)
{
	ChatMessage *pChatMsg = ChatCommon_CreateMsg(NULL, NULL, kChatLogEntryType_Channel, channel_name, msg, NULL);
	crh_SendMessage(link, uFromID, pChatMsg);
	StructDestroy(parse_ChatMessage, pChatMsg);
}

void crh_ChannelJoinOrCreate(NetLink *link, U32 uFromID, const char *channel_name)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crJoinOrCreateChannel ");
	estrAppend2(&cmdString, " \"");
	if (channel_name) estrAppendEscaped(&cmdString, channel_name);
	estrAppend2(&cmdString, "\" ");
	crh_SendPrivateCommand(link, uFromID, cmdString);
	estrDestroy(&cmdString);
}

void crh_ChannelRequestList(NetLink *link, U32 uAccountID)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crRequestUserChannelList ");
	estrConcatf(&cmdString, " %d ", USER_CHANNEL_SUBSCRIBED | USER_CHANNEL_INVITED);
	crh_SendPrivateCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_Login(NetLink *link, U32 uAccountID)
{
	char* cmdString = NULL;
	ChatLoginData loginData = {0};
	PlayerInfoStruct playerInfo = {0};
	loginData.pPlayerInfo = &playerInfo;
	loginData.pPlayerInfo->eLanguage = LANGUAGE_ENGLISH;

	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crUserLogin ");
	estrConcatf(&cmdString, " ");
	ParserWriteTextEscaped(&cmdString, parse_ChatLoginData, &loginData, 0, 0, 0);
	crh_SendPrivateCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_RequestFriendList(NetLink *link, U32 uAccountID)
{
	crh_SendPrivateCommand(link, uAccountID, "crRequestFriendList");
}
void crh_RequestIgnoreList(NetLink *link, U32 uAccountID)
{
	crh_SendPrivateCommand(link, uAccountID, "crRequestIgnoreList");
}

void crh_AddFriend(NetLink *link, U32 uAccountID, U32 uTargetAccountID)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crAddFriend ");
	estrConcatf(&cmdString, "%d \"\" 0", uTargetAccountID);
	crh_SendPublicCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_RemoveFriend(NetLink *link, U32 uAccountID, U32 uTargetAccountID)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crRemoveFriend ");
	estrConcatf(&cmdString, "%d \"\" 0", uTargetAccountID);
	crh_SendPublicCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_AddIgnore(NetLink *link, U32 uAccountID, U32 uTargetAccountID)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crAddIgnore ");
	estrConcatf(&cmdString, "%d \"\" 0", uTargetAccountID);
	crh_SendPublicCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_RemoveIgnore(NetLink *link, U32 uAccountID, U32 uTargetAccountID)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crRemoveIgnore ");
	estrConcatf(&cmdString, "%d \"\"", uTargetAccountID);
	crh_SendPublicCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_SendPlayerUpdate(NetLink *link, U32 uAccountID, U32 *piAccountIDs)
{
	char *cmdString = NULL;
	PlayerInfoStruct playerInfo = {0};
	ChatContainerIDList list = {0};
	playerInfo.eLanguage = LANGUAGE_ENGLISH;
	playerInfo.bIsDev = true;
	playerInfo.bGoldSubscriber = true;
	list.piContainerIDList = piAccountIDs;

	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crDebugForwardPlayerInfoToUsers ");
	estrConcatf(&cmdString, " ");
	ParserWriteTextEscaped(&cmdString, parse_PlayerInfoStruct, &playerInfo, 0, 0, 0);
	estrConcatf(&cmdString, " ");
	ParserWriteTextEscaped(&cmdString, parse_ChatContainerIDList, &list, 0, 0, 0);
	crh_SendPrivateCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}

void crh_PurgeChannels(NetLink *link, U32 uAccountID)
{
	char *cmdString = NULL;
	estrStackCreate(&cmdString);
	estrCopy2(&cmdString, "crPurgeChannels ");
	crh_SendPrivateCommand(link, uAccountID, cmdString);
	estrDestroy(&cmdString);
}