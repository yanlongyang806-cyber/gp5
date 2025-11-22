#pragma once

typedef struct CBRevision CBRevision;

enum {
	SUSPECTTYPE_CODECHECKIN,
	SUSPECTTYPE_DATACHECKIN,
	SUSPECTTYPE_ERRORBLAMEE,

	SUSPECTTYPE_COUNT
};

AUTO_STRUCT;
typedef struct SingleSuspectList
{
	const char **ppSuspects;
} SingleSuspectList;

AUTO_STRUCT;
typedef struct SuspectList
{
	SingleSuspectList lists[SUSPECTTYPE_COUNT]; AST(INDEX(0, CODECHECKIN) INDEX(1, DATACHECKIN) INDEX(2, ERRORBLAMEE))
} SuspectList;

typedef struct ErrorTrackerEntryList ErrorTrackerEntryList;


void AddPeopleToSuspectList(SuspectList *pCurSuspects, CBRevision *pOldRev, CBRevision *pNewRev, 
	ErrorTrackerEntryList *pErrorList, const char ***pppNewPeople, const char ***pppCurCheckins);

void PutSuspectsIntoSimpleList(SuspectList *pSuspects, const char ***pppList);
void ClearSuspectList(SuspectList *pSuspects);