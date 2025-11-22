#pragma once

#include "referencesystem.h"
#include "Message.h"

#define MICROTRANS_BANNER_BASE_DIR "defs/microtransactions"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT AST_IGNORE_STRUCTPARAM(Name);
typedef struct MicrotransBannerEntry
{
	// Display name for the game progression node
	const char* pchImageName;		AST( NAME( BannerImage ) POOL_STRING)
	DisplayMessage msgTitleKey;		AST( NAME( BannerTitleMsgKey ) STRUCT(parse_DisplayMessage))
	DisplayMessage msgDescKey;		AST( NAME( BannerDescMsgKey ) STRUCT(parse_DisplayMessage))
} MicrotransBannerEntry;

AUTO_STRUCT AST_IGNORE_STRUCTPARAM(Name);
typedef struct MicrotransBannerBlock
{
	const char *pchStartTime;			AST(NAME(StartTime))
	
	U32 uStartingTime;	// Converted from above string

	// The list of Banners for this block
	MicrotransBannerEntry **ppBannerEntries;	AST(NAME("Banner"))
} MicrotransBannerBlock;

AUTO_STRUCT ;
typedef struct MicrotransBannerSet
{
	// The unique name of this group of content (client/uigen can request different lists)
	const char *pchName;					AST(KEY STRUCTPARAM)

	// Used for error reporting
	const char* pchFilename;				AST(CURRENTFILE)
	
	MicrotransBannerBlock **ppBannerBlocks; AST(NAME("BannerBlock"))
	
} MicrotransBannerSet;


//// Dictionary holding the game progression nodes
//extern DictionaryHandle g_hMicrotransBannerListDictionary;

// Loads the suggested player content
void microtransBanner_LoadMicrotransBanner(void);

// Gets the MicrotransBanner from the dictionary
MicrotransBannerSet * microtransBanner_MicrotransBannerSetFromName(const char *pchName);
