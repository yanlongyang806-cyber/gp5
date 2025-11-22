#include "wiAccounts.h"
#include "AccountIntegration.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountServer.h"
#include "AccountServerConfig.h"
#include "HttpLib.h"
#include "StringUtil.h"
#include "timing.h"
#include "WebInterface/wiCommon.h"
#include "WikiToHTML.h"

#include "AutoGen/wiAccounts_c_ast.h"

extern int giServerMonitorPort;

/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleAccountsIndex(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* View                                                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIActivityLog
{
	char *pMessage; AST(ESTRING)
	U32 uTime;
} ASWIActivityLog;

AUTO_STRUCT;
typedef struct ASWIAccount
{
	const char *pSelf;					AST(UNOWNED)
	const AccountInfo *pAccount;		AST(UNOWNED)
	const PWCommonAccount *pPWEAccount;	AST(UNOWNED)
	const char *pEmployeeStatusText;	AST(UNOWNED)
	char *pServerMonitorLink;			AST(ESTRING)
	char *pLegacyLink;					AST(ESTRING)
	EARRAY_OF(ASWIActivityLog) eaActivities;
} ASWIAccount;

static void wiHandleAccountsView(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	const char *pSelf = "view" WI_EXTENSION;
	ASWIAccount aswiAccount = {0};
	U32 uAccountID = 0;
	bool bRedirect = false;

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIAccount, &aswiAccount);

	aswiAccount.pSelf = pSelf;

	// First, try account ID
	uAccountID = wiGetInt(pWebRequest, "id", 0);
	if (uAccountID)
	{
		aswiAccount.pAccount = findAccountByID(uAccountID);
	}
	
	// Then try Cryptic account name
	if (!aswiAccount.pAccount)
	{
		const char * pName = wiGetString(pWebRequest, "name");
		aswiAccount.pAccount = nullStr(pName) ? NULL : findAccountByName(pName);
	}

	// Then try Cryptic display name
	if (!aswiAccount.pAccount)
	{
		const char * pName = wiGetString(pWebRequest, "display_name");
		aswiAccount.pAccount = nullStr(pName) ? NULL : findAccountByDisplayName(pName);
	}

	// Then Cryptic GUID
	if (!aswiAccount.pAccount)
	{
		const char * pGUID = wiGetString(pWebRequest, "guid");
		aswiAccount.pAccount = nullStr(pGUID) ? NULL : findAccountByGUID(pGUID);
	}

	// Then Cryptic e-mail address
	if (!aswiAccount.pAccount)
	{
		const char * pEmail = wiGetString(pWebRequest, "email");
		aswiAccount.pAccount = nullStr(pEmail) ? NULL : findAccountByEmail(pEmail);
	}

	// Then PWE name
	if (!aswiAccount.pAccount)
	{
		const char * pName = wiGetString(pWebRequest, "pwe_name");
		aswiAccount.pPWEAccount = nullStr(pName) ? NULL : findPWCommonAccountByName(pName);
	}

	// Then PWE e-mail
	if (!aswiAccount.pAccount && !aswiAccount.pPWEAccount)
	{
		const char * pEmail = wiGetString(pWebRequest, "pwe_email");
		aswiAccount.pPWEAccount = nullStr(pEmail) ? NULL : findPWCommonAccountByEmail(pEmail);
	}
	
	// Found a PWE account but no Cryptic account
	if (!aswiAccount.pAccount && aswiAccount.pPWEAccount)
	{
		aswiAccount.pAccount = findAccountByID(aswiAccount.pPWEAccount->uLinkedID);
	}

	// Found a Cryptic account but no PWE account
	if (!aswiAccount.pPWEAccount && aswiAccount.pAccount)
	{
		aswiAccount.pPWEAccount = findPWCommonAccountByName(aswiAccount.pAccount->pPWAccountName);
	}

	if (aswiAccount.pAccount)
	{
		EARRAY_OF(const AccountLogEntry) eaEntries = NULL;

		accountGetLogEntries(aswiAccount.pAccount, &eaEntries, 0, 0);
		EARRAY_CONST_FOREACH_BEGIN(eaEntries, i, s);
		{
			ASWIActivityLog * pActivityLog = StructCreate(parse_ASWIActivityLog);
			pActivityLog->pMessage = wikiToHTML(eaEntries[i]->pMessage);
			pActivityLog->uTime = eaEntries[i]->uSecondsSince2000;
			eaPush(&aswiAccount.eaActivities, pActivityLog);
		}
		EARRAY_FOREACH_END;
		eaDestroy(&eaEntries);

		aswiAccount.pEmployeeStatusText = StaticDefineIntRevLookup(EmployeeStatusEnum, aswiAccount.pAccount->employeeStatus);

		estrPrintf(&aswiAccount.pServerMonitorLink, "http://%s:%u/viewxpath?xpath=AccountServer[1].globObj.Account[%u]",
			getHostName(), giServerMonitorPort, aswiAccount.pAccount->uID);

		estrPrintf(&aswiAccount.pLegacyLink, "/legacy/detail?id=%u", aswiAccount.pAccount->uID);

		// toggle employee status
		if (wiSubmitted(pWebRequest, "toggleEmployeeStatus"))
		{
			AccountInfo *pAccount = findAccountByID(aswiAccount.pAccount->uID);
			changeAccountEmployeeStatus(pAccount,
				aswiAccount.pAccount->employeeStatus ? EMPLOYEESTATUS_NOT_EMPLOYEE : EMPLOYEESTATUS_VALID,
				wiGetUsername(pWebRequest),
				"Account Server web interface");
			wiCommonRedirectf(pWebRequest, "%s?id=%d", pSelf, pAccount->uID);
			bRedirect = true;
		}
	}

	if (!bRedirect)
	{
		wiAppendStruct(pWebRequest, "AccountsView.cs", parse_ASWIAccount, &aswiAccount);
	}

	StructDeInit(parse_ASWIAccount, &aswiAccount);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Search                                                               */
/************************************************************************/

static void wiHandleAccountsSearch(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Create                                                               */
/************************************************************************/

static void wiHandleAccountsCreate(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}

/************************************************************************/
/* EmployeeStatus                                                       */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIAccountInfoWrapper
{
	AccountInfo *pAccountInfo;					AST(UNOWNED)
	const char *pEmployeeStatusText;			AST(UNOWNED)
	PWCommonAccount *pPWAccount;				AST(UNOWNED)
} ASWIAccountInfoWrapper;

AUTO_STRUCT;
typedef struct ASWIEmployeeStatusList
{
	const char *pSelf;								AST(UNOWNED)
	const char *pViewPage;							AST(UNOWNED)
	AccountServerConfig *pAccountServerConfig;		AST(UNOWNED)
	EARRAY_OF(ASWIAccountInfoWrapper) eaEntries;
} ASWIEmployeeStatusList;

static void wiHandleAccountsEmployeeStatus(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	const char *pSelf = "employeeStatus" WI_EXTENSION;
	const char *pViewPage = "view" WI_EXTENSION;
	EARRAY_OF(AccountInfo) eaAccounts = NULL;
	ASWIEmployeeStatusList aswiEmployeeStatusList = {0};
	

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIEmployeeStatusList, &aswiEmployeeStatusList);

	aswiEmployeeStatusList.pViewPage = pViewPage;
	aswiEmployeeStatusList.pSelf = pSelf;
	aswiEmployeeStatusList.pAccountServerConfig = GetAccountServerConfig();
	eaAccounts = getEmployeeStatusAccountList();
	EARRAY_CONST_FOREACH_BEGIN(eaAccounts, iCurAccount, iNumAccounts);
	{
		AccountInfo *pAccount = eaAccounts[iCurAccount];
		ASWIAccountInfoWrapper *pAccountInfoWrapper = StructCreate(parse_ASWIAccountInfoWrapper);
		pAccountInfoWrapper->pAccountInfo = pAccount;
		pAccountInfoWrapper->pEmployeeStatusText = StaticDefineIntRevLookup(EmployeeStatusEnum, pAccount->employeeStatus);
		pAccountInfoWrapper->pPWAccount = findPWCommonAccountByName(pAccount->pPWAccountName);
		eaPush(&aswiEmployeeStatusList.eaEntries, pAccountInfoWrapper);
	}
	EARRAY_FOREACH_END;

	wiAppendStruct(pWebRequest, "EmployeeStatus.cs", parse_ASWIEmployeeStatusList, &aswiEmployeeStatusList);

	eaDestroy(&eaAccounts); // DO NOT FREE CONTENTS
	StructDeInit(parse_ASWIEmployeeStatusList, &aswiEmployeeStatusList);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleAccounts(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_ACCOUNT_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_ACCOUNTS_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleAccounts##page(pWebRequest); \
		bHandled = true; \
	}

	WI_ACCOUNT_PAGE(Index);
	WI_ACCOUNT_PAGE(View);
	WI_ACCOUNT_PAGE(Search);
	WI_ACCOUNT_PAGE(Create);
	WI_ACCOUNT_PAGE(EmployeeStatus);

#undef WI_ACCOUNT_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}

#include "AutoGen/wiAccounts_c_ast.c"