// These are functions called *only* by the stand-alone version of gimme, and this file (gimmeUtil.c/.h)
// need not be included in other projects (such as the mapserver), since these functions are not
// references outside of the standalone

#include "gimmeUtil.h"
#include "gimmeUserGroup.h"
#include "gimme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <share.h>
#include "file.h"
#include "fileutil.h"
#include "utils.h"
#include "osdependent.h"

#include <process.h>
#include <time.h>
#include <fcntl.h>
#include <conio.h>
#include "RegistryReader.h"
#include "resource.h"
#include "earray.h"
#include "gimmeBranch.h"
#include "sysutil.h"
#include "error.h"
#include "logging.h"
#include "genericDialog.h"
#include "EString.h"
#include "commctrl.h"
#include "winutil.h"
#include "fileutil2.h"
#include "AppRegCache.h"
#include "mutex.h"

#include "strings_opt.h"
#include "UTF8.h"

#define LAST_CHAR(s) s[strlen(s)-1]

#define HOG_FORCE_UPDATE_NUM 3 // Change this number to force a Hogg update

typedef struct log_line {
	GimmeDir *gimme_dir;
	char relpath[131072];
	GIMMEOperation operation;
	const char *comments;
} log_line;

log_line last = {0};
log_line current;
static int count=0;
static char line[1024*1024]; // Comments can be really long!
static void writeCommentLine(void);

extern char *checkin_notes[];
extern enum checkin_note_ids;


int gimmeUtilApprove(int gimme_dir_num, __time32_t *time)
{
	FILE *file;
	GimmeDir *gimme_dir;
	FWStatType sbuf;
	char *approve_file;;

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: -approve called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: -approve called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimme_dir = eaGimmeDirs[gimme_dir_num];
	approve_file = getApprovedFile(gimme_dir);

	// Mark the current set as approved by dropping a approved.txt in the n:/game/locks/ folder
	if (fileExists(approve_file)) {
		//_chmod(approve_file, _S_IREAD | _S_IWRITE);
		//rename(approve_file, approve_old_file);
		fileMakeLocalBackup(approve_file, 60*60*24);
	}
	file = fopen(approve_file,"wb");
	if (!file)
	{
		gimmeLog(LOG_FATAL, "Couldn't make approval file: %s", approve_file);
		return GIMME_ERROR_LOCKFILE_CREATE;
	}
	pststat(approve_file, &sbuf);
	fprintf(file,"%d\n%s\n", time==NULL?sbuf.st_mtime:*time, gimmeGetUserName());
	fclose(file);
	gimmeLog(LOG_STAGE, "Current version approved [%s] with timestamp %d.", gimme_dir->lock_dir, time==NULL?sbuf.st_mtime:*time);
	return NO_ERROR;
}

int gimmeUtilDiffFile(const char *fname)
{
	char	fullpath[CRYPTIC_MAX_PATH],localfname[CRYPTIC_MAX_PATH],*username,dbname[CRYPTIC_MAX_PATH],buf[CRYPTIC_MAX_PATH*2],*relpath;
	GimmeDir	*gimme_dir;
	int		latestrev=-1;
	GimmeNode *node;
	intptr_t ret;
	char fname_temp[CRYPTIC_MAX_PATH], *fname2;
	char buf2[CRYPTIC_MAX_PATH];

	// Print out interesting info
	if (NO_ERROR==gimmeUtilStatFile(fname, 0, 0)) {
		// Find full paths to do the diff
		strcpy(fname_temp, fname);
		fname2 = fileLocateRead(fname_temp, buf2);
		if (fname2==NULL) {
			makefullpath(fname_temp,fullpath);
		} else {
			makefullpath(fname2,fullpath); // This might not do anything unless people have things like "./" in their gameDataDirs
		}

		fname = fullpath;
		makefullpath((char*)fname,fullpath);
		gimme_dir = findGimmeDir(fullpath);

		gimmeDirDatabaseLoad(gimme_dir, fullpath);

		relpath = findRelPath(fullpath, gimme_dir);
		username = isLocked(gimme_dir, relpath);
		makeLocalNameFromRel(gimme_dir, relpath, localfname);
		latestrev=makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
		if (latestrev>=0 && gimme_state.editor) 
		{
			sprintf(dbname, "%s%s", gimme_dir->lock_dir, gimmeNodeGetFullPath(node));
			// run diff
			sprintf(buf, "\"%s\"", dbname);
			strcpy(dbname, buf);
			sprintf(buf, "\"%s\"", localfname);
			strcpy(localfname, buf);
			
			{
				char *pFullCmdline = NULL;
				estrPrintf(&pFullCmdline, "%s %s %s", gimme_state.editor, backSlashes(localfname), backSlashes(dbname));
				ret=system(pFullCmdline);
				estrDestroy(&pFullCmdline);
			}

			if (ret==-1) {
				gimmeLog(LOG_FATAL, "Error launching diff program (%s)", gimme_state.editor);
				gimmeDirDatabaseClose(gimme_dir);
				return GIMME_ERROR_FILENOTFOUND;
			}
		}
		gimmeDirDatabaseClose(gimme_dir);
	}

	return NO_ERROR;
}


GimmeDatabase *db1;
time_t ignoreAfter;

int checkConsist(GimmeNode *param) {
	char relpath[CRYPTIC_MAX_PATH];
	GimmeNode *node;

	if (strEndsWith(param->name, ".comments") || strEndsWith(param->name, ".batchinfo"))
		return 1;

	strcpy(relpath, gimmeNodeGetFullPath(param));
	node = gimmeNodeFind(db1->root->contents, relpath);
	if (node==NULL) {
		if (param->checkintime > ignoreAfter) {
			gimmeLog(LOG_FATAL, "AFTER CHECK BEGAN:  %s", relpath);
		} else {
			gimmeLog(LOG_FATAL, "%s", relpath);
		}
	}
	return 1;
}

int gimmeUtilCheck(int gimme_dir_num) {
	GimmeDir *gimme_dir;
	GimmeDatabase *db0, *dbt;

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: -approve called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: -approve called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeSetBranchConfigRoot(eaGimmeDirs[gimme_dir_num]->lock_dir);
	gimme_dir = eaGimmeDirs[gimme_dir_num];

	ignoreAfter = getServerTime(gimme_dir);

	// Load from db.txt
	gimme_state.db_mode = GIMME_FORCE_DB;
	gimmeDirDatabaseLoad(gimme_dir, gimme_dir->lock_dir);
	db0 = gimme_dir->database;

	// Load from FileSystem
	gimme_dir->database = NULL;
	gimme_state.db_mode = GIMME_NO_DB;
	gimmeDirDatabaseLoad(gimme_dir, gimme_dir->lock_dir);
	db1 = gimme_dir->database;

	printf("Running DB Consistency Check...\n");
	printf("Note that these results may not be accurate if any transactions have occured while scanning the File System.\n\n");
	printf("Files in DATABASE.TXT but not in FileSystem:\n");
	gimmeNodeRecurse(db0->root->contents, checkConsist);
	dbt = db0;
	db0 = db1;
	db1 = dbt;
	printf("Files in FileSystem but not in DATABASE.TXT:\n");
	gimmeNodeRecurse(db0->root->contents, checkConsist);
	printf("done.\n");
	return NO_ERROR;
}

int gimmeUtilLabel(int gimme_dir_num, char *label)
{
	GimmeDir *gimme_dir;

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: -label called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: -label called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeSetBranchConfigRoot(eaGimmeDirs[gimme_dir_num]->lock_dir);
	gimme_dir = eaGimmeDirs[gimme_dir_num];
	last.comments = label;
	last.gimme_dir = gimme_dir;
	last.operation = GIMME_LABEL;
	strcpy(last.relpath, "/");
	writeCommentLine();

	return NO_ERROR;
}

static U32 getHogIndexTimestamp(const char *local_dir)
{
	char path[CRYPTIC_MAX_PATH];
	char **files;
	U32 count=0;
	U32 ret=0;
	U32 i;
	sprintf_s(SAFESTR(path), "%s/piggs/hoggs", local_dir);
	files = fileScanDir(path);
    count = eaSize( &files );
	for (i=0; i<count; i++) {
		ret += fileLastChanged(files[i]);
	}
	fileScanDirFreeNames(files);
	sprintf_s(SAFESTR(path), "%s/piggs/use_hoggs.txt", local_dir);
	if (fileExists(path))
		ret++;
	ret += HOG_FORCE_UPDATE_NUM;
	if (ret == -1) // 1 has a special meaning
		ret = -3;
	return ret; // Sum of all timestamps in folder + offset
}

void gimmeUtilCheckForHoggUpdate(int gimme_dir_num)
{
	RegReader reader;
	char fn[CRYPTIC_MAX_PATH];
	char key[32];
	U32 last_run;
	U32 timestamp;
	int ret;
	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: hogg update called when no source control folders are configured");
		return;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: hogg update called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return;
	}
	if (!gimmeShouldUseHogs(eaGimmeDirs[gimme_dir_num]))
		return;

	// Now, only run this if piggs/hoggs/*.txt have changed
	reader = createRegReader();
	sprintf(key, "LastRunHoggUpdate%d", gimme_dir_num);
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
	if (!rrReadInt(reader, key, &last_run)) {
		last_run = 0;
	}
	if (last_run == -1) { // Hack to get around running hogg updates
		destroyRegReader(reader);
		return;
	}
	timestamp = getHogIndexTimestamp(eaGimmeDirs[gimme_dir_num]->local_dir);
	if (last_run == timestamp && !gimmeIsForcedHoggUpdate(gimme_dir_num))
	{
		destroyRegReader(reader);
		return;
	}

	// Check for update_hoggs.bat
	sprintf(fn, "%s/piggs/hoggs/update_hoggs.bat", eaGimmeDirs[gimme_dir_num]->local_dir);
	forwardSlashes(fn);
	if (!fileExists(fn)) {
		// Write the value here to help with switching to and from branches with and without hogg index files
		rrWriteInt(reader, key, timestamp);
		destroyRegReader(reader);
		return;
	}

	// piggs/hoggs/update_hoggs.bat exists

	setConsoleTitle("Gimme: Updating Hogg files");
	if (last_run + 1 == timestamp && HOG_FORCE_UPDATE_NUM==1) {
		// Bugfix
		printf("\n\nHogg format changed (now using UTC times),\n  updating hogg files in %s, this may take a few minutes...\n\n", eaGimmeDirs[gimme_dir_num]->local_dir);
	} else {
		printf("\n\nHogg index files changed (or previous hogg update did not complete successfully),\n  updating hogg files in %s, this may take a few minutes...\n\n", eaGimmeDirs[gimme_dir_num]->local_dir);
	}
	backSlashes(fn);
	ret = system(fn);
	if (ret!=0)
		rrWriteInt(reader, key, 0); // Failed!  Re-run it next time!
	else
		rrWriteInt(reader, key, timestamp);
	printf("Done.\n");
	setConsoleTitle("Gimme: Done.");
	destroyRegReader(reader);
}

void gimmeUtilCheckForRunEvery(int gimme_dir_num)
{
	char fn[CRYPTIC_MAX_PATH];
	sprintf(fn, "%s/internal/RunEvery.bat", eaGimmeDirs[gimme_dir_num]->local_dir);
	forwardSlashes(fn);
	if (!fileExists(fn))
		return;
	backSlashes(fn);
	setConsoleTitle("Gimme: Running RunEvery.bat");
	system(fn);
	setConsoleTitle("Gimme: Done.");
}

static char *getLastCommentsFromRegistry() {
	RegReader reader;
	static char *commentBuffer=NULL;
	if (!commentBuffer) {
		commentBuffer = calloc(64*1024,1);
	}

	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
	if (!rrReadString(reader, "LastComments", commentBuffer, 64*1024)) {
		commentBuffer[0]=0;
	}
	destroyRegReader(reader);
	return commentBuffer;
}

void storeLastComments(char *comments) {
	RegReader reader;

	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
	rrWriteString(reader, "LastComments", comments);
	destroyRegReader(reader);
}

LRESULT CALLBACK DlgCheckinProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);

typedef struct
{
	char *fname;
	GimmeQueuedAction *action;
	HWND checkbox;
} CheckinFile;

static CheckinFile **g_filenames=NULL;
static unsigned int g_filenames_len=4096;
static char *g_batchinfo_filenames=NULL;
static unsigned int g_batchinfo_filenames_len=4096;
static char *gpComments =NULL;
static char *gpPatchNotes = NULL;

static void appendFilename(char *filename, GimmeQueuedAction *action) 
{
	CheckinFile *cfile = NULL;

	cfile = (CheckinFile*)malloc(sizeof(CheckinFile));
	cfile->fname = strdup(filename);
	cfile->action = action;

	eaPush(&g_filenames, cfile);
	//if (!g_filenames) {
	//	g_filenames = malloc(g_filenames_len);	
	//	g_filenames[0]=0;
	//}
	//if (strlen(g_filenames) + strlen(filename)+4 > g_filenames_len) {
	//	g_filenames_len *= 4;
	//	g_filenames = realloc(g_filenames, g_filenames_len);
	//}
	//if (g_filenames[0]) {
	//	strcat_s(g_filenames, g_filenames_len, "\r\n");
	//}
	//strcat_s(g_filenames, g_filenames_len, filename);
}

static void appendBatchInfoFilename(char *filename) {
	if (!g_batchinfo_filenames) {
		g_batchinfo_filenames = malloc(g_batchinfo_filenames_len);	
		g_batchinfo_filenames[0]=0;
	}
	if (strlen(g_batchinfo_filenames) + strlen(filename)+4 > g_batchinfo_filenames_len) {
		g_batchinfo_filenames_len *= 4;
		g_batchinfo_filenames = realloc(g_batchinfo_filenames, g_batchinfo_filenames_len);
	}
	if (g_batchinfo_filenames[0]) {
		strcat_s(g_batchinfo_filenames, g_batchinfo_filenames_len, "\r\n");
	}
	strcat_s(g_batchinfo_filenames, g_batchinfo_filenames_len, filename);
}



char *gimmeDialogCheckinGetComments(void)
{
	return gpComments;
}

char *gimmeDialogCheckinGetBatchInfo(void)
{
	return g_batchinfo_filenames;
}

static GimmeQueuedAction **g_actions=NULL;
// Returns 1 if they entered comments and clicked OK, 0 if they canceled
int gimmeDialogCheckin(GimmeQueuedAction **actions)
{
	int i;
	int ret, found_empty_file = 0, hit_oktoall = 0;
	g_actions = actions;

	if ( eaSize(&g_actions) == 0 )
		return 0;

	if (g_filenames)
	{
		for( i = 0; i < eaSize(&g_filenames); ++i )
			if ( g_filenames[i] )
				free(g_filenames[i]);
		eaSetSize(&g_filenames, 0);
		//g_filenames[0]=0;
	}

	for (i=0; i<eaSize(&actions); i++) {
		char buf[4096];
		int fsize = 0;
		sprintf_s(SAFESTR(buf), "%s%s", actions[i]->gimme_dir->local_dir, actions[i]->relpath);
		fsize = fileSize(buf);
		if ( fsize == 0 )
		{
			actions[i]->notes = checkin_notes[CHECKIN_NOTE_EMPTYFILE];
			found_empty_file = 1;
		}
		sprintf_s(SAFESTR(buf), "%s%s", buf, actions[i]->notes?actions[i]->notes:"");
		appendFilename(buf, actions[i]);
		if ( actions[i]->operation == GIMME_CHECKIN )
			appendBatchInfoFilename(buf);
	}
	if (!gpComments) {
		gpComments = estrDup(getLastCommentsFromRegistry());
	}
	if ( found_empty_file )
		MessageBox_UTF8(NULL, "You are attempting to check in one or more empty files.  These files should not be checked in unless absolutely necessary.  The empty files will be unchecked by default in the checkin dialog.  If these files already exist in the gimme database but are currently not used, they should be removed with the Gimme Remove command.", "Warning", MB_OK);
	printf("List of files to check in complete, please enter comments in dialog...\n");
	ret = (int)DialogBox (winGetHInstance(), MAKEINTRESOURCE(IDD_DLG_CHECKIN), NULL, (DLGPROC)DlgCheckinProc);
	return ret;
}

void getCommentsFromDialog(HWND hDlg) 
{
	GetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT), &gpComments);
}

void getPatchNotesFromDialog(HWND hDlg)
{

	GetWindowText_UTF8(GetDlgItem(hDlg, IDC_PATCHNOTES), &gpPatchNotes);
}

void updateBranchNotify(HWND hDlg)
{
	static int timer = 0;
	static unsigned int offs = 0;
	if (g_actions && g_actions[0] && gimmeGetCheckinWarning(g_actions[0]->gimme_dir->active_branch)) {
		char buffer[4096];
		const char *str = gimmeGetCheckinWarning(g_actions[0]->gimme_dir->active_branch);
		strcpy(buffer, str);
		strcat(buffer, " ");
		strcat(buffer, str);
		strcat(buffer, " ");
		strcat(buffer, str);
		if (offs > strlen(str)) {
			offs = 0;
		}
		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH_NOTIFY), buffer + offs);
		offs++;
	} else {
		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH_NOTIFY), "");
	}
}

void clearUncheckedFiles(HWND hDlg)
{
	int i;
	
	for ( i = 0; i < eaSize(&g_filenames); ++i )
	{
		if ( !ListView_GetCheckState(hDlg, i) )
			eaFindAndRemove(&g_actions, g_filenames[i]->action);
	}
}

WNDPROC orig_CheckinListViewProc = NULL;
int updateItemsCheckedIn = 0;

LRESULT CALLBACK CheckinListViewProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch( iMsg )
	{
	case WM_LBUTTONDOWN:
		updateItemsCheckedIn = 1;
		break;
	case WM_LBUTTONDBLCLK:
		{
			int i;
			char buff[1024];
			GimmeQueuedAction *action;
			
			i = ListView_GetNextItem(hDlg, -1, LVNI_SELECTED);
			if ( i != -1 )
			{
				action = g_filenames[i]->action;
				sprintf(buff, "%s%s", action->gimme_dir->local_dir, action->relpath);
				printf("Performing Gimme Diff on %s\n", buff);
				gimme_state.editor = gimmeDetectDiffProgram();
				gimmeUtilDiffFile(buff);
			}
		}
		break;
	}

	return CallWindowProc(orig_CheckinListViewProc,hDlg, iMsg, wParam, lParam);
}

// see http://www.codeproject.com/listctrl/listview.asp for notes on how this works
LRESULT CheckinListViewCustomDraw (LPARAM lParam)
{
    LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;

    switch(lplvcd->nmcd.dwDrawStage) 
    {
        case CDDS_PREPAINT : //Before the paint cycle begins
            //request notifications for individual listview items
            return CDRF_NOTIFYITEMDRAW;
            
        case CDDS_ITEMPREPAINT: //Before an item is drawn
			{
				int i;
				GimmeQueuedAction *action;

				i = (int)lplvcd->nmcd.dwItemSpec;
				action = g_filenames[i]->action;
				if ( !action->notes )
				{
					lplvcd->clrText   = RGB(0,0,180);
				}
				else if ( stricmp(action->notes, checkin_notes[CHECKIN_NOTE_NOCHANGE]) == 0 )
				{
					lplvcd->clrText   = RGB(100,100,100);
				}
				else if ( stricmp(action->notes, checkin_notes[CHECKIN_NOTE_NEW]) == 0 )
				{
					lplvcd->clrText   = RGB(0,180,0);
				}
				else if ( stricmp(action->notes, checkin_notes[CHECKIN_NOTE_DELETED]) == 0 )
				{
					lplvcd->clrText   = RGB(150,0,0);
				}
				else if ( stricmp(action->notes, checkin_notes[CHECKIN_NOTE_EMPTYFILE]) == 0 )
				{
					lplvcd->clrText   = RGB(150,0,150);
				}
			}
			break;
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK DlgCheckinProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	static char *old_comments=NULL;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i;
			LVCOLUMN lvc={0};
			char numItemsBuff[256];

			ListView_SetExtendedListViewStyle(GetDlgItem(hDlg, IDC_LIST), LVS_EX_CHECKBOXES 
										  | LVS_EX_FULLROWSELECT
										  | LVS_EX_ONECLICKACTIVATE 
			);

			lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvc.fmt = LVCFMT_LEFT;
			lvc.cx = 1000;
			ListView_InsertColumn(GetDlgItem(hDlg, IDC_LIST), 0, &lvc);
			lvc.iSubItem = 0;
			lvc.cx = 1000;
			lvc.pszText = L"File";
			ListView_InsertColumn(GetDlgItem(hDlg, IDC_LIST), 1, &lvc);

			orig_CheckinListViewProc = (WNDPROC)(LONG_PTR)SetWindowLongPtr(GetDlgItem(hDlg, IDC_LIST),
				GWLP_WNDPROC, (LONG_PTR)CheckinListViewProc);

			//SetWindowLong(GetDlgItem(hDlg, IDC_LIST), DWL_MSGRESULT, (LONG)CheckinListViewCustomDraw(lParam));
	
			//SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), g_filenames);
			for ( i = 0; i < eaSize(&g_filenames); ++i )
			{
				LVITEM lvi;
				lvi.mask = LVIF_COLUMNS | LVIF_TEXT;
				lvi.iItem = i;
				lvi.iSubItem = 0;
				lvi.pszText = UTF8_To_UTF16_malloc(g_filenames[i]->fname);
				ListView_InsertItem(GetDlgItem(hDlg, IDC_LIST), &lvi);
				ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i, g_filenames[i]->action->notes ? 
					(stricmp(g_filenames[i]->action->notes, checkin_notes[CHECKIN_NOTE_EMPTYFILE]) == 0 ? 0 : 1) : 1);
				free(lvi.pszText);
			}
			sprintf(numItemsBuff, "%d", eaSize(&g_filenames));
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_ITEMSCHECKEDIN), numItemsBuff);
			if (estrLength(&gpComments)) {
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT), gpComments);
				if ( gimme_state.repeat_last_comment && old_comments ) {
					getCommentsFromDialog(hDlg);
					storeLastComments(gpComments);
					EndDialog(hDlg, 1);
					return TRUE;
				} 
				else {
					old_comments = strdup(gpComments);
				}
			}
			if ( !gimmeArePatchNotesRequired() )
			{
				CheckDlgButton(hDlg, IDC_PATCHNOTES_CHECKBOX, BST_CHECKED);
			}
			SetTimer(hDlg, 0, 120, NULL);
			updateBranchNotify(hDlg);
			// flash the window if it is not the focus
			flashWindow(hDlg);
			return FALSE;
		}
	case WM_TIMER:
		updateBranchNotify(hDlg);
		{
			int i, cnt = eaSize(&g_filenames);
			char numItemsBuff[256];
			for ( i = 0; i < eaSize(&g_filenames); ++i )
			{
				if ( !ListView_GetCheckState(GetDlgItem(hDlg, IDC_LIST), i) )
					--cnt;
			}
			// if there are 0 items selected, change the unselect all button to select all
			if ( cnt == 0 )
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Select All");
			else if ( cnt == eaSize(&g_filenames) )
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Unselect All");
			sprintf(numItemsBuff, "%d", cnt);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_ITEMSCHECKEDIN), numItemsBuff);
			updateItemsCheckedIn = 0;

			// determine whether to enable the ok button based on if they have properly
			// filled out the patch notes box
			if ( GetWindowTextLength(GetDlgItem(hDlg,IDC_PATCHNOTES)) > 0 )
				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
			else
			{
				if ( IsDlgButtonChecked(hDlg, IDC_PATCHNOTES_CHECKBOX) )
				{
					EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
					EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), FALSE);
				}
				else
				{
					EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
					EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), TRUE);
				}
			}
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			getCommentsFromDialog(hDlg);
			if ( !IsDlgButtonChecked(hDlg, IDC_PATCHNOTES_CHECKBOX) )
				getPatchNotesFromDialog(hDlg);
			storeLastComments(gpComments);
			clearUncheckedFiles(GetDlgItem(hDlg, IDC_LIST));
			KillTimer(hDlg, 0);
			EndDialog(hDlg, 1);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			KillTimer(hDlg, 0);
			EndDialog(hDlg, 0);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDC_BTN_UNCHECKALL)
		{
			int i;
			char *pBuff = NULL;
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL), &pBuff);
			if ( strcmp(pBuff, "Unselect All") == 0 )
			{
				for ( i = 0; i < eaSize(&g_filenames); ++i )
				{
					ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i, 0);
				}
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Select All");
			}
			else
			{
				for ( i = 0; i < eaSize(&g_filenames); ++i )
				{
					ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i, 1);
				}
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Unselect All");
			}
			updateItemsCheckedIn = 1;
			estrDestroy(&pBuff);
		}
		else if (LOWORD(wParam) == IDC_BTN_CLIPBOARD)
		{
			int i;
			char *estrFileList = NULL;
			estrStackCreate(&estrFileList);
			for ( i = 0; i < eaSize(&g_filenames); ++i )
			{
				if ( ListView_GetCheckState(GetDlgItem(hDlg, IDC_LIST), i) )
				{
					estrConcat(&estrFileList, g_filenames[i]->fname, (int)strlen(g_filenames[i]->fname));
					estrConcat(&estrFileList, "\n", 1);
				}
			}
			if ( estrFileList )
				winCopyToClipboard(estrFileList);
			estrDestroy(&estrFileList);
		}
		break;
	case WM_ACTIVATE:
		getCommentsFromDialog(hDlg);
		if (old_comments && gpComments && strcmp(gpComments, old_comments)==0) {
			// Comments didn't change
			char *newcomments = getLastCommentsFromRegistry();
			if (strcmp(old_comments, newcomments)!=0) {
				// Another dialog saved some comments!
				estrCopy2(&gpComments, newcomments);
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT), gpComments);
				free(old_comments);
				old_comments = strdup(gpComments);
			}
		} else {
			// The use has changed comments manually!  Don't do anything!
		}
		break;
	case WM_NOTIFY:
		{
			if(((LPNMHDR)lParam)->code == NM_CUSTOMDRAW)
			{
			SetWindowLong(hDlg, DWL_MSGRESULT, 
					(LONG)CheckinListViewCustomDraw(lParam));
			return TRUE;
			}
		}
		break;
	}
	return FALSE;
}

const char *gimmeOperationToString(GIMMEOperation op)
{
	switch (op) {
		case GIMME_CHECKOUT:
			return "Checkout";
		case GIMME_CHECKIN:
			return "Checkin";
		case GIMME_FORCECHECKIN:
			return "Force Checkin";
		case GIMME_DELETE:
			return "Delete";
		case GIMME_GLV:
			return "Get Latest Version";
		case GIMME_UNDO_CHECKOUT:
			return "Undo Checkout";
		case GIMME_CHECKIN_LEAVE_CHECKEDOUT:
			return "Checkpoint";
		case GIMME_LABEL:
			return "LABEL";
		default:
			return "UNKNOWN";
	}
}

static void writeCommentLine(void)
{
	char commentfile[CRYPTIC_MAX_PATH], commentlockfile[CRYPTIC_MAX_PATH];
	int lockfile;
	FILE *f;
	__time32_t current_time = _time32(NULL);
	char timebuf[256];
	const char *comments = escapeString_unsafe(last.comments);
	_ctime32_s(SAFESTR(timebuf), &current_time);
	timebuf[strlen(timebuf)-1]=0; // chop off carriage return

	if (count>1){
		sprintf(line, "%s\t%s\t%s\t%s (%d files)\t%s\n", gimmeGetUserName(), timebuf, gimmeOperationToString(last.operation), last.relpath, count, comments);
	} else {
		sprintf(line, "%s\t%s\t%s\t%s\t%s\n", gimmeGetUserName(), timebuf, gimmeOperationToString(last.operation), last.relpath, comments);
	}
	sprintf(commentfile, "%s/comments%da.txt", last.gimme_dir->lock_dir, last.gimme_dir->active_branch);
	sprintf(commentlockfile, "%s.databaselock", commentfile);
	lockfile = gimmeWaitToAcquireLock(commentlockfile);

	f = fopen(commentfile, "a");
	fwrite(line, 1, strlen(line), f);
	fclose(f);

	_close(lockfile);
	gimmeEnsureDeleteLock(commentlockfile);

	// write patchnotes file
	{
		sprintf(commentfile, "%s/patchnotes%d.txt", last.gimme_dir->lock_dir, last.gimme_dir->active_branch);
		sprintf(commentlockfile, "%s.databaselock", commentfile);
		lockfile = gimmeWaitToAcquireLock(commentlockfile);

		f = fopen(commentfile, "a");
		if ( last.operation == GIMME_LABEL )
			fwrite(line, 1, strlen(line), f);
		else if ( estrLength(&gpPatchNotes))
		{
			const char *fixedPatchNotes = escapeString_unsafe(gpPatchNotes);
			sprintf(line, "%s\t%s\tPatchNote\t%s\t%s\n", gimmeGetUserName(), timebuf, last.relpath, fixedPatchNotes);
			fwrite(line, 1, strlen(line), f);
		}
		fclose(f);

		_close(lockfile);
		gimmeEnsureDeleteLock(commentlockfile);
	}
}

void mergeFileName(char *first, size_t first_size, const char *second)
{
	// Merges two filenames by just keeping what is in common.
	//  data/blarg.txt + data/foo.ogg => data/*
	//  data/myfile.txt + data/another.txt => data/*.txt
	//  abaaba + aba => aba*
	//  caba + aba => *aba
	//  ababa + ababbaba => abab*baba (not by design, could be fixed if we cared)
	char left[1024];
	char right[1024];
	char *c0, *c2;
	const char *c1;
	for (c0=first, c1=second, c2=left; *c0==*c1 && *c0 && *c1; c0++, c1++) {
		*(c2++) = *c0;
	}
	if (*c0==0 && *c1==0) {
		// the same!
		return;
	} else if (*c0==0) {
		// first = abc, second = abcd
		strcat_s(first, first_size, "*");
		return;
	} else if (*c1==0) {
		strcpy_s(first, first_size, second);
		strcat_s(first, first_size, "*");
		return;
	}
	*c2=0;
	c2=right+1023;
	*c2--=0;
	for (c0=first+strlen(first)-1, c1=second+strlen(second)-1; *c0==*c1 && c0>=first && c1>=second; c0--, c1--) {
		*(c2--) = *c0;
	}
	c2++;
	if (c0<first) {
		// first = aba, second = caba
		assert(strcmp(c2, c1+1)==0);
		assert(left[0]==0);
		sprintf_s(SAFESTR2(first), "*%s", c2);
	} else if (c1<second) {
		// first = caba, second = aba
		assert(strcmp(c2, second)==0);
		assert(left[0]==0);
		sprintf_s(SAFESTR2(first), "*%s", c2);
	} else {
		// the match is not complete
		// first = XXcaba, second = XXdeaba
		sprintf_s(SAFESTR2(first), "%s*%s", left, c2);
	}
}

void destroyCommentInfo(CommentInfo *comment)
{
	if (!comment)
		return;

	if (comment->user_name)
		free(comment->user_name);

	if (comment->timestamp)
		free(comment->timestamp);

	if (comment->operation)
		free(comment->operation);

	if (comment->comments)
		free(comment->comments);

	free(comment);
}

static CommentInfo *parseCommentLine(FILE *f)
{
	CommentInfo *commentInfo;
	int startPos, endPos, len, field = 0, readOk;
	char c;
	char buf[50];

	commentInfo = malloc(sizeof(CommentInfo));
	commentInfo->branch = -1;
	commentInfo->version = -1;
	commentInfo->user_name = NULL;
	commentInfo->timestamp = NULL;
	commentInfo->operation = NULL;
	commentInfo->comments = NULL;

	startPos = ftell(f);
	for (;;)
	{
		endPos = ftell(f);
		readOk = fread(&c, sizeof(char), 1, f) > 0;
		if (!readOk || c == '\t' || c == '\n')
		{
			// field separator
			len = endPos - startPos;
			if (len)
			{
				fseek(f, startPos, SEEK_SET);
				switch (field)
				{
				xcase 0:
					fread(buf, sizeof(char), len, f);
					buf[len] = 0;
					commentInfo->branch = atoi(buf);
				xcase 1:
					fread(buf, sizeof(char), len, f);
					buf[len] = 0;
					commentInfo->version = atoi(buf);
				xcase 2:
					commentInfo->user_name = malloc(len+1);
					fread(commentInfo->user_name, sizeof(char), len, f);
					commentInfo->user_name[len] = 0;
				xcase 3:
					commentInfo->timestamp = malloc(len+1);
					fread(commentInfo->timestamp, sizeof(char), len, f);
					commentInfo->timestamp[len] = 0;
				xcase 4:
					commentInfo->operation = malloc(len+1);
					fread(commentInfo->operation, sizeof(char), len, f);
					commentInfo->operation[len] = 0;
				xcase 5:
					{
						char *tempComments = malloc(len+1);
						fread(tempComments, sizeof(char), len, f);
						tempComments[len] = 0;
						commentInfo->comments = strdup(unescapeString_unsafe(tempComments));
						free(tempComments);
					}
				}
			}

			// read in the field separator again
			fread(&c, sizeof(char), 1, f);
			startPos = ftell(f);

			if (!readOk || c == '\n')
			{
				// end of file or line
				break;
			}

			field++;
		}
	}
	
	if (commentInfo->branch == -1)
	{
		destroyCommentInfo(commentInfo);
		return NULL;
	}

	return commentInfo;
}

// pass in the memory and size given by fileAlloc
static CommentInfo * parseCommentLineMem(void *mem, int fileLen)
{
	CommentInfo *commentInfo;
	static void * curPos = NULL, *startOfField = NULL;
	int field = 0, len = 0;

// this is just to make these void* to char casts look less messy
#define CAST_CHAR(c)(*((char*)(c)))

	if ( curPos == NULL )
		curPos = mem;

	// there is nothing to parse
	if ( mem == NULL || fileLen <= 0 || (intptr_t)curPos - (intptr_t)mem >= fileLen )
	{
		curPos = NULL;
		return NULL;
	}

	commentInfo = malloc(sizeof(CommentInfo));
	commentInfo->branch = -1;
	commentInfo->version = -1;
	commentInfo->user_name = NULL;
	commentInfo->timestamp = NULL;
	commentInfo->operation = NULL;
	commentInfo->comments = NULL;

	while ( (intptr_t)curPos - (intptr_t)mem < fileLen && CAST_CHAR(curPos) != '\n' )
	{
		startOfField = curPos;
		while ( (intptr_t)curPos - (intptr_t)mem < fileLen && CAST_CHAR(curPos) != '\t' && CAST_CHAR(curPos) != '\n' ) 
			++(char*)curPos;

		switch(field++)
		{
		xcase 0:
			commentInfo->branch = atoi((char*)startOfField);
		xcase 1:
			commentInfo->version = atoi((char*)startOfField);
		xcase 2:
			len = (intptr_t)curPos - (intptr_t)startOfField;
			commentInfo->user_name = malloc( len+1 );
			strncpy_s( commentInfo->user_name, len+1, startOfField, len );
			commentInfo->user_name[len] = 0;
		xcase 3:
			len = (intptr_t)curPos - (intptr_t)startOfField;
			commentInfo->timestamp = malloc( len+1 );
			strncpy_s( commentInfo->timestamp, len+1, startOfField, len );
			commentInfo->timestamp[len] = 0;
		xcase 4:
			len = (intptr_t)curPos - (intptr_t)startOfField;
			commentInfo->operation = malloc( len+1 );
			strncpy_s( commentInfo->operation, len+1, startOfField, len );
			commentInfo->operation[len] = 0;
		xcase 5:
			len = (intptr_t)curPos - (intptr_t)startOfField;
			{
				char *tempComments = malloc(len+1);
				strncpy_s( tempComments, len+1, startOfField, len );
				// the way everything else works causes an extra newline to be read,
				// but we dont really want it, so we just chop it off
				tempComments[len-1] = 0;
				commentInfo->comments = strdup(unescapeString_unsafe(tempComments));
				free(tempComments);
			}
		}
		
		// move to the next field
		if ( CAST_CHAR(curPos) != '\n' )
			++(char*)curPos;
	}

	if ( (intptr_t)curPos - (intptr_t)mem >= fileLen )
		curPos = NULL;

	// clear out that last newline
	++(char*)curPos;

	// destroy bad commentInfo
	if (commentInfo->branch == -1)
	{
		destroyCommentInfo(commentInfo);
		return NULL;
	}

	return commentInfo;

#undef CAST_CHAR
}

void gimmeGetComment(GimmeDir *gimme_dir, const char *relpath, int branch, int version, CommentInfo **comment)
{
	char commentFileName[CRYPTIC_MAX_PATH];
	void * f;
	int len;
	CommentInfo *tempComment;

	makeCommentFileName(gimme_dir, relpath, SAFESTR(commentFileName), branch);

	f = fileAlloc( commentFileName, &len );

	while (tempComment = parseCommentLineMem(f, len))
	{
		if (tempComment->version == version && tempComment->branch == branch)
			*comment = tempComment;
		else
			destroyCommentInfo(tempComment);
	}

	fileFree(f);
}

void gimmeGetComments(GimmeDir *gimme_dir, const char *relpath, int branch, CommentInfo ***comments)
{
	char commentFileName[CRYPTIC_MAX_PATH];
	void *f;
	int len;
	CommentInfo *comment;

	makeCommentFileName(gimme_dir, relpath, SAFESTR(commentFileName), branch);

	eaDestroy(comments);

	f = fileAlloc( commentFileName, &len );

	while (comment = parseCommentLineMem(f, len))
	{
		eaPush(comments, comment);
	}

	fileFree(f);
}

void gimmeGetBatchInfo(GimmeDir *gimme_dir, const char *relpath, int branch, CommentInfo ***comments)
{
	char commentFileName[CRYPTIC_MAX_PATH];
	void *f;
	int len;
	CommentInfo *comment;

	makeBatchInfoFileName(gimme_dir, relpath, SAFESTR(commentFileName), branch);

	eaDestroy(comments);

	f = fileAlloc( commentFileName, &len );

	while (comment = parseCommentLineMem(f, len))
	{
		eaPush(comments, comment);
	}

	fileFree(f);
}

typedef void (*makeCommentFileNameCallback)(GimmeDir *gimme_dir, const char *relpath, char *buf, size_t buf_size, int branch);

void gimmeLogCommentsInternal(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, const char *comments, makeCommentFileNameCallback makeFname)
{
	__time32_t current_time = _time32(NULL);
	char commentFileName[CRYPTIC_MAX_PATH];
	char commentLockFileName[CRYPTIC_MAX_PATH];
	char timebuf[256];
	int lockfile;
	int version;
	GimmeNode *node;
	FILE *f;
	const char *effcomments = comments ? escapeString_unsafe(comments) : "";

	// Log comments to .comments file
	makeFname(gimme_dir, relpath, SAFESTR(commentFileName), gimme_dir->active_branch);
	// Make line to print
	_ctime32_s(SAFESTR(timebuf), &current_time);
	timebuf[strlen(timebuf)-1]=0; // chop off carriage return
	// Find what version we just added
	version=makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
	sprintf(line, "%d\t%d\t%s\t%s\t%s\t%s\n", gimme_dir->active_branch, version, gimmeGetUserName(), 
		timebuf, gimmeOperationToString(operation), effcomments);
	// Lock and write!
	sprintf(commentLockFileName, "%s.databaselock", commentFileName);
	lockfile = gimmeWaitToAcquireLock(commentLockFileName);
	f = fopen(commentFileName, "a");
	fwrite(line, 1, strlen(line), f);
	fclose(f);
	_close(lockfile);
	gimmeEnsureDeleteLock(commentLockFileName);
}

void gimmeLogComments(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, const char *comments)
{

	if (operation == GIMME_UNDO_CHECKOUT) // don't log these
		return;

	if (relpath) {
		gimmeLogCommentsInternal(gimme_dir, relpath, operation, comments, makeCommentFileName);
	}

	// Make merged comments for comments.txt
	current.gimme_dir = gimme_dir;
	current.comments = comments;
	current.operation = operation;
	strcpy(current.relpath, relpath?relpath:"(null)");
	if (last.gimme_dir && gimme_dir && 
		stricmp(current.comments, last.comments)==0 &&
		current.gimme_dir == last.gimme_dir &&
		current.operation == last.operation)
	{
		// Same as the last one!  Just merge the filenames
		mergeFileName(SAFESTR(last.relpath), current.relpath);
		count++;
	} else {
		// A new and different one, flush the old one!
		if (last.gimme_dir) {
			writeCommentLine();
		}
		last = current;
		count=1;
	}

}

void gimmeLogBatchInfo(GimmeDir *gimme_dir, const char *relpath, GIMMEOperation operation, const char *comments)
{

	if (operation == GIMME_UNDO_CHECKOUT) // don't log these
		return;

	if (relpath) {
		gimmeLogCommentsInternal(gimme_dir, relpath, operation, comments, makeBatchInfoFileName);
	}
}

//static int gimme_reg_version=1;
// Increment this when changing the table below to change everything, regardless of old values
static struct {
	char *key;
	char *valueName;
	char *value;
	bool bOnlyForNoShellExt;
} keys[] = {
	// Lots of old keys to be deleted
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Checkin\\command",
		"","%s -put \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Checkpoint\\command",
		"","%s -leavecheckedout -put \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Checkout\\command",
		"","%s -checkout \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Undo Checkout\\command",
		"","%s -undo \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Checkout Noedit\\command",
		"","%s -editor null \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Get Latest Version\\command",
		"","%s -glvfile \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Diff\\command",
		"","%s -diff \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Stat\\command",
		"","%s -stat \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\*\\shell\\Gimme Remove\\command",
		"","%s -remove \"%%1\"", true},

	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Get Latest Version\\command",
		"","%s -glvfold \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Checkout Folder\\command",
		"","%s -checkoutfold \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Checkin Folder\\command",
		"","%s -checkinfold \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Checkpoint Folder\\command",
		"","%s -leavecheckedout -checkinfold \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Remove Folder\\command",
		"","%s -rmfold \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Diff Folder\\command",
		"","%s -simulate -checkinfold \"%%1\"", true},
	{"HKEY_CLASSES_ROOT\\Folder\\shell\\Gimme Undo Checkout Folder\\command",
		"","%s -undofold \"%%1\"", true},

// Only this one needed with a working shell hook
	{"HKEY_LOCAL_MACHINE\\Software\\RaGEZONE\\Gimme",
		"Registered","%s", false},

};


static void gimmeRegisterWithPath(const char *gimme_exe_path)
{
	RegReader rr = createRegReader();
	int i;
	char buf[1024];
	char newbuf[1024];
	int force_change=1;
	bool using_new_shell_extension=false;
	char patch_shell_path[1024];

	// Shell extension doesn't work for unknown reasons on XP 64 (but works on Vista 64), disable it for now.
	if (IsUsingX64() && !IsUsingVista() && !gimme_state.shell_extension_even_on_x64)
		gimme_state.no_shell_extension = 1;

	strcpy(patch_shell_path, gimme_exe_path);
	getDirectoryName(patch_shell_path);

	// Register both 32-bit and 64-bit shell extensions on 64-bit windows
	strcat(patch_shell_path, "/PatchShellMenu.DLL");
	backSlashes(patch_shell_path);
	if (!gimme_state.no_shell_extension)
	{
		// Register shell extension
		// Only delete fields if this succeeds
		sprintf(newbuf, "regsvr32 /s \"%s\"", patch_shell_path);
		if (0==system(newbuf)) {
			using_new_shell_extension = true;
		} else {
			// Failed to register shell extension
		}
	} else {
		// Unregister
		sprintf(newbuf, "regsvr32 /s /u \"%s\"", patch_shell_path);
		system(newbuf);
	}
	if (IsUsingX64()) {
		changeFileExt(patch_shell_path, "X64.DLL", patch_shell_path);
		if (!gimme_state.no_shell_extension)
		{
			// Register shell extension
			// Only delete fields if this succeeds
			sprintf(newbuf, "regsvr32 /s \"%s\"", patch_shell_path);
			if (0==system(newbuf)) {
				using_new_shell_extension = true;
			} else {
				// Failed to register shell extension
			}
		} else {
			// Unregister
			sprintf(newbuf, "regsvr32 /s /u \"%s\"", patch_shell_path);
			system(newbuf);
		}
	}

	for (i=0; i<ARRAY_SIZE(keys); i++) {
		if (keys[i].bOnlyForNoShellExt && using_new_shell_extension) {
			char *s;
			strcpy(newbuf, keys[i].key);
			s = strstr(newbuf, "\\command");
			assert(s);
			*s = '\0';
			registryDeleteTree(newbuf);
		} else {
			initRegReader(rr, keys[i].key);
			if (!rrReadString(rr, keys[i].valueName, SAFESTR(buf))
				|| force_change)
			{
				sprintf(newbuf, FORMAT_OK(keys[i].value), gimme_exe_path);
				backSlashes(newbuf);
				if (stricmp(newbuf, buf)!=0) {
					printf("Updating registry key %s\n", keys[i].key);
					rrWriteString(rr, keys[i].valueName, newbuf);
				}
			}
			rrClose(rr);
		}
	}

	// PicaView causes gimme right click functionality to not work
	initRegReader(rr, "HKEY_CLASSES_ROOT\\*\\shellex\\ContextMenuHandlers");
	rrDeleteKey(rr, "PicaView");
	rrClose(rr); 

	winAddToPath("C:\\Night\\tools\\bin", true);

	if (using_new_shell_extension)
	{
		// Check for Switch To registry hooks
		bool hadSwitchTo=false;
		char *switchtokeys[] = {
			"HKEY_CLASSES_ROOT\\Folder\\shell\\Switch to CORE",
			"HKEY_CLASSES_ROOT\\Folder\\shell\\Switch to FightClub",
			"HKEY_CLASSES_ROOT\\Folder\\shell\\Switch to data",
			"HKEY_CLASSES_ROOT\\Folder\\shell\\Switch to src",
			"HKEY_CLASSES_ROOT\\Folder\\shell\\Switch to object_library",
			"HKEY_CLASSES_ROOT\\Folder\\shell\\Switch to texture_library",
		};
		for (i=0; i<ARRAY_SIZE(switchtokeys); i++) {
			char buf[1024];
			sprintf(buf, "%s\\command", switchtokeys[i]);
			initRegReader(rr, buf);
			if (rrReadString(rr, "", buf, ARRAY_SIZE(buf)))
			{
				hadSwitchTo = true;
			}
			rrClose(rr);
			registryDeleteTree(switchtokeys[i]);
		}

		if (hadSwitchTo || gimme_state.register_switch_to)
		{
			registryWriteInt("HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme", "ShowSwitchTo", 1);
		}
	}
}

void gimmeRegister(void)
{
	gimmeRegisterWithPath(getExecutableName());
}

void gimmeUtilCheckAutoRegister(void)
{
	char path[MAX_PATH];
	strcpy(path, getExecutableName());
	forwardSlashes(path);
	if (strStartsWith(path, "N:/"))
	{
		if (fileExists("C:/Night/tools/bin/GimmeDLL.dll") &&
			fileExists("C:/Night/tools/bin/Gimme.exe") &&
			fileExists("C:/Night/tools/bin/PatchShellMenu.dll"))
		{
			gimmeRegisterWithPath("C:/Night/tools/bin/Gimme.exe");
		}
	}
}

void gimmeUtilBranchStat(void)
{
	int i;
	gimmeLoadConfig();
	for (i=0; i<eaSize(&eaGimmeDirs); i++) {
		gimmeSetBranchConfigRoot(eaGimmeDirs[i]->lock_dir);
		printf("Gimme Dir #%d: [%s] [%s]\n", i, eaGimmeDirs[i]->local_dir, eaGimmeDirs[i]->lock_dir);
		printf("  Active branch: %d (%s)\n", gimmeGetBranchNumber(eaGimmeDirs[i]->local_dir), gimmeGetBranchName(gimmeGetBranchNumber(eaGimmeDirs[i]->local_dir)));
	}
}

char *gimmeDetectDiffProgram(void)
{
	char *diff_prog="CMD /C echo Error finding Diff program!";
	static char buffer[CRYPTIC_MAX_PATH];
	bool found_one=false;
	// Check to see if there's already one there (passed on command line?)
	if (gimme_state.editor && fileExists(gimme_state.editor)) {
		diff_prog = gimme_state.editor;
		found_one = true;
	}
	// Look for beyond compare
	if (!found_one) {
		RegReader rr = createRegReader();
		initRegReader(rr, "HKEY_CLASSES_ROOT\\BeyondCompare.Snapshot\\DefaultIcon");
		if (rrReadString(rr, "", buffer, ARRAY_SIZE(buffer))) {
			if (strEndsWith(buffer, ",0")) {
				*strrchr(buffer, ',')=0;
			}
			if (fileExists(buffer)) {
				diff_prog = buffer;
				found_one = true;
			}
		}
		rrClose(rr);
		destroyRegReader(rr);
	}
	if (!found_one && fileExists("C:/Program Files (x86)/Beyond Compare 3/BComp.EXE")) {
		diff_prog = "C:/Program Files (x86)/Beyond Compare 3/BComp.EXE";
		found_one = true;
	}
	if (!found_one && fileExists("C:/Program Files/Beyond Compare 2/BC2.EXE")) {
		diff_prog = "C:/Program Files/Beyond Compare 2/BC2.EXE";
		found_one = true;
	}
	if (!found_one && fileExists("C:/bin/diff.exe")) {
		showConsoleWindow();
		diff_prog = "C:/bin/diff.exe";
		found_one = true;
	}
	if (!found_one && fileExists("C:/WINDOWS/System32/fc.exe")) {
		showConsoleWindow();
		diff_prog = "C:/WINDOWS/System32/fc.exe";
		found_one = true;
	}
	return diff_prog;
}

typedef struct CheckOutList {
	char relpath[CRYPTIC_MAX_PATH];
	int undo_checkout;
	int newfile;
	int need_glv;
} CheckOutList;

static CheckOutList **colist=NULL;
static CheckOutList ***eaColist = &colist;

static const char *cur_myname=NULL;
static int searchCheckOutsByNode(GimmeDir *gimme_dir, const char *relpath, int quiet) {
	char *username, localfname[CRYPTIC_MAX_PATH];

	username = isLocked(gimme_dir, relpath);
	if (username && stricmp(username,cur_myname)==0)
	{
		int latestrev;
		FWStatType filestat;
		GimmeNode *node;
		int undo_checkout = 0;
		int exists;
		
		latestrev=makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
		makeLocalNameFromRel(gimme_dir, relpath, localfname);
		exists = 0==pststat(localfname, &filestat);
		if (exists) {
			CheckOutList *col = malloc(sizeof(CheckOutList));
			if (latestrev==-2) {
				// This file shouldn't exist on this branch, treat it like it's a new file
				col->newfile = 1;
				col->undo_checkout = 0;
				strcpy(col->relpath, relpath);
				eaPush(eaColist, col);
			} else {
				if (node->size == filestat.st_size) {
					// Same size, check the contents
					char dbname[CRYPTIC_MAX_PATH];
					makeDBNameFromNode(gimme_dir, node, dbname);

					if (node->timestamp == filestat.st_mtime || fileCompare(dbname, localfname)==0) {
						if (!gimme_state.leavecheckedout) {
							gimmeLog(LOG_STAGE, "\t%s (no changes detected, undoing checkout)", relpath);
							undo_checkout=1;
						}
					}
				}

				if (!undo_checkout) {
					gimmeLog(LOG_STAGE, "\t%s",relpath);
				}

				col->newfile = 0;
				col->undo_checkout = undo_checkout;
				strcpy(col->relpath, relpath);
				eaPush(eaColist, col);
			}
		} else {
			// Local file doesn't exist, we don't care, leave the old backup if it's there in case
			// this is because of an error on the last run
		}
	}
	return NO_ERROR;
}

static int searchNewFiles(GimmeDir *gimme_dir, char *localdir) {
	int		count,i;
	char	*relpath,**file_list;
	int		fret=NO_ERROR, rev;
	if (!gimme_state.quiet) gimmeLog(LOG_STAGE, "Scanning folders for new files...");
	file_list = fileScanDirFolders(localdir, FSF_NOHIDDEN | FSF_FILES | (gimme_state.no_underscore?0:FSF_UNDERSCORED));
    count = eaSize( &file_list );
	for(i=0;i<count;i++)
	{
		relpath = file_list[i];
		if (!strEndsWith(relpath, ".bak")) {
			GimmeNode *node=NULL;
			relpath = findRelPath(relpath, gimme_dir);
			rev = makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
			if (rev==-1 && gimmeCheckExclusion(gimme_dir, relpath)==ET_OK && !gimmeIsBinFile(gimme_dir, relpath)) {
				CheckOutList *col = malloc(sizeof(CheckOutList));
				// New file!
				gimmeLog(LOG_STAGE, "\t%s",relpath);
				col->newfile=1;
				strcpy(col->relpath, relpath);
				col->undo_checkout = 0;
				eaPush(eaColist, col);
			}
		}
	}
	fileScanDirFreeNames(file_list);
	return fret;
}

static char backup_path[CRYPTIC_MAX_PATH];
static int searchBackupsByNode(GimmeDir *gimme_dir, const char *relpath, int quiet) {
	char *username;

	username = isLocked(gimme_dir, relpath);
	if (username && stricmp(username,cur_myname)==0)
	{
		char backup_file[CRYPTIC_MAX_PATH];
		CheckOutList *col = calloc(sizeof(CheckOutList),1);

		sprintf(backup_file, "%s%s", backup_path, relpath);
		if (fileExists(backup_file)) {
			strcpy(col->relpath, relpath);
			eaPush(eaColist, col);
			gimmeLog(LOG_STAGE, "\t%s",relpath);
		} else {
			strcpy(col->relpath, relpath);
			col->need_glv = true;
			eaPush(eaColist, col);
			gimmeLog(LOG_STAGE, "\t%s (Checked out but not backed up, getting latest)",relpath);
		}
	}
	return NO_ERROR;
}

// Searches for files in the backups folder that do not exist in the database, and therefore must
//		be new files that were backed up
// Should/could also search for files in the data folder that are not in the database and need
//		to be deleted
static int searchOrphanFiles(GimmeDir *gimme_dir, char *localdir)
{
	int		count,i;
	char	*relpath,**file_list;
	char	buf[CRYPTIC_MAX_PATH];
	int		fret=NO_ERROR;
	if (!gimme_state.quiet) gimmeLog(LOG_STAGE, "Scanning %s for backed up new files...", backup_path);
	file_list = fileScanDirFolders(backup_path, FSF_NOHIDDEN | FSF_FILES | (gimme_state.no_underscore?0:FSF_UNDERSCORED));
    count = eaSize( &file_list );
	for(i=0;i<count;i++)
	{
		relpath = file_list[i];
		if (!strEndsWith(relpath, ".bak")) {
			GimmeNode *node;
			assert(strnicmp(relpath, backup_path, strlen(backup_path))==0);
			relpath = relpath+strlen(backup_path);
			sprintf(buf, "%s_versions", relpath);
			node = gimmeNodeFind(gimme_dir->database->root->contents, buf);
			if (node==NULL) {
				CheckOutList *col = calloc(sizeof(CheckOutList), 1);
				// New file!
				gimmeLog(LOG_STAGE, "\t%s",relpath);
				col->newfile=1;
				strcpy(col->relpath, relpath);
				eaPush(eaColist, col);
			} else {
				int j;
				bool found_it=false;
				// The file exists in the database, check to see if it's already in the checkouts to restore
				// if not, print an error (and delete?)
				for (j=0; j<eaSize(eaColist) && !found_it; j++) {
					if (stricmp(colist[j]->relpath, relpath)==0) {
						found_it=true;
					}
				}
				if (!found_it) {
					gimmeLog(LOG_WARN_HEAVY, "%s was backed up when you switched from this branch, but upon switching back, it appears you no longer have it checked out!\n\tThe file will be ignored, you should delete it from %s if it is not needed", relpath, file_list[i]);
				}
			}
		}
	}
	fileScanDirFreeNames(file_list);
	return fret;
}

LRESULT CALLBACK DlgConfirmProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);
static char *g_notes;
int gimmeDialogConfirm(char *notes) {
	g_notes = notes;
	return (int)DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_CONFIRM), NULL, (DLGPROC)DlgConfirmProc);
}

LRESULT CALLBACK DlgConfirmProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i; 
			char *estrFilenames = NULL;
			for ( i = 0; i < eaSize(&g_filenames); ++i )
			{
				estrConcat(&estrFilenames, g_filenames[i]->fname, (int)strlen(g_filenames[i]->fname));
				estrConcatChar(&estrFilenames, '\n');
			}
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), estrFilenames);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_NOTES), g_notes);

			flashWindow(hDlg);
			return FALSE;
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			EndDialog(hDlg, 1);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

int gimmeUtilSwitchBranch(const char *localpath, int newbranch)
{
	int oldbranch;
	char path[CRYPTIC_MAX_PATH];
	char notes[2048];
	GimmeDir *gimme_dir;
	int gimme_dir_num;
	int i;
	int quiet=0;
	char srcpath[CRYPTIC_MAX_PATH], destpath[CRYPTIC_MAX_PATH];
	int ret=NO_ERROR;

	if (gimmeCheckDisconnected())
		return GIMME_ERROR_NO_SC;

	if (gimme_state.simulate) {
		gimmeLog(LOG_FATAL, "-simulate and -switchbranch are not supported together");
		return GIMME_ERROR_CANCELED;
	}

	eaClearEx(eaColist, NULL);
	if (g_filenames)
	{
		//g_filenames[0]=0;
		for( i = 0; i < eaSize(&g_filenames); ++i )
			if ( g_filenames[i] ) free(g_filenames[i]);
		eaSetSize(&g_filenames, 0);
	}

	strcpy(path, localpath);

	forwardSlashes(path);
	if (!(gimme_dir=findGimmeDir(path))) {
		gimmeLog(LOG_FATAL, "Error: the root specified (%s) is not a gimme dir.", path);
		return GIMME_ERROR_NODIR;
	}
	gimme_dir_num = eaFind(&eaGimmeDirs, gimme_dir);
	gimmeGettingLatestOn(gimme_dir_num);
	oldbranch = gimmeGetBranchNumber(path);
	if (newbranch==oldbranch) {
		gimmeLog(LOG_STAGE, "%s: Already on branch %d (%s)", path, newbranch, gimmeGetBranchName(newbranch));
		return GIMME_NO_ERROR;
	}

	if (newbranch > gimmeGetMaxBranchNumber()) {
		gimmeLog(LOG_FATAL, "%s: You cannot switch to a branch (%d) newer than the maximum (%d)!", path, newbranch, gimmeGetMaxBranchNumber());
		return GIMME_ERROR_CANCELED;
	}

	cur_myname = gimmeGetUserName();
	gimmeLog(LOG_STAGE, "%s: Moving from branch %d (%s) to %d (%s) for user %s", path, oldbranch, gimmeGetBranchName(oldbranch), newbranch, gimmeGetBranchName(newbranch), cur_myname);

	// Get list of modified checked out files, new files, and unchanged checked out files
	gimme_state.cur_command_line_operation++;
	doByFold(path, quiet, searchCheckOutsByNode, NULL, "Searching for checked out files in folder \"%s%s\"...", searchNewFiles, 0);
	if (eaSize(eaColist)>0) {
		int any_new=0;
		int any_undo=0;
		for (i=0; i<eaSize(eaColist); i++) {
			char buf[4096];
			sprintf(buf, "%s%s%s\r\n", gimme_dir->local_dir, colist[i]->relpath, colist[i]->newfile?" (New file)":colist[i]->undo_checkout?" (no changes detected, undoing checkout)":"");
			if (colist[i]->newfile) any_new=1;
			if (colist[i]->undo_checkout) any_undo=1;
			appendFilename(buf, NULL);
		}
		// Confirm if files checked out
		strcpy(notes, "You currently have the files listed above checked out.  These files will be backed up so that when you return to this branch they will still be checked out.\r\n");
		if (any_undo) {
			strcat(notes, "Some of the files have not been changed, so their checkouts will be automatically undone before switching branches.\r\n");
		}
		if (any_new) {
			strcat(notes, "Some of the files are new files that do not exist in the database, they need to be removed in order to switch branches, so they will be backed up as well.\r\n");
		}
		strcat(notes, "If you are not planning on returning to this branch, please check in the above files before switching branches.\r\n");

		strcat(notes, "\r\nAre you sure you want to switch branches?");
		if (!gimmeDialogConfirm(notes)) {
			gimmeLog(LOG_FATAL, "Branch switching canceled by user request");
			return GIMME_ERROR_CANCELED;
		}

		if (any_undo) {
			gimmeLog(LOG_STAGE, "Undoing checkouts on unchanged files...");
			// undo checkout on any unchanged files
			for (i=0; i<eaSize(eaColist); i++) {
				if (colist[i]->undo_checkout) {
					if (NO_ERROR!=gimmeDoOperationRelPath(gimme_dir, colist[i]->relpath, GIMME_UNDO_CHECKOUT, quiet)) {
						// Something failed, flag the file to be backed up instead
						colist[i]->undo_checkout = 0;
					}
				}
			}
		}

		// Backup checked out files
		gimmeLog(LOG_STAGE, "Backing up checked out files...");
		sprintf(backup_path, "C:/game/branch.Backups/%d/%s", oldbranch, forwardSlashes(getDirString(gimme_dir->local_dir)));
		for (i=0; i<eaSize(eaColist); i++) {
			if (!colist[i]->undo_checkout) {
				// Move to backup folder
				assert(colist[i]->relpath[0]=='/');
				sprintf(srcpath, "%s%s", gimme_dir->local_dir, colist[i]->relpath);
				sprintf(destpath, "%s%s", backup_path, colist[i]->relpath);
				forwardSlashes(destpath);
				mkdirtree(destpath);
				backSlashes(srcpath);
				backSlashes(destpath);
				gimmeLog(LOG_STAGE, "Moving from %s to %s", srcpath, destpath);
				// Doing a move here might be more efficient, but if one fails in the middle, I'm afraid to do the cleanup work
				if (fileMove(srcpath, destpath)!=0) {
					gimmeLog(LOG_WARN_HEAVY, "Error backing up file (%s), branch switching cannot complete, your local files may be inconsistent!", srcpath);
					ret = GIMME_ERROR_COPY;
					goto end;
				}
				rmdirtree(srcpath); // prune the directory
			}
		}
	}
	// set new branch
	gimmeSetBranchNumber(localpath, newbranch);
	// get latest
	gimme_state.doing_branch_switch = 1;
	gimme_state.cur_command_line_operation++;
	getLatestVersionFolder(localpath, REV_BLEEDINGEDGE, 1);
	gimme_state.doing_branch_switch = 0;

	// Check for checked out files that were backed up, and new files that were backed up
	// Any checked out file must either be a) restored from backup, or b) forced to get latest
	// Check for local files that are not in the current branch to be removed (this was done above when making backups?)
	// Get list of modified checked out files, new files, and unchanged checked out files
	eaClearEx(eaColist, NULL);
	sprintf(backup_path, "C:/game/branch.Backups/%d/%s", newbranch, forwardSlashes(getDirString(gimme_dir->local_dir)));
	gimme_state.cur_command_line_operation++;
	doByFold(path, quiet, searchBackupsByNode, NULL, "Searching for checked out files in the new branch in folder \"%s%s\"...", searchOrphanFiles, 0);
	if (eaSize(eaColist)>0) {
		// Confirm and restore backups
		for (i=0; i<eaSize(eaColist); i++) {
			if (!colist[i]->need_glv) {
				// Move from backup folder
				assert(colist[i]->relpath[0]=='/');
				sprintf(srcpath, "%s%s", backup_path, colist[i]->relpath);
				sprintf(destpath, "%s%s", gimme_dir->local_dir, colist[i]->relpath);
				forwardSlashes(destpath);
				mkdirtree(destpath);
				backSlashes(srcpath);
				backSlashes(destpath);
				gimmeLog(LOG_STAGE, "Moving from %s to %s", srcpath, destpath);
				if (fileMove(srcpath, destpath)!=0) {
					gimmeLog(LOG_WARN_HEAVY, "Error restoring backing up file (%s)!", destpath);
					ret = GIMME_ERROR_COPY;
				}
				rmdirtree(srcpath); // prune the directory
			} else {
				// This file is checked out, but doesn't exist in the backups/ folder, so we want to 
				// force a get latest version, since the getLatestVersionFolder won't have overwritten
				// it (because it's checked out)
				gimme_state.force_get_latest = 1;
				gimmeDoOperationRelPath(gimme_dir, colist[i]->relpath, GIMME_GLV, 1);
				gimme_state.force_get_latest = 0;
			}
		}
	}

end:
	eaClearEx(eaColist, NULL);
	eaDestroy(eaColist);
	return ret;
}


static int gimmeBackupFile(const char *_fname)
{
	// This code copyied from fileMakeLocalBackup and tweaked to use n:\revisions\backups\...
	int time_to_keep = -1;
	// Make backup
	char backupPathBase[CRYPTIC_MAX_PATH];
	char backupPath[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char fname[CRYPTIC_MAX_PATH];
	char dir[CRYPTIC_MAX_PATH];
	int backup_num=0;
	struct _finddata_t fileinfo;
	int i;
	intptr_t handle;

	strcpy(fname, _fname);
	forwardSlashes(fname);

	sprintf(backupPathBase, "N:/revisions/BACKUPS/%s.", getFileName(fname));
	forwardSlashes(backupPathBase);

	strcpy(dir, backupPathBase);
	*(strrchr(dir, '/')+1)=0; // truncate before the file name
	mkdirtree(dir);

	sprintf(backupPath, "%s*", backupPathBase);
	handle = findfirst_SAFE(backupPath, &fileinfo);
	backup_num=0;
	if (handle==-1) {
		// Error or no file currently exists
		// will default to backup_num of 0, that's OK
	} else {
		// 1 or more files already exist
		do {
			// check to see if the backup number on this file is newer than backup_num
			i = atoi(strrchr(fileinfo.name, '.')+1);
			if (i>backup_num)
				backup_num = i;
			// check to see if file is old
			if (time_to_keep!=-1 && (fileinfo.time_write < time(NULL) - time_to_keep)) {
				// old file!  Delete it!
				sprintf(temp, "%s%s", dir, fileinfo.name);
				remove(temp);
			}
		} while( findnext_SAFE( handle, &fileinfo ) == 0 );
		backup_num++;
		_findclose(handle);

	}
	sprintf(backupPath, "%s%d", backupPathBase, backup_num);
	return copyFile(fname, backupPath);
}

static void forceGrabLock(const char *lockfilename)
{
	int handle=-1;
	_wsopen_s_UTF8(&handle, lockfilename, _O_CREAT | _O_EXCL | _O_WRONLY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
	if (handle>=0) {
		_write(handle, "GIMME LOCK FIX RUNNING\n", (int)strlen("GIMME LOCK FIX RUNNING\n"));
		_close(handle);
	}
}

#define ENSUREDELETE(filename) \
	if (0!=remove(filename)){ \
		gimmeLog(LOG_FATAL, "Error deleting file %s.  Repair operation canceled, possible network probelsm (restart smbd?)", filename); \
		filelog_printf("lockfix", "Error deleting file %s.  Repair operation canceled, possible network probelsm (restart smbd?)", filename); \
		return GIMME_ERROR_CANCELED; \
	}

#define ENSURERENAME(src, dest) \
	if (0!=rename(src, dest)){ \
		gimmeLog(LOG_FATAL, "Error renaming file %s to %s.  Repair operation canceled, possible network probelsm (restart smbd?)", src, dest); \
		filelog_printf("lockfix", "Error renaming file %s to %s.  Repair operation canceled, possible network probelsm (restart smbd?)", src, dest); \
		return GIMME_ERROR_CANCELED; \
	}


GimmeErrorValue gimmeUtilLockFix(bool auto_unlock)
{
	int i, j;
	char journal_lock[CRYPTIC_MAX_PATH], database_lock[CRYPTIC_MAX_PATH], journal_txt[CRYPTIC_MAX_PATH], database_txt[CRYPTIC_MAX_PATH], database_tmp[CRYPTIC_MAX_PATH], database_bak[CRYPTIC_MAX_PATH], journal_proc[CRYPTIC_MAX_PATH], journal_proc_temp[CRYPTIC_MAX_PATH];
	bool did_anything=false;

	logSetDir("gimme");
	filelog_printf("lockfix", "******************************************************");
	filelog_printf("lockfix", "Beginning -lockfix operation %s", auto_unlock?"[auto unlocking]":"");

	gimmeLoadConfig();
	for (i=0; i<eaSize(&eaGimmeDirs); i++) {
		bool locked=false;
		bool didsomething=false;
		bool duplicate=false;
		time_t last_changed=0;
		FWStatType sbuf;

		// Check to make sure this isn't a second copy of a previous branch
		for (j=0; j<i; j++) {
			if (stricmp(eaGimmeDirs[i]->lock_dir, eaGimmeDirs[j]->lock_dir)==0) {
				duplicate = true;
			}
		}
		if (duplicate)
			continue;

		printf("Analyzing %s...\n", eaGimmeDirs[i]->lock_dir);
		filelog_printf("lockfix", "Analyzing %s...\n", eaGimmeDirs[i]->lock_dir);
		sprintf(journal_lock, "%s/journal.txt.databaselock", eaGimmeDirs[i]->lock_dir);
		sprintf(database_lock, "%s/database.txt.databaselock", eaGimmeDirs[i]->lock_dir);
		sprintf(journal_txt, "%s/journal.txt", eaGimmeDirs[i]->lock_dir);
		sprintf(database_txt, "%s/database.txt.gz", eaGimmeDirs[i]->lock_dir);
		sprintf(database_tmp, "%s/database.txt.gz.tmp", eaGimmeDirs[i]->lock_dir);
		sprintf(database_bak, "%s/database.txt.gz.bak", eaGimmeDirs[i]->lock_dir);
		sprintf(journal_proc, "%s/journal.txt.processing", eaGimmeDirs[i]->lock_dir);
		sprintf(journal_proc_temp, "%s/journal.txt.processing_temp", eaGimmeDirs[i]->lock_dir);

		if (fileExists(database_lock)) {
			printf("  database.txt: LOCKED\n");
			filelog_printf("lockfix", "  database.txt: LOCKED");
			locked=true;
			pststat(database_lock, &sbuf);
			last_changed = sbuf.st_mtime;
		} else {
			printf("  database.txt: unlocked\n");
			filelog_printf("lockfix", "  database.txt: unlocked\n");
		}
		if (fileExists(journal_lock)) {
			printf("  journal.txt: LOCKED\n");
			filelog_printf("lockfix", "  journal.txt: LOCKED\n");
			locked=true;
			pststat(journal_lock, &sbuf);
			last_changed = sbuf.st_mtime;
		} else {
			printf("  journal.txt: unlocked\n");
			filelog_printf("lockfix", "  journal.txt: unlocked\n");
		}
		if (!locked)
			continue;

		if (locked) {
			// Check date/time of lock file
			last_changed = getServerTime(eaGimmeDirs[i]) - last_changed;
			if (last_changed > 6*60 && last_changed < 100*60*60) { // More than 100 hours?  Might be a negative number
				// Okay to auto-unlock
				printf("  Lock file is OLD: Should be safe to delete\n");
				filelog_printf("lockfix", "  Lock file is OLD: Should be safe to delete\n");
			} else {
				if (auto_unlock) {
					printf("  Not auto-unlocking because lockfile is fresh.\n");
					filelog_printf("lockfix", "  Not auto-unlocking because lockfile is fresh.\n");
					continue;
				} else {
					printf("  Lock file is FRESH, please wait and try this later, someone might actually be doing something\n");
					filelog_printf("lockfix", "  Lock file is FRESH, please wait and try this later, someone might actually be doing something\n");
				}
			}
		}

		if (!auto_unlock) {
			printf("Do you wish to fix %s?  Saying Yes to this when someone *actually* has this database locked because\n", eaGimmeDirs[i]->lock_dir);
			printf("  gimme is still running could be DISASTROUS.  [y/N]");
			if (!consoleYesNo()) {
				filelog_printf("lockfix", "User said NO");
				continue;
			} else {
				filelog_printf("lockfix", "User said YES");
			}
		}

		did_anything = true;

		filelog_printf("lockfix", "UNLOCKING %s", eaGimmeDirs[i]->lock_dir);

		if (!fileExists(database_lock)) {
			forceGrabLock(database_lock);
		} else {
			gimmeBackupFile(database_lock);
		}

		{
			// Try 10 times to see if the lock file disappears (in case people are still operating on the db)
			int i;
			for (i=0; i<10; i++) {
				if (!fileExists(journal_lock)) {
					// The file disappeared!
					int handle = gimmeWaitToAcquireLock(journal_lock);
					_close(handle);
					break;
				}
				Sleep(300);
			}
			if (i==10) {
				// File still there, must be locked because of a crash!
				gimmeBackupFile(journal_lock);
				forceGrabLock(journal_lock);
			}
		}

		// Let's analyze! the state of journal.txt
		if (fileExists(journal_proc_temp)) {
			char temp[CRYPTIC_MAX_PATH];
			int len, len2;
			char *mem, *mem2;
			FILE *f;
			gimmeLog(LOG_STAGE, "  %s exists, Gimme was probably killed while writing the database, restoring .bak, appending the journal.* together, and unlocking...", journal_proc_temp);
			filelog_printf("lockfix", "  %s exists, Gimme was probably killed while writing the database, restoring .bak, appending the journal.* together, and unlocking...", journal_proc_temp);
			// Make a new file containing journal_temp, journal_proc, journal_txt

			// There's a temp file, it's probably the results of journal_proc + journal_txt
			// but the journal_txt may have been removed since then, so we need to keep the data
			gimmeBackupFile(journal_proc_temp);

			sprintf(temp, "%sjournal.txt.lockfixtemp", eaGimmeDirs[i]->lock_dir);
			f = fopen(temp, "wb");
			if (!f) {
				gimmeLog(LOG_FATAL, "Could not open temp file %s for writing, probably network error!  Aborting.", temp);
				filelog_printf("lockfix", "Could not open temp file %s for writing, probably network error!  Aborting.", temp);
				if (auto_unlock) gimme_state.pause = 0;
				return GIMME_ERROR_CANCELED;
			}

			if (fileExists(journal_txt)) {
				// Must have been in the middle of copying .proc+.txt>.proc_temp

				mem2 = fileAlloc(journal_proc_temp, &len2);
				if (fileExists(journal_proc)) {
					gimmeBackupFile(journal_proc);
					mem = fileAlloc(journal_proc, &len);
					// If .proc is larger than .proc_temp, then it must contain more data then .proc_temp
					// so it has to go *after* .proc_temp
					if (len > len2) {
						fwrite(mem2, 1, len2, f);
						fwrite("\r\n", 1, 2, f);
						fwrite(mem, 1, len, f);
						fwrite("\r\n", 1, 2, f);
					} else {
						// .proc_temp is bigger, it just contain more than .proc
						fwrite(mem, 1, len, f);
						fwrite("\r\n", 1, 2, f);
						fwrite(mem2, 1, len2, f);
						fwrite("\r\n", 1, 2, f);
					}
					fileFree(mem);
				} else {
					fwrite(mem2, 1, len2, f);
					fwrite("\r\n", 1, 2, f);
				}
				fileFree(mem2);
				// journal.txt is always the most recent changes!
				gimmeBackupFile(journal_txt);
				mem = fileAlloc(journal_txt, &len);
				fwrite(mem, 1, len, f);
				fwrite("\r\n", 1, 2, f);
				fileFree(mem);
			} else {
				// There is no journal.txt, it must have already been copied in it's entirety to .proc_temp, put that last!
				if (fileExists(journal_proc)) {
					gimmeBackupFile(journal_proc);
					mem = fileAlloc(journal_proc, &len);
					fwrite(mem, 1, len, f);
					fileFree(mem);
				}
				mem = fileAlloc(journal_proc_temp, &len);
				fwrite(mem, 1, len, f);
				fileFree(mem);
			}
			fclose(f);

			if (fileExists(journal_txt)) {
				if (0!=fileRenameToBak(journal_txt)) {
					gimmeLog(LOG_FATAL, "Could not rename old %s file, aborting.  Probably network error.", journal_txt);
					filelog_printf("lockfix", "Could not rename old %s file, aborting.  Probably network error.", journal_txt);
					if (auto_unlock) gimme_state.pause = 0;
					return GIMME_ERROR_CANCELED;
				}
			}
			ENSURERENAME(temp, journal_txt);
			if (fileExists(journal_proc)) {
				ENSUREDELETE(journal_proc);
			}
			if (fileExists(journal_proc_temp)) {
				ENSUREDELETE(journal_proc_temp);
			}
			gimmeLog(LOG_STAGE, "  journal.txt.processing/journal.txt merged.  If errors still occur, then there is probably a data error, backup the old journal.* and remove them.");
			filelog_printf("lockfix", "  journal.txt.processing/journal.txt merged.  If errors still occur, then there is probably a data error, backup the old journal.* and remove them.");
			didsomething = true;
		} else {
			gimmeLog(LOG_STAGE, "  journal.txt looks fine (journal.txt.processing_temp doesn't exist)");
			filelog_printf("lockfix", "  journal.txt looks fine (journal.txt.processing_temp doesn't exist)");
		}
		ENSUREDELETE(journal_lock);

		// Let's analyze! the state of database.txt
		if (fileExists(database_tmp)) {
			U32 size0 = fileSize(database_tmp);

			Sleep(2000);

			if (size0!=fileSize(database_tmp)) {
				gimmeLog(LOG_FATAL, "%s exists and it is currently being written to (file has grown in the last 2 seconds).", database_tmp);
				gimmeLog(LOG_FATAL, "  Someone *is* currently running gimme, doing anything with the database now would be");
				gimmeLog(LOG_FATAL, "  DISASTEROUS.  You are a bad, bad person.  Wait for the operation to finish before");
				gimmeLog(LOG_FATAL, "  jumping to conclusions about things being broken.");
				filelog_printf("lockfix", "  FILE WAS BEING WRITTEN TO!  last_changed = %d, increase this!.", last_changed);
				if (auto_unlock) gimme_state.pause = 0;
				return GIMME_ERROR_CANCELED;
			}

			gimmeLog(LOG_STAGE, "  %s exists, Gimme was probably killed while writing the database, restoring .bak and unlocking...", database_tmp);
			filelog_printf("lockfix", "  %s exists, Gimme was probably killed while writing the database, restoring .bak and unlocking...", database_tmp);
			if (fileExists(database_txt)) {
				gimmeLog(LOG_FATAL, "  %s EXISTS!  Inconsistent state, restoring .bak and unlocking...", database_txt);
				filelog_printf("lockfix", "  %s EXISTS!  Inconsistent state, restoring .bak and unlocking...", database_txt);
				if (NO_ERROR!=gimmeBackupFile(database_txt)) {
					gimmeLog(LOG_FATAL, "Could not backup file (%s), canceling process!", database_tmp);
					filelog_printf("lockfix", "Could not backup file (%s), canceling process!", database_tmp);
					if (auto_unlock) gimme_state.pause = 0;
					return GIMME_ERROR_CANCELED;
				}
				ENSUREDELETE(database_txt);
			}
			if (NO_ERROR!=gimmeBackupFile(database_tmp)) {
				gimmeLog(LOG_FATAL, "Could not backup file (%s), canceling process!", database_tmp);
				filelog_printf("lockfix", "Could not backup file (%s), canceling process!", database_tmp);
				if (auto_unlock) gimme_state.pause = 0;
				return GIMME_ERROR_CANCELED;
			}
			ENSUREDELETE(database_tmp);
			gimmeLog(LOG_STAGE, "Restoring .bak...");
			filelog_printf("lockfix", "Restoring .bak...");
			ENSURERENAME(database_bak, database_txt);
			gimmeLog(LOG_STAGE, "Removing lock and it should be good to go!");
			filelog_printf("lockfix", "Removing lock and it should be good to go!");
			ENSUREDELETE(database_lock);
			didsomething = true;
		} else if (!fileExists(database_txt)) {
			gimmeLog(LOG_STAGE, "  %s doesn't exist!  Restoring .bak version.", database_txt);
			filelog_printf("lockfix", "  %s doesn't exist!  Restoring .bak version.", database_txt);
			if (!fileExists(database_bak)) {
				gimmeLog(LOG_FATAL, "  %s doesn't exist!  No database can be restored.", database_bak);
				filelog_printf("lockfix", "  %s doesn't exist!  No database can be restored.", database_bak);
				if (auto_unlock) gimme_state.pause = 0;
				return GIMME_ERROR_CANCELED;
			}
			if (0!=copyFile(database_bak, database_txt)) {
				gimmeLog(LOG_FATAL, "  Error copying file!  Probably nextwork problems.  Aborting.");
				filelog_printf("lockfix", "  Error copying file!  Probably nextwork problems.  Aborting.");
				if (auto_unlock) gimme_state.pause = 0;
				return GIMME_ERROR_CANCELED;
			}
			ENSUREDELETE(database_lock);
			didsomething = true;			
		} else {
			gimmeLog(LOG_STAGE, "  database.txt looks fine (database.txt.tmp doesn't exist)");
			filelog_printf("lockfix", "  database.txt looks fine (database.txt.tmp doesn't exist)");
			didsomething = true;
			ENSUREDELETE(database_lock);
		}

		if (!didsomething) {
			gimmeLog(LOG_FATAL, "  I don't know how to repair based on what files are locked, a manual fix will probably be needed");
			filelog_printf("lockfix", "  I don't know how to repair based on what files are locked, a manual fix will probably be needed");
		}
	}
	gimme_state.pause=1;
	gimmeLog(LOG_STAGE, "Done checking for problems.");
	filelog_printf("lockfix", "Done checking for problems.");
	if (auto_unlock)
		gimme_state.pause = 0;
	return GIMME_NO_ERROR;
}

int gimmeIsNodeModifiedAfterLinkBreak(GimmeNode *node, int branch)
{
	GimmeNode *walk;
	// Look for a node at branch+1, and if any were checked in before this node, it's been modified after break
	for (walk=node; walk; walk=walk->next) {
		if (walk->branch == branch+1) {
			if (walk->checkintime < node->checkintime && walk->timestamp!=node->timestamp)
				return 1;
		}
	}
	// Look backwards too
	for (walk=node; walk; walk=walk->prev) {
		if (walk->branch == branch+1) {
			if (walk->checkintime < node->checkintime && walk->timestamp!=node->timestamp)
				return 1;
		}
	}
	return 0;
}

static int branch_to_report;
static int branchReportByNode(GimmeDir *gimme_dir, const char *relpath, int quiet)
{
	int highver; // Highest version in branch
	int latestver; // Highest version in branch+1
	char lockdir[CRYPTIC_MAX_PATH];
	char highFile[CRYPTIC_MAX_PATH];
	char latestFile[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	GimmeNode *nodeLatest;
	int branch = branch_to_report;

	sprintf(lockdir,"%s_versions/", relpath);

	highver = getHighestVersion(gimme_dir, lockdir, &node, branch, relpath);
	if (highver!=-1 && node) {
		// It exists on this branch
		if (!gimmeIsNodeLinkBroken(node, branch)) {
			// It's still linked, all is well
		} else {
			// The link has been broken, see if this node was checked in *after* the first link break occurred
			if (gimmeIsNodeModifiedAfterLinkBreak(node, branch)) {
				// Check to see if the latest in branch+1 is the same file as the latest in branch
				latestver = getHighestVersion(gimme_dir, lockdir, &nodeLatest, branch+1, relpath);
				makeDBNameFromNode(gimme_dir, nodeLatest, latestFile);
				makeDBNameFromNode(gimme_dir, node, highFile);
				if (fileCompare(latestFile, highFile)==0) {
					// Files are identical, this is virtually the same as them being linked
				} else {
					if (gimmeFileIsDeletedFile(node->name)) {
						gimmeLog(LOG_STAGE, "File %s was DELETED by %s after link break", relpath, gimmeGetAuthorFromNode(node));
					} else if (node->timestamp < nodeLatest->timestamp) {
						gimmeLog(LOG_STAGE, "File %s was modified by %s after link break and then modified again on branch %d", relpath, gimmeGetAuthorFromNode(node), branch+1);
					} else {
						gimmeLog(LOG_STAGE, "File %s was modified by %s after link break", relpath, gimmeGetAuthorFromNode(node));
					}
				}
			}
		}
	}
	return NO_ERROR;
}


int gimmeUtilBranchReport(int gimme_dir_num, int branchToReport)
{
	int ret;

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: branch report called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: branch report called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}
	gimmeSetBranchConfigRoot(eaGimmeDirs[gimme_dir_num]->lock_dir);
	if (branchToReport == gimmeGetMaxBranchNumber()) {
		gimmeLog(LOG_FATAL, "Error: branch report called on the latest branch, this won't do anything!");
		return GIMME_ERROR_NODIR;
	}

	gimmeLog(LOG_STAGE, "Reporting on all files in %s, in branch #%d", eaGimmeDirs[gimme_dir_num]->lock_dir, branchToReport);
	branch_to_report = branchToReport;
	ret = doByFold(eaGimmeDirs[gimme_dir_num]->local_dir, 0, branchReportByNode, NULL, "Scanning for files modified after link break...", NULL, 0);
	return ret;
}

void gimmeReconnect(void)
{
	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: reconnect called when no source control folders are configured");
		return;
	}
	if (!fileExists("C:\\gimme_disconnected.txt")) {
		gimmeLog(LOG_FATAL, "No disconnected transactions have been logged, nothing to be done.");
		return;
	} else {
		int len;
		char *data = fileAlloc("c:\\gimme_disconnected.txt", &len);
		char *data_last=NULL;
		char *line;
		int errors=0;
		int linenum=1;

		if (!gimme_state.simulate) {
			fileRenameToBak("C:\\gimme_disconnected.txt");
		}

		line = strtok_r(data, "\n", &data_last);
		while (line) {
			char *tok, *last=NULL;
			char *linetemp = strdup(line);
			bool errors_this_line=false;
			while (line[0]=='\r' || line[0]=='\n')
				line++;
			tok = strtok_r(line, "\t", &last);
			if (strlen(tok) < 19) {
				errors = 1;
				errors_this_line = true;
				gimmeLog(LOG_FATAL, "Unrecognized line %d: \"%s\"", linenum, linetemp);
			} else {
				tok += 18;
				if (stricmp(tok, "CHECKOUT")==0) {
					char *fullpath = strtok_r(NULL, "\t", &last);
					char *scheckout_time = strtok_r(NULL, "\t", &last);
					char *sprevmod_time = strtok_r(NULL, "\t", &last);
					if (!fullpath || !scheckout_time || !sprevmod_time) {
						errors = true;
						errors_this_line = true;
						gimmeLog(LOG_FATAL, "Missing parameter on line %d: \"%s\"", linenum, linetemp);
					} else {
						// Verify they had the latest version, then run checkout operation!
						time_t checkout_time = atoi(scheckout_time);
						time_t prevmod_time = atoi(sprevmod_time);
						GimmeDir *gimme_dir = findGimmeDir(fullpath);
						// Load the appropriate database
						if (!gimme_dir) {
							errors = 1;
							errors_this_line = true;
							gimmeLog(LOG_FATAL, "Error finding source control folder to match %s", fullpath);
						} else {
							if (NO_ERROR!=gimmeDirDatabaseLoad(gimme_dir, fullpath)) {
								errors = 1;
								errors_this_line = true;
								gimmeLog(LOG_FATAL, "Error loading database for \"%s\"", fullpath);
							} else {
								gimmeDirDatabaseClose(gimme_dir);
							}
						}
						if (!errors_this_line) {
							// So far, so good, do checks now
							GimmeNode *node=NULL;
							char *relpath = findRelPath(fullpath, gimme_dir);
							char *username = isLocked(gimme_dir, relpath);
							int latestrev=makeDBName(gimme_dir, relpath, REV_BLEEDINGEDGE, &node);
							if (!node) {
								gimmeLog(LOG_WARN_LIGHT, "File \"%s\" appears to be a new file, ignoring", fullpath);
							} else if (username && stricmp(username, gimmeGetUserName())==0) {
								gimmeLog(LOG_WARN_LIGHT, "File \"%s\" was already checked out by you", fullpath);
							} else if (username && stricmp(username, gimmeGetUserName())!=0) {
								gimmeLog(LOG_FATAL, "File \"%s\" is checked out by someone else (%s), cannot check it out", fullpath, username);
							} else {
								bool b=  (prevmod_time >= node->timestamp-IGNORE_TIMEDIFF) &&
									(prevmod_time <= node->timestamp+IGNORE_TIMEDIFF); 
								// Not locked by anyone
								if (b) {
									// Timestamps match, yay!
									if (NO_ERROR!=makeLock(gimme_dir, relpath))
									{
										gimmeLog(LOG_FATAL, "Error checking out file that was checked out disconnected: %s", fullpath);
										errors = true;
										errors_this_line = true;
									} else {
										gimmeLog(LOG_WARN_LIGHT, "Checked out file (while keeping local offline changes): %s", fullpath);
									}
								} else {
									gimmeLog(LOG_FATAL, "File was modified by someone else after disconnect: %s\n   Manual merge is required.\n  If you wish to lose changes to this file, simply delete c:\\gimme_disconnected.txt\n", fullpath);
									errors = true;
									errors_this_line = true;
								}
							}
						}
					}
				} else {
					errors = 1;
					errors_this_line = true;
					gimmeLog(LOG_FATAL, "Unrecognized token: %s on line %d", tok, linenum);
				}
			}
			if (errors_this_line && !gimme_state.simulate)
				gimmeOfflineTransactionLog(false, "%s", linetemp);
			line = strtok_r(NULL, "\n", &data_last);
			linenum++;
			free(linetemp);
		}
		fileFree(data);
	}	
}

int gimme_sync_no_checkout=0;
int gimme_sync_only_newer=0;
void gimmeUtilSyncNoCheckout(void)
{
	gimme_sync_no_checkout=1;
}

void gimmeUtilSyncOnlyNewer(void)
{
	gimme_sync_only_newer=1;
}


static int syncByNode(GimmeDir *gimme_dir, const char *relpath, int quiet)
{
	char lockdir[CRYPTIC_MAX_PATH];
	GimmeNode *node;
	char localfname[CRYPTIC_MAX_PATH];
	int ver;
	int ret;

	sprintf(lockdir,"%s_versions/", relpath);

	ver = getHighestVersion(gimme_dir, lockdir, &node, gimme_dir->active_branch, relpath);
	if (ver!=-1 && node) {
		FWStatType sbuf;
		makeLocalNameFromRel(gimme_dir, relpath, localfname);
		pststat(localfname, &sbuf);
		if (!gimme_sync_no_checkout) {
			// check if local version is writeable, if so, check out
			if (sbuf.st_mode & _S_IWRITE) {
				if (NO_ERROR!=(ret=makeLock(gimme_dir, relpath)) && !gimme_state.simulate)
				{
					gimmeLog(LOG_FATAL, "Error locking file that appears to be checked out by you: %s", relpath);
				} else {
					gimmeLog(LOG_INFO, "Checked out file: %s", relpath);
				}
			}
		}
		// check if local version is at least as new as the latest database version, if so:
		if (sbuf.st_mtime > node->timestamp) {
			// add if newer
			gimmeLog(LOG_INFO, "Your file is newer: %s", relpath);
			gimmeDoOperationRelPath(gimme_dir, relpath, GIMME_FORCECHECKIN, quiet);
		} else if (sbuf.st_mtime == node->timestamp && !gimme_sync_only_newer) {
			char dbname[CRYPTIC_MAX_PATH];
			// make remote file if the same
			makeDBNameFromNode(gimme_dir, node, dbname);
			if (!fileExists(dbname)) {
				gimmeLog(LOG_INFO, "Updating missing version file: %s", dbname);
				mkdirtree(dbname);
				GIMME_CRITICAL_START;
				if (NO_ERROR!=(ret=copyFile(localfname,dbname))) {
					remove(dbname);
				}
				GIMME_CRITICAL_END;
			}
		}
	} else {
		// file not in database
		gimmeLog(LOG_INFO, "File not in database: %s", relpath);
	}

	return NO_ERROR;
}

int gimmeUtilSync(int gimme_dir_num)
{
	int ret;

	gimmeLoadConfig();
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "Error: sync called when no source control folders are configured");
		return GIMME_ERROR_NODIR;
	}
	if (gimme_dir_num>=eaSize(&eaGimmeDirs)) {
		gimmeLog(LOG_FATAL, "Error: sync called with a database number (%d) out of range (0..%d)", gimme_dir_num, eaSize(&eaGimmeDirs)-1);
		return GIMME_ERROR_NODIR;
	}

	gimmeLog(LOG_STAGE, "Syncing on all files in %s", eaGimmeDirs[gimme_dir_num]->lock_dir);
	gimme_state.doing_sync = 1;
	ret = doByFold(eaGimmeDirs[gimme_dir_num]->local_dir, 0, syncByNode, NULL, "Syncing...", NULL, 0);
	gimme_state.doing_sync = 0;
	return ret;
}

static bool fileWatcherWatches(const char *path)
{
	int i;
	char *defaults[] = {
		"c:\\game", "c:\\gamefix"
	};
	char *data;
	char *walk;
	char *argv[10];
	int argc;
	int size;
	char pathtest[CRYPTIC_MAX_PATH];
	strcpy(pathtest, path);
	backSlashes(pathtest);
	data = fileAlloc("C:/filewatch.txt", &size);
	if (!data) {
		for (i=0; i<ARRAY_SIZE(defaults); i++) 
			if (strStartsWith(pathtest, defaults[i]))
				return true;
		return false;
	}
	argc = tokenize_line(data, argv, &walk);
	while (walk) {
		if (argc) {
			backSlashes(argv[0]);
			if (strStartsWith(pathtest, argv[0])) {
				free(data);
				return true;
			}
		}
		argc = tokenize_line(walk, argv, &walk);
	}
	free(data);
	return false;
}

static void fileWatcherWatch(const char *in_path)
{
	// If making a new filewatch.txt and path != game or gamefix, query if they want to remove CoH
	char *data;
	char newdata[10*1024];
	char path[CRYPTIC_MAX_PATH];
	char **entries=NULL;
	int size;
	bool pathAlreadyInList=false;
	char *walk;
	char *argv[10];
	int argc;
	int i;
	FILE *file;

	strcpy(path, in_path);
	backSlashes(path);
	if (strEndsWith(path, "\\"))
		path[strlen(path)-1]='\0';
	
	data = fileAlloc("c:/filewatch.txt", &size);
	if (!data) {
		strcpy(newdata, "C:\\game\\data\nC:\\game\\tools\nC:\\game\\docs\nC:\\game\\src\nC:\\gamefix\\data\nC:\\gamefix\\tools\nC:\\gamefix\\docs\nC:\\gamefix\\src\nC:\\Cryptic\nC:\\Core\\data\nC:\\Core\\tools\nC:\\Core\\src\n");
	} else {
		strcpy(newdata, data);
		free(data);
	}

	// Now, search for this path in the list, and if it's not there add it,
	// additionally, remove all CoH paths if desired
	argc = tokenize_line(newdata, argv, &walk);
	while (walk) {
		if (argc) {
			bool thisAlreadyInList=false;
			backSlashes(argv[0]);
			if (strEndsWith(argv[0], "\\"))
				argv[0][strlen(argv[0])-1]='\0';
			if (strStartsWith(path, argv[0])) {
				pathAlreadyInList = true;
			}
			for (i=eaSize(&entries)-1; i>=0; i--) {
				if (stricmp(argv[0], entries[i])==0)
					thisAlreadyInList = true;
			}
			if (thisAlreadyInList)
			{
				// Don't add it
			} else {
				eaPush(&entries, argv[0]);
			}
		}
		argc = tokenize_line(walk, argv, &walk);
	}
	if (!pathAlreadyInList)
		eaPush(&entries, path);
	file = fopen("C:/filewatch.txt", "wt");
	if (file) {
		for (i=0; i<eaSize(&entries); i++) {
			char buf[CRYPTIC_MAX_PATH];
			strcpy(buf, entries[i]);
			backSlashes(buf);
			fprintf(file, "%s\n", buf);
		}
		fclose(file);
	}
}

static bool hasBeenWarnedAbout(const char *path)
{
	// Checks if they have been warned about it
	char key[256];
	char *s;
	sprintf(key, "HasBeenWarned%s", path);
	forwardSlashes(key);
	for (s = key; *s; s++) {
		if (*s=='/') {
			strcpy_unsafe(s, s+1);
		}
	}
	return regGetAppInt(key, 0);
}

static void flagHasBeenWarnedAbout(const char *path)
{
	char key[256];
	char *s;
	sprintf(key, "HasBeenWarned%s", path);
	forwardSlashes(key);
	for (s = key; *s; s++) {
		if (*s=='/') {
			strcpy_unsafe(s, s+1);
		}
	}
	regPutAppInt(key, 1);
}

void checkFileWatcherConsistency(const char *folder)
{
	// Get project name
	GimmeDir *gimme_dir;
	char buf[1024];
	int ret;

 	if (!regGetAppInt("HasFixedFileWatchTXT", 0)) {
 		regPutAppInt("HasFixedFileWatchTXT", 1);
 		fileWatcherWatch("C:\\Cryptic");
 	}

	if (gimme_state.ignore_errors) // Absolutely no pop-ups with this flag on
		return;

	gimme_dir = findGimmeDir(folder);

	if (!gimme_dir)
		return;

	if (fileWatcherWatches(gimme_dir->local_dir)) {
		return;
	}
	if (hasBeenWarnedAbout(gimme_dir->local_dir)) {
		return;
	}
	sprintf(buf, "You are getting latest on %s, but FileWatcher is not configured to watch this folder.  Would you like this to be added to FileWatcher for you?\n\n(FileWatcher speeds up the loading speeds on folders it watches, but takes time at system startup to scan these folders.  Only projects you are working on should be watched by FileWatcher.)", gimme_dir->local_dir);
	if (strstriConst(folder, "Cryptic") || strstriConst(folder, "Core")) {
		ret = IDYES;
	} else {
		ret = MessageBox_UTF8(compatibleGetConsoleWindow(), buf, "FileWatcher update", MB_YESNOCANCEL);
	}
	switch (ret) {
		xcase IDYES:
			// do it
			fileWatcherWatch(gimme_dir->local_dir);
		xcase IDNO:
			flagHasBeenWarnedAbout(gimme_dir->local_dir);
		xcase IDCANCEL:
			// Nothing

			break;
	}
}

void gimmeBackupDatabases(void)
{
	int i;
	int j;
	gimmeLoadConfig();
	for (i=0; i<eaSize(&eaGimmeDirs); i++) {
		GimmeDir *gimme_dir = eaGimmeDirs[i];
		char tempfilename[CRYPTIC_MAX_PATH];
		char *dbroot = gimme_dir->lock_dir;
		int db_lockfile;
		bool duplicate=false;
		for (j=0; j<i; j++) {
			if (stricmp(gimme_dir->lock_dir, eaGimmeDirs[j]->lock_dir)==0) {
				duplicate = true;
			}
		}
		if (duplicate)
			continue;

		sprintf_s(SAFESTR(tempfilename), "%s/database.txt.databaselock", dbroot);
		db_lockfile = gimmeWaitToAcquireLock(tempfilename);

		sprintf_s(SAFESTR(tempfilename), "%s/database.txt.gz", dbroot);
		if (!fileExists(tempfilename))
			sprintf_s(SAFESTR(tempfilename), "%s/database.txt", dbroot);
		printf("Backing up %s...\n", tempfilename);
		fileMakeLocalBackup(tempfilename, 3600*24*14);

		sprintf_s(SAFESTR(tempfilename), "%s/database.txt.databaselock", dbroot);
		gimmeUnaquireAndDeleteLock(db_lockfile, tempfilename);
	}
}

static ThreadAgnosticMutex g_cache_mutex;
void gimmeCacheLock(void)
{
	assert(!g_cache_mutex);
	g_cache_mutex = acquireThreadAgnosticMutex("GimmeFileAllocCache");
}

void gimmeCacheUnlock(void)
{
	releaseThreadAgnosticMutex(g_cache_mutex);
	g_cache_mutex = NULL;
}

time_t fileLastChangedPST(const char *path)
{
	FWStatType sbuf;
	pststat(path, &sbuf);
	return sbuf.st_mtime;
}

void gimmeGetCachedFilename(const char *path, char *dest, size_t dest_size)
{
	char *s;
	char drive[MAX_PATH];
	char cache_path[MAX_PATH];
	char deleted_path[MAX_PATH];
	bool needToCopy;

	if (gimme_state.noRemoteFileCache) {
		strcpy_s(dest, dest_size, path);
		return;
	}

	// Determine if it's a network file and in need of caching
	strcpy(drive, path);
	s = strchr(drive, ':');
	if (!s) {
		forwardSlashes(drive);
		if (strStartsWith(drive, "//")) {
			// UNC path
			sprintf(cache_path, "C:/temp/gimmeNetworkCache/%s", drive+2);
			sprintf(deleted_path, "%s.deleted", cache_path);
		} else {
			strcpy_s(dest, dest_size, path);
			return;
		}
	} else {
		strcpy_s(s, ARRAY_SIZE(drive) - (s - drive), ":\\");
		if (GetDriveType_UTF8(drive) != DRIVE_REMOTE) {
			strcpy_s(dest, dest_size, path);
			return;
		}

		// Build local cache path
		drive[strlen(drive)-2] = '\0';
		sprintf(cache_path, "C:/temp/gimmeNetworkCache/%s/%s", drive, path + strlen(drive)+2);
		sprintf(deleted_path, "%s.deleted", cache_path);
	}

	if (gimme_state.updateRemoteFileCache) {
		// From command-line: verify cache
		if (fileExists(cache_path)) {
			if (fileLastChangedPST(cache_path) == fileLastChangedPST(path)) {
				needToCopy = false;
			} else {
				needToCopy = true;
			}
		} else if (fileExists(deleted_path)) {
			if (!fileExists(path)) {
				needToCopy = false;
			} else {
				needToCopy = true;
			}
		} else {
			needToCopy = true;
		}
	} else {
		// From App via DLL: Use cache, don't touch the network if possible
		if (fileExists(cache_path) || fileExists(deleted_path)) {
			needToCopy = false;
		} else {
			needToCopy = true;
		}
	}

	if (!needToCopy)
	{
		// File query has been cached as not existing
		// or cache file exists and is cached
		strcpy_s(dest, dest_size, cache_path);
		return;
	}

	// Update from network
	{
		char *data;
		int len;

		data = fileAlloc(path, &len);

		if (!data) {
			FILE *deleted_file;
			mkdirtree(deleted_path);
			deleted_file = fopen(deleted_path, "wb");
			if (deleted_file)
				fclose(deleted_file);
			fileForceRemove(cache_path);
		} else {
			FILE *cached_file;
			mkdirtree(cache_path);
			cached_file = fopen(cache_path, "wb");
			if (!cached_file) {
				// What horror is this?
				// Just return out the same path we got in
				strcpy(cache_path, path);
			} else {
				fwrite(data, 1, len, cached_file);
				fclose(cached_file);
				fileTouch(cache_path, path);
				fileForceRemove(deleted_path);
			}
			fileFree(data);
		}
		strcpy_s(dest, dest_size, cache_path);
	}
}


char *gimmeFileAllocCached(const char *path, int *lenp)
{
	char buf[MAX_PATH];
	char *ret;

	gimmeCacheLock();
	gimmeGetCachedFilename(path, SAFESTR(buf));
	ret = fileAlloc(buf, lenp);
	gimmeCacheUnlock();

	return ret;
}

const char * const * gimmeQueryGroupListForUser(const char *username)
{
	return LoadUserGroupInfoFromFile(username);
}

const char * const * gimmeQueryGroupList(void)
{
	return gimmeQueryGroupListForUser(gimmeGetUserName());
}

const char * const * gimmeQueryFullGroupList(void)
{
	return LoadUserGroupInfoFromFileFindGroups();
}

const char * const * gimmeQueryFullUserList(void)
{
	return LoadUserGroupInfoFromFileFindUsers();
}

//////////////////////////////////////////////////////////////////////////
// Gimme UI Utility Functions
//////////////////////////////////////////////////////////////////////////


void DlgItemGetRelativeRect(HWND hDlg, HWND hDlgItem, LPRECT rc)
{
	POINT pt1, pt2;
	GetWindowRect(hDlgItem, rc);
	pt1.x = rc->left, pt1.y = rc->top;
	pt2.x = rc->right, pt2.y = rc->bottom;
	ScreenToClient(hDlg, &pt1);
	ScreenToClient(hDlg, &pt2);
	rc->left = pt1.x, rc->top = pt1.y, rc->right = pt2.x, rc->bottom = pt2.y;
}

//Generalized function for resizing and positioning items inside a resizable window
void DlgItemDoResize(HWND hDlg, HWND hDlgItem, int widthDiff, int heightDiff, LPRECT windowRect, U32 flags)
{
	RECT itemRect;
	int newX, newY, newW, newH;
	DlgItemGetRelativeRect(hDlg, hDlgItem, &itemRect);
	newX = (itemRect.left-windowRect->left);
	newY = (itemRect.top-windowRect->top);
	newW = (itemRect.right-itemRect.left);
	newH = (itemRect.bottom-itemRect.top);
	if(flags & RESZ_RIGHT)
		newX += widthDiff;
	if(flags & RESZ_BOTTOM)
		newY += heightDiff;
	if(flags & RESZ_WIDTH)
		newW += widthDiff;
	if(flags & RESZ_HEIGHT)
		newH += heightDiff;
	SetWindowPos(hDlgItem, 0, newX, newY, newW, newH, SWP_NOZORDER);
}

//Prevents the window from being resized smaller than it originally started.
//This actually doesn't work 100% can you can resize slightly less,
//but it wasn't worth figuring out why.  I am sure it is ignoring border size or something.
void DlgWindowCheckResize(HWND hDlg, U32 moving, LPRECT newRect, LPRECT origRect)
{
	int origWidth = (origRect->right-origRect->left);
	int origHeight = (origRect->bottom-origRect->top);
	int newWidth = (newRect->right-newRect->left);
	int newHeight = (newRect->bottom-newRect->top);
	if(newWidth < origWidth) {
		if(moving == WMSZ_BOTTOMLEFT || moving == WMSZ_TOPLEFT || moving == WMSZ_LEFT)
			newRect->left = newRect->right - origWidth;
		else
			newRect->right = newRect->left + origWidth;
		InvalidateRect(hDlg, NULL, true);
	}
	if(newHeight < origHeight) {
		if(moving == WMSZ_TOPLEFT || moving == WMSZ_TOPRIGHT || moving == WMSZ_TOP)
			newRect->top = newRect->bottom - origHeight;
		else
			newRect->bottom = newRect->top + origHeight;
		InvalidateRect(hDlg, NULL, true);
	}
}