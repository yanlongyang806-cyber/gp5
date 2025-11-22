#include "AccountSearch.h"

#include "earray.h"
#include "estring.h"
#include "utils.h"
#include "timing.h"
#include "objContainer.h"
#include "AccountServer.h"
#include "AccountIntegration.h"

#include "AutoGen/AccountSearch_c_ast.h"

// -------------------------------------------------------------------------------------------
// Sorting comparison function macros. I completely agree with you, this code is very ugly.

#define BASIC_SORT_BY_INT_FUNC(FUNCNAME, VARIABLE) \
int FUNCNAME(const AccountInfo **account1, const AccountInfo **account2, const void *ign) { \
	if      ((*account1)->VARIABLE < (*account2)->VARIABLE) return -1; \
	else if ((*account1)->VARIABLE > (*account2)->VARIABLE) return  1; \
	else return 0; }

#define BASIC_SORT_BY_INT_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const AccountInfo **account1, const AccountInfo **account2, const void *ign) { \
	if      ((*account1)->VARIABLE < (*account2)->VARIABLE) return  1; \
	else if ((*account1)->VARIABLE > (*account2)->VARIABLE) return -1; \
	else return 0; }

#define BASIC_SORT_BY_STR_FUNC(FUNCNAME, VARIABLE) \
int FUNCNAME(const AccountInfo **account1, const AccountInfo **account2, const void *ign) { \
	if (!(*account1)->VARIABLE && !(*account2)->VARIABLE) return 0; \
	if (!(*account1)->VARIABLE) return -1; \
	if (!(*account2)->VARIABLE) return 1; \
	return -1 * stricmp((*account1)->VARIABLE, (*account2)->VARIABLE); }

#define BASIC_SORT_BY_STR_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const AccountInfo **account1, const AccountInfo **account2, const void *ign) { \
	if (!(*account1)->VARIABLE && !(*account2)->VARIABLE) return 0; \
	if (!(*account1)->VARIABLE) return 1; \
	if (!(*account2)->VARIABLE) return -1; \
	return stricmp((*account1)->VARIABLE, (*account2)->VARIABLE); }

#define DO_SORT(FUNCNAME, DESCENDING) \
	if(DESCENDING) {eaStableSort(ppAccounts, NULL, FUNCNAME ## Desc);} \
	else           {eaStableSort(ppAccounts, NULL, FUNCNAME);}

// -------------------------------------------------------------------------------------------
// Sorting comparison functions, defined by the above macros. This is prettier.

BASIC_SORT_BY_INT_FUNC     (SortByID,      uID);
BASIC_SORT_BY_INT_FUNC_DESC(SortByIDDesc,  uID);

BASIC_SORT_BY_STR_FUNC     (SortByAccountName,      accountName);
BASIC_SORT_BY_STR_FUNC_DESC(SortByAccountNameDesc,  accountName);

BASIC_SORT_BY_STR_FUNC     (SortByDisplayName,     displayName);
BASIC_SORT_BY_STR_FUNC_DESC(SortByDisplayNameDesc, displayName);

BASIC_SORT_BY_STR_FUNC     (SortByEmail,     personalInfo.email);
BASIC_SORT_BY_STR_FUNC_DESC(SortByEmailDesc, personalInfo.email);


// -------------------------------------------------------------------------------------------
// Sorting code

static void sortBySortOrder(AccountInfo **ppAccounts, SortOrder eSortOrder, bool bDescending)
{
	switch(eSortOrder)
	{
		xcase SORTORDER_ID:  		    DO_SORT(SortByID, bDescending);
		xcase SORTORDER_ACCOUNTNAME:    DO_SORT(SortByAccountName, bDescending);
		xcase SORTORDER_DISPLAYNAME:    DO_SORT(SortByDisplayName, bDescending);
		xcase SORTORDER_EMAIL:          DO_SORT(SortByEmail, bDescending);

		xdefault:					printf("sortBySortOrder(): Unknown sort order %d\n", eSortOrder);
	};
}

// -------------------------------------------------------------------------------------------
// Search Code

static bool searchMatches(AccountSearchData *pData, AccountInfo *account)
{
	int i;
	bool bFoundAnything;

	if (pData->uFlags & SEARCHFLAG_NAME)
	{
		bFoundAnything = false;
		if (pData->iNameFilter & SEARCHNAME_ACCOUNT)
		{
			if (strstri(account->accountName, pData->name))
				bFoundAnything = true;
		}
		if (!bFoundAnything && (pData->iNameFilter & SEARCHNAME_DISPLAY))
		{
			if (account->displayName && strstri(account->displayName, pData->name))
				bFoundAnything = true;
		}
		if (!bFoundAnything)
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_EMAIL)
	{
		if (!account->personalInfo.email || !strstri(account->personalInfo.email, pData->email))
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_PRODUCTSUB)
	{
		bFoundAnything = false;

		if (pData->iProductFilter & SEARCHPERMISSIONS_PRODUCT)
		{
			for (i=eaSize(&account->ppProducts)-1; i>=0; i--)
			{
				if (strstri(account->ppProducts[i]->name, pData->productSub))
				{
					bFoundAnything = true;
					break;
				}
			}
		}
		if (!bFoundAnything)
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_FIRSTNAME)
	{
		if (!account->personalInfo.firstName || !strstri(account->personalInfo.firstName, pData->firstName))
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_LASTNAME)
	{
		if (!account->personalInfo.lastName || !strstri(account->personalInfo.lastName, pData->lastName))
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_PRODUCTKEY)
	{
		bFoundAnything = false;
		for (i=eaSize(&account->ppProducts)-1; i>=0; i--)
		{
			if (account->ppProducts[i]->key && stricmp(account->ppProducts[i]->key, pData->productKey) == 0)
			{
				bFoundAnything = true;
				break;
			}
		}
		if (!bFoundAnything)
		{
			for (i=eaSize(&account->ppProductKeys)-1; i>=0; i--)
			{
				if (stricmp(account->ppProductKeys[i], pData->productKey) == 0)
				{
					bFoundAnything = true;
					break;
				}
			}
		}
		if (!bFoundAnything)
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_ALL)
	{
		if (strstri(account->accountName, pData->pAny))
			return true;
		if (account->displayName && strstri(account->displayName, pData->pAny))
			return true;
		if (account->personalInfo.email && strstri(account->personalInfo.email, pData->pAny))
			return true;
		if (account->personalInfo.firstName && strstri(account->personalInfo.firstName, pData->pAny))
			return true;
		if (account->personalInfo.lastName && strstri(account->personalInfo.lastName, pData->pAny))
			return true;
		return false;
	}
	return true;
}

static AccountInfo * internalSearchNext(AccountSearchData *pData, int iStartingIndex)
{
	AccountInfo *account = NULL;
	if (iStartingIndex < 0 || iStartingIndex >= eaSize(&pData->ppSortedEntries))
		return NULL;
	account = pData->ppSortedEntries[iStartingIndex];
	pData->iNextIndex++;
	return account;
}

static void internalSearch(AccountSearchData *pData)
{
	ContainerIterator iter = {0};
	Container *con;

	eaClear(&pData->ppSortedEntries);
	pData->iCount = 0;
	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		AccountInfo * account = (AccountInfo*) con->containerData;
		if(searchMatches(pData, account))
		{
			eaPush(&pData->ppSortedEntries, account);
			pData->iCount++;
		}
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	pData->iNextIndex = 0;
}

AccountInfo * searchFirst(AccountSearchData *pData)
{
	AccountInfo *account;

	internalSearch(pData);
	if(pData->uFlags & SEARCHFLAG_SORT)
	{
		sortBySortOrder(pData->ppSortedEntries, pData->eSortOrder, pData->bSortDescending);
	}

	account = internalSearchNext(pData, 0);
	
	if(account == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
	return account;
}

AccountInfo * searchNext(AccountSearchData *pData)
{
	AccountInfo *account = internalSearchNext(pData, pData->iNextIndex);

	if(account == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
	return account;
}

void searchEnd(AccountSearchData *pData)
{
	if (pData)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
}


// ---------------------------------------------------------------------------------------------------------------------
// Account search functions
//
// In all cases, these functions return false if the account doesn't exist, and true if all the criteria are either
// NULL or blank. This permits individual criteria to be excluded by passing NULL or "" for them.
// ---------------------------------------------------------------------------------------------------------------------

// Logic: if account name matches, or display name matches, return true; otherwise return false
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesAccountOrDisplay");
bool accountSearch_MatchesAccountOrDisplay(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pAccountName, const char *pDisplayName)
{
	PWCommonAccount *pPWAccount = NULL;

	if (!pAccount) return false;
	if (!SAFE_DEREF(pAccountName) && !SAFE_DEREF(pDisplayName)) return true;
	if (SAFE_DEREF(pAccountName) && strstri_safe(pAccount->accountName, pAccountName)) return true;
	if (SAFE_DEREF(pDisplayName) && strstri_safe(pAccount->displayName, pDisplayName)) return true;

	pPWAccount = findPWCommonAccountByName(pAccount->pPWAccountName);
	if (!pPWAccount) return false;
	if (SAFE_DEREF(pAccountName) && strstri_safe(pPWAccount->pAccountName, pAccountName)) return true;
	if (SAFE_DEREF(pDisplayName) && strstri_safe(pPWAccount->pForumName, pDisplayName)) return true;

	return false;
}

// Logic: if email matches, return true
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesEmail");
bool accountSearch_MatchesEmail(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pEmail)
{
	PWCommonAccount *pPWAccount = NULL;

	if (!pAccount) return false;
	if (!SAFE_DEREF(pEmail)) return true;
	if (strstri_safe(pAccount->personalInfo.email, pEmail)) return true;

	pPWAccount = findPWCommonAccountByName(pAccount->pPWAccountName);
	if (!pPWAccount) return false;
	if (strstri_safe(pPWAccount->pEmail, pEmail)) return true;

	return false;
}

// Logic: if first name and last name both match, return true
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesName");
bool accountSearch_MatchesName(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pFirstName, const char *pLastName)
{
	if (!pAccount) return false;
	if (!SAFE_DEREF(pFirstName) && !SAFE_DEREF(pLastName)) return true;
	if (SAFE_DEREF(pFirstName) && !strstri_safe(pAccount->personalInfo.firstName, pFirstName)) return false;
	if (SAFE_DEREF(pLastName) && !strstri_safe(pAccount->personalInfo.lastName, pLastName)) return false;
	return true;
}

// Logic: if any product matches, return true
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesProduct");
bool accountSearch_MatchesProduct(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pProduct)
{
	if (!pAccount) return false;
	if (!SAFE_DEREF(pProduct)) return true;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, iProduct, iNumProducts);
	{
		AccountProductSub *pOwnedProduct = pAccount->ppProducts[iProduct];
		if (strstri_safe(pOwnedProduct->name, pProduct)) return true;
	}
	EARRAY_FOREACH_END;

	return false;
}

// Logic: if any product or distributed key matches, return true
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesKey");
bool accountSearch_MatchesKeyExact(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pKey)
{
	if (!pAccount) return false;
	if (!SAFE_DEREF(pKey)) return true;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, iProduct, iNumProducts);
	{
		AccountProductSub *pOwnedProduct = pAccount->ppProducts[iProduct];
		if (!stricmp(pOwnedProduct->key, pKey)) return true;
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProductKeys, iKey, iNumKeys);
	{
		if (!stricmp(pAccount->ppProductKeys[iKey], pKey)) return true;
	}
	EARRAY_FOREACH_END;

	return false;
}

// Logic: if name and all card digits match, return t
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesCreditCard");
bool accountSearch_MatchesCreditCard(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pCardName, const char *pCardFirstSix, const char *pCardLastFour)
{
	if (!pAccount) return false;
	if (!SAFE_DEREF(pCardName) && !SAFE_DEREF(pCardFirstSix) && !SAFE_DEREF(pCardLastFour)) return true;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->personalInfo.ppPaymentMethods, iPaymentMethod, iNumPaymentMethods);
	{
		CachedPaymentMethod *pPaymentMethod = pAccount->personalInfo.ppPaymentMethods[iPaymentMethod];
		if (!pPaymentMethod->creditCard) continue;
		if (SAFE_DEREF(pCardName) && !strstri_safe(pPaymentMethod->accountName, pCardName)) continue;
		if (SAFE_DEREF(pCardFirstSix) && !strstri_safe(pPaymentMethod->creditCard->bin, pCardFirstSix)) continue;
		if (SAFE_DEREF(pCardLastFour) && !strstri_safe(pPaymentMethod->creditCard->lastDigits, pCardLastFour)) continue;
		return true;
	}
	EARRAY_FOREACH_END;

	return false;
}


// Logic: if all criteria match, return true
static bool accountSearch_MatchesAddress_internal(const AccountAddress *pAddress, const char *pStreetAddress, const char *pCity, const char *pState, const char *pZip)
{
	// we don't concat address1 and address2 to save process time
	if (SAFE_DEREF(pStreetAddress) && !(strstri_safe(pAddress->address1, pStreetAddress) || strstri_safe(pAddress->address2, pStreetAddress))) return false;
	if (SAFE_DEREF(pCity) && !strstri_safe(pAddress->city, pCity)) return false;
	if (SAFE_DEREF(pState) && !strstri_safe(pAddress->district, pState)) return false;
	if (SAFE_DEREF(pZip) && !strstri_safe(pAddress->postalCode, pZip)) return false;
	return true;
}

// Logic: if any address matches, return true
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesAddress");
bool accountSearch_MatchesAddress(SA_PARAM_NN_VALID AccountInfo *pAccount, const char *pStreetAddress, const char *pCity, const char *pState, const char *pZip)
{
	if (!pAccount) return false;
	if (!SAFE_DEREF(pStreetAddress) && !SAFE_DEREF(pCity) && !SAFE_DEREF(pState) && !SAFE_DEREF(pZip)) return true;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->personalInfo.ppPaymentMethods, iPaymentMethod, iNumPaymentMethods);
	{
		CachedPaymentMethod *pPaymentMethod = pAccount->personalInfo.ppPaymentMethods[iPaymentMethod];
		if (accountSearch_MatchesAddress_internal(&pPaymentMethod->billingAddress, pStreetAddress, pCity, pState, pZip)) return true;
	}
	EARRAY_FOREACH_END;

	return false;
}

// Logic: if all criteria match (except account/display names, which can be either), return true
bool accountSearch_MatchesAllDetails(SA_PARAM_NN_VALID AccountInfo *pAccount,
	const char *pAccountName,
	const char *pDisplayName,
	const char *pEmail,
	const char *pFirstName,
	const char *pLastName,
	const char *pProduct,
	const char *pKey,
	const char *pCardName,
	const char *pCardFirstSix,
	const char *pCardLastFour,
	const char *pStreetAddress,
	const char *pCity,
	const char *pState,
	const char *pZip
	)
{
	bool result = true;

	if (!pAccount) return false;

	// no match if nothing specified
	if (!(SAFE_DEREF(pAccountName) || SAFE_DEREF(pDisplayName) || SAFE_DEREF(pEmail) || SAFE_DEREF(pFirstName) || SAFE_DEREF(pLastName) || 
		SAFE_DEREF(pProduct) || SAFE_DEREF(pKey) || SAFE_DEREF(pCardName) || SAFE_DEREF(pCardFirstSix) || SAFE_DEREF(pCardLastFour) || 
		SAFE_DEREF(pStreetAddress) || SAFE_DEREF(pCity) || SAFE_DEREF(pState) || SAFE_DEREF(pZip)))
		return false;

	result &= accountSearch_MatchesAccountOrDisplay(pAccount, pAccountName,	pDisplayName);
	result &= accountSearch_MatchesEmail(pAccount, pEmail);
	result &= accountSearch_MatchesName(pAccount, pFirstName, pLastName);
	result &= accountSearch_MatchesProduct(pAccount, pProduct);
	result &= accountSearch_MatchesKeyExact(pAccount, pKey);
	result &= accountSearch_MatchesCreditCard(pAccount, pCardName, pCardFirstSix, pCardLastFour);
	result &= accountSearch_MatchesAddress(pAccount, pStreetAddress, pCity, pState, pZip);
	return result;
}

// Because of the auto expression parameter number limit, export this shorten version instead.
AUTO_EXPR_FUNC(Account) ACMD_NAME("AccountSearch_MatchesDetails");
bool accountSearch_MatchesDetails(SA_PARAM_NN_VALID AccountInfo *pAccount,
	const char *pAccountName,
	const char *pDisplayName,
	const char *pEmail,
	const char *pFirstName,
	const char *pLastName,
	const char *pProduct,
	const char *pKey
	)
{
	return accountSearch_MatchesAllDetails(pAccount, pAccountName, pDisplayName, pEmail, pFirstName, pLastName, pProduct, pKey,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

AUTO_STRUCT;
typedef struct AccountSearchRequest
{
	char *pAccountName; AST(ESTRING)
	char *pDisplayName; AST(ESTRING)
	char *pEmail; AST(ESTRING)
	char *pFirstName; AST(ESTRING)
	char *pLastName; AST(ESTRING)
	char *pProduct; AST(ESTRING)
	char *pKey; AST(ESTRING)
	char *pCardName; AST(ESTRING)
	char *pCardFirstSix; AST(ESTRING)
	char *pCardLastFour; AST(ESTRING)
	char *pStreetAddress; AST(ESTRING)
	char *pCity; AST(ESTRING)
	char *pState; AST(ESTRING)
	char *pZip; AST(ESTRING)
} AccountSearchRequest;

bool accountSearch_MatchesAllDetailsStruct(SA_PARAM_NN_VALID AccountInfo * pAccount, SA_PARAM_NN_VALID AccountSearchRequest * pRequest)
{
	if (!verify(pAccount))
		return false;
	if (!verify(pRequest))
		return false;
	return accountSearch_MatchesAllDetails(pAccount, pRequest->pAccountName, pRequest->pDisplayName, pRequest->pEmail,
		pRequest->pFirstName, pRequest->pLastName, pRequest->pProduct, pRequest->pKey, pRequest->pCardName,
		pRequest->pCardFirstSix, pRequest->pCardLastFour, pRequest->pStreetAddress, pRequest->pCity, pRequest->pState, pRequest->pZip);
}

typedef struct AccountSearchAsyncCBInfo
{
	AccountSearchRequest * pRequest;
	NonBlockingQueryCB pWrappedCB;
	void* pWrappedUserData;
} AccountSearchAsyncCBInfo;

void accountSearch_AsyncCB(U32 **ppOutContainerIDs, NonBlockingContainerIterationSummary *pSummary, AccountSearchAsyncCBInfo *pCBInfo)
{
	pCBInfo->pWrappedCB(ppOutContainerIDs, pSummary, pCBInfo->pWrappedUserData);

	StructDestroy(parse_AccountSearchRequest, pCBInfo->pRequest);
	free(pCBInfo);
}

// search for matches asynchronously
void accountSearch_Async(
	char *pAccountName,
	char *pDisplayName,
	char *pEmail,
	char *pFirstName,
	char *pLastName,
	char *pProduct,
	char *pKey,
	char *pCardName,
	char *pCardFirstSix,
	char *pCardLastFour,
	char *pStreetAddress,
	char *pCity,
	char *pState,
	char *pZip,
	int iMaxToReturn,
	NonBlockingQueryCB pCompleteCB,
	void *pUserData)
{
	AccountSearchRequest * pRequest = StructCreate(parse_AccountSearchRequest);
	AccountSearchAsyncCBInfo *pCBInfo = callocStruct(AccountSearchAsyncCBInfo);

#define ACCOUNTSEARCH_VALID_ESTR_CREATE(x) pRequest->x = estrDup(x)
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pAccountName);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pDisplayName);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pEmail);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pFirstName);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pLastName);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pProduct);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pKey);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pCardName);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pCardFirstSix);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pCardLastFour);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pStreetAddress);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pCity);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pState);
	ACCOUNTSEARCH_VALID_ESTR_CREATE(pZip);
#undef ACCOUNTSEARCH_VALID_ESTR_CREATE

	pCBInfo->pRequest = pRequest;
	pCBInfo->pWrappedCB = pCompleteCB;
	pCBInfo->pWrappedUserData = pUserData;

	NonBlockingContainerSearch(GLOBALTYPE_ACCOUNT, accountSearch_MatchesAllDetailsStruct, NULL, pRequest, iMaxToReturn, accountSearch_AsyncCB, pCBInfo);
}

// search for matches, using expression instead (slower)
void accountSearch_AsyncExpression(
	const char *pAccountName,
	const char *pDisplayName,
	const char *pEmail,
	const char *pFirstName,
	const char *pLastName,
	const char *pProduct,
	const char *pKey,
	const char *pCardName,
	const char *pCardFirstSix,
	const char *pCardLastFour,
	const char *pStreetAddress,
	const char *pCity,
	const char *pState,
	const char *pZip,
	int iMaxToReturn,
	NonBlockingQueryCB pCompleteCB,
	void *pUserData)
{
	char *pExpression = NULL;

#define EXPR_CONCAT_AND(expr) if (expr) estrConcatf(&expr, " and ");

	if (SAFE_DEREF(pAccountName) || SAFE_DEREF(pDisplayName))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesAccountOrDisplay(me, \"%s\", \"%s\")", NULL_TO_EMPTY(pAccountName), NULL_TO_EMPTY(pDisplayName));
	}

	if (SAFE_DEREF(pEmail))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesEmail(me, \"%s\")", NULL_TO_EMPTY(pEmail));
	}

	if (SAFE_DEREF(pFirstName) || SAFE_DEREF(pLastName))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesName(me, \"%s\", \"%s\")", NULL_TO_EMPTY(pFirstName), NULL_TO_EMPTY(pLastName));
	}

	if (SAFE_DEREF(pProduct))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesProduct(me, \"%s\")", NULL_TO_EMPTY(pProduct));
	}

	if (SAFE_DEREF(pKey))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesKey(me, \"%s\")", NULL_TO_EMPTY(pKey));
	}

	if (SAFE_DEREF(pCardName) || SAFE_DEREF(pCardFirstSix) || SAFE_DEREF(pCardLastFour))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesCreditCard(me, \"%s\", \"%s\", \"%s\")", NULL_TO_EMPTY(pCardName), NULL_TO_EMPTY(pCardFirstSix), NULL_TO_EMPTY(pCardLastFour));
	}

	if (SAFE_DEREF(pStreetAddress) || SAFE_DEREF(pCity) || SAFE_DEREF(pState) || SAFE_DEREF(pZip))
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "AccountSearch_MatchesAddress(me, \"%s\", \"%s\", \"%s\", \"%s\")", NULL_TO_EMPTY(pStreetAddress), NULL_TO_EMPTY(pCity), NULL_TO_EMPTY(pState), NULL_TO_EMPTY(pZip));
	}

#undef EXPR_CONCAT_AND

	NonBlockingContainerQuery(GLOBALTYPE_ACCOUNT, pExpression, "Account", iMaxToReturn, pCompleteCB, pUserData);
	estrDestroy(&pExpression);
}

#include "AutoGen/AccountSearch_c_ast.c"