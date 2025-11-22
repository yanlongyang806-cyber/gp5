#pragma once

#include "stashtable.h"

typedef struct TextParserBinaryBlock TextParserBinaryBlock;
typedef struct NetLink NetLink;
typedef struct SentryServerCommQueryProcInfo SentryServerCommQueryProcInfo;

AUTO_ENUM;
typedef enum SentryServerMachineType
{
	Machine_Unregistered = 0,
	Machine_User,
	Machine_Reserved,
	Machine_Open,
} SentryServerMachineType;

AUTO_STRUCT;
typedef struct SentryServerMachine
{
	char	*name;		AST(STRUCTPARAM)
	SentryServerMachineType	type;	AST(STRUCTPARAM)
} SentryServerMachine;

AUTO_STRUCT;
typedef struct SentryServerMachineList
{
	SentryServerMachine	**machine;
} SentryServerMachineList;

AUTO_STRUCT;
typedef struct Stat
{
	F64		value; NO_AST
	char	*key;
	U32		uid; NO_AST
	char	*str;
	int		str_size; NO_AST
	U32		tag; NO_AST
	F64		seconds[60]; NO_AST
	F64		minutes[60]; NO_AST
	U32		changed_seconds : 1; NO_AST
	U32		changed_minutes : 1; NO_AST
	U32		permanent : 1; NO_AST
} Stat;

//NOTE NOTE NOTE changing this struct will break MONITORSERVER_EXPRESSIONQUERY_RESULT which
//all controller use
AUTO_STRUCT;
typedef struct SentryClient
{
	char		name[MAX_PATH]; AST(KEY)
	U32			first_heard; 
	U32			last_heard;
	Stat		**stats; AST(NO_NETSEND)
	StashTable	stat_hashes; NO_AST 
	NetLink		*link; NO_AST
	SentryServerMachine		*machine; 
	int			connect_count; NO_AST
	U32			local_ip;
	U32			public_ip;
	U32			tag;
	U32			done_patching : 1;
	U32			read_only_local : 1; NO_AST
} SentryClient;

AUTO_STRUCT;
typedef struct SentryClientList
{ 
	SentryClient **ppClients;
} SentryClientList;




//in-code only, you can request a list of processes running on a machine, and get one of these back
AUTO_STRUCT;
typedef struct SentryProcess_FromSimpleQuery
{
	char *pProcessName;
	char *pProcessPath;
	U32 iPID;
} SentryProcess_FromSimpleQuery;

AUTO_STRUCT;
typedef struct SentryProcess_FromSimpleQuery_List
{
	char *pMachineName;
	int iQueryID;
	bool bSucceeded; //will only be true if the sentryServer doesn't know this machine name
	SentryProcess_FromSimpleQuery **ppProcesses;
} SentryProcess_FromSimpleQuery_List;

AUTO_STRUCT;
typedef struct SentryMachines_FromSimpleQuery
{
	int iQueryID;
	char **ppMachines;
} SentryMachines_FromSimpleQuery;

AUTO_STRUCT;
typedef struct FileContents_FromSimpleQuery
{
	int iQueryID;
	char *pMachineName;
	char *pFileName;
	TextParserBinaryBlock *pContents; //NULL if the file doesn't exist
} FileContents_FromSimpleQuery;