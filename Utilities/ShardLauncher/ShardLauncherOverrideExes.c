#include "ShardLauncher.h"
#include "ShardLauncherOverrideExes.h"
#include "earray.h"
#include "estring.h"
#include "resource.h"
#include "ShardLauncherUI.h"
#include "utils.h"
#include "file.h"
#include "error.h"
#include "UTF8.h"


//search in 4 places (if unique):
//(1) any directories of override exes already set
//(2) c:\src\projectname\bin
//(3) c:\src\core\bin
//(4) any directories specified in c:\shardlauncher\OverrideExeDirs.txt


//ideally we would get this list out of controller_serverSetup.txt, but that file might not exist locally and if it does,
//we might not know where.
char *pShardExeNames[] = 
{
	"controller",
	"launcher",
	"clusterController",
	"appServer",
	"gameserver",
	"objectdb",
	"transactionServer",
	"logparser",
	"logServer",
	"multiplexer",
	"mastercontrolprogram",
	"gameclient",
	"chatserver",
	"clientcontroller",
	"accountserver",
	"clientbinner",
	"testclient",
	"beaconserver",
	"beaconclient"
};



#define PUSH_DIR(str) { if (eaFindString(&ppDirs, (str)) == -1) eaPush(&ppDirs, strdup(str)); }



void GetPossibleOverrideExes(char ***pppOutNames)
{
	int i, j;
	static char *pTempDir = NULL;
	char tempFile[CRYPTIC_MAX_PATH];

	char **ppDirs = NULL;
	char *pBuf;

	mkdirtree_const("c:\\ShardLauncher\\foo.txt");

	for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
	{
		estrCopy2(&pTempDir, gpRun->ppOverrideExecutableNames[i]);
		backSlashes(pTempDir);
		estrTruncateAtLastOccurrence(&pTempDir, '\\');

		PUSH_DIR(pTempDir);
	}

	estrPrintf(&pTempDir, "c:\\src\\%s\\bin", gpRun->pProductName);
	PUSH_DIR(pTempDir);
	PUSH_DIR("c:\\src\\core\\bin");

	if ((pBuf = fileAlloc("c:\\shardLauncher\\OverrideExeDirs.txt", NULL)))
	{
		char **ppDirsFromFile = NULL;
		DivideString(pBuf, "\n", &ppDirsFromFile, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		for (i=0; i < eaSize(&ppDirsFromFile); i++)
		{
			if (ppDirsFromFile[i][0] != '/' && ppDirsFromFile[i][0] != '#')
			{
				PUSH_DIR(ppDirsFromFile[i]);
			}
		}

		free(pBuf);
		eaDestroyEx(&ppDirsFromFile, NULL);
	}
	else
	{
		FILE *pOutFile = fopen("c:\\shardLauncher\\OverrideExeDirs.txt", "wt");
		if (!pOutFile)
		{
			Errorf("Couldn't open c:\\shardLauncher\\OverrideExeDirs.txt for writing\n");
			return;
		}

		fprintf(pOutFile, "//This file lists directories that are automatically scanned for override\n//executables by ShardLauncher. One per line, no commas or anything\n");
		fclose(pOutFile);
	}

	eaDestroyEx(pppOutNames, NULL);

	for (i=0; i < eaSize(&ppDirs); i++)
	{
		for (j=0; j < ARRAY_SIZE(pShardExeNames); j++)
		{
			sprintf(tempFile, "%s\\%s.exe", ppDirs[i], pShardExeNames[j]);
			backSlashes(tempFile);

			if (fileExists(tempFile) && eaFindString(pppOutNames, tempFile) == -1)
			{
				eaPush(pppOutNames, strdup(tempFile));
			}

			sprintf(tempFile, "%s\\%sX64.exe", ppDirs[i], pShardExeNames[j]);
			backSlashes(tempFile);

			if (fileExists(tempFile) && eaFindString(pppOutNames, tempFile) == -1)
			{
				eaPush(pppOutNames, strdup(tempFile));
			}

			sprintf(tempFile, "%s\\%sFD.exe", ppDirs[i], pShardExeNames[j]);
			backSlashes(tempFile);

			if (fileExists(tempFile) && eaFindString(pppOutNames, tempFile) == -1)
			{
				eaPush(pppOutNames, strdup(tempFile));
			}

			sprintf(tempFile, "%s\\%sX64FD.exe", ppDirs[i], pShardExeNames[j]);
			backSlashes(tempFile);

			if (fileExists(tempFile) && eaFindString(pppOutNames, tempFile) == -1)
			{
				eaPush(pppOutNames, strdup(tempFile));
			}


		}
	}

	eaDestroyEx(&ppDirs, NULL);
}



	

	

BOOL overrideExesDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	SimpleWindow *pParentWindow;

	static char *pTempString = NULL;
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i;
			char **ppLocalExes = NULL;
			estrClear(&pTempString);
			for (i=0; i < eaSize(&gpRun->ppOverrideExecutableNames); i++)
			{
				estrConcatf(&pTempString, "%s\r\n", gpRun->ppOverrideExecutableNames[i]);
			}
		
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EXES_TO_USE), pTempString);

			GetPossibleOverrideExes(&ppLocalExes);

			estrClear(&pTempString);
			for (i=0; i < eaSize(&ppLocalExes); i++)
			{
				estrConcatf(&pTempString, "%s\r\n", ppLocalExes[i]);
			}
		
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_POTENTIAL_EXES), pTempString);

			eaDestroyEx(&ppLocalExes, NULL);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			pWindow->bCloseRequested = true;
			return false;
		case IDOK:
			estrClear(&pTempString);
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_EXES_TO_USE), &pTempString);
			eaDestroyEx(&gpRun->ppOverrideExecutableNames, NULL);
			DivideString(pTempString, "\n", &gpRun->ppOverrideExecutableNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

			pWindow->bCloseRequested = true;



			pParentWindow = SimpleWindowManager_FindWindowByType(WINDOWTYPE_MAINSCREEN);

			if (pParentWindow)
			{
				InvalidateRect(GetDlgItem(pParentWindow->hWnd, IDC_STATIC_OVERRIDEEXES), NULL, false);
			}

			return false;
		}
		break;
	}





	return false;
}