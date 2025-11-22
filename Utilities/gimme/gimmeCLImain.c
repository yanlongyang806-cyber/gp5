#include "gimmeDLLWrapper.h"
#include "EString.h"
#include "sysutil.h"
#include "MemAlloc.h"
#include "timing_profiler_interface.h"
#include "utils.h"
#include <conio.h>
#include "wininclude.h"

int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	char *cmdline=NULL;
	char **argv;
	GimmeErrorValue ret;
	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	gimmeDLLDisable(false);
	autoTimerInit();
	// TODO: Ideally DO_AUTO_RUNS and other initialize would be removed, because they do a bunch of stuff we don't really need or want here, but
	// the below stuff needs them.
	estrCopy2(&cmdline, "");
	for (i=1; i<argc; i++) {
		estrConcatStatic(&cmdline, " ");
		if (strchr(argv[i], ' ') && argv[i][0]!='\"') {
			estrConcatf(&cmdline, "\"%s\"", argv[i]);
		} else {
			estrConcat(&cmdline, argv[i], (int)strlen(argv[i]));
		}
	}
	consoleUpSize(140, 9999);
	ret = gimmeDLLDoCommand(cmdline);
	estrDestroy(&cmdline);
	if (ret == GIMME_ERROR_NO_DLL) {
		printf("\nERROR!  Could not find GimmeDLL.DLL.  Please contact IT.\n");
		printf("Press any key to exit...\n");
		i = _getch();
	}
	return ret;
	EXCEPTION_HANDLER_END
}
