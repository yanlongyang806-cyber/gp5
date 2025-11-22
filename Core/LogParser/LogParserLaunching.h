#pragma once

void LogParserLaunching_PeriodicUpdate(void);
U32 LogParserLaunching_LaunchOne(int iTimeOut, const char *pExtraArgs, U32 iForcedPort, int iUID, char *pLaunchComment);
int FillStandAloneList(STRING_EARRAY *pArray);
bool LogParserLaunching_IsActive(void);
void UpdateLinkToLiveLogParser();
void InitStandAloneListening();
void LogParserShutdownStandAlones();
void LogParserLaunching_KillByUID(int iUID);