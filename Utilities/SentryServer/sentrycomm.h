#pragma once

#include "sentrycfg.h"

typedef struct NetLink NetLink;
typedef struct NetComm NetComm;



extern SentryClient **sentries;
char				**stat_labels;

void sentryListen(NetComm *comm,char *sentry_name);
Stat *statFind(SentryClient *client,char *key,U32 uid);
char *statsGetLabel(char *key);
int labelCmp(const char **pa,const char **pb);
void sentryUpdate();
char *statGetTitle(char *str,char *dst);
SentryClient *sentrySendKill(char *machine,char *process_name);
SentryClient *sentrySendLaunch(char *machine,char *command);
SentryClient *sentrySendLaunchAndWait(char *machine,char *command);
SentryClient *sentryFindByName(char *machine);
SentryClient *sentrySendCreateFile(char *machine, char *pFileToCreate, int iCompressedSize, int iUncompressedSize, 
	void *pBuffer);

SentryClient *sentrySendGetFileCRC(char *machineName, int iRequestID, char *pFileName, int iMonitorLinkID);
SentryClient *sentrySendGetFileContents(char *machineName, int iRequestID, char *pFileName, int iMonitorLinkID);
SentryClient *sentrySendGetDirectoryContents(char *machine, int iRequestID, char *pDirectoryName, int iMonitorLinkID);