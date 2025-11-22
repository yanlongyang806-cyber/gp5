#include "continuousBuilder.h"
#include "ContinuousBuilderTiming.h"
#include "ContinuousBuilderTiming_h_ast.h"
#include "earray.h"
#include "timing.h"
#include "estring.h"
#include "file.h"

static const char *spBeginParallelString = "<li style=\"list-style-type:none; border:1px solid gray\"><i>In parallel</i><ul style=\"\">";
static const char *spEndParallelString = "</ul></li>";

static const char *spBeginDetachedString = "<li style=\"list-style-type:none; border:1px solid gray\"><i>Detached</i><ul style=\"\">";
static const char *spEndDetachedString = "</ul></li>";


CBTiming *pCurTiming = NULL;

CBTiming *pLoadedTiming = NULL;

void CBTiming_Begin(U32 iBuildStartTime)
{
	if (pCurTiming)
	{
		CBTiming_End(false);
	}

	pCurTiming = StructCreate(parse_CBTiming);
	pCurTiming->iBuildStartTime = iBuildStartTime;
}

void CBTimingStep_EndAllStepsAtOrBelowLevel(CBTimingStep *pStep, int iLevel, U64 iCurTime, bool bFail)
{
	CBTimingStep *pTailChild;

	if (!pStep)
	{
		return;
	}

	pTailChild = eaTail(&pStep->ppChildSteps);
	if (pTailChild && !pTailChild->bParentOfSubTree)
	{
		CBTimingStep_EndAllStepsAtOrBelowLevel(pTailChild, iLevel, iCurTime, bFail);
	}

	if (pStep->iLevel >= iLevel)
	{
		U64 iDuration;
		pStep->iEndTime = iCurTime;
		iDuration = pStep->iEndTime - pStep->iBeginTime;
		if (iDuration < 1000)
		{
			estrPrintf(&pStep->pFullTime, "< 1 second");
		}
		else
		{
			timeSecondsDurationToPrettyEString(iDuration / 1000, &pStep->pFullTime);
		}
	}

	pStep->bFailed = bFail;
}





void CBTiming_EndAllStepsAtOrBelowLevel(CBTiming *pTiming, int iLevel, U64 iCurTime, bool bFail)
{
	CBTimingStep *pTailStep = eaTail(&pTiming->ppSteps);

	if (pTailStep && !pTailStep->bParentOfSubTree)
	{
		CBTimingStep_EndAllStepsAtOrBelowLevel(pTailStep, iLevel, iCurTime, bFail);
	}
}

void CBTiming_Fail(void)
{
	U64 iCurTime = timeMsecsSince2000();

	if (!pCurTiming)
	{
		return;
	}

	CBTiming_EndAllStepsAtOrBelowLevel(pCurTiming, 0, iCurTime, true);

	pCurTiming->iBuildEndTime = iCurTime / 1000;
	timeSecondsDurationToPrettyEString(pCurTiming->iBuildEndTime - pCurTiming->iBuildStartTime, &pCurTiming->pFullTime);

	pCurTiming->bFailed = true;
}

void CBTiming_End(bool bFail)
{
	U64 iCurTime = timeMsecsSince2000();
	char fileName[CRYPTIC_MAX_PATH];

	if (!pCurTiming)
	{
		return;
	}

	if (!pCurTiming->iBuildEndTime)
	{
		CBTiming_EndAllStepsAtOrBelowLevel(pCurTiming, 0, iCurTime, bFail);

		pCurTiming->iBuildEndTime = iCurTime / 1000;
		timeSecondsDurationToPrettyEString(pCurTiming->iBuildEndTime - pCurTiming->iBuildStartTime, &pCurTiming->pFullTime);
		pCurTiming->bFailed = bFail;
	}

	sprintf(fileName, "%s/Timing.cbt", GetCBLogDirectoryNameFromTime(pCurTiming->iBuildStartTime));

	ParserWriteTextFile(fileName, parse_CBTiming, pCurTiming, 0, 0);

	StructDestroy(parse_CBTiming, pCurTiming);
	pCurTiming = NULL;
}

CBTimingStep *FindLastNonCommentChildStep(CBTimingStep **ppSteps)
{
	int i;

	for (i = eaSize(&ppSteps) - 1; i >= 0; i--)
	{
		if (ppSteps[i]->bComment == false)
		{
			return ppSteps[i];
		}
	}

	return NULL;
}

CBTimingStep *CBTiming_FindOrCreateActiveLevelNStep(CBTiming *pTiming, int iLevel, U64 iCurTime)
{
	CBTimingStep *pStep;
	CBTimingStep *pNextStep;

	pStep = FindLastNonCommentChildStep(pTiming->ppSteps);

	if (!pStep)
	{
		CBTimingStep *pAnonStep = StructCreate(parse_CBTimingStep);
		pAnonStep->iLevel = 0;
		pAnonStep->iBeginTime = iCurTime;
		pAnonStep->pName = strdup("(unnamed)");

		eaPush(&pTiming->ppSteps, pAnonStep);
		pStep = pAnonStep;
	}


	while (pStep->iLevel < iLevel && (pNextStep = FindLastNonCommentChildStep(pStep->ppChildSteps)))
	{
		pStep = pNextStep;
	}

	//now we have the "Currently active" step
	assert(pStep->iEndTime == 0);

	//if it's the correct level, return it, otherwise need to add anon steps in between
	if (pStep->iLevel == iLevel)
	{
		return pStep;
	}

	while (pStep->iLevel < iLevel)
	{
		CBTimingStep *pAnonStep = StructCreate(parse_CBTimingStep);
		pAnonStep->iLevel = pStep->iLevel + 1;
		pAnonStep->iBeginTime = iCurTime;
		pAnonStep->pName = strdup("(unnamed)");

		eaPush(&pStep->ppChildSteps, pAnonStep);
		pStep = pAnonStep;
	}

	return pStep;
}


CBTimingStep *CBTimingStep_FindOrCreateActiveLevelNStep(CBTimingStep *pMainStep, int iLevel, U64 iCurTime)
{
	CBTimingStep *pStep;
	CBTimingStep *pNextStep;

	if (pMainStep->iLevel == iLevel)
	{
		return pMainStep;
	}

	pStep = FindLastNonCommentChildStep(pMainStep->ppChildSteps);

	if (!pStep)
	{
		pStep = pMainStep;
	}


	while (pStep->iLevel < iLevel && (pNextStep = FindLastNonCommentChildStep(pStep->ppChildSteps)))
	{
		pStep = pNextStep;
	}

	//now we have the "Currently active" step
	assert(pStep->iEndTime == 0);

	//if it's the correct level, return it, otherwise need to add anon steps in between
	if (pStep->iLevel == iLevel)
	{
		return pStep;
	}

	while (pStep->iLevel < iLevel)
	{
		CBTimingStep *pAnonStep = StructCreate(parse_CBTimingStep);
		pAnonStep->iLevel = pStep->iLevel + 1;
		pAnonStep->iBeginTime = iCurTime;
		pAnonStep->pName = strdup("(unnamed)");

		eaPush(&pStep->ppChildSteps, pAnonStep);
		pStep = pAnonStep;
	}

	return pStep;
}



CBTimingStep *CBTiming_FindNamedSubTree(CBTimingStep **ppSteps, char *pName)
{
	CBTimingStep *pRetVal;
	FOR_EACH_IN_EARRAY(ppSteps, CBTimingStep, pStep)
	{
		if (stricmp(pStep->pName, pName) == 0 && pStep->bParentOfSubTree)
		{
			return pStep;
		}

		pRetVal = CBTiming_FindNamedSubTree(pStep->ppChildSteps, pName);
		if (pRetVal)
		{
			return pRetVal;
		}
	}
	FOR_EACH_END;
	return NULL;
}

void CBTiming_EndSubTree(char *pName, bool bFail)
{
	CBTimingStep *pStep = CBTiming_FindNamedSubTree(pCurTiming->ppSteps, pName);
	if (pName)
	{
		CBTimingStep_EndAllStepsAtOrBelowLevel(pStep, pStep->iLevel, timeMsecsSince2000(), bFail);
	}
}

bool CBTiming_Active(void)
{
	return !!pCurTiming;
}

void CBTiming_StepEx(char *pName, char *pSubTreeName, int iLevel, bool bParentOfNamedSubtree, bool bDetachedTree)
{
	U64 iCurTime = timeMsecsSince2000();
	CBTimingStep *pNewStep;
	CBTimingStep *pParent;
	CBTimingStep *pParentOfSubTree = NULL;

	assert(pCurTiming);

	if (pCurTiming->iBuildEndTime)
	{
		return;
	}

	
	pNewStep = StructCreate(parse_CBTimingStep);

	pNewStep->iLevel = iLevel;
	pNewStep->pName = strdup(pName);
	pNewStep->iBeginTime = iCurTime;
	pNewStep->bParentOfSubTree = bParentOfNamedSubtree;
	pNewStep->bDetachedTree = bDetachedTree;

	assert(pCurTiming);

	if (pSubTreeName)
	{
		pParentOfSubTree = CBTiming_FindNamedSubTree(pCurTiming->ppSteps, pSubTreeName);
	}

	if (pParentOfSubTree == NULL)
	{

		CBTiming_EndAllStepsAtOrBelowLevel(pCurTiming, iLevel, iCurTime, false);
		if (iLevel == 0)
		{
			eaPush(&pCurTiming->ppSteps, pNewStep);
			return;
		}

		pParent = CBTiming_FindOrCreateActiveLevelNStep(pCurTiming, iLevel - 1, iCurTime);

		eaPush(&pParent->ppChildSteps, pNewStep);
	}
	else
	{
		CBTimingStep_EndAllStepsAtOrBelowLevel(pParentOfSubTree, iLevel, iCurTime, false);
		pParent = CBTimingStep_FindOrCreateActiveLevelNStep(pParentOfSubTree, iLevel - 1, iCurTime);

		eaPush(&pParent->ppChildSteps, pNewStep);
	}
}

void CBTiming_CommentEx(char *pString, char *pSubTreeName)
{
	CBTimingStep *pParent;
	CBTimingStep *pNextParent;
	U64 iCurTime;
	CBTimingStep *pNewStep;
	CBTimingStep *pParentOfSubTree = NULL;

	assert(pCurTiming);

	if (pCurTiming->iBuildEndTime)
	{
		return;
	}


	iCurTime = timeMsecsSince2000();
	pNewStep = StructCreate(parse_CBTimingStep);

	assert(pCurTiming);

	pNewStep->pName = strdup(pString);
	pNewStep->iBeginTime = iCurTime;
	pNewStep->bComment = true;


	if (pSubTreeName)
	{
		pParentOfSubTree = CBTiming_FindNamedSubTree(pCurTiming->ppSteps, pSubTreeName);
	}

	if (pParentOfSubTree == NULL)
	{	

		pParent = FindLastNonCommentChildStep(pCurTiming->ppSteps);

		if (!pParent)
		{
			pNewStep->iLevel = 0;
			eaPush(&pCurTiming->ppSteps, pNewStep);
			return;
		}

		while ((pNextParent = FindLastNonCommentChildStep(pParent->ppChildSteps)))
		{
			pParent = pNextParent;
		}

		pNewStep->iLevel = pParent->iLevel + 1;

		eaPush(&pParent->ppChildSteps, pNewStep);
	}
	else
	{
		pParent = FindLastNonCommentChildStep(pParentOfSubTree->ppChildSteps);

		if (!pParent)
		{
			pNewStep->iLevel = pParentOfSubTree->iLevel + 1;
			eaPush(&pParentOfSubTree->ppChildSteps, pNewStep);
			return;
		}

		while ((pNextParent = FindLastNonCommentChildStep(pParent->ppChildSteps)))
		{
			pParent = pNextParent;
		}

		pNewStep->iLevel = pParent->iLevel + 1;

		eaPush(&pParent->ppChildSteps, pNewStep);
	}
}

typedef struct secondsGroup
{
	U32 iMax;
	char *pBg;
	char *pFg;
	char *pLabel;
} secondsGroup;

secondsGroup groups[] = 
{
	{ 1, "EEEEEE", "000000", "<1 sec" },
	{ 5, "C0FF00", "000000", "1-5 secs"},
	{ 30,"E8FF00", "000000", "5-30 secs"}, 
	{ 120,"FFEE00", "000000", "30 secs - 2 min"}, 
	{ 300, "FFDD00", "000000", "2 - 5 min" },
	{ 1200, "FFBB88", "000000", "5 - 20 min" },
	{ 3600, "FF6633", "000000", "20 min - 1 hr" },
	{ 0, "FF00AA", "FFFFFF", "> 1 hr" },
};

char *GetColorLabelString(void)
{
	static char *pRetVal = NULL;
	static char *pTimeString = NULL;
	int i;

	estrCopy2(&pRetVal, "<div>");
	

	for (i=0; i < ARRAY_SIZE(groups); i++)
	{
		estrConcatf(&pRetVal, "<span style=\"background-color: #%s; color: #%s\">%s</span>", groups[i].pBg, groups[i].pFg, groups[i].pLabel);		
	}

	estrConcatf(&pRetVal, "</div>");


	return pRetVal;
}


		
		



char *GetColoredDurationString(U32 iSeconds)
{
	
	static char *pPrefix = NULL;
	static char *pRetVal = NULL;
	int i;

	
	for (i = 0; i < ARRAY_SIZE(groups); i++)
	{
		if (iSeconds <= groups[i].iMax || i == (int)(ARRAY_SIZE(groups) - 1))
		{
			estrPrintf(&pPrefix, "<span style=\"background-color: #%s; color: #%s\">", groups[i].pBg, groups[i].pFg);
			break;
		}
	}

	estrPrintf(&pRetVal, "%s%s</span>", pPrefix, GetPrettyDurationString(iSeconds));

	return pRetVal;
}
	


void PutTimingStepIntoHtmlString(char **ppOutString, CBTimingStep *pStep, U32 iBuildStartTime, char *pStepIndexString,
				bool bIsCurRun, U64 iParentStartTime, CBTimingStep *pPrevStep)
{

	estrConcatf(ppOutString, "<li>");

	if (pStep->bComment)
	{
		estrConcatf(ppOutString, "COMMENT (%s into step", GetPrettyDurationString((pStep->iBeginTime - iParentStartTime) / 1000));

		if (pPrevStep && pPrevStep->bComment)
		{
			estrConcatf(ppOutString, ", %s after prev comment)", GetPrettyDurationString((pStep->iBeginTime - pPrevStep->iBeginTime) / 1000));
		}
		else
		{
			estrConcatf(ppOutString, ")");
		}

		estrConcatf(ppOutString, " %s", pStep->pName);
	}
	else
	{
		estrConcatf(ppOutString, "%s ", pStep->pName);

		if (pStep->iEndTime)
		{
			U64 iCompletionTime = pStep->iEndTime - pStep->iBeginTime;

			if (pStep->bFailed)
			{
				estrConcatf(ppOutString, "(FAILED in ");
			}
			else
			{
				estrConcatf(ppOutString, "( ");
			}

			if (iCompletionTime < 1000)
			{
				estrConcatf(ppOutString, "<span style=\"background-color: #%s; color: #%s\">&lt; 1 second</span>)",
					groups[0].pBg, groups[0].pFg);
			}
			else
			{
				estrConcatf(ppOutString, "%s)", GetColoredDurationString(iCompletionTime / 1000));
			}
		}
		else
		{
			if (bIsCurRun)
			{
				estrConcatf(ppOutString, "(Ongoing... run time %s)",
					GetColoredDurationString((timeMsecsSince2000() - pStep->iBeginTime) / 1000));
			}
			else
			{
				if (pStep->bFailed)
				{
					estrConcatf(ppOutString, "(FAILED)");
				}
				else
				{
					estrConcatf(ppOutString, "(never completed)");
				}
			}
		}

		if (eaSize(&pStep->ppChildSteps))
		{
			estrConcatf(ppOutString, "<a href=\"http://%s/CBTiming?buildStartSS2000=%u&StepIndex=%s\">%d substeps</a>",
				getHostName(), iBuildStartTime, pStepIndexString, eaSize(&pStep->ppChildSteps));
		}
	}

	estrConcatf(ppOutString, "</li>\n");
}

typedef enum eParallelType
{
	NOT_PARALLEL,
	PARALLEL,
	DETACHED
} eParallelType;

void DoParallelStuff(CBTimingStep **ppSteps, int iIndex, char **ppOutString)
{
	CBTimingStep *pLastStep, *pNextStep;
	eParallelType eLastType = NOT_PARALLEL;
	eParallelType eNextType = NOT_PARALLEL;

	if (eaSize(&ppSteps) == 0)
	{
		return;
	}

	if (iIndex == 0)
	{
		pLastStep = NULL;
		pNextStep = ppSteps[0];
	}
	else if (iIndex == eaSize(&ppSteps))
	{
		pLastStep = ppSteps[iIndex - 1];
		pNextStep = NULL;
	}
	else
	{
		pLastStep = ppSteps[iIndex - 1];
		pNextStep = ppSteps[iIndex];
	}

	if (pLastStep)
	{
		if (pLastStep->bDetachedTree)
		{
			eLastType = DETACHED;
		}
		else if (pLastStep->bParentOfSubTree)
		{
			eLastType = PARALLEL;
		}
	}

	if (pNextStep)
	{
		if (pNextStep->bDetachedTree)
		{
			eNextType = DETACHED;
		}
		else if (pNextStep->bParentOfSubTree)
		{
			eNextType = PARALLEL;
		}
	}

	if (eLastType == DETACHED)
	{
		estrConcatf(ppOutString, "%s", spEndDetachedString);
	}

	if (eLastType == PARALLEL && eNextType != PARALLEL)
	{
		estrConcatf(ppOutString, "%s", spEndParallelString);
	}

	if (eNextType == DETACHED)
	{
		estrConcatf(ppOutString, "%s", spBeginDetachedString);
	}

	if (eNextType == PARALLEL && eLastType != PARALLEL)
	{
		estrConcatf(ppOutString, "%s", spBeginParallelString);
	}

}


//stepIndexstring is something like "5,3,2,11" where each one is an index into the earray of steps
void CBTiming_ToHTMLString(char **ppOutString, U32 iBuildStartTime, char *pStepIndexString)
{
	static char **ppStepIndices = NULL;
	static U32 *piStepIndices = NULL;
	int i;
	CBTiming *pTimingToUse = NULL;
		bool bInsideParallel = false;

	static char *pStepIndexStringPlusOne = NULL;
	estrPrintf(ppOutString, "%s\n", GetColorLabelString());


	eaDestroy(&ppStepIndices);
	ea32Destroy(&piStepIndices);

	if (pStepIndexString)
	{
		DivideString(pStepIndexString, ",", &ppStepIndices, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

		for (i=0; i < eaSize(&ppStepIndices); i++)
		{
			ea32Push(&piStepIndices, atoi(ppStepIndices[i]));
		}
	}

	if (pCurTiming && pCurTiming->iBuildStartTime == iBuildStartTime)
	{
		pTimingToUse = pCurTiming;
	}
	else if (pLoadedTiming && pLoadedTiming->iBuildStartTime == iBuildStartTime)
	{
		pTimingToUse = pLoadedTiming;
	}
	else
	{
		char fileName[CRYPTIC_MAX_PATH];

		if (pLoadedTiming)
		{
			StructDestroy(parse_CBTiming, pLoadedTiming);
			pLoadedTiming = NULL;
		}
		
		sprintf(fileName, "%s/Timing.cbt", GetCBLogDirectoryNameFromTime(iBuildStartTime));

		if (!fileExists(fileName))
		{
			//oh, daylight savings time, how I despise thee
			sprintf(fileName, "%s/Timing.cbt", GetCBLogDirectoryNameFromTime(iBuildStartTime + 3600));
			if (!fileExists(fileName))
			{
				sprintf(fileName, "%s/Timing.cbt", GetCBLogDirectoryNameFromTime(iBuildStartTime - 3600));
				if (!fileExists(fileName))
				{
					estrPrintf(ppOutString, "ERROR: Couldn't find build timing\n");
					return;
				}
			}
		}

		pLoadedTiming = StructCreate(parse_CBTiming);
		ParserReadTextFile(fileName, parse_CBTiming, pLoadedTiming, 0);
		pTimingToUse = pLoadedTiming;

	}

	if (ea32Size(&piStepIndices) == 0)
	{
		estrConcatf(ppOutString, "CB run began %s:\n<ul>\n", timeGetLocalDateStringFromSecondsSince2000(iBuildStartTime));



		for (i=0; i < eaSize(&pTimingToUse->ppSteps); i++)
		{
			estrPrintf(&pStepIndexStringPlusOne, "%s,%d", pStepIndexString ? pStepIndexString : "", i);

	
			DoParallelStuff(pTimingToUse->ppSteps, i, ppOutString);

			PutTimingStepIntoHtmlString(ppOutString, pTimingToUse->ppSteps[i], iBuildStartTime,pStepIndexStringPlusOne,
				pTimingToUse == pCurTiming, ((U64)pTimingToUse->iBuildStartTime) * 1000, i > 0 ? pTimingToUse->ppSteps[i-1] : NULL);

		}

		DoParallelStuff(pTimingToUse->ppSteps, i, ppOutString);

		if (pTimingToUse->iBuildEndTime)
		{
			estrConcatf(ppOutString, "</ul>\nCB %s. Full run time: %s\n", pTimingToUse->bFailed ? "FAILED" : "completed", GetPrettyDurationString(pTimingToUse->iBuildEndTime - pTimingToUse->iBuildStartTime));
		}
		else
		{
			if (pTimingToUse == pCurTiming)
			{
				estrConcatf(ppOutString, "</ul>\nCB still running. Run time: %s\n", GetPrettyDurationString(timeSecondsSince2000_ForceRecalc() - pTimingToUse->iBuildStartTime));
			}
			else
			{
				estrConcatf(ppOutString, "</ul>\nCB %s.\n", pTimingToUse->bFailed ? "FAILED" : "never completed");
			}
		}
	}
	else
	{
		CBTimingStep **ppSteps = pTimingToUse->ppSteps;
		CBTimingStep *pStep = NULL;

		for (i=0; i < ea32Size(&piStepIndices); i++)
		{
			if ((U32)eaSize(&ppSteps) <= piStepIndices[i])
			{
				estrPrintf(ppOutString, "ERROR: Unknown timing step\n");
				return;
			}

			pStep = ppSteps[piStepIndices[i]];
			ppSteps = pStep->ppChildSteps;
		}

		assert(pStep);

		estrConcatf(ppOutString, "Step %s began at %s\n<ul>\n", 
			pStep->pName, timeGetLocalDateStringFromSecondsSince2000(pStep->iBeginTime / 1000));

		if (eaSize(&ppSteps))
		{
			U32 iDelayBeforeFirstSubstep = (ppSteps[0]->iBeginTime - pStep->iBeginTime) / 1000;
			if (iDelayBeforeFirstSubstep > 0)
			{
				estrConcatf(ppOutString, "<ul>%s unaccounted for before first substep</ul>\n",
					GetColoredDurationString(iDelayBeforeFirstSubstep));
			}
		}


		for (i=0; i < eaSize(&ppSteps); i++)
		{

			estrPrintf(&pStepIndexStringPlusOne, "%s,%d", pStepIndexString ? pStepIndexString : "", i);

			
			DoParallelStuff(ppSteps, i, ppOutString);

			PutTimingStepIntoHtmlString(ppOutString, ppSteps[i],  iBuildStartTime, pStepIndexStringPlusOne,
				pTimingToUse == pCurTiming, pStep->iBeginTime, i > 0 ? ppSteps[i-1] : NULL);
		}

		DoParallelStuff(ppSteps, i, ppOutString);


		if (pStep->iEndTime)
		{
			estrConcatf(ppOutString, "</ul>\nStep %s in %s\n", 
				pStep->bFailed ? "FAILED" : "complete",
				GetPrettyDurationString((pStep->iEndTime - pStep->iBeginTime) / 1000));
		}
		else
		{
			if (pTimingToUse == pCurTiming)
			{
				estrConcatf(ppOutString, "</ul>\nStep still ongoing. Current run time: %s",
					GetPrettyDurationString((timeMsecsSince2000() - pStep->iBeginTime) / 1000));
			}
			else
			{
				estrConcatf(ppOutString, "</ul>\nStep %s\n", pStep->bFailed ? "FAILED" : "never completed");
			}
		}

	}
}


AUTO_COMMAND;
char *GetTimingLink(void)
{
	static char *pRetVal = NULL;
	estrPrintf(&pRetVal, "<a href=\"http://%s/CBTiming?buildStartSS2000=%u\">Timing</a>",
			getHostName(),giLastTimeStarted);

	return pRetVal;
}

AUTO_COMMAND;
char *GetTimingHtmlSummary(void)
{
	static char *pRetVal = NULL;

	estrClear(&pRetVal);

	if (pCurTiming)
	{
		CBTiming_ToHTMLString(&pRetVal, pCurTiming->iBuildStartTime, "");
		return pRetVal;
	}

	return "(No timing info available)";
}

#include "ContinuousBuilderTiming_h_ast.c"