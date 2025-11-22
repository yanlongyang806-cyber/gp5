#pragma once

typedef enum StatusReporting_SelfReportedState StatusReporting_SelfReportedState;
	 
AUTO_ENUM;
typedef enum OverlordShardStatus
{
	SHARDSTATUS_UNKNOWN,

	SHARDSTATUS_DISCONNECTED,

	SHARDSTATUS_SHARDLAUNCHER_RUNNING,
	SHARDSTATUS_SHARDLAUNCHER_PATCHING,
	
	SHARDSTATUS_STARTINGUP_INITIAL,
	SHARDSTATUS_STARTINGUP_PATCHING,
	SHARDSTATUS_STARTINGUP_SERVERS_STARTING,
	SHARDSTATUS_LOCKED,
	SHARDSTATUS_RUNNING,
} OverlordShardStatus;


#define SHARDSTATUS_CONNECTED(status) ( status >= SHARDSTATUS_STARTINGUP_INITIAL && status <= SHARDSTATUS_RUNNING)


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "ClusterName, Status, PatchVersion, PrimedPatchVersion, NumPlayers");
typedef struct OverlordShard
{
	//specified in the config file
	const char *pShardName; AST(STRUCTPARAM REQUIRED)
	char *pClusterName; 
	char *pMachineName; AST(REQUIRED)
	char *pDirectory; AST(REQUIRED)
	char *pProductName; AST(REQUIRED POOL_STRING)

	//dynamically calculated
	OverlordShardStatus eStatus;
	char *pPatchVersion; AST(ESTRING)
	char *pPrimedPatchVersion; AST(ESTRING)
	int iNumPlayers;
} OverlordShard;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Launch, EmergencyShutdown, RestartClusterController, PatchVersion, ShardLauncherState, ClusterControllerConnected, LogServerConnected, LogParserConnected, NumShardsConnected, NumPlayers");
typedef struct OverlordCluster //specified in the config file
{
	char *pClusterName; AST(STRUCTPARAM)
	char *pMachineName; AST(REQUIRED)
	char *pProductName; AST(REQUIRED POOL_STRING)

	//dynamically calculated
	StatusReporting_SelfReportedState eShardLauncherState;
	
	char *pPatchVersion; AST(ESTRING)

	char *pLoggingMachine;

	bool bClusterControllerConnected;
	bool bLogServerConnected;
	bool bLogParserConnected;

	int iNumShardsConnected; NO_AST
	char *pNumShardsConnected; AST(ESTRING)
	int iNumPlayers;


	AST_COMMAND("Launch", "LaunchCluster $FIELD(ClusterName) $STRING(Patch Version)")
	AST_COMMAND("EmergencyShutdown", "EmergencyShutdownCluster $FIELD(ClusterName)  $STRING(Retype cluster name to confirm)")
	AST_COMMAND("RestartClusterController", "RestartClusterController $FIELD(ClusterName)")
} OverlordCluster;


AUTO_STRUCT;
typedef struct OverlordConfig
{
	char *pPatchServer;
	OverlordShard **ppShards; AST(NAME(Shard))
	OverlordCluster **ppClusters; AST(NAME(Cluster))
} OverlordConfig;

//these enumerated types are used to generate alert keys for alerts that are the "overlord errors" that the
//web level reads
AUTO_ENUM;
typedef enum OverlordErrorType
{
	OVERLORD_ERROR__COULDNT_LOAD_CLUSTER_SHARDLAUNCHER_FILE, //unable to load c:/shardlauncher/recentruns/clustername.txt 
		//on the cluster machine
		//arg: machine name

	OVERLORD_ERROR__CORRUPT_SHARDLAUNCHER_FILE, //loaded c:/shardlauncher/recentruns/clustername.txt on the cluster machine,
		//but it was corrupt or badly formatted
		//arg: machine name, cluster name

	OVERLORD_ERROR__DUP_SHARD_NAME, //two shards with the same name. Arg: shard name
} OverlordErrorType;

void OverlordError(OverlordErrorType eErrorType, FORMAT_STR const char  *pFmt, ...);

extern OverlordConfig *gpOverlordConfig;
