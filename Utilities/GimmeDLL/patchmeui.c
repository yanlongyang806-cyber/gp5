// Gentlemen, what you are about to see is a nightmare inexplicably torn from the pages of Kafka! (or gimmeUtil.c)

#include "GimmeUtil.h"
#include "patchmeui.h"
#include "wininclude.h"
#include "winutil.h"
#include "earray.h"
#include "file.h"
#include "EString.h"
#include "utils.h"
#include "shlobj.h"
#include "strings_opt.h"
#include "pcl_typedefs.h"
#include "gimmeDLLPrivateInterface.h"
#include "logging.h"
#include "patchcommonutils.h"
#include "trivia.h"
#include "patchtrivia.h"
#include "StringCache.h"
#include <Windowsx.h>
#include "ObjIdl.h"
#include "UTF8.h"
#include "StringUtil.h"

//////////////////////////////////////////////////////////////////////////////

#include "RegistryReader.h"

extern int g_patchme_simulate;
extern int g_patchme_notestwarn;

static void getLastCommentsFromRegistry(char** comments, char** patchnotes) {
	RegReader reader;
	char commentBuffer[2048];

	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");

	if (!rrReadString(reader, "LastComments", SAFESTR(commentBuffer)))
		commentBuffer[0]=0;
	*comments = strdup(commentBuffer);

	if (!rrReadString(reader, "LastPatchNotes", SAFESTR(commentBuffer)))
		commentBuffer[0]=0;
	*patchnotes = strdup(commentBuffer);

	destroyRegReader(reader);
}

static void storeLastComments(char *comments, char* patchnotes) {
	RegReader reader;

	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme");
	rrWriteString(reader, "LastComments", NULL_TO_EMPTY(comments));
	rrWriteString(reader, "LastPatchNotes", NULL_TO_EMPTY(patchnotes));
	destroyRegReader(reader);
}

//////////////////////////////////////////////////////////////////////////////

#include "textparser.h"
#include "timing.h"

typedef enum GVEType {
	GVE_START,
	GVE_BADFILE,
	GVE_RELOAD,
} GVEType;

typedef struct GimmeVerifyEvent {
	GVEType type;
	time_t time;
	char *file;
	char **ignoredFiles;
	int uid; // for resolving identical times
} GimmeVerifyEvent;

static GimmeVerifyEvent **eventList = {0};

static int evCmp(const GimmeVerifyEvent **ev0, const GimmeVerifyEvent **ev1)
{
	if ((*ev0)->time == (*ev1)->time)
		return (*ev0)->uid - (*ev1)->uid;
	return (*ev0)->time - (*ev1)->time;
}

static char **parseIgnoreFiles(char *str)
// Format is: IGNORE:v_,d_
{
	char *last=NULL;
	char **ret=0;
	char *s;
	if (!strStartsWith(str, "IGNORE:"))
		return NULL;
	str += strlen("IGNORE:");
	while (s = strtok_r(str, ",", &last)) {
		str = NULL;
		eaPush(&ret, StructAllocString(s));
	}
	return ret;
}

static void parseErrorLog(char *logfile)
{
	int len;
	char *data;
	char *s;
	char *args[16];
	int count;
	if (!fileExists(logfile))
		return;
	data = fileAlloc(logfile, &len);
	s = data;
	while(s)
	{
		count=tokenize_line_safe(s,args,ARRAY_SIZE(args),&s);
		if (count >= 5) {
			GimmeVerifyEvent *event = calloc(sizeof(GimmeVerifyEvent), 1);
			char *s = args[4];
			event->time = timeGetTimeFromDateString(args[0], args[1]);
			event->uid = atoi(args[3]);
			// In the latest log format, args[2] is the id, and args[4] is the type of line
			if(event->uid == 0)
				event->uid = atoi(args[2]);

			if (stricmp(s, "STARTING")==0) {
				event->type = GVE_START;
				event->file = StructAllocString(logfile);
				if (count >= 6)
					event->ignoredFiles = parseIgnoreFiles(args[5]);
				else
					event->ignoredFiles = NULL;
			} else if (stricmp(s, "FILEERROR:")==0 && count >= 6) {
				event->type = GVE_BADFILE;
				if (args[5][0]=='/')
					args[5]++;
				event->file = StructAllocString(args[5]);
			} else if (stricmp(s, "FILERELOAD:")==0 && count >= 6) {
				event->type = GVE_RELOAD;
				if (args[5][0]=='/')
					args[5]++;
				event->file = StructAllocString(args[5]);
			} else {
				if (count>=6)
				{
					char *s = args[5];
					char buf[128];
					sprintf(buf, "%s %s", args[0], args[1]);
					event->time = timeMakeLocalTimeFromSecondsSince2000(
						timeGetSecondsSince2000FromLogDateString(buf));
					event->uid = atoi(args[2]);
					if (stricmp(s, "STARTING")==0) {
						event->type = GVE_START;
						event->file = StructAllocString(logfile);
						if (count >= 8)
							event->ignoredFiles = parseIgnoreFiles(args[7]);
						else
							event->ignoredFiles = NULL;
					} else if (stricmp(s, "FILEERROR:")==0 && count >= 7) {
						event->type = GVE_BADFILE;
						if (args[6][0]=='/')
							args[6]++;
						event->file = StructAllocString(args[6]);
					} else if (stricmp(s, "FILERELOAD:")==0 && count >= 7) {
						event->type = GVE_RELOAD;
						if (args[6][0]=='/')
							args[6]++;
						event->file = StructAllocString(args[6]);
					} else {
						free(event);
						event = NULL;
					}
				} else {
					free(event);
					event = NULL;
				}
			}
			if (event) {
				eaPush(&eventList, event);
			}
		} else {
			//printf("Error in log file around %s\n", args[0]);
		}
	}
	fileFree(data);
}

static void loadEventList(char *root)
{
	char loggamepath[CRYPTIC_MAX_PATH], logmapserverpath[CRYPTIC_MAX_PATH];
	sprintf(loggamepath, "%s/logs", root);
	strcpy(logmapserverpath, loggamepath);
	// Check for CrypticEngine paths instead of CoH
	strcat(loggamepath, "/GameClient/errorLogLastRun.log");
	if (fileExists(loggamepath)) {
		strcat(logmapserverpath, "/GameServer/errorLogLastRun.log");
	}
	eaDestroy(&eventList);
	parseErrorLog(loggamepath);
	parseErrorLog(logmapserverpath);
	// sort
	eaQSort(eventList, evCmp);
}

static bool matchesIgnore(char *relpath, char **ignores)
{
	int i;
	char path[CRYPTIC_MAX_PATH];
	strcpy(path, relpath);
	forwardSlashes(path);
	for (i=0; i<eaSize(&ignores); i++) {
		char ig[16];
		sprintf(ig, "/%s", ignores[i]);
		if (strStartsWith(path, ig+1))
			return true;
		if (strstri(path, ig))
			return true;
	}
	return false;
}

static PCL_DiffType getTestInfo(char *root, char *dbname)
{
	int i;
	time_t modificationTime;
	char fullpath[MAX_PATH];
	char *relpath = dbname;
	PCL_DiffType status = PCLDIFF_NOTTESTED;

	if(eaSize(&eventList)==0) // No log file, could be an old branch
		return PCLDIFF_PRESUMEDGOOD;

	sprintf(fullpath, "%s/%s", root, dbname);
	modificationTime = fileLastChanged(fullpath);

	// Convert from "data/path/file.ext" to "path/file.ext"
	relpath = strchr(relpath, '/');
	if(!relpath)
		return PCLDIFF_PRESUMEDGOOD;
	relpath++; // skip the slash

	// Go through events list and find mo
	for(i = 0; i < eaSize(&eventList); i++)
	{
		GimmeVerifyEvent *event = eventList[i];
		switch (event->type) {
			case GVE_RELOAD:
				if (!simpleMatch(event->file, relpath))
					break;
				// fall-through
			case GVE_START:
				if (modificationTime <= event->time) {
					if (matchesIgnore(relpath, event->ignoredFiles)) {
						if (status == PCLDIFF_NOTTESTED)
							status = PCLDIFF_TESTEXCLUDED;
						// If already flagged as good or bad, assume it hasn't changed
					} else {
						status = PCLDIFF_PRESUMEDGOOD;
					}
				}
				break;
			case GVE_BADFILE:
				if (!simpleMatch(event->file, relpath))
					break;
				if (modificationTime <= event->time)
					status = PCLDIFF_TESTEDBAD;
				break;
		}
	}

	return status;
}

void patchmeGetTestInfo(char *root, char **filenames, PCL_DiffType *diff_types)
{
	int i;
	loadEventList(root);
	for(i = 0; i < eaSize(&filenames); i++)
		if((diff_types[i] & PCLDIFFMASK_TESTING) == PCLDIFF_NEEDSTESTING)
			diff_types[i] |= getTestInfo(root, filenames[i]); // or is okay, since PCLDIFF_NEEDSTESTING == 0
}

//////////////////////////////////////////////////////////////////////////////

#include "resource.h"
#include "commctrl.h"
#include "genericDialog.h"
#include "sysutil.h"

#include "patchme.h"

typedef struct
{
	char *fname;
	PCL_DiffType diff_type; // GimmeQueuedAction *action;
	bool empty_file;
	bool too_long;
	HWND checkbox; // TODO: it'd be great if it were a pulldown with revert, checkin, checkpoint, etc.
	char *real_fname;
} PatchMeCheckinFile;

static PatchMeCheckinFile **g_patchmefilenames=NULL;
static char *g_warning = NULL;
static char *spWarningAuthorizeName = NULL;
static char *g_notes = NULL;
static char *spBackupPath = NULL;
static bool s_disable_dialog = false;
static char *spComments = NULL;
static char *spPatchNotes = NULL;
static char *g_comments_and_patchnotes = NULL;
static char ***g_patchmefnames;
static PCL_DiffType **g_diff_types;
static char *g_branch_note;
static GIMMEOperation g_patchme_checkin_op;


static LRESULT CALLBACK DlgCheckinVerifyProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK DlgCheckinProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK DlgConfirmProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);

static void appendFilename(char *filename, PCL_DiffType diff_type, bool empty_file, bool too_long, char *real_filename) // , GimmeQueuedAction *action) 
{
	PatchMeCheckinFile *cfile = NULL;

	cfile = (PatchMeCheckinFile*)malloc(sizeof(PatchMeCheckinFile));
	cfile->fname = strdup(filename);
	cfile->diff_type = diff_type; //	cfile->action = action;
	cfile->empty_file = empty_file;
	cfile->too_long = too_long;
	cfile->real_fname = strdup(real_filename);
	eaPush(&g_patchmefilenames, cfile);
}

static void clearFilenames(void)
{
	int i;
	for(i = 0; i < eaSize(&g_patchmefilenames); ++i)
	{
		SAFE_FREE(g_patchmefilenames[i]->fname);
		SAFE_FREE(g_patchmefilenames[i]->real_fname);
		free(g_patchmefilenames[i]);
	}
	eaSetSize(&g_patchmefilenames, 0);
}

int patchmeDialogConfirm(char *root, char ***filenames, PCL_DiffType **diff_types)
{
	int ret, i;
	bool any_new = false;
	bool any_undo = false;
	for(i = 0; i < eaSize(filenames); i++)
	{
		char buf[4096], *extra = "", real_filename[MAX_PATH];
		if(((*diff_types)[i] & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED)
		{
			any_new = true;
			extra = " (New file)";
		}
		else if(((*diff_types)[i] & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE)
		{
			any_undo = true;
			extra = " (no changes detected, undoing checkout)";
		}
		sprintf(buf, "%s/%s%s", root, (*filenames)[i], extra);
		sprintf(real_filename, "%s/%s", root, (*filenames)[i]);
		appendFilename(buf, (*diff_types)[i], false, false, real_filename);
	}

	// Confirm if files checked out
	estrStackCreate(&g_notes);
	estrPrintf(&g_notes, "You currently have the files listed above checked out.  These files will be backed up so that when you return to this branch they will still be checked out.\r\n");
	if(any_undo)
		estrConcatf(&g_notes, "Some of the files have not been changed, so their checkouts will be automatically undone before switching branches.\r\n");
	if(any_new)
		estrConcatf(&g_notes, "Some of the files are new files that do not exist in the database, they need to be removed in order to switch branches, so they will be backed up as well.\r\n");
	estrConcatf(&g_notes, "If you are not planning on returning to this branch, please check in the above files before switching branches.\r\n");
	estrConcatf(&g_notes, "\r\nAre you sure you want to switch branches?");

	ret = (int)DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_CONFIRM), NULL, (DLGPROC)DlgConfirmProc);
	clearFilenames();
	estrDestroy(&g_notes);
	return ret;
}

static LRESULT CALLBACK DlgConfirmProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i; 
			char *estrFilenames = NULL;
			estrStackCreate(&estrFilenames);
			for(i = 0; i < eaSize(&g_patchmefilenames); ++i)
				estrConcatf(&estrFilenames, "%s\r\n", g_patchmefilenames[i]->fname);
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


// Set default folder in DlgBrowseForFolder().
int CALLBACK DlgBrowseForFolderCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	switch (uMsg)
	{
		// Initialize to specified directory.
		case BFFM_INITIALIZED:
			SendMessage(hwnd, BFFM_SETSELECTION, 1, lpData);
			break;

		// Allow invalid paths.
		case BFFM_VALIDATEFAILED:
			return 1;
	}

	return 0;
}

// Browse for a folder.
static void DlgBrowseForFolder(HWND parent, char **ppBackupPath)
{
	BROWSEINFO bi = {0};
	LPITEMIDLIST list;

	assertmsgf(0, "AaronL says this function is obsolete, so I haven't actually tested it in UTF16 mode. If you want it to work, take out this assert and cross your fingers");



	bi.lpszTitle = L"Choose backup folder";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lParam = (LPARAM)UTF8_To_UTF16_malloc(*ppBackupPath);;
	bi.lpfn = DlgBrowseForFolderCallbackProc;
	list = SHBrowseForFolder(&bi);
	SAFE_FREE((void*)bi.lParam);
	if (list)
	{
		IMalloc *imalloc = 0;

		// Get name.
		SHGetPathFromIDList_UTF8(list, ppBackupPath);

		// Free list.
		if (!SHGetMalloc(&imalloc))
		{
			imalloc->lpVtbl->Free(imalloc, list);
			imalloc->lpVtbl->Release(imalloc);
		}
	}
}

// WindowProc for patchmeDialogBackup().
static LRESULT CALLBACK DlgBackupProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i; 
			char *estrFilenames = NULL;
			estrStackCreate(&estrFilenames);
			for(i = 0; i < eaSize(&g_patchmefilenames); ++i)
				estrConcatf(&estrFilenames, "%s\r\n", g_patchmefilenames[i]->fname);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), estrFilenames);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LOCATION), spBackupPath);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_NOTES), g_notes);

			flashWindow(hDlg);
			return FALSE;
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_LOCATION), &spBackupPath);
			EndDialog(hDlg, 1);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, 0);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDC_BROWSE)
		{
			DlgBrowseForFolder(hDlg, &spBackupPath);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LOCATION), spBackupPath);
			return TRUE;
		}

		break;
	}
	return FALSE;
}

// Backup confirmation dialog
int patchmeDialogBackup(char *root, char ***filenames, PCL_DiffType **diff_types, char *backup_path, size_t backup_path_size)
{
	int ret, i;
	bool any_missing = false;

	// Process backup file list.
	for(i = 0; i < eaSize(filenames); i++)
	{
		char buf[4096], *extra = "", real_filename[MAX_PATH];
		if(((*diff_types)[i] & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED)
			extra = " (New file)";
		else if(((*diff_types)[i] & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE)
			continue;
		else if(((*diff_types)[i] & PCLDIFFMASK_ACTION) == PCLDIFF_DELETED)
		{
			any_missing = true;
			extra = " (Missing, won't be backed up)";
		}
		sprintf(buf, "%s/%s%s", root, (*filenames)[i], extra);
		sprintf(real_filename, "%s/%s", root, (*filenames)[i]);
		appendFilename(buf, (*diff_types)[i], false, false, real_filename);
	}

	// Set backup path.
	estrCopy2(&spBackupPath, backup_path);

	// Display dialog.
	estrStackCreate(&g_notes);
	estrCopy2(&g_notes, "You currently have the files listed above checked out with modifications, or they are new files.\r\n\r\n");
	if (any_missing)
		estrAppend2(&g_notes, "Some of the above files have been deleted from your working copy.  The deletion of these files will not be backed up."
		"  If you restore from this backup to a fresh working copy, you will need to delete these files again.\r\n\r\n");
	estrAppend2(&g_notes, "Warning: Files which would normally be ignored by Gimme Checkin will not be backed up.  This includes files that are in directories"
		" which are not under source control, files which have been excluded by a filespec, and local modifications to existing files which are"
		" not checked out (for instance, those that have had the read-only attribute manually removed).");
	ret = (int)DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_BACKUP), NULL, (DLGPROC)DlgBackupProc);
	estrDestroy(&g_notes);
	clearFilenames();

	return ret;
}

// Fill in default path.
static int promptBackupPathProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_INPUT_STRING), spBackupPath);
			break;
	}
	return FALSE;
}

// Prompt user for backup restore path.
int patchmeDialogRestorePath(char *backup_path, size_t backup_path_size)
{
	char *result;
	estrCopy2(&spBackupPath, backup_path);
	result = requestStringDialogEx("Location of backup that should be restored", "Gimme Restore From Backup", promptBackupPathProc);
	if (result)
		strcpy_s(SAFESTR2(backup_path), result);
	return !!result;
}

// WindowProc for patchmeDialogRestore().
static LRESULT CALLBACK DlgRestoreProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i; 
			char *estrFilenames = NULL;
			estrStackCreate(&estrFilenames);
			for(i = 0; i < eaSize(&g_patchmefilenames); ++i)
				estrConcatf(&estrFilenames, "%s\r\n", g_patchmefilenames[i]->fname);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), estrFilenames);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LOCATION), spBackupPath);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_NOTES), g_notes);
			if (s_disable_dialog)
				EnableWindow(GetDlgItem(hDlg, IDOK), false);

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

// Backup restore dialog
int patchmeDialogRestore(char *root, const char *backup_path, char ***filenames, PCL_DiffType **diff_types)
{
	int ret, i;
	bool any_conflict = false;
	bool any_checkouts = false;

	// Scan filenames.
	for(i = 0; i < eaSize(filenames); i++)
	{
		char buf[4096], *extra = "", real_filename[MAX_PATH];
		PCL_DiffType diff_type = (*diff_types)[i];
		if((diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED)
		{
			extra = " (New file)";
		}
		else if((diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE)
		{
			extra = " (no changes detected)";
		}
		else if((diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CHANGED)
		{
			if (diff_type & PCLDIFF_CONFLICT)
			{
				extra = " (CONFLICT)";
				any_conflict = true;
			}
			else if (diff_type & PCLDIFF_NEEDCHECKOUT)
			{
				extra = " (Needs check out)";
				any_checkouts = true;
			}
			else
			{
				extra = " (Changed)";
			}
		}
		sprintf(buf, "%s/%s%s", root, (*filenames)[i], extra);
		sprintf(real_filename, "%s/%s", root, (*filenames)[i]);
		appendFilename(buf, (*diff_types)[i], false, false, real_filename);
	}

	// Display appropriate restore notes.
	s_disable_dialog = false;
	estrStackCreate(&g_notes);
	estrPrintf(&g_notes, "The above files will be restored.\r\n\r\n");
	if(any_checkouts)
		estrAppend2(&g_notes, "Some of the above files are not checked out.  They will be checked out before restoring, and restore will fail if they can't be checked out for some reason.\r\n\r\n");
	if(any_conflict)
	{
		s_disable_dialog = true;
		estrCopy2(&g_notes, "ERROR: There are conflicts!\r\n\r\nSome of these files conflict with existing changes in your working copy.  You cannot restore from a backup until these files have been reverted.  You can revert these files by doing an undo checkout.\r\n");
	}

	ret = (int)DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_RESTORE), NULL, (DLGPROC)DlgRestoreProc);
	clearFilenames();
	estrDestroy(&g_notes);
	return ret;
}

// i want this particular message box to say "yes to all" "yes" and "no", instead of the default "ok to all" buttons
static int okToAllCancelCallback(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_OKTOALL), "Yes To All");
		SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), "Yes");
		SetWindowText_UTF8(GetDlgItem(hDlg, IDCANCEL), "No");
		break;
	}
	return FALSE;
}

void createFinalComment(char* comments, char* patchnotes)
{
	if (g_comments_and_patchnotes)
		free(g_comments_and_patchnotes);

	if (comments)
	{
		if(patchnotes && patchnotes[0])
		{
			size_t n = strlen(comments) + strlen(patchnotes) + 64;
			g_comments_and_patchnotes = malloc(n);
			snprintf_s(g_comments_and_patchnotes, n-1, "COMMENTS:\r\n%s\r\n\r\nPATCHNOTES:\r\n%s\r\n", comments, patchnotes);
		}
		else
		{
			size_t n = strlen(comments) + 1;
			g_comments_and_patchnotes = malloc(n);
			strcpy_s(g_comments_and_patchnotes, n, comments);
		}
	}
	else
	{
		g_comments_and_patchnotes = malloc(1);
		g_comments_and_patchnotes[0] = 0;
	}
}

const char* patchmeDialogCheckinGetComments(void)
{
	return g_comments_and_patchnotes;
}

static void getCommentsFromDialog(HWND hDlg) 
{
	estrDestroy(&spComments);
	GetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT), &spComments);

	estrDestroy(&spPatchNotes);
	if (!IsDlgButtonChecked(hDlg, IDC_PATCHNOTES_CHECKBOX))
	{
		GetWindowText_UTF8(GetDlgItem(hDlg, IDC_PATCHNOTES), &spPatchNotes);
	}
}

// Returns 1 if they entered comments and clicked OK, 0 if they canceled
int patchmeDialogCheckin(GIMMEOperation op, PCL_Client *client, char *root, char ***fnames, PCL_DiffType **diff_types)
{
	int i;
	char *log_message=NULL;
	char *warning = NULL, *prevbranchname = NULL;
	char *empty_files = NULL, *too_long_files = NULL;
	int empty_file_count = 0, too_long_count = 0;
	int ret, prevbranch = -1;
	bool oktoalllinkbreaks = false, oktoallbrokenlinks = false;
	char branch_note[1024];
	g_patchmefnames = fnames;
	g_diff_types = diff_types;
	sprintf(branch_note, "You are working on branch %d (%s)", GimmeQueryBranchNumber(root), GimmeQueryBranchName(root));
	g_branch_note = branch_note;
	g_patchme_checkin_op = op;

	if(!eaSize(fnames))
		return 0;

	estrStackCreate(&g_warning); // for untested files
	estrStackCreate(&warning); // for warning messages

	for(i = 0; i < eaSize(fnames); i++)
	{
		char path[MAX_PATH];
		char buf[4096];
		bool empty_file, too_long;
		PCL_DiffType type = (*diff_types)[i] & PCLDIFFMASK_ACTION;
		PCL_DiffType testing = (*diff_types)[i] & PCLDIFFMASK_TESTING;

		sprintf(path, "%s/%s", root, (*fnames)[i]);
		empty_file = fileSize(path) == 0;
		too_long = strlen((*fnames)[i]) > PATCH_MAX_PATH;	//should match checks in pcl_client.c/checkinSendNames and patchserver.c/fileCanBeCheckedIn

		STR_COMBINE_BEGIN(buf);
		if(type != PCLDIFF_CHANGED || empty_file || ((*diff_types)[i] & PCLDIFF_LINKWILLBREAK) || ((*diff_types)[i] & PCLDIFF_BLOCKED))
		{
			bool bFirst=true;
			STR_COMBINE_CAT_C('(');
			if(type == PCLDIFF_CREATED) {
				STR_COMBINE_CAT("NEW");
				bFirst = false;
			} else if(type == PCLDIFF_DELETED) {
				STR_COMBINE_CAT("DELETED");
				bFirst = false;
			} else if(type == PCLDIFF_NOCHANGE) {
				if ((*diff_types)[i] & PCLDIFF_NEWFILE) {
					STR_COMBINE_CAT("NEW, DELETING");
					bFirst = false;
				} else {
					STR_COMBINE_CAT("UNDO CHECKOUT");
					bFirst = false;
				}
			}
			if ((*diff_types)[i] & PCLDIFF_BLOCKED) {
				if (!bFirst)
					STR_COMBINE_CAT(", ");
				STR_COMBINE_CAT("BLOCKED");
				bFirst = false;
			}
			if(type == PCLDIFF_CREATED && too_long) {
				if (!bFirst)
					STR_COMBINE_CAT(", ");
				STR_COMBINE_CAT("PATH TOO LONG");
				bFirst = false;
			}
			if ((*diff_types)[i] & PCLDIFF_LINKWILLBREAK) {
				if (!bFirst)
					STR_COMBINE_CAT(", ");
				STR_COMBINE_CAT("LinkBreak");
				bFirst = false;
			}
			if(type != PCLDIFF_DELETED && empty_file) {
				if (!bFirst)
					STR_COMBINE_CAT(", ");
				STR_COMBINE_CAT("EMPTY");
				bFirst = false;
			}
			STR_COMBINE_CAT(") ");
		}
		STR_COMBINE_CAT(path);
		STR_COMBINE_END(buf);
		if(empty_file && type != PCLDIFF_DELETED && type != PCLDIFF_NOCHANGE) // don't warn about deleting empty files
		{
			// 14 chosen as an arbitrary display length to keep popup from getting too large
			if(empty_file_count == 14)
				estrConcat(&empty_files, "\n...", 4);
			else if(empty_file_count < 14)
				estrConcatf(&empty_files, "\n%s", path);
			++empty_file_count;
		}
		if(too_long && type == PCLDIFF_CREATED) // don't warn if somehow a file that is too long already exists
		{
			// 6 chosen as arbitrary display length, smaller than length above because we know these are really long names
			if(too_long_count == 6)
				estrConcat(&too_long_files, "\n...", 4);
			else if(too_long_count < 6)
				estrConcatf(&too_long_files, "\n%s", (*fnames)[i]);	//length for this checks against relative path, so show that here to give the correct size
			++too_long_count;
		}

		if(type != PCLDIFF_NOCHANGE && testing != PCLDIFF_DONTTEST && testing != PCLDIFF_PRESUMEDGOOD)
		{
			if(estrLength(&g_warning))
				estrConcatf(&g_warning, "\r\n");
			estrConcatf(&g_warning, "%s \t", path);
			if(testing == PCLDIFF_TESTEDBAD)
				estrConcatf(&g_warning, "BAD (client and/or server reported errors)");
			else if(testing == PCLDIFF_NOTTESTED)
				estrConcatf(&g_warning, "UNTESTED (you have not run the client/server since changing this file)");
			else if(testing == PCLDIFF_TESTEXCLUDED)
				estrConcatf(&g_warning, "UNTESTED (you may have run a non-COV client/server and this is a COV-only file)");
		}

		if(	(*diff_types)[i] & PCLDIFF_LINKWILLBREAK &&
			type != PCLDIFF_NOCHANGE)
		{
			if (!oktoalllinkbreaks) {
				if(	prevbranch >= 0 ||
					patchmeGetPreviousBranchInfo(client, &prevbranch, &prevbranchname))
				{
					estrPrintf(&warning, "%s\nChecking in this file will break it's link with branch %d (%s), any changes in the %s branch will no longer be inherited to this one.\nAre you sure you want to check in this file and break the link?",
											path, prevbranch, prevbranchname, prevbranchname);

					switch(okToAllCancelDialogEx(warning, "Confirm Link Break", okToAllCancelCallback))
					{
						xcase IDCANCEL:
							// Leave PCLDIFF_LINKWILLBREAK, it'll get unchecked
						xcase IDOKTOALL:
							oktoalllinkbreaks = true;
							// fall-through
						acase IDOK:
							(*diff_types)[i] &= ~PCLDIFF_LINKWILLBREAK;
					}
				}
			} else {
				(*diff_types)[i] &= ~PCLDIFF_LINKWILLBREAK;
			}
		}

		if(	!oktoallbrokenlinks &&
			(*diff_types)[i] & PCLDIFF_LINKISBROKEN &&
			type != PCLDIFF_NOCHANGE)
		{
			estrPrintf(&warning, "%s\r\nThis file is no longer linked to a later branch, if the change you are checking in is a bugfix, it will have to be duplicated in later branches as well!",
									path);
			if(okToAllDialog(warning, "WARNING:  File no longer linked"))
				oktoallbrokenlinks = true;
		}

		appendFilename(buf, (*diff_types)[i], empty_file, too_long, path);
	}
	if(estrLength(&g_warning) && !g_patchme_notestwarn)
	{
		ret = (int)DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_CHECKINVERIFY), NULL, (DLGPROC)DlgCheckinVerifyProc);
		if(!ret)
			goto returnret;
		else 
		{
			// Log their evil ways!
			bool bNeedComma=false;
			estrClear(&log_message);
			estrCreate(&log_message);
			estrPrintf(&log_message, "%s\t%s %s : ", timeGetLocalDateString(), GimmeQueryUserName(), spWarningAuthorizeName);
			for (i=0; i<eaSize(fnames); i++) {
				PCL_DiffType testing = (*diff_types)[i] & PCLDIFFMASK_TESTING;
				if (testing == PCLDIFF_TESTEDBAD) {
					if (bNeedComma)
						estrAppend2(&log_message, ", ");
					estrAppend2(&log_message, (*fnames)[i]);
					estrAppend2(&log_message, " (BAD)");
					bNeedComma = true;
				} else if (testing == PCLDIFF_NOTTESTED) {
					if (bNeedComma)
						estrAppend2(&log_message, ", ");
					estrAppend2(&log_message, (*fnames)[i]);
					estrAppend2(&log_message, " (Not tested)");
					bNeedComma = true;
				} else if (testing == PCLDIFF_TESTEXCLUDED) {
					if (bNeedComma)
						estrAppend2(&log_message, ", ");
					estrAppend2(&log_message, (*fnames)[i]);
					estrAppend2(&log_message, " (Excluded)");
					bNeedComma = true;
				}
			}
			estrAppend2(&log_message, "\r\n");
		}
	}
	if(empty_file_count)
	{
		estrPrintf(&warning, "You are attempting to check in the following empty files:%s\n\nThese files should not be checked in unless absolutely necessary.  The empty files will be unchecked by default in the checkin dialog.  If these files already exist in the gimme database but are currently not used, they should be removed with the Gimme Remove command.",
			empty_files);
		MessageBox_UTF8(NULL, warning, "Warning", MB_OK);
	}
	if(too_long_count)
	{
		estrPrintf(&warning, "The following files are over the path limit of %d characters:%s\n\nThese path names must be shortened before they can be checked in.",
			PATCH_MAX_PATH, too_long_files);
		MessageBox_UTF8(NULL, warning, "Warning", MB_OK);
	}
	printf("List of files to check in complete, please enter comments in dialog...\n");

	ret = (int)DialogBox (winGetHInstance(), MAKEINTRESOURCE(IDD_DLG_CHECKIN), NULL, (DLGPROC)DlgCheckinProc);
	clearFilenames();

	if (ret) {
		if (log_message)
			logToFileName("//SOMNUS/data/gimme/logs/theyClickedYes.log", log_message, 0);
		printf("Dialog completed, checking in files...\n");
	} else
		printf("Canceled.\n");

returnret:
	clearFilenames();
	estrDestroy(&g_warning);
	estrDestroy(&warning);
	estrDestroy(&empty_files);
	estrDestroy(&too_long_files);
	estrDestroy(&log_message);
	return ret;
}

int patchmeDialogDelete(const char *filepath, bool folder)
{
	char temp[512];
	if (folder)
		sprintf_s(SAFESTR(temp), "Are you sure you want to delete the folder\n\"%s\"\nand all its contents, from the local drive?\n\nThis file will be removed from the\nrevision control database the next time you do a\ncheckin on the folder.  You can undo this operation by\ndoing a gimme undo checkout.", filepath);
	else
		sprintf_s(SAFESTR(temp), "Are you sure you want to delete the file\n\"%s\"\nfrom the local drive?\n\nThis file will be removed from the\nrevision control database the next time you do a\ncheckin on the folder.  You can undo this operation by\ndoing a gimme undo checkout.", filepath);
	return IDYES == MessageBox_UTF8(NULL, temp, "Confirm File Delete", MB_YESNO | MB_SYSTEMMODAL |MB_ICONWARNING);
}

static WNDPROC orig_CheckinListViewProc = NULL;

static void clearUncheckedFiles(HWND hDlg)
{
	int i;

	for ( i = eaSize(&g_patchmefilenames)-1; i >= 0; --i )
	{
		if ( !ListView_GetCheckState(hDlg, i) )
		{
			eaRemove(g_patchmefnames, i);
			eaiRemove(g_diff_types, i);
		}
	}
}

static LRESULT CALLBACK DlgCheckinVerifyProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	static int count=10;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), g_warning);
		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_NOTES), 
			"The files above might have errors that will cause pop-ups on other people's computer.  \
Files show up on this list because either a) you have not run the client and server \
since editing the file, or b) the client or server reported there were errors in the \
file.\r\n\r\nIf this is a CORE asset, you must run the CORE Game or MaterialEditor.\r\n\r\n\
Do you still want to check in these files even though they may have errors?  \
You MUST enter the name of the producer authorizing you to check in these files in the box below. \
Clicking Yes is NOT RECOMMENDED and will be logged.");

		EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
		SetTimer(hDlg, 1, 1000, NULL);
		count = 10;
		// make the window flash if it is not the focus
		flashWindow(hDlg);
		return FALSE;

	case WM_TIMER:
		if (count == 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000000)) {
			if(GetWindowTextLength(GetDlgItem(hDlg, IDC_NAME)) > 2)
			{
				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
			}
			SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), "Yes");
			KillTimer(hDlg, 1);
		} else {
			char buf[32];
			sprintf(buf, "Yes (%d)", count);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), buf);
			count--;
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			GetWindowText_UTF8(GetDlgItem(hDlg, IDC_NAME), &spWarningAuthorizeName);
			EndDialog(hDlg, 1);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, 0);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDC_NAME && HIWORD(wParam) == EN_CHANGE)
		{
			if(GetWindowTextLength(GetDlgItem(hDlg, IDC_NAME)) > 2)
				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
			else
				EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static bool g_ConformMS = false;

// Select all items in the list.
static void DlgSelectAll(HWND hList)
{
	int i;
	bool ms = g_ConformMS;

	g_ConformMS = false;

	for ( i = 0; i < eaSize(&g_patchmefilenames); ++i )
		ListView_SetItemState(hList, i, LVIS_SELECTED, LVIS_SELECTED);

	g_ConformMS = ms;
}

// Copy the highlighted files to the clipboard.
static void DlgCopyHighlightedFilesToClipboard(HWND hList)
{
	int i;
	char *estrFileList = NULL;

	estrStackCreate(&estrFileList);
	for ( i = 0; i < eaSize(&g_patchmefilenames); ++i )
	{
		if ( ListView_GetItemState(hList, i, LVIS_SELECTED) )
		{
			estrConcat(&estrFileList, g_patchmefilenames[i]->fname, (int)strlen(g_patchmefilenames[i]->fname));
			estrConcat(&estrFileList, "\n", 1);
		}
	}
	if ( estrFileList )
		winCopyToClipboard(estrFileList);
	estrDestroy(&estrFileList);
}

static LRESULT CALLBACK CheckinListViewProc(HWND hList, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch( iMsg )
	{

	// Double-click triggers diff.
	case WM_LBUTTONDBLCLK:
		{
			int i;

			i = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
			if ( i != -1 )
			{
				char *fname = g_patchmefilenames[i]->fname;
				if (fname[0] == '(')
					fname = strchr(fname, ')')+2;
				assert(fname>(char*)2);
				printf("Performing Gimme Diff on %s\n", fname);
 				patchmeDiffFile(fname);
			}
		}
		break;

	// Ctrl-X key combinations
	case WM_KEYDOWN:
		if (GetAsyncKeyState(VK_CONTROL))
		{
			switch (wParam)
			{
				case 'A':
					DlgSelectAll(hList);
					break;
				case 'C':
					DlgCopyHighlightedFilesToClipboard(hList);
					break;
			}
		}

	// Passed to ListView.
	default:
		return CallWindowProc(orig_CheckinListViewProc, hList, iMsg, wParam, lParam);
	}

	// We handled it
	return true;
}

// see http://www.codeproject.com/listctrl/listview.asp for notes on how this works
static LRESULT CheckinListViewCustomDraw (LPARAM lParam)
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
			PCL_DiffType diff_type; // GimmeQueuedAction *action;

			i = (int)lplvcd->nmcd.dwItemSpec;
			diff_type = g_patchmefilenames[i]->diff_type; // action = g_patchmefilenames[i]->action;
			if ( ((diff_type & PCLDIFFMASK_ACTION) != PCLDIFF_DELETED) && ((diff_type & PCLDIFFMASK_ACTION) != PCLDIFF_NOCHANGE) && (diff_type & PCLDIFF_BLOCKED) )
			{
				lplvcd->clrText   = RGB(250,100,0);
			}
			else if ( g_patchmefilenames[i]->too_long && ((diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED) )
			{
				lplvcd->clrText   = RGB(250,100,0);
			}
			else if ( (diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_DELETED ) // stricmp(action->notes, checkin_notes[CHECKIN_NOTE_DELETED]) == 0 )
			{
				lplvcd->clrText   = RGB(150,0,0);
			}
			else if ( g_patchmefilenames[i]->empty_file ) // stricmp(action->notes, checkin_notes[CHECKIN_NOTE_EMPTYFILE]) == 0 )
			{
				lplvcd->clrText   = RGB(150,0,150);
			}
			else if ( (diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CHANGED ) // !action->notes )
			{
				lplvcd->clrText   = RGB(0,0,180);
			}
			else if ( (diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE ) // stricmp(action->notes, checkin_notes[CHECKIN_NOTE_NOCHANGE]) == 0 )
			{
				if (diff_type & PCLDIFF_NEWFILE)
					lplvcd->clrText   = RGB(150,50,50);
				else
					lplvcd->clrText   = RGB(100,100,100);
			}
			else if ( (diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED ) // stricmp(action->notes, checkin_notes[CHECKIN_NOTE_NEW]) == 0 )
			{
				lplvcd->clrText   = RGB(0,180,0);
			}
		}
		break;
	}
	return CDRF_DODEFAULT;
}

static WNDPROC orig_CheckinCommentsTextboxProc = NULL;
static HWND orig_CheckinCommentsTextboxHwnd = 0;
static WNDPROC orig_CheckinPatchNotesTextboxProc = NULL;
static HWND orig_CheckinPatchNotesTextboxHwnd = 0;

// Superclass the checkin window edit box
static LRESULT CALLBACK CheckinTextboxProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch( iMsg )
	{

	// Ctrl-X key combinations
	case WM_KEYDOWN:
		if (GetAsyncKeyState(VK_CONTROL))
		{
			switch (wParam)
			{
				case 'A':
					SendMessage(hWnd, EM_SETSEL, 0, -1);
					break;
			}
		}

	// Passed to ListView.
	default:
		if (hWnd == orig_CheckinCommentsTextboxHwnd)
			return CallWindowProc(orig_CheckinCommentsTextboxProc, hWnd, iMsg, wParam, lParam);
		else if (hWnd == orig_CheckinPatchNotesTextboxHwnd)
			return CallWindowProc(orig_CheckinPatchNotesTextboxProc, hWnd, iMsg, wParam, lParam);
		devassert(0);
		return false;
	}

	// We handled it
	return true;
}

static int DlgCheckinGetFocusedItem(HWND hList)
{
	int i;
	for ( i = 0; i < eaSize(&g_patchmefilenames); ++i ) {
		if(ListView_GetItemState(hList, i, LVIS_FOCUSED)) {
			return i;
		}
	}
	return -1;
}

static const char* DlgCheckinGetItemTypeName(PatchMeCheckinFile *pItem)
{
	PCL_DiffType eDiffType; // GimmeQueuedAction *action;

	eDiffType = pItem->diff_type; // action = g_patchmefilenames[i]->action;
	if ( (eDiffType & PCLDIFFMASK_ACTION) == PCLDIFF_DELETED ) {
		return "Deleted";
	} else if (pItem->empty_file) {
		return "Empty";
	} else if ( (eDiffType & PCLDIFFMASK_ACTION) == PCLDIFF_CHANGED ) { 
		return "Changed";
	} else if ( (eDiffType & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE ) { 
		return "Unchanged";
	} else if ( (eDiffType & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED ) { 
		return "New";
	} 
	return "ERROR";
}

static void DlgCheckinSwapComments(HWND hDlg)
{
	char *temp_comments = NULL, *temp_patchnotes = NULL;

	getLastCommentsFromRegistry(&temp_comments, &temp_patchnotes);
	if ((temp_comments && temp_comments[0]) || (temp_patchnotes && temp_patchnotes[0]))
	{
		getCommentsFromDialog(hDlg);
		if (estrLength(&spComments) || estrLength(&spPatchNotes))
		{
			storeLastComments(spComments, spPatchNotes);
		}

		estrCopy2(&spComments, temp_comments);
		estrCopy2(&spPatchNotes, temp_patchnotes);

		SAFE_FREE(temp_comments);
		SAFE_FREE(temp_patchnotes);

		SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT), NULL_TO_EMPTY(spComments));
		if (estrLength(&spPatchNotes))
		{
			CheckDlgButton(hDlg, IDC_PATCHNOTES_CHECKBOX, BST_UNCHECKED);
			EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), TRUE);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_PATCHNOTES), spPatchNotes);
		}
		else
		{
			CheckDlgButton(hDlg, IDC_PATCHNOTES_CHECKBOX, BST_CHECKED);
			EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), FALSE);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_PATCHNOTES), "");
		}
	}
	else
	{
		SAFE_FREE(temp_comments);
		SAFE_FREE(temp_patchnotes);
	}
}

static void DlgCheckinCheckSelected(HWND hDlg, bool check)
{
	int i;
	HWND hList = GetDlgItem(hDlg, IDC_LIST);
	for ( i = 0; i < eaSize(&g_patchmefilenames); ++i ) {
		if(ListView_GetItemState(hList, i, LVIS_SELECTED)) {
			ListView_SetCheckState(hList, i, check);
		}
	}
}

static void DlgCheckinCheckFileType(HWND hDlg, bool bCheck)
{
	int i;
	HWND hList = GetDlgItem(hDlg, IDC_LIST);
	int iFocused = DlgCheckinGetFocusedItem(hList);
	const char *pcFileExt;
	if(iFocused < 0)
		return;
	pcFileExt = FindExtensionFromFilename(g_patchmefilenames[iFocused]->fname);
	for ( i = 0; i < eaSize(&g_patchmefilenames); ++i ) {
		if(strEndsWith(g_patchmefilenames[i]->fname, pcFileExt)) {
			ListView_SetCheckState(hList, i, bCheck);
		}
	}
}

static void DlgCheckinCheckDiffType(HWND hDlg, bool bCheck)
{
	int i;
	HWND hList = GetDlgItem(hDlg, IDC_LIST);
	int iFocused = DlgCheckinGetFocusedItem(hList);
	const char *pcFocusedTypeStr;
	if(iFocused < 0)
		 return;
	pcFocusedTypeStr = DlgCheckinGetItemTypeName(g_patchmefilenames[iFocused]);
	for ( i = 0; i < eaSize(&g_patchmefilenames); ++i ) {
		const char *pcTypeStr = DlgCheckinGetItemTypeName(g_patchmefilenames[i]);;
		if(stricmp(pcFocusedTypeStr, pcTypeStr)==0) {
			ListView_SetCheckState(hList, i, bCheck);
		}
	}
}


static void removeSingleFile(HWND hList, const char* file, int sz)
{
	int i;
	for (i = 0; i < eaSize(&g_patchmefilenames); ++i)
	{
		if (!strncmp(g_patchmefilenames[i]->real_fname, file, sz))
		{
			eaRemove(&g_patchmefilenames, i);
			eaRemove(g_patchmefnames, i);
			eaiRemove(g_diff_types, i);
			ListView_DeleteItem(hList, i);
			return;
		}
	}
}

// Removes a list of files and all corresponding .ms files from the UI.
// Expects a semi-colon separated string.
static void removeFileList(HWND hDlg, const char* fileList)
{
	HWND hList = GetDlgItem(hDlg, IDC_LIST);
	char curFile[MAX_PATH];
	const char *fileStart = fileList;
	const char *fileEnd;
	const char *fileMS;

	do 
	{
		fileEnd = strchr(fileStart, ';');
		if (!fileEnd)
			fileEnd = fileStart + strlen(fileStart);

		strncpy_s(SAFESTR(curFile), fileStart, fileEnd-fileStart);
		removeSingleFile(hList, SAFESTR(curFile));

		fileMS = strstr(fileStart, ".ms");
		if (fileMS && fileMS < fileEnd)
		{
			curFile[fileMS-fileStart] = '\0';
			removeSingleFile(hList, SAFESTR(curFile));
		}
		else if (fileEnd-fileStart+3 <= MAX_PATH)
		{
			strcpy_s(curFile+(fileEnd-fileStart), sizeof(curFile)-(fileEnd-fileStart), ".ms");
			removeSingleFile(hList, SAFESTR(curFile));
		}

		if (*fileEnd == ';')
			fileStart = fileEnd + 1;
		else
			fileStart = NULL;
	} while(fileStart);
}


static void DlgCheckinUndoCheckout(HWND hDlg)
{
	HWND hList = GetDlgItem(hDlg, IDC_LIST);
	int i;
	GimmeErrorValue err;
	char* files_selected = NULL;
	char* files_to_delete = NULL;
	char* files_to_undo = NULL;
	int total_files = 0;
	bool delete_ellipsis = false;
	bool undo_ellipsis = false;

	estrStackCreate(&files_selected);
	estrStackCreate(&files_to_delete);
	estrStackCreate(&files_to_undo);

	for (i = 0; i < eaSize(&g_patchmefilenames); ++i)
	{
		if (ListView_GetItemState(hList, i, LVIS_SELECTED))
		{
			if(estrLength(&files_selected))
				estrConcatf(&files_selected, ";%s", g_patchmefilenames[i]->real_fname);
			else
				estrConcatf(&files_selected, "%s", g_patchmefilenames[i]->real_fname);

			// 14 chosen as an arbitrary display length to keep popup from getting too large
			if(total_files >= 14)
			{
				if((g_patchmefilenames[i]->diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED)
				{
					if(!delete_ellipsis)
					{
						delete_ellipsis = true;
						estrConcat(&files_to_delete, "\n...", 4);
					}
				}
				else
				{
					if(!undo_ellipsis)
					{
						undo_ellipsis = true;
						estrConcat(&files_to_undo, "\n...", 4);
					}
				}
			}
			else
			{
				if((g_patchmefilenames[i]->diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED)
					estrConcatf(&files_to_delete, "\n%s", g_patchmefilenames[i]->real_fname);
				else
					estrConcatf(&files_to_undo, "\n%s", g_patchmefilenames[i]->real_fname);
			}
			++total_files;
		}
	}

	if(total_files)
	{
		char* confirmation_message = NULL;
		estrConcatf(&confirmation_message, "Are you sure you want to ");
		if(estrLength(&files_to_delete))
		{
			estrConcatf(&confirmation_message, "delete the files:%s", files_to_delete);
			if(estrLength(&files_to_undo))
				estrConcatf(&confirmation_message, "\n\n\nand undo checkout on the files:%s", files_to_undo);
		}
		else
			estrConcatf(&confirmation_message, "undo checkout on the files:%s", files_to_undo);

		if(IDYES == MessageBox_UTF8(NULL, confirmation_message, "Confirm Undo / Delete", MB_YESNO|MB_ICONWARNING))
		{
			patchmeDoUnqueuedOperation(files_selected, GIMME_UNDO_CHECKOUT, GIMME_QUIET, &err, false);
			if (err != GIMME_NO_ERROR)
			{
				MessageBox_UTF8(NULL, "Undo checkout encountered an error.  See console for more details.", "Undo Checkout Error", MB_ICONWARNING | MB_OK);
				EndDialog(hDlg, 0);
			}
			else
			{
				removeFileList(hDlg, files_selected);
	
			}
		}
		estrDestroy(&confirmation_message);
	}

	estrDestroy(&files_selected);
	estrDestroy(&files_to_delete);
	estrDestroy(&files_to_undo);
}

//Resize all the items in the checkin window
static void DlgCheckinResize(HWND hDlg, int width, int height, LPRECT origRect)
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
	DlgItemResize(IDC_LIST,					RESZ_WIDTH|RESZ_HEIGHT);
	DlgItemResize(IDC_EDIT_ITEMSCHECKEDIN,	RESZ_BOTTOM|RESZ_RIGHT);
	DlgItemResize(IDC_STATIC_TOBECHECKEDIN, RESZ_BOTTOM|RESZ_RIGHT);
	DlgItemResize(IDC_BTN_CLIPBOARD,		RESZ_BOTTOM);
	DlgItemResize(IDC_BTN_UNCHECKALL,		RESZ_BOTTOM);
	DlgItemResize(IDC_BTN_LASTCOMMENT,		RESZ_BOTTOM);
	DlgItemResize(IDC_STATIC_COMMENTS,		RESZ_BOTTOM);
	DlgItemResize(IDC_EDIT,					RESZ_BOTTOM|RESZ_WIDTH);
	DlgItemResize(IDC_PATCHNOTES_CHECKBOX,	RESZ_BOTTOM);
	DlgItemResize(IDC_PATCHNOTESSTATIC,		RESZ_BOTTOM);
	DlgItemResize(IDC_PATCHNOTES,			RESZ_BOTTOM|RESZ_WIDTH);
	DlgItemResize(IDC_BRANCH_NOTIFY,		RESZ_BOTTOM);
	DlgItemResize(IDOK,						RESZ_BOTTOM|RESZ_RIGHT);
	DlgItemResize(IDCANCEL,					RESZ_BOTTOM|RESZ_RIGHT);

	InvalidateRect(hDlg, NULL, true);
	GetClientRect(hDlg, &prevRect);
}

#define     ID_CHECK_HIGHLIGHT		2001
#define     ID_UNCHECK_HIGHLIGHT	2002
#define     ID_CHECK_EXT			2003
#define     ID_UNCHECK_EXT			2004
#define     ID_CHECK_DIFF_TYPE		2005
#define     ID_UNCHECK_DIFF_TYPE	2006
#define     ID_UNDO_CHECKOUT		2007

static LRESULT CALLBACK DlgCheckinProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	static RECT origRect;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i;
			LVCOLUMN lvc={0};
			char numItemsBuff[256];

			if (g_patchme_simulate) {
				SetWindowText_UTF8(hDlg, "Gimme Diff");
				SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), "");
			} else if (g_patchme_checkin_op == GIMME_UNDO_CHECKOUT) {
				SetWindowText_UTF8(hDlg, "Gimme Undo Checkout");
				SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), "Undo Checkout");
			} else {
				SetWindowText_UTF8(hDlg, "Gimme Check In");
				SetWindowText_UTF8(GetDlgItem(hDlg, IDOK), "Check In");
			}

			ListView_SetExtendedListViewStyle(GetDlgItem(hDlg, IDC_LIST), LVS_EX_CHECKBOXES 
				| LVS_EX_FULLROWSELECT
				| LVS_EX_ONECLICKACTIVATE 
				);

			lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvc.fmt = LVCFMT_LEFT;
			lvc.cx = 2000;
			ListView_InsertColumn(GetDlgItem(hDlg, IDC_LIST), 0, &lvc);
			lvc.iSubItem = 0;
			lvc.cx = 1000;
			lvc.pszText = L"File";
			ListView_InsertColumn(GetDlgItem(hDlg, IDC_LIST), 1, &lvc);

			orig_CheckinListViewProc = (WNDPROC)(LONG_PTR)SetWindowLongPtr(GetDlgItem(hDlg, IDC_LIST),
				GWLP_WNDPROC, (LONG_PTR)CheckinListViewProc);

			// Superclass the text boxes.
			orig_CheckinCommentsTextboxHwnd = GetDlgItem(hDlg, IDC_EDIT);
			orig_CheckinCommentsTextboxProc = (WNDPROC)(LONG_PTR)SetWindowLongPtr(orig_CheckinCommentsTextboxHwnd,
				GWLP_WNDPROC, (LONG_PTR)CheckinTextboxProc);
			orig_CheckinPatchNotesTextboxHwnd = GetDlgItem(hDlg, IDC_PATCHNOTES);
			orig_CheckinPatchNotesTextboxProc = (WNDPROC)(LONG_PTR)SetWindowLongPtr(orig_CheckinPatchNotesTextboxHwnd,
				GWLP_WNDPROC, (LONG_PTR)CheckinTextboxProc);

			//SetWindowLong(GetDlgItem(hDlg, IDC_LIST), DWL_MSGRESULT, (LONG)CheckinListViewCustomDraw(lParam));

			//SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), g_patchmefilenames);
			for ( i = 0; i < eaSize(&g_patchmefilenames); ++i )
			{
				LVITEM lvi;
				lvi.mask = LVIF_COLUMNS | LVIF_TEXT | LVIF_PARAM;
				lvi.iItem = i;
				lvi.iSubItem = 0;
				lvi.pszText = UTF8_To_UTF16_malloc(g_patchmefilenames[i]->fname);
				lvi.lParam = (LPARAM)UTF8_To_UTF16_malloc(g_patchmefilenames[i]->real_fname);
				ListView_InsertItem(GetDlgItem(hDlg, IDC_LIST), &lvi);
				ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i,
					(((g_patchmefilenames[i]->diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_DELETED) ||
					((g_patchmefilenames[i]->diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE) ||
					!((g_patchmefilenames[i]->diff_type & PCLDIFF_BLOCKED) || g_patchmefilenames[i]->empty_file)) && // Not a blocked or empty file, and...
					!(g_patchmefilenames[i]->diff_type & PCLDIFF_LINKWILLBREAK) && // not a link breaking file, which they said they didn't want to check in, and ...
					!(((g_patchmefilenames[i]->diff_type & PCLDIFFMASK_ACTION) == PCLDIFF_CREATED) && g_patchmefilenames[i]->too_long) // not a created file with too long a path name
					);
// 					g_patchmefilenames[i]->action->notes ? (stricmp(g_patchmefilenames[i]->action->notes, checkin_notes[CHECKIN_NOTE_EMPTYFILE]) == 0 ? 0 : 1) : 1);

				SAFE_FREE(lvi.pszText);
				SAFE_FREE((void*)lvi.lParam);
			}
			sprintf(numItemsBuff, "%d", eaSize(&g_patchmefilenames));
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_ITEMSCHECKEDIN), numItemsBuff);

			CheckDlgButton(hDlg, IDC_PATCHNOTES_CHECKBOX, BST_CHECKED);
			EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), FALSE);

			if (g_patchme_simulate) {
				EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_EDIT), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_LASTCOMMENT), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES_CHECKBOX), FALSE);
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_STATIC_TOBECHECKEDIN), "Items in list:");
			} else if (g_patchme_checkin_op == GIMME_UNDO_CHECKOUT) {
				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
				EnableWindow(GetDlgItem(hDlg, IDC_EDIT), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_BTN_LASTCOMMENT), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES_CHECKBOX), FALSE);
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_STATIC_TOBECHECKEDIN), "Items to undo checkout:");
			} else {
				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
				EnableWindow(GetDlgItem(hDlg, IDC_EDIT), TRUE);
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_STATIC_TOBECHECKEDIN), "Items to be checked in:");
			}

			SetTimer(hDlg, 0, 120, NULL);

			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH_NOTIFY), g_branch_note);
			g_ConformMS = true;

			// flash the window if it is not the focus
			flashWindow(hDlg);
			GetClientRect(hDlg, &origRect);
			return FALSE;
		}
	case WM_DESTROY:
		{
			getCommentsFromDialog(hDlg);
			if (estrLength(&spComments) || estrLength(&spPatchNotes))
				storeLastComments(spComments, spPatchNotes);
			createFinalComment(spComments, spPatchNotes);
		}
		break;
	case WM_TIMER:
		{
			int i, cnt = eaSize(&g_patchmefilenames);
			char numItemsBuff[256];
			for ( i = 0; i < eaSize(&g_patchmefilenames); ++i )
			{
				if ( !ListView_GetCheckState(GetDlgItem(hDlg, IDC_LIST), i) )
					--cnt;
			}
			// if there are 0 items selected, change the unselect all button to select all
			if ( cnt == 0 )
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Check All");
			else if ( cnt == eaSize(&g_patchmefilenames) )
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Uncheck All");
			sprintf(numItemsBuff, "%d", cnt);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_EDIT_ITEMSCHECKEDIN), numItemsBuff);

// 			// determine whether to enable the ok button based on if they have properly
// 			// filled out the patch notes box
// 			if ( GetWindowTextLength(GetDlgItem(hDlg,IDC_PATCHNOTES)) > 0 )
// 				EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
// 			else
// 			{

			if ( IsDlgButtonChecked(hDlg, IDC_PATCHNOTES_CHECKBOX) )
			{
				//EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
				EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), FALSE);
			}
			else
			{
				//EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_PATCHNOTES), TRUE);
			}

// 			}
		}
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			{
				getCommentsFromDialog(hDlg);
				if(!estrLength(&spComments) && g_patchme_checkin_op != GIMME_UNDO_CHECKOUT)
				{
					MessageBox_UTF8(NULL, "Checkin comments are required!", "Missing checkin comments", MB_ICONWARNING | MB_OK);
					break;
				}
				clearUncheckedFiles(GetDlgItem(hDlg, IDC_LIST));
				KillTimer(hDlg, 0);
				EndDialog(hDlg, 1);
				return TRUE;
			}
			break;
		case IDCANCEL:
			{
				KillTimer(hDlg, 0);
				EndDialog(hDlg, 0);
				return TRUE;
			}
			break;
		case IDC_BTN_UNCHECKALL:
			{
				int i;
				char *pBuff = NULL;
				bool ms = g_ConformMS;
				GetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL), &pBuff);
				g_ConformMS = false;
				if ( strcmp(pBuff, "Uncheck All") == 0 )
				{
					for ( i = 0; i < eaSize(&g_patchmefilenames); ++i )
					{
						ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i, 0);
					}
					SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Check All");
				}
				else
				{
					for ( i = 0; i < eaSize(&g_patchmefilenames); ++i )
					{
						ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i, 1);
					}
					SetWindowText_UTF8(GetDlgItem(hDlg, IDC_BTN_UNCHECKALL),"Uncheck All");
				}
				g_ConformMS = ms;
				estrDestroy(&pBuff);
			}
			break;
		case IDC_BTN_CLIPBOARD:
			DlgCopyHighlightedFilesToClipboard(GetDlgItem(hDlg, IDC_LIST));
			break;
		case IDC_BTN_LASTCOMMENT:
			DlgCheckinSwapComments(hDlg);
			break;
		case ID_CHECK_HIGHLIGHT:
			DlgCheckinCheckSelected(hDlg, true);
			break;
		case ID_UNCHECK_HIGHLIGHT:
			DlgCheckinCheckSelected(hDlg, false);
			break;
		case ID_CHECK_EXT:
			DlgCheckinCheckFileType(hDlg, true);
			break;
		case ID_UNCHECK_EXT:
			DlgCheckinCheckFileType(hDlg, false);
			break;
		case ID_CHECK_DIFF_TYPE:
			DlgCheckinCheckDiffType(hDlg, true);
			break;
		case ID_UNCHECK_DIFF_TYPE:
			DlgCheckinCheckDiffType(hDlg, false);
			break;
		case ID_UNDO_CHECKOUT:
			DlgCheckinUndoCheckout(hDlg);
			break;
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
			else if(((LPNMHDR)lParam)->code == LVN_ITEMCHANGED)
			{
				NMLISTVIEW *pnmv = (LPNMLISTVIEW) lParam;
				WCHAR buf[1024];
				char mspath[MAX_PATH];
				int i;
				LVITEM item = {0};
				LVFINDINFO findinfo = {0};
				if(!g_ConformMS)
					break;
				item.mask = LVIF_TEXT|LVIF_PARAM;
				item.iItem = pnmv->iItem;
				item.iSubItem = pnmv->iSubItem;
				item.pszText = buf;
				item.cchTextMax = 1024;
				ListView_GetItem(pnmv->hdr.hwndFrom, &item);
				//printf("Item changed %s %u\n", item.lParam, pnmv->uNewState==0x2000);
				

				WideToUTF8StrConvert((WCHAR*)item.lParam, SAFESTR(mspath));
				
				if(strEndsWith(mspath, ".ms") && strlen(mspath) > 3)
					mspath[strlen(mspath)-3] = '\0';
				else if(strlen(mspath) + 3 <= MAX_PATH) // don't assert from checking for a .ms
					strcat(mspath, ".ms");
				findinfo.flags = LVFI_PARAM;
				findinfo.lParam = (LPARAM)UTF8_To_UTF16_malloc(mspath);
				i = ListView_FindItem(GetDlgItem(hDlg, IDC_LIST), -1, &findinfo);
				if(i != -1)
				{
					g_ConformMS = false;
					ListView_SetCheckState(GetDlgItem(hDlg, IDC_LIST), i, ListView_GetCheckState(GetDlgItem(hDlg, IDC_LIST), pnmv->iItem));
					g_ConformMS = true;
				}

				SAFE_FREE((void*)findinfo.lParam);
			}
		}
		break;
	case WM_CONTEXTMENU:
		if((HWND)(wParam) == GetDlgItem(hDlg, IDC_LIST))
		{
			S16 *pTemp = NULL;
			char buf[256];
			HMENU hMenu;
			POINT point;
			int iFocused = DlgCheckinGetFocusedItem(GetDlgItem(hDlg, IDC_LIST));
			const char *pcFocusedTypeStr;
			if(iFocused < 0)
				break;
			pcFocusedTypeStr = DlgCheckinGetItemTypeName(g_patchmefilenames[iFocused]);
			point.x = GET_X_LPARAM(lParam);
			point.y = GET_Y_LPARAM(lParam);
			hMenu = CreatePopupMenu();
			AppendMenu (hMenu, MF_STRING,		ID_CHECK_HIGHLIGHT,		L"Check");
			AppendMenu (hMenu, MF_STRING,		ID_UNCHECK_HIGHLIGHT,	L"Uncheck");
			AppendMenu (hMenu, MF_SEPARATOR,	0,						L"");
			AppendMenu (hMenu, MF_STRING,		ID_CHECK_EXT,			L"Check Same File Type");
			AppendMenu (hMenu, MF_STRING,		ID_UNCHECK_EXT,			L"Uncheck Same File Type");
			AppendMenu (hMenu, MF_SEPARATOR,	0,						L"");
			sprintf(buf, "Check All %s Files", pcFocusedTypeStr);
			pTemp = UTF8_To_UTF16_malloc(buf);
			AppendMenu (hMenu, MF_STRING,		ID_CHECK_DIFF_TYPE,		pTemp);
			SAFE_FREE(pTemp);
			sprintf(buf, "Uncheck All %s Files", pcFocusedTypeStr);
			pTemp = UTF8_To_UTF16_malloc(buf);
			AppendMenu (hMenu, MF_STRING,		ID_UNCHECK_DIFF_TYPE,	pTemp);
			SAFE_FREE(pTemp);
	
			AppendMenu (hMenu, MF_SEPARATOR,	0,						L"");
			AppendMenu (hMenu, MF_STRING,		ID_UNDO_CHECKOUT,		L"Undo Checkout / Delete New");
			
			TrackPopupMenu (hMenu, 0, point.x, point.y, 0, hDlg, NULL);
			DestroyMenu (hMenu);
		}
		break;
	case WM_SIZING:
		DlgWindowCheckResize(hDlg, (U32)wParam, (LPRECT)lParam, &origRect);
		return TRUE;
	case WM_SIZE:
		DlgCheckinResize(hDlg, LOWORD(lParam), HIWORD(lParam), &origRect);
		break;
	}
	return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

typedef struct getDateCtx_t
{
	char **times;
	char **timestrs;
	bool sync_core;
} getDateCtx_t;
static getDateCtx_t g_getDateCtx = {0};

static LRESULT CALLBACK DlgGetDateProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
	{
		HWND hListbox = GetDlgItem(hDlg, IDC_LIST);
		FOR_EACH_IN_EARRAY(g_getDateCtx.timestrs, char, timestr)
			ListBox_AddString(hListbox, timestr);
		FOR_EACH_END
		ListBox_AddString(hListbox, "Other...");
		ListBox_SetCurSel(hListbox, 0);
		if(!eaSize(&g_getDateCtx.timestrs))
		{
			EnableWindow(GetDlgItem(hDlg, IDC_DATE), TRUE);
			EnableWindow(GetDlgItem(hDlg, IDC_TIME), TRUE);
		}
		else
		{
			U32 time = strtol(g_getDateCtx.times[0], NULL, 10);
			SYSTEMTIME systime = {0};
			timerLocalSystemTimeFromSecondsSince2000(&systime, patchFileTimeToSS2000(time));
			DateTime_SetSystemtime(GetDlgItem(hDlg, IDC_DATE), GDT_VALID, &systime);
			DateTime_SetSystemtime(GetDlgItem(hDlg, IDC_TIME), GDT_VALID, &systime);
		}

		// Check the "sync core" box by default, except for C:\Core itself
		if(g_getDateCtx.sync_core)
			Button_SetCheck(GetDlgItem(hDlg, IDC_CORE), BST_CHECKED);
		else
		{
			Button_SetCheck(GetDlgItem(hDlg, IDC_CORE), BST_UNCHECKED);
			EnableWindow(GetDlgItem(hDlg, IDC_CORE), FALSE);
		}
	}
	break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
		{
			U32 time;
			SYSTEMTIME systime = {0}, systime2 = {0};
			DateTime_GetSystemtime(GetDlgItem(hDlg, IDC_DATE), &systime);
			DateTime_GetSystemtime(GetDlgItem(hDlg, IDC_TIME), &systime2);
			systime.wHour = systime2.wHour;
			systime.wMinute = systime2.wMinute;
			systime.wSecond = systime2.wSecond;
			systime.wMilliseconds = systime2.wMilliseconds;
			time = patchSS2000ToFileTime(timerSecondsSince2000FromLocalSystemTime(&systime));
			g_getDateCtx.sync_core = Button_GetCheck(GetDlgItem(hDlg, IDC_CORE)) == BST_CHECKED;
			EndDialog(hDlg, time);
		}
		break;
		case IDCANCEL:
			EndDialog(hDlg, 0);
		break;
		case IDC_LIST:
			switch(HIWORD(wParam))
			{
			case LBN_SELCHANGE:
			{
				int sel = ListBox_GetCurSel((HWND)lParam);
				bool enable = sel >= eaSize(&g_getDateCtx.timestrs);
				EnableWindow(GetDlgItem(hDlg, IDC_DATE), enable);
				EnableWindow(GetDlgItem(hDlg, IDC_TIME), enable);
				if(!enable)
				{
					U32 time = strtol(g_getDateCtx.times[sel], NULL, 10);
					SYSTEMTIME systime = {0};
					timerLocalSystemTimeFromSecondsSince2000(&systime, patchFileTimeToSS2000(time));
					DateTime_SetSystemtime(GetDlgItem(hDlg, IDC_DATE), GDT_VALID, &systime);
					DateTime_SetSystemtime(GetDlgItem(hDlg, IDC_TIME), GDT_VALID, &systime);
				}
			}
			break;
			}
		break;
		}
	break;
	}

	return FALSE;
}

U32 patchmeDialogGetDate(const char *root, bool *sync_core)
{
	char glvlog_path[MAX_PATH], *glvlog=NULL, buf[MAX_PATH], buf2[MAX_PATH];
	char *c;
	U32 ret;
	TriviaList *trivia;
	g_getDateCtx.sync_core = true;
	
	// Trim after the first ; in case of multiple paths
	strcpy(glvlog_path, root);
	c = strchr(glvlog_path, ';');
	if(c) *c = '\0';
	
	// Find the glv log and read it in
	if(trivia = triviaListGetPatchTriviaForFile(glvlog_path, SAFESTR(buf2)))
	{
		sprintf(glvlog_path, "%s/" PATCH_DIR "/glv.log", buf2);
		glvlog = fileAlloc(glvlog_path, NULL);
		if(strstri(triviaListGetValue(trivia, "PatchProject"), "Core") != NULL)
			g_getDateCtx.sync_core = false;
		triviaListDestroy(&trivia);
	}
	if(glvlog)
	{
		DivideString(glvlog, "\n", &g_getDateCtx.times, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	}
	FOR_EACH_IN_EARRAY(g_getDateCtx.times, char, t)
		char *end;
		U32 time = strtol(t, &end, 10);
		assertmsgf(t!=end, "Invalid glv time %s", t);
		timeMakeLocalDateStringFromSecondsSince2000(buf, patchFileTimeToSS2000(time));
		eaPush(&g_getDateCtx.timestrs, strdup(buf));
	FOR_EACH_END
	
	// Show the chooser dialog
	ret = (U32)DialogBox (winGetHInstance(), MAKEINTRESOURCE(IDD_DLG_GETBYDATE), NULL, (DLGPROC)DlgGetDateProc);

	if(sync_core)
		*sync_core = g_getDateCtx.sync_core;

	eaDestroyEx(&g_getDateCtx.times, NULL);
	eaDestroyEx(&g_getDateCtx.timestrs, NULL);


	return ret;
}