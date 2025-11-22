#ifndef CHATTEST_H
#define CHATTEST_H

typedef struct NetLink NetLink;

AUTO_STRUCT;
typedef struct ChatTestParameters
{
	// Init Settings
	int iTotalChannels; AST(DEFAULT(10000)) // Total number of channels to create.
	int iChannelsPerTick; AST(DEFAULT(6)) // Maximum channels to create per frame.
	int iTotalUsers; AST(DEFAULT(250000)) // Total number of users to create.
	int iUsersPerTick; AST(DEFAULT(6)) // Maximum users to create per frame.
	
	int iJoins; AST(DEFAULT(3000))
	int iJoinsPerTick; AST(DEFAULT(50))

	int iLeaves; AST(DEFAULT(6)) // Maximum channel leaves to request per frame.
	int iTells; AST(DEFAULT(6)) // Maximum tells per frame.
	int iChannelMessages; AST(DEFAULT(6)) // Maximum channel posts per frame.
	int iChannelListRequests; AST(DEFAULT(6)) // Maximum channel list requests per frame.
	int iChannelMemberListRequests; AST(DEFAULT(6)) // Maximum channel list requests per frame.
	int iMotdChanges; AST(DEFAULT(6)) // Maximum message of the day changes per frame.
	int iOperatorChanges; AST(DEFAULT(6)) // Maximum operator changes per frame.
	
	int iIgnores; AST(DEFAULT(6)) // Number of ignores to add per tick.

	int iFriends; AST(DEFAULT(6)) // Number of friend users to add per tick.
	int iMaxFriendsLists; AST(DEFAULT(150000)) // Maximum number of friends lists to add
	int iFriendsPerList; AST(DEFAULT(50)) // Maximum number of friends per list

	int iPlayerUpdates; AST(DEFAULT(1))
} ChatTestParameters;

bool InitUsersDone(void);
bool InitChannelsDone(void);
bool InitUsers(NetLink *link, const int count);
bool InitChannels(NetLink *link, const int count);
bool InitFriends(NetLink *link, const int count);

void InitJoinTooManyChannels(NetLink *link);
void InitJoinNonexistentChannels(NetLink *link);
void InitLeaveUnwatchedChannels(NetLink *link);
void InitOfflineUsers(NetLink *link);
void InitUnwatchedChannel(NetLink *link);

bool RecreateChannels(NetLink *link, const int count);
bool JoinChannels(NetLink *link, const int count);
bool JoinTooManyChannels(NetLink *link, const int count);
bool JoinNonexistentChannels(NetLink *link, const int count);
bool JoinLeaveChannels(NetLink *link, const int count);
bool LeaveUnwatchedChannels(NetLink *link, const int count);
bool LeaveChannels(NetLink *link, const int count);
bool SendTells(NetLink *link, const int count);
bool SendNonexistentTells(NetLink *link, const int count);
bool SendOfflineTells(NetLink *link, const int count);
bool SendChannelMessages(NetLink *link, const int count);
bool SendUnwatchedChannelMessages(NetLink *link, const int count);
bool RequestChannelLists(NetLink *link, const int count);
bool RequestChannelMemberLists(NetLink *link, const int count);
bool RequestUnwatchedChannelMemberLists(NetLink *link, const int count);
bool ChangeMotds(NetLink *link, const int count);
bool PromoteUsers(NetLink *link, const int count);
bool PromoteNonWatchingUsers(NetLink *link, const int count);

#endif // CHATTEST_H