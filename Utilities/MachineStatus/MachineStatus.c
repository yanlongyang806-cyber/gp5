#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"

#include "timing.h"

#include "SimpleWindowManager.h"
#include "StringUtil.h"
#include "GlobalTypes.h"
#include "net.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "MachineStatusPub.h"
#include "MachineStatusPub_h_Ast.h"
#include "structNet.h"
#include "MainScreen.h"
#include "MachineStatus.h"
#include "SimpleStatusMonitoring.h"


#include "StashTable.h"
#include "..\..\libs\PatchClientLib\PatchClientLibStatusMonitoring.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "TimedCallback.h"
#include "gimmeDLLWrapper.h"
#include "resource.h"
#include "../../libs/ServerLib/pub/GenericHttpServing.h"
#include "../../libs/ServerLib/pub/ServerLib.h"

bool gbSomethingChanged = true;

bool bEmulateOverlord = false;
AUTO_CMD_INT(bEmulateOverlord, EmulateOverlord);

typedef struct MachineStatusConnectionUserData
{
	char shardName[256];
	NetLink *pLink;
} MachineStatusConnectionUserData;

StashTable sUserDatasByShardName = NULL;

MachineStatusUpdate **gppCurrentShards = NULL;

void HandleStatusUpdate(MachineStatusUpdate *pUpdate)
{
	int i;
	gbSomethingChanged = true;

	for (i = 0; i < eaSize(&gppCurrentShards); i++)
	{
		if (stricmp(gppCurrentShards[i]->pShardName, pUpdate->pShardName) == 0)
		{
			StructCopy(parse_MachineStatusUpdate, pUpdate, gppCurrentShards[i], 0, 0, 0);
			StructDestroy(parse_MachineStatusUpdate, pUpdate);
			return;
		}
	}

	eaPush(&gppCurrentShards, pUpdate);
}

static void HandleMachineStatusMessage(Packet *pak,int cmd, NetLink *link, MachineStatusConnectionUserData *pUserData)
{
	if (!sUserDatasByShardName)
	{
		sUserDatasByShardName = stashTableCreateWithStringKeys(10, StashDefault);
	}

	switch(cmd)
	{
	xcase TO_MACHINESTATUS_HERE_IS_UPDATE:
		{
			MachineStatusUpdate *pUpdate = StructCreate(parse_MachineStatusUpdate);
			ParserRecvStructSafe(parse_MachineStatusUpdate, pak, pUpdate);

			pUpdate->iTime = timeSecondsSince2000();

			if (!pUserData->shardName[0])
			{
				strcpy_trunc(pUserData->shardName, pUpdate->pShardName);
				pUserData->pLink = link;
				stashAddPointer(sUserDatasByShardName, pUserData->shardName, pUserData, true);
			}
			HandleStatusUpdate(pUpdate);
		}
	}
}

static void MachineStatusDisconnect(NetLink* link,MachineStatusConnectionUserData *pUserData)
{
	if (pUserData->shardName[0])
	{
		stashRemovePointer(sUserDatasByShardName, pUserData->shardName, NULL);
	}
}


NetLink *FindNetLinkFromShardName(char *pShardName)
{
	MachineStatusConnectionUserData *pUserData;

	if (!stashFindPointer(sUserDatasByShardName, pShardName, &pUserData))
	{
		return NULL;
	}

	if (!pUserData->pLink)
	{
		return NULL;
	}

	if (stricmp(pUserData->shardName, pShardName) != 0)
	{
		return NULL;
	}

	return pUserData->pLink;
}

void PCLStatusCB(PCLStatusMonitoringUpdate *pUpdate)
{
	MainScreen_GetPatchingUpdate(pUpdate);


}

int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;


	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	SetAppGlobalType(GLOBALTYPE_MACHINESTATUS);

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

    gimmeDLLDisable(1);

	// MachineStatus loads relative path files from a .hogg file baked into the executable.
	PigSetIncludeResource(SRCDATAHOGG_BIN);
	FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);


	preloadDLLs(0);

	utilitiesLibStartup();
	serverLibStartup(argc, argv);


	srand((unsigned int)time(NULL));



/*
	SimpleWindowManager_Init("ShardLauncher", false);
	SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_STARTINGSCREEN,
		0, IDD_STARTSCREEN, true, startScreenDlgProc_SWM, startScreenDlgProc_SWMTick, NULL);
	SimpleWindowManager_Run(NULL, NULL);*/
	/*
	pRun = DoStartingScreen(&bNeedToConfigure);
	if (!pRun)
	{
		QUITFAIL();

	}

	if (bNeedToConfigure)
	{
		if (!ConfigureARun(pRun))
		{
			QUITFAIL();
		}
	}

	if (!RunTheShard(pRun))
	{
		QUITFAIL();
	}
*/


	hideConsoleWindow();

	commListenIp(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH|LINK_SMALL_LISTEN, 
		DEFAULT_MACHINESTATUS_PORT, HandleMachineStatusMessage, NULL, MachineStatusDisconnect, sizeof(MachineStatusConnectionUserData), htonl(INADDR_LOOPBACK));

	{
		U32 *spTimeouts = NULL;
		ea32Push(&spTimeouts, 15);
		ea32Push(&spTimeouts, 60);
	
		PCLStatusMonitoring_Begin(commDefault(), PCLStatusCB, DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT, spTimeouts, 300, 300);
		ea32Destroy(&spTimeouts);
	}

	SimpleStatusMonitoring_Begin(MACHINESTATUS_LOCAL_STATUS_PORT, SSMFLAG_FORWARD_STATUSES_TO_OVERLORD | 
		(bEmulateOverlord ? SSMFLAG_LISTEN_FOR_FORWARDED_STATUSES_AS_OVERLORD : 0));
	GenericHttpServing_Begin(MACHINESTATUS_HTTP_PORT, "MachineStatus", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	TimedCallback_Add(MainScreen_PatchingPeriodicUpdate, NULL, 1.0f);

	SimpleWindowManager_Init("MachineStatus", false);
	SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_MAIN,
		0, IDD_MACHINESTATUS_MAIN, true, mainScreenDlgProc_SWM, mainScreenDlgProc_SWMTick, NULL);
	SimpleWindowManager_Run(NULL, NULL);

	EXCEPTION_HANDLER_END

}


#include "MachineStatusPub_h_Ast.c"
