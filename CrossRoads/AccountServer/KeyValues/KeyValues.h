#pragma once

#define USE_NEW_KVCODE 1

typedef struct AccountInfo AccountInfo;

typedef enum AccountKeyValueResult AccountKeyValueResult;

AccountKeyValueResult AccountKeyValue_GetEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, bool bAllowLock, SA_PARAM_OP_VALID S64 *piValue);
bool AccountKeyValue_IsLockedEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey);
AccountKeyValueResult AccountKeyValue_LockEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, S64 iValue, bool bModify, SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword, bool bAllowRelock);
AccountKeyValueResult AccountKeyValue_SetEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, S64 iValue, bool bModify, SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword, bool bAllowRelock);

AccountKeyValueResult AccountKeyValue_MoveEx(
	SA_PARAM_OP_STR const char *pProxy,
	SA_PARAM_NN_VALID const AccountInfo *pSrcAccount,
	SA_PARAM_NN_STR const char *pSrcKey,
	SA_PARAM_NN_VALID const AccountInfo *pDestAccount,
	SA_PARAM_NN_STR const char *pDestKey,
	S64 iValue,
	SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword,
	bool bAllowRelock);

AccountKeyValueResult AccountKeyValue_FinalizeEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_STR const char *pPassword, bool bCommit);
AccountKeyValueResult AccountKeyValue_Finalize(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_STR const char *pPassword, bool bCommit);
AccountKeyValueResult AccountKeyValue_ForceCommit(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey);

AccountKeyValueResult AccountKeyValue_RemoveEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, bool bForce);
AccountKeyValueResult AccountKeyValue_UnlockEx(SA_PARAM_OP_STR const char *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey);

SA_RET_OP_OP_STR STRING_EARRAY AccountKeyValue_GetAccountKeyListEx(SA_PARAM_NN_VALID const AccountInfo *pAccount);

void AccountKeyValue_MigrateLegacyStorage(SA_PARAM_NN_VALID const AccountInfo *account);
bool AccountKeyValue_ConvertKeyValue(SA_PARAM_NN_VALID const AccountInfo *account, SA_PARAM_NN_STR const char *pOldKey, SA_PARAM_NN_STR const char *pNewKey);

#define AccountKeyValue_Get(pProxy, pAccount, pKey, piValue) AccountKeyValue_GetEx(pProxy, pAccount, pKey, false, piValue)
#define AccountKeyValue_GetLocked(pProxy, pAccount, pKey, piValue) AccountKeyValue_GetEx(pProxy, pAccount, pKey, true, piValue)
#define AccountKeyValue_IsLocked(pProxy, pAccount, pKey) AccountKeyValue_IsLockedEx(pProxy, pAccount, pKey)

#define AccountKeyValue_LockOnce(pProxy, pAccount, pKey, ppInOutPassword) AccountKeyValue_LockEx(pProxy, pAccount, pKey, 0, true, ppInOutPassword, false)
#define AccountKeyValue_Lock(pProxy, pAccount, pKey, ppInOutPassword) AccountKeyValue_LockEx(pProxy, pAccount, pKey, 0, true, ppInOutPassword, true)
#define AccountKeyValue_LockAgain(pProxy, pAccount, pKey, ppInOutPassword) AccountKeyValue_LockEx(pProxy, pAccount, pKey, 0, true, ppInOutPassword, true)

#define AccountKeyValue_SetOnce(pProxy, pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pProxy, pAccount, pKey, iValue, false, ppInOutPassword, false)
#define AccountKeyValue_Set(pProxy, pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pProxy, pAccount, pKey, iValue, false, ppInOutPassword, true)

#define AccountKeyValue_ChangeOnce(pProxy, pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pProxy, pAccount, pKey, iValue, true, ppInOutPassword, false)
#define AccountKeyValue_Change(pProxy, pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pProxy, pAccount, pKey, iValue, true, ppInOutPassword, true)

#define AccountKeyValue_MoveOnce(pProxy, pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword) \
	AccountKeyValue_MoveEx(pProxy, pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword, false)
#define AccountKeyValue_Move(pProxy, pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword) \
	AccountKeyValue_MoveEx(pProxy, pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword, true)

#define AccountKeyValue_Commit(pProxy, pAccount, pKey, pPassword) AccountKeyValue_Finalize(pProxy, pPassword, true)
#define AccountKeyValue_Rollback(pProxy, pAccount, pKey, pPassword) AccountKeyValue_Finalize(pProxy, pPassword, false)

#define AccountKeyValue_Remove(pProxy, pAccount, pKey) AccountKeyValue_RemoveEx(pProxy, pAccount, pKey, false)
#define AccountKeyValue_RemoveForce(pProxy, pAccount, pKey) AccountKeyValue_RemoveEx(pProxy, pAccount, pKey, true)

#define AccountKeyValue_Unlock(pProxy, pAccount, pKey) AccountKeyValue_UnlockEx(pProxy, pAccount, pKey)

#define AccountKeyValue_GetAccountKeyList(pAccount) AccountKeyValue_GetAccountKeyListEx(pAccount)
#define AccountKeyValue_DestroyAccountKeyList(eaKeys) eaDestroyEx(eaKeys, NULL)