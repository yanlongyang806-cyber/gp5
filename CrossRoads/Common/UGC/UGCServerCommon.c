#include "UGCCommon.h"

#include "cmdparse.h"

#if defined(GAMESERVER)

#include "GlobalStateMachine.h"
#include "EntityIterator.h"
#include "file.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#endif // defined(GAMESERVER)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//__CATEGORY UGC settings
// If zero, then author initiated UGC publishing is disabled. Otherwise, enabled.
static bool s_bUGCPublishEnabled = true;
AUTO_CMD_INT(s_bUGCPublishEnabled, UGCPublishEnabled) ACMD_AUTO_SETTING(Ugc, UGCDATAMANAGER, GAMESERVER) ACMD_CALLBACK(ugcPublishEnabledCallback);

bool ugcIsPublishEnabled( void )
{
	return s_bUGCPublishEnabled;
}

void ugcPublishEnabledCallback(CMDARGS)
{
#if defined(GAMESERVER)
	if(GSM_IsRunning() && isProductionEditMode())
	{
		EntityIterator *entIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		Entity *pEnt;
		while(pEnt = EntityIteratorGetNext(entIter))
			ClientCmd_gclUGCPublishEnabled(pEnt, ugcIsPublishEnabled());
		EntityIteratorRelease(entIter);
	}
#endif // defined(GAMESERVER)
}
