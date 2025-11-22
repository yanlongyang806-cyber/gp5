#include "GameLogNewReportTemplate.h"
#include "StringCache.h"
#include "NameValuePair.h"
#include "ExcelXMLFormatter.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "GameLogReporter"););

extern U32 g_uTimeStartSS2000;
extern U32 g_uTimeEndSS2000;

extern char g_pchWorkingDir[];

/*
//Summary-related variables
static char* s_estrSummaryFilename = NULL;
ExampleDailySummaries g_DailySummaries;
static ExampleDailySummary newSummary = {0};
*/
void NewReportTemplate_Init(void)
{
	//Set up lookup tables, etc. here.
}

void NewReportTemplate_AnalyzeParsedLog(ParsedLog* pLog)
{
	ExampleDailySummaryData* pSummaryData = NULL;

	if (!pLog || !pLog->pObjInfo)
		return;
	
	/*
	//parse log key/value pairs
	for (idx = 0; idx < eaSize(&pLog->ppPairs); idx++)
	{
		if (stricmp(pLog->ppPairs[idx]->pName, "Numeric") == 0)
		{
			pchItemDefName = pLog->ppPairs[idx]->pValue;
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Added") == 0)
		{
			iDelta = atoi(pLog->ppPairs[idx]->pValue);
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Reason") == 0)
		{
			stashFindPointer(stReasonDisplayNameMapping, pLog->ppPairs[idx]->pValue, &pchReasonDisplay);
			if (!pchReasonDisplay)
			{
				//unexpected reason
				printf("Unexpected ItemChangeReason: %s\n", pLog->ppPairs[idx]->pValue);
				pchReasonDisplay = pLog->ppPairs[idx]->pValue;
			}
		}
	}
	*/

	/*
	//NW-specific player information
	for (idx = 0; idx < eaSize(&pLog->pObjInfo->ppProjSpecific); idx++)
	{
		if (stricmp(pLog->pObjInfo->ppProjSpecific[idx]->pKey, "CL") == 0)
		{
			pchPlayerClassCode = pLog->pObjInfo->ppProjSpecific[idx]->pVal;
		}
		else if (stricmp(pLog->pObjInfo->ppProjSpecific[idx]->pKey, "LEV") == 0)
		{
			iLevel = atoi(pLog->pObjInfo->ppProjSpecific[idx]->pVal);
		}
	}
	*/

	/*
	//If we care about a summary file, add data to the new entry here
	idx = eaIndexedFindUsingString(&newSummary.eaData, pchKey);

	if (idx >= 0)
		pSummaryData = newSummary.eaData[idx];
	else
	{
		pSummaryData = StructCreate(parse_ExampleDailySummaryData);
		pSummaryData->pchKey = allocAddString(pchKey);
		if (!newSummary.eaData)
			eaIndexedEnable(&newSummary.eaData, parse_ExampleDailySummaryData);
		eaIndexedPushUsingStringIfPossible(&newSummary.eaData, pchKey, pSummaryData);
	}
	*/

	//do stuff with the ParsedLog here
}
/*
static int NewReportTemplate_SortSummariesByDate(const ExampleDailySummary** pSummaryA, const ExampleDailySummary** pSummaryB)
{
	return (S64)(*pSummaryA)->uDaysSince2000 - (S64)(*pSummaryB)->uDaysSince2000;
}
*/
void NewReportTemplate_AddExampleTable(ExcelXMLWorksheet* pSheet, int* pRow)
{
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int i;
	const char* ppchFruit[] = {"Apple", "Banana", "Kiwi", "Grape", "Orange"};

	ExcelXML_WorksheetSetRowHeight(pSheet, *pRow, 40);

	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 3);

	eaTableTmp[0]->pchLabel = "Numbers";
	eaTableTmp[0]->eType = kColumnType_Int;
	eaTableTmp[1]->pchLabel = "Fruit";
	eaTableTmp[1]->eType = kColumnType_String;
	eaTableTmp[2]->pchLabel = "Formulae";
	eaTableTmp[2]->eType = kColumnType_Formula;
	for (i = 0; i < 5; i++)
	{
		ExcelXML_TableColumnAddInt(eaTableTmp[0], i);
		ExcelXML_TableColumnAddString(eaTableTmp[1], ppchFruit[i]);
		ExcelXML_TableColumnAddString(eaTableTmp[2], "=R[0]C[-2]/2");
	}

	ExcelXML_WorksheetAddTable(pSheet, NULL, pRow, &eaTableTmp);

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
}

void NewReportTemplate_GenerateReport()
{
	int i = 0;
	char* estrOutput = NULL;
	ExcelXMLWorkbook* pBook = NULL;

	estrClear(&estrOutput);

	/*
	//If we care about a summary file, need to do some bookkeeping here.

	//Load file of previous report summaries, if applicable
	estrPrintf(&s_estrSummaryFilename, "%s/%s", g_pchWorkingDir, "DailyEconomy.summaries");
	ParserLoadFiles(NULL, 
		s_estrSummaryFilename, 
		NULL ,
		PARSER_OPTIONALFLAG, 
		parse_ItemDailySummaries,
		&g_DailySummaries);

	newSummary.uDaysSince2000 = (U32)(g_uTimeStartSS2000/86400);

	//If we already ran a report today, remove it to prevent dupes.
	for (i = 0; i < eaSize(&g_DailySummaries.eaSummaries); i++)
	{
		if (g_DailySummaries.eaSummaries[i]->uDaysSince2000 == newSummary.uDaysSince2000)
		{
			eaRemoveFast(&g_DailySummaries.eaSummaries, i);
			break;
		}
	}

	//Don't need to structclone newSummary since we only need one and we're just going to ParserWriteText and then close the process.
	eaPush(&g_DailySummaries.eaSummaries, &newSummary);

	//sort summaries by date
	eaQSort(g_DailySummaries.eaSummaries, NewReportTemplate_SortSummariesByDate);

	//remove excess summaries, oldest first.
	while (eaSize(&g_DailySummaries.eaSummaries) >= EXAMPLE_NUM_SUMMARIES_TO_KEEP)
	{
		StructDestroy(parse_ExampleDailySummary, g_DailySummaries.eaSummaries[0]);
		eaRemove(&g_DailySummaries.eaSummaries, 0);
		//remove oldest
	}
	*/

	//An example workbook.

	pBook = ExcelXML_CreateWorkbook();

	{
		int iRow = 1;
		ExcelXMLWorksheet* pCurSheet = ExcelXML_WorkbookAddSheet(pBook, "Example_Sheet_Name");

		ExcelXML_WorksheetSetRowHeight(pCurSheet, iRow, 30);
		ExcelXML_WorksheetAddStringRow(pCurSheet, NULL, &iRow, 3, "BigBold", "Worksheet header");

		NewReportTemplate_AddExampleTable(pCurSheet, &iRow);

		//Skip a row.
		iRow++;

		//Add the same table again, why not?
		NewReportTemplate_AddExampleTable(pCurSheet, &iRow);
	}

	ExcelXML_WorkbookWriteXML(pBook, &estrOutput);

	StructDestroy(parse_ExcelXMLWorkbook, pBook);

	WriteOutputFile("ShittyExampleReport.xml", estrOutput, false);

	/*
	//Write out our summaries file if we care
	ParserWriteTextFile(s_estrSummaryFilename, 
		parse_ItemDailySummaries,
		&g_DailySummaries,
		0,
		0);
	*/
	estrDestroy(&estrOutput);
}

void NewReportTemplate_SendResultEmail()
{
	SendReportEmail("ShittyExampleReport.xml");
}

#include "GameLogNewReportTemplate_h_ast.c"