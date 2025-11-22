#include "GameLogEconomyReport.h"
#include "StringCache.h"
#include "NameValuePair.h"
#include "ExcelXMLFormatter.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "GameLogReporter"););

ItemDailySummaries g_DailySummaries;

static ItemDailySummary newSummary = {0};

StashTable stRecordsByItemName;

StashTable stReasonDisplayNameMapping;
StashTable stPlayerClassCodeMap;

extern U32 g_uTimeStartSS2000;
extern U32 g_uTimeEndSS2000;

extern char g_pchWorkingDir[];
static char* s_estrSummaryFilename = NULL;

void EconomyReport_Init(void)
{
	estrPrintf(&s_estrSummaryFilename, "%s/%s", g_pchWorkingDir, "DailyEconomy.summaries");
	//Load "Past summaries" file.
	ParserLoadFiles(NULL, 
		s_estrSummaryFilename, 
		NULL ,
		PARSER_OPTIONALFLAG, 
		parse_ItemDailySummaries,
		&g_DailySummaries);

	//Init reason map
	stReasonDisplayNameMapping = stashTableCreateWithStringKeys(50, StashDeepCopyKeys_NeverRelease);

	stashAddPointer(stReasonDisplayNameMapping, "Powers:Grantrewardmod",		"Powers", false);
	stashAddPointer(stReasonDisplayNameMapping, "Powers:Respec",				"Respec", false);
	stashAddPointer(stReasonDisplayNameMapping, "Itemassign:Complete",			"Item Assignments", false);
	stashAddPointer(stReasonDisplayNameMapping, "Itemassign:Collectrewards",	"Item Assignments", false);
	stashAddPointer(stReasonDisplayNameMapping, "Numericconversion",			"Currency Refinement", false);
	stashAddPointer(stReasonDisplayNameMapping, "Numericconversionauto",		"Currency Refinement", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Purchase:Buyer",		"Auction House Buyout", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction-buy",					"Auction House", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Createlot",			"Auction Posting Fee", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Bid",					"Auction House Bid", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Purchase:Seller",		"Auction House Buyout", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Expire:Buyer",			"Auction House Winning Bid", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Expire:Seller",		"Auction House Winning Bid", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Expire:Seller:Nobuyer","Auction Expiration", false);
	stashAddPointer(stReasonDisplayNameMapping, "Auction:Purchase:Outbid",		"Auction House Outbid", false);
	stashAddPointer(stReasonDisplayNameMapping, "Mission:Changestatesubmission", "Missions", false);
	stashAddPointer(stReasonDisplayNameMapping, "Mission:Turnin",				"Missions", false);
	stashAddPointer(stReasonDisplayNameMapping, "Mission:Changestate",			"Missions", false);
	stashAddPointer(stReasonDisplayNameMapping, "Mission:Openmissionreward",	"Open missions", false);
	stashAddPointer(stReasonDisplayNameMapping, "Mission:Turninperk",			"Perks", false);
	stashAddPointer(stReasonDisplayNameMapping, "Supercritterpet Rename",		"SCP Rename", false);
	stashAddPointer(stReasonDisplayNameMapping, "Supercritterpet Rush Training", "SCP Rush Training", false);
	stashAddPointer(stReasonDisplayNameMapping, "Store:Buyitem",				"Stores", false);
	stashAddPointer(stReasonDisplayNameMapping, "Store:Sellitem",				"Stores", false);
	stashAddPointer(stReasonDisplayNameMapping, "Store:Buybackitem",			"Stores", false);
	stashAddPointer(stReasonDisplayNameMapping, "Loot:Killcritter",				"Critters", false);
	stashAddPointer(stReasonDisplayNameMapping, "Loot:Rollover",				"Rollover Loot", false);
	stashAddPointer(stReasonDisplayNameMapping, "Loot:Interacttakeall",			"Loot Interacts", false);
	stashAddPointer(stReasonDisplayNameMapping, "Loot:Takeitemviainteract",		"Loot Interacts", false);
	stashAddPointer(stReasonDisplayNameMapping, "Expression:Grantrewardtoentarray",	"FSMs", false);
	stashAddPointer(stReasonDisplayNameMapping, "Pvp:Reward",					"PvP rewards", false);
	stashAddPointer(stReasonDisplayNameMapping, "Trade:Complete",				"Player trades", false);
	stashAddPointer(stReasonDisplayNameMapping, "Item:Movenumerictoguild",		"Guild bank", false);
	stashAddPointer(stReasonDisplayNameMapping, "Guild:Changeallegiance",		"Guild Allegiance Change", false);
	stashAddPointer(stReasonDisplayNameMapping, "Guild:Playerbuyguildbanktab",	"Guild bank tab purchase", false);
	stashAddPointer(stReasonDisplayNameMapping, "Item:Ungem",					"UnGem", false);
	stashAddPointer(stReasonDisplayNameMapping, "Item:Openrewardpack",			"Rewardpacks", false);
	stashAddPointer(stReasonDisplayNameMapping, "Item:Operewardpack",			"Rewardpacks", false);
	stashAddPointer(stReasonDisplayNameMapping, "Ugctipsgivetip",				"UGC Tips", false);
	stashAddPointer(stReasonDisplayNameMapping, "Ugctipswithdraw",				"UGC Tips", false);
	stashAddPointer(stReasonDisplayNameMapping, "Item Transmutation",			"Item Transmute", false);
	stashAddPointer(stReasonDisplayNameMapping, "Promogamecurrency:Claim",		"Promo currency claim", false);
	stashAddPointer(stReasonDisplayNameMapping, "CurrencyExchange:ClaimTC",		"Currency Exchange Claim", false);
	stashAddPointer(stReasonDisplayNameMapping, "CurrencyExchange:EscrowTC",	"Currency Exchange Escrow", false);
	stashAddPointer(stReasonDisplayNameMapping, "Mail:TakeitemsNPC",			"NPC Mail", false);
	stashAddPointer(stReasonDisplayNameMapping, "Internal:Setexplevelusingcharacterpath", "!!! DEV COMMANDS !!!", false);

	stPlayerClassCodeMap = stashTableCreateWithStringKeys(7, StashDeepCopyKeys_NeverRelease);

	stashAddPointer(stPlayerClassCodeMap, "DC",	"D. Cleric", false);
	stashAddPointer(stPlayerClassCodeMap, "TR",	"T. Rogue", false);
	stashAddPointer(stPlayerClassCodeMap, "CW",	"C. Wizard", false);
	stashAddPointer(stPlayerClassCodeMap, "GF",	"G. Fighter", false);
	stashAddPointer(stPlayerClassCodeMap, "GW",	"G. W. Fighter", false);
	stashAddPointer(stPlayerClassCodeMap, "SW",	"S. Warlock", false);
	stashAddPointer(stPlayerClassCodeMap, "AR",	"A. Ranger", false);
}

void EconomyReport_AnalyzeParsedLog(ParsedLog* pLog)
{
	ItemDailyTransactionData* pTransactionData = NULL;
	ItemGainRecords* pItemData = NULL;
	PlayerItemGainLoss* pPlayerData = NULL;
	ReasonItemGainLoss* pReasonData = NULL;
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
			pchItemDefName = pLog->ppPairs[idx]->pValue;
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Added") == 0)
		{
			iDelta = atoi(pLog->ppPairs[idx]->pValue);
		}
		else if (stricmp(pLog->ppPairs[idx]->pName, "Reason") == 0)
		{
			stashFindPointer(stReasonDisplayNameMapping, pLog->ppPairs[idx]->pValue, &pchReasonDisplay);
			if (!pchReasonDisplay)
			{
				//unexpected reason
				printf("Unexpected ItemChangeReason: %s\n", pLog->ppPairs[idx]->pValue);
				pchReasonDisplay = pLog->ppPairs[idx]->pValue;
			}
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

	//daily summary object
	idx = eaIndexedFindUsingString(&newSummary.eaItemTransactions, pchItemDefName);

	if (idx >= 0)
		pTransactionData = newSummary.eaItemTransactions[idx];
	else
	{
		pTransactionData = StructCreate(parse_ItemDailyTransactionData);
		pTransactionData->pchItemDefName = allocAddString(pchItemDefName);
		if (!newSummary.eaItemTransactions)
			eaIndexedEnable(&newSummary.eaItemTransactions, parse_ItemDailyTransactionData);
		eaIndexedPushUsingStringIfPossible(&newSummary.eaItemTransactions, pchItemDefName, pTransactionData);
	}

	if (!stRecordsByItemName)
		stRecordsByItemName = stashTableCreateWithStringKeys(50, StashDeepCopyKeys_NeverRelease);
	stashFindPointer(stRecordsByItemName, pchItemDefName, &pItemData);
	
	if (!pItemData)
	{
		pItemData = StructCreate(parse_ItemGainRecords);
//		pItemData->pchItemDefName = allocAddString(pchItemDefName);
		stashAddPointer(stRecordsByItemName, pchItemDefName, pItemData, false);
	}

	estrStackCreate(&estrDebugName);
	estrPrintf(&estrDebugName, "P[%d@%d %s@%s]", pLog->pObjInfo->iObjID, pLog->pObjInfo->iownerID, pLog->pObjInfo->pObjName, pLog->pObjInfo->pOwnerName);

	idx = eaIndexedFindUsingString(&pItemData->eaPlayerGainLoss, estrDebugName);
	if (idx >= 0)
		pPlayerData = pItemData->eaPlayerGainLoss[idx];
	else
	{
		pPlayerData = StructCreate(parse_PlayerItemGainLoss);
		pPlayerData->pchPlayerDebugName = allocAddString(estrDebugName);
		pPlayerData->iMinLevel = 999;
		if (!pItemData->eaPlayerGainLoss)
			eaIndexedEnable(&pItemData->eaPlayerGainLoss, parse_PlayerItemGainLoss);
		eaIndexedPushUsingStringIfPossible(&pItemData->eaPlayerGainLoss, pPlayerData->pchPlayerDebugName, pPlayerData);

	}

	estrDestroy(&estrDebugName);

	idx = eaIndexedFindUsingString(&pItemData->eaReasonGainLoss, pchReasonDisplay);
	if (idx >= 0)
		pReasonData = pItemData->eaReasonGainLoss[idx];
	else
	{
		pReasonData = StructCreate(parse_ReasonItemGainLoss);
		pReasonData->pchReasonDisplayName = allocAddString(pchReasonDisplay);
		if (!pItemData->eaReasonGainLoss)
			eaIndexedEnable(&pItemData->eaReasonGainLoss, parse_ReasonItemGainLoss);
		eaIndexedPushUsingStringIfPossible(&pItemData->eaReasonGainLoss, pchReasonDisplay, pReasonData);

	}

	if (iLevel > 0)
	{
		MIN1(pPlayerData->iMinLevel, iLevel);
		MAX1(pPlayerData->iMaxLevel, iLevel);
	}

	if (pchPlayerClassCode)
	{
		stashFindPointer(stPlayerClassCodeMap, pchPlayerClassCode, &pPlayerData->pchClassName);
	}

	//record gain/loss
	if (iDelta > 0)
	{
		pTransactionData->uAmountEarned += iDelta;

		if (pPlayerData->uGain == 0)
			pTransactionData->uNumEarners++;

		pPlayerData->uGain += iDelta;

		pReasonData->iGainCount++;
		pReasonData->uGain += iDelta;
	}
	else if (iDelta < 0)
	{
		pTransactionData->uAmountSpent += -iDelta;

		if (pPlayerData->uLoss == 0)
			pTransactionData->uNumSpenders++;

		pPlayerData->uLoss += -iDelta;

		pReasonData->iLossCount++;
		pReasonData->uLoss += -iDelta;
	}
}

static int EconomyReport_SortSummariesByDate(const ItemDailySummary** pSummaryA, const ItemDailySummary** pSummaryB)
{
	return (S64)(*pSummaryA)->uDaysSince2000 - (S64)(*pSummaryB)->uDaysSince2000;
}

static int EconomyReport_SortPlayersByGain(const PlayerItemGainLoss** pPlayerA, const PlayerItemGainLoss** pPlayerB)
{
	if ((*pPlayerA)->uGain > (*pPlayerB)->uGain)
		return -1;
	else if ((*pPlayerA)->uGain < (*pPlayerB)->uGain)
		return 1;
	else
		return 0;
}

static int EconomyReport_SortPlayersByLoss(const PlayerItemGainLoss** pPlayerA, const PlayerItemGainLoss** pPlayerB)
{
	if ((*pPlayerA)->uLoss > (*pPlayerB)->uLoss)
		return -1;
	else if ((*pPlayerA)->uLoss < (*pPlayerB)->uLoss)
		return 1;
	else
		return 0;
}

static int EconomyReport_SortReasonsByGain(const ReasonItemGainLoss** pReasonA, const ReasonItemGainLoss** pReasonB)
{
	if ((*pReasonA)->uGain > (*pReasonB)->uGain)
		return -1;
	else if ((*pReasonA)->uGain < (*pReasonB)->uGain)
		return 1;
	else
		return 0;
}

static int EconomyReport_SortReasonsByLoss(const ReasonItemGainLoss** pReasonA, const ReasonItemGainLoss** pReasonB)
{
	if ((*pReasonA)->uLoss > (*pReasonB)->uLoss)
		return -1;
	else if ((*pReasonA)->uLoss < (*pReasonB)->uLoss)
		return 1;
	else
		return 0;
}

void EconomyReport_AddSummariesTable(ExcelXMLWorksheet* pSheet, const char* pchItemDefName, int* pRow)
{
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int i, iTrans;

	ExcelXML_WorksheetSetRowHeight(pSheet, *pRow, 40);

	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 7);

	eaTableTmp[0]->pchLabel = "Date";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[1]->pchLabel = "Earned";
	eaTableTmp[1]->eType = kColumnType_U64;
	eaTableTmp[2]->pchLabel = "Number of Earners";
	eaTableTmp[2]->eType = kColumnType_Int;
	eaTableTmp[3]->pchLabel = "Avg. Earned Per Player";
	eaTableTmp[3]->eType = kColumnType_Formula;
	eaTableTmp[4]->pchLabel = "Spent";
	eaTableTmp[4]->eType = kColumnType_U64;
	eaTableTmp[5]->pchLabel = "Number of Spenders";
	eaTableTmp[5]->eType = kColumnType_Int;
	eaTableTmp[6]->pchLabel = "Avg. Spent Per Player";
	eaTableTmp[6]->eType = kColumnType_Formula;

	for (i = 0; i < eaSize(&g_DailySummaries.eaSummaries); i++)
	{
		iTrans = eaIndexedFindUsingString(&g_DailySummaries.eaSummaries[i]->eaItemTransactions, pchItemDefName);

		if (iTrans == -1)
			continue;

		ExcelXML_TableColumnAddString(eaTableTmp[0], timeGetDateNoTimeStringFromSecondsSince2000(g_DailySummaries.eaSummaries[i]->uDaysSince2000*60*60*24));
		ExcelXML_TableColumnAddU64(eaTableTmp[1], g_DailySummaries.eaSummaries[i]->eaItemTransactions[iTrans]->uAmountEarned);
		ExcelXML_TableColumnAddInt(eaTableTmp[2], g_DailySummaries.eaSummaries[i]->eaItemTransactions[iTrans]->uNumEarners);
		ExcelXML_TableColumnAddString(eaTableTmp[3], "=R[0]C[-2]/R[0]C[-1]");
		ExcelXML_TableColumnAddU64(eaTableTmp[4], g_DailySummaries.eaSummaries[i]->eaItemTransactions[iTrans]->uAmountSpent);
		ExcelXML_TableColumnAddInt(eaTableTmp[5], g_DailySummaries.eaSummaries[i]->eaItemTransactions[iTrans]->uNumSpenders);
		ExcelXML_TableColumnAddString(eaTableTmp[6], "=R[0]C[-2]/R[0]C[-1]");
	}

	ExcelXML_WorksheetAddTable(pSheet, NULL, pRow, &eaTableTmp);


	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
}

void EconomyReport_AddPlayerTables(ItemGainRecords* pRecords, ExcelXMLWorksheet* pSheet, int* pRow)
{
	int iMaxRow = *pRow;
	int iCol = 1;
	PlayerItemGainLoss** eaSortablePlayerGains = NULL;
	PlayerItemGainLoss** eaSortablePlayerLosses = NULL;
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int iRowIndices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 99, 999};
	int i;

	//Sort gains and losses by magnitudes.
	eaCopy(&eaSortablePlayerGains, &pRecords->eaPlayerGainLoss);
	eaCopy(&eaSortablePlayerLosses, &pRecords->eaPlayerGainLoss);
	eaQSort(eaSortablePlayerGains, EconomyReport_SortPlayersByGain);
	eaQSort(eaSortablePlayerLosses, EconomyReport_SortPlayersByLoss);

	//Write "Top Earners" table.
	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 4);

	eaTableTmp[0]->pchLabel = "Top Earners";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[0]->iMergedColumns = 3;
	eaTableTmp[1]->pchLabel = "Level range";
	eaTableTmp[1]->eType = kColumnType_String;
	eaTableTmp[2]->pchLabel = "Class";
	eaTableTmp[2]->eType = kColumnType_String;
	eaTableTmp[3]->pchLabel = "Earned";
	eaTableTmp[3]->eType = kColumnType_U64;

	//add top 10, plus 100th and 1000th places to table.
	for (i = 0; i < 12; i++)
	{
		PlayerItemGainLoss* pGain;
		char* estrPlayerName = NULL;
		char* estrLevelRange = NULL;

		pGain = eaGet(&eaSortablePlayerGains, iRowIndices[i]);

		if (!pGain || pGain->uGain == 0)
			break;

		estrStackCreate(&estrPlayerName);
		estrStackCreate(&estrLevelRange);

		estrPrintf(&estrPlayerName, "%d) %s", iRowIndices[i]+1, pGain->pchPlayerDebugName);
		estrPrintf(&estrLevelRange, "%d-%d", pGain->iMinLevel, pGain->iMaxLevel);

		ExcelXML_TableColumnAddString(eaTableTmp[0], estrPlayerName);
		ExcelXML_TableColumnAddString(eaTableTmp[1], estrLevelRange);
		ExcelXML_TableColumnAddString(eaTableTmp[2], pGain->pchClassName);
		ExcelXML_TableColumnAddU64(eaTableTmp[3], pGain->uGain);

		estrDestroy(&estrLevelRange);
		estrDestroy(&estrPlayerName);
	}

	ExcelXML_WorksheetAddTable(pSheet, &iCol, &iMaxRow, &eaTableTmp);

	iCol++;

	//Write "Top Spenders" table.
	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 4);

	eaTableTmp[0]->pchLabel = "Top Spenders";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[0]->iMergedColumns = 3;
	eaTableTmp[1]->pchLabel = "Level range";
	eaTableTmp[1]->eType = kColumnType_String;
	eaTableTmp[2]->pchLabel = "Class";
	eaTableTmp[2]->eType = kColumnType_String;
	eaTableTmp[3]->pchLabel = "Spent";
	eaTableTmp[3]->eType = kColumnType_U64;

	//add top 10, plus 100th and 1000th places to table.
	for (i = 0; i < 12; i++)
	{
		PlayerItemGainLoss* pLoss;
		char* estrPlayerName = NULL;
		char* estrLevelRange = NULL;

		pLoss = eaGet(&eaSortablePlayerLosses, iRowIndices[i]);

		if (!pLoss || pLoss->uLoss == 0)
			break;

		estrStackCreate(&estrPlayerName);
		estrStackCreate(&estrLevelRange);

		estrPrintf(&estrPlayerName, "%d) %s", iRowIndices[i]+1, pLoss->pchPlayerDebugName);
		estrPrintf(&estrLevelRange, "%d-%d", pLoss->iMinLevel, pLoss->iMaxLevel);

		ExcelXML_TableColumnAddString(eaTableTmp[0], estrPlayerName);
		ExcelXML_TableColumnAddString(eaTableTmp[1], estrLevelRange);
		ExcelXML_TableColumnAddString(eaTableTmp[2], pLoss->pchClassName);
		ExcelXML_TableColumnAddU64(eaTableTmp[3], pLoss->uLoss);

		estrDestroy(&estrLevelRange);
		estrDestroy(&estrPlayerName);
	}

	ExcelXML_WorksheetAddTable(pSheet, &iCol, pRow, &eaTableTmp);

	if (iMaxRow > *pRow)
		*pRow = iMaxRow;

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
	eaDestroy(&eaSortablePlayerGains);
	eaDestroy(&eaSortablePlayerLosses);
}

void EconomyReport_AddTop100Table(ItemGainRecords* pRecords, ExcelXMLWorksheet* pSheet, int* pRow)
{
	int iMaxRow = *pRow;
	int iCol = 1;
	PlayerItemGainLoss** eaSortablePlayerGains = NULL;
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int i;

	//Sort gains and losses by magnitudes.
	eaCopy(&eaSortablePlayerGains, &pRecords->eaPlayerGainLoss);
	eaQSort(eaSortablePlayerGains, EconomyReport_SortPlayersByGain);

	//Write "Top Earners" table.
	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 4);

	eaTableTmp[0]->pchLabel = "Top Earners";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[0]->iMergedColumns = 3;
	eaTableTmp[1]->pchLabel = "Level range";
	eaTableTmp[1]->eType = kColumnType_String;
	eaTableTmp[2]->pchLabel = "Class";
	eaTableTmp[2]->eType = kColumnType_String;
	eaTableTmp[3]->pchLabel = "Earned";
	eaTableTmp[3]->eType = kColumnType_U64;

	//add top 100 places to table.
	for (i = 0; i < 100; i++)
	{
		PlayerItemGainLoss* pGain;
		char* estrPlayerName = NULL;
		char* estrLevelRange = NULL;

		pGain = eaGet(&eaSortablePlayerGains, i);

		if (!pGain || pGain->uGain == 0)
			break;

		estrStackCreate(&estrPlayerName);
		estrStackCreate(&estrLevelRange);

		estrPrintf(&estrPlayerName, "%d) %s", i+1, pGain->pchPlayerDebugName);
		estrPrintf(&estrLevelRange, "%d-%d", pGain->iMinLevel, pGain->iMaxLevel);

		ExcelXML_TableColumnAddString(eaTableTmp[0], estrPlayerName);
		ExcelXML_TableColumnAddString(eaTableTmp[1], estrLevelRange);
		ExcelXML_TableColumnAddString(eaTableTmp[2], pGain->pchClassName);
		ExcelXML_TableColumnAddU64(eaTableTmp[3], pGain->uGain);

		estrDestroy(&estrLevelRange);
		estrDestroy(&estrPlayerName);
	}

	ExcelXML_WorksheetAddTable(pSheet, &iCol, &iMaxRow, &eaTableTmp);

	iCol++;

	if (iMaxRow > *pRow)
		*pRow = iMaxRow;

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
	eaDestroy(&eaSortablePlayerGains);
}


void EconomyReport_AddSourceSinkTables(ItemGainRecords* pRecords, ExcelXMLWorksheet* pSheet, int* pRow)
{
	int iMaxRow = *pRow;
	int iCol = 1;
	ReasonItemGainLoss** eaSortableReasonGains = NULL;
	ReasonItemGainLoss** eaSortableReasonLosses = NULL;
	ExcelXMLTableColumn** eaTableTmp = NULL;
	int i;

	//Sort gains and losses by magnitudes.
	eaCopy(&eaSortableReasonGains, &pRecords->eaReasonGainLoss);
	eaCopy(&eaSortableReasonLosses, &pRecords->eaReasonGainLoss);
	eaQSort(eaSortableReasonGains, EconomyReport_SortReasonsByGain);
	eaQSort(eaSortableReasonLosses, EconomyReport_SortReasonsByLoss);

	//Write "Top Sources" table.
	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 4);

	eaTableTmp[0]->pchLabel = "Top Sources";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[0]->iMergedColumns = 3;
	eaTableTmp[1]->pchLabel = "Count";
	eaTableTmp[1]->eType = kColumnType_Int;
	eaTableTmp[2]->pchLabel = "Earned";
	eaTableTmp[2]->eType = kColumnType_U64;
	eaTableTmp[3]->pchLabel = "Avg. Profit";
	eaTableTmp[3]->eType = kColumnType_Formula;

	for (i = 0; i < eaSize(&eaSortableReasonGains); i++)
	{
		ReasonItemGainLoss* pGain = eaGet(&eaSortableReasonGains, i);

		if (!pGain || pGain->iGainCount == 0)
			break;

		ExcelXML_TableColumnAddString(eaTableTmp[0], pGain->pchReasonDisplayName);
		ExcelXML_TableColumnAddInt(eaTableTmp[1], pGain->iGainCount);
		ExcelXML_TableColumnAddU64(eaTableTmp[2], pGain->uGain);
		ExcelXML_TableColumnAddString(eaTableTmp[3], "=R[0]C[-1]/R[0]C[-2]");
	}

	ExcelXML_WorksheetAddTable(pSheet, &iCol, &iMaxRow, &eaTableTmp);

	iCol++;

	//Write "Top Sinks" table.
	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
	eaSetSizeStruct(&eaTableTmp, parse_ExcelXMLTableColumn, 4);

	eaTableTmp[0]->pchLabel = "Top Sinks";
	eaTableTmp[0]->eType = kColumnType_String;
	eaTableTmp[0]->iMergedColumns = 3;
	eaTableTmp[1]->pchLabel = "Count";
	eaTableTmp[1]->eType = kColumnType_Int;
	eaTableTmp[2]->pchLabel = "Spent";
	eaTableTmp[2]->eType = kColumnType_U64;
	eaTableTmp[3]->pchLabel = "Avg. Cost";
	eaTableTmp[3]->eType = kColumnType_Formula;

	for (i = 0; i < eaSize(&eaSortableReasonLosses); i++)
	{
		ReasonItemGainLoss* pLoss = eaGet(&eaSortableReasonLosses, i);

		if (!pLoss || pLoss->iLossCount == 0)
			break;

		ExcelXML_TableColumnAddString(eaTableTmp[0], pLoss->pchReasonDisplayName);
		ExcelXML_TableColumnAddInt(eaTableTmp[1], pLoss->iLossCount);
		ExcelXML_TableColumnAddU64(eaTableTmp[2], pLoss->uLoss);
		ExcelXML_TableColumnAddString(eaTableTmp[3], "=R[0]C[-1]/R[0]C[-2]");
	}

	ExcelXML_WorksheetAddTable(pSheet, &iCol, pRow, &eaTableTmp);

	if (iMaxRow > *pRow)
		*pRow = iMaxRow;

	eaDestroyStruct(&eaTableTmp, parse_ExcelXMLTableColumn);
	eaDestroy(&eaSortableReasonGains);
	eaDestroy(&eaSortableReasonLosses);
}

void EconomyReport_GenerateReport()
{
	int i;
	StashTableIterator itemIter;
	StashElement itemElem;
	char* estrOutput = NULL;
	ExcelXMLWorkbook* pBook = ExcelXML_CreateWorkbook();

	estrClear(&estrOutput);

	newSummary.uDaysSince2000 = (U32)(timeSecondsSince2000()/86400);
	
	//If we already ran a report today, remove it to prevent dupes.
	for (i = 0; i < eaSize(&g_DailySummaries.eaSummaries); i++)
	{
		if (g_DailySummaries.eaSummaries[i]->uDaysSince2000 == newSummary.uDaysSince2000)
		{
			eaRemoveFast(&g_DailySummaries.eaSummaries, i);
			break;
		}
	}

	//Don't need to structclone newSummary since we only need one and we're just going to ParserWriteText and then close the process.
	eaPush(&g_DailySummaries.eaSummaries, &newSummary);

	//sort summaries by date
	eaQSort(g_DailySummaries.eaSummaries, EconomyReport_SortSummariesByDate);

	//remove excess summaries, oldest first.
	while (eaSize(&g_DailySummaries.eaSummaries) >= GAMEECON_NUM_SUMMARIES_TO_KEEP)
	{
		StructDestroy(parse_ItemDailySummary, g_DailySummaries.eaSummaries[0]);
		eaRemove(&g_DailySummaries.eaSummaries, 0);
		//remove oldest
	}

	//One sheet per itemdef.
	stashGetIterator(stRecordsByItemName, &itemIter);
	while(stashGetNextElement(&itemIter, &itemElem))
	{
		int iRow = 1;
		ItemGainRecords *pRecords = stashElementGetPointer(itemElem);
		const char* pchItemDefName = stashElementGetStringKey(itemElem);
		ExcelXMLWorksheet* pCurSheet = ExcelXML_WorkbookAddSheet(pBook, pchItemDefName);
		char* estrHeader = NULL;

		estrStackCreate(&estrHeader);
		estrPrintf(&estrHeader, "%s <Font html:Size=\"10\">(", pchItemDefName);
		estrConcatf(&estrHeader, "%s through ", timeGetDateStringFromSecondsSince2000(g_uTimeStartSS2000));
		estrConcatf(&estrHeader, "%s)</Font>", timeGetDateStringFromSecondsSince2000(g_uTimeEndSS2000));

		ExcelXML_WorksheetSetRowHeight(pCurSheet, iRow, 30);
		ExcelXML_WorksheetAddStringRow(pCurSheet, NULL, &iRow, 14, "BigBold", estrHeader);

		estrClear(&estrHeader);

		EconomyReport_AddSummariesTable(pCurSheet, pchItemDefName, &iRow);
		//Skip a row.
		iRow++;

		EconomyReport_AddPlayerTables(pRecords, pCurSheet, &iRow);
		//skip a row
		iRow++;

		EconomyReport_AddSourceSinkTables(pRecords, pCurSheet, &iRow);

		{
			//Second sheet for 1-100th top earners
			char* estrSheetName = NULL;
			ExcelXMLWorksheet* pDetailSheet = NULL;
			
			iRow = 1;
			
			estrPrintf(&estrSheetName, "%s Top 100", pchItemDefName);
			estrPrintf(&estrHeader, "%s Top 100 Earners <Font html:Size=\"10\">(%s through ", pchItemDefName, timeGetDateStringFromSecondsSince2000(g_uTimeStartSS2000));
			estrConcatf(&estrHeader, "%s)</Font>", timeGetDateStringFromSecondsSince2000(g_uTimeEndSS2000));
			
			pDetailSheet = ExcelXML_WorkbookAddSheet(pBook, estrSheetName);
			
			ExcelXML_WorksheetSetRowHeight(pDetailSheet, iRow, 30);
			ExcelXML_WorksheetAddStringRow(pDetailSheet, NULL, &iRow, 14, "BigBold", estrHeader);
			
			EconomyReport_AddTop100Table(pRecords, pDetailSheet, &iRow);
			
			estrDestroy(&estrSheetName);
		}
		estrDestroy(&estrHeader);
	}

	ExcelXML_WorkbookWriteXML(pBook, &estrOutput);

	StructDestroy(parse_ExcelXMLWorkbook, pBook);

	WriteOutputFile("EconomyReport.xml", estrOutput, false);

	//write past summaries
	ParserWriteTextFile(s_estrSummaryFilename, 
		parse_ItemDailySummaries,
		&g_DailySummaries,
		0,
		0);

	estrDestroy(&estrOutput);
}

#include "GameLogEconomyReport_h_ast.c"