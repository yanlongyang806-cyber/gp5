#pragma once

//public headers for the MachineStatus utility, which runs on a (shared) gameserver machine, and provides quick visual status
//on what shards have gameservers on that machine, along with some simple manipulation (hide/show windows, etc)

//the general protocol is very simple... connect to localhost on DEFAULT_MACHINESTATUS_PORT, send a TO_MACHINESTATUS_HERE_IS_UPDATE
//packet, pktSendStructSafe into it one of these structs

AUTO_STRUCT;
typedef struct MachineStatusUpdate
{
	char *pShardName; 
	U32 *pGameServerPIDs;
	U32 *pOtherPIDs;
	U32 iPIDOfIgnoredServer;
	U64 iGSRam;
	float fGSCpu;
	float fGSCpuLast60;
	U32 iTime;
} MachineStatusUpdate;
