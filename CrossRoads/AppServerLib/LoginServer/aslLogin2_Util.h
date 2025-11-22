#pragma once

typedef U32 ContainerID;
typedef enum GlobalType GlobalType;

ContainerID aslLogin2_GetRandomServerOfTypeInShard(const char *shardName, GlobalType serverType);