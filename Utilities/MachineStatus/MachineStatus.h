#pragma once


typedef struct NetLink NetLink;
typedef struct MachineStatusUpdate MachineStatusUpdate;

typedef enum ShardLauncherWindowType
{
	WINDOWTYPE_NONE,
	WINDOWTYPE_MAIN,
} ShardLauncherWindowType;

NetLink *FindNetLinkFromShardName(char *pShardName);


#define SHARD_OUT_OF_CONTACT_SHOW_TIME 5
#define SHARD_OUT_OF_CONTACT_DISCONNECT_TIME 30

extern bool gbSomethingChanged;
extern MachineStatusUpdate **gppCurrentShards;


	