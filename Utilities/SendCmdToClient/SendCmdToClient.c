/*
 * Launcher - starts, tracks, and stops processes (mapservers) for dbserver
 * 
 * Supported commands:
 * 
 * 	start process
 * 	get processes

connection plan:

	launcher starts up, looks for dbserver
 */
      
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")

#include "net/net.h"
#include "timing.h"
#include "utils.h"
#include "error.h"
#include "logging.h"
#include "sock.h"
#include <stdio.h>
#include <conio.h>
#include "MemoryMonitor.h"

#include "file.h"
#include "FolderCache.h"
#include "version/AppVersion.h"
#include "SharedHeap.h"
#include "cpu_count.h"
#include "sysutil.h"
#include "version/AppRegCache.h"
#include "RegistryReader.h"
#include "winutil.h"
#include "textparser.h"
#include "serverlib.h"
#include "stashtable.h"
#include "utilitieslib.h"
#include "MemAlloc.h"
#include "fileutil.h"
#include "controllerLink.h"
#include "process_util.h"
#include "stringcache.h"
#include "GlobalTypes.h"
#include "fileutil2.h"
#include "structnet.h"
#include "cmdParse.h"
#include "Testclient_comm.h"

char clientProductName[64] = "all";
AUTO_CMD_STRING(clientProductName, product) ACMD_COMMANDLINE;

char **ppCommands = NULL;

AUTO_COMMAND ACMD_COMMANDLINE;
void command(char *pCmdStr)
{
	eaPush(&ppCommands, strdup(pCmdStr));
}

int main(int argc,char **argv)
{

	NetLink *pLinkToClient;
	int i;

	WAIT_FOR_DEBUGGER
	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();

	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	fileAllPathsAbsolute(true);

	cmdParseCommandLine(argc, argv);

	utilitiesLibStartup();
	sockStart();

	pLinkToClient = commConnectWait(commDefault(),LINK_FORCE_FLUSH,"127.00.0.1",CLIENT_SIMPLE_COMMAND_PORT,NULL,NULL,NULL,0,5.0f);

	if (!pLinkToClient)
	{
		exit(1);
	}

	for (i=0; i < eaSize(&ppCommands); i++)
	{
		Packet *pPak = pktCreate(pLinkToClient, FROM_TESTCLIENT_CMD_SENDCOMMAND);
		pktSendString(pPak, clientProductName);
		pktSendString(pPak, ppCommands[i]);
		pktSend(&pPak);
	}

	linkFlushAndClose(&pLinkToClient, "Closed at shutdown");

	exit(0);

	EXCEPTION_HANDLER_END
}
