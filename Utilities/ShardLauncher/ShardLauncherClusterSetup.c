#include "winInclude.h"
#include "ShardLauncherClusterSetup.h"
#include "ShardLauncherUI.h"
#include "resource.h"
#include "winutil.h"
#include "ShardLauncher.h"
#include "earray.h"
#include "GlobalTypes_h_ast.h"
#include "ShardLauncherMainScreen.h"
#include "structDefines.h"
#include "ShardLauncher_h_Ast.h"
#include "ShardLauncherWarningScreen.h"
#include "StashTable.h"
#include "file.h"
#include "StringUtil.h"
#include "ShardLauncher_pub_h_ast.h"
#include "utf8.h"

static const char **sppShardTypeNames = NULL;

static const char **sppStartingIDStrings = NULL;

//which index in the starting IDs was most recently set, so that if we need to go in and fix up the values to make them sane,
//we don't keep changing the one that was just edited
static int siMostRecentStartingIDModifyIndex = -1;




static HBRUSH sBad = 0;
#define BAD_COLOR RGB(240,0,0)


static bool ShardExists(SimpleWindow *pWindow, int i);


U32 sShardNameIDs[] = 
{
	IDC_SHARDNAME1,
	IDC_SHARDNAME2,
	IDC_SHARDNAME3,
	IDC_SHARDNAME4,
	IDC_SHARDNAME5,
	IDC_SHARDNAME6,
	IDC_SHARDNAME7,
	IDC_SHARDNAME8,
	IDC_SHARDNAME9,
};

U32 sMachineIDs[] = 
{
	IDC_MACHINE1,
	IDC_MACHINE2,
	IDC_MACHINE3,
	IDC_MACHINE4,
	IDC_MACHINE5,
	IDC_MACHINE6,
	IDC_MACHINE7,
	IDC_MACHINE8,
	IDC_MACHINE9,
};

U32 sShardTypeIDs[] = 
{
	IDC_SHARDTYPE1,
	IDC_SHARDTYPE2,
	IDC_SHARDTYPE3,
	IDC_SHARDTYPE4,
	IDC_SHARDTYPE5,
	IDC_SHARDTYPE6,
	IDC_SHARDTYPE7,
	IDC_SHARDTYPE8,
	IDC_SHARDTYPE9,
};

U32 sSetupFileIDs[] = 
{
	IDC_SETUPFILE1,
	IDC_SETUPFILE2,
	IDC_SETUPFILE3,
	IDC_SETUPFILE4,
	IDC_SETUPFILE5,
	IDC_SETUPFILE6,
	IDC_SETUPFILE7,
	IDC_SETUPFILE8,
	IDC_SETUPFILE9,
};

U32 sSetupDescIDs[] = 
{
	IDC_SETUPDESC1,
	IDC_SETUPDESC2,
	IDC_SETUPDESC3,
	IDC_SETUPDESC4,
	IDC_SETUPDESC5,
	IDC_SETUPDESC6,
	IDC_SETUPDESC7,
	IDC_SETUPDESC8,
	IDC_SETUPDESC9,
};

U32 sStartingIDIDs[] = 
{
	IDC_STARTINGID1,
	IDC_STARTINGID2,
	IDC_STARTINGID3,
	IDC_STARTINGID4,
	IDC_STARTINGID5,
	IDC_STARTINGID6,
	IDC_STARTINGID7,
	IDC_STARTINGID8,
	IDC_STARTINGID9,
};


static char *spCurErrorString = NULL;

//sees if there are any duplicated strings in this earray of strings, returns the first one found
static char *FindDupString(char **ppStrings)
{
	int iSize = eaSize(&ppStrings);
	int i, j;
	if (iSize < 2)
	{
		return NULL;
	}

	for (i = 0; i < iSize - 1; i++)
	{
		for (j = i + 1; j < iSize; j++)
		{
			if (stricmp(ppStrings[i], ppStrings[j]) == 0)
			{
				return ppStrings[i];
			}
		}
	}

	return NULL;
}

//given two earrays of strings, finds any string that appears in both of them, UNLESS it appears at the same index in both
static char *FindReusedString(char **ppStrings1, char **ppStrings2)
{
	int iSize1 = eaSize(&ppStrings1);
	int iSize2 = eaSize(&ppStrings2);
	int i, j;

	if (iSize1 < 2 && iSize2 < 2)
	{
		return NULL;
	}

	for (i = 0; i < iSize1; i++)
	{
		for (j = 0; j < iSize2; j++)
		{
			if (i != j)
			{
				if (stricmp(ppStrings1[i], ppStrings2[j]) == 0)
				{
					return ppStrings1[1];
				}
			}
		}
	}

	return NULL;
}

static bool ShardExists(SimpleWindow *pWindow, int i)
{
	static char *spShardName = NULL;
	estrGetWindowText(&spShardName, GetDlgItem(pWindow->hWnd, sShardNameIDs[i]));
	estrTrimLeadingAndTrailingWhitespace(&spShardName);
	return estrLength(&spShardName) > 2;
}

bool isLegalCharForShardOrMachineName(char c)
{
	if (isalnumorunderscore(c) || c == '-' || c == '.')
	{
		return true;
	}

	return false;
}


void CheckForShardNameLegality(char *pShardName, char **ppError)
{
	char *pTemp = pShardName;

	while (*pTemp)
	{
		if (!isLegalCharForShardOrMachineName(*pTemp))
		{
			estrConcatf(ppError, "Shard name %s contains illegaly character \"%c\"", pShardName, *pTemp);
		}
		pTemp++;
	}
}

void CheckForMachineNameLegality(char *pMachineName, char **ppError)
{
	char *pTemp = pMachineName;

	if (sMachineNamesFromSentryServer)
	{
		if (!stashFindPointer(sMachineNamesFromSentryServer, pMachineName, NULL))
		{
			estrConcatf(ppError, "Machine name %s not recognized by Sentry Server\n", pMachineName);
		}
	}

	while (*pTemp)
	{
		if (!isLegalCharForShardOrMachineName(*pTemp))
		{
			estrConcatf(ppError, "Machine name %s contains illegaly character %c", pMachineName, *pTemp);
		}
		pTemp++;
	}
}


void SetStartingIDToLowestUnused(SimpleWindow *pWindow, int index)
{
	bool bUsed[ARRAY_SIZE(sShardNameIDs)] = {0};
	int i;

	for (i = 0; i < ARRAY_SIZE(sShardNameIDs); i++)
	{
		if (ShardExists(pWindow, i) && i != index)
		{
			int iIndex = GetComboBoxSelectedIndex(pWindow->hWnd, sStartingIDIDs[i]);
			if (iIndex >= 0 && iIndex < ARRAY_SIZE(sShardNameIDs))
			{
				bUsed[iIndex] = true;
			}
		}
	}

	for (i = 0; i <ARRAY_SIZE(sShardNameIDs); i++)
	{
		if (!bUsed[i])
		{
			SendMessage(GetDlgItem(pWindow->hWnd, sStartingIDIDs[index]), CB_SETCURSEL, i, 0);
			return;
		}
	}
}

void MakeStartingIDsUnique(SimpleWindow *pWindow)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(sShardNameIDs) - 1; i++)
	{
		if (ShardExists(pWindow, i))
		{
			for (j = i + 1; j < ARRAY_SIZE(sShardNameIDs); j++)
			{
				if (ShardExists(pWindow, j))
				{
					int iIndex1 = GetComboBoxSelectedIndex(pWindow->hWnd, sStartingIDIDs[i]);
					int iIndex2 = GetComboBoxSelectedIndex(pWindow->hWnd, sStartingIDIDs[j]);

					if (iIndex1 == iIndex2)
					{
						if (siMostRecentStartingIDModifyIndex == j)
						{
							SetStartingIDToLowestUnused(pWindow, i);
						}
						else
						{
							SetStartingIDToLowestUnused(pWindow, j);
						}
					}
				}
			}
		}
	}
}



static void CheckForErrors(SimpleWindow *pWindow)
{
	static char *pClusterName = NULL;
	bool bFoundAShard = false;
	int i;
	char **ppShardNames = NULL;
	char **ppMachineNames = NULL;
	char **ppShardSetupFiles = NULL;

	static char *spLoggingMachineName = NULL;
	static char *spLoggingFilterFileName = NULL;
	static char *spLoggingDir = NULL;

	estrClear(&spCurErrorString);
	estrGetWindowText(&pClusterName, GetDlgItem(pWindow->hWnd, IDC_CLUSTERNAME));
	if (!estrLength(&pClusterName))
	{
		estrConcatf(&spCurErrorString, "No cluster name specified\n");
	}

	estrClear(&spLoggingMachineName);
	estrClear(&spLoggingFilterFileName);
	estrClear(&spLoggingDir);
	estrGetWindowText(&spLoggingMachineName, GetDlgItem(pWindow->hWnd, IDC_LOGMACHINENAME));
	estrGetWindowText(&spLoggingFilterFileName, GetDlgItem(pWindow->hWnd, IDC_LOGSERVERFILTERFILE));
	estrGetWindowText(&spLoggingDir, GetDlgItem(pWindow->hWnd, IDC_LOGGINGDIR));

	if (estrLength(&spLoggingMachineName))
	{
		if (sMachineNamesFromSentryServer && !stashFindPointer(sMachineNamesFromSentryServer, spLoggingMachineName, NULL))
		{
			estrConcatf(&spCurErrorString, "Logging machine name %s not recognized by sentryServer\n", spLoggingMachineName);
		}

		if (!estrLength(&spLoggingDir))
		{
			estrConcatf(&spCurErrorString, "No logging dir specified\n");
		}
		else
		{
			if (estrLength(&spLoggingDir) < 3 || spLoggingDir[1] != ':')
			{
				estrConcatf(&spCurErrorString, "Logging dir should be an absolute path (ie, d:\\logs), doesn't seem to be");
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(sShardNameIDs); i++)
	{
		if (ShardExists(pWindow, i))
		{
			static char *pShardName = NULL;
			static char *pMachineName = NULL;


			int iShardSetupFileIndex;

			estrGetWindowText(&pShardName, GetDlgItem(pWindow->hWnd, sShardNameIDs[i]));

			estrTrimLeadingAndTrailingWhitespace(&pShardName);


			bFoundAShard = true;

			eaPush(&ppShardNames, strdup(pShardName));

			estrGetWindowText(&pMachineName, GetDlgItem(pWindow->hWnd, sMachineIDs[i]));

			CheckForShardNameLegality(pShardName, &spCurErrorString);
			CheckForMachineNameLegality(pMachineName, &spCurErrorString);


			if (estrLength(&pMachineName))
			{
				eaPush(&ppMachineNames, strdup(pMachineName));
			}
			else
			{
				estrConcatf(&spCurErrorString, "Shard %s has no machine specified\n", pShardName);
			}

			if (stricmp_safe(pMachineName, spLoggingMachineName) == 0)
			{
				estrConcatf(&spCurErrorString, "Machine %s can't be both logging machine and a shard machine",
					pMachineName);
			}

			iShardSetupFileIndex = GetComboBoxSelectedIndex(pWindow->hWnd, sSetupFileIDs[i]);

			if (iShardSetupFileIndex == 0)
			{
				//useful for debug, allowing for now
				//estrConcatf(&spCurErrorString, "Shard %s has no shard setup file specified\n", pShardName);
			}
			else
			{
				eaPush(&ppShardSetupFiles, strdup(ppShardSetupFileNames[iShardSetupFileIndex]));
			}
		}
	}

	if (!bFoundAShard)
	{
		estrConcatf(&spCurErrorString, "No shards specified\n");
	}
	else
	{
		char *pBadName = FindDupString(ppShardNames);
		
		if (pBadName)
		{
			estrConcatf(&spCurErrorString, "Two shards both named %s, that is illegal\n", pBadName);
		}

		pBadName = FindDupString(ppMachineNames);
		if (pBadName)
		{
			estrConcatf(&spCurErrorString, "Two shards both using machine %s, that is illegal\n", pBadName);
		}

		pBadName = FindReusedString(ppShardNames, ppMachineNames);
		if (pBadName)
		{
			estrConcatf(&spCurErrorString, "Found a machine and a shard both named %s, but that machine is for a different shard\n",
				pBadName);
		}

		pBadName = FindDupString(ppShardSetupFiles);
		if (pBadName)
		{
			estrConcatf(&spCurErrorString, "Two shards both using shardSetupFile %s, that is illegal\n", pBadName);
		}
	}


	eaDestroyEx(&ppShardNames, NULL);
	eaDestroyEx(&ppMachineNames, NULL);
	eaDestroyEx(&ppShardSetupFiles, NULL);

	estrFixupNewLinesForWindows(&spCurErrorString);
	SetTextFast(GetDlgItem(pWindow->hWnd, IDC_ERRORS), spCurErrorString);

	if (estrLength(&spCurErrorString))
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDOK), SW_HIDE);
	}
	else
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDOK), SW_SHOW);
	}
}





bool clusterSetupDlgProc_SWMTick(SimpleWindow *pWindow)
{
	int i;
	static char *spShardName = NULL;

	for (i = 0; i < ARRAY_SIZE(sShardNameIDs); i++)
	{
		GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, sShardNameIDs[i]), &spShardName);
		if (estrLength(&spShardName))
		{
			int iShardSetupFileNum = GetComboBoxSelectedIndex(pWindow->hWnd, sSetupFileIDs[i]);
			SetTextFast(GetDlgItem(pWindow->hWnd, sSetupDescIDs[i]), ppShardSetupFileDescriptions[iShardSetupFileNum]);
		}

		CheckForErrors(pWindow);
	}

	
	return false;
}

char *strdupWindowText(HWND hDlg, int iID)
{
	HWND hDlg_inner = GetDlgItem(hDlg, iID);
	int iLen;
	char *pRetVal;
	char *pTemp = NULL;

	if (!hDlg_inner)
	{
		return NULL;
	}

	iLen = GetWindowTextLength(hDlg_inner);
	if (!iLen)
	{
		return NULL;
	}

	estrStackCreate(&pTemp);
	GetWindowText_UTF8(hDlg_inner, &pTemp);

	if (!estrLength(&pTemp))
	{
		pRetVal = strdup("");
	}
	else
	{
		pRetVal = malloc(estrLength(&pTemp) + 1);
		memcpy(pRetVal, pTemp, estrLength(&pTemp) + 1);
	}

	estrDestroy(&pTemp);

	return pRetVal;
}

void CopyDataIntoRun(SimpleWindow *pWindow)
{
	static char *pTemp = NULL;
	int i;

	SAFE_FREE(gpRun->pClusterName);
	gpRun->pClusterName = strdupWindowText(pWindow->hWnd, IDC_CLUSTERNAME);

	SAFE_FREE(gpRun->pLogServerAndParserMachineName);
	SAFE_FREE(gpRun->pLogServerFilterFileName);
	SAFE_FREE(gpRun->pLogServerExtraCmdLine);
	SAFE_FREE(gpRun->pLogParserExtraCmdLine);	
	SAFE_FREE(gpRun->pLoggingDir);
	gpRun->pLogServerAndParserMachineName = strdupWindowText(pWindow->hWnd, IDC_LOGMACHINENAME);
	gpRun->pLogServerFilterFileName = strdupWindowText(pWindow->hWnd, IDC_LOGSERVERFILTERFILE);
	gpRun->pLogServerExtraCmdLine = strdupWindowText(pWindow->hWnd, IDC_LOGSERVERCMDLINE);
	gpRun->pLogParserExtraCmdLine = strdupWindowText(pWindow->hWnd, IDC_LOGPARSERCMDLINE);
	gpRun->pLoggingDir = strdupWindowText(pWindow->hWnd, IDC_LOGGINGDIR);


	eaDestroyStruct(&gpRun->ppClusterShards, parse_ShardLauncherClusterShard);

	for (i = 0; i < ARRAY_SIZE(sShardNameIDs); i++)
	{
		if (ShardExists(pWindow, i))
		{
			ShardLauncherClusterShard *pShard = StructCreate(parse_ShardLauncherClusterShard);
			eaPush(&gpRun->ppClusterShards, pShard);
			
			pShard->pShardName = strdupWindowText(pWindow->hWnd, sShardNameIDs[i]);
			pShard->pMachineName = strdupWindowText(pWindow->hWnd, sMachineIDs[i]);
			
			pShard->eShardType = GetComboBoxSelectedIndex(pWindow->hWnd, sShardTypeIDs[i]) + SHARDTYPE_UNDEFINED + 1;
			pShard->pShardSetupFileName = strdup(ppShardSetupFileNames[GetComboBoxSelectedIndex(pWindow->hWnd, sSetupFileIDs[i])]);
			pShard->iStartingID = (GetComboBoxSelectedIndex(pWindow->hWnd, sStartingIDIDs[i]) + 1) * STARTING_ID_INTERVAL;
		}
	}
}

void ListShardOrTypeSpecificSettings(void)
{
	char *pString = NULL;
	int i;
	char temp[128];
	char *pShortName = NULL;

	estrConcatf(&pString, "Hardwired overrides:\n");
	FOR_EACH_IN_EARRAY(gppBuiltInOptionsForShardType, BuiltInOptionForShardType, pOption)
	{
		estrConcatf(&pString, "%s shards: %s is set to %s\n", StaticDefineInt_FastIntToString(ClusterShardTypeEnum, pOption->eShardType),
			pOption->pOptionName, pOption->pOptionValue);
	}
	FOR_EACH_END;

	for (i = SHARDTYPE_UNDEFINED + 1; i < SHARDTYPE_LAST; i++)
	{
		bool bFoundOne = false;
		sprintf(temp, "{%s}", StaticDefineInt_FastIntToString(ClusterShardTypeEnum, i));
		estrConcatf(&pString, "\nConfig options for shards of type %s:\n", StaticDefineInt_FastIntToString(ClusterShardTypeEnum, i));

		FOR_EACH_IN_EARRAY(gpRun->ppChoices, ShardLauncherConfigOptionChoice, pChoice)
		{
			
			if (strStartsWith(pChoice->pConfigOptionName, temp))
			{
				bFoundOne = true;
				estrCopy2(&pShortName, pChoice->pConfigOptionName);
				estrRemoveUpToFirstOccurrence(&pShortName, '}');
				estrTrimLeadingAndTrailingWhitespace(&pShortName);

				estrConcatf(&pString, "%s is set to %s\n", pShortName, pChoice->pValue);
			}
		}
		FOR_EACH_END;

		if (!bFoundOne)
		{
			estrConcatf(&pString, "(none)\n");
		}
	}

	FOR_EACH_IN_EARRAY_FORWARDS(gpRun->ppClusterShards, ShardLauncherClusterShard, pShard)
	{
		bool bFoundOne = false;
		sprintf(temp, "{%s}", pShard->pShardName);
		estrConcatf(&pString, "\nConfig options for shard %s:\n", pShard->pShardName);

		FOR_EACH_IN_EARRAY(gpRun->ppChoices, ShardLauncherConfigOptionChoice, pChoice)
		{
			
			if (strStartsWith(pChoice->pConfigOptionName, temp))
			{
				bFoundOne = true;
				estrCopy2(&pShortName, pChoice->pConfigOptionName);
				estrRemoveUpToFirstOccurrence(&pShortName, '}');
				estrTrimLeadingAndTrailingWhitespace(&pShortName);

				estrConcatf(&pString, "%s is set to %s\n", pShortName, pChoice->pValue);
			}
		}
		FOR_EACH_END;

		if (!bFoundOne)
		{
			estrConcatf(&pString, "(none)\n");
		}
	}
	FOR_EACH_END;

	DisplayWarning("Cluster-specific options", pString);

	estrDestroy(&pShortName);
	estrDestroy(&pString);
}

BOOL clusterSetupDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i;


	switch (iMsg)
	{
	case WM_INITDIALOG:

		if (!sBad)
		{
			sBad = CreateSolidBrush(BAD_COLOR);
		}

		SetTextFast(GetDlgItem(hDlg, IDC_LOGMACHINENAME), gpRun->pLogServerAndParserMachineName ? gpRun->pLogServerAndParserMachineName : "");
		SetTextFast(GetDlgItem(hDlg, IDC_LOGSERVERFILTERFILE), gpRun->pLogServerFilterFileName ? gpRun->pLogServerFilterFileName : "");
		SetTextFast(GetDlgItem(hDlg, IDC_LOGSERVERCMDLINE), gpRun->pLogServerExtraCmdLine ? gpRun->pLogServerExtraCmdLine : "");
		SetTextFast(GetDlgItem(hDlg, IDC_LOGPARSERCMDLINE), gpRun->pLogParserExtraCmdLine ? gpRun->pLogParserExtraCmdLine : "");
		SetTextFast(GetDlgItem(hDlg, IDC_LOGGINGDIR), gpRun->pLoggingDir ? gpRun->pLoggingDir : "");
		SetTextFast(GetDlgItem(hDlg, IDC_CLUSTERNAME), gpRun->pClusterName ? gpRun->pClusterName : "");

		estrClear(&spCurErrorString);

		if (!sppShardTypeNames)
		{
			for (i = SHARDTYPE_UNDEFINED + 1; i < SHARDTYPE_LAST; i++)
			{
				eaPush(&sppShardTypeNames, StaticDefineInt_FastIntToString(ClusterShardTypeEnum, i));
			}
		}

		if (!sppStartingIDStrings)
		{
			for (i = 0; i < ARRAY_SIZE(sShardNameIDs); i++)
			{
				char temp[64];
				sprintf(temp, "%dM", (i + 1) * 100);
				eaPush(&sppStartingIDStrings, strdup(temp));
			}
		}

		if (eaSize(&gpRun->ppClusterShards) > ARRAY_SIZE(sShardNameIDs))
		{
			LOG_FAIL("Too many shards... max allowed is %d", ARRAY_SIZE(sShardNameIDs));
			exit(-1);
		}

		for (i = 0; i < ARRAY_SIZE(sShardNameIDs); i++)
		{
			SetComboBoxFromEarrayWithDefault(hDlg, sStartingIDIDs[i], (char***)&sppStartingIDStrings, NULL, "100M");
			SetComboBoxFromEarrayWithDefault(hDlg, sShardTypeIDs[i], (char***)&sppShardTypeNames, NULL, "NORMAL");
			InitShardSetupFileComboBox(hDlg, sSetupFileIDs[i], "NONE", i == 0, true);
		}

		for (i = 0; i < eaSize(&gpRun->ppClusterShards); i++)
		{
			SetTextFast(GetDlgItem(hDlg, sShardNameIDs[i]), gpRun->ppClusterShards[i]->pShardName);
			SetTextFast(GetDlgItem(hDlg, sMachineIDs[i]), gpRun->ppClusterShards[i]->pMachineName);
			SendMessage_SelectString_UTF8(GetDlgItem(hDlg, sShardTypeIDs[i]), StaticDefineInt_FastIntToString(ClusterShardTypeEnum, gpRun->ppClusterShards[i]->eShardType));
			SendMessage_SelectString_UTF8(GetDlgItem(hDlg, sSetupFileIDs[i]), gpRun->ppClusterShards[i]->pShardSetupFileName);
			SendMessage(GetDlgItem(hDlg, sStartingIDIDs[i]), CB_SETCURSEL, (gpRun->ppClusterShards[i]->iStartingID / STARTING_ID_INTERVAL) - 1, 0);
		}

		for (i = eaSize(&gpRun->ppClusterShards); i < ARRAY_SIZE(sShardNameIDs); i++)
		{
			ShowWindow(GetDlgItem(hDlg, sMachineIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, sShardTypeIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, sSetupFileIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, sSetupDescIDs[i]), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, sStartingIDIDs[i]), SW_HIDE);
		}

		MakeStartingIDsUnique(pWindow);


		break;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_ERRORS))
		{
			if (estrLength(&spCurErrorString) == 0)
			{
				return false;
			}
			else
			{
				HDC hdc = (HDC)wParam;
				SetBkColor(hdc, BAD_COLOR);
				return (BOOL)((intptr_t)sBad);
			}
		}
		return false;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		for (i=0; i < ARRAY_SIZE(sShardNameIDs); i++)
		{
			if (LOWORD(wParam) == sStartingIDIDs[i])
			{
				siMostRecentStartingIDModifyIndex = i;
				MakeStartingIDsUnique(pWindow);
				return false;
			}

			if (LOWORD(wParam) == sShardNameIDs[i])
			{
				if (ShardExists(pWindow, i))
				{
					ShowWindow(GetDlgItem(hDlg, sMachineIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, sShardTypeIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, sSetupFileIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, sSetupDescIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, sStartingIDIDs[i]), SW_SHOW);
					SetStartingIDToLowestUnused(pWindow, i);
				}
				else
				{
					ShowWindow(GetDlgItem(hDlg, sMachineIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, sShardTypeIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, sSetupFileIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, sSetupDescIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, sStartingIDIDs[i]), SW_HIDE);
				}
				return false;
			}
		}

		switch (LOWORD (wParam))
		{
		case IDC_LISTSHARDSETTINGS:
			ListShardOrTypeSpecificSettings();
			break;

		case IDCANCEL:
			pWindow->bCloseRequested = true;
			break;

		case IDC_CLUSTERNAME:
			{
				char *pShardName = NULL;
				GetWindowText_UTF8(GetDlgItem(hDlg, IDC_CLUSTERNAME), &pShardName);
				if (estrLength(&pShardName))
				{
					SetTextFastf(GetDlgItem(hDlg, IDC_DATADIR), "Template Data Dir: c:\\%s\\Data",
						pShardName);
					SetTextFastf(GetDlgItem(hDlg, IDC_FRANKENBUILDDIR), "Frankenbuild Dir: c:\\%s\\FrankenBuilds",
						pShardName);
					SetTextFastf(GetDlgItem(hDlg, IDC_LOGGINGPATCHDIR), "Logging patch dir: c:\\%s_Logging", pShardName);
				}
				else
				{
					SetTextFast(GetDlgItem(hDlg, IDC_DATADIR), "Template Data Dir:");
					SetTextFast(GetDlgItem(hDlg, IDC_DATADIR), "Frankenbuild Dir:");
				}

				estrDestroy(&pShardName);
			}
			break;

		case IDOK:
			{
				CheckForErrors(pWindow);
				if (estrLength(&spCurErrorString))
				{
					return false;
				}

				CopyDataIntoRun(pWindow);
				pWindow->bCloseRequested = true;
				return false;


			}

		}
	}
	
	return false;
}



