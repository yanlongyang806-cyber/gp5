#include "ContinuousBuilder.h"
#include "CBReportToCBMonitor.h"
#include "net/net.h"
#include "crypticPorts.h"
#include "GlobalComm.h"
#include "utils.h"
#include "..\..\Utilities\cbmonitor\CBMonitor_pub.h"
#include "CbMonitor_pub_h_ast.h"
#include "StructNet.h"
#include "timing.h"
#include "BuildScripting.h"
#include "CBConfig.h"
#include "CBerrorProcessing.h"
#include "objContainer.h"
#include "ETCommon/ETCommonStructs.h"
#include "FileUtil2.h"
#include "CBStartup.h"
#include "TimedCallback.h"
#include "ContinuousBuilder_pub_h_Ast.h"

#include "CBConfig_h_ast.h"


static void GenerateAndSendBuilderPlexerLogDirReport(void);

static char sCBMonitorName[256] = "builders.cryptic.loc";
AUTO_CMD_STRING(sCBMonitorName, CBMonitorName);


static CommConnectFSM *pCBMonitorConnectFSM = NULL;
static NetLink *pCBMonitorNetLink = NULL;

//set to true when we get a build_started, false when we get a build_ended... send no packets when this is false so we don't
//get confusing state changes and heartbeats between builds which screw up the cbmonitor's record keeping
static bool sbBuildCurrentlyActive = false;

void CBReportToCBMonitor_ConnectCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (pCBMonitorNetLink)
	{
		CBReportToCBMonitor_ReportBuildComment();
		GenerateAndSendBuilderPlexerLogDirReport();
	}
}



void CBReportToCBMonitor_Connect(NetLink* link,void *user_data)
{
	Packet *pPak = pktCreate(link, FROM_CB_TO_CBMONITOR_CONNECT);
	pktSendString(pPak, getHostName());
	pktSend(&pPak);

	TimedCallback_Run(CBReportToCBMonitor_ConnectCB, NULL, 1.0f);
}

static U32 siLastHeartbeatTime = 0;

static void FillInHeartbeatReport(CBMonitorHeartbeatStruct *pHeartBeat, enumCBResult eResultThatisCurrentlyHappening)
{
	int i;
	ErrorTrackerEntryList *pCurList = CB_GetCurrentEntryList();


	estrCopy2(&pHeartBeat->pPresumedGimmeProductAndBranch, CBGetPresumedGimmeProjectAndBranch());
	estrCopy2(&pHeartBeat->pPresumedSVNBranch, CBGetPresumedSVNBranch());

	if (CBGetRootScriptingContext())
	{
		for (i = 0; i < eaSize(&gConfig.ppVariablesToReportToCBMonitorOnHeartbeat); i++)
		{
			char *pVal = NULL;
			char *pFixedUpName = NULL;

			estrStackCreate(&pFixedUpName);
			estrCopy2(&pFixedUpName, gConfig.ppVariablesToReportToCBMonitorOnHeartbeat[i]);
			if (pFixedUpName[0] != '$')
			{
				estrInsertf(&pFixedUpName, 0, "$");
			}

			if (pFixedUpName[estrLength(&pFixedUpName)-1] != '$')
			{
				estrConcatf(&pFixedUpName, "$");
			}


			if (BuildScripting_FindVarValue(CBGetRootScriptingContext(), pFixedUpName, &pVal))
			{
				NameValuePair *pPair = StructCreate(parse_NameValuePair);
				pPair->pName = strdup(pFixedUpName);
				pPair->pValue = strdup(pVal);
				eaPush(&pHeartBeat->ppVariables, pPair);
			}
			estrDestroy(&pFixedUpName);
		}
		
	}

	if (eResultThatisCurrentlyHappening)
	{
		estrCopy2(&pHeartBeat->pCurState, StaticDefineIntRevLookup(enumCBResultEnum, eResultThatisCurrentlyHappening));
	}
	else
	{
		CB_GetDescriptiveStateString(&pHeartBeat->pCurState);
	}

	pHeartBeat->iNumErrors = objCountTotalContainersWithType(pCurList->eContainerType);
}

static bool CheckForNoReporting(void)
{
	bool bRetVal = false;
	CBConfig tempConfig = {0};
	StructInit(parse_CBConfig, &tempConfig);

	ParserReadTextFile("c:\\continuousbuilder\\cbconfig.txt", parse_CBConfig, &tempConfig, 0);

	if (tempConfig.bDev && !tempConfig.bForceReportToCBMonitor)
	{

		bRetVal = true;
	}

	StructDeInit(parse_CBConfig, &tempConfig);
	
	return bRetVal;
}

void CBReportToCBMonitor_Update(void)
{
	static bool sbFirst = true;
	static bool sbNoReporting = false;

	if (sbFirst)
	{
		sbFirst = false;
		sbNoReporting = CheckForNoReporting();
	}

	if (sbNoReporting)
	{
		return;
	}


	if (!commConnectFSMForTickFunctionWithRetrying(&pCBMonitorConnectFSM, &pCBMonitorNetLink, "link to CBMonitor",
		2.0f, commDefault(), LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,sCBMonitorName,
		DEFAULT_CBMONITOR_PORT,0,CBReportToCBMonitor_Connect,0,0, NULL, 0, NULL, 0))
	{
		return;
	}

	if ((sbBuildCurrentlyActive || CB_IsWaitingBetweenBuilds()) && siLastHeartbeatTime < timeSecondsSince2000() - CBMONITOR_HEARTBEAT)
	{
		Packet *pPak = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_HEARTBEAT);
		CBMonitorHeartbeatStruct *pHeartBeat = StructCreate(parse_CBMonitorHeartbeatStruct);

		FillInHeartbeatReport(pHeartBeat, CBRESULT_NONE);

		ParserSendStructSafe(parse_CBMonitorHeartbeatStruct, pPak, pHeartBeat);
		pktSend(&pPak);
		StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);

		siLastHeartbeatTime = timeSecondsSince2000();

	}
}

Packet *CBReportToCBMonitor_GetPacket(int iCmd)
{
	if (pCBMonitorNetLink && sbBuildCurrentlyActive)
	{
		return pktCreate(pCBMonitorNetLink, iCmd);
	}

	return NULL;
}

void CBReportToCBMonitor_ReportState(void)
{
	if (pCBMonitorNetLink && sbBuildCurrentlyActive)
	{
		Packet *pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_STATESET);
		char *pTempStr = NULL;
		estrStackCreate(&pTempStr);
		CB_GetDescriptiveStateString(&pTempStr);
		pktSendString(pPkt, pTempStr);
		pktSend(&pPkt);
		estrDestroy(&pTempStr);
	}
}

void CBReportToCBMonitor_BuildEnded(enumCBResult eResult)
{
	if (pCBMonitorNetLink && sbBuildCurrentlyActive)
	{
		Packet *pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_RUNENDED);
		CBMonitorHeartbeatStruct *pHeartBeat = StructCreate(parse_CBMonitorHeartbeatStruct);

		pktSendBits(pPkt, 32, eResult);
		FillInHeartbeatReport(pHeartBeat, eResult);

		ParserSendStructSafe(parse_CBMonitorHeartbeatStruct, pPkt, pHeartBeat);
		pktSend(&pPkt);

		StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);
	}

	sbBuildCurrentlyActive = false;
}

void CBReportToCBMonitor_BuildStarting(void)
{
	sbBuildCurrentlyActive = true;

	if (pCBMonitorNetLink)
	{
		Packet *pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_RUNBEGAN);
		CBMonitorHeartbeatStruct *pHeartBeat = StructCreate(parse_CBMonitorHeartbeatStruct);

		FillInHeartbeatReport(pHeartBeat, CBRESULT_NONE);

		ParserSendStructSafe(parse_CBMonitorHeartbeatStruct, pPkt, pHeartBeat);
		pktSend(&pPkt);

		StructDestroy(parse_CBMonitorHeartbeatStruct, pHeartBeat);
	}

	CBReportToCBMonitor_ReportBuildComment();
}

void CBReportToCBMonitor_ReportSVNAndGimme(U32 iSVNRev, U32 iGimmeTime)
{
	if (pCBMonitorNetLink && sbBuildCurrentlyActive)
	{
		char tempSVNString[64];
		char tempGimmeString[128];
		Packet *pPkt = NULL;

		sprintf(tempSVNString, "%u", iSVNRev);
		strcpy(tempGimmeString, timeGetLocalDateStringFromSecondsSince2000(iGimmeTime));

		pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_VARIABLESET);
		pktSendString(pPkt, "$SVNREVNUM$");
		pktSendString(pPkt, tempSVNString);
		pktSend(&pPkt);

		pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_VARIABLESET);
		pktSendString(pPkt, "$GIMMETIME$");
		pktSendString(pPkt, tempGimmeString);
		pktSend(&pPkt);
	}
}

void CBReportToCBMonitor_ReportBuildComment(void)
{
	if (pCBMonitorNetLink)
	{
		Packet *pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_BUILDERCOMMENT);
		pktSendString(pPkt, GetBuilderComment());
		pktSend(&pPkt);	
	}
}

static char **sppBuilderPlexedLogDirs = NULL;

void CheckLogDirectoryForBuilderPlexedLogDirs(char *pDirName)
{
	char **ppSubDirs = fileScanDirFoldersNoSubdirRecurse(pDirName, FSF_FOLDERS);
	int i;
	for (i = 0; i < eaSize(&ppSubDirs); i++)
	{
		eaPush(&sppBuilderPlexedLogDirs, strdup(ppSubDirs[i]));
	}

	fileScanDirFreeNames(ppSubDirs);
}

void CheckProductDirectoryForBuilderPlexedLogDirs(char *pDirName)
{
	char **ppSubDirs = fileScanDirFoldersNoSubdirRecurse(pDirName, FSF_FOLDERS | FSF_RETURNSHORTNAMES);
	int i;

	for (i = 0; i < eaSize(&ppSubDirs); i++)
	{
		if (CBStartup_StringIsBuildTypeName(ppSubDirs[i]))
		{
			char fullSubDirName[CRYPTIC_MAX_PATH];
			sprintf(fullSubDirName, "%s\\%s\\logs", pDirName, ppSubDirs[i]);
			CheckLogDirectoryForBuilderPlexedLogDirs(fullSubDirName);
		}
	}

	fileScanDirFreeNames(ppSubDirs);
}

void CheckDirectoryForBuilderPlexedLogDirs(char *pDirName)
{
	char fullDirName[CRYPTIC_MAX_PATH];
	char **ppSubDirs;
	int i;

	sprintf(fullDirName, "c:\\%s", pDirName);
	ppSubDirs = fileScanDirFoldersNoSubdirRecurse(fullDirName, FSF_FOLDERS | FSF_RETURNSHORTNAMES);

	for (i = 0; i < eaSize(&ppSubDirs); i++)
	{
		if (CBStartup_StringIsProductName(ppSubDirs[i]))
		{
			char fullSubDirName[CRYPTIC_MAX_PATH];
			sprintf(fullSubDirName, "%s\\%s", fullDirName, ppSubDirs[i]);
			CheckProductDirectoryForBuilderPlexedLogDirs(fullSubDirName);
		}
	}

	fileScanDirFreeNames(ppSubDirs);
}

void GenerateAndSendBuilderPlexerLogDirReport(void)
{
	char **ppRootDirs = fileScanDirFoldersNoSubdirRecurse("c:\\", FSF_FOLDERS | FSF_RETURNSHORTNAMES | FSF_UNDERSCORED);
	int i;
	Packet *pPkt;

	for (i = 0; i < eaSize(&ppRootDirs); i++)
	{
		if (strStartsWith(ppRootDirs[i], "__") && strEndsWith(ppRootDirs[i], "ContinuousBuilder"))
		{
			CheckDirectoryForBuilderPlexedLogDirs(ppRootDirs[i]);
		}
	}

	pPkt = pktCreate(pCBMonitorNetLink, FROM_CB_TO_CBMONITOR_BUILDERPLEXLOGDIRS);

	for (i = 0; i < eaSize(&sppBuilderPlexedLogDirs); i++)
	{
		pktSendString(pPkt, sppBuilderPlexedLogDirs[i] );
	}

	pktSendString(pPkt, "");
	pktSend(&pPkt);


	fileScanDirFreeNames(ppRootDirs);
}


#include "CbMonitor_pub_h_ast.c"
