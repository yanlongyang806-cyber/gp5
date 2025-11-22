#include "wiRFCommon.h"

#include "RevisionFinder.h"
#include "AutoGen/RevisionFinder_h_ast.h"
#include "httputil.h"
#include "StringUtil.h"
#include "timing.h"
#include "WebInterface/wiCommon.h"
#include "wiRevisions.h"

#include "AutoGen/wiRFCommon_h_ast.h"
#include "AutoGen/wiRFCommon_c_ast.h"

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
	rfSearchResponse emptyResponse = {0};
	StructInit(parse_rfSearchResponse,&emptyResponse);
	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

	wiAppendStruct(pWebRequest,"RevisionView.cs",parse_rfSearchResponse,&emptyResponse);

	PERFINFO_AUTO_STOP_FUNC();
	StructDeInit(parse_rfSearchResponse,&emptyResponse);
	return true;
}

static bool rfwiHandleWebRequest(WICWebRequest *pWebRequest)
{
	bool bHandled = false;
	HttpRequest *pReq = wiGetHttpRequest(pWebRequest);

	if (!stricmp_safe(pReq->path, "/") || !stricmp_safe(pReq->path, "/index" WI_EXTENSION))
	{
		bHandled = wiHandleDefaultRequest(pWebRequest);
	}
	else if (strStartsWith(pReq->path, WI_FIND_REVISION_DIR))
	{
		bHandled = wiHandleRevisions(pWebRequest);
	}
	
	return bHandled;
}

#define BASE_SITE_TEMPLATE "WebSite.cs"

extern int gHttpPort;
void revisionFinderHttpInit(unsigned int port)
{
	WICommonSettings webSettings = {rfwiHandleWebRequest, NULL, wiHandleHTTPError, BASE_SITE_TEMPLATE};

	wiCommonInitDefaultDirectories("server/RevisionFinder/WebRoot/", "server/RevisionFinder/templates/");
	wiCommonHttpInit(port, REVISION_FINDER_INTERNAL_NAME, REVISION_FINDER_VERSION, &webSettings);
}

#include "AutoGen/wiRFCommon_h_ast.c"
#include "AutoGen/wiRFCommon_c_ast.c"