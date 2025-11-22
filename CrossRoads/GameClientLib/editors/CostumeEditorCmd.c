//
// CostumeEditorCmd.c
//

#ifndef NO_EDITORS

#include "CostumeEditor.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping("CostumeEditorCmd.c", BUDGET_Editors););


//---------------------------------------------------------------------------------------------------
// Keybinds & Commands
//---------------------------------------------------------------------------------------------------

#endif

AUTO_COMMAND ACMD_NAME("CostumeEditor.SelectBone");
void CmdSelectBone(void) 
{
#ifndef NO_EDITORS
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	if (pDoc && pDoc->pGraphics) {
		PCBoneDef *pBone = costumeView_GetSelectedBone(pDoc->pGraphics, pDoc->pCostume);
		if (pBone) {
			costumeEdit_SelectBone(pDoc, pBone);
		}
	}
#endif
}

AUTO_COMMAND ACMD_NAME("COE_CloneCostume");
void CmdCloneCostume(void) 
{
#ifndef NO_EDITORS
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();

	costumeEdit_UICostumeClone(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("COE_RevertCostume");
void CmdRevertCostume(void) 
{
#ifndef NO_EDITORS
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();

	costumeEdit_CostumeRevert(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("CostumeEditor.CenterCamera");
void CmdCostumeCenterCamera(void) 
{
#ifndef NO_EDITORS
	EMEditor *editor = costumeEditorEMGetEditor();
	costumeEdit_UICenterCamera(NULL, editor);
#endif
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_CLIENTONLY ACMD_PRIVATE;
void costumeGiveDyePackNames(CostumeEditDyePackStructList *dyePackNameList)
{
	CostumeEditDyePackStruct ***dyePackNames = costumeEdit_GetDyePacks();
	CostumeEditDyePackStruct *dyePack = NULL;
	CostumeEditDoc **eaDocs = NULL;
	int i;
	if (*dyePackNames)
	{
		eaClearStruct(dyePackNames, parse_CostumeEditDyePackStruct);
		eaDestroy(dyePackNames);
	}
	for (i = 0; i < eaSize(&dyePackNameList->eaDyePack); i++)
	{
		dyePack = StructCreate(parse_CostumeEditDyePackStruct);
		dyePack->pchDyePack = allocAddString(dyePackNameList->eaDyePack[i]->pchDyePack);
		eaPush(dyePackNames, dyePack);
	}
	if (eaSize(dyePackNames))
	{
		costumeEditorEMGetAllOpenDocs(&eaDocs);
		for (i = 0; i < eaSize(&eaDocs); i++)
		{
			ui_SetActive(eaDocs[i]->pDyePackField->pUIWidget, true);
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_CLIENTONLY ACMD_PRIVATE;
void costumeApplyDyePack(ItemDef *pDef, char *pName)
{
	CostumeEditDoc **eaDocs = NULL;
	int i;
	costumeEditorEMGetAllOpenDocs(&eaDocs);
	for (i = 0; i < eaSize(&eaDocs); i++)
	{
		if (strcmp(eaDocs[i]->pCostume->pcName, pName) == 0)
		{
			costumeEdit_UIApplyDyePackHelper(pDef, eaDocs[i]);
			return;
		}
	}
}
