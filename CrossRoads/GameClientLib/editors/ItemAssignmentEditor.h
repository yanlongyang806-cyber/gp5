#pragma once
GCC_SYSTEM
//
// ItemAssignmentEditor.h
//

#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

MEWindow *itemAssignmentEditor_Init(MultiEditEMDoc *pEditorDoc) ;

void itemAssignmentEditor_CreateItemAssignment(char *pcName);

#endif