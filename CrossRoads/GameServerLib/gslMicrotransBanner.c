/***************************************************************************



***************************************************************************/

#include "MicrotransBanner.h"
#include "AutoGen/MicrotransBanner_h_ast.h"
#include "Entity.h"
#include "Player.h"
#include "GameStringFormat.h"
#include "StringCache.h"
#include "shardClock.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_STARTUP(AS_MicrotransBanner) ;
void gslMicrotransBanner_Load(void)
{
	microtransBanner_LoadMicrotransBanner();
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE; 
void gslMicrotransBanner_GetBanners(Entity *pEntity, const char* pchBannerSetName)
{
    U32 uCurTime = ShardClock_SecondsSince2000();	

	// Get the right set
	MicrotransBannerSet *pBannerSet = microtransBanner_MicrotransBannerSetFromName(pchBannerSetName);

	// Loop through and find the right set.
	// Pass it to the client
	
	if (pBannerSet)
	{
		int i;
		// Loop backward so we get the latest one.
		for (i=eaSize(&pBannerSet->ppBannerBlocks)-1;i>=0;i--)
		{
			if (pBannerSet->ppBannerBlocks[i]->uStartingTime < uCurTime)
			{
				ClientCmd_gclMicrotransBannerReceiveInfo(pEntity,pBannerSet->ppBannerBlocks[i]);
				return;
			}
		}
	}
}
