/***************************************************************************



***************************************************************************/

#ifndef DBCLONE_H_
#define DBCLONE_H_

// Code needed by the objectDB running as a dedicated clone server

enum
{
	DBTOCLONE_HANDSHAKE = COMM_MAX_CMD,
	DBTOCLONE_LOGTRANSACTION,
	DBTOCLONE_REOPENOFFLINEHOG,
	DBTOCLONE_DEFRAGOFFLINEHOG,
};

int dbCloneFlush(void);
extern U32 gRetryWaitTimeoutMinutes;
extern U32 gTimeOfLastCloneConnectAttempt;
extern bool gRetryCloneConnection;

#endif