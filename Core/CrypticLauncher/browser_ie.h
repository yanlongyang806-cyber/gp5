#pragma once
#include "browser.h"

bool BrowserIEInit(HWND windowHandle);
bool BrowserIEShutdown(void);

HWND BrowserIEGetHtmlDocHWND(void);

bool BrowserIEGetCurrentURL(SA_PARAM_NN_STR char **currentURLOut); // caller must call GlobalFree on *currentURLOut;

bool BrowserIEGetElementInnerHTML(SA_PARAM_NN_STR const char *elementName, SA_PRE_GOOD SA_POST_OP_OP_STR char **msgOut); // caller must call GlobalFree on *msgOut;
bool BrowserIESetElementInnerHTML(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *msgIn);

bool BrowserIESetElementClassName(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *classNameIn);

bool BrowserIESetElementCSSText(SA_PARAM_NN_STR const char *elementName, SA_PARAM_NN_STR const char *cssText);

bool BrowserIEGetInputElementValue(SA_PARAM_NN_STR const char *inputElementName, SA_PRE_GOOD SA_POST_OP_OP_STR const char **valueTextOut); // caller must call GlobalFree on *valueTextOut;
bool BrowserIESetInputElementValue(SA_PARAM_NN_STR const char *inputElementName, SA_PARAM_NN_STR const char *valueTextIn);

bool BrowserIESetSelectElementValue(SA_PARAM_NN_STR const char *selectElementName, SA_PARAM_NN_STR const char *valueTextIn);
int BrowserIESetSelectElementOptions(SA_PARAM_NN_STR const char *selectElementName, OptionCallbackFunc optionCallbackFunc, void *userOptionData);
bool BrowserIEGetSelectElementValue(SA_PARAM_NN_STR const char *selectElementName, SA_PRE_GOOD SA_POST_OP_OP_STR const char **valueTextOut); // caller must call GlobalFree on *valueTextOut;

bool BrowserIESetSelectElementOnChangeCallback(SA_PARAM_NN_STR const char *selectElementName, OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData);

bool BrowserIEFocusElement(SA_PARAM_NN_STR const char *elementName);

bool BrowserIEExistsElement(SA_PARAM_NN_STR const char *elementName);

BOOL BrowserIEMessageCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool BrowserIEProcessKeystrokes(MSG msg, SimpleWindow *pWindow);

bool BrowserIEInvokeScript(SA_PARAM_NN_STR const char *scriptName, InvokeScriptArgType firstType, va_list args);
bool BrowserIEDisplayHTMLFromURL(SA_PARAM_NN_STR const char *webPageURL, SA_PARAM_OP_STR const char **eaEstrKeyValuePostData);
bool BrowserIEDisplayHTMLStr(SA_PARAM_NN_STR const char *htmlString);

