#ifndef LOGINHAMMER_H
#define LOGINHAMMER_H

typedef struct NetLink NetLink;
typedef struct ChatAuthData ChatAuthData;

AUTO_STRUCT;
typedef struct ChatRelayHammerStats
{
	// Attempts/failures/timeouts for each step
	int iChatConnect[3];
	int iChatAuth[3];
	int iChatLogin[3];

	// Timing avg/min/max
	float fChatConnectTime[3];
	float fChatAuthTime[3];
	float fChatLoginTime[3];

	// Commands Sent/Responses received
	int iChatPrivateMsg[2];

	int iChatFriendList[2];
	int iChatIgnoreList[2];
	int iChatChannelList[2];

	//int giChatChannelJoin[2];
	int iChatChannelSend[2];
	int iChatChannelReceiveTotal;

	int iFriendRequests[2];
	int iIgnoreUpdates[2];
	int iMiscNotifies;

	int iPlayerUpdates[2];
} ChatRelayHammerStats;

AUTO_STRUCT;
typedef struct ChatRelayAddress
{
	char address[64];
	U32 uPort;

	// this is populated on load and used to index the StashTable
	char *addressKey; AST(NO_TEXT_SAVE)
	ChatRelayHammerStats stats; AST(NO_TEXT_SAVE)
} ChatRelayAddress;

AUTO_STRUCT;
typedef struct CRHammerChannel
{
	char *channelName;
	U32 uPercentUsers; // 0-100
	U32 uMaxUsers;

	U32 uCurUsers; NO_AST
} CRHammerChannel;

AUTO_STRUCT;
typedef struct ChatRelayHammerSettings
{
	EARRAY_OF(ChatRelayAddress) ppChatRelays;
	EARRAY_OF(CRHammerChannel) ppChannels;
} ChatRelayHammerSettings;

typedef enum ChatAttemptState
{
	CHATRELAY_ATTEMPT_STATE_CONNECT = 0,
	CHATRELAY_ATTEMPT_STATE_AUTH,
	CHATRELAY_ATTEMPT_STATE_CONNECTED,
} ChatAttemptState;

typedef struct ChatAttempt
{
	int id;
	U32 timer;
	ChatAttemptState state;

	U32 uAccountID;
	char *account; // estring
	NetLink *link;

	bool bAuthFailed;
	float fAuthFailed;
	// Connection / Auth data
	float fChatRelayConnectWait;
	float fChatRelayAuthWait;
	float fChatRelayLoginWait;
	bool bAuthDone;
	bool bLoggedIn;

	// Commands
	float fPrivateMessageSent;
	float fRequestListSent;
	float fChannelMessageSent;
	float fFriendIgnores;
	float fPlayerUpdates;

	bool bJoinedChannels;

	U32 iFriends[5];
	U32 iIgnores[5];

	ChatRelayAddress *pRelayAddress;

	STRING_EARRAY ppChannels;
	INT_EARRAY piRandomFriends;
} ChatAttempt;

#endif