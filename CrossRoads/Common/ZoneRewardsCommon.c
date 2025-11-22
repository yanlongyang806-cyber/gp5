#include "ZoneRewardsCommon.h"

#include "GlobalTypes.h"
#include "file.h"
#include "fileutil.h"
#include "ResourceManager.h"
#include "referencesystem.h"
#include "FolderCache.h"
#include "error.h"
#include "itemCommon.h"
#include "inventoryCommon.h"

#include "AutoGen/ZoneRewardsCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Dictionary holding the zone rewards defs
DictionaryHandle g_hZoneRewardsDictionary = NULL;

#define ZONE_REWARDS_BASE_DIR "defs/zonerewards"

#include "AutoGen/ZoneRewardsCommon_h_ast.c"

static void ZoneRewards_ValidateRefs(ZoneRewardsDef *pZoneRewardsDef)
{
	// Check all ItemSets.
	FOR_EACH_IN_EARRAY(pZoneRewardsDef->ppItemSet, ZoneRewardsItemSet, pItemSet)
	{
		FOR_EACH_IN_EARRAY(pItemSet->ppItemDropInfo, ZoneRewardsItemDropInfo, pItemDropInfo)
		{
			bool bFoundInOther = false;
			// Make sure all DropInfo item names are really itemdefs.
			ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pItemDropInfo->pchItemName);
			if( !pItemDef )
			{
				ErrorFilenamef(pZoneRewardsDef->pchFilename, "Item \"%s\" in Set \"%s\" isn't a real item.", pItemDropInfo->pchItemName, pItemSet->pchName);
			}

			// Make sure all DropInfo item names are in an ItemSource too
			FOR_EACH_IN_EARRAY(pZoneRewardsDef->ppItemSource, ZoneRewardsItemSource, pItemSource)
			{
				FOR_EACH_IN_EARRAY(pItemSource->ppItemDropInfo, ZoneRewardsItemDropInfo, pItemDropInfoOther)
				{
					if(pItemDropInfo->pchItemName == pItemDropInfoOther->pchItemName)
					{
						bFoundInOther = true;
						break;
					}
				}
				FOR_EACH_END

				if( bFoundInOther )
				{
					break;
				}
			}
			FOR_EACH_END
			if( !bFoundInOther )
			{
				ErrorFilenamef(pZoneRewardsDef->pchFilename, "Item \"%s\" is in a Set, but not a Source.", pItemDropInfo->pchItemName);
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	// Check all ItemSources.
	FOR_EACH_IN_EARRAY(pZoneRewardsDef->ppItemSource, ZoneRewardsItemSource, pItemSource)
	{
		FOR_EACH_IN_EARRAY(pItemSource->ppItemDropInfo, ZoneRewardsItemDropInfo, pItemDropInfo)
		{
			bool bFoundInOther = false;
			// Make sure all DropInfo item names are really itemdefs.
			ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pItemDropInfo->pchItemName);
			if( !pItemDef )
			{
				ErrorFilenamef(pZoneRewardsDef->pchFilename, "Item \"%s\" in Source \"%s\" isn't a real item.", pItemDropInfo->pchItemName, pItemSource->pchName);
			}

			//// Make sure all DropInfo item names are in an ItemSet too
			// For now, we don't require every item to be in an ItemSet.
			//FOR_EACH_IN_EARRAY(pZoneRewardsDef->ppItemSet, ZoneRewardsItemSet, pItemSet)
			//{
			//	FOR_EACH_IN_EARRAY(pItemSet->ppItemDropInfo, ZoneRewardsItemDropInfo, pItemDropInfoOther)
			//	{
			//		if(pItemDropInfo->pchItemName == pItemDropInfoOther->pchItemName)
			//		{
			//			bFoundInOther = true;
			//			break;
			//		}
			//	}
			//	FOR_EACH_END

			//	if( bFoundInOther )
			//	{
			//		break;
			//	}
			//}
			//FOR_EACH_END

			//if( !bFoundInOther )
			//{
			//	ErrorFilenamef(pZoneRewardsDef->pchFilename, "Item \"%s\" is in a Source, but not a Set.", pItemDropInfo->pchItemName);
			//}
		}
		FOR_EACH_END
	}
	FOR_EACH_END
}

static int ZoneRewards_ValidateEventsCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, ZoneRewardsDef * pZoneRewardsDef, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_BINNING:
		{
			return VALIDATE_HANDLED;
		}
		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			if (IsGameServerBasedType())
			{
				ZoneRewards_ValidateRefs(pZoneRewardsDef);
				return VALIDATE_HANDLED;
			}
		}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void ZoneRewards_RegisterZoneRewardsDictionary(void)
{
	g_hZoneRewardsDictionary = RefSystem_RegisterSelfDefiningDictionary("ZoneRewards", false, parse_ZoneRewardsDef, true, true, NULL);

	resDictManageValidation(g_hZoneRewardsDictionary, ZoneRewards_ValidateEventsCB);

	if (IsServer()) 
	{
		resDictProvideMissingResources(g_hZoneRewardsDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hZoneRewardsDictionary, ".Name", NULL, NULL, NULL, NULL);
		}
	} 
	else 
	{
		resDictRequestMissingResources(g_hZoneRewardsDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

static void ZoneRewards_ReloadDefs(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Zone Rewards Def(s)...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hZoneRewardsDictionary);

	loadend_printf(" done (%d Zone Rewards Def(s))", RefSystem_GetDictionaryNumberOfReferentInfos(g_hZoneRewardsDictionary));
}

// Loads the zone rewards defs from the disk
AUTO_STARTUP(AutoStart_ZoneRewards) ASTRT_DEPS(Items);
void ZoneRewards_LoadDefs(void)
{
	resLoadResourcesFromDisk(g_hZoneRewardsDictionary, 
		ZONE_REWARDS_BASE_DIR, 
		".zonerewards", 
		"ZoneRewards.bin", 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, ZONE_REWARDS_BASE_DIR "/*.zonerewards", ZoneRewards_ReloadDefs);
	}
}

ZoneRewardsDef *ZoneRewards_GetRewardsDef(const char *pchDefName)
{
	return RefSystem_ReferentFromString(g_hZoneRewardsDictionary, pchDefName);
}

void ZoneRewards_UpdateItemDropInfoItemPointers(ZoneRewardsItemDropInfo **eaItemDropInfo)
{
	FOR_EACH_IN_EARRAY(eaItemDropInfo, ZoneRewardsItemDropInfo, pItemDropInfo)
	{
		if( pItemDropInfo->pFakeItem == NULL )
		{
			pItemDropInfo->pFakeItem = CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(pItemDropInfo->pchItemName, 0, 0, NULL, NULL, NULL, false, NULL));
		}
	}
	FOR_EACH_END
}