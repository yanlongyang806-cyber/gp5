#include "ShardLauncher.h"
#include "ShardLauncherStartScreen.h"
#include "ShardLauncherMainScreen.h"
#include "ShardLauncher_h_ast.h"
#include "resource.h"
#include "wininclude.h"
#include "winutil.h"
#include "fileutil2.h"
#include "timing.h"
#include "ShardLauncherUI.h"
#include "ShardLauncherWarningScreen.h"
#include "../../libs/patchclientLib/patchTrivia.h"
#include "../../libs/PatchClientLib/pcl_client_wt.h"
#include "sysutil.h"
#include "utilitiesLib.h"
#include "ShardLauncherRunTheShard.h"
#include "ShardLauncherWatchTheLaunch.h"
#include "ShardLauncher_pub_h_ast.h"
#include "StringUtil.h"
#include "SentryServerComm.h"
#include "net.h"
#include "UTF8.h"

bool sbNeedToConfigure;
static char **spTemplateNames = NULL;
static char **spTemplateDescriptions = NULL;
static char **spTemplateFileNames = NULL;


static ShardLauncherRun **sppRecentRuns = NULL;



//only need to sort by byte, not by bit
int sortRuns(const ShardLauncherRun **pRun1, const ShardLauncherRun **pRun2)
{
	if ((*pRun1)->iLastRunTime > (*pRun2)->iLastRunTime)
	{
		return -1;
	}
	else if ((*pRun1)->iLastRunTime < (*pRun2)->iLastRunTime)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void CheckForDuplicateConfigChoices(ShardLauncherRun *pRun, char *pFileName)
{
	int i;
	int j;
	ShardLauncherConfigOptionChoice *pChoice1;
	ShardLauncherConfigOptionChoice *pChoice2;

	for (i=0; i < eaSize(&pRun->ppChoices) - 1; i++)
	{
		pChoice1 = pRun->ppChoices[i];
		for (j= i+1; j < eaSize(&pRun->ppChoices); j++)
		{
			pChoice2 = pRun->ppChoices[j];
	
			if (stricmp(pChoice1->pConfigOptionName, pChoice2->pConfigOptionName) == 0)
			{
				//two settings for the same config option, presumably due to a bug from a while ago when
				//transitioning from the old settings to the new server-specific settings screens. Don't do anything
				//automatic because valuable stuff might be set in either/both
				char *pErrorString = NULL;

				estrPrintf(&pErrorString, "WARNING!!!! WARNING!!!! WARNING!!!!\nWhile loading previous run \"%s\", ShardLauncher noticed that you have two different option settings for \"%s\".\nThis is not your fault, but due to a bug that occurred back during a data format transition, combined with another bug which didn't notice it until now.\nAnyhow, ShardLauncher does not feel qualified to try fix this for you in case you have important settings in both config options\n",
					pRun->pRunName, pChoice1->pConfigOptionName);
				estrConcatf(&pErrorString, "\nThe two values are:\n\n%s\nand\n%s\n\n",
					pChoice1->pValue, pChoice2->pValue);
				estrConcatf(&pErrorString, "Please resolve this by hand-editing the following file: %s\n", pFileName);

				estrFixupNewLinesForWindows(&pErrorString);

				DisplayWarning("WARNING!", pErrorString);

				estrDestroy(&pErrorString);
			}
		}
	}



}


void LoadRecentRuns(void)
{
	char **ppFileNames = fileScanDirFolders(RECENT_RUNS_FOLDER, FSF_FILES);
	int i;

	eaDestroyStruct(&sppRecentRuns, parse_ShardLauncherRun);

	for (i=0; i < eaSize(&ppFileNames); i++)
	{
		ShardLauncherRun *pRun = StructCreate(parse_ShardLauncherRun);
		ParserReadTextFile(ppFileNames[i], parse_ShardLauncherRun, pRun, 0);

		if (pRun->pRunName)
		{
			//fix up last-run and last-modified for transition to new code which includes them
			if (pRun->iLastModifiedTime == 0)
			{
				pRun->iLastModifiedTime = pRun->iLastRunTime = fileLastChangedSS2000(ppFileNames[i]);
			}

			eaPush(&sppRecentRuns, pRun);
		}
		else
		{
			StructDestroy(parse_ShardLauncherRun, pRun);
		}

		CheckForDuplicateConfigChoices(pRun, ppFileNames[i]);
	}

	fileScanDirFreeNames(ppFileNames);

	eaQSort(sppRecentRuns, sortRuns);
}

static char sNewestShardLauncherVersion[256] = "(querying)";
static const char *spPatchedVersion = false;

static void PatchVersionsCB(PatchVersionInfo **ppVersions, PCL_ErrorCode error, char *pErrorDetails, void *pUserData)
{
	if (eaSize(&ppVersions))
	{
		strcpy(sNewestShardLauncherVersion, ppVersions[eaSize(&ppVersions)-1]->pName);
	}
}


bool startScreenDlgProc_SWMTick(SimpleWindow *pWindow)
{
	int iIndex = GetComboBoxSelectedIndex(pWindow->hWnd, IDC_TEMPLATES);
	if (iIndex != -1)
	{
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_TEMPLATE_DESC), spTemplateDescriptions[iIndex]);
	}

	iIndex = GetComboBoxSelectedIndex(pWindow->hWnd, IDC_HISTORY);
	if (iIndex != -1)
	{
		char *pDateString = NULL;
		char *pTempString = NULL;

		if (sppRecentRuns[iIndex]->iLastRunTime)
		{
			timeSecondsDurationToPrettyEString(timeSecondsSince2000() - sppRecentRuns[iIndex]->iLastRunTime, &pDateString);
			estrPrintf(&pTempString, "Last run: %s ago (%s)\r\n", pDateString, timeGetLocalDateStringFromSecondsSince2000(sppRecentRuns[iIndex]->iLastRunTime));
		}
		else
		{
			estrPrintf(&pTempString, "(Never run)\n");
		}

		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - sppRecentRuns[iIndex]->iLastModifiedTime, &pDateString);
		estrConcatf(&pTempString, "Last modified: %s ago (%s)", pDateString, timeGetLocalDateStringFromSecondsSince2000(sppRecentRuns[iIndex]->iLastModifiedTime));

		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_LAUNCH_DESC), pTempString);

		estrDestroy(&pDateString);
		estrDestroy(&pTempString);
	}
	
	if (spPatchedVersion)
	{
		char temp[512];
		sprintf(temp, "ShardLauncher version: %s\n\rNewest version: %s", spPatchedVersion, sNewestShardLauncherVersion);
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_UPDATE_TEXT), temp);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_UPDATE_SHARDLAUNCHER), SW_SHOW);
	}
	
	utilitiesLibOncePerFrame(REAL_TIME);

	return false;
}

static bool sbGotOptionLibraryForAutoRun = false;


static void ApplyAutoRunGetFileCB(void *pFileData, int iFileSize, PCL_ErrorCode error, char *pErrorDetails, void *pUserData)
{
	sbGotOptionLibraryForAutoRun = true;

	if (!pFileData)
	{
		assertmsgf(0, "Failed to get config library file for autoRun");
		return;
	}
	else
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
				}
			}
		}
		else
		{
			assertmsgf(0, "Loaded option library for auto run, but couldn't read it");
		}
	}

}

void ApplyAutoRunToRun(ShardLauncherAutoRun *pAutoRun, ShardLauncherRun *pRun)
{
	if (pAutoRun->pPatchVersion)
	{
		estrCopy2(&pRun->pPatchVersion, pAutoRun->pPatchVersion);
		estrDestroy(&pRun->pPatchVersionComment);

		if (pAutoRun->pPatchVersionComment)
		{
			estrCopy2(&pRun->pPatchVersionComment, pAutoRun->pPatchVersionComment);
		}

		if (!pRun->pOptionLibrary || stricmp_safe(pRun->pPatchVersion, pRun->pLibraryVersion) != 0)
		{
			int iCounter = 0;
			printf("Need to load patch option library for auto run\n");
			ThreadedPCL_GetFileIntoRAM(pRun->pPatchServer, STACK_SPRINTF("%sServer", pRun->pProductName), 
				pRun->pPatchVersion, "data/server/ShardLauncher_ConfigOptions.txt", ApplyAutoRunGetFileCB,
				NULL);

			while (!sbGotOptionLibraryForAutoRun)
			{
				iCounter++;
				utilitiesLibOncePerFrame(REAL_TIME);
				SentryServerComm_Tick();
				commMonitor(commDefault());
				Sleep(1);
				if (iCounter == 60000)
				{
					assertmsgf(0, "never got response back from patch server for config library for auto run");
				}
			}

			estrCopy2(&pRun->pLibraryVersion, pRun->pPatchVersion);
		}
	}

}



BOOL startScreenDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	LRESULT lResult;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			char **ppTemplateFiles = NULL;
			int i;
			

			LoadRecentRuns();

			eaDestroyEx(&spTemplateDescriptions, NULL);
			eaDestroyEx(&spTemplateFileNames, NULL);


			if (gpRun)
			{
				StructDestroy(parse_ShardLauncherRun, gpRun);
				gpRun = NULL;
			}

			ppTemplateFiles = fileScanDirFolders(TEMPLATE_DIR_NAME, FSF_FILES);
			for (i=0; i < eaSize(&ppTemplateFiles); i++)
			{
				ShardLauncherRun *pTemplate = StructCreate(parse_ShardLauncherRun);
				if (ParserReadTextFile(ppTemplateFiles[i], parse_ShardLauncherRun, pTemplate, 0))
				{
					eaPush(&spTemplateNames, strdup(pTemplate->pRunName));
					eaPush(&spTemplateDescriptions, strdup(pTemplate->pComment));
					eaPush(&spTemplateFileNames, strdup(ppTemplateFiles[i]));
				}
				StructDestroy(parse_ShardLauncherRun, pTemplate);
			}
			fileScanDirFreeNames(ppTemplateFiles);

			for (i=0; i < eaSize(&sppRecentRuns); i++)
			{
				lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_HISTORY), sppRecentRuns[i]->pRunName);
				SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
			}

			if (eaSize(&sppRecentRuns))
			{
				SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_SETCURSEL, 0, 0);
			}

			for (i=0; i < eaSize(&spTemplateNames); i++)
			{
				lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_TEMPLATES), spTemplateNames[i]);
				SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
			}

			if (eaSize(&spTemplateNames))
			{
				SendMessage_SelectString_UTF8(GetDlgItem(hDlg, IDC_TEMPLATES), spTemplateNames[0]);
			}

			spPatchedVersion = amIASimplePatchedApp();
			if (spPatchedVersion)
			{
				char temp[512];
				char exeDir[CRYPTIC_MAX_PATH];
				char project[256];
				char server[256];


				sprintf(temp, "ShardLauncher version: %s\n\rNewest version: %s", spPatchedVersion, sNewestShardLauncherVersion);
				SetTextFast(GetDlgItem(hDlg, IDC_UPDATE_TEXT), temp);
				ShowWindow(GetDlgItem(hDlg, IDC_UPDATE_SHARDLAUNCHER), SW_SHOW);
	

				getExecutableDir(exeDir);


				if (!triviaGetPatchTriviaForFile(SAFESTR(project), exeDir, "PatchProject"))
				{
					break;
				}
				if (!triviaGetPatchTriviaForFile(SAFESTR(server), exeDir, "PatchServer"))
				{
					break;
				}

				ThreadedPCL_GetPatchVersions(server, project, PatchVersionsCB, NULL);

			}
			else
			{
				SetTextFast(GetDlgItem(hDlg, IDC_UPDATE_TEXT), "Shardlauncher is unpatched");
				ShowWindow(GetDlgItem(hDlg, IDC_UPDATE_SHARDLAUNCHER), SW_HIDE);
			}

			if (!sMachineNamesFromSentryServer)
			{
				DisplayWarning("SentryServer comm failure", "Unable to get machine list from Sentry Server... if you're trying to launch a multi-machine shard, something is probably very wrong");
			}

			if (gpAutoRun && gpAutoRun->pRunName)
			{
				gpRun = LoadRunFromName(gpAutoRun->pRunName);
				gRunType = RUNTYPE_PATCHANDLAUNCH;	

				assertmsgf(gpRun, "Trying to use AutoRun.txt to load run %s... it doesn't seem to exist",
					gpAutoRun->pRunName);

				gpRun->iLastRunTime = timeSecondsSince2000();

				ApplyAutoRunToRun(gpAutoRun, gpRun);

				SaveRunToDisk(gpRun, timeSecondsSince2000());
				RunTheShard_Log("Began PatchAndLaunch of %s because of AutoRun.txt",
					gpAutoRun->pRunName);
				RunTheShard(gpRun);
				pWindow->bCloseRequested = true;
				SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_WATCHTHELAUNCH, 0, IDD_WATCHLAUNCH,
					true, watchTheLaunchDlgProc_SWM, watchTheLaunchDlgProc_SWMTick, NULL);

			}


		}
		break;

	case WM_CLOSE:
		pWindow->bCloseRequested = true;	
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_PATCH_AND_LAUNCH:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					char *pWarningString = NULL;
					gpRun = LoadRunFromName(sppRecentRuns[lResult]->pRunName);
					gRunType = RUNTYPE_PATCHANDLAUNCH;

					if (gpRun)
					{

						if (CheckRunForWarnings(gpRun, &pWarningString))
						{
							estrConcatf(&pWarningString, "\n\n---------------\n\nPlease click run again to proceed if you are very very confident");
							DisplayWarning("WARNING!", pWarningString);
							estrDestroy(&pWarningString);
						}
						else
						{

							gpRun->iLastRunTime = timeSecondsSince2000();
							SaveRunToDisk(gpRun, timeSecondsSince2000());
							RunTheShard(gpRun);
							pWindow->bCloseRequested = true;
							SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_WATCHTHELAUNCH, 0, IDD_WATCHLAUNCH,
								true, watchTheLaunchDlgProc_SWM, watchTheLaunchDlgProc_SWMTick, NULL);
						}					
					}
				}
			}
			break;
		case IDC_LAUNCH_W_O_PATCHING:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					
					gpRun = LoadRunFromName(sppRecentRuns[lResult]->pRunName);
					gRunType = RUNTYPE_LAUNCH;
					if (gpRun)
					{
						char *pWarningString = NULL;

						if (CheckRunForWarnings(gpRun, &pWarningString))
						{
							estrConcatf(&pWarningString, "\n\n---------------\n\nPlease click run again to proceed if you are very very confident");
							DisplayWarning("WARNING!", pWarningString);
							estrDestroy(&pWarningString);
						}
						else
						{
							pWindow->bCloseRequested = true;
							gpRun->iLastRunTime = timeSecondsSince2000();
							SaveRunToDisk(gpRun, timeSecondsSince2000());
							RunTheShard(gpRun);
							SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_WATCHTHELAUNCH, 0, IDD_WATCHLAUNCH,
								true, watchTheLaunchDlgProc_SWM, watchTheLaunchDlgProc_SWMTick, NULL);
							
						}
					}
				}
			}
			break;

		case IDC_MODIFY:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					pWindow->bCloseRequested = true;
					gpRun = LoadRunFromName(sppRecentRuns[lResult]->pRunName);
					if (gpRun)
					{
						SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_MAINSCREEN, 0, IDD_MAINSCREEN,
							true, mainScreenDlgProc_SWM, mainScreenDlgProc_SWMTick, NULL);
					}
				}
			}
			break;

		case IDC_LAUNCHTEMPLATE:
			{
				ShardLauncherRun *pTemplate = StructCreate(parse_ShardLauncherRun);
				int iTemplateIndex = GetComboBoxSelectedIndex(hDlg, IDC_TEMPLATES);

				if (!ParserReadTextFile(spTemplateFileNames[iTemplateIndex], parse_ShardLauncherRun, pTemplate, 0))
				{
					LOG_FAIL("Couldn't load template %s", spTemplateFileNames[iTemplateIndex]);
					EndDialog(hDlg, 0);
					return 0;
				}

				gpRun = StructCreate(parse_ShardLauncherRun);
				estrCopy2(&gpRun->pTemplateFileName, spTemplateFileNames[iTemplateIndex]);
				estrPrintf(&gpRun->pComment, "Copied from template %s: %s", spTemplateNames[iTemplateIndex], 
					timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()));

				estrCopy2(&gpRun->pPatchServer, pTemplate->pPatchServer);
				estrCopy2(&gpRun->pDirectory, pTemplate->pDirectory);
				estrCopy2(&gpRun->pControllerCommandsArbitraryText, pTemplate->pControllerCommandsArbitraryText);

				gpRun->ppTemplateChoices = pTemplate->ppChoices;
				pTemplate->ppChoices = NULL;

				StructDestroy(parse_ShardLauncherRun, pTemplate);
				SimpleWindowManager_AddOrActivateWindow(WINDOWTYPE_MAINSCREEN, 0, IDD_MAINSCREEN,
					true, mainScreenDlgProc_SWM, mainScreenDlgProc_SWMTick, NULL);
				pWindow->bCloseRequested = true;
			

			}
			break;
		case IDC_UPDATE_SHARDLAUNCHER:
			updateAndRestartSimplePatchedApp();
		}
	}




	return false;
}

/*
BOOL CALLBACK startScreenDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	LRESULT lResult;

	switch (iMsg)
	{

	case WM_INITDIALOG:
	
		sbNeedToConfigure = false;
		SetTimer(hDlg, 0, 1, NULL);

		for (i=0; i < eaSize(&spHistory->ppRunNames); i++)
		{
			lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_HISTORY), spHistory->ppRunNames[i]);
			SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
		}

		if (eaSize(&spHistory->ppRunNames))
		{
			SendMessage(GetDlgItem(hDlg, IDC_HISTORY), spHistory->ppRunNames[0]);
		}

		for (i=0; i < eaSize(&spTemplateNames); i++)
		{
			lResult = SendMessage_AddString_UTF8(GetDlgItem(hDlg, IDC_TEMPLATES), spTemplateNames[i]);
			SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
		}

		if (eaSize(&spTemplateNames))
		{
			SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), spTemplateNames[0]);
		}



		ShowWindow(hDlg, SW_SHOW);
		return true; 

	case WM_TIMER:
		if (spTemplateNames && eaSize(&spTemplateNames))
		{
			lResult = SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					SetTextFast(GetDlgItem(hDlg, IDC_TEMPLATE_DESC), spTemplateDescriptions[lResult]);
				}
			}
		}


		break;





	case WM_CLOSE:
		EndDialog(hDlg, 0);
	
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_LAUNCH:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					gpRun = LoadRunFromName(spHistory->ppRunNames[lResult]);
					EndDialog(hDlg, 0);
				}
			}
			break;
		case IDC_MODIFYANDLAUNCH:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_HISTORY), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					gpRun = LoadRunFromName(spHistory->ppRunNames[lResult]);
					sbNeedToConfigure = true;
					EndDialog(hDlg, 0);
				}
			}
			break;

		case IDC_MODIFYANDLAUNCHTEMPLATE:
			lResult = SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_TEMPLATES), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					ShardLauncherRun *pTemplate = StructCreate(parse_ShardLauncherRun);
					
					if (!ParserReadTextFile(spTemplateFileNames[lResult], parse_ShardLauncherRun, pTemplate, 0))
					{
						LOG_FAIL("Couldn't load template %s", spTemplateFileNames[lResult]);
						EndDialog(hDlg, 0);
						return 0;
					}



					gpRun = StructCreate(parse_ShardLauncherRun);
					estrCopy2(&gpRun->pTemplateFileName, spTemplateFileNames[lResult]);
					estrPrintf(&gpRun->pComment, "Copied from template %s: %s", spTemplateNames[lResult], 
						timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()));

					estrCopy2(&gpRun->pPatchServer, pTemplate->pPatchServer);
					estrCopy2(&gpRun->pDirectory, pTemplate->pDirectory);

					StructDestroy(parse_ShardLauncherRun, pTemplate);
					

					sbNeedToConfigure = true;
					
					EndDialog(hDlg, 0);
				}
			}
			break;
		}

	}

	

	return false;
}
*/

/*
ShardLauncherRun *DoStartingScreen(bool *pbNeedToConfigure)
{

	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_STARTSCREEN), 0, (DLGPROC)startScreenDlgProc);
	*pbNeedToConfigure = sbNeedToConfigure;


	return gpRun;
}
*/