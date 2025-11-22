#ifndef ITEM_UPGRADE_UI_H
#define ITEM_UPGRADE_UI_H

typedef struct Entity Entity;

void ItemUpgradeUI_Update(SA_PARAM_OP_VALID Entity *pEnt);
void ItemUpgradeUI_Tick(SA_PARAM_OP_VALID Entity *pEnt, F32 fTick);

#endif // ITEM_UPGRADE_UI_H