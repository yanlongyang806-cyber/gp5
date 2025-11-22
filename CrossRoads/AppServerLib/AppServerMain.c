#include "GlobalTypes.h"
#include "sysutil.h"

#include "AppServerLib.h"
#include "utils.h"
#include "winutil.h"

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER

	RegisterGenericGlobalTypes(); // We need to call these now, so the parsing works
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_NONE);

	DO_AUTO_RUNS;

	assertmsg(GetAppGlobalType() != GLOBALTYPE_NONE, "Didn't find -ContainerType in AppServer");

	consoleSetToUnicodeFont();
	setConsoleTitle(GlobalTypeToName(GetAppGlobalType()));
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), (GlobalTypeToName(GetAppGlobalType()))[0], 0x8080ff);


	// First, call the universal setup stuff
	aslPreMain(GetProjectName(), argc, argv);

	//calls app-specific app-init
	aslStartApp();


	aslMain();

	EXCEPTION_HANDLER_END
	return 0;
}
