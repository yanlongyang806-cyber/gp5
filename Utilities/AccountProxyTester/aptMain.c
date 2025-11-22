#include "aptMain.h"

#include "accountnet.h"
#include "aptMimicProxy.h"
#include "cmdparse.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "stdtypes.h"
#include "sysutil.h"
#include "utilitiesLib.h"
#include "wininclude.h"


// Instead of acting as a fake proxy, act as a fake AS and accept proxy connections
static bool gbMimicAccountServer = false;
AUTO_CMD_INT(gbMimicAccountServer, MimicAccountServer);

bool gbDone = false;

extern void setAccountServer(const char *pAccountServer);

static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{ 
	case CTRL_CLOSE_EVENT: 
	case CTRL_LOGOFF_EVENT: 
	case CTRL_SHUTDOWN_EVENT: 
	case CTRL_BREAK_EVENT: 
	case CTRL_C_EVENT:
		if (gbDone)
		{
			return FALSE;
		}
		else
		{
			gbDone = true;
			return TRUE;
		}

	default: 
		return FALSE; 
	} 
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;
	EXCEPTION_HANDLER_BEGIN;
	ARGV_WIDE_TO_ARGV;
	WAIT_FOR_DEBUGGER;
	DO_AUTO_RUNS;

	gimmeDLLDisable(1);
	FolderCacheChooseMode();
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);
	cmdParseCommandLine(argc, argv);
	utilitiesLibStartup();

	// Very very important line of code to prevent hilarity
	if (!accountServerWasSet())
		setAccountServer("localhost");

	if (gbMimicAccountServer)
	{
		// TODO: implement MimicAS mode
	}
	else
	{
		aptMimicProxyMain();
	}

	while (1)
		Sleep(1);

	EXCEPTION_HANDLER_END;

	return 0;
}