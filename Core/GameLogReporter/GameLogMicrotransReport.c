#include "GameLogMicrotransReport.h"
#include "StringCache.h"
#include "NameValuePair.h"
#include "ExcelXMLFormatter.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "GameLogReporter"););

extern U32 g_uTimeStartSS2000;
extern U32 g_uTimeEndSS2000;

extern char g_pchWorkingDir[];

StashTable stExpenditureByProduct = NULL;
StashTable stExpenditureByCategory = NULL;

StashTable stGatewayExpenditureByProduct = NULL;
StashTable stGatewayExpenditureByCategory = NULL;

U32* ea32SpentPerHour = NULL;
U32* ea32GatewaySpentPerHour = NULL;

void MicrotransReport_Init(void)
{
	stExpenditureByProduct = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);
	stExpenditureByCategory = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);

	stGatewayExpenditureByProduct = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);
	stGatewayExpenditureByCategory = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);
}

void MicrotransReport_AnalyzeParsedLog(ParsedLog* pLog)
{
	const char* pchProduct = NULL;
	const char* pchCategories = NULL;
	const char* pchCurrency = NULL;
	MicrotransAmountSpent* pSpent;
	char** eaCategories = NULL;
	U32 uHour = 0;
	int idx;
	int iPaid = 0;
	U32 uHourSum = 0;

	if (!pLog || !pLog->pObjInfo)
		return;
	
	if (stricmp(pLog->pObjInfo->pAction, "Microtransactionsuccess") != 0)
		return;

	//parse log key/value pairs
	for (idx = 0; idx < eaSize(&pLog->ppPairs); idx++)
	{
		if (stricmp(pLog->ppPairs[idx]->pName, "Product") == 0)
		{
			pchProduct = pLog->ppPairs[idx]->pValue;
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Categories") == 0)
		{
			pchCategories = pLog->ppPairs[idx]->pValue;
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Currency") == 0)
		{
			pchCurrency = pLog->ppPairs[idx]->pValue;
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Price") == 0)
		{
			iPaid = atoi(pLog->ppPairs[idx]->pValue);
		}
	}

	if (iPaid <= 0)
		return;

	uHour = pLog->iTime/SECONDS_PER_HOUR - (pLog->iTime/SECONDS_PER_DAY)*24 + 1;

	uHourSum = ea32Get(&ea32SpentPerHour, uHour);
	ea32Set(&ea32SpentPerHour, uHourSum + iPaid, uHour);
	if (pchCategories)
		DivideString(pchCategories, ",", &eaCategories, DIVIDESTRING_POSTPROCESS_ESTRINGS);

	if (stashFindPointer(stExpenditureByProduct, pchProduct, &pSpent))
	{
		pSpent->uAmount += iPaid;
	}
	else
	{
		pSpent = StructCreate(parse_MicrotransAmountSpent);
		pSpent->uAmount = iPaid;
		pSpent->pchKey = strdup(pchProduct);
		stashAddPointer(stExpenditureByProduct, pchProduct, pSpent, false);
	}

	for (idx = 0; idx < eaSize(&eaCategories); idx++)
	{
		char* estrCategory = eaCategories[idx];

		if (!estrCategory || !estrCategory[0])
			continue;

		if (stashFindPointer(stExpenditureByCategory, estrCategory, &pSpent))
		{
			pSpent->uAmount += iPaid;
		}
		else
		{
			pSpent = StructCreate(parse_MicrotransAmountSpent);
			pSpent->uAmount = iPaid;
			pSpent->pchKey = strdup(estrCategory);
			stashAddPointer(stExpenditureByCategory, estrCategory, pSpent, false);
		}
	}

	if (pLog->eServerType == GLOBALTYPE_GATEWAYSERVER)
	{
		uHourSum = ea32Get(&ea32GatewaySpentPerHour, uHour);
		ea32Set(&ea32GatewaySpentPerHour, uHourSum + iPaid, uHour);

		if (stashFindPointer(stGatewayExpenditureByProduct, pchProduct, &pSpent))
		{
			pSpent->uAmount += iPaid;
		}
		else
		{
			pSpent = StructCreate(parse_MicrotransAmountSpent);
			pSpent->uAmount = iPaid;
			pSpent->pchKey = strdup(pchProduct);
			stashAddPointer(stGatewayExpenditureByProduct, pchProduct, pSpent, false);
		}

		for (idx = 0; idx < eaSize(&eaCategories); idx++)
		{
			char* estrCategory = eaCategories[idx];

			if (!estrCategory || !estrCategory[0])
				continue;

			if (stashFindPointer(stGatewayExpenditureByCategory, estrCategory, &pSpent))
			{
				pSpent->uAmount += iPaid;
			}
			else
			{
				pSpent = StructCreate(parse_MicrotransAmountSpent);
				pSpent->uAmount = iPaid;
				pSpent->pchKey = strdup(estrCategory);
				stashAddPointer(stGatewayExpenditureByCategory, estrCategory, pSpent, false);
			}
		}
	}
}

static int MicrotransReport_SortSummariesByDate(const MicrotransSummary** pSummaryA, const MicrotransSummary** pSummaryB)
{
	return (S64)(*pSummaryA)->uStartTimeSS2000 - (S64)(*pSummaryB)->uStartTimeSS2000;
}

static int MicrotransReport_SortAmountSpent(const MicrotransAmountSpent** pSpentA, const MicrotransAmountSpent** pSpentB)
{
	return (S64)(*pSpentB)->uAmount - (S64)(*pSpentA)->uAmount;
}

void MicrotransReport_AddAmountSpentTable(ExcelXMLWorksheet* pSheet, int *pCol, int* pRow, const char* pchName, StashTable stData)
{
	ExcelXMLTableColumn** eaTableTmp = NULL;
	StashTableIterator stashIter;
	StashElement stashElem;
	MicrotransAmountSpent** eaSpent = NULL;
	int i;

	ExcelXML_WorksheetSetRowHeight(pSheet, *pRow, 40);

	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 2);

	eaTableTmp[0]->pchLabel = pchName;
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[0]->iMergedColumns = 2;
	eaTableTmp[1]->pchLabel = "Spent";
	eaTableTmp[1]->eType = kColumnType_Currency;

	stashGetIterator(stData, &stashIter);
	while(stashGetNextElement(&stashIter, &stashElem))
	{
		MicrotransAmountSpent* pSpent = stashElementGetPointer(stashElem);
		eaPush(&eaSpent, pSpent);
	}

	eaQSort(eaSpent, MicrotransReport_SortAmountSpent);

	for (i = 0; i < eaSize(&eaSpent); i++)
	{
		ExcelXML_TableColumnAddString(eaTableTmp[0], eaSpent[i]->pchKey);
		ExcelXML_TableColumnAddFloat(eaTableTmp[1], ((F32)eaSpent[i]->uAmount)/100.0);
	}

	ExcelXML_WorksheetAddTable(pSheet, pCol, pRow, &eaTableTmp);

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
}

void MicrotransReport_AddHourlyTable(ExcelXMLWorksheet* pSheet, U32** peaSpent, int* pCol, int* pRow)
{
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int i;

	ExcelXML_WorksheetSetRowHeight(pSheet, *pRow, 40);

	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 2);

	eaTableTmp[0]->pchLabel = "Hour";
	eaTableTmp[0]->eType = kColumnType_Int;
	eaTableTmp[1]->pchLabel = "Spent";
	eaTableTmp[1]->eType = kColumnType_Currency;

	for (i = 0; i < ea32Size(peaSpent); i++)
	{
		ExcelXML_TableColumnAddInt(eaTableTmp[0], i);
		ExcelXML_TableColumnAddFloat(eaTableTmp[1], ((F32)((*peaSpent)[i]))/100.0);
	}

	ExcelXML_WorksheetAddTable(pSheet, pCol, pRow, &eaTableTmp);

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
}

void MicrotransReport_GenerateReport()
{
	int i = 0;
	char* estrOutput = NULL;
	ExcelXMLWorkbook* pBook = NULL;

	estrClear(&estrOutput);

	//An example workbook.
	pBook = ExcelXML_CreateWorkbook();

	{
		int iRow = 1;
		int iCol = 1;
		ExcelXMLWorksheet* pCurSheet = ExcelXML_WorkbookAddSheet(pBook, "All Microtransactions");
		char* estrSheetHeader = NULL;

		ExcelXML_WorksheetSetRowHeight(pCurSheet, iRow, 30);

		estrPrintf(&estrSheetHeader, "Microtransaction report (%s - ", timeGetLocalDateStringFromSecondsSince2000(g_uTimeStartSS2000));
		estrConcatf(&estrSheetHeader, "%s)", timeGetLocalDateStringFromSecondsSince2000(g_uTimeEndSS2000));
		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 10, "BigBold", estrSheetHeader);
		estrDestroy(&estrSheetHeader);

		iCol = 1;
		iRow = 3;

		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 3, "BigBold", "Cumulative Product Expenditure");

		iCol++;
		iRow = 3;
		
		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 3, "BigBold", "Cumulative Category Expenditure");
		
		iCol++;
		iRow = 3;
		
		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 1, "BigBold", "Hourly Expenditure");
		
		iCol = 1;
		iRow = 4;

		MicrotransReport_AddAmountSpentTable(pCurSheet, &iCol, &iRow, "Product", stExpenditureByProduct);

		iCol++;
		iRow = 4;

		MicrotransReport_AddAmountSpentTable(pCurSheet, &iCol, &iRow, "Category", stExpenditureByCategory);

		iCol++;
		iRow = 4;

		MicrotransReport_AddHourlyTable(pCurSheet, &ea32SpentPerHour, &iCol, &iRow);
	}

	//gateway
	{
		int iRow = 1;
		int iCol = 1;
		ExcelXMLWorksheet* pCurSheet = ExcelXML_WorkbookAddSheet(pBook, "Gateway Only");
		char* estrSheetHeader = NULL;

		ExcelXML_WorksheetSetRowHeight(pCurSheet, iRow, 30);

		estrPrintf(&estrSheetHeader, "Gateway Microtransaction report (%s - ", timeGetLocalDateStringFromSecondsSince2000(g_uTimeStartSS2000));
		estrConcatf(&estrSheetHeader, "%s)", timeGetLocalDateStringFromSecondsSince2000(g_uTimeEndSS2000));
		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 10, "BigBold", estrSheetHeader);
		estrDestroy(&estrSheetHeader);

		iCol = 1;
		iRow = 3;

		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 3, "BigBold", "Cumulative Product Expenditure");

		iCol++;
		iRow = 3;

		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 3, "BigBold", "Cumulative Category Expenditure");

		iCol++;
		iRow = 3;

		ExcelXML_WorksheetAddStringRow(pCurSheet, &iCol, &iRow, 1, "BigBold", "Hourly Expenditure");

		iCol = 1;
		iRow = 4;

		MicrotransReport_AddAmountSpentTable(pCurSheet, &iCol, &iRow, "Product", stGatewayExpenditureByProduct);

		iCol++;
		iRow = 4;

		MicrotransReport_AddAmountSpentTable(pCurSheet, &iCol, &iRow, "Category", stGatewayExpenditureByCategory);

		iCol++;
		iRow = 4;

		MicrotransReport_AddHourlyTable(pCurSheet, &ea32GatewaySpentPerHour, &iCol, &iRow);
	}

	ExcelXML_WorkbookWriteXML(pBook, &estrOutput);

	StructDestroy(parse_ExcelXMLWorkbook, pBook);

	WriteOutputFile("MicrotransReport.xml", estrOutput, false);

	estrDestroy(&estrOutput);
}
#include "GameLogMicrotransReport_h_ast.c"