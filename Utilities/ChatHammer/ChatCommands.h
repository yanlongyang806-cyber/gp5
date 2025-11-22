#pragma once

typedef struct NetLink NetLink;

#define CHATHAMMER_SHARDNAME "ChatHammerShard"

void RegisterShard(NetLink *link);
void CreateUser(NetLink *link, int id, const char *account_name, const char *display_name);
void UserUpdate(NetLink *link, int id);
void CreateChannelByID(NetLink *link, int user_id, int channel_id);
void CreateChannel(NetLink *link, int user_id, int channel_id);
void JoinChannelById(NetLink *link, int user_id, int channel_id);
void JoinChannel(NetLink *link, int user_id, int channel_index);
void LeaveChannel(NetLink *link, int user_id, int channel_index);
void SendChannelMessage(NetLink *link, int from_user_id, int channel, const char *text);
void RequestChannelList(NetLink *link, int user_id);
void RequestChannelMemberList(NetLink *link, int user_id, int channel_index);
void ChangeMotd(NetLink *link, int channel_index, const char *text);
void PromoteUser(NetLink *link, int account_id, int channel_index);
void CreatePrivateMessage(char **estr, int from_user_id, int to_user_id, const char *text);
void CreateChannelMessage(char **estr, int from_user_id, int channel, const char *text);
void CreateUserUpdate(char **estr, int id);
void LogoutUser(NetLink *link, int id);


void AddFriend(NetLink *link, int from_user_id, int to_user_id);
void RejectFriend(NetLink *link, int from_user_id, int to_user_id);
void RemoveFriend(NetLink *link, int from_user_id, int to_user_id);
void AddIgnore(NetLink *link, int from_user_id, int to_user_id);
void RemoveIgnore(NetLink *link, int from_user_id, int to_user_id);
void RetrieveFriendsList(NetLink *link, int user_id);