#include "ChatRelayHammer.h"

#include "chatCommonFake.h"
#include "ChatRelayCommands.h"
#include "cmdparse.h"
#include "CrypticPorts.h"
#include "file.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "mathutil.h"
#include "MemoryPool.h"
#include "net.h"
#include "netpacketutil.h"
#include "objPath.h"
#include "qsortG.h"
#include "ReferenceSystem.h"
#include "sock.h"
#include "StringCache.h"
#include "structNet.h"
#include "sysutil.h"
#include "TestServerIntegration.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"

#include "AutoGen/ChatRelayHammer_h_ast.h"

// *******************************************************
// NOTES:
// Must run ChatServer and ChatRelay(s) with -DebugMode for fake authentication mode to work
// Must register all the ChatRelay addresses in src/Utilities/ChatRelayHammer/chatRelayAddresses.txt
// *******************************************************

#define TESTSERVER_WORKS 0
#define THREADED_CLIENTS 0
#define NUM_THREADS 3

static bool sThreadCompletion[NUM_THREADS] = { false };

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "LoginHammer"););

extern void setAccountServer(const char *pAccountServer);

#define NUMBER_OF_CHANNELS 5
static const char *sChannelList[NUMBER_OF_CHANNELS] = {
	"TestChan_0",
	"TestChan_1",
	"TestChan_2",
	"TestChan_3",
	"TestChan_4",
};

// Account ID starting offset
int giOffset = 1000;
int giConnections = 20000, giDelay = 3;
char gAccountPrefix[128] = "TestAccount_";
int giRandDeviation = 500;
bool gbConnectionsArePerRelay = false;
bool gbTestConnectionLimitMode = true;

/*float giTimeout = 10.0;
float giTransferTimeout = 120.0;*/
U32 giTimeToRun = 60; // in seconds
U32 giTimeToWait = 60;

AUTO_CMD_INT(giOffset, SetStartingOffset);
AUTO_CMD_INT(giConnections, SetConnections); // Specifies the number of connections to attempt.
AUTO_CMD_INT(giDelay, SetDelay); // Specifies the delay between connections, in milliseconds.
AUTO_CMD_INT(gbConnectionsArePerRelay, PerRelayConnections); // Changes if the connection count is per-relay or global
/*AUTO_CMD_FLOAT(giTimeout, SetTimeout);
AUTO_CMD_FLOAT(giTransferTimeout, SetTransferTimeout);*/
AUTO_CMD_INT(giTimeToRun, RunTime);
AUTO_CMD_INT(giTimeToWait, WaitTime);

// Global stat tracking
int giCounter = 0;
float gfMaxMainLoop = 0.0;
int giMainTimer;

// Attempts/failures/timeouts for each step
int giChatConnect[3];
int giChatAuth[3];
int giChatLogin[3];

// Timing avg/min/max
float gfChatConnect[3];
float gfChatAuth[3];
float gfChatLogin[3];

// Commands Sent/Responses received
int giChatPrivateMsg[2];
bool gbChatPrivateMsg = true;
int giChatPrivateMsgRepeat = 5000;
AUTO_CMD_INT(gbChatPrivateMsg, PMEnable);
AUTO_CMD_INT(giChatPrivateMsgRepeat, PMRepeat);

int giChatFriendList[2];
int giChatIgnoreList[2];
int giChatChannelList[2];
bool gbChatRequestLists = false;
int giChatRequestListsRepeat = 30000; // half minute
AUTO_CMD_INT(gbChatRequestLists, RequestEnable);
AUTO_CMD_INT(giChatRequestListsRepeat, RequestRepeat);

//int giChatChannelJoin[2];
int giChatChannelSend[2];
int giChatChannelReceiveTotal = 0;
bool gbChatChannels = false;
//int giChatChannelJoinRepeat = 30000;
int giChatChannelSendRepeat = 5000;
AUTO_CMD_INT(gbChatChannels, ChanEnable);
AUTO_CMD_INT(giChatChannelSendRepeat, ChanRepeat);

int giFriendRequests[2];
int giIgnoreUpdates[2];
bool gbChatFriendIgnore = false;
int giChatFriendIgnoreRepeat = 30000;
AUTO_CMD_INT(gbChatFriendIgnore, FriendIgnoreEnable);
AUTO_CMD_INT(giChatFriendIgnoreRepeat, FriendIgnoreRepeat);
int giMiscNotifies = 0;

int giPlayerUpdates[2];
bool gbPlayerUpdates = false;
int giPlayerUpdatesNumFriends = 20;
int giPlayerUpdateRepeat = 15000;
AUTO_CMD_INT(gbPlayerUpdates, PlayerUpdateEnable);
AUTO_CMD_INT(giPlayerUpdateRepeat, PlayerUpdateRepeat);
AUTO_CMD_INT(giPlayerUpdatesNumFriends, PlayerUpdateNumFriends);

bool gbComplete = false;
static bool sbAllUsersStarted = false;

#if (!THREADED_CLIENTS)
ChatAttempt **gppActiveAttempts = NULL;
#endif

// Relay Address file takes precedence
char gChatRelayAddress[64] = "localhost";
AUTO_CMD_STRING(gChatRelayAddress, ChatRelay);
int gChatRelayPort = STARTING_CHATRELAY_PORT;
AUTO_CMD_INT(gChatRelayPort, ChatRelayPort);
char gChatRelayAddressFile[128] = "chatRelayAddress.txt";
AUTO_CMD_STRING(gChatRelayAddressFile, ChatRelayFile);

static EARRAY_OF(ChatRelayAddress) seaChatRelays = NULL;
static ChatRelayHammerSettings sHammerSettings = {0};

static void crHammer_LoadRelayHammerSettings(void)
{
	if (fileExists(gChatRelayAddressFile))
	{
		ParserReadTextFile(gChatRelayAddressFile, parse_ChatRelayHammerSettings, &sHammerSettings, 0);
		seaChatRelays = sHammerSettings.ppChatRelays;
		if (eaSize(&seaChatRelays))
			return;
		else
			printf("Error loading relay address - no relays loaded.\n\n");
	}

	// File does not exist or no address were loaded
	{
		ChatRelayAddress *pAddress = StructCreate(parse_ChatRelayAddress);
		sprintf(pAddress->address, "%s", gChatRelayAddress);
		pAddress->uPort = gChatRelayPort;
	}

	if (eaSize(&sHammerSettings.ppChannels) == 0)
		gbChatChannels = false;
}

static ChatRelayAddress * crHammer_GetRelayToUse(U32 uAccountID)
{
	static U32 uSize = 0;
	if (!uSize)
		uSize = eaSize(&seaChatRelays);
	return seaChatRelays[uAccountID % uSize];
}

void DumpStats()
{
	printf("\nTOTAL TIME ELAPSED: (%0.3f)\n", timerElapsed(giMainTimer));
	printf("NUMBER OF CONNECTIONS COMPLETED: %d\n", giCounter);
	printf("MAX MAIN LOOP LENGTH: %0.3f\n", gfMaxMainLoop);

	printf("\nChat Relay---------------------------------------------------\n");
	printf("Stage\t\tSuccess\tAvg\tMin\tMax\tTimeout\tFailure\n");
	printf("Connection\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giChatConnect[0], 
		gfChatConnect[0]/giChatConnect[0], 
		gfChatConnect[1], gfChatConnect[2], giChatConnect[2], giChatConnect[1]);
	printf("Authorization\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giChatAuth[0], 
		gfChatAuth[0]/giChatAuth[0], 
		gfChatAuth[1], gfChatAuth[2], giChatAuth[2], giChatAuth[1]);
	printf("Login\t\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giChatLogin[0], 
		gfChatLogin[0]/giChatLogin[0], 
		gfChatLogin[1], gfChatLogin[2], giChatLogin[2], giChatLogin[1]);

	printf("----Commands-------------------------------------\n");
	printf("Command\t\tSent\tResponses Received\tTotal\n");
	printf("Private Msgs\t%d\t%d\n", giChatPrivateMsg[0], giChatPrivateMsg[1]);
	printf("Friend List\t%d\t%d\n", giChatFriendList[0], giChatFriendList[1]);
	printf("Ignore List\t%d\t%d\n", giChatIgnoreList[0], giChatIgnoreList[1]);
	printf("Channel List\t%d\t%d\n", giChatChannelList[0], giChatChannelList[1]);
	printf("Chat Msgs\t%d\t%d\t\t\t%d\n", giChatChannelSend[0], giChatChannelSend[1], giChatChannelReceiveTotal);

	printf("Friends\t\t%d\t%d\n", giFriendRequests[0], giFriendRequests[1]);
	printf("Ignores\t\t%d\t%d\n", giIgnoreUpdates[0], giIgnoreUpdates[1]);
	printf("Player Update\t%d\t%d\n", giPlayerUpdates[0], giPlayerUpdates[1]);
	printf("Other Notifications\t%d\n", giMiscNotifies);
	
	{
		U64 uBytesSent = 0, uBytesReceived = 0;
		EARRAY_FOREACH_BEGIN(gppActiveAttempts, i);
		{
			const LinkStats *stats = linkStats(gppActiveAttempts[i]->link);
			uBytesSent += stats->send.bytes;
			uBytesReceived += stats->recv.bytes;
		}
		EARRAY_FOREACH_END;
		printf ("Bytes Sent [%I64u], Received [%I64u]\n", uBytesSent, uBytesReceived);
	}

	EARRAY_FOREACH_BEGIN(seaChatRelays, i);
	{
		ChatRelayAddress *pAddress = seaChatRelays[i];
		printf("\nChat Relay #%d------------------------------------------------\n", i);
		printf("Stage\t\tSuccess\tAvg\tMin\tMax\tTimeout\tFailure\n");
		printf("Connection\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", pAddress->stats.iChatConnect[0], 
			pAddress->stats.fChatConnectTime[0]/pAddress->stats.iChatConnect[0], 
			pAddress->stats.fChatConnectTime[1], pAddress->stats.fChatConnectTime[2], 
			pAddress->stats.iChatConnect[2], pAddress->stats.iChatConnect[1]);
		printf("Authorization\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", pAddress->stats.iChatAuth[0], 
			pAddress->stats.fChatAuthTime[0]/pAddress->stats.iChatAuth[0], 
			pAddress->stats.fChatAuthTime[1], pAddress->stats.fChatAuthTime[2], 
			pAddress->stats.iChatAuth[2], pAddress->stats.iChatAuth[1]);
		printf("Login\t\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", pAddress->stats.iChatLogin[0], 
			pAddress->stats.fChatLoginTime[0]/pAddress->stats.iChatLogin[0], 
			pAddress->stats.fChatLoginTime[1], pAddress->stats.fChatLoginTime[2], 
			pAddress->stats.iChatLogin[2], pAddress->stats.iChatLogin[1]);

		printf("----Commands-------------------------------------\n");
		printf("Command\t\tSent\tResponses Received\tTotal\n");
		printf("Private Msgs\t%d\t%d\n", pAddress->stats.iChatPrivateMsg[0], pAddress->stats.iChatPrivateMsg[1]);
		printf("Friend List\t%d\t%d\n", pAddress->stats.iChatFriendList[0], pAddress->stats.iChatFriendList[1]);
		printf("Ignore List\t%d\t%d\n", pAddress->stats.iChatIgnoreList[0], pAddress->stats.iChatIgnoreList[1]);
		printf("Channel List\t%d\t%d\n", pAddress->stats.iChatChannelList[0], pAddress->stats.iChatChannelList[1]);
		printf("Chat Msgs\t%d\t%d\t\t\t%d\n", pAddress->stats.iChatChannelSend[0], pAddress->stats.iChatChannelSend[1], 
			pAddress->stats.iChatChannelReceiveTotal);

		printf("Friends\t\t%d\t%d\n", pAddress->stats.iFriendRequests[0], pAddress->stats.iFriendRequests[1]);
		printf("Ignores\t\t%d\t%d\n", pAddress->stats.iIgnoreUpdates[0], pAddress->stats.iIgnoreUpdates[1]);
		printf("Player Update\t%d\t%d\n", pAddress->stats.iPlayerUpdates[0], pAddress->stats.iPlayerUpdates[1]);
		printf("Other Notifications\t%d\n", pAddress->stats.iMiscNotifies);
	}
	EARRAY_FOREACH_END;
}

static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			if(gbComplete)
			{
				return FALSE;
			}

			DumpStats();
			return TRUE;

		default: 
			return FALSE; 
	} 
}

ChatAttempt *ChatAttemptCreate(int id, U32 uAccountID, char *pAccount)
{
	ChatAttempt *pAttempt;

	pAttempt = calloc(1, sizeof(ChatAttempt));
	pAttempt->id = id;
	pAttempt->timer = timerAlloc();
	pAttempt->state = CHATRELAY_ATTEMPT_STATE_CONNECT;

	pAttempt->uAccountID = uAccountID;

	estrCopy(&pAttempt->account, &pAccount);
	return pAttempt;
}

void ChatAttemptDestroy(ChatAttempt *pAttempt)
{
	if (linkConnected(pAttempt->link))
		linkRemove(&pAttempt->link);
	estrDestroy(&pAttempt->account);
	timerFree(pAttempt->timer);
#if THREADED_CLIENTS
	pAttempt->bLoggedIn = false;
	pAttempt->bAuthDone = false;
	pAttempt->bAuthFailed = false;
#else
	eaFindAndRemove(&gppActiveAttempts, pAttempt);
	eaDestroy(&pAttempt->ppChannels);
	free(pAttempt);
#endif
}

void ChatAttemptAddTimeData(const char *pchName, float *data, float time)
{
	// TODO(Theo) fix these calls to NOT occur from inside commMonitor
	//PushMetricToTestServer(NULL, pchName, time, false);
	data[0] += time;
	if(!data[1] || time < data[1]) data[1] = time;
	if(!data[2] || time > data[2]) data[2] = time;
}

void ChatAttemptAccountConnectCallback(NetLink *link, void *user_data);
void ChatAttemptAccountDisconnectCallback(NetLink *link, void *user_data);
void ChatAttemptAccountPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data);

int giChatLoginAttempts = 0;
float gfChatLoginTotalTime = 0.0f;

static void ChatLoginAttemptConnectCallback(NetLink *link, void *user_data)
{
	ChatAttempt *pAttempt = (ChatAttempt*) user_data;
	Packet *pak = pktCreate(pAttempt->link, TOSERVER_GAME_MSG);
	pktSendU32(pak, CHATRELAY_FAKE_AUTHENTICATE);
	pktSendU32(pak, pAttempt->uAccountID);
	pktSendU32(pak, 0);
	pktSendU32(pak, CHAT_CONFIG_SOURCE_PC_ACCOUNT);
	pktSend(&pak);
	pAttempt->state = CHATRELAY_ATTEMPT_STATE_AUTH;
	pAttempt->fChatRelayConnectWait += timerElapsed(pAttempt->timer);

	ChatAttemptAddTimeData("ChatRelay_Connect", gfChatConnect, pAttempt->fChatRelayConnectWait);
	ChatAttemptAddTimeData("ChatRelay_Connect", pAttempt->pRelayAddress->stats.fChatConnectTime, pAttempt->fChatRelayConnectWait);
	giChatConnect[0]++;
	pAttempt->pRelayAddress->stats.iChatConnect[0]++;
}

static void ChatLoginAttemptDisconnectCallback(NetLink *link, void *user_data)
{
	ChatAttempt *pAttempt = (ChatAttempt*) user_data;

	pAttempt->link = NULL;
	ChatAttemptDestroy(pAttempt);
}

static void ChatLoginAttemptPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data)
{
	ChatAttempt *pAttempt = (ChatAttempt*) user_data;
	if (cmd == TOCLIENT_GAME_MSG)
	{
		int ref = pktGetBitsPack(pak,8);
		int lib = pktGetBits(pak,1);
		int command = pktGetBitsPack(pak,GAME_MSG_SENDBITS) | (lib * LIB_MSG_BIT);
		char *msg = pktGetStringTemp(pak);
		U32 iFlags = pktGetBits(pak, 32);
		enumCmdContextHowCalled eHow = pktGetBits(pak, 32);

		if (strStartsWith(msg, "ChatAuth"))
		{
			if (strstri(msg, "ChatAuthFailed"))
			{
				pAttempt->bAuthFailed = true;
				pAttempt->fAuthFailed = timerElapsed(pAttempt->timer);
				giChatAuth[1]++;
				pAttempt->pRelayAddress->stats.iChatAuth[1]++;
			}
			else
			{
				pAttempt->state = CHATRELAY_ATTEMPT_STATE_CONNECTED;
				pAttempt->fChatRelayAuthWait += timerElapsed(pAttempt->timer);
				ChatAttemptAddTimeData("ChatRelay_Auth", gfChatAuth, pAttempt->fChatRelayAuthWait);
				ChatAttemptAddTimeData("ChatRelay_Auth", pAttempt->pRelayAddress->stats.fChatAuthTime, pAttempt->fChatRelayAuthWait);
				pAttempt->bAuthDone = true;
				giChatAuth[0]++;
				pAttempt->pRelayAddress->stats.iChatAuth[0]++;
				crh_Login(link, pAttempt->uAccountID);
			}
		}
		else if (strStartsWith(msg, "gclChat_LoginSuccess"))
		{
			pAttempt->bLoggedIn = true;
			pAttempt->fChatRelayLoginWait += timerElapsed(pAttempt->timer);
			ChatAttemptAddTimeData("ChatRelay_Login", gfChatLogin, pAttempt->fChatRelayLoginWait);
			ChatAttemptAddTimeData("ChatRelay_Login", pAttempt->pRelayAddress->stats.fChatLoginTime, pAttempt->fChatRelayLoginWait);
			giChatLogin[0]++;
			pAttempt->pRelayAddress->stats.iChatLogin[0]++;
		}
		else if (strStartsWith(msg, "ChatLog_AddMessage"))
		{
			// Only log the Private_Sent ones, ignore the other half
			if (strstri(msg, "Type Private_Sent"))
			{
				giChatPrivateMsg[1]++;
				pAttempt->pRelayAddress->stats.iChatPrivateMsg[1]++;
			}
			else if (strstri(msg, "Type Channel"))
			{
				char accountstr[32];
				sprintf(accountstr, "accountid %d", pAttempt->uAccountID);
				if (strstri(msg, accountstr))
				{
					giChatChannelSend[1]++;
					pAttempt->pRelayAddress->stats.iChatChannelSend[1]++;
				}
				giChatChannelReceiveTotal++;
				pAttempt->pRelayAddress->stats.iChatChannelReceiveTotal++;
			}
		}
		else if (strStartsWith(msg, "gclChatCmd_ReceiveFriends"))
		{
			giChatFriendList[1]++;
			pAttempt->pRelayAddress->stats.iChatFriendList[1]++;
		}
		else if (strStartsWith(msg, "gclChatCmd_ReceiveIgnores"))
		{
			giChatIgnoreList[1]++;
			pAttempt->pRelayAddress->stats.iChatIgnoreList[1]++;
		}
		else if (strStartsWith(msg, "ClientChat_ReceiveUserChannelList"))
		{
			giChatChannelList[1]++;
			pAttempt->pRelayAddress->stats.iChatChannelList[1]++;
		}
		else if (strStartsWith(msg, "NotifyChatSend"))
		{
			giMiscNotifies++;
			pAttempt->pRelayAddress->stats.iMiscNotifies++;
		}
		else if (strStartsWith(msg, "gclChatCmd_ReceiveFriendCB"))
		{
			if (strstri(msg, "PlayerInfoUpdateComment"))
			{
				giPlayerUpdates[1]++;
				pAttempt->pRelayAddress->stats.iPlayerUpdates[1]++;
			}
			else
			{
				giFriendRequests[1]++;
				pAttempt->pRelayAddress->stats.iFriendRequests[1]++;
			}
		}
		else if (strStartsWith(msg, "gclChatCmd_ReceiveIgnoreCB"))
		{
			giIgnoreUpdates[1]++;
			pAttempt->pRelayAddress->stats.iIgnoreUpdates[1]++;
		}
		else if (strStartsWith(msg, "ClientChat_ChannelUpdate"))
		{
			// does nothing
		}
		else
		{
			printf("Unknown cmd: %s\n", msg);
		}
		// TODO(Theo) parse more commands
	}
	else
		assertmsg(0, "Unknown packet received");
}

static void ChatAttemptStartConnection(NetComm *comm, ChatAttempt *pAttempt)
{
	pAttempt->pRelayAddress = crHammer_GetRelayToUse(pAttempt->uAccountID);
	pAttempt->link = commConnect(comm, 
		LINKTYPE_UNSPEC, LINK_FORCE_FLUSH | LINK_NO_COMPRESS,
		pAttempt->pRelayAddress->address, pAttempt->pRelayAddress->uPort, 
		ChatLoginAttemptPacketCallback,
		ChatLoginAttemptConnectCallback,
		ChatLoginAttemptDisconnectCallback, 0);

	if(!pAttempt->link)
	{
		pAttempt->bAuthFailed = true;
		pAttempt->fAuthFailed = timerElapsed(pAttempt->timer);
		return;
	}
	
	linkSetMaxAllowedPacket(pAttempt->link,1<<19);
	linkAutoPing(pAttempt->link,1);
	linkSetKeepAlive(pAttempt->link);
	linkSetUserData(pAttempt->link, (void *)pAttempt);
}

static void ChatAttemptFrame(NetComm *comm, ChatAttempt *pAttempt)
{
	F32 timeElapsed = timerElapsed(pAttempt->timer);
	int iRand;

	if (!pAttempt->bAuthDone || !pAttempt->bLoggedIn)
	{
		if (pAttempt->bAuthFailed && timeElapsed - pAttempt->fAuthFailed > 2.0)
		{
			ChatAttemptStartConnection(comm, pAttempt);
		}
		return;
	}

	if (gbTestConnectionLimitMode)
		return;

	iRand = randInt(giRandDeviation);
	if (gbChatPrivateMsg)
	{
		if (timeElapsed - pAttempt->fPrivateMessageSent > (giChatPrivateMsgRepeat+iRand) / 1000.0)
		{
			pAttempt->fPrivateMessageSent = timeElapsed;
			crh_SendPrivateMessage(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID, "Test message 1234567890");
			giChatPrivateMsg[0]++;
			pAttempt->pRelayAddress->stats.iChatPrivateMsg[0]++;
		}
	}

	if (gbChatRequestLists)
	{
		if (timeElapsed - pAttempt->fRequestListSent > (giChatRequestListsRepeat+iRand) / 1000.0)
		{
			pAttempt->fRequestListSent = timeElapsed;
			crh_RequestFriendList(pAttempt->link, pAttempt->uAccountID);
			crh_RequestIgnoreList(pAttempt->link, pAttempt->uAccountID);
			crh_ChannelRequestList(pAttempt->link, pAttempt->uAccountID);
			giChatFriendList[0]++;
			giChatIgnoreList[0]++;
			giChatChannelList[0]++;
			pAttempt->pRelayAddress->stats.iChatFriendList[0]++;
			pAttempt->pRelayAddress->stats.iChatIgnoreList[0]++;
			pAttempt->pRelayAddress->stats.iChatChannelList[0]++;
		}
	}

	if (gbChatChannels && sbAllUsersStarted && giChatLogin[0] == giConnections)
	{
		// Clear old channels, join new ones
		if (!pAttempt->bJoinedChannels)
		{
			crh_PurgeChannels(pAttempt->link, pAttempt->uAccountID);
			EARRAY_FOREACH_BEGIN(sHammerSettings.ppChannels, i);
			{
				CRHammerChannel *chan = sHammerSettings.ppChannels[i];
				if (chan->uCurUsers >= chan->uMaxUsers)
					continue;
				if (chan->uPercentUsers && chan->uPercentUsers < 100)
				{
					U32 randPercent = randInt(100);
					if (randPercent < chan->uPercentUsers)
						continue;
				}
				eaPush(&pAttempt->ppChannels, chan->channelName);
				chan->uCurUsers++;
				crh_ChannelJoinOrCreate(pAttempt->link, pAttempt->uAccountID, chan->channelName);
			}
			EARRAY_FOREACH_END;

			pAttempt->bJoinedChannels = true;
		}
		if (pAttempt->ppChannels && 
			timeElapsed - pAttempt->fChannelMessageSent > (giChatChannelSendRepeat+iRand) / 1000.0)
		{
			U32 uRandomChannel = randInt(eaSize(&pAttempt->ppChannels));
			pAttempt->fChannelMessageSent = timeElapsed;
			crh_SendChannelMessage(pAttempt->link, pAttempt->uAccountID, pAttempt->ppChannels[uRandomChannel], 
				"Test chan Message 1234567890");
			giChatChannelSend[0]++;
			pAttempt->pRelayAddress->stats.iChatChannelSend[0]++;
		}
	}

	if (sbAllUsersStarted && gbChatFriendIgnore)
	{
		if (timeElapsed - pAttempt->fFriendIgnores > (giChatFriendIgnoreRepeat+iRand) / 1000.0)
		{
			pAttempt->fFriendIgnores = timeElapsed;
			// Add and Remove Friends
			if (pAttempt->uAccountID == giOffset + giConnections)
			{
				crh_AddFriend(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID-1);
				crh_RemoveFriend(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID-1);
				crh_AddIgnore(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID-1);
				crh_RemoveIgnore(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID-1);
			}
			else
			{
				crh_AddFriend(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID+1);
				crh_RemoveFriend(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID+1);
				crh_AddIgnore(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID+1);
				crh_RemoveIgnore(pAttempt->link, pAttempt->uAccountID, pAttempt->uAccountID+1);
			}
			giFriendRequests[0] += 2;
			giIgnoreUpdates[0] += 2;
		}
	}

	if (sbAllUsersStarted && gbPlayerUpdates)
	{
		if (timeElapsed - pAttempt->fPlayerUpdates > (giPlayerUpdateRepeat+iRand) / 1000.0)
		{
			if (!pAttempt->piRandomFriends)
			{
				int i;
				for (i=0; i<giPlayerUpdatesNumFriends; i++)
				{
					U32 uID = randInt(giConnections) + giOffset;
					// Ignore duplicates and same-account-id
					if (uID != pAttempt->uAccountID)
						eaiPushUnique(&pAttempt->piRandomFriends, uID);
				}
			}
			pAttempt->fPlayerUpdates = timeElapsed;
			crh_SendPlayerUpdate(pAttempt->link, pAttempt->uAccountID, pAttempt->piRandomFriends);
			giPlayerUpdates[0]++;
			pAttempt->pRelayAddress->stats.iPlayerUpdates[0]++;
		}
	}
}

static void ChatHammerFrame(NetComm *comm, EARRAY_OF(ChatAttempt) *eaAttempts)
{
	EARRAY_FOREACH_BEGIN(*eaAttempts, i);
	{
		ChatAttemptFrame(comm, (*eaAttempts)[i]);
	}
	EARRAY_FOREACH_END;
}

static DWORD WINAPI ChatRelayHammerThread(LPVOID lpParam)
{
	NetComm *pThreadComm;
	ChatAttempt **ppAttempts = NULL;
	U32 uStartTime, uCurTime;
	U32 connTimer;
	U32 uConnectionsPerThread = giConnections / NUM_THREADS;
	U32 uConnections = 0;

	U32 uThreadNum = (U32)(intptr_t) lpParam;
	U32 uThreadOffset = giOffset + uThreadNum * uConnectionsPerThread;


	EXCEPTION_HANDLER_BEGIN
	pThreadComm = commCreate(0,1);
	connTimer = timerAlloc();	
	uStartTime = uCurTime = timeSecondsSince2000();
	while (uCurTime - uStartTime < giTimeToRun)
	{
		// Start additional connections
		if(uConnections < uConnectionsPerThread && timerElapsed(connTimer) >= giDelay / 1000.0)
		{
			char *pAccount = NULL;
			ChatAttempt *pAttempt;

			estrPrintf(&pAccount, "%s%06d", gAccountPrefix, uConnections+uThreadOffset);
			pAttempt = ChatAttemptCreate(uConnections, uConnections+uThreadOffset, pAccount);
			estrDestroy(&pAccount);
			uConnections++;

			eaPush(&ppAttempts, pAttempt);
			timerStart(connTimer);

			pAttempt->timer = timerAlloc();
			timerStart(pAttempt->timer);
			ChatAttemptStartConnection(pThreadComm, pAttempt);
		}

		commMonitor(pThreadComm);
		ChatHammerFrame(pThreadComm, &ppAttempts);

		uCurTime = timeSecondsSince2000();
		Sleep(1);
	}

	uStartTime = uCurTime = timeSecondsSince2000();
	// Continue monitoring for X seconds to let all out-standing commands to finish
	while (uCurTime - uStartTime < 5)
	{
		commMonitor(pThreadComm);
		uCurTime = timeSecondsSince2000();
		Sleep(1);
	}
	sThreadCompletion[uThreadNum] = true;
	EXCEPTION_HANDLER_END
	return 0;
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;
	int i = 0;
#if (!THREADED_CLIENTS)
	int connTimer, mainLoopTimer;
	U32 uStartTime, uCurTime;
#endif

	timerSetMaxTimers(32768);

	ARGV_WIDE_TO_ARGV	
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	gimmeDLLDisable(1);
	FolderCacheChooseMode();
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);
	cmdParseCommandLine(argc, argv);
	utilitiesLibStartup();

	srand(clock());

	for(i = 0; i < 3; i++)
	{
		giChatConnect[i] = 0;
		giChatAuth[i] = 0;
		giChatLogin[i] = 0;
		gfChatConnect[i] = 0.0;
		gfChatAuth[i] = 0.0;
		gfChatLogin[i] = 0.0;
	}
	for (i=0; i<2; i++)
	{
		giChatPrivateMsg[i] = 0;
		giChatFriendList[i] = 0;
		giChatIgnoreList[i] = 0;
		giChatChannelList[i] = 0;
		giChatChannelSend[i] = 0;
		giFriendRequests[i] = 0;
		giIgnoreUpdates[i] = 0;
		giPlayerUpdates[i] = 0;
	}

	i = 0;
	crHammer_LoadRelayHammerSettings();
	if (gbConnectionsArePerRelay)
		giConnections = giConnections * eaSize(&seaChatRelays);


#if TESTSERVER_WORKS
	//ClearGlobalOnTestServer(NULL, "LoginHammer_AccountConnect");
#endif

	giMainTimer = timerAlloc();
	timerStart(giMainTimer);
#if THREADED_CLIENTS
	for (i=0; i<NUM_THREADS; i++)
	{
		DWORD dummy;
		CloseHandle((HANDLE) _beginthreadex(0, 0, ChatRelayHammerThread, (void*)(intptr_t) i, 0, &dummy));
	}
	while (!sThreadCompletion[0])
	{
		utilitiesLibOncePerFrame(timerElapsed(giMainTimer), 0);
		Sleep(1);
	}
#else
	connTimer = timerAlloc();
	mainLoopTimer = timerAlloc();
	timerStart(connTimer);
	timerStart(mainLoopTimer);

	uStartTime = uCurTime = timeSecondsSince2000();
	while (uCurTime - uStartTime < giTimeToRun)
	{
		autoTimerThreadFrameBegin("main");
		
		// Start additional connections
		if ((gbTestConnectionLimitMode || i < giConnections) && timerElapsed(connTimer) >= giDelay / 1000.0)
		{
			char *pAccount = NULL;
			ChatAttempt *pAttempt;

			estrPrintf(&pAccount, "%s%06d", gAccountPrefix, (i+giOffset)%1000000);
			pAttempt = ChatAttemptCreate(i, giOffset+i, pAccount);
			estrDestroy(&pAccount);
			++i;

			eaPush(&gppActiveAttempts, pAttempt);
			timerStart(connTimer);

			pAttempt->timer = timerAlloc();
			timerStart(pAttempt->timer);
			ChatAttemptStartConnection(commDefault(), pAttempt);

			if (i == giConnections)
				sbAllUsersStarted = true;
			if (gbTestConnectionLimitMode && i % 1000 == 0)
				printf("Num sockets opened: %d\n", i);
		}

		utilitiesLibOncePerFrame(timerElapsed(giMainTimer), 0);
		commMonitor(commDefault());
		ChatHammerFrame(commDefault(), &gppActiveAttempts);

		if(timerElapsed(mainLoopTimer) > gfMaxMainLoop)
		{
			gfMaxMainLoop = timerElapsed(mainLoopTimer);
		}

		timerStart(mainLoopTimer);
		uCurTime = timeSecondsSince2000();
		autoTimerThreadFrameEnd();
	}

	uStartTime = uCurTime = timeSecondsSince2000();
	// Continue monitoring for X seconds to let all out-standing commands to finish
	while (uCurTime - uStartTime < giTimeToWait)
	{
		commMonitor(commDefault());
		uCurTime = timeSecondsSince2000();
		Sleep(5);
	}
#endif
	DumpStats();
	gbComplete = true;
	/*EARRAY_FOREACH_BEGIN(gppActiveAttempts, j);
	{
		ChatAttemptDestroy(gppActiveAttempts[j]);
	}
	EARRAY_FOREACH_END;*/
	while (1)
	{
		// showing stats
		Sleep(1);
	}

#if TESTSERVER_WORKS
	if(gbIsTestServerHostSet)
	{
		commFlushAndCloseAllComms(5.0f);
		return 0;
	}
#endif

	while(true)
	{
		Sleep(1);
	}

	return 0;
}

#include "AutoGen/ChatRelayHammer_h_ast.c"