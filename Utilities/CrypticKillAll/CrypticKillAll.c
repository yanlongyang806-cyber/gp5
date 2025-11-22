#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "file.h"
#include "process_util.h"
#include "earray.h"
#include "cmdparse.h"
#include "osdependent.h"
#include "utils.h"

//void KillAllEx(const char * module, bool bSkipSelf, U32 *pPIDsToIgnore, bool bDoFDNameFixup, char *pRestrictToThisDir);

char **ppExeNamesToKill = NULL;

AUTO_COMMAND ACMD_COMMANDLINE ACMD_NAME(Kill);
void KillThisExe(char *pExeName)
{
	eaPush(&ppExeNamesToKill, strdup(pExeName));
}

U32 *pPIDsToIgnore = 0;

AUTO_COMMAND ACMD_COMMANDLINE;
void PIDToIgnore(U32 PID)
{
	ea32Push(&pPIDsToIgnore, PID);
}

char *pRestrictToThisDir = NULL;
AUTO_CMD_ESTRING(pRestrictToThisDir, RestrictToDir) ACMD_COMMANDLINE;



int wmain(int argc, WCHAR** argv_wide)
{
	int pointerSize;
	int i;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	
	//if we're on a 64 bit machine running a 32-bit executable and the controller is going
	//to be launching 64 bit apps, then try to run launcherx64 instead
	pointerSize = sizeof(void*);
	if (pointerSize != 8 && IsUsingX64())
	{
		char *p64BitCommandLine = NULL;
		estrPrintf(&p64BitCommandLine, "cryptickillallX64.exe %s", GetCommandLineWithoutExecutable());
		
		if (system(p64BitCommandLine))
		{
			exit(0);
		}
	}


	utilitiesLibStartup_Lightweight();


	cmdParseCommandLine(argc, argv);

	if (!eaSize(&ppExeNamesToKill))
	{
		printf("Nothing to kill. Syntax is CrypticKillAll [[-Kill ExeNameToKill]] [[-PIDToIgnore n]] [-RestrictToDir dirName]\n");
		while(1);
		return;
	}

	for (i=0; i < eaSize(&ppExeNamesToKill); i++)
	{
		KillAllEx(ppExeNamesToKill[i], true, pPIDsToIgnore, true, true, pRestrictToThisDir);
	}




	EXCEPTION_HANDLER_END

}
