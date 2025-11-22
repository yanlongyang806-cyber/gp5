#pragma once

//if pTable == NULL, then it's an earray of strings
void SetComboBoxFromEarrayWithDefault(HWND hDlg, U32 iResID, void ***pppArray, ParseTable *pTable,
	char *pStartingValue);

int GetComboBoxSelectedIndex(HWND hDlg, U32 iResID);

#if 0
bool PickerWithDescriptions(char *pPickerName, char *pPickerLabel, char ***pppInChoices, char ***pppInDescriptions, char **ppOutChoice, char *pDefaultChoice);

AUTO_STRUCT;
typedef struct MultiPickerChoice
{
	char *pName;
	char *pDesc;
	char **ppChoices;
	bool bIsBool;
	char *pDefaultChoice;
	char **ppOutChoice; NO_AST
} MultiPickerChoice;

#define MAX_CHOICES_ONE_MULTIPICKER 26
bool MultiPicker(char *pName, MultiPickerChoice **ppChoices);

#endif