#pragma once

void UI_DisplayMessage(char *pMessage, ...);
void UI_GetString(char **ppOutString, char *pFmt, ...);
int UI_Picker(char *pTitleString, char ***pppNames, char ***pppDescriptions);