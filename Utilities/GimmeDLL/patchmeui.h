#pragma once
#include "pcl_typedefs.h"
#include "gimmeDLLPublicInterface.h"

void patchmeGetTestInfo(char *root, char **filenames, PCL_DiffType *diff_types); // not ui, but ripped from gimmeCheckinVerify

int patchmeDialogCheckin(GIMMEOperation op, PCL_Client *client, char *root, char ***filenames, PCL_DiffType **diff_types);

// Backup confirmation dialog
int patchmeDialogBackup(char *root, char ***filenames, PCL_DiffType **diff_types, char *backup_path, size_t backup_path_size);

// Prompt user for backup restore path.
int patchmeDialogRestorePath(char *backup_path, size_t backup_path_size);

// Backup restore dialog
int patchmeDialogRestore(char *root, const char *backup_path, char ***filenames, PCL_DiffType **diff_types);

const char* patchmeDialogCheckinGetComments(void);

int patchmeDialogDelete(const char *filepath, bool folder);

int patchmeDialogConfirm(char *root, char ***filenames, PCL_DiffType **diff_types);

U32 patchmeDialogGetDate(const char *root, bool *sync_core);
