
#define MAX_COMMENT_LINE 1024

extern char *requestPath;
extern char *masterRequestListPath;
extern char *masterRequestLockPath;

enum
{
	RQ_ACCEPTED	= (1<<0),
	RQ_DENIED	= (1<<1),
	RQ_OLD		= (1<<2),
};

int LockMasterRequestList();
int UnlockMasterRequestList();