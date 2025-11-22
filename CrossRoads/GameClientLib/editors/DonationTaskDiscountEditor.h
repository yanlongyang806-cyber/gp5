#pragma once
GCC_SYSTEM
//
// DonationTaskDiscountEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the donation task editor and displays the main window
MEWindow *donationTaskDiscountEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a donation task for editing
void donationTaskDiscountEditor_createDonationTaskDiscount(char *pcName);

#endif