#pragma once
#include <wininclude.h> // temporary, for NMHDR and perhaps other stuff that should not be exposed in browser.h

typedef enum InvokeScriptArgType
{
	INVOKE_SCRIPT_ARG_NULL=0,
	INVOKE_SCRIPT_ARG_STRING,
	INVOKE_SCRIPT_ARG_STRING_OBJ, // use this for preformatted json object, in the form of a string
	INVOKE_SCRIPT_ARG_INT,
}
InvokeScriptArgType;

// this matches AppLocale's LocaleID enum - do not change
// this is duplicated so we can remove utilities lib dependency from browser (header)
typedef enum BrowserLanguage
{
	BL_ENGLISH = 0,
	BL_TCHINESE, // taiwan
	BL_KOREAN,
	BL_JAPANESE,
	BL_GERMAN,
	BL_FRENCH,
	BL_SPANISH,
	BL_ITALIAN,
	BL_RUSSIAN,
	BL_POLISH,
	BL_SCHINESE, // mainland china
	BL_TURKISH,
	BL_PORTUGUESE, // brazil
}
BrowserLanguage;

typedef struct SimpleWindow SimpleWindow;

typedef bool (*OptionCallbackFunc)(void *userOptionData, int userOptionIndex, bool *bShouldInsert, char *optionValue, size_t optionValueMaxSize, char *optionText, size_t optionTextMaxSize);
typedef bool (*OnChangeCallbackFunc)(void *userOnChangeData);

#define BROWSER_MAIN_PAGE_ERROR_MSG_PREFIX	_("There was an error loading the login page.  Please restart the launcher and try again.")

bool BrowserIsIE(void);
bool BrowserInit(HWND windowHandle);
bool BrowserShutdown(void);

void BrowserSetLanguageCode(BrowserLanguage browserLanguage);
void BrowserAppendStandardDataToHeader(char ***headers);

HWND BrowserGetHtmlDocHWND(void);

bool BrowserGetCurrentURL(SA_PARAM_NN_STR char **currentURLOut); // caller must call GlobalFree on *currentURLOut;

bool BrowserGetElementInnerHTML(SA_PARAM_NN_STR const char *elementName, SA_PRE_GOOD SA_POST_OP_OP_STR char **msgOut); // caller must call GlobalFree on *msgOut;
bool BrowserSetElementInnerHTML(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *msgIn);

bool BrowserSetElementClassName(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *classNameIn);

bool BrowserSetElementVisible(SA_PARAM_NN_STR const char *elementName, bool bVisible); // when invisible, removes element, leaves space for element
bool BrowserSetElementDisplay(SA_PARAM_NN_STR const char *elementName, bool bDisplay); // when not displayed, removes element, and does NOT leave space for element

bool BrowserGetInputElementValue(SA_PARAM_NN_STR const char *inputElementName, SA_PRE_GOOD SA_POST_OP_OP_STR const char **valueTextOut); // caller must call GlobalFree on *valueTextOut;
bool BrowserSetInputElementValue(SA_PARAM_NN_STR const char *inputElementName, SA_PARAM_NN_STR const char *valueTextIn, bool assertOnFail);

bool BrowserSetSelectElementValue(SA_PARAM_NN_STR const char *selectElementName, SA_PARAM_NN_STR const char *valueTextIn);
int BrowserSetSelectElementOptions(SA_PARAM_NN_STR const char *selectElementName, OptionCallbackFunc optionCallbackFunc, void *userOptionData);
bool BrowserGetSelectElementValue(SA_PARAM_NN_STR const char *selectElementName, SA_PRE_GOOD SA_POST_OP_OP_STR const char **valueTextOut); // caller must call GlobalFree on *valueTextOut;

bool BrowserSetSelectElementOnChangeCallback(SA_PARAM_NN_STR const char *selectElementName, OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData);

bool BrowserFocusElement(SA_PARAM_NN_STR const char *elementName);

bool BrowserExistsElement(SA_PARAM_NN_STR const char *elementName);

BOOL BrowserMessageCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

bool BrowserProcessKeystrokes(MSG msg, SimpleWindow *pWindow);
bool BrowserProcessMouse(MSG msg);
bool BrowserProcessFocus(MSG msg);

bool BrowserInvokeScript(SA_PARAM_NN_STR const char *scriptName, InvokeScriptArgType firstType, ...);
char *BrowserInvokeScriptString(const char *scriptName, InvokeScriptArgType firstType, ...);
bool BrowserDisplayHTMLFromURL(SA_PARAM_NN_STR const char *webPageURL, SA_PARAM_OP_STR const char **eaEstrKeyValuePostData);
bool BrowserDisplayHTMLStr(SA_PARAM_NN_STR const char *htmlString);

void BrowserUpdate();
bool BrowserPaint(HWND hwnd);

void BrowserPageLoading();
void BrowserPageComplete(const char* webPageURL);
bool BrowserIsPageComplete();

