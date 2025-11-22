#pragma once

typedef U32 ContainerID;
typedef struct Login2CharacterDetail Login2CharacterDetail;

typedef void (*GetCharacterDetailCB)(Login2CharacterDetail *characterDetail, void *userData);

void aslLogin2_GetCharacterDetail(ContainerID accountID, ContainerID playerID, const char *shardName, bool returnActivePuppets, bool returnTeamRequestPets, bool fixupRequired, GetCharacterDetailCB cbFunc, void *userData);
