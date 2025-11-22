#include "queue.h"
#include "SimpleWindowManager.h"
#include "earray.h"
#include "utils.h"
#include "cmdparse.h"
#include "wininclude.h"
#include "Windowsx.h"
#include "Commctrl.h"
#include "logging.h"
#include "UTF8.h"

#include "gimmectrl.h"
#include "systemtray.h"
#include "resource.h"
#include "db.h"
#include "AutoGen/queue_c_ast.h"
#include "AutoGen/db_h_ast.h"

AUTO_STRUCT;
typedef struct QueuedCommand
{
	char *cmd; AST(ESTRING)
	time_t created; AST(INT)
	time_t start; AST(INT)
	bool running;
	bool returned;
	QueryableProcessHandle *proc; NO_AST
	U32 rv;
	qcmdCB cb; NO_AST
	void *cb_userdata; NO_AST
} QueuedCommand;

static QueuedCommand **g_cmdqueue = NULL;
bool g_queue_paused = true;
static g_run_pre_first = false;

static void resetMainUI(void)
{
	SimpleWindow *main_window = SimpleWindowManager_FindWindow(0, 0);
	assertmsg(main_window, "Can't find main window to update UI");
	SendMessage(main_window->hWnd, WM_APP_REUI, 0, 0);
}

// For the recursive call.
static void pumpQueue(void);

static void removeQueueHead(void)
{
	QueuedCommand *cmd = eaHead(&g_cmdqueue);
	assertmsg(cmd, "Can't pop empty queue");
	StructDestroy(parse_QueuedCommand, cmd);
	eaRemove(&g_cmdqueue, 0);

	// Re-pause when the queue empties
	if(eaSize(&g_cmdqueue)==0)
	{
		g_queue_paused = true;
		resetMainUI();
	}

	pumpQueue();
}

//Must be called before the command in question is added to the queue, as it will be deemed redundant!
//Returns true if the passed-in command should be added to the queue, false otherwise.
bool qcmdRemoveRedundant(char *cmdString)
{
	char *context = NULL;	
	char *cmdCopy = strdup(cmdString);
	char *command = strtok_s(cmdCopy, " ", &context);
	bool ret = true;


	if (stricmp(command, "switch_project") == 0 || stricmp(command, "switch_branch") == 0)
	{
		char *gamePath = strtok_s(NULL, " ", &context);
		char *gameStart = strdup(gamePath);
		char *game;
		int i;

		//Trim leading '"C:/' and trailing '"' to get just the game name.
		game = strchr(gameStart, '/') + 1;
		game[strlen(game)-1] = '\0';

		for (i = eaSize(&g_cmdqueue) - 1; i >= 0; i--)
		{
			QueuedCommand *qcmd = g_cmdqueue[i];
			if (qcmd->returned || qcmd->running)
				continue; //If it's already started going, it's too late - it's simpler to just let it finish.
			if (strstri(qcmd->cmd, command) && strstri(qcmd->cmd, gamePath)) //We already have a command of this type in the queue, remove it
			{
				filelog_printf("GimmeCtrl.log", "Removing redundant command \"%s\".", qcmd->cmd);
				StructDestroy(parse_QueuedCommand, qcmd);
				eaRemove(&g_cmdqueue, i);
			}
			else if (stricmp(command, "switch_project") == 0 && strstri(qcmd->cmd, "install") && strstri(qcmd->cmd, game)) //We're trying to install a different project, just install this one instead.
			{
				char *newProject = strtok_s(NULL, " ", &context);
				filelog_printf("GimmeCtrl.log", "Changing install project of command \"%s\" to %s.", qcmd->cmd, newProject);
				estrPrintf(&qcmd->cmd, "install %s %s", game, newProject);
				ret = false;
			}
		}

		resetMainUI();
		free(gameStart);
	}
	
	free(cmdCopy);
	return ret;
}

static void pumpQueue(void)
{
	QueuedCommand *cmd = eaHead(&g_cmdqueue);
	if(!cmd)
		return; // Queue is empty

	if(cmd->returned)
	{
		if(cmd->proc)
		{
			int rv;
			if(QueryableProcessComplete(&cmd->proc, &rv))
			{
				char msg[256];

				if(cmd->cb)
				{
					rv = cmd->cb(rv, cmd->cb_userdata);
					if(cmd->proc)
						return; // The CB setup a new subproc
				}


				if(rv == 0)
				{
					sprintf(msg, "Command \"%s\" finished", cmd->cmd);
					systemTrayNotfy(NULL, "Command complete", msg, NIIF_INFO);
					filelog_printf("GimmeCtrl.log", "Command \"%s\" finished successfully.", cmd->cmd);
				}
				else
				{
					sprintf(msg, "Command \"%s\" failed (%d)", cmd->cmd, rv);
					systemTrayNotfy(NULL, "Command failed", msg, NIIF_ERROR);
					filelog_printf("GimmeCtrl.log", "Command \"%s\" failed with error code %d.", cmd->cmd, rv);
					populateDB();
				}
				removeQueueHead();
				return;
			}
		}
		else
		{
			// Instant return
			if(cmd->rv != 0)
			{
				char msg[256];
				sprintf(msg, "Command \"%s\" failed (%d)", cmd->cmd, cmd->rv);
				systemTrayNotfy(NULL, "Command failed", msg, NIIF_ERROR);
				filelog_printf("GimmeCtrl.log", "Command \"%s\" failed with error code %d.", cmd->cmd, cmd->rv);
				populateDB();
			} 
			else
			{
				filelog_printf("GimmeCtrl.log", "Command \"%s\" finished successfully.", cmd->cmd);
			}

			removeQueueHead();
			return;
		}

	}
	else if(!cmd->running && !g_queue_paused)
	{
		cmd->start = time(NULL);
		cmd->running = true;
		globCmdParse(cmd->cmd);
		
	}
}

void qcmdRun(FORMAT_STR const char *cmd, ...)
{
	QueuedCommand *qcmd = StructCreate(parse_QueuedCommand);
	char buf[1024];

	// Enqueue the command
	estrGetVarArgs(&qcmd->cmd, cmd);
	qcmd->created = time(NULL);

	filelog_printf("GimmeCtrl.log", "Queuing command \"%s\".", qcmd->cmd);

	if(qcmdRemoveRedundant(qcmd->cmd))
		eaPush(&g_cmdqueue, qcmd);

	// Run the DB update pre-command
	sprintf(buf, "pre_%s", qcmd->cmd);
	globCmdParse(buf);

	// Update the UI
	resetMainUI();

	// Pump the queue in case this is the first thing
	pumpQueue();
}

AUTO_COMMAND ACMD_NAME(command) ACMD_CMDLINE;
void qcmdCmdlineRun(const char *cmd)
{
	QueuedCommand *qcmd = StructCreate(parse_QueuedCommand);
	estrCopy2(&qcmd->cmd, cmd);
	qcmd->created = time(NULL);
	eaPush(&g_cmdqueue, qcmd);
}

void qcmdReturn(QueryableProcessHandle *proc)
{
	QueuedCommand *cmd = eaHead(&g_cmdqueue);
	assertmsg(cmd, "Command returning but nothing in the queue");
	cmd->proc = proc;
	cmd->returned = true;
}

void qcmdReturnFast(U32 code)
{
	QueuedCommand *cmd = eaHead(&g_cmdqueue);
	assertmsg(cmd, "Command returning but nothing in the queue");
	cmd->proc = NULL;
	cmd->rv = code;
	cmd->returned = true;
}

void qcmdCallback(qcmdCB cb, void *userdata)
{
	QueuedCommand *cmd = eaHead(&g_cmdqueue);
	assertmsg(cmd, "Command returning but nothing in the queue");
	assertmsg(cmd->proc, "You can only use a callback with an async command");
	cmd->cb = cb;
	cmd->cb_userdata = userdata;
}

bool queueRunning(void)
{
	return eaSize(&g_cmdqueue);
}

void queueAbort(void)
{
	QueuedCommand *cmd = eaHead(&g_cmdqueue);
	if(cmd && cmd->running && cmd->returned && cmd->proc)
	{
		filelog_printf("GimmeCtrl.log", "Killing command \"%s\".", cmd->cmd);
		KillQueryableProcess(&cmd->proc);
	}
	filelog_printf("GimmeCtrl.log", "Aborting queue.");
	eaDestroyStruct(&g_cmdqueue, parse_QueuedCommand);
	g_queue_paused = true;
	populateDB();

	resetMainUI();
}

void queueDelete(int qIndex)
{
	QueuedCommand *cmd = g_cmdqueue[qIndex];
	if(cmd && cmd->running && cmd->returned && cmd->proc)
	{
		filelog_printf("GimmeCtrl.log", "Killing command \"%s\".", cmd->cmd);
		KillQueryableProcess(&cmd->proc);
	}

	if (strstri(cmd->cmd, "install") || strstri(cmd->cmd, "make_fix_branch"))
	{
		char *cmdCopy = strdup(cmd->cmd);
		char *context = NULL;
		char *command;
		char *game;
		Project *proj;
		int i;

		command = strtok_s(cmdCopy, " ", &context);
		game = strtok_s(NULL, " ", &context);
		
		for (i = eaSize(&g_cmdqueue) - 1; i > qIndex; i--)
		{
			QueuedCommand *qcmd = g_cmdqueue[i];
			if (strstri(qcmd->cmd, game) && (strcmpi(command, "make_fix_branch") || strstri(qcmd->cmd, "Fix")))
			{
				queueDelete(i);
			}
		}

		proj = getProject(game);
		if (!strcmpi(command, "install") && proj->local)
		{
			StructDestroy(parse_InstalledProject, proj->local);
			proj->local = NULL;
		}
		else if (!strcmpi(command, "make_fix_branch") && proj->fix)
		{
			StructDestroy(parse_InstalledProject, proj->fix);
			proj->fix = NULL;
		}
		free(cmdCopy);
	}
	else if (strstri(cmd->cmd, "switch_branch") && !strstri(cmd->cmd, "fix"))
	{
		char *cmdCopy = strdup(cmd->cmd);
		char *context = NULL;
		char *command;
		char *game;
		Project *proj;
		int i;

		command = strtok_s(cmdCopy, " ", &context);
		game = strtok_s(NULL, " ", &context); 
		game = strchr(game, '/') + 1; //Remove leading '"C:/'
		game[strlen(game)-1] = '\0'; //Removing trailing '"'
		proj = getProject(game);
		if (proj->local->branch->num == 0) //If we're going to still be on branch 0, we can't make a fix branch.
		{
			for (i = eaSize(&g_cmdqueue) - 1; i > qIndex; i--)
			{
				QueuedCommand *qcmd = g_cmdqueue[i];
				if (strstri(qcmd->cmd, game) && (strcmpi(command, "make_fix_branch") || strstri(qcmd->cmd, "Fix")))
				{
					queueDelete(i);
				}
			}
		}
	}

	filelog_printf("GimmeCtrl.log", "Removing command \"%s\".", cmd->cmd);
	StructDestroy(parse_QueuedCommand, cmd);
	eaRemove(&g_cmdqueue, qIndex);

	if (!g_queue_paused && qIndex == 0)
	{
		if (eaSize(&g_cmdqueue) > 0)
			pumpQueue();
		else
		{
			g_queue_paused = true;
			populateDB();
		}
	}

	resetMainUI();
}

BOOL gimmectrlQueueDialogCB(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	HICON hIcon;
	char buf[1024];

	switch(iMsg)
	{
	case WM_INITDIALOG:
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

		if(eaSize(&g_cmdqueue))
		{
			// If there were commands given on the command-line, don't auto-hide the queue dialog and start it automatically
			//g_queue_paused = false; // Unpause is done in the main UI, in the onload timer
			g_run_pre_first = true;
		}
		else
			pWindow->bDontAutoShow = true;
		SetTimer(hDlg, 1, 1000, NULL);
		return TRUE;

	case WM_TIMER:
		{
			HWND h = GetDlgItem(hDlg, IDC_QUEUE);
			QueuedCommand *head;
			char *pBuf = NULL;
			int sel;

			if(g_run_pre_first && !g_queue_paused)
			{
				
				g_run_pre_first = false;

				FOR_EACH_IN_EARRAY_FORWARDS(g_cmdqueue, QueuedCommand, cmd)
					// Run the DB update pre-command
					sprintf(buf, "pre_%s", cmd->cmd);
					globCmdParse(buf);
				FOR_EACH_END

				// Update the UI
				resetMainUI();
			}

			pumpQueue();
			sel = ListBox_GetCurSel(h);
			if(sel != -1)
				ListBox_GetText_UTF8(h, sel, &pBuf);
			ListBox_ResetContent(h);
			FOR_EACH_IN_EARRAY_FORWARDS(g_cmdqueue, QueuedCommand, cmd)
				ListBox_AddString_UTF8(h, cmd->cmd);
			FOR_EACH_END
			if(sel != -1)
				ListBox_SelectString_UTF8(h, (sel?sel-1:sel), pBuf);

			h = GetDlgItem(hDlg, IDC_STATUS);
			head = eaHead(&g_cmdqueue);
			if(head)
			{
				
				if(head->start)
				{
					U32 delta = time(NULL) - head->start;
					sprintf(buf, "Running - %u:%02u", delta/60, delta%60);
					Static_SetText_UTF8(h, buf);
				}
				else if(g_queue_paused)
					Static_SetText_UTF8(h, "Paused");
				else
					Static_SetText_UTF8(h, "Not running");
			}
			else if(g_queue_paused)
				Static_SetText_UTF8(h, "Paused");
			else
				Static_SetText_UTF8(h, "");
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			ShowWindow(hDlg, SW_HIDE);
			break;

		case IDC_ABORT:
			queueAbort();
			break;
		case IDC_DELETE:
			{
				HWND h = GetDlgItem(hDlg, IDC_QUEUE);
				UINT i = ListBox_GetCurSel(h);
				if (i != LB_ERR)
					queueDelete(i);
				break;
			}
		}
		break;
	//This is a hack to keep track of whether anything in the queue is selected - I can't figure out what event gets sent when the user clicks.
	case WM_CTLCOLORLISTBOX: 
		{ 
			HWND hDel = GetDlgItem(hDlg, IDC_DELETE);
			HWND hQueue = GetDlgItem(hDlg, IDC_QUEUE);
			UINT i = ListBox_GetCurSel(hQueue);
			if (i == LB_ERR)
				Button_Enable(hDel, false);
			else
				Button_Enable(hDel, true);
			return TRUE;
		}
	}
	return FALSE;
}

#include "AutoGen/queue_c_ast.c"