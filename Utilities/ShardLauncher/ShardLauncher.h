#pragma once
#include "GlobalTypes.h"
#include "ShardLauncher_pub.h"

typedef struct ShardLauncherConfigOptionLibrary ShardLauncherConfigOptionLibrary;


#define STARTING_ID_INTERVAL 100000000

AUTO_STRUCT;
typedef struct ShardLauncherProduct
{
	char *pProductName; AST(KEY, STRUCTPARAM)
	char *pShortProductName; AST(STRUCTPARAM)
	char *pProductDescription;
} ShardLauncherProduct;


	
static __forceinline bool OnlyOneShard(ShardLauncherRun *pRun)
{
	return !!(pRun->onlyShardToLaunch[0]);
}



	
void SaveRunToDisk(ShardLauncherRun *pRun, U32 iCurTime);

void GetFilenameForRun(char **ppOutFileName, char *pRunName);
void GetArchivedFilenameForRun(char **ppOutFileName, ShardLauncherRun *pRun);

ShardLauncherRun *LoadRunFromName(char *pRunName);


//returns NULL if the option is not set or is "0"
char *GetNonZeroOptionByName(ShardLauncherClusterShard *pShard, char *pOptionName);

//returns the version of this option that you should use, taking into account cluster-specific overriding
ShardLauncherConfigOptionChoice *FindHighestPriorityChoice(ShardLauncherRun *pRun, ShardLauncherClusterShard *pShard, char *pShortOptionName);



void LOG(char *pFmt, ...);
void LOG_FAIL(char *pFmt, ...);
void LOG_WARNING(char *pFmt, ...);


#define TEMPLATE_DIR_NAME ".\\ShardLauncher\\Templates"
#define OPTION_LIBRARY_FILE_NAME ".\\ShardLauncher\\ConfigOptions.txt"
#define PRODUCTS_FILE_NAME ".\\ShardLauncher\\Products.txt"
#define RECENT_RUNS_FOLDER "c:/ShardLauncher/RecentRuns"
#define ARCHIVE_RUNS_FOLDER "c:/ShardLauncher/ArchivedRuns"

extern ShardLauncherRun *gpRun;

typedef enum ShardLauncherWindowType
{
	WINDOWTYPE_NONE,
	WINDOWTYPE_STARTINGSCREEN,
	WINDOWTYPE_MAINSCREEN,
	WINDOWTYPE_CHOOSEVERSION,
	WINDOWTYPE_CHOOSENAME,
	WINDOWTYPE_OPTIONS,
	WINDOWTYPE_CONTROLLERCOMMANDS,
	WINDOWTYPE_OVERRIDEEXES,
	WINDOWTYPE_WARNING,
	WINDOWTYPE_CLUSTERSETUP,
	WINDOWTYPE_WATCHTHELAUNCH,
} ShardLauncherWindowType;

typedef enum enumShardLauncherRunType
{
	RUNTYPE_PATCHANDLAUNCH,
	RUNTYPE_LAUNCH,
	RUNTYPE_PATCH,
	RUNTYPE_SAVECHANGES,
} enumShardLauncherRunType;

extern enumShardLauncherRunType gRunType;


//option library lists > this number mean server specific lists. So list 1007 would be
//the server specific list for server type 7, etc
#define SERVER_TYPE_SPECIFIC_OPTIONS_MAGIC_NUMBER 1000

//option library lists > this number mean cluster specific lists... so list 2008 would be the
//8th cluster-specific list accessed during this run (they are created and pushed into
//gpOptionLibrary->ppClusterLevelLists as needed)
#define CLUSTERLEVEL_OPTIONS_MAGIC_NUMBER 2000

//returns "[Controller] FOO"
char *GetServerTypeSpecificOptionName(const char *pName, GlobalType eType);

//returns "{ShardNameOrType} FOO"
char *GetShardOrShardTypeSpecificOptionName(const char *pName, const char *pShardNameOrType);

//the name of the list for the cluster-specific options has this prefix
#define CLUSTER_SPECIFIC_OPTIONS_PREFIX "Cluster-specific settings for "

ShardLauncherConfigOptionLibrary *LoadAndFixupOptionLibrary(char *pBuffer, char **ppErrorString);

//shard types have some hard-wired options that are always set to a certain value
typedef struct BuiltInOptionForShardType
{
	ClusterShardType eShardType;
	char *pOptionName;
	char *pOptionValue;
} BuiltInOptionForShardType;


extern BuiltInOptionForShardType **gppBuiltInOptionsForShardType;



//fail/succeed patching counts
extern int giPatchSucceededCount;

extern StashTable sMachineNamesFromSentryServer;



//you can set up a script file which forces ShardLauncher to run automatically with no input. It requires
//already having a run set up. Put AutoRun.txt in c:\ShardLauncher, and put one of these in it:

extern ShardLauncherAutoRun *gpAutoRun;

typedef enum StatusReporting_SelfReportedState StatusReporting_SelfReportedState;
extern StatusReporting_SelfReportedState gStatusReportingState;