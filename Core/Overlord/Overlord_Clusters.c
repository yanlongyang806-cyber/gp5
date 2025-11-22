#include "Overlord_Shards.h"
#include "StashTable.h"
#include "earray.h"
#include "textparser.h"
#include "EString.h"
#include "ResourceInfo.h"
#include "TimedCallback.h"
#include "Overlord_h_ast.h"
#include "SimpleStatusMonitoring.h"
#include "nameValuePair.h"
#include "StringUtil.h"
#include "..\..\Utilities\ShardLauncher\ShardLauncher_pub.h"
#include "SentryServerComm.h"
#include "ShardLauncher_pub_h_ast.h"
#include "StringCache.h"
#include "..\..\libs\patchClientLib\pcl_client_wt.h"
#include "cmdparse.h"

static StashTable sClustersByName = NULL;

#define UNKNOWN "UNKNOWN"


OverlordCluster *OverlordCluster_FindByName(char *pName)
{
	OverlordCluster *pRetVal = NULL;
	if (stashFindPointer(sClustersByName, pName, &pRetVal))
	{
		return pRetVal;
	}
	return NULL;
}

static void ValidateCluster(OverlordCluster *pCluster)
{
	assertmsgf(pCluster->pClusterName && pCluster->pMachineName, "Cluster in config.txt badly configured... must include name and machine name");
}

void ClusterVersionGetFileCB(char *pMachineName, char *pFileName, void *pFileData, int iSize, char *pClusterName)
{
	OverlordCluster *pCluster;

	if (!iSize)
	{
		OverlordError(OVERLORD_ERROR__COULDNT_LOAD_CLUSTER_SHARDLAUNCHER_FILE, pMachineName);
		return;
	}

	if (stashFindPointer(sClustersByName, pClusterName, &pCluster))
	{
		if (stricmp_safe(pCluster->pPatchVersion, UNKNOWN) == 0)
		{
			ShardLauncherRun *pRun = StructCreate(parse_ShardLauncherRun);
			
			if (!ParserReadText((char*)pFileData, parse_ShardLauncherRun, pRun, 0))
			{
				OverlordError(OVERLORD_ERROR__CORRUPT_SHARDLAUNCHER_FILE, "%s, %s", pMachineName, pClusterName);
				return;
			}

			if (pRun->pPatchVersion)
			{
				estrCopy2(&pCluster->pPatchVersion, pRun->pPatchVersion);
			}

			FOR_EACH_IN_EARRAY(pRun->ppClusterShards, ShardLauncherClusterShard, pShardFromShardlauncher)
			{
				AddShardFromShardLauncherConfig(pCluster, pRun, pShardFromShardlauncher);
			}
			FOR_EACH_END;

			pCluster->pLoggingMachine = strdup(pRun->pLogServerAndParserMachineName);

			StructDestroy(parse_ShardLauncherRun, pRun);
		}
	}

}

static void TryToGetClusterVersionThroughSentryServer(OverlordCluster *pCluster)
{
	char fileName[CRYPTIC_MAX_PATH];
	sprintf(fileName, "c:\\shardlauncher\\recentruns\\%s.txt", pCluster->pClusterName);
	SentryServerComm_GetFileContents(pCluster->pMachineName, fileName, ClusterVersionGetFileCB, (void*)allocAddString(pCluster->pClusterName));
}

void OverlordClusters_Init(void)
{
	sClustersByName = stashTableCreateWithStringKeys(16, StashDefault);
	resRegisterDictionaryForStashTable("Clusters", RESCATEGORY_OTHER, 0, sClustersByName, parse_OverlordCluster);

	FOR_EACH_IN_EARRAY(gpOverlordConfig->ppClusters, OverlordCluster, pCluster)
	{
		ValidateCluster(pCluster);

		if (stashFindPointer(sClustersByName, pCluster->pClusterName, NULL))
		{
			assertmsgf(0, "More than one cluster named %s. This is illegal\n", pCluster->pClusterName);
		}

		stashAddPointer(sClustersByName, pCluster->pClusterName, pCluster, true);

		estrCopy2(&pCluster->pPatchVersion, UNKNOWN);

		TryToGetClusterVersionThroughSentryServer(pCluster);

	}
	FOR_EACH_END;

}

static void UpdateClusterForServerMon(OverlordCluster *pCluster)
{
	char tempName[256];
	SimpleMonitoringStatus *pStatus;
	int iTotalNumShards = 0;

	sprintf(tempName, "%s_ShardLauncher", pCluster->pClusterName);
	pStatus = SimpleStatusMonitoring_FindConnectedStatusByName(tempName);
	if (pStatus)
	{

		char *pVersionFromShardLauncher = NULL;
		if (pStatus->status.status.pNameValuePairs)
		{
			pVersionFromShardLauncher = GetValueFromNameValuePairs(&pStatus->status.status.pNameValuePairs->ppPairs, "patchVersion");
			if (pVersionFromShardLauncher)
			{
				estrCopy2(&pCluster->pPatchVersion, pVersionFromShardLauncher);
			}
		}	

		pCluster->eShardLauncherState = pStatus->status.status.eState;
	}
	else
	{
		pCluster->eShardLauncherState = STATUS_UNSPECIFIED;
	}

	sprintf(tempName, "%s_ClusterController", pCluster->pClusterName);
	pCluster->bClusterControllerConnected = !!SimpleStatusMonitoring_FindConnectedStatusByName(tempName);

	sprintf(tempName, "%s_LogServer", pCluster->pClusterName);
	pCluster->bLogServerConnected = !!SimpleStatusMonitoring_FindConnectedStatusByName(tempName);

	sprintf(tempName, "%s_LogParser", pCluster->pClusterName);
	pCluster->bLogParserConnected = !!SimpleStatusMonitoring_FindConnectedStatusByName(tempName);

	pCluster->iNumPlayers = 0;
	pCluster->iNumShardsConnected = 0;

	FOR_EACH_IN_STASHTABLE(gOverlordShardsByName, OverlordShard, pShard)
	{
		if (stricmp_safe(pShard->pClusterName, pCluster->pClusterName) == 0)
		{
			iTotalNumShards++;
			if (SHARDSTATUS_CONNECTED(pShard->eStatus))
			{
				pCluster->iNumShardsConnected++;
				pCluster->iNumPlayers += pShard->iNumPlayers;
			}
		}
	}
	FOR_EACH_END;

	estrPrintf(&pCluster->pNumShardsConnected, "%d/%d", pCluster->iNumShardsConnected, iTotalNumShards);
}

AUTO_FIXUPFUNC;
TextParserResult OverlordCluster_SingleMachineFixupFunc(OverlordCluster *pCluster, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
			UpdateClusterForServerMon(pCluster);
			break;
	}

	return 1;
}


/*to make shardlauncher auto-run, create c:\shardlauncher\autorun.txt, that looks like this:
{
	RunName "nw tip 1 machine"
}
*/

#define LOCAL_AUTO_RUN_FILENAME "c:\\temp\\ShardLauncherAutoRun.txt"

AUTO_COMMAND;
char *LaunchCluster(CmdContext *pContext, char *pClusterName, char *pPatchName)
{
	char *pCommandLine = NULL;
	OverlordCluster *pCluster = NULL;
	FILE *pAutoRunFile = NULL;
	ShardLauncherAutoRun *pAutoRun = NULL;

	if (!stashFindPointer(sClustersByName, pClusterName, &pCluster))
	{
		return "Unknown cluster";
	}

	if (pCluster->bClusterControllerConnected
		|| pCluster->bLogServerConnected
		|| pCluster->bLogParserConnected
		|| pCluster->iNumShardsConnected)
	{
		return "Shard still running";
	}

	SentryServerComm_KillProcess_1Machine(pCluster->pMachineName, "shardLauncher", NULL);

	pAutoRun = StructCreate(parse_ShardLauncherAutoRun);
	pAutoRun->pRunName = strdup(pCluster->pClusterName);
	pAutoRun->pPatchVersion = strdup(pPatchName);
	pAutoRun->pPatchVersionComment = strdup(GetCommentFromServerMonitorablePatchVersion(pCluster->pProductName, pPatchName));

	mkdirtree_const(LOCAL_AUTO_RUN_FILENAME);
	
	if (!ParserWriteTextFile(LOCAL_AUTO_RUN_FILENAME, parse_ShardLauncherAutoRun, pAutoRun, 0, 0))
	{
		return "ERROR writing " LOCAL_AUTO_RUN_FILENAME;
	}

	SentryServerComm_SendFile_1Machine(pCluster->pMachineName, LOCAL_AUTO_RUN_FILENAME, SHARDLAUNCHER_AUTORUN_FILE_NAME);

	estrPrintf(&pCommandLine, "WORKINGDIR(c:\\shardlauncher\\shardlauncher) c:\\shardlauncher\\shardlauncher\\shardLauncher.exe -TryToLoadAutoRun -SetOverlord %s",
		getHostName());

	SentryServerComm_RunCommand_1Machine(pCluster->pMachineName, pCommandLine);

	estrDestroy(&pCommandLine);

	return "Started";
}

AUTO_COMMAND;
char *RestartClusterController(char *pClusterName)
{
	OverlordCluster *pCluster = NULL;
	char temp[1024];

	if (!stashFindPointer(sClustersByName, pClusterName, &pCluster))
	{
		return "Unknown cluster";
	}

	sprintf(temp, "c:\\%s\\Launch_%s_ClusterController.bat",
		pClusterName, pClusterName);

	SentryServerComm_RunCommand_1Machine(pCluster->pMachineName, temp);

	return "restarted";
}



AUTO_COMMAND;
char *EmergencyShutdownCluster(char *pClusterName, char *pConfirm)
{
	OverlordCluster *pCluster = NULL;


	if (!stashFindPointer(sClustersByName, pClusterName, &pCluster))
	{
		return "Unknown cluster";
	}

	if (stricmp(pClusterName, pConfirm) != 0)
	{
		return "You didn't confirm";
	}

	SentryServerComm_KillProcess_1Machine(pCluster->pMachineName, "clusterController, clusterControllerX64, shardLauncher, patchClient, patchClientX64", NULL);

	if (pCluster->pLoggingMachine)
	{
		SentryServerComm_KillProcess_1Machine(pCluster->pLoggingMachine, "LogServer, patchClient, LogServerX64, patchClientX64", NULL);
	}




	FOR_EACH_IN_STASHTABLE(gOverlordShardsByName, OverlordShard, pShard)
	{
		if (stricmp_safe(pShard->pClusterName, pClusterName) == 0)
		{
			SentryServerComm_KillProcess_1Machine(pShard->pMachineName, "controller, patchClient, controllerX64, patchClientX64", NULL);
		}
	}
	FOR_EACH_END;

	return "Killed";
}



#include "ShardLauncher_pub_h_ast.c"

