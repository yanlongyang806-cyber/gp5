#pragma once
GCC_SYSTEM

// Forward declarations
typedef struct AttribAccrualSet		AttribAccrualSet;
typedef struct Character			Character;
typedef struct Entity				Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;

// Slimmed down and tweaked version of character_AccrueMods, specifically for
//  recalculating a character's innate effects.  Returns true if it filled in
//  pSet properly.
S32 character_AccrueModsInnate(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID AttribAccrualSet *pSet);

// Non-Character version of character_AccrueModsInnate, which just operates on
//  external Innate powers and uses them to change movement-related values
S32 entity_AccrueModsInnate(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pent);

// Accrues the effects of a character's attrib mods onto the character's attribs
void character_AccrueModsEx(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract, bool bBootstrapping, U32 uiTimeLoggedOut);
#define character_AccrueMods(iPartitionIdx, pchar, fRate, pExtract) character_AccrueModsEx(iPartitionIdx, pchar, fRate, pExtract, false, 0)
