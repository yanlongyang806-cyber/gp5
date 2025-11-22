#ifndef FRIENDTEST_H
#define FRIENDTEST_H

void InitStaticUsers(NetLink *link);
void InitIgnoreUsers(NetLink *link);
void InitNonexistentUsers(NetLink *link);
void InitReaddFriends(NetLink* link);
void InitReremoveFriends(NetLink* link);
void InitReaddIgnores(NetLink* link);
void InitReremoveIgnores(NetLink* link);


bool AddIgnoredFriends(NetLink *link, const int count);
bool AddNonexistentFriends(NetLink *link, const int count);
bool RemoveNonexistentFriends(NetLink *link, const int count);
bool AddNonexistentIgnores(NetLink *link, const int count);
bool RemoveNonexistentIgnores(NetLink *link, const int count);
bool AddFriends(NetLink *link, const int count);
bool RejectFriends(NetLink *link, const int count);
bool AddRemoveFriends(NetLink *link, const int count);
bool RemoveFriends(NetLink *link, const int count);
bool AddIgnores(NetLink *link, const int count);
bool AddRemoveIgnores(NetLink *link, const int count);
bool RemoveIgnores(NetLink *link, const int count);
bool ReaddFriends(NetLink *link, const int count);
bool ReaddIgnores(NetLink *link, const int count);
bool ReremoveIgnores(NetLink *link, const int count);
bool ReremoveFriends(NetLink *link, const int count);
bool RetrieveFriendsLists(NetLink *link, const int count);

#endif // FRIENDTEST_H