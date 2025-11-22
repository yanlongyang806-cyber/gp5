#include "ChatCommands.h"

#include "chatCommonFake.h"
#include "ChatHammer.h"
#include "earray.h"
#include "EString.h"
#include "GlobalComm.h"
#include "mathutil.h"
#include "net.h"
#include "rand.h"
#include "SimpleParser.h"
#include "textparser.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatHammer_h_ast.h"

extern FakeChannelData **gpChannelData;

void RegisterShard(NetLink *link)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_REGISTER_SHARD);
	pktSendString(pkt, CHATHAMMER_SHARDNAME);
	pktSendString(pkt, "ChatHammer"); // category
	pktSendString(pkt, "ChatHammer"); // product
	ChatHammer_pktSend(&pkt);
}

void CreateUser(NetLink *link, int id, const char *account_name, const char *display_name)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerLogin %d %d \"%s\" \"%s\" %d", id, 0, account_name, display_name, 0);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);	
}

static char *defaultPlayerInfoString = NULL;
void CreateUserUpdate (char **estr, int id)
{	
	if (!defaultPlayerInfoString)
	{
		PlayerInfoStruct info = {0};
		info.onlineCharacterID = 1;
		info.onlinePlayerName = strdup("FakeCharacter");
		info.shardName = strdup(CHATHAMMER_SHARDNAME);
		ParserWriteTextEscaped(&defaultPlayerInfoString, parse_PlayerInfoStruct, &info, 0, 0, 0);
		StructDeInit(parse_PlayerInfoStruct, &info);
	}
	estrPrintf(estr, "ChatServerPlayerUpdate %d \"%s\"", id, defaultPlayerInfoString);
}

void UserUpdate(NetLink *link, int id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	
	if (!defaultPlayerInfoString)
	{
		PlayerInfoStruct info = {0};
		info.onlineCharacterID = 1;
		info.onlinePlayerName = strdup("FakeCharacter");
		info.shardName = strdup(CHATHAMMER_SHARDNAME);
		ParserWriteTextEscaped(&defaultPlayerInfoString, parse_PlayerInfoStruct, &info, 0, 0, 0);
		StructDeInit(parse_PlayerInfoStruct, &info);
	}
	estrCreate(&command_string);
	CreateUserUpdate(&command_string, id);
	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);	
}

void LogoutUser(NetLink *link, int id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerLogout %d", id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);	
}

/////////////////////////////////////
// Channel commands

void CreateChannelByID(NetLink *link, int user_id, int channel_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;

	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerCreateChannel %d \"channel%d\" 0 0", user_id, channel_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void CreateChannel(NetLink *link, int user_id, int channel_id)
{
	FakeChannelData *data = StructCreate(parse_FakeChannelData);

	data->channel_id = channel_id;
	data->member_count = 0;
	data->owner_id = user_id;
	eaPush(&gpChannelData, data);
	CreateChannelByID(link, user_id, channel_id);
}

void JoinChannelById(NetLink *link, int user_id, int channel_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerJoinChannel %d \"channel%d\" 0 0", user_id, channel_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void JoinChannel(NetLink *link, int user_id, int channel_index)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerJoinChannel %d \"channel%d\" 0 0", user_id, gpChannelData[channel_index]->channel_id);

	if(gpChannelData[channel_index]->member_count < 100) // Putting too much limitation on channel joining.
		gpChannelData[channel_index]->members[gpChannelData[channel_index]->member_count++] = user_id;

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void LeaveChannel(NetLink *link, int user_id, int channel_index)
{
	int i;
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerLeaveChannel %d \"channel%d\"", user_id, gpChannelData[channel_index]->channel_id);

	for(i = 0; i < gpChannelData[channel_index]->member_count && i < 100; ++i)
	{
		if(gpChannelData[channel_index]->members[i] == user_id)
		{
			--gpChannelData[channel_index]->member_count;

			while(i < gpChannelData[channel_index]->member_count)
			{
				gpChannelData[channel_index]->members[i] = gpChannelData[channel_index]->members[i+1];
				++i;
			}
		}
	}

	if(gpChannelData[channel_index]->member_count == 0)
	{
		StructDestroy(parse_FakeChannelData, gpChannelData[channel_index]);
		eaRemove(&gpChannelData, channel_index);
	}

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

/////////////////////////////////////
// Message commands

void CreatePrivateMessage(char **estr, int from_user_id, int to_user_id, const char *text)
{
	ChatMessage *msg = StructCreate(parse_ChatMessage);
	msg->pFrom = StructCreate(parse_ChatUserInfo);
	msg->pFrom->accountID = from_user_id;
	msg->pFrom->playerID = from_user_id;
	estrCopy2(&msg->pFrom->pchHandle, "");
	estrCopy2(&msg->pFrom->pchName, "");

	msg->pTo = StructCreate(parse_ChatUserInfo);
	msg->pTo->accountID = to_user_id;
	msg->pTo->playerID = to_user_id;
	estrCopy2(&msg->pTo->pchHandle, "");
	estrCopy2(&msg->pTo->pchName, "");

	msg->eType = kChatLogEntryType_Private;
	estrCopy2(&msg->pchChannel, PRIVATE_CHANNEL_NAME);
	estrCopy2(&msg->pchText, text);

	estrPrintf(estr, "ChatServerPrivateMesssage ");
	ParserWriteTextEscaped(estr, parse_ChatMessage, msg, 0, 0, 0);
	StructDestroy(parse_ChatMessage, msg);
}

void CreateChannelMessage(char **estr, int from_user_id, int channel, const char *text)
{
	ChatMessage *msg = StructCreate(parse_ChatMessage);
	msg->pFrom = StructCreate(parse_ChatUserInfo);
	msg->pFrom->accountID = from_user_id;
	msg->pFrom->playerID = from_user_id;
	estrCopy2(&msg->pFrom->pchHandle, "");
	estrCopy2(&msg->pFrom->pchName, "");
	
	msg->eType = kChatLogEntryType_Channel;
	estrPrintf(&msg->pchChannel, "channel%d", channel);
	estrCopy2(&msg->pchText, text);

	estrPrintf(estr, "ChatServerMessageReceive ");
	ParserWriteTextEscaped(estr, parse_ChatMessage, msg, 0, 0, 0);
	StructDestroy(parse_ChatMessage, msg);
}

void SendChannelMessage(NetLink *link, int from_user_id, int channel, const char *text)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	ChatMessage *msg;
	char *command_string = NULL;
	const char *pchTmpText;

	msg = StructCreate(parse_ChatMessage);

	msg->pFrom = StructCreate(parse_ChatUserInfo);
	msg->pFrom->accountID = from_user_id;
	msg->pFrom->playerID = from_user_id;
	estrCopy2(&msg->pFrom->pchHandle, "");
	estrCopy2(&msg->pFrom->pchName, "");

	msg->eType = kChatLogEntryType_Channel;
	estrPrintf(&msg->pchChannel, "channel%d", channel);

	pchTmpText = removeLeadingWhiteSpaces(text);
	estrCopy2(&msg->pchText, pchTmpText);
	removeTrailingWhiteSpaces(msg->pchText);

	estrPrintf(&command_string, "ChatServerPrivateMesssage ");
	ParserWriteTextEscaped(&command_string, parse_ChatMessage, msg, 0, 0, 0);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);
	estrDestroy(&command_string);
	StructDestroy(parse_ChatMessage, msg);
}

/////////////////////////////////////
// Info Request commands

void RequestChannelList(NetLink *link, int user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerChannelList %d", user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void RequestChannelMemberList(NetLink *link, int user_id, int channel_index)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerChannelListMembers %d \"channel%d\"", user_id, gpChannelData[channel_index]->channel_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

/////////////////////////////////////
// Channel Admin commands

void ChangeMotd(NetLink *link, int channel_index, const char *text)
{
	int account_id = gpChannelData[channel_index]->members[randomIntRange(0, gpChannelData[channel_index]->member_count)];
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerSetMotd %d \"channel%d\" \"%s\"", account_id, gpChannelData[channel_index]->channel_id, text);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void PromoteUser(NetLink *link, int account_id, int channel_index)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;

	if(gpChannelData[channel_index]->member_count == 0)
		account_id = randomIntRange(0, next_user_id - 1);

	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerSetUserAccessByID %d \"channel%d\" %d %d %d", gpChannelData[channel_index]->owner_id, gpChannelData[channel_index]->channel_id, account_id, CHANPERM_PROMOTE, 0);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

/////////////////////////////////////
// Friend commands

void AddFriend(NetLink *link, int from_user_id, int to_user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerAddFriendByID %d %d", from_user_id, to_user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void RejectFriend(NetLink *link, int from_user_id, int to_user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerRejectFriendByID %d %d", from_user_id, to_user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void RemoveFriend(NetLink *link, int from_user_id, int to_user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerRemoveFriendByID %d %d", from_user_id, to_user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void AddIgnore(NetLink *link, int from_user_id, int to_user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerAddIgnoreByID %d %d %d", from_user_id, to_user_id, false);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void RemoveIgnore(NetLink *link, int from_user_id, int to_user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerRemoveIgnoreByID %d %d", from_user_id, to_user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

void RetrieveFriendsList(NetLink *link, int user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerGetFriendsList %d", user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}