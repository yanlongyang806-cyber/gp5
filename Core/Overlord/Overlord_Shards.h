#pragma once
#include "Overlord.h"
#include "StashTable.h"

typedef struct OverlordCluster OverlordCluster;
typedef struct ShardLauncherClusterShard ShardLauncherClusterShard;
typedef struct ShardLauncherRun ShardLauncherRun;

void OverlordShards_Init(void);

extern StashTable gOverlordShardsByName;

void AddShardFromShardLauncherConfig(OverlordCluster *pCluster, ShardLauncherRun *pRun, 
	ShardLauncherClusterShard *pShardConfig);