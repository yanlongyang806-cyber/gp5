#pragma once
GCC_SYSTEM
//
// AlgoPetEditor.h
//

#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

MEWindow *algoPetEditor_init(MultiEditEMDoc *pEditorDoc) ;

void algoPetEditor_createAlgoPet(char *pcName);

#endif