/***************************************************************************



***************************************************************************/

#ifndef GAMESERVERHANDLEMSG_H_
#define GAMESERVERHANDLEMSG_H_

#include "GlobalComm.h"

typedef struct Entity Entity;

int GameServerHandlePktMsg(Packet* pak, int cmd, Entity *ent);
int GameServerHandlePktInput(Packet* pak, int cmd);

#endif
