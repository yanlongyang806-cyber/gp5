#include "SimpleWindowManager.h"
#include "sysutil.h"
#include "file.h"
#include "fileWatch.h"
#include "earray.h"
#include "wininclude.h"
#include "Windowsx.h"
#include "Commctrl.h"
#include "gimmeDLLPublicInterface.h"

#include "FolderCache.h"
#include "cmdparse.h"
#include "MemoryMonitor.h"
#include "utilitiesLib.h"
#include "SharedMemory.h"
#include "crypt.h"
#include "logging.h"

#include "gimmectrl.h"
#include "db.h"
#include "queue.h"
#include "systemtray.h"
#include "resource.h"
#include "GlobalTypes.h"
#include "utils.h"
#include "UTF8.h"

//#include "AutoGen/gimmectrl2_c_ast.h"

static U32 g_taskbar_create_message = 0;

static Project *install_project;
static char *pInstallChoice = NULL;

extern bool g_queue_paused;

static INT_PTR CALLBACK gimmectrlInstallDialogCB(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	switch (iMsg) 
	{ 
	case WM_INITDIALOG:
		estrClear(&pInstallChoice);
		h = GetDlgItem(hDlg, IDC_PROJECT);
		FOR_EACH_IN_EARRAY_FORWARDS(install_project->projects, char, proj)
			ComboBox_AddString_UTF8(h, proj);
		FOR_EACH_END
		ComboBox_SetCurSel(h, 0);
		break;

	case WM_COMMAND: 
		switch (LOWORD(wParam)) 
		{ 
		case IDOK: 
			GetDlgItemText_UTF8(hDlg, IDC_PROJECT, &pInstallChoice);
			// Fall through. 

		case IDCANCEL: 
			EndDialog(hDlg, LOWORD(wParam)); 
			return TRUE; 
		} 
	} 
	return FALSE; 

}

static const char *activeProject(HWND hDlg)
{
	static char *pBuf = NULL;
	HWND list = GetDlgItem(hDlg, IDC_PROJECTS);
	int sel = ListBox_GetCurSel(list);
	ListBox_GetText_UTF8(list, sel, &pBuf);
	return pBuf;

}

void logStartingProjects()
{
	FOR_EACH_IN_EARRAY(g_projects, Project, proj)
		if(proj->local)
		{
			filelog_printf("GimmeCtrl.log", "Local INSTALL found for game %s and project %s on branch %d.", proj->name, proj->local->project,
				proj->local->branch->num);
			if (proj->fix)
			{
				filelog_printf("GimmeCtrl.log", "Local FIX found for game %s and project %s on branch %d.", proj->name, proj->fix->project,
					proj->fix->branch->num);
			}
			if (proj->fixcore)
			{
				filelog_printf("GimmeCtrl.log", "Local FIX CORE found for game %s and project %s on branch %d.", proj->name, proj->fixcore->project,
					proj->fixcore->branch->num);
			}
		}
		else
		{
			filelog_printf("GimmeCtrl.log", "No local copy found for game %s.", proj->name);
		}
	FOR_EACH_END
}

static BOOL gimmectrlDialogCB(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	HICON hIcon;
	char buf2[1024];


	switch(iMsg)
	{
	case WM_INITDIALOG:
		// Grab the message used for a taskbar reset
		ATOMIC_INIT_BEGIN;
		{
			g_taskbar_create_message = RegisterWindowMessage(L"TaskbarCreated");
		}
		ATOMIC_INIT_END;

		// Load the small and large icons
		hIcon = LoadImage(GetModuleHandle(NULL),
			MAKEINTRESOURCE(IDI_ICON),
			IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON),
			GetSystemMetrics(SM_CYSMICON),
			0);
		assertmsg(hIcon, "Can't load small icon");
		SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
		hIcon = LoadImage(GetModuleHandle(NULL),
			MAKEINTRESOURCE(IDI_ICON),
			IMAGE_ICON,
			GetSystemMetrics(SM_CXICON),
			GetSystemMetrics(SM_CYICON),
			0);
		assertmsg(hIcon, "Can't load big icon");
		SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

		// Show the system tray icon
		systemTrayAdd(hDlg);

		ListBox_AddString(GetDlgItem(hDlg, IDC_PROJECTS), L"Loading...");
		SetTimer(hDlg, 1, 10, NULL);
		return TRUE;

	case WM_TIMER:
		{
			HWND projects_list;
			populateDB();
			projects_list = GetDlgItem(hDlg, IDC_PROJECTS);
			ListBox_DeleteString(projects_list, 0);
			FOR_EACH_IN_EARRAY(g_projects, Project, proj)
				ListBox_AddString_UTF8(projects_list, proj->name);
			FOR_EACH_END
			logStartingProjects();
			if(queueRunning())
				g_queue_paused = false;
			KillTimer(hDlg, 1);
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			if(queueRunning())
			{
				int rv;
				char msg[1024];
				sprintf(msg, "There are still commands %s. Are you sure you want to quit?", (g_queue_paused?"queued":"running"));
				rv = MessageBox_UTF8(hDlg, msg, "Are you sure?", MB_YESNO|MB_ICONWARNING);
				if(rv == IDNO)
					break;
				queueAbort();
			}
			pWindow->bCloseRequested = true;
			systemTrayRemove(hDlg);
			filelog_printf("GimmeCtrl.log", "GimmeCtrl shutting down.");
			break;
		case IDC_SHOWQUEUE:
			SimpleWindowManager_AddOrActivateWindow(1, 0, IDD_QUEUE, false, gimmectrlQueueDialogCB, NULL, NULL);
			break;
		case IDC_APPLY:
			filelog_printf("GimmeCtrl.log", "Applying Queue.");
			g_queue_paused = false;
			Button_Enable((HWND)lParam, FALSE);
			break;
		case IDC_PROJECTS:
			switch(HIWORD(wParam))
			{
			case LBN_SELCHANGE:
				{
					HWND projects_list = GetDlgItem(hDlg, IDC_PROJECTS), h;
					int sel;
					Project *proj;
					char *pBuf = NULL;

					// Get the selected project
					sel = ListBox_GetCurSel(projects_list);
					ListBox_GetText_UTF8(projects_list, sel, &pBuf);
					proj = getProject(pBuf);
					estrDestroy(&pBuf);

					if(!proj)
						break;
					
					// Populate the UI

					// Populate the Project dropdown
					h = GetDlgItem(hDlg, IDC_PROJECT);
					ComboBox_ResetContent(h);
					if(proj->local)
					{
						FOR_EACH_IN_EARRAY_FORWARDS(proj->projects, char, proj_name)
							ComboBox_AddString_UTF8(h, proj_name);
						FOR_EACH_END
						ComboBox_SelectString_UTF8(h, 0, proj->local->project);
						ComboBox_Enable(h, TRUE);
					}
					else
					{
						ComboBox_Enable(h, FALSE);
					}

					// Populate the Branch dropdown
					h = GetDlgItem(hDlg, IDC_BRANCH);
					ComboBox_ResetContent(h);
					if(proj->local)
					{
						estrStackCreate(&pBuf);
						FOR_EACH_IN_EARRAY_FORWARDS(proj->branches, Branch, branch)
							estrPrintf(&pBuf, "%d - %s", branch->num, branch->name);
							ComboBox_AddString_UTF8(h, pBuf);
						FOR_EACH_END
						ComboBox_SetCurSel(h, proj->local->branch->num);
						ComboBox_Enable(h, TRUE);
						estrDestroy(&pBuf);
					}
					else
					{
						ComboBox_Enable(h, FALSE);
					}

					// Populate the Fix Project dropdown
					h = GetDlgItem(hDlg, IDC_FIX_PROJECT);
					ComboBox_ResetContent(h);
					if(proj->fix)
					{
						FOR_EACH_IN_EARRAY_FORWARDS(proj->projects, char, proj_name)
							ComboBox_AddString_UTF8(h, proj_name);
						FOR_EACH_END
						ComboBox_SelectString_UTF8(h, 0, proj->fix->project);
						ComboBox_Enable(h, TRUE);
					}
					else
					{
						ComboBox_Enable(h, FALSE);
					}

					// Populate the Fix Branch dropdown
					h = GetDlgItem(hDlg, IDC_FIX_BRANCH);
					ComboBox_ResetContent(h);
					if(proj->fix)
					{
						estrStackCreate(&pBuf);
						FOR_EACH_IN_EARRAY_FORWARDS(proj->branches, Branch, branch)
							estrPrintf(&pBuf, "%d - %s", branch->num, branch->name);
							ComboBox_AddString_UTF8(h, pBuf);
						FOR_EACH_END
						ComboBox_SetCurSel(h, proj->fix->branch->num);
						ComboBox_Enable(h, TRUE);
						estrDestroy(&pBuf);
					}
					else
					{
						ComboBox_Enable(h, FALSE);
					}

					// Populate the FixCore Project dropdown
					h = GetDlgItem(hDlg, IDC_FIXCORE_PROJECT);
					ComboBox_ResetContent(h);
					if(proj->fixcore)
					{
						FOR_EACH_IN_EARRAY_FORWARDS(proj->core->projects, char, proj_name)
							ComboBox_AddString_UTF8(h, proj_name);
						FOR_EACH_END
						ComboBox_SelectString(h, 0, proj->fixcore->project);
						ComboBox_Enable(h, TRUE);
					}
					else
					{
						ComboBox_Enable(h, FALSE);
					}

					// Populate the FixCore Branch dropdown
					h = GetDlgItem(hDlg, IDC_FIXCORE_BRANCH);
					ComboBox_ResetContent(h);
					if(proj->fixcore)
					{
						estrStackCreate(&pBuf);			
						FOR_EACH_IN_EARRAY_FORWARDS(proj->core->branches, Branch, branch)
							estrPrintf(&pBuf, "%d - %s", branch->num, branch->name);
							ComboBox_AddString_UTF8(h, pBuf);
						FOR_EACH_END
						ComboBox_SetCurSel(h, proj->fixcore->branch->num);
						ComboBox_Enable(h, TRUE);
						estrDestroy(&pBuf);
					}
					else
					{
						ComboBox_Enable(h, FALSE);
					}

					// Activate the Install button if needed
					h = GetDlgItem(hDlg, IDC_BUTTON_INSTALL);
					if(!proj->local)
					{
						Button_Enable(h, TRUE);
					}
					else
					{
						Button_Enable(h, FALSE);
					}

					// Activate the Make Fix button if needed
					h = GetDlgItem(hDlg, IDC_BUTTON_CREATEFIX);
					if(proj->core && proj->local && proj->local->branch->num && !proj->fix)
					{
						Button_Enable(h, TRUE);
					}
					else
					{
						Button_Enable(h, FALSE);
					}

					// Activate the Apply button if needed
					h = GetDlgItem(h, IDC_APPLY);
					Button_Enable(h, g_queue_paused);
				}
				break;
			}
			break;

		case IDC_PROJECT:
		case IDC_FIX_PROJECT:
		case IDC_FIXCORE_PROJECT:
			switch(HIWORD(wParam))
			{
			case CBN_SELENDOK:
				{
					char *pBuf = NULL;
					estrStackCreate(&pBuf);
					ComboBox_GetText_UTF8((HWND)lParam, &pBuf);
					strcpy(buf2, activeProject(hDlg));
					if(LOWORD(wParam) == IDC_FIX_PROJECT)
						strcat(buf2, "Fix");
					else if(LOWORD(wParam) == IDC_FIXCORE_PROJECT)
						strcat(buf2, "FixCore");
					qcmdRun("switch_project \"C:/%s\" %s", buf2, pBuf);
					estrDestroy(&pBuf);
				}
				break;
			}
			break;

		case IDC_BRANCH:
		case IDC_FIX_BRANCH:
		case IDC_FIXCORE_BRANCH:
			switch(HIWORD(wParam))
			{
			case CBN_SELENDOK:
				{
					InstalledProject *inst;
					Project *proj;
					char *msg = NULL;
					int branch_num = ComboBox_GetCurSel((HWND)lParam);
					int core_branch_num;

					strcpy(buf2, activeProject(hDlg));
					if(LOWORD(wParam) == IDC_FIX_BRANCH)
						strcat(buf2, "Fix");
					else if(LOWORD(wParam) == IDC_FIXCORE_BRANCH)
						strcat(buf2, "FixCore");

					inst = getInstalledProject(buf2);
					proj = inst->parent;
					if(LOWORD(wParam) == IDC_FIXCORE_BRANCH)
						proj = proj->core;
					if(inst->branch->num == branch_num)
						break;

					estrPrintf(&msg, "Are you sure you want to change C:/%s to branch %d (%s)", buf2, proj->branches[branch_num]->num, proj->branches[branch_num]->name);
					core_branch_num = proj->branches[branch_num]->core_branch;
					if(proj->core && core_branch_num != GIMME_BRANCH_UNKNOWN)
					{
						Branch *core_branch = proj->core->branches[core_branch_num];
						estrConcatf(&msg, " and %s to branch %d (%s)", inst->core_folder, core_branch->num, core_branch->name);
					}
					estrConcatf(&msg, "?");
					backSlashes(msg);
					if(MessageBox_UTF8(hDlg, msg, "Switch branch?", MB_YESNO|MB_ICONQUESTION)!=IDYES)
					{
						ComboBox_SetCurSel((HWND)lParam, inst->branch->num);
						estrDestroy(&msg);
						break;
					}
					estrDestroy(&msg);

					qcmdRun("switch_branch \"C:/%s\" %d", buf2, branch_num);
					if(proj->core && core_branch_num != GIMME_BRANCH_UNKNOWN)
						qcmdRun("switch_branch \"%s\" %d", inst->core_folder, core_branch_num);
				}
				break;
			}
			break;

		case IDC_BUTTON_INSTALL:
			{
				int ret;
				install_project = getProject(activeProject(hDlg));
				ret = DialogBox(NULL, MAKEINTRESOURCE(IDD_INSTALL), hDlg, gimmectrlInstallDialogCB);
				if(ret == IDOK && estrLength(&pInstallChoice))
					qcmdRun("install %s %s", activeProject(hDlg), pInstallChoice);
			}
			break;

		case IDC_BUTTON_CREATEFIX:
			qcmdRun("make_fix_branch %s", activeProject(hDlg));
			break;

		}
		break;
	case WM_APP_TRAYICON:
		if(LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_RBUTTONUP)
		{
			ShowWindow(hDlg, SW_NORMAL);
			SetForegroundWindow(hDlg);
		}
		break;
	case WM_APP_REUI:
		{
			HWND h;
			SendMessage(hDlg, WM_COMMAND, MAKELONG(IDC_PROJECTS, LBN_SELCHANGE), (LPARAM)GetDlgItem(hDlg, IDC_PROJECTS));
			h = GetDlgItem(hDlg, IDC_APPLY);
			Button_Enable(h, g_queue_paused);
		}
		break;
	default:
		if(g_taskbar_create_message && g_taskbar_create_message == iMsg)
			systemTrayAdd(hDlg);
	}
	return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, WCHAR*    pWideCmdLine, int nCmdShow)
{
	char *tokCmdLine, *argv[1000];
	int argc = 0;
	EXCEPTION_HANDLER_BEGIN
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);	
	WAIT_FOR_DEBUGGER_LPCMDLINE
	DO_AUTO_RUNS

	// Early init
	setDefaultAssertMode();
	fileAllPathsAbsolute(1);
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);

	// Parse the command line
	gbLimitCommandLineCommands = true;
	tokCmdLine = strdup(lpCmdLine);
	argv[0] = getExecutableName();
	argc = 1 + tokenize_line_quoted_safe(tokCmdLine, &argv[1], ARRAY_SIZE(argv)-1, 0);
	cmdParseCommandLine(argc, argv);

	// General init
	fileDisableAutoDataDir();
	memMonitorInit();
	utilitiesLibStartup();
	sharedMemorySetMode(SMM_DISABLED);
	SetAppGlobalType(GLOBALTYPE_NONE);
	logSetDir("C:/crypticsettings/GimmeCtrl/logs");
	filelog_printf("GimmeCtrl.log", "GimmeCtrl starting up.");

	// Display UI
	SimpleWindowManager_Init("gimmectrl", true);
	SimpleWindowManager_AddOrActivateWindow(0, 0, IDD_MAIN2, true, gimmectrlDialogCB, NULL, NULL);
	SimpleWindowManager_AddOrActivateWindow(1, 0, IDD_QUEUE, false, gimmectrlQueueDialogCB, NULL, NULL);
	SimpleWindowManager_Run(NULL, NULL);

	exit(0);
	EXCEPTION_HANDLER_END
}

//#include "AutoGen/gimmectrl_c_ast.c"