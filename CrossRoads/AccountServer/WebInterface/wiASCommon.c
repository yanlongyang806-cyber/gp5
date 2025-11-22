#include "wiASCommon.h"

#include "AccountServer.h"
#include "httputil.h"
#include "StringUtil.h"
#include "timing.h"
#include "WebInterface.h"
#include "WebInterface/wiCommon.h"
#include "wiAccounts.h"
#include "wiProducts.h"
#include "wiProductKeys.h"
#include "wiSubscriptions.h"
#include "wiAdmin.h"

#include "AutoGen/wiASCommon_h_ast.h"
#include "AutoGen/wiASCommon_c_ast.h"

AUTO_STRUCT;
typedef struct WICMessageBox
{
	const char *pSubject; AST(UNOWNED)
	const char *pMessage; AST(UNOWNED)
	const char *pType;	  AST(UNOWNED)
	const char *pReferer; AST(UNOWNED)
} WICMessageBox;

void wiAppendMessageBox(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
						SA_PARAM_NN_STR const char *pSubject,
						SA_PARAM_NN_STR const char *pMessage,
						WebMessageBoxFlags options)
{
	WICMessageBox messageBox = {0};

	if (!verify(pWebRequest)) return;
	if (!verify(pSubject)) return;
	if (!verify(pMessage)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_WICMessageBox, &messageBox);

	if (options & WMBF_Error)
	{
		messageBox.pType = "error";
	}
	else
	{
		messageBox.pType = "highlight";
	}

	messageBox.pSubject = pSubject;
	messageBox.pMessage = pMessage;

	if (options & WMBF_BackButton)
	{
		messageBox.pReferer = wiGetHeader(pWebRequest, "Referer");
	}

	wiAppendStruct(pWebRequest, "MessageBox.cs", parse_WICMessageBox, &messageBox);

	StructDeInit(parse_WICMessageBox, &messageBox);

	PERFINFO_AUTO_STOP_FUNC();
}

static bool wiHandleHTTPError(SA_PARAM_NN_VALID WICWebRequest *pWebRequest, WIResult eResult)
{
	switch (eResult)
	{
		xcase WIR_Forbidden:
			wiAppendMessageBox(pWebRequest, "403: Forbidden", "You do not have sufficient access to view the requested page.", WMBF_Error);
		xcase WIR_NotFound:
			wiAppendMessageBox(pWebRequest, "404: Page Not Found", "The page you have requested could not be found.", WMBF_Error);
	}
	return true;
}

static bool wiHandleDefaultRequest(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "index.html");

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

static bool wiHandleLegacy(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

	hrCallLegacyHandler(wiGetHttpRequest(pWebRequest), httpLegacyHandlePost, httpLegacyHandleGet);
	wiSetResult(pWebRequest, WIR_Legacy);

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

static WIAccessLevel aswiGetAccessLevel(int iAccessLevel)
{
	if (iAccessLevel == 9)
		return WIAL_SuperAdmin;
	if (iAccessLevel == 8)
		return WIAL_Admin;
	if (iAccessLevel == 7)
		return WIAL_Normal;
	if (iAccessLevel >= 0)
		return WIAL_Limited;
	return WIAL_Invalid;
}

static bool aswiHandleWebRequest(WICWebRequest *pWebRequest)
{
	bool bHandled = false;
	HttpRequest *pReq = wiGetHttpRequest(pWebRequest);

	if (!stricmp_safe(pReq->path, "/") || !stricmp_safe(pReq->path, "/index" WI_EXTENSION))
	{
		bHandled = wiHandleDefaultRequest(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_ACCOUNTS_DIR))
	{
		bHandled = wiHandleAccounts(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_SUBSCRIPTIONS_DIR))
	{
		bHandled = wiHandleSubscriptions(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_PRODUCTS_DIR))
	{
		bHandled = wiHandleProducts(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_PRODUCTKEYS_DIR))
	{
		bHandled = wiHandleProductKeys(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_ADMIN_DIR))
	{
		bHandled = wiHandleAdmin(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_LEGACY_DIR))
	{
		bHandled = wiHandleLegacy(pWebRequest);
	}
	return bHandled;
}

#define BASE_SITE_TEMPLATE "WebSite.cs"

extern int gHttpPort;
void accountServerHttpInit(unsigned int port)
{
	WICommonSettings webSettings = {aswiHandleWebRequest, aswiGetAccessLevel, wiHandleHTTPError, BASE_SITE_TEMPLATE};

	wiCommonInitDefaultDirectories("server/AccountServer/WebRoot/", "server/AccountServer/templates/");
	wiCommonHttpInit(port, ACCOUNT_SERVER_INTERNAL_NAME, ACCOUNT_SERVER_VERSION, &webSettings);
}

#include "AutoGen/wiASCommon_h_ast.c"
#include "AutoGen/wiASCommon_c_ast.c"