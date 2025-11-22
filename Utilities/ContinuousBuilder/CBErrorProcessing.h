#pragma once
typedef struct NetLink NetLink;
typedef struct ErrorData ErrorData;
typedef struct ErrorTrackerContext ErrorTrackerContext;
typedef struct ErrorTrackerEntryList ErrorTrackerEntryList;

ErrorTrackerContext *CB_GetCurrentContext(void);
ErrorTrackerEntryList *CB_GetCurrentEntryList(void);
ErrorTrackerContext *CB_GetLastContext(void);
ErrorTrackerEntryList *CB_GetLastEntryList(void);

void CB_InitializeErrorTrackerContexts(void);
void CB_SwitchErrorTrackerContexts(void);

void CB_ProcessErrorData(NetLink *link, ErrorData *pErrorData);
void CB_ErrorTrackerInit(U32 uOptions, U32 uWebOptions);

void CBErrorProcessing_SetDisableAll(bool bSet);