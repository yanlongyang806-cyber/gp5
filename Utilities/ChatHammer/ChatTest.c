#include "ChatTest.h"

#include "accountnet.h"
#include "ChatCommands.h"
#include "chatCommonFake.h"
#include "ChatHammer.h"
#include "cmdparse.h"
#include "GlobalTypes.h"
#include "MemoryPool.h"
#include "objPath.h"
#include "rand.h"
#include "SimpleParser.h"
#include "sock.h"
#include "structNet.h"


#include "AutoGen/ChatTest_h_ast.h"
#include "AutoGen/ChatHammer_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"

extern int giDelay;
extern U32 giTestLength;
ChatTestParameters gChatTestParams = {0};

int next_user_id = 1;
int next_channel_id = 1;

extern bool gbDebugText;

extern U32 gTestStartTime;

FakeChannelData **gpChannelData = NULL;

ChatTestCase ChatTests[] = 
{
	//{"Recreate Channels", NULL, &RecreateChannels, &gChatTestParams.iChannels},
	//{"Join/Leave Channels", NULL, &JoinLeaveChannels, &gChatTestParams.iJoins},
	//{"Join Too Many Channels", &InitJoinTooManyChannels, &JoinTooManyChannels, &gChatTestParams.iJoins},
	//{"Join Nonexistent Channels", &InitJoinNonexistentChannels, &JoinNonexistentChannels, &gChatTestParams.iJoins},
	//{"Leave Unwatched Channels", &InitLeaveUnwatchedChannels, &LeaveUnwatchedChannels, &gChatTestParams.iLeaves},
	//{"Join Channels", NULL, &JoinChannels, &gChatTestParams.iJoins},
	//{"Leave Channels", NULL, &LeaveChannels, &gChatTestParams.iLeaves},
	{"Send Tells", NULL, &SendTells, &gChatTestParams.iTells},
	//{"Send Tells to nonexistent users", NULL, &SendNonexistentTells, &gChatTestParams.iTells},
	//{"Send Tells to offline users", &InitOfflineUsers, &SendOfflineTells, &gChatTestParams.iTells},
	//{"Send Messages", NULL, &SendChannelMessages, &gChatTestParams.iChannelMessages},
	//{"Send Messages to unwatched channels", &InitUnwatchedChannel, &SendUnwatchedChannelMessages, &gChatTestParams.iChannelMessages},
	//{"Request Channel Lists", NULL, &RequestChannelLists, &gChatTestParams.iChannelListRequests},
	//{"Request Channel Member Lists", NULL, &RequestChannelMemberLists, &gChatTestParams.iChannelMemberListRequests},
	//{"Request Unwatched Channel Member Lists", &InitUnwatchedChannel, &RequestUnwatchedChannelMemberLists, &gChatTestParams.iChannelMemberListRequests},
	//{"Change Motd", NULL, &ChangeMotds, &gChatTestParams.iMotdChanges},
	//{"Promote users", NULL, &PromoteUsers, &gChatTestParams.iOperatorChanges},
	//{"Promote non watching users", &InitUnwatchedChannel, &PromoteNonWatchingUsers, &gChatTestParams.iOperatorChanges},
};

int GetChatTestSize()
{
	return sizeof(ChatTests)/sizeof(ChatTestCase);
}

//////////////////////////////////////////////////////////////////////////
// Basic Initialization

bool InitUsersDone(void)
{
	return (next_user_id >= gChatTestParams.iTotalUsers);
}
bool InitChannelsDone(void)
{
	return (next_channel_id >= gChatTestParams.iTotalChannels);
}

bool InitUsers(NetLink *link, const int count)
{
	int i;
	if(!gbInitDB)
	{
		next_user_id = gChatTestParams.iTotalUsers + 1;
		return true;
	}

	for(i = 0; i < count; ++i, ++next_user_id)
	{
		char *display_name = NULL;

		if(next_user_id > gChatTestParams.iTotalUsers)
			return true;

		estrPrintf(&display_name, "ChatTestUser%d", next_user_id);

		CreateUser(link, next_user_id, display_name, display_name);
		UserUpdate(link, next_user_id);
		estrDestroy(&display_name); 
	}
	return false;
}

bool InitChannels(NetLink *link, const int count)
{
	int i;
	if(!gbInitDB)
	{
		next_channel_id = gChatTestParams.iTotalChannels + 1;
		return true;
	}
	for(i = 0; i < count; ++i, ++next_channel_id)
	{
		if(next_channel_id > gChatTestParams.iTotalChannels)
			return true;
		CreateChannel(link, randomIntRange(1, next_user_id - 1), next_channel_id);
	}
	return false;
}

bool InitFriends(NetLink *link, const int count)
{
	static int iNextFriend = 2;
	int i;
	if(!gbInitDB)
		return true;
	for (i=0; i<count; ++i)
	{
		if (iNextFriend + i > gChatTestParams.iFriendsPerList + 2)
		{
			iNextFriend += i;
			return true;
		}
		AddFriend(link, 1, iNextFriend + i);
		AddFriend(link, iNextFriend + i, 1);
	}
	iNextFriend += count;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static int gJoinTooManyChannelsUserId;

void InitJoinTooManyChannels(NetLink *link)
{
	gJoinTooManyChannelsUserId = (next_user_id > 1) ? randomIntRange(1, next_user_id - 1) : 0;
}

static int gJoinNonexistentChannelsUserId;

void InitJoinNonexistentChannels(NetLink *link)
{
	gJoinNonexistentChannelsUserId = (next_user_id > 1) ? randomIntRange(1, next_user_id - 1) : 0;
}

static int gLeaveUnwatchedChannelsUserId;
static int gLeaveUnwatchedChannelsChannelIndex;

void InitLeaveUnwatchedChannels(NetLink *link)
{
	gLeaveUnwatchedChannelsUserId = (next_user_id > 1) ? randomIntRange(1, next_user_id - 1) : 0;
	gLeaveUnwatchedChannelsChannelIndex = 0;
}

bool CreateUsers(NetLink *link, const int count)
{
	int i;
	if(!gbInitDB)
	{
		next_user_id = gChatTestParams.iTotalUsers + 1;
		return true;
	}

	for(i = 0; i < count; ++i, ++next_user_id)
	{
		char *display_name = NULL;

		if(next_user_id > gChatTestParams.iTotalUsers)
			return true;

		estrPrintf(&display_name, "ChatTestUser%d", next_user_id);

		CreateUser(link, next_user_id, display_name, display_name);
		estrDestroy(&display_name); 
	}
	return false;
}

bool CreateChannels(NetLink *link, const int count)
{
	int i;
	if(!gbInitDB)
	{
		next_channel_id = gChatTestParams.iTotalChannels + 1;
		return true;
	}
	for(i = 0; i < count; ++i, ++next_channel_id)
	{
		if(next_channel_id > gChatTestParams.iTotalChannels)
			return true;
		CreateChannel(link, randomIntRange(1, next_user_id - 1), next_channel_id);
	}
	return false;
}

bool RecreateChannels(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		CreateChannelByID(link, randomIntRange(1, next_user_id - 1), gpChannelData[randomIntRange(0, eaSize(&gpChannelData) - 1)]->channel_id);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool JoinChannels(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (eaSize(&gpChannelData) < 1)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		JoinChannel(link, randomIntRange(1, next_user_id - 1), randomIntRange(0, eaSize(&gpChannelData) - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool JoinTooManyChannels(NetLink *link, const int count)
{
	int i;
	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		JoinChannel(link, gJoinTooManyChannelsUserId, randomIntRange(0, eaSize(&gpChannelData) - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool JoinNonexistentChannels(NetLink *link, const int count)
{
	int i;
	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0))
		return true;

	for(i = 0; i < count; ++i)
	{
		JoinChannelById(link, gJoinNonexistentChannelsUserId, next_channel_id);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool JoinLeaveChannels(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0)) 
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		int user_id = randomIntRange(1, next_user_id - 1);
		int channel_index = randomIntRange(0, eaSize(&gpChannelData) - 1);
		JoinChannel(link, user_id, channel_index);
		LeaveChannel(link, user_id, channel_index);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool LeaveChannels(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		LeaveChannel(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, eaSize(&gpChannelData) - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool LeaveUnwatchedChannels(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		LeaveChannel(link, gLeaveUnwatchedChannelsUserId, gLeaveUnwatchedChannelsChannelIndex);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendTells(NetLink *link, const int count)
{
	int i;
	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		char *pCommandString = NULL;
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		CreatePrivateMessage(&pCommandString, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1), "This is a test message of some intermediate length. This is a test message of some intermediate length.");
		pktSendString(pkt, pCommandString);
		ChatHammer_pktSend(&pkt);	
		estrDestroy(&pCommandString);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendNonexistentTells(NetLink *link, const int count)
{
	int i;
	if(next_user_id <= 1)
		return true;
	for(i = 0; i < count; ++i)
	{
		char *pCommandString = NULL;
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		CreatePrivateMessage(&pCommandString, randomIntRange(1, next_user_id - 1), next_user_id, "This is a test message of some intermediate length. This is a test message of some intermediate length.");
		pktSendString(pkt, pCommandString);
		ChatHammer_pktSend(&pkt);	
		estrDestroy(&pCommandString);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gOfflineTellsUserId;

void InitOfflineUsers(NetLink *link)
{
	gOfflineTellsUserId = (next_user_id > 1) ? randomIntRange(1, next_user_id - 1) : 0;
	LogoutUser(link, gOfflineTellsUserId);
}

bool SendOfflineTells(NetLink *link, const int count)
{
	char *pCommandString = NULL;
	int i;
	if(next_user_id <= 1)
		return true;
	CreatePrivateMessage(&pCommandString, 1, gOfflineTellsUserId, "This is a test message of some intermediate length. This is a test message of some intermediate length.");
	for(i = 0; i < count; ++i)
	{
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		pktSendString(pkt, pCommandString);
		ChatHammer_pktSend(&pkt);	
	}
	estrDestroy(&pCommandString);
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendChannelMessages(NetLink *link, const int count)
{
	int i;

	if(eaSize(&gpChannelData) == 0)
		return true;

	for(i = 0; i < count; ++i)
	{
		int channel_index = randomIntRange(1, eaSize(&gpChannelData) - 1);
		int from_user_id = gpChannelData[channel_index]->members[randomIntRange(0, gpChannelData[channel_index]->member_count) - 1];
		SendChannelMessage(link, from_user_id, channel_index, "blah blah blah");
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gUnwatchedChannelsUserId;
static int gUnwatchedChannelsChannelIndex;

void InitUnwatchedChannel(NetLink *link)
{
	gUnwatchedChannelsUserId = randomIntRange(1, next_user_id - 1);
	gUnwatchedChannelsChannelIndex = randomIntRange(1, eaSize(&gpChannelData) - 1);
	LeaveChannel(link, gUnwatchedChannelsUserId, gUnwatchedChannelsChannelIndex);
}

bool SendUnwatchedChannelMessages(NetLink *link, const int count)
{
	int i;

	if(next_channel_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendChannelMessage(link, gUnwatchedChannelsUserId, gUnwatchedChannelsChannelIndex, "blah blah blah");
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RequestChannelLists(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		RequestChannelList(link, randomIntRange(1, next_user_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RequestChannelMemberLists(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (next_channel_id <= 1)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		RequestChannelMemberList(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_channel_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RequestUnwatchedChannelMemberLists(NetLink *link, const int count)
{
	int i;

	if((next_user_id <= 1) || (eaSize(&gpChannelData) == 0)) 
		return true;

	for(i = 0; i < count; ++i)
	{
		RequestChannelMemberList(link, gUnwatchedChannelsUserId, gUnwatchedChannelsChannelIndex);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool ChangeMotds(NetLink *link, const int count)
{
	int i;

	if(eaSize(&gpChannelData) == 0)
		return true;

	for(i = 0; i < count; ++i)
	{
		ChangeMotd(link, randomIntRange(1, eaSize(&gpChannelData) - 1), "New MOTD");
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool PromoteUsers(NetLink *link, const int count)
{
	int i;

	if(eaSize(&gpChannelData) == 0)
		return true;

	for(i = 0; i < count; ++i)
	{
		int channel_index = randomIntRange(1, eaSize(&gpChannelData) - 1);
		int account_id = gpChannelData[channel_index]->members[randomIntRange(0, gpChannelData[channel_index]->member_count)];
		PromoteUser(link, account_id, channel_index);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool PromoteNonWatchingUsers(NetLink *link, const int count)
{
	int i;

	if(eaSize(&gpChannelData) == 0)
		return true;

	for(i = 0; i < count; ++i)
	{
		PromoteUser(link, gUnwatchedChannelsUserId, gUnwatchedChannelsChannelIndex);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

#include "AutoGen/ChatTest_h_ast.c"