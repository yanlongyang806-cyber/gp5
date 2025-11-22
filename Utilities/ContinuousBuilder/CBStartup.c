#include "CBStartup.h"
#include "CBStartup_h_ast.h"
#include "textparser.h"
#include "earray.h"
#include "superassert.h"
#include "ContinuousBuilder.h"
#include "estring.h"
#include "file.h"
#include "sysUtil.h"
#include "CBConfig.h"
#include "wininclude.h"
#include "CBOverrideables.h"
#include "statusReporting.h"
#include <shlobj.h>
#include "CBStartup_c_ast.h"
#include "UTF8.h"

CBProductAndTypeList gProductAndTypeList = {0};

static char sStartupProductName[256] = "";
static char sStartupCBType[256] = "";

#define PRODUCTANDTYPEFILE "n:\\continuousbuilder\\ProductAndTypeList.txt"

CBProduct *gpCBProduct = NULL;
CBType *gpCBType = NULL;


AUTO_CMD_STRING(sStartupProductName, StartupProductName);
AUTO_CMD_STRING(sStartupCBType, StartupCBType);

//stores what prod/type choices were made last manual run
AUTO_STRUCT;
typedef struct LastRun
{
	char *pProdName;
	char *pTypeName;
} LastRun;

//given an earray of strings, how many of that array to use, and a string, returns the index of the string in the earray which is a prefix
//of pInString, and is the shortest one that is
int FindShortestPrefix(char ***pppStrings, int iNumToCheck, char *pInString)
{
	int iBestIndex = -1;
	int iBestLength = 0;
	int i;

	for (i = 0; i < iNumToCheck; i++)
	{
		char *pCurString = (*pppStrings)[i];
		if (strStartsWith(pInString, pCurString))
		{
			int iCurLen = strlen(pCurString);
			if (iBestIndex == -1 || iCurLen < iBestLength )
			{
				iBestIndex = i;
				iBestLength = iCurLen;
			}
		}
	}

	return iBestIndex;
}
				

void CB_DoStartup(void)
{
	char **ppNames = NULL;
	char **ppDescriptions = NULL;

	char *pChoice = NULL;
	char *pScriptDirSuffix = NULL;

	int i;

	bool bSomeManualPicking = false;
	bool bCreateShortCut = false;

	char batchFileName[CRYPTIC_MAX_PATH] = "";
	
	LastRun lastRun = {0};

	ParserReadTextFile("c:\\ContinuousBuilder\\LastRun.txt", parse_LastRun, &lastRun, 0);

	ParserReadTextFile(PRODUCTANDTYPEFILE, parse_CBProductAndTypeList, &gProductAndTypeList, 0);

	assertmsgf(eaSize(&gProductAndTypeList.ppProduct) > 0 || eaSize(&gProductAndTypeList.ppType) > 0, "CB can't find any product or types in %s", PRODUCTANDTYPEFILE);
	
	assertmsgf(eaSize(&gProductAndTypeList.ppConfigFileLocation), "CB Can't find any config file locations in %s", PRODUCTANDTYPEFILE);

	if (!sStartupProductName[0])
	{
		for (i=0; i < eaSize(&gProductAndTypeList.ppProduct); i++)
		{
			eaPush(&ppNames, gProductAndTypeList.ppProduct[i]->pProductName);
			eaPush(&ppDescriptions, gProductAndTypeList.ppProduct[i]->pProductDescription);
		}

		if (!PickerWithDescriptions("Choose Product", "What product is this ContinuousBuilder for?", &ppNames, &ppDescriptions, &pChoice, lastRun.pProdName))
		{
			exit(-1);
		}

		strcpy(sStartupProductName, pChoice);

		eaDestroy(&ppNames);
		eaDestroy(&ppDescriptions);

		bSomeManualPicking = true;
	}

	//at this point, we have gStartupProductName set, now validate it

	for (i=0; i < eaSize(&gProductAndTypeList.ppProduct); i++)
	{
		if (stricmp(sStartupProductName, gProductAndTypeList.ppProduct[i]->pProductName) == 0)
		{
			gpCBProduct = gProductAndTypeList.ppProduct[i];
			break;
		}
	}

	assertmsgf(gpCBProduct, "Didn't recognize product %s. Legal products are listed in %s", sStartupProductName, PRODUCTANDTYPEFILE);

	assertmsgf(dirExists("c:\\core"), "c:\\core doesn't exist... CB can not run at all\n");
	assertmsgf(dirExists(STACK_SPRINTF("c:\\%s", gpCBProduct->pProductName)), "c:\\%s doesn't exist... CB can not run at all\n",
		gpCBProduct->pProductName);
	
		


	if (!sStartupCBType[0])
	{
		for (i=0; i < eaSize(&gProductAndTypeList.ppType); i++)
		{
			eaPush(&ppNames, gProductAndTypeList.ppType[i]->pTypeName);
			eaPush(&ppDescriptions, gProductAndTypeList.ppType[i]->pTypeDescription);
		}

		if (!PickerWithDescriptions("Choose CB Type", "What type of ContinuousBuilder is this?", &ppNames, &ppDescriptions, &pChoice, lastRun.pTypeName))
		{
			exit(-1);
		}

		strcpy(sStartupCBType, pChoice);

		eaDestroy(&ppNames);
		eaDestroy(&ppDescriptions);

		bSomeManualPicking = true;
	}

	estrDestroy(&pChoice);

	for (i=0; i < eaSize(&gProductAndTypeList.ppType); i++)
	{
		if (stricmp(sStartupCBType, gProductAndTypeList.ppType[i]->pTypeName) == 0 || stricmp(sStartupCBType, gProductAndTypeList.ppType[i]->pShortTypeName) == 0)
		{
			gpCBType = gProductAndTypeList.ppType[i];
			break;
		}
	}

	assertmsgf(gpCBType, "Didn't recognize type %s. Legal types are listed in %s", sStartupCBType, PRODUCTANDTYPEFILE);


	//now we have chosen a legal type and product. If any manual picking occurred, create batch files and shortcuts

	if (bSomeManualPicking)
	{
//		char shortCutName[CRYPTIC_MAX_PATH];
		FILE *pFile;

		sprintf(batchFileName, "c:\\continuousBuilder\\%s_%s.bat", gpCBProduct->pProductName, gpCBType->pShortTypeName);
		mkdirtree_const(batchFileName);

		if (!fileExists(batchFileName))
		{


			pFile = fopen(batchFileName, "wt");

			if (pFile)
			{
				fprintf(pFile, "%s -StartupProductName %s -StartupCBType %s %%1 %%2 %%3 %%4 %%5 %%6 %%7 %%8",
					getExecutableName(), gpCBProduct->pProductName, gpCBType->pShortTypeName);
				fclose(pFile);

				bCreateShortCut = true;
			}
		}

		//also, if we did some manual picking, write back out LastRun
		SAFE_FREE(lastRun.pProdName);
		SAFE_FREE(lastRun.pTypeName);

		lastRun.pProdName = strdup(gpCBProduct->pProductName);
		lastRun.pTypeName = strdup(gpCBType->pTypeName);

		ParserWriteTextFile("c:\\ContinuousBuilder\\LastRun.txt", parse_LastRun, &lastRun, 0, 0);
	}


	//need to load config now, to see whether we need to make shortcuts
	CB_LoadConfig(true);

	if (!gConfig.bDev && bCreateShortCut)
	{
		char shortCutName[CRYPTIC_MAX_PATH];
		char *pDesktopName = NULL;
		
		char *pShortCutDesc = NULL;

		SHGetSpecialFolderPath_UTF8(NULL, &pDesktopName, CSIDL_DESKTOPDIRECTORY, 0);
		sprintf(shortCutName, "%s\\CB %s %s", pDesktopName, gpCBProduct->pProductName, gpCBType->pTypeName);

		estrPrintf(&pShortCutDesc, "Run ContinuousBuilder.exe for product %s with type \"%s\"(%s)",
			gpCBProduct->pProductName, gpCBType->pTypeName, gpCBType->pTypeDescription);


		createShortcut(batchFileName, shortCutName, 0, NULL, NULL, pShortCutDesc);

		estrDestroy(&pShortCutDesc);
		estrDestroy(&pDesktopName);
	}

	CB_DoOverrides(false);
	CB_DoOverrides(true);

	//special case to set the utilitiesLib LeaveCrashesUpForever var
	gbLeaveCrashesUpForever = CheckConfigVarExistsAndTrue("LEAVE_CRASHES_UP_FOREVER");

	//now that we've loaded the overrides, we can fixup ppScriptingDirs
	
	//ScriptDirSuffix is used for big scripting changes... for instance, when rewriting all
	//scripts to use multithreading, duplicate them all into continuousbuilder/scripts_MultiThread, then
	//set SCRIPT_DIR_SUFFIX to "MultiThread" (note that an underscore is always inserted)
	pScriptDirSuffix = GetConfigVar("SCRIPT_DIR_SUFFIX");
	if (pScriptDirSuffix)
	{
		int iStartingSize = eaSize(&gConfig.ppScriptDirectories);

		//note that the dirs are checked in reverse order, so newer, "more correct", directory names will
		//be added to the end

		//this logic is trickier than it seems it should be, because when we have
		//"c:/core/tools/cb/scripts" and "c:/core/tools/cb/scripts/productionbuild", we can't just suffix
		//them both, we actually need to suffix the first one, and then realize that it's a prefix of the second
		//one, and suffix that one appropriately

		for (i = 0; i < iStartingSize; i++)
		{
			char *pCurString = gConfig.ppScriptDirectories[i];
			int iPrefixIndex = FindShortestPrefix(&gConfig.ppScriptDirectories, iStartingSize, pCurString);
			if (iPrefixIndex != i)
			{
				char temp[CRYPTIC_MAX_PATH];
				char *pPrefix = gConfig.ppScriptDirectories[iPrefixIndex];
				
				sprintf(temp, "%s_%s%s", pPrefix, pScriptDirSuffix, pCurString + strlen(pPrefix));
				eaPush(&gConfig.ppScriptDirectories, estrDup(temp));
			}
			else
			{
				char temp[CRYPTIC_MAX_PATH];
				sprintf(temp, "%s_%s", gConfig.ppScriptDirectories[i], pScriptDirSuffix);
				eaPush(&gConfig.ppScriptDirectories, estrDup(temp));
			}
		}
	}

	if (!gConfig.bDev)
	{
		_putenv_s("GIMME_USERNAME", getComputerName());
	}
	
}




bool CBTypeIsCONT(void)
{
	static int iFound = -1;

	if (iFound == -1)
	{
		if (stricmp(gpCBType->pShortTypeName, "CONT") == 0 || stricmp(gpCBType->pShortTypeName, "CONT_XBOX") == 0 )
		{
			iFound = 1;
		}
		else
		{
			iFound = 0;
		}
	}

	return iFound;
}

bool CBStartup_StringIsProductName(const char *pStr)
{
	int i;

	for (i = 0; i < eaSize(&gProductAndTypeList.ppProduct); i++)
	{
		if (stricmp(pStr, gProductAndTypeList.ppProduct[i]->pProductName) == 0)
		{
			return true;
		}
	}

	return false;
}
bool CBStartup_StringIsBuildTypeName(const char *pStr)
{
	int i;

	for (i = 0; i < eaSize(&gProductAndTypeList.ppType); i++)
	{
		if (stricmp(pStr, gProductAndTypeList.ppType[i]->pShortTypeName) == 0)
		{
			return true;
		}
	}

	return false;
}


#include "CBStartup_h_ast.c"
#include "CBStartup_c_ast.c"
