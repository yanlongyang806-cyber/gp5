#pragma once

#include "EString.h"

//when setting a note comment, this means "leave it as it currently is"
#define NOTE_SETCOMMENT_LEAVE_AS_IS "__LEAVE"

AUTO_ENUM;
typedef enum NoteScopeType
{
	NOTESCOPE_GLOBAL,
	NOTESCOPE_PRODUCT,
	NOTESCOPE_SYSTEM,

	NOTESCOPE_COUNT, EIGNORE
} NoteScopeType;

AUTO_STRUCT;
typedef struct SingleNote_Internal
{
	char *pNormal; AST(ESTRING)
	char *pCritical; AST(ESTRING)
} SingleNote_Internal;

static __forceinline bool InternalNoteIsEmpty(SingleNote_Internal *pNote)
{
	if (!pNote || estrLength(&pNote->pNormal) == 0 && estrLength(&pNote->pCritical) == 0)
	{
		return true;
	}

	return false;
}

//the main note structure that is used by note clients
AUTO_STRUCT;
typedef struct SingleNote
{
	char *pNoteName; AST(ESTRING KEY)
	SingleNote_Internal comments[NOTESCOPE_COUNT]; AST(INDEX(0, Global) INDEX(1, Product) INDEX(2, System))
} SingleNote;

//sent from notes server to a client saying "here are all your notes"
AUTO_STRUCT;
typedef struct SingleNoteList
{
	SingleNote **ppNotes; AST(NO_INDEX)
} SingleNoteList;

//sent by client to server saying "I am registering myself for notes"
AUTO_STRUCT;
typedef struct NotesRegisterStruct
{
	char *pDomainName;
	char *pProductName;
	char *pSystemName;
} NotesRegisterStruct;

//sent by client to server saying "I am setting a note"
AUTO_STRUCT;
typedef struct NoteSettingStruct
{
	NotesRegisterStruct *pRegisterStruct; //ought to be redundant, but including it all
		//in case the notes server has lost track of things

	char *pSettingComment; //how this was set, ideally with authname
	
	SingleNote *pNote;
} NoteSettingStruct;



//send by server to client saying "here is some basic info about me
AUTO_STRUCT;
typedef struct NotesServerInfoStruct
{
	U32 iIP;
	int iPort;
} NotesServerInfoStruct; 

