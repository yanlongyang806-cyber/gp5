#pragma once
#include "SimpleWindowManager.h"


AUTO_STRUCT;
typedef struct MultiPickerChoice
{
	char *pName;
	char *pDesc;
	char **ppChoices;
	bool bIsBool;
	char *pDefaultChoice; AST(ESTRING)
	char *pStartingChoice; AST(ESTRING)
	bool bAlphaNumOnly;
} MultiPickerChoice;

AUTO_STRUCT;
typedef struct MultiPickerChoiceList
{
	MultiPickerChoice **ppMultiPickerChoices;
} MultiPickerChoiceList;



BOOL shardLauncherOptionsDlgProc_SWM(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
