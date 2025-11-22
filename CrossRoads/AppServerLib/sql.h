/***************************************************************************
 * SQL helper utilities
 *
 * This is a SQLite wrapper module that helps reduce the code needed
 * to interface with a SQLite database.
 *
 * Its main goal is to assist in building and executing SQL statements.
 *
 * This module is by no means complete and direct calls to the SQLite API
 * functions are still required (e.g. when opening the database connection).
 ***************************************************************************/
#pragma once

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

// Alerting SQL errors:

void sqlCritAlert_dbg(sqlite3 *p_sqlite3, const char *key, const char *pFileName, int iLineNum);
#define sqlCritAlert(p_sqlite3, key) sqlCritAlert_dbg(p_sqlite3, key, __FILE__, __LINE__)

void sqlWarnAlert_dbg(sqlite3 *p_sqlite3, const char *key, const char *pFileName, int iLineNum);
#define sqlWarnAlert(p_sqlite3, key) sqlWarnAlert_dbg(p_sqlite3, key, __FILE__, __LINE__)

void sqlProgAlert_dbg(sqlite3 *p_sqlite3, const char *key, const char *pFileName, int iLineNum);
#define sqlProgAlert(p_sqlite3, key) sqlProgAlert_dbg(p_sqlite3, key, __FILE__, __LINE__)

// Preparing SQL statements:

// When preparing statements directly from a string, there is a very small performance improvement possible when it is known
// if a string is an EString or not.
sqlite3_stmt *sqlPrepareStmtFromStr(sqlite3 *p_sqlite3, const char *strStmt);
sqlite3_stmt *sqlPrepareStmtFromEstr(sqlite3 *p_sqlite3, const char *estrStmt);

sqlite3_stmt *sqlPrepareStmtfv(sqlite3 *p_sqlite3, const char *pStmtFormat, va_list va);
sqlite3_stmt *sqlPrepareStmtf(sqlite3 *p_sqlite3, const char *pStmtFormat, ...);

// Binding SQL statement parameters denoted by a question-mark (?):

// SQLBindType and SQLBindData encapsulate a binding with type information. An EArray of these can be constructed using
// the sqlBuildBind*** functions. For code that builds SQL SELECT statements procedurally, calls to sqlBuildBind***
// functions should be made in conjunction with each addition of an expression containing a '?' symbol in it.
// Once the SQL statement has been constructed and prepared, use sqlBindDataArray to bind all SQLBindData.

typedef enum SQLBindType
{
	SQL_BIND_TYPE_NULL,
	SQL_BIND_TYPE_TEXT,
	SQL_BIND_TYPE_INTEGER,
	SQL_BIND_TYPE_REAL,
} SQLBindType;

typedef struct SQLBindData
{
	SQLBindType type;
	union
	{
		char *_text;
		int _int;
		float _float;
	};
} SQLBindData;

// For building SQLBindData:
//
// NOTE: All escaping assumes the escape character is a backslash

void sqlBuildBindTextForInsertOrUpdate(SQLBindData ***peaSQLBindData, const char *text);
void sqlBuildBindTextForLike(SQLBindData ***peaSQLBindData, const char *text);
void sqlBuildBindTextForLikef(SQLBindData ***peaSQLBindData, const char *text, ...);
void sqlBuildBindTextForMatch(SQLBindData ***peaSQLBindData, const char *text);
void sqlBuildBindTextForMatchf(SQLBindData ***peaSQLBindData, const char *text, ...);
void sqlBuildBindInteger(SQLBindData ***peaSQLBindData, int value);
void sqlBuildBindReal(SQLBindData ***peaSQLBindData, float value);

// For binding raw text data into SQL.  If you use this, you should call one of the escape functions below.

void sqlBuildBindTextRaw(SQLBindData ***peaSQLBindData, const char *text);

// For binding SQLBindData:

bool sqlBindData(sqlite3_stmt *stmt, int index, SQLBindData *pSQLBindData);
bool sqlBindDataArray(sqlite3_stmt *stmt, SQLBindData **eaSQLBindData);

// Destroying and Copying SQLBindData EArrays:

void sqlDestroyBindData(SQLBindData ***peaSQLBindData);
void sqlCopyBindData(SQLBindData ***peaSQLBindDataDst, SQLBindData **eaSQLBindDataSrc);

// Wrappers for sqlite3_bind_*** functions:

bool sqlBindNull(sqlite3_stmt *stmt, int index);
bool sqlBindText(sqlite3_stmt *stmt, int index, const char *text);
bool sqlBindInteger(sqlite3_stmt *stmt, int index, int value);
bool sqlBindReal(sqlite3_stmt *stmt, int index, float value);

// Arguments must be in the order of the ? symbols in the prepared statement and end in a NULL terminator.
// Example:
//   sqlite3_stmt *stmt = sqlPrepareStmtFromStr(p_sqlite3, "SELECT * FROM table1 WHERE column1 = ? AND column2 = ?");
//   sqlBindOrdered(stmt, "value1", "value2", NULL);
bool sqlBindTextOrdered(sqlite3_stmt *stmt, ...);

// Arguments must be in pairs where the first of the pair is the index of the ?, starting at 1, and the second argument is the text: (index, text), (index, text), ..., NULL terminator
// Example:
//   sqlite3_stmt *stmt = sqlPrepareStmtFromStr(p_sqlite3, "SELECT * FROM table1 WHERE column1 = ? AND column2 = ?");
//   sqlBindInterleaved(stmt, 2, "value2", 1, "value1", NULL);
bool sqlBindTextInterleaved(sqlite3_stmt *stmt, ...);

// Executing SQL statements:

// Callback function type for each row returned from a SQL statement.
//
// return true to continue executing statement, false otherwise
typedef bool (*SQLCallback)(sqlite3_stmt *stmt, UserData pUserData);

bool sqlExecEx(sqlite3_stmt *stmt, SQLCallback cb, UserData data);
#define sqlExec(stmt) sqlExecEx(stmt, NULL, NULL)

// Preparing and Executing SQL statements in one function call. No binding semantics are provided for these helpers. If you require binding semantics,
// you must call the Prepare and the Exec functions individually.

bool sqlDoStmtFromStrEx(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *strStmt);
#define sqlDoStmtFromStr(p_sqlite, strStmt) sqlDoStmtFromStrEx(p_sqlite, NULL, NULL, strStmt)
bool sqlDoStmtFromEstrEx(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *estrStmt);
#define sqlDoStmtFromEstr(p_sqlite, estrStmt) sqlDoStmtFromEstrEx(p_sqlite, NULL, NULL, estrStmt)

bool sqlDoStmtExfv(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *pStmtFormat, va_list va);
#define sqlDoStmtfv(p_sqlite, pStmtFormat, va) sqlDoStmtExfv(p_sqlite, NULL, NULL, pStmtFormat, va)
bool sqlDoStmtExf(sqlite3 *p_sqlite3, SQLCallback cb, UserData data, const char *pStmtFormat, ...);
#define sqlDoStmtf(p_sqlite, pStmtFormat, ...) sqlDoStmtExf(p_sqlite, NULL, NULL, pStmtFormat, ##__VA_ARGS__)

// Building SQL statement strings:

// Does escaping for a LIKE statement.  Assumes the escape character is backslash.
void sqlEscapeStringForLike(char** pestr);

// Does escaping for a MATCH statement.
void sqlEscapeStringForMatch(char** pestr);

// Adds a string on the end of an EArray of strings. The resulting EArray (peaStrExpressions) can be used with sqlBuildConjunction.
void sqlBuildArray(char ***peaStrExpressions, const char* format, ...);
void sqlBuildArrayUnique(char ***peaStrExpressions, const char* value);

// Builds a conjunction of the form "[expr0] AND [expr1] AND [expr2] ..." and concatenates it onto pEstrResult.
// When peaStrExpressions points to an empty EArray, "1" is concatenated to pEstrResult
void sqlBuildConjunction(char **pEstrResult, char ***peaStrExpressions);

// Builds a disjunction of the form "[expr0] OR [expr1] OR [expr2] ..." and concatenates it onto pEstrResult.
// When peaStrExpressions points to an empty EArray, "1" is concatenated to pEstrResult
void sqlBuildDisjunction(char **pEstrResult, char ***peaStrExpressions);

// Builds a list of the form "[expr0], [expr1], [expr2] ..." and concatenates it onto pEstrResult.
// When peaStrExpressions points to an empty EArray, "*" is concatenated to pEstrResult
void sqlBuildColumnList(char **pEstrResult, char ***peaStrExpressions);

// Builds a list of the form "[expr0], [expr1], [expr2] ..." and concatenates it onto pEstrResult.
// It is an error for peaStrExpressions points to an empty EArray.
void sqlBuildTableList(char **pEstrResult, char ***peaStrExpressions);

void sqlPrintColumnHeaders(char **pEstrResult, sqlite3_stmt *stmt);
void sqlPrintRowData(char **pEstrResult, sqlite3_stmt *stmt);
