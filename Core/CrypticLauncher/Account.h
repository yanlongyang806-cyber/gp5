#pragma once

#include "GlobalComm.h"

// pre-dec's
typedef struct Packet Packet;

// procedural
extern void AccountLinkInit(bool bUseConnectCB);
extern bool AccountLogin(void);
extern bool AccountLinkWaitAndLogin(void);
extern const char *AccountLastError(void);
extern void AccountAppendDataToPacket(Packet* pak);

// getters/setters
extern void AccountSetPassword(const char *password, bool bPasswordHashed);
extern const char *AccountGetPassword(void);
extern bool AccountGetPasswordHashed(void);

extern void AccountSetLoginType(AccountLoginType loginType);
extern void AccountSetForceMigrate(bool bForceMigrate);

// no productName necessary - internally uses cryptic launcher as productName
extern bool AccountSetUsername(const char *username);
extern bool AccountGetUsername(char *username, int usernameMaxLength); // called from patcher
