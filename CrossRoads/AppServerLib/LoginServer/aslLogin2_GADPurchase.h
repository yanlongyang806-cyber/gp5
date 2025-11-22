#pragma once

typedef U32 ContainerID;
typedef enum GlobalType GlobalType;

typedef void (*GADPurchaseCB)(bool success, U64 userData);

// Do a purchase that will change gad
void aslLogin2_GADPurchase(ContainerID playerID, ContainerID accountID, const char *gadPurchaseDef, GADPurchaseCB cbFunc, U64 userData);
