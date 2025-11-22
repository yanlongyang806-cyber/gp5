typedef struct AccountInfo AccountInfo;
typedef struct RecentStipendGrant RecentStipendGrant;
typedef struct NOCONST(RecentStipendGrant) NOCONST(RecentStipendGrant);

typedef struct ScheduledStipendSource
{
	bool bInternal;
	int iBillingDay;
} ScheduledStipendSource;

typedef struct ScheduledStipendGrant
{
	U32 uGrantTime;
	U32 uOriginalGrantTime;
	U32 uAccountID;
	char *pInternalSubName;
	int iQueueIndex;

	ScheduledStipendSource source;
} ScheduledStipendGrant;

void scheduleInitialStipendGrant(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pInternalSubName, int iBillingDay, bool bInternal);
void updateStipendBillingDay(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pInternalSubName, int iBillingDay, bool bInternal);

RecentStipendGrant *findRecentStipendForAccount(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pInternalSubName);
ScheduledStipendGrant *findScheduledStipendForAccount(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pInternalSubName);

void fixupRecentStipend(AccountInfo *pAccount, NOCONST(RecentStipendGrant) *pRecentGrant);

void stipendTick(void);
void initStipends(void);