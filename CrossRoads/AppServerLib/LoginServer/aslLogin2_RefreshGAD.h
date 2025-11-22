#pragma once

typedef U32 ContainerID;
typedef enum PlayerType PlayerType;

typedef void (*RefreshGADCB)(ContainerID accountID, bool succeeded, U32 highestLevel, U32 numCharacters, U32 lastLogoutTime, U32 refreshTime, void *userData);

// Request the creation and/or refresh of the GameAccountData container for the given accountID.  
// The provided callback will be called when the GameAccountData has been updates or the process failed.
// Note - extraGamePermissions is an array of pooled strings.
void aslLogin2_RefreshGAD(ContainerID accountID, PlayerType playerType, bool isLifetime, bool isPress, U32 overrideLevel, char **extraGamePermissions, RefreshGADCB cbFunc, void *userData);