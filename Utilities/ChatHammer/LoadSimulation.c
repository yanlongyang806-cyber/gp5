#include "accountnet.h"
#include "structNet.h"
#include "GlobalTypes.h"
#include "cmdparse.h"
#include "objPath.h"
#include "MemoryPool.h"
#include "sock.h"
#include "chatCommonFake.h"
#include "rand.h"

#include "ChatHammer.h"
#include "ChatTest.h"
#include "FriendTest.h"
#include "MailTest.h"

#include "ChatHammer_h_ast.h"
#include "LoadSimulation_c_ast.h"

#include "Autogen/chatCommonStructs_h_ast.h"

char gLoadSimulationConfig[128] = "LoadSimulation.cfg";
AUTO_CMD_STRING(gLoadSimulationConfig, LoadSimulationConfigFile);

AUTO_STRUCT;
typedef struct LoadSimulationStruct
{
	U32 TestLength; // in hours
	int BaseUsers;
	int BaseChannels;
	int BaseFriendsLists;
	int BaseFriends;
	int UsersOnline;

	F32 User_Creates;
	F32 Channel_Creates;
	F32 Channel_Recreates;
	F32 Channel_ExtraJoins;
	F32 Channel_IncorrectJoins;
	F32 Channel_IncorrectLeaves;
	F32 Channel_Joins;
	F32 Channel_Leaves;
	F32 Tell_Sends;
	F32 Tell_NonexistentSends;
	F32 Tell_OfflineSends;
	F32 Message_Sends;
	F32 Message_IncorrectSends;
	F32 ChannelList_Requests;
	F32 ChannelMemberList_Requests;
	F32 ChannelMemberList_IncorrectRequests;
	F32 Motd_Changes;
	F32 Promote_User;
	F32 Promote_IncorrectUser;
	F32 Friend_AddRemove;
	F32 Friend_Readd;
	F32 Friend_AddIgnored;
	F32 Friend_AddIncorrect;
	F32 Friend_Reject;
	F32 Friend_Reremove;
	F32 Friend_IncorrectRemove;
	F32 Friend_GetList;
	F32 Ignore_AddRemove;
	F32 Ignore_AddIgnored;
	F32 Ignore_AddIncorrect;
	F32 Ignore_Reremove;
	F32 Ignore_RemoveIncorrect;
	F32 Mail_SendBasic;
	F32 Mail_SendBasicToNobody;
	F32 Mail_SendItem;
	F32 Mail_SendItemToNobody;
	F32 Mail_SendEmpty;
	F32 Mail_Get;
	F32 Mail_SetRead;
	F32 Mail_DeleteBasic;
	F32 Mail_DeleteItem;
} LoadSimulationStruct;

static U32 gLoadSimulationStartTime;
static LoadSimulationStruct *lss;
extern ChatTestParameters gChatTestParams;

bool IsLoadSimulationDone()
{
	U32 cur_time = timeSecondsSince2000();
	return cur_time > gLoadSimulationStartTime + lss->TestLength * 3600;
}

extern int giMaxFriendsLists;
extern int giFriendsPerList;
extern int giTotalChannels;
extern int giTotalUsers;

extern ChatTestParameters gChatTestParams;

static void InitializeChatTestFromLoadSimulator(void)
{
	if (!lss)
		return;

	gChatTestParams.iMaxFriendsLists = lss->BaseFriendsLists;
	gChatTestParams.iFriendsPerList = lss->BaseFriends;
	gChatTestParams.iTotalChannels = lss->BaseChannels;
	gChatTestParams.iTotalUsers = lss->BaseUsers;
	// Everything else just uses lss directly for running the sims
}

void ReadLoadSimulationStruct()
{
	lss = StructCreate(parse_LoadSimulationStruct);

	ParserReadTextFile(gLoadSimulationConfig, parse_LoadSimulationStruct, lss, 0);

	gChatTestParams.iMaxFriendsLists = lss->BaseFriendsLists;
	gChatTestParams.iFriendsPerList = lss->BaseFriends;
	gChatTestParams.iTotalChannels = lss->BaseChannels;
	gChatTestParams.iTotalUsers = lss->BaseUsers;
}

void InitLoadSimulation(NetLink *link)
{
	gLoadSimulationStartTime = timeSecondsSince2000();

	InitStaticMailUser(link);

	InitJoinTooManyChannels(link);
	InitJoinNonexistentChannels(link);
	InitLeaveUnwatchedChannels(link);
	InitOfflineUsers(link);
	InitUnwatchedChannel(link);

	InitNonexistentUsers(link);
	InitIgnoreUsers(link);
	InitReaddFriends(link);
	InitReremoveFriends(link);
	InitReaddIgnores(link);
	InitReremoveIgnores(link);
}

static void SimulateAction(NetLink* link, ChatTestFunction* fun, F32 per_user, int online_users)
{
	// per_user is how many actions the user would take in one hour.
	F32 total = (per_user * online_users) / 3600;
	F32 percentage = total - floor(total);
	int count = (int)floor(total);

	if(randomPositiveF32() < percentage)
		++count;

	fun(link, count);
}

void UpdateLoadSimulator(NetLink *link)
{
	PERFINFO_AUTO_START_FUNC();
	SimulateAction(link, &InitChannels, lss->Channel_Creates, lss->UsersOnline);
	SimulateAction(link, &RecreateChannels, lss->Channel_Recreates, lss->UsersOnline);
	SimulateAction(link, &JoinTooManyChannels, lss->Channel_ExtraJoins, lss->UsersOnline);
	SimulateAction(link, &JoinNonexistentChannels, lss->Channel_IncorrectJoins, lss->UsersOnline);
	SimulateAction(link, &LeaveUnwatchedChannels, lss->Channel_IncorrectLeaves, lss->UsersOnline);
	SimulateAction(link, &JoinChannels, lss->Channel_Joins, lss->UsersOnline);
	SimulateAction(link, &LeaveChannels, lss->Channel_Leaves, lss->UsersOnline);

	SimulateAction(link, &SendTells, lss->Tell_Sends, lss->UsersOnline);
	SimulateAction(link, &SendNonexistentTells, lss->Tell_NonexistentSends, lss->UsersOnline);
	SimulateAction(link, &SendOfflineTells, lss->Tell_OfflineSends, lss->UsersOnline);

	SimulateAction(link, &SendChannelMessages, lss->Message_Sends, lss->UsersOnline);
	SimulateAction(link, &SendUnwatchedChannelMessages, lss->Message_IncorrectSends, lss->UsersOnline);

	SimulateAction(link, &RequestChannelLists, lss->ChannelList_Requests, lss->UsersOnline);
	SimulateAction(link, &RequestChannelMemberLists, lss->ChannelMemberList_Requests, lss->UsersOnline);
	SimulateAction(link, &RequestUnwatchedChannelMemberLists, lss->ChannelMemberList_IncorrectRequests, lss->UsersOnline);

	SimulateAction(link, &ChangeMotds, lss->Motd_Changes, lss->UsersOnline);

	SimulateAction(link, &PromoteUsers, lss->Promote_User, lss->UsersOnline);
	SimulateAction(link, &PromoteNonWatchingUsers, lss->Promote_IncorrectUser, lss->UsersOnline);

	SimulateAction(link, &AddRemoveFriends, lss->Friend_AddRemove, lss->UsersOnline);
	SimulateAction(link, &ReaddFriends, lss->Friend_Readd, lss->UsersOnline);
	SimulateAction(link, &AddIgnoredFriends, lss->Friend_AddIgnored, lss->UsersOnline);
	SimulateAction(link, &AddNonexistentFriends, lss->Friend_AddIncorrect, lss->UsersOnline);
	SimulateAction(link, &RejectFriends, lss->Friend_Reject, lss->UsersOnline);
	SimulateAction(link, &ReremoveFriends, lss->Friend_Reremove, lss->UsersOnline);
	SimulateAction(link, &RemoveNonexistentFriends, lss->Friend_IncorrectRemove, lss->UsersOnline);
	SimulateAction(link, &RetrieveFriendsLists, lss->Friend_GetList, lss->UsersOnline);

	SimulateAction(link, &AddRemoveIgnores, lss->Ignore_AddRemove, lss->UsersOnline);
	SimulateAction(link, &ReaddIgnores, lss->Ignore_AddIgnored, lss->UsersOnline);
	SimulateAction(link, &AddNonexistentIgnores, lss->Ignore_AddIncorrect, lss->UsersOnline);
	SimulateAction(link, &ReremoveIgnores, lss->Ignore_Reremove, lss->UsersOnline);
	SimulateAction(link, &RemoveNonexistentIgnores, lss->Ignore_RemoveIncorrect, lss->UsersOnline);

	SimulateAction(link, &SendBasicMails, lss->Mail_SendBasic, lss->UsersOnline);
	SimulateAction(link, &SendBasicMailsToInvalidPlayers, lss->Mail_SendBasicToNobody, lss->UsersOnline);
	SimulateAction(link, &SendItemMails, lss->Mail_SendItem, lss->UsersOnline);
	SimulateAction(link, &SendItemMailsToInvalidPlayers, lss->Mail_SendItemToNobody, lss->UsersOnline);
	SimulateAction(link, &SendEmptyMails, lss->Mail_SendEmpty, lss->UsersOnline);
	SimulateAction(link, &GetMails, lss->Mail_Get, lss->UsersOnline);
	SimulateAction(link, &SetMailsRead, lss->Mail_SetRead, lss->UsersOnline);
	SimulateAction(link, &SendDeleteBasicMails, lss->Mail_DeleteBasic, lss->UsersOnline);
	SimulateAction(link, &SendDeleteItemMails, lss->Mail_DeleteItem, lss->UsersOnline);
	PERFINFO_AUTO_STOP_FUNC();
}

#include "LoadSimulation_c_ast.c"
