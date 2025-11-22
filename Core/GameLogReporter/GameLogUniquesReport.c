#include "GameLogUniquesReport.h"
#include "StringCache.h"
#include "NameValuePair.h"
#include "ExcelXMLFormatter.h"
#include "../Autogen/AppLocale_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "GameLogReporter"););

extern U32 g_uTimeStartSS2000;
extern U32 g_uTimeEndSS2000;

extern char g_pchWorkingDir[];

DailyUniquesData** eaDailyUniques = NULL;
DailyUniquesData** eaGatewayDailyUniques = NULL;

U32 uStartDay;
U32 uEndDay;

S32* ea32AllLangsUsed = NULL;

void UniquesReport_Init(void)
{
	uStartDay = g_uTimeStartSS2000/SECONDS_PER_DAY;
	uEndDay = g_uTimeEndSS2000/SECONDS_PER_DAY;
	eaSetSize(&eaDailyUniques, uEndDay-uStartDay + 1);
	eaSetSize(&eaGatewayDailyUniques, uEndDay-uStartDay + 1);
//	ea32PushUnique(&ea32AllLangsUsed, LANGUAGE_NONE);
}

void UniquesReport_AnalyzeParsedLog(ParsedLog* pLog)
{
	int idx;
	U32 uAccountID = 0;
	DailyUniquesData* pLogDay = NULL;
	LoginAttempt* pAttempt;
	U32 uLogDaysSince2000;

	if (!pLog || pLog->pObjInfo)
		return;
	
	uLogDaysSince2000 = (U32)(pLog->iTime/SECONDS_PER_DAY);

	if (uLogDaysSince2000-uStartDay < 0 || uLogDaysSince2000-uStartDay >= (U32)eaSize(&eaDailyUniques))
	{
		//Oops.
		return;
	}

	pLogDay = eaDailyUniques[uLogDaysSince2000-uStartDay];

	if (!pLogDay)
	{
		pLogDay = StructCreate(parse_DailyUniquesData);
		pLogDay->uDaysSince2000 = uLogDaysSince2000;

		pLogDay->stLoginAttemptsByAccountID = stashTableCreateInt(20480);

		eaDailyUniques[uLogDaysSince2000-uStartDay] = pLogDay;
	}
	
	if (STRING_STARTS_WITH(pLog->pMessage, "ClientLoginComplete"))
	{
		Language eLang = LANGUAGE_NONE;
		//get language
		for (idx = 0; idx < eaSize(&pLog->ppPairs); idx++)
		{
			if (stricmp(pLog->ppPairs[idx]->pName, "AccountID") == 0)
			{
				uAccountID = atoi(pLog->ppPairs[idx]->pValue);
			}
			if (stricmp(pLog->ppPairs[idx]->pName, "LanguageID") == 0)
			{
				eLang = atoi(pLog->ppPairs[idx]->pValue);
			}
		}

		if (uAccountID != 0)
		{
			stashIntFindPointer(pLogDay->stLoginAttemptsByAccountID, uAccountID, &pAttempt);
			if (!pAttempt)
			{
				pAttempt = StructCreate(parse_LoginAttempt);
				pAttempt->uAccountID = uAccountID;
				pAttempt->eLang = LANGUAGE_NONE;
				stashIntAddPointer(pLogDay->stLoginAttemptsByAccountID, uAccountID, pAttempt, true);
			}
			if (pAttempt->eLang == LANGUAGE_NONE)
			{
				pAttempt->eLang = eLang;
			}
			if (!pAttempt->bSuccessful)
			{
				pAttempt->bSuccessful = true;
				pLogDay->uTotalLogins++;
			}
			ea32PushUnique(&ea32AllLangsUsed, eLang);
		}
	}
/*	else if (STRING_STARTS_WITH(pLog->pMessage, "GameAccountData refresh succeeded"))
	{
		for (idx = 0; idx < eaSize(&pLog->ppPairs); idx++)
		{
			if (stricmp(pLog->ppPairs[idx]->pName, "AccountID") == 0)
			{
				uAccountID = atoi(pLog->ppPairs[idx]->pValue);
			}
		}

		if (uAccountID != 0)
		{
			stashIntFindPointer(pLogDay->stLoginAttemptsByAccountID, uAccountID, &pAttempt);
			if (!pAttempt)
			{
				pAttempt = StructCreate(parse_LoginAttempt);
				pAttempt->uAccountID = uAccountID;
				pAttempt->eLang = LANGUAGE_NONE;
				stashIntAddPointer(pLogDay->stLoginAttemptsByAccountID, uAccountID, pAttempt, true);
			}
		}
	} */
	else if (STRING_STARTS_WITH(pLog->pMessage, "LOG: CreateClientSession:"))
	{
		pLogDay = eaGatewayDailyUniques[uLogDaysSince2000-uStartDay];

		if (!pLogDay)
		{
			pLogDay = StructCreate(parse_DailyUniquesData);
			pLogDay->uDaysSince2000 = uLogDaysSince2000;

			pLogDay->stLoginAttemptsByAccountID = stashTableCreateInt(20480);

			eaGatewayDailyUniques[uLogDaysSince2000-uStartDay] = pLogDay;
		}
		for (idx = 0; idx < eaSize(&pLog->ppPairs); idx++)
		{
			if (stricmp(pLog->ppPairs[idx]->pName, "Account") == 0)
			{
				uAccountID = atoi(pLog->ppPairs[idx]->pValue);
			}
		}

		if (uAccountID != 0)
		{
			stashIntFindPointer(pLogDay->stLoginAttemptsByAccountID, uAccountID, &pAttempt);
			if (!pAttempt)
			{
				pAttempt = StructCreate(parse_LoginAttempt);
				pAttempt->uAccountID = uAccountID;
				pAttempt->eLang = LANGUAGE_NONE;
				stashIntAddPointer(pLogDay->stLoginAttemptsByAccountID, uAccountID, pAttempt, true);
			}
			if (!pAttempt->bSuccessful)
			{
				pAttempt->bSuccessful = true;
				pLogDay->uTotalLogins++;
			}
		}
	}
}

void UniquesReport_AddDailyTable(ExcelXMLWorksheet* pSheet, DailyUniquesData*** peaData, bool bIncludeLanguages, int* pRow)
{
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int i, j;
	int iLangIdx = 0;
	int iLangColumns = bIncludeLanguages ? ea32Size(&ea32AllLangsUsed) : 0;

	ExcelXML_WorksheetSetRowHeight(pSheet, *pRow, 40);

	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 4 + iLangColumns);

	eaTableTmp[0]->pchLabel = "Date";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[1]->pchLabel = "Daily Uniques";
	eaTableTmp[1]->eType = kColumnType_Int;
	eaTableTmp[2]->pchLabel = "7-day Uniques";
	eaTableTmp[2]->eType = kColumnType_Int;
	eaTableTmp[3]->pchLabel = "30-day Uniques";
	eaTableTmp[3]->eType = kColumnType_Int;

	if (bIncludeLanguages)
	{
		for (i = 0; i < ea32Size(&ea32AllLangsUsed); i++)
		{
			Language eLang = ea32AllLangsUsed[i];
			char* estrColumnName = NULL;
			if (eLang == LANGUAGE_NONE)
				eaTableTmp[4 + i]->pchLabel = "Didn't reach gameserver";
			else
			{
				estrPrintf(&estrColumnName, "Language %s", StaticDefineInt_FastIntToString(LanguageEnum, eLang));
				eaTableTmp[4 + i]->pchLabel = estrColumnName;
			}
			eaTableTmp[4 + i]->eType = kColumnType_Int;
		}
	}
	//Skip the first 29 days for which we don't have enough data to appear complete.
	for (i = 29; i < eaSize(peaData); i++)
	{
		if (!(*peaData)[i])
			continue;

		ExcelXML_TableColumnAddString(eaTableTmp[0], timeGetLocalDateNoTimeStringFromSecondsSince2000((*peaData)[i]->uDaysSince2000*SECONDS_PER_DAY));
		ExcelXML_TableColumnAddInt(eaTableTmp[1], (*peaData)[i]->uTotalLogins);
		ExcelXML_TableColumnAddInt(eaTableTmp[2], (*peaData)[i]->u7DaySum);
		ExcelXML_TableColumnAddInt(eaTableTmp[3], (*peaData)[i]->u30DaySum);

		iLangIdx = 0;

		if (bIncludeLanguages)
		{
			for (j = 0; j < ea32Size(&ea32AllLangsUsed); j++)
			{
				Language eLang = ea32AllLangsUsed[j];
				int iCount;
				stashIntFindInt((*peaData)[i]->stSuccessfulLoginsByLang, eLang+1, &iCount);
				ExcelXML_TableColumnAddInt(eaTableTmp[4+j], iCount);
			}
		}
	}

	ExcelXML_WorksheetAddTable(pSheet, NULL, pRow, &eaTableTmp);

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
}

void UniquesReport_CalculateSums(DailyUniquesData** eaUniques)
{
	int u7DaySum = 0;
	int u30DaySum = 0;
	int i;
	StashTableIterator stIter;
	StashElement stElem;
	for (i = 0; i < eaSize(&eaUniques); i++)
	{
		u7DaySum += SAFE_MEMBER(eaUniques[i], uTotalLogins);
		u30DaySum += SAFE_MEMBER(eaUniques[i], uTotalLogins);
		if (i >= 6)
		{
			u7DaySum -= SAFE_MEMBER(eaUniques[i-7], uTotalLogins);

			if (eaUniques[i])
				eaUniques[i]->u7DaySum = u7DaySum;
		}
		if (i >= 29)
		{
			u30DaySum -= SAFE_MEMBER(eaUniques[i-29], uTotalLogins);

			if (eaUniques[i])
				eaUniques[i]->u30DaySum = u30DaySum;
		}
		if (eaUniques[i])
		{
			eaUniques[i]->stSuccessfulLoginsByLang = stashTableCreateInt(LANGUAGE_MAX);
			stashGetIterator(eaUniques[i]->stLoginAttemptsByAccountID, &stIter);
			while(stashGetNextElement(&stIter, &stElem))
			{
				LoginAttempt* pAttempt = stashElementGetPointer(stElem);
				if (pAttempt && pAttempt->bSuccessful)
				{
					S32 iCount;
					stashIntFindInt(eaUniques[i]->stSuccessfulLoginsByLang, pAttempt->eLang+1, &iCount);
					stashIntAddInt(eaUniques[i]->stSuccessfulLoginsByLang, pAttempt->eLang+1, ++iCount, true);
				}
			}
		}
	}
}

void UniquesReport_GenerateReport()
{
	int i = 0;
	char* estrOutput = NULL;
	ExcelXMLWorkbook* pBook = NULL;

	UniquesReport_CalculateSums(eaDailyUniques);
	UniquesReport_CalculateSums(eaGatewayDailyUniques);

	estrClear(&estrOutput);

	//An example workbook.

	pBook = ExcelXML_CreateWorkbook();

	{
		int iRow = 1;
		ExcelXMLWorksheet* pCurSheet = ExcelXML_WorkbookAddSheet(pBook, "Daily Uniques");

		ExcelXML_WorksheetSetRowHeight(pCurSheet, iRow, 30);
		ExcelXML_WorksheetAddStringRow(pCurSheet, NULL, &iRow, 3, "BigBold", "Daily Uniques Report");

		UniquesReport_AddDailyTable(pCurSheet, &eaDailyUniques, true, &iRow);
	}

	{
		int iRow = 1;
		ExcelXMLWorksheet* pCurSheet = ExcelXML_WorkbookAddSheet(pBook, "Gateway Daily Uniques");

		ExcelXML_WorksheetSetRowHeight(pCurSheet, iRow, 30);
		ExcelXML_WorksheetAddStringRow(pCurSheet, NULL, &iRow, 3, "BigBold", "Gateway Daily Uniques Report");

		UniquesReport_AddDailyTable(pCurSheet, &eaGatewayDailyUniques, false, &iRow);
	}

	ExcelXML_WorkbookWriteXML(pBook, &estrOutput);

	StructDestroy(parse_ExcelXMLWorkbook, pBook);

	WriteOutputFile("DailyUniques.xml", estrOutput, false);

	estrDestroy(&estrOutput);
}

#include "GameLogUniquesReport_h_ast.c"