//
// GroupProjectBonusEditor.c
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

#define GPB_GROUP_MAIN			"Main"
#define GPB_SUBGROUP_REQUIRES	"Requirements"
#define GPB_SUBGROUP_STARTREWARDS	"StartRewards"
#define GPB_SUBGROUP_REWARDS	"Rewards"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *gpbWindow = NULL;
static int gpbRequirements = 0;
static int gpbRewards = 0;
static int gpbStartRewards = 0;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int gpb_validateCallback(METable *pTable, GroupProjectBonusDef *pGroupProjectBonusDef, void *pUserData)
{
	return true;
}

static void gpb_fixMessages(GroupProjectBonusDef *pBonusDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	estrCopy2(&tmpKeyPrefix, "GroupProjectBonusDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Display name
	if (!pBonusDef->displayNameMsg.pEditorCopy) {
		pBonusDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pBonusDef->name);
	if (!pBonusDef->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pBonusDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pBonusDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pBonusDef->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pBonusDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pBonusDef->displayNameMsg.pEditorCopy->pcDescription);
		pBonusDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectBonusDef");
	if (!pBonusDef->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pBonusDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pBonusDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pBonusDef->descriptionMsg.pEditorCopy) {
		pBonusDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, pBonusDef->name);
	if (!pBonusDef->descriptionMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pBonusDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
		pBonusDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pBonusDef->descriptionMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pBonusDef->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pBonusDef->descriptionMsg.pEditorCopy->pcDescription);
		pBonusDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectBonusDef");
	if (!pBonusDef->descriptionMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pBonusDef->descriptionMsg.pEditorCopy->pcScope) != 0)) {
		pBonusDef->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void gpb_postOpenCallback(METable *pTable, GroupProjectBonusDef *pBonusDef, GroupProjectBonusDef *pOrigBonusDef)
{
	gpb_fixMessages(pBonusDef);
	if (pOrigBonusDef) {
		gpb_fixMessages(pOrigBonusDef);
	}
}

static void gpb_preSaveCallback(METable *pTable, GroupProjectBonusDef *pBonusDef)
{
	gpb_fixMessages(pBonusDef);
}

static void *gpb_createObject(METable *pTable, GroupProjectBonusDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	GroupProjectBonusDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;
	Message *pDisplayMessage;
	Message *pDescriptionMessage;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_GroupProjectBonusDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;

		pDisplayMessage = langCreateMessage("", "", "", pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pDescriptionMessage = langCreateMessage("", "", "", pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewDef = StructCreate(parse_GroupProjectBonusDef);

		pcBaseName = "_New_GroupProjectBonus";

		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pDescriptionMessage = langCreateMessage("", "", "", NULL);
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create group project bonus");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, GROUP_PROJECT_BONUS_BASE_DIR"/%s.%s", true, false)),
		pNewDef->name, GROUP_PROJECT_BONUS_EXT);
	pNewDef->filename = (char*)allocAddString(buf);

	// Fill in messages
	pNewDef->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewDef->descriptionMsg.pEditorCopy = pDescriptionMessage;

	estrDestroy(&pchPath);
	
	return pNewDef;
}

static void *gpb_tableCreateCallback(METable *pTable, GroupProjectBonusDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return gpb_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}

static void *gpb_windowCreateCallback(MEWindow *pWindow, GroupProjectBonusDef *pObjectToClone)
{
	return gpb_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static void gpb_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void gpb_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static void gpb_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, gpb_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, gpb_validateCallback, pTable);
	METableSetCreateCallback(pTable, gpb_tableCreateCallback);
	METableSetPostOpenCallback(pTable, gpb_postOpenCallback);
	METableSetPreSaveCallback(pTable, gpb_preSaveCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_GroupProjectBonusDict, gpb_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, gpb_MessageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void gpb_initGroupProjectBonusColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, GROUP_PROJECT_BONUS_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, GPB_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "filename", 210, GPB_GROUP_MAIN, NULL, pchPath, pchPath, "."GROUP_PROJECT_BONUS_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "Display Name", ".displayNameMsg.EditorCopy", 100, GPB_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Description", ".descriptionMsg.EditorCopy", 100, GPB_GROUP_MAIN, kMEFieldType_Message);
	METableAddColumn(pTable, "Icon", "Icon", 180, GPB_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddGlobalDictColumn(pTable, "Project Numeric", "Numeric", 240, GPB_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, g_GroupProjectNumericDict, "ResourceName");
	METableAddEnumColumn(pTable, "Bonus Type", "BonusType", 120, GPB_GROUP_MAIN, kMEFieldType_Combo, GroupProjectBonusTypeEnum);
	METableAddSimpleColumn(pTable, "Bonus Value", "value", 100, GPB_GROUP_MAIN, kMEFieldType_TextEntry);

	estrDestroy(&pchPath);
}

static void gpb_init(MultiEditEMDoc *pEditorDoc)
{
	if (!gpbWindow) {
		// Create the editor window
		gpbWindow = MEWindowCreate("Group Project Bonus Editor", "GroupProjectBonus", "GroupProjectBonuses", SEARCH_TYPE_GROUPPROJECTBONUS, g_GroupProjectBonusDict, parse_GroupProjectBonusDef, "name", "filename", "scope", pEditorDoc);

		// Add task-specific columns
		gpb_initGroupProjectBonusColumns(gpbWindow->pTable);
		METableFinishColumns(gpbWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(gpbWindow);

		// Set the callbacks
		gpb_initCallbacks(gpbWindow, gpbWindow->pTable);

		// Set edit mode
		resSubscribeToInfoIndex(g_GroupProjectNumericDict, true);
	}

	// Show the window
	ui_WindowPresent(gpbWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *groupProjectBonusEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	gpb_init(pEditorDoc);

	return gpbWindow;
}

void groupProjectBonusEditor_createGroupProjectBonus(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = gpb_createObject(gpbWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(gpbWindow->pTable, pObject, 1, 1);
}

#endif