#include "wiRevisions.h"
#include "wiRFCommon.h"
#include "rfMergeInfoCache.h"
#include "RevisionFinder.h"
#include "WebInterface/wiCommon.h"
#include "timing.h"
#include "ContinuousBuilder_Pub.h"
#include "Autogen/ContinuousBuilder_Pub_h_ast.h"
#include "CBMonitor.h"
#include "AutoGen/CBMonitor_h_ast.h"
#include "AutoGen/RevisionFinder_h_ast.h"
#include "NameValuePair.h"
#include "rfUtil.h"
#include "file.h"

extern int giServerMonitorPort;

extern RevisionFinderConfig *rfConfig;
extern BranchNamesList *rfBranches;

int SameBuildType(const rfBuildType *buildType, const char *buildTypeName)
{
	return (stricmp(buildType->pBuildTypeName, buildTypeName) == 0);
}

int CompareBuildInfos(const BuildDeployInfo *bdiA, const BuildDeployInfo *bdiB)
{
	if(stricmp(bdiA->pMachineName, bdiB->pMachineName) == 0 && stricmp(bdiA->pBuildStartTime, bdiB->pBuildStartTime) == 0) return 1;
	else return 0;
}

//Look up the builds for a revision
int GetBuilds(RevisionInfo *revision, rfSearchResponse *pResponse)
{
	int i = 0;
	int len;
	int didFind = 0;
	len = eaSize(&(revision->ppBuilds));
	for(i = 0; i < len; i ++)
	{
		int patchVarIndex;
		//Create a BuildDeployInfo and its component parts.
		SingleBuildInfo *sbi = StructCreate(parse_SingleBuildInfo);
		PatchDeployInfo *pdi = StructCreate(parse_PatchDeployInfo);
		BuildDeployInfo *bdi = StructCreate(parse_BuildDeployInfo);
		char filenameBuf[CRYPTIC_MAX_PATH]; //Buffer for patch info filename;
		char buildFileNameBuf[CRYPTIC_MAX_PATH];
		char* machineNameCopyBuf;
		int foundPatchInfo = 0;

		sprintf(buildFileNameBuf, "%s%s", fileLocalDataDir(), revision->ppBuilds[i]->pFilename);

		//Load the build info from file and add it to the BuildDeployInfo
		ParserReadTextFile(buildFileNameBuf,parse_SingleBuildInfo,sbi,PARSER_OPTIONALFLAG);
		bdi->pBuildInfo = sbi;

		machineNameCopyBuf = StructAllocString(revision->ppBuilds[i]->pMachineName);
		bdi->pMachineName = machineNameCopyBuf;
		bdi->iRevisionNumber = revision->iRevisionNumber;
		bdi->pSvnBranchName = sbi->pPresumedSVNBranch + RF_SVN_PREFIX_LENGTH;
		bdi->pBuildStartTime = StructAllocString(timeGetLocalSystemStyleStringFromSecondsSince2000(sbi->iStartTime));

		//Look for the patch name in the build info
		patchVarIndex = GetVarIndex(sbi->ppVariables,"$PATCHVERSION$");

		if(patchVarIndex != -1)
		{
			//If the patch name is listed, look up the patch name and get deployments
			sprintf(filenameBuf,"%s/%s/patches/%s.txt",fileLocalDataDir(),rfConfig->pBaseDir,sbi->ppVariables[patchVarIndex]->pValue);
			foundPatchInfo = fileExists(filenameBuf);
			if(foundPatchInfo == 0)
			{
				//If the patch version isn't listed, fill in only the patch name.
				pdi->pPatchName = StructAllocString(sbi->ppVariables[patchVarIndex]->pValue);
			}
			else
			{
				int iDeploy, deployCount;
				ParserReadTextFile(filenameBuf,parse_PatchDeployInfo,pdi,PARSER_OPTIONALFLAG);

				deployCount = eaSize(&(pdi->ppDeployments));
				if(((sbi->eResult == CBRESULT_SUCCEEDED_W_ERRS) || (sbi->eResult == CBRESULT_SUCCEEDED)))
				{
					for(iDeploy = 0; iDeploy < deployCount; iDeploy++)
					{
						int typeIndex = 0;
						//Here anything that says it's a "CONTROLLER" is renamed to a "SHARD", because no-one calls them CONTROLLERs
						if(pdi->ppDeployments[iDeploy]->pShardType == NULL) pdi->ppDeployments[iDeploy]->pShardType = StructAllocString("SHARD");
						else if (strcmp(pdi->ppDeployments[iDeploy]->pShardType, "CONTROLLER") == 0)
						{
							quick_sprintf(pdi->ppDeployments[iDeploy]->pShardType, 6, "SHARD");
						}
						typeIndex = eaFindCmp(&(pResponse->ppShardTypes), pdi->ppDeployments[iDeploy], SameDeployType);
						if(typeIndex == -1)
						{
							eaPush(&(pResponse->ppShardTypes), pdi->ppDeployments[iDeploy]);
						}
						else
						{
							U32 thisTime = timeGetSecondsSince2000FromSystemStyleString(pdi->ppDeployments[iDeploy]->pStartTime);
							U32 existingTime = timeGetSecondsSince2000FromSystemStyleString(pResponse->ppShardTypes[typeIndex]->pStartTime);
							if(thisTime < existingTime) pResponse->ppShardTypes[typeIndex] = pdi->ppDeployments[iDeploy];
						}
					}
				}
			}
			bdi->pDeployInfo = pdi; //Add the deployments list to the BuildDeployInfo
		}
		else
		{
			StructDestroy(parse_PatchDeployInfo,pdi);
			bdi->pDeployInfo = NULL;
		}
		
		
		

		//Filter for builds that succeeded
		if(((sbi->eResult == CBRESULT_SUCCEEDED_W_ERRS) || (sbi->eResult == CBRESULT_SUCCEEDED)))
		{
			int typeIndex = GetVarIndex(bdi->pBuildInfo->ppVariables, "$CBTYPE_VERBOSE$");
			if(typeIndex != -1)
			{
				int buildTypeIndex = eaFindCmp(&(pResponse->ppBuildTypes), bdi->pBuildInfo->ppVariables[typeIndex]->pValue, SameBuildType);
				if(buildTypeIndex == -1)
				{
					rfBuildType *buildType = StructCreate(parse_rfBuildType);
					buildType->iExists = 1;
					buildType->pBuildInfo = bdi;
					buildType->pBuildTypeName = bdi->pBuildInfo->ppVariables[typeIndex]->pValue;
					eaPush(&(pResponse->ppBuildTypes), buildType);
				}
				else
				{
					int bdiStartTime = timeGetSecondsSince2000FromSystemStyleString(bdi->pBuildStartTime);
					int existingStartTime = timeGetSecondsSince2000FromSystemStyleString(pResponse->ppBuildTypes[buildTypeIndex]->pBuildInfo->pBuildStartTime);
					if(bdiStartTime < existingStartTime)
					{
						pResponse->ppBuildTypes[buildTypeIndex]->pBuildInfo = bdi;
						pResponse->ppBuildTypes[buildTypeIndex]->pBuildTypeName = bdi->pBuildInfo->ppVariables[typeIndex]->pValue;
					}
				}
			}

			if(eaFindCmp(&(pResponse->ppBuildInfos), bdi, CompareBuildInfos) == -1)
			{
				eaPush(&(pResponse->ppBuildInfos),bdi);
			}
			else
			{
				StructDestroy(parse_BuildDeployInfo,bdi);
			}
			
			didFind = 1;
		}
		else
		{
			StructDestroy(parse_BuildDeployInfo,bdi);
		}
	}

	return didFind;
}

int rfCheckBranch(U32 svnRevision, const char* branchName, rfSearchResponse *pResponse, const char* revType);

int RevisionLookup(U32 minRevNum, const char* revFileName, rfSearchResponse *pResponse)
{
	static int depth = 3; //Limit recursion depth for searches *just in case*
	RevisionInfo revision = {0};
	int didFind = 0;
	int foundBranch = 0;
	if(fileExists(revFileName))
	{
		ParserReadTextFile(revFileName,parse_RevisionInfo,&revision,PARSER_OPTIONALFLAG);
		if(revision.pBranchName != NULL)
		{
			didFind = GetBuilds(&revision, pResponse); //Important part; look up the builds
	
			if(depth != 0 && revision.ppMergedTo != NULL) //Only SVN revisions should have merges
			{
				int mergedToCount = eaSize(&(revision.ppMergedTo));
				int iOtherBranch = 0;
				for(iOtherBranch = 0; iOtherBranch < mergedToCount; iOtherBranch ++)
				{
					//printf("Checking revision %d merged to %s\n", minRevNum,  revision.ppMergedTo[iOtherBranch]);
					depth --;
					didFind = rfCheckBranch(minRevNum, revision.ppMergedTo[iOtherBranch], pResponse, "SVN") || didFind;
					depth ++;
					if(didFind) foundBranch = 1;
				}
			}
		}
		StructDeInit(parse_RevisionInfo,&revision);
	}
	if(foundBranch) return 2;
	return didFind;
}

int rfCheckBranch(U32 svnRevision, const char* branchName, rfSearchResponse *pResponse, const char* revType)
{
	int didFind = 0;
	char systemCommandBuf[CRYPTIC_MAX_PATH];
	char branchFullPath[CRYPTIC_MAX_PATH];
	int errorLevel;
	char *dirContents = NULL;
	int dirContentsOffset = 0;

	if(eaiFind(&(pResponse->pSearchedRevisions), svnRevision) != -1) return 0;
	else eaiPush(&(pResponse->pSearchedRevisions), svnRevision);

	if(strcmp(revType, "SVN") == 0) sprintf(branchFullPath,"%s/%s/%s/code/svn/%s", fileLocalDataDir(), rfConfig->pBaseDir, revType, branchName);
	else sprintf(branchFullPath,"%s/%s/%s/%s", fileLocalDataDir(), rfConfig->pBaseDir, revType, branchName);

	sprintf(systemCommandBuf, "cmd /C dir \"%s\" /B", branchFullPath);
	errorLevel = system_w_output(systemCommandBuf, &dirContents);

	while(dirContents[dirContentsOffset] != 0)
	{
		char fileNameNoPath[32];
		int fileNameNoPathOffset = 0;
		U32 revNum = atoi(dirContents + dirContentsOffset);
		char fullFilePath[CRYPTIC_MAX_PATH];
		while(dirContents[dirContentsOffset] != '\n' && dirContents[dirContentsOffset] != 0)
		{
			fileNameNoPath[fileNameNoPathOffset] = dirContents[dirContentsOffset];
			fileNameNoPathOffset++;
			dirContentsOffset++;
		}
		dirContentsOffset++;
		fileNameNoPath[fileNameNoPathOffset - 1] = 0;
		sprintf(fullFilePath,"%s/%s", branchFullPath, fileNameNoPath);
		if(revNum >= svnRevision)
		{
			didFind = RevisionLookup(revNum, fullFilePath, pResponse) || didFind;
			if(didFind == 2) return 2;
		}
	}
	estrDestroy(&dirContents);
	return didFind;
}

int rfGetSameSVNBranchRevisions(U32 svnRevision, rfSearchResponse *pResponse)
{
	char systemCommandBuf[CRYPTIC_MAX_PATH];
	char *svnLogInfo = NULL;
	char *svnBranchStart;
	char branchNameBuf[CRYPTIC_MAX_PATH];
	int errorLevel;
	int didFind = 0;
	int branchNameChar = 0;
	sprintf(systemCommandBuf,"svn log http://code/svn -r %d -v", svnRevision);
	errorLevel = system_w_output(systemCommandBuf, &svnLogInfo);
	svnBranchStart = svnLogInfo;
	if(errorLevel == 0)
	{
		while(*svnBranchStart != '/' && *svnBranchStart != 0) svnBranchStart++;
		if(*svnBranchStart != 0)
		{
			char branchFullPath[CRYPTIC_MAX_PATH];
			while(*svnBranchStart != '\n')
			{
				branchNameBuf[branchNameChar] = *svnBranchStart;
				svnBranchStart++;
				branchNameChar++;
			}
			branchNameChar--;
			branchNameBuf[branchNameChar] = 0;
			sprintf(branchFullPath,"%s/%s/SVN/code/svn/%s", fileLocalDataDir(), rfConfig->pBaseDir, branchNameBuf);
			while(branchNameChar != 0 && !dirExists(branchFullPath))
			{
				while(branchNameBuf[branchNameChar] != '/') branchNameChar --;
				branchNameBuf[branchNameChar] = 0;
				sprintf(branchFullPath,"%s/%s/SVN/code/svn/%s", fileLocalDataDir(), rfConfig->pBaseDir, branchNameBuf);
			}
			didFind = rfCheckBranch(svnRevision, branchNameBuf, pResponse, "SVN");
			estrDestroy(&svnLogInfo);
			return didFind;
		}
		else
		{
			estrDestroy(&svnLogInfo);
			return 0;
		}
	}
	else
	{
		return 0;
	}
}

void rfSortBuilderTypes(rfSearchResponse *pResponse)
{
	int iOrder = 0;
	int typeCount = eaSize(&(rfConfig->ppBuilderTypeOrder));
	for(iOrder = 0; iOrder < typeCount; iOrder++)
	{
		int typeIndex = eaFindCmp(&(pResponse->ppBuildTypes), rfConfig->ppBuilderTypeOrder[iOrder], SameBuildType);
		rfBuildType *newBuildTypeEntry = StructCreate(parse_rfBuildType);
		newBuildTypeEntry->pBuildTypeName = rfConfig->ppBuilderTypeOrder[iOrder];
		newBuildTypeEntry->iExists = 0;
		newBuildTypeEntry->pBuildInfo = NULL;
		if(typeIndex != -1)
		{
			newBuildTypeEntry->iExists = 1;
			newBuildTypeEntry->pBuildInfo = pResponse->ppBuildTypes[typeIndex]->pBuildInfo;
			eaRemove(&(pResponse->ppBuildTypes), typeIndex);
		}
		eaPush(&(pResponse->ppSortedBuildTypes), newBuildTypeEntry);
	}
}

//Handle the search request
static void wiHandleRevisionFind(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	rfSearchResponse *pResponse = StructCreate(parse_rfSearchResponse);
	U32 uSVN_ID = 0;
	U32 uGimme_ID = 0;
	int didFind = 0;
	char pageTitle[64];
	const char *pGimmeBranch;
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	//Get parameters from request
	uSVN_ID = wiGetInt(pWebRequest, "svnID", 0);
	uGimme_ID = wiGetInt(pWebRequest,"gimmeID",0);
	pGimmeBranch = wiGetString(pWebRequest,"gimmeBranch");

	pResponse->pPageTitle = pageTitle; //Set the results table header

	//Look up the revision
	if(uSVN_ID)
	{
		didFind = rfGetSameSVNBranchRevisions(uSVN_ID, pResponse);
		sprintf(pageTitle,"SVN Revision #%d", uSVN_ID);
	}
	else if(uGimme_ID)
	{
		if(*pGimmeBranch)
		{
			didFind = rfCheckBranch(uGimme_ID, pGimmeBranch, pResponse, "Gimme");
			sprintf(pageTitle,"%s Revision #%d", pGimmeBranch, uGimme_ID);
		}
		else
		{
			sprintf(pageTitle,"Error: Gimme queries require the Gimme Product and Branch field.\neg. Night Branch 5 or StarTrek branch 31");
		}
	}
	else
	{
		pResponse->pPageTitle = NULL;
	}
	
	rfSortBuilderTypes(pResponse);
	wiAppendStruct(pWebRequest, "RevisionView.cs", parse_rfSearchResponse, pResponse);
	pResponse->pPageTitle = NULL;
	StructDestroy(parse_rfSearchResponse,pResponse);

	PERFINFO_AUTO_STOP_FUNC();
}

/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleRevisions(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	bool bHandled = false;
	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();
	
	wiHandleRevisionFind(pWebRequest);
	bHandled = true;

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}