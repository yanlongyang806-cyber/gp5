#ifndef RFMERGEINFOCACHE_H_
#define RFMERGEINFOCACHE_H_

AUTO_STRUCT;
typedef struct MergeInfoCacheEntry
{
	char *pBranchName;
	char *pMergeInfo; AST(ESTRING)
} MergeInfoCacheEntry;

const char* GetMergeInfoCached(const char *pBranchName);

void ClearMergeInfoCache(void);

void InitMergeInfoCache(void);

#endif