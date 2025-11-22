#ifndef CHATHAMMER_H
#define CHATHAMMER_H

typedef struct NetComm NetComm;
typedef struct NetLink NetLink;
typedef struct Packet Packet;

extern bool gbInitDB;
extern int next_user_id;
extern int next_channel_id;

NetComm *getFakeChatServerComm(void);

// ChatTestFunctions should return true when they are done with their test and it is time to move on.
typedef bool(ChatTestFunction)(NetLink*, const int);

// ChatTestInitFunctions should set up anything that the test function needs.
typedef void(ChatTestInitFunction)(NetLink*);

typedef struct ChatTestCase
{
	const char *name;
	ChatTestInitFunction *init_function; // Run once before test_function
	ChatTestFunction *test_function; // Run every giDelay milliseconds
	int *count; // How many times to loop inside test_function per call. Passed in by pointer to make command line arguments easier
	U32 avg_throughput; // A return value set by getting metric data from the global chat server
	PERFINFO_TYPE *init_pi;
	PERFINFO_TYPE *test_pi;
} ChatTestCase;

AUTO_STRUCT;
typedef struct FakeChannelData
{
	int channel_id;
	int owner_id;
	int members[100];
	int member_count;
} FakeChannelData;

extern ChatTestCase ChatTests[];
int GetChatTestSize();

extern ChatTestCase FriendTests[];
int GetFriendTestSize();

extern ChatTestCase MailTests[];
int GetMailTestSize();

void InitReturnValues(void);
void PrintReturnValues(void);

int ChatHammer_pktSend(Packet **pakptr);

extern int giUsers;
extern int giChannels;
bool CreateUsers(NetLink *link, const int count);
bool CreateChannels(NetLink *link, const int count);

extern int giFriends;
void InitFillFriendsList(NetLink *link);
bool FillFriendsList(NetLink *link, const int count);

#endif