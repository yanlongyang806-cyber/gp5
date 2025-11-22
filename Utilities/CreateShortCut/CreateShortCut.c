#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "Estring.h"
#include "earray.h"
#include "StringCache.h"
#include "process_Util.h"
#include "net.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "timing.h"
#include "Alerts.h"
#include "StructDefines.h"
#include "alerts_h_ast.h"
#include "CrypticPorts.h"
#include "timedCallback.h"
#include "GlobalComm.h"
#include "StructNet.h"
#include "wininclude.h"
#include <shlobj.h>

char *spShortCutName = NULL;
AUTO_CMD_ESTRING(spShortCutName, ShortCutName) ACMD_COMMANDLINE;

char *pShortCutDesc = NULL;
AUTO_CMD_ESTRING(pShortCutDesc, ShortCutDesc) ACMD_COMMANDLINE;

char *pFileToRun = NULL;
AUTO_CMD_ESTRING(pFileToRun, FileToRun) ACMD_COMMANDLINE;


int main(int argc,char **argv)
{
	int i;
	bool bNeedToConfigure = false;
	CommConnectFSM *pFSM = NULL;
	int iRetryCount = 0;
	bool bSucceeded = false;

	EXCEPTION_HANDLER_BEGIN
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	if (!(spShortCutName && pShortCutDesc && pFileToRun))
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "FAILURE! You must specify -ShortCutName, -ShortCutDesc and -FileToRun");
		exit(-1);
	}


	{
		char shortCutName[CRYPTIC_MAX_PATH];
		char desktopName[CRYPTIC_MAX_PATH];
		
		SHGetSpecialFolderPath(NULL, desktopName, CSIDL_DESKTOPDIRECTORY, 0);
		sprintf(shortCutName, "%s\\%s", desktopName, spShortCutName);


		createShortcut(pFileToRun, shortCutName, 0, NULL, NULL, pShortCutDesc);
	}


	EXCEPTION_HANDLER_END

}


