#pragma once

typedef U32 ContainerID;
typedef enum PlayerType PlayerType;

typedef void (*EntitySharedBankCB)(ContainerID accountID, bool succeeded, void *userData);

// Request the creation and/or refresh of the GameAccountData container for the given accountID.  
// The provided callback will be called when the GameAccountData has been updates or the process failed.
// Note - extraGamePermissions is an array of pooled strings.
void aslLogin2_CheckEntitySharedBank(ContainerID accountID, EntitySharedBankCB cbFunc, void *userData);