#pragma once

typedef struct ShardLauncherConfigOptionLibrary ShardLauncherConfigOptionLibrary;



AUTO_ENUM;
typedef enum enumThingToDoType
{
	THINGTODOTYPE_LAUNCHSERVER,
	THINGTODOTYPE_SHAREDCOMMANDLINE,
	THINGTODOTYPE_SERVERTYPECOMMANDLINE,
	THINGTODOTYPE_FIRSTSERVEROFTYPECOMMANDLINE,
	THINGTODOTYPE_INIT_AUTOSETTING,
	THINGTODOTYPE_DONOTHING,
} enumThingToDoType;


AUTO_STRUCT;
typedef struct ShardLauncherConfigOptionThingToDo
{
	enumThingToDoType eType; AST(STRUCTPARAM) 
	GlobalType eServerType_MightNeedFixup; AST(STRUCTPARAM)
	char *pString; AST(STRUCTPARAM) 
} ShardLauncherConfigOptionThingToDo;

AUTO_STRUCT;
typedef struct ShardLauncherConfigOptionChoiceWhichRequiresWarning
{
	char *pValue; AST(STRUCTPARAM)
	char *pWarningString; AST(STRUCTPARAM)
} ShardLauncherConfigOptionChoiceWhichRequiresWarning;

AUTO_STRUCT;
typedef struct ShardLauncherConfigOptionConditionalThingsToDo
{
	bool bApplyIfNonClustered;
	ClusterShardType eOnlyApplyToClusteredShardOfType;
	ClusterShardType eOnlyApplyIfClusteredShardOfTypeExist;
	ShardLauncherConfigOptionThingToDo **ppThingsToDo; AST(NAME(Do))
} ShardLauncherConfigOptionConditionalThingsToDo;


AUTO_STRUCT;
typedef struct ShardLauncherConfigOption
{
	char *pName; AST(STRUCTPARAM)//no spaces
	char *pDescription; AST(STRUCTPARAM)
	bool bIsBool;
	char **ppChoices; AST(NAME(Choice))
	ShardLauncherConfigOptionChoiceWhichRequiresWarning **ppChoicesWhichRequireWarning; AST(NAME(ChoiceWhichRequiresWarning))
	ShardLauncherConfigOptionThingToDo **ppThingsToDoIfSet; AST(NAME(IfSet))
	ShardLauncherConfigOptionThingToDo **ppThingsToDoIfNotSet; AST(NAME(IfNotSet))

	ShardLauncherConfigOptionConditionalThingsToDo **ppConditionalThingsToDo; AST(NAME(Conditional))

	bool bSet; NO_AST
	bool bAlphaNumOnly;
	char *pDefaultChoice;

	//if someone tries to start a run with a clustered shard of this type but WITHOUT
	//this option set, generate a warning. For instance, running a cluster with a UGC shard but without the ENABLE_UGC option
	//is pretty nonsensical. Note that this requires that the option be set at the cluster level, not just on the specific shard
	ClusterShardType eIfShardOfTypeExistsThenThisShouldBeSet; 

	//if this option is set, in a clustered shard, then there should be a shard of the following type (usually goes hand
	//in hand with eShardTypeWhoseExistenceRequiresThis
	ClusterShardType eIfThisIsSetThenShardOfTypeShouldExist;

} ShardLauncherConfigOption;

AUTO_STRUCT;
typedef struct ShardLauncherConfigOptionList
{
	char *pListName; AST(STRUCTPARAM)
	GlobalType *pGlobalTypesForExtraCommandLineSetting; AST(NAME(TypeForCommandLines))
	ShardLauncherConfigOption **ppOptions; AST(NAME(Option))
} ShardLauncherConfigOptionList;


AUTO_STRUCT;
typedef struct ShardLauncherConfigOptionLibrary
{
	GlobalType *pLaunchOrderList;
	GlobalType *pServerSpecificScreenTypes;
	ShardLauncherConfigOptionList **ppLists; AST(NAME(List))
	ShardLauncherConfigOptionList *pServerTypeSpecificList;

	//created as needed, one for each normal option, with shard- and shard-type specific versions
	//of the option
	ShardLauncherConfigOptionList **ppClusterLevelLists; AST(NO_WRITE)
} ShardLauncherConfigOptionLibrary;


AUTO_STRUCT;
typedef struct ShardLauncherConfigOptionChoice
{
	//is usually a string like "LAUNCH_X64". Can also be "[CONTROLLER] FOO", which means that
	//it's setting FOO specifically for the controller
	char *pConfigOptionName; AST(STRUCTPARAM KEY)
	char *pValue; AST(STRUCTPARAM)
} ShardLauncherConfigOptionChoice;



AUTO_STRUCT;
typedef struct ShardLauncherClusterShard
{
	char *pShardName;
	char *pMachineName;
	ClusterShardType eShardType;
	char *pShardSetupFileName;
	int iStartingID; //ie, 100000000, meaning that this shard will use
		//IDs from 100000000 to 199999999
	char *pDirectory_FixedUp; AST(ESTRING NO_WRITE)
	char *pLocalBatchFileName; AST(ESTRING NO_WRITE)
	char *pRemoteBatchFileName; AST(ESTRING NO_WRITE)
	char *pLocalCommandFileName; AST(ESTRING NO_WRITE)
	char *pRemoteCommandFileName; AST(ESTRING NO_WRITE)
	char *pPatchingCommandLine; AST(ESTRING NO_WRITE)
} ShardLauncherClusterShard;


//all the information needed for a particular running of ShardLauncher
AUTO_STRUCT;
typedef struct ShardLauncherRun
{
	bool bClustered; //if true, this is a clustered run
	ShardLauncherClusterShard **ppClusterShards;
	char *pClusterName;


	//all these are identical for clustered vs non-clustered runs
	char *pRunName; AST(ESTRING)
	char *pComment; AST(ESTRING) //for template files, describes the build in detail
	char *pProductName; AST(ESTRING)
	char *pShortProductName; AST(ESTRING)
	char *pPatchServer; AST(ESTRING)
	char *pPatchVersion; AST(ESTRING)
	char *pPatchVersionComment; AST(ESTRING)
	char *pTemplateFileName; AST(ESTRING)
	char *pControllerCommandsArbitraryText; AST(ESTRING)

	//for clustered runs, this MUST have $SHARDNAME$ in it
	char *pDirectory; AST(ESTRING)

	//ignored entirely for clustered runs
	char *pShardSetupFile; AST(ESTRING)
	
	//identical between clustered and non-clustered runs, except that clustered
	//runs presumably have some number of "{shardname} optionname" options
	ShardLauncherConfigOptionChoice **ppChoices;

	//loaded during execution only
	ShardLauncherConfigOptionChoice **ppTemplateChoices; AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_1))
	
	//full pathnames to files that should be copied into the patch directory after patching
	//before launching
	char **ppOverrideExecutableNames;

	//calculated at shard run time for clustered runs... this is the template copy of the /data dir for all controllers
	char *pLocalDataTemplateDir; AST(ESTRING NO_TEXT_SAVE)
	//similar, but for Frankenbuild exes:
	char *pLocalFrankenBuildDir; AST(ESTRING NO_TEXT_SAVE)

	//set user option bit so we can compare to runs and exclude these fields to see if options
	//have changed
	U32 iLastRunTime; AST(USERFLAG(TOK_USEROPTIONBIT_1))
	U32 iLastModifiedTime; AST(USERFLAG(TOK_USEROPTIONBIT_1))


	ShardLauncherConfigOptionLibrary *pOptionLibrary; //will ideally come from 
		//data/server/shardLauncher_ConfigOptions.txt in the patch version. If the patching fails
		//for some reason, falls back to being loaded from the version patched into shardlauncher itself
	char *pLibraryVersion; AST(ESTRING)


	//stuff relevant for clustered runs only
	char *pLogServerAndParserMachineName;
	char *pLogServerFilterFileName;
	char *pLogServerExtraCmdLine;
	char *pLogParserExtraCmdLine;
	char *pLoggingDir; //the direcotry in which logs end up, ie, d:\logs


	//the directory into which LogServer/LogParser get patched on the logging machine, 
	char *pLogServerDir; AST(ESTRING NO_TEXT_SAVE)



	//if set, then only launch this shard during RunTheShard (should only end up set for launch-without-patching)
	char onlyShardToLaunch[256]; NO_AST
} ShardLauncherRun;


AUTO_STRUCT;
typedef struct ShardLauncherAutoRun
{
	char *pRunName;
	char *pPatchVersion;
	char *pPatchVersionComment;
} ShardLauncherAutoRun;
#define SHARDLAUNCHER_AUTORUN_FILE_NAME "c:\\ShardLauncher\\AutoRun.txt"