#pragma once


bool RunTheShard(ShardLauncherRun *pRun);

//returns true if there are warnings
bool CheckRunForWarnings(ShardLauncherRun *pRun, char **ppOutWarnings);

ShardLauncherConfigOption *FindConfigOption(ShardLauncherRun *pRun, char *pName, GlobalType *pOutOverrideType);

void RunTheShard_Log(FORMAT_STR const char *pFmt, ...);
void RunTheShard_LogWarning(FORMAT_STR const char *pFmt, ...);
void RunTheShard_LogFail(FORMAT_STR const char *pFmt, ...);
void RunTheShard_LogSucceed(FORMAT_STR const char *pFmt, ...);