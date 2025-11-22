#include "cmdXBox.h"
#include "xbox\XSession.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void xBoxSessionCreate(U32 iTeamId)
{
#if _XBOX
	xSession_CreateSession(iTeamId);
#endif
}