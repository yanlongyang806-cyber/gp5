#include "GameLogPerCharacterNumericCSV.h"
#include "StringCache.h"
#include "NameValuePair.h"
#include "ExcelXMLFormatter.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "GameLogReporter"););

StashTable stRecordsByDebugName;
StashTable stPlayerClassCodeMap;
StashTable stItemFilter;

extern g_pchEmailRecipients[];

StashTable stAllItemDefsEncountered;

void PerCharacterNumericCSV_Init(void)
{
	stPlayerClassCodeMap = stashTableCreateWithStringKeys(7, StashDeepCopyKeys_NeverRelease);
	stAllItemDefsEncountered = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);
	stItemFilter = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);

	stashAddPointer(stPlayerClassCodeMap, "DC",	"D. Cleric", false);
	stashAddPointer(stPlayerClassCodeMap, "TR",	"T. Rogue", false);
	stashAddPointer(stPlayerClassCodeMap, "CW",	"C. Wizard", false);
	stashAddPointer(stPlayerClassCodeMap, "GF",	"G. Fighter", false);
	stashAddPointer(stPlayerClassCodeMap, "GW",	"G. W. Fighter", false);
	stashAddPointer(stPlayerClassCodeMap, "SW",	"S. Warlock", false);
	stashAddPointer(stPlayerClassCodeMap, "AR",	"A. Ranger", false);
	stashAddPointer(stItemFilter, "Potion_Healing_Pvp_5",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_5_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_2",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll",	NULL, false);
	stashAddPointer(stItemFilter, "Fuse_Ward_Coalescent_Mt",	NULL, false);
	stashAddPointer(stItemFilter, "Reward_Token_Seal_Tier_3",	NULL, false);
	stashAddPointer(stItemFilter, "Injury_Kit_2_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Fuse_Ward_Coalescent",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll_Bound_2",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll_Mt",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_4",	NULL, false);
	stashAddPointer(stItemFilter, "Item_Portable_Altar_Mt",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll_2",	NULL, false);
	stashAddPointer(stItemFilter, "Injury_Kit_3_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_5",	NULL, false);
	stashAddPointer(stItemFilter, "Item_Portable_Altar",	NULL, false);
	stashAddPointer(stItemFilter, "Reward_Token_Seal_Tier_2",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_Pvp_1",	NULL, false);
	stashAddPointer(stItemFilter, "Fuse_Ward_Preservation",	NULL, false);
	stashAddPointer(stItemFilter, "Reward_Token_Seal_Tier_4",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_2_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "T10_Enchantment_Special_Lockbox_Nightmare",	NULL, false);
	stashAddPointer(stItemFilter, "Reward_Token_Seal_Tier_5",	NULL, false);
	stashAddPointer(stItemFilter, "Injury_Kit_2",	NULL, false);
	stashAddPointer(stItemFilter, "Injury_Kit_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_4_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Reward_Token_Seal_Tier_6",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll_Bound_3",	NULL, false);
	stashAddPointer(stItemFilter, "Scroll_Teleport_Pe",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_Pvp_4",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_3",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_0",	NULL, false);
	stashAddPointer(stItemFilter, "Injury_Kit_3",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_Pvp_2",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_3_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Fuse_Ward_Preservation_Mt",	NULL, false);
	stashAddPointer(stItemFilter, "T9_Enchantment_Special_Lockbox_Nightmare",	NULL, false);
	stashAddPointer(stItemFilter, "Lockbox_Tradebar",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing",	NULL, false);
	stashAddPointer(stItemFilter, "Scroll_Teleport_Pe_Bound",	NULL, false);
	stashAddPointer(stItemFilter, "Consumable_Id_Scroll_3",	NULL, false);
	stashAddPointer(stItemFilter, "Injury_Kit",	NULL, false);
	stashAddPointer(stItemFilter, "Crafting_Resource_Dragon_Egg",	NULL, false);
	stashAddPointer(stItemFilter, "Potion_Healing_Pvp_3",	NULL, false);

}

void PerCharacterNumericCSV_AnalyzeParsedLog(ParsedLog* pLog)
{
	CharacterNumericData* pCharData = NULL;
	LevelNumericData* pLevelData = NULL;
	NumericGainLossData* pGainLossData = NULL;
	const char* pchItemDefName = NULL;
	const char* pchPlayerClassCode = NULL;
	char* pchReasonDisplay = NULL;
	int iDelta = 0;
	int idx;
	int iLevel = 0;
	char* estrDebugName = NULL;

	if (!pLog || !pLog->pObjInfo)
		return;

	for (idx = 0; idx < eaSize(&pLog->ppPairs); idx++)
	{
		if (stricmp(pLog->ppPairs[idx]->pName, "Numeric") == 0)
		{
			pchItemDefName = allocAddString(pLog->ppPairs[idx]->pValue);
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Added") == 0)
		{
			iDelta = atoi(pLog->ppPairs[idx]->pValue);
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Item") == 0)
		{
			pchItemDefName = allocAddString(pLog->ppPairs[idx]->pValue);
			if (stItemFilter && !stashFindPointer(stItemFilter, pchItemDefName, NULL))
				return;
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Count") == 0)
		{
			if (stricmp(pLog->pObjInfo->pAction, "Itemadded") == 0)
				iDelta = atoi(pLog->ppPairs[idx]->pValue);
			else if (stricmp(pLog->pObjInfo->pAction, "Itemremoved") == 0)
				iDelta = -atoi(pLog->ppPairs[idx]->pValue);
		}
	}

	for (idx = 0; idx < eaSize(&pLog->pObjInfo->ppProjSpecific); idx++)
	{
		if (stricmp(pLog->pObjInfo->ppProjSpecific[idx]->pKey, "CL") == 0)
		{
			pchPlayerClassCode = pLog->pObjInfo->ppProjSpecific[idx]->pVal;
		}
		else if (stricmp(pLog->pObjInfo->ppProjSpecific[idx]->pKey, "LEV") == 0)
		{
			iLevel = atoi(pLog->pObjInfo->ppProjSpecific[idx]->pVal);
		}
	}

	if (!pchItemDefName || iDelta == 0)
		return;

	stashAddPointer(stAllItemDefsEncountered, pchItemDefName, pchItemDefName, false);

	estrStackCreate(&estrDebugName);
	estrPrintf(&estrDebugName, "P[%d@%d %s@%s]", pLog->pObjInfo->iObjID, pLog->pObjInfo->iownerID, pLog->pObjInfo->pObjName, pLog->pObjInfo->pOwnerName);

	if (!stRecordsByDebugName)
		stRecordsByDebugName = stashTableCreateWithStringKeys(50, StashDeepCopyKeys_NeverRelease);
	
	stashFindPointer(stRecordsByDebugName, estrDebugName, &pCharData);

	if (!pCharData)
	{
		pCharData = StructCreate(parse_CharacterNumericData);
		pCharData->accountID = pLog->pObjInfo->iownerID;
		pCharData->containerID = pLog->pObjInfo->iObjID;
		pCharData->pchClassname = pchPlayerClassCode;
		stashAddPointer(stRecordsByDebugName, estrDebugName, pCharData, false);
	}

	idx = eaIndexedFindUsingInt(&pCharData->eaLevels, iLevel);
	if (idx >= 0)
		pLevelData = pCharData->eaLevels[idx];
	else
	{
		pLevelData = StructCreate(parse_LevelNumericData);
		pLevelData->iLevel = iLevel;
		pLevelData->uiLevelStart = pLog->iTime;

		if (!pLevelData->stGainLossByItemName)
			pLevelData->stGainLossByItemName = stashTableCreateWithStringKeys(50, StashDeepCopyKeys_NeverRelease);

		if (!pCharData->eaLevels)
			eaIndexedEnable(&pCharData->eaLevels, parse_LevelNumericData);
		eaIndexedPushUsingIntIfPossible(&pCharData->eaLevels, pLevelData->iLevel, pLevelData);
	}

	estrDestroy(&estrDebugName);

	stashFindPointer(pLevelData->stGainLossByItemName, pchItemDefName, &pGainLossData);
	if (!pGainLossData)
	{
		pGainLossData = StructCreate(parse_NumericGainLossData);
		stashAddPointer(pLevelData->stGainLossByItemName, pchItemDefName, pGainLossData, false);
	}

	MAX1(pLevelData->uiLevelEnd, pLog->iTime);

	if (pchPlayerClassCode)
	{
		char* pchCode;
		stashFindPointer(stPlayerClassCodeMap, pchPlayerClassCode, &pchCode);
		pCharData->pchClassname = pchCode;
	}

	//record gain/loss
	if (iDelta > 0)
	{
		pGainLossData->uGain += iDelta;
	}
	else if (iDelta < 0)
	{
		pGainLossData->uLoss += -iDelta;
	}
}

void PerCharacterNumericCSV_GenerateReport()
{
	StashTableIterator charIter;
	StashElement charElem;
	StashTableIterator itemIter;
	StashElement itemElem;
	char* estrOutLevels1_5 = NULL;
	char* estrOutLevels6_15 = NULL;
	char* estrOutLevels16_35 = NULL;
	char* estrOutLevels36_59 = NULL;
	char* estrOutLevel60 = NULL;

	estrPrintf(&estrOutLevels1_5, "Entity ID,Level,Account ID,Debug Name,Class,Level Start,Level End,Time Played At Level");
	
	stashGetIterator(stAllItemDefsEncountered, &itemIter);
	while(stashGetNextElement(&itemIter, &itemElem))
	{
		estrConcatf(&estrOutLevels1_5, ",%s gained,%s lost", stashElementGetStringKey(itemElem), stashElementGetStringKey(itemElem));
	}
	estrConcatf(&estrOutLevels1_5, "\n");

	estrCopy(&estrOutLevels6_15, &estrOutLevels1_5);
	estrCopy(&estrOutLevels16_35, &estrOutLevels1_5);
	estrCopy(&estrOutLevels16_35, &estrOutLevels1_5);
	estrCopy(&estrOutLevels36_59, &estrOutLevels1_5);
	estrCopy(&estrOutLevel60, &estrOutLevels1_5);

	//one line per player per level
	stashGetIterator(stRecordsByDebugName, &charIter);
	while(stashGetNextElement(&charIter, &charElem))
	{
		CharacterNumericData *pRecords = stashElementGetPointer(charElem);
		const char* pchDebugName = stashElementGetStringKey(charElem);
		int iLevel;
		for(iLevel = 0; iLevel < eaSize(&pRecords->eaLevels); iLevel++)
		{
			NumericGainLossData* pGainLoss = NULL;
			char** pestrOut;

			if (pRecords->eaLevels[iLevel]->iLevel < 6)
				pestrOut = &estrOutLevels1_5;
			else if (pRecords->eaLevels[iLevel]->iLevel < 16)
				pestrOut = &estrOutLevels6_15;
			else if (pRecords->eaLevels[iLevel]->iLevel < 36)
				pestrOut = &estrOutLevels16_35;
			else if (pRecords->eaLevels[iLevel]->iLevel < 60)
				pestrOut = &estrOutLevels36_59;
			else
				pestrOut = &estrOutLevel60;

			estrConcatf(pestrOut, "%u,%d,%u,%s,%s,%u,%u,%u", pRecords->containerID, pRecords->eaLevels[iLevel]->iLevel, pRecords->accountID, pchDebugName, pRecords->pchClassname, pRecords->eaLevels[iLevel]->uiLevelStart, pRecords->eaLevels[iLevel]->uiLevelEnd, pRecords->eaLevels[iLevel]->uiLevelEnd - pRecords->eaLevels[iLevel]->uiLevelStart);

			stashGetIterator(stAllItemDefsEncountered, &itemIter);
			while(stashGetNextElement(&itemIter, &itemElem))
			{
				stashFindPointer(pRecords->eaLevels[iLevel]->stGainLossByItemName, stashElementGetStringKey(itemElem), &pGainLoss);
				if (pGainLoss)
					estrConcatf(pestrOut, ",%llu,%llu", pGainLoss->uGain, pGainLoss->uLoss);
				else
					estrConcatf(pestrOut, ",0,0");
			}
			estrConcatf(pestrOut, "\n");
		}
	}

	WriteOutputFile("TrackedItems_Levels1_5.csv", estrOutLevels1_5, true);
	WriteOutputFile("TrackedItems_Levels6_15.csv", estrOutLevels6_15, true);
	WriteOutputFile("TrackedItems_Levels16_35.csv", estrOutLevels16_35, true);
	WriteOutputFile("TrackedItems_Levels36_59.csv", estrOutLevels36_59, true);
	WriteOutputFile("TrackedItems_Level60.csv", estrOutLevel60, true);

}

void PerCharacterNumericCSV_SendResultEmail()
{
	SendDownloadLinkEmail("TrackedItem");
}

#include "GameLogPerCharacterNumericCSV_h_ast.c"