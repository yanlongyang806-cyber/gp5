#include "sql.h"

#include "EArray.h"
#include "EString.h"
#include "error.h"
#include "StringUtil.h"
#include "MemTrack.h"
#include "textparser.h"

#include "../../3rdparty/sqlite/sqlite3.h"

void sqlCritAlert_dbg(sqlite3 *p_sqlite3, const char *key, const char *pFileName, int iLineNum)
{
	AssertOrAlertEx(key, pFileName, iLineNum, "%s", sqlite3_errmsg(p_sqlite3));
}

void sqlWarnAlert_dbg(sqlite3 *p_sqlite3, const char *key, const char *pFileName, int iLineNum)
{
	AssertOrAlertWarningEx(key, pFileName, iLineNum, "%s", sqlite3_errmsg(p_sqlite3));
}

void sqlProgAlert_dbg(sqlite3 *p_sqlite3, const char *key, const char *pFileName, int iLineNum)
{
	AssertOrProgrammerAlertEx(key, pFileName, iLineNum, "%s", sqlite3_errmsg(p_sqlite3));
}

sqlite3_stmt *sqlPrepareStmtFromStr(sqlite3 *p_sqlite3, const char *strStmt)
{
	sqlite3_stmt *stmt = NULL;

	sqlite3_prepare_v2(p_sqlite3, strStmt, (int)(strlen(strStmt) + 1), &stmt, NULL);

	return stmt;
}

sqlite3_stmt *sqlPrepareStmtFromEstr(sqlite3 *p_sqlite3, const char *estrStmt)
{
	sqlite3_stmt *stmt = NULL;

	sqlite3_prepare_v2(p_sqlite3, estrStmt, estrLength(&estrStmt) + 1, &stmt, NULL);

	return stmt;
}

sqlite3_stmt *sqlPrepareStmtfv(sqlite3 *p_sqlite3, const char *pStmtFormat, va_list va)
{
	char *estrStmt = NULL;
	sqlite3_stmt *stmt = NULL;

	estrConcatfv(&estrStmt, pStmtFormat, va);

	stmt = sqlPrepareStmtFromEstr(p_sqlite3, estrStmt);

	estrDestroy(&estrStmt);

	return stmt;
}

sqlite3_stmt *sqlPrepareStmtf(sqlite3 *p_sqlite3, const char *pStmtFormat, ...)
{
	sqlite3_stmt *stmt = NULL;

	VA_START(va, pStmtFormat);
	stmt = sqlPrepareStmtfv(p_sqlite3, pStmtFormat, va);
	VA_END();

	return stmt;
}

void sqlBuildBindTextRaw(SQLBindData ***peaSQLBindData, const char *text)
{
	SQLBindData *data = calloc(1, sizeof(SQLBindData));
	data->type = SQL_BIND_TYPE_TEXT;
	data->_text = StructAllocString(text);
	eaPush(peaSQLBindData, data);
}

void sqlBuildBindTextForInsertOrUpdate(SQLBindData ***peaSQLBindData, const char *text)
{
	// No escaping is needed
	sqlBuildBindTextRaw(peaSQLBindData, text);
}

void sqlBuildBindTextForLike(SQLBindData ***peaSQLBindData, const char *text)
{
	char* estrEscaped = estrCreateFromStr( text );
	sqlEscapeStringForLike( &estrEscaped );
	sqlBuildBindTextRaw(peaSQLBindData, estrEscaped);
	estrDestroy(&estrEscaped);
}

void sqlBuildBindTextForLikef(SQLBindData ***peaSQLBindData, const char *format, ...)
{
	char* estrEscaped = NULL;
	estrGetVarArgs(&estrEscaped, format);
	sqlEscapeStringForLike( &estrEscaped );
	sqlBuildBindTextRaw(peaSQLBindData, estrEscaped);
	estrDestroy(&estrEscaped);
}

void sqlBuildBindTextForMatch(SQLBindData ***peaSQLBindData, const char *text)
{
	char* estrEscaped = estrCreateFromStr( text );
	sqlEscapeStringForMatch( &estrEscaped );
	sqlBuildBindTextRaw(peaSQLBindData, estrEscaped);
	estrDestroy(&estrEscaped);
}

void sqlBuildBindTextForMatchf(SQLBindData ***peaSQLBindData, const char *format, ...)
{
	char* estrEscaped = NULL;
	estrGetVarArgs(&estrEscaped, format);
	sqlEscapeStringForMatch( &estrEscaped );
	sqlBuildBindTextRaw(peaSQLBindData, estrEscaped);
	estrDestroy(&estrEscaped);
}

void sqlBuildBindInteger(SQLBindData ***peaSQLBindData, int value)
{
	SQLBindData *data = calloc(1, sizeof(SQLBindData));
	data->type = SQL_BIND_TYPE_INTEGER;
	data->_int = value;
	eaPush(peaSQLBindData, data);
}

void sqlBuildBindReal(SQLBindData ***peaSQLBindData, float value)
{
	SQLBindData *data = calloc(1, sizeof(SQLBindData));
	data->type = SQL_BIND_TYPE_REAL;
	data->_float = value;
	eaPush(peaSQLBindData, data);
}

bool sqlBindData(sqlite3_stmt *stmt, int index, SQLBindData *pSQLBindData)
{
	switch(pSQLBindData->type)
	{
		case SQL_BIND_TYPE_NULL:
			return sqlBindNull(stmt, index);
		case SQL_BIND_TYPE_TEXT:
			return sqlBindText(stmt, index, pSQLBindData->_text);
		case SQL_BIND_TYPE_INTEGER:
			return sqlBindInteger(stmt, index, pSQLBindData->_int);
		case SQL_BIND_TYPE_REAL:
			return sqlBindReal(stmt, index, pSQLBindData->_float);
	}

	return false;
}

bool sqlBindDataArray(sqlite3_stmt *stmt, SQLBindData **eaSQLBindData)
{
	int index = 1;

	FOR_EACH_IN_EARRAY_FORWARDS(eaSQLBindData, SQLBindData, data)
	{
		if(!sqlBindData(stmt, index++, data))
			return false;
	}
	FOR_EACH_END;

	return true;
}

static void sqlDestroyBindDataCB(SQLBindData *data)
{
	free(data);
}

void sqlDestroyBindData(SQLBindData ***peaSQLBindData)
{
	eaDestroyEx(peaSQLBindData, sqlDestroyBindDataCB);
	if(peaSQLBindData) *peaSQLBindData = NULL;
}

SQLBindData *sqlCopyBindDataCB(const SQLBindData *data)
{
	SQLBindData *copy = calloc(1, sizeof(SQLBindData));
	copy->type = data->type;
	switch(data->type)
	{
		case SQL_BIND_TYPE_TEXT:
			copy->_text = StructAllocString(data->_text);
			break;
		case SQL_BIND_TYPE_INTEGER:
			copy->_int = data->_int;
			break;
		case SQL_BIND_TYPE_REAL:
			copy->_float = data->_float;
			break;
	}
	return copy;
}

void sqlCopyBindData(SQLBindData ***peaSQLBindDataDst, SQLBindData **eaSQLBindDataSrc)
{
	eaCopyEx(&eaSQLBindDataSrc, peaSQLBindDataDst, sqlCopyBindDataCB, sqlDestroyBindDataCB);
}

// index starts at 1 for the first ? in the prepared statement
bool sqlBindNull(sqlite3_stmt *stmt, int index)
{
	if(SQLITE_OK != sqlite3_bind_null(stmt, index))
		return false;
	return true;
}

// index starts at 1 for the first ? in the prepared statement
bool sqlBindText(sqlite3_stmt *stmt, int index, const char *text)
{
	if(SQLITE_OK != sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT))
		return false;
	return true;
}

// index starts at 1 for the first ? in the prepared statement
bool sqlBindInteger(sqlite3_stmt *stmt, int index, int value)
{
	if(SQLITE_OK != sqlite3_bind_int(stmt, index, value))
		return false;
	return true;
}

// index starts at 1 for the first ? in the prepared statement
bool sqlBindReal(sqlite3_stmt *stmt, int index, float value)
{
	if(SQLITE_OK != sqlite3_bind_double(stmt, index, value))
		return false;
	return true;
}

// arguments must be in the order of the ? symbols in the prepared statement
bool sqlBindTextOrdered(sqlite3_stmt *stmt, ...)
{
	va_list va;
	const char *text;
	int index = 1;
	va_start(va, stmt);
	while((text = va_arg(va, const char *)) != 0)
	{
		if(!sqlBindText(stmt, index, text))
			break;
		index++;
	}
	va_end(va);
	return true;
}

// arguments must be in pairs: (match index, text), (match index, text), ..., NULL terminator
bool sqlBindTextInterleaved(sqlite3_stmt *stmt, ...)
{
	va_list va;
	int index;
	va_start(va, stmt);
	while((index = va_arg(va, int)) != 0)
	{
		const char *text = va_arg(va, const char *);
		if(!sqlBindText(stmt, index, text))
			break;
	}
	va_end(va);
	return true;
}

bool sqlExecEx(sqlite3_stmt *stmt, SQLCallback cb, UserData data)
{
	bool bSuccess = false;
	bool bDone = false;

	if(!stmt)
		return false;

	while(!bDone)
	{
		switch(sqlite3_step(stmt))
		{
			case SQLITE_DONE:
				bSuccess = true;
				bDone = true;
				break;
			case SQLITE_BUSY:
				break;
			case SQLITE_ROW:
				// If no callback provided, assume the caller just does not care about the resulting rows
				if(!cb || !cb(stmt, data))
				{
					bSuccess = true;
					bDone = true;
				}
				break;
			default:
				bDone = true;
				break;
		}
	}

	return bSuccess;
}

bool sqlDoStmtFromStrEx(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *strStmt)
{
	return sqlExecEx(sqlPrepareStmtFromStr(p_sqlite3, strStmt), cb, data);
}

bool sqlDoStmtFromEstrEx(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *estrStmt)
{
	return sqlExecEx(sqlPrepareStmtFromEstr(p_sqlite3, estrStmt), cb, data);
}

bool sqlDoStmtExfv(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *pStmtFormat, va_list va)
{
	return sqlExecEx(sqlPrepareStmtfv(p_sqlite3, pStmtFormat, va), cb, data);
}

bool sqlDoStmtExf(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *pStmtFormat, ...)
{
	bool success;

	VA_START(va, pStmtFormat);
	success = sqlDoStmtExfv(p_sqlite3, cb, data, pStmtFormat, va);
	VA_END();

	return success;
}

void sqlEscapeStringForLike(char** pestr)
{
	estrReplaceOccurrences( pestr, "\\", "\\\\" );
	estrReplaceOccurrences( pestr, "%", "\\%" );
	estrReplaceOccurrences( pestr, "_", "\\_" ); 
}

void sqlEscapeStringForMatch(char** pestr)
{
	// MJF Aug/26/2013 -- This logic is intimately tied to internal
	// details of SQLite Full Text Search.  There is no good
	// documentation for what characters have special meaning, this
	// was all gleaned from the source code.
	#if SQLITE_VERSION_NUMBER != 3007016
		#error "SQLite version has changed!  Make sure to update sql.c's escape logic."
	#endif

	// SQLite FTS ignores all non-alnum characters and assigns some of
	// them special meanings.  Convert them all to spaces to avoid the
	// special meanings.
	{
		int it;
		for( it = estrLength( pestr ) - 1; it >= 0; --it ) {
			char* pc = &(*pestr)[ it ];
			if( *pc < 128 && !isalnum( *pc )) {
				*pc = ' ';
			}
		}
	}

	// SQLite FTS has the following four operators with no way to
	// escape them.  However, they all need to be in uppercase to work.
	// "Escape" them by using lowercase.
	//
	// This was deduced by looking at getNextNode(), which does a
	// memcmp against all-caps versions of the keywords.
	estrReplaceOccurrences( pestr, "NEAR", "near" );
	estrReplaceOccurrences( pestr, "AND", "and" );
	estrReplaceOccurrences( pestr, "OR", "or" );
	estrReplaceOccurrences( pestr, "NOT", "not" );
}

void sqlBuildArray(char ***peaStrExpressions, const char* format, ...)
{
	char *estr = NULL;
	estrGetVarArgs(&estr, format);
	eaPush(peaStrExpressions, estr);
}

static int findCompare(const char *pValue1, const char *pValue2)
{
	return 0 == stricmp(pValue1, pValue2);
}

void sqlBuildArrayUnique(char ***peaStrExpressions, const char* value)
{
	int index = eaFindCmp(peaStrExpressions, value, findCompare);
	if(index < 0)
		eaPush(peaStrExpressions, estrCreateFromStr(value));
}

void sqlBuildConjunction(char **pEstrResult, char ***peaStrExpressions)
{
	if(estrLength(pEstrResult))
		estrAppend2(pEstrResult, " ");

	if(eaSize(peaStrExpressions))
	{
		estrAppend2(pEstrResult, "(");
		estrConcatSeparatedStringEarray(pEstrResult, peaStrExpressions, " AND ");
		estrAppend2(pEstrResult, ")");
	}
	else
		estrAppend2(pEstrResult, "1");
}

void sqlBuildDisjunction(char **pEstrResult, char ***peaStrExpressions)
{
	if(estrLength(pEstrResult))
		estrAppend2(pEstrResult, " ");

	if(eaSize(peaStrExpressions))
	{
		estrAppend2(pEstrResult, "(");
		estrConcatSeparatedStringEarray(pEstrResult, peaStrExpressions, " OR ");
		estrAppend2(pEstrResult, ")");
	}
	else
		estrAppend2(pEstrResult, "1");
}

void sqlBuildColumnList(char **pEstrResult, char ***peaStrExpressions)
{
	if(estrLength(pEstrResult))
		estrAppend2(pEstrResult, " ");

	if(eaSize(peaStrExpressions))
		estrConcatSeparatedStringEarray(pEstrResult, peaStrExpressions, ", ");
	else
		estrAppend2(pEstrResult, "*");
}

void sqlBuildTableList(char **pEstrResult, char ***peaStrExpressions)
{
	if(estrLength(pEstrResult))
		estrAppend2(pEstrResult, " ");

	devassert(eaSize(peaStrExpressions));

	estrConcatSeparatedStringEarray(pEstrResult, peaStrExpressions, ", ");
}

void sqlPrintColumns(char **pEstrResult, sqlite3_stmt *stmt)
{
	int count;
	int index;

	if(!stmt)
		return;

	count = sqlite3_column_count(stmt);
	for(index = 0; index < count; index++)
	{
		if(index > 0)
			estrAppend2(pEstrResult, ", ");
		estrConcatf(pEstrResult, "%s", (const char *)sqlite3_column_name(stmt, index));
	}
	estrAppend2(pEstrResult, "\n");

	for(index = 0; index < count; index++)
	{
		if(index > 0)
			estrAppend2(pEstrResult, ", ");
		estrConcatf(pEstrResult, "%s", (const char *)sqlite3_column_text(stmt, index));
	}
	estrAppend2(pEstrResult, "\n");
}

void sqlPrintColumnHeaders(char **pEstrResult, sqlite3_stmt *stmt)
{
	int count;
	int index;

	if(!stmt)
		return;

	count = sqlite3_column_count(stmt);
	for(index = 0; index < count; index++)
	{
		if(index > 0)
			estrAppend2(pEstrResult, ", ");
		estrConcatf(pEstrResult, "%s", (const char *)sqlite3_column_name(stmt, index));
	}
	estrAppend2(pEstrResult, "\n");
}

void sqlPrintRowData(char **pEstrResult, sqlite3_stmt *stmt)
{
	int count;
	int index;

	if(!stmt)
		return;

	count = sqlite3_column_count(stmt);
	for(index = 0; index < count; index++)
	{
		if(index > 0)
			estrAppend2(pEstrResult, ", ");
		estrConcatf(pEstrResult, "%s", (const char *)sqlite3_column_text(stmt, index));
	}
	estrAppend2(pEstrResult, "\n");
}

static void *crypticMemMalloc(int nByte)
{
	return malloc(nByte);
}

static void crypticMemFree(void *pPrior)
{
	free(pPrior);
}

static int crypticMemSize(void *pPrior)
{
	return pPrior ? (int)GetAllocSize(pPrior) : 0;
}

static void *crypticMemRealloc(void *pPrior, int nByte)
{
	return realloc(pPrior, nByte);
}

static int crypticMemRoundup(int n)
{
	return ((n + 7) & ~7);
}

static int crypticMemInit(void *unused)
{
	return SQLITE_OK;
}

static void crypticMemShutdown(void *unused)
{
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void sqlUsesCrypticMemTracker(int bValue)
{
	if(bValue)
	{
		const sqlite3_mem_methods crypticMethods = {
			crypticMemMalloc,
			crypticMemFree,
			crypticMemRealloc,
			crypticMemSize,
			crypticMemRoundup,
			crypticMemInit,
			crypticMemShutdown,
			0
		};
		int rc = sqlite3_config(SQLITE_CONFIG_MALLOC, &crypticMethods); // crypticMethods' pointers are copied to internal storage by sqlite
	}
}
