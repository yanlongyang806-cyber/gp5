#include "excelxmlformatter.h"
#include "xmlformatstrings.h"

ExcelXMLWorkbook* ExcelXML_CreateWorkbook()
{
	ExcelXMLWorkbook* pBook = StructCreate(parse_ExcelXMLWorkbook);
	pBook->estrHeader = estrDup(pchDocumentHeader);
	pBook->estrStyles = estrDup(pchStyleBlock);
	pBook->estrFooter = estrDup(pchDocumentFooter);
	return pBook;
}

ExcelXMLWorksheet* ExcelXML_WorkbookAddSheet(ExcelXMLWorkbook* pBook, const char* pchName)
{
	ExcelXMLWorksheet* pSheet;

	if (!pBook)
		return NULL;

	pSheet = StructCreate(parse_ExcelXMLWorksheet);
	pSheet->estrName = estrDup(pchName);
	eaPush(&pBook->eaWorksheets, pSheet);
	return pSheet;
}

static ExcelXMLCell* ExcelXML_WorksheetAddCellInternal(ExcelXMLWorksheet* pSheet, int iColumn, int iRow)
{
	int idxRow;
	ExcelXMLCell* pCell;
	ExcelXMLRow* pRow;

	if (!pSheet)
		return NULL;

	if (!pSheet->eaRows)
		eaIndexedEnable(&pSheet->eaRows, parse_ExcelXMLRow);

	idxRow = eaIndexedFindUsingInt(&pSheet->eaRows, iRow);

	if (idxRow > -1)
		pRow = pSheet->eaRows[idxRow];
	else
	{
		pRow = StructCreate(parse_ExcelXMLRow);
		pRow->iIndex = iRow;
		eaIndexedEnable(&pRow->eaCells, parse_ExcelXMLCell);
		eaIndexedAdd(&pSheet->eaRows, pRow);
	}

	assertmsgf(eaIndexedFindUsingInt(&pRow->eaCells, iColumn) == -1, "Tried to add a duplicate cell within a single row. This is illegal.");

	pCell = StructCreate(parse_ExcelXMLCell);
	pCell->iIndex = iColumn;

	eaIndexedAdd(&pRow->eaCells, pCell);

	return pCell;
}

bool ExcelXML_CellWriteValueInternal(ExcelXMLCell* pCell, int iMergedColumns, const char* pchStyle, ExcelXMLTableColumnType eType, ExcelXMLTableCell* pCellData)
{
	if (!pCell)
		return false;

	if (!pchStyle)
		pchStyle = "Default";

	pCell->iMerged = iMergedColumns;

	switch(eType)
	{
	case kColumnType_Empty:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockString), pCell->iIndex, pchStyle, iMergedColumns, "");
		}break;
	case kColumnType_Int:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockNumber), pCell->iIndex, pchStyle, "", pCellData->iData);
		}break;
	case kColumnType_U64:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockNumberU64), pCell->iIndex, pchStyle, pCellData->u64Data);
		}break;
	case kColumnType_Float:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockNumberFloat), pCell->iIndex, pchStyle, pCellData->fFloatData);
		}break;
	case kColumnType_Currency:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockNumberFloat), pCell->iIndex, pchStyle, pCellData->fFloatData);
		}break;
	case kColumnType_Formula:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockNumber), pCell->iIndex, pchStyle, pCellData->estrData, 0);
		}break;
	case kColumnType_String:
		{
			estrPrintf(&pCell->estrCellData, FORMAT_OK(pchCellBlockString), pCell->iIndex, pchStyle, iMergedColumns, pCellData->estrData);
		}break;
	}
	return true;
}

const char* ExcelXML_GetStyleNameForCell(ExcelXMLTableColumn* pColumn, int iCol, int iRow)
{
	static char* estrStyle = NULL;

	if (iRow == -1)
		estrPrintf(&estrStyle, "ColumnHeader");
	else if (iRow % 2)
		estrPrintf(&estrStyle, "Grey");
	else
		estrClear(&estrStyle);
	
	if (iRow >= 0 && pColumn->eType == kColumnType_Currency)
		estrConcatf(&estrStyle, "Currency");
	else
		estrConcatf(&estrStyle, "Default");

	if (iCol > 0)
		estrConcatf(&estrStyle, "RightAligned");

	return estrStyle;
}

void ExcelXML_WorksheetAddTable(ExcelXMLWorksheet* pSheet, int* pColumnInOut, int* pRowInOut, ExcelXMLTableColumn*** peaColumnData)
{
	int iWidth = eaSize(peaColumnData);
	int i, j, iMaxHeight = 0;
	int col = 1;
	int iMerged = 0;
	const char* pchStyle;

	if (!pColumnInOut)
		pColumnInOut = &col;

	for (i = 0; i < iWidth; i++)
	{
		ExcelXMLTableColumn* pCurTableColumn = (*peaColumnData)[i];
		ExcelXMLCell* pCell = ExcelXML_WorksheetAddCellInternal(pSheet, (*pColumnInOut), (*pRowInOut));
		ExcelXMLTableCell cellTemp;
		assertmsgf(pCell, "Failed to add cell to table.");

		iMerged += pCurTableColumn->iMergedColumns;
		pchStyle = ExcelXML_GetStyleNameForCell(pCurTableColumn, i, -1);

		cellTemp.estrData = (char*)pCurTableColumn->pchLabel;
		ExcelXML_CellWriteValueInternal(pCell, pCurTableColumn->iMergedColumns, pchStyle, kColumnType_String, &cellTemp);
		
		for (j = 0; j < eaSize(&pCurTableColumn->eaCells); j++)
		{
			pCell = ExcelXML_WorksheetAddCellInternal(pSheet, (*pColumnInOut), (*pRowInOut) + j + 1);

			pchStyle = ExcelXML_GetStyleNameForCell(pCurTableColumn, i, j);

			ExcelXML_CellWriteValueInternal(pCell, pCurTableColumn->iMergedColumns, pchStyle, pCurTableColumn->eType, pCurTableColumn->eaCells[j]);
		}

		if (j > iMaxHeight)
			iMaxHeight = j;

		(*pColumnInOut)++;
		(*pColumnInOut)+= pCurTableColumn->iMergedColumns;
	}

	(*pRowInOut) += iMaxHeight+1;
}

void ExcelXML_WorksheetAddStringRow(ExcelXMLWorksheet* pSheet, int* pColInOut, int* pRowInOut, int iMerged, const char* pchStyle, const char* pchText)
{
	int col = 1;
	ExcelXMLCell* pCell = NULL;
	ExcelXMLTableCell cellTemp;

	if (!pColInOut)
		pColInOut = &col;

	pCell = ExcelXML_WorksheetAddCellInternal(pSheet, (*pColInOut), (*pRowInOut));

	assertmsgf(pCell, "Failed to add cell to table.");

	cellTemp.estrData = (char*)pchText;

	ExcelXML_CellWriteValueInternal(pCell, iMerged, pchStyle, kColumnType_String, &cellTemp);
	if (pColInOut)
		(*pColInOut) += iMerged+1;
	if (pRowInOut)
		(*pRowInOut)++;
}

void ExcelXML_WorksheetSetRowHeight(ExcelXMLWorksheet* pSheet, int iRow, int iHeight)
{
	ExcelXMLRow* pRow;
	int idxRow;

	if (!pSheet)
		return;

	if (!pSheet->eaRows)
		eaIndexedEnable(&pSheet->eaRows, parse_ExcelXMLRow);

	idxRow = eaIndexedFindUsingInt(&pSheet->eaRows, iRow);

	if (idxRow > -1)
		pRow = pSheet->eaRows[idxRow];
	else
	{
		pRow = StructCreate(parse_ExcelXMLRow);
		pRow->iIndex = iRow;
		eaIndexedEnable(&pRow->eaCells, parse_ExcelXMLCell);
		eaIndexedAdd(&pSheet->eaRows, pRow);
	}

	pRow->iHeight = iHeight;
}

void ExcelXML_TableColumnAddString(ExcelXMLTableColumn* pCol, const char* pchStr)
{
	ExcelXMLTableCell* pCell = StructCreate(parse_ExcelXMLTableCell);
	pCell->estrData = estrDup(pchStr);
	eaPush(&pCol->eaCells, pCell);
}

void ExcelXML_TableColumnAddInt(ExcelXMLTableColumn* pCol, int val)
{
	ExcelXMLTableCell* pCell = StructCreate(parse_ExcelXMLTableCell);
	pCell->iData = val;
	eaPush(&pCol->eaCells, pCell);
}

void ExcelXML_TableColumnAddFloat(ExcelXMLTableColumn* pCol, float val)
{
	ExcelXMLTableCell* pCell = StructCreate(parse_ExcelXMLTableCell);
	pCell->fFloatData = val;
	eaPush(&pCol->eaCells, pCell);
}

void ExcelXML_TableColumnAddU64(ExcelXMLTableColumn* pCol, U64 val)
{
	ExcelXMLTableCell* pCell = StructCreate(parse_ExcelXMLTableCell);
	pCell->u64Data = val;
	eaPush(&pCol->eaCells, pCell);
}

void ExcelXML_WorkbookWriteXML(ExcelXMLWorkbook* pBook, char** estrOut)
{
	int iSheet, iRow, iCell;
	int iTotalRows = 0;
	int iTotalCols = 0;  
	char* estrSheets = NULL;
	char* estrRows = NULL;
	char* estrCols = NULL;
	char* estrCells = NULL;
	//blah
	estrClear(estrOut);
	estrConcatf(estrOut, "%s", pchDocumentHeader);
	estrConcatf(estrOut, "%s", pchStyleBlock);

	//Write each sheet
	for (iSheet = 0; iSheet < eaSize(&pBook->eaWorksheets); iSheet++)
	{
		ExcelXMLWorksheet* pSheet = pBook->eaWorksheets[iSheet];

		//Write each row
		for (iRow = 0; iRow < eaSize(&pSheet->eaRows); iRow++)
		{
			ExcelXMLRow* pRow = pSheet->eaRows[iRow];
			//Write each cell
			for (iCell = 0; iCell < eaSize(&pRow->eaCells); iCell++)
			{
				ExcelXMLCell* pCell = pRow->eaCells[iCell];
				if (pCell->bIgnore)
					continue;
				estrConcatf(&estrCells, "%s", pCell->estrCellData);
				MAX1(iTotalCols, pCell->iIndex + pCell->iMerged);
			}
			estrConcatf(&estrRows, FORMAT_OK(pchRowBlock), pRow->iIndex, pRow->iHeight > 0 ? pRow->iHeight : 16, estrCells);
			MAX1(iTotalRows, pRow->iIndex);
			estrClear(&estrCells);
		}
		estrConcatf(&estrCols, FORMAT_OK(pchColumnBlock), iTotalCols-1);

		estrConcatf(&estrSheets, FORMAT_OK(pchWorksheetBlock), pSheet->estrName, iTotalCols, iTotalRows, estrCols, estrRows);

		estrClear(&estrCols);
		estrClear(&estrRows);
	}
	estrConcatf(estrOut, "%s", estrSheets);
	estrConcatf(estrOut, "%s", pchDocumentFooter);

	estrDestroy(&estrSheets);
	estrDestroy(&estrRows);
	estrDestroy(&estrCols);
	estrDestroy(&estrCells);
}

#include "ExcelXMLFormatter_h_ast.c"