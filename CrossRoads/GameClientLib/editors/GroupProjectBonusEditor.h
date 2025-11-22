#pragma once
GCC_SYSTEM
//
// GroupProjectBonusEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the group project bonus editor and displays the main window
MEWindow *groupProjectBonusEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a group project bonus for editing
void groupProjectBonusEditor_createGroupProjectBonus(char *pcName);

#endif