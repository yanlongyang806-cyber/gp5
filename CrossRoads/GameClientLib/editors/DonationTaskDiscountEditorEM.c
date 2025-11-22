//
// DonationTaskEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "DonationTaskDiscountEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gDonationTaskDiscountEditor;

EMPicker gDonationTaskDiscountPicker;

static MultiEditEMDoc *gDonationTaskDiscountEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *donationTaskDiscountEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	donationTaskDiscountEditor_createDonationTaskDiscount(NULL);

	return (EMEditorDoc*)gDonationTaskDiscountEditorDoc;
}


static EMEditorDoc *donationTaskDiscountEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gDonationTaskDiscountEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gDonationTaskDiscountEditorDoc;
}


static EMTaskStatus donationTaskDiscountEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gDonationTaskDiscountEditorDoc->pWindow);
}


static EMTaskStatus donationTaskDiscountEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gDonationTaskDiscountEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void donationTaskDiscountEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gDonationTaskDiscountEditorDoc->pWindow);
}


static void donationTaskDiscountEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gDonationTaskDiscountEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void donationTaskDiscountEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gDonationTaskDiscountEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void donationTaskDiscountEditorEMEnter(EMEditor *pEditor)
{

}

static void donationTaskDiscountEditorEMExit(EMEditor *pEditor)
{

}

static void donationTaskDiscountEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gDonationTaskDiscountEditorDoc->pWindow);
}

static void donationTaskDiscountEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gDonationTaskDiscountEditorDoc) {
		// Create the global document
		gDonationTaskDiscountEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gDonationTaskDiscountEditorDoc->emDoc.editor = &gDonationTaskDiscountEditor;
		gDonationTaskDiscountEditorDoc->emDoc.saved = true;
		gDonationTaskDiscountEditorDoc->pWindow = donationTaskDiscountEditor_init(gDonationTaskDiscountEditorDoc);
		sprintf(gDonationTaskDiscountEditorDoc->emDoc.doc_name, "Donation Task Discount Editor");
		sprintf(gDonationTaskDiscountEditorDoc->emDoc.doc_display_name, "Donation Task Discount Editor");
		gDonationTaskDiscountEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gDonationTaskDiscountEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int donationTaskDiscountEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gDonationTaskDiscountEditor.editor_name, "Donation Task Discount Editor");
	gDonationTaskDiscountEditor.type = EM_TYPE_MULTIDOC;
	gDonationTaskDiscountEditor.hide_world = 1;
	gDonationTaskDiscountEditor.disable_single_doc_menus = 1;
	gDonationTaskDiscountEditor.disable_auto_checkout = 1;
	gDonationTaskDiscountEditor.default_type = "DonationTaskDiscountDef";
	strcpy(gDonationTaskDiscountEditor.default_workspace, "Game Design Editors");

	gDonationTaskDiscountEditor.init_func = donationTaskDiscountEditorEMInit;
	gDonationTaskDiscountEditor.enter_editor_func = donationTaskDiscountEditorEMEnter;
	gDonationTaskDiscountEditor.exit_func = donationTaskDiscountEditorEMExit;
	gDonationTaskDiscountEditor.lost_focus_func = donationTaskDiscountEditorEMLostFocus;
	gDonationTaskDiscountEditor.new_func = donationTaskDiscountEditorEMNewDoc;
	gDonationTaskDiscountEditor.load_func = donationTaskDiscountEditorEMLoadDoc;
	gDonationTaskDiscountEditor.save_func = donationTaskDiscountEditorEMSaveDoc;
	gDonationTaskDiscountEditor.sub_save_func = donationTaskDiscountEditorEMSaveSubDoc;
	gDonationTaskDiscountEditor.close_func = donationTaskDiscountEditorEMCloseDoc;
	gDonationTaskDiscountEditor.sub_close_func = donationTaskDiscountEditorEMCloseSubDoc;
	gDonationTaskDiscountEditor.sub_reload_func = donationTaskDiscountEditorEMReloadSubDoc;

	gDonationTaskDiscountEditor.keybinds_name = "MultiEditor";

	// register picker
	gDonationTaskDiscountPicker.allow_outsource = 1;
	strcpy(gDonationTaskDiscountPicker.picker_name, "DonationTaskDiscount Library");
	strcpy(gDonationTaskDiscountPicker.default_type, gDonationTaskDiscountEditor.default_type);
	emPickerManage(&gDonationTaskDiscountPicker);
	eaPush(&gDonationTaskDiscountEditor.pickers, &gDonationTaskDiscountPicker);

	emRegisterEditor(&gDonationTaskDiscountEditor);
	emRegisterFileType(gDonationTaskDiscountEditor.default_type, "DonationTaskDiscount", gDonationTaskDiscountEditor.editor_name);
#endif

	return 1;
}

