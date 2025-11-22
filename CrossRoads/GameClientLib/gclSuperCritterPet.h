#ifndef GCLSUPERCRITTERPET_H
#define GCLSUPERCRITTERPET_H

#include "SuperCritterPet.h"
#include "referencesystem.h"
#include "itemCommon.h"
#include "inventorycommon.h"

AUTO_STRUCT;
typedef struct UIActiveSuperCritterPet
{
	char* pchName;
	int level;
	bool bSummoned;
	bool bDead;
	bool bLocked;
	bool bSlotted;
	bool bTraining;
	U32 uiTrainingTimeInSeconds;
	EntityRef entRef;
	Entity* pFakeEnt;	AST(UNOWNED)
} UIActiveSuperCritterPet;

void scp_PostGemChange();
Entity* scp_GetFakePetEntity(int idx);

const char* scp_ExprGetPetEquipmentSlotTypeTranslate(SA_PARAM_OP_VALID Item* pPetItem, int iSlot);

SA_RET_OP_VALID Item* scp_GetInspectItem();
#ifndef AILIB
#include "AutoGen/gclSuperCritterPet_h_ast.h"
#endif

#endif