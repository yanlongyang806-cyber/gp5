#include "BuilderPlexer.h"
#include "BuilderPlexerActions.h"
#include "estring.h"
#include "Earray.h"
#include "BuilderPlexerUI.h"
#include "file.h"
#include "timing.h"

#include "autogen/BuilderPlexerActions_h_ast.h"
#include "autogen/BuilderPlexerActions_c_ast.h"

AUTO_STRUCT;
typedef struct SingleFolderRename
{
	char *pOldName; AST(ESTRING)
	char *pNewName; AST(ESTRING)
} SingleFolderRename;

AUTO_STRUCT;
typedef struct FolderRenames
{
	SingleFolderRename **ppRenames;
} FolderRenames;


BuilderPlexerAction **sppActions = NULL;


void DeviseActions(void)
{
	BuilderPlexerAction *pDoNothing = StructCreate(parse_BuilderPlexerAction);
	BuilderPlexerChoice *pCurActive = GetCurActiveChoice();
	int i;

	eaDestroyStruct(&sppActions, parse_BuilderPlexerAction);

	pDoNothing->eType = BPACTION_DONOTHING;
	estrPrintf(&pDoNothing->pName, "Do Nothing");
	estrPrintf(&pDoNothing->pDescription, "Leave things in their current state (see above)");
	eaPush(&sppActions, pDoNothing);

	if (pCurActive)
	{
		BuilderPlexerAction *pDisableActive = StructCreate(parse_BuilderPlexerAction);
		pDisableActive->eType = BPACTION_DISABLEACTIVE;
		estrPrintf(&pDisableActive->pName, "Disable %s", pCurActive->pBuilderName);
		estrPrintf(&pDisableActive->pDescription, "Disable the current builder (%s) and activate nothing. This will allow you to set up a new builder",
			pCurActive->pBuilderName);
		eaPush(&sppActions, pDisableActive);
	}

	for (i=0; i < eaSize(&gBPState.ppAllChoices); i++)
	{
		BuilderPlexerChoice *pChoice = gBPState.ppAllChoices[i];
		if (pChoice != pCurActive)
		{
			BuilderPlexerAction *pActivate = StructCreate(parse_BuilderPlexerAction);
			pActivate->eType = BPACTION_ACTIVATE;
			pActivate->pChoice = pChoice;
			estrPrintf(&pActivate->pName, "Enable %s", pChoice->pBuilderName);
			if (pCurActive)
			{
				estrPrintf(&pActivate->pDescription, "Disable %s and enable [%s]", pCurActive->pBuilderName, GetDescriptionOfBuild(pChoice));
			}
			else
			{
				estrPrintf(&pActivate->pDescription, "Enable [%s]", GetDescriptionOfBuild(pChoice));
			}
			eaPush(&sppActions, pActivate);
		}
	}
}


BuilderPlexerAction *ChooseAction(void)
{
	char **ppNames = NULL;
	char **ppDescriptions = NULL;
	int iChoiceNum = 0;

	int i;
	char *pMainString = NULL;

	for (i=0; i < eaSize(&sppActions); i++)
	{
		eaPush(&ppNames, sppActions[i]->pName);
		eaPush(&ppDescriptions, sppActions[i]->pDescription);
	}

	estrPrintf(&pMainString, "What would you like BuilderPlexer to do? Current state is:\n\n %s", GetCurSummaryString());

	iChoiceNum = UI_Picker(pMainString, &ppNames, &ppDescriptions);

	estrDestroy(&pMainString);
	eaDestroy(&ppNames);
	eaDestroy(&ppDescriptions);

	assert(sppActions);

	return sppActions[iChoiceNum];
}

SingleFolderRename *CreateSingleRename(BuilderPlexerChoice *pChoice, BuilderPlexerProduct *pProduct, char *pBaseName, bool bActivating)
{
	SingleFolderRename *pRename = StructCreate(parse_SingleFolderRename);
	char *pTemp = NULL;

	if (bActivating)
	{
		MakeRenamedDirName(&pRename->pOldName, pBaseName, pChoice);
		MakeNonRenamedDirName(&pRename->pNewName, pBaseName);
	}
	else
	{
		MakeRenamedDirName(&pRename->pNewName, pBaseName, pChoice);
		MakeNonRenamedDirName(&pRename->pOldName, pBaseName);
	}

	ReplaceDirNameMacros(&pRename->pNewName, pProduct);
	ReplaceDirNameMacros(&pRename->pOldName, pProduct);

	//newName should be local path, not full path
	estrGetDirAndFileName(pRename->pNewName, NULL, &pTemp);
	estrCopy(&pRename->pNewName, &pTemp);
	estrDestroy(&pTemp);
	

	return pRename;
}


FolderRenames *FindRenames(BuilderPlexerChoice *pChoice, bool bActivating)
{
	char *pOldName = NULL;
	char *pNewName = NULL;
	FolderRenames *pRenames = StructCreate(parse_FolderRenames);
	int i;
	BuilderPlexerProduct *pProduct = FindProductByName(pChoice->pProductName);
	assert(pProduct);

	for (i=0; i < eaSize(&gBPConfig.ppRequiredDirNames); i++)
	{
		SingleFolderRename *pRename = CreateSingleRename(pChoice, pProduct, gBPConfig.ppRequiredDirNames[i], bActivating);
		if (!dirExists(pRename->pOldName))
		{
			FailWithMessage("While setting up to do renaming for %s (%s), noticed that directory %s does not exist. It must",
				pChoice->pBuilderName, pChoice->pProductName, pRename->pOldName);
		}

		eaPush(&pRenames->ppRenames, pRename);
	}

	for (i=0; i < eaSize(&gBPConfig.ppOptionalDirNames); i++)
	{
		SingleFolderRename *pRename = CreateSingleRename(pChoice, pProduct, gBPConfig.ppOptionalDirNames[i], bActivating);
		if (!dirExists(pRename->pOldName))
		{
			StructDestroy(parse_SingleFolderRename, pRename);
		}
		else
		{
			eaPush(&pRenames->ppRenames, pRename);
		}
	}

	return pRenames;
}

void ConfirmRenames(FolderRenames *pRenames, char *pFmt, ...)
{
	char *pFullString = NULL;
	int i;

	estrGetVarArgs(&pFullString, pFmt);

	for (i=0; i < eaSize(&pRenames->ppRenames); i++)
	{
		estrConcatf(&pFullString, "%s -> %s\n", pRenames->ppRenames[i]->pOldName, pRenames->ppRenames[i]->pNewName);
	}

	UI_DisplayMessage(pFullString);

	estrDestroy(&pFullString);

}

void DoRenames(FolderRenames *pRenames)
{
	int i;
	char systemString[1024];
	for (i=0;i < eaSize(&pRenames->ppRenames); i++)
	{
		
		sprintf(systemString, "rename %s %s", pRenames->ppRenames[i]->pOldName, pRenames->ppRenames[i]->pNewName);
		if (system(systemString))
		{
			FailWithMessage("Something went wrong while trying to do \"%s\". Look in the console to see what",
				systemString);
		}
	}

}

void DisableActive(void)
{
	FolderRenames *pRenames = FindRenames(GetCurActiveChoice(), false);

	ConfirmRenames(pRenames, "In order to deactivate %s, about to rename the following folders (close instead of clicking OK if this not what you want to do):\n", GetCurActiveChoice()->pBuilderName);
	DoRenames(pRenames);

	StructDestroy(parse_FolderRenames, pRenames);

	GetCurActiveChoice()->iLastDeactivationTime = timeSecondsSince2000_ForceRecalc();

	estrClear(&gBPState.pCurActiveChoiceName);
	SaveState();

	UI_DisplayMessage("Finished all renames for deactivation");

}

void ActivateChoice(BuilderPlexerChoice *pChoice)
{
	FolderRenames *pRenames = FindRenames(pChoice, true);

	ConfirmRenames(pRenames, "In order to activate %s, about to rename the following folders (close instead of clicking OK if this not what you want to do):\n", pChoice->pBuilderName);
	DoRenames(pRenames);

	StructDestroy(parse_FolderRenames, pRenames);

	pChoice->iLastActivationTime = timeSecondsSince2000_ForceRecalc();

	estrCopy2(&gBPState.pCurActiveChoiceName, pChoice->pBuilderName);
	SaveState();

	UI_DisplayMessage("Finished all renames. Builder %s should now be active", pChoice->pBuilderName);

}

void DoAction(BuilderPlexerAction *pAction)
{
	switch (pAction->eType)
	{
	case BPACTION_DONOTHING:
		exit(0);
		break;
	case BPACTION_DISABLEACTIVE:
		DisableActive();
		break;
	case BPACTION_ACTIVATE:
		if (GetCurActiveChoice())
		{
			DisableActive();
		}
		ActivateChoice(pAction->pChoice);
		break;
	}
}























#include "BuilderPlexerActions_h_ast.c"
#include "BuilderPlexerActions_c_ast.c"
