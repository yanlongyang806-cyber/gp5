#pragma once

typedef U32 ContainerID;
typedef struct Entity Entity;

// This can't be in Login2Common.h because we need to avoid polluting the ObjectDB with Entity.
AUTO_STRUCT;
typedef struct Login2CharacterDetail
{
    ContainerID playerID;               AST(KEY)
    Entity *playerEnt;
    EARRAY_OF(Entity) activePuppetEnts;
	EARRAY_OF(Entity) teamRequestEnts;
} Login2CharacterDetail;