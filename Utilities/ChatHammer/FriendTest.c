#include "accountnet.h"
#include "chatCommonFake.h"
#include "cmdparse.h"
#include "GlobalTypes.h"
#include "MemoryPool.h"
#include "objPath.h"
#include "rand.h"
#include "sock.h"
#include "structNet.h"

#include "ChatCommands.h"
#include "ChatHammer.h"
#include "ChatTest.h"
#include "FriendTest.h"

#include "AutoGen/ChatHammer_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"

extern int giDelay;
extern int giTestLength;

extern bool gbDebugText;

extern U32 gTestStartTime;

extern ChatTestParameters gChatTestParams;

ChatTestCase FriendTests[] = 
{
	{"Add/Remove Friends", NULL, &AddRemoveFriends, &gChatTestParams.iFriends},
	{"Readd Friends", &InitReaddFriends, &ReaddFriends, &gChatTestParams.iFriends},
	{"Add Ignored Friends", &InitIgnoreUsers, &AddIgnoredFriends, &gChatTestParams.iFriends},
	{"Add nonexistent Friends", &InitNonexistentUsers, &AddNonexistentFriends, &gChatTestParams.iFriends},
	{"Reject Friends", NULL, &RejectFriends, &gChatTestParams.iFriends},
	{"Reremove Friends", &InitReremoveFriends, &ReremoveFriends, &gChatTestParams.iFriends},
	{"Remove Nonexistent Friends", &InitNonexistentUsers, &RemoveNonexistentFriends, &gChatTestParams.iFriends},
	{"Add/Remove Ignores", NULL, &AddRemoveIgnores, &gChatTestParams.iIgnores},
	{"Add Ignored Ignores", &InitReaddIgnores, &ReaddIgnores, &gChatTestParams.iIgnores},
	{"Add Nonexistent Ignores", &InitNonexistentUsers, &AddNonexistentIgnores, &gChatTestParams.iIgnores},
	{"Reremove Ignores", &InitReremoveIgnores, &ReremoveIgnores, &gChatTestParams.iIgnores},
	{"Remove Nonexistent Ignores", &InitNonexistentUsers, &RemoveNonexistentIgnores, &gChatTestParams.iIgnores},
	{"Retrieve Friends Lists", NULL, &RetrieveFriendsLists, &gChatTestParams.iFriends},
};

int GetFriendTestSize()
{
	return sizeof(FriendTests)/sizeof(ChatTestCase);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static int gFriendsStartUser;
static int gFriendsFromUser;
static int gFriendsToUser;

void InitStaticUsers(NetLink *link)
{
	gFriendsFromUser = randomIntRange(1, next_user_id - 1);
	gFriendsToUser = gFriendsFromUser + 1;
}

static int gIgnoreUsersFromUser;
static int gIgnoreUsersToUser;

void InitIgnoreUsers(NetLink *link)
{
	gIgnoreUsersFromUser = randomIntRange(1, next_user_id - 1);
	gIgnoreUsersToUser = gIgnoreUsersFromUser + 1;
	AddIgnore(link, gIgnoreUsersFromUser, gIgnoreUsersToUser);
}

static int gNonexistentUsersFromUser;
static int gNonexistentUsersToUser;

void InitNonexistentUsers(NetLink *link)
{
	gNonexistentUsersFromUser = randomIntRange(1, next_user_id - 1);
	gNonexistentUsersToUser = next_user_id;
}

void InitFillFriendsList(NetLink *link)
{
	gFriendsStartUser = 1;
	gFriendsFromUser = 1;
	gFriendsToUser = 1;
}

bool FillFriendsList(NetLink *link, const int count)
{
	int i;

	if(!gbInitDB)
		return true;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count && gFriendsStartUser < next_user_id - gChatTestParams.iFriendsPerList - 1; ++i)
	{
		AddFriend(link, gFriendsFromUser, gFriendsToUser);
		if(++gFriendsToUser >= gFriendsStartUser + gChatTestParams.iFriendsPerList + 1)
		{
			if(++gFriendsFromUser >= gFriendsStartUser + gChatTestParams.iFriendsPerList + 1)
			{
				gFriendsStartUser = gFriendsStartUser + gChatTestParams.iFriendsPerList + 1;
				gFriendsFromUser = gFriendsStartUser;
			}
			gFriendsToUser = gFriendsStartUser;
		}
	}
//	printf("%d: %d->%d\n", gFriendsStartUser, gFriendsFromUser, gFriendsToUser);
	return gFriendsStartUser >= gChatTestParams.iMaxFriendsLists || gFriendsStartUser >= next_user_id;
}

bool AddRemoveFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/4); ++i)
	{
		int from_user_id = randomIntRange(1, next_user_id - 1);
		int to_user_id = randomIntRange(1, next_user_id - 1);
		AddFriend(link, from_user_id, to_user_id);
		AddFriend(link, to_user_id, from_user_id);
		RemoveFriend(link, from_user_id, to_user_id);
		RemoveFriend(link, to_user_id, from_user_id);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool AddFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		int from_user_id = randomIntRange(1, next_user_id - 1);
		int to_user_id = randomIntRange(1, next_user_id - 1);
		AddFriend(link, from_user_id, to_user_id);
		AddFriend(link, to_user_id, from_user_id);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RejectFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		int from_user_id = randomIntRange(1, next_user_id - 1);
		int to_user_id = randomIntRange(1, next_user_id - 1);
		AddFriend(link, from_user_id, to_user_id);
		RejectFriend(link, to_user_id, from_user_id);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RemoveFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		RemoveFriend(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool AddRemoveIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/4); ++i)
	{
		int from_user_id = randomIntRange(1, next_user_id - 1);
		int to_user_id = randomIntRange(1, next_user_id - 1);
		AddIgnore(link, from_user_id, to_user_id);
		AddIgnore(link, to_user_id, from_user_id);
		RemoveIgnore(link, from_user_id, to_user_id);
		RemoveIgnore(link, to_user_id, from_user_id);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool AddIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		AddIgnore(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RemoveIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		RemoveIgnore(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RetrieveFriendsLists(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		RetrieveFriendsList(link, randomIntRange(1, next_user_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gReaddFriendsFromUser;
static int gReaddFriendsToUser;

void InitReaddFriends(NetLink* link)
{
	gReaddFriendsFromUser = randomIntRange(1, next_user_id - 2);
	gReaddFriendsToUser = gReaddFriendsFromUser + 1;
}

bool ReaddFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		AddFriend(link, gReaddFriendsFromUser, gReaddFriendsToUser);
		AddFriend(link, gReaddFriendsToUser, gReaddFriendsFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gReremoveFriendsFromUser;
static int gReremoveFriendsToUser;

void InitReremoveFriends(NetLink* link)
{
	gReremoveFriendsFromUser = randomIntRange(1, next_user_id - 2);
	gReremoveFriendsToUser = gReremoveFriendsFromUser + 1;
}

bool ReremoveFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		RemoveFriend(link, gReremoveFriendsFromUser, gReremoveFriendsToUser);
		RemoveFriend(link, gReremoveFriendsToUser, gReremoveFriendsFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gReaddIgnoresFromUser;
static int gReaddIgnoresToUser;

void InitReaddIgnores(NetLink* link)
{
	gReaddIgnoresFromUser = randomIntRange(1, next_user_id - 2);
	gReaddIgnoresToUser = gReaddIgnoresFromUser + 1;
}

bool ReaddIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		AddIgnore(link, gReaddIgnoresFromUser, gReaddIgnoresToUser);
		AddIgnore(link, gReaddIgnoresToUser, gReaddIgnoresFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gReremoveIgnoresFromUser;
static int gReremoveIgnoresToUser;

void InitReremoveIgnores(NetLink* link)
{
	gReremoveIgnoresFromUser = randomIntRange(1, next_user_id - 1);
	gReremoveIgnoresToUser = gReremoveIgnoresFromUser + 1;
}

bool ReremoveIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		RemoveIgnore(link, gReremoveIgnoresFromUser, gReremoveIgnoresToUser);
		RemoveIgnore(link, gReremoveIgnoresToUser, gReremoveIgnoresFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool AddIgnoredFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		AddFriend(link, gIgnoreUsersFromUser, gIgnoreUsersToUser);
		AddFriend(link, gIgnoreUsersToUser, gIgnoreUsersFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool AddNonexistentFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		AddFriend(link, gNonexistentUsersFromUser, gNonexistentUsersToUser);
		AddFriend(link, gNonexistentUsersToUser, gNonexistentUsersFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RemoveNonexistentFriends(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0;(i == 0) || (i < count/2); ++i)
	{
		AddFriend(link, gNonexistentUsersFromUser, gNonexistentUsersToUser);
		AddFriend(link, gNonexistentUsersToUser, gNonexistentUsersFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool AddNonexistentIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		AddFriend(link, gNonexistentUsersFromUser, gNonexistentUsersToUser);
		AddFriend(link, gNonexistentUsersToUser, gNonexistentUsersFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool RemoveNonexistentIgnores(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; (i == 0) || (i < count/2); ++i)
	{
		AddFriend(link, gNonexistentUsersFromUser, gNonexistentUsersToUser);
		AddFriend(link, gNonexistentUsersToUser, gNonexistentUsersFromUser);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}