#ifndef CRYPTICERROR_UI_H
#define CRYPTICERROR_UI_H

#define MAX_DESCRIPTION_LENGTH (1023)

void uiStart();
void uiRequestSwitchMode();
void uiWroteDumps();
void uiNotifyProcessGone();
void uiCheckErrorTrackerResponse();
void uiWorkComplete(bool bHarvestSuccess);
bool uiWorkIsComplete();
void uiPopErrorID();
void uiShutdown();
bool uiIsShuttingDown();
void uiDumpSendComplete();
void uiFinished(bool bHarvestSuccess);
void uiDisableDescription();

bool shouldAutoClose(bool bHarvestSuccess);
bool shouldForceClose();

bool uiDescriptionDialogIsComplete();
const char * uiGetDescription();

// Logs to disk only; all others display in the UI somehow
void LogDisk(const char *text);
void LogDiskf(const char *format, ...);

void LogBold(const char *text);
void LogNormal(const char *text);
void LogNote(const char *text);
void LogError(const char *text);
void LogImportant(const char *text);
void LogStatusBar(const char *text);
void LogTransferProgress(size_t uRemainingBytes, size_t uTotalBytes, F32 fKBytesPerSec);

bool uiChooseProcess();

#endif
