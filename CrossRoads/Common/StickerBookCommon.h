#pragma once

#include "referencesystem.h"
#include "Message.h"

typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct ItemDef ItemDef;
typedef struct Item Item;
typedef struct StickerBookCollection StickerBookCollection;
typedef struct StickerBookItemSet StickerBookItemSet;
typedef struct StickerBookItemLocation StickerBookItemLocation;

AUTO_STRUCT;
typedef struct StickerBookItem
{
	const char *pchItemName;							AST(STRUCTPARAM NAME(Item) POOL_STRING)

	// Only valid for items in a location
	DisplayMessage msgSubLocation;						AST(NAME(SubLocation) STRUCT(parse_DisplayMessage))

	Item *pFakeItem;									AST(NAME(FakeItem) NO_NETSEND NO_TEXT_SAVE)

	// StickerBook points awarded for acquiring this Item
	U32 iPoints;										AST(NAME(Points))

	StickerBookItemSet *pStickerBookItemSet;			NO_AST
	StickerBookItemLocation *pStickerBookItemLocation;	NO_AST
} StickerBookItem;
extern ParseTable parse_StickerBookItem[];
#define TYPE_parse_StickerBookItem StickerBookItem

AUTO_STRUCT;
typedef struct StickerBookItemSet
{
	const char *pchName;							AST(KEY STRUCTPARAM POOL_STRING)

	// Post-processed on load, ref string for the StickerBookItemSet consisting of "<Collection Name>::<Set Name>".
	const char* pchRefString;						AST( NO_TEXT_SAVE POOL_STRING )

	DisplayMessage msgDisplayName;					AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	DisplayMessage msgBlurb;						AST(NAME(Blurb) STRUCT(parse_DisplayMessage))
	const char *pchImageForeName;					AST(NAME(ImageFore) POOL_STRING)
	const char *pchImageBackName;					AST(NAME(ImageBack) POOL_STRING)
	StickerBookItem **ppItems;						AST(NAME(Item))

	// Additional StickerBook points awarded for completing this Set
	U32 iPoints;									AST(NAME(Points))

	const char *pchRewardTitleItem;					AST(NAME(RewardTitleItem) POOL_STRING)
	Item *pFakeRewardTitleItem;						AST(NAME(FakeRewardTitleItem) NO_NETSEND NO_TEXT_SAVE)

	StickerBookCollection *pStickerBookCollection;	NO_AST
} StickerBookItemSet;
extern ParseTable parse_StickerBookItemSet[];
#define TYPE_parse_StickerBookItemSet StickerBookItemSet

AUTO_STRUCT;
typedef struct StickerBookItemLocation
{
	const char *pchName;							AST(KEY STRUCTPARAM POOL_STRING)
	DisplayMessage msgDisplayName;					AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	DisplayMessage msgBlurb;						AST(NAME(Blurb) STRUCT(parse_DisplayMessage))
	const char *pchImageForeName;					AST(NAME(ImageFore) POOL_STRING)
	const char *pchImageBackName;					AST(NAME(ImageBack) POOL_STRING)
	StickerBookItem **ppItems;						AST(NAME(Item))

	// Additional StickerBook points awarded for completing this Location
	U32 iPoints;									AST(NAME(Points))

	StickerBookCollection *pStickerBookCollection;	NO_AST
} StickerBookItemLocation;
extern ParseTable parse_StickerBookItemLocation[];
#define TYPE_parse_StickerBookItemLocation StickerBookItemLocation

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pStickerBookCollectionTypes);
typedef enum StickerBookCollectionType
{
	kStickerBookCollectionType_Unspecified, ENAMES(Unspecified)
	kStickerBookCollectionType_FIRST_DATA_DEFINED, EIGNORE
} StickerBookCollectionType;
extern StaticDefineInt StickerBookCollectionTypeEnum[];

AUTO_STRUCT;
typedef struct StickerBookItemAlias
{
	// The logical name of the Item def
	const char *pchName;					AST(STRUCTPARAM POOL_STRING)

	const char **eaItems;					AST(NAME(Item) POOL_STRING)
} StickerBookItemAlias;
extern ParseTable parse_StickerBookItemAlias[];
#define TYPE_parse_StickerBookItemAlias StickerBookItemAlias

AUTO_STRUCT;
typedef struct StickerBookItemAliases
{
	StickerBookItemAlias **eaItemAliases;	AST(NAME(ItemAlias))
} StickerBookItemAliases;
extern ParseTable parse_StickerBookItemAliases[];
#define TYPE_parse_StickerBookItemAliases StickerBookItemAliases

AUTO_STRUCT;
typedef struct StickerBookCollection
{
	// The logical name of the def
	const char *pchName;						AST(KEY STRUCTPARAM POOL_STRING)

	// Used for reloading and error reporting purposes
	const char* pchFilename;					AST(CURRENTFILE)

	StickerBookCollectionType eStickerBookCollectionType;	AST(NAME(Type))

	int iSortPosition;							AST(NAME(SortPosition))

	DisplayMessage msgShortName;				AST(NAME(shortname) STRUCT(parse_DisplayMessage))
	DisplayMessage msgTitle;					AST(NAME(Title) STRUCT(parse_DisplayMessage))
	DisplayMessage msgDescription;				AST(NAME(Description) STRUCT(parse_DisplayMessage))

	const char *pchItemSetBorderAssembly;		AST(NAME(ItemSetBorderAssembly) RESOURCEDICT(UITextureAssembly) POOL_STRING)

	const char *pchTabImage;					AST(NAME(TabImage) POOL_STRING)
	const char *pchTitleImage;					AST(NAME(TitleImage) POOL_STRING)

	StickerBookItemSet **ppItemSet;				AST(NAME(Set))
	StickerBookItemLocation **ppItemLocation;	AST(NAME(Location))
} StickerBookCollection;
extern ParseTable parse_StickerBookCollection[];
#define TYPE_parse_StickerBookCollection StickerBookCollection

extern DictionaryHandle g_hStickerBookDictionary;

typedef struct StickerBookTrackedItem
{
	StickerBookItem **ppItems;
} StickerBookTrackedItem;

StickerBookItemSet* StickerBook_ItemSetGetByRefString(const char *pchRefString);
StickerBookCollection* StickerBook_CollectionGetByRefString(const char *pchRefString);

StickerBookCollection *StickerBook_GetCollection(const char *pchCollectionName);

// Call StickerBook_UpdateItemPointers before using anything in a StickerBookItem in the UI
void StickerBook_UpdateItemPointers(StickerBookItem **eaStickerBookItems);
// Call StickerBook_UpdateItemPointers before using anything in a StickerBookItemSet in the UI
void StickerBook_UpdateItemSetPointers(StickerBookItemSet **eaStickerBookItemSets);

// Fast query (stash table lookup) to determine if an Item participates in the StickerBook. Called from inventory transactions to determine
// If this Item needs to be added to the EntPlayer's eaAstrRecentlyAcquiredStickerBookItems array.
// If the return value is NULL, then the Item does not participate in the Sticker Book. Otherwise, a pooled string item name that should be used
// is returned. This allows support for Sticker Book Item Aliases.
const char *StickerBook_DoesItemParticipate(const char *astrItemName);
StickerBookTrackedItem *StickerBook_GetTrackedItem(const char *astrItemName);

// Helper functions for computing the Points available as well as the Points an Entity Player has acquired.
U32 StickerBook_CountPoints(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountPointsForCollectionType(SA_PARAM_NN_VALID StickerBookCollectionType type, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountPointsForCollection(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountPointsForSet(SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountPointsForLocation(SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);

U32 StickerBook_CountTotalPoints(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountTotalPointsForCollectionType(SA_PARAM_NN_VALID StickerBookCollectionType type, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountTotalPointsForCollection(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountTotalPointsForSet(SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);
U32 StickerBook_CountTotalPointsForLocation(SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount);

//AUTO_TRANS_HELPER
void StickerBook_trh_MaybeRecentlyAcquiredItem(ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef);
