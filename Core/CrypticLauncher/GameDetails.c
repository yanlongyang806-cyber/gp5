#include "GameDetails.h"
#include "Environment.h"
#include "Organization.h"

// UtilitiesLib
#include "utils.h"
#include "AppLocale.h"

// these are used for error messages, etc
#define US_CHAMPIONS_REG_URL				"http://" ORGANIZATION_DOMAIN "/"
#define US_STO_REG_URL						"http://" ORGANIZATION_DOMAIN "/"
#define US_NW_REG_URL						"http://" ORGANIZATION_DOMAIN "/"


static struct {
	const char *name; // this is the short product name
	const char *code; // this is the two/three letter product name
	const char *displayName; // registry keys are actually stored using this key
	const char *launcherURL;
	const char *mainURL;
	const char *qaLauncherURL;
	const char *devLauncherURL;
	const char *pwrdLauncherURL;
	const char *pwtLauncherURL;
	const char *liveShard;
	const char *ptsShard1;
	const char *ptsShard2;
} GameTable[] = {
	// Do not change the order of these
	{
		"CrypticLauncher",
		"CL", 
		"Cryptic Launcher", 
		ENV_US_CHAMPIONS_LAUNCHER_URL,
		US_CHAMPIONS_REG_URL,
		ENV_QA_CHAMPIONS_LAUNCHER_URL,
		ENV_DEV_CHAMPIONS_LAUNCHER_URL,
		ENV_PWRD_LAUNCHER_URL,
		ENV_PWT_LAUNCHER_URL,
		NULL,
		NULL,
		NULL,
	},		// Do not move this entry.  Index 0 is used as the default data.
	{
		"FightClub", 
		"FC", 
		"Champions Online", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"Live",
		NULL,
		"PublicTest",
	},
	{
		"StarTrek", 
		"ST", 
		"Star Trek Online",
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"Holodeck",
		"Tribble",
		"RedShirt",
	},
	{
		"Creatures", 
		"CN", 
		"Creatures of the Night", 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"Nocturne",
		NULL,
		NULL,
	},
	{
		"Night", 
		"NW", 
		"Never RaGE in Winter",
		ENV_US_NW_LAUNCHER_URL,
		US_NW_REG_URL,
		ENV_QA_NW_LAUNCHER_URL,
		ENV_DEV_NW_LAUNCHER_URL,
		NULL,
		NULL,
		"Neverwinter",
		"NeverwinterPreview",
		"Owlbear",
	},
	{
		"PrimalAge", 
		"PA", 
		"Primal Age", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	},
	{
		"Bronze", 
		"BA", 
		"Bronze Age", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"Hades",
		NULL,
		NULL,
	},
	{
		"Local", 
		"??", 
		"Local", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	}
};

bool gdIDIsValid(U32 gameID)
{
	if (gameID >= ARRAY_SIZE(GameTable) || gameID < 0)
		return false;
	else
		return true;
}

const char *gdGetName(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	return GameTable[gameID].name;
}

const char *gdGetLocalName(void)
{
	// returns the last product name in the table, which should always be the "local" product/shard
	return GameTable[ARRAY_SIZE(GameTable)-1].name;
}

const char *gdGetCode(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	return GameTable[gameID].code;
}

const char *gdGetDisplayName(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	return GameTable[gameID].displayName;
}

const char *gdGetLauncherURL(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].launcherURL)
		return GameTable[gameID].launcherURL;
	else
		return GameTable[DEFAULT_GAME_ID].launcherURL;
}

const char *gdGetURL(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].mainURL)
		return GameTable[gameID].mainURL;
	else
		return GameTable[DEFAULT_GAME_ID].mainURL;
}

const char *gdGetQALauncherURL(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].qaLauncherURL)
		return GameTable[gameID].qaLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].qaLauncherURL;
}

const char *gdGetDevLauncherURL(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].devLauncherURL)
		return GameTable[gameID].devLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].devLauncherURL;
}

const char *gdGetPWRDLauncherURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].pwrdLauncherURL)
		return GameTable[gameID].pwrdLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].pwrdLauncherURL;
}

const char *gdGetPWTLauncherURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].pwtLauncherURL)
		return GameTable[gameID].pwtLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].pwtLauncherURL;
}

const char *gdGetLiveShard(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].liveShard)
		return GameTable[gameID].liveShard;
	else
		return GameTable[DEFAULT_GAME_ID].liveShard;
}

const char *gdGetPtsShard1(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].ptsShard1)
		return GameTable[gameID].ptsShard1;
	else
		return GameTable[DEFAULT_GAME_ID].ptsShard1;
}

const char *gdGetPtsShard2(U32 gameID)
{
	if (!gdIDIsValid(gameID))
		return NULL;

	if (GameTable[gameID].ptsShard2)
		return GameTable[gameID].ptsShard2;
	else
		return GameTable[DEFAULT_GAME_ID].ptsShard2;
}

U32 gdGetIDByName(const char *name)
{
	U32 i;
	for (i = 0; i < ARRAY_SIZE(GameTable); i++)
	{
		if (stricmp(GameTable[i].name, name)==0)
			return i;
	}

	return DEFAULT_GAME_ID;
}

U32 gdGetIDByCode(const char *code)
{
	U32 i;
	for (i = 0; i < ARRAY_SIZE(GameTable); i++)
	{
		if (stricmp(GameTable[i].code, code)==0)
			return i;
	}

	return DEFAULT_GAME_ID;
}

U32 gdGetIDByExecutable(const char *executablename)
{
	U32 i;

	// Check if either the name or display name are in the executable path.
	for (i = 0; i < ARRAY_SIZE(GameTable); i++)
	{
		if (strstri(executablename, GameTable[i].name))
			return i;
		if (strstri(executablename, GameTable[i].displayName))
			return i;
	}

	return DEFAULT_GAME_ID;
}

bool gdIsLocalProductName(const char *productName)
{
	return stricmp(productName, gdGetLocalName()) == 0;
}
