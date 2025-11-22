#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "ShardLauncher.h"
#include "ShardLauncher_h_ast.h"
#include "ShardLauncherUI.h"
#include "timing.h"
#include "ShardLauncherStartScreen.h"
#include "SimpleWindowManager.h"
#include "Resource.h"
#include "StringUtil.h"
#include "GlobalTypes.h"
#include "../../libs/PatchClientLib/PatchClientLibStatusMonitoring.h"
#include "net.h"
#include "CrypticPorts.h"
#include "GlobalTypes.h"
#include "GlobalTypes_h_ast.h"
#include "ShardLauncherRunTheShard.h"
#include "SentryServerComm.h"
#include "StashTable.h"
#include "ShardLauncher_pub_h_ast.h"
#include "StatusReporting.h"
#include "NameValuePair.h"



enumShardLauncherRunType gRunType;
ShardLauncherAutoRun *gpAutoRun = NULL;

bool gbTryToLoadAutoRun = false;
AUTO_CMD_INT(gbTryToLoadAutoRun, TryToLoadAutoRun);


ShardLauncherConfigOptionChoice choices[] = 
{
	{
		"SHARD_NAME",
		"AlexTest",
	},
	{
		"LAUNCH_NORMAL_SERVERS",
		"1"
	},
};



void LOG(char *pFmt, ...)
{
	char *pFullLogString = NULL;

	va_list ap;
	va_start(ap, pFmt);
	estrConcatfv(&pFullLogString, pFmt, ap);
	va_end(ap);

	if (!strEndsWith(pFullLogString, "\n"))
	{
		estrConcatf(&pFullLogString, "\n");
	}

	consoleSetColor(0, COLOR_RED | COLOR_GREEN | COLOR_BLUE);
	printf("%s: ", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()));
	consoleSetColor(COLOR_BRIGHT | COLOR_GREEN, 0);
	printf("%s", pFullLogString);

	estrDestroy(&pFullLogString);
}


void LOG_FAIL(char *pFmt, ...)
{
	char *pFullLogString = NULL;

	va_list ap;
	va_start(ap, pFmt);
	estrConcatfv(&pFullLogString, pFmt, ap);
	va_end(ap);

	if (!strEndsWith(pFullLogString, "\n"))
	{
		estrConcatf(&pFullLogString, "\n");
	}


	consoleSetColor(0, COLOR_RED | COLOR_GREEN | COLOR_BLUE);
	printf("%s: ", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()));
	consoleSetColor(COLOR_BRIGHT | COLOR_RED, 0);
	printf("SHARD LAUNCHING FAILED: %s", pFullLogString);

	estrDestroy(&pFullLogString);
}


void LOG_WARNING(char *pFmt, ...)
{
	char *pFullLogString = NULL;

	va_list ap;
	va_start(ap, pFmt);
	estrConcatfv(&pFullLogString, pFmt, ap);
	va_end(ap);

	if (!strEndsWith(pFullLogString, "\n"))
	{
		estrConcatf(&pFullLogString, "\n");
	}


	consoleSetColor(0, COLOR_RED | COLOR_GREEN | COLOR_BLUE);
	printf("%s: ", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()));
	consoleSetColor(COLOR_BRIGHT | COLOR_RED | COLOR_GREEN, 0);
	printf("SHARD LAUNCHING FAILED: %s", pFullLogString);

	estrDestroy(&pFullLogString);
}

void GetArchivedFilenameForRun(char **ppOutFileName, ShardLauncherRun *pRun)
{
	char *pTemp = NULL;
	char *pTemp2 = NULL;
	estrStackCreate(&pTemp);
	estrStackCreate(&pTemp2);

	estrCopy2(&pTemp, pRun->pRunName);
	estrMakeAllAlphaNumAndUnderscores(&pTemp);

	estrCopy2(&pTemp2, timeGetLocalDateStringFromSecondsSince2000(pRun->iLastModifiedTime));
	estrMakeAllAlphaNumAndUnderscores(&pTemp2);

	estrPrintf(ppOutFileName, ARCHIVE_RUNS_FOLDER "/%s_%s.txt", pTemp, pTemp2);
	
	estrDestroy(&pTemp);
	estrDestroy(&pTemp2);

}

void GetFilenameForRun(char **ppOutFileName, char *pRunName)
{
	char *pTemp = NULL;
	estrStackCreate(&pTemp);
	estrCopy2(&pTemp, pRunName);
	estrMakeAllAlphaNumAndUnderscores(&pTemp);
	estrPrintf(ppOutFileName, RECENT_RUNS_FOLDER "/%s.txt", pTemp);
	
	estrDestroy(&pTemp);
}


void SaveRunToDisk(ShardLauncherRun *pRun, U32 iCurTime)
{
	static char *pFileName = NULL;
	static char *pArchivedName = NULL;

	GetFilenameForRun(&pFileName, pRun->pRunName);
	mkdirtree_const(pFileName);


	if (fileExists(pFileName))
	{
		ShardLauncherRun *pSavedRun = StructCreate(parse_ShardLauncherRun);

		ParserReadTextFile(pFileName, parse_ShardLauncherRun, pSavedRun, 0);

		if (StructCompare(parse_ShardLauncherRun, pRun, pSavedRun, COMPAREFLAG_EMPTY_STRINGS_MATCH_NULL_STRINGS, 0, TOK_USEROPTIONBIT_1) != 0 || pRun->iLastModifiedTime == 0)
		{
			pRun->iLastModifiedTime = iCurTime;

			//saved run is going to be replaced... archive it
			GetArchivedFilenameForRun(&pArchivedName, pSavedRun);
			ParserWriteTextFile(pArchivedName, parse_ShardLauncherRun, pSavedRun, 0, 0); 

		}

		StructDestroy(parse_ShardLauncherRun, pSavedRun);
	}
	else
	{
		pRun->iLastModifiedTime = iCurTime;
	}
		
	ParserWriteTextFile(pFileName, parse_ShardLauncherRun, pRun, 0, 0);
}


ShardLauncherRun *LoadRunFromName(char *pRunName)
{
	char *pFileName = NULL;
	ShardLauncherRun *pRun = StructCreate(parse_ShardLauncherRun);
	GetFilenameForRun(&pFileName, pRunName);
	
	if (!ParserReadTextFile(pFileName, parse_ShardLauncherRun, pRun, 0))
	{
		LOG_FAIL("Something went wrong while reading %s", pFileName);
		StructDestroy(parse_ShardLauncherRun, pRun);
		pRun = NULL;
	}

	if (pRun && pRun->pTemplateFileName)
	{
		ShardLauncherRun *pTemplate = StructCreate(parse_ShardLauncherRun);
		if (!ParserReadTextFile(pRun->pTemplateFileName, parse_ShardLauncherRun, pTemplate, 0))
		{
			StructDestroy(parse_ShardLauncherRun, pTemplate);
			LOG_FAIL("Couldn't read template file %s", pRun->pTemplateFileName);
			StructDestroy(parse_ShardLauncherRun, pRun);
			pRun = NULL;
		}
		else
		{
			eaDestroyStruct(&pRun->ppTemplateChoices, parse_ShardLauncherConfigOptionChoice);
			pRun->ppTemplateChoices = pTemplate->ppChoices;
			pTemplate->ppChoices = NULL;
			StructDestroy(parse_ShardLauncherRun, pTemplate);


		}
	}

	estrDestroy(&pFileName);
	return pRun;
}


ShardLauncherConfigOptionLibrary *LoadAndFixupOptionLibrary(char *pBuffer, char **ppErrorString)
{
	int iListNum;
	int iOptionNum;
	int iThingToDoNum;
	int i;

	ShardLauncherConfigOptionLibrary *pLibrary = StructCreate(parse_ShardLauncherConfigOptionLibrary);

	if (!ParserReadText(pBuffer, parse_ShardLauncherConfigOptionLibrary,
		pLibrary, 0))
	{
		char *pTempString = NULL;
		
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserReadText(pBuffer, parse_ShardLauncherConfigOptionLibrary, pLibrary, 0);
		ErrorfPopCallback();


		estrPrintf(ppErrorString, "ParserReadText failed: %s", pTempString);
		estrDestroy(&pTempString);
		StructDestroySafe(parse_ShardLauncherConfigOptionLibrary, &pLibrary);
		return NULL;
	}

	//when global types are changing, we may end up with invalid types in this list
	for (i=ea32Size(&pLibrary->pServerSpecificScreenTypes)-1; i >= 0; i--)
	{
		if (!pLibrary->pServerSpecificScreenTypes[i])
		{
			ea32RemoveFast((int**)&pLibrary->pServerSpecificScreenTypes, i);
		}
	}


	for (iListNum = 0; iListNum < eaSize(&pLibrary->ppLists) + (pLibrary->pServerTypeSpecificList ? 1 : 0); iListNum++)
	{
		ShardLauncherConfigOptionList *pList;
		
		if (iListNum == eaSize(&pLibrary->ppLists))
		{
			pList = pLibrary->pServerTypeSpecificList;
		}
		else
		{
			pList = pLibrary->ppLists[iListNum];
		}

		for (iOptionNum = 0; iOptionNum < eaSize(&pList->ppOptions); iOptionNum++)
		{
			ShardLauncherConfigOption *pOption = pList->ppOptions[iOptionNum];

			//check for $$ in any of the strings. If there are none, this must be a bool option
			pOption->bIsBool = true;

			for (iThingToDoNum=0 ; iThingToDoNum < eaSize(&pOption->ppThingsToDoIfSet); iThingToDoNum++)
			{
				ShardLauncherConfigOptionThingToDo *pThingToDo = pOption->ppThingsToDoIfSet[iThingToDoNum];

				if (pThingToDo->pString)
				{
					if (strstri(pThingToDo->pString, "$$"))
					{
						pOption->bIsBool = false;
						break;
					}
				}
			}

			if (pOption->bIsBool && pOption->ppChoices)
			{
				estrPrintf(ppErrorString, "Option %s has choices but has no $$. This makes no sense.",
					pOption->pName);
				StructDestroySafe(parse_ShardLauncherConfigOptionLibrary, &pLibrary);
				return NULL;
			}
		}

		for (i=0; i < ea32Size(&pList->pGlobalTypesForExtraCommandLineSetting); i++)
		{
			char *pTemp = NULL;

			ShardLauncherConfigOption *pSharedOption = StructCreate(parse_ShardLauncherConfigOption);
			ShardLauncherConfigOption *pFirstTimeOption = StructCreate(parse_ShardLauncherConfigOption);
			GlobalType eType = pList->pGlobalTypesForExtraCommandLineSetting[i];
			char *pTypeName = GlobalTypeToName(eType);
			ShardLauncherConfigOptionThingToDo *pThingToDo;

			estrPrintf(&pTemp, "%s_CMDLINE", pTypeName);
			strupr(pTemp);
			pSharedOption->pName = strdup(pTemp);
			estrPrintf(&pTemp, "Add custom options for all servers of type %s that are launched",
				pTypeName);
			pSharedOption->pDescription = strdup(pTemp);
			pThingToDo = StructCreate(parse_ShardLauncherConfigOptionThingToDo);
			pThingToDo->eServerType_MightNeedFixup= eType;
			pThingToDo->eType = THINGTODOTYPE_SERVERTYPECOMMANDLINE;
			pThingToDo->pString = strdup("$$");

			eaPush(&pSharedOption->ppThingsToDoIfSet, pThingToDo);

			estrPrintf(&pTemp, "1ST_%s_CMDLINE", pTypeName);
			strupr(pTemp);
			pFirstTimeOption->pName = strdup(pTemp);
			estrPrintf(&pTemp, "Add custom options for first server of type %s that is launched",
				pTypeName);
			pFirstTimeOption->pDescription = strdup(pTemp);
			pThingToDo = StructCreate(parse_ShardLauncherConfigOptionThingToDo);
			pThingToDo->eServerType_MightNeedFixup = eType;
			pThingToDo->eType = THINGTODOTYPE_FIRSTSERVEROFTYPECOMMANDLINE;
			pThingToDo->pString = strdup("$$");

			eaPush(&pFirstTimeOption->ppThingsToDoIfSet, pThingToDo);

			eaPush(&pList->ppOptions, pSharedOption);
			eaPush(&pList->ppOptions, pFirstTimeOption);

			estrDestroy(&pTemp);
		}
	}

	return pLibrary;
}

//compatibilty hack to force old CONTROLLER_CMDLINE and 1ST_CONTROLLER_CMDLINE options to get moved over to
//"[Controller] 1ST_CMDLINE" format
AUTO_FIXUPFUNC;
TextParserResult ShardLauncherConfigOptionChoiceFixup(ShardLauncherConfigOptionChoice *pChoice, enumTextParserFixupType eFixupType, void *pExtraData)
{
	char temp[1024];

	switch (eFixupType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		if (strStartsWith(pChoice->pConfigOptionName, "1ST_") && strEndsWith(pChoice->pConfigOptionName, "_CMDLINE"))
		{
			int iLen = (int)strlen(pChoice->pConfigOptionName);
			GlobalType eServerType;
			memcpy(temp, pChoice->pConfigOptionName + 4, iLen - 12);
			temp[iLen - 12] = 0;

			eServerType = NameToGlobalType(temp);
			if (eServerType && eServerType != GLOBALTYPE_CLIENT)
			{
				free(pChoice->pConfigOptionName);
				pChoice->pConfigOptionName = strdup(GetServerTypeSpecificOptionName("1ST_CMDLINE", eServerType));
			}
		}
		else if (strEndsWith(pChoice->pConfigOptionName, "_CMDLINE"))
		{
			int iLen = (int)strlen(pChoice->pConfigOptionName);
			GlobalType eServerType;
			memcpy(temp, pChoice->pConfigOptionName, iLen - 8);
			temp[iLen - 8] = 0;

			eServerType = NameToGlobalType(temp);
			if (eServerType && eServerType != GLOBALTYPE_CLIENT)
			{
				free(pChoice->pConfigOptionName);
				pChoice->pConfigOptionName = strdup(GetServerTypeSpecificOptionName("ALL_CMDLINES", eServerType));
			}		
		}	
	}
	
	return PARSERESULT_SUCCESS;
}



#define QUITFAIL() { printf("Press any key...\n"); c = _getch(); return -1; }

ShardLauncherRun *gpRun = NULL;

int giPatchSucceededCount = 0;

void PCLStatusCB(PCLStatusMonitoringUpdate *pUpdate)
{
	switch (pUpdate->internalStatus.eState)
	{
	case PCLSMS_SUCCEEDED:
		RunTheShard_Log("Patching has succeeded for %s", pUpdate->internalStatus.pMyIDString);
		giPatchSucceededCount++;
		break;


	case PCLSMS_FAILED:
		RunTheShard_Log("Patching has failed for %s: %s", pUpdate->internalStatus.pMyIDString,
			pUpdate->internalStatus.pUpdateString);
		break;
	
	case PCLSMS_FAILED_TIMEOUT:
		RunTheShard_Log("Patching has timed out fatally for %s", pUpdate->internalStatus.pMyIDString);
		break;
	}
}

StashTable sMachineNamesFromSentryServer = NULL;

void GetMachinesFromSentryServerCB(SentryMachines_FromSimpleQuery *pList, void *pUserData)
{
	sMachineNamesFromSentryServer = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);
	FOR_EACH_IN_EARRAY(pList->ppMachines, char, pMachine)
	{
		stashAddPointer(sMachineNamesFromSentryServer, pMachine, NULL, true);
	}
	FOR_EACH_END;
}


void GetMachinesFromSentryServer(void)
{
	int iCounter = 0;
	SentryServerComm_QueryForMachines_Simple(GetMachinesFromSentryServerCB, NULL);

	while (!sMachineNamesFromSentryServer && iCounter < 5000)
	{
		commMonitor(commDefault());
		SentryServerComm_Tick();
		Sleep(1);
		iCounter++;
	}
}


void LoadAutoRun(void)
{
	printf("About to check if %s exists\n", SHARDLAUNCHER_AUTORUN_FILE_NAME);
	if (fileExists(SHARDLAUNCHER_AUTORUN_FILE_NAME))
	{
		char *pErrors = NULL;
		printf("It exists\n");
		gpAutoRun = StructCreate(parse_ShardLauncherAutoRun);
		if (!ParserReadTextFile_CaptureErrors(SHARDLAUNCHER_AUTORUN_FILE_NAME, parse_ShardLauncherAutoRun, gpAutoRun, 0, &pErrors))
		{
			assertmsgf(0, "Parse errors while reading ShardLauncher AutoRun out of %s: %s",
				SHARDLAUNCHER_AUTORUN_FILE_NAME, pErrors);
		}
		estrDestroy(&pErrors);

	
		assertmsgf(estrLength(&gpAutoRun->pRunName), "Loaded ShardLauncher AutoRun out of %s, it had no RunName",
			SHARDLAUNCHER_AUTORUN_FILE_NAME);
	}
	else
	{
		printf("It does NOT exist\n");
	}

}



int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	bool bNeedToConfigure = false;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);

	fileAllPathsAbsolute(true);

	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	{
		U32 *pTimeouts = NULL;
		ea32Push(&pTimeouts, 120);
		if (!PCLStatusMonitoring_Begin(commDefault(), PCLStatusCB, SHARDLAUNCHER_PATCHSTATUS_PORT, pTimeouts, 
			10000000, 120))
		{
			assertmsgf(0, "Unable to start PCL status monitoring... is another copy of ShardLauncher running?");
		}
		ea32Destroy(&pTimeouts);

	}

	GetMachinesFromSentryServer();

	if (gbTryToLoadAutoRun)
	{
		LoadAutoRun();
	}

	SimpleWindowManager_Init("ShardLauncher", false);
	SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_STARTINGSCREEN,
		0, IDD_STARTSCREEN, true, startScreenDlgProc_SWM, startScreenDlgProc_SWMTick, NULL);
	SimpleWindowManager_Run(NULL, NULL);


	EXCEPTION_HANDLER_END

}


//takes in a short option name, finds the highest priority set version of it
ShardLauncherConfigOptionChoice *FindHighestPriorityChoice(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pShortOptionName)
{
	ShardLauncherConfigOptionChoice *pChoice;

	if (!pRun->bClustered || !pShard)
	{
		return eaIndexedGetUsingString(&pRun->ppChoices, pShortOptionName);
	}

	pChoice = eaIndexedGetUsingString(&pRun->ppChoices, GetShardOrShardTypeSpecificOptionName(pShortOptionName, pShard->pShardName));
	if (pChoice)
	{
		return pChoice;
	}

	pChoice = eaIndexedGetUsingString(&pRun->ppChoices, GetShardOrShardTypeSpecificOptionName(pShortOptionName, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pShard->eShardType)));
	if (pChoice)
	{
		return pChoice;
	}

	pChoice = eaIndexedGetUsingString(&pRun->ppChoices, pShortOptionName);
	if (pChoice)
	{
		return pChoice;
	}

	return NULL;
}


char *GetNonZeroOptionByName(ShardLauncherClusterShard *pShard, char *pOptionName)
{
	ShardLauncherConfigOptionChoice *pChoice;

	if (!gpRun)
	{
		return NULL;
	}

	pChoice = FindHighestPriorityChoice(gpRun, pShard, pOptionName);
	if (!pChoice)
	{
		pChoice = eaIndexedGetUsingString(&gpRun->ppTemplateChoices, pOptionName);
	}

	if (pChoice)
	{
		if (stricmp_safe(pChoice->pValue, "0") == 0)
		{
			return NULL;
		}

		return pChoice->pValue;
	}

	return NULL;
}


char *GetServerTypeSpecificOptionName(const char *pName, GlobalType eType)
{
	static char temp[1024];
	sprintf(temp, "[%s] %s", GlobalTypeToName(eType), pName);
	return temp;
}

char *GetShardOrShardTypeSpecificOptionName(const char *pName, const char *pShardNameOrType)
{
	static char temp[1024];
	sprintf(temp, "{%s} %s", pShardNameOrType, pName);
	return temp;
}


static BuiltInOptionForShardType sBuiltInOptions[] = 
{
	{
		SHARDTYPE_UGC,
		"ENABLE_UGC",
		"1"
	}
};

BuiltInOptionForShardType **gppBuiltInOptionsForShardType = NULL;

AUTO_RUN;
void InitBuildInOptions(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sBuiltInOptions); i++)
	{
		eaPush(&gppBuiltInOptionsForShardType, &sBuiltInOptions[i]);
	}
}

StatusReporting_SelfReportedState gStatusReportingState = STATUS_SHARDLAUNCHER_STARTINGUP;

int OVERRIDE_LATELINK_StatusReporting_GetSelfReportedState(void)
{
	return gStatusReportingState;
}


NameValuePairList *OVERRIDE_LATELINK_StatusReporting_GetSelfReportedNamedValuePairs(void)
{
	static NameValuePairList *spRetVal = NULL;
	static NameValuePair *spVersion = NULL;
	if (!spRetVal)
	{
		spRetVal = StructCreate(parse_NameValuePairList);
		spVersion = StructCreate(parse_NameValuePair);
		spVersion->pName = strdup("PatchVersion");
		eaPush(&spRetVal->ppPairs, spVersion);
	}

	SAFE_FREE(spVersion->pValue);
	if (gpRun->pPatchVersion)
	{
		spVersion->pValue = strdup(gpRun->pPatchVersion);
	}

	return spRetVal;
}


#include "ShardLauncher_h_ast.c"
#include "ShardLauncher_pub_h_ast.c"
