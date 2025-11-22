//
// GroupProjectBonusEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "GroupProjectBonusEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gGroupProjectBonusEditor;

EMPicker gGroupProjectBonusPicker;

static MultiEditEMDoc *gGroupProjectBonusEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *groupProjectBonusEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	groupProjectBonusEditor_createGroupProjectBonus(NULL);

	return (EMEditorDoc*)gGroupProjectBonusEditorDoc;
}


static EMEditorDoc *groupProjectBonusEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gGroupProjectBonusEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gGroupProjectBonusEditorDoc;
}


static EMTaskStatus groupProjectBonusEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gGroupProjectBonusEditorDoc->pWindow);
}


static EMTaskStatus groupProjectBonusEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gGroupProjectBonusEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectBonusEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gGroupProjectBonusEditorDoc->pWindow);
}


static void groupProjectBonusEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gGroupProjectBonusEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectBonusEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gGroupProjectBonusEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectBonusEditorEMEnter(EMEditor *pEditor)
{

}

static void groupProjectBonusEditorEMExit(EMEditor *pEditor)
{

}

static void groupProjectBonusEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gGroupProjectBonusEditorDoc->pWindow);
}

static void groupProjectBonusEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gGroupProjectBonusEditorDoc) {
		// Create the global document
		gGroupProjectBonusEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gGroupProjectBonusEditorDoc->emDoc.editor = &gGroupProjectBonusEditor;
		gGroupProjectBonusEditorDoc->emDoc.saved = true;
		gGroupProjectBonusEditorDoc->pWindow = groupProjectBonusEditor_init(gGroupProjectBonusEditorDoc);
		sprintf(gGroupProjectBonusEditorDoc->emDoc.doc_name, "Group Project Bonus Editor");
		sprintf(gGroupProjectBonusEditorDoc->emDoc.doc_display_name, "Group Project Bonus Editor");
		gGroupProjectBonusEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gGroupProjectBonusEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int groupProjectBonusEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gGroupProjectBonusEditor.editor_name, "Group Project Bonus Editor");
	gGroupProjectBonusEditor.type = EM_TYPE_MULTIDOC;
	gGroupProjectBonusEditor.hide_world = 1;
	gGroupProjectBonusEditor.disable_single_doc_menus = 1;
	gGroupProjectBonusEditor.disable_auto_checkout = 1;
	gGroupProjectBonusEditor.default_type = "GroupProjectBonusDef";
	strcpy(gGroupProjectBonusEditor.default_workspace, "Game Design Editors");

	gGroupProjectBonusEditor.init_func = groupProjectBonusEditorEMInit;
	gGroupProjectBonusEditor.enter_editor_func = groupProjectBonusEditorEMEnter;
	gGroupProjectBonusEditor.exit_func = groupProjectBonusEditorEMExit;
	gGroupProjectBonusEditor.lost_focus_func = groupProjectBonusEditorEMLostFocus;
	gGroupProjectBonusEditor.new_func = groupProjectBonusEditorEMNewDoc;
	gGroupProjectBonusEditor.load_func = groupProjectBonusEditorEMLoadDoc;
	gGroupProjectBonusEditor.save_func = groupProjectBonusEditorEMSaveDoc;
	gGroupProjectBonusEditor.sub_save_func = groupProjectBonusEditorEMSaveSubDoc;
	gGroupProjectBonusEditor.close_func = groupProjectBonusEditorEMCloseDoc;
	gGroupProjectBonusEditor.sub_close_func = groupProjectBonusEditorEMCloseSubDoc;
	gGroupProjectBonusEditor.sub_reload_func = groupProjectBonusEditorEMReloadSubDoc;

	gGroupProjectBonusEditor.keybinds_name = "MultiEditor";

	// register picker
	gGroupProjectBonusPicker.allow_outsource = 1;
	strcpy(gGroupProjectBonusPicker.picker_name, "GroupProjectBonus Library");
	strcpy(gGroupProjectBonusPicker.default_type, gGroupProjectBonusEditor.default_type);
	emPickerManage(&gGroupProjectBonusPicker);
	eaPush(&gGroupProjectBonusEditor.pickers, &gGroupProjectBonusPicker);

	emRegisterEditor(&gGroupProjectBonusEditor);
	emRegisterFileType(gGroupProjectBonusEditor.default_type, "GroupProjectBonus", gGroupProjectBonusEditor.editor_name);
#endif

	return 1;
}

