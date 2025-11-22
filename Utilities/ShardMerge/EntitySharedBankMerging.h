#pragma once

typedef struct Entity Entity;
typedef struct Entity_AutoGen_NoConst Entity_AutoGen_NoConst;

void InitializeEntitySharedBank(NOCONST(Entity) *lhs);
void MergeTwoEntitySharedBanks(NOCONST(Entity) *lhs, Entity *rhs);
