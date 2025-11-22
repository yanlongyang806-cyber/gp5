#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "builderPlexer.h"
#include "timing.h"
#include "SimpleWindowManager.h"
#include "Resource.h"
#include "StringUtil.h"
#include "GlobalTypes.h"
#include "BuilderPlexer_h_Ast.h"
#include "BuilderPlexerUI.h"
#include "BuilderPlexerActions.h"

BuilderPlexerGlobalConfig gBPConfig = {0};
BuilderPlexerState gBPState = {0};


char *spCommentToSet = NULL;
AUTO_COMMAND;
void SetComment(ACMD_SENTENCE pComment)
{
	estrCopy2(&spCommentToSet, pComment);
}


//if we're in "comment-to-set" mode, then we are being run automatedly by CB.exe, and are also very low priority,
//so just print out the failure message and continue
void FailWithMessage(char *pFmt, ...)
{
	char *pFullMessage = NULL;
	estrGetVarArgs(&pFullMessage, pFmt);

	if (estrLength(&spCommentToSet))
	{
		estrInsertf(&pFullMessage, 0, "Builderplexer error while setting comment: ");
		Errorf("%s", pFullMessage);
		printf("%s\n", pFullMessage);
		exit(0);
	}

	UI_DisplayMessage("%s", pFullMessage);
	exit(-1);
}

void MakeRenamedDirName(char **ppOutDirName, char *pInDirName, BuilderPlexerChoice *pChoice)
{
	estrPrintf(ppOutDirName, "c:\\__%s_%s", pChoice->pBuilderName, pInDirName);
}
void MakeNonRenamedDirName(char **ppOutDirName, char *pInDirName)
{
	estrPrintf(ppOutDirName, "c:\\%s", pInDirName);
}


void LoadConfig(void)
{
	if (!ParserReadTextFile("n:\\BuilderPlexer\\BPConfig.txt", parse_BuilderPlexerGlobalConfig, &gBPConfig, 0))
	{
		FailWithMessage("Couldn't load global config file from n:\\builderPlexer\\BPConfig.txt");
	}

	if (!eaSize(&gBPConfig.ppProducts) || !eaSize(&gBPConfig.ppRequiredDirNames))
	{
		FailWithMessage("Global config file (n:\\builderPlexer\\BPConfig.txt) seems to be corrupt... need at least one product and at least one required folder");
	}
}

void LoadState(void)
{
	if (fileExists("c:\\BuilderPlexer\\BPState.txt"))
	{
		if (!ParserReadTextFile("c:\\BuilderPlexer\\BPState.txt", parse_BuilderPlexerState, &gBPState, 0))
		{
			FailWithMessage("Couldn't load global config file from n:\\builderPlexer\\BPConfig.txt");
		}
	}
}

void SaveState(void)
{
	ParserWriteTextFile("c:\\BuilderPlexer\\BPState.txt", parse_BuilderPlexerState, &gBPState, 0, 0);
}


BuilderPlexerChoice *FindChoiceByName(char *pName)
{
	return eaIndexedGetUsingString(&gBPState.ppAllChoices, pName);
}

BuilderPlexerChoice *GetCurActiveChoice(void)
{
	if (gBPState.pCurActiveChoiceName && gBPState.pCurActiveChoiceName[0])
	{
		return FindChoiceByName(gBPState.pCurActiveChoiceName);
	}
	
	return NULL;
}

BuilderPlexerProduct *FindProductByName(char *pName)
{
	int iProductNum;

	for (iProductNum = 0; iProductNum < eaSize(&gBPConfig.ppProducts); iProductNum++)
	{
		BuilderPlexerProduct *pProduct = gBPConfig.ppProducts[iProductNum];

		if (stricmp(pProduct->pName, pName) == 0)
		{
			return pProduct;
		}
	}
	
	return NULL;
}

char *GetDescriptionOfBuild(BuilderPlexerChoice *pChoice)
{
	static char *pOutString = NULL;
	static char *pDateString = NULL;
	estrPrintf(&pOutString, "%s (product %s). ", pChoice->pBuilderName, pChoice->pProductName);

	estrConcatf(&pOutString, "Last activated: %s. ", timeGetLocalDateStringFromSecondsSince2000(pChoice->iLastActivationTime));
	if (pChoice->iLastDeactivationTime > pChoice->iLastActivationTime)
	{
		estrConcatf(&pOutString, "Last deactivated: %s. ", timeGetLocalDateStringFromSecondsSince2000(pChoice->iLastDeactivationTime));
	}

	if (pChoice->pLastRunComment && pChoice->pLastRunComment[0])
	{
		estrConcatf(&pOutString, "Last run: (%s).", pChoice->pLastRunComment);
	}
	else
	{
		estrConcatf(&pOutString, "(Never run to completion).");
	}
	
	return pOutString;
}

char *GetCurSummaryString(void)
{
	static char *pOutString = NULL;
	BuilderPlexerChoice *pCurChoice = GetCurActiveChoice();
	int i;

	estrClear(&pOutString);

	if (pCurChoice)
	{
		estrConcatf(&pOutString, "Currently active builder: %s\n\n\n", GetDescriptionOfBuild(pCurChoice));
		
		if (eaSize(&gBPState.ppAllChoices) == 1)
		{
			estrConcatf(&pOutString, "No other builders\n");
		}
		else
		{
			estrConcatf(&pOutString, "Other builders waiting to activate:\n");
			for (i = 0; i < eaSize(&gBPState.ppAllChoices); i++)
			{
				BuilderPlexerChoice *pOtherChoice = gBPState.ppAllChoices[i];
				if (pOtherChoice != pCurChoice)
				{
					estrConcatf(&pOutString, "%s\n\n", GetDescriptionOfBuild(pOtherChoice));
				}
			}
		}
	}
	else
	{
		if (eaSize(&gBPState.ppAllChoices) == 0)
		{
			estrConcatf(&pOutString, "No builders have ever been configured for BuilderPlexer. If you wish one, just create c:\\src, c:\\continuousbuilder, c:\\core and c:\\productname as normal");
		}
		else
		{
			estrConcatf(&pOutString, "There is no currently active builder. The following builders can be activated:\n\n");
			for (i = 0; i < eaSize(&gBPState.ppAllChoices); i++)
			{
				BuilderPlexerChoice *pOtherChoice = gBPState.ppAllChoices[i];	
				estrConcatf(&pOutString, "%s\n\n", GetDescriptionOfBuild(pOtherChoice));
			}
		}
	}

	return pOutString;
}
		
int ReplaceDirNameMacros(char **ppOutString, BuilderPlexerProduct *pProduct)
{
	int iRetVal = 0;

	iRetVal += estrReplaceOccurrences(ppOutString, "$PRODUCTNAME$", pProduct->pName);
	iRetVal += estrReplaceOccurrences(ppOutString, "$SHORTPRODUCTNAME$", pProduct->pShortName);

	return iRetVal;

}
		

#define COUNT_ONLY_OPTIONAL (1 << 0)
#define COUNT_ONLY_PRODUCT_SPECIFIC (1 << 1)
#define COUNT_RENAMED (1 << 2)

int CountFolders(BuilderPlexerProduct *pProduct, char **ppOutFoundString, char **ppOutDidntFindString, U32 iFlags, BuilderPlexerChoice *pChoice /*only set if iFlags & COUNT_RENAMED*/)
{
	int i;
	static char *pDirNameToUse = NULL;
	int iMacroCount;
	int iFoundCount = 0;

	char ***pppDirNames;

	estrClear(ppOutFoundString);
	estrClear(ppOutDidntFindString);

	if (iFlags & COUNT_ONLY_OPTIONAL)
	{
		pppDirNames = &gBPConfig.ppOptionalDirNames;
	}
	else
	{
		pppDirNames = &gBPConfig.ppRequiredDirNames;
	}

	for (i=0; i < eaSize(pppDirNames); i++)
	{
		iMacroCount = 0;
		if (iFlags & COUNT_RENAMED)
		{
			MakeRenamedDirName(&pDirNameToUse, (*pppDirNames)[i], pChoice);
		}
		else
		{
			MakeNonRenamedDirName(&pDirNameToUse, (*pppDirNames)[i]);
		}

		iMacroCount += ReplaceDirNameMacros(&pDirNameToUse, pProduct);

		if (iMacroCount == 0 && (iFlags & COUNT_ONLY_PRODUCT_SPECIFIC))
		{
			continue;
		}

		if (dirExists(pDirNameToUse))
		{
			iFoundCount++;
			estrConcatf(ppOutFoundString, "%s%s", estrLength(ppOutFoundString) == 0 ? "" : ", ", pDirNameToUse);
		}
		else
		{
			estrConcatf(ppOutDidntFindString, "%s%s", estrLength(ppOutDidntFindString) == 0 ? "" : ", ", pDirNameToUse);
		}
	}

	return iFoundCount;
}

bool VerifyFoldersForActiveState(BuilderPlexerChoice *pChoice, char **ppErrorString)
{
	static char *pExistString = NULL;
	static char *pDontExistString = NULL;
	int iProductNum;
	BuilderPlexerProduct *pMainProduct = FindProductByName(pChoice->pProductName);
	if (!pMainProduct)
	{
		estrPrintf(ppErrorString, "Builder choice %s wants product %s, which doesn't seem to exist",
			pChoice->pBuilderName, pChoice->pProductName);
		return false;
	}

	for (iProductNum = 0; iProductNum < eaSize(&gBPConfig.ppProducts); iProductNum++)
	{
		BuilderPlexerProduct *pProduct = gBPConfig.ppProducts[iProductNum];

		if (pProduct == pMainProduct)
		{
			//for "our" product, only need to verify all required folders exist
			if (CountFolders(pProduct, &pExistString, &pDontExistString, 0, NULL) != eaSize(&gBPConfig.ppRequiredDirNames))
			{
				estrPrintf(ppErrorString, "One or more required folders for product type %s don't exist: %s", pProduct->pName, pDontExistString);
				return false;
			}
		}
		else
		{
			//no product-specific required folders can exist for other products
			if (CountFolders(pProduct, &pExistString, &pDontExistString, COUNT_ONLY_PRODUCT_SPECIFIC, NULL))
			{
				estrPrintf(ppErrorString, "Want to be set up for a build of product %s, but some folders for product %s exist: %s",
					pMainProduct->pName, pProduct->pName, pExistString);
				return false;
			}

			if (CountFolders(pProduct, &pExistString, &pDontExistString, COUNT_ONLY_PRODUCT_SPECIFIC | COUNT_ONLY_OPTIONAL, NULL))
			{
				estrPrintf(ppErrorString, "Want to be set up for a build of product %s, but some folders for product %s exist: %s",
					pMainProduct->pName, pProduct->pName, pExistString);
				return false;
			}
		}
	}

	if (CountFolders(FindProductByName(pChoice->pProductName), &pExistString, &pDontExistString, COUNT_RENAMED, pChoice))
	{
		estrPrintf(ppErrorString, "Want to be set up for build %s but renamed folders exist: %s",
			pChoice->pBuilderName, pExistString);
		return false;
	}

	if (CountFolders(FindProductByName(pChoice->pProductName), &pExistString, &pDontExistString, COUNT_RENAMED | COUNT_ONLY_OPTIONAL, pChoice))
	{
		estrPrintf(ppErrorString, "Want to be set up for build %s but renamed folders exist: %s",
			pChoice->pBuilderName, pExistString);
		return false;
	}


	return true;
}

int CheckForFoldersForChoiceCreation(BuilderPlexerProduct **ppOutProduct, char **ppErrorString)
{
	BuilderPlexerProduct *pFoundProduct = NULL;
	static char *pExistString = NULL;
	static char *pDontExistString = NULL;
	static char *pRequiredFoldersThatExistBadly = NULL; 
	static char *pRequiredFoldersThatDontExistBadly = NULL; 
	int iProductNum;

	estrClear(&pRequiredFoldersThatExistBadly);
	estrClear(&pRequiredFoldersThatDontExistBadly);

	for (iProductNum = 0; iProductNum < eaSize(&gBPConfig.ppProducts); iProductNum++)
	{
		BuilderPlexerProduct *pProduct = gBPConfig.ppProducts[iProductNum];
		int iRequiredCount = CountFolders(pProduct, &pExistString, &pDontExistString, 0, NULL);

		if (iRequiredCount == eaSize(&gBPConfig.ppRequiredDirNames))
		{
			if (pFoundProduct)
			{
				estrPrintf(ppErrorString, "Seem to have all required folders for both products %s and %s. This is unacceptable",
					pFoundProduct->pName, pProduct->pName);
				return -1;
			}

			pFoundProduct = pProduct;
		}
		else if (iRequiredCount)
		{
			if (estrLength(&pExistString) > estrLength(&pRequiredFoldersThatExistBadly))
			{
				estrCopy(&pRequiredFoldersThatExistBadly, &pExistString);
				estrCopy(&pRequiredFoldersThatDontExistBadly, &pDontExistString);
			}
		}

		if (pFoundProduct != pProduct)
		{
			if (CountFolders(pProduct, &pExistString, &pDontExistString, COUNT_ONLY_PRODUCT_SPECIFIC | COUNT_ONLY_OPTIONAL, NULL))
			{
				estrPrintf(ppErrorString, "Checking to see if we have the folders we need for a product, and we have SOME but not ALL folders for a product: %s",
					pExistString);
				return -1;
			}
		}
	}

	if (estrLength(&pRequiredFoldersThatExistBadly) && !pFoundProduct)
	{
		estrPrintf(ppErrorString, "Checking to see if we have the folders we need for a product, and we have SOME but not ALL folders for a product: Have (%s). Don't have (%s)",
			pRequiredFoldersThatExistBadly, pRequiredFoldersThatDontExistBadly);
		return -1;
	}

	if (pFoundProduct)
	{
		*ppOutProduct = pFoundProduct;
		return 1;
	}

	return 0;
}

		
void DoIt(void)
{
	char *pErrorString = NULL;
	BuilderPlexerChoice *pActiveChoice;
	char *pAllNamesString = NULL;
	int i;

	BuilderPlexerAction *pChosenAction;


	if ((pActiveChoice = GetCurActiveChoice()))
	{
		if (!VerifyFoldersForActiveState(pActiveChoice, &pErrorString))
		{
			FailWithMessage("BuilderPlexer thinks that %s should be active, but couldn't find appropriate folders: %s",
				pActiveChoice->pBuilderName, pErrorString);
		}
	}
	else
	{
		BuilderPlexerProduct *pProductForNewChoice;
		BuilderPlexerChoice *pNewChoice;
		char *pNewName = NULL;
		switch (CheckForFoldersForChoiceCreation(&pProductForNewChoice, &pErrorString))
		{
		case -1:
			FailWithMessage("While checking folders to see if a new builder can be added, something was corrupt: %s",
				pErrorString);
		case 0:
			break; //do nothing, no new folders
		case 1:

			for (i=0; i < eaSize(&gBPState.ppAllChoices); i++)
			{
				estrConcatf(&pAllNamesString, "%s%s", i == 0 ? "" : ", ", gBPState.ppAllChoices[i]->pBuilderName);
			}

			UI_GetString(&pNewName, "Found folders indicating a previously unknown builder is being requested for product %s. If so, please enter a name for this build. Otherwise, leave this blank and get rid of the offending folders. (Currently used names: %s)",
				pProductForNewChoice->pName, pAllNamesString);
			estrDestroy(&pAllNamesString);
			estrTrimLeadingAndTrailingWhitespace(&pNewName);
			if (estrLength(&pNewName) == 0)
			{
				FailWithMessage("No new builder requested");
			}


			estrMakeAllAlphaNumAndUnderscores(&pNewName);

			if (FindChoiceByName(pNewName))
			{
				FailWithMessage("Trying to create new builder %s, but that name is already in use.", pNewName);
			}


			pNewChoice = StructCreate(parse_BuilderPlexerChoice);
			pNewChoice->pBuilderName = strdup(pNewName);
			pNewChoice->pProductName = strdup(pProductForNewChoice->pName);
			pNewChoice->iLastActivationTime = timeSecondsSince2000();

			if (!VerifyFoldersForActiveState(pNewChoice, &pErrorString))
			{
				FailWithMessage("Trying to create new builder %s, but couldn't find appropriate folders: %s",
					pNewChoice->pBuilderName, pErrorString);
			}

			eaPush(&gBPState.ppAllChoices, pNewChoice);
			estrCopy2(&gBPState.pCurActiveChoiceName, pNewChoice->pBuilderName);
			SaveState();
		}
	}

	DeviseActions();
	pChosenAction = ChooseAction();
	DoAction(pChosenAction);
}


int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;
	char *pErrorString = NULL;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	StructInit(parse_BuilderPlexerGlobalConfig, &gBPConfig);
	StructInit(parse_BuilderPlexerState, &gBPState);

	LoadConfig();

	LoadState();

	if (estrLength(&spCommentToSet))
	{
		if (GetCurActiveChoice())
		{
			estrCopy(&GetCurActiveChoice()->pLastRunComment, &spCommentToSet);
			SaveState();
		}

		exit(0);
	}

	{
		char *pTemp = NULL;
		UI_GetString(&pTemp, "Did you just restart this PC? Or at least restart it since you did any actual build? If so, type 'yes'. If not, this WILL NOT WORK. Seriously.");
		if (stricmp(pTemp, "yes") != 0)
		{
			FailWithMessage("Restart your PC, sucka!");
		}
		estrDestroy(&pTemp);
	}

	while (1)
	{
		DoIt();
	}

	EXCEPTION_HANDLER_END

}


#include "BuilderPlexer_h_Ast.c"
