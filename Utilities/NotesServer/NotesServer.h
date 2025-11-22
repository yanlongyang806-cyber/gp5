#pragma once

typedef struct NameValuePair NameValuePair;

#include "NotesServer_pub.h"


AUTO_STRUCT;
typedef struct NotesServerNoteWrapper
{
	char *pSystemOrProductName; AST(KEY REQUIRED)
	SingleNote_Internal note;
} NotesServerNoteWrapper;

AUTO_STRUCT;
typedef struct NotesServerNote
{
	char *pDomainName; AST(REQUIRED)
	char *pNoteName; AST(REQUIRED)
	char *pUniqueName; AST(ESTRING, KEY) //domain.note
	NotesServerNoteWrapper **ppSystemNotes;
	NotesServerNoteWrapper **ppProductNotes;
	SingleNote_Internal globalNote;

	char *pFileName; AST(CURRENTFILE)

	char **ppLogs;
} NotesServerNote;



