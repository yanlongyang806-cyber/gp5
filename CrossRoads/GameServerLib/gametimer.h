/***************************************************************************



***************************************************************************/

#ifndef GAMETIMER_H
#define GAMETIMER_H

typedef struct Entity Entity;
typedef struct GameTimer GameTimer;

// Hopefully there is only one at a time for UI reasons...?
extern GameTimer** g_GlobalGameTimers;

void gametimer_RefreshGameTimersForPlayer(Entity *playerEnt);

void gametimer_ClearAllTimersForPlayer(Entity *playerEnt);

#endif