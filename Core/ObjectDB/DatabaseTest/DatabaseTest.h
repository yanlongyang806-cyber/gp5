/***************************************************************************



***************************************************************************/

#ifndef DATABASETEST_H_
#define DATABASETEST_H_


#include "objContainer.h"
#include "objPath.h"

typedef struct DatabaseTestState
{
	int testingEnabled;
	int totalUsers;
	int concurrentUsers;
	int totalTransactions;
	char testDir[MAX_PATH];
} DatabaseTestState;

extern DatabaseTestState gDBTestState;

extern StringQueryList * gDBQueryList;

#endif