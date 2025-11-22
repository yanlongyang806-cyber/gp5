#include "memlog.h"
#include "patchmirroring.h"
#include "patchmirroring_opt.h"
#include "patchmirroring_opt_h_ast.h"
#include "patchupdate.h"
#include "timing.h"

// Debugging memlog.
MemLog updatememlog={0};

// Current update step.
UpdateStates g_update_state = UPDATE_NOTIFYME_START;

// Return true if this state should be considered verbose.
static bool patchmirroringVerboseState(UpdateStates state)
{
	switch (state)
	{
		case UPDATE_MANIFEST_CHECKIN_VIEW:
		case UPDATE_MANIFEST_CHECKIN_FILE:
		case UPDATE_MANIFEST_CHECKIN_DONE:
			return true;
	}
	return false;
}

// Switch to a new state; use patchmirroringSwitchStatef().
void patchmirroringSwitchStatef_dbg(UpdateStates new_state, FORMAT_STR const char *format, ...)
{
	char *extra = NULL;
	char *extraEscaped = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Format extra information.
	estrStackCreate(&extra);
	estrGetVarArgs(&extra, format);
	estrStackCreate(&extraEscaped);
	estrAppendEscaped(&extraEscaped, extra);
	estrDestroy(&extra);

	// Log state change.
	memlog_printf(&updatememlog, "%s: Entering %s, %s",
		StaticDefineIntRevLookup(UpdateStatesEnum, g_update_state),
		StaticDefineIntRevLookup(UpdateStatesEnum, new_state),
		extraEscaped);
	if (patchmirroringVerboseState(new_state))
		patchupdateLogVerbose("%s: Entering %s, %s",
			StaticDefineIntRevLookup(UpdateStatesEnum, g_update_state),
			StaticDefineIntRevLookup(UpdateStatesEnum, new_state),
			extraEscaped);
	else
		patchupdateLog("%s: Entering %s, %s",
			StaticDefineIntRevLookup(UpdateStatesEnum, g_update_state),
			StaticDefineIntRevLookup(UpdateStatesEnum, new_state),
			extraEscaped);
	estrDestroy(&extraEscaped);

	// Set new state.
	g_update_state = new_state;

	PERFINFO_AUTO_STOP();
}

#include "patchmirroring_opt_h_ast.c"
