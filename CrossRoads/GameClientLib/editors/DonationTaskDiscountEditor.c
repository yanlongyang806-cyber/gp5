//
// DonationTaskDiscountEditor.c
//

#ifndef NO_EDITORS

#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "GroupProjectCommon.h"
#include "StringCache.h"
#include "EString.h"
#include "Expression.h"
#include "GameBranch.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/itemEnums_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define DTE_GROUP_MAIN			"Main"
#define DTE_SUBGROUP_REQUIRES	"Requirements"
#define DTE_SUBGROUP_STARTREWARDS	"StartRewards"
#define DTE_SUBGROUP_REWARDS	"Rewards"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *dteWindow = NULL;
static int dteRequirements = 0;
static int dteRewards = 0;
static int dteStartRewards = 0;

extern ExprContext *g_pItemContext;
extern ExprContext *g_DonationTaskDiscountExprContext;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int dte_validateCallback(METable *pTable, DonationTaskDiscountDef *pDonationTaskDiscountDef, void *pUserData)
{
	//return DonationTaskDiscount_Validate(pDonationTaskDiscountDef);
	return true;
}

static void dte_fixMessages(DonationTaskDiscountDef *pTaskDiscountDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	estrCopy2(&tmpKeyPrefix, "DonationTaskDiscountDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Display name
	if (!pTaskDiscountDef->displayNameMsg.pEditorCopy) {
		pTaskDiscountDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pTaskDiscountDef->name);
	if (!pTaskDiscountDef->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pTaskDiscountDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pTaskDiscountDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pTaskDiscountDef->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pTaskDiscountDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pTaskDiscountDef->displayNameMsg.pEditorCopy->pcDescription);
		pTaskDiscountDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "DonationTaskDiscountDef");
	if (!pTaskDiscountDef->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pTaskDiscountDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pTaskDiscountDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pTaskDiscountDef->descriptionMsg.pEditorCopy) {
		pTaskDiscountDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, pTaskDiscountDef->name);
	if (!pTaskDiscountDef->descriptionMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pTaskDiscountDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
		pTaskDiscountDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pTaskDiscountDef->descriptionMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pTaskDiscountDef->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pTaskDiscountDef->descriptionMsg.pEditorCopy->pcDescription);
		pTaskDiscountDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "DonationTaskDiscountDef");
	if (!pTaskDiscountDef->descriptionMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pTaskDiscountDef->descriptionMsg.pEditorCopy->pcScope) != 0)) {
		pTaskDiscountDef->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void dte_postOpenCallback(METable *pTable, DonationTaskDiscountDef *pTaskDiscountDef, DonationTaskDiscountDef *pOrigTaskDiscountDef)
{
	dte_fixMessages(pTaskDiscountDef);
	if (pOrigTaskDiscountDef) {
		dte_fixMessages(pOrigTaskDiscountDef);
	}
}

static void dte_preSaveCallback(METable *pTable, DonationTaskDiscountDef *pTaskDiscountDef)
{
	dte_fixMessages(pTaskDiscountDef);
}

static void *dte_createObject(METable *pTable, DonationTaskDiscountDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	DonationTaskDiscountDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;
	Message *pDisplayMessage;
	Message *pDescriptionMessage;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_DonationTaskDiscountDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;

		pDisplayMessage = langCreateMessage("", "", "", pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pDescriptionMessage = langCreateMessage("", "", "", pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewDef = StructCreate(parse_DonationTaskDiscountDef);

		pcBaseName = "_New_DonationTaskDiscount";

		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pDescriptionMessage = langCreateMessage("", "", "", NULL);
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create donation task");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, DONATION_TASK_DISCOUNT_BASE_DIR"/%s.%s", true, false)),
		pNewDef->name, DONATION_TASK_DISCOUNT_EXT);
	pNewDef->filename = (char*)allocAddString(buf);

	// Fill in messages
	pNewDef->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewDef->descriptionMsg.pEditorCopy = pDescriptionMessage;

	estrDestroy(&pchPath);
	
	return pNewDef;
}

static void *dte_tableCreateCallback(METable *pTable, DonationTaskDiscountDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return dte_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}

static void *dte_windowCreateCallback(MEWindow *pWindow, DonationTaskDiscountDef *pObjectToClone)
{
	return dte_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static void dte_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void dte_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static void dte_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, dte_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, dte_validateCallback, pTable);
	METableSetCreateCallback(pTable, dte_tableCreateCallback);
	METableSetPostOpenCallback(pTable, dte_postOpenCallback);
	METableSetPreSaveCallback(pTable, dte_preSaveCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_DonationTaskDiscountDict, dte_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, dte_MessageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void dte_initDonationTaskDiscountColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, DONATION_TASK_DISCOUNT_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, DTE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "filename", 210, DTE_GROUP_MAIN, NULL, pchPath, pchPath, "."DONATION_TASK_DISCOUNT_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "Display Name", ".displayNameMsg.EditorCopy", 100, DTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Description", ".descriptionMsg.EditorCopy", 100, DTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddColumn(pTable, "Icon", "Icon", 180, DTE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddEnumColumn(pTable, "Discount Type", "DiscountType", 120, DTE_GROUP_MAIN, kMEFieldType_Combo, GroupProjectDonationDiscountTypeEnum);
	METableAddSimpleColumn(pTable, "Percent", "discountPercent", 120, DTE_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Amount", "discountAmount", 100, DTE_GROUP_MAIN, kMEFieldType_TextEntry);

	estrDestroy(&pchPath);
}

static void dte_init(MultiEditEMDoc *pEditorDoc)
{
	if (!dteWindow) {
		// Create the editor window
		dteWindow = MEWindowCreate("Donation Task Discount Editor", "DonationTaskDiscount", "DonationTaskDiscounts", SEARCH_TYPE_DONATIONTASKDISCOUNT, g_DonationTaskDiscountDict, parse_DonationTaskDiscountDef, "name", "filename", "scope", pEditorDoc);

		// Add task-specific columns
		dte_initDonationTaskDiscountColumns(dteWindow->pTable);
		METableFinishColumns(dteWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(dteWindow);

		// Set the callbacks
		dte_initCallbacks(dteWindow, dteWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(dteWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *donationTaskDiscountEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	dte_init(pEditorDoc);

	return dteWindow;
}

void donationTaskDiscountEditor_createDonationTaskDiscount(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = dte_createObject(dteWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(dteWindow->pTable, pObject, 1, 1);
}

#endif