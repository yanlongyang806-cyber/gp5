#ifndef CRYPTICERROR_HARVEST_H
#define CRYPTICERROR_HARVEST_H

// ------------------------------------------------------------------------
// Main Harvest Functions

void harvestStartup(void);
bool harvestDisplayInfo(void);

bool harvestCheckManualUserDump();
bool harvestPerformManualUserDump();

bool harvestLockDeferred();
bool harvestCheckDeferred(); // Implicitly calls harvestLockDeferred()
bool harvestPerformDeferred();

bool harvestPerform();

void harvestWorkComplete(bool bHarvestSuccess);

// ------------------------------------------------------------------------
// Harvest Mode Querying

typedef enum CrypticErrorMode
{
	CEM_CUSTOMER = 0,  // Temporary dumps, pretty dialogs, auto-closing
	CEM_DEVELOPER,     // Temporary dumps, ugly dialogs with extra functionality / data, manual closing
	CEM_DEVSERVER,     // Permanent dumps, ugly dialogs with extra functionality / data, manual closing
	CEM_PRODSERVER,

	CEM_COUNT	
} CrypticErrorMode;

CrypticErrorMode harvestGetMode();

// These are NOT direct opposites! They are both false on production servers, for example
bool harvestIsEndUserMode();
bool harvestIsDeveloperMode();

bool harvestNeedsProgrammer();

const char *harvestGetFilename(); // "GameServer.exe" (does not include path)

// ------------------------------------------------------------------------
// Crashed Process Details Querying

int   harvestGetPid();
char* harvestGetStringVar(const char *name, const char *defaultVal);
int   harvestGetIntVar(const char *name, int defaultVal);

// ------------------------------------------------------------------------
// Crashed Process Manipulation

typedef enum ProcessAction
{
	PA_DEBUG = 0,
	PA_IGNORE,
	PA_TERMINATE,
	PA_REPORT,

	PA_COUNT
} ProcessAction;

bool isProcessGone();
void performProcessAction(ProcessAction eAction);

// ------------------------------------------------------------------------

extern bool gbDontSendDumps;

#endif
