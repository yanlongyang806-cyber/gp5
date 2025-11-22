#ifndef REVISIONFINDER_H_
#define REVISIONFINDER_H_

// Used for authentication
#define REVISION_FINDER_INTERNAL_NAME "RevisionFinder"

#define REVISION_FINDER_VERSION "(dev)" // Change to X.X once branched

#define REVISION_FINDER_BUILDERS_URL "http://builders.cryptic.loc:8091/viewxpath?xpath=CBMonitor[1].Builders&textparser=1"
#define REVISION_FINDER_BASE_URL "http://builders.cryptic.loc:8091"
#define REVISION_FINDER_TEXT_OPTION "&textparser=1"

#define REVISION_FINDER_BASE_DIR_DEFAULT "server/RevisionFinder"

#define REVISION_FINDER_CONFIG_FILE "server/RevisionFinder/rfConfig.txt"
#define REVISION_FINDER_BRANCHES_FILE "server/RevisionFinder/rfBranchFile.txt"

#define MAX_SERVERNAME 128

#define RF_MERGEDTO_UPDATE_INTERVAL 3600

#define RF_PARSE_CT_BUF_SIZE 10000
#define RF_PARSE_LINK_BUF_SIZE 128
#define RF_AUTHSTRING_SIZE 128
#define RF_SYSTEM_COMMAND_SIZE 512
#define RF_MAX_PATCHNAME 128

#define RF_DO_EVERYTHING 0
#define RF_DONT_COLLECT_DATA 1
#define RF_DONT_RESPOND_REQS 2

//The length of the "http://code/svn" portion of SVN locations
#define RF_SVN_PREFIX_LENGTH 15
//The length of the "http://" at the start of locations
#define RF_HTTP_PREFIX_LENGTH 7
typedef struct UrlArgumentList	UrlArgumentList;

typedef struct SingleBuildInfo SingleBuildInfo;

typedef void (*ResponseHandler)(void* userdata, const char* xmldoc, int len);
typedef U32 (*u32VoidStarFunc)(void* userdata);

//Configuration struct to load from a file,
//so that RevisionFinder can be configured externally without recompile
AUTO_STRUCT;
typedef struct RevisionFinderConfig
{
	char *pBaseDir;
	char *pBuilderPageBaseURL;
	char *pBuilderPageURL;
	char **ppControllerTrackerURLs;
	char **ppCritSysCatNames;
	char **ppCritSysURLs;
	char *pAuthentication;
	char **ppBuilderTypeOrder;
} RevisionFinderConfig;

AUTO_STRUCT;
typedef struct BranchInfo
{
	char *pBranchName;
	U32 *pRevisionNumbers;
} BranchInfo;

//Struct to contain a list of branch names/paths to search
//This is built by the periodic data collection process in RevisionFinder.c
AUTO_STRUCT;
typedef struct BranchNamesList
{
	BranchInfo **ppBranchInfos;
} BranchNamesList;

//Reference to a buildInfo that's been parsed to text
AUTO_STRUCT;
typedef struct BuildRef
{
	const char* pMachineName;
	const char* pFilename;
} BuildRef;

//Information on a patch deployment destination
AUTO_STRUCT;
typedef struct Deployment
{
	char* pShardName;
	char* pShardType;
	char* pStartTime;
} Deployment;

//Information on all the places a patch has been deployed
AUTO_STRUCT;
typedef struct PatchDeployInfo
{
	char* pPatchName;
	Deployment** ppDeployments; //Lists destinations
} PatchDeployInfo;

//A pairing of Build information with deployment information for the resulting patch
AUTO_STRUCT;
typedef struct BuildDeployInfo
{
	int iRevisionNumber;
	const char *pMachineName;
	char *pSvnBranchName; AST(UNOWNED)
	char *pBuildStartTime;
	SingleBuildInfo *pBuildInfo;
	PatchDeployInfo *pDeployInfo;
} BuildDeployInfo;

AUTO_STRUCT;
typedef struct rfBuildType
{
	char *pBuildTypeName; AST(UNOWNED)
	int iExists;
	BuildDeployInfo *pBuildInfo; AST(UNOWNED)
} rfBuildType;

//A response for a search on Revision Finder,
//contains a collection of BuildDeployInfos
//for all the builds a revision has gone into,
//and all the places each of those builds was deployed
AUTO_STRUCT;
typedef struct rfSearchResponse
{
	char *pPageTitle; //Not the WEBPAGE title but rather a partial heading for the search results
	BuildDeployInfo **ppBuildInfos;
	rfBuildType **ppBuildTypes;
	rfBuildType **ppSortedBuildTypes;
	Deployment **ppShardTypes; AST(UNOWNED)
	U32 *pSearchedRevisions; //A list of searched revision numbers
} rfSearchResponse;

//Struct that gets saved to associate a revision number with one or more build information files
AUTO_STRUCT;
typedef struct RevisionInfo
{
	int iRevisionNumber;
	const char *pBranchName;
	BuildRef **ppBuilds; //A list of BuildRefs, which so far just contain filenames of buildInfo text files
	const char **ppMergedTo;
} RevisionInfo;

//Struct to partially mimic CriticalSystem_Status since its header file is so whiny about getting included externally
AUTO_STRUCT;
typedef struct CriticalSystem_Status_Partial
{
	char *pName_Internal;
	char *pVersion;
	char *pMytype;
} CriticalSystem_Status_Partial;

int RevisionFinderInit(void);
void RevisionFinderShutdown(void);
void RevisionFinderEachHeartbeat(void);

#endif