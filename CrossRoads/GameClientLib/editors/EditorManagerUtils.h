#pragma once
GCC_SYSTEM

#ifndef __EDITORMANAGERUTILS_H__
#define __EDITORMANAGERUTILS_H__

#ifndef NO_EDITORS

typedef struct EMFile EMFile;
typedef enum GimmeErrorValue GimmeErrorValue;

typedef enum directoryType
{
	NONE = 0,
	DATA = 1,
	SRC = 2
} directoryType;

int emuGetBranchNumber(void);

// Open and OpenContaining operate on a single file
int emuOpenFile(const char *filename);
void emuOpenContainingDirectoryEx(const char *filename, directoryType type);
#define emuOpenContainingDirectory(filename) emuOpenContainingDirectoryEx(filename, NONE)

// These others also operate on any linked files
int emuCheckoutFile(EMFile *file);
int emuCheckoutFileEx(EMFile *file, const char *name, bool show_dialog);
bool emuHandleGimmeMessage(const char *filename, GimmeErrorValue gimmeMessage, bool doing_checkout, bool show_dialog);

void emuCheckinFile(EMFile *file);
void emuCheckinFileEx(EMFile *file, const char *name, bool show_dialog);

void emuCheckpointFile(EMFile *file);
void emuCheckpointFileEx(EMFile *file, const char *name, bool show_dialog);

void emuUndoCheckout(EMFile *file);
void emuUndoCheckoutEx(EMFile *file, const char *name, bool show_dialog);

void emuGetLatest(EMFile *file);
void emuGetLatestEx(EMFile *file, const char *name, bool show_dialog);

void emuCheckRevisions(EMFile *file);
void emuCheckRevisionsEx(EMFile *file, const char *name, bool show_dialog);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUTILS_H__
