#pragma once

//this file declares that global states that are used by GameServerLib

//init state
#define GSL_INIT "gslInit"

//handshaking with map manager
#define GSL_MAPMANAGERHANDSHAKE "gslMapManagerHandshake"

//loading the world (may take a while)
#define GSL_LOADING "gslLoading"

//initializing client networking
#define GSL_NETINIT "gslNetInit"

//running
#define GSL_RUNNING "gslRunning"
//IF YOU CHANGE THIS, CHANGE aslMapManager.c where it gets the list of servers from the controller and checks 
//their state

//a special state where you want the server to be responding to remote commands and so forth, but not actually doing
//anything else
#define GSL_SPINNING "gslSpinning"

//creating/replaying synthetic log data. In order to simulate fake subscriptions we need our own state so we can receive the containers and not block the net link.
#define GSL_REPLAYSYNTHETICLOGS "gslReplaySyntheticLogs"

void gslRunning_BeginFrame_GameSystems(void);
void gslStateBeginMemLeakTracking(void);

