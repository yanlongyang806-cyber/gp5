#include "LoginCommon.h"
#include "GlobalTypes.h"
#include "objPath.h"
#include "error.h"
#include "ResourceInfo.h"
#include "entCritter.h"
#include "AutoGen/LoginCommon_h_ast.h"

S32 g_iNumOfUnlockedCreateFlags = 0;

AUTO_STARTUP(UnlockedCreateFlags);
void UnlockedCreateFlagsRegDictionary(void)
{
	UnlockableCreateNames unlockflags = {0};
	S32 i;

	g_pUnlockedCreateFlags = DefineCreate();

	loadstart_printf("Loading UnlockedCreateFlags... ");

	ParserLoadFiles(NULL, "defs/config/createunlock.def", "createunlock.bin", PARSER_OPTIONALFLAG, parse_UnlockableCreateNames, &unlockflags);

	// I use i+1 for the default "uncategorized" index, which is always present
	for (i = 0; i < eaSize(&unlockflags.pchNames); i++)
	{
		DefineAddInt(g_pUnlockedCreateFlags, unlockflags.pchNames[i], 1<<i);
	}
	g_iNumOfUnlockedCreateFlags = i+1;

	StructDeInit(parse_UnlockableCreateNames, &unlockflags);

	loadend_printf(" done (%d UnlockedCreateFlags).", i);
}
