// CrypticLauncher
#include "browser.h"
#include "browser_ie.h"
#include "browser_libcef.h"
#include "LauncherMain.h"
#include "UI.h"

// UtilitiesLib
#include "utils.h"
#include "sysutil.h"
#include "EString.h"
#include "EArray.h"
#include "SimpleWindowManager.h"
#include "AppLocale.h"

#if ENABLE_LIBCEF
static bool sbUseIE = false;
#else
static bool sbUseIE = true;
#endif

AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void UseLibCEF_IfAvailable(int value)
{
#if ENABLE_LIBCEF
	sbUseIE = !value;
#endif
}

static const char *sLanguageCode = NULL;

static const char *sVisibilityHiddenCSSText = "visibility:hidden"; // visibility:hidden hides an element, but it will still take up the same space as before. The element will be hidden, but still not affect the layout.
static const char *sVisibilityCSSText = "visibility:"; // visibility: unhides an element.
static const char *sDisplayNoneCSSText = "display:none"; // display:none hides an element, and it will not take up any space. The element will be hidden, and the page will be displayed as if the element is not there.
static const char *sDisplayCSSText = "display:"; // display: unhides an element.

typedef enum CrypticBrowserState
{
	CB_STATE_UNINITIALIZED,
	CB_STATE_PAGE_LOADING,
	CB_STATE_PAGE_COMPLETE
}
CrypticBrowserState;

static const char* sStateString[] = 
{
	"Uninitialized",			//CB_STATE_UNINITIALIZED,
	"Page Loading",				//CB_STATE_PAGE_LOADING
	"Page Complete"				//CB_STATE_PAGE_COMPLETE
};

static CrypticBrowserState sBrowserState = CB_STATE_UNINITIALIZED;

static void BrowserSetState(CrypticBrowserState newState)
{
	if (gDebugMode)
	{
		printf("Browser State Transition - '%s' -> '%s'\n", sStateString[sBrowserState], sStateString[newState]);
	}
	sBrowserState = newState;
}

static CrypticBrowserState BrowserGetState(void)
{
	return sBrowserState;
}

// Temporary
bool BrowserIsIE(void)
{
	return sbUseIE;
}

bool BrowserInit(HWND windowHandle)
{
	bool retVal = false;

	if (sbUseIE)
	{
		sbUseIE = BrowserIEInit(windowHandle);
		retVal = sbUseIE;
	}

	if (!sbUseIE)
	{
		retVal = BrowserLibCEFInit(windowHandle);
	}

	if (retVal && BrowserGetState() == CB_STATE_UNINITIALIZED)
	{
		BrowserSetState(CB_STATE_PAGE_COMPLETE);
	}
	return retVal;
}

bool BrowserShutdown(void)
{
	bool retVal;

	if (sbUseIE)
	{
		retVal = BrowserIEShutdown();
	}
	else
	{
		retVal = BrowserLibCEFShutdown();
	}

	if (retVal)
	{
		BrowserSetState(CB_STATE_UNINITIALIZED);
	}
	else if (gDebugMode && BrowserGetState() != CB_STATE_UNINITIALIZED )
	{
		printf("BrowserShutdown failed while browser in state %s\n", sStateString[BrowserGetState()]);
	}

	return retVal;
}

void BrowserSetLanguageCode(BrowserLanguage browserLanguage)
{
	sLanguageCode = locGetIETFLanguageTag(browserLanguage);
}

void BrowserAppendStandardDataToHeader(char ***headers)
{
	eaPush(headers, estrCreateFromStr("X-Accept-Language-Cryptic"));
	eaPush(headers, estrCreateFromStr(sLanguageCode));

	eaPush(headers, estrCreateFromStr("User-Agent"));
	eaPush(headers, estrCreateFromStr("Mozilla/4.0 (compatible; CrypticLauncher)"));
}

HWND BrowserGetHtmlDocHWND(void)
{
	if (sbUseIE)
	{
		return BrowserIEGetHtmlDocHWND();
	}
	else
	{
		return BrowserLibCEFGetHtmlDocHWND();
	}
}

bool BrowserGetCurrentURL(char **currentURLOut)
{
	if (sbUseIE)
	{
		return BrowserIEGetCurrentURL(currentURLOut);
	}
	else
	{
		return BrowserLibCEFGetCurrentURL(currentURLOut);
	}
}

bool BrowserGetElementInnerHTML(const char *elementName, char **msgOut)
{
	if (sbUseIE)
	{
		return BrowserIEGetElementInnerHTML(elementName, msgOut);
	}
	else
	{
		return BrowserLibCEFGetElementInnerHTML(elementName, msgOut);
	}
}

bool BrowserSetElementInnerHTML(const char *elementName, const char *msgIn)
{
	if (sbUseIE)
	{
		return BrowserIESetElementInnerHTML(elementName, msgIn);
	}
	else
	{
		return BrowserLibCEFSetElementInnerHTML(elementName, msgIn);
	}
}

bool BrowserSetElementClassName(const char *elementName, const char *classNameIn)
{
	if (sbUseIE)
	{
		return BrowserIESetElementClassName(elementName, classNameIn);
	}
	else
	{
		return BrowserLibCEFSetElementClassName(elementName, classNameIn);
	}
}

static bool BrowserSetElementCSSText(const char *elementName, const char *cssText)
{
	if (sbUseIE)
	{
		return BrowserIESetElementCSSText(elementName, cssText);
	}
	else
	{
		return BrowserLibCEFSetElementCSSText(elementName, cssText);
	}
}

bool BrowserSetElementVisible(const char *elementName, bool bVisible)
{
	return BrowserSetElementCSSText(elementName, bVisible ? sVisibilityCSSText : sVisibilityHiddenCSSText);
}

bool BrowserSetElementDisplay(const char *elementName, bool bDisplay)
{
	return BrowserSetElementCSSText(elementName, bDisplay ? sDisplayCSSText : sDisplayNoneCSSText);
}

bool BrowserGetInputElementValue(const char *inputElementName, const char **valueTextOut)
{
	if (sbUseIE)
	{
		return BrowserIEGetInputElementValue(inputElementName, valueTextOut);
	}
	else
	{
		return BrowserLibCEFGetInputElementValue(inputElementName, valueTextOut);
	}
}

bool BrowserSetInputElementValue(const char *inputElementName, const char *valueTextIn, bool assertOnFail)
{
	if (sbUseIE)
	{
		return BrowserIESetInputElementValue(inputElementName, valueTextIn);
	}
	else
	{
		return BrowserLibCEFSetInputElementValue(inputElementName, valueTextIn, assertOnFail);
	}
}

bool BrowserSetSelectElementValue(const char *selectElementName, const char *valueTextIn)
{
	if (sbUseIE)
	{
		return BrowserIESetSelectElementValue(selectElementName, valueTextIn);
	}
	else
	{
		return BrowserLibCEFSetSelectElementValue(selectElementName, valueTextIn);
	}
}

// returns number of options added, or -1 on failure
int BrowserSetSelectElementOptions(const char *selectElementName, OptionCallbackFunc optionCallbackFunc, void *userOptionData)
{
	if (sbUseIE)
	{
		return BrowserIESetSelectElementOptions(selectElementName, optionCallbackFunc, userOptionData);
	}
	else
	{
		return BrowserLibCEFSetSelectElementOptions(selectElementName, optionCallbackFunc, userOptionData);
	}
}

bool BrowserGetSelectElementValue(const char *selectElementName, const char **valueTextOut)
{
	if (sbUseIE)
	{
		return BrowserIEGetSelectElementValue(selectElementName, valueTextOut);
	}
	else
	{
		return BrowserLibCEFGetSelectElementValue(selectElementName, valueTextOut);
	}
}

bool BrowserSetSelectElementOnChangeCallback(const char *selectElementName, OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData)
{
	if (sbUseIE)
	{
		return BrowserIESetSelectElementOnChangeCallback(selectElementName, onChangeCallbackFunc, userOnChangeData);
	}
	else
	{
		return BrowserLibCEFSetSelectElementOnChangeCallback(selectElementName, onChangeCallbackFunc, userOnChangeData);
	}
}

bool BrowserFocusElement(const char *elementName)
{
	if (sbUseIE)
	{
		return BrowserIEFocusElement(elementName);
	}
	else
	{
		return BrowserLibCEFFocusElement(elementName);
	}
}

bool BrowserExistsElement(const char *elementName)
{
	if (sbUseIE)
	{
		return BrowserIEExistsElement(elementName);
	}
	else
	{
		return BrowserLibCEFExistsElement(elementName);
	}
}

BOOL BrowserMessageCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	if(UI_GetShowTrayIcon(NULL))
		UI_ShowTrayIcon();

	if(pWindow->eWindowType == CL_WINDOW_MAIN && iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST && hDlg == BrowserGetHtmlDocHWND())
	{
		bool bInLoggedInState = LauncherIsInLoggedInState();
		bool bControlKeyDepressed = GetKeyState(VK_CONTROL) & 0x8000 ? true : false;

		if(bInLoggedInState && bControlKeyDepressed && iMsg == WM_KEYDOWN && wParam == 'X')
		{
			PostMessage(pWindow->hWnd, WM_APP, CLMSG_OPEN_XFER_DEBUG, 0);
			return TRUE;
		}
		else if(bInLoggedInState && bControlKeyDepressed && wParam == 'C')
		{
			LauncherCreateConsoleWindow();
		}
	}

	if (sbUseIE)
	{
		return BrowserIEMessageCallback(hDlg, iMsg, wParam, lParam, pWindow);
	}
	else
	{
		return BrowserLibCEFMessageCallback(hDlg, iMsg, wParam, lParam, pWindow);
	}
}

bool BrowserProcessKeystrokes(MSG msg, SimpleWindow *pWindow)
{
	switch(msg.message)
	{
		case WM_KEYDOWN:
		case WM_CHAR:
		case WM_DEADCHAR:
		case WM_SYSKEYDOWN:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
		case WM_KEYLAST:
			// Do not process repeated keys
			// From http://msdn.microsoft.com/en-us/library/ms646280(VS.85).aspx
			// 30 - Specifies the previous key state. The value is 1 if the key is down before the message is sent, or it is zero if the key is up.
			if(msg.lParam & 0x40000000)
				break;

			if(msg.wParam == VK_RETURN)
			{
				// IMPORTANT
				//
				// This code ensures that pressing Enter will login on the login form, start/stop patching, and play game,
				// depending on the current state of the UI.
				switch(LauncherGetState())
				{
					case CL_STATE_LOGINPAGELOADED:
						PostMessage(pWindow->hWnd, WM_APP, 4, 0);
						return true;
					case CL_STATE_WAITINGFORPATCH:
					case CL_STATE_READY:
						PostMessage(pWindow->hWnd, WM_APP, 2, 0);
						return true;
				}
			}
	}

	if (sbUseIE)
	{
		return BrowserIEProcessKeystrokes(msg, pWindow);
	}
	else
	{
		return BrowserLibCEFProcessKeystrokes(msg, pWindow);
	}
}

bool BrowserProcessMouse(MSG msg)
{
	if (sbUseIE)
	{
		// Noop
		return false;
	}
	else
	{
		return BrowserLibCEFProcessMouse(msg);
	}
}

bool BrowserProcessFocus(MSG msg)
{
	if (sbUseIE)
	{
		// Noop
		return false;
	}
	else
	{
		return BrowserLibCEFProcessFocus(msg);
	}
}

bool BrowserInvokeScript(const char *scriptName, InvokeScriptArgType firstType, ...)
{
	bool retVal;

	va_list args;
	va_start(args, firstType);
	if (sbUseIE)
	{
		char *ieScriptName = (char *)scriptName;
		char *locationOfLastPeriod = strrchr(scriptName, '.');

		if (locationOfLastPeriod)
		{
			ieScriptName = locationOfLastPeriod+1;
		}

		retVal = BrowserIEInvokeScript(ieScriptName, firstType, args);
	}
	else
	{
		retVal = BrowserLibCEFInvokeScript(scriptName, firstType, args);
	}
	va_end(args);

	return retVal;
}

char *BrowserInvokeScriptString(const char *scriptName, InvokeScriptArgType firstType, ...)
{
	char *retVal = NULL;

	va_list args;
	va_start(args, firstType);
	if (sbUseIE)
	{
		assertmsg(false, "IE intergration does not support calling JS functions and getting a return string value back");
	}
	else
	{
		retVal = BrowserLibCEFInvokeScriptString(scriptName, firstType, args);
	}
	va_end(args);

	return retVal;
}


bool BrowserDisplayHTMLFromURL(const char *webPageURL, const char **eaEstrKeyValuePostData)
{
	if (gDebugMode)
	{
		printf("Requested : %s\n", webPageURL);
	}

	if (sbUseIE)
	{
		return BrowserIEDisplayHTMLFromURL(webPageURL, eaEstrKeyValuePostData);
	}
	else
	{
		return BrowserLibCEFDisplayHTMLFromURL(webPageURL, eaEstrKeyValuePostData);
	}
}

bool BrowserDisplayHTMLStr(SA_PARAM_NN_STR const char *htmlString)
{
	if (gDebugMode)
	{
		printf("Requested : <raw page, no URL>\n");
	}

	if (sbUseIE)
	{
		return BrowserIEDisplayHTMLStr(htmlString);
	}
	else
	{
		return BrowserLibCEFDisplayHTMLStr(htmlString);
	}
}

void BrowserUpdate()
{
	if (sbUseIE)
	{
		// Noop
	}
	else
	{
		BrowserLibCEFUpdate();
	}
}

bool BrowserPaint(HWND hwnd)
{
	if (sbUseIE)
	{
		// Noop
		return false;
	}
	else
	{
		return BrowserLibCEFPaint(hwnd);
	}
}

void BrowserPageLoading()
{
	BrowserSetState(CB_STATE_PAGE_LOADING);
}

void BrowserPageComplete(const char* webPageURL)
{
	if (gDebugMode)
	{
		printf("Browser completed loading page '%s'\n", webPageURL);
	}
	BrowserSetState(CB_STATE_PAGE_COMPLETE);
}

bool BrowserIsPageComplete()
{
	return BrowserGetState() == CB_STATE_PAGE_COMPLETE;
}
