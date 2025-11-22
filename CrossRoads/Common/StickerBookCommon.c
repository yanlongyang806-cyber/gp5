#include "StickerBookCommon.h"

#include "GlobalTypes.h"
#include "file.h"
#include "fileutil.h"
#include "ResourceManager.h"
#include "referencesystem.h"
#include "FolderCache.h"
#include "error.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"

#include "Entity.h"
#include "Player.h"

#ifdef GAMECLIENT
#include "UIGen.h"
#include "gclEntity.h"
#endif

#include "Entity_h_ast.h"
#include "Player_h_ast.h"

#include "AutoTransDefs.h"

#include "AutoGen/StickerBookCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DefineContext* g_pStickerBookCollectionTypes;
DictionaryHandle g_hStickerBookDictionary = NULL;

#define STICKER_BOOK_BASE_DIR "defs/stickerbook"

static StickerBookItemAliases *pStickerBookItemAliases = NULL;

static StashTable s_StickerBookItemTracking = NULL;
static StashTable s_StickerBookItemAliases = NULL;

StickerBookItemSet* StickerBook_ItemSetGetByRefString(const char *pchRefString)
{
	StickerBookCollection *pStickerBookCollection = NULL;
	static char *parentName = NULL;
	char *pcChildName = NULL;

	if(nullStr(pchRefString))
		return NULL;

	estrCopy2(&parentName, pchRefString);
	pcChildName = strstr(parentName, "::");
	if(pcChildName)
	{
		*pcChildName = 0;
		pcChildName += 2;
	}

	pStickerBookCollection = (StickerBookCollection*)StickerBook_GetCollection(parentName);
	if(pStickerBookCollection && pcChildName)
	{
		FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
		{
			if(0 == stricmp(pStickerBookItemSet->pchName, pcChildName))
				return pStickerBookItemSet;
		}
		FOR_EACH_END;
	}

	return NULL;
}

StickerBookCollection* StickerBook_CollectionGetByRefString(const char *pchRefString)
{
	static char *parentName = NULL;
	char *pcChildName = NULL;

	if(nullStr(pchRefString))
		return NULL;

	estrCopy2(&parentName, pchRefString);
	pcChildName = strstr(parentName, "::");
	if(pcChildName)
	{
		*pcChildName = 0;
		pcChildName += 2;
	}

	if(pcChildName)
		return StickerBook_GetCollection(parentName);
	return NULL;
}

static void StickerBook_CreateRefStringsForCollection(StickerBookCollection *pStickerBookCollection)
{
	char *estrTemp = NULL;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		estrPrintf(&estrTemp, "%s::%s", pStickerBookCollection->pchName, pStickerBookItemSet->pchName);
		pStickerBookItemSet->pchRefString = allocAddString(estrTemp);
	}
	FOR_EACH_END;

	estrDestroy(&estrTemp);
}

static void StickerBook_Validate(StickerBookCollection *pStickerBookCollection)
{
	if(kStickerBookCollectionType_Unspecified == pStickerBookCollection->eStickerBookCollectionType)
		ErrorFilenamef(pStickerBookCollection->pchFilename, "Unspecified Type for StickerBookCollection \"%s\".", pStickerBookCollection->pchName);

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		if(pStickerBookItemSet->iPoints < 0)
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBook Item sets should not have a negative Points: collection \"%s\", set \"%s\".",
				pStickerBookCollection->pchName, pStickerBookItemSet->pchName);

		if(0 == eaSize(&pStickerBookItemSet->ppItems))
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBook items sets cannot be empty: collection \"%s\", set \"%s\".",
				pStickerBookCollection->pchName, pStickerBookItemSet->pchName);

		if(pStickerBookItemSet->iPoints < 1 && pStickerBookItemSet->pchRewardTitleItem && pStickerBookItemSet->pchRewardTitleItem[0])
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBook Item sets cannot reward a title if the have give zero Points: collection \"%s\", set \"%s\", title \"%s\".",
				pStickerBookCollection->pchName, pStickerBookItemSet->pchName, pStickerBookItemSet->pchRewardTitleItem);

		FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
		{
			if(pStickerBookItem->iPoints < 1)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBook items in a set must grant at least 1 award point: collection \"%s\", set \"%s\", item \"%s\".",
					pStickerBookCollection->pchName, pStickerBookItemSet->pchName, pStickerBookItem->pchItemName);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
	{
		if(pStickerBookItemLocation->iPoints != 0)
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBook item locations should not award points (%d): collection \"%s\", location \"%s\".",
				pStickerBookItemLocation->iPoints, pStickerBookCollection->pchName, pStickerBookItemLocation->pchName);
		FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
		{
			if(pStickerBookItem->iPoints != 0)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBook items in a location should not award points (%d): collection \"%s\", location \"%s\", item \"%s\".",
					pStickerBookItem->iPoints, pStickerBookCollection->pchName, pStickerBookItemLocation->pchName, pStickerBookItem->pchItemName);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

#ifdef GAMESERVER

static void StickerBook_ValidateRefs(StickerBookCollection *pStickerBookCollection)
{
	if(REF_STRING_FROM_HANDLE(pStickerBookCollection->msgShortName.hMessage))
	{
		if(!GET_REF(pStickerBookCollection->msgShortName.hMessage))
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookCollection ShortName refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pStickerBookCollection->msgShortName.hMessage));
	}
	else
		ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookCollection ShortName message is empty");

	if(REF_STRING_FROM_HANDLE(pStickerBookCollection->msgTitle.hMessage))
	{
		if(!GET_REF(pStickerBookCollection->msgTitle.hMessage))
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookCollection Title refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pStickerBookCollection->msgTitle.hMessage));
	}
	else
		ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookCollection Title message is empty");

	if(REF_STRING_FROM_HANDLE(pStickerBookCollection->msgDescription.hMessage))
	{
		if(!GET_REF(pStickerBookCollection->msgDescription.hMessage))
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookCollection Description refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pStickerBookCollection->msgDescription.hMessage));
	}
	else
		ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookCollection Description message is empty");

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		if(REF_STRING_FROM_HANDLE(pStickerBookItemSet->msgDisplayName.hMessage))
		{
			if(!GET_REF(pStickerBookItemSet->msgDisplayName.hMessage))
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet '%s' DisplayName refers to non-existent message '%s'", pStickerBookItemSet->pchName, REF_STRING_FROM_HANDLE(pStickerBookItemSet->msgDisplayName.hMessage));
		}
		else
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet '%s' DisplayName message is empty", pStickerBookItemSet->pchName);

		if(REF_STRING_FROM_HANDLE(pStickerBookItemSet->msgBlurb.hMessage))
		{
			if(!GET_REF(pStickerBookItemSet->msgBlurb.hMessage))
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet '%s' Blurb refers to non-existent message '%s'", pStickerBookItemSet->pchName, REF_STRING_FROM_HANDLE(pStickerBookItemSet->msgBlurb.hMessage));
		}
		else
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet '%s' Blurb message is empty", pStickerBookItemSet->pchName);

		if(!pStickerBookItemSet->pchRefString || !pStickerBookItemSet->pchRefString[0])
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet \"%s\" does not have its reference string set. Tell Andy Ames.",
				pStickerBookItemSet->pchName);

		if(pStickerBookItemSet->pchRewardTitleItem && pStickerBookItemSet->pchRewardTitleItem[0])
		{
			ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItemSet->pchRewardTitleItem);
			if(!pItemDef)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet \"%s\" has a non-existent RewardTitleItem \"%s\".",
					pStickerBookItemSet->pchName, pStickerBookItemSet->pchRewardTitleItem);
			else if(pItemDef->eType != kItemType_Title)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemSet \"%s\" has a RewardTitleItem \"%s\" that is not flagged as a title.",
					pStickerBookItemSet->pchName, pStickerBookItemSet->pchRewardTitleItem);
		}

		FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
		{
			bool bFoundInOther = false;

			ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);
			if(!pItemDef)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItem \"%s\" in StickerBookItemSet \"%s\" of StickerBookCollection \"%s\" isn't a real item.",
					pStickerBookItem->pchItemName, pStickerBookItemSet->pchName, pStickerBookCollection->pchName);

			if(REF_STRING_FROM_HANDLE(pStickerBookItem->msgSubLocation.hMessage))
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItem '%s' in StickerBookSet '%s' has a SubLocation, which is invalid.", pStickerBookItem->pchItemName, pStickerBookItemSet->pchName);

			// Make sure all item names are in an Set too
			FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
			{
				FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItemOther)
				{
					if(pStickerBookItem->pchItemName == pStickerBookItemOther->pchItemName)
					{
						bFoundInOther = true;
						break;
					}
				}
				FOR_EACH_END
			
				if(bFoundInOther)
					break;
			}
			FOR_EACH_END
			
			if(!bFoundInOther)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItem \"%s\" is in StickerBookItemSet \"%s\" of StickerBookCollection \"%s\", but not a StickerBookItemLocation.",
					pStickerBookItem->pchItemName, pStickerBookItemSet->pchName, pStickerBookCollection->pchName);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
	{
		if(REF_STRING_FROM_HANDLE(pStickerBookItemLocation->msgDisplayName.hMessage))
		{
			if(!GET_REF(pStickerBookItemLocation->msgDisplayName.hMessage))
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemLocation '%s' DisplayName refers to non-existent message '%s'", pStickerBookItemLocation->pchName, REF_STRING_FROM_HANDLE(pStickerBookItemLocation->msgDisplayName.hMessage));
		}
		else
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemLocation '%s' DisplayName message is empty", pStickerBookItemLocation->pchName);

		if(REF_STRING_FROM_HANDLE(pStickerBookItemLocation->msgBlurb.hMessage))
		{
			if(!GET_REF(pStickerBookItemLocation->msgBlurb.hMessage))
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemLocation '%s' Blurb refers to non-existent message '%s'", pStickerBookItemLocation->pchName, REF_STRING_FROM_HANDLE(pStickerBookItemLocation->msgBlurb.hMessage));
		}
		else
			ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItemLocation '%s' Blurb message is empty", pStickerBookItemLocation->pchName);

		FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
		{
			bool bFoundInOther = false;

			ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);
			if(!pItemDef)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItem \"%s\" in StickerBookItemLocation \"%s\" of StickerBookCollection \"%s\" isn't a real item.",
					pStickerBookItem->pchItemName, pStickerBookItemLocation->pchName, pStickerBookCollection->pchName);

			if(!GET_REF(pStickerBookItem->msgSubLocation.hMessage) && REF_STRING_FROM_HANDLE(pStickerBookItem->msgSubLocation.hMessage))
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItem '%s' in StickerBookLocation '%s' SubLocation refers to non-existent message '%s'", pStickerBookItem->pchItemName, pStickerBookItemLocation->pchName, REF_STRING_FROM_HANDLE(pStickerBookItem->msgSubLocation.hMessage));

			// Make sure all item names are in an Set too
			FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
			{
				FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItemOther)
				{
					if(pStickerBookItem->pchItemName == pStickerBookItemOther->pchItemName)
					{
						bFoundInOther = true;
						break;
					}
				}
				FOR_EACH_END
			
				if(bFoundInOther)
					break;
			}
			FOR_EACH_END
			
			if(!bFoundInOther)
				ErrorFilenamef(pStickerBookCollection->pchFilename, "StickerBookItem \"%s\" is in StickerBookItemLocation \"%s\" of StickerBookCollection \"%s\", but not a StickerBookItemSet.",
					pStickerBookItem->pchItemName, pStickerBookItemLocation->pchName, pStickerBookCollection->pchName);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

#endif

static void StickerBook_FixBackPointers(StickerBookCollection *pStickerBookCollection)
{
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		pStickerBookItemSet->pStickerBookCollection = pStickerBookCollection;

		FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
		{
			pStickerBookItem->pStickerBookItemSet = pStickerBookItemSet;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
	{
		pStickerBookItemLocation->pStickerBookCollection = pStickerBookCollection;

		FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
		{
			pStickerBookItem->pStickerBookItemLocation = pStickerBookItemLocation;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

static int StickerBook_ValidateCB(enumResourceValidateType eType, const char *pcDictName, const char *pcResourceName, StickerBookCollection *pStickerBookCollection, U32 userID)
{
	switch(eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			StickerBook_CreateRefStringsForCollection(pStickerBookCollection);

			StickerBook_Validate(pStickerBookCollection);

			return VALIDATE_HANDLED;
		}
		break;

		case RESVALIDATE_FINAL_LOCATION:
		{
#ifdef GAMECLIENT
			// This is here to make testing locally easier when hot reloading changes to a StickerBook and the points it awards.
			Entity *pEnt = entActivePlayerPtr();
			if(pEnt && pEnt->pPlayer)
				StructDestroySafe(parse_StickerBookPointCache, &pEnt->pPlayer->pStickerBookPointCache);
#endif
			StickerBook_FixBackPointers(pStickerBookCollection);

			return VALIDATE_HANDLED;
		}
		break;

		case RESVALIDATE_CHECK_REFERENCES:
		{
#ifdef GAMESERVER
			StickerBook_ValidateRefs(pStickerBookCollection);

			return VALIDATE_HANDLED;
#endif
		}
		break;
	}

	return VALIDATE_NOT_HANDLED;
}

#ifdef GAMESERVER

static void StickerBook_StartTrackingItem(StickerBookItem *pStickerBookItem)
{
	StashElement element;
	StickerBookTrackedItem *pStickerBookTrackedItem = calloc(1, sizeof(StickerBookTrackedItem));
	if(!stashAddPointerAndGetElement(s_StickerBookItemTracking, pStickerBookItem->pchItemName, pStickerBookTrackedItem, /*bOverwriteIfFound=*/false, &element))
	{
		// we already added this item for another collection
		free(pStickerBookTrackedItem);
		pStickerBookTrackedItem = (StickerBookTrackedItem *)stashElementGetPointer(element);
	}
	eaPush(&pStickerBookTrackedItem->ppItems, pStickerBookItem);
}

static void StickerBook_StartTrackingCollection(StickerBookCollection *pStickerBookCollection)
{
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
		{
			StickerBook_StartTrackingItem(pStickerBookItem);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
	{
		FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
		{
			StickerBook_StartTrackingItem(pStickerBookItem);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

static void StickerBook_StopTrackingItem(StickerBookItem *pStickerBookItem)
{
	StickerBookTrackedItem *pStickerBookTrackedItem = NULL;
	if(stashFindPointer(s_StickerBookItemTracking, pStickerBookItem->pchItemName, &pStickerBookTrackedItem) && pStickerBookTrackedItem)
	{
		FOR_EACH_IN_EARRAY(pStickerBookTrackedItem->ppItems, StickerBookItem, pStickerBookItemOther)
		{
			if(pStickerBookItem == pStickerBookItemOther)
				eaRemoveFast(&pStickerBookTrackedItem->ppItems, FOR_EACH_IDX(pStickerBookTrackedItem->ppItems, pStickerBookItemOther));
		}
		FOR_EACH_END;

		if(0 == eaSize(&pStickerBookTrackedItem->ppItems))
		{
			eaDestroy(&pStickerBookTrackedItem->ppItems);
			stashRemovePointer(s_StickerBookItemTracking, pStickerBookItem->pchItemName, &pStickerBookTrackedItem);
			free(pStickerBookTrackedItem);
		}
	}
}

static void StickerBook_StopTrackingCollection(StickerBookCollection *pStickerBookCollection)
{
	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
		{
			StickerBook_StopTrackingItem(pStickerBookItem);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemLocation, StickerBookItemLocation, pStickerBookItemLocation)
	{
		FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
		{
			StickerBook_StopTrackingItem(pStickerBookItem);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

#endif

static void StickerBook_EventCB(enumResourceEventType eType, const char *pcDictName, const char *pcRefData, Referent pReferent, void *pUserData)
{
#ifdef GAMESERVER
	StickerBookCollection *pStickerBookCollection = (StickerBookCollection *)pReferent;

	// Updates tracking data
	if(RESEVENT_RESOURCE_PRE_MODIFIED == eType || RESEVENT_RESOURCE_REMOVED == eType)
	{
		// stop tracking this Collection's items
		StickerBook_StopTrackingCollection(pStickerBookCollection);
	}

	if(RESEVENT_RESOURCE_ADDED == eType || RESEVENT_RESOURCE_MODIFIED == eType)
	{
		// start tracking this Collection's items
		StickerBook_StartTrackingCollection(pStickerBookCollection);
	}
#endif
}

AUTO_RUN;
void StickerBook_RegisterDictionary(void)
{
	g_hStickerBookDictionary = RefSystem_RegisterSelfDefiningDictionary("StickerBook", false, parse_StickerBookCollection, true, true, NULL);

	resDictManageValidation(g_hStickerBookDictionary, StickerBook_ValidateCB);

	if(isDevelopmentMode() || isProductionEditMode())
		resDictMaintainInfoIndex("StickerBook", ".name", NULL, NULL, NULL, NULL);

	if(IsServer())
		resDictRegisterEventCallback(g_hStickerBookDictionary, StickerBook_EventCB, NULL);
}

static void StickerBook_Reload(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Sticker Book Collection(s)...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hStickerBookDictionary);

	loadend_printf(" done (%d Sticker Book Collection(s))", RefSystem_GetDictionaryNumberOfReferentInfos(g_hStickerBookDictionary));
}

static void StickerBook_LoadItemAliasesInternal(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading Sticker Book Item Aliases...");

	StructDestroySafe(parse_StickerBookItemAliases, &pStickerBookItemAliases);
	pStickerBookItemAliases = StructCreate(parse_StickerBookItemAliases);

	stashTableClear(s_StickerBookItemAliases);

	ParserLoadFiles(NULL, "defs/config/StickerBookItemAliases.def", "StickerBookItemAliases.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_StickerBookItemAliases, pStickerBookItemAliases);

	FOR_EACH_IN_EARRAY(pStickerBookItemAliases->eaItemAliases, StickerBookItemAlias, pStickerBookItemAlias)
	{
		FOR_EACH_IN_EARRAY(pStickerBookItemAlias->eaItems, const char, astrItemAlias)
		{
			if(!stashAddPointer(s_StickerBookItemAliases, astrItemAlias, pStickerBookItemAlias->pchName, /*bOverwriteIfFound=*/false))
			{
				// we already added this item for another alias
				ErrorFilenamef("defs/config/StickerBookItemAliases.def", "Duplicate Sticker Book Item Alias: %s", astrItemAlias);
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	loadend_printf(" done (%d Sticker Book Item Aliases)", eaSize(&pStickerBookItemAliases->eaItemAliases));
}

static void StickerBook_LoadCollectionTypesInternal(const char *pchPath, S32 iWhen)
{
	const char *pchMessage;
	S32 iSize;
	if(g_pStickerBookCollectionTypes)
		DefineDestroy(g_pStickerBookCollectionTypes);
	g_pStickerBookCollectionTypes = DefineCreate();
	loadstart_printf("StickerBookCollectionTypes... ");
	iSize = DefineLoadFromFile(g_pStickerBookCollectionTypes, "StickerBookCollectionType", "StickerBookCollectionTypes", NULL, "defs/config/StickerBookCollectionTypes.def", "StickerBookCollectionTypes.bin", 1);
	loadend_printf(" done (%d StickerBookCollectionTypes).", iSize - 1);
	if(IsClient() || IsGameServerBasedType())
		if(pchMessage = StaticDefineVerifyMessages(StickerBookCollectionTypeEnum))
			ErrorFilenamef("defs/config/StickerBookCollectionTypes.def", "Not all StickerBookCollectionType messages were found: %s", pchMessage);
}

AUTO_STARTUP(AutoStart_StickerBook) ASTRT_DEPS(Items);
void StickerBook_Load(void)
{
	s_StickerBookItemTracking = stashTableCreate(1024, StashDefault, StashKeyTypeAddress, sizeof(void*));
	s_StickerBookItemAliases = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);

	StickerBook_LoadCollectionTypesInternal(NULL, 0);
	StickerBook_LoadItemAliasesInternal(NULL, 0);

	resLoadResourcesFromDisk(g_hStickerBookDictionary, 
		STICKER_BOOK_BASE_DIR, 
		".stickerbook", 
		"StickerBook.bin", 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY | PARSER_BINS_ARE_SHARED);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/StickerBookCollectionTypes.def", StickerBook_LoadCollectionTypesInternal);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/StickerBookItemAliases.def", StickerBook_LoadItemAliasesInternal);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, STICKER_BOOK_BASE_DIR "/*.stickerbook", StickerBook_Reload);
	}

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(StickerBookCollectionTypeEnum, "StickerBookCollectionType_");
#endif
}

StickerBookCollection *StickerBook_GetCollection(const char *pchCollectionName)
{
	return RefSystem_ReferentFromString(g_hStickerBookDictionary, pchCollectionName);
}

void StickerBook_UpdateItemPointers(StickerBookItem **eaStickerBookItems)
{
	FOR_EACH_IN_EARRAY(eaStickerBookItems, StickerBookItem, pStickerBookItem)
	{
		if(pStickerBookItem->pFakeItem == NULL)
			pStickerBookItem->pFakeItem = CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(pStickerBookItem->pchItemName, 0, 0, NULL, NULL, NULL, false, NULL));
	}
	FOR_EACH_END;
}

void StickerBook_UpdateItemSetPointers(StickerBookItemSet **eaStickerBookItemSets)
{
	FOR_EACH_IN_EARRAY(eaStickerBookItemSets, StickerBookItemSet, pStickerBookItemSet)
	{
		if(pStickerBookItemSet->pchRewardTitleItem && pStickerBookItemSet->pchRewardTitleItem[0] && pStickerBookItemSet->pFakeRewardTitleItem == NULL)
			pStickerBookItemSet->pFakeRewardTitleItem = CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(pStickerBookItemSet->pchRewardTitleItem, 0, 0, NULL, NULL, NULL, false, NULL));
	}
	FOR_EACH_END;
}

const char *StickerBook_DoesItemParticipate(const char *astrItemName)
{
	if(s_StickerBookItemTracking)
	{
		if(stashFindPointer(s_StickerBookItemTracking, astrItemName, NULL))
			return astrItemName;

		if(s_StickerBookItemAliases)
		{
			char *astrItemAlias = NULL;
			if(stashFindPointer(s_StickerBookItemAliases, astrItemName, &astrItemAlias))
				if(stashFindPointer(s_StickerBookItemTracking, astrItemAlias, NULL))
					return (const char *)astrItemAlias;
		}
	}
	return NULL;
}

StickerBookTrackedItem *StickerBook_GetTrackedItem(const char *astrItemName)
{
	StickerBookTrackedItem *pStickerBookTrackedItem = NULL;
	if(s_StickerBookItemTracking)
	{
		if(stashFindPointer(s_StickerBookItemTracking, astrItemName, &pStickerBookTrackedItem))
			return pStickerBookTrackedItem;

		if(s_StickerBookItemAliases)
		{
			char *astrItemAlias = NULL;
			if(stashFindPointer(s_StickerBookItemAliases, astrItemName, &astrItemAlias))
				if(stashFindPointer(s_StickerBookItemTracking, astrItemAlias, &pStickerBookTrackedItem))
					return pStickerBookTrackedItem;
		}
	}
	return pStickerBookTrackedItem;
}

#define RETURN_CACHED_POINTS_OR_INIT(pEnt, stickerBookPointCacheData) \
	if(stickerBookPointCacheData.iLastItemInfoSize == eaSize(&pEnt->pPlayer->eaStickerBookItemInfo)) \
		return stickerBookPointCacheData.iPoints; \
	stickerBookPointCacheData.iPoints = 0;

#define UPDATE_CACHED_POINTS(pEnt, stickerBookPointCacheData) \
	stickerBookPointCacheData.iLastItemInfoSize = eaSize(&pEnt->pPlayer->eaStickerBookItemInfo);

#define RETURN_CACHED_TOTAL_POINTS_OR_INIT(stickerBookPointCacheData) \
	if(stickerBookPointCacheData.bTotalPointsValid) \
		return stickerBookPointCacheData.iTotalPoints; \
	stickerBookPointCacheData.iTotalPoints = 0;

#define UPDATE_CACHED_TOTAL_POINTS(stickerBookPointCacheData) \
	stickerBookPointCacheData.bTotalPointsValid = true;

static void EnsureStickerBookPointCache(SA_PARAM_NN_VALID Entity *pEnt)
{
	if(!pEnt->pPlayer->pStickerBookPointCache)
	{
		pEnt->pPlayer->pStickerBookPointCache = StructCreate(parse_StickerBookPointCache);
		eaIndexedEnable(&pEnt->pPlayer->pStickerBookPointCache->eaPointCacheByCollectionType, parse_StickerBookPointCacheByCollectionType);
		eaIndexedEnable(&pEnt->pPlayer->pStickerBookPointCache->eaPointCacheByCollection, parse_StickerBookPointCacheByCollection);
	}
}

static StickerBookPointCacheByCollectionType *EnsureStickerBookPointCacheByCollectionType(SA_PARAM_NN_VALID Entity *pEnt, StickerBookCollectionType type)
{
	StickerBookPointCacheByCollectionType *pStickerBookPointCacheByCollectionType = eaIndexedGetUsingInt(&pEnt->pPlayer->pStickerBookPointCache->eaPointCacheByCollectionType, type);
	if(!pStickerBookPointCacheByCollectionType)
	{
		pStickerBookPointCacheByCollectionType = StructCreate(parse_StickerBookPointCacheByCollectionType);
		pStickerBookPointCacheByCollectionType->type = type;
		eaIndexedPushUsingIntIfPossible(&pEnt->pPlayer->pStickerBookPointCache->eaPointCacheByCollectionType, type, pStickerBookPointCacheByCollectionType);
	}
	return pStickerBookPointCacheByCollectionType;
}

static StickerBookPointCacheByCollection *EnsureStickerBookPointCacheByCollection(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection)
{
	StickerBookPointCacheByCollection *pStickerBookPointCacheByCollection = eaIndexedGetUsingString(&pEnt->pPlayer->pStickerBookPointCache->eaPointCacheByCollection, pStickerBookCollection->pchName);
	if(!pStickerBookPointCacheByCollection)
	{
		pStickerBookPointCacheByCollection = StructCreate(parse_StickerBookPointCacheByCollection);
		pStickerBookPointCacheByCollection->astrCollectionName = pStickerBookCollection->pchName;
		eaIndexedPushUsingStringIfPossible(&pEnt->pPlayer->pStickerBookPointCache->eaPointCacheByCollection, pStickerBookCollection->pchName, pStickerBookPointCacheByCollection);
		eaIndexedEnable(&pStickerBookPointCacheByCollection->eaPointCacheBySet, parse_StickerBookPointCacheBySet);
	}
	return pStickerBookPointCacheByCollection;
}

static StickerBookPointCacheBySet *EnsureStickerBookPointCacheBySet(SA_PARAM_NN_VALID StickerBookPointCacheByCollection *pStickerBookPointCacheByCollection, SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet)
{
	StickerBookPointCacheBySet *pStickerBookPointCacheBySet = eaIndexedGetUsingString(&pStickerBookPointCacheByCollection->eaPointCacheBySet, pStickerBookItemSet->pchName);
	if(!pStickerBookPointCacheBySet)
	{
		pStickerBookPointCacheBySet = StructCreate(parse_StickerBookPointCacheBySet);
		pStickerBookPointCacheBySet->astrSetName = pStickerBookItemSet->pchName;
		eaIndexedPushUsingStringIfPossible(&pStickerBookPointCacheByCollection->eaPointCacheBySet, pStickerBookItemSet->pchName, pStickerBookPointCacheBySet);
	}
	return pStickerBookPointCacheBySet;
}

U32 StickerBook_CountPoints(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	EnsureStickerBookPointCache(pEnt);

	RETURN_CACHED_POINTS_OR_INIT(pEnt, pEnt->pPlayer->pStickerBookPointCache->pointCacheData);

	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
	{
		pEnt->pPlayer->pStickerBookPointCache->pointCacheData.iPoints += StickerBook_CountPointsForCollection(pStickerBookCollection, pEnt, &bFullCount);
	}

	if(bFullCount)
		UPDATE_CACHED_POINTS(pEnt, pEnt->pPlayer->pStickerBookPointCache->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pEnt->pPlayer->pStickerBookPointCache->pointCacheData.iPoints;
}

U32 StickerBook_CountPointsForCollectionType(SA_PARAM_NN_VALID StickerBookCollectionType type, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;
	StickerBookPointCacheByCollectionType *pStickerBookPointCacheByCollectionType;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	EnsureStickerBookPointCache(pEnt);

	pStickerBookPointCacheByCollectionType = EnsureStickerBookPointCacheByCollectionType(pEnt, type);

	RETURN_CACHED_POINTS_OR_INIT(pEnt, pStickerBookPointCacheByCollectionType->pointCacheData);

	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
		if(type == pStickerBookCollection->eStickerBookCollectionType)
			pStickerBookPointCacheByCollectionType->pointCacheData.iPoints += StickerBook_CountPointsForCollection(pStickerBookCollection, pEnt, &bFullCount);

	if(bFullCount)
		UPDATE_CACHED_POINTS(pEnt, pStickerBookPointCacheByCollectionType->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pStickerBookPointCacheByCollectionType->pointCacheData.iPoints;
}

U32 StickerBook_CountPointsForCollection(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	StickerBookPointCacheByCollection *pStickerBookPointCacheByCollection;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	EnsureStickerBookPointCache(pEnt);

	pStickerBookPointCacheByCollection = EnsureStickerBookPointCacheByCollection(pEnt, pStickerBookCollection);

	RETURN_CACHED_POINTS_OR_INIT(pEnt, pStickerBookPointCacheByCollection->pointCacheData);

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		pStickerBookPointCacheByCollection->pointCacheData.iPoints += StickerBook_CountPointsForSet(pStickerBookItemSet, pEnt, &bFullCount);
	}
	FOR_EACH_END;

	if(bFullCount)
		UPDATE_CACHED_POINTS(pEnt, pStickerBookPointCacheByCollection->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pStickerBookPointCacheByCollection->pointCacheData.iPoints;
}

U32 StickerBook_CountPointsForSet(SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	StickerBookPointCacheByCollection *pStickerBookPointCacheByCollection;
	StickerBookPointCacheBySet *pStickerBookPointCacheBySet;
	bool bCompleted = true;
	bool bRequirementsMet = false;
	NOCONST(Entity) *pEntNoConst;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	pEntNoConst = CONTAINER_NOCONST(Entity, pEnt);

	EnsureStickerBookPointCache(pEnt);

	pStickerBookPointCacheByCollection = EnsureStickerBookPointCacheByCollection(pEnt, pStickerBookItemSet->pStickerBookCollection);

	pStickerBookPointCacheBySet = EnsureStickerBookPointCacheBySet(pStickerBookPointCacheByCollection, pStickerBookItemSet);

	RETURN_CACHED_POINTS_OR_INIT(pEnt, pStickerBookPointCacheBySet->pointCacheData);

	FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
	{
		ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);
		if(pItemDef)
		{
			if(!pItemDef->pRestriction ||
				(itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)
					&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)))
			{
				bRequirementsMet = true;
				if(eaIndexedGetUsingString(&pEnt->pPlayer->eaStickerBookItemInfo, pStickerBookItem->pchItemName))
					pStickerBookPointCacheBySet->pointCacheData.iPoints += pStickerBookItem->iPoints;
				else
					bCompleted = false;
			}
		}
		else
			bFullCount = false;
	}
	FOR_EACH_END;
	if(bCompleted && bRequirementsMet)
		pStickerBookPointCacheBySet->pointCacheData.iPoints += pStickerBookItemSet->iPoints;

	if(bFullCount)
		UPDATE_CACHED_POINTS(pEnt, pStickerBookPointCacheBySet->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pStickerBookPointCacheBySet->pointCacheData.iPoints;
}

U32 StickerBook_CountPointsForLocation(SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	U32 iPoints = 0;
	bool bCompleted = true;
	bool bRequirementsMet = false;
	NOCONST(Entity) *pEntNoConst;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	pEntNoConst = CONTAINER_NOCONST(Entity, pEnt);

	assertmsgf(0, "Unsupported StickerBook function: %s", __FUNCTION__);

	FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
	{
		ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);
		if(pItemDef)
		{
			if(!pItemDef->pRestriction ||
				(itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)
				&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)))
			{
				bRequirementsMet = true;
				if(eaIndexedGetUsingString(&pEnt->pPlayer->eaStickerBookItemInfo, pStickerBookItem->pchItemName))
					iPoints += pStickerBookItem->iPoints;
				else
					bCompleted = false;
			}
		}
		else
			bFullCount = false;
	}
	FOR_EACH_END;
	if(bCompleted && bRequirementsMet)
		iPoints += pStickerBookItemLocation->iPoints;

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return iPoints;
}

U32 StickerBook_CountTotalPoints(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	EnsureStickerBookPointCache(pEnt);

	RETURN_CACHED_TOTAL_POINTS_OR_INIT(pEnt->pPlayer->pStickerBookPointCache->pointCacheData);

	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
	{
		pEnt->pPlayer->pStickerBookPointCache->pointCacheData.iTotalPoints += StickerBook_CountTotalPointsForCollection(pStickerBookCollection, pEnt, &bFullCount);
	}

	if(bFullCount)
		UPDATE_CACHED_TOTAL_POINTS(pEnt->pPlayer->pStickerBookPointCache->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pEnt->pPlayer->pStickerBookPointCache->pointCacheData.iTotalPoints;
}

U32 StickerBook_CountTotalPointsForCollectionType(SA_PARAM_NN_VALID StickerBookCollectionType type, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	RefDictIterator iter;
	StickerBookCollection *pStickerBookCollection;
	StickerBookPointCacheByCollectionType *pStickerBookPointCacheByCollectionType;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	EnsureStickerBookPointCache(pEnt);

	pStickerBookPointCacheByCollectionType = EnsureStickerBookPointCacheByCollectionType(pEnt, type);

	RETURN_CACHED_TOTAL_POINTS_OR_INIT(pStickerBookPointCacheByCollectionType->pointCacheData);

	RefSystem_InitRefDictIterator(g_hStickerBookDictionary, &iter);
	while(pStickerBookCollection = RefSystem_GetNextReferentFromIterator(&iter))
		if(type == pStickerBookCollection->eStickerBookCollectionType)
			pStickerBookPointCacheByCollectionType->pointCacheData.iTotalPoints += StickerBook_CountTotalPointsForCollection(pStickerBookCollection, pEnt, &bFullCount);

	if(bFullCount)
		UPDATE_CACHED_TOTAL_POINTS(pStickerBookPointCacheByCollectionType->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pStickerBookPointCacheByCollectionType->pointCacheData.iTotalPoints;
}

U32 StickerBook_CountTotalPointsForCollection(SA_PARAM_NN_VALID StickerBookCollection *pStickerBookCollection, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	StickerBookPointCacheByCollection *pStickerBookPointCacheByCollection;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	EnsureStickerBookPointCache(pEnt);

	pStickerBookPointCacheByCollection = EnsureStickerBookPointCacheByCollection(pEnt, pStickerBookCollection);

	RETURN_CACHED_TOTAL_POINTS_OR_INIT(pStickerBookPointCacheByCollection->pointCacheData);

	FOR_EACH_IN_EARRAY(pStickerBookCollection->ppItemSet, StickerBookItemSet, pStickerBookItemSet)
	{
		pStickerBookPointCacheByCollection->pointCacheData.iTotalPoints += StickerBook_CountTotalPointsForSet(pStickerBookItemSet, pEnt, &bFullCount);
	}
	FOR_EACH_END;

	if(bFullCount)
		UPDATE_CACHED_TOTAL_POINTS(pStickerBookPointCacheByCollection->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pStickerBookPointCacheByCollection->pointCacheData.iTotalPoints;
}

U32 StickerBook_CountTotalPointsForSet(SA_PARAM_NN_VALID StickerBookItemSet *pStickerBookItemSet, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	StickerBookPointCacheByCollection *pStickerBookPointCacheByCollection;
	StickerBookPointCacheBySet *pStickerBookPointCacheBySet;
	bool bRequirementsMet = false;
	NOCONST(Entity) *pEntNoConst;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	pEntNoConst = CONTAINER_NOCONST(Entity, pEnt);

	EnsureStickerBookPointCache(pEnt);

	pStickerBookPointCacheByCollection = EnsureStickerBookPointCacheByCollection(pEnt, pStickerBookItemSet->pStickerBookCollection);

	pStickerBookPointCacheBySet = EnsureStickerBookPointCacheBySet(pStickerBookPointCacheByCollection, pStickerBookItemSet);

	RETURN_CACHED_TOTAL_POINTS_OR_INIT(pStickerBookPointCacheBySet->pointCacheData);

	FOR_EACH_IN_EARRAY(pStickerBookItemSet->ppItems, StickerBookItem, pStickerBookItem)
	{
		ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);
		if(pItemDef)
		{
			if(!pItemDef->pRestriction ||
				(itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)
				&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)))
			{
				bRequirementsMet = true;
				pStickerBookPointCacheBySet->pointCacheData.iTotalPoints += pStickerBookItem->iPoints;
			}
		}
		else
			bFullCount = false;
	}
	FOR_EACH_END;
	if(bRequirementsMet)
		pStickerBookPointCacheBySet->pointCacheData.iTotalPoints += pStickerBookItemSet->iPoints;

	if(bFullCount)
		UPDATE_CACHED_TOTAL_POINTS(pStickerBookPointCacheBySet->pointCacheData);

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return pStickerBookPointCacheBySet->pointCacheData.iTotalPoints;
}

U32 StickerBook_CountTotalPointsForLocation(SA_PARAM_NN_VALID StickerBookItemLocation *pStickerBookItemLocation, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID bool *pbFullCount)
{
	U32 iPoints = 0;
	bool bRequirementsMet = false;
	NOCONST(Entity) *pEntNoConst;
	bool bFullCount = true;

	if(!SAFE_MEMBER(pEnt, pPlayer))
		return 0;

	pEntNoConst = CONTAINER_NOCONST(Entity, pEnt);

	assertmsgf(0, "Unsupported StickerBook function: %s", __FUNCTION__);

	FOR_EACH_IN_EARRAY(pStickerBookItemLocation->ppItems, StickerBookItem, pStickerBookItem)
	{
		ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pStickerBookItem->pchItemName);
		if(pItemDef)
		{
			if(!pItemDef->pRestriction ||
				(itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)
				&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, pEntNoConst, NULL)))
			{
				bRequirementsMet = true;
				iPoints += pStickerBookItem->iPoints;
			}
		}
		else
			bFullCount = false;
	}
	FOR_EACH_END;
	if(bRequirementsMet)
		iPoints += pStickerBookItemLocation->iPoints;

	// If caller wants to know if we have a FullCount (due to Item resources all being downloaded) we only set it if it is still true.
	// That way, if any count sets it to false, false is preserved. As a result, the root caller will end up with FullCount false if any Item resource has still not been received.
	if(pbFullCount && *pbFullCount)
		*pbFullCount = bFullCount;

	return iPoints;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Eaastrrecentlyacquiredstickerbookitems");
void StickerBook_trh_MaybeRecentlyAcquiredItem(ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef)
{
	const char *astrItemName;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer) || !pItemDef || GLOBALTYPE_ENTITYPLAYER != pEnt->myEntityType)
		return;

	astrItemName = StickerBook_DoesItemParticipate(pItemDef->pchName);
	if(astrItemName)
		eaPushUnique(&pEnt->pPlayer->eaAstrRecentlyAcquiredStickerBookItems, (char *)astrItemName);
}

#include "AutoGen/StickerBookCommon_h_ast.c"
