#include "ShardLauncher.h"
#include "TextParser.h"
#include "ShardLauncher_h_ast.h"
#include "StringUtil.h"
#include "estring.h"
#include "Earray.h"
#include "file.h"
#include "utils.h"
#include "timing.h"
#include "WinInclude.h"
#include "InStringCommands.h"
#include "../../core/controller/pub/ControllerPub.h"
#include "ControllerPub_h_Ast.h"
#include "sysUtil.h"
#include "patchclient.h"
#include "AutoGen/ControllerStartupSupport_h_ast.h"
#include "ShardLauncherRunTheShard.h"
#include "SentryServerComm.h"
#include "sock.h"
#include "CrypticPorts.h"
#include "utilitiesLib.h"
#include "net.h"
#include "../../libs/PatchClientLib/PatchClientLibStatusMonitoring.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "process_util.h"
#include "GlobalTypes_h_ast.h"
#include "fileutil2.h"
#include "ThreadManager.h"
#include "ShardLauncherWatchTheLaunch.h"
#include "ScratchStack.h"
#include "osdependent.h"
#include "qsortG.h"
#include "../../utilities/SentryServer/Sentry_comm.h"
#include "ShardLauncherWatchTheLaunch.h"
#include "ShardLauncher_pub_h_ast.h"
#include "StatusReporting.h"

#define MDASH (-106)

#define NO_STR(pStr) ( !(pStr) || !(pStr[0]) || StringIsAllWhiteSpace(pStr))

static MachineInfoForShardSetupList *spShardSetupFile;

static bool sbFailed = false;

static bool sbLoggingExecutablesAlreadyRunning = false;

static bool sbKillClusterStuffAtRunTime = false;
AUTO_CMD_INT(sbKillClusterStuffAtRunTime, KillClusterStuffAtRunTime) ACMD_COMMANDLINE;

bool ReplaceAllMacrosInString(char **ppString, ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pDebugLabel);


char *pDeprecatedOptions[] = 
{
	"SHARD_NAME",
};

char *GetOverlordSnippet(void)
{
	char *pOverlordName = UtilitiesLib_GetOverlordName();

	if (pOverlordName && pOverlordName[0])
	{
		static char *spRetVal = NULL;
		estrPrintf(&spRetVal, "-SetOverlord %s", pOverlordName);
		return spRetVal;
	}
	else
	{
		return "";
	}
}

char *GetQuotedString(char *pInString)
{
	static char *spRetVal = NULL;
	FILE *pFile;

	estrClear(&spRetVal);
	pFile = fileOpenEString(&spRetVal);
	WriteQuotedString(pFile, pInString, 0, 0);
	fclose(pFile);

	return spRetVal;
}



bool OptionIsDeprecated(char *pOptionName)
{
	int i;
	for (i = 0 ; i < ARRAY_SIZE(pDeprecatedOptions); i++)
	{
		if (stricmp(pOptionName, pDeprecatedOptions[i]) == 0)
		{
			return true;
		}
	}
	return false;
}

void RunTheShard_Log(FORMAT_STR const char *pFmt, ...)
{
	char *pText = NULL;
	estrStackCreate(&pText);
	estrGetVarArgs(&pText, pFmt);

	WatchTheLaunch_Log(WTLLOG_NORMAL, pText);
	StatusReporting_LogFromBGThread(pText);
	estrDestroy(&pText);

}
void RunTheShard_LogWarning(FORMAT_STR const char *pFmt, ...)
{
	char *pText = NULL;
	estrStackCreate(&pText);
	estrGetVarArgs(&pText, pFmt);

	WatchTheLaunch_Log(WTLLOG_WARNING, pText);
	StatusReporting_LogFromBGThread(pText);
	estrDestroy(&pText);
}
void RunTheShard_LogFail(FORMAT_STR const char *pFmt, ...)
{
	char *pText = NULL;
	estrStackCreate(&pText);
	estrGetVarArgs(&pText, pFmt);

	WatchTheLaunch_Log(WTLLOG_FATAL, pText);
	StatusReporting_LogFromBGThread(pText);
	estrDestroy(&pText);

	while (1)
	{
		Sleep(1000);
	}
}

void RunTheShard_LogSucceed(FORMAT_STR const char *pFmt, ...)
{
	char *pText = NULL;
	estrStackCreate(&pText);
	estrGetVarArgs(&pText, pFmt);

	WatchTheLaunch_Log(WTLLOG_SUCCEEDED, pText);
	estrDestroy(&pText);
}

bool ReplaceBasicMacros(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char **ppEString)
{
	bool bSomethingChanged = false;

	static char *spStartTime = NULL;

	static char *spShardNameWithCluster = NULL;

	if (pShard)
	{
		estrPrintf(&spShardNameWithCluster, "%s_%s", pRun->pClusterName, pShard->pShardName);
		if (estrReplaceOccurrences(ppEString, "$SHARD_NAME_WITH_CLUSTER$", spShardNameWithCluster))
		{
			bSomethingChanged = true;
		}
	}
	else
	{
		char *pShardName = GetNonZeroOptionByName(NULL, "SHARD_NAME");

		if (pShardName)
		{
			if (estrReplaceOccurrences(ppEString, "$SHARD_NAME_WITH_CLUSTER$", pShardName))
			{
				assertmsgf(NO_STR(pRun->pClusterName), "Not properly using SHARD_NAME_WITH_CLUSTER for some reason");

				bSomethingChanged = true;
			}
		}
	}

	if (!spStartTime)
	{
		estrPrintf(&spStartTime, "%s", timeGetLocalTimeStringFromSecondsSince2000(timeSecondsSince2000()));
		estrMakeAllAlphaNumAndUnderscores(&spStartTime);
	}

	if (estrReplaceOccurrences(ppEString, "$STARTTIME$", spStartTime))
	{
		bSomethingChanged = true;
	}

	if (estrReplaceOccurrences(ppEString, "$PRODUCTNAME$", gpRun->pProductName))
	{
		bSomethingChanged = true;
	}

	if (estrReplaceOccurrences(ppEString, "$HOSTNAME$", getComputerName()))
	{
		bSomethingChanged = true;
	}

	if (estrReplaceOccurrences(ppEString, "$SHARDSETUPFILE_SET$", (gpRun->pShardSetupFile && gpRun->pShardSetupFile[0] && stricmp(gpRun->pShardSetupFile, "none") != 0) ? "1" : "0"))
	{
		bSomethingChanged = true;
	}

	if (estrReplaceOccurrences(ppEString, "$OVERRIDEEXES_SET$", eaSize(&gpRun->ppOverrideExecutableNames)? "1" : "0"))
	{
		bSomethingChanged = true;
	}



	if (pShard)
	{
		if (estrReplaceOccurrences(ppEString, "$SHARDNAME$", pShard->pShardName))
		{
			bSomethingChanged = true;
		}
		if (estrReplaceOccurrences(ppEString, "$SHARD_NAME$", pShard->pShardName))
		{
			bSomethingChanged = true;
		}

		if (estrReplaceOccurrences(ppEString, "$MACHINENAME$", pShard->pMachineName))
		{
			bSomethingChanged = true;
		}

		
	}
	

	return bSomethingChanged;
}


void CheckForBuiltInOptions(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard)
{
	FOR_EACH_IN_EARRAY(gppBuiltInOptionsForShardType, BuiltInOptionForShardType, pOption)
	{
		if (pOption->eShardType == pShard->eShardType)
		{
			ShardLauncherConfigOptionChoice *pChoice = FindHighestPriorityChoice(pRun, pShard, pOption->pOptionName);
			if (pChoice && stricmp(pChoice->pValue, pOption->pOptionValue) != 0)
			{
				RunTheShard_LogFail("Option %s is hard wired to value %s for shards of type %s, but shard %s is trying to set it to value %s",
					pOption->pOptionName, pOption->pOptionValue, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pOption->eShardType), 
					pShard->pShardName, pChoice->pValue);
			}

			if (!pChoice)
			{
				pChoice = StructCreate(parse_ShardLauncherConfigOptionChoice);
				pChoice->pConfigOptionName = strdup(GetShardOrShardTypeSpecificOptionName(pOption->pOptionName, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pOption->eShardType)));
				pChoice->pValue = strdup(pOption->pOptionValue);

				eaPush(&pRun->ppChoices, pChoice);
			}
		}
	}
	FOR_EACH_END;



}

bool VerifyAndFixupLauncherRun(ShardLauncherRun *pRun)
{
	if (!gpRun->pOptionLibrary)
	{
		RunTheShard_LogFail("No option librayr");
		return false;
	}

	if (NO_STR(gpRun->pRunName))
	{
		RunTheShard_LogFail("pRunName not set");
		return false;
	}

	if (NO_STR(gpRun->pProductName))
	{
		RunTheShard_LogFail("pProductName not set");
		return false;
	}

	if (NO_STR(gpRun->pShortProductName))
	{
		RunTheShard_LogFail("pShortProductName not set");
		return false;
	}

	if (NO_STR(gpRun->pPatchVersion))
	{
		RunTheShard_LogFail("pPatchServer not set");
		return false;
	}

	if (NO_STR(gpRun->pDirectory))
	{
		RunTheShard_LogFail("pDirectory not set");
		return false;
	}

	if (gpRun->pTemplateFileName)
	{
		ShardLauncherRun *pTemplate = StructCreate(parse_ShardLauncherRun);
		if (!ParserReadTextFile(gpRun->pTemplateFileName, parse_ShardLauncherRun, pTemplate, 0))
		{
			StructDestroy(parse_ShardLauncherRun, pTemplate);
			RunTheShard_LogFail("Couldn't load template %s", gpRun->pTemplateFileName);
			return false;
		}

		eaDestroyStruct(&gpRun->ppTemplateChoices, parse_ShardLauncherConfigOptionChoice);
		gpRun->ppTemplateChoices = pTemplate->ppChoices;
		pTemplate->ppChoices = NULL;
		StructDestroy(parse_ShardLauncherRun, pTemplate);
	}

	if (gpRun->bClustered)
	{
		if (!strstri(gpRun->pDirectory, "$SHARDNAME$"))
		{
			RunTheShard_LogFail("Directory name must contain $SHARDNAME$ for clustered run");
			return false;
		}

		if (NO_STR(gpRun->pClusterName))
		{
			RunTheShard_LogFail("Cluster name");
				return false;
		}

		estrPrintf(&gpRun->pLocalDataTemplateDir, "c:\\%s\\Data", gpRun->pClusterName);
		estrPrintf(&gpRun->pLocalFrankenBuildDir, "c:\\%s\\Frankenbuilds", gpRun->pClusterName);

		if (!eaSize(&gpRun->ppClusterShards))
		{
			RunTheShard_LogFail("No cluster shards");
			return false;
		}

		FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
		{

			if (NO_STR(pShard->pShardName) || NO_STR(pShard->pMachineName) || NO_STR(pShard->pShardSetupFileName))
			{
				RunTheShard_LogFail("Shard %s is missing name/machine name/shard setup file name", pShard->pShardName);
				return false;
			}

			if (pShard->eShardType == SHARDTYPE_UNDEFINED)
			{
				RunTheShard_LogFail("Shard %s has no shard type", pShard->pShardName);
				return false;
			}
		

			estrCopy2(&pShard->pDirectory_FixedUp, gpRun->pDirectory);
			ReplaceBasicMacros(gpRun, pShard, &pShard->pDirectory_FixedUp);


			estrPrintf(&pShard->pLocalBatchFileName, "c:\\shardLauncher\\%s_%s_RunTheShard.bat", gpRun->pClusterName, pShard->pShardName);
			estrPrintf(&pShard->pRemoteBatchFileName, "%s\\%s_RunTheShard.bat", pShard->pDirectory_FixedUp, pShard->pShardName);

			estrPrintf(&pShard->pLocalCommandFileName, "c:\\shardLauncher\\%s_%s_ControllerCommands.txt", gpRun->pClusterName, pShard->pShardName);
			estrPrintf(&pShard->pRemoteCommandFileName, "%s\\%s_ControllerCommands.txt", pShard->pDirectory_FixedUp, pShard->pShardName);

			CheckForBuiltInOptions(gpRun, pShard);

			if (stricmp_safe(pShard->pShardSetupFileName, "NONE") == 0)
			{
				FILE *pFile;
				char *pFileName = "c:\\shardSetupFiles\\1Machine_AutoGen.txt";
				mkdirtree_const(pFileName);
				pFile = fopen(pFileName, "wt");
				if (!pFile)
				{
					RunTheShard_LogFail("Couldn't open %s for writing", pFileName);
					return false;			
				}

				fprintf(pFile, "{\nMachines\n{\n\tMachineName localhost\n}\n}\n");
				fclose(pFile);

				free(pShard->pShardSetupFileName);
				pShard->pShardSetupFileName = strdup("1Machine_Autogen");
			}
		}
		FOR_EACH_END;

		
		if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0])
		{
			estrPrintf(&gpRun->pLogServerDir, "c:\\%s_Logging", gpRun->pClusterName);
		}


	}
	else
	{
		ReplaceAllMacrosInString(&gpRun->pDirectory, gpRun, NULL, "VerifyAndFixupLauncherRun");
	}

	return true;
}

void ExecuteSystemCommand(char *pStr, int iTimeout, bool *pOutTimedOut, int *pOutRetVal)
{
	QueryableProcessHandle *pHandle;
	U32 iLastSetTime;
	U32 iStartTime ;
		
	iLastSetTime = iStartTime = timeSecondsSince2000_ForceRecalc();

	*pOutTimedOut = false;

	pHandle = StartQueryableProcess(pStr, NULL, true, false, false, NULL);

	if (!pHandle)
	{
		RunTheShard_LogFail("Couldn't start queryable process from command line: %s", pStr);
		*pOutRetVal = -1;
		return;
	}

	while (1)
	{
		U32 iCurTime = timeSecondsSince2000_ForceRecalc();
		
		if (QueryableProcessComplete(&pHandle, pOutRetVal))
		{
			return;
		}

		if (iCurTime > iStartTime + iTimeout)
		{
			KillQueryableProcess(&pHandle);
			*pOutTimedOut = true;
			return;
		}

		if (iCurTime >= iLastSetTime + 15)
		{
			char *pPrettyString = NULL;
			char *pTempString = NULL;

			timeSecondsDurationToPrettyEString(iTimeout - (iCurTime - iStartTime), &pPrettyString);
			estrPrintf(&pTempString, "Timeout in %s", pPrettyString);
			setConsoleTitle(pTempString);

			estrDestroy(&pPrettyString);
			estrDestroy(&pTempString);

			iLastSetTime = iCurTime;
		}


		Sleep(1000);
	}


}

typedef struct RunShardTypeInfo
{
	char *pSharedCommandLine; 
	char *pFirstCommandLine; 
} RunShardTypeInfo;

static RunShardTypeInfo sTypeInfo[GLOBALTYPE_MAXTYPES] = {0};
static char *spGlobalCommandLine = NULL;
static U32 *spTypesToLaunch = NULL;

bool GlobalTypeIsGood(GlobalType eType)
{
	if (eType <= 0 || eType >= GLOBALTYPE_MAXTYPES)
	{
		return false;
	}

	return true;
}


//returns number of replacements, or -1 on failure
int ApplyConfigMacrosToString(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pDebugLabel, char **ppString)
{
	int iReplaceCount = 0;
	char *pFirstDollarSign;
	char *pSecondDollarSign;
	char *pChar;
	int iStartOffset;
	int iNumToRemove;
	int iNumToInsert;

	if (!estrLength(ppString))
	{
		return 0;
	}
	
	while (1)
	{			
		char varName[256];
		ShardLauncherConfigOptionChoice *pChoice;

		pFirstDollarSign = strchr(*ppString, '$');
		if (!pFirstDollarSign)
		{
			return iReplaceCount;
		}

		pSecondDollarSign = strchr(pFirstDollarSign + 1, '$');

		if (!pSecondDollarSign)
		{
			RunTheShard_LogFail("Nonmatching dollar signs in string %s while applying macros for %s",
				*ppString, pDebugLabel);
			return -1;
		}

		if (pSecondDollarSign == pFirstDollarSign + 1)
		{
			RunTheShard_LogFail("Corrupt string %s while applying macros for %s... how did two dollar signs end up next to each other?",
				*ppString, pDebugLabel);
			return -1;
		}

		if (pSecondDollarSign - pFirstDollarSign > 250)
		{
			RunTheShard_LogFail("Excessively long potential variable name in %s while applying macros for %s",
				*ppString, pDebugLabel);
			return -1;
		}

		for (pChar = pFirstDollarSign + 1; pChar < pSecondDollarSign; pChar++)
		{
			if (!(isalnum(*pChar) || *pChar == '_'))
			{
				RunTheShard_LogFail("Invalid character found in presumed macro name in %s while applying macros for %s",
					*ppString, pDebugLabel);
				return -1;
			}
		}

		strncpy(varName, pFirstDollarSign + 1, pSecondDollarSign - pFirstDollarSign - 1);


		pChoice = FindHighestPriorityChoice(gpRun, pShard, varName);
		if (!pChoice)
		{
			pChoice = eaIndexedGetUsingString(&gpRun->ppTemplateChoices, varName);
		}

		if (!pChoice)
		{
			return iReplaceCount;;
		}

		iNumToRemove = pSecondDollarSign - pFirstDollarSign + 1;
		iStartOffset = pFirstDollarSign - *ppString;
		iNumToInsert = pChoice->pValue ? (int)strlen(pChoice->pValue) : 0;

		estrRemove(ppString, iStartOffset, iNumToRemove);
		if (iNumToInsert)
		{
			estrInsert(ppString, iStartOffset, pChoice->pValue, iNumToInsert);
		}

		iReplaceCount++;
	}
}

//returns true on success, false on bad formatting of some sort.
//
//Does 3 kinds of replacing: Basic macros, in-string commands, and Currently chosen options
bool ReplaceAllMacrosInString(char **ppString, ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pDebugLabel)
{
	bool bSomethingChanged = true;
	int iResult;
	sbFailed = false;

	while (bSomethingChanged)
	{

		bSomethingChanged = ReplaceBasicMacros(gpRun, pShard, ppString);

		iResult = InStringCommands_Apply(ppString, NULL, NULL);

		if (iResult == -1)
		{
			RunTheShard_LogFail("InStringCommand failed on string %s while applying macros for %s", *ppString, pDebugLabel);
			return false;
		}

		if (iResult)
		{
			bSomethingChanged = true;
		}

		iResult = ApplyConfigMacrosToString(gpRun, pShard, pDebugLabel, ppString);

		if (iResult == -1)
		{
			return false;
		}

		if (iResult)
		{
			bSomethingChanged = true;
		}
	}

	if (strchr(*ppString, '$'))
	{
		RunTheShard_LogFail("Unable to replace all macros in string %s", *ppString);
		return false;
	}

	if (sbFailed)
	{
		RunTheShard_LogFail("Something went wrong while applying macros to %s for %s", *ppString, pDebugLabel);
		return false;
	}

	return true;
}

static char **sppAutoSettingInits = NULL;
static char **sppAutoSettingOptionNames = NULL;

void InitAutoSetting(ShardLauncherRun *pRun, const char *pSetting, const char *pOptionName)
{
	eaPush(&sppAutoSettingInits, strdup(pSetting));
	eaPush(&sppAutoSettingOptionNames, strdup(pOptionName));
}

char *GetAutoSettingFileName(ShardLauncherRun *pRun)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{
		estrPrintf(&spRetVal, "%s/%sServer/%s_ControllerAutoSettings.txt", 
			pRun->pDirectory, pRun->pProductName, pRun->pProductName);
	}

	return spRetVal;
}


bool ProcessAutoSettingInits(ShardLauncherRun *pRun)
{
	char *pAutoSettingFileName = GetAutoSettingFileName(pRun);
	int i;

	RunTheShard_Log("We have one or more AutoSettings to Init... checking if %s already exists\n",
		pAutoSettingFileName);

	if (fileExists(pAutoSettingFileName))
	{
		char *pWarningString = NULL;
		char *pBuff = NULL;

		RunTheShard_Log("It already exists, checking if values match...\n");

		pBuff = fileAlloc(pAutoSettingFileName, NULL);

		for (i = 0; i < eaSize(&sppAutoSettingInits); i++)
		{
			if (strstri(pBuff, sppAutoSettingInits[i]))
			{
				//happy
			}
			else
			{
				if (!estrLength(&pWarningString))
				{
					estrPrintf(&pWarningString, "One or more Shardlauncher options are attempting to initialize AUTO_SETTINGs, but those AUTO_SETTINGs have already been changed in %s. ShardLauncher will NOT set them, you may wish to either erase that file (which will clear all AUTO_SETTINGs to their starting values) or turn off those options to avoid this warning.\n",
						pAutoSettingFileName);
				}
				estrConcatf(&pWarningString, "Option %s trying to set: %s\n", sppAutoSettingOptionNames[i], sppAutoSettingInits[i]);
			}
		}

		if (estrLength(&pWarningString))
		{
			GetHumanConfirmationDuringShardRunning(pWarningString);
		}

		estrDestroy(&pWarningString);

	}
	else
	{
		FILE *pAutSettingFile;
		mkdirtree_const(pAutoSettingFileName);
		pAutSettingFile = fopen(pAutoSettingFileName, "wt");
		if (!pAutSettingFile)
		{
			RunTheShard_LogFail("Unable to open %s", pAutoSettingFileName);
			return false;
		}

		FOR_EACH_IN_EARRAY(sppAutoSettingInits, char, pSetting)
		{
			fprintf(pAutSettingFile, "%s", pSetting);
		}
		FOR_EACH_END;

		fclose(pAutSettingFile);

		RunTheShard_Log("Wrote %d settings into %s\n", eaSize(&sppAutoSettingInits), pAutoSettingFileName);
	}

	return true;

}


bool DoThingToDo(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, ShardLauncherConfigOptionThingToDo *pThing, char *pValue, char *pDebugLabel, GlobalType eOverrideServerType)
{
	static char *pFixedUpString = NULL;
	U32 i;
	GlobalType eServerTypeToUse;
	static char *pSuperEscValue = NULL;

	if (eOverrideServerType)
	{
		eServerTypeToUse = eOverrideServerType;
	}
	else
	{
		eServerTypeToUse = pThing->eServerType_MightNeedFixup;
	}

	estrClear(&pSuperEscValue);

	if (pValue)
	{
		estrSuperEscapeString(&pSuperEscValue, pValue);
	}

	//handle all cases that don't use the string
	switch (pThing->eType)
	{

	case THINGTODOTYPE_DONOTHING:
		return true;

	case THINGTODOTYPE_LAUNCHSERVER:
		if (!GlobalTypeIsGood(eServerTypeToUse))
		{
			RunTheShard_LogFail("Invalid global type %d", eServerTypeToUse);
			return false;
		}

		ea32Push(&spTypesToLaunch, eServerTypeToUse);
		return true;
	}

	estrCopy2(&pFixedUpString, pThing->pString);

	if (pValue)
	{
		estrReplaceOccurrences(&pFixedUpString, "$$SUPERESC$$", pSuperEscValue);
		estrReplaceOccurrences(&pFixedUpString, "$$", pValue);
	}

	if (!ReplaceAllMacrosInString(&pFixedUpString, gpRun, pShard, pDebugLabel))
	{
		return false;
	}


	//replace all M-dashes with normal dashes
	for (i=0; i < estrLength(&pFixedUpString); i++)
	{
		if (pFixedUpString[i] == MDASH)
		{
			pFixedUpString[i] = '-';
		}
	}

	switch (pThing->eType)
	{
	case THINGTODOTYPE_SHAREDCOMMANDLINE:
		if (!pFixedUpString)
		{
			RunTheShard_LogFail("Empty string to add to global command line");
			return false;
		}

		
		estrConcatf(&spGlobalCommandLine, " %s%s",( pFixedUpString[0] == '-' ||  pFixedUpString[0] == '+') ? "" : "-", pFixedUpString);
		break;

	case THINGTODOTYPE_SERVERTYPECOMMANDLINE:
		if (!GlobalTypeIsGood(eServerTypeToUse))
		{
			RunTheShard_LogFail("Invalid global type %d", eServerTypeToUse);
			return false;
		}
		if (!pFixedUpString)
		{
			RunTheShard_LogFail("Empty string to add to server type command line");
			return false;
		}
		estrConcatf(&sTypeInfo[eServerTypeToUse].pSharedCommandLine, " %s%s", ( pFixedUpString[0] == '-' ||  pFixedUpString[0] == '+') ? "" : "-", pFixedUpString);
		break;


	case THINGTODOTYPE_FIRSTSERVEROFTYPECOMMANDLINE:
		if (!GlobalTypeIsGood(eServerTypeToUse))
		{
			RunTheShard_LogFail("Invalid global type %d", eServerTypeToUse);
			return false;
		}
		if (!pFixedUpString)
		{
			RunTheShard_LogFail("Empty string to add to server type command line");
			return false;
		}
		estrConcatf(&sTypeInfo[eServerTypeToUse].pFirstCommandLine, " %s%s", ( pFixedUpString[0] == '-' ||  pFixedUpString[0] == '+') ? "" : "-", pFixedUpString);
		break;

	case THINGTODOTYPE_INIT_AUTOSETTING:
		if (!pFixedUpString)
		{
			RunTheShard_LogFail("Empty string to use to init auto setting");
			return false;
		}
		InitAutoSetting(pRun, pFixedUpString, pDebugLabel);
		break;

	default:
		RunTheShard_LogFail("Unknown thing to do");
		return false;
	}

	return true;
}

bool ShardOfTypeExists(ShardLauncherRun *pRun, ClusterShardType eType)
{
	if (!gpRun->bClustered)
	{
		return false;
	}

	FOR_EACH_IN_EARRAY(pRun->ppClusterShards, ShardLauncherClusterShard, pShard)
	{
		if (pShard->eShardType == eType)
		{
			return true;
		}
	}
	FOR_EACH_END;

	return false;
}

bool ConditionalThingsToDo_ConditionMet(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, 
	ShardLauncherConfigOptionConditionalThingsToDo *pConditionalThing)
{
	if (gpRun->bClustered)
	{
		if (pConditionalThing->eOnlyApplyToClusteredShardOfType && pShard->eShardType != pConditionalThing->eOnlyApplyToClusteredShardOfType)
		{
			return false;
		}

		if (pConditionalThing->eOnlyApplyIfClusteredShardOfTypeExist)
		{
			if (!ShardOfTypeExists(pRun, pConditionalThing->eOnlyApplyIfClusteredShardOfTypeExist))
			{
				return false;
			}
		}

		return true;
	}
	else
	{
		return pConditionalThing->bApplyIfNonClustered;
	}
}

bool ApplyStuffFromConfigOption(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, ShardLauncherConfigOption *pOption, char *pValue, GlobalType eOverrideType)
{
	int i;

	if (pValue && pValue[0] && !(pValue[0] == '0' && pValue[1] == 0))
	{
		for (i=0; i < eaSize(&pOption->ppThingsToDoIfSet); i++)
		{
			if (!DoThingToDo(gpRun, pShard, pOption->ppThingsToDoIfSet[i], pValue, pOption->pName, eOverrideType))
			{
				RunTheShard_LogFail("Something went wrong while processing option %s", pOption->pName);
				return false;
			}
		}

		FOR_EACH_IN_EARRAY(pOption->ppConditionalThingsToDo, ShardLauncherConfigOptionConditionalThingsToDo, pConditionals)
		{
			if (ConditionalThingsToDo_ConditionMet(pRun, pShard, pConditionals))
			{
				FOR_EACH_IN_EARRAY(pConditionals->ppThingsToDo, ShardLauncherConfigOptionThingToDo, pThingToDo)
				{
					if (!DoThingToDo(gpRun, pShard, pThingToDo, pValue, pOption->pName, eOverrideType))
					{
						RunTheShard_LogFail("Something went wrong while processing option %s", pOption->pName);
						return false;
					}
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;
	}
	else
	{
		for (i=0; i < eaSize(&pOption->ppThingsToDoIfNotSet); i++)
		{
			if (!DoThingToDo(gpRun, pShard, pOption->ppThingsToDoIfNotSet[i], NULL, pOption->pName, eOverrideType))
			{
				RunTheShard_LogFail("Something went wrong while processing option %s", pOption->pName);
				return false;
			}
		}
	}
	return true;
}

ShardLauncherConfigOption *FindConfigOption(ShardLauncherRun *pRun, char *pName, GlobalType *pOutOverrideType)
{
	int i;

	if (!gpRun->pOptionLibrary)
	{
		return NULL;
	}

	if (pName[0] == '{')
	{
		char *pCloseBrace = strchr(pName, '}');
		assertmsgf(pCloseBrace, "Corrupted option name %s", pName);
		pName = pCloseBrace + 1;
		while (IS_WHITESPACE(*pName))
		{
			pName++;
		}
	}
		

	if (pName[0] == '[')
	{
		char temp[1024];
		char *pFirstRightBracket;
		strcpy(temp, pName);

		pFirstRightBracket = strchr(temp, ']');
		assertmsgf(pFirstRightBracket, "Corrupted option name %s", pName);
		*pFirstRightBracket = 0;
		*pOutOverrideType = NameToGlobalType(temp + 1);
		assertmsgf(*pOutOverrideType, "Corrupted option name %s", pName);
		pName += pFirstRightBracket - temp + 2;
	}
	else
	{
		*pOutOverrideType = GLOBALTYPE_NONE;
	}

	for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists) + (gpRun->pOptionLibrary->pServerTypeSpecificList ? 1 : 0); i++)
	{
		ShardLauncherConfigOptionList *pList;
		int j;
		if (i == eaSize(&gpRun->pOptionLibrary->ppLists))
		{
			pList = gpRun->pOptionLibrary->pServerTypeSpecificList;
		}
		else
		{
			pList =  gpRun->pOptionLibrary->ppLists[i];
		}


		for (j = 0; j < eaSize(&pList->ppOptions); j++)
		{
			if (stricmp(pList->ppOptions[j]->pName, pName) == 0)
			{
				return pList->ppOptions[j];
			}
		}
	}

	return NULL;
}

char *GetShortExecutableName(char *pInName, bool bRemoveFD)
{
	static char *pShortName = NULL;
	estrGetDirAndFileName(pInName, NULL, &pShortName);

	if (bRemoveFD)
	{
		if (strEndsWith(pShortName, "FD.exe"))
		{
			estrReplaceOccurrences(&pShortName, "FD.exe", ".exe");
		}
	}

	return pShortName;
}

void MaybeAddAutoSettingsPauseStuff(ShardLauncherClusterShard *pShard, FILE *pOutFile)
{
	if (GetNonZeroOptionByName(pShard, "AUTO_SETTING_PAUSE"))
	{
		fprintf(pOutFile, "EXECUTECOMMAND Controller \"AuditUpcomingLaunchesforAutoSettingConflicts\"\n");
		fprintf(pOutFile, "WAITFORCONFIRM \"Auto settings have been processed, generated {{COMMAND NumAutoSettingErrors}} error(s). Please review them, confirm to proceed\"\n");
	}
}

void SetPatchVersionOnCB(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pPatchingCommandLine)
{
	char *pCT = GetNonZeroOptionByName(pShard, "CONTROLLERTRACKER");
	char *pCTName = GetNonZeroOptionByName(pShard, "WHICH_CONTROLLERTRACKER");
	char *pCustomCTName = GetNonZeroOptionByName(pShard, "CUSTOM_CONTROLLERTRACKER");
	char *pShardName_Raw = pShard ? pShard->pShardName : GetNonZeroOptionByName(pShard, "SHARD_NAME");
	char *pShardNameCopy = NULL;

	char *pVersionSuperEsc = NULL;
	char *pPatchingCommandLineSuperEsc = NULL;
	char *pCommandLine = NULL;

	if (stricmp_safe(pCTName, "unspecified") == 0)
	{
		pCTName = pCustomCTName;
	}

	if (!pCT || !atoi(pCT) || !pCTName || !pShardName_Raw)
	{
		return;
	}

	estrCopy2(&pShardNameCopy, pShardName_Raw);
	ReplaceBasicMacros(gpRun, pShard, &pShardNameCopy);

	estrSuperEscapeString(&pVersionSuperEsc, gpRun->pPatchVersionComment);
	estrSuperEscapeString(&pPatchingCommandLineSuperEsc, pPatchingCommandLine);
	
	estrPrintf(&pCommandLine, "SetShardVersionOnCT.exe -ShardName %s -CTName %s -SuperEsc VersionString %s -SuperEsc PatchCommandLine %s",
		pShardNameCopy, pCTName, pVersionSuperEsc, pPatchingCommandLineSuperEsc);

	system_detach(pCommandLine, true, true);

	estrDestroy(&pShardNameCopy);
	estrDestroy(&pVersionSuperEsc);
	estrDestroy(&pPatchingCommandLineSuperEsc);
	estrDestroy(&pCommandLine);

}

//if the choice name is cluster-specific, ie, {foo}bar, returns foo and bar separately. Otherwise just returns the name
void SubdivideChoiceName(char *pInChoice, char **ppOutChoice, char **ppOutShardOrShardType)
{
	if (pInChoice[0] == '{')
	{
		char *pFirstCloseBrace = strchr(pInChoice, '}');
		assert(pFirstCloseBrace);

		estrClear(ppOutShardOrShardType);
		estrConcat(ppOutShardOrShardType, pInChoice + 1, pFirstCloseBrace - pInChoice - 1);
		estrCopy2(ppOutChoice, pFirstCloseBrace + 1);
		estrTrimLeadingAndTrailingWhitespace(ppOutChoice);
	}
	else
	{
		estrClear(ppOutShardOrShardType);
		estrCopy2(ppOutChoice, pInChoice);
	}
}

static bool OptionIsSet_WithFullName(ShardLauncherRun *pRun, char *pOptionName)
{
	return !!eaIndexedGetUsingString(&gpRun->ppChoices, pOptionName);
}

//takes in a "short" option name, checks if any version of it is set for this run
static bool OptionIsSetForClusteredRun(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pOptionName)
{
	if (OptionIsSet_WithFullName(gpRun, pOptionName))
	{
		return true;
	}

	if (OptionIsSet_WithFullName(gpRun, GetShardOrShardTypeSpecificOptionName(pOptionName, pShard->pShardName)))
	{
		return true;
	}

	if (OptionIsSet_WithFullName(gpRun, GetShardOrShardTypeSpecificOptionName(pOptionName, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pShard->eShardType))))
	{
		return true;
	}

	return false;
}



static bool ChoiceIsHighestPriorityForClusterShard(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, int iChoiceIndex)
{
	static char *pChoiceName_Raw = NULL;
	static char *pShardOrTypeName = NULL;

	static char *pNameWithShard = NULL;
	static char *pNameWithShardType = NULL;

	char *pChoiceName = gpRun->ppChoices[iChoiceIndex]->pConfigOptionName;

	SubdivideChoiceName(pChoiceName, &pChoiceName_Raw, &pShardOrTypeName);
	estrCopy2(&pNameWithShard, GetShardOrShardTypeSpecificOptionName(pChoiceName_Raw, pShard->pShardName));
	estrCopy2(&pNameWithShardType, GetShardOrShardTypeSpecificOptionName(pChoiceName_Raw, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pShard->eShardType)));

	if (stricmp(pChoiceName, pNameWithShard) == 0)
	{
		return true;
	}

	if (stricmp(pChoiceName, pNameWithShardType) == 0)
	{
		if (OptionIsSet_WithFullName(gpRun, pNameWithShard))
		{
			return false;
		}

		return true;
	}

	if (OptionIsSet_WithFullName(gpRun, pNameWithShard))
	{
		return false;
	}

	if (OptionIsSet_WithFullName(gpRun, pNameWithShardType))
	{
		return false;
	}

	return true;
}

//for a non-clustered shard, throw out all clusterspecific options. For a clustered runs, filters out options that apply to other shards,
//or lower priority options which are overriden by shard-specific ones
static bool ChoiceAppliesToCurrentShard(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, int iChoiceIndex)
{
	char *pChoiceName = gpRun->ppChoices[iChoiceIndex]->pConfigOptionName;

	if (gpRun->bClustered)
	{
		static char *pChoiceName_Raw = NULL;
		static char *pShardOrTypeName = NULL;

		estrClear(&pShardOrTypeName);

		SubdivideChoiceName(pChoiceName, &pChoiceName_Raw, &pShardOrTypeName);

		if (estrLength(&pShardOrTypeName))
		{
			if (stricmp(pShard->pShardName, pShardOrTypeName) == 0 
				|| stricmp(StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pShard->eShardType), pShardOrTypeName) == 0)
			{
				return ChoiceIsHighestPriorityForClusterShard(gpRun, pShard, iChoiceIndex);
			}
			else
			{
				return false;
			}
		}
		else
		{
			return ChoiceIsHighestPriorityForClusterShard(gpRun, pShard, iChoiceIndex);
		}
	}
	else
	{
		if (pChoiceName[0] == '{')
		{
			return false;
		}

		return true;
	}
}

char *GetNonZeroOptionByName_WithMacros(ShardLauncherClusterShard *pShard, char *pOptionName)
{
	char *pRaw = GetNonZeroOptionByName(pShard, pOptionName);

	if (pRaw)
	{
		static char *pFixedUp = NULL;
		estrCopy2(&pFixedUp, pRaw);
		ReplaceAllMacrosInString(&pFixedUp, gpRun, pShard, "GetNonZeroOptionByName_WithMacros");
		
		return pFixedUp;
	}

	return NULL;
}




static DWORD WINAPI RunTheShard_Unclustered( LPVOID lpParam )
{
	static char *pLogString = NULL;
	static char *pCommandLine = NULL;
	static char *pControllerCommandsFileName = NULL;
	static char *pRunTheShardBatchName = NULL;
	bool bTimedOut;
	int iRetVal;
	FILE *pFile;
	char fileName[CRYPTIC_MAX_PATH];
	char *pSafeRunName = NULL;
	char *pClientCommandLine = NULL;
	char *pPCLSpawnCmdLine = GetNonZeroOptionByName(NULL, "PCL_SPAWN_CMDLINE");
	char *pIgnorePCLFail = GetNonZeroOptionByName(NULL, "IGNORE_PCL_FAIL"); 
	GlobalType eOverrideType;

	int i, j;
	GlobalType eServerType;

	estrDestroy(&spGlobalCommandLine);
	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		estrDestroy(&sTypeInfo[i].pFirstCommandLine);
		estrDestroy(&sTypeInfo[i].pSharedCommandLine);
	}

	ea32Destroy(&spTypesToLaunch);

	if (!VerifyAndFixupLauncherRun(gpRun))
	{
		return false;
	}

	RunTheShard_Log("About to actually run the shard.");

	RunTheShard_Log("Loading ShardSetup file in case it's needed");
	if (spShardSetupFile)
	{
		StructDestroy(parse_MachineInfoForShardSetupList, spShardSetupFile);
		spShardSetupFile = NULL;
	}

	if (gpRun->pShardSetupFile && stricmp(gpRun->pShardSetupFile, "NONE") != 0)
	{
		spShardSetupFile = StructCreate(parse_MachineInfoForShardSetupList);
		if (!ParserReadTextFile(gpRun->pShardSetupFile, parse_MachineInfoForShardSetupList, spShardSetupFile, 0))
		{
			RunTheShard_LogFail("ShardSetupFile Loading failed");
			return false;
		}
	}


	RunTheShard_Log("About to create directory %s", gpRun->pDirectory);
	
	mkdirtree_const(STACK_SPRINTF("%s/foo.txt", gpRun->pDirectory));
	if (!dirExists(gpRun->pDirectory))
	{
		RunTheShard_LogFail("Couldn't create directory %s", gpRun->pDirectory);
		return false;
	}

	if (gRunType != RUNTYPE_LAUNCH)
	{
		char *pPCLDir = GetNonZeroOptionByName(NULL, "PCL_DIR");
		char *pPCL64Bit = GetNonZeroOptionByName(NULL, "PCL_64_BIT");
		char *pPCLNoUpdate = GetNonZeroOptionByName(NULL, "PCL_NOUPDATE");
		char *pPCL1stCmdLine = GetNonZeroOptionByName(NULL, "PCL_1ST_CMDLINE");
		const char *pBuildPatchClient = NULL;
		char *pPatchClient = NULL;

		RunTheShard_Log("About to try to patch %s %s from %s into %s",
			gpRun->pProductName, gpRun->pPatchVersion, gpRun->pPatchServer && gpRun->pPatchServer[0] ? gpRun->pPatchServer : "(unspecified patchserver)", gpRun->pDirectory);

		if (chdir(gpRun->pDirectory))
		{
			RunTheShard_LogFail("Couldn't chdir to %s", gpRun->pDirectory);
			return false;
		}

		estrStackCreate(&pPatchClient);
		if (pPCLDir)
			estrPrintf(&pPatchClient, "%s\\patchclient%s.exe", pPCLDir, "X64");
		// Note: Specifically avoid setting default options here.  patchclient may auto-update if -skipselfpatch is not passed.
		else if ((pBuildPatchClient = patchclientFullPath(!!pPCL64Bit)))
			estrCopy2(&pPatchClient, pBuildPatchClient);
		else
		{
			RunTheShard_LogFail("Can't find patchclient!  Try setting PCL_DIR.");
			return false;
		}

		estrClear(&pCommandLine);
		estrPrintf(&pCommandLine, "-project %sServer -name %s -sync %s",

			gpRun->pProductName, gpRun->pPatchVersion, 
			pPCLNoUpdate ? "-skipselfpatch ": "");
	
		if (gpRun->pPatchServer && gpRun->pPatchServer[0])
		{
			estrConcatf(&pCommandLine, " -server %s", gpRun->pPatchServer);
		}

		if (pPCL1stCmdLine)
		{
			estrConcatf(&pCommandLine, " %s ", pPCL1stCmdLine);
		}

		backSlashes(pCommandLine);

		for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
		{
			static char *pExeName = NULL;
			static char *pPdbName = NULL;
			estrClear(&pExeName);
			estrGetDirAndFileName(gpRun->ppOverrideExecutableNames[i], NULL, &pExeName);
			estrCopy(&pPdbName, &pExeName);
			estrReplaceOccurrences(&pPdbName, ".exe", ".pdb");

			estrConcatf(&pCommandLine, " -hide %s -hide %s ", GetShortExecutableName(pExeName, true), pPdbName);
		}

			



		SetPatchVersionOnCB(gpRun, NULL, pCommandLine);

		estrInsertf(&pCommandLine, 0, "%s ", pPatchClient);
		estrDestroy(&pPatchClient);

		gStatusReportingState = STATUS_SHARDLAUNCHER_PATCHING;

		ExecuteSystemCommand(pCommandLine, 30 * 60 * 60, &bTimedOut, &iRetVal);

		gStatusReportingState = STATUS_SHARDLAUNCHER_LAUNCHING;

		if (bTimedOut)
		{
			RunTheShard_LogFail("timed out (more than 30 minutes) while patching locally");
			return false;
		}

		if (iRetVal)
		{
			RunTheShard_LogFail("Patching locally failed with return code %d", iRetVal);
			return false;
		}
	}

	if (gRunType != RUNTYPE_PATCH)
	{
		if (eaSize(&gpRun->ppOverrideExecutableNames))
		{
			RunTheShard_Log("About to copy override executables...");

			for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
			{
				static char *pSystemString = NULL;
				static char *pPdbName = NULL;

				RunTheShard_Log("About to copy %s", gpRun->ppOverrideExecutableNames[i]);
				estrPrintf(&pSystemString, "copy %s %s\\%sServer\\%s", gpRun->ppOverrideExecutableNames[i], gpRun->pDirectory, gpRun->pProductName, GetShortExecutableName(gpRun->ppOverrideExecutableNames[i], true));

				ExecuteSystemCommand(pSystemString, 60, &bTimedOut, &iRetVal);

				if (bTimedOut)
				{
					RunTheShard_LogFail("Copying %s timed out", gpRun->ppOverrideExecutableNames[i]);
					return false;
				}

				if (iRetVal)
				{
					RunTheShard_LogFail("Copying %s failed", gpRun->ppOverrideExecutableNames[i]);
					return false;
				}

				estrCopy2(&pPdbName, gpRun->ppOverrideExecutableNames[i]);
				estrReplaceOccurrences(&pPdbName, ".exe", ".pdb");

				if (fileExists(pPdbName))
				{
					RunTheShard_Log("Also copying %s\n", pPdbName);
					estrPrintf(&pSystemString, "copy %s %s\\%sServer", pPdbName, gpRun->pDirectory, gpRun->pProductName);

					ExecuteSystemCommand(pSystemString, 60, &bTimedOut, &iRetVal);

					if (bTimedOut)
					{
						RunTheShard_LogFail("Copying %s timed out", pPdbName);
						return false;
					}

					if (iRetVal)
					{
						RunTheShard_LogFail("Copying %s failed", pPdbName);
						return false;
					}
				}
				else
				{
					RunTheShard_LogWarning("Note: %s does not exist\n", pPdbName);
				}
			}
		}
		else
		{
			RunTheShard_Log("No override executables");
		}

	
		for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
		{
			for (j=0; j < eaSize(&gpRun->pOptionLibrary->ppLists[i]->ppOptions); j++)
			{
				gpRun->pOptionLibrary->ppLists[i]->ppOptions[j]->bSet = false;
			}
		}
		

		if (gpRun->pTemplateFileName)
		{
			RunTheShard_Log("About to process options from template file %s", gpRun->pTemplateFileName);
		}
		for (i=0; i < eaSize(&gpRun->ppTemplateChoices); i++)
		{
			if (eaIndexedGetUsingString(&gpRun->ppChoices, gpRun->ppTemplateChoices[i]->pConfigOptionName))
			{
				RunTheShard_Log("Skipping option %s from template, it is overridden", gpRun->ppTemplateChoices[i]->pConfigOptionName);
			}
			else
			{	
				ShardLauncherConfigOption *pOption = FindConfigOption(gpRun, gpRun->ppTemplateChoices[i]->pConfigOptionName, &eOverrideType);
				
				RunTheShard_Log("Processing option %s", gpRun->ppTemplateChoices[i]->pConfigOptionName);
			

				if (!pOption)
				{
					if (!OptionIsDeprecated(gpRun->ppTemplateChoices[i]->pConfigOptionName))
					{
						RunTheShard_LogWarning("Couldn't find config option named %s which wants to be set to value %s", 
							gpRun->ppTemplateChoices[i]->pConfigOptionName, gpRun->ppTemplateChoices[i]->pValue);
					}
				}
				else
				{
					pOption->bSet = true;
					if (!ApplyStuffFromConfigOption(gpRun, NULL, pOption, gpRun->ppTemplateChoices[i]->pValue, eOverrideType))
					{
						return false;
					}
				}
			}
		}


		RunTheShard_Log("About to process overridden options");
		for (i=0; i < eaSize(&gpRun->ppChoices); i++)
		{
			if (ChoiceAppliesToCurrentShard(gpRun, NULL, i))
			{
				ShardLauncherConfigOption *pOption = FindConfigOption(gpRun, gpRun->ppChoices[i]->pConfigOptionName, &eOverrideType);
			
				RunTheShard_Log("Processing option %s", gpRun->ppChoices[i]->pConfigOptionName);
		

				if (!pOption)
				{
					if (!OptionIsDeprecated(gpRun->ppChoices[i]->pConfigOptionName))
					{
						RunTheShard_LogWarning("Couldn't find config option named %s which wants to be set to value %s", 
							gpRun->ppChoices[i]->pConfigOptionName, gpRun->ppChoices[i]->pValue);
					}
				}
				else
				{
					pOption->bSet = true;
					if (!ApplyStuffFromConfigOption(gpRun, NULL, pOption, gpRun->ppChoices[i]->pValue, eOverrideType))
					{
						return false;
					}
				}
			}
		}

	
		for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
		{
			for (j=0; j < eaSize(&gpRun->pOptionLibrary->ppLists[i]->ppOptions); j++)
			{
				if (!gpRun->pOptionLibrary->ppLists[i]->ppOptions[j]->bSet)
				{
					ApplyStuffFromConfigOption(gpRun, NULL, gpRun->pOptionLibrary->ppLists[i]->ppOptions[j], NULL, GLOBALTYPE_NONE);
				}
			}
		}
		

		estrCopy2(&pSafeRunName, gpRun->pRunName);
		estrMakeAllAlphaNumAndUnderscores(&pSafeRunName);

		estrPrintf(&pControllerCommandsFileName, "%s/ControllerCommands_%s.txt", gpRun->pDirectory, pSafeRunName);
		estrReplaceOccurrences(&pControllerCommandsFileName, " ", "_");


		RunTheShard_Log("About to create %s", pControllerCommandsFileName);
		pFile = fopen(pControllerCommandsFileName, "wt");
		if (!pFile)
		{
			RunTheShard_LogFail("Couldn't open %s", pControllerCommandsFileName);
			return false;
		}



		for (eServerType = 0; eServerType < GLOBALTYPE_MAXTYPES; eServerType++)
		{
			if (eServerType != GLOBALTYPE_CONTROLLER && eServerType != GLOBALTYPE_CLIENT && eServerType != GLOBALTYPE_LAUNCHER)
			{
				bool bHide = GetNonZeroOptionByName(NULL, GetServerTypeSpecificOptionName("HIDE_CONSOLES", eServerType)) != NULL;
				bool bShow = GetNonZeroOptionByName(NULL, GetServerTypeSpecificOptionName("SHOW_CONSOLES", eServerType)) != NULL;
				if (sTypeInfo[eServerType].pSharedCommandLine || bHide || bShow )
				{
					fprintf(pFile, "Command APPENDSERVERTYPECOMMANDLINE %s\n{\n\tScriptString %s\n",
						GlobalTypeToName(eServerType), 
						GetQuotedString(sTypeInfo[eServerType].pSharedCommandLine ? sTypeInfo[eServerType].pSharedCommandLine : ""));
				
					if (bHide || bShow)
					{
						fprintf(pFile, "\tStartHidden %d\n", bHide);
					}
						
					fprintf(pFile, "}\n");
				}
			}
		}

		//process all "[serverType] LAUNCH" and "[serverType] NO_NORMAL_LAUNCH" options
		for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			if (GetNonZeroOptionByName(NULL, GetServerTypeSpecificOptionName("LAUNCH", i)))
			{
				ea32PushUnique(&spTypesToLaunch, i);
			}

			if (GetNonZeroOptionByName(NULL, GetServerTypeSpecificOptionName("NO_NORMAL_LAUNCH", i)))
			{
				ea32FindAndRemove(&spTypesToLaunch, i);
			}
		}

		//first go through everything in our option library launch order. Then everything else
	
		for (i=0; i < ea32Size(&gpRun->pOptionLibrary->pLaunchOrderList); i++)
		{
			GlobalType eTypeToLaunch = gpRun->pOptionLibrary->pLaunchOrderList[i];
			if (ea32Find(&spTypesToLaunch, eTypeToLaunch) != -1)
			{
				char *pCountString;
				fprintf(pFile, "Command LAUNCH_NORMALLY %s\n{\n", GlobalTypeToName(eTypeToLaunch));
				if (sTypeInfo[eTypeToLaunch].pFirstCommandLine)
				{
					fprintf(pFile, "\tExtraCommandLine %s\n", GetQuotedString(sTypeInfo[eTypeToLaunch].pFirstCommandLine));
				}

				pCountString = GetNonZeroOptionByName(NULL, GetServerTypeSpecificOptionName("LAUNCH_COUNT", eTypeToLaunch));
				if (pCountString)
				{
					fprintf(pFile, "\tCount %d\n", atoi(pCountString));
				}

				fprintf(pFile, "}\n");

				//special case... immediately after launching logserver is when we do the AUTO_SETTINGS pause stuff
				if (eTypeToLaunch == GLOBALTYPE_LOGSERVER)
				{
					MaybeAddAutoSettingsPauseStuff(NULL, pFile);
				}
			}
		}
		

		for (i=0; i < ea32Size(&spTypesToLaunch); i++)
		{
			GlobalType eTypeToLaunch = spTypesToLaunch[i];
			if (ea32Find((U32**)&gpRun->pOptionLibrary->pLaunchOrderList, eTypeToLaunch) == -1)
			{
				char *pCountString;
				fprintf(pFile, "Command LAUNCH_NORMALLY %s\n{\n", GlobalTypeToName(spTypesToLaunch[i]));
				if (sTypeInfo[spTypesToLaunch[i]].pFirstCommandLine)
				{
					fprintf(pFile, "\tExtraCommandLine %s\n", GetQuotedString(sTypeInfo[spTypesToLaunch[i]].pFirstCommandLine));
				}
					
				pCountString = GetNonZeroOptionByName(NULL, GetServerTypeSpecificOptionName("LAUNCH_COUNT", spTypesToLaunch[i]));
	
				if (pCountString)
				{
					fprintf(pFile, "\tCount %d\n", atoi(pCountString));
				}
				fprintf(pFile, "}\n");

				//special case... immediately after launching logserver is when we do the AUTO_SETTINGS pause stuff
				if (eTypeToLaunch == GLOBALTYPE_LOGSERVER)
				{
					MaybeAddAutoSettingsPauseStuff(NULL, pFile);
				}
			}
		}

		if (gpRun->pControllerCommandsArbitraryText)
		{
			fprintf(pFile, "\n#From here down, this comes from the arbitrary user-set text\n%s\n",
				gpRun->pControllerCommandsArbitraryText);
		}

		fclose(pFile);
		RunTheShard_Log("%s created succcessfully", pControllerCommandsFileName);
		

		estrPrintf(&pRunTheShardBatchName, "%s/RunTheShard_%s.bat", gpRun->pDirectory, pSafeRunName);
		estrReplaceOccurrences(&pRunTheShardBatchName, " ", "_");

		RunTheShard_Log("About to create %s", pRunTheShardBatchName);
		pFile = fopen(pRunTheShardBatchName, "wt");
		if (!pFile)
		{
			RunTheShard_LogFail("Couldn't open %s", pRunTheShardBatchName);
			return false;
		}
		fprintf(pFile, "REM Auto-created batch file for shard type %s product %s at %s\n",
			gpRun->pRunName, gpRun->pProductName, timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
		sprintf(fileName, "%s/%sServer", gpRun->pDirectory, gpRun->pProductName);
		backSlashes(fileName);
		
		fprintf(pFile, "cd %s\n", fileName);

		if (GetNonZeroOptionByName(NULL, "ENABLE_UGC"))
		{
			fprintf(pFile, "rd /s /q .\\data\\ns\n");
		}

		fprintf(pFile, "start controller.exe %s %s -cookie %u -ShardName %s -KillAllOtherShardExesAtStartup -SetProductName %s %s -ScriptFile %s -ProductionShardMode 1 -execDir %s -coreExecDir %s - %s - %s - %s",
			GetOverlordSnippet(),
			gRunType == RUNTYPE_LAUNCH ? "-DontPatchOtherMachines" : "",
			timeSecondsSince2000(),
			GetNonZeroOptionByName_WithMacros(NULL, "SHARD_NAME"),
			gpRun->pProductName, gpRun->pShortProductName, pControllerCommandsFileName, fileName, fileName,
			spGlobalCommandLine ? spGlobalCommandLine : "",
			sTypeInfo[GLOBALTYPE_CONTROLLER].pFirstCommandLine ? sTypeInfo[GLOBALTYPE_CONTROLLER].pFirstCommandLine : "",
			sTypeInfo[GLOBALTYPE_CONTROLLER].pSharedCommandLine ? sTypeInfo[GLOBALTYPE_CONTROLLER].pSharedCommandLine : "");

		if (!gpRun->pShardSetupFile || gpRun->pShardSetupFile[0] == 0 || stricmp(gpRun->pShardSetupFile, "NONE") == 0)
		{

		}
		else
		{
			fprintf(pFile, " -UseSentryServer 1 -ShardSetupFile %s", gpRun->pShardSetupFile);
		}

		if (sTypeInfo[GLOBALTYPE_CLIENT].pFirstCommandLine || sTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine)
		{
			char *pSuperEscapedClientCommandLine = NULL;


			estrPrintf(&pClientCommandLine, "%s - %s", sTypeInfo[GLOBALTYPE_CLIENT].pFirstCommandLine ? sTypeInfo[GLOBALTYPE_CLIENT].pFirstCommandLine : "", 
				sTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine ? sTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine : "");

			estrSuperEscapeString(&pSuperEscapedClientCommandLine, pClientCommandLine);
			fprintf(pFile, " -SuperEsc AutoPatchedClientCommandLine %s ", pSuperEscapedClientCommandLine);
			estrDestroy(&pSuperEscapedClientCommandLine);
		}

		if (spGlobalCommandLine && spGlobalCommandLine[0])
		{
			char *pSuperEscapedGlobalCommandLine = NULL;

			estrSuperEscapeString(&pSuperEscapedGlobalCommandLine, spGlobalCommandLine);
			fprintf(pFile, " -SuperEsc GlobalSharedCommandLine %s", pSuperEscapedGlobalCommandLine);
			estrDestroy(&pSuperEscapedGlobalCommandLine);
		}

		if (pPCLSpawnCmdLine || pIgnorePCLFail)
		{
			char *pTemp = NULL;
			char *pSuperEscapedSpawnCmdLine = NULL;

			estrConcatf(&pTemp, "%s %s", pPCLSpawnCmdLine ? pPCLSpawnCmdLine : "", pIgnorePCLFail ? "-DoExecuteEvenOnFail" : "");
			estrSuperEscapeString(&pSuperEscapedSpawnCmdLine, pTemp);
			fprintf(pFile, " -SuperEsc SpawnPatchCmdLine %s", pSuperEscapedSpawnCmdLine);
			estrDestroy(&pSuperEscapedSpawnCmdLine);
			estrDestroy(&pTemp);
		}

		if (sTypeInfo[GLOBALTYPE_LAUNCHER].pSharedCommandLine && sTypeInfo[GLOBALTYPE_LAUNCHER].pSharedCommandLine[0])
		{
			char *pSuperEscapedLaunchernCmdLine = NULL;

			estrSuperEscapeString(&pSuperEscapedLaunchernCmdLine, sTypeInfo[GLOBALTYPE_LAUNCHER].pSharedCommandLine);
			fprintf(pFile, " -SuperEsc SetLauncherCommandLine %s", pSuperEscapedLaunchernCmdLine);
			estrDestroy(&pSuperEscapedLaunchernCmdLine);
		}

		fprintf(pFile, " %%1 %%2 %%3 %%4 %%5 %%6 %%7 %%8 %%9\nexit\n");

		if (pClientCommandLine)
		{
			fprintf(pFile, "REM AutoPatchecClientCommandLine: %s\n", pClientCommandLine);
			estrDestroy(&pClientCommandLine);
		}
		if (spGlobalCommandLine && spGlobalCommandLine[0])
		{
			fprintf(pFile, "REM GlobalComandLine: %s\n", spGlobalCommandLine);
		}

		fclose(pFile);

		if (eaSize(&sppAutoSettingInits))
		{
			if (!ProcessAutoSettingInits(gpRun))
			{
				return false;
			}			
		}



		RunTheShard_Log("%s created successfully... time to run it. Cross your fingers. You heard me, cross them!",
			pRunTheShardBatchName);

		backSlashes(pRunTheShardBatchName);
		system_detach(pRunTheShardBatchName, 0, 0);
	}

	RunTheShard_LogSucceed("Done... (Shardlauncher will close in 30 seconds)");
	Sleep(30000);
	exit(0);

	return true;
}

bool StartShardPatching(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard)
{
	char *pPCLDir = GetNonZeroOptionByName(pShard, "PCL_DIR");
	char *pPCL64Bit = GetNonZeroOptionByName(pShard, "PCL_64_BIT");
	char *pPCLNoUpdate = GetNonZeroOptionByName(pShard, "PCL_NOUPDATE");
	char *pPCL1stCmdLine = GetNonZeroOptionByName(pShard, "PCL_1ST_CMDLINE");
	const char *pBuildPatchClient = NULL;
	char *pPatchClient = NULL;

	char patchingTaskName[1024];

	static char localIP[32] = "";

	int i;

	if (!localIP[0])
	{
		strcpy(localIP, makeIpStr(getHostLocalIp()));
	}


	RunTheShard_Log("About to try to patch %s %s from %s into %s on machine %s",
		gpRun->pProductName, gpRun->pPatchVersion, gpRun->pPatchServer && gpRun->pPatchServer[0] ? gpRun->pPatchServer : "(unspecified patchserver)", 
		pShard->pDirectory_FixedUp, pShard->pMachineName);

	SentryServerComm_KillProcess_1Machine(pShard->pMachineName, "PatchClient.exe", NULL);
	SentryServerComm_KillProcess_1Machine(pShard->pMachineName, "PatchClientX64.exe", NULL);

	estrStackCreate(&pPatchClient);
	if (pPCLDir)
		estrPrintf(&pPatchClient, "%s\\patchclient%s.exe", pPCLDir, "X64");
	// Note: Specifically avoid setting default options here.  patchclient may auto-update if -skipselfpatch is not passed.
	else if ((pBuildPatchClient = patchclientFullPath(!!pPCL64Bit)))
		estrCopy2(&pPatchClient, pBuildPatchClient);
	else
	{
		RunTheShard_LogFail("Can't find patchclient!  Try setting PCL_DIR.");
		return false;
	}

	estrClear(&pShard->pPatchingCommandLine);
	estrPrintf(&pShard->pPatchingCommandLine, "-project %sServer -name %s -sync %s",

		gpRun->pProductName, gpRun->pPatchVersion, 
		pPCLNoUpdate ? "-skipselfpatch ": "");
	
	if (gpRun->pPatchServer && gpRun->pPatchServer[0])
	{
		estrConcatf(&pShard->pPatchingCommandLine, " -server %s", gpRun->pPatchServer);
	}

	if (pPCL1stCmdLine)
	{
		estrConcatf(&pShard->pPatchingCommandLine, " %s ", pPCL1stCmdLine);
	}

	backSlashes(pShard->pPatchingCommandLine);

	for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
	{
		static char *pExeName = NULL;
		static char *pPdbName = NULL;
		estrClear(&pExeName);
		estrGetDirAndFileName(gpRun->ppOverrideExecutableNames[i], NULL, &pExeName);
		estrCopy(&pPdbName, &pExeName);
		estrReplaceOccurrences(&pPdbName, ".exe", ".pdb");

		estrConcatf(&pShard->pPatchingCommandLine, " -hide %s -hide %s ", GetShortExecutableName(pExeName, true), pPdbName);
	}


	SetPatchVersionOnCB(gpRun, pShard, pShard->pPatchingCommandLine);

	estrInsertf(&pShard->pPatchingCommandLine, 0, "%s ", pPatchClient);
	estrDestroy(&pPatchClient);

	estrInsertf(&pShard->pPatchingCommandLine, 0, "WORKINGDIR(%s)", pShard->pDirectory_FixedUp);

	//machine name should always be first
	sprintf(patchingTaskName, "%s:%s:Controller", pShard->pMachineName, gpRun->pClusterName);

	estrConcatf(&pShard->pPatchingCommandLine, " -beginPatchStatusReporting \"%s\" localhost %d -beginpatchstatusreporting_critical \"%s\" %s %d ",
		patchingTaskName, DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT,
		patchingTaskName, localIP, SHARDLAUNCHER_PATCHSTATUS_PORT);

	estrConcatf(&pShard->pPatchingCommandLine, " -beginPatchStatusReporting \"%s\" %s %d ",
		patchingTaskName, localIP,
		CLUSTERCONTROLLER_PATCHSTATUS_PORT);

	SentryServerComm_RunCommand_1Machine(pShard->pMachineName, pShard->pPatchingCommandLine);

	AddPatchingTaskForRestarting(patchingTaskName, pShard->pMachineName, pShard->pPatchingCommandLine);
	PCLStatusMonitoring_Add(patchingTaskName);


	return true;
}

static int siSucceedCount = 0;
static int siFailCount = 0;
/*
void RunTheShardPatchCB(PCLStatusMonitoringUpdate *pUpdate)
{
	switch (pUpdate->internalStatus.eState)
	{
	case PCLSMS_SUCCEEDED:
		RunTheShard_Log("Patching has succeeded for %s", pUpdate->internalStatus.pMyIDString);
		siSucceedCount++;
		break;


	case PCLSMS_FAILED:
		RunTheShard_LogFail("Patching has failed for %s", pUpdate->internalStatus.pMyIDString);
		siFailCount++;
		break;
	
	case PCLSMS_FAILED_TIMEOUT:
		RunTheShard_LogFail("Patching has timed out fatally for %s", pUpdate->internalStatus.pMyIDString);
		siFailCount++;
		break;
	}


}*/



bool StartLoggingPatching(void)
{
	const char *pBuildPatchClient = NULL;
	char *pPatchClient = NULL;

	char patchingTaskName[1024];
	char *pLoggingPatchingCmdLine = NULL;

	static char localIP[32] = "";

	if (!localIP[0])
	{
		strcpy(localIP, makeIpStr(getHostLocalIp()));
	}


	RunTheShard_Log("About to try to patch %s %s from %s into %s on machine %s",
		gpRun->pProductName, gpRun->pPatchVersion, gpRun->pPatchServer && gpRun->pPatchServer[0] ? gpRun->pPatchServer : "(unspecified patchserver)", 
		gpRun->pLogServerDir, gpRun->pLogServerAndParserMachineName);


	estrPrintf(&pLoggingPatchingCmdLine, "PatchClientX64.exe -project %sServerMini -name %s -sync ",
		gpRun->pProductName, gpRun->pPatchVersion);
	
	if (gpRun->pPatchServer && gpRun->pPatchServer[0])
	{
		estrConcatf(&pLoggingPatchingCmdLine, " -server %s", gpRun->pPatchServer);
	}

	backSlashes(pLoggingPatchingCmdLine);

	estrInsertf(&pLoggingPatchingCmdLine, 0, "WORKINGDIR(%s)", gpRun->pLogServerDir);

	//machine name should always be first
	sprintf(patchingTaskName, "%s:%s:Logging", gpRun->pLogServerAndParserMachineName, gpRun->pClusterName);

	estrConcatf(&pLoggingPatchingCmdLine, " -beginPatchStatusReporting \"%s\" localhost %d -beginpatchstatusreporting_critical \"%s\" %s %d ",
		patchingTaskName, DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT,
		patchingTaskName, localIP, SHARDLAUNCHER_PATCHSTATUS_PORT);

	estrConcatf(&pLoggingPatchingCmdLine, " -beginPatchStatusReporting \"%s\" %s %d ",
		patchingTaskName, localIP,
		CLUSTERCONTROLLER_PATCHSTATUS_PORT);

	AddPatchingTaskForRestarting(patchingTaskName, gpRun->pLogServerAndParserMachineName, pLoggingPatchingCmdLine);

	SentryServerComm_RunCommand_1Machine(gpRun->pLogServerAndParserMachineName, pLoggingPatchingCmdLine);
	PCLStatusMonitoring_Add(patchingTaskName);

	estrDestroy(&pLoggingPatchingCmdLine);

	return true;
}

static void Tick(void)
{
	Sleep(5);
}

static char *spDirectoryContentsString = NULL;
static bool sbDirectoryContentGettingFailed = false;

static void CopyDirectoryContentsCB(const char *pMachineName, char *pDirName, void *pUserData, char *pFiles, bool bFailed)
{
	if (bFailed)
	{
		sbDirectoryContentGettingFailed = true;
	}
	else
	{
		spDirectoryContentsString = strdup(pFiles ? pFiles : "");
	}
}

#define SEND(pFile) { estrSetSize(&pLocalFull, iLocalDirNameLength); estrSetSize(&pRemoteFull, iRemoteDirNameLength);		\
	estrConcatf(&pLocalFull, "\\%s", pFile); estrConcatf(&pRemoteFull, "\\%s", pFile); SentryServerComm_SendFileDeferred(pSendHandle, pMachineName, pLocalFull, pRemoteFull); }

void RemoveDirAndSortFileNames(char ***pppInNames, char ***pppOutNames, char *pDirName)
{
	int iNameLen = strlen(pDirName);
	int i;
	int iInSize = eaSize(pppInNames);

	for (i = 0; i < iInSize; i++)
	{
		char *pInName = (*pppInNames)[i];
		char *pOutName;
		pInName += iNameLen;
		while (*pInName == '/' || *pInName == '\\')
		{
			pInName++;
		}

		pOutName = strdup(pInName);

		if (!(pOutName && pOutName[0]))
		{
			assertmsgf(0, "Something went wrong when making short file name for %s in dir %s", 
				(*pppInNames)[i], pDirName);
		}

		backSlashes(pOutName);
		eaPush(pppOutNames, pOutName);
	}

	eaQSort(*pppOutNames, strCmp);

}

//for a "frankenbuild" copy, two things are different: (1) don't recurse, (2) ignore all files that aren't of the form a_b.exe or a_b.pdb
typedef bool (*CopyDirectoryContents_FilterCB)(char *pFileName);

static void CopyDirectoryContents(SentryServerCommDeferredSendHandle *pSendHandle, char *pMachineName, char *pLocalDirName, char *pRemoteDirName, bool bDontRecurse, CopyDirectoryContents_FilterCB pFilterCB)
{
	char **ppRemoteFiles_WithDir = NULL;
	char **ppLocalFiles_WithDir = NULL;
	char **ppRemoteFiles_NoDir = NULL;
	char **ppLocalFiles_NoDir = NULL;
	
	char *pLocalFull = NULL;
	char *pRemoteFull = NULL;
	char *pEraseString = NULL;

	int iLocalDirNameLength, iRemoteDirNameLength;

	estrStackCreate(&pLocalFull);
	estrStackCreate(&pRemoteFull);

	estrCopy2(&pLocalFull, pLocalDirName);
	estrCopy2(&pRemoteFull, pRemoteDirName);

	iLocalDirNameLength = estrLength(&pLocalFull);
	iRemoteDirNameLength = estrLength(&pRemoteFull);

	SAFE_FREE(spDirectoryContentsString);
	sbDirectoryContentGettingFailed = false;

	RunTheShard_Log("Querying machine %s for contents of directory %s so we can clone it properly",
		pMachineName, pRemoteDirName);

	if (bDontRecurse)
	{
		SentryServerComm_GetDirectoryContents(pMachineName, STACK_SPRINTF("%s%s", GETDIRCONTENTS_PREFIX_NORECURSE, pRemoteDirName), CopyDirectoryContentsCB, NULL);
	}
	else
	{
		SentryServerComm_GetDirectoryContents(pMachineName, pRemoteDirName, CopyDirectoryContentsCB, NULL);
	}

	do
	{
		Tick();
	}
	while (!sbDirectoryContentGettingFailed && !spDirectoryContentsString);

	if (sbDirectoryContentGettingFailed)
	{
		RunTheShard_LogFail("Something went wrong while querying machine %s for contents of directory %s",
			pMachineName, pRemoteDirName);
		return;
	}

	if (NO_STR(spDirectoryContentsString))
	{
		//do nothing, leave ppRemoteFiles_WithDir empty
	}
	else
	{
		DivideString(spDirectoryContentsString, ";", &ppRemoteFiles_WithDir, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	}

	printf("Found %d files", eaSize(&ppRemoteFiles_WithDir));

	if (pFilterCB)
	{
		int i;

		for (i = eaSize(&ppRemoteFiles_WithDir) - 1; i >= 0; i--)
		{
			if (!pFilterCB(ppRemoteFiles_WithDir[i]))
			{
				free(eaRemove(&ppRemoteFiles_WithDir, i));
			}
		}

		printf("%d after filtering", eaSize(&ppRemoteFiles_WithDir));

	}

	RemoveDirAndSortFileNames(&ppRemoteFiles_WithDir, &ppRemoteFiles_NoDir, pRemoteDirName);

	if (bDontRecurse)
	{
		ppLocalFiles_WithDir = fileScanDirFoldersNoSubdirRecurse(pLocalDirName, FSF_FILES);
	}
	else
	{
		ppLocalFiles_WithDir = fileScanDir(pLocalDirName);
	}
	printf("%d files locally in %s", eaSize(&ppLocalFiles_WithDir), pLocalDirName);


	if (pFilterCB)
	{
		int i;

		for (i = eaSize(&ppLocalFiles_WithDir) - 1; i >= 0; i--)
		{
			if (!pFilterCB(ppLocalFiles_WithDir[i]))
			{
				free(eaRemove(&ppLocalFiles_WithDir, i));
			}
		}

		printf("%d after filtering", eaSize(&ppLocalFiles_WithDir));

	}

	RemoveDirAndSortFileNames(&ppLocalFiles_WithDir, &ppLocalFiles_NoDir, pLocalDirName);

	while (eaSize(&ppLocalFiles_NoDir) && eaSize(&ppRemoteFiles_NoDir))
	{
		char *pLocalFile = ppLocalFiles_NoDir[0];
		char *pRemoteFile = ppRemoteFiles_NoDir[0];
		int iCmp = stricmp(pLocalFile, pRemoteFile);

		if (iCmp == 0) //file exists both places... need to do deferred send in case it's different
		{
			printf("%s exist both places... sending it\n", pLocalFile);
			SEND(pLocalFile);
			free(eaRemove(&ppLocalFiles_NoDir, 0));
			free(eaRemove(&ppRemoteFiles_NoDir, 0));
		}
		else if (iCmp < 0) //local file doesn't exist remotely, need to send it
		{
			printf("%s exists locally but not remotely, sending it\n", pLocalFile);
			SEND(pLocalFile);
			free(eaRemove(&ppLocalFiles_NoDir, 0));
		}
		else //file exists remotely only... delete it
		{
			printf("%s exists only remotely... deleting it\n", pRemoteFile);
			if (!pEraseString)
			{
				estrPrintf(&pEraseString, "cmd /c erase ");
			}
			estrConcatf(&pEraseString, " %s\\%s", pRemoteDirName, pRemoteFile);
			
			free(eaRemove(&ppRemoteFiles_NoDir, 0));
		}
	}

	while (eaSize(&ppLocalFiles_NoDir))
	{
		char *pLocal = eaRemove(&ppLocalFiles_NoDir, 0);
		assert(pLocal && pLocal[0]);
		printf("%s exists locally but not remotely, sending it\n", pLocal);
		SEND(pLocal);
		free(pLocal);
	}

	while (eaSize(&ppRemoteFiles_NoDir))
	{
		char *pRemote = eaRemove(&ppRemoteFiles_NoDir, 0);
		assert(pRemote && pRemote[0]);
		printf("%s exists only remotely... deleting it\n", pRemote);
		if (!pEraseString)
		{
			estrPrintf(&pEraseString, "cmd /c erase ");
		}
		estrConcatf(&pEraseString, " %s\\%s", pRemoteDirName, pRemote);
			
		free(pRemote);
	}
	
	if (pEraseString)
	{
		SentryServerComm_RunCommand_1Machine(pMachineName, pEraseString);
		estrDestroy(&pEraseString);
	}

	eaDestroyEx(&ppRemoteFiles_NoDir, NULL);
	eaDestroyEx(&ppLocalFiles_NoDir, NULL);	
	eaDestroyEx(&ppRemoteFiles_WithDir, NULL);
	fileScanDirFreeNames(ppLocalFiles_WithDir);
	estrDestroy(&pLocalFull);
	estrDestroy(&pRemoteFull);
}

//should return true if this is a frankenbuild filename, false otherwise
bool FrankenBuildDirCB(char *pFileName)
{
	static char *pShortName = NULL;
	estrClear(&pShortName);
	estrGetDirAndFileName(pFileName, NULL, &pShortName);
	if (strchr(pShortName, '_') && (strEndsWith(pShortName, ".exe") || strEndsWith(pShortName, ".pdb")))
	{
		return true;
	}

	return false;
}

static void TransferFiles(	SentryServerCommDeferredSendHandle *pSendHandle, ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard)
{
	char localFileName[CRYPTIC_MAX_PATH];
	char remoteFileName[CRYPTIC_MAX_PATH];

	int i;


	RunTheShard_Log("Copying shardSetupFile %s.txt to %s", pShard->pShardSetupFileName, pShard->pMachineName);
	sprintf(localFileName, "c:\\ShardSetupFiles\\%s.txt", pShard->pShardSetupFileName);
	sprintf(remoteFileName, "c:\\ShardSetupFiles\\%s.txt", pShard->pShardSetupFileName);

	SentryServerComm_SendFileDeferred(pSendHandle, pShard->pMachineName, localFileName, remoteFileName);

	Tick();

	RunTheShard_Log("Copying batch and command files to %s", pShard->pMachineName);
	SentryServerComm_SendFileDeferred(pSendHandle, pShard->pMachineName, pShard->pLocalBatchFileName, pShard->pRemoteBatchFileName);
	SentryServerComm_SendFileDeferred(pSendHandle, pShard->pMachineName, pShard->pLocalCommandFileName, pShard->pRemoteCommandFileName);
	Tick();

	RunTheShard_Log("Cloning template data directory (%s) into /data for shard %s", gpRun->pLocalDataTemplateDir, pShard->pShardName);
	sprintf(remoteFileName, "%s\\%sServer\\data", pShard->pDirectory_FixedUp, gpRun->pProductName);
	CopyDirectoryContents(pSendHandle, pShard->pMachineName, gpRun->pLocalDataTemplateDir, remoteFileName, false, NULL);

	if (eaSize(&gpRun->ppOverrideExecutableNames))
	{
		RunTheShard_Log("About to copy override executables...");

		for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
		{
			static char *pRemoteName = NULL;

			//for pdb files, we don't change the name from gameserverFD.pdb to gameserver.pdb, for .exe files we do, so
			//we need two versions of this
			static char *pRemoteName_NoFDFixup = NULL;
			static char *pPDBName = NULL;
			static char *pRemotePDBName = NULL;

			estrPrintf(&pRemoteName, "%s\\%sServer\\%s", pShard->pDirectory_FixedUp, gpRun->pProductName, GetShortExecutableName(gpRun->ppOverrideExecutableNames[i], true));
			estrPrintf(&pRemoteName_NoFDFixup, "%s\\%sServer\\%s", pShard->pDirectory_FixedUp, gpRun->pProductName, GetShortExecutableName(gpRun->ppOverrideExecutableNames[i], false));
			RunTheShard_Log("About to send %s through sentry server", gpRun->ppOverrideExecutableNames[i]);

			SentryServerComm_SendFileDeferred(pSendHandle, pShard->pMachineName, gpRun->ppOverrideExecutableNames[i], pRemoteName);
			Tick();

			estrCopy2(&pPDBName, gpRun->ppOverrideExecutableNames[i]);
			estrCopy2(&pRemotePDBName, pRemoteName_NoFDFixup);

			estrReplaceOccurrences(&pPDBName, ".exe", ".pdb");
			estrReplaceOccurrences(&pRemotePDBName, ".exe", ".pdb");

			if (fileExists(pPDBName))
			{
				RunTheShard_Log("Also sending %s\n", pPDBName);
				SentryServerComm_SendFileDeferred(pSendHandle, pShard->pMachineName, pPDBName, pRemotePDBName);
				Tick();			
			}
			else
			{
				RunTheShard_LogWarning("Note: %s does not exist\n", pPDBName);

			}
		}
	}
	else
	{
		RunTheShard_Log("No override executables");
	}


	RunTheShard_Log("Copying over all frankenbuild exes and pdbs");
	sprintf(remoteFileName, "%s\\%sServer", pShard->pDirectory_FixedUp, gpRun->pProductName);
	CopyDirectoryContents(pSendHandle, pShard->pMachineName, gpRun->pLocalFrankenBuildDir, remoteFileName, true, FrankenBuildDirCB);

	RunTheShard_Log("Done");

}


void CreateBatchAndCommandFiles(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard)
{
	int i, j;
	GlobalType eOverrideType;
	FILE *pFile;
	GlobalType eServerType;
	char fileName[CRYPTIC_MAX_PATH];
	static char *pClientCommandLine = NULL;	

	char *pPCLSpawnCmdLine = GetNonZeroOptionByName(pShard, "PCL_SPAWN_CMDLINE");
	char *pIgnorePCLFail = GetNonZeroOptionByName(pShard, "IGNORE_PCL_FAIL"); 

	mkdirtree_const("c:\\shardLauncher\\fake.txt");

	estrDestroy(&spGlobalCommandLine);
	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		estrDestroy(&sTypeInfo[i].pFirstCommandLine);
		estrDestroy(&sTypeInfo[i].pSharedCommandLine);
	}

	estrClear(&pClientCommandLine);
	ea32Destroy(&spTypesToLaunch);


	RunTheShard_Log("Loading ShardSetup file in case it's needed");
	if (spShardSetupFile)
	{
		StructDestroy(parse_MachineInfoForShardSetupList, spShardSetupFile);
		spShardSetupFile = NULL;
	}

	spShardSetupFile = StructCreate(parse_MachineInfoForShardSetupList);
	sprintf(fileName, "c:\\ShardSetupFiles\\%s.txt", pShard->pShardSetupFileName);
	if (!ParserReadTextFile(fileName, parse_MachineInfoForShardSetupList, spShardSetupFile, 0))
	{
		RunTheShard_LogFail("ShardSetupFile Loading failed");
		exit(-1);
	}

	for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
	{
		for (j=0; j < eaSize(&gpRun->pOptionLibrary->ppLists[i]->ppOptions); j++)
		{
			gpRun->pOptionLibrary->ppLists[i]->ppOptions[j]->bSet = false;
		}
	}
		

	if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0])
	{
		RunTheShard_Log("Adding special commands for logServer to fork to global logserver\n");

		estrConcatf(&sTypeInfo[GLOBALTYPE_LOGSERVER].pSharedCommandLine, 
			" -SetClusterLevelLogServerName %s %s ", gpRun->pLogServerAndParserMachineName, gpRun->pLogServerFilterFileName);
	}

	if (gpRun->pTemplateFileName)
	{
		RunTheShard_Log("About to process options from template file %s", gpRun->pTemplateFileName);
	}
	for (i=0; i < eaSize(&gpRun->ppTemplateChoices); i++)
	{
		if (OptionIsSetForClusteredRun(gpRun, pShard, gpRun->ppTemplateChoices[i]->pConfigOptionName))
		{
			RunTheShard_Log("Skipping option %s from template, it is overridden", gpRun->ppTemplateChoices[i]->pConfigOptionName);
		}
		else
		{	
			ShardLauncherConfigOption *pOption = FindConfigOption(gpRun, gpRun->ppTemplateChoices[i]->pConfigOptionName, &eOverrideType);
				
			RunTheShard_Log("Processing option %s", gpRun->ppTemplateChoices[i]->pConfigOptionName);
			

			if (!pOption)
			{
				if (!OptionIsDeprecated(gpRun->ppTemplateChoices[i]->pConfigOptionName))
				{
					RunTheShard_LogWarning("Couldn't find config option named %s which wants to be set to value %s", 
						gpRun->ppTemplateChoices[i]->pConfigOptionName, gpRun->ppTemplateChoices[i]->pValue);
				}
			}	
			else
			{
				pOption->bSet = true;
				if (!ApplyStuffFromConfigOption(gpRun, pShard, pOption, gpRun->ppTemplateChoices[i]->pValue, eOverrideType))
				{
					exit(-1);
				}
			}
		}
	}


	RunTheShard_Log("About to process overridden options");
	for (i=0; i < eaSize(&gpRun->ppChoices); i++)
	{
		if (ChoiceAppliesToCurrentShard(gpRun, pShard, i))
		{
			ShardLauncherConfigOption *pOption = FindConfigOption(gpRun, gpRun->ppChoices[i]->pConfigOptionName, &eOverrideType);
			
			RunTheShard_Log("Processing option %s", gpRun->ppChoices[i]->pConfigOptionName);
		

			if (!pOption)
			{	
				if (!OptionIsDeprecated(gpRun->ppChoices[i]->pConfigOptionName))
				{
					RunTheShard_LogWarning("Couldn't find config option named %s which wants to be set to value %s", 
						gpRun->ppChoices[i]->pConfigOptionName, gpRun->ppChoices[i]->pValue);
				}
			}
			else
			{
				pOption->bSet = true;
				if (!ApplyStuffFromConfigOption(gpRun, pShard, pOption, gpRun->ppChoices[i]->pValue, eOverrideType))
				{
					exit(-1);
				}
			}
		}
	}

	
	for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
	{
		for (j=0; j < eaSize(&gpRun->pOptionLibrary->ppLists[i]->ppOptions); j++)
		{
			if (!gpRun->pOptionLibrary->ppLists[i]->ppOptions[j]->bSet)
			{
				ApplyStuffFromConfigOption(gpRun, pShard, gpRun->pOptionLibrary->ppLists[i]->ppOptions[j], NULL, GLOBALTYPE_NONE);
			}
		}
	}

	pFile = fopen(pShard->pLocalCommandFileName, "wt");
	if (!pFile)
	{
		RunTheShard_LogFail("Couldn't open %s", pShard->pLocalCommandFileName);
		exit(-1);
	}

	for (eServerType = 0; eServerType < GLOBALTYPE_MAXTYPES; eServerType++)
	{
		if (eServerType != GLOBALTYPE_CONTROLLER && eServerType != GLOBALTYPE_CLIENT && eServerType != GLOBALTYPE_LAUNCHER)
		{
			bool bHide = GetNonZeroOptionByName(pShard, GetServerTypeSpecificOptionName("HIDE_CONSOLES", eServerType)) != NULL;
			bool bShow = GetNonZeroOptionByName(pShard, GetServerTypeSpecificOptionName("SHOW_CONSOLES", eServerType)) != NULL;
			if (sTypeInfo[eServerType].pSharedCommandLine || bHide || bShow )
			{
				fprintf(pFile, "Command APPENDSERVERTYPECOMMANDLINE %s\n{\n\tScriptString %s\n",
					GlobalTypeToName(eServerType), 
					GetQuotedString(sTypeInfo[eServerType].pSharedCommandLine ? sTypeInfo[eServerType].pSharedCommandLine : ""));
				
				if (bHide || bShow)
				{
					fprintf(pFile, "\tStartHidden %d\n", bHide);
				}
						
				fprintf(pFile, "}\n");
			}
		}
	}

	//process all "[serverType] LAUNCH" and "[serverType] NO_NORMAL_LAUNCH" options
	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GetNonZeroOptionByName(pShard, GetServerTypeSpecificOptionName("LAUNCH", i)))
		{
			ea32PushUnique(&spTypesToLaunch, i);
		}

		if (GetNonZeroOptionByName(pShard, GetServerTypeSpecificOptionName("NO_NORMAL_LAUNCH", i)))
		{
			ea32FindAndRemove(&spTypesToLaunch, i);
		}
	}

	//first go through everything in our option library launch order. Then everything else
	
	for (i=0; i < ea32Size(&gpRun->pOptionLibrary->pLaunchOrderList); i++)
	{
		GlobalType eTypeToLaunch = gpRun->pOptionLibrary->pLaunchOrderList[i];
		if (ea32Find(&spTypesToLaunch, eTypeToLaunch) != -1)
		{
			char *pCountString;
			fprintf(pFile, "Command LAUNCH_NORMALLY %s\n{\n", GlobalTypeToName(eTypeToLaunch));
			if (sTypeInfo[eTypeToLaunch].pFirstCommandLine)
			{
				fprintf(pFile, "\tExtraCommandLine %s\n", GetQuotedString(sTypeInfo[eTypeToLaunch].pFirstCommandLine));
			}

			pCountString = GetNonZeroOptionByName(pShard, GetServerTypeSpecificOptionName("LAUNCH_COUNT", eTypeToLaunch));
			if (pCountString)
			{
				fprintf(pFile, "\tCount %d\n", atoi(pCountString));
			}

			fprintf(pFile, "}\n");

			//special case... immediately after launching logserver is when we do the AUTO_SETTINGS pause stuff
			if (eTypeToLaunch == GLOBALTYPE_LOGSERVER)
			{
				MaybeAddAutoSettingsPauseStuff(NULL, pFile);
			}
		}
	}
		

	for (i=0; i < ea32Size(&spTypesToLaunch); i++)
	{
		GlobalType eTypeToLaunch = spTypesToLaunch[i];
		if (ea32Find((U32**)&gpRun->pOptionLibrary->pLaunchOrderList, eTypeToLaunch) == -1)
		{
			char *pCountString;
			fprintf(pFile, "Command LAUNCH_NORMALLY %s\n{\n", GlobalTypeToName(spTypesToLaunch[i]));
			if (sTypeInfo[spTypesToLaunch[i]].pFirstCommandLine)
			{
				fprintf(pFile, "\tExtraCommandLine %s\n", GetQuotedString(sTypeInfo[spTypesToLaunch[i]].pFirstCommandLine));
			}
					
			pCountString = GetNonZeroOptionByName(pShard, GetServerTypeSpecificOptionName("LAUNCH_COUNT", spTypesToLaunch[i]));
	
			if (pCountString)
			{
				fprintf(pFile, "\tCount %d\n", atoi(pCountString));
			}
			fprintf(pFile, "}\n");

			//special case... immediately after launching logserver is when we do the AUTO_SETTINGS pause stuff
			if (eTypeToLaunch == GLOBALTYPE_LOGSERVER)
			{
				MaybeAddAutoSettingsPauseStuff(NULL, pFile);
			}
		}
	}

	if (gpRun->pControllerCommandsArbitraryText)
	{
		fprintf(pFile, "\n#From here down, this comes from the arbitrary user-set text\n%s\n",
			gpRun->pControllerCommandsArbitraryText);
	}

	fclose(pFile);
	RunTheShard_Log("%s created succcessfully", pShard->pLocalCommandFileName);

	RunTheShard_Log("About to create %s", pShard->pLocalBatchFileName);
	pFile = fopen(pShard->pLocalBatchFileName, "wt");
	if (!pFile)
	{
		RunTheShard_LogFail("Couldn't open %s", pShard->pLocalBatchFileName);
		exit(-1);
	}
	fprintf(pFile, "REM Auto-created batch file for shard type %s product %s at %s\n",
		gpRun->pRunName, gpRun->pProductName, timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
	sprintf(fileName, "%s/%sServer", pShard->pDirectory_FixedUp, gpRun->pProductName);
	backSlashes(fileName);
		
	fprintf(pFile, "cd %s\n", fileName);

	if (GetNonZeroOptionByName(pShard, "ENABLE_UGC"))
	{
		fprintf(pFile, "rd /s /q .\\data\\ns\n");
	}

	fprintf(pFile, "start controller.exe -console %s %s -cookie %u -ShardName %s -KillAllOtherShardExesAtStartup -SetProductName %s %s -ScriptFile %s -ProductionShardMode 1 -execDir %s -coreExecDir %s - %s - %s - %s",
		GetOverlordSnippet(),
		gRunType == RUNTYPE_LAUNCH ? "-DontPatchOtherMachines" : "",
		timeSecondsSince2000(),
		pShard->pShardName,
		gpRun->pProductName, gpRun->pShortProductName, pShard->pRemoteCommandFileName, fileName, fileName,
		spGlobalCommandLine ? spGlobalCommandLine : "",
		sTypeInfo[GLOBALTYPE_CONTROLLER].pFirstCommandLine ? sTypeInfo[GLOBALTYPE_CONTROLLER].pFirstCommandLine : "",
		sTypeInfo[GLOBALTYPE_CONTROLLER].pSharedCommandLine ? sTypeInfo[GLOBALTYPE_CONTROLLER].pSharedCommandLine : "");

	fprintf(pFile, " -ContainerID %u -SetContainerIDRange %u %u ", 
		pShard->iStartingID, pShard->iStartingID, pShard->iStartingID + STARTING_ID_INTERVAL - 1);

	fprintf(pFile, " -UseSentryServer 1 -ShardSetupFile %s", pShard->pShardSetupFileName);

	fprintf(pFile, " -ClusterController %s -ShardClusterName %s ",
		getHostName(), gpRun->pClusterName);

	if (sTypeInfo[GLOBALTYPE_CLIENT].pFirstCommandLine || sTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine)
	{
		char *pSuperEscapedClientCommandLine = NULL;


		estrPrintf(&pClientCommandLine, "%s - %s", sTypeInfo[GLOBALTYPE_CLIENT].pFirstCommandLine ? sTypeInfo[GLOBALTYPE_CLIENT].pFirstCommandLine : "", 
			sTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine ? sTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine : "");

		estrSuperEscapeString(&pSuperEscapedClientCommandLine, pClientCommandLine);
		fprintf(pFile, " -SuperEsc AutoPatchedClientCommandLine %s ", pSuperEscapedClientCommandLine);
		estrDestroy(&pSuperEscapedClientCommandLine);
	}

	if (spGlobalCommandLine && spGlobalCommandLine[0])
	{
		char *pSuperEscapedGlobalCommandLine = NULL;

		estrSuperEscapeString(&pSuperEscapedGlobalCommandLine, spGlobalCommandLine);
		fprintf(pFile, " -SuperEsc GlobalSharedCommandLine %s", pSuperEscapedGlobalCommandLine);
		estrDestroy(&pSuperEscapedGlobalCommandLine);
	}

	if (pPCLSpawnCmdLine || pIgnorePCLFail)
	{
		char *pTemp = NULL;
		char *pSuperEscapedSpawnCmdLine = NULL;

		estrConcatf(&pTemp, "%s %s", pPCLSpawnCmdLine ? pPCLSpawnCmdLine : "", pIgnorePCLFail ? "-DoExecuteEvenOnFail" : "");
		estrSuperEscapeString(&pSuperEscapedSpawnCmdLine, pTemp);
		fprintf(pFile, " -SuperEsc SpawnPatchCmdLine %s", pSuperEscapedSpawnCmdLine);
		estrDestroy(&pSuperEscapedSpawnCmdLine);
		estrDestroy(&pTemp);
	}

	if (sTypeInfo[GLOBALTYPE_LAUNCHER].pSharedCommandLine && sTypeInfo[GLOBALTYPE_LAUNCHER].pSharedCommandLine[0])
	{
		char *pSuperEscapedLaunchernCmdLine = NULL;

		estrSuperEscapeString(&pSuperEscapedLaunchernCmdLine, sTypeInfo[GLOBALTYPE_LAUNCHER].pSharedCommandLine);
		fprintf(pFile, " -SuperEsc SetLauncherCommandLine %s", pSuperEscapedLaunchernCmdLine);
		estrDestroy(&pSuperEscapedLaunchernCmdLine);
	}

	FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pOtherShard)
	{
		if (pOtherShard != pShard)
		{
			fprintf(pFile, " -AddShardToCluster %s %s ", pOtherShard->pShardName, pOtherShard->pMachineName);
		}
	}
	FOR_EACH_END;

	fprintf(pFile, " -ShardTypeInCluster %s ", StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pShard->eShardType));


	fprintf(pFile, " %%1 %%2 %%3 %%4 %%5 %%6 %%7 %%8 %%9\n\n\n");

	if (pClientCommandLine)
	{
		fprintf(pFile, "REM AutoPatchecClientCommandLine: %s\n", pClientCommandLine);
		estrDestroy(&pClientCommandLine);
	}
	if (spGlobalCommandLine && spGlobalCommandLine[0])
	{
		fprintf(pFile, "REM GlobalComandLine: %s\n", spGlobalCommandLine);
	}

	fclose(pFile);
	RunTheShard_Log("Batch and command files created");


}

bool PatchClusterController(ShardLauncherRun *pRun)
{
	char clusterControllerDir[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char *pCommandLine = NULL;
	char *pCriticalSystemPage = GetNonZeroOptionByName(NULL, "CRITICALSYSTEM_PAGE");
	char patchingTaskName[128];


	sprintf(clusterControllerDir, "c:\\%s\\ClusterController", pRun->pClusterName);
	sprintf(temp, "%s\\temp.txt", clusterControllerDir);
	mkdirtree_const(temp);

	RunTheShard_Log("About to try to patch ClusterController into %s", clusterControllerDir);

	estrPrintf(&pCommandLine, "WORKINGDIR(%s)PatchClient%s.exe -project %sServerMini -name %s -sync ",
		clusterControllerDir, IsUsingX64() ? "X64" : "", gpRun->pProductName, gpRun->pPatchVersion);
	
	if (gpRun->pPatchServer && gpRun->pPatchServer[0])
	{
		estrConcatf(&pCommandLine, " -server %s", gpRun->pPatchServer);
	}

	//machine name should always be first
	sprintf(patchingTaskName, "%s:%s:ClusterController", getHostName(), pRun->pClusterName);

	estrConcatf(&pCommandLine, " -beginpatchstatusreporting_critical \"%s\" localhost %d ",
		patchingTaskName, SHARDLAUNCHER_PATCHSTATUS_PORT);


	SentryServerComm_RunCommand_1Machine(getHostName(), pCommandLine);

	AddPatchingTaskForRestarting(patchingTaskName, getHostName(), pCommandLine);
	giPatchSucceededCount = 0;
	PCLStatusMonitoring_Add(patchingTaskName);

	return true;
}


bool LaunchClusterController(ShardLauncherRun *pRun)
{
	char *pOverlord = UtilitiesLib_GetOverlordName();
	char clusterControllerDir[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char batchFileName[CRYPTIC_MAX_PATH];
	FILE *pFile;
	char *pCommandLine = NULL;
	char *pCriticalSystemPage = GetNonZeroOptionByName(NULL, "CRITICALSYSTEM_PAGE");


	sprintf(clusterControllerDir, "c:\\%s\\ClusterController", pRun->pClusterName);
	sprintf(temp, "%s\\temp.txt", clusterControllerDir);
	mkdirtree_const(temp);

	RunTheShard_Log("About to try to launch ClusterController from %s", clusterControllerDir);


	//if clustercontroller.exe is an override exe, copy it
	if (eaSize(&gpRun->ppOverrideExecutableNames))
	{
		int i;
		int iRetVal;
		bool bTimedOut;
		RunTheShard_Log("About to check whether we need to copy override ClusterController executable...");
		
		for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
		{
			if (strstri(gpRun->ppOverrideExecutableNames[i], "clusterController"))
			{
				static char *pSystemString = NULL;
				static char *pPdbName = NULL;

				RunTheShard_Log("About to copy %s", gpRun->ppOverrideExecutableNames[i]);
				estrPrintf(&pSystemString, "copy %s %s\\%sServerMini\\%s", gpRun->ppOverrideExecutableNames[i], clusterControllerDir, gpRun->pProductName, GetShortExecutableName(gpRun->ppOverrideExecutableNames[i], true));

				ExecuteSystemCommand(pSystemString, 60, &bTimedOut, &iRetVal);

				if (bTimedOut)
				{
					RunTheShard_LogFail("Copying %s timed out", gpRun->ppOverrideExecutableNames[i]);
					return false;
				}

				if (iRetVal)
				{
					RunTheShard_LogFail("Copying %s failed", gpRun->ppOverrideExecutableNames[i]);
					return false;
				}

				estrCopy2(&pPdbName, gpRun->ppOverrideExecutableNames[i]);
				estrReplaceOccurrences(&pPdbName, ".exe", ".pdb");

				if (fileExists(pPdbName))
				{
					RunTheShard_Log("Also copying %s\n", pPdbName);
					estrPrintf(&pSystemString, "copy %s %s\\%sServerMini", pPdbName, clusterControllerDir, gpRun->pProductName);

					ExecuteSystemCommand(pSystemString, 60, &bTimedOut, &iRetVal);

					if (bTimedOut)
					{
						RunTheShard_LogFail("Copying %s timed out", pPdbName);
						return false;
					}

					if (iRetVal)
					{
						RunTheShard_LogFail("Copying %s failed", pPdbName);
						return false;
					}
				}
				else
				{
					RunTheShard_LogWarning("Note: %s does not exist\n", pPdbName);
				}
			}
		}
	}


	sprintf(batchFileName, "c:\\%s\\Launch_%s_ClusterController.bat", pRun->pClusterName, pRun->pClusterName);

	mkdirtree_const(batchFileName);
	pFile = fopen(batchFileName, "wt");


	if (!pFile)
	{
		RunTheShard_LogFail("Couldn't open %s for writing", batchFileName);
		return false;
	}

	fprintf(pFile, "c:\n");
	fprintf(pFile, "cd %s\\%sServerMini\n", clusterControllerDir, pRun->pProductName);
	fprintf(pFile, "crypticKillAll -kill ClusterController\n") ;

	fprintf(pFile, "ClusterController.exe -FrankenBuildDir %s -LocalTemplateDataDir %s ", pRun->pLocalFrankenBuildDir, pRun->pLocalDataTemplateDir);

	FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
	{
		fprintf(pFile, " -addShard %s %s %s", pShard->pShardName, pShard->pMachineName, pShard->pRemoteBatchFileName);
	}
	FOR_EACH_END;

	fprintf(pFile, " -ShardClusterName %s -SetProductName %s %s ", gpRun->pClusterName, 
		gpRun->pProductName, gpRun->pShortProductName);

	if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0])
	{
		fprintf(pFile, " -LogServer %s ", gpRun->pLogServerAndParserMachineName);
	}

	if (pCriticalSystemPage)
	{
		fprintf(pFile, " -BeginStatusReporting %s_ClusterController %s 8086 ", 
			gpRun->pClusterName, pCriticalSystemPage);

		if (pOverlord)
		{
			fprintf(pFile, " -SetOverlord %s -DoExtraStatusReporting %s:%d -AllowCommandsOverStatusReportingLinks ",
				pOverlord, pOverlord, OVERLORD_SIMPLE_STATUS_PORT);
		}	
	}
	else
	{
		if (pOverlord)
		{
			fprintf(pFile, " -SetOverlord %s -BeginStatusReporting %s_ClusterController %s:%d 8086 -AllowCommandsOverStatusReportingLinks ",
				pOverlord, gpRun->pClusterName, pOverlord, OVERLORD_SIMPLE_STATUS_PORT);
		}
	}

	if (GetNonZeroOptionByName(NULL, "NO_CONSOLE_CLOSE_BUTTON"))
	{
		fprintf(pFile, " -removeConsoleCloseButton ");
	}


	if (GetNonZeroOptionByName(NULL, "SHARD_CATEGORY"))
	{
		fprintf(pFile, " -SetShardCategoryManually %s ", GetNonZeroOptionByName(NULL, "SHARD_CATEGORY"));
	}

	fprintf(pFile, "\n");
	fclose(pFile);

	estrPrintf(&pCommandLine, "CreateShortCut -ShortCutName \"Restart ClusterController\" -ShortCutDesc \"Restart ClusterController for %s\" -FileToRun %s",
		gpRun->pClusterName, batchFileName);
	system_detach(pCommandLine, false, false);

	estrPrintf(&pCommandLine, "cmd.exe /c %s", batchFileName);
	system_detach(pCommandLine, false, false);

	estrDestroy(&pCommandLine);

	return true;
//	system_detach_with_fulldebug_fixup(pCommandLine, false, false);
}

static volatile bool sbReturned = false;
static char *spCheckForExesErrorString = NULL;

void CheckForRunningExesCB(SentryProcess_FromSimpleQuery_List *pList, void *pUserData)
{

	FOR_EACH_IN_EARRAY(pList->ppProcesses, SentryProcess_FromSimpleQuery, pProc)
	{
		if (strstri(pProc->pProcessName, "controller") && !strstri(pProc->pProcessName, "clusterController") && !strstri(pProc->pProcessName, "controllerTracker"))
		{
			estrPrintf(&spCheckForExesErrorString, "Machine %s appears to have a controller already running on it... FAIL", pList->pMachineName);
		}

		if (strstri(pProc->pProcessName, "patchclient"))
		{
			estrPrintf(&spCheckForExesErrorString, "Machine %s appears to have a patchclient already running on it... FAIL", pList->pMachineName);
		}

	}
	FOR_EACH_END;
	

	sbReturned = true;

}


void CheckForLoggingExesCB(SentryProcess_FromSimpleQuery_List *pList, void *pUserData)
{

	FOR_EACH_IN_EARRAY(pList->ppProcesses, SentryProcess_FromSimpleQuery, pProc)
	{
		if (strstri(pProc->pProcessName, "logserver"))
		{
			estrPrintf(&spCheckForExesErrorString, "Machine %s appears to have a logserver already running on it... FAIL", pList->pMachineName);
		}

		if (strstri(pProc->pProcessName, "logparser"))
		{
			estrPrintf(&spCheckForExesErrorString, "Machine %s appears to have a logparser already running on it... FAIL", pList->pMachineName);
		}
	}
	FOR_EACH_END;

	sbReturned = true;

}

void CheckForRunningExes(char *pMachineName)
{
	U64 siTimeStarted;


TryAgain:

	siTimeStarted = timeGetTime();

	estrClear(&spCheckForExesErrorString);

	RunTheShard_Log("Going to check for running Exes on %s", pMachineName);

	sbReturned = false;

	SentryServerComm_QueryMachineForRunningExes_Simple(pMachineName, CheckForRunningExesCB, NULL);

	while (1)
	{
		Tick();

		MemoryBarrier();

		if (sbReturned)
		{
			MemoryBarrier();

			if (estrLength(&spCheckForExesErrorString))
			{
				if (sbKillClusterStuffAtRunTime)
				{
					RunTheShard_Log("We killed everything, so waiting for everything to die\n");
					Sleep(1000);

					goto TryAgain;
				}
				RunTheShard_LogFail("%s", spCheckForExesErrorString);
			}

			RunTheShard_Log("%s is clear", pMachineName);

			return;
		}

		if (timeGetTime() - siTimeStarted > 5000)
		{
			RunTheShard_LogFail("Waited more than 5 seconds, never got query back from sentryServer to see if controller/patchclient was running on %s... this is fatal",
				pMachineName);
		}
	}

}



void CheckForLoggingExes(char *pMachineName)
{
	U64 siTimeStarted;

TryAgain:

	siTimeStarted = timeGetTime();

	estrClear(&spCheckForExesErrorString);

	RunTheShard_Log("Going to check for running logging Exes on %s", pMachineName);

	sbReturned = false;

	SentryServerComm_QueryMachineForRunningExes_Simple(pMachineName, CheckForLoggingExesCB, NULL);

	while (1)
	{
		Tick();

		MemoryBarrier();

		if (sbReturned)
		{
			MemoryBarrier();

			if (estrLength(&spCheckForExesErrorString))
			{

				if (sbKillClusterStuffAtRunTime)
				{
					RunTheShard_Log("We killed everything, so waiting for everything to die\n");
					Sleep(1000);

					goto TryAgain;
				}

				GetHumanConfirmationDuringShardRunning("One or more logging executables are still running on %s... if you continue, they will not be patched or restarted, and no newly selected options will be applied to them. NOTE THAT EVEN IF YOU GO KILL THEM NOW IT IS TOO LATE FOR THEM TO BE PART OF THIS LAUNCH!!!! Continue?", pMachineName);
				sbLoggingExecutablesAlreadyRunning = true;
				RunTheShard_Log("Continuing despite logging executables already running... will leave them");

			}
			else
			{
				RunTheShard_Log("%s is clear", pMachineName);
			}

			return;
		}

		if (timeGetTime() - siTimeStarted > 5000)
		{
			RunTheShard_LogFail("Waited more than 5 seconds, never got query back from sentryServer to see if logging exes were running on %s... this is fatal",
				pMachineName);
		}
	}

}

void RunTheShardFileSendUpdate(char *pUpdateString, void *pUserData)
{


}
void RunTheShardFileSendResult(bool bAllSucceeded, char *pErrorString, bool *pUserData)
{
	if (!bAllSucceeded)
	{
		RunTheShard_LogFail("Failed to transfer files: %s", pErrorString);
	}
	else
	{
		*pUserData = true;
	}
}

/*
	pSendHandle = SentryServerComm_BeginDeferredFileSending("Sending Logserver/Parser batch files");

	SentryServerComm_SendFileDeferred(pSendHandle, gpRun->pLogServerAndParserMachineName, pLogServerLocal, pLogServerRemote);
	Tick();

	SentryServerComm_SendFileDeferred(pSendHandle, gpRun->pLogServerAndParserMachineName, pLogParserLocal, pLogParserRemote);
	Tick();

	SentryServerComm_DeferredFileSending_DoIt(pSendHandle, RunTheShardFileSendUpdate, RunTheShardFileSendResult, (void*)&bFileSendingDone);

	while (!bFileSendingDone)
	{
		Tick();
	}
	*/

void MaybeSendLoggingOverrideExes(void)
{
	volatile bool bFileSendingDone = false;
	SentryServerCommDeferredSendHandle *pSendHandle = NULL;
	int i;

	RunTheShard_Log("About to copy logging override executables (if any)");


	for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
	{
		char *pExeName = gpRun->ppOverrideExecutableNames[i];
		
		if (strstri(pExeName, "logserver") || strstri(pExeName, "logparser"))
		{
		
			static char *pRemoteName = NULL;
			static char *pPDBName = NULL;
			static char *pRemotePDBName = NULL;

			if (!pSendHandle)
			{
				pSendHandle = SentryServerComm_BeginDeferredFileSending("Sending Logserver/Parser override files");
			}

			estrPrintf(&pRemoteName, "%s\\%sServerMini\\%s", gpRun->pLogServerDir, gpRun->pProductName, GetShortExecutableName(pExeName, true));
			RunTheShard_Log("About to send %s through sentry server", pExeName);

			SentryServerComm_SendFileDeferred(pSendHandle, gpRun->pLogServerAndParserMachineName, pExeName, pRemoteName);
			Tick();

			estrCopy2(&pPDBName, pExeName);
			estrCopy2(&pRemotePDBName, pRemoteName);

			estrReplaceOccurrences(&pPDBName, ".exe", ".pdb");
			estrReplaceOccurrences(&pRemotePDBName, ".exe", ".pdb");

			if (fileExists(pPDBName))
			{
				RunTheShard_Log("Also sending %s\n", pPDBName);
				SentryServerComm_SendFileDeferred(pSendHandle, gpRun->pLogServerAndParserMachineName, pPDBName, pRemotePDBName);
				Tick();			
			}
			else
			{
				RunTheShard_LogWarning("Note: %s does not exist\n", pPDBName);

			}
		}
	}

	if (pSendHandle)
	{
		SentryServerComm_DeferredFileSending_DoIt(pSendHandle, RunTheShardFileSendUpdate, RunTheShardFileSendResult, (void*)&bFileSendingDone);

		while (!bFileSendingDone)
		{
			Tick();	
		}
	}


}


void CreateLoggingBatchFiles(void)
{
	char *pOverlord = UtilitiesLib_GetOverlordName();
	char *pLogServerLocal = NULL;
	char *pLogServerRemote = NULL;
	char *pLogParserLocal = NULL;
	char *pLogParserRemote = NULL;
	FILE *pFile;
	char *pCmdString = NULL;
	char *pCriticalSystemPage = GetNonZeroOptionByName(NULL, "CRITICALSYSTEM_PAGE");
	SentryServerCommDeferredSendHandle *pSendHandle;
	volatile bool bFileSendingDone = false;

	estrPrintf(&pLogServerLocal, "c:\\%s\\%s_LaunchShardLevelLogServer.bat", gpRun->pClusterName, gpRun->pClusterName);
	estrPrintf(&pLogParserLocal, "c:\\%s\\%s_LaunchShardLevelLogParser.bat", gpRun->pClusterName, gpRun->pClusterName);

	estrPrintf(&pLogServerRemote, "%s\\%s_LaunchShardLevelLogServer.bat", 
		gpRun->pLogServerDir, gpRun->pClusterName);
	estrPrintf(&pLogParserRemote, "%s\\%s_LaunchShardLevelLogParser.bat", 
		gpRun->pLogServerDir, gpRun->pClusterName);

	mkdirtree_const(pLogServerLocal);
	pFile = fopen(pLogServerLocal, "wt");

	if (!pFile)
	{
		RunTheShard_LogFail("Couldn't open %s", pLogServerLocal);
		return;
	}

	fprintf(pFile, "cd %s\\%sServerMini\n", gpRun->pLogServerDir, gpRun->pProductName);
	fprintf(pFile, "IF DEFINED programw6432 (set SUFFIX=X64.exe) else (SET SUFFIX=.exe)\n");
	fprintf(pFile, "start LogServer%%SUFFIX%% -ExtraWriteOutMinAge 180 -NoShardMode -DoGenericHTTPServing 8081 -LogSetDir %s - %s - ",
		gpRun->pLoggingDir, gpRun->pLogServerExtraCmdLine ? gpRun->pLogServerExtraCmdLine : "");

	fprintf(pFile, " -SetProductName %s %s -ShardClusterName %s ", 
		gpRun->pProductName, gpRun->pShortProductName, gpRun->pClusterName);

	if (pCriticalSystemPage)
	{
		fprintf(pFile, "-BeginStatusReporting %s_LogServer %s 8081 -DoExtraStatusReporting %s ",
			gpRun->pClusterName, pCriticalSystemPage, getHostName());
	}
	else
	{
		fprintf(pFile, "-BeginStatusReporting %s_LogServer %s 8081 ",
			gpRun->pClusterName, getHostName());
	}

	fprintf(pFile, "-RestartCommand %s ", pLogServerRemote);

	if (pOverlord)
	{
		fprintf(pFile, " -SetOverlord %s -DoExtraStatusReporting %s:%d ",
			pOverlord, pOverlord, OVERLORD_SIMPLE_STATUS_PORT);
	}

	fprintf(pFile, "\nexit\n");
	fclose(pFile);

	pFile = fopen(pLogParserLocal, "wt");

	if (!pFile)
	{
		RunTheShard_LogFail("Couldn't open %s", pLogParserLocal);
		return;
	}
	
	fprintf(pFile, "cd %s\\%sServerMini\n", gpRun->pLogServerDir, gpRun->pProductName);
	fprintf(pFile, "IF DEFINED programw6432 (set SUFFIX=X64.exe) else (SET SUFFIX=.exe)\n");
	fprintf(pFile, "start LogParser%%SUFFIX%% -NoShardMode -EnableLAunching 6 %s -LogServer localhost -DataDir %s\\LogParserData - %s - ",
		gpRun->pLoggingDir, gpRun->pLogServerDir, gpRun->pLogParserExtraCmdLine ? gpRun->pLogParserExtraCmdLine : "");

	fprintf(pFile, " -SetProductName %s %s -ShardClusterName %s ", gpRun->pProductName, gpRun->pShortProductName,
		gpRun->pClusterName );


	if (pCriticalSystemPage)
	{
		fprintf(pFile, "-BeginStatusReporting %s_LogParser %s 8084 -DoExtraStatusReporting %s ",
			gpRun->pClusterName, pCriticalSystemPage, getHostName());
	}
	else
	{
		fprintf(pFile, "-BeginStatusReporting %s_LogParser %s 8084",
			gpRun->pClusterName, getHostName());
	}

	fprintf(pFile, "-RestartCommand %s ", pLogParserRemote);

	FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
	{
		fprintf(pFile, " -SetClusterShardStartingID %s %d ",
			pShard->pShardName, pShard->iStartingID);
	}
	FOR_EACH_END;

	if (pOverlord)
	{
		fprintf(pFile, " -SetOverlord %s -DoExtraStatusReporting %s:%d ",
			pOverlord, pOverlord, OVERLORD_SIMPLE_STATUS_PORT);
	}

	fprintf(pFile, "\nexit\n");
	fclose(pFile);

	RunTheShard_Log("About to send logserver/parser launching batch files to %s", gpRun->pLogServerAndParserMachineName);

	pSendHandle = SentryServerComm_BeginDeferredFileSending("Sending Logserver/Parser batch files");

	SentryServerComm_SendFileDeferred(pSendHandle, gpRun->pLogServerAndParserMachineName, pLogServerLocal, pLogServerRemote);
	Tick();

	SentryServerComm_SendFileDeferred(pSendHandle, gpRun->pLogServerAndParserMachineName, pLogParserLocal, pLogParserRemote);
	Tick();

	SentryServerComm_DeferredFileSending_DoIt(pSendHandle, RunTheShardFileSendUpdate, RunTheShardFileSendResult, (void*)&bFileSendingDone);

	while (!bFileSendingDone)
	{
		Tick();
	}

	RunTheShard_Log("Running logserver/logparser");

	estrPrintf(&pCmdString, "cmd /c start %s", pLogServerRemote);
	SentryServerComm_RunCommand_1Machine(gpRun->pLogServerAndParserMachineName, pCmdString);
	estrPrintf(&pCmdString, "cmd /c start %s", pLogParserRemote);
	SentryServerComm_RunCommand_1Machine(gpRun->pLogServerAndParserMachineName, pCmdString);
	estrDestroy(&pCmdString);
	Tick();
		
}


void KillRunningExes(char *pMachineName)
{
	SentryServerComm_KillProcess_1Machine(pMachineName, "controller, patchclient, logserver, logparser, controllerFD, patchclientFD, logserverFD, logparserFD, controllerX64, patchclientX64, logserverX64, logparserX64, controllerX64FD, patchclientX64FD, logserverX64FD, logparserX64FD", NULL);
	Tick();
}

void CheckForFrankenBuilds(void)
{
	char **ppFiles = fileScanDirNoSubdirRecurse(gpRun->pLocalFrankenBuildDir);
	char *pFrankenBuildExes = NULL;

	FOR_EACH_IN_EARRAY(ppFiles, char, pFile)
	{
		if (strEndsWith(pFile, ".exe"))
		{
			estrConcatf(&pFrankenBuildExes, "%s%s", estrLength(&pFrankenBuildExes) ? ", " : "", 
				pFile);
		}
	}
	FOR_EACH_END;

	fileScanDirFreeNames(ppFiles);

	if (estrLength(&pFrankenBuildExes))
	{
		estrInsertf(&pFrankenBuildExes, 0, "Warning: One or more .exes found in %s. Because FRANKENBUILD_DIR is set, these will be copied to all shard machines. This means that shards will require console input to start up. Exes: ",
			gpRun->pLocalFrankenBuildDir);

		GetHumanConfirmationDuringShardRunning_WithExtraButton("Delete them", "DeleteFrankenbuilds (OK)", pFrankenBuildExes);

		estrDestroy(&pFrankenBuildExes);
	}
}

AUTO_COMMAND;
void DeleteFrankenBuilds(void)
{
	char **ppFiles;
	RunTheShard_Log("Deleting local frankenbuilds");

	ppFiles = fileScanDirNoSubdirRecurse(gpRun->pLocalFrankenBuildDir);

	FOR_EACH_IN_EARRAY(ppFiles, char, pFile)
	{
		if (strEndsWith(pFile, ".exe") || strEndsWith(pFile, ".pdb"))
		{
			fileForceRemove(pFile);
		}
	}
	FOR_EACH_END;

	fileScanDirFreeNames(ppFiles);
}

bool UseThisShard(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard)
{
	if (!OnlyOneShard(pRun))
	{
		return true;
	}

	if (stricmp(pShard->pShardName, pRun->onlyShardToLaunch) == 0)
	{
		return true;
	}

	return false;
}

void DoClusterAutoSettingWarning(void)
{
	static char *pWarningString = NULL;
	int i;

	estrPrintf(&pWarningString, "Warning: One or more shardlauncher settings are attempting to set AUTO_SETTINGs. This is currently NOT supported for clustered shards, and will do nothing:\n");

	for (i = 0; i < eaSize(&sppAutoSettingInits); i++)
	{
		estrConcatf(&pWarningString, "%s is trying to set: %s\n", sppAutoSettingOptionNames[i], sppAutoSettingInits[i]);
	}

	GetHumanConfirmationDuringShardRunning(pWarningString);
}

static DWORD WINAPI RunTheShard_Clusters( LPVOID lpParam )
{
	int iCounter = 0;

	if (!VerifyAndFixupLauncherRun(gpRun))
	{
		return false;
	}

	if (!OnlyOneShard(gpRun))
	{
		if (GetNonZeroOptionByName(NULL, "FRANKENBUILD_DIR"))
		{
			CheckForFrankenBuilds();
		}

		if (sbKillClusterStuffAtRunTime)
		{
			RunTheShard_LogWarning("Killing all running stuff on all machines (DEBUG ONLY!!!)");
			FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
			{
				KillRunningExes(pShard->pMachineName);	
			}
			FOR_EACH_END;
			if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0])
			{
				KillRunningExes(gpRun->pLogServerAndParserMachineName);
			}
			RunTheShard_LogWarning("done");
		}

		RunTheShard_Log("Checking for running controller/patchclient on shard machines");
		FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
		{
			CheckForRunningExes(pShard->pMachineName);	
		}
		FOR_EACH_END;

		if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0])
		{
			CheckForLoggingExes(gpRun->pLogServerAndParserMachineName);
		}


		giPatchSucceededCount = 0;



		if (gRunType != RUNTYPE_LAUNCH)
		{	
			int iNumToWaitFor = 1;

			RunTheShard_Log("Patching ClusterController");
			gStatusReportingState = STATUS_SHARDLAUNCHER_PATCHING;
			PatchClusterController(gpRun);
	

			if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0] && !sbLoggingExecutablesAlreadyRunning)
			{
				iNumToWaitFor++;
	
				RunTheShard_Log("Going to patch logging executables to %s", gpRun->pLogServerAndParserMachineName);
				gStatusReportingState = STATUS_SHARDLAUNCHER_PATCHING;
				StartLoggingPatching();
		
			}

			RunTheShard_Log("waiting for clustercontroller and logging patching to complete");

			while (giPatchSucceededCount < iNumToWaitFor)
			{
				Tick();
			}
		}

		if (!sbLoggingExecutablesAlreadyRunning)
		{
			if (gpRun->pLogServerAndParserMachineName && gpRun->pLogServerAndParserMachineName[0])
			{
				if (gRunType != RUNTYPE_LAUNCH)
				{	
					MaybeSendLoggingOverrideExes();
				}

				if (gRunType != RUNTYPE_PATCH)
				{
					CreateLoggingBatchFiles();
				}
			}
			else
			{
				RunTheShard_LogWarning("No logging machine specified... doing nothing");
			}
		}

		if (gRunType != RUNTYPE_PATCH)
		{	
			RunTheShard_Log("Launching ClusterController");
			LaunchClusterController(gpRun);
		}
	}

	if (gRunType != RUNTYPE_LAUNCH)
	{
		int iPatchStartedCount = 0;
		giPatchSucceededCount = 0;


		//start all patching
		FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
		{
			if (UseThisShard(gpRun, pShard))
			{
				iPatchStartedCount++;
				gStatusReportingState = STATUS_SHARDLAUNCHER_PATCHING;
				StartShardPatching(gpRun, pShard);
			}
		}
		FOR_EACH_END;

		while (giPatchSucceededCount < iPatchStartedCount)
		{			
			Tick();
		}

		RunTheShard_Log("Patching completed");
	}
	


	gStatusReportingState = STATUS_SHARDLAUNCHER_LAUNCHING;

	if (gRunType != RUNTYPE_PATCH)
	{
		volatile bool bFileSendingDone = false;
		SentryServerCommDeferredSendHandle *pSendHandle;

		RunTheShard_Log("now going to create batch file and controller command file");
		FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
		{
			if (UseThisShard(gpRun, pShard))
			{
				CreateBatchAndCommandFiles(gpRun, pShard);
			}
		}
		FOR_EACH_END;

		if (eaSize(&sppAutoSettingInits))
		{
			DoClusterAutoSettingWarning();
		}


		RunTheShard_Log("now going to transfer all other files through SentryServer");

		pSendHandle = SentryServerComm_BeginDeferredFileSending("RunTheShard TransferFiles");

		FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
		{
			if (UseThisShard(gpRun, pShard))
			{
				TransferFiles(pSendHandle, gpRun, pShard);
			}
		}
		FOR_EACH_END;

		RunTheShard_Log("Actually doing deferred file sends");
		SentryServerComm_DeferredFileSending_DoIt(pSendHandle, RunTheShardFileSendUpdate, RunTheShardFileSendResult, (void*)&bFileSendingDone);

		while (!bFileSendingDone)
		{
			Tick();
		}

		RunTheShard_Log("Done transferring files... time to launch the shards! Booyah!");
		FOR_EACH_IN_EARRAY(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
		{
			if (UseThisShard(gpRun, pShard))
			{
				SentryServerComm_RunCommand_1Machine(pShard->pMachineName, pShard->pRemoteBatchFileName);
				Tick();
			}
		}
		FOR_EACH_END;

	}

	RunTheShard_LogSucceed("Done... (Shardlauncher will close in 30 seconds)");
	Sleep(30000);
	exit(0);


	return true;
}
	

	


//AUTO_COMMANDs used by in string commands
AUTO_COMMAND;
const char *GetMachineForType(char *pTypeName)
{
	GlobalType eType;
	int i, j;

	if (!spShardSetupFile)
	{
		return "localHost";
	}

	eType = NameToGlobalType(pTypeName);
	if (eType == GLOBALTYPE_NONE)
	{
		RunTheShard_LogFail("Unknown global type %s", pTypeName);
		return "localHost";
	}

	for (i=0; i < eaSize(&spShardSetupFile->ppMachines); i++)
	{
		MachineInfoForShardSetup *pMachine = spShardSetupFile->ppMachines[i];
		for (j=0; j < eaSize(&pMachine->ppSettings); j++)
		{
			if (pMachine->ppSettings[j]->eServerType == eType && pMachine->ppSettings[j]->eSetting == CAN_LAUNCH_SPECIFIED)
			{
				if (stricmp(pMachine->pMachineName, "localhost") == 0)
				{
					return getHostName();
				}
				else
				{
					return pMachine->pMachineName;
				}
			}
		}
	}

	return "localHost";
}

//if we check twice in a row with no changing, then return no warnings
bool CheckRunForWarnings(ShardLauncherRun *pRun, char **ppOutWarnings)
{
	GlobalType eOverrideType;
	static U32 siLastCRC = 0;
	bool bRetVal = false;

	U32 iCurCRC = StructCRC(parse_ShardLauncherRun, gpRun);

	if (!gpRun->pOptionLibrary)
	{
		estrPrintf(ppOutWarnings, "This run has no config options... go to the modify screen and choose a patch version");
		return true;
	}

	if (iCurCRC == siLastCRC)
	{
		return false;
	}

	siLastCRC = iCurCRC;

	if (!estrLength(&gpRun->pLibraryVersion))
	{
		estrPrintf(ppOutWarnings, "This run has no version for its config options. This probably means that you've just updated to a new version of ShardLauncher, which is the first version that cares about this issue. To fix this, go into the modify screen (with all the options) and just sit there for 10 seconds or so, and then run again\n");
		bRetVal = true;
	}

	if (stricmp_safe(gpRun->pLibraryVersion, gpRun->pPatchVersion) != 0)
	{
		estrPrintf(ppOutWarnings, "This run wants to patch %s, but its option library comes from patch version %s. This should generally not happen, but is not alarming. If it is not intentional, it's probably due to a patching system hiccup. Please change to a different patch version and then back, that should fix things.\n",
			gpRun->pPatchVersion, gpRun->pLibraryVersion);
		bRetVal = true;
	}

	FOR_EACH_IN_EARRAY(gpRun->ppChoices, ShardLauncherConfigOptionChoice, pChoice)
	{
		ShardLauncherConfigOption *pOption = FindConfigOption(gpRun, pChoice->pConfigOptionName, &eOverrideType);

		if (!pOption)
		{
			estrConcatf(ppOutWarnings, "Could not find option %s, with value %s. ShardLauncher options have presumably changed, this may or may not be a bad thing\n",
				pChoice->pConfigOptionName, pChoice->pValue);
			bRetVal = true;
		}
		else
		{
			FOR_EACH_IN_EARRAY(pOption->ppChoicesWhichRequireWarning, ShardLauncherConfigOptionChoiceWhichRequiresWarning, pWarningChoice)
			{
				if (stricmp(pChoice->pValue, pWarningChoice->pValue) == 0)
				{
					estrConcatf(ppOutWarnings, "%s\n", pWarningChoice->pWarningString);
					bRetVal = true;
				}
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;

	//check for eIfShardOfTypeExistsThenThisShouldBeSet and eIfThisIsSetThenShardOfTypeShouldExist
	if (gpRun->bClustered)
	{
		FOR_EACH_IN_EARRAY(gpRun->pOptionLibrary->ppLists, ShardLauncherConfigOptionList, pList)
		{
			FOR_EACH_IN_EARRAY(pList->ppOptions, ShardLauncherConfigOption, pOption)
			{
				if (pOption->eIfShardOfTypeExistsThenThisShouldBeSet 
					&& ShardOfTypeExists(gpRun, pOption->eIfShardOfTypeExistsThenThisShouldBeSet)
					&& !OptionIsSet_WithFullName(gpRun, pOption->pName))
				{
					estrConcatf(ppOutWarnings, "Your cluster includes a %s shard. This means that %s should be set, but it isn't set",
						StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pOption->eIfShardOfTypeExistsThenThisShouldBeSet), pOption->pName);
					bRetVal = true;
				}

				if (pOption->eIfThisIsSetThenShardOfTypeShouldExist && OptionIsSet_WithFullName(gpRun, pOption->pName)
					&& !ShardOfTypeExists(gpRun, pOption->eIfThisIsSetThenShardOfTypeShouldExist))
				{
					estrConcatf(ppOutWarnings, "Your cluster has %s set, but does not have a %s shard. This is probably wrong",
						pOption->pName, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pOption->eIfThisIsSetThenShardOfTypeShouldExist));
					bRetVal = true;
				}

			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	return bRetVal;
}
	



void LastMinuteFixupOptionLibrary(ShardLauncherConfigOptionLibrary *pLibrary)
{
	int i;

	//remove deprecated options... presumably ones that are now done entirely via code
	FOR_EACH_IN_EARRAY(pLibrary->ppLists, ShardLauncherConfigOptionList, pList)
	{
		for (i = eaSize(&pList->ppOptions) - 1; i >= 0; i--)
		{
			if (OptionIsDeprecated(pList->ppOptions[i]->pName))
			{
				StructDestroy(parse_ShardLauncherConfigOption, eaRemove(&pList->ppOptions, i));
			}
		}
	}
	FOR_EACH_END;
}


bool RunTheShard(ShardLauncherRun *pRun)
{
	char *pOverlordName = UtilitiesLib_GetOverlordName();

	if (pOverlordName)
	{
		char myStatusReportingName[256];
		if (gpRun->bClustered)
		{
			sprintf(myStatusReportingName, "%s_ShardLauncher", 
				gpRun->pClusterName);
		}
		else
		{
			sprintf(myStatusReportingName, "%s_ShardLauncher",
				GetNonZeroOptionByName(NULL, "SHARD_NAME"));
		}

		BeginStatusReporting(myStatusReportingName, STACK_SPRINTF("%s:%d", pOverlordName, OVERLORD_SIMPLE_STATUS_PORT),
			0);
	}

	LastMinuteFixupOptionLibrary(gpRun->pOptionLibrary);
		

	if (gpRun->bClustered)
	{
		assert(tmCreateThread(RunTheShard_Clusters, NULL));
	}
	else
	{
		assert(tmCreateThread(RunTheShard_Unclustered, NULL));
	}

	return true;
}