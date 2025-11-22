#include "rfSvnUtil.h"
#include "RevisionFinder.h"
#include "AutoGen/RevisionFinder_h_ast.h"
#include "httpAsync.h"
#include "url.h"
#include "file.h"
#include "fileutil.h"
#include "earray.h"
#include "timing.h"
#include "timing_profiler_interface.h"

extern RevisionFinderConfig *rfConfig;
extern BranchNamesList *rfBranches;
extern bool didAddBranchName;

int rfMergeInfoMakeEntry = 0;

//Add merge to revision iff it exists, otherwise do nothing
void AddMergeToRevision(const char* revisionFile, const char* destinationBranch, int revNumber, const char* srcBranch)
{
	RevisionInfo targetRevision = {0};
	int didCreate = 0;
	int skip = 0;
	PERFINFO_AUTO_START_FUNC();
	if(fileExists(revisionFile))
	{
		ParserReadTextFile(revisionFile, parse_RevisionInfo, &targetRevision, 0);
		if(targetRevision.ppBuilds != NULL && eaSize(&(targetRevision.ppBuilds)) != 0)
		{
			rfMergeInfoMakeEntry = 1;
		}
	}
	else if(rfMergeInfoMakeEntry == 1)
	{
		rfMergeInfoMakeEntry = 0;
		didCreate = 1;
		StructInit(parse_RevisionInfo, &targetRevision);
		targetRevision.iRevisionNumber = revNumber;
		targetRevision.pBranchName = srcBranch + RF_SVN_PREFIX_LENGTH;
	}
	else
	{
		skip = 1;
	}

	if(!skip)
	{
		if(eaFindString(&(targetRevision.ppMergedTo), destinationBranch) == -1)
		{
			int newIndex = eaPush(&(targetRevision.ppMergedTo), destinationBranch + RF_SVN_PREFIX_LENGTH);
			ParserWriteTextFile(revisionFile, parse_RevisionInfo, &targetRevision, 0, 0);
			targetRevision.ppMergedTo[newIndex] = NULL;
		}
		if(didCreate)
		{
			targetRevision.pBranchName = NULL;
		}
		StructDeInit(parse_RevisionInfo, &targetRevision);
	}
	PERFINFO_AUTO_STOP();
}

void CheckForMerges(U32 src, U32 dst)
{
	const char *sourceBranch = rfBranches->ppBranchInfos[src]->pBranchName;
	const char *destinationBranch = rfBranches->ppBranchInfos[dst]->pBranchName;
	char cmd[RF_SYSTEM_COMMAND_SIZE];
	char *pSystemResponse = NULL;
	int errorLevel;
	PERFINFO_AUTO_START_FUNC();
	rfMergeInfoMakeEntry = 1;
	sprintf(cmd, "svn mergeinfo --show-revs merged %s %s", sourceBranch, destinationBranch);
	errorLevel = system_w_output(cmd, &pSystemResponse);

	if(errorLevel == 0 && pSystemResponse != NULL)
	{
		char *responseReader = pSystemResponse;
		while(*responseReader != 0)
		{
			while(*responseReader != 'r' && *responseReader != 0) responseReader ++; //Advance to the next 'r', or the end if that comes first
			if(*responseReader == 'r') responseReader ++; //Advance to start of number
			if(*responseReader != 0) //Just in case something went wrong and there's a 0 after the 'r'
			{
				int revNumber = atoi(responseReader);
				if(revNumber != 0)
				{
					char pRevisionFile[CRYPTIC_MAX_PATH];
					sprintf(pRevisionFile, "%s/%s/SVN/%s/%d.txt", fileLocalDataDir(), rfConfig->pBaseDir, sourceBranch + RF_HTTP_PREFIX_LENGTH, revNumber);
					AddMergeToRevision(pRevisionFile, destinationBranch, revNumber, sourceBranch);
				}
			}
		}
	}
	estrDestroy(&pSystemResponse);
	PERFINFO_AUTO_STOP();
}

//Each revision file has an array listing the branches it has been merged to
//This will allow RevisionFinder to chase changes across merges
void UpdateSvnMergedToLists()
{
	int rfBranchListSize = eaSize(&(rfBranches->ppBranchInfos));
	int src, dst;
	PERFINFO_AUTO_START_FUNC();
	printf("Updating \"Merged To\" fields in revision data... (%s)\n", timeGetLocalTimeString());
	for(src = 0; src < rfBranchListSize; src ++)
	{
		const char *pSrcBranch = rfBranches->ppBranchInfos[src]->pBranchName;
		if(strStartsWith(pSrcBranch, "http://")) //Only do 'real' branches
		{
			for(dst = 0; dst < rfBranchListSize; dst ++)
			{
				if(src != dst) //Don't test for merges from and to the same branch.
				{
					const char *pDstBranch = rfBranches->ppBranchInfos[dst]->pBranchName;
					if(strStartsWith(pDstBranch, "http://")) //Only do 'real' branches
					{
						CheckForMerges(src, dst);
					}
				}
			}
		}
	}
	printf("Finished updating \"Merged To\" fields. (%s)\n", timeGetLocalTimeString());
	PERFINFO_AUTO_STOP();
}