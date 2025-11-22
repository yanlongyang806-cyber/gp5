#include "rfMergeInfoCache.h"
#include "rfMergeInfoCache_h_ast.h"
#include "RevisionFinder.h"
#include "earray.h"

MergeInfoCacheEntry **ppMergeInfoCache = NULL;

const char* GetMergeInfoAndCache(const char *pBranchName)
{
	char cmd[RF_SYSTEM_COMMAND_SIZE];
	char *pMergeInfo = NULL;
	MergeInfoCacheEntry *pNewEntry = StructCreate(parse_MergeInfoCacheEntry);
	sprintf(cmd,"svn pg svn:mergeinfo %s", pBranchName);
	system_w_output(cmd, &pMergeInfo);

	pNewEntry->pBranchName = StructAllocString(pBranchName);
	pNewEntry->pMergeInfo = pMergeInfo;
	eaPush(&ppMergeInfoCache, pNewEntry);

	return pMergeInfo;
}

const char* GetMergeInfoCached(const char *pBranchName)
{
	int iBranch;
	for(iBranch = eaSize(&ppMergeInfoCache) - 1; iBranch >= 0; iBranch --)
	{
		if(strcmp(ppMergeInfoCache[iBranch]->pBranchName, pBranchName) == 0) return ppMergeInfoCache[iBranch]->pMergeInfo;
	}
	return GetMergeInfoAndCache(pBranchName);
}

void ClearMergeInfoCache(void)
{
	eaClearStruct(&ppMergeInfoCache, parse_MergeInfoCacheEntry);
}

void InitMergeInfoCache(void)
{
	if(ppMergeInfoCache != NULL)
	{
		eaDestroyStruct(&ppMergeInfoCache, parse_MergeInfoCacheEntry);
	}
	ppMergeInfoCache = NULL;
	eaCreate(&ppMergeInfoCache);
}

#include "AutoGen/rfMergeInfoCache_h_ast.c"