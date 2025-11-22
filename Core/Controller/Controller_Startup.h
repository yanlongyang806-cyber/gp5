#pragma once

typedef enum SimpleMonitoringState SimpleMonitoringState;

typedef struct NetLink NetLink;
typedef struct Packet Packet;
void HandleLauncherRequestsLocalExesForMirroring(NetLink *pLink, Packet *pPacket);
void HandleLauncherGotLocalExesForMirroring(NetLink *pLink, Packet *pPacket);

//returns STATUS_UNSPECIFIED if startup is not active
SimpleMonitoringState ControllerStartup_GetState(void);


extern bool gbMirrorLocalExecutables;