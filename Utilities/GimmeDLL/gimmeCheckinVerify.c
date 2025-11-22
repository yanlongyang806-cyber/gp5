#include "gimmeCheckinVerify.h"
#include "gimme.h"
#include "textparser.h"
#include "file.h"
#include "earray.h"
#include "utils.h"
#include "resource.h"
#include "EString.h"
#include "error.h"
#include "logging.h"
#include "timing.h"
#include "gimmeUtil.h"
#include "genericDialog.h"
#include "winutil.h"
#include "UTF8.h"

typedef struct GimmeRulesWarn {
	char **exclusions;
	char **inclusions; // Not used, put in for future compatibility if needed
} GimmeRulesWarn;

ParseTable parse_rules_warn[] = {
	{ "Excl",		TOK_STRINGARRAY(GimmeRulesWarn,exclusions)	},
	{ "Incl",		TOK_STRINGARRAY(GimmeRulesWarn,inclusions)	},
	{ NULL, 0, 0 }
};

AUTO_RUN;
void gimmeRegisterParseTables2(void)
{
	ParserSetTableInfo(parse_rules_warn, sizeof(GimmeRulesWarn), "parse_rules_warn", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

static GimmeRulesWarn *loadRulesWarn(GimmeDir *gimme_dir)
{
	static GimmeDir *last_gimme_dir = 0;
	static GimmeRulesWarn rules_warn={0};
	char filename[CRYPTIC_MAX_PATH];
	if (gimme_dir == last_gimme_dir)
		return &rules_warn;
	StructDeInitVoid(parse_rules_warn, &rules_warn);
	sprintf(filename, "%s/rulesWarn.txt", gimme_dir->lock_dir);
	gimmeCacheLock();
	gimmeGetCachedFilename(filename, SAFESTR(filename));
	if (fileExists(filename)) {
		ParserLoadFiles(NULL, filename, NULL, 0, parse_rules_warn, &rules_warn);
	}
	gimmeCacheUnlock();
	last_gimme_dir = gimme_dir;
	return &rules_warn;
}

static bool ruleExcludes(GimmeRulesWarn *rules_warn, char *relpath)
{
	int i;
	for (i=0; i<eaSize(&rules_warn->inclusions); i++) {
		if (simpleMatch(rules_warn->inclusions[i], relpath)) {
			return false;
		}
	}
	for (i=0; i<eaSize(&rules_warn->exclusions); i++) {
		if (simpleMatch(rules_warn->exclusions[i], relpath)) {
			return true;
		}
	}
	return false;
}

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

GimmeVerifyEvent **eventList = {0};

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
				free(event);
				event = NULL;
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

static void loadEventList(GimmeDir *gimme_dir)
{
	char *s;
	char loggamepath[CRYPTIC_MAX_PATH], logmapserverpath[CRYPTIC_MAX_PATH];
	strcpy(loggamepath, gimme_dir->local_dir);
	forwardSlashes(loggamepath);
	if (strEndsWith(loggamepath, "/"))
		loggamepath[strlen(loggamepath)-1]=0;
	s = strrchr(loggamepath, '/');
	if (s)
		*s = 0;
	strcat(loggamepath, "/logs");
	strcpy(logmapserverpath, loggamepath);
	// Check for CrypticEngine paths instead of CoH
	strcat(loggamepath, "/GameClient/errorLogLastRun.log");
	if (fileExists(loggamepath)) {
		strcat(logmapserverpath, "/GameServer/errorLogLastRun.log");
	} else {
		strcpy(loggamepath, logmapserverpath);
		strcat(loggamepath, "/game/errorLogLastRun.log");
		strcat(logmapserverpath, "/mapserver/errorLogLastRun.log");
	}
	eaDestroy(&eventList);
	parseErrorLog(loggamepath);
	parseErrorLog(logmapserverpath);
	// sort
	eaQSort(eventList, evCmp);
}

typedef enum GCFStatus {
	GCFS_NOTTESTED,
	GCFS_PRESUMEDGOOD,
	GCFS_BAD,
	GCFS_EXCLUDED,
} GCFStatus;

typedef struct GimmeCheckinFile {
	GimmeQueuedAction *action;
	GCFStatus status;
} GimmeCheckinFile;

static char *g_filenames=NULL;
static unsigned int g_filenames_len=4096;
static void appendFilename(char *filename, char *message)
{
	char str[CRYPTIC_MAX_PATH];
	sprintf(str, "%s \t%s", filename, message);
	if (!g_filenames) {
		g_filenames = malloc(g_filenames_len);
		g_filenames[0]=0;
	}
	if (strlen(g_filenames) + strlen(str)+4 > g_filenames_len) {
		g_filenames_len *= 4;
		g_filenames = realloc(g_filenames, g_filenames_len);
	}
	if (g_filenames[0]) {
		strcat_s(g_filenames, g_filenames_len, "\r\n");
	}
	strcat_s(g_filenames, g_filenames_len, str);
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

static void checkFile(GimmeCheckinFile *file)
{
	int i;
	time_t modificationTime;
	char fullpath[CRYPTIC_MAX_PATH];
	char *relpath = file->action->relpath;
	bool excluded = false;
	file->status = GCFS_NOTTESTED;
	sprintf(fullpath, "%s%s", file->action->gimme_dir->local_dir, file->action->relpath);
	modificationTime = fileLastChanged(fullpath);
	if (relpath[0] == '/')
		relpath++;
	if (eaSize(&eventList)==0) {
		// No log file, could be an old branch
		file->status = GCFS_PRESUMEDGOOD;
		return;
	}
	// Go through events list and find mo
	for (i=0; i<eaSize(&eventList); i++) {
		GimmeVerifyEvent *event = eventList[i];
		switch (event->type) {
			case GVE_RELOAD:
				if (!simpleMatch(event->file, relpath))
					break;
				// fall-through
			case GVE_START:
				if (matchesIgnore(file->action->relpath, event->ignoredFiles)) {
					excluded = true;
				} else {
					excluded = false;
				}
				if (modificationTime <= event->time) {
					if (excluded) {
						if (file->status == GCFS_NOTTESTED)
							file->status = GCFS_EXCLUDED;
						// If already flagged as good or bad, assume it hasn't changed
					} else {
						file->status = GCFS_PRESUMEDGOOD;
					}
				}
				break;
			case GVE_BADFILE:
				if (!simpleMatch(event->file, relpath))
					break;
				if (modificationTime <= event->time)
					file->status = GCFS_BAD;
				break;
		}
	}
}



LRESULT CALLBACK DlgCheckinVerifyProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);
int gimmeDialogCheckinVerify() {
	return (int)DialogBox (winGetHInstance(), (LPCTSTR) (IDD_DLG_CHECKINVERIFY), NULL, (DLGPROC)DlgCheckinVerifyProc);
}

LRESULT CALLBACK DlgCheckinVerifyProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	static int count=10;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_LIST), g_filenames);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_TXT_NOTES), 
"The files above might have errors that will cause pop-ups on other people's computer.  \
Files show up on this list because either a) you have not run the client and server \
since editting the file, or b) the client or server reported there were errors in the \
file.\r\n\r\nIf this is a CORE asset, you must run the CORE AssetManager or MaterialEditor.\r\n\r\n\
Do you still want to check in these files even though they may have errors?  \
Clicking Yes is NOT RECOMMENDED and will logged.");

			EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
			SetTimer(hDlg, 1, 1000, NULL);
			count = 10;
			// make the window flash if it is not the focus
			flashWindow(hDlg);
			return FALSE;
		}

	case WM_TIMER:
		if (count == 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000000)) {
			EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
			SetWindowText(GetDlgItem(hDlg, IDOK), L"Yes");
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

static char *log_message=NULL;
static char log_filename[CRYPTIC_MAX_PATH];

// returns 1 if OK, 0 if they cancelled/were not allowed
int gimmeCheckinVerify(GimmeQueuedAction **actions)
{
	// Check for not having ran the game if any of these files are later than the time of the last game running!
	int i;
	int hit_oktoall = 0;
	GimmeDir *gimme_dir;
	GimmeRulesWarn *rules_warn;
	GimmeCheckinFile **files=0;
	int ret;
	
	if (eaSize(&actions)==0)
		return 1;

	gimme_dir = actions[0]->gimme_dir;

	// Make local copy of list
	for (i=eaSize(&actions)-1; i>=0; i--) {
		char buf[4096];
		GimmeCheckinFile *file = NULL;
		// make a fake DB name to make sure that, when the name is created later, it won't be too long.
		sprintf_s(SAFESTR(buf), "%s%s_versions/%s_vb#99_v#999_%s.txt", actions[i]->gimme_dir->lock_dir, actions[i]->relpath, getFileName(actions[i]->relpath), gimmeGetUserName());
		// the "+ 10" is to account for later checkins by other users or unanticipated variations on the file name (such as ".deleted", etc).
		if ( strlen(buf) + 10 >= CRYPTIC_MAX_PATH)
		{
			int input;
			GimmeQueuedAction *action = NULL;
			if ( !hit_oktoall )
			{
				char dlgText[4096];
				sprintf_s(SAFESTR(dlgText), "The path for the following file is too long: \"%s\".  This file will not be added to the checkin list.  Choose cancel to cancel this checkin.", actions[i]->relpath);
				input = okToAllCancelDialog(dlgText, "Gimme Warning");
				hit_oktoall = ( IDOKTOALL == input );
				if ( input == IDCANCEL )
					return 0;
			}
			action = eaRemove(&actions, i);
			free(action);
			continue;
		}

		file = calloc(sizeof(GimmeCheckinFile), 1);
		file->action = actions[i];
		eaPush(&files, file);
	}

	// Filter files that are excluded and that are undo checkouts
	for (i=eaSize(&files)-1; i>=0; i--) {
		rules_warn = loadRulesWarn(files[i]->action->gimme_dir);
		if (ruleExcludes(rules_warn, files[i]->action->relpath) || files[i]->action->operation == GIMME_UNDO_CHECKOUT)
		{
			GimmeCheckinFile *file = eaRemove(&files, i);
			//printf("  %s excluded from checking rules\n", file->action->relpath);
			free(file);
		}
	}
	//printf("  %d files to check\n", eaSize(&files));
	if (eaSize(&files)==0)
		return 1;

	// Parse appropriate log files
	loadEventList(gimme_dir);

	// Check each file
	if (g_filenames)
		g_filenames[0]=0;
	for (i=0; i<eaSize(&files); i++) {
		checkFile(files[i]);
		if (files[i]->status == GCFS_BAD) {
			appendFilename(files[i]->action->relpath, "BAD (game and/or mapserver reported errors");
		} else if (files[i]->status == GCFS_NOTTESTED) {
			appendFilename(files[i]->action->relpath, "UNTESTED (you have not ran the game/mapserver since changing this file)");
		} else if (files[i]->status == GCFS_EXCLUDED) {
			appendFilename(files[i]->action->relpath, "UNTESTED (you may have ran a non-COV game/mapserver and this is a COV-only file)");
		}
		//printf("  %s: %s\n", files[i]->action->relpath, files[i]->status==GCFS_BAD?"BAD":files[i]->status==GCFS_NOTTESTED?"NOT TESTED":"Presumed good");
	}
	if (g_filenames && g_filenames[0]) {
		ret = gimmeDialogCheckinVerify();
		if (ret) {
			// Log this!
			char *s;
			strcpy(log_filename, gimme_dir->lock_dir);
			forwardSlashes(log_filename);
			if (strEndsWith(log_filename, "/"))
				log_filename[strlen(log_filename)-1]=0;
			s = strrchr(log_filename, '/');
			if (s)
				*s = 0;
			strcat(log_filename, "/logs/theyClickedYes.log");
			estrClear(&log_message);
			estrCreate(&log_message);
			estrPrintf(&log_message, "%s\t%s : ", timeGetLocalDateString(), gimmeGetUserName());
			for (i=0; i<eaSize(&files); i++) {
				if (files[i]->status == GCFS_BAD) {
					estrAppend2(&log_message, files[i]->action->relpath);
					estrAppend2(&log_message, " (BAD), ");
					if (i != eaSize(&files)-1)
						estrAppend2(&log_message, ", ");
				} else if (files[i]->status == GCFS_NOTTESTED) {
					estrAppend2(&log_message, files[i]->action->relpath);
					estrAppend2(&log_message, " (Not tested)");
					if (i != eaSize(&files)-1)
						estrAppend2(&log_message, ", ");
				} else if (files[i]->status == GCFS_EXCLUDED) {
					estrAppend2(&log_message, files[i]->action->relpath);
					estrAppend2(&log_message, " (Excluded)");
					if (i != eaSize(&files)-1)
						estrAppend2(&log_message, ", ");
				}
			}
			estrAppend2(&log_message, "\n");
		}
	} else {
		ret = 1;
	}
	// Cleanup

	return ret;
}

void gimmeCheckinVerifySaveLog(int save)
{
	if (log_message && save)
		logToFileName(log_filename, log_message, 0);
	estrDestroy(&log_message);
}
