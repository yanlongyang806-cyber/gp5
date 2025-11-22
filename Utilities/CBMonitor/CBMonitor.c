#include "CBMonitor.h"
#include "superassert.h"
#include "sysutil.h"
#include "MemoryMonitor.h"
#include "cmdparse.h"
#include "foldercache.h"
#include "file.h"
#include "globaltypes.h"
#include "UtilitiesLIb.h"
#include "net/net.h"
#include "serverLib.h"
#include "NameValuePair.h"
#include "CBMonitor_pub.h"
#include "CBMonitor_pub_h_ast.h"
#include "StructNet.h"
#include "..\..\utilities\ContinuousBuilder\ContinuousBuilder_Pub.h"
#include "ContinuousBuilder_Pub_h_ast.h"
#include "CBMonitor.h"
#include "CBMonitor_h_ast.h"
#include "StashTable.h"
#include "resourceInfo.h"
#include "StringUtil.h"
#include "GenericHTTPServing.h"
#include "fileUtil2.h"
#include "Alerts.h"
#include "url.h"
#include "qsortG.h"
#include "logging.h"



void SetDowntimeComment(char *pHostName, char *pComment);


static void MaybeCreateLogsLink(BuilderInfo *pBuilder, SingleBuildInfo *pBuild);

int iSecsToKeepOldBuilds = 60 * 60 * 24 * 14;
AUTO_CMD_INT(iSecsToKeepOldBuilds, SecsToKeepOldBuilds);

int iSecsToKeepOldAbortedBuilds = 60 * 60 * 2;
AUTO_CMD_INT(iSecsToKeepOldAbortedBuilds, SecsToKeepOldAbortedBuilds);

int iSecsToKeepOldUnknownBuilds = 60 * 60 * 48;
AUTO_CMD_INT(iSecsToKeepOldUnknownBuilds, SecsToKeepOldUnknownBuilds);


#define MAX_STATE_LENGTH 250

static NetListen *spCBMonitorListen = NULL;

typedef struct CBMonitorUserData 
{
	char CBHostName[256];
} CBMonitorUserData;

static StashTable sBuildersByName = NULL;
 
#define CBMONITOR_DATA_DIR "c:\\CBMonitor\\Builders"

void SaveBuilderInfoToFile(BuilderInfo *pBuilder)
{
	char filename[CRYPTIC_MAX_PATH];
	sprintf(filename, "%s\\%s.txt", CBMONITOR_DATA_DIR, pBuilder->pMachineName);
	mkdirtree_const(filename);
	ParserWriteTextFile(filename, parse_BuilderInfo, pBuilder, 0, 0);
}

static void LazyCreateStashTable(void)
{
	if (!sBuildersByName)
	{
		sBuildersByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("Builders", RESCATEGORY_SYSTEM, 0, sBuildersByName, parse_BuilderInfo);
	}
}

void LoadBuildersFromFiles(void)
{
	char **ppFileList;
	int i;

	ppFileList = fileScanDirFolders(CBMONITOR_DATA_DIR, FSF_FILES);
	
	for (i = 0; i < eaSize(&ppFileList); i++)
	{
		BuilderInfo *pInfo = StructCreate(parse_BuilderInfo);
		if (!ParserReadTextFile(ppFileList[i], parse_BuilderInfo, pInfo, 0))
		{
			CRITICAL_NETOPS_ALERT("CORRUPT_CBMONITOR_FILE", "Couldn't load from %s... corrupt file?", ppFileList[i]);
			StructDestroy(parse_BuilderInfo, pInfo);
		}
		else
		{
			LazyCreateStashTable();

			stashAddPointer(sBuildersByName, pInfo->pMachineName, pInfo, true);
		}
	}

	fileScanDirFreeNames(ppFileList);
}

BuilderInfo *FindBuilderByName(char *pName)
{
	BuilderInfo *pRetVal;

	if (!pName || !pName[0])
	{
		return NULL;
	}


	LazyCreateStashTable();
	
	if (!stashFindPointer(sBuildersByName, pName, &pRetVal))
	{
		pRetVal = StructCreate(parse_BuilderInfo);
		pRetVal->pMachineName = strdup(pName);
		stashAddPointer(sBuildersByName, pRetVal->pMachineName, pRetVal, false);
	}

	return pRetVal;
}

static void PurgePreviousBuilds(BuilderInfo *pBuilder)
{
	int i;
	U32 iCutoffTime = timeSecondsSince2000() - iSecsToKeepOldBuilds;
	U32 iAbortedCutoffTime = timeSecondsSince2000() - iSecsToKeepOldAbortedBuilds;
	U32 iUnknownCutoffTime = timeSecondsSince2000() - iSecsToKeepOldUnknownBuilds;

	for (i = eaSize(&pBuilder->ppPreviousBuilds) - 1; i >= 0; i--)
	{
		SingleBuildInfo *pBuild = pBuilder->ppPreviousBuilds[i];

		if (pBuild->eResult == CBRESULT_ABORTED && pBuild->iEndTime < iAbortedCutoffTime 
			|| pBuild->eResult == CBRESULT_UNKNOWN && pBuild->iEndTime < iUnknownCutoffTime
			|| pBuild->iEndTime < iCutoffTime)
		{
			eaRemove(&pBuilder->ppPreviousBuilds, i);
			StructDestroy(parse_SingleBuildInfo, pBuild);
		}
	}
}

static void BuilderLog(BuilderInfo *pBuilder, FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;
	estrStackCreate(&pFullString);
	estrPrintf(&pFullString, "Builder %s: ", pBuilder ? pBuilder->pMachineName : "NONEXISTENT");
	estrGetVarArgs(&pFullString, pFmt);
	log_printf(LOG_CBMONITOR, "%s", pFullString);
	estrDestroy(&pFullString);
}

static void BuilderLogName(char *pName, FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;
	estrStackCreate(&pFullString);
	estrPrintf(&pFullString, "Builder %s: ", pName);
	estrGetVarArgs(&pFullString, pFmt);
	log_printf(LOG_CBMONITOR, "%s", pFullString);
	estrDestroy(&pFullString);
}


static char *GetPresumedBuildName(SingleBuildInfo *pBuild)
{
	NameValuePair *pPair;

	if (!pBuild)
	{
		return "NONEXISTENT";
	}

	pPair = eaIndexedGetUsingString(&pBuild->ppVariables, "$OVERALL_RUN_START_TIME$");
	if (pPair && pPair->pValue && pPair->pValue[0])
	{
		return pPair->pValue;
	}

	return "UNKNOWN";
}

static void FinishBuild(BuilderInfo *pBuilder, enumCBResult eLastBuildResult, FORMAT_STR const char *pCommentFmt, ...)
{
	static char *spComment = NULL;
	estrClear(&spComment);
	estrGetVarArgs(&spComment, pCommentFmt);

	BuilderLog(pBuilder, "Finishing build %s with result %s because %s", GetPresumedBuildName(pBuilder->pCurBuild),
		StaticDefineIntRevLookup(enumCBResultEnum, eLastBuildResult), spComment);

	if (pBuilder->pCurBuild)
	{

		if (pBuilder->pCurBuild->eResult == CBRESULT_NONE || pBuilder->pCurBuild->eResult == CBRESULT_UNKNOWN)
		{
			pBuilder->pCurBuild->eResult = eLastBuildResult;

			if (eLastBuildResult == CBRESULT_ABORTED)
			{
				MaybeCreateLogsLink(pBuilder, pBuilder->pCurBuild);
			}

			if (eLastBuildResult == CBRESULT_SUCCEEDED || eLastBuildResult == CBRESULT_SUCCEEDED_W_ERRS || eLastBuildResult == CBRESULT_FAILED)
			{
				char *pPatchVersion = GetValueFromNameValuePairs(&pBuilder->pCurBuild->ppVariables, "$PATCHVERSION$");
				char *pGimmeTime = GetValueFromNameValuePairs(&pBuilder->pCurBuild->ppVariables, "$GIMMETIME$");
				char *pSVNRevNum = GetValueFromNameValuePairs(&pBuilder->pCurBuild->ppVariables, "$SVNREVNUM$");
				char *pGimmeRevNum = GetValueFromNameValuePairs(&pBuilder->pCurBuild->ppVariables, "$GIMMEREVNUM$");

				estrPrintf(&pBuilder->pMostRecentBuildSummary, "%s: %s. ", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()),
					StaticDefineIntRevLookup(enumCBResultEnum, eLastBuildResult));

				if (pSVNRevNum && pSVNRevNum[0])
				{
					estrConcatf(&pBuilder->pMostRecentBuildSummary, "SVN Rev: %s ", pSVNRevNum);
				}

				if (pGimmeTime && pGimmeTime[0])
				{
					estrConcatf(&pBuilder->pMostRecentBuildSummary, "Gimme Time: %s ", pGimmeTime);
				}

				if (pGimmeRevNum && pGimmeRevNum[0])
				{
					estrConcatf(&pBuilder->pMostRecentBuildSummary, "Gimme Rev: %s ", pGimmeRevNum);
				}

				if (pPatchVersion && pPatchVersion[0])
				{
					estrConcatf(&pBuilder->pMostRecentBuildSummary, "Patch Version: %s ", pPatchVersion);
				}

				if (eLastBuildResult == CBRESULT_SUCCEEDED || eLastBuildResult == CBRESULT_SUCCEEDED_W_ERRS)
				{
					estrCopy(&pBuilder->pMostRecentSuccessfulBuildSummary, &pBuilder->pMostRecentBuildSummary);
				}
				
			}
		
		}

		pBuilder->pCurBuild->iEndTime = timeSecondsSince2000();
	
		eaPush(&pBuilder->ppPreviousBuilds, pBuilder->pCurBuild);
		pBuilder->pCurBuild = NULL;

		PurgePreviousBuilds(pBuilder);

		SaveBuilderInfoToFile(pBuilder);
	}
}

static void StartNewBuild(BuilderInfo *pBuilder, enumCBResult eLastBuildResult, FORMAT_STR const char *pCommentFmt, ...)
{
	static char *spComment = NULL;
	estrClear(&spComment);
	estrGetVarArgs(&spComment, pCommentFmt);

	BuilderLog(pBuilder, "Starting new build because %s. Last result: %s", spComment, StaticDefineIntRevLookup(enumCBResultEnum, eLastBuildResult));

	if (pBuilder->pCurBuild)
	{
		FinishBuild(pBuilder, eLastBuildResult, "StartNewBuild called, an old build exists");
	}

	pBuilder->pCurBuild = StructCreate(parse_SingleBuildInfo);
	pBuilder->pCurBuild->iStartTime = timeSecondsSince2000();
	pBuilder->pCurBuild->eResult = CBRESULT_NONE;
}

static void MakeFileSystemLink(char **ppOutString, char *pLinkName, BuilderInfo *pBuilder, FORMAT_STR const char *pFmt, ...)
{
	char *pTemp = NULL;
	char *pTemp2 = NULL;

	estrGetVarArgs(&pTemp, pFmt);
	estrCopy(&pTemp2, &pTemp);

	estrReplaceOccurrences(&pTemp, "c:", "");
	backSlashes(pTemp);
	estrInsertf(&pTemp, 0, "cryptic://explore/\\\\%s", pBuilder->pMachineName);

	estrPrintf(ppOutString, "<a href = \"%s\">%s</a>", pTemp, pLinkName);

	estrConcatf(ppOutString, " (%s)", pTemp2);

	estrDestroy(&pTemp);
	estrDestroy(&pTemp2);
}

static void MaybeCreateLogsLink(BuilderInfo *pBuilder, SingleBuildInfo *pBuild)
{
	char *pOverall = GetValueFromNameValuePairs(&pBuild->ppVariables, "$OVERALL_RUN_START_TIME$");
	char *pType = GetValueFromNameValuePairs(&pBuild->ppVariables, "$CBTYPE$");
	char *pProduct = GetValueFromNameValuePairs(&pBuild->ppVariables, "$PRODUCTNAME$");

	if (pOverall && pOverall[0] && pType && pType[0] && pProduct && pProduct[0])
	{
		MakeFileSystemLink(&pBuild->pLogs, "Logs", pBuilder, "c:\\ContinuousBuilder\\%s\\%s\\logs\\%s%s",
			pProduct, pType, pOverall, pBuild->eResult == CBRESULT_ABORTED ? "_ABORTED" : "");
	}
}

void SetBuilderVariable(BuilderInfo *pBuilder, char *pVarName, char *pVarValue)
{
	NameValuePair *pPreviousValue;


	if (!pBuilder->pCurBuild)
	{
		StartNewBuild(pBuilder, CBRESULT_UNKNOWN, "Want to set variable %s to %s, but there is no build, creating a new one",
			pVarName, pVarValue);
	}

	pPreviousValue = eaIndexedGetUsingString(&pBuilder->pCurBuild->ppVariables, pVarName);
	
	if (stricmp(pVarName, "$OVERALL_RUN_START_TIME$") == 0)
	{
		if (pPreviousValue && stricmp_safe(pPreviousValue->pValue, pVarValue) != 0)
		{
			StartNewBuild(pBuilder, CBRESULT_UNKNOWN, "Setting $OVERALL_RUN_START_TIME$ to %s, but it previously had value %s, this means we need a new build",
				pVarValue, pPreviousValue->pValue);
			pPreviousValue = NULL;
		}
	}

	if (pPreviousValue && stricmp_safe(pPreviousValue->pValue, pVarValue) == 0)
	{
		return;
	}

	if (pPreviousValue)
	{
		SAFE_FREE(pPreviousValue->pValue);
		pPreviousValue->pValue = strdup(pVarValue);
	}
	else
	{
		NameValuePair *pNewValue = StructCreate(parse_NameValuePair);
		pNewValue->pName = strdup(pVarName);
		pNewValue->pValue = pVarValue ? strdup(pVarValue) : NULL;
		eaPush(&pBuilder->pCurBuild->ppVariables, pNewValue);
	}

	if (!pBuilder->pCurBuild->pLogs)
	{
		MaybeCreateLogsLink(pBuilder, pBuilder->pCurBuild);
	}

}

void SetBuilderIsConnected(BuilderInfo *pBuilder)
{
	pBuilder->bConnected = true;
	pBuilder->iLastContactTime = timeSecondsSince2000();
}

void HandleVariableSet(BuilderInfo *pBuilder, Packet *pPkt)
{
	char *pName;
	char *pValue;

	if (!pBuilder)
	{
		return;
	}

	SetBuilderIsConnected(pBuilder);
	pName = pktGetStringTemp(pPkt);
	pValue = pktGetStringTemp(pPkt);

	SetBuilderVariable(pBuilder, pName, pValue);
}

//walks through all builds, newest to oldest, until it finds one with this variable set
char *GetMostRecentSetVariable(BuilderInfo *pBuilder, char *pVarName)
{
	NameValuePair *pPair;
	
	if (pBuilder->pCurBuild)
	{
		pPair = eaIndexedGetUsingString(&pBuilder->pCurBuild->ppVariables, pVarName);
		if (pPair)
		{
			return pPair->pValue;
		}
	}

	FOR_EACH_IN_EARRAY(pBuilder->ppPreviousBuilds, SingleBuildInfo, pBuild)
	{
		pPair = eaIndexedGetUsingString(&pBuild->ppVariables, pVarName);
		if (pPair)
		{
			return pPair->pValue;
		}	
	}
	FOR_EACH_END;

	return "";
}

//checks only the most recent build (usually current, sometimes last one in ppPreviousBuilds
char *GetCurrentSetVariable(BuilderInfo *pBuilder, char *pVarName)
{
	NameValuePair *pPair;
	if (pBuilder->pCurBuild)
	{
		pPair = eaIndexedGetUsingString(&pBuilder->pCurBuild->ppVariables, pVarName);
		if (pPair)
		{
			return pPair->pValue;
		}
	}

	if (eaSize(&pBuilder->ppPreviousBuilds))
	{
		pPair = eaIndexedGetUsingString(&pBuilder->ppPreviousBuilds[eaSize(&pBuilder->ppPreviousBuilds)-1]->ppVariables, pVarName);
		if (pPair)
		{
			return pPair->pValue;
		}
	}

	return "";
}

char *GetCurrentState(BuilderInfo *pBuilder)
{
	if (pBuilder->pCurBuild)
	{
		return pBuilder->pCurBuild->pCurState;
	}

	if (eaSize(&pBuilder->ppPreviousBuilds))
	{
		return pBuilder->ppPreviousBuilds[eaSize(&pBuilder->ppPreviousBuilds)-1]->pCurState;
	}

	return "";
}

void ApplyHeartbeatToCurrentBuild(BuilderInfo *pBuilder, CBMonitorHeartbeatStruct *pHeartBeat)
{

	int i;
	NameValuePair *pPair;


	//always want to process OVERALL_RUN_START_TIME first so that variables all end up in the right build
	pPair = eaIndexedGetUsingString(&pHeartBeat->ppVariables, "$OVERALL_RUN_START_TIME$");

	if (pPair)
	{
		SetBuilderVariable(pBuilder, pPair->pName, pPair->pValue);
	}

	//now just set all the variables, don't worry about skipping OVERALL_RUN_START_TIME, it's harmless to set it again
	for (i = 0;i < eaSize(&pHeartBeat->ppVariables); i++)
	{
		SetBuilderVariable(pBuilder, pHeartBeat->ppVariables[i]->pName, pHeartBeat->ppVariables[i]->pValue);
	}

	//very unlikely case in which we get a heartbeat with no variables in it
	if (!pBuilder->pCurBuild)
	{
		StartNewBuild(pBuilder, CBRESULT_UNKNOWN, "Got a heartbeat with no variables and have no CurBuild");
	}

	if (pHeartBeat->pPresumedGimmeProductAndBranch)
	{
		estrCopy2(&pBuilder->pCurBuild->pPresumedGimmeProductAndBranch, pHeartBeat->pPresumedGimmeProductAndBranch);
	}

	if (pHeartBeat->pPresumedSVNBranch)
	{
		estrCopy2(&pBuilder->pCurBuild->pPresumedSVNBranch, pHeartBeat->pPresumedSVNBranch);
	}

	if (pHeartBeat->pCurState)
	{
		estrCopy2(&pBuilder->pCurBuild->pCurState, pHeartBeat->pCurState);
	}

	//make sure it never decreases, in case of weird corner cases as the build ends, or what have you
	pBuilder->pCurBuild->iNumErrors = MAX(pHeartBeat->iNumErrors, pBuilder->pCurBuild->iNumErrors);

}

void HandleHeartbeat(BuilderInfo *pBuilder, Packet *pPak)
{
	CBMonitorHeartbeatStruct *pHeartBeat = StructCreate(parse_CBMonitorHeartbeatStruct);

	ParserRecvStructSafe(parse_CBMonitorHeartbeatStruct, pPak, pHeartBeat);

	if (!pBuilder)
	{
		StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);
		return;
	}

	SetBuilderIsConnected(pBuilder);
	ApplyHeartbeatToCurrentBuild(pBuilder, pHeartBeat);

	StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);

}

void HandleState(BuilderInfo *pBuilder, Packet *pPak)
{
	if (!pBuilder)
	{
		return;
	}

	SetBuilderIsConnected(pBuilder);
	if (!pBuilder->pCurBuild)
	{
		StartNewBuild(pBuilder, CBRESULT_UNKNOWN, "Got HandleState when there was no build, creating one");
	}

	estrCopy2(&pBuilder->pCurBuild->pCurState, pktGetStringTemp(pPak));

}

void HandleRunEnded(BuilderInfo *pBuilder, Packet *pPak)
{
	enumCBResult eResult = pktGetBits(pPak, 32);
	CBMonitorHeartbeatStruct *pHeartBeat = StructCreate(parse_CBMonitorHeartbeatStruct);
	ParserRecvStructSafe(parse_CBMonitorHeartbeatStruct, pPak, pHeartBeat);

	if (!pBuilder)
	{
		StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);
		return;
	}

	SetBuilderIsConnected(pBuilder);
	BuilderLog(pBuilder, "Got run ended, going to first apply heartbeat to build %s before applying result %s",
		GetPresumedBuildName(pBuilder->pCurBuild), StaticDefineIntRevLookup(enumCBResultEnum, eResult));
	ApplyHeartbeatToCurrentBuild(pBuilder, pHeartBeat);
	FinishBuild(pBuilder, eResult, "RunEnded called, heartbeat already applied");
	StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);
	SaveBuilderInfoToFile(pBuilder);
}

void HandleBuilderComment(BuilderInfo *pBuilder, Packet *pPak)
{
	if (!pBuilder)
	{
		return;
	}

	estrCopy2(&pBuilder->pBuilderComment, pktGetStringTemp(pPak));
}

void HandleBuilderPlexedLogDirs(BuilderInfo *pBuilder, Packet *pPak)
{
	char *pDirName;

	if (!pBuilder)
	{
		return;
	}

	while (1)
	{
		pDirName = pktGetStringTemp(pPak);
		if (!pDirName[0])
		{
			break;
		}

		//go through every build we already know about on this builder... if its run start time matches 
		//the run start time encoded into any builderplexer dir, then set its builderPlexerLogDir field
		FOR_EACH_IN_EARRAY(pBuilder->ppPreviousBuilds, SingleBuildInfo, pBuildInfo)
		{
			if (!pBuildInfo->pBuilderPlexedLogs)
			{
				char *pOverall = GetValueFromNameValuePairs(&pBuildInfo->ppVariables, "$OVERALL_RUN_START_TIME$");
				if (pOverall && pOverall[0])
				{
					char aborted[128];
					sprintf(aborted, "%s_ABORTED", pOverall);
					if (strEndsWith(pDirName, aborted))
					{
						MakeFileSystemLink(&pBuildInfo->pBuilderPlexedLogs, "BuilderPlexed Logs", pBuilder, "%s_ABORTED", pDirName);
					}
					else if (strEndsWith(pDirName, pOverall))
					{
						MakeFileSystemLink(&pBuildInfo->pBuilderPlexedLogs, "BuilderPlexed Logs", pBuilder, "%s", pDirName);
					}
				}
			}
		}
		FOR_EACH_END;
	}
}


void CBMonitorHandleMsg(Packet *pkt,int cmd,NetLink* link,CBMonitorUserData *user_data)
{
	switch (cmd)
	{
	xcase FROM_CB_TO_CBMONITOR_CONNECT:
		sprintf(user_data->CBHostName, "%s", pktGetStringTemp(pkt));
		printf("Got connection from %s\n", user_data->CBHostName);
		BuilderLogName(user_data->CBHostName, "Connected");
	
		//whenever we get an initial connection from a CB, assume downtime ended
		SetDowntimeComment(user_data->CBHostName, "");



	xcase FROM_CB_TO_CBMONITOR_VARIABLESET:
		HandleVariableSet(FindBuilderByName(user_data->CBHostName), pkt);

	xcase FROM_CB_TO_CBMONITOR_HEARTBEAT:
		HandleHeartbeat(FindBuilderByName(user_data->CBHostName), pkt);

	xcase FROM_CB_TO_CBMONITOR_RUNBEGAN:
		HandleHeartbeat(FindBuilderByName(user_data->CBHostName), pkt);

	xcase FROM_CB_TO_CBMONITOR_STATESET:
		HandleState(FindBuilderByName(user_data->CBHostName), pkt);

	xcase FROM_CB_TO_CBMONITOR_RUNENDED:
		HandleRunEnded(FindBuilderByName(user_data->CBHostName), pkt);

	xcase FROM_CB_TO_CBMONITOR_BUILDERCOMMENT:
		HandleBuilderComment(FindBuilderByName(user_data->CBHostName), pkt);

	xcase FROM_CB_TO_CBMONITOR_BUILDERPLEXLOGDIRS:
		HandleBuilderPlexedLogDirs(FindBuilderByName(user_data->CBHostName), pkt);


	}

}

void CBMonitorDisconnect(NetLink* link,CBMonitorUserData *user_data)
{
	BuilderInfo *pBuilder = FindBuilderByName(user_data->CBHostName);
	if (pBuilder)
	{
		char *pDisconnectReason = NULL;
		estrStackCreate(&pDisconnectReason);
		pBuilder->bConnected = false;
		linkGetDisconnectReason(link, &pDisconnectReason);
		BuilderLog(pBuilder, "Disconnected because: %s", pDisconnectReason);
		estrDestroy(&pDisconnectReason);
	}
	printf("%s disconnected\n", user_data->CBHostName);
}

void CBMonitorConnect(NetLink* link,CBMonitorUserData *user_data)
{
	linkSetTimeout(link, 300);
}

char *GetCategoryNameForBuilder(BuilderInfo *pBuilder)
{
	SingleBuildInfo *pMostRecentBuild = pBuilder->pCurBuild;
	int i;

	if (pMostRecentBuild && estrLength(&pMostRecentBuild->pPresumedGimmeProductAndBranch))
	{
		return pMostRecentBuild->pPresumedGimmeProductAndBranch;
	}

	for (i = eaSize(&pBuilder->ppPreviousBuilds) - 1; i >= 0; i--)
	{
		if (estrLength(&pBuilder->ppPreviousBuilds[i]->pPresumedGimmeProductAndBranch))
		{
			return pBuilder->ppPreviousBuilds[i]->pPresumedGimmeProductAndBranch;
		}
	}

	return "Uncategorized";
}


static char *spCanonicalBuildOrder[] =
{
	"Continuous Dev Build",
	"Looping Prod Builder",
	"Baseline Production Build",
	"Incremental Production Build",
};

int SortBuildersByType(const BuilderOverview **pInfo1, const BuilderOverview **pInfo2)
{
	char *pType1 = (*pInfo1)->pType;
	char *pType2 = (*pInfo2)->pType;
	int iIndex1 = ARRAY_SIZE(spCanonicalBuildOrder), iIndex2 =  ARRAY_SIZE(spCanonicalBuildOrder), i;

	for (i = 0; i < ARRAY_SIZE(spCanonicalBuildOrder); i++)
	{
		if (stricmp(pType1, spCanonicalBuildOrder[i]) == 0)
		{
			iIndex1 = i;
		}
		if (stricmp(pType2, spCanonicalBuildOrder[i]) == 0)
		{
			iIndex2 = i;
		}
	}

	if (iIndex1 < iIndex2)
	{
		return -1;
	}

	if (iIndex1 > iIndex2)
	{
		return 1;
	}

	return stricmp(pType1, pType2);
}

static void FillInLinks(CBMonitorOverview *pOverview)
{
	char **ppCategoryNames = NULL;
	char **ppProductNames = NULL;
	char **ppTypeNames = NULL;
	int i;

	FOR_EACH_IN_STASHTABLE(sBuildersByName, BuilderInfo, pBuilder)
	{
		char *pCategoryName = GetCategoryNameForBuilder(pBuilder);
		char *pProductName = GetMostRecentSetVariable(pBuilder, "$PRODUCTNAME$");
		char *pTypeName = GetMostRecentSetVariable(pBuilder, "$CBTYPE$");

		if (pCategoryName)
		{
			if (eaFindString(&ppCategoryNames, pCategoryName) == -1)
			{
				eaPush(&ppCategoryNames, pCategoryName);
			}
		}

		if (pProductName)
		{
			if (eaFindString(&ppProductNames, pProductName) == -1)
			{
				eaPush(&ppProductNames, pProductName);
			}
		}

		if (pTypeName)
		{
			if (eaFindString(&ppTypeNames, pTypeName) == -1)
			{
				eaPush(&ppTypeNames, pTypeName);
			}
		}
	}
	FOR_EACH_END;

	eaQSort(ppProductNames, strCmp);
	eaQSort(ppCategoryNames, strCmp);
	eaQSort(ppTypeNames, strCmp);

	estrPrintf(&pOverview->pLinks, "<span><a href=\"%s.Builders\">All</a></span>  ",
		LinkToThisServer());

	for (i = 0; i < eaSize(&ppProductNames); i++)
	{
		estrConcatf(&pOverview->pLinks,  "<span><a href=\"%s.Builders&svrProduct=%s\">%s</a></span>  ",
			LinkToThisServer(), ppProductNames[i], ppProductNames[i]);
	}
	
	estrConcatf(&pOverview->pLinks, "<div>");

	for (i = 0; i < eaSize(&ppCategoryNames); i++)
	{
		estrConcatf(&pOverview->pLinks,  "<span><a href=\"%s.Builders&svrCategory=%s\">%s</a></span>  ",
			LinkToThisServer(), ppCategoryNames[i], ppCategoryNames[i]);
	}

	estrConcatf(&pOverview->pLinks, "</div> <div>");


	for (i = 0; i < eaSize(&ppTypeNames); i++)
	{
		estrConcatf(&pOverview->pLinks,  "<span><a href=\"%s.Builders&svrType=%s\">%s</a></span>  ",
			LinkToThisServer(), ppTypeNames[i], ppTypeNames[i]);
	}

	estrConcatf(&pOverview->pLinks, "</div>");

	eaDestroy(&ppProductNames);
	eaDestroy(&ppCategoryNames);
}

static bool ResultIsReal(enumCBResult eResult)
{
	switch (eResult)
	{
	case CBRESULT_FAILED:
	case CBRESULT_SUCCEEDED:
	case CBRESULT_SUCCEEDED_W_ERRS:
		return true;
	}

	return false;
}

static enumCBResult FindMostRecentRealResult(BuilderInfo *pBuilder)
{
	if (pBuilder->pCurBuild)
	{
		if (ResultIsReal(pBuilder->pCurBuild->eResult))
		{
			return pBuilder->pCurBuild->eResult;
		}
	}

	FOR_EACH_IN_EARRAY(pBuilder->ppPreviousBuilds, SingleBuildInfo, pBuild)
	{
		if (ResultIsReal(pBuild->eResult))
		{
			return pBuild->eResult;
		}
	}
	FOR_EACH_END;

	return CBRESULT_NONE;
}

static CBMonitorOverview *GetOverview(UrlArgumentList *pArgList)
{
	static CBMonitorOverview *pOverview = NULL;
	const char *pCategoryNameToMatch = NULL;
	const char *pProductNameToMatch = NULL;
	const char *pTypeNameToMatch = NULL;

	pCategoryNameToMatch = urlFindValue(pArgList, "svrCategory");
	pProductNameToMatch = urlFindValue(pArgList, "svrProduct");
	pTypeNameToMatch = urlFindValue(pArgList, "svrType");

	if (pOverview)
	{
		StructReset(parse_CBMonitorOverview, pOverview);
	}
	else
	{
		pOverview = StructCreate(parse_CBMonitorOverview);
	}
	
	FillInLinks(pOverview);

	FOR_EACH_IN_STASHTABLE(sBuildersByName, BuilderInfo, pBuilder)
	{
		char *pCategoryName = GetCategoryNameForBuilder(pBuilder);
		BuilderCategoryOverview *pCategory = eaIndexedGetUsingString(&pOverview->ppCategories, pCategoryName);
		BuilderOverview *pBuilderOverview;

		enumCBResult eMostRecentRealResult;


		if (pCategoryNameToMatch && stricmp_safe(pCategoryName, pCategoryNameToMatch) != 0)
		{
			continue;
		}

		if (pProductNameToMatch)
		{
			char *pProductName = GetMostRecentSetVariable(pBuilder, "$PRODUCTNAME$");
			if (stricmp_safe(pProductName, pProductNameToMatch) != 0)
			{
				continue;
			}			
		}

		if (pTypeNameToMatch)
		{
			char *pTypeName = GetMostRecentSetVariable(pBuilder, "$CBTYPE$");
			if (stricmp_safe(pTypeName, pTypeNameToMatch) != 0)
			{
				continue;
			}			
		}

		if (!pCategory)
		{
			pCategory = StructCreate(parse_BuilderCategoryOverview);
			pCategory->pCategoryName = strdup(pCategoryName);
			eaPush(&pOverview->ppCategories, pCategory);
		}

		pBuilderOverview = StructCreate(parse_BuilderOverview);

		eMostRecentRealResult = FindMostRecentRealResult(pBuilder);
		if (eMostRecentRealResult == CBRESULT_FAILED)
		{
			estrCopy2(&pBuilderOverview->pSucceeded, "No");
		}
		else if (eMostRecentRealResult == CBRESULT_SUCCEEDED || eMostRecentRealResult == CBRESULT_SUCCEEDED_W_ERRS)
		{
			estrCopy2(&pBuilderOverview->pSucceeded, "Yes");
		}

		if (pBuilder->bConnected)
		{
			estrCopy2(&pBuilderOverview->pConnected, "Yes");
		}
		else if (estrLength(&pBuilder->pDowntimeComment))
		{
			estrPrintf(&pBuilderOverview->pConnected, "DOWNTIME - %s", pBuilder->pDowntimeComment);
		}
		else
		{
			if (pBuilder->iLastContactTime == 0)
			{
				estrCopy2(&pBuilderOverview->pConnected, "Never");
			}
			else
			{
				estrPrintf(&pBuilderOverview->pConnected, "%s ago", GetPrettyDurationString(timeSecondsSince2000() - pBuilder->iLastContactTime));
			}
		}


		estrPrintf(&pBuilderOverview->pLink, "<a href=\"http://%s\">%s</a> <a href=\"%s%sBuilders[%s]\">(internal)</a>", pBuilder->pMachineName, pBuilder->pMachineName,
			LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, pBuilder->pMachineName);

		estrPrintf(&pBuilderOverview->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pBuilder->pMachineName);

		estrCopy2(&pBuilderOverview->pType, GetMostRecentSetVariable(pBuilder, "$CBTYPE_VERBOSE$"));

		estrCopy2(&pBuilderOverview->pSVNRevision, GetCurrentSetVariable(pBuilder, "$SVNREVNUM$"));
		estrCopy2(&pBuilderOverview->pGimmeTime, GetCurrentSetVariable(pBuilder, "$GIMMETIME$"));
		estrCopy2(&pBuilderOverview->pPatchVersion, GetCurrentSetVariable(pBuilder, "$PATCHVERSION$"));

		estrCopy2(&pBuilderOverview->pCurState, GetCurrentState(pBuilder));
		estrTruncateAtFirstOccurrence(&pBuilderOverview->pCurState, ' ');

		estrCopy2(&pBuilderOverview->pSuccessfulBuild, pBuilder->pMostRecentSuccessfulBuildSummary);

		if (estrLength(&pBuilderOverview->pCurState) > MAX_STATE_LENGTH)
		{
			estrSetSize(&pBuilderOverview->pCurState, MAX_STATE_LENGTH);
			estrConcatf(&pBuilderOverview->pCurState, "...");
		}

		eaPush(&pCategory->ppBuilders, pBuilderOverview);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pOverview->ppCategories, BuilderCategoryOverview, pCategory)
	{
		eaQSort(pCategory->ppBuilders, SortBuildersByType);
	}
	FOR_EACH_END;

	


	return pOverview;
}


bool ProcessCBMonitorOverviewIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{

	if (pLocalXPath[0] != '[')
	{
		CBMonitorOverview *pOverview = GetOverview(pArgList);

		return ProcessStructIntoStructInfoForHttp("", pArgList,
			pOverview, parse_CBMonitorOverview, iAccessLevel, 0, pStructInfo, eFlags);
	}

	GetMessageForHttpXpath("Error, don't do that", pStructInfo, true);
	return true;
}

AUTO_RUN;
void initCBMonitorServerMon(void)
{
	RegisterCustomXPathDomain(".Builders", ProcessCBMonitorOverviewIntoStructInfoForHttp, NULL);
}

AUTO_COMMAND;
void RemoveBuilder(char *pBuilderName, char *pConfirm)
{
	BuilderInfo *pBuilder = FindBuilderByName(pBuilderName);
	char filename[CRYPTIC_MAX_PATH];
	if (!pBuilder || stricmp(pConfirm, "yes") != 0)
	{
		return;
	}

	BuilderLog(pBuilder, "Removing via AUTO_COMMAND");

	stashRemovePointer(sBuildersByName, pBuilderName, NULL);
	sprintf(filename, "%s\\%s.txt", CBMONITOR_DATA_DIR, pBuilder->pMachineName);
	unlink(filename);
	StructDestroy(parse_BuilderInfo, pBuilder);
}

AUTO_COMMAND;
void SaveEverything(void)
{
	FOR_EACH_IN_STASHTABLE(sBuildersByName, BuilderInfo, pBuilder)
	{
		SaveBuilderInfoToFile(pBuilder);
	}
	FOR_EACH_END;
}

AUTO_COMMAND;
void SaveEverythingAndClose(void)
{
	FOR_EACH_IN_STASHTABLE(sBuildersByName, BuilderInfo, pBuilder)
	{
		SaveBuilderInfoToFile(pBuilder);
	}
	FOR_EACH_END;

	exit(0);
}


AUTO_COMMAND;
void SetDowntimeComment(char *pHostName, ACMD_SENTENCE pComment)
{
	BuilderInfo *pBuilder = FindBuilderByName(pHostName);
	if (!pBuilder)
	{
		return;
	}

	if (stricmp_safe(pBuilder->pDowntimeComment, pComment) == 0)
	{
		return;
	}

	estrCopy2(&pBuilder->pDowntimeComment, pComment);
	SaveBuilderInfoToFile(pBuilder);

}



int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	DO_AUTO_RUNS

	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	SetAppGlobalType(GLOBALTYPE_ERRORTRACKER);
	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);


	//set it so that gimme never pauses
	_putenv_s("GIMME_NO_PAUSE", "1");






	srand((unsigned int)time(NULL));

	sprintf(gServerLibState.logServerHost, "NONE");
	sprintf(gServerLibState.controllerHost, "NONE");

	serverLibStartup(argc, argv);

	while (!(spCBMonitorListen = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH,DEFAULT_CBMONITOR_PORT,
		CBMonitorHandleMsg,CBMonitorConnect,CBMonitorDisconnect,sizeof(CBMonitorUserData))))
	{
		Sleep(1);
	}

	SetAppGlobalType(GLOBALTYPE_CBMONITOR);

	GenericHttpServing_Begin(DEFAULT_CBMONITOR_HTML_PORT, "CB", NULL, 0);

	LoadBuildersFromFiles();

	while (1)
	{
		utilitiesLibOncePerFrame(REAL_TIME);
		serverLibOncePerFrame();
		Sleep(1);
		commMonitor(commDefault());
		GenericHttpServing_Tick();
	}

	EXCEPTION_HANDLER_END
}



#include "CBMonitor_pub_h_ast.c"
#include "ContinuousBuilder_Pub_h_ast.c"
#include "CBMonitor_h_ast.c"
