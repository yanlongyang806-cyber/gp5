#pragma once

typedef U32 ContainerID;

typedef void (*DeleteCharacterCB)(bool success, U64 userData);

// Delete a character from a remote shard.
void aslLogin2_DeleteCharacter(ContainerID playerID, const char *shardName, DeleteCharacterCB cbFunc, U64 userData);
