#include "StressTest.h"

#include "ChatCommands.h"
#include "ChatHammer.h"
#include "ChatTest.h"
#include "GlobalComm.h"
#include "net.h"
#include "sock.h"
#include "textparser.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"

#include "AutoGen/ChatTest_h_ast.h"

extern ChatTestParameters gChatTestParams;
extern U32 gTestStartTime;
extern U32 giTestLength;

bool InitChannelMembership(NetLink *link, const int count)
{
	static int iLastUserID = 0;
	int i;	
	if (iLastUserID >= gChatTestParams.iJoins)
		return true;
	for(i = iLastUserID; i < iLastUserID+count; ++i)
	{
		if (i == gChatTestParams.iJoins)
			break;
		JoinChannel(link, i+1, 0);
	}
	iLastUserID = i;
	if (iLastUserID == gChatTestParams.iJoins)
		return true;
	return false;
}

void InitializeStressTests(NetLink *link, int timer)
{
	bool bInitChannelMembersDone = false;
	bool bInitFriendsDone = false;
	StructInit(parse_ChatTestParameters, &gChatTestParams);
	ParserReadTextFile("chatHammer_Stress.txt", parse_ChatTestParameters, &gChatTestParams, 0);
	// Max number of joins can't be greater than the total number of users
	gChatTestParams.iJoins = min(gChatTestParams.iTotalUsers, gChatTestParams.iJoins);
	RegisterShard(link);
	while(linkConnected(link))
	{
		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(timer), 1);
		commMonitor(getFakeChatServerComm());

		if (!InitUsersDone())
			InitUsers(link, gChatTestParams.iUsersPerTick);
		else if (!InitChannelsDone())
			InitChannels(link, gChatTestParams.iChannelsPerTick);
		else
		{
			if (!bInitChannelMembersDone)
				bInitChannelMembersDone = InitChannelMembership(link, gChatTestParams.iJoinsPerTick);
			if (!bInitFriendsDone)
				bInitFriendsDone = InitFriends(link, gChatTestParams.iFriends);
			if (bInitChannelMembersDone && bInitFriendsDone)
				break;
		}
		autoTimerThreadFrameEnd();
	}
}

///////////////////////////////
// Stress Tests

extern U32 gTotalReturns;
extern U32 gSentCommands;

static void PrivateMessageStressTest_Tick(NetLink *link, const int count)
{
	static char *pCommandString = NULL;
	int i;
	assert(next_user_id >= 1);
	if (!pCommandString)
		CreatePrivateMessage(&pCommandString, 1, 1, "This is a test message of some intermediate length. This is a test message of some intermediate length.");
	for(i = 0; i < count; ++i)
	{
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		pktSendString(pkt, pCommandString);
		ChatHammer_pktSend(&pkt);
	}
}

static void RunPrivateMessageStressTest(NetLink *link, int timer)
{
	U32 lastTestTime, curTime;
	InitReturnValues();
	curTime = lastTestTime = gTestStartTime = timeSecondsSince2000();
	printf("\nRunning Private Message Stress Test:\n");
	while(linkConnected(link))
	{
		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(timer), 1);
		curTime = timeSecondsSince2000();
		if (curTime - lastTestTime  > 6)
		{
			lastTestTime = curTime;
			while (1)
			{
				curTime = timeSecondsSince2000();
				commMonitor(getFakeChatServerComm());
				if (curTime - lastTestTime > 3)
					break;
				Sleep(5);
			}
			lastTestTime = curTime;
			if (gTotalReturns >= gSentCommands)
			{
				printf("Sent: %d commands, Received: %d responses.\n", gSentCommands, gTotalReturns);
				++gChatTestParams.iTells;
				InitReturnValues();
			}
			else
			{
				printf("Sent: %d commands, Received: %d responses.\n", gSentCommands, gTotalReturns);
				printf("Max Reached Over 3 Seconds.\n");
				break;
			}
		}
		commMonitor(getFakeChatServerComm());
		PrivateMessageStressTest_Tick(link, gChatTestParams.iTells);
		if (gTestStartTime + giTestLength < curTime)
			break;
		autoTimerThreadFrameEnd();
	}
	PrintReturnValues();
}

static void ChannelStressTest_Tick(NetLink *link, const int count)
{
	static char *pCommandString = NULL;
	int i;
	assert(next_user_id >= 1);
	if (!pCommandString)
		CreateChannelMessage(&pCommandString, 1, 1, "This is a test message of some intermediate length. This is a test message of some intermediate length.");
	for(i = 0; i < count; ++i)
	{
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		pktSendString(pkt, pCommandString);
		ChatHammer_pktSend(&pkt);
	}
}
static void RunChannelStressTest(NetLink *link, int timer)
{
	U32 lastTestTime, curTime;
	printf("\nRunning Channel Message Stress Test:\n");
	InitReturnValues();
	curTime = lastTestTime = gTestStartTime = timeSecondsSince2000();
	while(linkConnected(link))
	{
		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(timer), 1);
		curTime = timeSecondsSince2000();
		if (curTime - lastTestTime  > 6)
		{
			lastTestTime = curTime;
			while (1)
			{
				curTime = timeSecondsSince2000();
				commMonitor(getFakeChatServerComm());
				if (curTime - lastTestTime > 3)
					break;
				Sleep(5);
			}
			lastTestTime = curTime;
			if (gTotalReturns >= gSentCommands)
			{
				printf("Sent: %d commands, Received: %d responses.\n", gSentCommands, gTotalReturns);
				++gChatTestParams.iChannelMessages;
				InitReturnValues();
			}
			else
			{
				printf("Sent: %d commands, Received: %d responses.\n", gSentCommands, gTotalReturns);
				printf("Max Reached Over 3 Seconds.\n");
				break;
			}
		}
		commMonitor(getFakeChatServerComm());
		ChannelStressTest_Tick(link, gChatTestParams.iChannelMessages);
		if (gTestStartTime + giTestLength < curTime)
			break;
		autoTimerThreadFrameEnd();
	}
	PrintReturnValues();
}

static void PlayerUpdateStressTest_Tick(NetLink *link, const int count)
{
	static char *pCommandString = NULL;
	int i;
	assert(next_user_id >= 1);
	if (!pCommandString)
		CreateUserUpdate(&pCommandString, 1);
	for(i = 0; i < count; ++i)
	{
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		pktSendString(pkt, pCommandString);
		ChatHammer_pktSend(&pkt);
	}
}
static void RunPlayerUpdateStressTest(NetLink *link, int timer)
{
	U32 lastTestTime, curTime;
	printf("\nRunning Player Updates Stress Test:\n");
	InitReturnValues();
	curTime = lastTestTime = gTestStartTime = timeSecondsSince2000();
	while(linkConnected(link))
	{
		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(timer), 1);
		curTime = timeSecondsSince2000();
		if (curTime - lastTestTime  > 6)
		{
			lastTestTime = curTime;
			while (1)
			{
				curTime = timeSecondsSince2000();
				commMonitor(getFakeChatServerComm());
				if (curTime - lastTestTime > 3)
					break;
				Sleep(5);
			}
			lastTestTime = curTime;
			if (gTotalReturns >= gSentCommands*50)
			{
				printf("Sent: %d commands, Received: %d responses.\n", gSentCommands, gTotalReturns);
				++gChatTestParams.iPlayerUpdates;
				InitReturnValues();
			}
			else
			{
				printf("Sent: %d commands, Received: %d responses.\n", gSentCommands, gTotalReturns);
				printf("Max Reached Over 3 Seconds.\n");
				break;
			}
		}
		commMonitor(getFakeChatServerComm());
		PlayerUpdateStressTest_Tick(link, gChatTestParams.iPlayerUpdates);
		if (gTestStartTime + giTestLength < curTime)
			break;
		autoTimerThreadFrameEnd();
	}
	PrintReturnValues();
}

static void ClearNetBuffer(NetLink *link)
{
	U32 uLastNumReturns;
	do
	{
		uLastNumReturns = gTotalReturns;
		Sleep(3);
		commMonitor(getFakeChatServerComm());
	} while (linkConnected(link) && uLastNumReturns != gTotalReturns);
}

void RunStressTests(NetLink *link, int timer)
{
	// Get all responses from initializations
	ClearNetBuffer(link);

	// Run Private Message Test
	//RunPrivateMessageStressTest(link, timer);
	//ClearNetBuffer(link);
	
	// Run Channel Message Tests
	//RunChannelStressTest(link, timer);
	//ClearNetBuffer(link);

	// Run Player Updates Stress Test
	RunPlayerUpdateStressTest(link, timer);
	ClearNetBuffer(link);
}
