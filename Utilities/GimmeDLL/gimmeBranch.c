#include "gimmeBranch.h"
#include "gimmeUtil.h"
#include "RegistryReader.h"
#include "textparser.h"
#include "earray.h"
#include "StashTable.h"
#include "gimme.h"
#include "utils.h"

#include "file.h"


typedef struct GimmeBranch {
	char *name;
	char *warning;
	int warnOnLinkBreak; // Warn when breaking a link to branch-1
	int warnOnLinkBroken; // Warn when link to branch+1 already broken
} GimmeBranch;

typedef struct GimmeBranchConfig {
	int minBranchNum;
	int maxBranchNum;
	int patchnotes_required;
	GimmeBranch **eaBranches;
	StashTable htFreezeFiles;
	StashTable htFreezeDirs;
} GimmeBranchConfig;

ParseTable parse_branch[] = {
	{ "Name",				TOK_STRING(GimmeBranch,name, 0)	},
	{ "CheckinWarning",		TOK_STRING(GimmeBranch,warning, 0)	},
	{ "WarnOnLinkBreak",	TOK_INT(GimmeBranch,warnOnLinkBreak, 0)	},
	{ "WarnOnLinkBroken",	TOK_INT(GimmeBranch,warnOnLinkBroken, 0)	},
	{ "EndBranch",			TOK_END,		0						},
	{ NULL, 0, 0 }
};

ParseTable parse_branch_config[] = {
	{ "MinBranchNum",		TOK_INT(GimmeBranchConfig,minBranchNum, 0)	},
	{ "MaxBranchNum",		TOK_INT(GimmeBranchConfig,maxBranchNum, 0)	},
	{ "DefaultBranchNum",	TOK_IGNORE	},
	{ "PatchNotesRequired",	TOK_INT(GimmeBranchConfig,patchnotes_required, 0)	},
	{ "Branch",				TOK_STRUCT(GimmeBranchConfig,eaBranches,parse_branch) },
	{ NULL, 0, 0 }
};

AUTO_RUN;
void gimmeRegisterParseTables(void)
{
	ParserSetTableInfo(parse_branch_config, sizeof(GimmeBranchConfig), "parse_branch_config", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_branch, sizeof(GimmeBranch), "parse_branch", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

GimmeBranchConfig *branch_config = NULL;

void gimmeLoadBranchConfig(void)
{
	static GimmeBranchConfig *defaultConfig=NULL;
	if (defaultConfig==NULL) {
		char buf[MAX_PATH];
		defaultConfig = calloc(sizeof(GimmeBranchConfig), 1);
		gimmeCacheLock();
		gimmeGetCachedFilename("n:/revisions/gimmebranchcfg.txt", SAFESTR(buf));
		ParserLoadFiles(NULL, buf, NULL, 0, parse_branch_config, defaultConfig);
		gimmeCacheUnlock();
	}
	if (branch_config==NULL) {
		branch_config = defaultConfig;
	}
}

StashTable htBranchConfigs=0;

// load the frozen files
void loadFreezeFiles(const char * tree_dir);


void gimmeSetBranchConfigRootFromLocal(const char *rootdir)
{
	findGimmeDir(rootdir);
}

void gimmeSetBranchConfigRoot(const char *lockdir)
{
	char config_file[CRYPTIC_MAX_PATH];
	int ret;

	if (!htBranchConfigs) {
		htBranchConfigs = stashTableCreateWithStringKeys(8, StashDeepCopyKeys_NeverRelease);
	}

	{
		GimmeBranchConfig *gbc = stashFindPointerReturnPointer(htBranchConfigs, lockdir);
		if (gbc) {
			branch_config = gbc;
			return;
		}
	}
	
	sprintf_s(SAFESTR(config_file), "%s/gimmebranchcfg.txt", lockdir);
	branch_config = calloc(sizeof(GimmeBranchConfig),1);
	gimmeCacheLock();
	gimmeGetCachedFilename(config_file, SAFESTR(config_file));
	if (!fileExists(config_file)) {
		ret = 0;
	} else {
		ret = ParserLoadFiles(NULL, config_file, NULL, 0, parse_branch_config, branch_config);
	}
	gimmeCacheUnlock();
	if (!ret) {
		free(branch_config);
		branch_config = NULL; // tell it to load the default
		gimmeLoadBranchConfig();
	}
	loadFreezeFiles(lockdir);
	stashAddPointer(htBranchConfigs, lockdir, branch_config, false);
}

static int g_branch_override=-1;
int gimmeSetBranchNumberOverride(int branch)
{
	if (branch >= gimmeGetMinBranchNumber()) {
		g_branch_override = branch;
		return 0;
	}
	return 1;
}

static StashTable htBranches = 0;
int gimmeGetBranchNumber(const char *localpath)
{
	int branch;
	if (g_branch_override!=-1) {
		return g_branch_override;
	}
	if (htBranches == 0) {
		htBranches = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	}
	if (!stashFindInt(htBranches, getDirString(localpath), &branch)) {
		RegReader reader;
		char key[CRYPTIC_MAX_PATH];
		reader = createRegReader();
		sprintf_s(SAFESTR(key), "HKEY_LOCAL_MACHINE\\SOFTWARE\\RaGEZONE\\Gimme\\%s", getDirString(localpath));
		initRegReader(reader, key);
		if (!rrReadInt(reader, "BranchNumber", &branch)) {
			branch = 0; // branch_config->defaultBranchNum; // Default branches changing under people doesn't work
		}
		destroyRegReader(reader);
		stashAddInt(htBranches, getDirString(localpath), branch+1, true);
	} else {
		// pulled out of ht
		branch = branch - 1;
	}
	return branch;
}

void gimmeSetBranchNumber(const char *localpath, int number)
{
	RegReader reader;
	char key[CRYPTIC_MAX_PATH];
	int i;

	if (htBranches == 0) {
		htBranches = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	}
	reader = createRegReader();
	sprintf_s(SAFESTR(key), "HKEY_LOCAL_MACHINE\\SOFTWARE\\RaGEZONE\\Gimme\\%s", getDirString(localpath));
	initRegReader(reader, key);
	rrWriteInt(reader, "BranchNumber", number);
	destroyRegReader(reader);
	reader = createRegReader();
	sprintf_s(SAFESTR(key), "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Gimme\\%s", getDirString(localpath));
	initRegReader(reader, key);
	rrWriteInt(reader, "BranchNumber", number);
	destroyRegReader(reader);

	stashAddInt(htBranches, getDirString(localpath), number+1, true);
	// Reset the branch number in gimme_dirs as well!
	for (i=0; i<eaSize(&eaGimmeDirs); i++) {
		eaGimmeDirs[i]->active_branch = gimmeGetBranchNumber(eaGimmeDirs[i]->local_dir);
	}
	printf("Active branch number for %s set to %d (%s)\n", localpath, gimmeGetBranchNumber(localpath), gimmeGetBranchName(gimmeGetBranchNumber(localpath)));
}

const char *gimmeGetBranchName(int branch)
{
	gimmeLoadBranchConfig();
	if (branch >= eaSize(&branch_config->eaBranches)) {
		gimmeLog(LOG_FATAL, "You are running in a branch higher than the number of branches defined!");
		return "ERROR";
	}
	if (branch < 0)
		return "NotValidBranch";
	return branch_config->eaBranches[branch]->name;
}

int gimmeQueryBranchNumber(const char *localdir)
{
	// Hack to not load config file to get active branch number
	if (!eaGimmeDirs) {
		// Config not loaded, do it the hacky way (won't work if querying branch number of a file, not a dir!)
		return gimmeGetBranchNumber(localdir);
	} else {
		// Config loaded, do it the robust way
		GimmeDir *gimme_dir = findGimmeDir(localdir);
		if (!gimme_dir)
			return 0;
		return gimmeGetBranchNumber(gimme_dir->local_dir);
	}
}

AUTO_STRUCT AST_ENDTOK("\n");
typedef struct GimmeCoreBranchMapping {
	int from; AST( STRUCTPARAM )
	int to; AST( STRUCTPARAM )
} GimmeCoreBranchMapping;

#include "gimmeBranch_c_ast.c"

static ParseTable parse_core_mappings[] = {
	{ "CoreBranchMapping",	TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	0,	sizeof(GimmeCoreBranchMapping),		parse_GimmeCoreBranchMapping},
	{ "", 0, 0 }
};

AUTO_RUN;
void InitParseCoreMappings(void)
{
	ParserSetTableInfo(parse_core_mappings,sizeof(void *),"CoreMappings",NULL,__FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

int gimmeQueryCoreBranchNumForDir(const char *localdir)
{
	GimmeCoreBranchMapping **mappings=NULL;
	GimmeDir *gimme_dir = findGimmeDir(localdir);
	int current_branch;
	int i;
	int ret = GIMME_BRANCH_UNKNOWN;
	char filename[CRYPTIC_MAX_PATH];
	if (!gimme_dir)
		return GIMME_BRANCH_UNKNOWN;
	current_branch = gimmeQueryBranchNumber(localdir);
	sprintf(filename, "%s/gimmeCoreBranchMapping.txt", gimme_dir->lock_dir);
	gimmeCacheLock();
	gimmeGetCachedFilename(filename, SAFESTR(filename));
	ParserLoadFiles(NULL, filename, NULL, PARSER_OPTIONALFLAG, parse_core_mappings, &mappings);
	gimmeCacheUnlock();
	for (i=0; i<eaSize(&mappings); i++) {
		if (mappings[i]->from == current_branch)
			ret = mappings[i]->to;
	}
	StructDeInitVoid(parse_core_mappings, &mappings);
	eaDestroy(&mappings);
	return ret;
}

const char *gimmeQueryBranchName(const char *localdir)
{
	GimmeDir *gimme_dir = findGimmeDir(localdir);
	if (!gimme_dir)
		return gimmeGetErrorString(GIMME_ERROR_NODIR);
	return gimmeGetBranchName(gimmeGetBranchNumber(gimme_dir->local_dir));
}


const char *gimmeGetCheckinWarning(int branch)
{
	gimmeLoadBranchConfig();
	if (branch >= eaSize(&branch_config->eaBranches)) {
		gimmeLog(LOG_FATAL, "You are running in a branch higher than the number of branches defined!");
		return "ERROR";
	}
	if (branch < 0)
		return "NotValidBranch";
	return branch_config->eaBranches[branch]->warning;
}


static int warn_override;
int gimmeGetWarnOnLinkBreak(int branch)
{
	if (warn_override)
		return 0;
	gimmeLoadBranchConfig();
	if (branch >= eaSize(&branch_config->eaBranches)) {
		gimmeLog(LOG_FATAL, "You are running in a branch higher than the number of branches defined!");
		return 0;
	}
	if (branch < 0)
		return 0;
	return branch_config->eaBranches[branch]->warnOnLinkBreak;
}

int gimmeGetWarnOnLinkBroken(int branch)
{
	if (warn_override)
		return 0;
	gimmeLoadBranchConfig();
	if (branch >= eaSize(&branch_config->eaBranches)) {
		gimmeLog(LOG_FATAL, "You are running in a branch higher than the number of branches defined!");
		return 0;
	}
	if (branch < 0)
		return 0;
	return branch_config->eaBranches[branch]->warnOnLinkBroken;
}

void gimmeDisableWarnOverride(int val) {
	warn_override = val;
}

const char *gimmeGetBranchPrevName(int branch)
{
	gimmeLoadBranchConfig();
	if (branch-1 >= eaSize(&branch_config->eaBranches) ||
		branch-1 < 0)
	{
		return "BRANCH NOT DEFINED";
	}
	return branch_config->eaBranches[branch-1]->name;
}

const char *gimmeGetBranchLaterName(int branch)
{
	gimmeLoadBranchConfig();
	if (branch+1 >= eaSize(&branch_config->eaBranches))
	{
		return "Future Branch";
	}
	return branch_config->eaBranches[branch+1]->name;
}

int gimmeIsNodeLinkedPrev(GimmeNode *node, int branch)
{
	if (node->branch!=branch) {
		return 1;
	}
	return 0;
}

int gimmeIsNodeLinkBroken(GimmeNode *node, int branch)
{
	GimmeNode *walk;
	// Look for a node at branch+1, if none are found, this file is linked to the next one
	for (walk=node; walk; walk=walk->next) {
		if (walk->branch > branch) {
			// Not linked anymore
			return walk->branch;
		}
	}
	// Look backwards too
	for (walk=node; walk; walk=walk->prev) {
		if (walk->branch > branch) {
			// Not linked anymore
			return walk->branch;
		}
	}
	return 0;
}

int gimmeGetMaxBranchNumber()
{
	gimmeLoadBranchConfig();
	return branch_config->maxBranchNum;
}

int gimmeGetMinBranchNumber()
{
	gimmeLoadBranchConfig();
	return branch_config->minBranchNum;
}

int gimmeArePatchNotesRequired()
{
	gimmeLoadBranchConfig();
	return branch_config->patchnotes_required;
}


typedef struct FrozenFileBranch_t
{
	int branch;
	char ** files;
	char ** dirs;
} FrozenFileBranch;

typedef struct FrozenFileData_t
{
	FrozenFileBranch ** frozen_branches;
} FrozenFileData;


static ParseTable frozenBranches[] =
{
	{"",			TOK_STRUCTPARAM | TOK_INT(FrozenFileBranch, branch, 0) },
	{"{",			TOK_START, 0 },
	{"File",		TOK_STRINGARRAY(FrozenFileBranch, files) },
	{"Directory",	TOK_STRINGARRAY(FrozenFileBranch, dirs) },
	{"}",			TOK_END, 0 },
	{"",0,0}
};

static ParseTable frozenFileData[] =
{
	{"Branch",	TOK_STRUCT(FrozenFileData, frozen_branches, frozenBranches) },
	{"",0,0}
};

AUTO_RUN;
void initFreezeFilesTPI(void)
{
	ParserSetTableInfo(frozenBranches, sizeof(FrozenFileBranch), "frozenBranches", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(frozenFileData, sizeof(FrozenFileData), "frozenFileData", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

void loadFreezeFiles(const char * tree_dir)
{
	FrozenFileData ffd = {0};
	int i, j;
	char freeze_file[256];

	sprintf_s(SAFESTR(freeze_file), "%s/FrozenFiles.txt", tree_dir );

	gimmeCacheLock();
	gimmeGetCachedFilename(freeze_file, SAFESTR(freeze_file));
	ParserLoadFiles( 0, freeze_file, 0, 0, frozenFileData, &ffd);
	gimmeCacheUnlock();

	branch_config->htFreezeFiles = stashTableCreateWithStringKeys( 10, StashDeepCopyKeys_NeverRelease );
	branch_config->htFreezeDirs = stashTableCreateWithStringKeys( 10, StashDeepCopyKeys_NeverRelease );

	for ( i = 0; i < eaSize(&ffd.frozen_branches); ++i )
	{
		for ( j = 0; j < eaSize(&ffd.frozen_branches[i]->files); ++j )
		{
			stashAddInt( branch_config->htFreezeFiles, ffd.frozen_branches[i]->files[j], ffd.frozen_branches[i]->branch, false);
		}
		for ( j = 0; j < eaSize(&ffd.frozen_branches[i]->dirs); ++j )
		{
			stashAddInt( branch_config->htFreezeDirs, ffd.frozen_branches[i]->dirs[j], ffd.frozen_branches[i]->branch, false);
		}
	}

	StructDeInitVoid( frozenFileData, &ffd );
}

int getFreezeBranch(GimmeDir *gimme_dir, const char * relpath)
{
	int index = -1;

	if ( stashFindInt( branch_config->htFreezeFiles, relpath, &index ) )
	{
		return index;
	}
	else
	{
		// figure out what directory the file is in and look for that directory
		static char buff[256];
		char * c = 0;
		// make a copy of the directory and make the first slash from the end into a null
		strcpy_s( SAFESTR(buff), relpath);
		forwardSlashes( buff );

		// it is possible that this is a subdirectory, so remove trailing directories until
		// you find the directory or reach the root directory
		do
		{
			c = strrchr( buff, '/' );
			if ( !c )
				break;
			*c = 0;
			if ( stashFindInt( branch_config->htFreezeDirs, buff, &index ) )
			{
				return index;
			}
		} while ( c );
	}
	return -1;
}
