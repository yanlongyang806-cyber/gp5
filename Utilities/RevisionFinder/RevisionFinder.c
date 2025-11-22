#include "RevisionFinder.h"
#include "rfMergeInfoCache.h"
#include "rfSvnUtil.h"
#include "rfUtil.h"
#include "url.h"
#include "file.h"
#include "fileutil2.h"
#include "net.h"
#include "httpAsync.h"
#include "NameValuePair.h"
#include "ContinuousBuilder_Pub.h"
#include "Autogen/ContinuousBuilder_Pub_h_ast.h"
#include "CBMonitor.h"
#include "AutoGen/CBMonitor_h_ast.h"
#include "earray.h"
#include "timing.h"
#include "TimedCallback.h"
#include "httputil.h"
#include "webInterface/wiRFCommon.h"
#include "NewControllerTracker_Pub.h"
#include "NewControllerTracker_Pub_h_ast.h"

#include "AutoGen/RevisionFinder_h_ast.h"

//This file contains mainly the code for collecting data on REVISIONS, PATCHES, and SHARDS
//RevisionFinderEachHeatbeat is the first thing to run, called every SOME INTERVAL from
//revisionMain.c

//when BGB shards are run, the local Revision Finder talks to the controller, so that it will
//know to kill itself when the controller goes away
bool gbConnectToController = false;
AUTO_CMD_INT(gbConnectToController, ConnectToController) ACMD_CMDLINE;

int giWebInterfacePort = 80;
AUTO_CMD_INT(giWebInterfacePort, httpPort) ACMD_CMDLINE;

extern int rfDisabled;

RevisionFinderConfig *rfConfig = NULL;
BranchNamesList *rfBranches = NULL;
bool didAddBranchName = false;

int rfCompareDeployments(const Deployment *a, const Deployment *b)
{
	if(stricmp(a->pShardName,b->pShardName) == 0) return 1;
	else return 0;
}

void SavePatchInfo(char *patchNameBuf, char* sysName, U32 patchTime, char* sysType) 
{
	//Reached the end of a patch name, so create/update the file
	//Here we save a file named after the Patch Name containing a list of the
	//Shards its been deployed to. If a patch name appears in a shard's history
	//we add the Shard's name to that Patch's deployment list.
	char filenameBuf[256];
	Deployment *pDeploy = NULL;
	PatchDeployInfo *pPatchInfo = StructCreate(parse_PatchDeployInfo);

	pDeploy = StructCreate(parse_Deployment);

	sprintf(filenameBuf,"%s/%s/patches/%s.txt",fileLocalDataDir(),rfConfig->pBaseDir,patchNameBuf); //Build the filename
	if(fileExists(filenameBuf))
	{
		//If the file exists load it
		ParserReadTextFile(filenameBuf,parse_PatchDeployInfo,pPatchInfo, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
	}
	else
	{
		//Otherwise initialize a new one to save
		pPatchInfo->pPatchName = patchNameBuf;
		pPatchInfo->ppDeployments = NULL;
		eaCreate(&(pPatchInfo->ppDeployments));
	}

	pDeploy->pShardName = sysName;
	if(patchTime != 0)
	{
		pDeploy->pStartTime = timeGetLocalSystemStyleStringFromSecondsSince2000(patchTime);
		if(sysType == NULL) pDeploy->pShardType = "CONTROLLER";
		else pDeploy->pShardType = sysType;
		
	}
	else
	{
		pDeploy->pStartTime = NULL;
		pDeploy->pShardType = NULL;
	}

	//If the shard name is not already in the list we push it to the list and save the file
	if(eaFindCmp(&(pPatchInfo->ppDeployments),pDeploy,rfCompareDeployments) == -1)
	{
		eaPush(&(pPatchInfo->ppDeployments),pDeploy);
		ParserWriteTextFile(filenameBuf,parse_PatchDeployInfo,pPatchInfo,0,0);
		pDeploy->pShardName = NULL;
		pDeploy->pStartTime = NULL;
		pDeploy->pShardType = NULL;
	}
	else
	{
		//If it is already in the list we don't push it or write the file
		pDeploy->pShardName = NULL;
		pDeploy->pStartTime = NULL;
		pDeploy->pShardType = NULL;
		StructDestroy(parse_Deployment,pDeploy);
	}

	//We're done here
	if(pPatchInfo->pPatchName == patchNameBuf) pPatchInfo->pPatchName = NULL;
	StructDestroy(parse_PatchDeployInfo,pPatchInfo);
}

//Collect Shard information from the ControllerTracker's shard list response
void ParseCriticalShardsResponse(const char *ctText, int len, int critSysIndex)
{
	int strOffset = 0;
	char *ctTextNotConst = (char *) ctText;
	int structStartOffset = 0;
	int depth = 0;
	while(len > 0 && ctText[strOffset] != 0)
	{
		if(ctText[strOffset] == '{')
		{
			depth ++;
			if(depth == 2) //Basically any time we're three curly-bracket-levels in we're parsing a new ShardInfo
			{
				structStartOffset = strOffset;
			}
		}

		if(ctText[strOffset] == '}')
		{
			depth --;
			if(depth == 1) //Check if we have just exited a ShardInfo block
			{ //We have reached the end of a block and shall now parse the text, and push the shard onto the list
				CriticalSystem_Status_Partial *critsys = StructCreate(parse_CriticalSystem_Status_Partial);
				char existingChar = ctTextNotConst[strOffset+1];
				char* structStart = ctTextNotConst + structStartOffset;
				ctTextNotConst[strOffset+1] = 0;
				//The struct that ControllerTracker actually responds with is one of those weird generic wrapper things
				//Only interested in a few bits of the information anyway so parse it into a ShardInfo_Basic with IGNORE_UNKNOWN flags
				ParserReadText(ctTextNotConst + structStartOffset,parse_CriticalSystem_Status_Partial,critsys,PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
				ctTextNotConst[strOffset+1] = existingChar;

				if(critsys->pName_Internal != NULL && critsys->pVersion != NULL)
				{
					int i = 0;
					char enhancedType[MAX_SERVERNAME];
					//Renaming any "CONTROLLER"s to "SHARD"s to match usual parlance
					if(critsys->pMytype == NULL || strcmp(critsys->pMytype, "CONTROLLER") == 0)
					{
						sprintf(enhancedType, "%s_%s", rfConfig->ppCritSysCatNames[critSysIndex], "SHARD");
					}
					else
					{
						sprintf(enhancedType, "%s_%s", rfConfig->ppCritSysCatNames[critSysIndex], critsys->pMytype);
					}
					while(critsys->pVersion[i] != ' ' && critsys->pVersion[i] != 0) i++;
					critsys->pVersion[i] = 0;
					SavePatchInfo(critsys->pVersion, critsys->pName_Internal, timeSecondsSince2000(), enhancedType);

				}
				
				StructDestroy(parse_CriticalSystem_Status_Partial, critsys);
			}
		}
		strOffset++;
		len --;
	}
}

void rfCriticalSystemsHttpBody(const char *response, int len, int response_code, void *userData)
{
	PERFINFO_AUTO_START_FUNC();
	if(response_code == HTTP_OK)
	{
		int critSysIndex = 0;
		if(userData != NULL)
		{
			critSysIndex = *((int*) userData);
			free(userData);
		}
		ParseCriticalShardsResponse(response, len, critSysIndex);
	}
	PERFINFO_AUTO_STOP();
}

//Parse the response from the shard monitor page
//It's in XML so there will be some parsing to do
void rfMonitorHttpBody(const char *response, int len, int response_code, ShardInfo_Basic *shardInfo)
{
	PERFINFO_AUTO_START_FUNC();
	if(response_code == HTTP_OK)
	{
		int responseOffset = 0;

		char patchNameBuf[CRYPTIC_MAX_PATH];
		int patchNameOffset = 0;

		const char* cmpString    = "<patchname><![CDATA["; //Actually only interested in the presence of patch names here
		const char* startTimeStr = "<starttime>";
		int cmpIndex = 0;
		int startTimeCmpIndex = 0;
		U32 startTimeForNextEntry = 0;

		bool copying = false;

		while(responseOffset < len)
		{
			if(copying) //If we're in COPYING mode we're parsing a new patch name from the list
			{
				if(response[responseOffset] == ' ' || response[responseOffset] == ']')
				{
					copying = false;
					patchNameBuf[patchNameOffset] = 0;
					patchNameOffset = 0;
					SavePatchInfo(patchNameBuf, shardInfo->pShardName, startTimeForNextEntry, "SHARD");

				}
				else
				{
					patchNameBuf[patchNameOffset] = response[responseOffset];
					patchNameOffset ++;
					if(patchNameOffset == CRYPTIC_MAX_PATH) patchNameOffset --;
				}
			}
			else
			{
				//Check whether our compare string is still matching
				if(response[responseOffset] == cmpString[cmpIndex])
				{
					cmpIndex++;
				}
				else
				{	//If its not, check if we reached the end of the compare string
					if(cmpString[cmpIndex] == 0)
					{
						//We have, so back up one character and start copying
						copying = true;
						responseOffset --;
					}
					cmpIndex = 0;
				}

				//Check whether our compare string is still matching
				if(response[responseOffset] == startTimeStr[startTimeCmpIndex])
				{
					startTimeCmpIndex++;
				}
				else
				{	//If its not, check if we reached the end of the compare string
					if(startTimeStr[startTimeCmpIndex] == 0)
					{
						startTimeForNextEntry = atoi(response + responseOffset);
					}
					startTimeCmpIndex = 0;
				}
			}
			responseOffset ++;
		}
	}

	if(response_code == 401)
	{
		printf("Error! Incorrect authorization code given.\n");
	}

	StructDestroy(parse_ShardInfo_Basic, shardInfo);
	PERFINFO_AUTO_STOP();
}

//Collect Shard information from the ControllerTracker's shard list response
ShardInfo_Basic** ParseCTShardsResponse(const char *ctText, int len)
{
	int strOffset = 0;
	char textBuf[RF_PARSE_CT_BUF_SIZE];
	int bufIndex = 0;
	int depth = 0;
	ShardInfo_Basic **ppParsedShards = NULL;
	eaCreate(&ppParsedShards);
	while(ctText[strOffset] != 0 && len > 0)
	{
		if(ctText[strOffset] == '{') depth ++;

		if(depth >= 3) //Basically any time we're three curly-bracket-levels in we're parsing a new ShardInfo
		{
			textBuf[bufIndex] = ctText[strOffset];
			if(bufIndex + 1 != RF_PARSE_CT_BUF_SIZE) bufIndex ++; //Cut off at end of buffer instead of running off into la-la-land
			
		}

		if(ctText[strOffset] == '}')
		{
			depth --;
			if(depth == 2) //Check if we have just exited a ShardInfo block
			{ //We have reached the end of a block and shall now parse the text, and push the shard onto the list
				ShardInfo_Basic *shard = StructCreate(parse_ShardInfo_Basic);
				textBuf[bufIndex] = 0;
				//The struct that ControllerTracker actually responds with is one of those weird generic wrapper things
				//Only interested in a few bits of the information anyway so parse it into a ShardInfo_Basic with IGNORE_UNKNOWN flags
				ParserReadText(textBuf,parse_ShardInfo_Basic,shard,PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
				eaPush(&ppParsedShards,shard);
				bufIndex = 0;
			}
		}
		strOffset++;
		len --;
	}
	return ppParsedShards;
}

//Get the link from the monitor hyperlink in pShardInfo
//Also make sure we've selected XML format in the request
UrlArgumentList* GenerateMonitorURL(ShardInfo_Basic *pShardInfo)
{
	char linkBuf[RF_PARSE_LINK_BUF_SIZE];
	char linkBuf2[RF_PARSE_LINK_BUF_SIZE + 40]; //Make sure there's room for what we're tacking on
	int linkBufIndex = 0;
	int shardMonitorLinkOffset = 0;
	int quoteCount = 0;
	while(pShardInfo->pMonitoringLink[shardMonitorLinkOffset] != 0 && quoteCount < 2) //Loop to get the portion of the text in between quotes, ignore the rest
	{
		if(pShardInfo->pMonitoringLink[shardMonitorLinkOffset] == '\"') quoteCount++;
		if(quoteCount == 1 && (pShardInfo->pMonitoringLink[shardMonitorLinkOffset] != '\"'))
		{
			linkBuf[linkBufIndex] = pShardInfo->pMonitoringLink[shardMonitorLinkOffset];
			if(linkBufIndex + 1 != RF_PARSE_LINK_BUF_SIZE) linkBufIndex++;
		}
		shardMonitorLinkOffset++;
	}
	linkBuf[linkBufIndex] = 0;
	sprintf(linkBuf2,"%s?xpath=controller[0].custom&format=xml",linkBuf); //Assemble the link

	return urlToUrlArgumentList(linkBuf2);
}

void SaveShardPatchHistory(ShardInfo_Basic *pShardInfo)
{
	UrlArgumentList *url = GenerateMonitorURL(pShardInfo); //Parse the monitor page URL from the hyperlink
	char authString[RF_AUTHSTRING_SIZE];
	if(rfConfig->pAuthentication != NULL)
	{
		sprintf(authString, "Basic %s", rfConfig->pAuthentication);
		authString[ARRAY_SIZE_CHECKED(authString)-1] = 0;
		urlAddValue(url, "Authorization", authString,HTTPMETHOD_HEADER);
	}
	haRequest(commDefault(), &url, rfMonitorHttpBody, NULL, 200, pShardInfo); //Collect patch history from monitor page, in XML
	urlDestroy(&url);
}

//Collect patch information from Controller Tracker
void rfControllerTrackerHttpBody(const char *response, int len, int response_code, void *req)
{
	PERFINFO_AUTO_START_FUNC();
	if(response_code == HTTP_OK)
	{
		//Parse out a list of ShardInfo_Basic type from the ControllerTracker's response
		ShardInfo_Basic **ppShardsInfo = ParseCTShardsResponse(response, len);
		//For each Shard in the list we'll go to its monitor and grab the patch history
		eaForEach(&ppShardsInfo,SaveShardPatchHistory);
 		eaDestroy(&ppShardsInfo);
	}
	PERFINFO_AUTO_STOP();
}

//Used to check whether a given BuildRef has already been recorded
int rfBuildRefCompare(const BuildRef *a, const BuildRef *b)
{
	int cmp = stricmp(a->pFilename,b->pFilename);
	if(cmp == 0) return 1;
	else return 0;
}

void UpdateRevision(const char* buildFileName, const char* machineName, const char* incType, const char* branchName, int iIncNum) 
{
	char incFileName[128];
	RevisionInfo *pIncremental = StructCreate(parse_RevisionInfo);
	BuildRef *pBuildRef = StructCreate(parse_BuildRef);
	int doesExist = 0;
	pBuildRef->pFilename = buildFileName;
	pBuildRef->pMachineName = machineName;
	sprintf(incFileName,"%s/%s/%s/%s/%d.txt",fileLocalDataDir(),rfConfig->pBaseDir,incType,branchName,iIncNum);
	doesExist = fileExists(incFileName);
	if(!doesExist)
	{
		StructInit(parse_RevisionInfo,pIncremental);
		pIncremental->iRevisionNumber = iIncNum;
		pIncremental->pBranchName = branchName;
		eaCreate(&(pIncremental->ppBuilds));
	}
	else
	{
		ParserReadTextFile(incFileName,parse_RevisionInfo,pIncremental,0);
	}

	if((!doesExist) || (eaFindCmp(&(pIncremental->ppBuilds),pBuildRef,rfBuildRefCompare) == -1))
	{

		eaPush(&(pIncremental->ppBuilds),pBuildRef);
		ParserWriteTextFile(incFileName,parse_RevisionInfo,pIncremental,0,0);
		if(!doesExist) pIncremental->pBranchName = NULL;
		pBuildRef->pFilename = NULL;
		pBuildRef->pMachineName = NULL;
	}
	else
	{
		pBuildRef->pFilename = NULL;
		pBuildRef->pMachineName = NULL;
		StructDestroy(parse_BuildRef, pBuildRef);
	}

	StructDestroy(parse_RevisionInfo,pIncremental);
}

//Take a string listing revision numbers represented by commas,
//for each of these add/update the lists of BUILDS these REVISIONS have gone into
int rfParseIncrementals(const char* incStr, const char* buildFileName, const char* incType, const char* machineName, const char* branchName)
{
	int incOffset = 0;
	if(incStr == NULL) //If the list is empty we're done, nothing to record.
	{
		return 0;
	}
	while(incStr[incOffset] != 0) //Go until we reach null terminator
	{
		int iIncNum = atoi(incStr + incOffset); //Get the first number in the string+offset
		if(iIncNum) //If a nonzero value has been found, create or update corresponding textfile
		{
			UpdateRevision(buildFileName, machineName, incType, branchName, iIncNum);
		}
		while(incStr[incOffset] != ',' && incStr[incOffset] != 0) incOffset++;
		if(incStr[incOffset] == ',') incOffset ++;
	}
	return 1;
}

BranchInfo *CheckBranchList(const char* name)
{
	int i = 0;
	for(i = eaSize(&(rfBranches->ppBranchInfos)) - 1; i >= 0; i --)
	{
		if(stricmp(name, rfBranches->ppBranchInfos[i]->pBranchName) == 0) return rfBranches->ppBranchInfos[i];
	}
	return NULL;
}

const char* rfGetBranchNameForSVN(U32 svn)
{
	char *svnLogInfo = NULL;
	char sysCommandBuf[RF_SYSTEM_COMMAND_SIZE];
	int errorLevel;
	sprintf(sysCommandBuf,"svn log http://code/svn -r %d -v", svn);
	errorLevel = system_w_output(sysCommandBuf, &svnLogInfo);
	if(errorLevel == 0)
	{
		char *svnBranchStart = svnLogInfo;
		while(*svnBranchStart != '/' && *svnBranchStart != 0) svnBranchStart++;
		if(*svnBranchStart != 0)
		{
			int knownBranchCount = eaSize(&(rfBranches->ppBranchInfos));
			int iKnownBranch;
			for(iKnownBranch = 0; iKnownBranch < knownBranchCount; iKnownBranch ++)
			{
				char *knownBranch = rfBranches->ppBranchInfos[iKnownBranch]->pBranchName;
				if(strlen(knownBranch) > RF_SVN_PREFIX_LENGTH) knownBranch += RF_SVN_PREFIX_LENGTH; //Skip redundant part of branchname
				if(strStartsWith(svnBranchStart, knownBranch))
				{
					estrDestroy(&svnLogInfo);
					return rfBranches->ppBranchInfos[iKnownBranch]->pBranchName + RF_HTTP_PREFIX_LENGTH;
				}
			}
		}
	}
	estrDestroy(&svnLogInfo);
	return NULL;
}

int rfGetPreviousRevisionOnBranch(int svn, const char* branch)
{
	char cmdBuf[RF_SYSTEM_COMMAND_SIZE];
	char *outputBuf = NULL;
	const char *revPtr;
	int prevRev = 0;
	int errorLevel;
	sprintf(cmdBuf, "svn log -l 1 %s@%d", branch, svn);

	errorLevel = system_w_output(cmdBuf, &outputBuf);

	if(errorLevel == 0)
	{
		revPtr = strchr_fast(outputBuf, 'r');
		if(revPtr != NULL)
		{
			revPtr++;
			prevRev = atoi(revPtr);
		}
	}
	estrDestroy(&outputBuf);
	return prevRev;
}

//Handle the http response from the Builder page
//For each build we'll check for revision numbers and then handle those with rfParseIncrementals
void rfBuilderHttpBody(const char *response, int len, int response_code, void *req)
{
	PERFINFO_AUTO_START_FUNC();
	if(response_code == HTTP_OK)
	{
		BuilderInfo *builder = StructCreate(parse_BuilderInfo);
		int buildCount;
		int iBuild;
		BranchInfo *seenBranch = NULL;
		ParserReadText(response,parse_BuilderInfo,builder,0); //Get the builder object from the response
		buildCount = eaSize(&(builder->ppPreviousBuilds));
		//For each build in the builder's log
		for(iBuild = buildCount - 1; iBuild >= 0; iBuild--) //Iterate over builds
		{
			if(builder->ppPreviousBuilds[iBuild]->eResult == CBRESULT_SUCCEEDED || builder->ppPreviousBuilds[iBuild]->eResult == CBRESULT_SUCCEEDED_W_ERRS)
			{
				int iVar;
				int listedSVN = 0;
				char builderFileName[CRYPTIC_MAX_PATH];
				sprintf(builderFileName,"/%s/builds/%s_%d.txt",rfConfig->pBaseDir,builder->pMachineName,builder->ppPreviousBuilds[iBuild]->iStartTime);
				//For each variable in the build

				for(iVar = eaSize(&(builder->ppPreviousBuilds[iBuild]->ppVariables)) - 1; iVar >= 0; iVar--) //Check the VARIABLES entry for any tasty revision numbers
				{
					if(stricmp(builder->ppPreviousBuilds[iBuild]->ppVariables[iVar]->pName,"$GIMMEREVNUM$") == 0) //Check its singular revision numbers
					{
						//The function expects a CSV list but a string containing a single number works too
						rfParseIncrementals(builder->ppPreviousBuilds[iBuild]->ppVariables[iVar]->pValue,builderFileName,"Gimme", builder->pMachineName,
							builder->ppPreviousBuilds[iBuild]->pPresumedGimmeProductAndBranch);
					}
					else if(stricmp(builder->ppPreviousBuilds[iBuild]->ppVariables[iVar]->pName,"$SVNREVNUM$") == 0)
					{
						int prevSVN;
						listedSVN = atoi(builder->ppPreviousBuilds[iBuild]->ppVariables[iVar]->pValue);
						prevSVN = rfGetPreviousRevisionOnBranch(listedSVN, builder->ppPreviousBuilds[iBuild]->pPresumedSVNBranch);
						if(prevSVN != 0 && prevSVN != listedSVN)
						{
							listedSVN = prevSVN;
						}
						UpdateRevision(builderFileName, builder->pMachineName, "SVN", builder->ppPreviousBuilds[iBuild]->pPresumedSVNBranch + RF_HTTP_PREFIX_LENGTH, listedSVN);
					}
					else if(stricmp(builder->ppPreviousBuilds[iBuild]->ppVariables[iVar]->pName,"$GIMME_INCREMENTALS$") == 0) //Check its incrementals 
					{
						//These entries are basically CSV lists so we'll parse them
						rfParseIncrementals(builder->ppPreviousBuilds[iBuild]->ppVariables[iVar]->pValue,builderFileName,"Gimme", builder->pMachineName, 
							builder->ppPreviousBuilds[iBuild]->pPresumedGimmeProductAndBranch);
					}
				}

				sprintf(builderFileName,"%s/%s/builds/%s_%d.txt",fileLocalDataDir(),rfConfig->pBaseDir,builder->pMachineName,builder->ppPreviousBuilds[iBuild]->iStartTime);
				if(!fileExists(builderFileName))
				{
					ParserWriteTextFile(builderFileName,parse_SingleBuildInfo,builder->ppPreviousBuilds[iBuild],0,0);
				}

				//Check if we've seen this branch before
				seenBranch = CheckBranchList(builder->ppPreviousBuilds[iBuild]->pPresumedSVNBranch);
				//If not, add it to the list. Save the list.
				if(seenBranch == NULL)
				{
					BranchInfo *pBranch = StructCreate(parse_BranchInfo);
					char *svnBranchNameCopy = StructAllocString(builder->ppPreviousBuilds[iBuild]->pPresumedSVNBranch);
					pBranch->pBranchName = svnBranchNameCopy;
					eaiPush(&(pBranch->pRevisionNumbers), listedSVN);
					eaPush(&(rfBranches->ppBranchInfos),pBranch);
					didAddBranchName = true;
				}
				else
				{
					if(eaiFind(&(seenBranch->pRevisionNumbers), listedSVN) == -1)
					{
						eaiPush(&(seenBranch->pRevisionNumbers), listedSVN);
						didAddBranchName = true;
					}
				}
			}
		}
		StructDestroy(parse_BuilderInfo,builder);
		
	}
	PERFINFO_AUTO_STOP();
}

//Parse out the (internal) monitoring link for a builder
// then bother the page at that URL
void GetInfoFromBuilder(BuilderOverview *builder)
{
	int startOffset = 0;
	int len = 0;
	int quoteCount = 0;
	char * linkStr;
	UrlArgumentList *args;
	char linkFull[RF_PARSE_LINK_BUF_SIZE];
	while(quoteCount < 3) //Skip over the first three quote marks to get to the second URL in the link field
	{
		if(builder->pLink[startOffset] == '\"') quoteCount ++;
		startOffset ++;
	}

	while(builder->pLink[startOffset + len] != '\"') len++; //Get the length until the next quote
	linkStr = malloc(len + 1);
	strncpy_s(linkStr,len + 1,builder->pLink + startOffset,len); //Copying out the URL from the hyperlink
	linkStr[len] = 0;
	sprintf(linkFull,"%s%s%s",REVISION_FINDER_BASE_URL,linkStr,REVISION_FINDER_TEXT_OPTION);

	args = urlToUrlArgumentList(linkFull);
	
	haRequest(commDefault(), &args, rfBuilderHttpBody, NULL, 200, NULL); //Bother the builder's monitor page to get the BUILDS

	free(linkStr);
	urlDestroy(&args);
}

//Called in an eaForEach below
//Iterate over the builders in a category
void GetInfoFromCategory(BuilderCategoryOverview *builderCat)
{
	eaForEach(&(builderCat->ppBuilders), GetInfoFromBuilder);
}

void UpdateMergedToTimedCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	UpdateSvnMergedToLists();
}

//Initialize Revision Finder
int RevisionFinderInit(void)
{
	char configFilenameBuf[CRYPTIC_MAX_PATH];
	sprintf(configFilenameBuf,"%s/%s",fileLocalDataDir(),REVISION_FINDER_CONFIG_FILE);
	rfConfig = StructCreate(parse_RevisionFinderConfig);
	if(fileExists(configFilenameBuf))
	{
		ParserReadTextFile(configFilenameBuf,parse_RevisionFinderConfig,rfConfig,0);
	}
	else //If can't find the config file, create one with sample values to edit later
	{
		rfConfig->pAuthentication = NULL;
		rfConfig->ppControllerTrackerURLs = NULL;
		rfConfig->pBaseDir = StructAllocString(REVISION_FINDER_BASE_DIR_DEFAULT);
		rfConfig->pBuilderPageBaseURL = StructAllocString(REVISION_FINDER_BASE_URL);
		rfConfig->pBuilderPageURL = StructAllocString(REVISION_FINDER_BUILDERS_URL);
		eaPush(&(rfConfig->ppControllerTrackerURLs), StructAllocString("http://qact:8080/viewxpath?xpath=ControllerTracker[1].globObj.Shards&textparser=1"));
		eaPush(&(rfConfig->ppCritSysURLs), StructAllocString("http://qact:8080/viewxpath?xpath=ControllerTracker[1].CriticalSystems.all&textparser=1"));
		eaPush(&(rfConfig->ppBuilderTypeOrder), StructAllocString("Continuous Dev Build"));
		eaPush(&(rfConfig->ppBuilderTypeOrder), StructAllocString("Looping Prod Builder"));
		eaPush(&(rfConfig->ppBuilderTypeOrder), StructAllocString("Incremental Production Build"));
		ParserWriteTextFile(configFilenameBuf,parse_RevisionFinderConfig,rfConfig,0,0);
	}

	sprintf(configFilenameBuf,"%s/%s",fileLocalDataDir(),REVISION_FINDER_BRANCHES_FILE);
	rfBranches = StructCreate(parse_BranchNamesList);
	if(fileExists(configFilenameBuf))
	{
		ParserReadTextFile(configFilenameBuf,parse_BranchNamesList,rfBranches,0);
	}
	else
	{
		rfBranches->ppBranchInfos = NULL;
		eaCreate(&(rfBranches->ppBranchInfos));
	}
	InitMergeInfoCache();

	if((rfDisabled & RF_DONT_RESPOND_REQS) == 0) revisionFinderHttpInit(giWebInterfacePort); //Initialize http serving
	
	//Updating "MergedTo" sections of revisions less frequently because it can take some time, around 15 minutes
	if((rfDisabled & RF_DONT_COLLECT_DATA) == 0) TimedCallback_Add(UpdateMergedToTimedCallback, NULL, RF_MERGEDTO_UPDATE_INTERVAL);
	//Update merged to lists at start
	//if((rfDisabled & RF_DONT_COLLECT_DATA) == 0) UpdateSvnMergedToLists();

	return 1;
}

//Handle response from the Builders overview page
//Handle it by iterating over the categories and builders within those categories to collect build revision information
void rfOverviewHttpBody(const char *response, int len, int response_code, void *req)
{
	PERFINFO_AUTO_START_FUNC();
	if(response_code == HTTP_OK)
	{
		CBMonitorOverview *cbm_over = StructCreate(parse_CBMonitorOverview);
		ParserReadText(response,parse_CBMonitorOverview,cbm_over,0);
		eaForEach(&(cbm_over->ppCategories),GetInfoFromCategory); //Nested eaForEach calls- ultimately calls GetInfoFromBuilder
		StructDestroy(parse_CBMonitorOverview,cbm_over);
	}
	PERFINFO_AUTO_STOP();
}

void rfSendCritSysRequests()
{
	int i = 0;
	int count = eaSize(&(rfConfig->ppCritSysURLs));
	for(i = 0; i < count; i++)
	{
		char *url = rfConfig->ppCritSysURLs[i];
		UrlArgumentList *args = urlToUrlArgumentList(url);
		int *critSysIndex = malloc(sizeof(int)); //Passing the critical system index to rfCriticalSystemsHttpBody so it knows which tracker it's checking
		*critSysIndex = i;
		haRequest(commDefault(), &args, rfCriticalSystemsHttpBody, NULL, 200, critSysIndex); //Bother the ControllerTracker Critical Systems page.
		urlDestroy(&args);
	}
}

void rfSendCTRequest(char* url)
{
	UrlArgumentList *args = urlToUrlArgumentList(url);
	haRequest(commDefault(), &args, rfControllerTrackerHttpBody, NULL, 200, NULL); //Bother the ControllerTracker.
	urlDestroy(&args);
}

void rfUpdateMergeCache(const BranchInfo* branch)
{
	GetMergeInfoCached(branch->pBranchName);
}

//Called from revisionMain every SOME INTERVAL or so
//Checks with the Builder monitor for revisions and build data
//then checks with ControllerTrackers for patch deployment info
void RevisionFinderEachHeartbeat(void)
{
	if((rfDisabled & RF_DONT_COLLECT_DATA) == 0)
	{
		UrlArgumentList *args = urlToUrlArgumentList(REVISION_FINDER_BUILDERS_URL);
		haRequest(commDefault(), &args, rfOverviewHttpBody, NULL, 200, NULL); //Bother the Builders page
		urlDestroy(&args);

		//Read the ControllerTracker shards pages
		//Commented out because it's a little finickey due to access authorizations. Worth fixing because shard pages show their patch history, though.
		//eaForEach(&(rfConfig->ppControllerTrackerURLs),rfSendCTRequest); //Bother all the ControllerTrackers on our list
		
		//Read the ControllerTracker critical systems pages
		rfSendCritSysRequests();

		if(didAddBranchName)
		{
			char branchFileName[CRYPTIC_MAX_PATH];
			sprintf(branchFileName,"%s/%s",fileLocalDataDir(),REVISION_FINDER_BRANCHES_FILE);
			ParserWriteTextFile(branchFileName,parse_BranchNamesList,rfBranches,0,0);
		}
	}
}

void RevisionFinderShutdown(void)
{
	//Do shut down behavior
}

#include "AutoGen/RevisionFinder_h_ast.c"
#include "AutoGen/CBMonitor_h_ast.c"
#include "AutoGen/ContinuousBuilder_Pub_h_ast.c"
#include "AutoGen/NewControllerTracker_Pub_h_ast.c"