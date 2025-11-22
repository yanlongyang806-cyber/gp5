/***************************************************************************



***************************************************************************/

#include "MicrotransBanner.h"
//#include "error.h"
#include "Entity.h"
#include "StringCache.h"
#include "ResourceManager.h"
#include "FolderCache.h"
#include "file.h"
#include "fileutil.h"
#include "referencesystem.h"

#include "AutoGen/MicrotransBanner_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// Dictionary holding the game progression nodes
DictionaryHandle g_hMicrotransBannerSetDictionary = NULL;

void MicrotransBannerSet_Fixup(MicrotransBannerSet* pMicrotransBannerSet)
{
	int i;

	for(i=0;i<eaSize(&pMicrotransBannerSet->ppBannerBlocks);i++)
	{
		MicrotransBannerBlock *pMicrotransBannerBlock = pMicrotransBannerSet->ppBannerBlocks[i];

		if (pMicrotransBannerBlock->pchStartTime)
		{
			pMicrotransBannerBlock->uStartingTime=timeGetSecondsSince2000FromGenericString(pMicrotransBannerBlock->pchStartTime);
		}
	}
}

static int microtransBanner_MicrotransBannerSetValidateCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, MicrotransBannerSet *pMicrotransBannerSet, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			MicrotransBannerSet_Fixup(pMicrotransBannerSet);

			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}


static void microtransBanner_ReloadMicrotransBanner(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Microtrans Banner Data...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hMicrotransBannerSetDictionary);

	loadend_printf(" done (%d Microtrans Banner Data)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hMicrotransBannerSetDictionary));
}

AUTO_RUN;
void RegisterMicrotransBannerSetDictionaries(void)
{
	g_hMicrotransBannerSetDictionary = RefSystem_RegisterSelfDefiningDictionary("MicrotransBannerSet", false, parse_MicrotransBannerSet, true, true, NULL);

	resDictManageValidation(g_hMicrotransBannerSetDictionary, microtransBanner_MicrotransBannerSetValidateCB);

	// This is Server only at the moment
	
	resDictProvideMissingResources(g_hMicrotransBannerSetDictionary);

	if (isDevelopmentMode() || isProductionEditMode()) 
	{
		resDictMaintainInfoIndex(g_hMicrotransBannerSetDictionary, ".Name", NULL, NULL, NULL, NULL);
	}
}

// Loads the microtrans Banner 
void microtransBanner_LoadMicrotransBanner(void)
{
	// Load game progression nodes, do not load into shared memory
	resLoadResourcesFromDisk(g_hMicrotransBannerSetDictionary, 
		MICROTRANS_BANNER_BASE_DIR, 
		".mtbanner", 
		"MicrotransBanner.bin", 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, MICROTRANS_BANNER_BASE_DIR "/*.mtbanner", microtransBanner_ReloadMicrotransBanner);
	}
}

// Gets the MicrotransBannerSet from the dictionary
MicrotransBannerSet * microtransBanner_MicrotransBannerSetFromName(const char *pchName)
{
	return RefSystem_ReferentFromString(g_hMicrotransBannerSetDictionary, pchName);
}

#include "AutoGen/MicrotransBanner_h_ast.c"






