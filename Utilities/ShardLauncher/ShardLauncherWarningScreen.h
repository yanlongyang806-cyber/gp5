#pragma once
#include "SimpleWindowManager.h"


void DisplayWarning(const char *pTitle, char *pStr);
void DisplayWarningf(const char *pTitle, FORMAT_STR const char* format, ...);

//if the button command has "(OK)" in it, then it also implicitly clicks OK
void DisplayConfirmation(char *pStr, char *pExtraButton, char *pExtraButtonCommand);

bool AreWarningsCurrentlyDisplaying(void);