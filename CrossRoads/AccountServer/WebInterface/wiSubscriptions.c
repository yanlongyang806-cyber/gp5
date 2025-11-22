#include "wiSubscriptions.h"
#include "wiSubscriptions_c_ast.h"
#include "WebInterface/wiCommon.h"
#include "timing.h"
#include "StringUtil.h"
#include "Subscription.h"


/************************************************************************/
/* Subscription list support                                            */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWISubscriptionsListEntry
{
	const char *pName;			AST(UNOWNED)
	const char *pInternalName;	AST(UNOWNED)
	char *pPeriod;				AST(ESTRING)
	const char *pDescription;	AST(UNOWNED)
} ASWISubscriptionsListEntry;

AUTO_STRUCT;
typedef struct ASWISubscriptionsList
{
	const char *pViewPage;	AST(UNOWNED)
	const char *pSelf;		AST(UNOWNED)
	EARRAY_OF(ASWISubscriptionsListEntry) eaEntries;
} ASWISubscriptionsList;

static void wiAddSubscriptionToList(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
							   SA_PARAM_NN_VALID ASWISubscriptionsList *pSubsList,
							   SA_PARAM_NN_VALID const SubscriptionContainer *pSub)
{
	ASWISubscriptionsListEntry *pEntry = NULL;

	if (!verify(pWebRequest)) return;
	if (!verify(pSubsList)) return;
	if (!verify(pSub)) return;

	PERFINFO_AUTO_START_FUNC();

	pEntry = StructCreate(parse_ASWISubscriptionsListEntry);

	pEntry->pName = pSub->pName;
	pEntry->pInternalName = pSub->pInternalName;
	pEntry->pDescription = pSub->pDescription;

	estrPrintf(&pEntry->pPeriod, "%d %s", pSub->iPeriodAmount, getSubscriptionPeriodName(pSub->periodType));

	eaPush(&pSubsList->eaEntries, pEntry);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleSubscriptionsIndex(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* View                                                                 */
/************************************************************************/

static void wiHandleSubscriptionsView(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* List                                                                 */
/************************************************************************/

static void wiHandleSubscriptionsList(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	const char *pSelf = "list" WI_EXTENSION;
	const char *pViewPage = "view" WI_EXTENSION;
	EARRAY_OF(SubscriptionContainer) eaSubs = NULL;
	ASWISubscriptionsList aswiSubsList = {0};

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	eaSubs = getSubscriptionList();
	EARRAY_CONST_FOREACH_BEGIN(eaSubs, iCurSub, iNumSubs);
	{
		const SubscriptionContainer *pSub = eaSubs[iCurSub];
		wiAddSubscriptionToList(pWebRequest, &aswiSubsList, pSub);
	}
	EARRAY_FOREACH_END;

	wiAppendStruct(pWebRequest, "SubscriptionsList.cs", parse_ASWISubscriptionsList, &aswiSubsList);

	StructDeInit(parse_ASWISubscriptionsList, &aswiSubsList);

	eaDestroy(&eaSubs); // DO NOT FREE CONTENTS

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Create                                                               */
/************************************************************************/

static void wiHandleSubscriptionsCreate(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleSubscriptions(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_SUBSCRIPTION_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_SUBSCRIPTIONS_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleSubscriptions##page(pWebRequest); \
		bHandled = true; \
	}

	WI_SUBSCRIPTION_PAGE(Index);
	WI_SUBSCRIPTION_PAGE(View);
	WI_SUBSCRIPTION_PAGE(List);
	WI_SUBSCRIPTION_PAGE(Create);

#undef WI_SUBSCRIPTION_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}

#include "wiSubscriptions_c_ast.c"