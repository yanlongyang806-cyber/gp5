#include "itemSalvage.h"
#include "Character.h"
#include "Entity.h"
#include "error.h"
#include "ExpressionFunc.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "inventoryCommon.h"
#include "AutoTransDefs.h"

#include "AutoGen/itemSalvage_h_ast.h"
#include "AutoGen/itemEnums_h_ast.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define ITEMSALVAGE_FILENAME	"ItemSalvage.def"

ItemSalvageDef g_ItemSalvageDef = {0};

// --------------------------------------------------------------------------------------------------------------------
void ItemSalvage_Fixup(ItemSalvageDef *pDef)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaItemSalvageRecipes, ItemSalvageRecipeDef, pRecipeDef)
	{
		if (pRecipeDef->iLevelMin > pRecipeDef->iLevelMax)
			pRecipeDef->iLevelMax = pRecipeDef->iLevelMin;
	}
	FOR_EACH_END

	if (pDef->pSalvageableCheck)
	{
		if (pDef->pSalvageableCheck->iLevelMin > pDef->pSalvageableCheck->iLevelMax)
			pDef->pSalvageableCheck->iLevelMax = pDef->pSalvageableCheck->iLevelMin;
	}
}


// --------------------------------------------------------------------------------------------------------------------
static S32 ItemSalvage_ValidateDef(ItemSalvageDef *pDef)
{
	if (!pDef->pchFilename)
		return true; // def file not present

	// check for existence of required basics
	if (!pDef->pSalvageableCheck)
	{
		ErrorFilenamef(ITEMSALVAGE_FILENAME, "No SalvageableCheck Defined. It is required.");
		return false;
	}
		
	// todo: check for non-exclusivity?

#if GAMESERVER
	if (!GET_REF(pDef->hDefaultRewardTable))
	{
		ErrorFilenamef(ITEMSALVAGE_FILENAME, "DefaultRewardTable is not defined or valid!");
		return false;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaItemSalvageRecipes, ItemSalvageRecipeDef, pRecipeDef)
	{
		if (!GET_REF(pRecipeDef->hRewardTable))
		{
			ErrorFilenamef(ITEMSALVAGE_FILENAME, "Missing a RewardTable is not defined or valid!");
			return false;
		}
	}
	FOR_EACH_END

#endif

	return true;
}
// --------------------------------------------------------------------------------------------------------------------
static void ItemSalvage_LoadInternal(const char *pchPath, S32 iWhen)
{
	StructReset(parse_ItemSalvageDef, &g_ItemSalvageDef);

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	if (IsClient()) 
	{
		ParserLoadFiles(NULL, 
						"defs/config/ItemSalvage.def", 
						"ItemSalvageClient.bin", 
						PARSER_OPTIONALFLAG, 
						parse_ItemSalvageDef, 
						&g_ItemSalvageDef);
	} 
	else
	{
		ParserLoadFiles(NULL, 
						"defs/config/ItemSalvage.def", 
						"ItemSalvage.bin", 
						PARSER_OPTIONALFLAG, 
						parse_ItemSalvageDef, 
						&g_ItemSalvageDef);
	}

	ItemSalvage_Fixup(&g_ItemSalvageDef);

	if (IsGameServerBasedType())
	{
		ItemSalvage_ValidateDef(&g_ItemSalvageDef);
	}
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(AS_ItemSalvageDef) ASTRT_DEPS(Items, RewardTables);
void ItemSalvageDef_Load(void)
{
	ItemSalvage_LoadInternal(NULL, 0);
	

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemSalvage.def", ItemSalvage_LoadInternal);
	}
}

AUTO_STARTUP(AS_ItemSalvageDefMinimal) ASTRT_DEPS(ItemQualities, ItemTags);
void ItemSalvageDef_LoadMinimal(void)
{
	ItemSalvage_LoadInternal(NULL, 0);
	

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemSalvage.def", ItemSalvage_LoadInternal);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// returns true if they can perform the salvage, 
//  checks things like if the player has a given powerMode on them (example: being near some object in the world)
S32 ItemSalvage_CanPerformSalvage(Entity *pEnt)
{
	if (pEnt && pEnt->pChar && g_ItemSalvageDef.iRequiredPowerMode != -1)
	{
		return character_HasMode(pEnt->pChar, g_ItemSalvageDef.iRequiredPowerMode);
	}

	return true;
}


// --------------------------------------------------------------------------------------------------------------------
// returns true if peSource contains all of the numerals from peMatchers
static S32 ItemSalvage_MatchesAllCategories(ItemCategory *peMatchers, ItemCategory *peSource)
{
	S32 i, size = eaiSize(&peMatchers);
	
	if (eaiSize(&peSource) == 0)
		return false;

	for (i = 0; i < size; ++i)
	{
		if (eaiFind(&peSource, peMatchers[i]) == -1)
		{
			return false;
		}
	}

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// returns true if peSource has any of the numerals from peMatchers
static S32 ItemSalvage_MatchesAnyCategory(ItemCategory *peMatchers, ItemCategory *peSource)
{
	S32 i, size = eaiSize(&peMatchers);
	
	if (eaiSize(&peSource) == 0)
		return false;

	for (i = 0; i < size; ++i)
	{
		if (eaiFind(&peSource, peMatchers[i]) != -1)
		{
			return true;
		}
	}

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
// given an item, finds the appropriate reward table.
// assumes that the item is already salvageable
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Hitem");
RewardTable* ItemSalvage_trh_GetRewardTableForItem(ATH_ARG NOCONST(Item) *pItem)
{
	ItemDef *pItemDef = GET_REF(pItem->hItem);

	if (pItemDef)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(g_ItemSalvageDef.eaItemSalvageRecipes, ItemSalvageRecipeDef, pSalvage)
		{
			// is it the right quality
			if (eaiSize(&pSalvage->peItemQualities))
			{
				if (eaiFind(&pSalvage->peItemQualities, pItemDef->Quality) == -1)
					continue; // failed quality check
			}

			if (pSalvage->iSlotType && pSalvage->iSlotType != pItemDef->eRestrictSlotType)
			{
				continue;
			}
			
			// does it have all the required item categories
			if (eaiSize(&pSalvage->peRequiredItemCategories) && 
				!ItemSalvage_MatchesAllCategories(pSalvage->peRequiredItemCategories, pItemDef->peCategories))
			{
				continue;
			}

			// does it have any of the excluded item categories
			if (eaiSize(&pSalvage->peExcludeItemCategories) && 
				!ItemSalvage_MatchesAnyCategory(pSalvage->peExcludeItemCategories, pItemDef->peCategories))
			{
				continue;
			}

			// is it the right item type
			if (eaiSize(&pSalvage->peItemTypes))
			{
				if (eaiFind(&pSalvage->peItemTypes, pItemDef->eType) == -1)
					continue;
			}

			// is it the right level
			if (pSalvage->iLevelMin && pSalvage->iLevelMax)
			{
				if (pItemDef->iLevel < pSalvage->iLevelMin || pItemDef->iLevel > pSalvage->iLevelMax)
					continue;
			}

			// does it have the right restrictBagIDs
			if (eaiSize(&pSalvage->peAllowedRestrictBagIDs) > 0)
			{
				S32 i, size = eaiSize(&pSalvage->peAllowedRestrictBagIDs);
				bool bFound = false;
								
				if (eaiSize(&pItemDef->peRestrictBagIDs) == 0)
					continue;

				for (i = 0; i < size; ++i)
				{
					if (eaiFind(&pItemDef->peRestrictBagIDs, pSalvage->peAllowedRestrictBagIDs[i]) != -1)
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
					continue;
			}

			// restricted to class? check if the item has any of the character classes
			if (eaSize(&pSalvage->ppCharacterClasses))
			{	
				bool bFound = false;
				
				if (!pItemDef->pRestriction)
					continue;

				FOR_EACH_IN_EARRAY_FORWARDS(pSalvage->ppCharacterClasses, CharacterClassRef, pSalvageCharRef)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pSalvage->ppCharacterClasses, CharacterClassRef, pItemCharRef)
					{
						if (REF_HANDLE_COMPARE(pSalvageCharRef->hClass, pItemCharRef->hClass))
						{
							bFound = true;
							break;
						}
					}
					FOR_EACH_END
					if (bFound)
						break;
				}
				FOR_EACH_END

				if (!bFound)
					continue;
			}
			
			return GET_REF(pSalvage->hRewardTable);
		}
		FOR_EACH_END
	}

	return GET_REF(g_ItemSalvageDef.hDefaultRewardTable);
}

// --------------------------------------------------------------------------------------------------------------------
// returns if the item is a candidate for being salvaged at all
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Hitem");
S32 ItemSalvage_trh_IsItemSalvageable(ATH_ARG NOCONST(Item) *pItem)
{
	ItemDef *pItemDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;

	if (g_ItemSalvageDef.pSalvageableCheck && pItemDef)
	{
		ItemSalvageCheckDef *pSalvageableCheck = g_ItemSalvageDef.pSalvageableCheck; 

		// check if the item has any disallowed categories
		if (eaiSize(&pSalvageableCheck->peExcludeItemCategories) && 
			ItemSalvage_MatchesAnyCategory(pSalvageableCheck->peExcludeItemCategories, pItemDef->peCategories))
		{
			return false;
		}

		// check if the item has any of the AllowedItemCategories
		if (eaiSize(&pSalvageableCheck->peAllowedItemCategories) && 
			!ItemSalvage_MatchesAnyCategory(pSalvageableCheck->peAllowedItemCategories, pItemDef->peCategories))
		{
			return false;
		}

		// check if the item is of the right quality
		if (eaiSize(&pSalvageableCheck->peItemQualities))
		{
			if (eaiFind(&pSalvageableCheck->peItemQualities, pItemDef->Quality) == -1)
				return false;
		}

		// is it the right item type?
		if (eaiSize(&pSalvageableCheck->peItemTypes))
		{
			if (eaiFind(&pSalvageableCheck->peItemTypes, pItemDef->eType) == -1)
				return false;
		}

		// is it the right level
		if (pSalvageableCheck->iLevelMin && pSalvageableCheck->iLevelMax)
		{
			if (pItemDef->iLevel < pSalvageableCheck->iLevelMin || pItemDef->iLevel > pSalvageableCheck->iLevelMax)
				return false;
		}

		// todo: add check if it's in an appropriate bag?

		return true;
	}

	return false;
}


#include "AutoGen/Itemsalvage_h_ast.c"
