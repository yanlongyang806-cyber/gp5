/***************************************************************************



***************************************************************************/

#pragma once

typedef struct ZoneMap ZoneMap;

// Called on map load and unload
void shardvariable_MapLoad(ZoneMap *pZoneMap);
void shardvariable_MapUnload(void);
void shardvariable_MapValidate(ZoneMap *pZoneMap);