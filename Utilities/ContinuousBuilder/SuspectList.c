#include "suspectList.h"
#include "earray.h"
#include "continuousBuilder.h"
#include "svnutils.h"
#include "StringCache.h"
#include "error.h"
#include "simpleparser.h"
#include "gimmeutils.h"
#include "CBConfig.h"
#include "ETCommon/ETCommonStructs.h"

#include "autogen/SuspectList_h_ast.h"
#include "autogen/SVNUtils_h_ast.h"
#include "logging.h"
#include "timing.h"
#include "estring.h"
#include "StructDefines.h"


char *GetSuspectListLogFileName(void)
{
	static char retVal[CRYPTIC_MAX_PATH] = "";

	if (retVal[0])
	{
		return retVal;
	}

	sprintf(retVal, "%s\\SuspectListLog.txt", GetCBLogDirectoryName(NULL));

	return retVal;
}

void GetErrorfBlamees(char *pBuf, const char ***pppList)
{
	char *pReadHead = pBuf;

	while (1)
	{
		char tempBuf[256];
		char *pNext = strstr(pReadHead, g_lastAuthorIntro);
		char *pNewLine;
		char *pTempBuf = tempBuf;
		
		if (!pNext)
		{
			return;
		}

		pReadHead = pNext + strlen(g_lastAuthorIntro);
		memcpy(tempBuf, pReadHead, 255);
		tempBuf[255] = 0;

		pNewLine = strchr(tempBuf, '\n');

		if (pNewLine)
		{
			*pNewLine = 0;
		}


		pTempBuf = (char*)removeLeadingWhiteSpaces(pTempBuf);
		removeTrailingWhiteSpaces(pTempBuf);
		if ( Gimme_IsReasonableGimmeName(pTempBuf))
		{
			eaPushUnique(pppList, allocAddCaseSensitiveString(pTempBuf));
		}
	}
}

void GetSuspects(SuspectList *pList, CBRevision *pOldRev, CBRevision *pNewRev, ErrorTrackerEntryList *pErrorList)
{
	CheckinInfo **ppSVNCheckins = NULL;
	CheckinInfo **ppGimmeCheckins = NULL;
	int i;

	SVN_GetCheckins(pOldRev->iSVNRev, pNewRev->iSVNRev, NULL, gConfig.ppSVNFolders, NULL, &ppSVNCheckins, 600, 0);
	Gimme_GetCheckinsBetweenTimes(pOldRev->iGimmeTime, pNewRev->iGimmeTime, NULL, gConfig.ppGimmeFolders, GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS, &ppGimmeCheckins, 600);

	for (i=0; i < eaSize(&ppSVNCheckins); i++)
	{
		eaPushUnique(&pList->lists[SUSPECTTYPE_CODECHECKIN].ppSuspects, allocAddCaseSensitiveString(ppSVNCheckins[i]->userName));
	}

	for (i=0; i < eaSize(&ppGimmeCheckins); i++)
	{
		eaPushUnique(&pList->lists[SUSPECTTYPE_DATACHECKIN].ppSuspects, allocAddCaseSensitiveString(ppGimmeCheckins[i]->userName));
	}

	for (i=0; i < eaSize(&pErrorList->ppEntries); i++)
	{
		//if there's a space in the blamed username, it must be a special string like "you do not have the latest version"
		//or what have you
		if (pErrorList->ppEntries[i]->pLastBlamedPerson && !strchr(pErrorList->ppEntries[i]->pLastBlamedPerson, ' '))
		{
			eaPushUnique(&pList->lists[SUSPECTTYPE_ERRORBLAMEE].ppSuspects, allocAddCaseSensitiveString(pErrorList->ppEntries[i]->pLastBlamedPerson));
		}
	}


	eaDestroyStruct(&ppSVNCheckins, parse_CheckinInfo);
	eaDestroyStruct(&ppGimmeCheckins, parse_CheckinInfo);
}



bool ListHasMemberNotInOtherTwoLists(const char ***pppList, const char ***pppOther1, const char ***pppOther2)
{
	int iSize = eaSize(pppList);
	int i;

	for (i=0; i < iSize; i++)
	{
		if (eaFind(pppOther1, (*pppList)[i]) == -1 && eaFind(pppOther2, (*pppList)[i]) == -1)
		{
			return true;
		}
	}

	return false;
}

void RemoveThingsFromListNotInOtherList(const char ***pppMainList, const char ***pppOtherList)
{
	int i = 0;

	while (i < eaSize(pppMainList))
	{
		if (eaFind(pppOtherList, (*pppMainList)[i]) == -1)
		{
			eaRemoveFast(pppMainList, i);
		}
		else
		{
			i++;
		}
	}
}

bool IsASuspect(SuspectList *pList, const char *pName)
{
	int i;

	for (i=0; i < SUSPECTTYPE_COUNT; i++)
	{
		if (eaFind(&pList->lists[i].ppSuspects, pName) != -1)
		{
			return true;
		}
	}

	return false;
}

void PutSuspectListIntoEString(SuspectList *pList, char **ppEString)
{
	int i;

	estrConcatf(ppEString, "%d code suspects: ", eaSize(&pList->lists[0].ppSuspects));

	for (i=0; i < eaSize(&pList->lists[0].ppSuspects); i++)
	{
		estrConcatf(ppEString, "%s ", pList->lists[0].ppSuspects[i]);
	}
	estrConcatf(ppEString, "%d data suspects: ", eaSize(&pList->lists[1].ppSuspects));

	for (i=0; i < eaSize(&pList->lists[1].ppSuspects); i++)
	{
		estrConcatf(ppEString, "%s ", pList->lists[1].ppSuspects[i]);
	}
	estrConcatf(ppEString, "%d errorblame suspects: ", eaSize(&pList->lists[2].ppSuspects));

	for (i=0; i < eaSize(&pList->lists[2].ppSuspects); i++)
	{
		estrConcatf(ppEString, "%s ", pList->lists[2].ppSuspects[i]);
	}
}

void AddPeopleToSuspectList(SuspectList *pCurSuspects, CBRevision *pOldRev, CBRevision *pNewRev, 
	ErrorTrackerEntryList *pErrorList, const char ***pppNewPeople, const char ***pppCurCheckins)
{
	SuspectList newSuspects = {0};

	bool bBlameesOnly = false;
	int iListNum, iNameNum;

	static char *pTempString = NULL;
	int i;


	filelog_printf(GetSuspectListLogFileName(), "AddPeopleToSuspectList() called");
	filelog_printf(GetSuspectListLogFileName(), "Old Rev: SVN: %u. Gimme: %s", 
		pOldRev->iSVNRev, timeGetLocalDateStringFromSecondsSince2000(pOldRev->iGimmeTime));
	filelog_printf(GetSuspectListLogFileName(), "New Rev: SVN: %u. Gimme: %s", 
		pNewRev->iSVNRev, timeGetLocalDateStringFromSecondsSince2000(pNewRev->iGimmeTime));

	estrClear(&pTempString);

	estrConcatf(&pTempString, "%d errors: ", eaSize(&pErrorList->ppEntries));
	for (i=0; i < eaSize(&pErrorList->ppEntries); i++)
	{
		estrConcatf(&pTempString, "%s", StaticDefineIntRevLookup(ErrorDataTypeEnum, pErrorList->ppEntries[i]->eType));	
		if (pErrorList->ppEntries[i]->pLastBlamedPerson && !strchr(pErrorList->ppEntries[i]->pLastBlamedPerson, ' '))
		{
			estrConcatf(&pTempString, "(%s)", pErrorList->ppEntries[i]->pLastBlamedPerson);
		}
		estrConcatf(&pTempString, " ");
	}

	filelog_printf(GetSuspectListLogFileName(), "%s", pTempString);
	
	estrPrintf(&pTempString, "Current suspects: ");

	PutSuspectListIntoEString(pCurSuspects, &pTempString);

	filelog_printf(GetSuspectListLogFileName(), "%s", pTempString);

	if (!pOldRev->iSVNRev)
	{
		filelog_printf(GetSuspectListLogFileName(), "No old SVN rev... aborting");
		return;
	}

	GetSuspects(&newSuspects, pOldRev, pNewRev, pErrorList);

	estrClear(&pTempString);
	PutSuspectListIntoEString(&newSuspects, &pTempString);
	filelog_printf(GetSuspectListLogFileName(), "New suspects: %s", pTempString);


	//check if all errors appear to be blamed on people who actually checked things in. If so, ignore all code
	//checkins and all other data checkins
	if (ErrorListIsErrorsOnly(pErrorList) && eaSize(&newSuspects.lists[SUSPECTTYPE_ERRORBLAMEE].ppSuspects))
	{
		filelog_printf(GetSuspectListLogFileName(), "Error list is errors only and we have some blamees... might only count blamees");
		if (ListHasMemberNotInOtherTwoLists(&newSuspects.lists[SUSPECTTYPE_ERRORBLAMEE].ppSuspects, 
			&newSuspects.lists[SUSPECTTYPE_DATACHECKIN].ppSuspects, &pCurSuspects->lists[SUSPECTTYPE_DATACHECKIN].ppSuspects))
		{
			filelog_printf(GetSuspectListLogFileName(), "One of the blamees didn't do a checkin... proceeding as normal");

			bBlameesOnly = false;
		}
		else
		{
			filelog_printf(GetSuspectListLogFileName(), "Using blamees only");
			bBlameesOnly = true;
		}
	}

	if (bBlameesOnly)
	{
		eaDestroy(&newSuspects.lists[SUSPECTTYPE_CODECHECKIN].ppSuspects);

		RemoveThingsFromListNotInOtherList(&newSuspects.lists[SUSPECTTYPE_DATACHECKIN].ppSuspects, 
			&newSuspects.lists[SUSPECTTYPE_ERRORBLAMEE].ppSuspects);
	}

	//now construct our pppNewPeople list
	for (iListNum = 0; iListNum < SUSPECTTYPE_COUNT; iListNum++)
	{

		for (iNameNum = 0; iNameNum < eaSize(&newSuspects.lists[iListNum].ppSuspects); iNameNum++)
		{
			if (newSuspects.lists[iListNum].ppSuspects[iNameNum])
			{
				if (!IsASuspect(pCurSuspects, newSuspects.lists[iListNum].ppSuspects[iNameNum]))
				{
					eaPushUnique(pppNewPeople, newSuspects.lists[iListNum].ppSuspects[iNameNum]);
				}
			}
		}
	}

	//now construct our pppCurCheckins list
	for (iNameNum = 0; iNameNum < eaSize(&newSuspects.lists[SUSPECTTYPE_CODECHECKIN].ppSuspects); iNameNum++)
	{
		if (newSuspects.lists[SUSPECTTYPE_CODECHECKIN].ppSuspects[iNameNum])
		{
			eaPushUnique(pppCurCheckins, newSuspects.lists[SUSPECTTYPE_CODECHECKIN].ppSuspects[iNameNum]);
		}
	}
	for (iNameNum = 0; iNameNum < eaSize(&newSuspects.lists[SUSPECTTYPE_DATACHECKIN].ppSuspects); iNameNum++)
	{
		if (newSuspects.lists[SUSPECTTYPE_DATACHECKIN].ppSuspects[iNameNum])
		{
			eaPushUnique(pppCurCheckins, newSuspects.lists[SUSPECTTYPE_DATACHECKIN].ppSuspects[iNameNum]);
		}
	}

	//now add everything from new suspects into cur suspects
	for (iListNum = 0; iListNum < SUSPECTTYPE_COUNT; iListNum++)
	{
		for (iNameNum = 0; iNameNum < eaSize(&newSuspects.lists[iListNum].ppSuspects); iNameNum++)
		{
			if (newSuspects.lists[iListNum].ppSuspects[iNameNum])
			{
				eaPushUnique(&pCurSuspects->lists[iListNum].ppSuspects, newSuspects.lists[iListNum].ppSuspects[iNameNum]);
			}
		}

		eaDestroy(&newSuspects.lists[iListNum].ppSuspects);
	}

	estrClear(&pTempString);
	PutSuspectListIntoEString(pCurSuspects, &pTempString);
	filelog_printf(GetSuspectListLogFileName(), "Updated suspects: %s", pTempString);

}


void PutSuspectsIntoSimpleList(SuspectList *pSuspects, const char ***pppList)
{
	int iListNum, iNameNum;

	for (iListNum = 0; iListNum < SUSPECTTYPE_COUNT; iListNum++)
	{
		for (iNameNum = 0; iNameNum < eaSize(&pSuspects->lists[iListNum].ppSuspects); iNameNum++)
		{
			eaPushUnique(pppList, pSuspects->lists[iListNum].ppSuspects[iNameNum]);
		}
	}
}

void ClearSuspectList(SuspectList *pSuspects)
{
	int iListNum;

	for (iListNum = 0; iListNum < SUSPECTTYPE_COUNT; iListNum++)
	{
		eaDestroy(&pSuspects->lists[iListNum].ppSuspects);
	}
}


#include "autogen/SuspectList_h_ast.c"
