#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "Estring.h"
#include "earray.h"
#include "StringCache.h"
#include "process_Util.h"
#include "net.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "timing.h"
#include "Alerts.h"
#include "StructDefines.h"
#include "alerts_h_ast.h"
#include "CrypticPorts.h"
#include "timedCallback.h"
#include "GlobalComm.h"
#include "StructNet.h"
#include "sock.h"
#include "../../Core/NewControllerTracker/pub/NewControllerTracker_Pub.h"
#include "autogen/NewControllerTracker_Pub_h_ast.h"

char *pCTName = NULL;
AUTO_CMD_ESTRING(pCTName, CTName);

char *pShardName = NULL;
AUTO_CMD_ESTRING(pShardName, ShardName);

char *pVersionString = NULL;
AUTO_CMD_ESTRING(pVersionString, VersionString);

char *pPatchCommandLine = NULL;
AUTO_CMD_ESTRING(pPatchCommandLine, PatchCommandLine);




int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;
	CommConnectFSM *pFSM = NULL;
	int iRetryCount = 0;
	bool bSucceeded = false;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	fileAllPathsAbsolute(true);

	sockStart();


	if (!(pCTName && pVersionString && pPatchCommandLine && pShardName))
	{
		exit(-1);
	}
	else
	{
		char **sppControllerTrackerIPs = NULL;
		CommConnectFSM **ppConnectFSMs = NULL;

		if (strstri(pCTName, "qa"))
		{
			U32 iIP = ipFromString(pCTName);
			if (iIP)
			{
				eaPush(&sppControllerTrackerIPs, (char*)allocAddString(makeIpStr(iIP)));
			}
		}
		else
		{

			GetAllUniqueIPs(pCTName, &sppControllerTrackerIPs);
		}

		if (!eaSize(&sppControllerTrackerIPs))
		{
			exit(-1);
		}

		for (i = 0; i < eaSize(&sppControllerTrackerIPs); i++)
		{
			eaPush(&ppConnectFSMs, commConnectFSM(COMMFSMTYPE_TRY_ONCE, 10.0f, commDefault(), LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH,
				sppControllerTrackerIPs[i], CONTROLLERTRACKER_SHARD_INFO_PORT, NULL, NULL, NULL, 0, NULL, NULL));
		}

		while (eaSize(&ppConnectFSMs))
		{
			Sleep(1);
			utilitiesLibOncePerFrame(REAL_TIME);
			commMonitor(commDefault());

			for (i = eaSize(&ppConnectFSMs) - 1; i >= 0; i--)
			{
				NetLink *pLink;

				CommConnectFSMStatus eStatus = commConnectFSMUpdate(ppConnectFSMs[i], &pLink);

				switch (eStatus)
				{
				case COMMFSMSTATUS_FAILED:
				case COMMFSMSTATUS_DONE:
					commConnectFSMDestroy(&ppConnectFSMs[i]);
					eaRemove(&ppConnectFSMs, i);
					break;

				case COMMFSMSTATUS_SUCCEEDED:
					{
						ShardVersionInfoFromStandaloneUtil *pVersionInfo = StructCreate(parse_ShardVersionInfoFromStandaloneUtil);
						Packet *pPak = pktCreate(pLink, TO_NEWCONTROLLERTRACKER_HERE_IS_VERSION_FOR_OFFLINE_SHARD);
						pVersionInfo->pShardName = strdup(pShardName);
						pVersionInfo->pVersionString = strdup(pVersionString);
						pVersionInfo->pPatchCommandLine = strdup(pPatchCommandLine);
						ParserSendStructSafe(parse_ShardVersionInfoFromStandaloneUtil, pPak, pVersionInfo);
						pktSend(&pPak);

						StructDestroy(parse_ShardVersionInfoFromStandaloneUtil, pVersionInfo);

						commMonitor(commDefault());
						linkFlushAndClose(&pLink, "Done");

						commConnectFSMDestroy(&ppConnectFSMs[i]);
						eaRemove(&ppConnectFSMs, i);
						break;
					}
				}
			}
		}
	}

				
	commFlushAndCloseAllComms(10.0f);


	EXCEPTION_HANDLER_END

}


#include "NewControllerTracker_Pub_h_ast.c"
