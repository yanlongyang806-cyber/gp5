#include "wininclude.h"
#include "ShardLauncher.h"
#include "shardLauncherMainScreen.h"
#include "ShardLauncherMainScreen_c_ast.h"
#include "resource.h"
#include "../../core/controller/pub/ControllerPub.h"
#include "shardLauncherUI.h"
#include "ShardLauncher_h_ast.h"
#include "WinUtil.h"
#include "ShardLauncherPatchVersionPicker.h"
#include "ShardLauncherOptions.h"
#include "ShardLauncherChooseName.h"
#include "fileUtil2.h"
#include "../../core/controller/pub/ControllerPUb.h"
#include "ControllerPub_h_ast.h"

#include "ShardLauncherControllerCommands.h"
#include "ShardLauncherOverrideExes.h"
#include "stringUtil.h"
#include "MapDescription.h"
#include "MapDescription_h_ast.h"
#include "AutoGen/ControllerStartupSupport_h_ast.h"
#include "../../libs/PatchClientLib/pcl_client_wt.h"
#include "utilitiesLib.h"
#include "ShardLauncherWarningScreen.h"
#include "ShardLauncherRunTheShard.h"
#include "ShardLauncherClusterSetup.h"
#include "ShardLauncher_pub_h_ast.h"
#include "UTF8.h"

static bool gbOptionLibraryCurrentlyPatching = false;

char *pCustomConfigOptionsFileLocation = NULL;
AUTO_CMD_ESTRING(pCustomConfigOptionsFileLocation, CustomConfigOptionsFileLocation) ACMD_COMMANDLINE;

char **ppShardSetupFileNames = NULL;
char **ppShardSetupFileDescriptions = NULL;

static HBRUSH sBad = 0;
#define BAD_COLOR RGB(240,0,0)

static HBRUSH sPurple = 0;
#define PURPLE_COLOR RGB(240,0,240)

static HBRUSH sYellow = 0;
#define YELLOW_COLOR RGB(240,240,256)


void GetDescStringForShardSetupFile(char *pFileName, char **ppOutDescString);

U32 gOptionButtonIDs[] = 
{
	IDC_STATICBUTTON1,
	IDC_STATICBUTTON2,
	IDC_STATICBUTTON3,
	IDC_STATICBUTTON4,
	IDC_STATICBUTTON5,
	IDC_STATICBUTTON6,
	IDC_STATICBUTTON7,
	IDC_STATICBUTTON8,
	IDC_STATICBUTTON9,
	IDC_STATICBUTTON10,
	IDC_STATICBUTTON11,
	IDC_STATICBUTTON12,
	IDC_STATICBUTTON13,
	IDC_STATICBUTTON14,
	IDC_STATICBUTTON15,
	IDC_STATICBUTTON16,
	IDC_STATICBUTTON17,
	IDC_STATICBUTTON18,
	IDC_STATICBUTTON19,
	IDC_STATICBUTTON20,
	IDC_STATICBUTTON21,
	IDC_STATICBUTTON22,
	IDC_STATICBUTTON23,
	IDC_STATICBUTTON24,
};

U32 gServerScreenButtonIDs[] = 
{
	IDC_SERVERBUTTON1,
	IDC_SERVERBUTTON2,
	IDC_SERVERBUTTON3,
	IDC_SERVERBUTTON4,
	IDC_SERVERBUTTON5,
	IDC_SERVERBUTTON6,
	IDC_SERVERBUTTON7,
	IDC_SERVERBUTTON8,
	IDC_SERVERBUTTON9,
	IDC_SERVERBUTTON10,
	IDC_SERVERBUTTON11,
	IDC_SERVERBUTTON12,
	IDC_SERVERBUTTON13,
	IDC_SERVERBUTTON14,
	IDC_SERVERBUTTON15,
	IDC_SERVERBUTTON16,
	IDC_SERVERBUTTON17,
	IDC_SERVERBUTTON18,
	IDC_SERVERBUTTON19,
	IDC_SERVERBUTTON20,
	IDC_SERVERBUTTON21,
	IDC_SERVERBUTTON22,
	IDC_SERVERBUTTON23,
	IDC_SERVERBUTTON24,
	IDC_SERVERBUTTON25,
	IDC_SERVERBUTTON26,
	IDC_SERVERBUTTON27,
	IDC_SERVERBUTTON28,
	IDC_SERVERBUTTON29,
	IDC_SERVERBUTTON30,
	IDC_SERVERBUTTON31,
	IDC_SERVERBUTTON32,
	IDC_SERVERBUTTON33,
	IDC_SERVERBUTTON34,
	IDC_SERVERBUTTON35,
	IDC_SERVERBUTTON36,
	IDC_SERVERBUTTON37,
	IDC_SERVERBUTTON38,
	IDC_SERVERBUTTON39,
	IDC_SERVERBUTTON40,
	IDC_SERVERBUTTON41,
	IDC_SERVERBUTTON42,
	IDC_SERVERBUTTON43,
	IDC_SERVERBUTTON44,
	IDC_SERVERBUTTON45,
};

AUTO_STRUCT;
typedef struct ShardLauncherProductList 
{
	ShardLauncherProduct **ppProducts; AST(NAME(Product))
} ShardLauncherProductList;

ShardLauncherProductList *gpProductList = NULL;

static bool sbLibraryHasChanged = false;

void LibraryChanged(void)
{
	SimpleWindow **ppWindows = NULL;
	SimpleWindow *pMainWindow;

	sbLibraryHasChanged = true;

	SimpleWindowManager_FindAllWindowsByType(WINDOWTYPE_OPTIONS, &ppWindows);
	FOR_EACH_IN_EARRAY(ppWindows, SimpleWindow, pWindow)
	{
		pWindow->bCloseRequested = true;
		InvalidateRect(GetDlgItem(pWindow->hWnd, IDC_TITLE), NULL, false);
	}
	FOR_EACH_END;

	pMainWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);
	if (pMainWindow)
	{
		InvalidateRect(pMainWindow->hWnd, NULL, false);
	}

	eaDestroy(&ppWindows);
}

bool IsProductSet(HWND hDlg)
{
	static char *pTempStr = NULL;
	GetWindowText_UTF8(GetDlgItem(hDlg, IDC_PRODUCTPICKER), &pTempStr);

	if (pTempStr && pTempStr[0] == '?')
	{
		return false;
	}

	return true;
}

bool IsPatchVersionSet(HWND hDlg)
{
	static char *pTempStr = NULL;
	GetWindowText_UTF8(GetDlgItem(hDlg, IDC_PATCHVERSION_LABEL), &pTempStr);

	if (stricmp_safe(pTempStr, "ERROR") != 0)
	{
		return true;
	}

	return false;
}

bool AllRequiredFieldsSet(void)
{
	int i;

	if (gpRun->bClustered && !eaSize(&gpRun->ppClusterShards))
	{
		return false;
	}

	for (i=0; i < eaSize(&gpRun->ppTemplateChoices); i++)
	{
		if (stricmp(gpRun->ppTemplateChoices[i]->pValue, "?") == 0)
		{
			ShardLauncherConfigOptionChoice *pChoice = eaIndexedGetUsingString(&gpRun->ppChoices, gpRun->ppTemplateChoices[i]->pConfigOptionName);
			if (!pChoice)
			{
				return false;
			}

			if (stricmp(pChoice->pValue, "?") == 0)
			{
				return false;
			}
		}
	}

	return true;
}

void InitShardSetupFileComboBox(HWND hDlg, U32 iResID, char *pDefault, bool bReload, bool bShortNames)
{
	char **ppFileList = NULL;
	int i;

	if (bReload || !ppFileList)
	{
		fileScanDirFreeNames(ppFileList);

		eaDestroyEx(&ppShardSetupFileNames, NULL);
		eaDestroyEx(&ppShardSetupFileDescriptions, NULL);

		eaPush(&ppShardSetupFileNames, strdup("NONE"));
		eaPush(&ppShardSetupFileDescriptions, strdup("No shard setup file. Your shard will run entirely on this machine."));

		ppFileList = fileScanDirFolders("c:\\ShardSetupFiles", FSF_FILES);
		for (i=0; i < eaSize(&ppFileList); i++)
		{
			char *pDesc = NULL;

			GetDescStringForShardSetupFile(ppFileList[i], &pDesc);

			if (bShortNames)
			{
				static char *pShortName = NULL;
				estrGetDirAndFileNameAndExtension(ppFileList[i], NULL, &pShortName, NULL);
				eaPush(&ppShardSetupFileNames, strdup(pShortName));
				estrClear(&pShortName);
			}
			else
			{	
				eaPush(&ppShardSetupFileNames, strdup(ppFileList[i]));
			}

			backSlashes(ppShardSetupFileNames[eaSize(&ppShardSetupFileNames) - 1]);
			eaPush(&ppShardSetupFileDescriptions, strdup(pDesc));

			estrDestroy(&pDesc);
		}
		fileScanDirFreeNames(ppFileList);
	}

	SetComboBoxFromEarrayWithDefault(hDlg, iResID, &ppShardSetupFileNames, NULL,
		pDefault);
}

bool AllRequiredFieldsSetInOptionList(ShardLauncherConfigOptionList *pList)
{
	int i;

	for (i=0; i < eaSize(&pList->ppOptions); i++)
	{
		ShardLauncherConfigOptionChoice *pTemplateChoice = eaIndexedGetUsingString(&gpRun->ppTemplateChoices, pList->ppOptions[i]->pName);
		if (pTemplateChoice && stricmp(pTemplateChoice->pValue, "?") == 0)
		{
			ShardLauncherConfigOptionChoice *pChoice = eaIndexedGetUsingString(&gpRun->ppChoices, pTemplateChoice->pConfigOptionName);
			if (!pChoice)
			{
				return false;
			}

			if (stricmp(pChoice->pValue, "?") == 0)
			{
				return false;
			}
		}
	}

	return true;
}


void GetDescStringForShardSetupFile(char *pFileName, char **ppOutDescString)
{
	int i;
	MachineInfoForShardSetupList *pMachineInfo = StructCreate(parse_MachineInfoForShardSetupList);
	ParserReadTextFile(pFileName, parse_MachineInfoForShardSetupList, pMachineInfo, 0);
	
	if (pMachineInfo->pComment && pMachineInfo->pComment[0])
	{
		estrCopy2(ppOutDescString, pMachineInfo->pComment);
	}
	else
	{
		estrPrintf(ppOutDescString, "No comment provided.");

		if (pMachineInfo->pBaseGameServerGroupName)
		{
			estrConcatf(ppOutDescString, " Base Group: %s.", pMachineInfo->pBaseGameServerGroupName);
		}

		if (eaSize(&pMachineInfo->ppMachineGroups))
		{
			estrConcatf(ppOutDescString, " Extra groups: ");

			for (i = 0; i < eaSize(&pMachineInfo->ppMachineGroups); i++)
			{
				estrConcatf(ppOutDescString, "%s%s", i == 0 ? "" : ", ", 
					pMachineInfo->ppMachineGroups[i]->pPredefinedGroupName ? pMachineInfo->ppMachineGroups[i]->pPredefinedGroupName : "(Unnamed)");
			}
		}

		estrConcatf(ppOutDescString, " Machines: ");

		for (i=0; i < eaSize(&pMachineInfo->ppMachines); i++)
		{
			estrConcatf(ppOutDescString, "%s%s", i == 0 ? "" : ", ", pMachineInfo->ppMachines[i]->pMachineName);
		}
	}

	StructDestroy(parse_MachineInfoForShardSetupList, pMachineInfo);
}
	
void SetupClusterOptions(SimpleWindow *pWindow)
{
	HWND hDlg = pWindow->hWnd;
	if (gpRun->bClustered)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CLUSTER_SETUP), SW_SHOW);
		ShowWindow(GetDlgItem(hDlg, IDC_SHARDSETUP_STATIC), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_SHARDSETUPFILEPICKER), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CLUSTER_SETUP), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_SHARDSETUP_STATIC), SW_SHOW);
		ShowWindow(GetDlgItem(hDlg, IDC_SHARDSETUPFILEPICKER), SW_SHOW);
		ShowWindow(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), SW_HIDE);


	}
}


void SetUpOptionButtonsFromLibrary(SimpleWindow *pWindow)
{
	int i;
	HWND hDlg = pWindow->hWnd;
	
	if (gpRun->pOptionLibrary)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_NO_LIBRARY_TEXT), SW_HIDE);
		assert(eaSize(&gpRun->pOptionLibrary->ppLists) <= ARRAY_SIZE(gOptionButtonIDs));
		for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
		{
			SetTextFast(GetDlgItem(hDlg, gOptionButtonIDs[i]), gpRun->pOptionLibrary->ppLists[i]->pListName);
			ShowWindow(GetDlgItem(hDlg, gOptionButtonIDs[i]), SW_SHOW);
		}

		for (i = eaSize(&gpRun->pOptionLibrary->ppLists); i < ARRAY_SIZE(gOptionButtonIDs); i++)
		{
			ShowWindow(GetDlgItem(hDlg, gOptionButtonIDs[i]), SW_HIDE);
		}

		assert(ea32Size(&gpRun->pOptionLibrary->pServerSpecificScreenTypes) <= ARRAY_SIZE(gServerScreenButtonIDs));
		for (i=0; i < ea32Size(&gpRun->pOptionLibrary->pServerSpecificScreenTypes); i++)
		{
			SetTextFast(GetDlgItem(hDlg, gServerScreenButtonIDs[i]), GlobalTypeToName(gpRun->pOptionLibrary->pServerSpecificScreenTypes[i]));
			ShowWindow(GetDlgItem(hDlg, gServerScreenButtonIDs[i]), SW_SHOW);
		}

		for (i = ea32Size(&gpRun->pOptionLibrary->pServerSpecificScreenTypes); i < ARRAY_SIZE(gServerScreenButtonIDs); i++)
		{
			ShowWindow(GetDlgItem(hDlg, gServerScreenButtonIDs[i]), SW_HIDE);
		}
	}
	else
	{
		ShowWindow(GetDlgItem(hDlg, IDC_NO_LIBRARY_TEXT), SW_SHOW);
		for (i = 0; i < ARRAY_SIZE(gOptionButtonIDs); i++)
		{
			ShowWindow(GetDlgItem(hDlg, gOptionButtonIDs[i]), SW_HIDE);
		}

		for (i = 0; i < ARRAY_SIZE(gServerScreenButtonIDs); i++)
		{
			ShowWindow(GetDlgItem(hDlg, gServerScreenButtonIDs[i]), SW_HIDE);
		}
	}
}

bool MainScreen_Init(HWND hDlg, SimpleWindow *pWindow)
{


	sbLibraryHasChanged = false;

	SetTextFast(GetDlgItem(pWindow->hWnd, IDC_NO_LIBRARY_TEXT), "No config options currently present... will begin loading them when a product/version are chosen");

	if (!gpProductList)
	{
		gpProductList = StructCreate(parse_ShardLauncherProductList);
		ParserReadTextFile(PRODUCTS_FILE_NAME, parse_ShardLauncherProductList, gpProductList, 0);

		if (!eaSize(&gpProductList->ppProducts))
		{
			LOG_FAIL("Couldn't load products from %s", PRODUCTS_FILE_NAME);
			return false;
		}
	}

	SetComboBoxFromEarrayWithDefault(hDlg, IDC_PRODUCTPICKER, &gpProductList->ppProducts, parse_ShardLauncherProduct,
		gpRun->pProductName);

	SetTextFast(GetDlgItem(hDlg, IDC_PATCHSERVER), gpRun->pPatchServer);
	SetTextFast(GetDlgItem(hDlg, IDC_LOCALDIR), gpRun->pDirectory);
	SetTextFast(GetDlgItem(hDlg, IDC_PATCHVERSION_LABEL), gpRun->pPatchVersion);
	SetTextFast(GetDlgItem(hDlg, IDC_PATCHVERSION_COMMENT), gpRun->pPatchVersionComment);

	SetUpOptionButtonsFromLibrary(pWindow);

	SetupClusterOptions(pWindow);


	InitShardSetupFileComboBox(hDlg, IDC_SHARDSETUPFILEPICKER, gpRun->pShardSetupFile ? gpRun->pShardSetupFile : "NONE", true, false);



	CheckDlgButton(hDlg, IDC_CLUSTERED, gpRun->bClustered ? BST_CHECKED : BST_UNCHECKED);

	if (!sBad)
	{
		sBad = CreateSolidBrush(BAD_COLOR);
		sPurple = CreateSolidBrush(PURPLE_COLOR);
		sYellow = CreateSolidBrush(YELLOW_COLOR);
	}


	if (pCustomConfigOptionsFileLocation)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_LOAD_CONFIG_CUSTOM), SW_SHOW);
		SetTextFast(GetDlgItem(hDlg, IDC_LOAD_CONFIG_CUSTOM), STACK_SPRINTF("Load from %s", pCustomConfigOptionsFileLocation));
	}
	else
	{
		ShowWindow(GetDlgItem(hDlg, IDC_LOAD_CONFIG_CUSTOM), SW_HIDE);
	}

	if (gpRun->bClustered)
	{
		LRESULT lResult;
		int i;

		SendMessage(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), CB_RESETCONTENT, 0, 0);

		lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), "");

		SendMessage(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)0);



		for (i=0; i < eaSize(&gpRun->ppClusterShards); i++)
		{
		
			lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), gpRun->ppClusterShards[i]->pShardName);
			SendMessage(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i + 1);
		}

		SendMessage_SelectString_UTF8(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), "");
	}

	return true;
}


static void ShardLauncherGetFileCB(void *pFileData, int iFileSize, PCL_ErrorCode error, char *pErrorDetails, void *pUserData)
{
	char inPatchVersion[1024];
	static char *pCurPatchVersion = NULL;
	SimpleWindow *pWindow;
	bool bLoadedOldStyle = false;

	gbOptionLibraryCurrentlyPatching = false;

	if (!pUserData)
	{
		bLoadedOldStyle = true;
		sprintf(inPatchVersion, "LOADED_FROM_OLD_STYLE_FILE");
	}
	else
	{
		strcpy(inPatchVersion, (char*)pUserData);
		free(pUserData);
	}

	pWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);

	if (!pWindow)
	{
		return;
	}

	if (!bLoadedOldStyle)
	{
		if (!IsPatchVersionSet(pWindow->hWnd))
		{
			return;
		}

		GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHVERSION_LABEL), &pCurPatchVersion);

		if (stricmp_safe(pCurPatchVersion, inPatchVersion) != 0)
		{
			return;
		}
	}

	if (pFileData)
	{
		char *pNullTerminatedBuffer = malloc(iFileSize + 1);
		char *pErrorString = NULL;
		ShardLauncherConfigOptionLibrary *pLibrary;
		pNullTerminatedBuffer[iFileSize] = 0;
		memcpy(pNullTerminatedBuffer, pFileData, iFileSize);

		pLibrary = LoadAndFixupOptionLibrary(pNullTerminatedBuffer, &pErrorString);
		free(pNullTerminatedBuffer);

		if (pLibrary)
		{
			if (!gpRun->pOptionLibrary)
			{
				gpRun->pOptionLibrary = pLibrary;
				LibraryChanged();				
			}
			else
			{
				if (StructCompare(parse_ShardLauncherConfigOptionLibrary, pLibrary, gpRun->pOptionLibrary, 0, 0, 0) == 0)
				{
					//our new library is identical to the previous one
					StructDestroy(parse_ShardLauncherConfigOptionLibrary, pLibrary);
				}
				else
				{

					StructDestroy(parse_ShardLauncherConfigOptionLibrary, gpRun->pOptionLibrary);
					gpRun->pOptionLibrary = pLibrary;
					LibraryChanged();
				}
			}

			estrCopy2(&gpRun->pLibraryVersion, inPatchVersion);
		}
		else
		{
			if (gpRun->pOptionLibrary)
			{
				DisplayWarningf("WARNING!", "Found config file for patch %s, but had errors while trying to read it: %s",
					inPatchVersion, pErrorString);
			}
			else
			{
				SetTextFastf(GetDlgItem(pWindow->hWnd, IDC_NO_LIBRARY_TEXT), "Found config file for patch %s, but had errors while trying to read it: %s",
					inPatchVersion, pErrorString);
			}
			
			estrDestroy(&pErrorString);
		}
	}
	else
	{
		if (gpRun->pOptionLibrary)
		{
			DisplayWarningf("WARNING!", "Could not find a config file for patch %s. Error was %s(%s)",
				inPatchVersion, StaticDefineInt_FastIntToString(PCL_ErrorCodeEnum, error), pErrorDetails);
		}
		else
		{
			SetTextFastf(GetDlgItem(pWindow->hWnd, IDC_NO_LIBRARY_TEXT), "Could not find a config file for patch %s. Error was %s(%s)",
				inPatchVersion, StaticDefineInt_FastIntToString(PCL_ErrorCodeEnum, error), pErrorDetails);
		}
	}
}

void LoadOldStyleConfig(void)
{
	int iBufSize;
	char *pBuf = fileAlloc(OPTION_LIBRARY_FILE_NAME, &iBufSize);
	if (!pBuf)
	{
		DisplayWarning("WARNING!", "Unable to load old-style config options");
		return;
	}

	ShardLauncherGetFileCB(pBuf, iBufSize, 0, NULL, NULL);
	free(pBuf);
}

void LoadCustomConfig(void)
{
	int iBufSize;
	char *pBuf = fileAlloc(pCustomConfigOptionsFileLocation, &iBufSize);
	if (!pBuf)
	{
		DisplayWarning("WARNING!", "Unable to load old-style config options");
		return;
	}

	ShardLauncherGetFileCB(pBuf, iBufSize, 0, NULL, NULL);
	free(pBuf);
}


//void ThreadedPCL_GetFileIntoRAM(char *pServer, char *pProduct, char *pView, char *pFileName, GetFileIntoRAMCB pCB, void *pUserData);



void UpdateConfigOptionsFileFromPatch(SimpleWindow *pWindow)
{
	static char *pLastPatchVersionQueried = NULL;
	static char *pLastProductQueried = NULL;
	static char *pLastPatchServerQueried = NULL;

	static char *pCurPatchVersion = NULL;
	static char *pCurProduct = NULL;
	static char *pCurPatchServer = NULL;

	if (gbOptionLibraryCurrentlyPatching)
	{
		return;
	}

	if (!IsPatchVersionSet(pWindow->hWnd))
	{
		return;
	}

	if (!IsProductSet(pWindow->hWnd))
	{
		return;
	}

	GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHVERSION_LABEL), &pCurPatchVersion);
	GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PRODUCTPICKER), &pCurProduct);
	GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHSERVER), &pCurPatchServer);

	if (estrLength(&pCurPatchVersion) && estrLength(&pCurProduct) 
		&& (stricmp(pCurPatchVersion, pLastPatchVersionQueried) != 0  
			|| stricmp(pCurProduct, pLastProductQueried) != 0
			|| stricmp(pCurPatchServer, pLastPatchServerQueried) != 0))
	{
		char *pPatchServer = NULL;
		GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHSERVER), &pPatchServer);

		SetTextFastf(GetDlgItem(pWindow->hWnd, IDC_NO_LIBRARY_TEXT), 
			"Loading config options for %s %s", pCurProduct, pCurPatchVersion);

		estrCopy(&pLastPatchVersionQueried, &pCurPatchVersion);
		estrCopy(&pLastProductQueried,  &pCurProduct);
		estrCopy(&pLastPatchServerQueried,  &pCurPatchServer);
		ThreadedPCL_GetFileIntoRAM(pCurPatchServer, STACK_SPRINTF("%sServer", pCurProduct), 
			pCurPatchVersion, "data/server/ShardLauncher_ConfigOptions.txt", ShardLauncherGetFileCB,
			strdup(pCurPatchVersion));
		gbOptionLibraryCurrentlyPatching = true;
	}

	
}

char *GetClusterSummaryString(void)
{
	static char *spRetVal = NULL;

	if (eaSize(&gpRun->ppClusterShards))
	{
		int i;
		estrPrintf(&spRetVal, "Cluster %s has %d shards: ", gpRun->pClusterName, eaSize(&gpRun->ppClusterShards));
		for (i = 0; i < eaSize(&gpRun->ppClusterShards); i++)
		{
			estrConcatf(&spRetVal, "%s%s%s", i == 0 ? "" : ", ", gpRun->ppClusterShards[i]->pShardName,
				gpRun->ppClusterShards[i]->eShardType == SHARDTYPE_UGC ? "(UGC)" : "");
		}

		return spRetVal;
	}

	return "Cluster not yet set up";
}

bool mainScreenDlgProc_SWMTick(SimpleWindow *pWindow)
{
	int iShardSetupFileNum = GetComboBoxSelectedIndex(pWindow->hWnd, IDC_SHARDSETUPFILEPICKER);

	if (gpRun->bClustered)
	{
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_SHARDSETUPFILE_COMMENT), GetClusterSummaryString());
	}
	else
	{
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_SHARDSETUPFILE_COMMENT), ppShardSetupFileDescriptions[iShardSetupFileNum]);
	}

	if (gbOptionLibraryCurrentlyPatching)
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OPTIONLIBRARYPATCHINGONGOING), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_CLOSEWINDOWS), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_JUSTPATCH), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SAVECHANGES), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHWITHOUTPATCHING), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHANDLAUNCH), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_REQUIREDFIELDS), SW_HIDE);	
	}
	else if (SimpleWindowManager_FindWindowByType(WINDOWTYPE_OPTIONS) || SimpleWindowManager_FindWindowByType(WINDOWTYPE_CONTROLLERCOMMANDS))
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_CLOSEWINDOWS), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OPTIONLIBRARYPATCHINGONGOING), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_JUSTPATCH), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SAVECHANGES), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHWITHOUTPATCHING), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHANDLAUNCH), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_REQUIREDFIELDS), SW_HIDE);
	}
	else if (AllRequiredFieldsSet() && IsPatchVersionSet(pWindow->hWnd))
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_CLOSEWINDOWS), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OPTIONLIBRARYPATCHINGONGOING), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_JUSTPATCH), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SAVECHANGES), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHWITHOUTPATCHING), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHANDLAUNCH), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_REQUIREDFIELDS), SW_HIDE);
	}
	else
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_REQUIREDFIELDS), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_CLOSEWINDOWS), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OPTIONLIBRARYPATCHINGONGOING), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_JUSTPATCH), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SAVECHANGES), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHWITHOUTPATCHING), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHANDLAUNCH), SW_HIDE);
	}

	if (IsProductSet(pWindow->hWnd))
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_CANT_CHOOSE_VERSION), SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SELECTPATCHVERSION), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_CANT_CHOOSE_VERSION), SW_SHOW);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SELECTPATCHVERSION), SW_HIDE);
	}

	utilitiesLibOncePerFrame(REAL_TIME);
	UpdateConfigOptionsFileFromPatch(pWindow);

	if (sbLibraryHasChanged)
	{
		SetUpOptionButtonsFromLibrary(pWindow);
		sbLibraryHasChanged = false;
	}

	return false;
}

bool AnyFieldsAreSet(ShardLauncherConfigOptionList *pList)
{
	int i;
	for (i=0; i < eaSize(&pList->ppOptions); i++)
	{
		GlobalType tempType;
		ShardLauncherConfigOptionChoice *pTemplateChoice = eaIndexedGetUsingString(&gpRun->ppTemplateChoices, pList->ppOptions[i]->pName);
		ShardLauncherConfigOptionChoice *pChoice = eaIndexedGetUsingString(&gpRun->ppChoices, pList->ppOptions[i]->pName);

		ShardLauncherConfigOption *pOption = FindConfigOption(gpRun, pList->ppOptions[i]->pName, &tempType);


		if (!pTemplateChoice && !pChoice)
		{
			continue;
		}

		//this means that the template value is set and is untouched in the choice, so it inherits
		if (pTemplateChoice && !pChoice)
		{
			continue;
		}

		if (pChoice && !pTemplateChoice)
		{
			if (pOption && stricmp_safe(pOption->pDefaultChoice, pChoice->pValue) == 0)
			{
				continue;
			}

			return true;
		}

		if (stricmp_safe(pChoice->pValue, pTemplateChoice->pValue) != 0)
		{
			return true;
		}
	}


	return false;
}



bool AnyFieldsAreSetShardSpecific(ShardLauncherConfigOptionList *pList)
{

	FOR_EACH_IN_EARRAY(pList->ppOptions, ShardLauncherConfigOption, pOption)
	{
		FOR_EACH_IN_EARRAY(gpRun->ppChoices, ShardLauncherConfigOptionChoice, pChoice)
		{
			if (pChoice->pConfigOptionName[0] == '{')
			{
				static char *pTemp = NULL;
				estrCopy2(&pTemp, pChoice->pConfigOptionName);
				estrRemoveUpToFirstOccurrence(&pTemp, '}');
				estrTrimLeadingAndTrailingWhitespace(&pTemp);
				if (stricmp(pTemp, pOption->pName) == 0)
				{
					return true;
				}
			}
		}
		FOR_EACH_END;

	}
	FOR_EACH_END;


	return false;
}

bool AnyServerSpecificFieldsAreSet(GlobalType eType)
{
	int i;

	if (!gpRun->pOptionLibrary || !gpRun->pOptionLibrary->pServerTypeSpecificList)
	{
		return false;
	}

	for (i=0; i < eaSize(&gpRun->pOptionLibrary->pServerTypeSpecificList->ppOptions); i++)
	{
		ShardLauncherConfigOptionChoice *pTemplateChoice = eaIndexedGetUsingString(&gpRun->ppTemplateChoices, GetServerTypeSpecificOptionName(gpRun->pOptionLibrary->pServerTypeSpecificList->ppOptions[i]->pName, eType));
		ShardLauncherConfigOptionChoice *pChoice = eaIndexedGetUsingString(&gpRun->ppChoices, GetServerTypeSpecificOptionName(gpRun->pOptionLibrary->pServerTypeSpecificList->ppOptions[i]->pName, eType));

		if (!pTemplateChoice && !pChoice)
		{
			continue;
		}

		//this means that the template value is set and is untouched in the choice, so it inherits
		if (pTemplateChoice && !pChoice)
		{
			continue;
		}

		if (pChoice && !pTemplateChoice)
		{
			return true;
		}

		if (stricmp_safe(pChoice->pValue, pTemplateChoice->pValue) != 0)
		{
			return true;
		}
	}


	return false;
}

bool AnyServerSpecificFieldsAreSetShardSpecific(GlobalType eType)
{
	char bracketed[256];
	sprintf(bracketed, "[%s]", GlobalTypeToName(eType));


	FOR_EACH_IN_EARRAY(gpRun->ppChoices, ShardLauncherConfigOptionChoice, pChoice)
	{
		if (pChoice->pConfigOptionName[0] == '{')
		{
			static char *pTemp = NULL;
			estrCopy2(&pTemp, pChoice->pConfigOptionName);
			estrRemoveUpToFirstOccurrence(&pTemp, '}');
			estrTrimLeadingAndTrailingWhitespace(&pTemp);

			if (strStartsWith(pTemp, bracketed))
			{
				return true;
			}
		}
	}
	FOR_EACH_END;

	return false;
}

BOOL mainScreenDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
//	LRESULT lResult;
	int i;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		if (!MainScreen_Init(hDlg, pWindow))
		{
			pWindow->bCloseRequested = true;
		}
		break;


	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_SHARDSETUPFILE_COMMENT))
		{
			if (!gpRun->bClustered)
			{
				return false;
			}

			if (eaSize(&gpRun->ppClusterShards))
			{
				return false;
			}

			{
				HDC hdc = (HDC)wParam;
				SetBkColor(hdc, YELLOW_COLOR);
				return (BOOL)((intptr_t)sYellow);
			}
		}
		else if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_PRODUCTPICKER)
			|| (HANDLE)lParam == GetDlgItem(hDlg, IDC_PRODUCT))
		{
			if (IsProductSet(hDlg))
			{
				return false;
			}
			else
			{
				HDC hdc = (HDC)wParam;
				SetBkColor(hdc, YELLOW_COLOR);
				return (BOOL)((intptr_t)sYellow);
			}
		}
		else if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_PATCHVERSION_LABEL) 
			|| (HANDLE)lParam == GetDlgItem(hDlg, IDC_PATCHVERSION_COMMENT))
		{
			if (IsPatchVersionSet(hDlg))
			{
				return false;
			}
			else
			{
				HDC hdc = (HDC)wParam;
				SetBkColor(hdc, YELLOW_COLOR);
				return (BOOL)((intptr_t)sYellow);
			}

		}
		else if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_CONTROLLERCOMMANDSTXT))
		{
			if (gpRun->pControllerCommandsArbitraryText && gpRun->pControllerCommandsArbitraryText[0])
			{
				HDC hdc = (HDC)wParam;
				SetBkColor(hdc, BAD_COLOR);
				return (BOOL)((intptr_t)sBad);
			}
			else
			{
				return false;
			}
		}
		else if ((HANDLE)lParam == GetDlgItem(hDlg, IDC_STATIC_OVERRIDEEXES))
		{
			if (eaSize(&gpRun->ppOverrideExecutableNames) == 0)
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
		else 
		{
			if (gpRun->pOptionLibrary)
			{
				for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
				{
					if ((HANDLE)lParam == GetDlgItem(hDlg, gOptionButtonIDs[i]))
					{
						if (AllRequiredFieldsSetInOptionList(gpRun->pOptionLibrary->ppLists[i]))
						{
							if (AnyFieldsAreSetShardSpecific(gpRun->pOptionLibrary->ppLists[i]))
							{
								HDC hdc = (HDC)wParam;
								SetBkColor(hdc, PURPLE_COLOR);
								return (BOOL)((intptr_t)sPurple);
							}

							if (AnyFieldsAreSet(gpRun->pOptionLibrary->ppLists[i]))
							{
								HDC hdc = (HDC)wParam;
								SetBkColor(hdc, BAD_COLOR);
								return (BOOL)((intptr_t)sBad);
							}

							return false;
						}
						else
						{
							HDC hdc = (HDC)wParam;
							SetBkColor(hdc, YELLOW_COLOR);
							return (BOOL)((intptr_t)sYellow);
						}
					}
				}
			

				for (i=0; i < ea32Size(&gpRun->pOptionLibrary->pServerSpecificScreenTypes); i++)
				{
					if ((HANDLE)lParam == GetDlgItem(hDlg, gServerScreenButtonIDs[i]))
					{		
						if (AnyServerSpecificFieldsAreSetShardSpecific(gpRun->pOptionLibrary->pServerSpecificScreenTypes[i]))
						{
							HDC hdc = (HDC)wParam;
							SetBkColor(hdc, PURPLE_COLOR);
							return (BOOL)((intptr_t)sPurple);
						}

						if (AnyServerSpecificFieldsAreSet(gpRun->pOptionLibrary->pServerSpecificScreenTypes[i]))
						{
							HDC hdc = (HDC)wParam;
							SetBkColor(hdc, BAD_COLOR);
							return (BOOL)((intptr_t)sBad);
						}

						return false;
					}
				}
			}
		}
		return false;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_CLUSTERED:
			gpRun->bClustered = IsDlgButtonChecked(hDlg, IDC_CLUSTERED);
	
			SetupClusterOptions(pWindow);
			break;



		case IDC_LOAD_CONFIG_OLD:
			LoadOldStyleConfig();
			break;

		case IDC_LOAD_CONFIG_CUSTOM:
			LoadCustomConfig();
			break;

		case IDC_CLUSTER_SETUP:
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_CLUSTERSETUP, 0, IDD_CLUSTERSETUP,
				false, clusterSetupDlgProc_SWM, clusterSetupDlgProc_SWMTick, NULL);
			break;


		case IDC_PRODUCTPICKER:
			InvalidateRect(GetDlgItem(hDlg, IDC_PRODUCT), NULL, false);
			break;

		case IDC_SELECTPATCHVERSION:
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_CHOOSEVERSION, 0, IDD_PICKPATCHVERSION,
				false, patchVersionPickerDlgProc_SWM, patchVersionPickerDlgProc_SWMTick, NULL);
			break;

		case IDC_CONTROLLERCOMMANDSTXT:
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_CONTROLLERCOMMANDS, 0, IDD_CONTROLLERCOMMANDS, 
				false, controllerCommandsDlgProc_SWM, NULL, NULL);
			break;

		case IDC_STATIC_OVERRIDEEXES:
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_OVERRIDEEXES, 0, IDD_OVERRIDEEXES, 
				false, overrideExesDlgProc_SWM, NULL, NULL);
			break;


		case IDC_JUSTPATCH:
		case IDC_LAUNCHWITHOUTPATCHING:
		case IDC_PATCHANDLAUNCH:
		case IDC_SAVECHANGES:

			GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PRODUCTPICKER), &gpRun->pProductName);
			GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHSERVER), &gpRun->pPatchServer);
			GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_LOCALDIR), &gpRun->pDirectory);
			GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHVERSION_LABEL), &gpRun->pPatchVersion);
			GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_SHARDSETUPFILEPICKER), &gpRun->pShardSetupFile);
			GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PATCHVERSION_COMMENT), &gpRun->pPatchVersionComment);

			for (i=0; i < eaSize(&gpProductList->ppProducts); i++)
			{
				if (stricmp(gpRun->pProductName, gpProductList->ppProducts[i]->pProductName) == 0)
				{
					estrCopy2(&gpRun->pShortProductName, gpProductList->ppProducts[i]->pShortProductName);
					break;
				}
			}

			if (i == eaSize(&gpProductList->ppProducts))
			{
				LOG_FAIL("Unrecognized product %s", gpRun->pShortProductName);
				pWindow->bCloseRequested = true;
				return false;
			}

			switch (LOWORD (wParam))
			{
			xcase IDC_JUSTPATCH:
				gRunType = RUNTYPE_PATCH;
			xcase IDC_LAUNCHWITHOUTPATCHING:
				gRunType = RUNTYPE_LAUNCH;
			xcase IDC_PATCHANDLAUNCH:
				gRunType = RUNTYPE_PATCHANDLAUNCH;
			xcase IDC_SAVECHANGES:
				gRunType = RUNTYPE_SAVECHANGES;
			}

			if (gRunType != RUNTYPE_SAVECHANGES && gpRun->bClustered)
			{
				char *pTemp = NULL;
				GetWindowText_UTF8(GetDlgItem(hDlg, IDC_SHARDTOLAUNCH), &pTemp);
				strcpy_trunc(gpRun->onlyShardToLaunch, pTemp);
				estrDestroy(&pTemp);
			}

			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_CHOOSENAME, 0, IDD_CHOOSENAME,
				true, chooseNameDlgProc_SWM, chooseNameDlgProc_SWMTick, NULL);
			pWindow->bCloseRequested = true;
			break;


/*
			gRunType = RUNTYPE_LAUNCH;
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_CHOOSENAME, 0, IDD_CHOOSENAME,
				true, chooseNameDlgProc_SWM, chooseNameDlgProc_SWMTick, NULL);
			pWindow->bCloseRequested = true;
			break;
			gRunType = RUNTYPE_PATCHANDLAUNCH;
			SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_CHOOSENAME, 0, IDD_CHOOSENAME,
				true, chooseNameDlgProc_SWM, chooseNameDlgProc_SWMTick, NULL);
			pWindow->bCloseRequested = true;
			break;*/
		}

		if (gpRun->pOptionLibrary)
		{
			for (i=0; i < eaSize(&gpRun->pOptionLibrary->ppLists); i++)
			{
				if (LOWORD(wParam) == gOptionButtonIDs[i])
				{
					SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_OPTIONS, i, IDD_MULTIPICKER,
						false, shardLauncherOptionsDlgProc_SWM, NULL, NULL);
				}
			}

			for (i=0; i < ea32Size(&gpRun->pOptionLibrary->pServerSpecificScreenTypes); i++)
			{
				if (LOWORD(wParam) == gServerScreenButtonIDs[i])
				{
					SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_OPTIONS, gpRun->pOptionLibrary->pServerSpecificScreenTypes[i] + SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER, IDD_MULTIPICKER,
						false, shardLauncherOptionsDlgProc_SWM, NULL, NULL);
				}
			}
		}
	}

	return false;
}

char *GetAutoSettingsWarningString(void)
{
	static char *spRetVal = NULL;
	SimpleWindow *pWindow;
	static char *pLocalDir = NULL;
	static char *pProductName = NULL;
	static char *pCurProduct = NULL;
	char fileName[CRYPTIC_MAX_PATH];

	estrClear(&spRetVal);
	estrClear(&pLocalDir);
	estrClear(&pProductName);

	pWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);

	assert(pWindow);

	if (!IsProductSet(pWindow->hWnd))
	{
		return "These options only apply to brand new shards never before run. You currently haven't set a product, so we can't verify that is the case here. Tread lightly";
	}

	GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_LOCALDIR), &pLocalDir);

	if (!estrLength(&pLocalDir))
	{
		return "These options only apply to brand new shards never before run. You currently haven't set a local dir, so we can't verify that is the case here. Tread lightly";
	}

	GetWindowText_UTF8(GetDlgItem(pWindow->hWnd, IDC_PRODUCTPICKER), &pCurProduct);

	estrReplaceOccurrences(&pLocalDir, "$PRODUCTNAME$", pCurProduct);

	sprintf(fileName, "%s/%sServer/%s_ControllerAutoSettings.txt", pLocalDir, pCurProduct, pCurProduct);

	if (fileExists(fileName))
	{
		estrPrintf(&spRetVal, "These options only apply to brand new shards never before run. %s exists, so changes here will have no effect",
			fileName);
		return spRetVal;
	}

	return NULL;
}

#include "ShardLauncherMainScreen_c_ast.c"
#include "ControllerPub_h_Ast.c"
#include "svrglobalinfo_h_Ast.c"
#include "globalenums_h_Ast.c"
#include "MapDescription_h_Ast.c"