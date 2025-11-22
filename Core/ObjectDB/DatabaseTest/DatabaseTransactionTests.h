/***************************************************************************



***************************************************************************/

#ifndef DATABASETRANSACTIONTESTS_H_
#define DATABASETRANSACTIONTESTS_H_

#include "LocalTransactionManager.h"

// Unit tests for the database
void SendAddManyContainers(void);
void SendAddOneContainer(void);
void SendClearDatabase(void);
void WaitOnClone(void);
void WaitOnMaster(void);

void ScanLogsForQueries(void);
void ChangeDirectory(void);

#endif