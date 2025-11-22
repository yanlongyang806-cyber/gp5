#include "gimmeUtil.h"
#include "gimme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file.h"
#include "fileutil.h"
#include "utils.h"

#include <process.h>
#include <time.h>
#include "RegistryReader.h"
#include "resource.h"
#include "earray.h"
#include "wininclude.h"
#include <Shellapi.h>
#include "ListView.h"
#include "textparser.h"
#include "gimmeBranch.h"
#include <CommCtrl.h>
#include "winutil.h"

#include "strings_opt.h"
#include "UTF8.h"

typedef struct GimmeHistory {
	char *name;
	char modtime[128];
	char checkintime[128];
	time_t time_modtime;
	time_t time_checkintime;
	size_t size;
	int  revision;
	int  branch;
} GimmeHistory;

ParseTable GimmeHistoryInfo[] =
{
	{ "Name",				TOK_STRING(GimmeHistory, name, 0), 0, TOK_FORMAT_LVWIDTH(175)},
	{ "Size",				TOK_INT(GimmeHistory, size, 0), 0, TOK_FORMAT_LVWIDTH(60)},
	{ "Rev",				TOK_INT(GimmeHistory, revision, 0), 0, TOK_FORMAT_LVWIDTH(40)},
	{ "Branch",				TOK_INT(GimmeHistory, branch, 0), 0, TOK_FORMAT_LVWIDTH(50)},
	{ "Modification Time (PST)",	TOK_FIXEDSTR(GimmeHistory, modtime), 0, TOK_FORMAT_LVWIDTH(150)},
	// Checkin time will only show up if launched with gimme -forcedb
	{ "Checkin Time",		TOK_FIXEDSTR(GimmeHistory, checkintime), 0, TOK_FORMAT_LVWIDTH(150)},
	{ 0 }
};

typedef struct GimmeLockList {
	char lockee[128];
	int  branch;
	char time[128];
} GimmeLockList;

ParseTable GimmeLockListInfo[] =
{
	{ "Checked out by",			TOK_FIXEDSTR(GimmeLockList, lockee), 0, TOK_FORMAT_LVWIDTH(175)},
	{ "Branch",					TOK_INT(GimmeLockList, branch, 0), 0, TOK_FORMAT_LVWIDTH(50)},
	{ "Checkout Time (PST)",	TOK_FIXEDSTR(GimmeLockList, time), 0, TOK_FORMAT_LVWIDTH(150)},
	{ 0 }
};

AUTO_RUN;
void gimmeRegisterParseTables3(void)
{
	ParserSetTableInfo(GimmeLockListInfo, sizeof(GimmeLockList), "GimmeLockListInfo", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(GimmeHistoryInfo, sizeof(GimmeHistory), "GimmeHistoryInfo", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

static GimmeDir *g_gimme_dir;
static char *g_relpath;
static char *g_localname;
static char *g_lastauthor;
static char *g_dbname;
static char *g_lockee;
static char *g_timestamps;
static char *g_nextbranch;
static char *g_prevbranch;
static char g_branchnote[256];
static int g_no_later=0;
static int g_no_prev=0;
static int g_in_db=1;
static ListView *lvDetails;
static GimmeHistory **eaHistory_data=NULL;
static GimmeHistory ***eaHistory=&eaHistory_data;
static ListView *lvLockList;
static GimmeLockList **eaLockList_data=NULL;
static GimmeLockList ***eaLockList=&eaLockList_data;
static int g_latestrev;
static int g_restartdialog = 0;
static int g_restarteddialog = 0;

typedef struct
{
	int branch;
	CommentInfo** commentInfo;
} BranchCommentInfo;

BranchCommentInfo** g_file_comments = NULL;
BranchCommentInfo** g_file_batchinfo = NULL;

static void destroyBranchComments(BranchCommentInfo *info)
{
	eaDestroyEx(&info->commentInfo, destroyCommentInfo);
	free(info);
}

//////////////////////////////////////////
// comments
//////////////////////////////////////////

static void destroyFileComments()
{
	eaDestroyEx(&g_file_comments, destroyBranchComments);
}

static void addFileComments(int branch)
{
	if (eaSize(&g_file_comments) <= branch || !g_file_comments[branch])
	{
		BranchCommentInfo *comments = malloc(sizeof(BranchCommentInfo));
		comments->branch = branch;
		comments->commentInfo = NULL;
		gimmeGetComments(g_gimme_dir, g_relpath, branch, &(comments->commentInfo));

		if ( eaSize(&g_file_comments) <= branch )
			eaSetSize(&g_file_comments, branch + 1);
		g_file_comments[branch] = comments;
	}
}

static CommentInfo *findFileComment(int branch, int version)
{
	int i;

	if (eaSize(&g_file_comments) <= branch || !g_file_comments[branch])
		return NULL;

	for (i = eaSize(&(g_file_comments[branch]->commentInfo)) - 1; i >= 0; i--)
	{
		if (g_file_comments[branch]->commentInfo[i]->version == version)
			return g_file_comments[branch]->commentInfo[i];
	}

	return NULL;
}

//////////////////////////////////////////
// batch info
//////////////////////////////////////////

static void destroyFileBatchInfo()
{
	eaDestroyEx(&g_file_batchinfo, destroyBranchComments);
}

static void addFileBatchInfo(int branch)
{
	if (eaSize(&g_file_batchinfo) <= branch || !g_file_batchinfo[branch])
	{
		BranchCommentInfo *comments = malloc(sizeof(BranchCommentInfo));
		comments->branch = branch;
		comments->commentInfo = NULL;
		gimmeGetBatchInfo(g_gimme_dir, g_relpath, branch, &(comments->commentInfo));

		if ( eaSize(&g_file_batchinfo) <= branch )
			eaSetSize(&g_file_batchinfo, branch + 1);
		g_file_batchinfo[branch] = comments;
	}
}

static CommentInfo *findFileBatchInfo(int branch, int version)
{
	int i;

	if (eaSize(&g_file_batchinfo) <= branch || !g_file_batchinfo[branch])
		return NULL;

	for (i = eaSize(&(g_file_batchinfo[branch]->commentInfo)) - 1; i >= 0; i--)
	{
		if (g_file_batchinfo[branch]->commentInfo[i]->version == version)
			return g_file_batchinfo[branch]->commentInfo[i];
	}

	return NULL;
}


static char *dbNameFromGimmeHistory(GimmeHistory *gh, char *buf, size_t buf_size)
{
	char *s;
	assert(buf);
	if (!buf) {
		return "Error";
	}
	buf[0] = '\"';
	strcpy_s(buf+1, buf_size-1, g_dbname);
	backSlashes(buf);
	s = strrchr(buf, '\\');
	assert(s);
	if (s) {
		*s=0;
		strcatf_s(buf, buf_size, "\\%s\"", gh->name);
	} else {
		strcpy_s(buf, buf_size, "Error");
		return buf;
	}
	return buf;
}

static void singleDiff(ListView *lv, GimmeHistory *gh, void *data)
{
	char dbname[CRYPTIC_MAX_PATH];
	char localfname[CRYPTIC_MAX_PATH];


	gimmeUserLogf("\tSingle Diff: %s", gh->name);

	// Assemble remote name
	dbNameFromGimmeHistory(gh, SAFESTR(dbname));
	sprintf(localfname, "\"%s\"", g_localname);
	gimme_state.editor = gimmeDetectDiffProgram();

	// run diff
	{
		char *pFullCmdLine = NULL;
		int ret; 
		estrPrintf(&pFullCmdLine, "%s %s %s", gimme_state.editor, backSlashes(localfname), backSlashes(dbname));
		ret = system_detach(pFullCmdLine, 0, 0);
		estrDestroy(&pFullCmdLine);
			
		if (!ret)
			gimmeLog(LOG_FATAL, "Error launching diff program (%s)", gimme_state.editor);
	}
}

static void viewFile(ListView *lv, GimmeHistory *gh, void *data)
{
	char dbname[CRYPTIC_MAX_PATH];
	char localname[CRYPTIC_MAX_PATH];
	char *s;

	gimmeUserLogf("\tView File: %s", gh->name);

	// Assemble remote name
	dbNameFromGimmeHistory(gh, SAFESTR(dbname));

	// Make appropriate local name
	sprintf(localname, "c:/temp/%s", getFileName(dbname));
	// remove _v#1_vb#2_jimb.txt portion
	s = strstri(localname, "_v#");
	if (s)
		*s=0;
	s = strstri(localname, "_vb#");
	if (s)
		*s=0;
	mkdirtree(localname);
	if (dbname[0]=='\"' && strEndsWith(dbname, "\"")) {
		strcpy(dbname, dbname+1);
		dbname[strlen(dbname)-1]=0;
	}

	fileCopy(dbname, localname);

	fileOpenWithEditor(localname);
}

static void getFile(ListView *lv, GimmeHistory *gh, void *data)
{
	char dbname[CRYPTIC_MAX_PATH];
	int ret;

	gimmeUserLogf("\tGet File: %s", gh->name);

	// Assemble remote name
	dbNameFromGimmeHistory(gh, SAFESTR(dbname));
	if (dbname[0]=='\"' && strEndsWith(dbname, "\"")) {
		strcpy(dbname, dbname+1);
		dbname[strlen(dbname)-1]='\0';
	}

	GIMME_CRITICAL_START;
	ret = copyFileToLocal(g_gimme_dir, dbname, g_localname, gh->time_modtime, !gimmeQueryIsFileLockedByMeOrNew(g_localname));
	GIMME_CRITICAL_END;
/*
//	fileCopy(backSlashes(dbname), g_localname);
	ret = copyFile(dbname, g_localname);
	if (ret==NO_ERROR) {
		// change timestamp
		struct _utimbuf utb;
		utb.actime = gh->time_modtime;
		utb.modtime = gh->time_modtime;
		pstutime(g_localname, &utb);

		// set the file create time to the check in time
		//pstSetFileCreatedTime(g_localname, gh->time_checkintime);
	}
	if (gimmeQueryIsFileLockedByMeOrNew(g_localname)) {
		_chmod(g_localname, _S_IREAD | _S_IWRITE);
	} else {
		_chmod(g_localname, _S_IREAD);
	}
	*/
}

static void relinkFile(ListView *lv, GimmeHistory *gh, void *data)
{
	gimmeUserLogf("\tRelink File: %s", gh->name);
	gimmeRelinkFileToBranch(g_localname, gh->branch, -1, 0);
}

static void doubleDiff(ListView *lv, GimmeHistory *gh, void *data)
{
	static int count=0;
	static char firstFile[CRYPTIC_MAX_PATH];
	if (count==0) {
		dbNameFromGimmeHistory(gh, SAFESTR(firstFile));
		count++;
	} else {
		char secondFile[CRYPTIC_MAX_PATH];
		int ret;

		dbNameFromGimmeHistory(gh, SAFESTR(secondFile));
		gimmeUserLogf("\tDouble Diff: %s - %s", firstFile, secondFile);
		gimme_state.editor = gimmeDetectDiffProgram();
		{
			char *pFullCmdLine = NULL;
			estrPrintf(&pFullCmdLine, "%s %s %s", gimme_state.editor, backSlashes(firstFile), backSlashes(secondFile));
			ret = system_detach(pFullCmdLine, 0, 0);
			estrDestroy(&pFullCmdLine);

			if (!ret)
				gimmeLog(LOG_FATAL, "Error launching diff program (%s)", gimme_state.editor);
		}
		count=0;
	}
}

static void displayComments(ListView *lv, GimmeHistory *gh, void *data)
{
	HWND wnd = (HWND)data;
	CommentInfo *commentInfo = findFileComment(gh->branch, gh->revision);

	if (commentInfo)
	{
		SetWindowText_UTF8(wnd, commentInfo->comments);
	}
	else
	{
		SetWindowText_UTF8(wnd, "");
	}
}

static void displayBatchInfo(ListView *lv, GimmeHistory *gh, void *data)
{
	HWND wnd = (HWND)data;
	CommentInfo *commentInfo = findFileBatchInfo(gh->branch, gh->revision);

	if (commentInfo)
	{
		SetWindowText_UTF8(wnd, commentInfo->comments);
	}
	else
	{
		SetWindowText_UTF8(wnd, "");
	}
}

LRESULT CALLBACK DlgStatProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char buf[CRYPTIC_MAX_PATH];
	char *s;
	int i;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_LOCAL), g_localname);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_LASTAUTHOR), g_lastauthor);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_DBNAME), g_dbname);
			if (g_lockee) {
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_LOCKEE), g_lockee);
			} else {
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_LOCKEE), "Not checked out by anyone");
			}
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_TIMESTAMPS), g_timestamps);
			if (gimme_state.db_mode!=GIMME_FORCE_DB) {
				// TODO comment these out if you always want to show the checkin time
				GimmeHistoryInfo[5].name=0;
				GimmeHistoryInfo[5].type=0;
			} else {
				GimmeHistoryInfo[5].name="Checkin Time";
				GimmeHistoryInfo[5].type=TOK_STRING_X;
			}
			// History List
			listViewInit(lvDetails, GimmeHistoryInfo, hDlg, GetDlgItem(hDlg, IDC_DETAILS));
			for (i=0; i<eaSize(eaHistory); i++) {
				listViewAddItem(lvDetails, eaGet(eaHistory, i));
			}
			listViewSort(lvDetails, 2); // Revision
			listViewSort(lvDetails, 3); // Branch
			// Lock list
			listViewInit(lvLockList, GimmeLockListInfo, hDlg, GetDlgItem(hDlg, IDC_LST_LOCKS));
			for (i=0; i<eaSize(eaLockList); i++) {
				listViewAddItem(lvLockList, eaGet(eaLockList, i));
			}
			listViewSort(lvLockList, 1); // Branch
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_BRANCHNOTE), g_branchnote);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_PREVBRANCH), g_prevbranch);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_NEXTBRANCH), g_nextbranch);

			if (g_no_later) {
				// There is no later branch
				SendDlgItemMessage(hDlg, IDC_TXT_NEXTBRANCH, WM_ENABLE, FALSE, 0);
				//SendDlgItemMessage(hDlg, IDC_LBL_NEXTBRANCH, WM_ENABLE, FALSE, 0);
			}
			if (g_no_prev) {
				SendDlgItemMessage(hDlg, IDC_TXT_PREVBRANCH, WM_ENABLE, FALSE, 0);
				//SendDlgItemMessage(hDlg, IDC_LBL_PREVBRANCH, WM_ENABLE, FALSE, 0);
			}
			if (!g_in_db) {
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_GODB), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_DIFF), FALSE);
			}
			EnableWindow(GetDlgItem(hDlg, IDC_BTN_COMPARE), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_BTN_RELINK), FALSE);
			if (g_lockee) { // Someone's got it checked out
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_CHECKOUT), FALSE);
			}
			if (g_lockee && stricmp(g_lockee, gimmeGetUserName())==0) {
				// File is checked out by us
				
			} else {
				// Not checked out by us
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_CHECKIN), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNDO), FALSE);
				SetDlgItemText_UTF8(hDlg, IDC_BTN_GOLOCAL, "View");
				if (g_lockee) {
					// Someone else
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE), FALSE);
				}
			}

			{
				TC_ITEM tabitem;
				tabitem.mask = TCIF_TEXT | TCIF_IMAGE; 
				tabitem.iImage = -1; 

				tabitem.pszText = L"Checkouts"; 
				TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_TAB), 0, &tabitem);

				tabitem.pszText = L"Comments"; 
				TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_TAB), 1, &tabitem);

				tabitem.pszText = L"Batch Info"; 
				TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_TAB), 2, &tabitem);

				ShowWindow(GetDlgItem(hDlg, IDC_LST_LOCKS), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_TXT_COMMENTS), SW_HIDE);
				BringWindowToTop(GetDlgItem(hDlg, IDC_LST_LOCKS));
			}

			return FALSE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			case IDOK:
			case IDCANCEL:
				EndDialog(hDlg, 0);
				return TRUE;
			case IDC_BTN_GODB:
				strcpy(buf, g_dbname);
				backSlashes(buf);
				s = strrchr(buf, '\\');
				if (s) {
					*s=0;
					gimmeUserLogf("\tGo DB: %s", buf);
					ShellExecute_UTF8 ( hDlg, "open", buf, NULL, NULL, SW_SHOW);
				}
				break;
			case IDC_BTN_DIFF:
				{
					char dbname[CRYPTIC_MAX_PATH];
					char localfname[CRYPTIC_MAX_PATH];
					int ret;
					// run diff
					sprintf(dbname, "\"%s\"", g_dbname);
					sprintf(localfname, "\"%s\"", g_localname);
					gimme_state.editor = gimmeDetectDiffProgram();
					{
						char *pFullCmdLine = NULL;
						estrPrintf(&pFullCmdLine, "%s %s %s", gimme_state.editor, backSlashes(localfname), backSlashes(dbname));
						ret = system_detach(pFullCmdLine, 0, 0);
						estrDestroy(&pFullCmdLine);
						if (!ret) 
							gimmeLog(LOG_FATAL, "Error launching diff program (%s)", gimme_state.editor);
					}
				}
				break;
			case IDC_BTN_COMPARE:
				{
					// Diffs vs selected item in history view
					int count = listViewDoOnSelected(lvDetails, NULL, NULL);
					if (count==1) {
						listViewDoOnSelected(lvDetails, singleDiff, NULL);
					} else if (count==2) {
						listViewDoOnSelected(lvDetails, doubleDiff, NULL);
					}
				}
				break;
			case IDC_BTN_VIEW:
				{
					// Diffs vs selected item in history view
					int count = listViewDoOnSelected(lvDetails, NULL, NULL);
					if (count==1) {
						listViewDoOnSelected(lvDetails, viewFile, NULL);
					}
				}
				break;
			case IDC_BTN_GET:
				{
					// Gets selected item in history view
					int count = listViewDoOnSelected(lvDetails, NULL, NULL);
					if (count==1) {
						listViewDoOnSelected(lvDetails, getFile, NULL);
						EndDialog(hDlg, 1);
					}
				}
				break;
			case IDC_BTN_CHECKOUT:
				gimme_state.editor = "null";
				gimmeUserLogf("\tCheckout: %s", g_relpath);
				if (NO_ERROR==gimmeDoOperationRelPath(g_gimme_dir, g_relpath, GIMME_CHECKOUT, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_UNDO:
				gimmeUserLogf("\tUndo: %s", g_relpath);
				if (NO_ERROR==gimmeDoOperationRelPath(g_gimme_dir, g_relpath, GIMME_UNDO_CHECKOUT, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_CHECKIN:
				{
					int ret;
					if (getCommentLevel(g_gimme_dir->local_dir) != CM_DONTASK) {
						gimme_state.just_queue=1;
					}
					gimmeUserLogf("\tCheckin: %s", g_relpath);
					ret=gimmeDoOperationRelPath(g_gimme_dir, g_relpath, GIMME_CHECKIN, 0);
					if (getCommentLevel(g_gimme_dir->local_dir) != CM_DONTASK) {
						gimme_state.just_queue=0;
						if (ret==NO_ERROR) {
							ret = doQueuedActions();
						}
					}
					if (ret==NO_ERROR)
						EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_REMOVE:
				gimmeUserLogf("\tRemove: %s", g_relpath);
				if (NO_ERROR==gimmeDoOperationRelPath(g_gimme_dir, g_relpath, GIMME_DELETE, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_GLV:
				gimme_state.force_get_latest = 1;
				gimmeUserLogf("\tGet Latest Version: %s", g_relpath);
				if (NO_ERROR==gimmeDoOperationRelPath(g_gimme_dir, g_relpath, GIMME_GLV, 0)) {
					EndDialog(hDlg, 1);
				}
				gimme_state.force_get_latest = 0;
				break;
			case IDC_BTN_GOLOCAL:
				fileOpenWithEditor(g_localname);
				break;
			case IDC_BTN_RELINK:
				{
					listViewDoOnSelected(lvDetails, relinkFile, NULL);
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_CHECKINTIMES:
				gimme_state.db_mode = GIMME_FORCE_DB;
				EndDialog(hDlg, 1);
				break;

		}
		break;

	case WM_NOTIFY:
		{
			int idCtrl = (int)wParam;
			int iTabPage = TabCtrl_GetCurSel(GetDlgItem(hDlg, IDC_TAB));
			listViewOnNotify(lvDetails, wParam, lParam, NULL);
			listViewOnNotify(lvLockList, wParam, lParam, NULL);

			// comments
			if (iTabPage == 1)
			{
				if (listViewDoOnSelected(lvDetails, displayComments, GetDlgItem(hDlg, IDC_TXT_COMMENTS)) == 0)
				{
					CommentInfo *commentInfo = findFileComment(g_gimme_dir->active_branch, g_latestrev);
					if (commentInfo)
					{
						SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_COMMENTS), commentInfo->comments);
					}
					else
					{
						SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_COMMENTS), "");
					}
				}
			}


			// batch checkin info
			if (iTabPage == 2)
			{
				if (listViewDoOnSelected(lvDetails, displayBatchInfo, GetDlgItem(hDlg, IDC_TXT_COMMENTS)) == 0)
				{
					CommentInfo *commentInfo = findFileBatchInfo(g_gimme_dir->active_branch, g_latestrev);
					if (commentInfo)
					{
						SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_COMMENTS), commentInfo->comments);
					}
					else
					{
						SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_COMMENTS), "");
					}
				}
			}


			if (idCtrl == IDC_DETAILS) {
				// Count number of selected elements
				i = listViewDoOnSelected(lvDetails, NULL, NULL);
				if (i==1 || i==2) {
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_COMPARE), TRUE);
				} else {
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_COMPARE), FALSE);
				}
				if (i==1) {
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_VIEW), TRUE);
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_GET), TRUE);
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_RELINK), TRUE);
				} else {
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_VIEW), FALSE);
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_GET), FALSE);
					EnableWindow(GetDlgItem(hDlg, IDC_BTN_RELINK), FALSE);
				}
			}
			else if (idCtrl == IDC_TAB)
			{
				if (iTabPage == 0)
				{
					ShowWindow(GetDlgItem(hDlg, IDC_LST_LOCKS), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, IDC_TXT_COMMENTS), SW_HIDE);
					BringWindowToTop(GetDlgItem(hDlg, IDC_LST_LOCKS));
				}
				// comments
				else if (iTabPage == 1)
				{
					ShowWindow(GetDlgItem(hDlg, IDC_LST_LOCKS), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, IDC_TXT_COMMENTS), SW_SHOW);
					BringWindowToTop(GetDlgItem(hDlg, IDC_TXT_COMMENTS));
				}

				// batch checkin info
				else if (iTabPage == 2)
				{
					ShowWindow(GetDlgItem(hDlg, IDC_LST_LOCKS), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, IDC_TXT_COMMENTS), SW_SHOW);
					BringWindowToTop(GetDlgItem(hDlg, IDC_TXT_COMMENTS));
				}
			}
		}
		break;
	}
	return FALSE;
}

int gimmeUtilStatFile(const char *fname, int graphical, int history)
{
	char	fullpath[CRYPTIC_MAX_PATH],localfname[CRYPTIC_MAX_PATH],*username,dbname[CRYPTIC_MAX_PATH],*last_author;
	GimmeNode *node;
	FWStatType sbuf;
	char fname_temp[CRYPTIC_MAX_PATH], *fname2=NULL;
	char buf2[CRYPTIC_MAX_PATH];
	g_latestrev=-1;
	strcpy(fname_temp, fname);
	if (!fileIsAbsolutePath(fname_temp)) {
		fname2 = fileLocateRead(fname_temp, buf2);
	}
	if (fname2==NULL) {
		makefullpath(fname_temp,fullpath);
	} else {
		makefullpath(fname2,fullpath); // This might not do anything unless people have things like "./" in their gameDataDirs
	}

	fname = fullpath;
	if (fname==NULL) {
		gimmeLog(LOG_FATAL, "Cannot locate file or no file specified.");
		return GIMME_ERROR_FILENOTFOUND;
	}
	makefullpath((char*)fname,fullpath);
	g_gimme_dir = findGimmeDir(fullpath);
	if (eaSize(&eaGimmeDirs)==0) {
		gimmeLog(LOG_FATAL, "No databases defined, cannot do diff.");
		return GIMME_ERROR_NO_SC;
	}
	if (!g_gimme_dir)
	{
		gimmeLog(LOG_FATAL, "Can't find source control folder to match %s!",fullpath);
		return GIMME_ERROR_NODIR;
	}
//	if (stricmp(&fullpath[strlen(fullpath)-5],".lock")==0)
//	{
//		gimmeLog(LOG_FATAL, "%s is a lock file - you can't operate on that!",fullpath);
//		return GIMME_ERROR_LOCKFILE;
//	}

	gimmeDirDatabaseLoad(g_gimme_dir, fullpath);

	g_relpath = findRelPath(fullpath, g_gimme_dir);
	username = isLocked(g_gimme_dir, g_relpath);
	makeLocalNameFromRel(g_gimme_dir, g_relpath, localfname);
	g_latestrev=makeDBName(g_gimme_dir, g_relpath, REV_BLEEDINGEDGE, &node);
	if (g_latestrev==-1) {
		g_in_db=0;
		sprintf(dbname, "Not in database");
	} else if (g_latestrev==-2) { // not actually used
		g_in_db=0;
		sprintf(dbname, "Not in this branch");
	} else {
		g_in_db=1;
		sprintf(dbname, "%s%s", g_gimme_dir->lock_dir, gimmeNodeGetFullPath(node));
	}

	// Assemble history
	destroyFileComments();
	destroyFileBatchInfo();
	eaClearEx(eaHistory, NULL);
	if (g_latestrev >= 0) {
		GimmeNode *walk=node;
		GimmeHistory *gh;
		while (walk->prev)
			walk = walk->prev;
		while (walk) {
			gh = calloc(sizeof(GimmeHistory),1);
			printDate(walk->checkintime, gh->checkintime);
			gh->time_checkintime = walk->checkintime;
			printDate(walk->timestamp, gh->modtime);
			gh->time_modtime = walk->timestamp;
			gh->size = walk->size;
			gh->name = walk->name;
			gh->revision = walk->revision;
			gh->branch = walk->branch;
			addFileComments(walk->branch);
			addFileBatchInfo(walk->branch);
			eaPush(eaHistory, gh);
			walk = walk->next;
		}

		// Determine branch linkage status
		if (g_gimme_dir->active_branch==0) {
			g_prevbranch = "This file has no previous branches to inherit from.";
		} else if (node->branch == g_gimme_dir->active_branch) {
			g_prevbranch = "Link BROKEN; This file's link to the previous branch has been broken, no changes will be inherited.";
		} else {
			g_prevbranch = "LINKED; This file is still linked to the previous branch, any changes in the previous branch will be reflected in this one.  WARNING: Any changes in this branch will sever the link.";
		}
		{
			GimmeNode *node2;
			int nextbranchrev;
			char lockdir[CRYPTIC_MAX_PATH];
			sprintf(lockdir,"%s_versions/", g_relpath);
			nextbranchrev=getHighestVersion(g_gimme_dir, lockdir, &node2, g_gimme_dir->active_branch+100, g_relpath);
			if (node2->branch != g_gimme_dir->active_branch) {
				// link broken
				g_nextbranch = "Link BROKEN; This file's link has been broken, changes to this file will ONLY AFFECT THIS BRANCH.  Bug fixes will need to be manually applied to the later branch(es).";
			} else {
				g_nextbranch = "LINKED; Changes to this file will affect both this and the later branch.";
			}
		}

	} else {
		g_prevbranch = g_nextbranch = "Not in database";
	}

	// Assemble lock list
	{
		int i;
		eaClearEx(eaLockList, NULL);
		for (i=gimmeGetMinBranchNumber(); i<=gimmeGetMaxBranchNumber(); i++) {
			GimmeNode *nodelock;
			char	buf[CRYPTIC_MAX_PATH];

			makeLockName(g_gimme_dir, g_relpath, SAFESTR(buf), i);
			nodelock = gimmeNodeFind(g_gimme_dir->database->root->contents, buf);
			if (nodelock && nodelock->lock) {
				GimmeLockList *ll = malloc(sizeof(GimmeLockList));
				ll->branch = i;
				strcpy(ll->lockee, nodelock->lock);
				printDate(nodelock->checkintime, ll->time);
				eaPush(eaLockList, ll);
			}
		}
	}

	last_author = gimmeQueryLastAuthor(fullpath);
	g_localname = localfname;
	g_lastauthor = last_author;
	g_dbname = dbname;
	g_lockee = username;
	sprintf(g_branchnote, "You are working on branch #%d (%s)", g_gimme_dir->active_branch, gimmeGetBranchName(g_gimme_dir->active_branch));
	g_no_later = g_gimme_dir->active_branch == gimmeGetMaxBranchNumber();
	g_no_prev = g_gimme_dir->active_branch == gimmeGetMinBranchNumber();
	g_timestamps = "";
	if (g_latestrev!=-1) {
		bool b;
		pststat(localfname, &sbuf);
		b=  (sbuf.st_mtime >= node->timestamp-IGNORE_TIMEDIFF) &&
			(sbuf.st_mtime <= node->timestamp+IGNORE_TIMEDIFF); 
		if (b) {
			g_timestamps = "File timestamps are identical";
		} else if (sbuf.st_mtime > node->timestamp) {
			g_timestamps = "Local version has a newer timestamp";
		} else {
			g_timestamps = "Version in database has a newer timestamp";
		}
	}


	if (!graphical) {
		printf(" o Local file: ");
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
		printf("%s\n", localfname);
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		printf(" o Last Author/Status: ");
		consoleSetFGColor(COLOR_GREEN | COLOR_BRIGHT);
		printf("%s\n", last_author);
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		forwardSlashes(dbname);
		printf(" o Latest revision (%d): ", g_latestrev);
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
		printf("%s\n", dbname);
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		printf(" o File is ");
		if (username) {
			consoleSetFGColor(COLOR_GREEN | COLOR_BRIGHT);
			printf("checked out by %s\n", username);
		} else {
			consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
			printf("not checked out by anyone.\n");
		}
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		g_timestamps = "";
		if (g_latestrev!=-1) {
			bool b;
			printf(" o ");
			pststat(localfname, &sbuf);
			b=  (sbuf.st_mtime >= node->timestamp-IGNORE_TIMEDIFF) &&
				(sbuf.st_mtime <= node->timestamp+IGNORE_TIMEDIFF);
			pststat(localfname, &sbuf);
			if (b) {
				printf("File timestamps are ");
				consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
				printf("identical\n");
				g_timestamps = "File timestamps are identical";
			} else if (sbuf.st_mtime > node->timestamp) {
				consoleSetFGColor(COLOR_RED | COLOR_BRIGHT);
				printf("Local version has a newer timestamp\n");
				g_timestamps = "Local version has a newer timestamp";
			} else {
				consoleSetFGColor(COLOR_RED | COLOR_BRIGHT);
				printf("Version in database has a newer timestamp\n");
				g_timestamps = "Version in database has a newer timestamp";
			}
			consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);
			if (history) {
				int i;
				for (i=0; i<eaSize(eaHistory); i++) {
					GimmeHistory *gh = eaGet(eaHistory, i);
					printf("%s r:%2d b:%d %8db %s\n", gh->modtime, gh->revision, gh->branch, gh->size, gh->name);
				}
				for (i=0; i<eaSize(eaLockList); i++) {
					GimmeLockList *ll = eaGet(eaLockList, i);
					printf("Checked out by %s on branch %d on %s\n", ll->lockee, ll->branch, ll->time);
				}
			}
		}
	}

	if (graphical) {
		int ret;
		lvDetails = listViewCreate();
		lvLockList = listViewCreate();
		ret = DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_STAT), NULL, (DLGPROC)DlgStatProc);
		listViewDestroy(lvDetails);
		listViewDestroy(lvLockList);
		destroyFileComments();
		destroyFileBatchInfo();
		if (ret==1) { // They changed something, redisplay it!
			gimmeDirDatabaseClose(g_gimme_dir);
			return gimmeUtilStatFile(fname, graphical, history);
		}
	}
	gimmeDirDatabaseClose(g_gimme_dir);
	return NO_ERROR;
}
