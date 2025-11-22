//////////////////////////////////////////////////////////////////////////
// TODO:
//  Folder cache callback letting us know if we need to rescan (probably no auto-rescan)

#include <stdio.h>
#include <conio.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include "cmdparse.h"
#include "sysutil.h"
#include "systemspecs.h"
#include "UnitSpec.h"
#include "crypt.h"
#include "rand.h"
#include "EString.h"
#include "systemspecs.h"
#include "utilitiesLib.h"
#include "wininclude.h"
#include "resource.h"
#include "ListView.h"
#include "trivia.h"
#include "file.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "winutil.h"
#include "FilespecMap.h"
#include "textparser.h"
#include "StringCache.h"
#include "AppRegCache.h"
#include "gimmeDLLWrapper.h"
#include "FolderCache.h"
#include <ShlObj.h>

#define MAX_PROJECTS 10

const char *root="C:/Core";

typedef struct CoreSyncDef
{
	const char *filename;
	const char *name;
	SimpleFileSpec filespec;
} CoreSyncDef;

CoreSyncDef **core_sync_defs;

static ParseTable parse_CoreSyncDef[] = {
	{ "CoreSync",	TOK_IGNORE},
	{ "filename",	TOK_POOL_STRING|TOK_CURRENTFILE(CoreSyncDef,filename)},
	{ "Incl",		TOK_STRUCT(CoreSyncDef, filespec.entries, parse_filespec_include)},
	{ "Excl",		TOK_REDUNDANTNAME | TOK_STRUCT(CoreSyncDef, filespec.entries, parse_filespec_exclude) },
	{ "EndCoreSync",TOK_END,			0},
	{ "", 0, 0 }
};

static ParseTable parse_CoreSync_list[] = {
	{ "CoreSync",	TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	0,	sizeof(CoreSyncDef),		parse_CoreSyncDef},
	{ "", 0, 0 }
};

AUTO_RUN;
void initCoreSyncTPIs(void)
{
	ParserSetTableInfo(parse_CoreSyncDef, sizeof(CoreSyncDef), "parse_CoreSyncDef", NULL, __FILE__, false);
	ParserSetTableInfo(parse_CoreSync_list, sizeof(CoreSyncDef**), "parse_CoreSync_list", NULL, __FILE__, false);
}

bool scan_fix_branches=false;
AUTO_CMD_INT(scan_fix_branches, scan_fix_branches) ACMD_COMMANDLINE;

bool option_no_checkout=false;
bool option_only_orphans=false;
bool option_no_deletes=false;
bool option_all_matches=false; // Include all files in destination folders which match the filespecs (only for systems which should match in *both* directions with Core)
int option_selected_project_index=0;

char *lines[1024];
int numlines=0;
int insertline=0;
int maxlines=ARRAY_SIZE(lines);
int scrollLine=0;
CRITICAL_SECTION csLines;

typedef struct FileDesc FileDesc;
typedef struct ProjectDesc ProjectDesc;

typedef enum WorkerCmdType
{
	WorkerCmd_Nothing,
	WorkerCmd_Scan,
	WorkerCmd_Diff,
	WorkerCmd_Sync,
} WorkerCmdType;
typedef struct DiffCmd
{
	const char *root1;
	const char *root2;
	const char **relpaths;
} DiffCmd;
typedef struct SyncCmd
{
	const char *rootSrc;
	const char **rootDsts;
	int *dst_idxs;
	FileDesc **files;
	U64 totalSize;
} SyncCmd;

typedef struct WorkerCmd
{
	WorkerCmdType cmd_type;
	CoreSyncDef *scan_def;
	DiffCmd diff_cmd;
	SyncCmd sync_cmd;
} WorkerCmd;
struct
{
	WorkerCmd **cmds;
	int queued_scan;
} worker_cmds;

typedef enum UICmdType
{
	UICmd_Nothing,
	UICmd_ProjectsDone,
	UICmd_ClearFiles,
	UICmd_DoneScanning,
	UICmd_AddProject,
	UICmd_AddFile,
	UICmd_AddFileConditional, // Only if it doesn't exist
	UICmd_ModFile,
} UICmdType;
typedef struct UICmd
{
	UICmdType cmd_type;
	union
	{
		FileDesc *file;
		ProjectDesc *project;
	};
} UICmd;
struct  
{
	UICmd **cmds;
} ui_cmds;

void setScrollLine(void)
{
	scrollLine = insertline;
}

#define outputLine(...) outputLineEx(true, __VA_ARGS__)

void outputLineEx(bool bPrintf, const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);

	if (bPrintf)
		printf("\n%s", buf);
	EnterCriticalSection(&csLines);
	if (insertline == maxlines)
	{
		SAFE_FREE(lines[scrollLine]);
		memmove(&lines[scrollLine], &lines[scrollLine+1], (maxlines - scrollLine - 1)*sizeof(lines[0]));
		insertline--;
	} else {
		assert(insertline < ARRAY_SIZE(lines)-1);
		SAFE_FREE(lines[insertline]);
	}
	assert(insertline < ARRAY_SIZE(lines)-1);
	lines[insertline] = strdup(buf);
	insertline++;
	MAX1(numlines, insertline);
	LeaveCriticalSection(&csLines);
}

void updateLastLine(const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);

	printf("\r%s              ", buf);
	EnterCriticalSection(&csLines);
	SAFE_FREE(lines[insertline-1]);
	lines[insertline-1] = strdup(buf);
	LeaveCriticalSection(&csLines);
}

AUTO_STRUCT;
typedef struct LineDesc
{
	char *text; AST(FORMAT_LVWIDTH(255))
} LineDesc;

AUTO_STRUCT;
typedef struct ProjectDesc
{
	const char *name;
	const char *path;
} ProjectDesc;

AUTO_STRUCT;
typedef struct FileDesc
{
	const char *path; AST(POOL_STRING FORMAT_LVWIDTH(200))
	U32 CoreSize; NO_AST
	U32 Core; AST(FORMAT_FRIENDLYDATE FORMAT_LVWIDTH(115))
	U32 project[10]; AST(FORMAT_FRIENDLYDATE AUTO_INDEX(project) FORMAT_LVWIDTH(115))
} FileDesc;
STATIC_ASSERT(MAX_PROJECTS == ARRAY_SIZE(((FileDesc*)0)->project));

#include "AutoGen/CoreSync_c_ast.c"

HANDLE hUIThreadReady;
ListView *lvStatus;
ListView *lvFiles;
ListView *lvProjects;

ProjectDesc **projects;
FileDesc **files; // Same contents as the ListView, but possibly differently ordered

CoreSyncDef *current_def;

bool inited_def_list=false;

int scanning=0;

void updateParseTables(void)
{
	int i;
	FOR_EACH_IN_EARRAY_FORWARDS(projects, ProjectDesc, project)
	{
		char old_key[100];
		bool bFoundOne=false;
		sprintf(old_key, "project_%d", iprojectIndex);
		FORALL_PARSETABLE(parse_FileDesc, i)
		{
			if (stricmp(parse_FileDesc[i].name, old_key)==0)
			{
				parse_FileDesc[i].name = allocAddString(project->name);
				bFoundOne = true;
			}
		}
		assertmsg(bFoundOne, "Could not find key in parsetable - possibly too many projects found.");
	}
	FOR_EACH_END;

	// Remove unused projects
	FORALL_PARSETABLE(parse_FileDesc, i)
	{
		if (strStartsWith(parse_FileDesc[i].name, "project_"))
		{
			parse_FileDesc[i].name = NULL;
			parse_FileDesc[i].type = 0;
		}
	}

}

int color_base = 200;

void filesSubItemColor(ListView *lv, void *structptr, int row, int column, COLORREF *clrText, COLORREF *clrTextBk)
{
	FileDesc *file = structptr;
	//*clrText = RGB(0, 0, 0);
	if (column == 0) {
		// path
	} else if (column == 1) {
		// Core
	} else {
		// specific project
		int project_index = column - 2;
		if (file->project[project_index] == file->Core)
		{
			*clrTextBk = RGB(color_base, 255, color_base);
		} else if (file->project[project_index] == 0) {
			*clrTextBk = RGB(255, 255, color_base);
		} else {
			bool bMatchesFirst=false;
			int i;
			for (i=0; i<=project_index; i++)
			{
				if (file->project[i] && file->project[i] != file->Core)
				{
					if (file->project[i] == file->project[project_index])
						bMatchesFirst = true;
					break;
				}
			}
			if (bMatchesFirst)
			{
				*clrTextBk = RGB(255, color_base, color_base);
			} else {
				*clrTextBk = RGB(255, color_base, 255);
			}
		}
	}
}

void gatherProjects(ListView *lv, void *structptr, void *data)
{
	int **projects_selected = data;
	eaiPush(projects_selected, eaFind(&projects, structptr));
}

void diffGatherFiles(ListView *lv, void *structptr, void *data)
{
	FileDesc *file = structptr;
	DiffCmd *cmd = data;
	eaPush(&cmd->relpaths, file->path);
}

void doDiff(void)
{
	WorkerCmd cmd = {0};
	WorkerCmd *pcmd;
	int *projects_selected=NULL;
	listViewDoOnSelected(lvProjects, gatherProjects, &projects_selected);
	if (eaiSize(&projects_selected)==1)
	{
		// diff vs. Core
		cmd.diff_cmd.root1 = root;
		cmd.diff_cmd.root2 = projects[projects_selected[0]]->path;
	} else if (eaiSize(&projects_selected)==2) {
		// diff between projects
		cmd.diff_cmd.root1 = projects[projects_selected[0]]->path;
		cmd.diff_cmd.root2 = projects[projects_selected[1]]->path;
	} else {
		// wrong number of projects selected
		return;
	}
	eaiDestroy(&projects_selected);
	listViewDoOnSelected(lvFiles, diffGatherFiles, &cmd.diff_cmd);

	cmd.cmd_type = WorkerCmd_Diff;
	pcmd = memdup(&cmd, sizeof(cmd));
	EnterCriticalSection(&csLines);
	eaPush(&worker_cmds.cmds, pcmd);
	LeaveCriticalSection(&csLines);
}

typedef struct SyncGather
{
	SyncCmd *cmd;
	int src_idx;
} SyncGather;

void syncGatherFiles(ListView *lv, void *structptr, void *data)
{
	FileDesc *file = structptr;
	SyncGather *sync = data;
	U32 srct = file->project[sync->src_idx];
	bool needSync=false;
	int i;
	for (i=0; i<eaiSize(&sync->cmd->dst_idxs); i++)
	{
		if (option_only_orphans)
		{
			if (file->project[sync->cmd->dst_idxs[i]] == 0)
				needSync = true;
		} else {
			if (file->project[sync->cmd->dst_idxs[i]] != srct &&
				(!option_no_deletes || file->project[sync->src_idx]))
				needSync=true;
		}
	}

	if (needSync)
	{
		if (file->CoreSize != (U32)-1)
			sync->cmd->totalSize += file->CoreSize;
		eaPush(&sync->cmd->files, file);
	}
}

void destroySyncCmd(SyncCmd *cmd)
{
	eaDestroy(&cmd->rootDsts);
	eaiDestroy(&cmd->dst_idxs);
	eaDestroy(&cmd->files);
}

void doSyncFinishGather(HWND hDlg, SyncGather *gather, WorkerCmd **pcmd_out)
{
	WorkerCmd cmd = {0};
	listViewDoOnSelected(lvFiles, syncGatherFiles, gather);

	if (eaSize(&gather->cmd->files)==0)
	{
		destroySyncCmd(gather->cmd);
		if (!pcmd_out)
			MessageBox(hDlg, "No selected files need syncing", "Nothing to do", MB_OK);
	} else {
		WorkerCmd *pcmd;
		cmd.sync_cmd = *gather->cmd;
		cmd.cmd_type = WorkerCmd_Sync;
		pcmd = memdup(&cmd, sizeof(cmd));
		if (pcmd_out)
		{
			*pcmd_out = pcmd;
		} else {
			EnterCriticalSection(&csLines);
			eaPush(&worker_cmds.cmds, pcmd);
			LeaveCriticalSection(&csLines);
		}
	}
}

void doSyncToProjects(HWND hDlg, WorkerCmd **pcmd)
{
	SyncCmd cmd = {0};
	SyncGather gather = {0};
	int i;
	listViewDoOnSelected(lvProjects, gatherProjects, &cmd.dst_idxs);
	if (eaiSize(&cmd.dst_idxs)==0)
	{
		destroySyncCmd(&cmd);
		// no projects selected
		return;
	} else {
		cmd.rootSrc = root;
		for (i=0; i<eaiSize(&cmd.dst_idxs); i++)
		{
			eaPush(&cmd.rootDsts, projects[cmd.dst_idxs[i]]->path);
		}
	}
	gather.src_idx = -1; // Yes, this is totally evil what happens with this -1...
	gather.cmd = &cmd;

	doSyncFinishGather(hDlg, &gather, pcmd);
}

void doSyncToCore(HWND hDlg)
{
	SyncCmd cmd = {0};
	SyncGather gather = {0};

	if (IDYES!=MessageBox(hDlg, "This will sync from the selected project and OVERWRITE CORE.  Are you sure you want to do this?", "Sync TO CORE", MB_YESNO))
		return;

	listViewDoOnSelected(lvProjects, gatherProjects, &cmd.dst_idxs);
	if (eaiSize(&cmd.dst_idxs)!=1)
	{
		destroySyncCmd(&cmd);
		// not the correct number of projects selected (1)
		return;
	}
	gather.src_idx = cmd.dst_idxs[0];
	cmd.rootSrc = projects[cmd.dst_idxs[0]]->path;
	cmd.dst_idxs[0] = -1; // Yes, this is totally evil what happens with this -1...
	eaPush(&cmd.rootDsts, root);

	gather.cmd = &cmd;

	doSyncFinishGather(hDlg, &gather, NULL);
}

void doFilesetReload(void)
{
	WorkerCmd *cmd = calloc(1, sizeof(WorkerCmd));
	bool notokay;
	EnterCriticalSection(&csLines);
	notokay = scanning || eaSize(&ui_cmds.cmds) || eaSize(&worker_cmds.cmds) || !current_def || !inited_def_list;
	LeaveCriticalSection(&csLines);
	if (notokay)
		return;
	eaDestroyStruct(&current_def->filespec.entries, parse_filespec_include);
	ParserLoadFiles(NULL, current_def->filename, NULL, 0, parse_CoreSyncDef, current_def);
	assert(current_def->name); // Should be preserved

	// Initiate new scan
	EnterCriticalSection(&csLines);
	cmd->cmd_type = WorkerCmd_Scan;
	cmd->scan_def = current_def;
	worker_cmds.queued_scan++;
	scanning++;
	eaPush(&worker_cmds.cmds, cmd);
	LeaveCriticalSection(&csLines);
}

void gatherFile(ListView *lv, void *structptr, void *data)
{
	FileDesc *file = structptr;
	FileDesc **file_out = data;
	*file_out = file;
}

void doOpenFolder(HWND hDlg)
{
	int *projects_selected=NULL;
	FileDesc *file=NULL;
	int numFiles;
	listViewDoOnSelected(lvProjects, gatherProjects, &projects_selected);
	numFiles = listViewDoOnSelected(lvFiles, gatherFile, &file);
	if (numFiles == 1 && eaiSize(&projects_selected)<=1)
	{
		char path[MAX_PATH];
		assert(file);
		if (eaiSize(&projects_selected)==1)
		{
			// Open the selected projects
			sprintf(path, "%s/%s", projects[projects_selected[0]]->path, file->path);
		} else {
			// Open Core
			sprintf(path, "%s/%s", root, file->path);
		}
		getDirectoryName(path);
		ShellExecute(hDlg, "", path, NULL, path, SW_NORMAL);
	}
	eaiDestroy(&projects_selected);
}

void updateFilesText(HWND hDlg, int num_selected_files, int num_selected_projects)
{
	char buf[1024];
	if (lvFiles)
	{
		WorkerCmd *cmd=NULL;
		doSyncToProjects(hDlg, &cmd);
		if (cmd)
		{
			sprintf(buf, "Files (%d):  %d selected files, %d mismatches, %d selected projects", eaSize(&files), num_selected_files, eaSize(&cmd->sync_cmd.files), num_selected_projects);
			destroySyncCmd(&cmd->sync_cmd);
			free(cmd);
		} else if (num_selected_files && num_selected_projects) {
			sprintf(buf, "Files (%d):  %d selected files, 0 mismatches, %d selected projects", eaSize(&files), num_selected_files, num_selected_projects);
		} else {
			sprintf(buf, "Files (%d):  %d selected files, %d selected projects", eaSize(&files), num_selected_files, num_selected_projects);
		}
		SetWindowText(GetDlgItem(hDlg, IDC_TXT_FILES), buf);
	}
}

void updateButtons(HWND hDlg)
{
	int num_selected_projects;
	int num_selected_files;
	num_selected_projects = lvProjects?listViewDoOnSelected(lvProjects, NULL, NULL):0;
	num_selected_files = lvFiles?listViewDoOnSelected(lvFiles, NULL, NULL):0;
	EnableWindow(GetDlgItem(hDlg, IDC_FILES_DIFF), !scanning && num_selected_files && ((num_selected_projects == 1 || num_selected_projects == 2)));
	EnableWindow(GetDlgItem(hDlg, IDC_SYNC_TO_CORE), !scanning && num_selected_files && (num_selected_projects==1));
	EnableWindow(GetDlgItem(hDlg, IDC_SYNC_TO_PROJECTS), !scanning && num_selected_files && num_selected_projects);
	EnableWindow(GetDlgItem(hDlg, IDC_OPEN_FOLDER), !scanning && (num_selected_files==1) && (num_selected_projects<=1));
	EnableWindow(GetDlgItem(hDlg, IDC_FILES_SELECTALL), !scanning);
	EnableWindow(GetDlgItem(hDlg, IDC_FILESET_RELOAD), !scanning);
	updateFilesText(hDlg, num_selected_files, num_selected_projects);
}

void onProjectsNotify(void)
{
	static int *selected_projects;
	int i;
	for (i=0; i<eaSize(&projects); i++)
	{
		int selected=0;
		if (listViewIsSelected(lvProjects, projects[i]))
		{
			selected = 1;
		}
		if (selected != eaiGet(&selected_projects, i))
		{
			char key[1024];
			sprintf(key, "Selected_%d", i);
			eaiSet(&selected_projects, selected, i);
			regPutAppInt(key, selected);
		}
	}
}


bool need_update_buttons=true;

LRESULT CALLBACK DialogMain(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	RECT rect;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			HICON hIcon;

			hIcon = LoadIcon(winGetHInstance(), MAKEINTRESOURCE(IDI_ICON1));
			SendMessage(hDlg, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)hIcon);
			SendMessage(hDlg, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
			DeleteObject(hIcon);

			SendMessage(GetDlgItem(hDlg, IDC_NO_CHECKOUT), BM_SETCHECK, (WPARAM)(int)option_no_checkout, 0L);
			SendMessage(GetDlgItem(hDlg, IDC_ONLY_ORPHANS), BM_SETCHECK, (WPARAM)(int)option_only_orphans, 0L);
			SendMessage(GetDlgItem(hDlg, IDC_NO_DELETES), BM_SETCHECK, (WPARAM)(int)option_no_deletes, 0L);
			SendMessage(GetDlgItem(hDlg, IDC_ALL_MATCHES), BM_SETCHECK, (WPARAM)(int)option_all_matches, 0L);

			lvStatus = listViewCreate();
			listViewInit(lvStatus, parse_LineDesc, hDlg, GetDlgItem(hDlg, IDC_STATUS));
			listViewSetColumnWidth(lvStatus, 0, 1000);

			lvProjects = listViewCreate();
			listViewInit(lvProjects, parse_ProjectDesc, hDlg, GetDlgItem(hDlg, IDC_PROJECTS));
			listViewEnableColor(lvProjects, false);

			GetClientRect(hDlg, &rect); 
			doDialogOnResize(hDlg, (WORD)(rect.right - rect.left), (WORD)(rect.bottom - rect.top), IDC_ALIGNME, IDC_UPPERLEFT);
			setDialogMinSize(hDlg, (WORD)(rect.right - rect.left), (WORD)(rect.bottom - rect.top));

			GetWindowRect(GetDlgItem(hDlg, IDC_STATUS), &rect);
			SendMessage(GetDlgItem(hDlg, IDC_STATUS), LVM_SETCOLUMNWIDTH, (WPARAM)0, MAKELPARAM((rect.right - rect.left - 32), 0));

			SetTimer(hDlg, 0, 100, NULL);

			SetEvent(hUIThreadReady);
		}
		break;
	case WM_TIMER:
	{
		bool bFilesChanged=false;
		EnterCriticalSection(&csLines);
		// Update status line display
		{
			static LineDesc last_lines[ARRAY_SIZE(lines)];
			static int last_numlines;
			static int last_state = -1;
			int i;
			for (i=0; i<last_numlines; i++)
			{
				if (strcmp(last_lines[i].text, lines[i])!=0)
				{
					SAFE_FREE(last_lines[i].text);
					last_lines[i].text = strdup(lines[i]);
					listViewItemChanged(lvStatus, &last_lines[i]);
				}
			}
			for (i=last_numlines; i<numlines; i++)
			{
				assert(i<ARRAY_SIZE(last_lines)-1);
				last_lines[i].text = strdup(lines[i]);
				listViewAddItem(lvStatus, &last_lines[i]);
			}
			last_numlines = numlines;
		}

		// Updated list of filespecs
		if (!inited_def_list && core_sync_defs)
		{
			WorkerCmd *cmd = calloc(1, sizeof(WorkerCmd));
			int i;
			for (i=0; i<eaSize(&core_sync_defs); i++) 
				SendMessage(GetDlgItem(hDlg, IDC_FILESET), CB_ADDSTRING, 0, (LPARAM)core_sync_defs[i]->name);


			if (option_selected_project_index < eaSize(&core_sync_defs))
			{
				SendMessage(GetDlgItem(hDlg, IDC_FILESET), CB_SETCURSEL, option_selected_project_index, 0);
				current_def = core_sync_defs[option_selected_project_index];
			} else {
				SendMessage(GetDlgItem(hDlg, IDC_FILESET), CB_SETCURSEL, 0, 0);
				current_def = core_sync_defs[0];
			}
			// Initiate scan
			cmd->cmd_type = WorkerCmd_Scan;
			cmd->scan_def = current_def;
			worker_cmds.queued_scan++;
			scanning++;
			eaPush(&worker_cmds.cmds, cmd);
			inited_def_list = true;
		}
		// Handle commands
		{
			int i;
			UICmd **cmd_list=NULL;
			if (eaSize(&ui_cmds.cmds))
			{
				cmd_list = ui_cmds.cmds;
				ui_cmds.cmds = NULL;
			}
			LeaveCriticalSection(&csLines);
			for (i=0; i<eaSize(&cmd_list); i++)
			{
				UICmd *cmd = cmd_list[i];
				switch (cmd->cmd_type)
				{
				case UICmd_ProjectsDone:
					{
						int j;
						bool selected_one=false;
						for (j=0; j<eaSize(&projects); j++)
						{
							char key[1024];
							sprintf(key, "Selected_%d", j);
							if (regGetAppInt(key, 0))
							{
								listViewSetSelected(lvProjects, projects[j], true);
								selected_one = true;
							}
						}

						if (!selected_one)
							listViewSelectAll(lvProjects, true);
					}
					// Update parsetable with friendly names
					updateParseTables();
					// Now create the listview with the new names
					lvFiles = listViewCreate();
					listViewInit(lvFiles, parse_FileDesc, hDlg, GetDlgItem(hDlg, IDC_FILES));
					listViewSetNoScrollToEndOnAdd(lvFiles, true);
					listViewEnableColor(lvFiles, true);
					listViewSetColorFuncs(lvFiles, NULL, filesSubItemColor);
					listViewDoingInitialBuild(lvFiles, true);
					break;
				case UICmd_ClearFiles:
					listViewDelAllItems(lvFiles, NULL);
					eaClearEx(&files, NULL);
					break;
				case UICmd_DoneScanning:
					scanning--;
					need_update_buttons = true;
					break;
				case UICmd_AddProject:
					listViewAddItem(lvProjects, cmd->project);
					eaPush(&projects, cmd->project);
					break;
				case UICmd_AddFile:
					listViewAddItem(lvFiles, cmd->file);
					eaPush(&files, cmd->file);
					bFilesChanged=true;
					break;
				case UICmd_AddFileConditional:
					{
						int j;
						bool bAdd=true;
						for (j=0; j<eaSize(&files) && bAdd; j++)
						{
							if (files[j]->path == cmd->file->path)
								bAdd = false;
						}
						if (bAdd)
						{
							listViewAddItem(lvFiles, cmd->file);
							eaPush(&files, cmd->file);
							bFilesChanged=true;
						} else {
							free(cmd->file);
						}
					}
					break;
				case UICmd_ModFile:
					listViewItemChanged(lvFiles, cmd->file);
					break;
				}
				free(cmd);
			}
			eaDestroy(&cmd_list);

			if (bFilesChanged)
				need_update_buttons = true;
		}

		if (need_update_buttons)
		{
			updateButtons(hDlg);
			need_update_buttons = false;
		}
		break;
	}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_FILESET_EDIT:
			fileOpenWithEditor(current_def->filename);
			break;
		case IDC_FILESET_RELOAD:
			doFilesetReload();
			break;
		case IDC_NO_CHECKOUT:
			if (SendMessage(GetDlgItem(hDlg, IDC_NO_CHECKOUT), BM_GETCHECK, 0, 0))
				option_no_checkout = 1;
			else
				option_no_checkout = 0;
			regPutAppInt("NoCheckout", option_no_checkout);
			break;
		case IDC_ONLY_ORPHANS:
			if (SendMessage(GetDlgItem(hDlg, IDC_ONLY_ORPHANS), BM_GETCHECK, 0, 0))
				option_only_orphans = 1;
			else
				option_only_orphans = 0;
			need_update_buttons = true;
			regPutAppInt("OnlyOrphans", option_only_orphans);
			break;
		case IDC_NO_DELETES:
			if (SendMessage(GetDlgItem(hDlg, IDC_NO_DELETES), BM_GETCHECK, 0, 0))
				option_no_deletes = 1;
			else
				option_no_deletes = 0;
			need_update_buttons = true;
			regPutAppInt("NoDeletes", option_no_deletes);
			break;
		case IDC_ALL_MATCHES:
			if (SendMessage(GetDlgItem(hDlg, IDC_ALL_MATCHES), BM_GETCHECK, 0, 0))
				option_all_matches = 1;
			else
				option_all_matches = 0;
			need_update_buttons = true;
			regPutAppInt("AllMatches", option_all_matches);
			break;
		case IDC_FILESET:
			if (inited_def_list)
			{
				char buf[1024];
				int i;
				option_selected_project_index = SendMessage(GetDlgItem(hDlg, IDC_FILESET), CB_GETCURSEL, 0, 0);
				regPutAppInt("SelectedProject", option_selected_project_index);
				GetWindowText(GetDlgItem(hDlg, IDC_FILESET), buf, ARRAY_SIZE(buf));
				// Initiate new scan
				for (i=0; i<eaSize(&core_sync_defs); i++)
				{
					if (stricmp(core_sync_defs[i]->name, buf)==0)
					{
						if (current_def != core_sync_defs[i])
						{
							WorkerCmd *cmd = calloc(1, sizeof(WorkerCmd));
							current_def = core_sync_defs[i];
							cmd->cmd_type = WorkerCmd_Scan;
							cmd->scan_def = current_def;
							EnterCriticalSection(&csLines);
							worker_cmds.queued_scan++;
							scanning++;
							eaPush(&worker_cmds.cmds, cmd);
							LeaveCriticalSection(&csLines);
						}
						break;
					}
				}
			}
			break;
		case IDOK:
			return TRUE;
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			exit(0);
			return TRUE;
		case IDC_FILES_DIFF:
			doDiff();
			return TRUE;
		case IDC_SYNC_TO_PROJECTS:
			doSyncToProjects(hDlg, NULL);
			return TRUE;
		case IDC_SYNC_TO_CORE:
			doSyncToCore(hDlg);
			return TRUE;
		case IDC_OPEN_FOLDER:
			doOpenFolder(hDlg);
			return TRUE;
		case IDC_FILES_SELECTALL:
			if (lvFiles)
			{
				if (listViewGetNumItems(lvFiles) == listViewDoOnSelected(lvFiles, NULL, NULL))
					listViewSelectAll(lvFiles, false);
				else
					listViewSelectAll(lvFiles, true);
			}
			return TRUE;
		case IDC_PROJECTS_SELECTALL:
			if (lvProjects)
			{
				if (listViewGetNumItems(lvProjects) == listViewDoOnSelected(lvProjects, NULL, NULL))
					listViewSelectAll(lvProjects, false);
				else
					listViewSelectAll(lvProjects, true);
			}
			return TRUE;
		}
		break;
	case WM_SIZE:
	{
		RECT r;
		WORD w = LOWORD(lParam);
		WORD h = HIWORD(lParam);
		doDialogOnResize(hDlg, w, h, IDC_ALIGNME, IDC_UPPERLEFT);
		GetWindowRect(GetDlgItem(hDlg, IDC_STATUS), &r);
		SendMessage(GetDlgItem(hDlg, IDC_STATUS), LVM_SETCOLUMNWIDTH, (WPARAM)0, MAKELPARAM((r.right - r.left - 32), 0));
		break;
	}
	case WM_NOTIFY:
	{
		BOOL handled=FALSE;
		int idCtrl = (int)wParam;
		if (lvProjects)
		{
			handled |= listViewOnNotify(lvProjects, wParam, lParam, NULL);
			if (idCtrl == IDC_PROJECTS)
				onProjectsNotify();
		}
		if (lvFiles)
			handled |= listViewOnNotify(lvFiles, wParam, lParam, NULL);

		need_update_buttons = true;
		return handled;
	}
	case WM_DESTROY:
	case WM_CLOSE:
		EndDialog(hDlg, IDCANCEL);
		exit(0);
		return TRUE;
		break;
	}

	return FALSE;
}

int __cdecl UIThread(void *param)
{
	EXCEPTION_HANDLER_BEGIN
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
		DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MAIN), compatibleGetConsoleWindow(), DialogMain);
	}
	EXCEPTION_HANDLER_END;
	return 0;
}

void startUIThread(void)
{
	InitializeCriticalSection(&csLines);
	hUIThreadReady = CreateEvent(NULL, FALSE, FALSE, NULL);
	_beginthread(UIThread, 0, NULL);
	WaitForSingleObject(hUIThreadReady, INFINITE);
}

void scanForProjects(void)
{
	UICmd *cmd;
	int i;
	char **folders = fileScanDirFoldersNoSubdirRecurse("C:\\", FSF_FOLDERS|FSF_NOHIDDEN);
	for (i=0; i<eaSize(&folders); i++)
	{
		char path[MAX_PATH];
		strcpy(path, folders[i]);
		if (stricmp(path, "C:\\Cryptic")==0)
			continue;
		if (strstri(path, "fix") && !scan_fix_branches)
			continue;
		if (strstri(path, "beacon"))
			continue;
		if (strstri(path, "core"))
			continue;
		strcat(path, "\\.patch\\patch_trivia.txt");
		if (fileExists(path))
		{
			callocStruct(cmd, UICmd);
			callocStruct(cmd->project, ProjectDesc);
			cmd->project->name = allocAddFilename(folders[i] + 3);
			cmd->project->path = allocAddFilename(folders[i]);
			cmd->cmd_type = UICmd_AddProject;
			EnterCriticalSection(&csLines);
			eaPush(&ui_cmds.cmds, cmd);
			LeaveCriticalSection(&csLines);
		}
	}
	eaDestroyEx(&folders, NULL);

	callocStruct(cmd, UICmd);
	cmd->cmd_type = UICmd_ProjectsDone;
	EnterCriticalSection(&csLines);
	eaPush(&ui_cmds.cmds, cmd);
	LeaveCriticalSection(&csLines);
}

void coreSyncLoadDefs(void)
{
	CoreSyncDef **temp=NULL;
	U32 startt = timerCpuMs();
	outputLine("Loading CoreSync defs...");
	ParserLoadFiles("C:/Cryptic/tools/CoreSync/", "CoreSync", NULL, 0, parse_CoreSync_list, &temp);
	FOR_EACH_IN_EARRAY(temp, CoreSyncDef, def)
	{
		char buf[MAX_PATH];
		getFileNameNoExt(buf, def->filename);
		def->name = allocAddFilename(buf);
	}
	FOR_EACH_END;
	updateLastLine("Loading CoreSync defs... done (%d defs, %1.1fs).", eaSize(&temp), (timerCpuMs() - startt)/1000.f);
	EnterCriticalSection(&csLines);
	core_sync_defs = temp;
	LeaveCriticalSection(&csLines);
}

void scanAddFile(const char *relative_path, bool conditional)
{
	int i;
	UICmd *cmd;
	FileDesc *file;
	char full_path[MAX_PATH];

	file = calloc(sizeof(*file), 1);
	file->path = allocAddFilename(relative_path);
	sprintf(full_path, "%s/%s", root, relative_path);
	file->CoreSize = fileSize(full_path);
	file->Core = fileLastChanged(full_path);
	for (i=0; i<eaSize(&projects); i++)
	{
		sprintf(full_path, "%s/%s", projects[i]->path, relative_path);
		file->project[i] = fileLastChanged(full_path);
	}
	callocStruct(cmd, UICmd);
	cmd->cmd_type = conditional?UICmd_AddFileConditional:UICmd_AddFile;
	cmd->file = file;
	EnterCriticalSection(&csLines);
	eaPush(&ui_cmds.cmds, cmd);
	LeaveCriticalSection(&csLines);
}

bool canAnyMatch(const char *relpath, SimpleFileSpec *filespec)
{
	int i;
	for (i=0; i<eaSize(&filespec->entries); i++)
	{
		const char *s = relpath;
		const char *fs = filespec->entries[i]->filespec;
		bool bCanMatch=true;
		if (!filespec->entries[i]->doInclude)
			continue;
		while (*s && *fs && *fs != '*')
		{
			if (toupper(*s) != toupper(*fs))
			{
				bCanMatch = false;
				break;
			}
			s++;
			fs++;
		}
		if (bCanMatch)
			return true;
	}
	return false;
}

CoreSyncDef *currently_scanning;
bool scan_cancelled=false;
FileScanAction doScanProcessor(char *dir, struct _finddata32_t* data, void *userData)
{
	CoreSyncDef *scan_def = (CoreSyncDef*)userData;
	char relpath[MAX_PATH];
	if (worker_cmds.queued_scan)
	{
		scan_cancelled = true;
		// Must be a command to start a new scan coming
		return FSA_STOP;
	}
	assert(strStartsWith(dir, root));
	if (stricmp(dir, root)==0)
		sprintf(relpath, "%s", data->name);
	else
		sprintf(relpath, "%s/%s", dir + strlen(root) + 1, data->name);
	if (data->attrib & _A_SUBDIR)
	{
		// Optimization: check if no filespec entry references this folder in any way, skip it.  something like *.ext will make this fail/do nothing though
		if (canAnyMatch(relpath, &scan_def->filespec))
			return FSA_EXPLORE_DIRECTORY;
		else
			return FSA_NO_EXPLORE_DIRECTORY;
	}
	if (simpleFileSpecIncludesFile(relpath, &scan_def->filespec))
	{
		//printf("%s\n", relpath);
		scanAddFile(relpath, false);
	}
	return FSA_EXPLORE_DIRECTORY;
}

int other_project_index;
FileScanAction doScanProcessorOtherProject(char *dir, struct _finddata32_t* data, void *userData)
{
	CoreSyncDef *scan_def = (CoreSyncDef*)userData;
	char relpath[MAX_PATH];
	if (worker_cmds.queued_scan)
	{
		scan_cancelled = true;
		// Must be a command to start a new scan coming
		return FSA_STOP;
	}
	assert(strStartsWith(dir, projects[other_project_index]->path));
	if (stricmp(dir, projects[other_project_index]->path)==0)
		sprintf(relpath, "%s", data->name);
	else
		sprintf(relpath, "%s/%s", dir + strlen(projects[other_project_index]->path) + 1, data->name);
	if (data->attrib & _A_SUBDIR)
	{
		// Optimization: check if no filespec entry references this folder in any way, skip it.  something like *.ext will make this fail/do nothing though
		if (canAnyMatch(relpath, &scan_def->filespec))
			return FSA_EXPLORE_DIRECTORY;
		else
			return FSA_NO_EXPLORE_DIRECTORY;
	}

	if (simpleFileSpecIncludesFile(relpath, &scan_def->filespec))
	{
		//printf("%s\n", relpath);
		scanAddFile(relpath, true);
	}
	return FSA_EXPLORE_DIRECTORY;
}

void doScan(CoreSyncDef *scan_def)
{
	UICmd *cmd;
	U32 startt = timerCpuMs();
	currently_scanning = scan_def;
	scan_cancelled = false;
	outputLine("Scanning for matching files...");
	// clear current file list
	callocStruct(cmd, UICmd);
	cmd->cmd_type = UICmd_ClearFiles;
	EnterCriticalSection(&csLines);
	eaPush(&ui_cmds.cmds, cmd);
	LeaveCriticalSection(&csLines);

	fileScanDirRecurseEx(root, doScanProcessor, scan_def);

	if (option_all_matches)
	{
		int i;
		for (i=0; i<eaSize(&projects); i++)
		{
			other_project_index = i;
			assert(!strEndsWith(projects[other_project_index]->path, "/"));
			fileScanDirRecurseEx(projects[other_project_index]->path, doScanProcessorOtherProject, scan_def);
			other_project_index = -1;
		}
	}

	if (scan_cancelled)
		updateLastLine("Scanning for matching files... CANCELLED.");
	else
		updateLastLine("Scanning for matching files... done (%1.1fs).", (timerCpuMs() - startt)/1000.f);

	callocStruct(cmd, UICmd);
	cmd->cmd_type = UICmd_DoneScanning;
	EnterCriticalSection(&csLines);
	eaPush(&ui_cmds.cmds, cmd);
	LeaveCriticalSection(&csLines);
}

void diffExecute(DiffCmd *cmd, const char *relpath, char **diff_result, bool diff_single_pair)
{
	char path1[MAX_PATH];
	char path2[MAX_PATH];

	sprintf(path1, "%s/%s", cmd->root1, relpath);
	sprintf(path2, "%s/%s", cmd->root2, relpath);
	if (diff_single_pair)
	{
		fileLaunchDiffProgram(path1, path2);
	} else {
		if (!fileExists(path1)) {
			if (!fileExists(path2)) {
				estrConcatf(diff_result, "     SAME: %s (neither exists)\n", relpath);
			} else {
				estrConcatf(diff_result, "   ORPHAN: %s does not exist in %s\n", relpath, cmd->root1);
			}
		} else if (!fileExists(path2)) {
				estrConcatf(diff_result, "   ORPHAN: %s does not exist in %s\n", relpath, cmd->root2);
		} else {
			if (fileCompare(path1, path2)==0)
			{
				estrConcatf(diff_result, "     SAME: %s\n", relpath);
			} else {
				estrConcatf(diff_result, "DIFFERENT: %s\n", relpath);
			}
		}
	}
}

void workerDoDiff(DiffCmd *cmd)
{
	int i;
	FILE *f;
	char result_path[MAX_PATH] = "C:/temp/diff_result.txt";
	bool diff_single_pair = (eaSize(&cmd->relpaths)==1);
	char *diff_result=NULL;
	U32 startt = timerCpuMs();

	outputLine("Running diff...");

	estrConcatf(&diff_result, "Comparing %s and %s\n", cmd->root1, cmd->root2);
	for (i=0; i<eaSize(&cmd->relpaths); i++)
	{
		diffExecute(cmd, cmd->relpaths[i], &diff_result, diff_single_pair);
	}
	makeDirectoriesForFile(result_path);
	if (!diff_single_pair)
	{
		f = fopen(result_path, "w");
		if (f)
		{
			fwrite(diff_result, 1, estrLength(&diff_result), f);
			fclose(f);
			fileOpenWithEditor(result_path);
		} else {
			Errorf("Failed to open %s for writing", result_path);
		}
	}
	estrDestroy(&diff_result);
	eaDestroy(&cmd->relpaths);
	updateLastLine("Running diff... done (%1.1fs).", (timerCpuMs() - startt)/1000.f);
}

char syncbuf[64*1024];

void syncExecute(SyncCmd *cmd, FileDesc *file)
{
	char path[MAX_PATH];
	FILE *fin;
	FILE *fout[100];
	int i;
	int numread=0;
	struct _stat32 sbuf;
	bool bDelete=false;
	bool bSbufInited=false;

	assert(eaSize(&cmd->rootDsts) < ARRAY_SIZE(fout));
	
	sprintf(path, "%s/%s", cmd->rootSrc, file->path);
	if (!fileExists(path))
	{
		if (option_no_deletes)
			return;
		bDelete = true;
	} else {
		fin = fopen(path, "rb");
		_stat32(path, &sbuf);
		bSbufInited = true;
		if (!fin)
		{
			updateLastLine("Error opening %s for reading", path);
			outputLine("");
			return;
		}
	}

	for (i=0; i<eaSize(&cmd->rootDsts); i++)
	{
		sprintf(path, "%s/%s", cmd->rootDsts[i], file->path);

		if (bDelete)
		{
			fileForceRemove(path);
			rmdirtree(path);
			fout[i] = NULL;
		} else {
			if (option_only_orphans)
			{
				if (fileExists(path))
				{
					fout[i] = NULL;
					continue;
				}
			} else {
				if (option_no_checkout)
					chmod(path, _S_IREAD|_S_IWRITE);
			}
			mkdirtree(path);
			fout[i] = fopen(path, "wb");
			if (!fout[i])
			{
				updateLastLine("Error opening %s for writing", path);
				outputLine("");
			}
		}
	}

	if (!bDelete)
	{
		do 
		{
			numread = (int)fread(syncbuf, 1, ARRAY_SIZE(syncbuf), fin);
			if (numread)
			{
				for (i=0; i<eaSize(&cmd->rootDsts); i++)
				{
					if (fout[i])
					{
						fwrite(syncbuf, 1, numread, fout[i]);
					}
				}
			}
		} while (numread);
		fclose(fin);
	}

	for (i=0; i<eaSize(&cmd->rootDsts); i++)
	{
		if (fout[i])
		{
			struct _utimbuf utb;
			fclose(fout[i]);
			sprintf(path, "%s/%s", cmd->rootDsts[i], file->path);
			assert(bSbufInited);
			utb.actime = sbuf.st_atime;
			utb.modtime = sbuf.st_mtime;
			if (_utime(path, &utb)!=0)
			{
				updateLastLine("Error updating timestamp on %s", path);
				outputLine("");
			}
		}
		if (fout[i] || bDelete)
		{
			UICmd *uicmd;
			file->project[cmd->dst_idxs[i]] = fileLastChanged(path);
			callocStruct(uicmd, UICmd);
			uicmd->cmd_type = UICmd_ModFile;
			uicmd->file = file;
			EnterCriticalSection(&csLines);
			eaPush(&ui_cmds.cmds, uicmd);
			LeaveCriticalSection(&csLines);
		}
	}
}

void workerDoSync(SyncCmd *cmd)
{
	int i;
	char buf[1024];
	char buf2[1024];
	U32 startt = timerCpuMs();
	U32 last_updatet = timerCpuMs();
	int num_files_synced=0;
	U64 bytes_synced=0;
	bool failed=false;

	assert(cmd->rootDsts);

	outputLine("Syncing %s to %s, %d files (%s)...", cmd->rootSrc, (eaSize(&cmd->rootDsts)>1)?"Multiple Projects":cmd->rootDsts[0],
		eaSize(&cmd->files), friendlyBytesBuf(cmd->totalSize, buf));

	if (!option_no_checkout)
	{
		int j;
		// Check out all destination files
		char **paths=NULL;
		int files_checkedout=0;
		int total_files = eaSize(&cmd->files)*eaSize(&cmd->rootDsts);
		outputLine("Checkout Progress %1.1f%% (%d of %d files)",
			files_checkedout*100.f / total_files, files_checkedout, total_files);
		newConsoleWindow();
		// loop through each destination
		for (j=0; j<eaSize(&cmd->rootDsts); j++)
		{
			for (i=0; i<eaSize(&cmd->files); i++)
			{
				char path[MAX_PATH];
				sprintf(path, "%s/%s", cmd->rootDsts[j], cmd->files[i]->path);
				eaPush(&paths, strdup(path));
				files_checkedout++;
				if (i == eaSize(&cmd->files)-1 ||
					eaSize(&paths) >= 90) // Doing 90 at a time to prevent 100 file warning and slowdowns
				{
					GimmeErrorValue ret = gimmeDLLDoOperations(paths, GIMME_CHECKOUT, 0);
					if (isGimmeErrorFatal(ret))
					{
						failed = true;
						outputLine("Checkout failed: %s", gimmeDLLGetErrorString(ret));
						outputLine("");
					} else {
						updateLastLine("Checkout Progress %1.1f%% (%d of %d files, %1.1fs)",
							files_checkedout*100.f / total_files, files_checkedout, total_files,
							(timerCpuMs() - startt)/1000.f);
					}
					eaClearEx(&paths, NULL);
				}
			}
		}
		eaDestroyEx(&paths, NULL);
	}

	if (failed)
	{
		outputLine("Some checkouts failed");
	}

	startt = timerCpuMs();
	outputLine("Sync Progress %1.1f%% (%d of %d files, %s of %s)",
		cmd->totalSize?(bytes_synced*100.f / cmd->totalSize):(num_files_synced*100.f/eaSize(&cmd->files)),
		num_files_synced, eaSize(&cmd->files),
		friendlyBytesBuf(bytes_synced, buf), friendlyBytesBuf(cmd->totalSize, buf2));

	for (i=0; i<eaSize(&cmd->files); i++)
	{
		syncExecute(cmd, cmd->files[i]);
		num_files_synced++;
		if (cmd->files[i]->CoreSize != (U32)-1)
			bytes_synced+=cmd->files[i]->CoreSize;
		if (i == eaSize(&cmd->files)-1 ||
			timerCpuMs() - last_updatet > 250)
		{
			updateLastLine("Sync Progress %1.1f%% (%d of %d files, %s of %s, %1.1fs)",
				cmd->totalSize?(bytes_synced*100.f / cmd->totalSize):(num_files_synced*100.f/eaSize(&cmd->files)),
				num_files_synced, eaSize(&cmd->files),
				friendlyBytesBuf(bytes_synced, buf), friendlyBytesBuf(cmd->totalSize, buf2),
				(timerCpuMs() - startt)/1000.f);
			last_updatet = timerCpuMs();
		}
	}
	destroySyncCmd(cmd);
}


//int main(int argc, char **argv)
int __stdcall WinMain( __in HINSTANCE hInstance, __in_opt HINSTANCE hPrevInstance, __in_opt LPSTR lpCmdLine, __in int nShowCmd)
{
	U32 startt;
	extern bool file_scan_dirs_skip_dot_paths;
	EXCEPTION_HANDLER_BEGIN;

	// same as setCavemanMode() except no disabling of Gimme
	gbCavemanMode = true;
	gbSurpressStartupMessages = true;
	fileAllPathsAbsolute(true);
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	fileDisableAutoDataDir();

	file_scan_dirs_skip_dot_paths = false;

	winSetHInstance(hInstance);

	DO_AUTO_RUNS;

	{
		char *args[1000];
		char **argv = args;
		int argc;
		args[0] = getExecutableName();
		argc = 1 + tokenize_line_quoted_safe(lpCmdLine,&args[1],ARRAY_SIZE(args)-1,0);
		cmdParseCommandLine(argc, argv);
	}

	if (0)
		newConsoleWindow();

	regSetAppName("CoreSync");
	option_no_checkout = regGetAppInt("NoCheckout", 0);
	option_only_orphans = regGetAppInt("OnlyOrphans", 0);
	option_no_deletes = regGetAppInt("NoDeletes", 0);
	option_all_matches = regGetAppInt("AllMatches", 0);
	option_selected_project_index = regGetAppInt("SelectedProject", 0);

	//InitCommonControls();

	startUIThread();

	Sleep(500);

	coreSyncLoadDefs();

	outputLine("Scanning for projects...");
	startt = timerCpuMs();
	scanForProjects();
	updateLastLine("Scanning for projects... done (%1.1fs).", (timerCpuMs() - startt)/1000.f);

	while (true)
	{
		WorkerCmd *cmd = NULL;
		// handle commands
		Sleep(100)
		EnterCriticalSection(&csLines);
		if (eaSize(&worker_cmds.cmds))
		{
			cmd = eaRemove(&worker_cmds.cmds, 0);
			if (cmd->cmd_type == WorkerCmd_Scan)
			{
				worker_cmds.queued_scan--;
			}
		}
		LeaveCriticalSection(&csLines);
		if (cmd)
		{
			switch (cmd->cmd_type)
			{
			case WorkerCmd_Scan:
				doScan(cmd->scan_def);
				break;
			case WorkerCmd_Diff:
				workerDoDiff(&cmd->diff_cmd);
				break;
			case WorkerCmd_Sync:
				workerDoSync(&cmd->sync_cmd);
				break;
			}
			free(cmd);
		}
	}

	EXCEPTION_HANDLER_END;
	return 0;
}