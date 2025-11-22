#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

#include "sysutil.h"
#include "file.h"
#include "estring.h"
#include "earray.h"
#include "cmdparse.h"
#include "errornet.h"
#include "logging.h"

#include "harvest.h"
#include "ui.h"
#include "textparser.h"
#include "AppRegCache.h"
#include "UtilitiesLib.h"
#include "windefinclude.h"

PCHAR* CommandLineToArgvA(PCHAR CmdLine, int* _argc);

int gDeferredMode = 0;
AUTO_CMD_INT(gDeferredMode, deferred) ACMD_CMDLINE;

bool gbForceAutoClose = true; // R Enabled
AUTO_CMD_INT(gbForceAutoClose, ForceAutoClose) ACMD_CMDLINE;
bool gbForceStayUp = false;
AUTO_CMD_INT(gbForceStayUp, ForceStayUp) ACMD_CMDLINE;

bool gbDontSendDumps = false;
AUTO_CMD_INT(gbDontSendDumps, DontSendDumps) ACMD_CMDLINE;


// Skips all user input and automatically sends all data to Error Tracker with no dialogs rendered
bool gbSkipUserInput = true; // R Enabled
AUTO_CMD_INT(gbSkipUserInput, SkipUserInput) ACMD_CMDLINE;

extern void ForkPrintfsToFile(char *pFileName);


int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, WCHAR* pWideCmdLine, int nShowCmd)
{
	bool bHarvestSuccess = false;
	int argc = 0;
	char *lpCmdLine;
	char **argv;

	lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);	
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER_LPCMDLINE
	
	argv = CommandLineToArgvA(lpCmdLine, &argc);

	regSetAppName("CrypticError");

	DO_AUTO_RUNS

	if (getIsTransgaming())
	{
		gbSkipUserInput = true;
		ForkPrintfsToFile("C:\\Night\\tools\\bin\\crypticerror_stdout.txt");
	}

	setCavemanMode();

	setAssertMode(
		  ASSERTMODE_MINIDUMP
		| ASSERTMODE_SENDCALLSTACK
		| ASSERTMODE_TEMPORARYDUMPS
		| ASSERTMODE_NODEBUGBUTTONS
		| ASSERTMODE_ISEXTERNALAPP);

	errorTrackerEnableErrorThreading(false);

	// As long as the dumps ultimately arrive, the machine can do other things first.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	cmdParseCommandLine(argc, argv);

	harvestStartup();

	if(harvestDisplayInfo() || (gDeferredMode && !harvestCheckDeferred()))
	{
		// Do nothing ... either there is nothing to send, 
		// or we couldn't get the deferred system-wide lock.
	}
	else
	{
		if (!gbSkipUserInput)
			uiStart();

		if(gDeferredMode)
			bHarvestSuccess = harvestPerformDeferred();
		else
		{
			if(harvestCheckManualUserDump())
				bHarvestSuccess = harvestPerformManualUserDump();
			else
				bHarvestSuccess = harvestPerform();
		}
		
		if (!gbSkipUserInput)
			uiWorkComplete(bHarvestSuccess);
		harvestWorkComplete(bHarvestSuccess);
	}

	logWaitForQueueToEmpty();

	EXCEPTION_HANDLER_END

	return 0;
}

#pragma warning( disable : 6386 )

PCHAR*
CommandLineToArgvA(
				   PCHAR CmdLine,
				   int* _argc
				   )
{
	PCHAR* argv;
	PCHAR  _argv;
	ULONG   len;
	ULONG   argc;
	CHAR   a;
	ULONG   i, j;

	BOOLEAN  in_QM;
	BOOLEAN  in_TEXT;
	BOOLEAN  in_SPACE;

	len = (ULONG)strlen(CmdLine);
	i = ((len+2)/2)*sizeof(PVOID) + sizeof(PVOID);

	argv = (PCHAR*)GlobalAlloc(GMEM_FIXED,
		i + (len+2)*sizeof(CHAR));

	_argv = (PCHAR)(((PUCHAR)argv)+i);

	argc = 0;
	argv[argc] = _argv;
	in_QM = FALSE;
	in_TEXT = FALSE;
	in_SPACE = TRUE;
	i = 0;
	j = 0;

	while( a = CmdLine[i] ) {
		if(in_QM) {
			if(a == '\"') {
				in_QM = FALSE;
			} else {
				_argv[j] = a;
				j++;
			}
		} else {
			switch(a) {
				case '\"':
					in_QM = TRUE;
					in_TEXT = TRUE;
					if(in_SPACE) {
						argv[argc] = _argv+j;
						argc++;
					}
					in_SPACE = FALSE;
					break;
				case ' ':
				case '\t':
				case '\n':
				case '\r':
					if(in_TEXT) {
						_argv[j] = '\0';
						j++;
					}
					in_TEXT = FALSE;
					in_SPACE = TRUE;
					break;
				default:
					in_TEXT = TRUE;
					if(in_SPACE) {
						argv[argc] = _argv+j;
						argc++;
					}
					_argv[j] = a;
					j++;
					in_SPACE = FALSE;
					break;
			}
		}
		i++;
	}
	_argv[j] = '\0';
	argv[argc] = NULL;

	(*_argc) = argc;
	return argv;
}

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
