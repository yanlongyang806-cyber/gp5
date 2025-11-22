#ifndef PVP_SCOREBOARD_UI_H
#define PVP_SCOREBOARD_UI_H

GCC_SYSTEM

typedef struct Entity Entity;
typedef struct Power Power;
typedef struct EntityPowerData EntityPowerData;

void gclPowerListFilter(Entity* pEntity,
	Power** ppPowersIn, 
	Power*** pppPowersOut,
	EntityPowerData*** pppEntPowersOut,
	S32* piCount,
	S32* pePowerTypes,
	S32* peIncludeCategories,
	S32* peExcludeCategories,
	S32* peAllowedPurposes,
	bool bActivatable,
	bool bIncludeItemPowers,
	bool bSort);

bool gclGetPowerPurposesFromString(ExprContext *pContext, 
	const char* pchPowerPurposes, 
	S32** peaPurposes);

#endif