#include "Shards.h"
#include "Environment.h"
#include "Account.h"
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "GameDetails.h"
#include "registry.h"
#include "patcher.h" // for GetLastShardPatchedDescriptor()
#include "UI.h" // only for UI_DisplayStatusMsg() at present - lets keep it that way

// NewControllerTracker
#include "NewControllerTracker_Pub.h"
#include "autogen/NewControllerTracker_pub_h_ast.h"

// UtilitiesLib
#include "net.h"
#include "sock.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "EString.h"
#include "timing.h"
#include "textparser.h"
#include "structNet.h"

// Common
#include "GlobalComm.h"

// CT DEFINES
#define CONTROLLERTRACKER_ATTEMPT_TIMEOUT 1

//a little struct which is for a simple FSM of the status of attempting to connect
//to the controller tracker
typedef struct ControllerTrackerConnectionStatusStruct
{
	U32 iOverallBeginTime; //if 0, then this is our first attempt, or our first attempt since connection failed
	U32 iCurBeginTime; //every 30 seconds, we kill our current link and attempt to connect again
}
ControllerTrackerConnectionStatusStruct;

static void controllerTrackerLinkRemove(void);

static NetLink *sLinkToControllerTracker = NULL;
static char **sControllerTrackerIPs = NULL;
static ControllerTrackerConnectionStatusStruct sConnectionStatus;
static NetComm *sControllerTrackerComm = NULL;


// the IP of the last controller tracker connected to
char *gControllerTrackerLastIP = NULL;
// list of shards discovered
static ShardInfo_Basic_List *sShardList = NULL;
// True if we are running in generic-launcher-for-all-games mode
static bool sbAllMode;


static char *sOverrideControllerTracker = NULL;
AUTO_COMMAND ACMD_NAME(ct) ACMD_CMDLINE;
void cmd_SetOverrideCT(const char *ct)
{
	SAFE_FREE(sOverrideControllerTracker);
	sOverrideControllerTracker = strdup(ct);
}

static bool sNoLocalShard = false;
AUTO_CMD_INT(sNoLocalShard, no_localshard);

static char *sFakeShards = NULL;
AUTO_COMMAND ACMD_NAME(FakeShards) ACMD_CMDLINE;
void cmd_FakeShards(const char *path)
{
	SAFE_FREE(sFakeShards);
	sFakeShards = strdup(path);
}

// Write out the list of shards to a file.
static char *sWriteShards = NULL;
AUTO_COMMAND ACMD_NAME(WriteShards) ACMD_CMDLINE;
void cmd_WriteShards(const char *path)
{
	SAFE_FREE(sWriteShards);
	sWriteShards = strdup(path);
}

ShardInfo_Basic *gPrePatchShard = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(prepatch);
void cmd_prepatch(char *product, char *name, char *category, char *prepatch, char *cwd)
{
	char *tmp = NULL;
	int ignored;
	gPrePatchShard = StructCreate(parse_ShardInfo_Basic);
	gPrePatchShard->pProductName = StructAllocString(product);
	estrSuperUnescapeString(&tmp, name);
	gPrePatchShard->pShardName = StructAllocString(tmp);
	gPrePatchShard->pShardCategoryName = StructAllocString(category);
	estrSuperUnescapeString(&tmp, prepatch);
	gPrePatchShard->pPrePatchCommandLine = StructAllocString(tmp);

	// Set the working dir to something more sane
	// We don't mind if it fails though.
	estrSuperUnescapeString(&tmp, cwd);
	ignored = _chdir(tmp);

	estrDestroy(&tmp);
}

static void controllerTrackerOnConnectCallback(Packet *pak,int cmd, NetLink *link, void *user_data)
{
	switch (cmd)
	{
	case FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST:
		// Decode the shard list from the controller tracker
		StructDeInit(parse_ShardInfo_Basic_List, sShardList);
		ParserRecvStructSafe(parse_ShardInfo_Basic_List, pak, sShardList);

		// If we have a debugger, show the local shard
		if (IsDebuggerPresent() && !sNoLocalShard)
		{
			ShardInfo_Basic *local_shard = StructCreate(parse_ShardInfo_Basic);
			local_shard->pProductName = StructAllocString(gdGetLocalName());
			// note - we use the Local product name for the shard name and shard category name, also
			local_shard->pShardName = StructAllocString(gdGetLocalName());
			local_shard->pShardCategoryName = StructAllocString(gdGetLocalName());
			local_shard->pShardLoginServerAddress = StructAllocString("localhost");

			local_shard->pShardControllerAddress = StructAllocString("localhost");
			//local_shard->pPrePatchCommandLine = StructAllocString("-sync -project FightClubServer -name FC_9_20090713_0952");
			eaPush(&sShardList->ppShards, local_shard);
		}

		// Write out the shard list, if requested.
		if (sWriteShards)
		{
			ParserWriteTextFile(sWriteShards, parse_ShardInfo_Basic_List, sShardList, 0, 0);
		}

		// Allow making a fake list of shards for testing
		if (sFakeShards)
		{
			StructDeInit(parse_ShardInfo_Basic_List, sShardList);
			assert(ParserReadTextFile(sFakeShards, parse_ShardInfo_Basic_List, sShardList, 0));
		}

		LauncherResetAvailableLocalesFromAllShards(sShardList->ppShards);

		// Get a new ticket for the web server.
		if (AccountLogin())
		{
			LauncherSetState(CL_STATE_GETTINGPAGETICKET);
		}
		else
		{
			// already displaying the launcher_login page - so set the state back to the beginning, display an error, set the error timeout, and move on.
			LauncherSetState(CL_STATE_LOGINPAGELOADED);
			UI_DisplayStatusMsg(_("Unable to contact account server"), true /* bSet5SecondTimeout */);
		}
		controllerTrackerLinkRemove();
		break;
	}
}

void ShardsInit(void)
{
	sShardList = callocStruct(ShardInfo_Basic_List);
	sControllerTrackerComm = commDefault();
	sConnectionStatus.iCurBeginTime = 0;
	sConnectionStatus.iOverallBeginTime = 0;
}

static void controllerTrackerLinkRemove(void)
{
	linkRemove(&sLinkToControllerTracker);
	sLinkToControllerTracker = NULL;
}

void ShardsAlwaysTickFunc(void)
{
	commMonitor(sControllerTrackerComm);
}

void ShardsLoggedInTickFunc(void)
{
	char *pControllerTrackerStatusString = NULL;
	U32 iCurTime;
	static int ctIndex = 0;
	int i;

	if (sLinkToControllerTracker && linkConnected(sLinkToControllerTracker) && !linkDisconnected(sLinkToControllerTracker))
	{
		if (sConnectionStatus.iOverallBeginTime != 0)
		{
			Packet *pak;
			char *prodName = GetProductName();

			if (LauncherGetShowAllGamesMode())
			{
				prodName = PRODUCT_NAME_ALL;
			}

			pak = pktCreate(sLinkToControllerTracker, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST);
			pktSendString(pak, prodName);
			AccountAppendDataToPacket(pak);
			pktSend(&pak);

			sConnectionStatus.iOverallBeginTime = 0;
		}
	}
	else if (!eaSize(&sControllerTrackerIPs))
	{
		if (sOverrideControllerTracker)
			eaPush(&sControllerTrackerIPs, strdup(sOverrideControllerTracker));
		else if (gQAMode)
			eaPush(&sControllerTrackerIPs, strdup(ENV_QA_CONTROLLERTRACKER_HOST));
		else if (gDevMode)
			eaPush(&sControllerTrackerIPs, strdup(ENV_DEV_CONTROLLERTRACKER_HOST));
		else if (gPWRDMode)
			eaPush(&sControllerTrackerIPs, strdup(ENV_PWRD_CONTROLLERTRACKER_HOST));
		else if (gPWTMode)
			eaPush(&sControllerTrackerIPs, strdup(ENV_PWT_CONTROLLERTRACKER_HOST));
		else
			GetAllUniqueIPs(ENV_US_CONTROLLERTRACKER_HOST, &sControllerTrackerIPs);

		if (!eaSize(&sControllerTrackerIPs))
		{
			estrCopy2(&pControllerTrackerStatusString, _("DNS can't resolve controller tracker"));
			UI_ClearPasswordField();
		}
		else
		{
			iCurTime = timeSecondsSince2000();
			ctIndex = iCurTime % eaSize(&sControllerTrackerIPs);

			if (sConnectionStatus.iOverallBeginTime == 0)
			{
				sConnectionStatus.iCurBeginTime = sConnectionStatus.iOverallBeginTime = iCurTime;
			}

			// Timeout, move on to the next possible server
			if (iCurTime - sConnectionStatus.iCurBeginTime > CONTROLLERTRACKER_ATTEMPT_TIMEOUT)
			{
				controllerTrackerLinkRemove();
				ctIndex += 1;
				ctIndex %= eaSize(&sControllerTrackerIPs);
			}

			if (!sLinkToControllerTracker)
			{
				sLinkToControllerTracker = commConnect(sControllerTrackerComm, LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, sControllerTrackerIPs[ctIndex], NEWCONTROLLERTRACKER_GENERAL_MCP_PORT, controllerTrackerOnConnectCallback, 0, 0, 0);
				if (sLinkToControllerTracker)
				{
					linkSetUserData(sLinkToControllerTracker, NULL);
				}
				else
				{
					ctIndex += 1;
					ctIndex %= eaSize(&sControllerTrackerIPs);
				}
				sConnectionStatus.iCurBeginTime = iCurTime;
				gControllerTrackerLastIP = sControllerTrackerIPs[ctIndex];
			}

			estrCopy2(&pControllerTrackerStatusString, _("Attempting to connect to controller tracker"));
			for (i = 0; i < eaSize(&sControllerTrackerIPs); i++)
			{
				estrConcatf(&pControllerTrackerStatusString, ".");
			}
		}
	}

	if (pControllerTrackerStatusString)
	{
		UI_DisplayStatusMsg(pControllerTrackerStatusString, false /* bSet5SecondTimeout */);
		estrDestroy(&pControllerTrackerStatusString);
	}
}

void ShardsControllerTrackerClearIPs(void)
{
	eaDestroyEx(&sControllerTrackerIPs, NULL);
}

int ShardsGetCount(void)
{
	if (sShardList)
	{
		return eaSize(&sShardList->ppShards);
	}

	return 0;
}

const ShardInfo_Basic *ShardsFindLast(void)
{
	char lastShardDescriptor[512];

	if (GetLastShardPatchedDescriptor(NULL, SAFESTR(lastShardDescriptor)))
	{
		// Set the box back to the last selected shard if possible
		FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
			char shardDescriptor[512];
			LauncherFormShardDescriptor(SAFESTR(shardDescriptor), shard->pProductName, shard->pShardName);
			if (stricmp(lastShardDescriptor, shardDescriptor) == 0)
			{						
				return shard;
			}
		FOR_EACH_END
	}

	return NULL;
}

const ShardInfo_Basic *ShardsGetDefault(const char *productName)
{
	ShardInfo_Basic *retVal = NULL;

	FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
		if (stricmp(shard->pProductName, productName)==0)
		{
			if (ShardShouldDisplay(shard))
			{
				if (stricmp(shard->pShardCategoryName, SHARD_CATEGORY_LIVE)==0)
				{
					return shard;
				}
				if (!retVal)
				{
					retVal = shard;
				}
			}
		}
	FOR_EACH_END

	return retVal;
}

const ShardInfo_Basic *ShardsGetFirstDisplayed()
{
	FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
		if (ShardShouldDisplay(shard))
		{
			return shard;
		}
	FOR_EACH_END

	return NULL;
}

const ShardInfo_Basic *ShardsGetByProductName(const char *productName, const char *shardName)
{
	FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
		if (!stricmp(productName, shard->pProductName) &&
			!stricmp(shardName, shard->pShardName))
		{
			return shard;
		}
	FOR_EACH_END

	// did not find productName and shardName in list
	return NULL;
}

bool ShardsAreDown(const char *productName)
{
	const char *launcherProductShortName = gdGetName(0);
	if (stricmp(productName, launcherProductShortName) == 0)
	{
		// if there are no shards in the list, we consider the shards down
		return (ShardsGetCount() == 0);
	}
	else
	{
		bool bShardDown = true;
		FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
			if (stricmp(productName, shard->pProductName) == 0 &&
				!shard->bNotReallyThere)
			{
				bShardDown = false;
				break;
			}
		FOR_EACH_END
		return bShardDown;
	}
}

// the logic here is:
//  if the product your interested in is the ONLY product you see in the shards returned from the CT, and
//  it is a live, pts1, or pts2 shard category, 
// then show the buttons in the UI
bool ShardsUseButtons(const char *productName)
{
	bool use_buttons = true;
	U32 gameID = gdGetIDByName(productName);
	const char *liveShard = gdGetLiveShard(gameID);
	const char *ptsShard1 = gdGetPtsShard1(gameID);
	const char *ptsShard2 = gdGetPtsShard2(gameID);

	assert(eaSize(&sShardList->ppShards) > 1);
	FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
		bool matches_button = false;
		if (stricmp(shard->pProductName, productName)!=0)
		{
			use_buttons = false;
			break;
		}

		if (liveShard && stricmp(shard->pShardName, liveShard)==0)
			matches_button = true;
		else if (ptsShard1 && stricmp(shard->pShardName, ptsShard1)==0)
			matches_button = true;
		else if (ptsShard2 && stricmp(shard->pShardName, ptsShard2)==0)
			matches_button = true;
		if (!matches_button)
		{
			use_buttons = false;
			break;
		}
	FOR_EACH_END

	return use_buttons;
}

void ShardsGetButtonStates(const char *productName, bool *button1, bool *button2, bool *button3)
{
	U32 gameID = gdGetIDByName(productName);
	const char *liveShard = gdGetLiveShard(gameID);
	const char *ptsShard1 = gdGetPtsShard1(gameID);
	const char *ptsShard2 = gdGetPtsShard2(gameID);

	assert(button1 && button2 && button3);
	*button1 = false;
	*button2 = false;
	*button3 = false;
	FOR_EACH_IN_EARRAY(sShardList->ppShards, ShardInfo_Basic, shard)
		if (liveShard && stricmp(shard->pShardName, liveShard)==0)
		{
			*button1 = true;
		}
		else if (ptsShard1 && stricmp(shard->pShardName, ptsShard1)==0)
		{
			*button2 = true;
		}
		else if (ptsShard2 && stricmp(shard->pShardName, ptsShard2)==0)
		{
			*button3 = true;
		}
	FOR_EACH_END
}

bool ShardsGetMessage(char **msg)
{
	if (sShardList->pMessage && sShardList->pMessage[0])
	{
		if (sShardList->pUserMessage && sShardList->pUserMessage[0])
		{
			*msg = sShardList->pUserMessage;
		}
		else
		{
			*msg = sShardList->pMessage;
		}

		return true;
	}
	else
	{
		return false;
	}
}

static int cmpShardInfo(const ShardInfo_Basic **a, const ShardInfo_Basic **b)
{
	int i = stricmp((*a)->pProductName, (*b)->pProductName);
	if (i == 0)
		i = stricmp((*a)->pShardName, (*b)->pShardName);
	return i;
}

bool ShardsPrepareForDisplay(const char *productName, void **userdata)
{
	int i;
	bool bShowProduct = false;
	assert(userdata);

	*userdata = sShardList->ppShards;

	if (sShardList)
	{
		eaQSort(sShardList->ppShards, cmpShardInfo);
	}

	for (i = eaSize(&sShardList->ppShards) - 1; i >= 0; i--)
	{
		// this snippet of code will likely not be exercised, since we do not run any xbox shards (written: 5/2013)
		if (stricmp(sShardList->ppShards[i]->pShardCategoryName, SHARD_CATEGORY_XBOX)==0)
		{
			StructDestroy(parse_ShardInfo_Basic, sShardList->ppShards[i]);
			eaRemove(&sShardList->ppShards, i);
			continue;
		}

		if (stricmp(sShardList->ppShards[i]->pProductName, productName)!=0 &&
			!gdIsLocalProductName(sShardList->ppShards[i]->pProductName))
		{
			bShowProduct = true;
		}
	}

	return bShowProduct;
}

bool ShardShouldDisplay(const ShardInfo_Basic *shard)
{
	if ((shard->pPatchCommandLine && shard->pPatchCommandLine[0]) || gdIsLocalProductName(shard->pProductName))
	{
		// Always show all shards in show all games mode, otherwise filter by language.
		return LauncherGetShowAllGamesMode() || ShardSupportsLocale(shard, getCurrentLocale());
	}
	return false;
}

bool ShardSupportsLocale(const ShardInfo_Basic *shard, int localeId)
{
	if (shard->ppLanguages)
	{
		EARRAY_CONST_FOREACH_BEGIN(shard->ppLanguages, i, n);
			if (localeId == locGetIDByName(shard->ppLanguages[i]))
			{
				return true;
			}
		EARRAY_FOREACH_END;
		return false;
	}
	else
	{
		// Assume English if the shard has no specified locales, and default to showing local 
		// shards if they do not specify available locales.
		return (localeId == LOCALE_ID_ENGLISH) || gdIsLocalProductName(shard->pProductName);
	}
}

