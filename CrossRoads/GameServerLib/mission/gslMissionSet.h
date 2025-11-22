/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct MissionDef MissionDef;
typedef struct MissionSet MissionSet;

MissionDef* missionset_RandomMissionFromSet(MissionSet *pSet);
MissionDef* missionset_RandomAvailableMissionFromSet(Entity *pEnt, MissionSet *pSet);

