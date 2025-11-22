#pragma once

typedef U32 ContainerID;

typedef void (*RequestEntityFixupCB)(ContainerID entityID, bool succeeded, void *userData);

void aslLogin2_RequestPlayerEntityFixup(ContainerID playerEntityID, ContainerID accountID, bool fixupSharedBank, RequestEntityFixupCB cbFunc, void *userData);
void aslLogin2_EntityFixupTick(void);