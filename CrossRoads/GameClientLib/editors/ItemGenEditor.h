#pragma once
GCC_SYSTEM
//
// ItemGenEditor.h
//

#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

MEWindow *itemGenEditor_init(MultiEditEMDoc *pEditorDoc) ;

void itemGenEditor_createItemGen(char *pcName);

#endif