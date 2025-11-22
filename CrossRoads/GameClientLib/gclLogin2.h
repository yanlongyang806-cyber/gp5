#pragma once
/***************************************************************************



***************************************************************************/

typedef U32 ContainerID;
typedef struct Login2CharacterDetail Login2CharacterDetail;
typedef struct Entity Entity;
typedef struct CharClassCategorySet CharClassCategorySet;
typedef enum CharClassTypes CharClassTypes;
typedef struct Login2CharacterChoice Login2CharacterChoice;
typedef struct GameAccountDataNumericPurchaseDef GameAccountDataNumericPurchaseDef;

AUTO_ENUM;
typedef enum GCLLogin2FetchResult
{
    FetchResult_Succeeded,
    FetchResult_Failed,
    FetchResult_Timeout,
    FetchResult_Pending,
} GCLLogin2FetchResult;

typedef void (*FetchEntityDetailCB)(ContainerID playerID, GCLLogin2FetchResult result, void *userData);

void gclLogin2_CharacterDetailCache_Clear(void);
Entity *gclLogin2_CharacterDetailCache_GetEntity(ContainerID characterID);
Login2CharacterDetail *gclLogin2_CharacterDetailCache_Get(ContainerID characterID);
Entity *gclLogin2_CharacterDetailCache_GetPuppet(ContainerID playerID, CharClassTypes puppetClassType);
Entity *gclLogin2_CharacterDetailCache_GetPuppetWithSet(ContainerID playerID, CharClassTypes puppetClassType, CharClassCategorySet *pSet);
Entity *gclLogin2_CharacterDetailCache_GetPuppetWithSetName(ContainerID playerID, CharClassTypes puppetClassType, const char* pchSet);
void gclLogin2_CharacterDetailCache_Add(Login2CharacterDetail **characterDetail);
void gclLogin2_CharacterDetailCache_Fetch(ContainerID characterID, FetchEntityDetailCB cbFunc, void *cbData);
const char *gclLogin_GetExtraHeaderForPurchaseDef(Login2CharacterChoice *pChoice, GameAccountDataNumericPurchaseDef* pDef);
S32 gclLogin_GetNumericValueFromCharacterDetail(Login2CharacterChoice *pChoice, GameAccountDataNumericPurchaseDef* pDef);
