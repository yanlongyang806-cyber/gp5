#include "rfUtil.h"
#include "utils.h"
#include "NameValuePair.h"
#include "RevisionFinder.h"
#include "earray.h"
#include "fileutil.h"
#include "file.h"

SysAsyncRequest **rfActiveSysRequests = NULL;

extern RevisionFinderConfig *rfConfig;

//Check a list of NameValuePairs for an entry with a certain key
int GetVarIndex(const NameValuePair **ppVarList, const char *pKey)
{
	int index = 0;
	int listSize = eaSize(&ppVarList);
	while(index < listSize)
	{
		if(stricmp(ppVarList[index]->pName, pKey) == 0) return index;
		index ++;
	}
	return -1;
}

int StrEqual(const char *stringA, const char *stringB)
{
	if(stringA == stringB) return 1;
	if(stringA == NULL || stringB == NULL) return 0;
	return (stricmp(stringA, stringB) == 0);
}

int SameDeployType(const Deployment *deploymentA, const Deployment *deploymentB)
{
	return StrEqual(deploymentA->pShardType, deploymentB->pShardType);
}