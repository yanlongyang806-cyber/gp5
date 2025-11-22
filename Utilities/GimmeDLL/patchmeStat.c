#include "GimmeUtil.h"
#include "patchmeStat.h"
#include "patchdb.h"
#include "patchme.h"
#include "gimmeDLLPrivateInterface.h"
#include "earray.h"
#include "utils.h"
#include "UnitSpec.h"
#include "timing.h"
#include "patchcommonutils.h"
#include "textparser.h"
#include "ListView.h"
#include "resource.h"
#include "file.h"
#include "trivia.h"
#include "patchtrivia.h"
#include "winutil.h"
#include "StringCache.h"

#include <CommCtrl.h>

typedef struct PatchmeHistory {
	const char *author;
	char modtime[128];
	char checkintime[128];
	time_t time_modtime;
	time_t time_checkintime;
	size_t size;
	int version;
	int branch;
	int global_revision;
} PatchmeHistory;

#define TYPE_parse_PatchmeHistory PatchmeHistory
ParseTable parse_PatchmeHistory[] =
{
	{ "Author",				TOK_POOL_STRING | TOK_STRING(PatchmeHistory, author, 0), 0, TOK_FORMAT_LVWIDTH(175)},
	{ "Size",				TOK_INT(PatchmeHistory, size, 0), 0, TOK_FORMAT_LVWIDTH(60)},
	{ "Version",			TOK_INT(PatchmeHistory, version, 0), 0, TOK_FORMAT_LVWIDTH(40)},
	{ "Branch",				TOK_INT(PatchmeHistory, branch, 0), 0, TOK_FORMAT_LVWIDTH(50)},
	{ "Modification Time",	TOK_FIXEDSTR(PatchmeHistory, modtime), 0, TOK_FORMAT_LVWIDTH(150)},
	{ "Checkin Time",		TOK_FIXEDSTR(PatchmeHistory, checkintime), 0, TOK_FORMAT_LVWIDTH(150)},
	{ "Checkin Revision",	TOK_INT(PatchmeHistory, global_revision, 0), 0, TOK_FORMAT_LVWIDTH(100)},
	{ 0 }
};

typedef struct PatchmeLockList {
	char *lockee;
	int  branch;
	char time[128];
} PatchmeLockList;

#define TYPE_parse_PatchmeLockList PatchmeLockList
ParseTable parse_PatchmeLockList[] =
{
	{ "Checked out by",			TOK_POOL_STRING | TOK_STRING(PatchmeLockList, lockee, 0), 0, TOK_FORMAT_LVWIDTH(175)},
	{ "Branch",					TOK_INT(PatchmeLockList, branch, 0), 0, TOK_FORMAT_LVWIDTH(50)},
	{ "Checkout Time",			TOK_FIXEDSTR(PatchmeLockList, time), 0, TOK_FORMAT_LVWIDTH(150)},
	{ 0 }
};

AUTO_RUN;
void PatchmeRegisterParseTables(void)
{
	ParserSetTableInfo(parse_PatchmeLockList, sizeof(PatchmeLockList), "PatchmeLockListInfo", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_PatchmeHistory, sizeof(PatchmeHistory), "PatchmeHistoryInfo", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}


struct
{
	const char *localname;
	const char *last_author;
	const char *lock_author;
	const char *web_url;
	const char *branch_note;
	const char *timestamp_note;
	const char *prev_branch_note;
	const char *next_branch_note;
	PatchmeHistory **history;
	ListView *lvDetails;
	int history_mine_index;
	PatchmeLockList **lockList;
	ListView *lvLockList;

	int cur_branch;
	int latest_revision;
	bool in_db;

	PatcherFileHistory *history_data;
} stat_data;



static void singleDiff(ListView *lv, PatchmeHistory *gh, void *data)
{
	// Compares local to selected history entry
	int ret;
	char tempname[MAX_PATH];
	char localname[MAX_PATH];
	strcpy(tempname, patchmeGetTempFileName(stat_data.localname, gh->global_revision));
	strcpy(localname, stat_data.localname);

	gimmeUserLogf("\tSingle Diff: %s  branch %d rev %d", stat_data.localname, gh->branch, gh->version);

	patchmeGetSpecificVersionTo(stat_data.localname, tempname, gh->branch, gh->global_revision);

	ret = fileLaunchDiffProgram(backSlashes(localname), backSlashes(tempname));
	if (!ret) {
		printf("Error launching diff program (%s)\n", fileDetectDiffProgram(localname, tempname));
	}
}

static void viewFile(ListView *lv, PatchmeHistory *gh, void *data)
{
	// Views specific history entry
	const char *tempname = patchmeGetTempFileName(stat_data.localname, gh->global_revision);

	gimmeUserLogf("\tView File: %s  branch %d rev %d", stat_data.localname, gh->branch, gh->version);

	patchmeGetSpecificVersionTo(stat_data.localname, tempname, gh->branch, gh->global_revision);

	// Replaced with void version of fileOpenWithEditor()
	fileOpenWithEditor(tempname);

	/*
	ret = fileOpenWithEditor(tempname);
	if (!ret) {
		printf("Error opening viewer for file \"%s\"\n", tempname);
	}
	*/
}

static void getFile(ListView *lv, PatchmeHistory *gh, void *data)
{
	// Gets specific history entry
	gimmeUserLogf("\tGet File: %s  branch %d rev %d", stat_data.localname, gh->branch, gh->version);

	patchmeGetSpecificVersion(stat_data.localname, gh->branch, gh->global_revision);
}

static void doubleDiff(ListView *lv, PatchmeHistory *gh, void *data)
{
	// Compares two selected history entries
	static int count=0;
	static char firstFile[CRYPTIC_MAX_PATH];
	if (count==0) {
		strcpy(firstFile, patchmeGetTempFileName(stat_data.localname, gh->global_revision));
		patchmeGetSpecificVersionTo(stat_data.localname, firstFile, gh->branch, gh->global_revision);
		count++;
	} else {
		char secondFile[CRYPTIC_MAX_PATH];
		int ret;

		strcpy(secondFile, patchmeGetTempFileName(stat_data.localname, gh->global_revision));

		patchmeGetSpecificVersionTo(stat_data.localname, secondFile, gh->branch, gh->global_revision);
		gimmeUserLogf("\tDouble Diff: %s - %s", firstFile, secondFile);
		ret = fileLaunchDiffProgram(backSlashes(firstFile), backSlashes(secondFile));
		if (!ret) {
			printf("Error launching diff program (%s)\n", fileDetectDiffProgram(firstFile, secondFile));
		}
		count=0;
	}
}

static const char *findFileComment(int branch, int rev)
{
	FOR_EACH_IN_EARRAY(stat_data.history_data->checkins, Checkin, checkin)
	{
		if (checkin->branch == branch && checkin->rev == rev)
			return checkin->comment;
	}
	FOR_EACH_END;
	return NULL;
}

static const char *findFileBatchInfo(int branch, int rev)
{
	FOR_EACH_IN_EARRAY(stat_data.history_data->checkins, Checkin, checkin)
	{
		if (checkin->branch == branch && checkin->rev == rev)
			return eaGet(&stat_data.history_data->batch_info, icheckinIndex);
	}
	FOR_EACH_END;
	return NULL;
}

static void displayComments(ListView *lv, PatchmeHistory *gh, void *data)
{
	HWND wnd = (HWND)data;
	const char *comments = findFileComment(gh->branch, gh->global_revision);

	if (comments)
	{
		SetWindowTextCleanedup(wnd, comments);
	}
	else
	{
		SetWindowTextCleanedup(wnd, "");
	}
}

static void displayBatchInfo(ListView *lv, PatchmeHistory *gh, void *data)
{
	HWND wnd = (HWND)data;
	const char *batchinfo = findFileBatchInfo(gh->branch, gh->global_revision);

	if (batchinfo)
	{
		SetWindowTextCleanedup(wnd, batchinfo);
	}
	else
	{
		SetWindowTextCleanedup(wnd, "");
	}
}






//Resize all the items in the checkin window
static void DlgStatResize(HWND hDlg, int width, int height, LPRECT origRect)
{
	RECT windowRect;
	static RECT prevRect;
	static bool first = true;
	int widthDiff, heightDiff;
	if(first) {
		CopyRect(&prevRect, origRect);
		first = false;
	}
	GetClientRect(hDlg, &windowRect);

	widthDiff = (windowRect.right-windowRect.left) - (prevRect.right-prevRect.left);
	heightDiff = (windowRect.bottom-windowRect.top) - (prevRect.bottom-prevRect.top);

	#define DlgItemResize(item, flags) DlgItemDoResize(hDlg, GetDlgItem(hDlg, (item)), widthDiff, heightDiff, &windowRect, (flags));
	DlgItemResize(IDC_BTN_GOLOCAL,			RESZ_RIGHT);
	DlgItemResize(IDC_TXT_LOCAL,			RESZ_WIDTH);
	DlgItemResize(IDC_TXT_LASTAUTHOR,		RESZ_WIDTH);
	DlgItemResize(IDC_BTN_GOWEB,			RESZ_RIGHT);
	DlgItemResize(IDC_TXT_WEBURL,			RESZ_WIDTH);
	DlgItemResize(IDC_TXT_LOCKEE,			RESZ_WIDTH);
	DlgItemResize(IDC_TXT_NEXTBRANCH,		RESZ_WIDTH);
	DlgItemResize(IDC_TXT_PREVBRANCH,		RESZ_WIDTH);
	DlgItemResize(IDC_BTN_GET,				RESZ_RIGHT);
	DlgItemResize(IDC_BTN_VIEW,				RESZ_RIGHT);
	DlgItemResize(IDC_BTN_COMPARE,			RESZ_RIGHT);
	DlgItemResize(IDC_DETAILS,				RESZ_WIDTH|RESZ_HEIGHT);
	DlgItemResize(IDC_TAB,					RESZ_BOTTOM|RESZ_WIDTH);
	DlgItemResize(IDC_LST_LOCKS,			RESZ_BOTTOM|RESZ_WIDTH);
	DlgItemResize(IDC_TXT_COMMENTS,			RESZ_BOTTOM|RESZ_WIDTH);
	DlgItemResize(IDC_TXT_BRANCHNOTE,		RESZ_BOTTOM);
	DlgItemResize(IDOK,						RESZ_BOTTOM|RESZ_RIGHT);

	InvalidateRect(hDlg, NULL, true);
	GetClientRect(hDlg, &prevRect);
}

LRESULT CALLBACK DlgPatchmeStatProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	static RECT origRect;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_LOCAL), stat_data.localname);
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_LASTAUTHOR), stat_data.last_author);
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_WEBURL), stat_data.web_url);
			if (stat_data.lock_author) {
				SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_LOCKEE), stat_data.lock_author);
			} else {
				SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_LOCKEE), "Not checked out by anyone");
			}
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_TIMESTAMPS), stat_data.timestamp_note);
			// History List
			listViewInit(stat_data.lvDetails, parse_PatchmeHistory, hDlg, GetDlgItem(hDlg, IDC_DETAILS));
			for (i=0; i<eaSize(&stat_data.history); i++) {
				listViewAddItem(stat_data.lvDetails, eaGet(&stat_data.history, i));
				if (i == stat_data.history_mine_index) // Highlight the version we have locally
					listViewSetItemColor(stat_data.lvDetails, eaGet(&stat_data.history, i), RGB(10, 10, 10), RGB(200, 200, 200));
			}
			listViewSort(stat_data.lvDetails, 2); // Revision
			if (!(stat_data.history_data->flags & DIRENTRY_FROZEN))
				listViewSort(stat_data.lvDetails, 3); // Branch
			// Lock list
			listViewInit(stat_data.lvLockList, parse_PatchmeLockList, hDlg, GetDlgItem(hDlg, IDC_LST_LOCKS));
			for (i=0; i<eaSize(&stat_data.lockList); i++) {
				listViewAddItem(stat_data.lvLockList, eaGet(&stat_data.lockList, i));
			}
			listViewSort(stat_data.lvLockList, 1); // Branch
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_BRANCHNOTE), stat_data.branch_note);
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_PREVBRANCH), stat_data.prev_branch_note);
			SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_NEXTBRANCH), stat_data.next_branch_note);

			/*
			// We don't know this info on the client with the new gimme
			if (g_no_later) {
				// There is no later branch
				SendDlgItemMessage(hDlg, IDC_TXT_NEXTBRANCH, WM_ENABLE, FALSE, 0);
				//SendDlgItemMessage(hDlg, IDC_LBL_NEXTBRANCH, WM_ENABLE, FALSE, 0);
			}
			*/
			if (stat_data.cur_branch == 0) {
				SendDlgItemMessage(hDlg, IDC_TXT_PREVBRANCH, WM_ENABLE, FALSE, 0);
			}
			if (!stat_data.in_db) {
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_GOWEB), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_DIFF), FALSE);
			}
			EnableWindow(GetDlgItem(hDlg, IDC_BTN_COMPARE), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_BTN_RELINK), FALSE);
			if (stat_data.lock_author) { // Someone's got it checked out
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_CHECKOUT), FALSE);
			}
			if (stat_data.lock_author && stricmp(stat_data.lock_author, GimmeQueryUserName())==0) {
				// File is checked out by us
				
			} else {
				// Not checked out by us
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_CHECKIN), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNDO), FALSE);
				SetDlgItemText(hDlg, IDC_BTN_GOLOCAL, L"View");
				if (stat_data.lock_author) {
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

			GetClientRect(hDlg, &origRect);
			return FALSE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			case IDOK:
			case IDCANCEL:
				EndDialog(hDlg, 0);
				return TRUE;
			case IDC_BTN_GOWEB:
				openURL(stat_data.web_url);
				break;
			case IDC_BTN_DIFF:
				{
					char cmd[CRYPTIC_MAX_PATH];
					// run diff
					sprintf(cmd, "-diff \"%s\"", stat_data.localname);
					GimmeDoCommand(cmd);
				}
				break;
			case IDC_BTN_COMPARE:
				{
					// Diffs vs selected item in history view
					int count = listViewDoOnSelected(stat_data.lvDetails, NULL, NULL);
					if (count==1) {
						listViewDoOnSelected(stat_data.lvDetails, singleDiff, NULL);
					} else if (count==2) {
						listViewDoOnSelected(stat_data.lvDetails, doubleDiff, NULL);
					}
				}
				break;
			case IDC_BTN_VIEW:
				{
					// Diffs vs selected item in history view
					int count = listViewDoOnSelected(stat_data.lvDetails, NULL, NULL);
					if (count==1) {
						listViewDoOnSelected(stat_data.lvDetails, viewFile, NULL);
					}
				}
				break;
			case IDC_BTN_GET:
				{
					// Gets selected item in history view
					int count = listViewDoOnSelected(stat_data.lvDetails, NULL, NULL);
					if (count==1) {
						listViewDoOnSelected(stat_data.lvDetails, getFile, NULL);
						EndDialog(hDlg, 1);
					}
				}
				break;
			case IDC_BTN_CHECKOUT:
				gimmeUserLogf("\tCheckout: %s", stat_data.localname);
				if (NO_ERROR==GimmeDoOperation(stat_data.localname, GIMME_CHECKOUT, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_UNDO:
				gimmeUserLogf("\tUndo: %s", stat_data.localname);
				if (NO_ERROR==GimmeDoOperation(stat_data.localname, GIMME_UNDO_CHECKOUT, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_CHECKIN:
				{
					int ret;
					gimmeUserLogf("\tCheckin: %s", stat_data.localname);
					ret=GimmeDoOperation(stat_data.localname, GIMME_CHECKIN, 0);
					if (ret==NO_ERROR)
						EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_REMOVE:
				gimmeUserLogf("\tRemove: %s", stat_data.localname);
				if (NO_ERROR==GimmeDoOperation(stat_data.localname, GIMME_DELETE, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_GLV:
				gimmeUserLogf("\tGet Latest Version: %s", stat_data.localname);
				if (NO_ERROR==GimmeDoOperation(stat_data.localname, GIMME_GLV, 0)) {
					EndDialog(hDlg, 1);
				}
				break;
			case IDC_BTN_GOLOCAL:
				fileOpenWithEditor(stat_data.localname);
				break;
			/* Not supported
			case IDC_BTN_RELINK:
				{
					listViewDoOnSelected(lvDetails, relinkFile, NULL);
					EndDialog(hDlg, 1);
				}
				break;
			*/
		}
		break;

	case WM_NOTIFY:
		{
			int idCtrl = (int)wParam;
			int iTabPage = TabCtrl_GetCurSel(GetDlgItem(hDlg, IDC_TAB));
			BOOL bRet=FALSE;
			bRet |= listViewOnNotify(stat_data.lvDetails, wParam, lParam, NULL);
			bRet |= listViewOnNotify(stat_data.lvLockList, wParam, lParam, NULL);

			// comments
			if (iTabPage == 1)
			{
				if (listViewDoOnSelected(stat_data.lvDetails, displayComments, GetDlgItem(hDlg, IDC_TXT_COMMENTS)) == 0)
				{
					const char *comments = findFileComment(stat_data.cur_branch, stat_data.latest_revision);
					if (comments)
					{
						SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_COMMENTS), comments);
					}
					else
					{
						SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_COMMENTS), "");
					}
				}
			}


			// batch checkin info
			if (iTabPage == 2)
			{
				if (listViewDoOnSelected(stat_data.lvDetails, displayBatchInfo, GetDlgItem(hDlg, IDC_TXT_COMMENTS)) == 0)
				{
					const char *batchinfo = findFileBatchInfo(stat_data.cur_branch, stat_data.latest_revision);
					if (batchinfo)
					{
						SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_COMMENTS), batchinfo);
					}
					else
					{
						SetWindowTextCleanedup(GetDlgItem(hDlg, IDC_TXT_COMMENTS), "");
					}
				}
			}

			if (idCtrl == IDC_DETAILS) {
				// Count number of selected elements
				i = listViewDoOnSelected(stat_data.lvDetails, NULL, NULL);
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
			if (bRet)
				return TRUE;
		}
		break;
	case WM_SIZING:
		DlgWindowCheckResize(hDlg, (U32)wParam, (LPRECT)lParam, &origRect);
		return TRUE;
	case WM_SIZE:
		DlgStatResize(hDlg, LOWORD(lParam), HIWORD(lParam), &origRect);
		break;
	}
	return FALSE;
}






int patchmeStatShow(const char *fname, bool graphical, PatcherFileHistory *history)
{
	char url[MAX_PATH];
	char root[MAX_PATH];
	char branch_note[128];
	char timestamp_note[128];
	int i;
	int ret=0;
	TriviaList *trivia;
	PatchmeHistory *latest_elem=NULL;
	PatchmeHistory *highest_elem=NULL;
	// Gather data
	ZeroStruct(&stat_data);

	trivia = triviaListGetPatchTriviaForFile(fname, SAFESTR(root));
	if(trivia)
	{
		const char *project = triviaListGetValue(trivia,"PatchProject");
		const char *server = triviaListGetValue(trivia,"PatchServer");
		assert(strStartsWith(fname, root));
		sprintf(url, "http://%s/%s/file%s/", server, project, fname + strlen(root));
		stat_data.web_url = url;
	}

	stat_data.history_data = history;
	stat_data.history_mine_index = -1;
	stat_data.localname = fname;
	stat_data.cur_branch = GimmeQueryBranchNumber(fname);
	sprintf(branch_note, "You are working on branch %d (%s)", stat_data.cur_branch, GimmeQueryBranchName(fname));
	stat_data.branch_note = branch_note;

	patchmeQueryLastAuthor(fname, &stat_data.last_author);
	stat_data.in_db = !(stricmp(stat_data.last_author, GIMME_GLA_ERROR)==0);
	patchmeQueryIsFileLocked(fname, &stat_data.lock_author);

	if (history->dir_entry) {
		assert(eaSize(&history->dir_entry->versions) == eaSize(&history->checkins));

		for (i=0; i<eaSize(&history->dir_entry->versions); i++) {
			PatchmeHistory *elem = StructAlloc(parse_PatchmeHistory);
			FileVersion *ver = history->dir_entry->versions[i];
			Checkin *checkin = history->checkins[i];
			elem->time_modtime = ver->modified;
			elem->time_checkintime = checkin->time;;
			printDate(elem->time_modtime, elem->modtime);
			printDate(elem->time_checkintime, elem->checkintime);
			if (ver->flags & FILEVERSION_DELETED) {
				char buf[1024];
				sprintf(buf, "%s (DELETED)", checkin->author);
				elem->author = allocAddString(buf);
			} else {
				elem->author = checkin->author;
			}
			elem->version = ver->version;
			elem->global_revision = ver->rev;
			elem->branch = checkin->branch;
			elem->size = ver->size;
			if (history->flags & DIRENTRY_FROZEN) {
				if (!latest_elem)
					latest_elem = elem;
				else if (elem->global_revision > latest_elem->global_revision)
					latest_elem = elem;
			} else {
				if (elem->branch <= stat_data.cur_branch) {
					if (!latest_elem)
						latest_elem = elem;
					else if (elem->branch > latest_elem->branch ||
							elem->branch == latest_elem->branch && elem->version > latest_elem->version)
						latest_elem = elem;
				}
				if (!highest_elem)
					highest_elem = elem;
				else if (elem->branch > highest_elem->branch ||
						elem->branch == highest_elem->branch && elem->version > highest_elem->version)
					highest_elem = elem;
			}
			eaPush(&stat_data.history, elem);
			if (elem->time_modtime == fileLastChanged(fname))
				stat_data.history_mine_index = i;
		}
		for (i=0; i<eaSize(&history->dir_entry->checkouts); i++) {
			PatchmeLockList *lock = StructAlloc(parse_PatchmeLockList);
			Checkout *checkout = history->dir_entry->checkouts[i];
			lock->branch = checkout->branch;
			lock->lockee = checkout->author;
			printDate(checkout->time, lock->time);
			eaPush(&stat_data.lockList, lock);
		}
	}

	// Determine branch linkage status
	if (latest_elem)
	{
		stat_data.latest_revision = latest_elem->global_revision;
		if (history->flags & DIRENTRY_FROZEN) {
			stat_data.prev_branch_note = "This file is branch frozen - branch numbers are ignored.";
		} else if (stat_data.cur_branch==0) {
			stat_data.prev_branch_note = "This file has no previous branches to inherit from.";
		} else if (latest_elem->branch == stat_data.cur_branch) {
			stat_data.prev_branch_note = "Link BROKEN; This file's link to the previous branch has been broken, no changes will be inherited.";
		} else {
			stat_data.prev_branch_note = "LINKED; This file is still linked to the previous branch, any changes in the previous branch will be reflected in this one.  WARNING: Any changes in this branch will sever the link.";
		}
		if (history->flags & DIRENTRY_FROZEN) {
			stat_data.next_branch_note = "This file is branch frozen - branch numbers are ignored.";
		} else if (highest_elem->branch > stat_data.cur_branch) {
			// link broken
			stat_data.next_branch_note = "Link BROKEN; This file's link has been broken, changes to this file will ONLY AFFECT THIS BRANCH.  Bug fixes will need to be manually applied to the later branch(es).";
		} else {
			stat_data.next_branch_note = "LINKED; Changes to this file will affect both this and the later branch.";
		}
	} else {
		stat_data.prev_branch_note = stat_data.next_branch_note = "Not in database";
	}

	// Timestamps
	if (latest_elem)
	{
		bool b;
		U32 timestamp = fileLastChangedAbsolute(fname);
		b = (timestamp == latest_elem->time_modtime);
		if (b) {
			strcpy(timestamp_note, "File timestamps are identical");
		} else if (timestamp > latest_elem->time_modtime) {
			strcpy(timestamp_note, "Local version has a newer timestamp");
			if (timestamp ==  latest_elem->time_modtime + 3600)
				strcat(timestamp_note, ", by exactly 1 hour");
		} else {
			strcpy(timestamp_note, "Version in database has a newer timestamp");
			if (timestamp + 3600 ==  latest_elem->time_modtime)
				strcat(timestamp_note, ", by exactly 1 hour");
		}
	} else {
		strcpy(timestamp_note, "Not in database");
	}

	stat_data.timestamp_note = timestamp_note;


	// Display results
	if (!graphical) {
		printf(" o Local file: ");
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
		printf("%s\n", fname);
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		printf(" o Last Author/Status: ");
		consoleSetFGColor(COLOR_GREEN | COLOR_BRIGHT);
		printf("%s\n", stat_data.last_author);
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		printf(" o File is ");
		if (stat_data.lock_author) {
			consoleSetFGColor(COLOR_GREEN | COLOR_BRIGHT);
			printf("checked out by %s\n", stat_data.lock_author);
		} else {
			consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
			printf("not checked out by anyone.\n");
		}
		consoleSetFGColor(COLOR_GREEN | COLOR_RED | COLOR_BLUE);

		printf(" o Timestamps: %s\n", stat_data.timestamp_note);
		printf(" o %s\n", stat_data.prev_branch_note);
		printf(" o %s\n", stat_data.next_branch_note);

		for (i=0; i<eaSize(&stat_data.history); i++) {
			PatchmeHistory *elem = stat_data.history[i];
			printf("%20s %20s r:%3d b:%2d %10db %s\n", elem->checkintime, elem->modtime, elem->version, elem->branch, elem->size, elem->author);
		}
		for (i=0; i<eaSize(&stat_data.lockList); i++) {
			PatchmeLockList *elem = stat_data.lockList[i];
			printf("Checked out by %s on branch %d, %20s\n", elem->lockee, elem->branch, elem->time);
		}
		printf("\n");
		printf(" o %s\n", stat_data.branch_note);
	} else {
		// Graphical view
		stat_data.lvDetails = listViewCreate();
		stat_data.lvLockList = listViewCreate();
		ret = DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_PATCHME_STAT), NULL, (DLGPROC)DlgPatchmeStatProc);
		listViewDestroy(stat_data.lvDetails);
		listViewDestroy(stat_data.lvLockList);
		//destroyFileComments();
		//destroyFileBatchInfo();
	}
	eaDestroyStruct(&stat_data.history, parse_PatchmeHistory);
	eaDestroyStruct(&stat_data.lockList, parse_PatchmeLockList);
	if (trivia)
		triviaListDestroy(&trivia);
	return ret;
}
