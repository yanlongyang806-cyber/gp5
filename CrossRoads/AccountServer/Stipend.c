#include "Stipend.h"

#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountServer.h"
#include "AccountTransactionLog.h"
#include "Array.h"
#include "billing.h"
#include "InternalSubs.h"
#include "KeyValues/KeyValues.h"
#include "TransactionOutcomes.h"

#include "AccountServer_h_ast.h"

#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

Array *gpStipendQueue = NULL;

// TODO Add configuration of retry delay, grant interval, grant cooldown, etc.

// Activate stipend debug mode - stipends are granted every day, and retries are much faster
bool gbStipendDebug = false;
AUTO_CMD_INT(gbStipendDebug, StipendDebug) ACMD_COMMANDLINE ACMD_CATEGORY(AccountServer_Test);

// Minimum number of days that must pass between two stipend grants, regardless of billing day changes
U32 giStipendCooldown = 10;
AUTO_CMD_INT(giStipendCooldown, StipendCooldown) ACMD_COMMANDLINE ACMD_CATEGORY(Account_Server_Billing);

static int scheduledStipendCompare(const ScheduledStipendGrant *lhs, const ScheduledStipendGrant *rhs)
{
	if (rhs->uGrantTime > lhs->uGrantTime)
		return 1;
	else if (rhs->uGrantTime == lhs->uGrantTime)
		return 0;
	else
		return -1;
}

static void scheduledStipendIndexUpdate(ScheduledStipendGrant *pEntry, int index)
{
	pEntry->iQueueIndex = index;
}

static void rescheduleStipendGrant(AccountInfo *pAccount, ScheduledStipendGrant *pEntry, const char *pReason, bool bForceOneDay)
{
	U32 uOffset = 0;

	if (gbStipendDebug)
	{
		uOffset = SECONDS(5);
	}
	else if (bForceOneDay || pEntry->uGrantTime - pEntry->uOriginalGrantTime >= DAYS(1))
	{
		uOffset = DAYS(1);
		accountLog(pAccount, "Stipend for %s delayed by a day because: %s", pEntry->pInternalSubName, pReason);
	}
	else
	{
		uOffset = HOURS(1);
	}

	pEntry->uGrantTime += uOffset;
	pqPush(gpStipendQueue, pEntry, scheduledStipendCompare, scheduledStipendIndexUpdate);
}

static void scheduleNextStipendGrant(AccountInfo *pAccount, ScheduledStipendGrant *pEntry)
{
	struct tm localTime = {0};
	U32 uNextGrant = 0;

	if (gbStipendDebug)
	{
		uNextGrant = pEntry->uOriginalGrantTime + DAYS(1);
		goto queue;
	}

	timeMakeLocalTimeStructFromSecondsSince2000(pEntry->uOriginalGrantTime, &localTime);
	
	if (localTime.tm_mon == 11)
	{
		++localTime.tm_year;
		localTime.tm_mon = 0;
	}
	else
	{
		++localTime.tm_mon;
	}

	localTime.tm_mday = pEntry->source.iBillingDay;

	if (localTime.tm_mday > timeDaysInMonth(localTime.tm_mon, localTime.tm_year + 1900))
	{
		localTime.tm_mday = timeDaysInMonth(localTime.tm_mon, localTime.tm_year + 1900);
	}

	uNextGrant = timeGetSecondsSince2000FromLocalTimeStruct(&localTime);

queue:
	pEntry->uOriginalGrantTime = uNextGrant;
	pEntry->uGrantTime = uNextGrant;
	pqPush(gpStipendQueue, pEntry, scheduledStipendCompare, scheduledStipendIndexUpdate);
}

static void freeStipendGrant(SA_PARAM_OP_VALID AccountInfo *pAccount, SA_PRE_NN_VALID SA_POST_FREE ScheduledStipendGrant *pGrant)
{
	if (pAccount)
	{
		eaFindAndRemoveFast(&pAccount->eaScheduledStipends, pGrant);
	}

	free(pGrant->pInternalSubName);
	free(pGrant);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Earecentstipends[]");
enumTransactionOutcome trStipendLogRecent(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pInternalSubName, U32 uGrantTimeSS2000, const char *pCurrency, U32 uAmount)
{
	NOCONST(RecentStipendGrant) *pRecentGrant = eaIndexedGetUsingString(&pAccount->eaRecentStipends, pInternalSubName);

	if (!pRecentGrant)
	{
		pRecentGrant = StructCreateNoConst(parse_RecentStipendGrant);
		estrCopy2(&pRecentGrant->pInternalSubName, pInternalSubName);
		if (!eaIndexedPushUsingStringIfPossible(&pAccount->eaRecentStipends, pInternalSubName, pRecentGrant))
			return TRANSACTION_OUTCOME_FAILURE;
	}

	estrCopy2(&pRecentGrant->pCurrency, pCurrency);
	pRecentGrant->uAmount = uAmount;
	pRecentGrant->uGrantedSS2000 = uGrantTimeSS2000;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Earecentstipends[]");
enumTransactionOutcome trStipendFixupRecent(ATR_ARGS, NOCONST(AccountInfo) *pAccount, NON_CONTAINER const RecentStipendGrant *pRecentGrant, const char *pInternalSubName)
{
	return trStipendLogRecent(ATR_PASS_ARGS, pAccount, pInternalSubName, pRecentGrant->uGrantedSS2000, pRecentGrant->pCurrency, pRecentGrant->uAmount);
}

void fixupRecentStipend(AccountInfo *pAccount, NOCONST(RecentStipendGrant) *pRecentGrant)
{
	AutoTrans_trStipendFixupRecent(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, CONTAINER_RECONST(RecentStipendGrant, pRecentGrant), pRecentGrant->pInternalSubName);
}

RecentStipendGrant *findRecentStipendForAccount(AccountInfo *pAccount, const char *pInternalSubName)
{
	RecentStipendGrant *pRecentGrant = eaIndexedGetUsingString(&pAccount->eaRecentStipends, pInternalSubName);

	return pRecentGrant;
}

static void stipendGrant(SA_PRE_NN_VALID SA_POST_OP_VALID ScheduledStipendGrant *pGrant)
{
	const StipendConfig *pConfig = NULL;
	AccountKeyValueResult eResult = AKV_FAILURE;
	char *pPassword = NULL;
	AccountInfo *pAccount = findAccountByID(pGrant->uAccountID);
	bool bSubscribed = false;
	bool bBilled = false;
	bool bExcluded = false;
	int iNewBillingDay = pGrant->source.iBillingDay;
	RecentStipendGrant *pRecentGrant = NULL;

	if (!pAccount)
	{
		freeStipendGrant(NULL, pGrant);
		return;
	}

	pConfig = billingGetStipendConfig(pGrant->pInternalSubName);

	if (!pConfig)
	{
		freeStipendGrant(pAccount, pGrant);
		return;
	}

	// Find subscription associated with this grant
	if (pGrant->source.bInternal)
	{
		const InternalSubscription *pInternalSub = findInternalSub(pAccount->uID, pGrant->pInternalSubName);
		
		if (pInternalSub)
		{
			bSubscribed = true;
			bBilled = isInternalSubBilledThroughTime(pInternalSub, pGrant->uOriginalGrantTime);
			iNewBillingDay = getInternalSubAnniversaryDay(pInternalSub);
		}
	}
	else
	{
		if (pAccount->pCachedSubscriptionList)
		{
			EARRAY_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, iSub);
			{
				CachedAccountSubscription *pSub = pAccount->pCachedSubscriptionList->ppList[iSub];

				if (pSub && !stricmp(pSub->internalName, pGrant->pInternalSubName))
				{
					bSubscribed = getCachedSubscriptionStatus(pSub) == SUBSCRIPTIONSTATUS_ACTIVE;
					bBilled = isCachedSubBilledThroughTime(pSub, pGrant->uOriginalGrantTime);
					iNewBillingDay = getCachedSubBillingDay(pSub);
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	if (!bSubscribed && !bBilled)
	{
		freeStipendGrant(pAccount, pGrant);
		return;
	}

	if (!bBilled)
	{
		rescheduleStipendGrant(pAccount, pGrant, "subscription was not yet billed", false);
		return;
	}

	if ((pRecentGrant = findRecentStipendForAccount(pAccount, pGrant->pInternalSubName)) && pGrant->uGrantTime - pRecentGrant->uGrantedSS2000 < (gbStipendDebug ? MINUTES(giStipendCooldown) : DAYS(giStipendCooldown)))
	{
		rescheduleStipendGrant(pAccount, pGrant, "another stipend was granted recently", true);
		return;
	}

	// Grant the actual stipend, and make a transaction log for it
	{
		NOCONST(TransactionLogContainer) *pLog = NULL;
		TransactionLogKeyValueChange **eaChanges = NULL;
		const char **eaKeys = NULL;

		eResult = AccountKeyValue_Change("stipend", pAccount, pConfig->currency, pConfig->amount, &pPassword);

		if (eResult != AKV_SUCCESS)
		{
			rescheduleStipendGrant(pAccount, pGrant, "the user's currency was locked", false);
			return;
		}

		eaPush(&eaKeys, pConfig->currency);
		AccountTransactionGetKeyValueChanges(pAccount, eaKeys, &eaChanges);
		eaDestroy(&eaKeys);

		eResult = AccountKeyValue_Commit("stipend", pAccount, pConfig->currency, pPassword);

		if (eResult != AKV_SUCCESS)
		{
			accountLog(pAccount, "Failed to commit stipend for %s! Check the ACCOUNT_KEY_VALUE logs for more details!", pGrant->pInternalSubName);
			freeStipendGrant(pAccount, pGrant);
			AccountTransactionFreeKeyValueChanges(&eaChanges);
			return;
		}

		pLog = AccountTransactionOpenNonTransacted(pAccount->uID, TransLogType_Stipend, "internal", TPROVIDER_AccountServer, NULL, NULL, NULL);
		AccountTransactionRecordKeyValueChangesNonTransacted(pLog, eaChanges, pGrant->pInternalSubName);
		AccountTransactionFreeKeyValueChanges(&eaChanges);
		AccountTransactionFinishNonTransacted(&pLog);
	}

	AutoTrans_trStipendLogRecent(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pGrant->pInternalSubName, pGrant->uGrantTime, pConfig->currency, pConfig->amount);

	pGrant->source.iBillingDay = iNewBillingDay;
	scheduleNextStipendGrant(pAccount, pGrant);
}

void stipendTick(void)
{
	ScheduledStipendGrant **ppGrant = NULL;
 	ScheduledStipendGrant *pGrant = NULL;
	int index = 0;

	if (!gpStipendQueue)
		return;

	ppGrant = (ScheduledStipendGrant**)arrayGetNextItem(gpStipendQueue, &index);

	if (ppGrant)
		pGrant = *ppGrant;

	if (!pGrant)
		return;

	if (pGrant->uGrantTime > timeSecondsSince2000())
		return;

	pqRemove(gpStipendQueue, index, scheduledStipendCompare, scheduledStipendIndexUpdate);
	stipendGrant(pGrant);
}

static U32 calculateInitialGrantTime(AccountInfo *pAccount, const char *pInternalSubName, int iBillingDay)
{
	struct tm grantTime = {0};
	struct tm currentTime = {0};
	RecentStipendGrant *pRecentGrant = findRecentStipendForAccount(pAccount, pInternalSubName);
	int iDaysInCurrentMonth = 0;
	int iDaysInGrantMonth = 0;

	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &currentTime);
	iDaysInCurrentMonth = timeDaysInMonth(currentTime.tm_mon, currentTime.tm_year + 1900);

	if (gbStipendDebug)
	{
		grantTime.tm_mon = currentTime.tm_mon;
		grantTime.tm_year = currentTime.tm_year;
		grantTime.tm_mday = currentTime.tm_mday;
		goto queue;
	}
	else if (!pRecentGrant)
	{
		grantTime.tm_mon = currentTime.tm_mon;
		grantTime.tm_year = currentTime.tm_year;
		iDaysInGrantMonth = iDaysInCurrentMonth;
	}
	else
	{
		timeMakeLocalTimeStructFromSecondsSince2000(pRecentGrant->uGrantedSS2000, &grantTime);

		if (grantTime.tm_year < currentTime.tm_year || grantTime.tm_mon < currentTime.tm_mon || grantTime.tm_mday < iBillingDay)
		{
			grantTime.tm_mon = currentTime.tm_mon;
			grantTime.tm_year = currentTime.tm_year;
			iDaysInGrantMonth = iDaysInCurrentMonth;
		}
		else
		{
			if (currentTime.tm_mon == 11)
			{
				grantTime.tm_mon = 0;
				grantTime.tm_year = currentTime.tm_year + 1;
			}
			else
			{
				grantTime.tm_mon = currentTime.tm_mon + 1;
				grantTime.tm_year = currentTime.tm_year;
			}

			iDaysInGrantMonth = timeDaysInMonth(grantTime.tm_mon, grantTime.tm_year + 1900);
		}
	}

	if (iBillingDay > iDaysInGrantMonth)
	{
		grantTime.tm_mday = iDaysInGrantMonth;
	}
	else
	{
		grantTime.tm_mday = iBillingDay;
	}

queue:
	// Set all grants for 1 PM local time at the earliest - this should be late enough for billing to occur
	grantTime.tm_hour = 13;

	// If the grant is today, and it is after 1 PM, then schedule the grant right now rather than in the past
	if (grantTime.tm_mon == currentTime.tm_mon && grantTime.tm_mday == currentTime.tm_mday && grantTime.tm_year == currentTime.tm_year && currentTime.tm_hour >= 13)
	{
		grantTime.tm_hour = currentTime.tm_hour;
		grantTime.tm_min = currentTime.tm_min;
		grantTime.tm_sec = currentTime.tm_sec;
	}

	return timeGetSecondsSince2000FromLocalTimeStruct(&grantTime);
}

ScheduledStipendGrant *findScheduledStipendForAccount(AccountInfo *pAccount, const char *pInternalSubName)
{
	EARRAY_FOREACH_BEGIN(pAccount->eaScheduledStipends, iStipend);
	{
		ScheduledStipendGrant *pExistingGrant = pAccount->eaScheduledStipends[iStipend];

		if (!stricmp(pExistingGrant->pInternalSubName, pInternalSubName))
		{
			return pExistingGrant;
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

// Returns false only if there was an existing, "better" stipend (e.g. internal > external)
static bool cancelStipend(AccountInfo *pAccount, const char *pInternalSubName, bool bCancelForInternal)
{
	ScheduledStipendGrant *pExistingGrant = findScheduledStipendForAccount(pAccount, pInternalSubName);

	if (!pExistingGrant)
	{
		return true;
	}

	if (pExistingGrant->source.bInternal && !bCancelForInternal)
	{
		// Can only replace an internal grant with another internal grant
		return false;
	}

	pqRemove(gpStipendQueue, pExistingGrant->iQueueIndex, scheduledStipendCompare, scheduledStipendIndexUpdate);
	freeStipendGrant(pAccount, pExistingGrant);
	return true;
}

void scheduleInitialStipendGrant(AccountInfo *pAccount, const char *pInternalSubName, int iBillingDay, bool bInternal)
{
	ScheduledStipendGrant *pGrant = NULL;
	const StipendConfig *pConfig = billingGetStipendConfig(pInternalSubName);

	if (!pConfig)
	{
		return;
	}

	if (!cancelStipend(pAccount, pInternalSubName, bInternal))
	{
		// If it returns false, there was a "better" stipend already queued, so keep it
		return;
	}

	pGrant = callocStruct(ScheduledStipendGrant);
	pGrant->uAccountID = pAccount->uID;
	pGrant->pInternalSubName = strdup(pInternalSubName);
	pGrant->source.iBillingDay = iBillingDay;
	pGrant->source.bInternal = bInternal;

	pGrant->uOriginalGrantTime = calculateInitialGrantTime(pAccount, pInternalSubName, iBillingDay);
	pGrant->uGrantTime = pGrant->uOriginalGrantTime;

	pqPush(gpStipendQueue, pGrant, scheduledStipendCompare, scheduledStipendIndexUpdate);
	eaPush(&pAccount->eaScheduledStipends, pGrant);
}

void updateStipendBillingDay(AccountInfo *pAccount, const char *pInternalSubName, int iBillingDay, bool bInternal)
{
	ScheduledStipendGrant *pExistingGrant = findScheduledStipendForAccount(pAccount, pInternalSubName);

	if (!pExistingGrant)
	{
		return;
	}

	if (pExistingGrant->source.bInternal != bInternal)
	{
		return;
	}

	pExistingGrant->source.iBillingDay = iBillingDay;
}

void initStipends(void)
{
	gpStipendQueue = createArray();
}