#include "CBConfig.h"
#include "CBStartup.h"
#include "CBConfig_h_Ast.h"
#include "TextParser.h"
#include "estring.h"
#include "earray.h"
#include "Stringcache.h"
#include "file.h"
#include "StringUtil.h"
#include "structinternals.h"
#include "BuildScripting.h"
#include "GimmeUtils.h"
#include "xboxHostIo.h"
#include "timing.h"
#include "wininclude.h"
#include "NetSMTP.h"
#include "net/net.h"
#include "structinternals_h_ast.h"
#include "ContinuousBuilder.h"
#include "Organization.h"
#include "ContinuousBuilder_h_ast.h"


CBConfig gConfig = {0};

//true if at least one config variable had either $ONCE_A_DAY(foo) or $ALL_BUT_ONCE_A_DAY(foo) st
static bool sbHadOnceADayVariables = false;

//looks at the list of ppFieldNamesNotToInherit in pRefConfig. Clears
//all those fields in pTargetConfig
void ClearFieldsNotToInherit(CBConfig *pTargetConfig, CBConfig *pRefConfig)
{
	int i;

	for (i=0; i < eaSize(&pRefConfig->ppFieldNamesNotToInherit); i++)
	{
		ParseTable *pTPI;
		void *pStruct;
		int iColumn;
		int iIndex;

		if (ParserResolvePath(pRefConfig->ppFieldNamesNotToInherit[i], parse_CBConfig, pTargetConfig,
			&pTPI, &iColumn, &pStruct, &iIndex, NULL, NULL, 0))
		{
			destroystruct_autogen(pTPI, &pTPI[iColumn], iColumn, pStruct, 0);
		}
		else
		{
			assertmsgf(0, "Invalid xpath %s for FieldNameNotToInherit", pRefConfig->ppFieldNamesNotToInherit[i]);
		}
	}
}

static StartingVariable *FindStartingVariableByName(CBConfig *pConfig, char *pName)
{
	int i;
	for (i = 0; i < eaSize(&pConfig->ppStartingVariable); i++)
	{
		if (stricmp(pConfig->ppStartingVariable[i]->pVarName, pName) == 0)
		{
			return pConfig->ppStartingVariable[i];
		}
	}

	return NULL;
}
static OverrideableVariable *FindOverrideableByName(CBConfig *pConfig, char *pName)
{
	int i;
	for (i = 0; i < eaSize(&pConfig->ppOverrideableVariables); i++)
	{
		if (stricmp(pConfig->ppOverrideableVariables[i]->pVarName, pName) == 0)
		{
			return pConfig->ppOverrideableVariables[i];
		}
	}
	return NULL;
}
static OverrideableVariable *FindDevOnlyOverrideableByName(CBConfig *pConfig, char *pName)
{
	int i;
	for (i = 0; i < eaSize(&pConfig->ppDevOnlyOverrideableVariables); i++)
	{
		if (stricmp(pConfig->ppDevOnlyOverrideableVariables[i]->pVarName, pName) == 0)
		{
			return pConfig->ppDevOnlyOverrideableVariables[i];
		}
	}
	return NULL;
}

//given two CBConfigs, look at all startingVars, overrideables, and devOnlyOverrideables in the pNew. Delete
//all in pOld which match any of their names but are of a different type. Then when pNew is loaded over pOld,
//we won't ever have a startingVariable and an Overrideable with the same name
void CB_DoVariableReplacing(CBConfig *pOld, CBConfig *pNew)
{
	int i;

	for (i=eaSize(&pOld->ppStartingVariable) - 1; i >= 0; i--)
	{
		if (FindOverrideableByName(pNew, pOld->ppStartingVariable[i]->pVarName)
			|| FindDevOnlyOverrideableByName(pNew, pOld->ppStartingVariable[i]->pVarName)
			|| FindStartingVariableByName(pNew, pOld->ppStartingVariable[i]->pVarName))
		{
			StructDestroy(parse_StartingVariable, pOld->ppStartingVariable[i]);
			eaRemove(&pOld->ppStartingVariable, i);
		}
	}

	for (i=eaSize(&pOld->ppOverrideableVariables) - 1; i >= 0; i--)
	{
		if (FindStartingVariableByName(pNew, pOld->ppOverrideableVariables[i]->pVarName)
			|| FindDevOnlyOverrideableByName(pNew, pOld->ppOverrideableVariables[i]->pVarName)
			|| FindOverrideableByName(pNew, pOld->ppOverrideableVariables[i]->pVarName))
		{
			StructDestroy(parse_OverrideableVariable, pOld->ppOverrideableVariables[i]);
			eaRemove(&pOld->ppOverrideableVariables, i);
		}
	}

	for (i=eaSize(&pOld->ppDevOnlyOverrideableVariables) - 1; i >= 0; i--)
	{
		if (FindStartingVariableByName(pNew, pOld->ppDevOnlyOverrideableVariables[i]->pVarName)
			|| FindOverrideableByName(pNew, pOld->ppDevOnlyOverrideableVariables[i]->pVarName)
			|| FindDevOnlyOverrideableByName(pNew, pOld->ppDevOnlyOverrideableVariables[i]->pVarName))
		{
			StructDestroy(parse_OverrideableVariable, pOld->ppDevOnlyOverrideableVariables[i]);
			eaRemove(&pOld->ppDevOnlyOverrideableVariables, i);
		}
	}
}


void SendEmailToNetops(char *pInFailureString, ...)
{
	static U32 siLastTime = 0;
	U32 iCurTime = timeSecondsSince2000_ForceRecalc();
	SMTPMessageRequest *pReq;
	char *pResultEStr = NULL;

	static char *pFailureString = NULL;


	estrGetVarArgs(&pFailureString, pInFailureString);

	printf("Sending this message to Netops: %s\n", pFailureString);

	if (siLastTime > iCurTime - (20 * 60 * 60))
	{
		return;
	}



	siLastTime = iCurTime;


	pReq = StructCreate(parse_SMTPMessageRequest);


	eaPush(&pReq->to, estrDup("CBStartupErrors"));
	
	estrPrintf(&pReq->from, "%s", getHostName());
	estrConcatf(&pReq->from, "@"ORGANIZATION_DOMAIN);

	estrPrintf(&pReq->subject, "CB error on %s before configs loaded", getHostName());
	estrPrintf(&pReq->body, "%s", pFailureString);

	pReq->priority = 1;
	siLastTime = iCurTime;
	
	pReq->pResultCBFunc = GenericSendEmailResultCB;
	pReq->pUserData = strdup(pReq->subject);

	smtpMsgRequestSend_BgThread(pReq);

	estrDestroy(&pResultEStr);

	StructDestroy(parse_SMTPMessageRequest, pReq);

}

//specially hacked-up version of Gimme_UpdateFileToTime which doesn't echo output and doesn't
//complain if the file doesn't exist

bool Gimme_UpdateFileIfExists(char *pFileName)
{
	char systemString[1024];
	int iRetVal;
	QueryableProcessHandle *pHandle;

	U32 iStartingTime = timeSecondsSince2000_ForceRecalc();

	mkdirtree_const("c:\\temp\\test.txt");

	sprintf(systemString, "gimme -glvfile %s > c:\\temp\\CB_gimme_temp.txt", pFileName);


	if (!(pHandle = StartQueryableProcess(systemString, NULL, true, false, false, NULL)))
	{
		SendEmailToNetops("Couldn't launch Gimme.exe to get latest version of %s", pFileName);
		return false;
	}

	while (1)
	{

		if (QueryableProcessComplete(&pHandle, &iRetVal))
		{
			break;
		}

		if (timeSecondsSince2000_ForceRecalc() - iStartingTime > 600)
		{
		
			KillQueryableProcess(&pHandle);
			SendEmailToNetops("Gimme.exe took more than 10 minutes while getting latest version of %s", pFileName);
			return false;
		}

		Sleep(1);
	}

	if (iRetVal != 0)
	{
		char *pBuf = fileAlloc("c:\\temp\\CB_gimme_temp.txt", NULL);
		SendEmailToNetops("Gimme.exe returned an error while getting %s. Console output: %s", pFileName, pBuf);
		SAFE_FREE(pBuf);

		return false;
	}

	return true;

	
}



void CB_LoadConfig(bool bFirstTime)
{
	char *pFileName = NULL;
	static char **ppFileNames = NULL;
	int i, j;
	bool gbDontGet = false;
	bool bResult;

	OverrideableVariable **ppOverrideableVariables = NULL;
	OverrideableVariable **ppDevOnlyOverrideableVariables = NULL;

	if (!gbFastTestingMode)
	{
		bResult = system_w_timeout("gimme -glvfold c:/cryptic -nopause", NULL, 600);
		if (bResult)
		{
			printf("\n\n\n\n\n\n\n\n\n\n\nERROR ERROR EROR Gimme get of c:/cryptic failed\n\n\n\n\n\n\n\n");
		}
	}

	
	//after the first time, reuse the overrides already set
	if (!bFirstTime)
	{
		ppOverrideableVariables = gConfig.ppOverrideableVariables;
		ppDevOnlyOverrideableVariables = gConfig.ppDevOnlyOverrideableVariables;

		gConfig.ppOverrideableVariables = NULL;
		gConfig.ppDevOnlyOverrideableVariables = NULL;
	}






	StructDeInit(parse_CBConfig, &gConfig);
	StructInit(parse_CBConfig, &gConfig);

	//first, load c:\continuousBuilder\CBConfig.txt just to see if we're in dev mode
	{
		CBConfig tempConfig = {0};
		StructInit(parse_CBConfig, &tempConfig);

		ParserReadTextFile("c:\\continuousbuilder\\cbconfig.txt", parse_CBConfig, &tempConfig, 0);

		if (tempConfig.bDev)
		{

			gbDontGet = true;
		}

		StructDeInit(parse_CBConfig, &tempConfig);
	}



	if (!ppFileNames)
	{
		for (i=0; i < eaSize(&gProductAndTypeList.ppConfigFileLocation); i++)
		{
			estrCopy2(&pFileName, gProductAndTypeList.ppConfigFileLocation[i]);

			estrReplaceOccurrences(&pFileName, "SHORTPRODUCTNAME", gpCBProduct->pShortProductName);
			estrReplaceOccurrences(&pFileName, "PRODUCTNAME", gpCBProduct->pProductName);


			//if the config file location contains TYPENAME, we need to apply the type inheritance
			if (strstr(pFileName, "TYPENAME"))
			{
				//funny for loop with <= instead of <, final iteration means use actual product name
				for (j = 0; j <= eaSize(&gpCBType->ppInheritsFrom); j++)
				{
					char *pFileName2;
					estrStackCreate(&pFileName2);
					estrCopy(&pFileName2, &pFileName);
					estrReplaceOccurrences(&pFileName2, "TYPENAME", j == eaSize(&gpCBType->ppInheritsFrom) ? gpCBType->pShortTypeName : gpCBType->ppInheritsFrom[j]);

					if (eaFindString(&ppFileNames, pFileName2) == -1)
					{
						eaPush(&ppFileNames, strdup(pFileName2));
					}

					estrDestroy(&pFileName2);
				}
			}
			else
			{
				if (eaFindString(&ppFileNames, pFileName) == -1)
				{
					eaPush(&ppFileNames, strdup(pFileName));
				}
			}
		}

		estrDestroy(&pFileName);

	}


	for (i=0; i < eaSize(&ppFileNames); i++)
	{
		if (!gpCBType->bDontUpdateConfigFiles && !gbDontGet && !strStartsWith(ppFileNames[i], "n:") && !strStartsWith(ppFileNames[i], "c:\\continuousbuilder"))
		{
			while (!Gimme_UpdateFileIfExists(ppFileNames[i])) {};
		}

		if (fileExists(ppFileNames[i]))
		{
			CBConfig tempConfig = {0};

			printf("Reading config from %s\n", ppFileNames[i]);

			StructInit(parse_CBConfig, &tempConfig);

			ParserReadTextFile(ppFileNames[i], parse_CBConfig, &tempConfig, 0);

			ClearFieldsNotToInherit(&gConfig, &tempConfig);

			CB_DoVariableReplacing(&gConfig, &tempConfig);

			StructDeInit(parse_CBConfig, &tempConfig);


			ParserReadTextFile(ppFileNames[i], parse_CBConfig, &gConfig, 0);
		}
		else
		{
			printf("Can't read config from %s... doesn't exist\n", ppFileNames[i]);
		}
	}

	//replace PRODUCTNAME with productname in directory names
	for (i=0; i < eaSize(&gConfig.ppSVNFolders); i++)
	{
		estrReplaceOccurrences(&gConfig.ppSVNFolders[i], "PRODUCTNAME", gpCBProduct->pProductName);
	}
	for (i=0; i < eaSize(&gConfig.ppGimmeFolders); i++)
	{
		estrReplaceOccurrences(&gConfig.ppGimmeFolders[i], "PRODUCTNAME", gpCBProduct->pProductName);
	}
	for (i=0; i < eaSize(&gConfig.ppScriptDirectories); i++)
	{
		estrReplaceOccurrences(&gConfig.ppScriptDirectories[i], "PRODUCTNAME", gpCBProduct->pProductName);
	}

	//remove all duplications among the various earrays we load. This is so that we can list
	//c:\core and c:\productname without ending up with two copies of c:\core for core builders,
	//and to avoid duplication of other config options as the config is loaded spread among many files
	eaRemoveDuplicateEStrings(&gConfig.ppSVNFolders);
	eaRemoveDuplicateEStrings(&gConfig.ppGimmeFolders);
	eaRemoveDuplicateEStrings(&gConfig.ppScriptDirectories);

	eaRemoveDuplicateStrings(&gConfig.continuousConfig.ppPeopleWhoAlwaysWantCheckinNotification);
	eaRemoveDuplicateStrings(&gConfig.ppDefaultEmailRecipient);
	eaRemoveDuplicateStrings(&gConfig.ppAdministrator);


	//now remove duplicate starting variables
	for (i=0; i < eaSize(&gConfig.ppStartingVariable) - 1; i++)
	{
		j = i+1;

		while (j < eaSize(&gConfig.ppStartingVariable))
		{
			if (stricmp(gConfig.ppStartingVariable[i]->pVarName, gConfig.ppStartingVariable[j]->pVarName) == 0)
			{
				SAFE_FREE(gConfig.ppStartingVariable[i]->pVarValue);
				gConfig.ppStartingVariable[i]->pVarValue = gConfig.ppStartingVariable[j]->pVarValue;
				gConfig.ppStartingVariable[j]->pVarValue = 0;
				StructDestroy(parse_StartingVariable, gConfig.ppStartingVariable[j]);
				eaRemove(&gConfig.ppStartingVariable, j);
			}
			else
			{
				j++;
			}
		}
	}



	if (!bFirstTime)
	{
		eaDestroyStruct(&gConfig.ppOverrideableVariables, parse_OverrideableVariable);
		eaDestroyStruct(&gConfig.ppDevOnlyOverrideableVariables, parse_OverrideableVariable);

		gConfig.ppOverrideableVariables = ppOverrideableVariables;
		gConfig.ppDevOnlyOverrideableVariables = ppDevOnlyOverrideableVariables;
	}

	if (!gConfig.sVariablesToReportToCBMonitor)
	{
		gConfig.sVariablesToReportToCBMonitor = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);

		for (i = 0; i < eaSize(&gConfig.ppVariablesToReportToCBMonitor); i++)
		{
			char *pFixedUpName = NULL;
			estrCopy2(&pFixedUpName, gConfig.ppVariablesToReportToCBMonitor[i]);
			if (!strStartsWith(pFixedUpName, "$"))
			{
				estrInsertf(&pFixedUpName, 0, "$");
			}

			if (!strEndsWith(pFixedUpName, "$"))
			{
				estrConcatf(&pFixedUpName, "$");
			}
			stashAddPointer(gConfig.sVariablesToReportToCBMonitor, pFixedUpName, NULL, false);
		}

		for (i = 0; i < eaSize(&gConfig.ppVariablesToReportToCBMonitorOnHeartbeat); i++)
		{
			char *pFixedUpName = NULL;
			estrCopy2(&pFixedUpName, gConfig.ppVariablesToReportToCBMonitorOnHeartbeat[i]);
			if (!strStartsWith(pFixedUpName, "$"))
			{
				estrInsertf(&pFixedUpName, 0, "$");
			}

			if (!strEndsWith(pFixedUpName, "$"))
			{
				estrConcatf(&pFixedUpName, "$");
			}
			stashAddPointer(gConfig.sVariablesToReportToCBMonitor, pFixedUpName, NULL, false);
		}
	}

	gConfig.bLoaded = true;
}

void GetCommaSeparatedList(char **ppOutStr, char ***pppNames)
{
	int i;

	estrClear(ppOutStr);
	for (i = 0; i < eaSize(pppNames); i++)
	{
		estrConcatf(ppOutStr, "%s%s", i > 0 ? ", " : "", (*pppNames)[i]);
	}
}

char *ProcessVarStringForOnceADay(const char *pInString)
{
	static char *pRetString = NULL;
	estrCopy2(&pRetString, pInString);

	if (estrResolveOnOffParenMacro(&pRetString, "$ONCE_A_DAY", gbIsOnceADayBuild))
	{
		sbHadOnceADayVariables = true;
	}

	if (estrResolveOnOffParenMacro(&pRetString, "$ALL_BUT_ONCE_A_DAY", !gbIsOnceADayBuild))
	{
		sbHadOnceADayVariables = true;
	}

	return pRetString;
}

void CB_SetScriptingVariablesFromConfig(void)
{
	int i;

	char *pTemp = NULL;

	sbHadOnceADayVariables = false;

	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "PRODUCTNAME", gpCBProduct->pProductName, "BUILTIN_STARTUP");
	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "SHORTPRODUCTNAME", gpCBProduct->pShortProductName, "BUILTIN_STARTUP");

	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "CBTYPE", gpCBType->pShortTypeName, "BUILTIN_STARTUP");
	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "CBTYPE_VERBOSE", gpCBType->pTypeName, "BUILTIN_STARTUP");

	GetCommaSeparatedList(&pTemp, &gConfig.ppSVNFolders);
	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "SVN_FOLDERS", pTemp, "BUILTIN_STARTUP");

	GetCommaSeparatedList(&pTemp, &gConfig.ppGimmeFolders);
	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "GIMME_FOLDERS", pTemp, "BUILTIN_STARTUP");

	if (gbIsOnceADayBuild)
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "ONCE_A_DAY", "1", "BUILTIN_STARTUP");
	}


	if (CheckConfigVarExistsAndTrue("USES_XBOX"))
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "XBOXBINDIR", xboxGetBinDir(), "BUILTIN_STARTUP");
	}

	if (gConfig.bDev)
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "CB_DEV", "1", "BUILTIN_STARTUP");
	}

	estrDestroy(&pTemp);





	for (i = 0; i < eaSize(&gConfig.ppStartingVariable); i++)
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), gConfig.ppStartingVariable[i]->pVarName, ProcessVarStringForOnceADay(gConfig.ppStartingVariable[i]->pVarValue), "Starting variable from config");
	}

	for (i=0; i < eaSize(&gConfig.ppOverrideableVariables); i++)
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), gConfig.ppOverrideableVariables[i]->pVarName, ProcessVarStringForOnceADay(gConfig.ppOverrideableVariables[i]->pCurVal), "Overrideable from config");
	}

	for (i=0; i < eaSize(&gConfig.ppDevOnlyOverrideableVariables); i++)
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), gConfig.ppDevOnlyOverrideableVariables[i]->pVarName, ProcessVarStringForOnceADay(gConfig.ppDevOnlyOverrideableVariables[i]->pCurVal), "Dev only overrideable from config");
	}

	if (sbHadOnceADayVariables)
	{
		BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "HAD_ONCE_A_DAY_VARS", "1", "BUILTIN_STARTUP");
	}

	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "LAST_BUILD_RESULT", (char*)GetLastResultStateString(), "BUILTIN_STARTUP");
	BuildScripting_AddResettableStartingVariable(CBGetRootScriptingContext(), "LAST_NON_ABORT_BUILD_RESULT", (char*)GetLastNonAbortResultStateString(), "BUILTIN_STARTUP");
}

void ReplaceConfigVarsInString(char **ppString)
{
	bool bChanged;
	char temp[256];
	int i;

	do
	{
		bChanged = false;

		for (i=0;i < eaSize(&gConfig.ppStartingVariable); i++)
		{
			sprintf(temp, "$%s$", gConfig.ppStartingVariable[i]->pVarName);
			bChanged |= estrReplaceOccurrences(ppString, temp, gConfig.ppStartingVariable[i]->pVarValue ? gConfig.ppStartingVariable[i]->pVarValue : "");
		}
			
		for (i=0;i < eaSize(&gConfig.ppOverrideableVariables); i++)
		{
			sprintf(temp, "$%s$", gConfig.ppOverrideableVariables[i]->pVarName);
			bChanged |= estrReplaceOccurrences(ppString, temp, gConfig.ppOverrideableVariables[i]->pCurVal ? gConfig.ppOverrideableVariables[i]->pCurVal : "");
		}
			
		for (i=0;i < eaSize(&gConfig.ppDevOnlyOverrideableVariables); i++)
		{
			sprintf(temp, "$%s$", gConfig.ppDevOnlyOverrideableVariables[i]->pVarName);
			bChanged |= estrReplaceOccurrences(ppString, temp, gConfig.ppDevOnlyOverrideableVariables[i]->pCurVal ? gConfig.ppDevOnlyOverrideableVariables[i]->pCurVal : "");
		}
			
	} while (bChanged);
}


//searches startingVariables, OverrideableVariables, and DevOnlyOverrideableVariables
char *GetConfigVar(char *pVarName)
{
	int i;

	for (i=0;i < eaSize(&gConfig.ppStartingVariable); i++)
	{
		if (stricmp(pVarName, gConfig.ppStartingVariable[i]->pVarName) == 0)
		{
			return gConfig.ppStartingVariable[i]->pVarValue;
		}
	}

	for (i=0;i < eaSize(&gConfig.ppOverrideableVariables); i++)
	{
		if (stricmp(pVarName, gConfig.ppOverrideableVariables[i]->pVarName) == 0)
		{
			return gConfig.ppOverrideableVariables[i]->pCurVal;
		}
	}

	for (i=0;i < eaSize(&gConfig.ppDevOnlyOverrideableVariables); i++)
	{
		if (stricmp(pVarName, gConfig.ppDevOnlyOverrideableVariables[i]->pVarName) == 0)
		{
			return gConfig.ppDevOnlyOverrideableVariables[i]->pCurVal;
		}
	}

	return NULL;
}



bool CheckConfigVar(char *pVarName)
{
	char *pVal = GetConfigVar(pVarName);

	assertmsgf(pVal, "CB Config variable %s required, but not found", pVarName);

	if (stricmp(pVal, "0") == 0 || StringIsAllWhiteSpace(pVal))
	{
		return false;
	}

	return true;
}

bool CheckConfigVarExistsAndTrue(char *pVarName)
{
	char *pVal = GetConfigVar(pVarName);

	if (!pVal)
	{
		return false;
	}

	if (stricmp(pVal, "0") == 0 || StringIsAllWhiteSpace(pVal))
	{
		return false;
	}

	return true;
}

//non-devmode CBs should always send dumps to ET when they crash
bool OVERRIDE_LATELINK_assertForceDumps(void)
{
	if (gConfig.bLoaded && !gConfig.bDev)
	{
		return true;
	}

	return false;
}

#include "CBConfig_h_ast.c"
