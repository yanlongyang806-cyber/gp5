
AUTO_STRUCT;
typedef struct ExcelXMLCell
{
	int iIndex; AST(KEY)
	char* estrCellData;	AST(ESTRING)
	int iMerged;
	bool bIgnore;
} ExcelXMLCell;

AUTO_STRUCT;
typedef struct ExcelXMLRow
{
	int iIndex; AST(KEY)
	int iHeight;
	ExcelXMLCell** eaCells;
} ExcelXMLRow;

AUTO_STRUCT;
typedef struct ExcelXMLWorksheet
{
	char* estrName;	AST(ESTRING)
	ExcelXMLRow** eaRows;
} ExcelXMLWorksheet;

AUTO_STRUCT;
typedef struct ExcelXMLWorkbook
{
	char* estrHeader;	AST(ESTRING)
	char* estrStyles; 	AST(ESTRING)
	ExcelXMLWorksheet** eaWorksheets; 
	char* estrFooter;	AST(ESTRING)
} ExcelXMLWorkbook;

AUTO_ENUM;
typedef enum ExcelXMLTableColumnType
{
	kColumnType_Empty = 0,
	kColumnType_String,
	kColumnType_Int,
	kColumnType_Formula,
	kColumnType_U64,
	kColumnType_Float,
	kColumnType_Currency
} ExcelXMLTableColumnType;

/*
	These two structs are just for ease of creation of simple tables in code.
*/
AUTO_STRUCT;
typedef struct ExcelXMLTableCell
{
	char* estrData;	AST(ESTRING)
	int iData;
	F32 fFloatData;
	U64 u64Data;
} ExcelXMLTableCell;

AUTO_STRUCT;
typedef struct ExcelXMLTableColumn
{
	ExcelXMLTableColumnType eType;
	const char* pchLabel;		AST(UNOWNED)
	ExcelXMLTableCell** eaCells;
	int iMergedColumns;
} ExcelXMLTableColumn;

ExcelXMLWorkbook* ExcelXML_CreateWorkbook();
ExcelXMLWorksheet* ExcelXML_WorkbookAddSheet(ExcelXMLWorkbook* pBook, const char* pchName);
void ExcelXML_WorksheetAddStringRow(ExcelXMLWorksheet* pSheet, int* pColInOut, int* pRowInOut, int iMerged, const char* pchStyle, const char* pchText);
void ExcelXML_WorksheetAddTable(ExcelXMLWorksheet* pSheet, int* pColumnInOut, int* pRowInOut, ExcelXMLTableColumn*** peaColumnData);
void ExcelXML_WorkbookWriteXML(ExcelXMLWorkbook* pBook, char** estrOut);

void ExcelXML_TableColumnAddString(ExcelXMLTableColumn* pCol, const char* pchStr);
void ExcelXML_TableColumnAddInt(ExcelXMLTableColumn* pCol, int val);
void ExcelXML_TableColumnAddU64(ExcelXMLTableColumn* pCol, U64 val);
void ExcelXML_TableColumnAddFloat(ExcelXMLTableColumn* pCol, float val);
void ExcelXML_WorksheetSetRowHeight(ExcelXMLWorksheet* pSheet, int iRow, int iHeight);

#include "ExcelXMLFormatter_h_ast.h"