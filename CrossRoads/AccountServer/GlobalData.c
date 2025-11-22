#include "AccountServer.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "GlobalData.h"
#include "GlobalData_c_ast.h"
#include "Money.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "ProductKey.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "TimedCallback.h"

/************************************************************************/
/* Macros (meta programming ftl)                                        */
/************************************************************************/

// I REMOVED THEM ALL!!! HAHAHAHAHA, DELICIOUS FREEDOM
//	-- VAS 031513


/************************************************************************/
/* Types                                                                */
/************************************************************************/

AST_PREFIX(PERSIST)

// Primary container that is the root of globally stored data
AUTO_STRUCT AST_CONTAINER AST_IGNORE(PurchaseStatsHourlyCurrentStart) AST_IGNORE(PurchaseStatsHourlyPreviousStart);
typedef struct ASGlobalData
{
	const U32 ID;								AST(KEY)
	const U32 LastNightlyWalk;									// The last time, in seconds since 2000, that the nightly crawl was started

	// Statistics gathering.
	GlobalAccountStatsContainer GlobalStats;					// General global AccountServer statistics
	CONST_EARRAY_OF(CurrencyPurchasesContainer)
		PurchaseStats;											// Purchase tracking statistics
	CONST_EARRAY_OF(CurrencyPurchasesContainer)
		PurchaseStatsHourlyCurrent;								// Purchase tracking statistics, for this hour
	CONST_EARRAY_OF(CurrencyPurchasesContainer)
		PurchaseStatsHourlyPrevious;							// Purchase tracking statistics, for the previous hour
	CONST_EARRAY_OF(PlayTimeContainer) PlayTime;				// Aggregated play time
	CONST_EARRAY_OF(TotalPointsBalanceContainer)
		TotalAccountsPointsBalance;								// Accounts with points and total balance, by points type
	CONST_EARRAY_OF(TransactionCodesContainer)
		TransactionCodes;										// Distribution of transaction response codes
	CONST_EARRAY_OF(U64Container) KeyActivations;				// Total key activations
	CONST_EARRAY_OF(U64Container) KeyActivationsDailyCurrent;	// Key activations today
	CONST_EARRAY_OF(U64Container) KeyActivationsDailyPrevious;	// Key activations yesterday
	CONST_EARRAY_OF(LockedKey) LockedKeys;						// Locked keys

	CONST_EARRAY_OF(EncryptionKeyVerificationString) EncryptionKeyVerificationStrings; //for verifying that encryption keys have
		//loaded properly

	const enumPasswordVersion PasswordVersion;					//how passwords are stored

	const U32 PasswordBackupCreationTime;						//set this whenever we create passwords, clear it when
																//we destroy them, so that we can issue reminders


	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerGlobalData $FIELD(ID) $STRING(Transaction String)")
} ASGlobalData;

AST_PREFIX()

/************************************************************************/
/* Globals                                                              */
/************************************************************************/

// This will be true once it is okay to pull or push data to the global data store
static bool gbInitialized = false;

// This will be true iff there are pending non-transacted changes to the global data
static bool gbModified = false;

// Fast indices for substructures
static StashTable stPlayTime = NULL;
static StashTable stPurchaseStats = NULL;
static StashTable stPurchaseStatsHourly = NULL;
static StashTable stTransactionCodes = NULL;
static StashTable stPointsTypes = NULL;

/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Create a StashTable key for PlayTime.
static void PlayTimeStashKey(SA_PARAM_NN_VALID char **ppKey, SA_PARAM_OP_STR const char *pLocale,
							 SA_PARAM_NN_STR const char *pProduct, SA_PARAM_NN_STR const char *pShard)
{

	// Locale is optional.
	if (!pLocale)
		pLocale = "";
	devassert(pLocale && pProduct && pShard);

	// Format key.
	estrPrintf(ppKey, "%s,%s,%s", pLocale, pProduct, pShard);
}

// Create a StashTable key for PurchaseStats.
static void PurchaseStatsStashKey(SA_PARAM_NN_VALID char **ppKey, SA_PARAM_NN_VALID const char *pCurrency,
								  SA_PARAM_OP_STR const char *pLocale, SA_PARAM_NN_VALID const char *pProductName)
{
	// Locale is optional.
	if (!pLocale)
		pLocale = "";
	devassert(pCurrency && pLocale && pProductName);

	// Format key.
	estrPrintf(ppKey, "%s,%s,%s", pCurrency, pLocale, pProductName);
}

// Create a StashTable key for TransactionCodes.
static void TransactionCodesStashKey(SA_PARAM_NN_VALID char **ppKey, SA_PARAM_OP_STR const char *pLocale, SA_PARAM_NN_STR const char *pCurrency,
									 SA_PARAM_OP_STR const char *pAuthCode, SA_PARAM_OP_STR const char *pAvsCode, SA_PARAM_OP_STR const char *pCvnCode)
{
	devassert(pCurrency);

	// Format key.
	estrPrintf(ppKey, "%s,%s,%s,%s,%s", NULL_TO_EMPTY(pLocale), pCurrency, NULL_TO_EMPTY(pAuthCode), NULL_TO_EMPTY(pAvsCode), NULL_TO_EMPTY(pCvnCode));
}

// Initialize StashTable for play time if necessary.
static void InitPlayTimeIfNecessary(void)
{
	if (!stPlayTime)
	{
		CONST_EARRAY_OF(PlayTimeContainer) playTime = asgGetPlayTime();
		char *key = NULL;
		estrStackCreate(&key);
		stPlayTime = stashTableCreateWithStringKeys(1009, StashDeepCopyKeys_NeverRelease);
		EARRAY_CONST_FOREACH_BEGIN(playTime, i, n);
			PlayTimeStashKey(&key, playTime[i]->Locale, playTime[i]->Product, playTime[i]->Shard);
			stashAddInt(stPlayTime, key, i, true);
		EARRAY_FOREACH_END;
		estrDestroy(&key);
	}
}

// Initialize StashTable for purchases if necessary.
static void InitPurchaseStatsIfNecessary(void)
{
	if (!stPurchaseStats)
	{
		CONST_EARRAY_OF(CurrencyPurchasesContainer) purchases = asgGetPurchaseStats();
		char *currency = NULL;
		char *key = NULL;
		estrStackCreate(&currency);
		estrStackCreate(&key);
		stPurchaseStats = stashTableCreateWithStringKeys(1009, StashDeepCopyKeys_NeverRelease);
		EARRAY_CONST_FOREACH_BEGIN(purchases, i, n);
			estrCurrency(&currency, moneyContainerToMoneyConst(&purchases[i]->TotalPurchases));
			PurchaseStatsStashKey(&key, currency, purchases[i]->Locale, purchases[i]->Product);
			stashAddInt(stPurchaseStats, key, i, true);
		EARRAY_FOREACH_END;
		estrDestroy(&currency);
		estrDestroy(&key);
	}
}

// Initialize StashTable for hourly purchases if necessary.
static void InitPurchaseStatsHourlyIfNecessary(void)
{
	if (!stPurchaseStatsHourly)
		stPurchaseStatsHourly = stashTableCreateWithStringKeys(1009, StashDeepCopyKeys_NeverRelease);
}

// Initialize StashTable for purchases if necessary.
static void InitTransactionCodesIfNecessary(void)
{
	if (!stTransactionCodes)
	{
		CONST_EARRAY_OF(TransactionCodesContainer) combinations = asgGetTransactionCodes();
		char *key = NULL;
		estrStackCreate(&key);
		stTransactionCodes = stashTableCreateWithStringKeys(1009, StashDeepCopyKeys_NeverRelease);
		EARRAY_CONST_FOREACH_BEGIN(combinations, i, n);
			TransactionCodesStashKey(&key, combinations[i]->Locale, combinations[i]->Currency, combinations[i]->AuthCode,
				combinations[i]->AvsCode, combinations[i]->CvnCode);
			stashAddInt(stTransactionCodes, key, i, true);
		EARRAY_FOREACH_END;
		estrDestroy(&key);
	}
}

// Initialize StashTable for points balances if necessary.
static void InitPointsTypesIfNecessary(void)
{
	if (!stPointsTypes)
	{
		CONST_EARRAY_OF(TotalPointsBalanceContainer) balances = asgGetTotalAccountsPointsBalance();
		char *key = NULL;
		estrStackCreate(&key);
		stPointsTypes = stashTableCreateWithStringKeys(13, StashDeepCopyKeys_NeverRelease);
		EARRAY_CONST_FOREACH_BEGIN(balances, i, n);
			estrCurrency(&key, moneyContainerToMoneyConst(&balances[i]->TotalBalances));
			stashAddInt(stPointsTypes, key, i, true);
		EARRAY_FOREACH_END;
		estrDestroy(&key);
	}
}

// Add information for a purchase to the PurchaseStatsHourly array.
static void asgAddPurchaseStatsHourlyItem(const char *pCurrency, const char *pLocale, const char *pProduct, const Money *pAmount)
{
	char *key = NULL;
	int index;
	bool success;

	// Locale is optional.
	if (!pLocale)
		pLocale = "";

	// Look up the StashTable entry.
	InitPurchaseStatsHourlyIfNecessary();
	PurchaseStatsStashKey(&key, pCurrency, pLocale, pProduct);
	success = stashFindInt(stPurchaseStatsHourly, key, &index);
	if (!success)
		index = -1;

	// Run transaction.
	AutoTrans_trAddPurchaseStatsHourlyItem(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pCurrency, pLocale, pProduct, pAmount, index);

	// Update StashTable if necessary.
	if (index == -1)
	{
		CONST_EARRAY_OF(CurrencyPurchasesContainer) purchases = asgGetPurchaseStatsHourlyCurrent();
		char *currency = NULL;
		estrStackCreate(&currency);
		EARRAY_FOREACH_REVERSE_BEGIN(purchases, i);
			const CurrencyPurchasesContainer *purchase = purchases[i];
			estrCurrency(&currency, moneyContainerToMoneyConst(&purchase->TotalPurchases));
			if (!stricmp(currency, pCurrency)
				&& !stricmp_safe(purchase->Locale, pLocale)
				&& !stricmp(purchase->Product, pProduct))
			{
				index = i;
				break;
			}
		EARRAY_FOREACH_END;
		devassert(index >= 0);
		stashAddInt(stPurchaseStatsHourly, key, index, true);
		estrDestroy(&currency);
	}
}

static ASGlobalData *GetASGlobalData(void)
{
	return objGetContainerData(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1);
}

static ASGlobalData *GetASGlobalDataForNonTransactedModify(void)
{
	static ASGlobalData *nonTransactedModifyData = NULL;

	if (!nonTransactedModifyData)
	{
		ASGlobalData *globalData = GetASGlobalData();

		if (!globalData)
		{
			return NULL;
		}

		nonTransactedModifyData = StructClone(parse_ASGlobalData, globalData);
	}

	return nonTransactedModifyData;
}

/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Initialize the schema
void asgRegisterSchema(void)
{
	// Register the schema
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, parse_ASGlobalData, NULL, NULL, NULL, NULL, NULL);
}

static void FlushASGlobalDataNonTransactedChanges(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
{
	static char *pDiffStr = NULL;
	static char *pUpdateCmd = NULL;
	ASGlobalData *pModifyCopy = NULL, *pBackupCopy = NULL;

	if (!gbModified || !gbInitialized)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	pBackupCopy = GetASGlobalData();
	pModifyCopy = GetASGlobalDataForNonTransactedModify();

	if (!pUpdateCmd)
	{
		estrPrintf(&pUpdateCmd, "dbUpdateContainer %u %u ", GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1);
	}

	assert(pBackupCopy && pModifyCopy);
	estrCopy(&pDiffStr, &pUpdateCmd);

	// Ugh, yes, we're using this stupid "invert exclude flags" thing here again. This is because we need to diff some NO_TRANSACT fields that are subfields of transacted structs.
	// I am very, very sorry that this has to be done. - VAS 031413
	StructWriteTextDiff(&pDiffStr, parse_ASGlobalData, pBackupCopy, pModifyCopy, NULL, TOK_PERSIST, TOK_NO_TRANSACT, TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT);

	// The act of applying this update string will update the main copy of the container, bringing it in line with the modify copy.
	accountdbHandleDatabaseUpdateStringEx(pDiffStr, 0, 0, false);
	estrClear(&pDiffStr);

	gbModified = false;
	PERFINFO_AUTO_STOP();
}

// Initialize the account server global data
void asgInitialize(void)
{
	if (objGetContainer(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1))
	{
		gbInitialized = true;
	}
	else
	{
		TransactionRequest *request = NULL;
		TransactionReturnVal retVal = {0};

		// Make sure the container exists in the ObjectDB
		request = objCreateTransactionRequest();
		objAddToTransactionRequestf(request, objServerType(), 0, NULL, 
			"VerifyContainer containerIDVar %s 1",
			GlobalTypeToName(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA));
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, &retVal, "EnsureASGContainerExists", request);
		objDestroyTransactionRequest(request);

		if (retVal.eOutcome != TRANSACTION_OUTCOME_SUCCESS)
			AssertOrAlert("ACCOUNTSERVER_GLOBALDATA_CONTAINER_FAILURE", "Could not ensure the account server global data container exists.");
		else
			gbInitialized = true;

		ReleaseReturnValData(objLocalManager(), &retVal);
	}

	if (gbInitialized)
	{
		TimedCallback_Add(FlushASGlobalDataNonTransactedChanges, NULL, 1.0f);
	}
}

// Return true if the global data container has been initialized.
bool astInitialized(void)
{
	return gbInitialized;
}

U32 asgGetLastNightlyWalk(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->LastNightlyWalk : 0;
}

void asgSetLastNightlyWalk(U32 uTime)
{
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, "SetLastNightlyWalk", "set .Lastnightlywalk = \"%u\"", uTime);
}

const GlobalAccountStatsContainer *asgGetGlobalStats(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? &globalData->GlobalStats : NULL;
}

void asgResetStats(void)
{
	stashTableDestroy(stPlayTime);
	stPlayTime = NULL;
	stashTableDestroy(stPurchaseStats);
	stPurchaseStats = NULL;
	stashTableDestroy(stPurchaseStatsHourly);
	stPurchaseStatsHourly = NULL;
	stashTableDestroy(stPointsTypes);
	stPointsTypes = NULL;
	stashTableDestroy(stTransactionCodes);
	stTransactionCodes = NULL;
	AutoTrans_trClearStats(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1);
}

U32 asgGetGlobalStatsLastReset(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->GlobalStats.LastReset : 0;
}

void asgSetGlobalStatsLastReset(U32 uTime)
{
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, "SetGlobalStatsLastReset", "set .Globalstats.Lastreset = \"%lu\"", uTime);
}

void asgSetGlobalStatsLastEnabled(U32 uTime)
{
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, "SetGlobalStatsLastEnabled", "set .Globalstats.Lastenabled = \"%lu\"", uTime);
}

void asgSetGlobalStatsLastDisabled(U32 uTime)
{
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, "SetGlobalStatsLastDisabled", "set .Globalstats.Lastdisabled = \"%lu\"", uTime);
}

void asgAddGlobalStatsTotalPurchases(U64 numPurchases)
{
	ASGlobalData *globalData = GetASGlobalDataForNonTransactedModify();

	if (globalData && numPurchases)
	{
		globalData->GlobalStats.TotalPurchases += numPurchases;
		gbModified = true;
	}
}

U32 asgGetGlobalStatsDailyCurrentStart(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->GlobalStats.DailyCurrentStart : 0;
}

void asgRotateStatsDaily(U32 uStarted)
{
	AutoTrans_trRotateStatsDaily(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, uStarted);
}

CONST_EARRAY_OF(CurrencyPurchasesContainer) asgGetPurchaseStats(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->PurchaseStats : NULL;
}

void asgAddGlobalStatsTotalLogins(U64 numLogins)
{
	ASGlobalData *globalData = GetASGlobalDataForNonTransactedModify();

	if (globalData && numLogins)
	{
		globalData->GlobalStats.TotalLogins += numLogins;
		gbModified = true;
	}
}

void asgAddPurchaseStatsItem(const char *pLocale, const char *pProduct, const Money *pAmount)
{
	char *key = NULL;
	int index;
	bool success;
	char *pCurrency = NULL;

	// Locale is optional.
	if (!pLocale)
		pLocale = "";

	// Get the currency.
	estrStackCreate(&pCurrency);
	estrCurrency(&pCurrency, pAmount);

	// Look up the StashTable entry.
	InitPurchaseStatsIfNecessary();
	estrStackCreate(&key);
	PurchaseStatsStashKey(&key, pCurrency, pLocale, pProduct);
	success = stashFindInt(stPurchaseStats, key, &index);
	if (!success)
		index = -1;

	// Run transaction.
	AutoTrans_trAddPurchaseStatsItem(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pCurrency, pLocale, pProduct, pAmount, index);

	// Update StashTable if necessary.
	if (index == -1)
	{
		CONST_EARRAY_OF(CurrencyPurchasesContainer) purchases = asgGetPurchaseStats();
		char *currency = NULL;
		estrStackCreate(&currency);
		EARRAY_FOREACH_REVERSE_BEGIN(purchases, i);
			const CurrencyPurchasesContainer *purchase = purchases[i];
			estrCurrency(&currency, moneyContainerToMoneyConst(&purchase->TotalPurchases));
			if (!stricmp(currency, pCurrency)
				&& !stricmp_safe(purchase->Locale, pLocale)
				&& !stricmp(purchase->Product, pProduct))
			{
				index = i;
				break;
			}
		EARRAY_FOREACH_END;
		devassert(index >= 0);
		stashAddInt(stPurchaseStats, key, index, true);
		estrDestroy(&currency);
	}

	// Do the same for the hourly statistics.
	asgAddPurchaseStatsHourlyItem(pCurrency, pLocale, pProduct, pAmount);
	estrDestroy(&pCurrency);
	estrDestroy(&key);
}

CONST_EARRAY_OF(CurrencyPurchasesContainer) asgGetPurchaseStatsHourlyCurrent(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->PurchaseStatsHourlyCurrent : NULL;
}

CONST_EARRAY_OF(CurrencyPurchasesContainer) asgGetPurchaseStatsHourlyPrevious(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->PurchaseStatsHourlyPrevious : NULL;
}

U32 asgGetGlobalStatsHourlyCurrentStart(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->GlobalStats.HourlyCurrentStart : 0;
}

void asgRotateStatsHourly(U32 uStarted)
{
	AutoTrans_trRotateStatsHourly(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, uStarted);
}

CONST_EARRAY_OF(PlayTimeContainer) asgGetPlayTime(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->PlayTime : NULL;
}

void asgAddPlayTime(SA_PARAM_OP_STR const char *pLocale, SA_PARAM_NN_STR const char *pProduct, SA_PARAM_NN_STR const char *pShard,
					U32 uPlayTime)
{
	char *key = NULL;
	int index;
	bool success;

	devassert(pProduct && pShard);

	// Locale is optional.
	if (!pLocale)
		pLocale = "";

	// Look up the StashTable entry.
	InitPlayTimeIfNecessary();
	estrStackCreate(&key);
	PlayTimeStashKey(&key, pLocale, pProduct, pShard);
	success = stashFindInt(stPlayTime, key, &index);
	if (!success)
		index = -1;

	// Run transaction.
	AutoTrans_trAddPlayTime(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pLocale, pProduct, pShard, uPlayTime, index);

	// Update StashTable if necessary.
	if (index == -1)
	{
		CONST_EARRAY_OF(PlayTimeContainer) playTimes = asgGetPlayTime();
		EARRAY_FOREACH_REVERSE_BEGIN(playTimes, i);
			const PlayTimeContainer *playTime = playTimes[i];
			if (!stricmp_safe(playTime->Locale, pLocale)
				&& !stricmp(playTime->Product, pProduct)
				&& !stricmp(playTime->Shard, pShard))
			{
				index = i;
				break;
			}
		EARRAY_FOREACH_END;
		devassert(index >= 0);
		stashAddInt(stPlayTime, key, index, true);
	}
	estrDestroy(&key);
}

CONST_EARRAY_OF(TotalPointsBalanceContainer) asgGetTotalAccountsPointsBalance(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->TotalAccountsPointsBalance : NULL;
}

void asgResetTotalAccountsPointsBalance()
{
	stashTableDestroy(stPointsTypes);
	stPointsTypes = NULL;
	AutoTrans_trResetTotalAccountsPointsBalance(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1);
}

void asgAddTotalAccountsPointsBalanceItem(SA_PARAM_NN_VALID const Money *pAmountChange, U64 uAccountChange)
{
	char *key = NULL;
	int index;
	bool success;

	// Look up the StashTable entry.
	InitPointsTypesIfNecessary();
	estrStackCreate(&key);
	estrCurrency(&key, pAmountChange);
	success = stashFindInt(stPointsTypes, key, &index);
	if (!success)
		index = -1;

	// Run transaction.
	AutoTrans_trAddTotalAccountsPointsBalanceItem(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pAmountChange, uAccountChange, index);

	// Update StashTable if necessary.
	if (index == -1)
	{
		char *currency = NULL;
		CONST_EARRAY_OF(TotalPointsBalanceContainer) balances = asgGetTotalAccountsPointsBalance();
		estrStackCreate(&currency);
		EARRAY_FOREACH_REVERSE_BEGIN(balances, i);
			const TotalPointsBalanceContainer *balance = balances[i];
			estrCurrency(&currency, moneyContainerToMoneyConst(&balance->TotalBalances));
			if (!stricmp(key, currency))
			{
				index = i;
				break;
			}
		EARRAY_FOREACH_END;
		devassert(index >= 0);
		stashAddInt(stPointsTypes, key, index, true);
		estrDestroy(&currency);
	}
	estrDestroy(&key);
}

CONST_EARRAY_OF(TransactionCodesContainer) asgGetTransactionCodes(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->TransactionCodes : NULL;
}

// Update transaction response codes.
void asgAddTransactionCodes(SA_PARAM_OP_STR const char *pLocale, SA_PARAM_NN_STR const char *pCurrency, const char *pAuthCode,
							const char *pAvsCode, const char *pCvnCode)
{
	char *key = NULL;
	int index;
	bool success;

	// Look up the StashTable entry.
	InitTransactionCodesIfNecessary();
	estrStackCreate(&key);
	TransactionCodesStashKey(&key, pLocale, pCurrency, pAuthCode, pAvsCode, pCvnCode);
	success = stashFindInt(stTransactionCodes, key, &index);
	if (!success)
		index = -1;

	// Run transaction.
	AutoTrans_trAddTransactionCodes(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pLocale, pCurrency, pAuthCode, pAvsCode,
		pCvnCode, index);

	// Update StashTable if necessary.
	if (index == -1)
	{
		CONST_EARRAY_OF(TransactionCodesContainer) combinations = asgGetTransactionCodes();
		EARRAY_FOREACH_REVERSE_BEGIN(combinations, i);
			const TransactionCodesContainer *combination = combinations[i];
			if (!stricmp_safe(combination->Locale, pLocale) && !stricmp(combination->Currency, pCurrency)
				&& !stricmp_safe(combination->AuthCode, pAuthCode) && !stricmp_safe(combination->AvsCode, pAvsCode)
				&& !stricmp_safe(combination->CvnCode, pCvnCode))
			{
				index = i;
				break;
			}
		EARRAY_FOREACH_END;
		devassert(index >= 0);
		stashAddInt(stTransactionCodes, key, index, true);
	}
	estrDestroy(&key);
}

// Set the fetch delta stats
void asgSetFetchDeltaStats(FetchDeltaType eType, const FetchDeltaStats *pStats)
{
	AutoTrans_trSetFetchDeltaStats(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, eType, (FetchDeltaStatsContainer *)pStats);
}

CONST_EARRAY_OF(U64Container) asgGetKeyActivations(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->KeyActivations : NULL;
}

CONST_EARRAY_OF(U64Container) asgGetKeyActivationsDailyCurrent(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->KeyActivationsDailyCurrent : NULL;
}

CONST_EARRAY_OF(U64Container) asgGetKeyActivationsDailyPrevious(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->KeyActivationsDailyPrevious : NULL;
}

// Add activated keys.
void asgAddKeyActivations(int iBatchId, S64 addend)
{
	CONST_EARRAY_OF(U64Container) total = asgGetKeyActivations();
	CONST_EARRAY_OF(U64Container) current = asgGetKeyActivationsDailyCurrent();
	int minSize = MIN(eaSize(&total), eaSize(&current));

	// Enlarge array if necessary.
	if (iBatchId >= minSize)
		AutoTrans_trResizeKeyActivations(astrRequireSuccess("asgAddKeyActivations(): enlarge"),
			objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, MAX(iBatchId, getMaxBatchId()) + 1);

	// Add the activated keys.
	objRequestTransactionSimplef(astrRequireSuccess("asgAddKeyActivations(): total"),
		GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, "asgAddKeyActivations(): total", "set KeyActivations[%d].uValue += %"FORM_LL"u",
		iBatchId, addend);
	objRequestTransactionSimplef(astrRequireSuccess("asgAddKeyActivations(): daily"),
		GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, "asgAddKeyActivations(): daily", "set KeyActivationsDailyCurrent[%d].uValue += %"FORM_LL"u",
		iBatchId, addend);
}

CONST_EARRAY_OF(LockedKey) asgGetLockedKeys(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->LockedKeys : NULL;
}

CONST_EARRAY_OF(EncryptionKeyVerificationString) asgGetEncryptionKeyVerificationStrings(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->EncryptionKeyVerificationStrings : NULL;
}

// Lock a key.
void asgAddKeyLock(const char *pKeyName, U32 uLockTime)
{
	AutoTrans_trLockKey(astrRequireSuccess("asgAddKeyLock"), objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pKeyName, uLockTime);
}

// Unlock a key.
bool asgRemoveKeyLock(const char *pKeyName)
{
	TransactionReturnVal result;
	bool success;
	AutoTrans_trUnlockKey(&result, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pKeyName);
	success = result.eOutcome == TRANSACTION_OUTCOME_SUCCESS;
	ReleaseReturnValData(objLocalManager(), &result);
	return success;
}

enumPasswordVersion asgGetPasswordVersion(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->PasswordVersion : 0;
}

U32 asgGetPasswordBackupCreationTime(void)
{
	ASGlobalData *globalData = GetASGlobalData();
	return globalData ? globalData->PasswordBackupCreationTime : 0;
}

/************************************************************************/
/* Transactions                                                         */
/************************************************************************/

// Clear all statistics-related members of GlobalData.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Globalstats, .Keyactivations, .Keyactivationsdailycurrent, .Keyactivationsdailyprevious, .Playtime, .Purchasestats, .Purchasestatshourlycurrent, .Purchasestatshourlyprevious, .Totalaccountspointsbalance, .Transactioncodes");
enumTransactionOutcome trClearStats(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData)
{

	// Clear statistics.
	StructResetNoConst(parse_GlobalAccountStatsContainer, &pGlobalData->GlobalStats);
	eaDestroyStructVoid(&pGlobalData->KeyActivations, parse_U64Container);
	eaDestroyStructVoid(&pGlobalData->KeyActivationsDailyCurrent, parse_U64Container);
	eaDestroyStructVoid(&pGlobalData->KeyActivationsDailyPrevious, parse_U64Container);
	eaDestroyStructVoid(&pGlobalData->PlayTime, parse_PlayTimeContainer);
	eaDestroyStructVoid(&pGlobalData->PurchaseStats, parse_CurrencyPurchasesContainer);
	pGlobalData->GlobalStats.HourlyCurrentStart = 0;
	pGlobalData->GlobalStats.HourlyPreviousStart = 0;
	eaDestroyStructVoid(&pGlobalData->PurchaseStatsHourlyCurrent, parse_CurrencyPurchasesContainer);
	eaDestroyStructVoid(&pGlobalData->PurchaseStatsHourlyPrevious, parse_CurrencyPurchasesContainer);
	eaDestroyStructVoid(&pGlobalData->TotalAccountsPointsBalance, parse_TotalPointsBalanceContainer);
	eaDestroyStructVoid(&pGlobalData->TransactionCodes, parse_TransactionCodesContainer);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Update the purchases statistics for a new purchase.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Purchasestatshourlycurrent");
enumTransactionOutcome trAddPurchaseStatsHourlyItem(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, const char *pCurrency, const char *pLocale,
											  const char *pProduct, const Money *pAmount, int index)
{
	NOCONST(CurrencyPurchasesContainer) *purchases;
	char *currency = NULL;
	estrStackCreate(&currency);

	// Allocate entry if needed.
	if (index == -1)
	{
		index = eaPush(&pGlobalData->PurchaseStatsHourlyCurrent, StructCreateNoConst(parse_CurrencyPurchasesContainer));
		pGlobalData->PurchaseStatsHourlyCurrent[index]->Locale = estrDupIfNonempty(pLocale);
		pGlobalData->PurchaseStatsHourlyCurrent[index]->Product = estrDup(pProduct);
		moneyInitFromStr(moneyContainerToMoney(&pGlobalData->PurchaseStatsHourlyCurrent[index]->TotalPurchases), "0", pCurrency);
	}
	devassert(index >= 0);

	// Add this purchase to entry.
	purchases = pGlobalData->PurchaseStatsHourlyCurrent[index];
	estrCurrency(&currency, moneyContainerToMoney(&purchases->TotalPurchases));
	devassert(!stricmp(currency, pCurrency)
		&& !stricmp(purchases->Product, pProduct)
		&& !stricmp_safe(purchases->Locale, pLocale));
	++purchases->PurchaseCount;
	moneyAdd(moneyContainerToMoney(&purchases->TotalPurchases), pAmount);
	estrDestroy(&currency);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Update the purchases statistics for a new purchase.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Purchasestats");
enumTransactionOutcome trAddPurchaseStatsItem(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, const char *pCurrency, const char *pLocale,
										  const char *pProduct, const Money *pAmount, int index)
{
	NOCONST(CurrencyPurchasesContainer) *purchases;
	char *currency = NULL;
	estrStackCreate(&currency);

	// Allocate entry if needed.
	if (index == -1)
	{
		index = eaPush(&pGlobalData->PurchaseStats, StructCreateNoConst(parse_CurrencyPurchasesContainer));
		pGlobalData->PurchaseStats[index]->Locale = estrDupIfNonempty(pLocale);
		pGlobalData->PurchaseStats[index]->Product = estrDup(pProduct);
		moneyInitFromStr(moneyContainerToMoney(&pGlobalData->PurchaseStats[index]->TotalPurchases), "0", pCurrency);
	}
	devassert(index >= 0);

	// Add this purchase to entry.
	purchases = pGlobalData->PurchaseStats[index];
	estrCurrency(&currency, moneyContainerToMoney(&purchases->TotalPurchases));
	devassert(!stricmp(currency, pCurrency)
		&& !stricmp(purchases->Product, pProduct)
		&& !stricmp_safe(purchases->Locale, pLocale));
	++purchases->PurchaseCount;
	moneyAdd(moneyContainerToMoney(&purchases->TotalPurchases), pAmount);
	estrDestroy(&currency);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Move current to previous.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Purchasestatshourlyprevious, .Purchasestatshourlycurrent, .Globalstats.Hourlypreviousstart, .Globalstats.Hourlycurrentstart");
enumTransactionOutcome trRotateStatsHourly(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, U32 uStarted)
{
	// Free old previous.
	eaDestroyStructVoid(&pGlobalData->PurchaseStatsHourlyPrevious, parse_CurrencyPurchasesContainer);

	// Copy current to previous.
	pGlobalData->PurchaseStatsHourlyPrevious = pGlobalData->PurchaseStatsHourlyCurrent;
	pGlobalData->GlobalStats.HourlyPreviousStart = pGlobalData->GlobalStats.HourlyCurrentStart;

	// Reinitialize current.
	pGlobalData->PurchaseStatsHourlyCurrent = NULL;
	stashTableDestroy(stPurchaseStatsHourly);
	stPurchaseStatsHourly = NULL;
	pGlobalData->GlobalStats.HourlyCurrentStart = uStarted;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Move today's stats to yesterday.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Keyactivationsdailyprevious, .Keyactivationsdailycurrent, .Globalstats.Dailypreviousstart, .Globalstats.Dailycurrentstart");
enumTransactionOutcome trRotateStatsDaily(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, U32 uStarted)
{
	NOCONST(U64Container) **previous;

	// Zero out old previous so it can be re-used.
	EARRAY_FOREACH_BEGIN(pGlobalData->KeyActivationsDailyPrevious, i);
		pGlobalData->KeyActivationsDailyPrevious[i]->uValue = 0;
	EARRAY_FOREACH_END;
	previous = pGlobalData->KeyActivationsDailyPrevious;

	// Copy current to previous.
	pGlobalData->KeyActivationsDailyPrevious = pGlobalData->KeyActivationsDailyCurrent;
	pGlobalData->GlobalStats.DailyPreviousStart = pGlobalData->GlobalStats.DailyCurrentStart;

	// Initialize current.
	pGlobalData->KeyActivationsDailyCurrent = previous;
	pGlobalData->GlobalStats.DailyCurrentStart = uStarted;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Add play time.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Playtime");
enumTransactionOutcome trAddPlayTime(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, SA_PARAM_OP_STR const char *pLocale,
									  SA_PARAM_NN_STR const char *pProduct, SA_PARAM_NN_STR const char *pShard,	U32 uPlayTime, int index)
{

	NOCONST(PlayTimeContainer) *playTime;

	// Allocate entry if needed.
	if (index == -1)
	{
		index = eaPush(&pGlobalData->PlayTime, StructCreateNoConst(parse_PlayTimeContainer));
		pGlobalData->PlayTime[index]->Locale = estrDupIfNonempty(pLocale);
		pGlobalData->PlayTime[index]->Product = estrDup(pProduct);
		pGlobalData->PlayTime[index]->Shard = estrDup(pShard);
		pGlobalData->PlayTime[index]->TotalPlayTime = 0;
	}
	devassert(index >= 0);

	// Add this purchase to entry.
	playTime = pGlobalData->PlayTime[index];
	devassert(!stricmp_safe(playTime->Locale, pLocale))
		&& !stricmp(playTime->Product, pProduct)
		&& !stricmp(playTime->Shard, pShard);
	devassert(uPlayTime);
	playTime->TotalPlayTime += uPlayTime;
	++playTime->TotalLogouts;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Reset the accounts points balance statistics.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Totalaccountspointsbalance");
enumTransactionOutcome trResetTotalAccountsPointsBalance(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData)
{
	eaClear(&pGlobalData->TotalAccountsPointsBalance);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Update the accounts balances for a change in an account's balance.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Totalaccountspointsbalance");
enumTransactionOutcome trAddTotalAccountsPointsBalanceItem(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData,
														   const Money *pAmountChange, U64 uAccountChange, int index)
{
	NOCONST(TotalPointsBalanceContainer) *balances;
	char *currency = NULL, *actualCurrency = NULL;
	Money zero;

	estrStackCreate(&currency);
	estrCurrency(&currency, pAmountChange);
	moneyInitFromStr(&zero, "0", currency);
	devassert(!moneyEqual(pAmountChange, &zero));

	// Allocate entry if needed.
	if (index == -1)
	{
		
		index = eaPush(&pGlobalData->TotalAccountsPointsBalance, StructCreateNoConst(parse_TotalPointsBalanceContainer));
		moneyInitFromStr(moneyContainerToMoney(&pGlobalData->TotalAccountsPointsBalance[index]->TotalBalances), "0", currency);
		pGlobalData->TotalAccountsPointsBalance[index]->AccountsWithPoints = 0;
		
	}
	devassert(index >= 0);

	// Add this purchase to entry.
	balances = pGlobalData->TotalAccountsPointsBalance[index];
	estrStackCreate(&actualCurrency);
	estrCurrency(&actualCurrency, moneyContainerToMoney(&balances->TotalBalances));
	devassert(!stricmp(currency, actualCurrency));
	moneyAdd(moneyContainerToMoney(&pGlobalData->TotalAccountsPointsBalance[index]->TotalBalances), pAmountChange);
	balances->AccountsWithPoints += uAccountChange;
	estrDestroy(&actualCurrency);
	estrDestroy(&currency);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Add a transaction response to the codes array.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Transactioncodes");
enumTransactionOutcome trAddTransactionCodes(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, SA_PARAM_OP_STR const char *pLocale,
											 SA_PARAM_NN_STR const char *pCurrency, SA_PARAM_OP_STR const char *pAuthCode,
											 SA_PARAM_OP_STR const char *pAvsCode, SA_PARAM_OP_STR const char *pCvnCode, int index)
{
	NOCONST(TransactionCodesContainer) *combination;

	devassert(pLocale && pCurrency && pAuthCode && pAvsCode && pCvnCode);

	// Allocate entry if needed.
	if (index == -1)
	{
		index = eaPush(&pGlobalData->TransactionCodes, StructCreateNoConst(parse_TransactionCodesContainer));
		pGlobalData->TransactionCodes[index]->Locale = estrDupIfNonempty(pLocale);
		pGlobalData->TransactionCodes[index]->Currency = estrDupIfNonempty(pCurrency);
		pGlobalData->TransactionCodes[index]->AuthCode = estrDupIfNonempty(pAuthCode);
		pGlobalData->TransactionCodes[index]->AvsCode = estrDupIfNonempty(pAvsCode);
		pGlobalData->TransactionCodes[index]->CvnCode = estrDupIfNonempty(pCvnCode);

	}
	devassert(index >= 0);

	// Count this transaction.
	combination = pGlobalData->TransactionCodes[index];
	devassert(!stricmp_safe(combination->Locale, pLocale) && !stricmp(combination->Currency, pCurrency)
		&& !stricmp_safe(combination->AuthCode, pAuthCode) && !stricmp_safe(combination->AvsCode, pAvsCode)
		&& !stricmp_safe(combination->CvnCode, pCvnCode));
	++combination->Count;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Set fetch delta stats
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Globalstats.Autobillfetchdelta, .Globalstats.Entitlementfetchdelta");
enumTransactionOutcome trSetFetchDeltaStats(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData,
											int eType,
											NON_CONTAINER FetchDeltaStatsContainer *pStats)
{
	NOCONST(FetchDeltaStatsContainer) *pDest = NULL;

	if (!verify(eType == FDT_AUTOBILLS || eType == FDT_ENTITLEMENTS))
		return TRANSACTION_OUTCOME_FAILURE;

	if (!verify(pStats))
		return TRANSACTION_OUTCOME_FAILURE;

	switch (eType)
	{
	xcase FDT_AUTOBILLS:
		pDest = &pGlobalData->GlobalStats.AutoBillFetchDelta;

	xcase FDT_ENTITLEMENTS:
		pDest = &pGlobalData->GlobalStats.EntitlementFetchDelta;
	}

	if (!devassert(pDest))
		return TRANSACTION_OUTCOME_FAILURE;

	eaDestroyEString(&pDest->Errors);

	// Do an assignment, which does a deep-copy
	*pDest = *CONTAINER_NOCONST(FetchDeltaStatsContainer, pStats);

	// Set it to NULL because the pointer was copied but we need to copy the array ourselves
	pDest->Errors = NULL;
	eaCopyEStrings(&pStats->Errors, &pDest->Errors);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Enlarge activated keys array for more batches.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Keyactivations, .Keyactivationsdailycurrent");
enumTransactionOutcome trResizeKeyActivations(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, int iNewSize)
{
	// Resize EArrays when appropriate.
	if (eaSize(&pGlobalData->KeyActivations) < iNewSize)
		eaSetSizeStructNoConst(&pGlobalData->KeyActivations, parse_U64Container, iNewSize);
	if (eaSize(&pGlobalData->KeyActivationsDailyCurrent) < iNewSize)
		eaSetSizeStructNoConst(&pGlobalData->KeyActivationsDailyCurrent, parse_U64Container, iNewSize);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Lock a key.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Lockedkeys");
enumTransactionOutcome trLockKey(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, const char *pKeyName, U32 uLockTime)
{
	NOCONST(LockedKey) searchKey, *pSearchKey = &searchKey;
	NOCONST(LockedKey) *key;
	int index;

	// Make sure the key isn't already locked.
	if (!devassert(pKeyName))
		return TRANSACTION_OUTCOME_FAILURE;
	searchKey.keyName = (char *)pKeyName;
	index = (int)eaBFind(pGlobalData->LockedKeys, keyLockCmp, pSearchKey);
	if (index < eaSize(&pGlobalData->LockedKeys) && !stricmp(pGlobalData->LockedKeys[index]->keyName, pKeyName))
		return TRANSACTION_OUTCOME_FAILURE;

	// Add the lock.
	key = StructCreateNoConst(parse_LockedKey);
	key->keyName = strdup(pKeyName);
	key->uLockTime = uLockTime;
	eaInsert(&pGlobalData->LockedKeys, key, index);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Unlock a key.
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".Lockedkeys");
enumTransactionOutcome trUnlockKey(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, const char *pKeyName)
{
	NOCONST(LockedKey) searchKey, *pSearchKey = &searchKey;
	int index;

	// Find key.
	if (!devassert(pKeyName))
		return TRANSACTION_OUTCOME_FAILURE;
	searchKey.keyName = (char *)pKeyName;
	index = (int)eaBFind(pGlobalData->LockedKeys, keyLockCmp, pSearchKey);
	if (index < eaSize(&pGlobalData->LockedKeys) && !stricmp(pGlobalData->LockedKeys[index]->keyName, pKeyName))
	{
		eaRemove(&pGlobalData->LockedKeys, index);
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	// Not found
	return TRANSACTION_OUTCOME_FAILURE;
}


AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".EncryptionKeyVerificationStrings");
enumTransactionOutcome trAddEncryptionKeyVerificationString(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, 
	S32 iIndex, char *pEncodedString)
{
	int i;
	int iSize = eaSize(&pGlobalData->EncryptionKeyVerificationStrings);
	NOCONST(EncryptionKeyVerificationString) *pStruct;

	for (i = 0; i < iSize; i++)
	{
		if (pGlobalData->EncryptionKeyVerificationStrings[i]->iKeyIndex == iIndex)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	pStruct = (NOCONST(EncryptionKeyVerificationString)*)StructCreate(parse_EncryptionKeyVerificationString);
	estrCopy2(&pStruct->encodedStringForComparing, pEncodedString);
	pStruct->iKeyIndex = iIndex;
	eaPush(&pGlobalData->EncryptionKeyVerificationStrings, pStruct);

	return TRANSACTION_OUTCOME_SUCCESS;
}
	
AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".EncryptionKeyVerificationStrings");
enumTransactionOutcome trRemoveEncryptionKeyVerificationString(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, 
	U32 iIndex)
{
	int iFoundIndex = eaIndexedFindUsingInt(&pGlobalData->EncryptionKeyVerificationStrings, iIndex);
	if (iFoundIndex == -1)
	{
		return TRANSACTION_OUTCOME_FAILURE;	
	}

	StructDestroyVoid(parse_EncryptionKeyVerificationString, eaRemove(&pGlobalData->EncryptionKeyVerificationStrings, iFoundIndex));

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".PasswordVersion");
enumTransactionOutcome trSetPasswordVersion(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, int iPasswordVersion)
{
	pGlobalData->PasswordVersion = iPasswordVersion;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGlobalData, ".PasswordBackupCreationTime");
enumTransactionOutcome trSetPasswordBackupCreationTime(ATR_ARGS, NOCONST(ASGlobalData) *pGlobalData, U32 iTime)
{
	pGlobalData->PasswordBackupCreationTime = iTime;
	return TRANSACTION_OUTCOME_SUCCESS;
}

#include "GlobalData_h_ast.c"
#include "GlobalData_c_ast.c"
