#include "ChatHammer.h"
#include "accountnet.h"
#include "chatCommonFake.h"
#include "cmdparse.h"
#include "FolderCache.h"
#include "file.h"
#include "gimmeDLLWrapper.h"
#include "GlobalTypes.h"
#include "MemoryPool.h"
#include "net.h"
#include "objPath.h"
#include "rand.h"
#include "sock.h"
#include "StressTest.h"
#include "structNet.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"

#include "ChatTest.h"
#include "LoadSimulation.h"

#include "AutoGen/ChatHammer_h_ast.h"

#include "Autogen/AppLocale_h_ast.h"
#include "Autogen/ChatData_h_ast.h"
#include "Autogen/ChatTest_h_ast.h"
#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/chatCommonStructs_h_ast.h"

//extern ParseTable parse_ChatMessage[];
//extern ParseTable parse_ChatUserInfo[];

U32 gConnectStartTime;

int giDelay = 1;
U32 giTestLength = 60;

bool gbInitDB = true;
bool gbDebugText = false;
bool gbRunLoadSimulation = false;

char gGlobalChatServer[128] = "localhost";
U32 gTestStartTime;
float gfPercentPass = 1.0, gfPercentCreate = 0.0;

AUTO_CMD_INT(giDelay, SetDelay); // Specifies the delay in milliseconds.
AUTO_CMD_INT(gbDebugText, DebugText);
AUTO_CMD_INT(gbRunLoadSimulation, RunLoadSimulation);
AUTO_CMD_INT(gbInitDB, InitDB);
AUTO_CMD_INT(giTestLength, SetTestLength);

AUTO_CMD_STRING(gGlobalChatServer, SetGlobalChatServer); // Specifies the address of the GlobalChatServer to attack.

typedef int(TestSizeFunction)();

typedef struct ChatTestSuite
{
	const char *name;
	ChatTestCase *tests;
	TestSizeFunction *size;
} ChatTestSuite;

extern ChatTestParameters gChatTestParams;
ChatTestCase InitTests[] = 
{
	{"Init Users", NULL, &InitUsers, &gChatTestParams.iTotalUsers},
	{"Init Channels", NULL, &InitChannels, &gChatTestParams.iTotalChannels},
	{"Fill Friends List", &InitFillFriendsList, &FillFriendsList, &gChatTestParams.iFriends},
};

int GetInitTestSize()
{
	return sizeof(InitTests)/sizeof(ChatTestCase);
}

ChatTestSuite gLoadInitSuite[] = 
{
	{"InitTests", InitTests, &GetInitTestSize},
};

int GetNumberOfLoadInitSuites()
{
	return sizeof(gLoadInitSuite)/sizeof(ChatTestSuite);
}

ChatTestSuite gTestSuite[] = 
{
	{"InitTests", InitTests, &GetInitTestSize},
	{"ChatTests", ChatTests, &GetChatTestSize},
	//{"FriendTests", FriendTests, &GetFriendTestSize},
	//{"MailTests", MailTests, &GetMailTestSize},
};

int GetNumberOfTestSuites()
{
	return sizeof(gTestSuite)/sizeof(ChatTestSuite);
}

static void ResetMetrics(NetLink *link)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_RESET_METRICS);

	pktSend(&pkt);
}

static void RequestMetrics(NetLink *link)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_METRICS_REQUEST);

	pktSend(&pkt);
}

U32 gReturnValues[CHATRETURN_UNKNOWN_COMMAND + 1];
U32 gTotalReturns;
U32 gSentCommands;
bool gWaitForMetrics = false;

void InitReturnValues(void)
{
	U32 i;
	for(i = 0; i < CHATRETURN_UNKNOWN_COMMAND + 1; ++i)
		gReturnValues[i] = 0;

	gSentCommands = 0;
	gTotalReturns = 0;
}

static void IncremementReturnValue(U32 value)
{
	++gReturnValues[value];
	++gTotalReturns;
}

#define PRINT_RETURN_VALUE(return_value) \
	if(gReturnValues[return_value] > 0) printf("%s : %lu\n", #return_value, gReturnValues[return_value])

void PrintReturnValues(void)
{
	printf("Commands sent: %lu\n", gSentCommands);

	PRINT_RETURN_VALUE(CHATRETURN_NONE);
	PRINT_RETURN_VALUE(CHATRETURN_FWD_NONE);
	PRINT_RETURN_VALUE(CHATRETURN_FWD_SENDER);
	PRINT_RETURN_VALUE(CHATRETURN_FWD_ALLLOCAL);
	PRINT_RETURN_VALUE(CHATRETURN_FWD_ALLLOCAL_MINUSENDER);
	PRINT_RETURN_VALUE(CHATRETURN_UNSPECIFIED);
	PRINT_RETURN_VALUE(CHATRETURN_INVALIDNAME);
	PRINT_RETURN_VALUE(CHATRETURN_USER_OFFLINE);
	PRINT_RETURN_VALUE(CHATRETURN_USER_DNE);
	PRINT_RETURN_VALUE(CHATRETURN_USER_PERMISSIONS);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_ALREADYEXISTS);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_RESERVEDPREFIX);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_WATCHINGMAX);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_FULL);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_ALREADYMEMBER);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_NOTMEMBER);
	PRINT_RETURN_VALUE(CHATRETURN_CHANNEL_DNE);
	PRINT_RETURN_VALUE(CHATRETURN_USER_IGNORING);
	PRINT_RETURN_VALUE(CHATRETURN_INVALIDMSG);
	PRINT_RETURN_VALUE(CHATRETURN_UNKNOWN_COMMAND);
	printf("Total received: %lu\n", gTotalReturns);
}

int gMetricSuiteIter = 0;
int gMetricTestIter = 0;

static void WriteMetrics(U32 avg)
{
	if(gbRunLoadSimulation)
	{
		printf("Throughput : %lu\n", avg);
	}
	else
	{
		if(gMetricSuiteIter < GetNumberOfTestSuites())
		{
			ChatTestSuite *cur_suite = &gTestSuite[gMetricSuiteIter];
			if(gMetricTestIter < cur_suite->size())
			{
				ChatTestCase *cur_test = &cur_suite->tests[gMetricTestIter];
				cur_test->avg_throughput = avg;
				if(++gMetricTestIter >= cur_suite->size())
				{
					++gMetricSuiteIter;
					gMetricTestIter = 0;
				}
			}
		}
	}
}

static bool DoneTesting()
{
	return gMetricSuiteIter >= GetNumberOfTestSuites();
}

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "ChatHammer"););

MP_DEFINE(PCBoneRef);
MP_DEFINE(PCCategoryRef);
MP_DEFINE(PCColor);
MP_DEFINE(PCExtraTexture);
MP_DEFINE(PCMaterialNameRef);
MP_DEFINE(PCRegionRef);
MP_DEFINE(PCScaleEntry);
MP_DEFINE(PCScaleInfo);
MP_DEFINE(PCScaleInfoGroup);
MP_DEFINE(PCTextureNameRef);

bool gbComplete = false;
NetLink *gFakeChatLink = NULL;

extern FakeChannelData **gpChannelData;

static NetComm *spFakeChatServerComm = NULL;

NetComm *getFakeChatServerComm(void)
{
	if (!spFakeChatServerComm)
		spFakeChatServerComm = commCreate(1,1);
	return spFakeChatServerComm;
}

int ChatHammer_pktSend(Packet **pakptr)
{
	++gSentCommands;
	return pktSend(pakptr);
}

static void PrintMetricValues()
{
	int suite_iter;
	int test_iter;

	printf("Average throughput for each test.\n");

	for(suite_iter = 0; suite_iter < GetNumberOfTestSuites(); ++suite_iter)
	{
		ChatTestSuite *cur_suite = &gTestSuite[suite_iter];
		printf("Test suite: %s\n", cur_suite->name);
		for(test_iter = 0; test_iter < cur_suite->size(); ++test_iter)
		{
			ChatTestCase *cur_test = &cur_suite->tests[test_iter];
			printf("\t%s : Sent %d/second, Processed %lu/second, %lu/hour\n", cur_test->name, (*cur_test->count * 1000)/giDelay, cur_test->avg_throughput, cur_test->avg_throughput*3600);
		}
	}
}

static void DumpStats()
{
	PrintReturnValues();
	PrintMetricValues();
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

static void GlobalChatConnect(NetLink* link,void *user_data)
{
	linkSetTimeout(link, 0.0);
	gConnectStartTime = timeSecondsSince2000();
	printf("Connected to Global Chat Server\n");
}

static void GlobalChatDisconnect(NetLink* link,void *user_data)
{
	char *pLinkDisReason = NULL;
	linkGetDisconnectReason(link, &pLinkDisReason);
	printf("Disconnected from Global Chat Server: %s\n", pLinkDisReason);
	printf("Uptime: %lu seconds\n", timeSecondsSince2000() - gConnectStartTime);
	linkRemove(&link);
	estrDestroy(&pLinkDisReason);
}

#define PACKET_CASE(cmdName, packet)	xcase cmdName: PERFINFO_AUTO_START(#cmdName, 1); START_BIT_COUNT(packet, #cmdName)
#define PACKET_CASE_END(packet) STOP_BIT_COUNT(packet); PERFINFO_AUTO_STOP()

static void GlobalChatHandleMessage(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	PACKET_CASE(FROM_GLOBALCHATSERVER_COMMAND, pkt);
		{
			U32 retVal = pktGetU32(pkt);
			char *pCommandString = pktGetStringTemp(pkt);

			IncremementReturnValue(retVal);

			if(gbDebugText)
				printf ("Local: %s%s\n", pCommandString, (retVal == CHATRETURN_UNKNOWN_COMMAND) ? "Unknown Command" : "");
		}
	PACKET_CASE_END(pkt);

	PACKET_CASE(FROM_GLOBALCHATSERVER_MESSAGE, pkt);
		{
			U32 uAccountID = pktGetU32(pkt);
			int eChatType = pktGetU32(pkt);
			const char *channel = pktGetStringTemp(pkt);
			const char *message = pktGetStringTemp(pkt);

			if(gbDebugText)
				printf("Message: %d, %d, %s, %s\n", uAccountID, eChatType, channel, message);
		}
	PACKET_CASE_END(pkt);

	PACKET_CASE(FROM_GLOBALCHATSERVER_ALERT, pkt);
		{
			U32 uAccountID = pktGetU32(pkt);
			const char *title = pktGetStringTemp(pkt);
			const char *message = pktGetStringTemp(pkt);

			if(gbDebugText)
				printf("Alert: %d, %s, %s\n", uAccountID, title, message);
		}
	PACKET_CASE_END(pkt);

	PACKET_CASE(FROM_GLOBALCHATSERVER_ACCOUNT_LIST, pkt);
		{
			//char *dataString = pktGetStringTemp(pkt);
		}
	PACKET_CASE_END(pkt);

	PACKET_CASE(FROM_GLOBALCHATSERVER_CHANNEL_LIST, pkt);
		{
			//char *dataString = pktGetStringTemp(pkt);
		}
	PACKET_CASE_END(pkt);

	PACKET_CASE(FROM_GLOBALCHATSERVER_CHANNEL_DELETE, pkt);
		if(gbDebugText)
		{
			U32 uID = pktGetU32(pkt);
			printf("Delete: %d\n", uID);
		}
	PACKET_CASE_END(pkt);

	PACKET_CASE(FROM_GLOBALCHATSERVER_METRICS, pkt);
		{
			U32 avg = pktGetU32(pkt);
			WriteMetrics(avg);
			gTestStartTime = timeSecondsSince2000();
			gWaitForMetrics = false;
			if(gbDebugText)
				printf("Average packets in interval: %lu\n", avg);
		}
	PACKET_CASE_END(pkt);

	xdefault:
		//printf("[Error: Unknown packet received]\n");
		break;
	}
}

static void InitChatTestCase(ChatTestSuite testsuites[], int size, int *suite_iter, int *test_iter)
{
	ChatTestSuite *cur_suite;
	ChatTestCase *cur_test;

	*suite_iter = 0;
	*test_iter = 0;
	
	if(!gbInitDB)
		*suite_iter++;

	if(*suite_iter < size)
	{
		cur_suite = &testsuites[*suite_iter];
		printf("%s.\n", cur_suite->name);
		if(*test_iter < cur_suite->size())
		{
			cur_test = &cur_suite->tests[*test_iter];
			printf("\t%s.\n", cur_test->name);
			if(cur_test->init_function)
			{
				PERFINFO_AUTO_START("Init Test", 1);
				PERFINFO_AUTO_START_STATIC(cur_test->name, &cur_test->init_pi, 1);
				cur_test->init_function(gFakeChatLink);
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
			}
		}
	}	
}

static void IncrementChatTestCase(ChatTestSuite testsuites[], int size, int *suite_iter, int *test_iter)
{
	gWaitForMetrics = true;

	if(*suite_iter < size)
	{
		ChatTestSuite *cur_suite = &testsuites[*suite_iter];
		++*test_iter;
		if(*test_iter < cur_suite->size())
		{
			ChatTestCase *cur_test = &cur_suite->tests[*test_iter];
			printf("\t%s.\n", cur_test->name);

			if(cur_test->init_function)
			{
				PERFINFO_AUTO_START("Init Test", 1);
				PERFINFO_AUTO_START_STATIC(cur_test->name, &cur_test->init_pi, 1);
				cur_test->init_function(gFakeChatLink);
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
			}

			RequestMetrics(gFakeChatLink);
			ResetMetrics(gFakeChatLink);
		}
		else
		{
			++*suite_iter;
			if(*suite_iter < size)
			{
				ChatTestCase *cur_test;

				cur_suite = &testsuites[*suite_iter];
				*test_iter = 0;
				cur_test = &cur_suite->tests[*test_iter];
				printf("%s.\n", cur_suite->name);
				printf("\t%s.\n", cur_test->name);

				if(cur_test->init_function)
				{
					PERFINFO_AUTO_START("Init Test", 1);
					PERFINFO_AUTO_START_STATIC(cur_test->name, &cur_test->init_pi, 1);
					cur_test->init_function(gFakeChatLink);
					PERFINFO_AUTO_STOP();
					PERFINFO_AUTO_STOP();
				}

				RequestMetrics(gFakeChatLink);
				ResetMetrics(gFakeChatLink);
			}
			else
			{
				printf("Done Testing.\n");
				RequestMetrics(gFakeChatLink);
			}
		}
	}
}

static void FakeChatUpdate(ChatTestSuite testsuites[], int size, int *suite_iter, int *test_iter)
{
	if(gWaitForMetrics)
		return;

	if(*suite_iter < size)
	{
		ChatTestSuite *cur_suite = &testsuites[*suite_iter];
		if(*test_iter < cur_suite->size())
		{
			ChatTestCase *cur_test = &cur_suite->tests[*test_iter];
			if(cur_test->test_function)
			{
				bool bResult;

				PERFINFO_AUTO_START("Run Test", 1);
				PERFINFO_AUTO_START_STATIC(cur_test->name, &cur_test->test_pi, 1);
				bResult = cur_test->test_function(gFakeChatLink, *cur_test->count);
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();

				if(bResult)
				{
					IncrementChatTestCase(testsuites, size, suite_iter, test_iter);
				}
			}
		}
	}
}

static void RunLoadSimulation()
{
	int testsuite = -1;
	int test = 0;

	ReadLoadSimulationStruct();

	gFakeChatLink = commConnect(getFakeChatServerComm(), LINKTYPE_DEFAULT, LINK_COMPRESS|LINK_FORCE_FLUSH, gGlobalChatServer, DEFAULT_GLOBAL_CHATSERVER_PORT,
		GlobalChatHandleMessage, GlobalChatConnect, GlobalChatDisconnect, 0);

	linkConnectWait(&gFakeChatLink, 0);

	while(linkConnected(gFakeChatLink) && testsuite < GetNumberOfLoadInitSuites())
	{
		autoTimerThreadFrameBegin("main");
		commMonitor(getFakeChatServerComm());

		if(gFakeChatLink && linkConnected(gFakeChatLink))
		{
			if(testsuite < 0)
				InitChatTestCase(gLoadInitSuite, GetNumberOfLoadInitSuites(), &testsuite, &test);

			FakeChatUpdate(gLoadInitSuite, GetNumberOfLoadInitSuites(), &testsuite, &test);
		}
		//Sleep(giDelay);
		autoTimerThreadFrameEnd();
	}

	while(linkConnected(gFakeChatLink) && gWaitForMetrics && gFakeChatLink)
	{
		autoTimerThreadFrameBegin("main");
		commMonitor(getFakeChatServerComm());
		//Sleep(giDelay);
		autoTimerThreadFrameEnd();
	}

	InitLoadSimulation(gFakeChatLink);

	while(!IsLoadSimulationDone() && gFakeChatLink && linkConnected(gFakeChatLink))
	{
		U32 start_time = timeSecondsSince2000();
		static int timer = 0;

		autoTimerThreadFrameBegin("main");
		gSentCommands = 0;

		if(!timer)
			timer = timerAlloc();


		UpdateLoadSimulator(gFakeChatLink);

		do
		{
			utilitiesLibOncePerFrame(timerElapsedAndStart(timer), 1);
			commMonitor(getFakeChatServerComm());
		} while(start_time >= timeSecondsSince2000());

		printf("Commands sent in %lu %s: %d\n", timeSecondsSince2000() - start_time, timeSecondsSince2000() - start_time == 1 ? "second" : "seconds", gSentCommands);
		RequestMetrics(gFakeChatLink);
		ResetMetrics(gFakeChatLink);
		autoTimerThreadFrameEnd();
	}
}

static void RunStressTest()
{
	int testsuite = -1;
	int test = 0;
	int timer = timerAlloc();

	gFakeChatLink = commConnect(getFakeChatServerComm(), LINKTYPE_DEFAULT, LINK_COMPRESS|LINK_FORCE_FLUSH, gGlobalChatServer, DEFAULT_GLOBAL_CHATSERVER_PORT,
		GlobalChatHandleMessage, GlobalChatConnect, GlobalChatDisconnect, 0);

	linkConnectWait(&gFakeChatLink, 0);

	InitializeStressTests(gFakeChatLink, timer);
	RunStressTests(gFakeChatLink, timer);
}

int main(int argc, char **argv)
{
	DO_AUTO_RUNS;

	FolderCacheChooseMode();
	fileLoadGameDataDirAndPiggs();

	gimmeDLLDisable(1);
	utilitiesLibStartup();
 
	setConsoleTitle("ChatHammer");
	printf("ChatHammer\n");

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);
	cmdParseCommandLine(argc, argv);

	eaCreate(&gpChannelData);
	InitReturnValues();

	if(gbRunLoadSimulation)
		RunLoadSimulation();
	else
		RunStressTest();

	if(linkConnected(gFakeChatLink))
		printf("Test Succeeded\n");
	else
		printf("Test Failed\n");

	while(true)
		Sleep(1);

	return 0;
}

#include "AutoGen/ChatHammer_h_ast.c"