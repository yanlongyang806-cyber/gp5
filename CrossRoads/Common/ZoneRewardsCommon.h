#pragma once

#include "referencesystem.h"
#include "Message.h"

// Forward declarations for structs and enums
typedef struct ZoneRewardsItemSource ZoneRewardsItemSource;
typedef struct ZoneRewardsItemSet ZoneRewardsItemSet;
typedef struct Item Item;


AUTO_STRUCT;
typedef struct ZoneRewardsItemDropInfo
{
	const char* pchItemName;					AST(STRUCTPARAM NAME(Item) POOL_STRING)
	Item *pFakeItem;							AST(NAME(FakeItem) NO_NETSEND NO_TEXT_SAVE)
} ZoneRewardsItemDropInfo;

AUTO_STRUCT;
typedef struct ZoneRewardsItemSet
{
	const char *pchName;						AST(KEY STRUCTPARAM POOL_STRING)
	DisplayMessage msgDisplayName;				AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	DisplayMessage msgBlurb;					AST(NAME(Blurb) STRUCT(parse_DisplayMessage))
	const char *pchImageForeName;				AST(NAME(ImageFore) POOL_STRING)
	const char *pchImageBackName;				AST(NAME(ImageBack) POOL_STRING)
	U32 iUISortOrder;
	ZoneRewardsItemDropInfo **ppItemDropInfo;	AST(NAME(Item))
	bool bHideClassRestrictedItems : 1;
	bool bHideThis : 1;
} ZoneRewardsItemSet;

AUTO_STRUCT;
typedef struct ZoneRewardsItemSource
{
	const char *pchName;						AST(KEY STRUCTPARAM POOL_STRING)
	DisplayMessage msgDisplayName;				AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	DisplayMessage msgBlurb;					AST(NAME(Blurb) STRUCT(parse_DisplayMessage))
	const char *pchImageForeName;				AST(NAME(ImageFore) POOL_STRING)
	const char *pchImageBackName;				AST(NAME(ImageBack) POOL_STRING)
	U32 iUISortOrder;
	ZoneRewardsItemDropInfo **ppItemDropInfo;	AST(NAME(Item))
	bool bHideClassRestrictedItems : 1;
	bool bHideThis : 1;
} ZoneRewardsItemSource;

AUTO_STRUCT;
typedef struct ZoneRewardsDef
{
	// The logical name of the def
	const char *pchName;						AST(KEY STRUCTPARAM POOL_STRING)

	// Used for reloading and error reporting purposes
	const char* pchFilename;					AST(CURRENTFILE)

	DisplayMessage msgShortName;				AST(NAME(shortname) STRUCT(parse_DisplayMessage))
	DisplayMessage msgTitle;					AST(NAME(Title) STRUCT(parse_DisplayMessage))
	DisplayMessage msgDescription;				AST(NAME(Description) STRUCT(parse_DisplayMessage))

	const char *pchItemSetBorderAssembly;		AST(NAME(ItemSetBorderAssembly) RESOURCEDICT(UITextureAssembly) POOL_STRING)
	const char *pchItemSourceBorderAssembly;	AST(NAME(ItemSourceBorderAssembly) RESOURCEDICT(UITextureAssembly) POOL_STRING)

	const char *pchTabImage;					AST(NAME(TabImage) POOL_STRING)
	const char *pchTitleImage;					AST(NAME(TitleImage) POOL_STRING)

	ZoneRewardsItemSource **ppItemSource;		AST(NAME(Source))
	ZoneRewardsItemSet**ppItemSet;				AST(NAME(Set))
} ZoneRewardsDef;
extern ParseTable parse_ZoneRewardsDef[];
#define TYPE_parse_ZoneRewardsDef ZoneRewardsDef

AUTO_STRUCT;
typedef struct ZoneRewardsDefRef
{
	REF_TO(ZoneRewardsDef) hDef;
} ZoneRewardsDefRef;

// Dictionary holding the zone rewards defs
extern DictionaryHandle g_hZoneRewardsDictionary;

ZoneRewardsDef *ZoneRewards_GetRewardsDef(const char *pchDefName);

// Call UpdateItemDropInfoItemPointers before using anything in a ppItemDropInfo in the UI
void ZoneRewards_UpdateItemDropInfoItemPointers(ZoneRewardsItemDropInfo **eaItemDropInfo);
