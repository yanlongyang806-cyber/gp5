#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#include "Entity.h"

typedef struct GameEncounter GameEncounter;

typedef struct EntityPresenceCheckInfo EntityPresenceCheckInfo;

void gslEntityPresence_OnEncounterActivate(int iPartitionIdx, GameEncounter * pEncounter);

void gslEntityPresenceTick(void);

void gslRequestEntityPresenceUpdate();

void gslEntityPresenceRelease(Player * pPlayer);