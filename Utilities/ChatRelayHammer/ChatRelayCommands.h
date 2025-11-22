#pragma once

typedef struct NetLink NetLink;

void crh_SendPrivateMessage(NetLink *link, U32 uFromID, U32 uToID, const char *msg);
void crh_SendChannelMessage(NetLink *link, U32 uFromID, const char *channel_name, const char *msg);
void crh_ChannelJoinOrCreate(NetLink *link, U32 uFromID, const char *channel_name);
void crh_Login(NetLink *link, U32 uAccountID);

void crh_RequestFriendList(NetLink *link, U32 uAccountID);
void crh_RequestIgnoreList(NetLink *link, U32 uAccountID);
void crh_ChannelRequestList(NetLink *link, U32 uAccountID);

void crh_AddFriend(NetLink *link, U32 uAccountID, U32 uTargetAccountID);
void crh_RemoveFriend(NetLink *link, U32 uAccountID, U32 uTargetAccountID);

void crh_AddIgnore(NetLink *link, U32 uAccountID, U32 uTargetAccountID);
void crh_RemoveIgnore(NetLink *link, U32 uAccountID, U32 uTargetAccountID);
void crh_SendPlayerUpdate(NetLink *link, U32 uAccountID, U32 *piAccountIDs);

void crh_PurgeChannels(NetLink *link, U32 uAccountID);