/***************************************************************************



***************************************************************************/

#pragma once

typedef struct Mission Mission;

// Updates the debugging information attached to the mission
void missiondebug_UpdateDebugInfo(Mission *pMission);
void missiondebug_UpdateAllDebugInfo(CONST_EARRAY_OF(Mission) eaMissions);
