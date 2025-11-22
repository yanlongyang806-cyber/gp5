#pragma once
#include "browser.h"

bool BrowserLibCEFInit(HWND windowHandle);
bool BrowserLibCEFShutdown(void);

HWND BrowserLibCEFGetHtmlDocHWND(void);

bool BrowserLibCEFGetCurrentURL(SA_PARAM_NN_STR char **currentURLOut); // caller must call GlobalFree on *currentURLOut;

bool BrowserLibCEFGetElementInnerHTML(SA_PARAM_NN_STR const char *elementName, SA_PRE_GOOD SA_POST_OP_OP_STR char **msgOut); // callback must call GlobalFree on *msgOut;
bool BrowserLibCEFSetElementInnerHTML(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *msgIn);

bool BrowserLibCEFSetElementClassName(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *classNameIn);

bool BrowserLibCEFSetElementCSSText(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *cssText);

bool BrowserLibCEFGetInputElementValue(SA_PARAM_NN_STR const char *inputElementName, SA_PRE_GOOD SA_POST_OP_OP_STR const char **valueTextOut); // caller must call GlobalFree on *valueTextOut;
bool BrowserLibCEFSetInputElementValue(SA_PARAM_NN_STR const char *inputElementName, SA_PARAM_NN_STR const char *valueTextIn, bool assertOnFail);

bool BrowserLibCEFSetSelectElementValue(SA_PARAM_NN_STR const char *selectElementName, SA_PARAM_NN_STR const char *valueTextIn);
int BrowserLibCEFSetSelectElementOptions(SA_PARAM_NN_STR const char *selectElementName, OptionCallbackFunc optionCallbackFunc, void *userOptionData);
bool BrowserLibCEFGetSelectElementValue(SA_PARAM_NN_STR const char *selectElementName, SA_PRE_GOOD SA_POST_OP_OP_STR const char **valueTextOut); // caller must call GlobalFree on *valueTextOut;

bool BrowserLibCEFSetSelectElementOnChangeCallback(SA_PARAM_NN_STR const char *selectElementName, OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData);

bool BrowserLibCEFFocusElement(SA_PARAM_NN_STR const char *elementName);

bool BrowserLibCEFExistsElement(SA_PARAM_NN_STR const char *elementName);

BOOL BrowserLibCEFMessageCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool BrowserLibCEFProcessKeystrokes(MSG msg, SimpleWindow *pWindow);
bool BrowserLibCEFProcessMouse(MSG msg);
bool BrowserLibCEFProcessFocus(MSG msg);

bool BrowserLibCEFInvokeScript(const char *scriptName, InvokeScriptArgType firstType, va_list args);
char *BrowserLibCEFInvokeScriptString(const char *scriptName, InvokeScriptArgType firstType, va_list args);
bool BrowserLibCEFDisplayHTMLFromURL(SA_PARAM_NN_STR const char *webPageURL, SA_PARAM_OP_STR const char **eaEstrKeyValuePostData);
bool BrowserLibCEFDisplayHTMLStr(SA_PARAM_NN_STR const char *htmlString);

void BrowserLibCEFUpdate();
bool BrowserLibCEFPaint(HWND hwnd);
