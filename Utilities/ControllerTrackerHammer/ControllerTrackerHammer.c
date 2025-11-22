#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "file.h"
#include "process_util.h"
#include "earray.h"
#include "cmdparse.h"
#include "osdependent.h"
#include "net.h"
#include "textparser.h"
#include "ControllerTrackerHammer_c_ast.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "accountnet.h"
#include "../../Core/NewControllerTracker/pub/NewControllerTracker_Pub.h"
#include "NewControllerTracker_Pub_h_ast.h"
#include "structNet.h"
#include "rand.h"
#include "TimedCallback.h"

static char sCTName[256] = "awerner";
AUTO_CMD_STRING(sCTName, CTName);

static int siMsecsPause = 100;
AUTO_CMD_INT(siMsecsPause, MsecsPause);

int siNumHammers = 10;
AUTO_CMD_INT(siNumHammers, NumHammers);

AUTO_ENUM;
typedef enum enumHammerState
{
	HAMMERSTATE_PAUSING,
	HAMMERSTATE_CONNECTING,
	HAMMERSTATE_SENT_TICKET,
} enumHammerState;

AUTO_STRUCT;
typedef struct Hammer
{
	enumHammerState eState;
	NetLink *pLink; NO_AST
	CommConnectFSM *pConnectFSM; NO_AST
	S64 iNextConnectTime;
	bool bDestroy;
} Hammer;

Hammer **sppHammers = NULL;


static S64 siNumCompleted = 0;



void HammerMessageCB(Packet *pak,int cmd, NetLink *link, Hammer *pHammer)
{
	ShardInfo_Basic_List *pList;

	//shouldn't be getting any other cmd than this one
	assert(pHammer);
	assert(cmd == FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST);

	pList = StructCreate(parse_ShardInfo_Basic_List);
	ParserRecvStructSafe(parse_ShardInfo_Basic_List, pak, pList);

	assert(eaSize(&pList->ppShards));

	StructDestroy(parse_ShardInfo_Basic_List, pList);

	pHammer->eState = HAMMERSTATE_PAUSING;


	pHammer->iNextConnectTime = timeMsecsSince2000() + randomIntRange(siMsecsPause / 2, siMsecsPause * 2);

	linkSetUserData(pHammer->pLink, NULL);
	linkFlushAndClose(&pHammer->pLink, "MessageCB");

	siNumCompleted++;

}

void HammerDisconnectCB(NetLink *link, Hammer *pHammer)
{
	assert(pHammer == NULL);
}

//returns true if it destroyed itself
bool UpdateHammer(Hammer *pHammer)
{
	S64 iCurTime = timeMsecsSince2000();
	switch (pHammer->eState)
	{
	xcase HAMMERSTATE_PAUSING:
		if (pHammer->bDestroy)
		{
			StructDestroy(parse_Hammer, pHammer);
			return true;
		}


		if (pHammer->iNextConnectTime < iCurTime)
		{
			pHammer->eState = HAMMERSTATE_CONNECTING;
		}

	xcase HAMMERSTATE_CONNECTING:
		if (commConnectFSMForTickFunctionWithRetrying(&pHammer->pConnectFSM, &pHammer->pLink, "Link to CT", 60.0f, commDefault(), 
			LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH,
			sCTName, NEWCONTROLLERTRACKER_GENERAL_MCP_PORT, HammerMessageCB,0,HammerDisconnectCB,0, NULL, 0, NULL, 0))
		{
			Packet *pPack;
			linkSetUserData(pHammer->pLink, pHammer);
			pPack = pktCreate(pHammer->pLink, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST);
			pktSendString(pPack, "all");
			pktSendString(pPack, ACCOUNT_FASTLOGIN_LABEL);
			pktSendBits(pPack, 32, 1234); //fake account ID
			pktSendBits(pPack, 32, 1234); //fake ticket ID
			pktSend(&pPack);
			pHammer->eState = HAMMERSTATE_SENT_TICKET;
		}

	xcase HAMMERSTATE_SENT_TICKET:
		break;
		//do nothing
	}

	return false;
}


void DumpStats(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static S64 siTicksLastTime = 0;
	static S64 siTicketsLastTime = 0;

	if (siTicksLastTime)
	{
		printf("During last 10 seconds, ran at %I64d fps, processed %I64d tickets per second\n",
			(gUtilitiesLibTicks - siTicksLastTime) / 10, 
			(siNumCompleted - siTicketsLastTime) / 10);
	}

	siTicksLastTime = gUtilitiesLibTicks;
	siTicketsLastTime = siNumCompleted;
}

int main(int argc,char **argv)
{
	int i;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS

	setDefaultAssertMode();
	memMonitorInit();
	
	

	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);


	for (i = 0; i < siNumHammers; i++)
	{
		eaPush(&sppHammers, StructCreate(parse_Hammer));
	}

	TimedCallback_Add(DumpStats, NULL, 10.0f);

	while(1)
	{
		utilitiesLibOncePerFrame(REAL_TIME);
		commMonitor(commDefault());
		Sleep(1);

		for (i = eaSize(&sppHammers) - 1; i >= 0; i--)
		{
			if (UpdateHammer(sppHammers[i]))
			{
				eaRemoveFast(&sppHammers, i);
			}
		}
	}


	EXCEPTION_HANDLER_END

}


#include "ControllerTrackerHammer_c_ast.c"
#include "NewControllerTracker_Pub_h_ast.c"