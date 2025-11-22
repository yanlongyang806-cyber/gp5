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
#include "SentryServerComm.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "trivia.h"
#include "trivia_h_ast.h"
#include "Overlord_Clusters.h"
#include "..\..\Utilities\ShardLauncher\ShardLauncher_pub.h"

StashTable gOverlordShardsByName = NULL;

static void UpdateShard(OverlordShard *pShard)
{
	char controllerName[256];
	char shardLauncherName[256];
	SimpleMonitoringStatus *pController;
	SimpleMonitoringStatus *pShardLauncher;

	if (pShard->pClusterName)
	{
		sprintf(shardLauncherName, "%s_ShardLauncher", pShard->pClusterName);
		sprintf(controllerName, "%s_%s", pShard->pClusterName, pShard->pShardName);
	}
	else
	{
		sprintf(shardLauncherName, "%s_ShardLauncher", pShard->pShardName);
		sprintf(controllerName, "%s", pShard->pShardName);
	}

	pController = SimpleStatusMonitoring_FindConnectedStatusByName(controllerName);
	pShardLauncher = SimpleStatusMonitoring_FindConnectedStatusByName(shardLauncherName);
	
	if (!pController && !pShardLauncher)
	{
		pShard->eStatus = SHARDSTATUS_DISCONNECTED;
		return;
	}

	if (pController)
	{
		estrCopy2(&pShard->pPatchVersion, pController->status.pVersion);
		

		switch (pController->status.status.eState)
		{
		xcase STATUS_UNSPECIFIED:
			pShard->eStatus = SHARDSTATUS_UNKNOWN;

		xcase STATUS_CONTROLLER_STARTUP:
			pShard->eStatus = SHARDSTATUS_STARTINGUP_INITIAL;

		xcase STATUS_CONTROLLER_PATCHING: 
			pShard->eStatus = SHARDSTATUS_STARTINGUP_PATCHING;

		xcase STATUS_CONTROLLER_POST_PATCH_STARTUP: 
		case STATUS_CONTROLLER_SERVERS_STARTING: 
			pShard->eStatus = SHARDSTATUS_STARTINGUP_SERVERS_STARTING;

		xcase STATUS_CONTROLLER_LOCKED:
			pShard->eStatus = SHARDSTATUS_LOCKED;

		xcase STATUS_CONTROLLER_RUNNING:
			pShard->eStatus = SHARDSTATUS_RUNNING;
		}
	}
	else if (pShardLauncher)
	{
		char *pVersionFromShardLauncher = NULL;
		if (pShardLauncher->status.status.pNameValuePairs)
		{
			pVersionFromShardLauncher = GetValueFromNameValuePairs(&pShardLauncher->status.status.pNameValuePairs->ppPairs, "patchVersion");
			if (pVersionFromShardLauncher)
			{
				estrCopy2(&pShard->pPatchVersion, pVersionFromShardLauncher);
			}
		}

		switch(pShardLauncher->status.status.eState)
		{
		xcase STATUS_UNSPECIFIED:
			pShard->eStatus = SHARDSTATUS_UNKNOWN;

		xcase STATUS_SHARDLAUNCHER_STARTINGUP:
		case STATUS_SHARDLAUNCHER_LAUNCHING:
				pShard->eStatus = SHARDSTATUS_SHARDLAUNCHER_RUNNING;
		
		xcase STATUS_SHARDLAUNCHER_PATCHING:
			pShard->eStatus = SHARDSTATUS_SHARDLAUNCHER_PATCHING;
		}
	}


}

static void OverlordShards_Tick(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	FOR_EACH_IN_STASHTABLE(gOverlordShardsByName, OverlordShard, pShard)
	{
		UpdateShard(pShard);
	}
	FOR_EACH_END;
}

#define UNKNOWN "UNKNOWN"

void GetShardVersionThroughSentryServerCB(char *pMachineName, char *pFileName, void *pFileData, int iSize, char *pShardName)
{
	if (iSize)
	{
		OverlordShard *pShard;

		if (stashFindPointer(gOverlordShardsByName, pShardName, &pShard))
		{
			if (stricmp_safe(pShard->pPatchVersion, UNKNOWN) == 0)
			{
				TriviaList *pList = StructCreate(parse_TriviaList);
				const char *pVersion;

				ParserReadText((char*)pFileData, parse_file_TriviaList, pList, 0);

				pVersion = triviaListGetValue(pList, "PatchName");

				if (pVersion)
				{
					estrCopy2(&pShard->pPatchVersion, pVersion);
				}

				StructDestroy(parse_TriviaList, pList);
			}
		}
	}
}




static void GetShardVersionThroughSentryServer(OverlordShard *pShard)
{
	char fileName[CRYPTIC_MAX_PATH];
	sprintf(fileName, "%s\\%sServer\\.patch\\patch_trivia.txt", 
		pShard->pDirectory, pShard->pProductName);

	SentryServerComm_GetFileContents(pShard->pMachineName, fileName, GetShardVersionThroughSentryServerCB, (void*)allocAddString(pShard->pShardName));
}



void GetPrimedShardVersionThroughSentryServerCB(char *pMachineName, char *pFileName, void *pFileData, int iSize, char *pShardName)
{
	if (iSize)
	{
		OverlordShard *pShard;

		if (stashFindPointer(gOverlordShardsByName, pShardName, &pShard))
		{
			if (stricmp_safe(pShard->pPrimedPatchVersion, UNKNOWN) == 0)
			{
				TriviaList *pList = StructCreate(parse_TriviaList);
				const char *pVersion;

				ParserReadText((char*)pFileData, parse_file_TriviaList, pList, 0);

				pVersion = triviaListGetValue(pList, "PatchName");

				if (pVersion)
				{
					estrCopy2(&pShard->pPrimedPatchVersion, pVersion);
				}

				StructDestroy(parse_TriviaList, pList);
			}
		}
	}
}

static void GetPrimedShardVersionThroughSentryServer(OverlordShard *pShard)
{
	char fileName[CRYPTIC_MAX_PATH];
	sprintf(fileName, "%s\\%sServer\\prepatch\\.patch\\patch_trivia.txt", 
		pShard->pDirectory, pShard->pProductName);

	SentryServerComm_GetFileContents(pShard->pMachineName, fileName, GetPrimedShardVersionThroughSentryServerCB, (void*)allocAddString(pShard->pShardName));

}


void OverlordShards_Init(void)
{
	gOverlordShardsByName = stashTableCreateWithStringKeys(16, StashDefault);
	resRegisterDictionaryForStashTable("Shards", RESCATEGORY_OTHER, 0, gOverlordShardsByName, parse_OverlordShard);

	FOR_EACH_IN_EARRAY(gpOverlordConfig->ppShards, OverlordShard, pShard)
	{
		if (pShard->pClusterName)
		{
			OverlordCluster *pCluster;
			if (!(pCluster = OverlordCluster_FindByName(pShard->pClusterName)))
			{
				assertmsgf(0, "Shard %s thinks it belongs to cluster %s, which doesn't exist",
					pShard->pShardName, pShard->pClusterName);

				if (stricmp_safe(pCluster->pProductName, pShard->pProductName) != 0)
				{
					assertmsgf(0, "Shard %s thinks it belongs to cluster %s, product names don't match",
						pShard->pShardName, pShard->pClusterName);
				}
			}
		}


		if (stashFindPointer(gOverlordShardsByName, pShard->pShardName, NULL))
		{
			assertmsgf(0, "More than one shard named %s. This is illegal\n", pShard->pShardName);
		}

		stashAddPointer(gOverlordShardsByName, pShard->pShardName, pShard, true);

		if (!pShard->pPatchVersion)
		{
			estrCopy2(&pShard->pPatchVersion, UNKNOWN);
			GetShardVersionThroughSentryServer(pShard);	
		}

		if (!pShard->pPrimedPatchVersion)
		{
			estrCopy2(&pShard->pPrimedPatchVersion, UNKNOWN);
			GetPrimedShardVersionThroughSentryServer(pShard);	
		}
	}
	FOR_EACH_END;

	TimedCallback_Add(OverlordShards_Tick, NULL, 1.0f);
}

void AddShardFromShardLauncherConfig(OverlordCluster *pCluster, ShardLauncherRun *pRun, 
	ShardLauncherClusterShard *pShardConfig)
{
	const char *pShardName = allocAddString(pShardConfig->pShardName);
	OverlordShard *pShard;
	char *pDirName = NULL;

	if (stashFindPointer(gOverlordShardsByName, pShardName, NULL))
	{
		OverlordError(OVERLORD_ERROR__DUP_SHARD_NAME, pShardName);
		return;
	}

	pShard = StructCreate(parse_OverlordShard);
	pShard->pShardName = pShardName;
	pShard->pClusterName = strdup(pCluster->pClusterName);
	pShard->pMachineName = strdup(pShardConfig->pMachineName);
	pShard->pProductName = pCluster->pProductName;

	estrCopy2(&pDirName, pRun->pDirectory);
	estrReplaceOccurrences(&pDirName, "$SHARDNAME$", pShardName);
	pShard->pDirectory = strdup(pDirName);
	estrDestroy(&pDirName);

	stashAddPointer(gOverlordShardsByName, pShard->pShardName, pShard, true);

	estrCopy2(&pShard->pPatchVersion, UNKNOWN);
	GetShardVersionThroughSentryServer(pShard);	

	estrCopy2(&pShard->pPrimedPatchVersion, UNKNOWN);
	GetPrimedShardVersionThroughSentryServer(pShard);	
}
		