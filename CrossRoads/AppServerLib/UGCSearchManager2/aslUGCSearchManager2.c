#include "aslUGCSearchManager.h"
#include "aslUGCSearchManager2.h"

#include "ServerLib.h"
#include "AppServerLib.h"
#include "ugcprojectcommon.h"
#include "UGCCommon.h"
#include "ReferenceSystem.h"
#include "StringUtil.h"
#include "ResourceManager.h"
#include "AutoStartupSupport.h"
#include "error.h"
#include "sock.h"
#include "objTransactions.h"
#include "ContinuousBuilderSupport.h"
#include "Alerts.h"
#include "logging.h"

#include "sql.h"
#include "../../3rdparty/sqlite/sqlite3.h"

#include "AutoGen\Controller_autogen_RemoteFuncs.h"
#include "AutoGen\AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen\GameServerLib_autogen_RemoteFuncs.h"

static sqlite3 *s_pSQLite3 = NULL;

// Cache of prepared SELECT statements.
static StashTable s_SQLStmtCache = NULL;

// Prepared statements for UGCProject and UGCProjectSeries resource change callback.
static sqlite3_stmt *s_InsertProjectStmt = NULL;
static sqlite3_stmt *s_InsertSeriesStmt = NULL;
static sqlite3_stmt *s_UpdateProjectStmt = NULL;
static sqlite3_stmt *s_UpdateSeriesStmt = NULL;
static sqlite3_stmt *s_DeleteProjectStmt = NULL;
static sqlite3_stmt *s_DeleteSeriesStmt = NULL;

// (Re)Creates the prepared statements for the resource change callback.
static bool aslUGCSearchManager2_PrepareStatements()
{
	S32 *eaiValues = NULL;
	char *estrFormat;

	UGCTagFillAllKeysAndValues(NULL, &eaiValues);

	estrFormat = estrCreateFromStr("INSERT INTO content ("
		"first_project_in_series, id_str, title, author_name, description, location, allegiance, author_id, language, duration, rating_count, rating, last_published, "
		"became_reviewed, allows_featured, featured_copy, featured_start, featured_end, temp_ban_end, banned, min_level, max_level");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrConcatf(&estrFormat, ", tag_%d", value);
	}
	FOR_EACH_END;
	estrAppend2(&estrFormat, ", projectID) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrAppend2(&estrFormat, ", ?");
	}
	FOR_EACH_END;
	estrAppend2(&estrFormat, ", ?)");

	if(s_InsertProjectStmt) sqlite3_finalize(s_InsertProjectStmt);
	s_InsertProjectStmt = sqlPrepareStmtf(s_pSQLite3, estrFormat);
	if(!s_InsertProjectStmt)
	{
		estrDestroy(&estrFormat);
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_PREPARE_INSERT_STATEMENT_FAILURE");
		return false;
	}
	estrDestroy(&estrFormat);

	estrFormat = estrCreateFromStr("UPDATE content SET " \
		"first_project_in_series = ?, id_str = ?, title = ?, author_name = ?, description = ?, location = ?, allegiance = ?, author_id = ?, language = ?, duration = ?, " \
		"rating_count = ?, rating = ?, last_published = ?, became_reviewed = ?, allows_featured = ?, featured_copy = ?, featured_start = ?, featured_end = ?, " \
		"temp_ban_end = ?, banned = ?, min_level = ?, max_level = ?");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrConcatf(&estrFormat, ", tag_%d = ?", value);
	}
	FOR_EACH_END;
	estrAppend2(&estrFormat, " WHERE projectID = ?");

	if(s_UpdateProjectStmt) sqlite3_finalize(s_UpdateProjectStmt);
	s_UpdateProjectStmt = sqlPrepareStmtf(s_pSQLite3, estrFormat);
	if(!s_UpdateProjectStmt)
	{
		estrDestroy(&estrFormat);
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_PREPARE_UPDATE_STATEMENT_FAILURE");
		return false;
	}
	estrDestroy(&estrFormat);

	estrFormat = estrCreateFromStr("INSERT INTO content ("
		"first_project_in_series, id_str, title, author_name, description, location, allegiance, author_id, language, duration, rating_count, rating, last_published, "
		"became_reviewed, allows_featured, featured_copy, featured_start, featured_end, temp_ban_end, banned, min_level, max_level");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrConcatf(&estrFormat, ", tag_%d", value);
	}
	FOR_EACH_END;
	estrAppend2(&estrFormat, ", seriesID) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrAppend2(&estrFormat, ", ?");
	}
	FOR_EACH_END;
	estrAppend2(&estrFormat, ", ?)");

	if(s_InsertSeriesStmt) sqlite3_finalize(s_InsertSeriesStmt);
	s_InsertSeriesStmt = sqlPrepareStmtf(s_pSQLite3, estrFormat);
	if(!s_InsertSeriesStmt)
	{
		estrDestroy(&estrFormat);
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_PREPARE_INSERT_STATEMENT_FAILURE");
		return false;
	}
	estrDestroy(&estrFormat);

	estrFormat = estrCreateFromStr("UPDATE content SET " \
		"first_project_in_series = ?, id_str = ?, title = ?, author_name = ?, description = ?, location = ?, allegiance = ?, author_id = ?, language = ?, duration = ?, " \
		"rating_count = ?, rating = ?, last_published = ?, became_reviewed = ?, allows_featured = ?, featured_copy = ?, featured_start = ?, featured_end = ?, " \
		"temp_ban_end = ?, banned = ?, min_level = ?, max_level = ?");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrConcatf(&estrFormat, ", tag_%d = ?", value);
	}
	FOR_EACH_END;
	estrAppend2(&estrFormat, " WHERE seriesID = ?");

	if(s_UpdateSeriesStmt) sqlite3_finalize(s_UpdateSeriesStmt);
	s_UpdateSeriesStmt = sqlPrepareStmtf(s_pSQLite3, estrFormat);
	if(!s_UpdateSeriesStmt)
	{
		estrDestroy(&estrFormat);
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_PREPARE_UPDATE_STATEMENT_FAILURE");
		return false;
	}
	estrDestroy(&estrFormat);

	if(s_DeleteProjectStmt) sqlite3_finalize(s_DeleteProjectStmt);
	s_DeleteProjectStmt = sqlPrepareStmtf(s_pSQLite3, "DELETE FROM content WHERE projectID = ?");
	if(!s_DeleteProjectStmt)
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_PREPARE_DELETE_STATEMENT_FAILURE");
		return false;
	}

	if(s_DeleteSeriesStmt) sqlite3_finalize(s_DeleteSeriesStmt);
	s_DeleteSeriesStmt = sqlPrepareStmtf(s_pSQLite3, "DELETE FROM content WHERE seriesID = ?");
	if(!s_DeleteSeriesStmt)
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_PREPARE_DELETE_STATEMENT_FAILURE");
		return false;
	}

	return true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
int aslUGCSearchManager2_OptimizeFts()
{
	int result = 1;

	PERFINFO_AUTO_START_FUNC();

	if(!sqlDoStmtFromStr(s_pSQLite3, "INSERT INTO data(data) VALUES('optimize')"))
	{
		sqlProgAlert(s_pSQLite3, "UGC_SEARCH_FTS_OPTIMIZE_FAILURE");
		result = 0;
	}

	PERFINFO_AUTO_STOP();

	return result;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
int aslUGCSearchManager2_Analyze(void)
{
	int result = 1;

	PERFINFO_AUTO_START_FUNC();

	if(!sqlDoStmtFromStr(s_pSQLite3, "ANALYZE"))
	{
		sqlProgAlert(s_pSQLite3, "UGC_SEARCH_ANALYZE_FAILURE");
		result = 0;
	}

	if(result)
	{
		// ANALYZE does not actually invalidate prepared statements, but to take advantage of new statistics, we re-create them.
		result = aslUGCSearchManager2_PrepareStatements();
		stashTableDestroySafe(&s_SQLStmtCache);
	}

	PERFINFO_AUTO_STOP();

	return result;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
int aslUGCSearchManager2_Optimize()
{
	int result = 1;

	PERFINFO_AUTO_START_FUNC();

	if(!aslUGCSearchManager2_Analyze())
		result = 0;

	if(!aslUGCSearchManager2_OptimizeFts())
		result = 0;

	PERFINFO_AUTO_STOP();

	return result;
}

// bind order: first_project_in_series, id_str, title, author_name, description, location, allegiance, author_id, language, duration, rating_count, rating, last_published,
// became_reviewed, allows_featured, featured_copy, featured_start, featured_end, temp_ban_end, banned, min_level, max_level, tag_#, projectID
static bool sqliteExecProjectStmt(sqlite3_stmt *stmt, UGCProject *pProject)
{
	bool result;

	PERFINFO_AUTO_START_FUNC();

	{
		const char **eaKeys = NULL;
		S32 *eaiValues = NULL;
		const UGCProjectVersion *pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
		const UGCProjectSeries *pProjectSeries = pProject->seriesID ? aslUGCSearchManager_GetProjectSeries(pProject->seriesID) : NULL;
		const UGCProjectSeriesVersion *pSeriesVersion = pProjectSeries ? UGCProjectSeries_GetMostRecentPublishedVersion(pProjectSeries) : NULL;
		const UGCProject *pFirstProject = pSeriesVersion ? aslUGCSearchManager_GetFirstPublishedProject(pSeriesVersion) : pProject;
		int index = 1;

		if(SAFE_MEMBER(pVersion, pRestrictions) && eaSize(&pVersion->pRestrictions->eaFactions) > 1)
		{
			AssertOrProgrammerAlert("UGC_SEARCH_MORE_THAN_ONE_FACTION_UNSUPPORTED", "If you get this, then a UGCProject is configured to allow more than one faction to play it. This is not supported. UGC Project id: %u", pProject->id);
			return false;
		}

		sqlite3_reset(stmt);

		if(!sqlBindInteger(stmt, index, pProject == pFirstProject)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(pProject->pIDString))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(UGCProject_GetVersionName(pProject, pVersion)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(pProject->pOwnerAccountName))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(SAFE_MEMBER(pVersion, pDescription)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(SAFE_MEMBER(pVersion, pLocation)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, (SAFE_MEMBER(pVersion, pRestrictions) && eaSize(&pVersion->pRestrictions->eaFactions)) ? NULL_TO_EMPTY(pVersion->pRestrictions->eaFactions[0]->pcFaction) : "")) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProject->iOwnerAccountID)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER(pVersion, eLanguage))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindReal(stmt, index, UGCProject_AverageDurationInMinutes(pProject))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, ugcReviews_GetRatingCount(CONTAINER_NOCONST(UGCProjectReviews, &pProject->ugcReviews)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindReal(stmt, index, UGCProject_RatingForSorting(pProject))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER(pVersion, sLastPublishTimeStamp.iTimestamp))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProject->ugcReviews.iTimeBecameReviewed)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProject->bAuthorAllowsFeatured)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, !!pProject->uUGCFeaturedOrigProjectID)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER2(pProject, pFeatured, iStartTimestamp))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER2(pProject, pFeatured, iEndTimestamp))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProject->ugcReporting.uTemporaryBanExpireTime)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProject->bBanned)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER2(pVersion, pRestrictions, iMinLevel))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER2(pVersion, pRestrictions, iMaxLevel))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;

		UGCTagFillAllKeysAndValues(&eaKeys, &eaiValues);

		FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
		{
			const UGCTagData *pUGCTagData = eaIndexedGetUsingInt(&pProject->ugcReviews.eaTagData, value);
			if(!sqlBindInteger(stmt, index, !!(pUGCTagData && pUGCTagData->iCount))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); eaDestroy(&eaKeys); eaiDestroy(&eaiValues); return false; } index++;
		}
		FOR_EACH_END;

		eaDestroy(&eaKeys);
		eaiDestroy(&eaiValues);

		if(!sqlBindInteger(stmt, index, pProject->id)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_STMT_BIND"); return false; } index++;

		result = sqlExec(stmt);
	}

	PERFINFO_AUTO_STOP();

	return result;
}

// bind order: first_project_in_series, id_str, title, author_name, description, location, allegiance, author_id, language, duration, rating_count, rating, last_published,
// became_reviewed, allows_featured, featured_copy, featured_start, featured_end, temp_ban_end, banned, min_level, max_level, tag_#, seriesID
static bool sqliteExecSeriesStmt(sqlite3_stmt *stmt, UGCProjectSeries *pProjectSeries)
{
	bool result;

	PERFINFO_AUTO_START_FUNC();

	{
		const char **eaKeys = NULL;
		S32 *eaiValues = NULL;
		const UGCProjectSeriesVersion *pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pProjectSeries);
		const UGCProject *pFirstProject = aslUGCSearchManager_GetFirstPublishedProject(pVersion);
		const UGCProjectVersion *pFirstProjectVersion = UGCProject_GetMostRecentPublishedVersion(pFirstProject);
		int index = 1;

		sqlite3_reset(stmt);

		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(pProjectSeries->strIDString))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(SAFE_MEMBER(pVersion, strName)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(pProjectSeries->strOwnerAccountName))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(SAFE_MEMBER(pVersion, strDescription)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, NULL_TO_EMPTY(SAFE_MEMBER(pFirstProjectVersion, pLocation)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindText(stmt, index, "")) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProjectSeries->iOwnerAccountID)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, SAFE_MEMBER(pFirstProjectVersion, eLanguage))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindReal(stmt, index, aslUGCSearchManager_SeriesAverageDurationInMinutes(pVersion))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, ugcReviews_GetRatingCount(CONTAINER_NOCONST(UGCProjectReviews, &pProjectSeries->ugcReviews)))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindReal(stmt, index, UGCProjectSeries_RatingForSorting(pProjectSeries))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProjectSeries->iLastUpdatedTime)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, pProjectSeries->ugcReviews.iTimeBecameReviewed)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;
		if(!sqlBindInteger(stmt, index, 0)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;

		UGCTagFillAllKeysAndValues(&eaKeys, &eaiValues);

		FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
		{
			const UGCTagData *pUGCTagData = eaIndexedGetUsingInt(&pProjectSeries->ugcReviews.eaTagData, value);
			if(!sqlBindInteger(stmt, index, !!(pUGCTagData && pUGCTagData->iCount))) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); eaDestroy(&eaKeys); eaiDestroy(&eaiValues); return false; } index++;
		}
		FOR_EACH_END;

		eaDestroy(&eaKeys);
		eaiDestroy(&eaiValues);

		if(!sqlBindInteger(stmt, index, pProjectSeries->id)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_SERIES_STMT_BIND"); return false; } index++;

		result = sqlExec(stmt);
	}

	PERFINFO_AUTO_STOP();

	return result;
}

static void aslUGCSearchManager2_UpdateProjectSeries(UGCProjectSeries *pProjectSeries)
{
	PERFINFO_AUTO_START_FUNC();

	if(!sqliteExecSeriesStmt(s_UpdateSeriesStmt, pProjectSeries))
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_SERIES_UPDATE_FAILURE");

	PERFINFO_AUTO_STOP();
}

static void aslUGCSearchManager2_UpdateProjectSeriesForProject(UGCProject *pProject)
{
	if(pProject && pProject->seriesID)
	{
		UGCProjectSeries *pProjectSeries = aslUGCSearchManager_GetProjectSeries(pProject->seriesID);
		if(pProjectSeries)
			aslUGCSearchManager2_UpdateProjectSeries(pProjectSeries);
	}
}

static void aslUGCSearchManager2_UpdateProject(UGCProject *pProject);

static void aslUGCSearchManager2_UpdateProjectsForChildNodes(CONST_EARRAY_OF(UGCProjectSeriesNode) eaChildNodes)
{
	FOR_EACH_IN_CONST_EARRAY(eaChildNodes, UGCProjectSeriesNode, node)
	{
		if(node->iProjectID)
		{
			UGCProject *pProject = aslUGCSearchManager_GetProject(node->iProjectID);
			if(pProject)
				aslUGCSearchManager2_UpdateProject(pProject);
		}
		else
			aslUGCSearchManager2_UpdateProjectsForChildNodes(node->eaChildNodes);
	}
	FOR_EACH_END;
}

static void aslUGCSearchManager2_UpdateProjectsForProjectSeries(UGCProjectSeries *pProjectSeries)
{
	if(pProjectSeries)
	{
		const UGCProjectSeriesVersion *pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pProjectSeries);
		if(pVersion)
			aslUGCSearchManager2_UpdateProjectsForChildNodes(pVersion->eaChildNodes);
	}
}

static void aslUGCSearchManager2_AddProject(UGCProject *pProject)
{
	PERFINFO_AUTO_START_FUNC();

	if(!sqliteExecProjectStmt(s_InsertProjectStmt, pProject))
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_ADD_FAILURE");

	PERFINFO_AUTO_STOP();
}

static void aslUGCSearchManager2_UpdateProject(UGCProject *pProject)
{
	PERFINFO_AUTO_START_FUNC();

	if(!sqliteExecProjectStmt(s_UpdateProjectStmt, pProject))
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_UPDATE_FAILURE");

	PERFINFO_AUTO_STOP();
}

static void aslUGCSearchManager2_RemoveProject(UGCProject *pProject)
{
	PERFINFO_AUTO_START_FUNC();

	sqlite3_reset(s_DeleteProjectStmt);

	if(!sqlBindInteger(s_DeleteProjectStmt, 1, pProject->id)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_DELETE_PROJECT_STMT_BIND"); return; }

	if(!sqlExec(s_DeleteProjectStmt)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_DELETE_PROJECT_STMT_EXEC"); return; }

	PERFINFO_AUTO_STOP();
}

typedef struct UGCProjectModificationCache2
{
	bool bSearchable;
	U32 iContainerID;
} UGCProjectModificationCache2;
static UGCProjectModificationCache2 sProjectModificationCache2 = {0};

void aslUGCSearchManager2_ProjectResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProject *pProject, void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();

	switch(eType)
	{
		case RESEVENT_RESOURCE_ADDED:
		{
			// Analyzing every 10000 calls so we are optimized at startup
			static int iCountAdded = 0;

			if(0 == (++iCountAdded) % 10000)
				aslUGCSearchManager2_Optimize();

			if(g_bShowUnpublishedProjectsForDebugging || ProjectIsSearchable(pProject))
				aslUGCSearchManager2_AddProject(pProject);
		}
		break;

		case RESEVENT_RESOURCE_REMOVED:
			if(g_bShowUnpublishedProjectsForDebugging || ProjectIsSearchable(pProject))
				aslUGCSearchManager2_RemoveProject(pProject);
		break;

		case RESEVENT_RESOURCE_PRE_MODIFIED:
		{
			sProjectModificationCache2.iContainerID = pProject->id;
			sProjectModificationCache2.bSearchable = g_bShowUnpublishedProjectsForDebugging || ProjectIsSearchable(pProject);
		}
		break;

		case RESEVENT_RESOURCE_MODIFIED:
		{
			if(sProjectModificationCache2.iContainerID == pProject->id)
			{
				if(g_bShowUnpublishedProjectsForDebugging || ProjectIsSearchable(pProject))
				{
					if(sProjectModificationCache2.bSearchable)
						aslUGCSearchManager2_UpdateProject(pProject);
					else
						aslUGCSearchManager2_AddProject(pProject);
				}
				else
				{
					if(sProjectModificationCache2.bSearchable)
						aslUGCSearchManager2_RemoveProject(pProject);
				}
			}
			else
			{
				TriggerAutoGroupingAlert("UGC_NO_PREMOD_FOR_UGC", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 30, "UGCSearchManager2 got a RESOURCE_MODIFIED without a corresponding RESOURCE_PREMODIFIED for project %u. This shouldn't break anything but is a performance hit", pProject->id);

				aslUGCSearchManager2_RemoveProject(pProject);
				aslUGCSearchManager2_AddProject(pProject);
			}
		}
		break;
	}

	aslUGCSearchManager2_UpdateProjectSeriesForProject(pProject);

	PERFINFO_AUTO_STOP();
}

static void aslUGCSearchManager2_InsertProjectSeries(UGCProjectSeries *pProjectSeries)
{
	PERFINFO_AUTO_START_FUNC();

	if(!sqliteExecSeriesStmt(s_InsertSeriesStmt, pProjectSeries))
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_PROJECT_SERIES_ADD_FAILURE");

	PERFINFO_AUTO_STOP();
}

static void aslUGCSearchManager2_RemoveProjectSeries(UGCProjectSeries *pProjectSeries)
{
	PERFINFO_AUTO_START_FUNC();

	sqlite3_reset(s_DeleteSeriesStmt);

	if(!sqlBindInteger(s_DeleteSeriesStmt, 1, pProjectSeries->id)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_DELETE_SERIES_STMT_BIND"); return; }

	if(!sqlExec(s_DeleteSeriesStmt)) { sqlCritAlert(s_pSQLite3, "UGC_SEARCH_DELETE_SERIES_STMT_EXEC"); return; }

	PERFINFO_AUTO_STOP();
}

void aslUGCSearchManager2_SeriesResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProjectSeries *pProjectSeries, void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();

	switch(eType)
	{
		case RESEVENT_RESOURCE_ADDED:
		{
			// Analyzing every 100 primarily so we are optimized at startup
			static int iCountAdded = 0;

			if(0 == (++iCountAdded) % 100)
				aslUGCSearchManager2_Optimize();

			aslUGCSearchManager2_InsertProjectSeries(pProjectSeries);
		}
		break;

		case RESEVENT_RESOURCE_REMOVED:
			aslUGCSearchManager2_RemoveProjectSeries(pProjectSeries);
		break;

		case RESEVENT_RESOURCE_MODIFIED:
			aslUGCSearchManager2_UpdateProjectSeries(pProjectSeries);
		break;
	}

	aslUGCSearchManager2_UpdateProjectsForProjectSeries(pProjectSeries);

	PERFINFO_AUTO_STOP();
}

static bool debugQuerySQLiteStmtCB(sqlite3_stmt *stmt, char **pEstrResult)
{
	sqlPrintRowData(pEstrResult, stmt);

	return true; // continue
}

static char *s_EstrDebugStmt = NULL;

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void aslUGCSearchManager2_DebugClearStmt()
{
	estrClear(&s_EstrDebugStmt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void aslUGCSearchManager2_DebugStmt(const char *pSelectStmt)
{
	estrAppend2(&s_EstrDebugStmt, pSelectStmt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
int aslUGCSearchManager2_DebugExecStmt()
{
	char *estrResult = NULL;
	sqlite3_stmt *stmt = sqlPrepareStmtFromEstr(s_pSQLite3, s_EstrDebugStmt);
	aslUGCSearchManager2_DebugClearStmt();
	if(!stmt)
	{
		printf("SQLITE DEBUG: %s\n", sqlite3_errmsg(s_pSQLite3));
		return 0;
	}

	sqlPrintColumnHeaders(&estrResult, stmt);

	if(!sqlExecEx(stmt, debugQuerySQLiteStmtCB, &estrResult))
	{
		printf("SQLITE DEBUG: %s\n", sqlite3_errmsg(s_pSQLite3));
		return 0;
	}

	printf("%s", estrResult);

	estrDestroy(&estrResult);

	sqlite3_finalize(stmt);

	return 1;
}

// If non-zero, then slow queries are logged and alerted
static int s_iUGCSearchManagerSlowQueryThresholdInMilliseconds = 100;
AUTO_CMD_INT(s_iUGCSearchManagerSlowQueryThresholdInMilliseconds, iUGCSearchManagerSlowQueryThresholdInMilliseconds) ACMD_AUTO_SETTING(Ugc, UGCSEARCHMANAGER);

bool s_bUGCSearchManager2_PrintTiming = false;
AUTO_CMD_INT(s_bUGCSearchManager2_PrintTiming, UGCSearchmanager2_PrintTiming);

bool s_bUGCSearchManager2_PrintSQL = false;
AUTO_CMD_INT(s_bUGCSearchManager2_PrintSQL, UGCSearchmanager2_PrintSQL);

bool s_bUGCSearchManager2_ExplainSQL = false;
AUTO_CMD_INT(s_bUGCSearchManager2_ExplainSQL, UGCSearchmanager2_ExplainSQL);

int s_iUGCSearchManager2_MaxResults = 100;
AUTO_CMD_INT(s_iUGCSearchManager2_MaxResults, UGCSearchManager2_MaxResults);

static bool contentQuerySQLiteStmtCB(sqlite3_stmt *stmt, UGCSearchResult *pUGCSearchResult)
{
	UGCContentInfo *pUGCContentInfo = StructCreate(parse_UGCContentInfo);
	pUGCContentInfo->iUGCProjectID = sqlite3_column_int(stmt, 0);
	pUGCContentInfo->iUGCProjectSeriesID = sqlite3_column_int(stmt, 1);
	eaPush(&pUGCSearchResult->eaResults, pUGCContentInfo);
	return true; // continue
}

static bool queryContent(char **eaTableList, char **eaWhereExpr, SQLBindData **eaSQLBindData, char **eaWhereExprFTS, SQLBindData **eaSQLBindDataFTS, char *strOrderingTerms, UGCSearchResult *pUGCSearchResult, eSearchType eType, U32 resultLimit)
{
	U64 iStartTime = timerCpuTicks64();

	char *estrTableList = NULL;
	char *estrWhereExpr = NULL;
	char *estrWhereExprFTS = NULL;
	char *estrSelectStmt = NULL;
	sqlite3_stmt *stmt = NULL;
	StashElement element = NULL;

	PERFINFO_AUTO_START_FUNC();

	sqlBuildTableList(&estrTableList, &eaTableList);
	sqlBuildConjunction(&estrWhereExpr, &eaWhereExpr);

	FOR_EACH_IN_EARRAY(eaWhereExprFTS, char, whereFTS)
	{
		if(estrWhereExprFTS)
		{
			char *estrWhereExprFTSInner = estrWhereExprFTS;
			estrWhereExprFTS = NULL;
			estrConcatf(&estrWhereExprFTS, "%s AND data.rowid IN (SELECT data.rowid from data WHERE %s)", whereFTS, estrWhereExprFTSInner);
			estrDestroy(&estrWhereExprFTSInner);
		}
		else
			estrConcatf(&estrWhereExprFTS, "%s", whereFTS);
	}
	FOR_EACH_END;

	if(estrWhereExprFTS)
	{
		if(estrWhereExpr)
			estrAppend2(&estrWhereExpr, " AND ");
		estrConcatf(&estrWhereExpr, "%s", estrWhereExprFTS);
	}

	estrPrintf(&estrSelectStmt, "SELECT content.projectID, content.seriesID FROM %s WHERE %s ORDER BY %s LIMIT %u",
		estrTableList, estrWhereExpr, strOrderingTerms, resultLimit);

	estrDestroy(&estrWhereExpr);
	estrDestroy(&estrWhereExprFTS);
	estrDestroy(&estrTableList);

	if(s_bUGCSearchManager2_PrintSQL)
		printf("\n%s\n", estrSelectStmt);

	if(!s_SQLStmtCache) s_SQLStmtCache = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);

	if(stashAddPointerAndGetElement(s_SQLStmtCache, estrSelectStmt, stmt, /*bOverwriteIfFound=*/false, &element))
	{
		stmt = sqlPrepareStmtFromEstr(s_pSQLite3, estrSelectStmt);
		if(!stmt)
		{
			sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_SQL_PREPARE_FAILURE");
			SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pUGCSearchResult->hErrorMessage);
			estrDestroy(&estrSelectStmt);
			return false;
		}
		stashElementSetPointer(element, stmt);
	}
	else
	{
		stmt = stashElementGetPointer(element);
		sqlite3_reset(stmt);
	}

	if(eaSize(&eaSQLBindData) + eaSize(&eaSQLBindDataFTS) != sqlite3_bind_parameter_count(stmt))
	{
		AssertOrAlertWarning("UGC_SEARCH_SQL_PARAMETER_COUNT_MISMATCH", "%d parameters expected, yet there are %d being bound.", sqlite3_bind_parameter_count(stmt), eaSize(&eaSQLBindData));
		return false;
	}

	{
		int index = 1;
		FOR_EACH_IN_EARRAY_FORWARDS(eaSQLBindData, SQLBindData, data)
		{
			if(!sqlBindData(stmt, index++, data))
			{
				sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_BIND_FAILURE");
				SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pUGCSearchResult->hErrorMessage);
				return false;
			}
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY_FORWARDS(eaSQLBindDataFTS, SQLBindData, data)
		{
			if(!sqlBindData(stmt, index++, data))
			{
				sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_BIND_FAILURE");
				SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pUGCSearchResult->hErrorMessage);
				return false;
			}
		}
		FOR_EACH_END;
	}

	if(!sqlExecEx(stmt, contentQuerySQLiteStmtCB, pUGCSearchResult))
	{
		sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_QUERY_FAILURE");
		SET_HANDLE_FROM_STRING("Message", "UGCSearchError_Generic", pUGCSearchResult->hErrorMessage);
	}

	{
		U64 iTimingMicroSeconds = (timerCpuTicks64() - iStartTime) / (timerCpuSpeed64() / 1000000);
		U64 iTimingMilliSeconds = iTimingMicroSeconds / 1000;
		SearchTypeReport *pReport = &sSearchTypeReports[eType];
		SearchTypeReport *pReportTotal = &sSearchTypeReports[SEARCH_TOTAL];
		bool bSlowQuery = s_iUGCSearchManagerSlowQueryThresholdInMilliseconds ? (iTimingMilliSeconds >= s_iUGCSearchManagerSlowQueryThresholdInMilliseconds) : false;

		if(s_bUGCSearchManager2_ExplainSQL || bSlowQuery)
		{
			char *estrExplain = NULL;
			char *estrResults = NULL;

			estrPrintf(&estrExplain, "EXPLAIN QUERY PLAN %s", estrSelectStmt);
			stmt = sqlPrepareStmtFromEstr(s_pSQLite3, estrExplain);
			if(!stmt)
			{
				sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_FAILURE_TO_EXPLAIN_SLOW_QUERY");
				return true;
			}

			sqlPrintColumnHeaders(&estrResults, stmt);
			if(!sqlExecEx(stmt, debugQuerySQLiteStmtCB, &estrResults))
			{
				sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_FAILURE_TO_EXPLAIN_SLOW_QUERY");
				sqlite3_finalize(stmt);
				return 0;
			}
			sqlite3_finalize(stmt);

			estrDestroy(&estrExplain);

			if(s_bUGCSearchManager2_ExplainSQL)
				printf("%s", estrResults);

			if(bSlowQuery)
			{
				char *estrRecentSlowQuery = NULL;

				estrPrintf(&estrRecentSlowQuery, "%s\n%s", estrSelectStmt, estrResults);

				TriggerAutoGroupingAlert("UGC_SEARCH_SLOW_QUERY", ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER, 60*60, estrRecentSlowQuery);

				if(eaSize(&pReport->eaEstrRecentSlowQueries) >= 10)
					eaSetSizeEString(&pReport->eaEstrRecentSlowQueries, 9);

				eaInsert(&pReport->eaEstrRecentSlowQueries, estrRecentSlowQuery, 0);

				log_printf(LOG_UGC, "SLOW QUERY: %s", estrSelectStmt);
				log_printf(LOG_UGC, "SLOW QUERY EXPLAIN: %s", estrResults);
			}

			estrDestroy(&estrResults);
		}

		if(s_bUGCSearchManager2_PrintTiming)
			printf("sql timing: microsecs = %llu, millisecs = %llu\n", iTimingMicroSeconds, iTimingMilliSeconds);

		pReport->iCount_2++;
		pReport->iTotalMicroSecs_2 += iTimingMicroSeconds;
		pReport->iAverageMicroSecs_2 = pReport->iTotalMicroSecs_2 / pReport->iCount_2;
		pReport->iTotalMilliSecs_2 = pReport->iTotalMicroSecs_2 / 1000;
		pReport->iAverageMilliSecs_2 = pReport->iTotalMilliSecs_2 / pReport->iCount_2;
		pReportTotal->iCount_2++;
		pReportTotal->iTotalMicroSecs_2 += iTimingMicroSeconds;
		pReportTotal->iAverageMicroSecs_2 = pReportTotal->iTotalMicroSecs_2 / pReportTotal->iCount_2;
		pReportTotal->iTotalMilliSecs_2 = pReportTotal->iTotalMicroSecs_2 / 1000;
		pReportTotal->iAverageMilliSecs_2 = pReportTotal->iTotalMilliSecs_2 / pReportTotal->iCount_2;
		if(bSlowQuery)
		{
			pReport->iSlowQueryCount++;
			pReportTotal->iSlowQueryCount++;
		}
	}

	estrDestroy(&estrSelectStmt);

	PERFINFO_AUTO_STOP();

	return true;
}

UGCSearchResult *aslUGCSearchManager2_FindUGCMapsForPlaying(UGCProjectSearchInfo *pSearchInfo)
{
	eSearchType eType = SEARCH_UNKNOWN;

	UGCSearchResult *pUGCSearchResult = NULL;
	char *estrSelectStmt = NULL;
	sqlite3_stmt *stmt = NULL;

	char **eaWhereExpr = NULL;
	char *estrWhereExpr = NULL;
	char **eaTableList = NULL;
	char *estrTableList = NULL;
	char *estrOrderingTerms = NULL;
	char **eaWhereExprFTS = NULL;
	SQLBindData **eaSQLBindData = NULL;
	SQLBindData **eaSQLBindDataFTS = NULL;

	PERFINFO_AUTO_START_FUNC();

	pUGCSearchResult = StructCreate(parse_UGCSearchResult);

	sqlBuildArrayUnique(&eaTableList, "content");

	if(LANGUAGE_DEFAULT != pSearchInfo->eLang)
	{
		sqlBuildArray(&eaWhereExpr, "content.language = ?");
		sqlBuildBindInteger(&eaSQLBindData, pSearchInfo->eLang);
	}

	if(pSearchInfo->iAccessLevel <= ACCESS_UGC && !g_isContinuousBuilder)
	{
		// The plusses (+) tell SQLite not to use those columns to constrain an index. This has helped ensure SQLite uses the
		// indexes in the ORDER BY clause in all of the default searches.
		//
		// http://www.sqlite.org/optoverview.html#multi_index
		//
		// Refactoring for the 2-table approach (see jfinder or andrewa) would prevent us from having to do this. Briefly, the
		// 2-table approach is where we put all AL-only content into one table and all other content in another table and UNION
		// the 2 results. Doing so would allow us to eliminate the following 3 query constraints, but would require us to have
		// periodic callbacks that move content between the tables.
		sqlBuildArray(&eaWhereExpr, "(+content.featured_copy = 0 OR +content.featured_start <= ?)");
		sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000());
		sqlBuildArray(&eaWhereExpr, "(+content.temp_ban_end = 0 OR +content.temp_ban_end <= ?)");
		sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000());
		sqlBuildArray(&eaWhereExpr, "+content.banned = 0");

		if(ugcDefaultsSearchFiltersByPlayerLevel())
		{
			sqlBuildArray(&eaWhereExpr, "(content.min_level = 0 OR content.min_level <= ?)");
			sqlBuildBindInteger(&eaSQLBindData, pSearchInfo->iPlayerLevel);
			sqlBuildArray(&eaWhereExpr, "(content.max_level = 0 OR content.max_level >= ?)");
			sqlBuildBindInteger(&eaSQLBindData, pSearchInfo->iPlayerLevel);
		}

		if(!nullStr(pSearchInfo->pchPlayerAllegiance))
		{
			sqlBuildArray(&eaWhereExpr, "content.allegiance LIKE ? ESCAPE '\\'");
			sqlBuildBindTextForLike(&eaSQLBindData, pSearchInfo->pchPlayerAllegiance);
		}

		if(pSearchInfo->eSpecialType == SPECIALSEARCH_REVIEWER)
			sqlBuildArray(&eaWhereExpr, "content.became_reviewed = 0");
		else
			sqlBuildArray(&eaWhereExpr, "content.became_reviewed > 0");
	}
	else if(pSearchInfo->eSpecialType == SPECIALSEARCH_REVIEWER)
		sqlBuildArray(&eaWhereExpr, "content.became_reviewed = 0");

	if(!nullStr(pSearchInfo->pSimple_Raw))
	{
		if(UTF8GetLength(pSearchInfo->pSimple_Raw) < UGCPROJ_MIN_SIMPLE_SEARCH_STRING_LEN)
		{
			eaDestroyEString(&eaWhereExpr);
			SET_HANDLE_FROM_STRING("Message", "UGCSearchError_SimpleStringTooShort", pUGCSearchResult->hErrorMessage);
			PERFINFO_AUTO_STOP();
			return pUGCSearchResult;
		}
		else
		{
			sqlBuildArray(&eaWhereExprFTS, "data MATCH ?");
			sqlBuildBindTextForMatch(&eaSQLBindDataFTS, pSearchInfo->pSimple_Raw);

			sqlBuildArrayUnique(&eaWhereExpr, "content.rowid = data.rowid");
			sqlBuildArrayUnique(&eaTableList, "data");
		}
	}

	FOR_EACH_IN_EARRAY(pSearchInfo->ppFilters, UGCProjectSearchFilter, pFilter)
	{
		switch(pFilter->eType)
		{
			case UGCFILTER_AVERAGEPLAYTIME:
				if(0 == stricmp(pFilter->pField, "AveragePlayTime"))
				{
					switch(pFilter->eComparison)
					{
						case UGCCOMPARISON_LESSTHAN:
							sqlBuildArray(&eaWhereExpr, "content.duration < ?");
							sqlBuildBindReal(&eaSQLBindData, pFilter->fFloatValue);
							break;

						case UGCCOMPARISON_GREATERTHAN:
							sqlBuildArray(&eaWhereExpr, "content.duration > ?");
							sqlBuildBindReal(&eaSQLBindData, pFilter->fFloatValue);
							break;
					}
				}
			break;

			case UGCFILTER_STRING:
				if(0 == stricmp(pFilter->pField, "ID"))
				{
					if(UTF8GetLength(pFilter->pStrValue) < UGCPROJ_MIN_ID_SEARCH_STRING_LEN)
					{
						eaDestroyEString(&eaWhereExpr);
						SET_HANDLE_FROM_STRING("Message", "UGCSearchError_SimpleStringTooShort", pUGCSearchResult->hErrorMessage);
						PERFINFO_AUTO_STOP();
						return pUGCSearchResult;
					}
					else
					{
						if(UGCCOMPARISON_EXACT == pFilter->eComparison)
						{
							sqlBuildArray(&eaWhereExpr, "content.id_str LIKE ? ESCAPE '\\'");
							sqlBuildBindTextForLike(&eaSQLBindData, pFilter->pStrValue);
						}
						else if(UGCCOMPARISON_CONTAINS == pFilter->eComparison)
						{
							char* estr = estrCreateFromStr( pFilter->pStrValue );
							sqlBuildArray(&eaWhereExpr, "content.id_str LIKE ? ESCAPE '\\'");
							sqlEscapeStringForLike( &estr );

							// surround with wildcards
							estrInsert( &estr, 0, "%", 1 );
							estrConcat( &estr, "%", 1 );

							sqlBuildBindTextRaw( &eaSQLBindData, estr );
							estrDestroy( &estr );
						}
					}
				}
				else if(0 == stricmp(pFilter->pField, "Name"))
				{
					if(UTF8GetLength(pFilter->pStrValue) < UGCPROJ_MIN_NAME_SEARCH_STRING_LEN)
					{
						eaDestroyEString(&eaWhereExpr);
						SET_HANDLE_FROM_STRING("Message", "UGCSearchError_SimpleStringTooShort", pUGCSearchResult->hErrorMessage);
						PERFINFO_AUTO_STOP();
						return pUGCSearchResult;
					}
					else
					{
						if(UGCCOMPARISON_EXACT == pFilter->eComparison)
						{
							sqlBuildArray(&eaWhereExpr, "content.title LIKE ? ESCAPE '\\'");
							sqlBuildBindTextForLike(&eaSQLBindData, pFilter->pStrValue);
						}
						else if(UGCCOMPARISON_CONTAINS == pFilter->eComparison)
						{
							sqlBuildArray(&eaWhereExprFTS, "data.title MATCH ?");
							sqlBuildBindTextForMatch(&eaSQLBindDataFTS, pFilter->pStrValue);

							sqlBuildArrayUnique(&eaWhereExpr, "content.rowid = data.rowid");
							sqlBuildArrayUnique(&eaTableList, "data");
						}
					}
				}
				else if(0 == stricmp(pFilter->pField, "Author"))
				{
					if(UTF8GetLength(pFilter->pStrValue) < UGCPROJ_MIN_AUTHOR_NAME_SEARCH_STRING_LEN)
					{
						eaDestroyEString(&eaWhereExpr);
						SET_HANDLE_FROM_STRING("Message", "UGCSearchError_SimpleStringTooShort", pUGCSearchResult->hErrorMessage);
						PERFINFO_AUTO_STOP();
						return pUGCSearchResult;
					}
					else
					{
						if(UGCCOMPARISON_EXACT == pFilter->eComparison)
						{
							sqlBuildArray(&eaWhereExpr, "content.author_name LIKE ? ESCAPE '\\'");
							sqlBuildBindTextForLike(&eaSQLBindData, pFilter->pStrValue);
						}
						else if(UGCCOMPARISON_CONTAINS == pFilter->eComparison)
						{
							sqlBuildArray(&eaWhereExprFTS, "data.author_name MATCH ?");
							sqlBuildBindTextForMatch(&eaSQLBindDataFTS, pFilter->pStrValue);

							sqlBuildArrayUnique(&eaWhereExpr, "content.rowid = data.rowid");
							sqlBuildArrayUnique(&eaTableList, "data");
						}
					}
				}
				else if(0 == stricmp(pFilter->pField, "Description"))
				{
					if(UTF8GetLength(pFilter->pStrValue) < UGCPROJ_MIN_DESCRIPTION_SEARCH_STRING_LEN)
					{
						eaDestroyEString(&eaWhereExpr);
						SET_HANDLE_FROM_STRING("Message", "UGCSearchError_SimpleStringTooShort", pUGCSearchResult->hErrorMessage);
						PERFINFO_AUTO_STOP();
						return pUGCSearchResult;
					}
					else
					{
						sqlBuildArray(&eaWhereExprFTS, "data.description MATCH ?");
						sqlBuildBindTextForMatch(&eaSQLBindDataFTS, pFilter->pStrValue);

						sqlBuildArrayUnique(&eaWhereExpr, "content.rowid = data.rowid");
						sqlBuildArrayUnique(&eaTableList, "data");
					}
				}
			break;
		}
	}
	FOR_EACH_END;

	if(eaiSize(&pSearchInfo->eaiIncludeAllTags))
	{
		FOR_EACH_IN_EARRAY_INT(pSearchInfo->eaiIncludeAllTags, UGCTag, ugcTag)
		{
			sqlBuildArray(&eaWhereExpr, "tag_%d = ?", ugcTag);
			sqlBuildBindInteger(&eaSQLBindData, 1);
		}
		FOR_EACH_END;
	}

	if(eaiSize(&pSearchInfo->eaiIncludeAnyTags))
	{
		char *estrAny = NULL;
		char **eaEstrAny = NULL;
		FOR_EACH_IN_EARRAY_INT(pSearchInfo->eaiIncludeAnyTags, UGCTag, ugcTag)
		{
			sqlBuildArray(&eaEstrAny, "tag_%d = ?", ugcTag);
			sqlBuildBindInteger(&eaSQLBindData, 1);
		}
		FOR_EACH_END;
		sqlBuildDisjunction(&estrAny, &eaEstrAny);
		sqlBuildArray(&eaWhereExpr, estrAny);
		eaDestroy(&eaEstrAny);
	}

	if(eaiSize(&pSearchInfo->eaiIncludeNoneTags))
	{
		FOR_EACH_IN_EARRAY_INT(pSearchInfo->eaiIncludeNoneTags, UGCTag, ugcTag)
		{
			sqlBuildArray(&eaWhereExpr, "tag_%d = ?", ugcTag);
			sqlBuildBindInteger(&eaSQLBindData, 0);
		}
		FOR_EACH_END;
	}

	switch(pSearchInfo->eSpecialType)
	{
		case SPECIALSEARCH_NONE:
		{
			estrOrderingTerms = estrCreateFromStr("content.rating DESC");

			eType = SEARCH_BROWSE;
		}
		break;

		case SPECIALSEARCH_FEATURED:
		{
			sqlBuildArray(&eaWhereExpr, "content.featured_start > 0 AND content.featured_start <= ?");
			sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000());
			if(!pSearchInfo->bFeaturedIncludeArchives)
			{
				sqlBuildArray(&eaWhereExpr, "(content.featured_end = 0 OR content.featured_end > ?)");
				sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000());
			}

			estrOrderingTerms = estrCreateFromStr("content.featured_start DESC");

			eType = SEARCH_FEATURED;
		}
		break;

		case SPECIALSEARCH_FEATURED_AND_MATCHING:
		{
			// This needs to be 2 queries, Featured without location, then Best with location. We perform the first one here.
			char *estrWhereExprTemp = NULL;
			char **eaWhereExprTemp = NULL;
			SQLBindData **eaSQLBindDataTemp = NULL;

			sqlBuildArray(&eaWhereExpr, "content.seriesID IS NULL"); // no series because the UI will not render them anyway

			eaCopyEStrings(&eaWhereExpr, &eaWhereExprTemp);
			sqlCopyBindData(&eaSQLBindDataTemp, eaSQLBindData);

			// The plusses (+) tell SQLite not to use those columns to constrain an index. This has helped ensure SQLite uses the
			// indexes in the ORDER BY clause in all of the default searches.
			//
			// http://www.sqlite.org/optoverview.html#multi_index
			//
			sqlBuildArray(&eaWhereExprTemp, "+content.featured_start > 0 AND +content.featured_start <= ?");
			sqlBuildBindInteger(&eaSQLBindDataTemp, timeSecondsSince2000());
			if(!pSearchInfo->bFeaturedIncludeArchives)
			{
				sqlBuildArray(&eaWhereExprTemp, "(+content.featured_end = 0 OR +content.featured_end > ?)");
				sqlBuildBindInteger(&eaSQLBindDataTemp, timeSecondsSince2000());
			}

			queryContent(eaTableList, eaWhereExprTemp, eaSQLBindDataTemp, eaWhereExprFTS, eaSQLBindDataFTS, "content.featured_start DESC", pUGCSearchResult, SEARCH_FEATURED_NO_SERIES, s_iUGCSearchManager2_MaxResults);

			eaDestroyEString(&eaWhereExprTemp);
			sqlDestroyBindData(&eaSQLBindDataTemp);

			if(!nullStr(pSearchInfo->pchLocation))
			{
				sqlBuildArray(&eaWhereExpr, "content.location LIKE ? ESCAPE '\\'");
				sqlBuildBindTextForLike(&eaSQLBindData, pSearchInfo->pchLocation);
			}

			sqlBuildArray(&eaWhereExpr, "content.first_project_in_series > 0");

			// This is the negation of the featured search above so we never get duplicates between the 2 searches
			sqlBuildArray(&eaWhereExpr, "(+content.featured_start = 0 OR +content.featured_start > ?)");
			sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000());
			if(!pSearchInfo->bFeaturedIncludeArchives)
			{
				sqlBuildArray(&eaWhereExpr, "+content.featured_end <= ?");
				sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000());
			}

			estrOrderingTerms = estrCreateFromStr("content.rating DESC");

			eType = SEARCH_LOCATION_NO_SERIES;
		}
		break;

		case SPECIALSEARCH_SUBCRIBED:
		{
			char *estrIn = NULL;
			if(pSearchInfo->pSubscription && eaiSize(&pSearchInfo->pSubscription->eaiAuthors))
			{
				char **eaEstrAuthorIDs = NULL;
				FOR_EACH_IN_EARRAY_INT(pSearchInfo->pSubscription->eaiAuthors, ContainerID, author_id)
				{
					sqlBuildArray(&eaEstrAuthorIDs, "?");
					sqlBuildBindInteger(&eaSQLBindData, author_id);
				}
				FOR_EACH_END;
				estrConcatSeparatedStringEarray(&estrIn, &eaEstrAuthorIDs, ", ");
				eaDestroyEString(&eaEstrAuthorIDs);
			}
			else
			{
				// do not even bother with query because player has no author subscriptions
				eaDestroyEString(&eaWhereExpr);
				return pUGCSearchResult;
			}

			sqlBuildArray(&eaWhereExpr, "content.author_id IN (%s)", estrIn);
			estrDestroy(&estrIn);

			estrOrderingTerms = estrCreateFromStr("content.last_published DESC");

			eType = SEARCH_SUBSCRIBED;
		}
		break;

		case SPECIALSEARCH_NEW:
		{
			sqlBuildArray(&eaWhereExpr, "content.became_reviewed <= ?");
			sqlBuildBindInteger(&eaSQLBindData, timeSecondsSince2000() + g_iMaximumDaysOldForNewContentList * (24 * 60 * 60));

			estrOrderingTerms = estrCreateFromStr("content.rating DESC");

			eType = SEARCH_NEW;
		}
		break;

		case SPECIALSEARCH_REVIEWER:
		{
			estrOrderingTerms = estrCreateFromStr("content.rating_count DESC");

			eType = SEARCH_NEW;
		}
		break;

		default:
		{
			estrOrderingTerms = estrCreateFromStr("content.rating DESC");

			eType = SEARCH_UNKNOWN;

			AssertOrProgrammerAlert("UGC_SEARCH_UNKNOWN_TYPE", "Unknown search type passed to UGCSearcManager2: %d", pSearchInfo->eSpecialType);
		}
	}

	queryContent(eaTableList, eaWhereExpr, eaSQLBindData, eaWhereExprFTS, eaSQLBindDataFTS, estrOrderingTerms, pUGCSearchResult, eType, s_iUGCSearchManager2_MaxResults);

	eaDestroyEString(&eaWhereExpr);
	eaDestroyEString(&eaWhereExprFTS);
	eaDestroyEString(&eaTableList);
	sqlDestroyBindData(&eaSQLBindData);
	sqlDestroyBindData(&eaSQLBindDataFTS);
	estrDestroy(&estrOrderingTerms);

	PERFINFO_AUTO_STOP();

	return pUGCSearchResult;
}

static void aslUGCSearchManager2_SearchConfigSetup(UGCSearchConfig *pUGCSearchConfig)
{
	if(pUGCSearchConfig && s_pSQLite3)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCSearchConfig->eaIndexes, UGCSearchIndex, pUGCSearchIndex)
		{
			if(!sqlDoStmtf(s_pSQLite3, "CREATE INDEX idx_%s ON content(%s)", pUGCSearchIndex->strName, pUGCSearchIndex->strCommaSeparatedColumns))
				sqlWarnAlert(s_pSQLite3, "BAD_UGC_SEARCH_CONFIG_INDEX");
		}
		FOR_EACH_END;

		aslUGCSearchManager2_Analyze();
	}
}

static void aslUGCSearchManager2_SearchConfigTeardown(UGCSearchConfig *pUGCSearchConfig)
{
	if(pUGCSearchConfig && s_pSQLite3)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCSearchConfig->eaIndexes, UGCSearchIndex, pUGCSearchIndex)
		{
			if(!sqlDoStmtf(s_pSQLite3, "DROP INDEX idx_%s", pUGCSearchIndex->strName))
				sqlWarnAlert(s_pSQLite3, "BAD_UGC_SEARCH_CONFIG_INDEX");
		}
		FOR_EACH_END;
	}
}

int aslUGCSearchManager2_Init(void)
{
	S32 *eaiValues = NULL;
	char *estrCreateTableSQL;

	UGCTagFillAllKeysAndValues(NULL, &eaiValues);

	estrCreateTableSQL = estrCreateFromStr("CREATE TABLE content " \
"(projectID INTEGER UNIQUE, seriesID INTEGER UNIQUE, first_project_in_series INTEGER, id_str TEXT, title TEXT, author_name TEXT, description TEXT, location TEXT, " \
"allegiance TEXT, author_id INTEGER NOT NULL, language INTEGER NOT NULL, duration FLOAT, rating_count INTEGER, rating FLOAT, last_published INTEGER, " \
"became_reviewed INTEGER, allows_featured INTEGER, featured_copy INTEGER, featured_start INTEGER, featured_end INTEGER, temp_ban_end INTEGER, banned INTEGER, " \
"min_level INTEGER, max_level INTEGER");
	FOR_EACH_IN_EARRAY_INT(eaiValues, S32, value)
	{
		estrConcatf(&estrCreateTableSQL, ", tag_%d", value);
	}
	FOR_EACH_END;
	estrAppend2(&estrCreateTableSQL, ");");

	if(sqlite3_open(":memory:", &s_pSQLite3) != SQLITE_OK)
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}

	if(!sqlDoStmtFromEstr(s_pSQLite3, estrCreateTableSQL))
	{
		estrDestroy(&estrCreateTableSQL);
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}
	estrDestroy(&estrCreateTableSQL);

	if(!sqlDoStmtFromStr(s_pSQLite3, "CREATE UNIQUE INDEX project_index ON content (projectID)"))
	{
		sqlProgAlert(s_pSQLite3, "UGC_SEARCH_CREATE_PROJECT_INDEX_FAILURE");
		return 0;
	}

	if(!sqlDoStmtFromStr(s_pSQLite3, "CREATE UNIQUE INDEX series_index ON content (seriesID)"))
	{
		sqlProgAlert(s_pSQLite3, "UGC_SEARCH_CREATE_SERIES_INDEX_FAILURE");
		return 0;
	}

	if(!sqlDoStmtFromStr(s_pSQLite3, "CREATE VIRTUAL TABLE data USING fts4(content='content', author_name, title, description, tokenize='unicode61')"))
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}

	if(!sqlDoStmtFromStr(s_pSQLite3,
			"CREATE TRIGGER content_bu BEFORE UPDATE ON content BEGIN\n" \
				"DELETE FROM data WHERE docid=old.rowid;\n" \
			"END"
		))
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}

	if(!sqlDoStmtFromStr(s_pSQLite3,
			"CREATE TRIGGER content_bd BEFORE DELETE ON content BEGIN\n" \
				"DELETE FROM data WHERE docid=old.rowid;\n" \
			"END"
		))
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}

	if(!sqlDoStmtf(s_pSQLite3,
			"CREATE TRIGGER content_au AFTER UPDATE ON content BEGIN\n" \
				"INSERT INTO data(docid, title, author_name, description) VALUES(new.rowid, new.title, new.author_name, new.description);\n"
			"END"
		))
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}

	if(!sqlDoStmtFromStr(s_pSQLite3,
			"CREATE TRIGGER content_ai AFTER INSERT ON content BEGIN\n" \
				"INSERT INTO data(docid, title, author_name, description) VALUES(new.rowid, new.title, new.author_name, new.description);\n" \
			"END"
		))
	{
		sqlCritAlert(s_pSQLite3, "UGC_SEARCH_STARTUP_FAILURE");
		return 0;
	}

	// Load and process the indexes in the config. This will do it for dev and prod modes.
	aslUGCSearchManager2_SearchConfigSetup(ugcGetSearchConfig());

	// Specify setup and teardown functions for when the source text file for UGCSearchConfig hot reloads. Only does anything in development.
	ugcSetSearchConfigSetupFunction(aslUGCSearchManager2_SearchConfigSetup);
	ugcSetSearchConfigTeardownFunction(aslUGCSearchManager2_SearchConfigTeardown);

	if(!aslUGCSearchManager2_PrepareStatements())
		return 0;

	return 1;
}

static bool HourIsBetween(U32 now, U32 start, U32 end)
{
	if(start <= end)
		return now >= start && now <= end;
	else
		return now <= end || now >= start;
}

S32 aslUGCSearchManager2_OncePerFrame(F32 fElapsed)
{
	static U32 iLastAnalyzeDay = 0;

	SYSTEMTIME	t;
	U32 iTime = timeSecondsSince2000();
	U32 iToday;
	U32 iCurrentHour;

	timerLocalSystemTimeFromSecondsSince2000(&t, iTime);
	iCurrentHour = t.wHour;
	t.wHour = t.wMinute = t.wSecond = t.wMilliseconds = 0;

	iToday = timerSecondsSince2000FromLocalSystemTime(&t);
	if(0 == iLastAnalyzeDay || iToday >= iLastAnalyzeDay + 60*60*24)
	{
		if(HourIsBetween(iCurrentHour, /*3:00am=*/3, /*5:00am=*/5))
		{
			iLastAnalyzeDay = iToday;
			if(!aslUGCSearchManager2_Optimize())
				return 0;
		}
	}

	return 1;
}

static bool allowsFeaturedQuerySQLiteStmtCB(sqlite3_stmt *stmt, UGCIDList *id_list)
{
	eaiPush(&id_list->eaProjectIDs, sqlite3_column_int(stmt, 0));
	return true; // continue
}

void aslUGCSearchManager2_GetAuthorAllowsFeaturedList(UGCIDList *id_list, bool bIncludeAlreadyFeatured)
{
	PERFINFO_AUTO_START_FUNC();

	if(!sqlDoStmtExf(s_pSQLite3, allowsFeaturedQuerySQLiteStmtCB, id_list,
			"SELECT projectID FROM content WHERE (content.temp_ban_end = 0 OR content.temp_ban_end <= %u) AND content.banned = 0 AND content.allows_featured = 1%s",
			timeSecondsSince2000(), !bIncludeAlreadyFeatured ? " AND featured_start = 0" : ""))
		sqlWarnAlert(s_pSQLite3, "UGC_SEARCH_QUERY_FAILURE");

	PERFINFO_AUTO_STOP();
}
