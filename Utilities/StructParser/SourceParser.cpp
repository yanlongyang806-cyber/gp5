#include "sourceparser.h"
#include "strutils.h"

#include "MagicCommandManager.h"
#include "structparser.h"
#include "AutoTransactionManager.h"
#include "autorunmanager.h"
#include "windows.h"
#include "direct.h"
#include "autoTestManager.h"
#include "latelinkmanager.h"
#include "intrin.h"
#include "SourceParserXML.h"

extern int gDoMasterWikiCreation;



#define MAX_WILDCARD_MAGIC_WORDS 16

#define STRUCTPARSERSTUB_PROJ "StructParserStub"

char sTime[] = __TIME__;

typedef enum
{
	RW_FILE = RW_COUNT,
	RW_RELATIVEPATH,
	RW_CONFIGURATION,
	RW_ADDITIONALINCLUDEDIRECTORIES,
	RW_PREPROCESSORDEFINITIONS,
	RW_OUTPUTDIRECTORY,
	RW_OBJECTFILE,
	RW_NAME,
	RW_TOOL,
	RW_PROPERTYSHEETS,
	RW_INTERMEDIATEDIRECTORY,
	RW_PROJECTREFERENCE,
	RW_PROJECT_INPROJFILE,
};

static char *sProjectReservedWords[] =
{
	"File",
	"RelativePath",
	"Configuration",
	"AdditionalIncludeDirectories",
	"PreprocessorDefinitions",
	"OutputDirectory",
	"ObjectFile",
	"Name",
	"Tool",
	"InheritedPropertySheets",
	"IntermediateDirectory",
	"ProjectReference",
	"Project",

	NULL
};
StringTree *spProjectReservedWordTree = NULL;




typedef enum
{
	RW_GLOBAL = RW_COUNT,
	RW_PROJECT_INSOLUTIONFILE,
	RW_PROJECTDEPENDENCIES,
	RW_ENDPROJECTSECTION,
	RW_ENDPROJECT,
	RW_POSTSOLUTION,
};

static char *sSolutionReservedWords[] =
{
	"Global",
	"Project",
	"ProjectDependencies",
	"EndProjectSection",
	"EndProject",
	"postSolution",
	NULL
};

StringTree *spSolutionReservedWordTree = NULL;


//must be all caps
static char *sFileNamesToExclude[] =
{
	"STDTYPES.H",
	NULL
};

static char *sProjectNamesToExclude[] =
{
	"GimmeDLL",
	NULL
};

bool ShouldFileBeExcluded(char *pFileName)
{
	char tempFileName[MAX_PATH];
	strcpy(tempFileName, pFileName);

    {
        static const char *sFilePrefixesToExclude[] =
        {
            "Program Files",
            //"..\\..\\..\\..\\Program Files\\Microsoft Xbox 360 SDK\\include",
            "3rdparty",
            //"..\\..\\3rdparty\\DirectX\\Include",
            //"..\\..\\3rdparty\\ps3\\cell\\target\\ppu\\include",
        };

        for(int i=0; i<sizeof(sFilePrefixesToExclude)/sizeof(sFilePrefixesToExclude[0]); i++) {
            if(strstr(pFileName, sFilePrefixesToExclude[i]))
                return true;
        }
    }

	char *pTemp;
	char *pSimpleFileName = pTemp = tempFileName;

	while (*pTemp)
	{
		if ((*pTemp == '/' || *pTemp == '\\') && *(pTemp + 1))
		{
			pSimpleFileName = pTemp + 1;
		}

		pTemp++;
	}

	MakeStringUpcase(pSimpleFileName);

	int i = 0;

	while (sFileNamesToExclude[i])
	{
		if (AreFilenamesEqual(pSimpleFileName, sFileNamesToExclude[i]))
		{
			return true;
		}


		i++;
	}

	if (strstr(pSimpleFileName, "AUTOGEN"))
	{
		return true;
	}

	return false;
}




SourceParser::SourceParser()
{
	m_iNumSourceParsers = NUM_BASE_SOURCE_PARSERS;

	memset(m_pSourceParsers, 0, sizeof(m_pSourceParsers));

	m_pAutoRunManager = NULL;

	m_iNumProjectFiles = 0;

	memset(m_iNumDependencies, 0, sizeof(m_iNumDependencies));

	m_pFileListLoader = new FileListLoader;
	m_pFileListWriter = new FileListWriter;

	memset(m_bFilesNeedToBeUpdated, 0, sizeof(m_bFilesNeedToBeUpdated));

	memset(m_iExtraDataPerFile, 0, sizeof(m_iExtraDataPerFile));

	m_iNumDependentLibraries = 0;

	m_FoundAutoGenFile1 = false;
	m_FoundAutoGenFile1 = false;
	m_bIsAnExecutable = false;

	m_bProjectFileChanged = false;
	m_bCleanBuildHappened = false;

	m_iNumMasterWikiProjects = 0;

	m_pFirstVar = NULL;

	m_FoundAutoGenFile1 = m_FoundAutoGenFile2 = 0;

}

void SourceParser::CreateParsers(void)
{
	m_pSourceParsers[SOURCEPARSERINDEX_MAGICCOMMANDMANAGER] = new MagicCommandManager;
	m_pSourceParsers[SOURCEPARSERINDEX_STRUCTPARSER] = new StructParser;
	m_pSourceParsers[SOURCEPARSERINDEX_AUTOTRANSACTIONMANAGER] = new AutoTransactionManager;
	m_pSourceParsers[SOURCEPARSERINDEX_AUTOTESTMANAGER] = new AutoTestManager;
	m_pSourceParsers[SOURCEPARSERINDEX_LATELINKMANAGER] = new LateLinkManager;

	//AutoRunManager should generally be last
	m_pSourceParsers[SOURCEPARSERINDEX_AUTORUNMANAGER] = m_pAutoRunManager = new AutoRunManager;
}


SourceParser::~SourceParser()
{
	int i;

	for (i=0; i < m_iNumSourceParsers; i++)
	{
		if (m_pSourceParsers[i])
		{
			delete m_pSourceParsers[i];
		}
	}

	while (m_pFirstVar)
	{
		SourceParserVar *pNext = m_pFirstVar->pNext;
		delete m_pFirstVar->pVarName;
		delete m_pFirstVar->pValue;
		delete m_pFirstVar;
		m_pFirstVar = pNext;
	}

	delete m_pFileListLoader;
	delete m_pFileListWriter;
}

bool SourceParser::IsLibraryXBoxExcluded(char *pLibName)
{
	if (strstr(pLibName, "GLRenderLib"))
	{
		return true;
	}

	return false;
}

bool SourceParser::IsLibraryPS3Excluded(char *pLibName)
{
	if (strstr(pLibName, "GLRenderLib"))
	{
		return true;
	}

	return false;
}

/*	char *pProjectIDStrings[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectFullPaths[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectDependencyIDs[MAX_PROJECTS_ONE_SOLUTION][MAX_DEPENDENCIES_ONE_PROJECT];
	int iNumDependenciesPerProject[MAX_PROJECTS_ONE_SOLUTION] = {0};
*/

void InsertedProjectIntoSortedList(int iProjNum, int iOutSortedIndices[MAX_PROJECTS_ONE_SOLUTION], int iNumProjects, 
		char *pProjectIDStrings[MAX_PROJECTS_ONE_SOLUTION], char *pProjectDependencyIDs[MAX_PROJECTS_ONE_SOLUTION][MAX_DEPENDENCIES_ONE_PROJECT], 
		int iNumDependenciesPerProject[MAX_PROJECTS_ONE_SOLUTION], bool iAlreadyInserted[MAX_PROJECTS_ONE_SOLUTION],
		int *piListSize, int iRecurseDepth)
{
	int iDepNum, iOtherProjNum;

	if (iAlreadyInserted[iProjNum])
	{
		return;
	}

	STATICASSERTF(iRecurseDepth < MAX_PROJECTS_ONE_SOLUTION, "Presumed project dependency circularity");

	for (iDepNum = 0; iDepNum < iNumDependenciesPerProject[iProjNum]; iDepNum++)
	{
		char *pOtherProjID = pProjectDependencyIDs[iProjNum][iDepNum];

		for (iOtherProjNum = 0; iOtherProjNum < iNumProjects; iOtherProjNum++)
		{
			if (_stricmp(pOtherProjID, pProjectIDStrings[iOtherProjNum]) == 0)
			{
				InsertedProjectIntoSortedList(iOtherProjNum, iOutSortedIndices, iNumProjects,
					pProjectIDStrings, pProjectDependencyIDs, iNumDependenciesPerProject, iAlreadyInserted,
					piListSize, iRecurseDepth + 1);
			}
		}
	}

	STATICASSERTF(*piListSize < iNumProjects, "Corruption during project sorting");

	iAlreadyInserted[iProjNum] = true;
	iOutSortedIndices[*piListSize] = iProjNum;
	(*piListSize)++;
}



void SortProjectsByDependencies(int iOutSortedIndices[MAX_PROJECTS_ONE_SOLUTION], int iNumProjects, 
		char *pProjectIDStrings[MAX_PROJECTS_ONE_SOLUTION], char *pProjectDependencyIDs[MAX_PROJECTS_ONE_SOLUTION][MAX_DEPENDENCIES_ONE_PROJECT], 
		int iNumDependenciesPerProject[MAX_PROJECTS_ONE_SOLUTION])
{
	bool iAlreadyInserted[MAX_PROJECTS_ONE_SOLUTION] = {0};
	int i;
	int iListSize = 0;

	for (i=0; i < iNumProjects; i++)
	{
		InsertedProjectIntoSortedList(i, iOutSortedIndices, iNumProjects, pProjectIDStrings, pProjectDependencyIDs,
			iNumDependenciesPerProject, iAlreadyInserted, &iListSize, 0);
	}
}

bool SourceParser::ShouldSkipProjectEntirely(char *pProjName)
{
	if (VariableIsSuffix("ProjectsToIgnoreCompletelySuffixes", pProjName))
	{
		return true;
	}

	return false;
}

void SourceParser::TryToReadProjectDepenciesFromProjectFile(char *pProjectDependencyIDs[MAX_DEPENDENCIES_ONE_PROJECT], int *piNumDependencies, char *pProjFullPath)
{
	Tokenizer tokenizer;
	char projAbsPath[MAX_PATH];

	MakeFilenameRelativeToOnePathRelativeToAnotherPath(projAbsPath, pProjFullPath, m_SolutionPath, NULL);

	bool bResult = tokenizer.LoadFromFile(projAbsPath);

	STATICASSERTF(bResult, "Couldn't load project file %s", pProjFullPath);

	Token token;
	enumTokenType eType;
	
	tokenizer.SetExtraReservedWords(sProjectReservedWords, &spProjectReservedWordTree);

	while ((eType = tokenizer.GetNextToken(&token)) != TOKEN_NONE)
	{
		//skip over everything after a slash, so we don't get fooled by /ProjectReference
		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_SLASH)
		{
			eType = tokenizer.GetNextToken(&token);
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_PROJECTREFERENCE)
		{
			while (1)
			{
				char tempBuf[256] = "";
				eType = tokenizer.GetNextToken(&token);
				if (eType == TOKEN_NONE)
				{
					tokenizer.AssertFailed("Found EOF while looking for <Project> after <ProjectReference>");
				}

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_PROJECT_INPROJFILE)
				{
					if (*piNumDependencies == MAX_DEPENDENCIES_ONE_PROJECT)
					{
						tokenizer.AssertFailed("Too many dependencies for one project");
					}

					tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_GT, "Didn't find > after Project");
	
					tokenizer.AssertGetBracketBalancedBlock(RW_LEFTBRACE, RW_RIGHTBRACE, "Parse error of some sort while reading dependent project ID", 
						tempBuf, sizeof(tempBuf));

					if (strlen(tempBuf) < 4)
					{
						tokenizer.AssertFailedf("Something went wrong while reading proejct ID for dependencies... read %s", tempBuf);
					}


					pProjectDependencyIDs[*piNumDependencies] = STRDUP(tempBuf);
					(*piNumDependencies)++;

					break;
				}
			}
		}
	}
}


void SourceParser::ProcessSolutionFile(bool bRecursivelyCallStructParser, bool bForceReadAllFiles)
{
	Tokenizer tokenizer;
	
	int iNumProjects = 0;
	char *pProjectNames[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectIDStrings[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectFullPaths[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectDependencyIDs[MAX_PROJECTS_ONE_SOLUTION][MAX_DEPENDENCIES_ONE_PROJECT];
	char pProjectConfigsToUse[MAX_PROJECTS_ONE_SOLUTION][32] = {0};
	char pProjectPlatformsToUse[MAX_PROJECTS_ONE_SOLUTION][32] = {0};

	int iNumDependenciesPerProject[MAX_PROJECTS_ONE_SOLUTION] = {0};

	bool bResult = tokenizer.LoadFromFile(m_SolutionPath);
		
	bool bFoundStubProj = false;
	bool bDependsOnStubProj = false;

	STATICASSERT(bResult, "Couldn't load solution file");

	//set reservedwords used for parsing through .vcproj file
	tokenizer.SetExtraReservedWords(sSolutionReservedWords, &spSolutionReservedWordTree);

	Token token;
	enumTokenType eType;
	int iThisProjectNum = -1;

	while ((eType = tokenizer.GetNextToken(&token)) != TOKEN_NONE)
	{
		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_GLOBAL)
		{
			break;
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_PROJECT_INSOLUTIONFILE)
		{
			ASSERT(&tokenizer,iNumProjects < MAX_PROJECTS_ONE_SOLUTION, "Too many projects in .sln file");

			do
			{
				eType = tokenizer.GetNextToken(&token);
				ASSERT(&tokenizer,eType != TOKEN_NONE, "Unexpected end of .sln file while parsing project");
			}
			while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_EQUALS));

			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Expected project name");

			if (ShouldSkipProjectEntirely(token.sVal) || (StringIsInList(token.sVal, sProjectNamesToExclude) && strcmp(token.sVal, m_ShortenedProjectFileName) != 0))
			{
				//skip this project
			}
			else
			{

				pProjectNames[iNumProjects] = new char[token.iVal + 1];
				strcpy(pProjectNames[iNumProjects], token.sVal);

				if (stricmp(token.sVal, STRUCTPARSERSTUB_PROJ) == 0)
				{
					bFoundStubProj = true;
				}

				if (stricmp(token.sVal, m_ShortenedProjectFileName) == 0)
				{
					iThisProjectNum  = iNumProjects;
				}


				tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after project name");
				tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Expected project full path");

				pProjectFullPaths[iNumProjects] = STRDUP(token.sVal);
				
				tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after project full path");
				tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Expected project ID string");

				pProjectIDStrings[iNumProjects] = new char[token.iVal + 1];
				strcpy(pProjectIDStrings[iNumProjects], token.sVal);

				do
				{
					eType = tokenizer.GetNextToken(&token);

					ASSERT(&tokenizer,eType != TOKEN_NONE, "unexpected end of file before EndProject");

					if (token.eType == TOKEN_RESERVEDWORD && token.iVal == RW_ENDPROJECT)
					{
						break;
					}

					if (token.eType == TOKEN_RESERVEDWORD && token.iVal == RW_PROJECTDEPENDENCIES)
					{
						do
						{
							eType = tokenizer.GetNextToken(&token);
							ASSERT(&tokenizer,eType != TOKEN_NONE, "unexpected end of file before EndProjectSection");

							if (token.eType == TOKEN_RESERVEDWORD && token.iVal == RW_ENDPROJECTSECTION)
							{
								break;
							}

							if (token.eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE)
							{
								char tempString[1024] = "{";
								
								tokenizer.SetDontParseInts(true);

								do
								{
									eType = tokenizer.GetNextToken(&token);

									ASSERT(&tokenizer,eType != TOKEN_NONE, "unexpected end of file in project UID");

									if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
									{
										break;
									}

									ASSERT(&tokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_RESERVEDWORD && token.iVal == RW_MINUS, 
										"found unexpected characters while parsing projectUID");
									tokenizer.StringifyToken(&token);

									strcat(tempString, token.sVal);
								} while (1);

								tokenizer.SetDontParseInts(false);

								strcat(tempString, "}");

								ASSERTF(&tokenizer,iNumDependenciesPerProject[iNumProjects] < MAX_DEPENDENCIES_ONE_PROJECT,
									"Project %s is dependent on too many other projects", pProjectFullPaths[iNumProjects]);

								pProjectDependencyIDs[iNumProjects][iNumDependenciesPerProject[iNumProjects]++] = STRDUP(tempString);

								do
								{
									eType = tokenizer.GetNextToken(&token);

									ASSERT(&tokenizer,eType != TOKEN_NONE, "unexpected end of file in project UID");

								} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE));

							}
						} while (1);

					}
				} while (1);

				//for VS 2010, load dependencies an entirely different way
				if (!iNumDependenciesPerProject[iNumProjects])
				{
					TryToReadProjectDepenciesFromProjectFile(pProjectDependencyIDs[iNumProjects], &(iNumDependenciesPerProject[iNumProjects]), pProjectFullPaths[iNumProjects]);
				}


				iNumProjects++;
			}
		}
	}

	if (stricmp(m_ShortenedProjectFileName, "StructParserStub")==0 && iThisProjectNum == -1)
	{
		// IncrediBuild on VS2010 erroneously builds StructParserStub even if it is not in the solution.
		return;
	}
	ASSERT(&tokenizer,iThisProjectNum != -1, "Didn't find current project referenced in .sln file");


	//now look for postSolution, so we can read in config info
	do
	{
		eType = tokenizer.GetNextToken(&token);
		if (eType == TOKEN_NONE)
		{
			ASSERT(&tokenizer,0, "Didn't find postSolution");
		}
	} 
	while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_POSTSOLUTION));
	
	char build0String[256];	
	char activeCfgString[256];
	int iBuild0StrLen, iActiveCfgStrLen;
	
	sprintf(build0String, ".%s|%s.Build.0 = ", m_pCurConfiguration, m_pCurTarget);
	sprintf(activeCfgString, ".%s|%s.ActiveCfg = ", m_pCurConfiguration, m_pCurTarget);

	iBuild0StrLen = (int)strlen(build0String);
	iActiveCfgStrLen = (int)strlen(activeCfgString);


	while (1)
	{
		char IDBuf[256];
		char lastIDBuf[256] = "";
		int iLastProjectNum = -1;
		int iCurProjectNum;

		eType = tokenizer.CheckNextToken(&token);
		if (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE))
		{
			break;
		}

		tokenizer.GetSimpleBracketedString('{', '}', IDBuf, sizeof(IDBuf));
		
		if (lastIDBuf[0] && strcmp(IDBuf, lastIDBuf) == 0)
		{
			iCurProjectNum = iLastProjectNum;
		}
		else
		{
			int i;
			for (i=0; i < iNumProjects; i++)
			{
				if (stricmp(IDBuf, pProjectIDStrings[i]) == 0)
				{
					iCurProjectNum = iLastProjectNum = i;
					strcpy(lastIDBuf, IDBuf);
					break;
				}
			}

			if (i != iNumProjects)
			{
				char *pFoundCfg = NULL;

				if (memcmp(tokenizer.GetReadHead(), build0String, iBuild0StrLen) == 0)
				{
					pFoundCfg = tokenizer.GetReadHead() + iBuild0StrLen;
				}
				else if (memcmp(tokenizer.GetReadHead(), activeCfgString, iActiveCfgStrLen) == 0)
				{
					pFoundCfg = tokenizer.GetReadHead() + iActiveCfgStrLen;
				}

				if (pFoundCfg)
				{
					char *pWriteHead = pProjectConfigsToUse[i];
					while (*pFoundCfg && *pFoundCfg != '|')
					{
						ASSERT(&tokenizer,*pFoundCfg, "Unexpected file end");
						*pWriteHead = *pFoundCfg;
						pWriteHead++;
						pFoundCfg++;
					}
					pFoundCfg++;
					pWriteHead = pProjectPlatformsToUse[i];
					while (*pFoundCfg && *pFoundCfg != '\n')
					{
						ASSERT(&tokenizer,*pFoundCfg, "Unexpected file end");
						*pWriteHead = *pFoundCfg;
						pWriteHead++;
						pFoundCfg++;
					}

					//sometimes a tab or \r or something slips in
					RemoveTrailingWhiteSpace(pProjectPlatformsToUse[i]);
				}
			}
		}

		tokenizer.AdvanceToBeginningOfLine();
	}








	int iDependencyNum;
	bool bFound =false;
	

	for (iDependencyNum = 0; iDependencyNum < iNumDependenciesPerProject[iThisProjectNum]; iDependencyNum++)
	{
		int i;

		for (i=0; i < iNumProjects; i++)
		{
			if (i != iThisProjectNum)
			{
				if (stricmp(pProjectDependencyIDs[iThisProjectNum][iDependencyNum], pProjectIDStrings[i]) == 0)
				{
					if (stricmp(pProjectNames[i], STRUCTPARSERSTUB_PROJ) != 0)
					{
						ASSERT(&tokenizer,m_iNumDependentLibraries < MAX_DEPENDENT_LIBRARIES, "too many dependent libraries");
						strcpy(m_DependentLibraryNames[m_iNumDependentLibraries], pProjectNames[i]);
						assembleFilePath(m_DependentLibraryAbsolutePaths[m_iNumDependentLibraries], m_SolutionDir, pProjectFullPaths[i]);
						MakeFilenameRelativeToOnePathRelativeToAnotherPath(m_DependentLibraryRelativePaths[m_iNumDependentLibraries], pProjectFullPaths[i], m_SolutionDir, m_ProjectPath);

						m_bExcludeLibrariesFromXBOX[m_iNumDependentLibraries] = IsLibraryXBoxExcluded(pProjectNames[i]);
                        m_bExcludeLibrariesFromPS3[m_iNumDependentLibraries] = IsLibraryPS3Excluded(pProjectNames[i]);
						
						
						m_iNumDependentLibraries++;
					}
					else
					{
						bDependsOnStubProj = true;
					}
					break;
				}
			}
		}
	}

						


	if (bRecursivelyCallStructParser)
	{
		int iProjIndex;
		int iProjNum;
		char solutionDirectory[MAX_PATH];
		int iSortedProjectNums[MAX_PROJECTS_ONE_SOLUTION];


		strcpy(solutionDirectory, m_SolutionPath);
		int iSolutionLength = (int)strlen(solutionDirectory);
		while (solutionDirectory[iSolutionLength - 1] != '\\' && solutionDirectory[iSolutionLength - 1] != '/')
		{
			solutionDirectory[iSolutionLength - 1] = 0;
			iSolutionLength--;
		}


		if (bForceReadAllFiles)
		{
			TRACE("Erasing all autogenerated files for all all projects\n");
			CleanOutAllAutoGenFiles(m_ProjectPath, m_ShortenedProjectFileName);

			for (iProjNum = 0; iProjNum < iNumProjects; iProjNum++)
			{
				char fullPath[MAX_PATH];

				assembleFilePath(fullPath, solutionDirectory, pProjectFullPaths[iProjNum]);

				int iFullPathLen = (int)strlen(fullPath);
				while (fullPath[iFullPathLen - 1] != '\\' && fullPath[iFullPathLen - 1] != '/')
				{
					fullPath[iFullPathLen - 1] = 0;
					iFullPathLen--;
				}


				CleanOutAllAutoGenFiles(fullPath, pProjectNames[iProjNum]);
			}
		}


/*	char *pProjectIDStrings[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectFullPaths[MAX_PROJECTS_ONE_SOLUTION];
	char *pProjectDependencyIDs[MAX_PROJECTS_ONE_SOLUTION][MAX_DEPENDENCIES_ONE_PROJECT];
	int iNumDependenciesPerProject[MAX_PROJECTS_ONE_SOLUTION] = {0};
*/

		SortProjectsByDependencies(iSortedProjectNums, iNumProjects, pProjectIDStrings, pProjectDependencyIDs, iNumDependenciesPerProject);


		SourceParserRecursionContext recursionContext = {0};


		
		for (iProjIndex=0; iProjIndex < iNumProjects; iProjIndex++)
		{
			iProjNum = iSortedProjectNums[iProjIndex];

			if (iProjNum != iThisProjectNum)
			{

				if (pProjectConfigsToUse[iProjNum] &&
					!(IsLibraryXBoxExcluded(pProjectNames[iProjNum]) && strstr(m_pCurTarget, "Xbox")) )
				{
				

					char fullPath[MAX_PATH];

					assembleFilePath(fullPath, solutionDirectory, pProjectFullPaths[iProjNum]);

					int iFullPathLen = (int)strlen(fullPath);
					while (fullPath[iFullPathLen - 1] != '\\' && fullPath[iFullPathLen - 1] != '/')
					{
						fullPath[iFullPathLen - 1] = 0;
						iFullPathLen--;
					}

					char projectName[256];
					sprintf(projectName, "%s%s", pProjectNames[iProjNum], strrchr(m_FullProjectFileName, '.'));
			
					SourceParser *pRecurseParser = new SourceParser;

					printf("About to recursively call: %s X %s X %s X %s X %s X %s X %s\n",
						m_Executable, fullPath, projectName, pProjectPlatformsToUse[iProjNum], pProjectConfigsToUse[iProjNum], m_pCurVCDir, m_SolutionPath);

	//				if (gDoMasterWikiCreation)
					{
						pRecurseParser->ParseSource(fullPath, projectName, pProjectPlatformsToUse[iProjNum], pProjectConfigsToUse[iProjNum], m_pCurVCDir, m_SolutionPath, m_Executable, &recursionContext);

						if (ProjectGoesIntoMasterWiki(projectName))
						{
							AddProjectForMasterWikiCreation(fullPath, projectName);

							pRecurseParser->AddDependentProjectsForMasterWikiCreation(this);
						}

						

						delete pRecurseParser;

					}
				/*	else
					{
						char systemString[2048];

						sprintf(systemString, "xgsubmit /group=StructParser # %s X %s X %s X %s X %s X %s X %s", m_Executable, fullPath, projectName, platformToUse, configToUse, m_pCurVCDir, m_SolutionPath);

						int iResult = system(systemString);

						if (iResult != 0)
						{
							exit(iResult);
						}
					}*/


				}
			}
		}
/*
		if (!gDoMasterWikiCreation)
		{
			int iRetVal = system("xgwait /group=Structparser /exitcode=highest");
			if (iRetVal != 0)
			{
				exit(iRetVal);
			}
		}
*/

//now do all the C file compiling for all recursed sourceParsers
#if 0
		bool bFailed = false;
		int iFailNum;
		int i;

		for (i=0; i < recursionContext.iNumCompileBatchFiles; i++)
		{
			char systemString[1024];

			sprintf(systemString, "%s > NUL", recursionContext.compileBatchFileNames[i]);

			int retVal = system(systemString);

			if (retVal)
			{
				bFailed = true;
				iFailNum = i;
				sprintf(systemString, "erase %s 2>nul\n", recursionContext.compileBatchFileNameKeyFiles[i]);
				system(systemString);
			}
		}


		if (bFailed)
		{
			//KLUDGE: if we get a compile error, run again without NUL redirection so that error message is visible
			system(recursionContext.compileBatchFileNames[iFailNum]);

			printf("ERROR::: Something went wrong while compiling %s\n", recursionContext.compileBatchFileNames[iFailNum]);
		
			fflush(stdout);
			BreakIfInDebugger();

			Sleep(100);

			exit(1);
		}
#endif

		ProcessProjectFile();


		if (gDoMasterWikiCreation)
		{
			DoMasterWikiCreation(false);
			DoMasterWikiCreation(true);
		}

	}
	else
	{
		if (bFoundStubProj && !bDependsOnStubProj)
		{
			char errorString[1024];
			sprintf(errorString, "StructParserStub project exists, but project %s doesn't depend on it", m_ShortenedProjectFileName);
			STATICASSERT(0, errorString);
		}
	}

	int i;
	for (i=0; i < iNumProjects; i++)
	{
		int j;

		delete [] pProjectNames[i];
		delete [] pProjectIDStrings[i];
		delete [] pProjectFullPaths[i];

		for (j=0; j < iNumDependenciesPerProject[i]; j++)
		{
			delete[] pProjectDependencyIDs[i][j];
		}
	}
}

void SourceParser::CheckForRequiredFiles(char *pFileName)
{
	char *pShortName = GetFileNameWithoutDirectories(pFileName);

	while (pShortName[0] == '\\' || pShortName[0] == '/')
	{
		pShortName++;
	}

	if (_stricmp(m_AutoGenFile1Name, pShortName) == 0)
	{
		m_FoundAutoGenFile1 = true;
	}
	else if (_stricmp(m_AutoGenFile2Name, pShortName) == 0)
	{
		m_FoundAutoGenFile2 = true;
	}
}

void SourceParser::GetAdditionalStuffFromPropertySheets(char *pDirsAlreadyFound, char *pPropertySheetNames, char *pToolName, int iReservedWordToFind)
{
	char *pTemp;

	if ((pTemp = strstr(pDirsAlreadyFound, "$(NOINHERIT)")))
	{
		*pTemp = 0;
		return;
	}

	while (pPropertySheetNames && pPropertySheetNames[0])
	{
		char fileName[MAX_PATH];
		strcpy(fileName, pPropertySheetNames);

		if ((pTemp = strchr(fileName, ';')))
		{
			*pTemp = 0;
		}

		if ((pTemp = strchr(pPropertySheetNames, ';')))
		{
			pPropertySheetNames = pTemp + 1;
		}
		else
		{
			pPropertySheetNames = NULL;
		}

		if (strstr(fileName, "UpgradeFromVC71.vsprops"))
		{
			continue;
		}

		Tokenizer tokenizer;
		tokenizer.LoadFromFile(fileName);
		tokenizer.SetExtraReservedWords(sProjectReservedWords, &spProjectReservedWordTree);
		
		Token token;
		enumTokenType eType;
	
		bool bDone = false;

		do
		{
			eType = tokenizer.GetNextToken(&token);

			if (eType == TOKEN_NONE)
			{
				break;
			}

			if (eType == TOKEN_STRING && strcmp(token.sVal, pToolName) == 0)
			{
				do
				{
					eType = tokenizer.GetNextToken(&token);
					if (eType == TOKEN_NONE)
					{
						bDone = true;
						break;
					}

					if (eType == TOKEN_RESERVEDWORD && token.iVal == iReservedWordToFind)
					{
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Expected = ");
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string");
					
						strcat(pDirsAlreadyFound, ";");
						strcat(pDirsAlreadyFound, token.sVal);
						bDone = true;
						break;
					}

					if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_GT)
					{
						bDone = true;
						break;
					}
				} while (true);
			}

		} while (!bDone);

	}
}

char *getExecutableDir(void)
{
	static char buf[MAX_PATH];
	GetModuleFileName(NULL, buf, MAX_PATH);
	char *s;
	while (s = strchr(buf, '\\'))
		*s = '/';
	s = strrchr(buf, '/');
	*s = '\0';
	return buf;
}

//the way this works is that you first compare the first iPrefixLen characters to see if this is
//the "right" condition to check. If those don't match, then you don't even care, so you can't reject.
//If those do match, then the whole string needs to match.
typedef struct ConditionToMatch
{
	char stringToMatch[1024];
	int iPrefixLen;
} ConditionToMatch;

bool PruneVS2010ProjFileByCondition(XMLNode *pNode, void *pUserData)
{
	ConditionToMatch *pCondition = (ConditionToMatch*)pUserData;

	XMLNodeAttrib *pAttrib = XMLNode_FindAttrib(pNode, "Condition");
	if (!pAttrib || !pAttrib->pValue)
	{
		return false;
	}

	if (strnicmp(pAttrib->pValue, pCondition->stringToMatch, pCondition->iPrefixLen) != 0)
	{
		return false;
	}

	if (stricmp(pAttrib->pValue, pCondition->stringToMatch) != 0)
	{
		return true;
	}

	return false;
}
	
void SourceParser::ReplacePlatformAndConfigurationMacros_2010(char *pString)
{
	char *sTempMacros[][2] =
	{
		{
			"$(Configuration)",
			m_pCurConfiguration,
		},
		{
			"$(Platform)",
			m_pCurTarget,
		},
		{
			"$(PlatformToolsetVersion)",
			"100",
		},
		
		{
			"$(ProjectName)",
			m_ShortenedProjectFileName,
		},
		{
			NULL,
			NULL
		}
	};

	ReplaceMacrosInPlace(pString, sTempMacros);
}


void SourceParser::GetVariableFromProjFileAndPropertySheets(char outString[TOKENIZER_MAX_STRING_LENGTH], XMLNode *pParentNode, XMLNode *pPropertySheetNodes[MAX_PROPERTYSHEETS_PER_PROJ], int iNumPropertySheets, char *pVariablePath, char *pVariableName)
{
	char tempString[TOKENIZER_MAX_STRING_LENGTH] = "";
	char varNameFixedUp[128];
	int i;

	outString[0] = 0;

	sprintf(varNameFixedUp, "%%(%s)", pVariableName);

	for (i=0; i <= iNumPropertySheets; i++)
	{
		XMLNodeList list = {0};
		XMLNode *pNode = (i == iNumPropertySheets) ? pParentNode : pPropertySheetNodes[i];
		XMLNode_SearchWithPath(&list, pNode, pVariablePath);
		if (list.iSize)
		{
			STATICASSERTF(list.iSize == 1, "Found %d occurrences of %s in %s... that's not good!", 
				list.iSize, pVariablePath, i == iNumPropertySheets ? "project file" : "property sheet");
			
			char *pCData = XMLNode_GetCData(list.ppNodes[0]);

			STATICASSERTF(pCData, "Found an empty string for %s in %s", pVariableName, i == iNumPropertySheets ? "project file" : "property sheet");
			
			if (strstri(pCData, varNameFixedUp))
			{
				strcpy(tempString, pCData);
				RemoveTrailingWhiteSpace(tempString);
				RemoveLeadingWhiteSpace(tempString);
				ReplaceFirstOccurrenceOfSubString(tempString, varNameFixedUp, outString);
				strcpy(outString, tempString);
			}
			else
			{
				strcpy(outString, pCData);
				RemoveTrailingWhiteSpace(outString);
				RemoveLeadingWhiteSpace(outString);

			}
		}
	}

}

void SourceParser::ProcessProjectFile_VS2010(void)
{
	int iBufSize;
	char *pBuf = fileAlloc(m_FullProjectFileName, &iBufSize);
	XMLNodeList list = {0};
	int i;
	ConditionToMatch condition;
	bool bProjectPathEndsWithSlash = StringEndsWith(m_ProjectPath, "\\");

	int iNumPropertySheets = 0;
	XMLNode *pPropertySheetNodes[MAX_PROPERTYSHEETS_PER_PROJ];

	XMLNodeAttrib *pAttrib;

	STATICASSERTF(pBuf, "Couldn't open %s", m_FullProjectFileName);
	XMLNode *pRootNode = XMLNode_ParseFromBuffer(pBuf, iBufSize);
	free(pBuf);
	STATICASSERTF(pRootNode, "Basic XML parsing failed for %s", m_FullProjectFileName);

	sprintf(condition.stringToMatch, "'$(Configuration)|$(Platform)'=='%s|%s'", 
		m_pCurConfiguration, m_pCurTarget);
	condition.iPrefixLen = 32; //length of the above string through the ==
	XMLNode_RecursivelyPruneWithCallBack(pRootNode, PruneVS2010ProjFileByCondition, (void*)&condition);

	//open property sheet if any
	XMLNode_SearchWithPath(&list, pRootNode, "Project.ImportGroup(Label=PropertySheets).Import");
	
	STATICASSERTF(list.iSize <= MAX_PROPERTYSHEETS_PER_PROJ, "%s has more than %d property sheets, not allowed", m_FullProjectFileName, MAX_PROPERTYSHEETS_PER_PROJ);
	
	for (i = 0; i < list.iSize; i++)
	{
		char fullFileName[MAX_PATH];
		
		pAttrib = XMLNode_FindAttrib(list.ppNodes[i], "Project");
		STATICASSERT(pAttrib, "Didn't find Project attrib while trying to find name of property sheet");

		MakeFilenameRelativeToOnePathRelativeToAnotherPath(fullFileName, pAttrib->pValue, m_FullProjectFileName, NULL);

		char *pPropertySheetBuf;
		int iPropertySheetSize;

		//ignore this one property sheet. Ask Jimb why.
		if (!strstri(fullFileName, "UpgradeFromVC71"))
		{

			pPropertySheetBuf = fileAlloc(fullFileName, &iPropertySheetSize);
			STATICASSERTF(pPropertySheetBuf, "Couldn't load %s while processing %s", fullFileName, m_FullProjectFileName);

			pPropertySheetNodes[iNumPropertySheets] = XMLNode_ParseFromBuffer(pPropertySheetBuf, iPropertySheetSize);
			free(pPropertySheetBuf);

			STATICASSERTF(pPropertySheetNodes[iNumPropertySheets], "Couldnpt parse %s while processing %s", fullFileName, m_FullProjectFileName);
			iNumPropertySheets++;
		}
	}

	XMLNodeList_Clear(&list, false);


	//get all filenames
	XMLNode_SearchWithPath(&list, pRootNode, "Project.ItemGroup.ClCompile");
	XMLNode_SearchWithPath(&list, pRootNode, "Project.ItemGroup.ClInclude");

	for (i=0; i < list.iSize; i++)
	{
		XMLNode *pFileNode = list.ppNodes[i];
		XMLNodeAttrib *pFileAttrib = XMLNode_FindAttrib(pFileNode, "Include");
		if (pFileAttrib)
		{
			CheckForRequiredFiles(pFileAttrib->pValue);

			int len = (int)strlen(pFileAttrib->pValue);	

			if ((len >= 3 && pFileAttrib->pValue[len - 2] == '.' && (pFileAttrib->pValue[len - 1] == 'h' || pFileAttrib->pValue[len - 1] == 'c')))
			{

				char sourceFileName[256];
				sprintf(sourceFileName, "%s%s%s", m_ProjectPath, bProjectPathEndsWithSlash ? "" : "\\", pFileAttrib->pValue);

				STATICASSERT(m_iNumProjectFiles < MAX_FILES_IN_PROJECT, "Too many files in project");
				
				if (!ShouldFileBeExcluded(sourceFileName))
				{
					strcpy(m_ProjectFiles[m_iNumProjectFiles++], sourceFileName);
				}
			}
		}
	}

	XMLNodeList_Clear(&list, false);

	//check whether it's an executable
	XMLNode_SearchWithPath(&list, pRootNode, "Project.ItemDefinitionGroup.Link");
	m_bIsAnExecutable = list.iSize > 0;
	XMLNodeList_Clear(&list, false);

	//get object file dir
	XMLNode_SearchWithPath(&list, pRootNode, "Project.PropertyGroup.IntDir");
	if (list.iSize == 0)
	{
		// No intermediate dir, must be using default from GeneralSettings.props
		strcpy(m_ObjectFileDir, "..\\..\\obj$(PlatformToolsetVersion)\\$(ProjectName)\\$(Configuration) $(Platform)\\");
	} else {
		STATICASSERTF(list.iSize == 1, "Found %d intDirs instead of 1 in %s", list.iSize, m_FullProjectFileName);
		STATICASSERTF(XMLNode_GetCData(list.ppNodes[0]), "Found no attrib string for IntDir in %s", m_FullProjectFileName);
		strcpy(m_ObjectFileDir, XMLNode_GetCData(list.ppNodes[0]));
	}
	RemoveTrailingWhiteSpace(m_ObjectFileDir);
	RemoveLeadingWhiteSpace(m_ObjectFileDir);
	STATICASSERTF(strlen(m_ObjectFileDir), "Found no attrib string for IntDir in %s", m_FullProjectFileName);
	ReplacePlatformAndConfigurationMacros_2010(m_ObjectFileDir);
	PutSlashAtEndOfString(m_ObjectFileDir);
	

	GetVariableFromProjFileAndPropertySheets(m_AdditionalIncludeDirs, pRootNode, pPropertySheetNodes, iNumPropertySheets, 
		"Project.ItemDefinitionGroup.CLCompile.AdditionalIncludeDirectories", "AdditionalIncludeDirectories");

	GetVariableFromProjFileAndPropertySheets(m_PreprocessorDefines, pRootNode, pPropertySheetNodes, iNumPropertySheets, 
		"Project.ItemDefinitionGroup.CLCompile.PreprocessorDefinitions", "PreprocessorDefinitions");

	ReplaceCharWithChar(m_PreprocessorDefines, ',', ';');

	XMLNode_Destroy(pRootNode);

	for (i=0; i < iNumPropertySheets; i++)
	{	
		XMLNode_Destroy(pPropertySheetNodes[i]);
	}

}


void SourceParser::ProcessProjectFile()
{
	if (StringEndsWith(m_FullProjectFileName, ".vcxproj"))
	{
		ProcessProjectFile_VS2010();
		return;
	}

	char fullConfigName[100];
	char propertySheetNames[512] = "";

	Tokenizer tokenizer;

	bool bResult = tokenizer.LoadFromFile(m_FullProjectFileName);
		
	STATICASSERT(bResult, "Couldn't open project file");

	//set reservedwords used for parsing through .vcproj file
	tokenizer.SetExtraReservedWords(sProjectReservedWords, &spProjectReservedWordTree);

	Token token;
	enumTokenType eType;


	bool bFoundConfiguration = false;
	bool bFoundIntermediateDirectory = false;

	sprintf(fullConfigName, "%s|%s", m_pCurConfiguration, m_pCurTarget);
	m_AdditionalIncludeDirs[0] = 0;
	m_PreprocessorDefines[0] = 0;
	m_ObjectFileDir[0] = 0;

	char compilerToolName[256] = "";

	do
	{
		eType = tokenizer.GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_CONFIGURATION)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_NAME, "Didn't find name after configuration");
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after name");
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find string after =");

			if (strcmp(token.sVal, fullConfigName) == 0)
			{
				ASSERT(&tokenizer,!bFoundConfiguration, "Found configuration more than once");

				bFoundConfiguration = true;

				char toolName[300] = "";

				do
				{
					eType = tokenizer.GetNextToken(&token);

					ASSERT(&tokenizer,eType != TOKEN_NONE, "EOF found in middle of configuration");


					if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_PROPERTYSHEETS)
					{
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after propertysheets");
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 512, "Didn't find string after =");
						strcpy(propertySheetNames, token.sVal);
					}
					if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_TOOL)
					{
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_NAME, "Didn't find name after Tool");
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after name");
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find string after =");

						strcpy(toolName, token.sVal);

						if (strstr(toolName, "LinkerTool"))
						{
							Token nextToken;
							enumTokenType eNextType;

							eNextType = tokenizer.CheckNextToken(&nextToken);

							if (!(eNextType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_SLASH))
							{
								m_bIsAnExecutable = true;
							}
						}

						if (strcmp(toolName, "VCCLCompilerTool") == 0 || strcmp(toolName, "VCCLX360CompilerTool") == 0)
						{
							strcpy(compilerToolName, toolName);
						}
					} 
					else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ADDITIONALINCLUDEDIRECTORIES)
					{
						if (strcmp(toolName, "VCCLCompilerTool") == 0 || strcmp(toolName, "VCCLX360CompilerTool") == 0)
						{
							if (m_AdditionalIncludeDirs[0] == 0)
							{
								tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after AdditionalIncludeDirectories");
								tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find string after =");
								strcpy(m_AdditionalIncludeDirs, token.sVal);
								GetAdditionalStuffFromPropertySheets(m_AdditionalIncludeDirs, propertySheetNames, toolName, RW_ADDITIONALINCLUDEDIRECTORIES);
							}
						}
					}
					else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_PREPROCESSORDEFINITIONS)
					{
						if (strcmp(toolName, "VCCLCompilerTool") == 0 || strcmp(toolName, "VCCLX360CompilerTool") == 0)
						{
							if (m_PreprocessorDefines[0] == 0)
							{
								tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after PreprocessorDefinitions");
								tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find string after =");
								strcpy(m_PreprocessorDefines, token.sVal);
								GetAdditionalStuffFromPropertySheets(m_PreprocessorDefines, propertySheetNames, toolName, RW_PREPROCESSORDEFINITIONS);

								//mixed commas and semicolons screw things up... use all semicolons
								ReplaceCharWithChar(m_PreprocessorDefines, ',', ';');
							}
						}
					}
					else if (eType == TOKEN_RESERVEDWORD && (token.iVal == RW_OUTPUTDIRECTORY || token.iVal == RW_OBJECTFILE || token.iVal == RW_INTERMEDIATEDIRECTORY))
					{
						//an "intermediateDirectory" token is higher priority than "outputdirectory" and/or "objectfile", 
						//which are basically obsolete
						if (token.iVal == RW_INTERMEDIATEDIRECTORY)
						{
							bFoundIntermediateDirectory = true;
						}
						else
						{
							if (bFoundIntermediateDirectory)
							{
								continue;
							}
						}
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after outputdirectory or objectfile");
						tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find string after =");
						strcpy(m_ObjectFileDir, token.sVal);

						char *sTempMacros[][2] =
						{
							{
								"$(ConfigurationName)",
								m_pCurConfiguration,
							},
							{
								"$(PlatformName)",
								m_pCurTarget,
							},
							{
								NULL,
								NULL
							}
						};

						ReplaceMacrosInPlace(m_ObjectFileDir, sTempMacros);

						
						PutSlashAtEndOfString(m_ObjectFileDir);
					}
				}
				while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_CONFIGURATION));
			}
			else
			{
				tokenizer.GetTokensUntilReservedWord((enumReservedWordType)RW_CONFIGURATION);
			}
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_FILE)
		{

			eType = tokenizer.GetNextToken(&token);

			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RELATIVEPATH)
			{
				tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Didn't find = after reservedword");
				tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find filename after =");

				//check for the two autogen files that are supposed to be included
				CheckForRequiredFiles(token.sVal);

				int len = (int)strlen(token.sVal);	

				if ((len >= 3 && token.sVal[len - 2] == '.' && (token.sVal[len - 1] == 'h' || token.sVal[len - 1] == 'c')))
				{

					char sourceFileName[256];
					sprintf(sourceFileName, "%s\\%s", m_ProjectPath, token.sVal);

					STATICASSERT(m_iNumProjectFiles < MAX_FILES_IN_PROJECT, "Too many files in project");
					
					if (!ShouldFileBeExcluded(sourceFileName))
					{
						strcpy(m_ProjectFiles[m_iNumProjectFiles++], sourceFileName);
					}
				}
			}
		}
	} while (eType != TOKEN_NONE);

	if (m_PreprocessorDefines[0] == 0 && compilerToolName)
	{
		GetAdditionalStuffFromPropertySheets(m_PreprocessorDefines, propertySheetNames, compilerToolName, RW_PREPROCESSORDEFINITIONS);
		ReplaceCharWithChar(m_PreprocessorDefines, ',', ';');
	}

	if (m_AdditionalIncludeDirs[0] == 0 && compilerToolName)
	{
		GetAdditionalStuffFromPropertySheets(m_AdditionalIncludeDirs, propertySheetNames, compilerToolName, RW_ADDITIONALINCLUDEDIRECTORIES);
	}


	ASSERT(&tokenizer,bFoundConfiguration, "never found configuration");

}

bool SourceParser::NeedToUpdateFile(char *pFileName, int iExtraData, bool bForceUpdateUnlessFileDoesntExist)
{
	HANDLE hFile;

	hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return true;
	}
	else 
	{
		if (bForceUpdateUnlessFileDoesntExist)
		{
			CloseHandle(hFile);
			return true;
		}
		else
		{	
			bool bNeedToUpdate = false;

			if (m_pFileListLoader->IsFileInList(pFileName))
			{
				FILETIME mainFileTime;

				GetFileTime(hFile, NULL, NULL, &mainFileTime);

				if (CompareFileTime(&mainFileTime, m_pFileListLoader->GetMasterFileTime()) == 1)
				{
					bNeedToUpdate = true;
				}
				else if (iExtraData)
				{
					int i;

					for (i=0; i < m_iNumSourceParsers; i++)
					{
						if (iExtraData & ( 1 << i))
						{
							if (m_pSourceParsers[i]->DoesFileNeedUpdating(pFileName))
							{
								bNeedToUpdate = true;
							}
						}
					}
				}
			}
			else
			{
				bNeedToUpdate = true;
			}

			CloseHandle(hFile);

			return bNeedToUpdate;
		}
	}
}
void SourceParser::MakeAutoGenDirectory()
{
	char dirName[1024];
	char commandString[1024];

	sprintf(dirName, "%sAutoGen", m_ProjectPath);
	if (!dirExists(dirName))
	{
		sprintf(commandString, "mkdir \"%s\"", dirName);
		system(commandString);
	}

	sprintf(dirName, "%s\\..\\Common\\AutoGen", m_ProjectPath);
	if (!dirExists(dirName))
	{
		sprintf(commandString, "mkdir \"%s\"", dirName);
		system(commandString);
	}
}



bool SourceParser::DoMasterFilesExist()
{
	char fileName[MAX_PATH];
	FILE *pFile;

	sprintf(fileName, "%sAutoGen\\%s", m_ProjectPath, m_AutoGenFile1Name);

	pFile = fopen(fileName, "rt");
	if(!pFile)
	{
		return false;
	}
	fclose(pFile);

	sprintf(fileName, "%sAutoGen\\%s", m_ProjectPath, m_AutoGenFile2Name);

	pFile = fopen(fileName, "rt");
	if(!pFile)
	{
		return false;
	}
	fclose(pFile);

	return true;
}



void SourceParser::PrepareMasterFiles(bool bForceBuildAll)
{
	char fileName[MAX_PATH];
	FILE *pFile;
	int i;
	sprintf(fileName, "%sAutoGen\\%s", m_ProjectPath, m_AutoGenFile1Name);

	pFile = fopen_nofail(fileName, "wt");

	fprintf(pFile, "//autogenerated""nocheckin  this file is autogenerated by structparser, and includes all other autogenerated .c files\n");

	fprintf(pFile, "GCC_SYSTEM\n#pragma warning(default:4242)\n\n#include \"cmdparse.h\"\n#include \"textparser.h\"\n#include \"globaltypes.h\"\n#include \"memorybudget.h\"\n");

	fprintf(pFile, "//extra define of assertmsg for weird projects like crashrptDLL\n#if !defined(assertmsg) \n#define assertmsg(x,y) assert(x)\n#endif\n\n");

	for (i=0; i < m_iNumSourceParsers; i++)
	{
		char *pFileName = m_pSourceParsers[i]->GetAutoGenCFileName1();

		if (pFileName)
		{
			ForceIncludeFile(pFile, pFileName, "#ifdef THIS_SYMBOL_IS_NOT_DEFINED");
		}
	}

	if (MakeSpecialAutoRunFunction())
	{
		char SVNBranch[128] = "";
		int iBuildVersion = GetSVNVersionAndBranch(m_FullProjectFileName, SVNBranch, false);
		fprintf(pFile, "//special internal auto-run function that does global fixup and setup stuff\n");
		fprintf(pFile, "extern int gBuildVersion;\n");
		fprintf(pFile, "extern void SetBuildBranch(char *pBranchName);\n");
		fprintf(pFile, "//If this assert hits, it's because someone is linking together libraries and executables from different\n//SVN branches. Somehow.\n");
		fprintf(pFile, "void %s(void) { if (%d > gBuildVersion) gBuildVersion = %d; SetBuildBranch(\"%s\");}\n", m_SpecialAutoRunFuncName, iBuildVersion, iBuildVersion, SVNBranch);
	}
//	fprintf(pFile, "STATIC_ASSERT(__LINE__ < 65535) //If compile fails on this line, the file is too long and won't compile on XBOX. Talk to Alex.\n");

	fclose(pFile);

    //YVS
    const bool ps3 = !strcmp(m_pCurTarget, "PS3") || strstr(m_pCurConfiguration,"PS3 ")==m_pCurConfiguration;
	if(!ps3)
	if ( gbLastFWCloseActuallyWrote )
	{
		CompileCFile(m_AutoGenFile1Name, "AutoGen", false);
	}
	
	sprintf(fileName, "%sAutoGen\\%s", m_ProjectPath, m_AutoGenFile2Name);

	pFile = fopen_nofail(fileName, "wt");

	fprintf(pFile, "//autogenerated""nocheckin  this file is autogenerated by structparser, and includes secondary .c files\n");



	for (i=0; i < m_iNumSourceParsers; i++)
	{
		char *pFileName = m_pSourceParsers[i]->GetAutoGenCFileName2();

		if (pFileName)
		{
			ForceIncludeFile(pFile, pFileName, "#ifdef THIS_SYMBOL_IS_NOT_DEFINED");
		}
	}

	fclose(pFile);


	//if any commands changed in this or any library, we want to recompile autogen_2.c. This is because of the crazy
	//web of exprCode dependencies
	if(!ps3)
	if (SourceParserChangedInAnyDependentLibrary(SOURCEPARSERINDEX_MAGICCOMMANDMANAGER) || gbLastFWCloseActuallyWrote )
	{
		CompileCFile(m_AutoGenFile2Name, "AutoGen", false);
	}

}

void SourceParser::CreateCleanBuildMarkerFile(void)
{
	char fileName[MAX_PATH];

	char fullDirString[MAX_PATH];
	char systemString[1024];

	sprintf(fullDirString, "%s%s", m_ProjectPath, m_ObjectFileDir);

	RemoveSuffixIfThere(fullDirString, "/");
	RemoveSuffixIfThere(fullDirString, "\\");
	
	if (!dirExists(fullDirString))
	{
		sprintf(systemString, "md \"%s\"  > NUL 2>&1", fullDirString);

		system(systemString);
	} 


	sprintf(fileName, "%s%sTHIS_FILE_CHECKS_FOR_CLEAN_BUILDS.obj", 
		m_ProjectPath, m_ObjectFileDir);


	FILE *pFile = fopen(fileName, "wt");


	if (pFile)
	{
		fprintf(pFile, "This file exists so that structparser will know when a clean build happens");
		fclose(pFile);
	}
}

bool SourceParser::DidCleanBuildJustHappen()
{
	char fileName[MAX_PATH];

	sprintf(fileName, "%s\\%s\\THIS_FILE_CHECKS_FOR_CLEAN_BUILDS.obj", 
		m_ProjectPath, m_pFileListLoader->GetObjFileDirectory());

	HANDLE hFile;

	hFile = CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return true;;
	}
	
	CloseHandle(hFile);

	return false;
}



void SourceParser::CleanOutAllAutoGenFiles(char *pProjectDir, char *pShortProjectFileName)
{
	char tempString[256];
	sprintf(tempString, "del /Q \"%sAutoGen\\*.*\" 2>nul", pProjectDir);
	system(tempString);
	
	sprintf(tempString, "del /Q \"%swiki\\*.*\" 2>nul", pProjectDir);
	system(tempString);

	sprintf(tempString, "del /Q \"%s..\\Common\\AutoGen\\%s_*.*\" 2>nul", pProjectDir, pShortProjectFileName);
	system(tempString);
}

bool SourceParser::IsQuickExitPossible()
{
	HANDLE hFile;
	FILETIME fileTime;

	hFile = CreateFile(m_FullProjectFileName, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}
		
	GetFileTime(hFile, NULL, NULL, &fileTime);
	CloseHandle(hFile);

	if (CompareFileTime(&fileTime, m_pFileListLoader->GetMasterFileTime()) == 1)
	{
		m_bProjectFileChanged = true;
		return false;
	}




	hFile = CreateFile(m_SolutionPath, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}
		
	GetFileTime(hFile, NULL, NULL, &fileTime);
	CloseHandle(hFile);

	if (CompareFileTime(&fileTime, m_pFileListLoader->GetMasterFileTime()) == 1)
	{
		return false;
	}



	int i;
	int iNumFiles = m_pFileListLoader->GetNumFiles();

	for (i=0; i < iNumFiles; i++)
	{
		if (!m_pFileListLoader->GetNthFileExists(i))
		{
			return false;
		}

		if (CompareFileTime(m_pFileListLoader->GetNthFileTime(i), m_pFileListLoader->GetMasterFileTime()) == 1)
		{
			return false;
		}
	}

	//if any dependent library had a command change then we can't quick-exit because we need to 
	//recompile the autogen_2.c file
	if (SourceParserChangedInAnyDependentLibrary(SOURCEPARSERINDEX_MAGICCOMMANDMANAGER))
	{
		return false;
	}

	//project file and solution file and all project files are unchanged, quit
	TRACE("Project file, solution file, and all project files are unchanged... quitting\n");
	return true;
}


void SourceParser::ParseSource(char *pProjectPath, char *pProjectFileName, char *pCurTarget, char *pCurConfiguration, char *pCurVCDir, char *pSolutionPath, char *pExecutable,
							   SourceParserRecursionContext *pRecursionContext)
{



	m_pRecursionContext = pRecursionContext;


	sprintf(m_FullProjectFileName, "%s\\%s", pProjectPath, pProjectFileName);
	strcpy(m_ProjectPath, pProjectPath);
	strcpy(m_ShortenedProjectFileName,pProjectFileName);
	strcpy(m_SolutionPath, pSolutionPath);
	strcpy(m_Executable, pExecutable);

	strcpy(m_SolutionDir, pSolutionPath);
	TruncateStringAfterLastOccurrence(m_SolutionDir, '\\');


	m_pCurTarget = pCurTarget;
	m_pCurConfiguration = pCurConfiguration;
	m_pCurVCDir = pCurVCDir;

	TruncateStringAtLastOccurrence(m_ShortenedProjectFileName, '.');

	//sets up the SUPER-fast exit for the second call during a full solution build
	if (pRecursionContext)
	{
		WriteRecursionMarkerFile();
	}
	else
	{
		CheckRecursionMarkerFile();
	}

	//if the project file name passed in has "_donotc heckin" stuck on the end of it, then it's a temporary
	//project file generated by Conor's weird auto-build, and we should ignore the _donot checkin
	RemoveSuffixIfThere(m_ShortenedProjectFileName, "_donot""checkin");


	char listFileName[MAX_PATH];
	char shortListFileName[MAX_PATH];
	bool bForceReadAllFiles = false; 
	bool bCheckForQuickExit = false;

	

	sprintf(shortListFileName, "%s", m_ShortenedProjectFileName);
	MakeStringAllAlphaNum(shortListFileName);


	sprintf(listFileName, "%s\\%s.SPFileList", pProjectPath, shortListFileName);
	sprintf(m_AutoGenFile1Name, "%s_AutoGen_1.c", m_ShortenedProjectFileName);
	sprintf(m_AutoGenFile2Name, "%s_AutoGen_2.c", m_ShortenedProjectFileName);
	sprintf(m_SpecialAutoRunFuncName, "_%s_AutoRun_SPECIALINTERNAL", m_ShortenedProjectFileName);

	bool bNeedToRecurseOnAllOtherProjects = (_stricmp(m_ShortenedProjectFileName, STRUCTPARSERSTUB_PROJ) == 0);

	if (gDoMasterWikiCreation)
	{
		bForceReadAllFiles = true;
	}

	TRACE("About to start parsing... project %s config %s\n", m_ShortenedProjectFileName, m_pCurConfiguration);

	if (!m_pFileListLoader->LoadFileList(listFileName))
	{
		TRACE("Couldn't load spfilelist file... doing full rebuild\n");
		bForceReadAllFiles = true;
	}
	else
	{
		if (!bForceReadAllFiles)
		{
			if (!DoMasterFilesExist())
			{
				TRACE("Master files don't exist... doing full rebuild\n");
				bForceReadAllFiles = true;
			}
			else if (DidCleanBuildJustHappen())
			{
				TRACE("Clean build happened... doing full rebuild\n");
				bForceReadAllFiles = true;
				m_bCleanBuildHappened = true;
			}
			else
			{
				//in unsafe mode we quit now before loading the solution file. This means that if
				//someone changed a command which added an exprCode in one of the libraries we depend on, we won't know,
				//and we will crash at startup time until recompiling occurs
				if (!bNeedToRecurseOnAllOtherProjects && !SlowSafeDependencyMode())
				{
					if (IsQuickExitPossible())
					{
						return;
					}

					TRACE("Not doing quick exit... something must have changed\n");



				}
				else
				{
					bCheckForQuickExit = true;
					TRACE("Quick exit might be possible... we will check after processings solution file\n");
				}
			}
		}
	
	
	}




	MakeAutoGenDirectory();


	if (bNeedToRecurseOnAllOtherProjects)
	{
		TRACE("This appears to be the StructParserStub project, so all we'll do is recursively\ncall structparser on other projects\n");
	}

	FindVariablesFileAndLoadVariables();


	ProcessSolutionFile(bNeedToRecurseOnAllOtherProjects, bForceReadAllFiles);

	if (bCheckForQuickExit)
	{
		if (IsQuickExitPossible())
		{
			return;
		}

		TRACE("Not doing quick exit... something must have changed\n");
	}

	if (m_bProjectFileChanged)
	{
		bForceReadAllFiles = true;
	}

	// Spawn ProjectManager -massage to fix up the project file
	//
	//if we did quickexit, we know project file hasn't changed
	char massageCommand[2048];
	int massageRet;
	sprintf(massageCommand, "%s/ProjectManager.exe -massage \"%s\"", getExecutableDir(), m_FullProjectFileName);
	if (0 != (massageRet=system(massageCommand)))
	{
		printf("Error: return value %d from \"%s\"\n", massageRet, massageCommand);
	}

	if (StringEndsWith(m_FullProjectFileName, ".vcxproj"))
	{
		// VS2010, also hit filters
		char filters_name[MAX_PATH];
		sprintf(filters_name, "%s.filters", m_FullProjectFileName);
		if (FileExists(filters_name))
		{
			sprintf(massageCommand, "%s/ProjectManager.exe -massage \"%s\"", getExecutableDir(), filters_name);
			if (0 != (massageRet=system(massageCommand)))
			{
				printf("Error: return value %d from \"%s\"\n", massageRet, massageCommand);
			}
		}
	}

	CreateParsers();


	ProcessProjectFile();


	CreateCleanBuildMarkerFile();




	bool bAtLeastOneFileUpdated = false;




	


	int i;
	
	for (i=0; i < m_iNumSourceParsers; i++)
	{
		m_pSourceParsers[i]->SetParent(this, i);
		m_pSourceParsers[i]->SetProjectPathAndName(pProjectPath, m_ShortenedProjectFileName);
	}

	
	if (!m_IdentifierDictionary.SetFileNameAndLoad(pProjectPath, m_ShortenedProjectFileName))
	{
		TRACE("Couldn't load identifier dictionary... forcing read all files\n");
		bForceReadAllFiles = true;
	}


	for (i=0; i < m_iNumSourceParsers; i++)
	{		
		if (!m_pSourceParsers[i]->LoadStoredData(bForceReadAllFiles))
		{
			TRACE("Couldn't load stored data %d, forcing read all files\n", i);
			bForceReadAllFiles = true;
		}
	
	}
	
	if (MakeSpecialAutoRunFunction())
	{
		//make sure AutoRunManager has magic internal autorun
		GetAutoRunManager()->AddAutoRunSpecial(m_SpecialAutoRunFuncName, "_SPECIAL_INTERNAL", true, AUTORUN_ORDER_FIRST);
	}
	else
	{
		GetAutoRunManager()->ResetSourceFile("_SPECIAL_INTERNAL");
	}


	//must be after LoadStoredData
	LoadSavedDependenciesAndRemoveObsoleteFiles();

	for (i = 0 ; i < m_iNumProjectFiles; i++)
	{
		bAtLeastOneFileUpdated |= m_bFilesNeedToBeUpdated[i] |= NeedToUpdateFile(m_ProjectFiles[i], m_iExtraDataPerFile[i], bForceReadAllFiles);
	}

/*	if (!bAtLeastOneFileUpdated && !bForceReadAllFiles)
	{
		return;
	}*/





	if (bForceReadAllFiles)
	{
		ProcessAllFiles_ReadAll();
	}
	else
	{
		ProcessAllFiles();

	}

	bool bMasterFilesChanged = false;
	for (i=0; i < m_iNumSourceParsers; i++)
	{
		bMasterFilesChanged |= m_pSourceParsers[i]->WriteOutData();
	}

	m_IdentifierDictionary.WriteOutFile();

	m_pFileListWriter->OpenFile(listFileName, m_ObjectFileDir);

	for (i = 0 ; i < m_iNumProjectFiles; i++)
	{
		m_pFileListWriter->AddFile(m_ProjectFiles[i], m_iExtraDataPerFile[i], m_iNumDependencies[i], m_iDependencies[i]);
	}

	m_pFileListWriter->CloseFile();


	if ((bAtLeastOneFileUpdated && bMasterFilesChanged) || bForceReadAllFiles
		|| SourceParserChangedInAnyDependentLibrary(SOURCEPARSERINDEX_MAGICCOMMANDMANAGER))
	{
		PrepareMasterFiles(bForceReadAllFiles);
	}

	if (m_pRecursionContext)
	{
		SourceParserWhatThingsChangedTracker *pThingsChangedTracker = &m_pRecursionContext->thingsChangedTrackers[m_pRecursionContext->iNumThingsChangedTrackers++];
	
		sprintf(pThingsChangedTracker->projName, GetShortProjectName());

		pThingsChangedTracker->changeBits = 0;

		//if we recursed, we want to specify which of our base source parsers had something change
		for (i = 0; i < m_iNumSourceParsers; i++)
		{
			if (m_pSourceParsers[i]->m_bSomethingChanged)			
			{
				pThingsChangedTracker->changeBits |= (1 << i);
			}
		}
	}

	if (!bNeedToRecurseOnAllOtherProjects && !(m_FoundAutoGenFile1 && m_FoundAutoGenFile2))
	{
		char errorString[1024];
		sprintf(errorString, "Didn't find %s and %s. They have now been created and must be included in the project file",
			m_AutoGenFile1Name, m_AutoGenFile2Name);
		STATICASSERT(0, errorString);
	}

}

static char **sppSourceMagicWords = NULL;
static StringTree *spSourceMagicWordTree = NULL;
/*
void StringTree_Test(void)
{
	int iVal;
	StringTree *pTree = StringTree_Create();
	StringTree_AddWord(pTree, "FOO_WAKKA", 3);
	StringTree_AddPrefix(pTree, "FOO_", 1);
	StringTree_AddWord(pTree, "FOO_BAR", 2);

	iVal = StringTree_CheckWord(pTree, "FOO_WAKKA");
	iVal = StringTree_CheckWord(pTree, "FOO_DFDF");
	iVal = StringTree_CheckWord(pTree, "FOO_BAR");
	iVal = StringTree_CheckWord(pTree, "FOO_");
	iVal = StringTree_CheckWord(pTree, "bob");
}
*/

void SourceParser::ScanSourceFile(char *pSourceFile)
{
//	StringTree_Test();

	if (strstri(pSourceFile, "uicolor.c"))
	{
		int iBrk = 0;
	}

	TRACE("Parsing %s\n", pSourceFile);

	if (!sppSourceMagicWords)
	{
		sppSourceMagicWords = (char**)calloc(sizeof(char*) * (m_iNumSourceParsers * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + 1), 1);
		spSourceMagicWordTree = StringTree_Create();
	
		sppSourceMagicWords[m_iNumSourceParsers * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER] = NULL;

		int i, j;

		for (i=0; i < m_iNumSourceParsers; i++)
		{
			for (j=0; j < MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER; j++)
			{
				sppSourceMagicWords[i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j] = m_pSourceParsers[i]->GetMagicWord(j);

				if (StringContainsWildcards(sppSourceMagicWords[i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j]))
				{
					char *pTemp = strdup(sppSourceMagicWords[i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j]);
					int iTempLen = (int)strlen(pTemp);
					pTemp[iTempLen - 26] = 0;
					if (!StringTree_CheckWord(spSourceMagicWordTree, pTemp))
					{
						StringTree_AddPrefix(spSourceMagicWordTree, pTemp, i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j + RW_COUNT);
					}				
					free(pTemp);
				}
				else
				{
					if (!StringTree_CheckWord_IgnorePrefixes(spSourceMagicWordTree, sppSourceMagicWords[i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j]))
					{
						StringTree_AddWord(spSourceMagicWordTree, sppSourceMagicWords[i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j], i * MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER + j + RW_COUNT);
					}
				}
			}
		}
	}

	Tokenizer tokenizer;

	bool bResult = tokenizer.LoadFromFile(pSourceFile);

	if (!bResult)
	{
		char errorString[256];
		sprintf(errorString, "Couldn't find file %s\n", pSourceFile);
		STATICASSERT(0, errorString);
	}

	tokenizer.SetCSourceStyleStrings(true);
	tokenizer.SetNoNewlinesInStrings(true);
	tokenizer.SetSkipDefines(true);
	tokenizer.SetUsesIfDefStack(true);
	tokenizer.SetCheckForInvisibleTokens(true);

	Token token;
	enumTokenType eType;
	int i;

	for (i=0; i < m_iNumSourceParsers; i++)
	{
		m_pSourceParsers[i]->FoundMagicWord(pSourceFile, &tokenizer, MAGICWORD_BEGINNING_OF_FILE, NULL);
	}
	
	tokenizer.SetExtraReservedWords(sppSourceMagicWords, &spSourceMagicWordTree);

	do
	{
		eType = tokenizer.GetNextToken(&token);

		if (strstri(pSourceFile, "uicolor.c"))
		{
			TRACE("Token: %s (%d:%d) ", token.sVal, token.eType, token.iVal);
			tokenizer.StringifyToken(&token);
			TRACE(" Stringified: %s\n", token.sVal);
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal >= RW_COUNT)
		{
			int iMagicWordNum = token.iVal - RW_COUNT;
			m_pSourceParsers[iMagicWordNum / MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER]->FoundMagicWord(pSourceFile, &tokenizer, iMagicWordNum % MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER, token.sVal);
			tokenizer.SetExtraReservedWords(sppSourceMagicWords, &spSourceMagicWordTree);
		}
	} 
	while (eType != TOKEN_NONE);

	for (i=0; i < m_iNumSourceParsers; i++)
	{
		m_pSourceParsers[i]->FoundMagicWord(pSourceFile, &tokenizer, MAGICWORD_END_OF_FILE, NULL);
	}

	if (!gDontCheckForBadIfsInCode)
	{
		bool bDone = false;
		tokenizer.SetOffset(0, 1);
		int iParenDepth;

		while (!bDone)
		{
			eType = tokenizer.GetNextToken(&token);

			if (eType == TOKEN_NONE)
			{
				bDone = true;
				break;
			}

			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_IF)
			{
				int iIfLine = tokenizer.GetCurLineNum();

				eType = tokenizer.CheckNextToken(&token);

				// some weird syntax like # if or something. Don't worry about it)
				if (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS))
				{
					//do nothing, don't go into our paren checking loop
				}
				else
				{
					tokenizer.GetNextToken(&token);
					iParenDepth = 1;

					while (!bDone)
					{
						eType = tokenizer.GetNextToken(&token);
						if (eType == TOKEN_NONE)
						{
							tokenizer.AssertFailedf("Got end of file while checking for badly formed if command on line %d", iIfLine);
							bDone = true;
							break;
						}

						if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
						{
							iParenDepth++;
						}
						else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
						{
							iParenDepth--;
							if (iParenDepth == 0)
							{
								bDone = true;
								eType = tokenizer.CheckNextToken(&token);
								if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON)
								{
									tokenizer.AssertFailedf("if statement on line %d has a semicolon after its condition, this is almost certainly wrong", iIfLine);
								}
								break;
							}
						}
					}
				}
			}
		}
	}




}

void GetStringWithSeparator(char *pOutString, char **ppInString, char separator)
{

	*pOutString = 0;

	while (**ppInString != 0 && **ppInString != separator)
	{
		*pOutString = **ppInString;
		pOutString++;
		(*ppInString)++;
	}

	if (**ppInString == separator)
	{
		(*ppInString)++;
	}

	*pOutString = 0;
}



void PutThingsIntoCommandLine(char *pCommandLine, char *pInputString, char *pPrefixString, bool bStripTrailingSlashes)
{
	int commandLineLength = (int) strlen(pCommandLine);


	do
	{
		char includeDirString[TOKENIZER_MAX_STRING_LENGTH];
	
		//ignore all leading semicolons
		while (pInputString[0] == ';')
		{
			pInputString++;
		}

		GetStringWithSeparator(includeDirString, &pInputString, ';');

		if (includeDirString[0] == 0)
		{
			break;
		}
	
		if (bStripTrailingSlashes)
		{
			RemoveSuffixIfThere(includeDirString, "\\");
			RemoveSuffixIfThere(includeDirString, "/");
		}

		int tempLen = (int)strlen(includeDirString);

		sprintf(pCommandLine + commandLineLength, "%s \"%s\" " ,pPrefixString, includeDirString);

		commandLineLength = (int)strlen(pCommandLine);
	} while (1);
}




void SourceParser::CompileCFile(char *pFileName, char *pRelativePath, bool bIsCPPFile)
{
/*	FILE *pBatchFile;
	
	char shortName[MAX_PATH];
	char batName[MAX_PATH];

	sprintf(shortName, "comp_%s_%s", pFileName, pRelativePath);
	MakeStringAllAlphaNum(shortName);
	sprintf(batName, "%s%s.bat",  GetProjectPath(), shortName);

	pBatchFile = fopen_nofail(batName, "wt");

	fprintf(pBatchFile, "@pushd %s > NUL 2>&1\n", m_ProjectPath);

	if (strcmp(m_pCurTarget, "Xenon") == 0 || _stricmp(m_pCurTarget, "Xbox 360")==0)
	{
		char xedk[1000];

		if (!GetEnvironmentVariable("XEDK", xedk, 1000))
		{
			STATICASSERT(0, "ERROR: Environment variable XEDK not set\n");
		}

		fprintf(pBatchFile, "@call \"%s\\bin\\win32\\xdkvars\"  > NUL 2>&1\n", xedk);
		fprintf(pBatchFile, "@pushd %s > NUL 2>&1\n", m_ProjectPath);
	}
	else if (strcmp(m_pCurTarget, "Win32") == 0)
	{

		//kludge for debugging purposes to remove trailing doublequote 
		int tempLen = (int)strlen(m_pCurVCDir);

		while (m_pCurVCDir[tempLen - 1] == '"' || m_pCurVCDir[tempLen - 1] == ' ')
		{
			m_pCurVCDir[tempLen - 1] = 0;
			tempLen--;
		}


		fprintf(pBatchFile, "@call \"%s\\bin\\vcvars32\" > NUL 2>&1\n", m_pCurVCDir);
	}
	else if (strcmp(m_pCurTarget, "x64") == 0)
	{
		fprintf(pBatchFile, "@call \"%s\\vcvarsall.bat\" x86_amd64 > NUL 2>&1\n", m_pCurVCDir);
	}
	else
	{
		STATICASSERT(0, "ERROR: Unknown target (not Xenon or Xbox 360 or Win32)\n");
	}

	{
		char tempString[MAX_PATH];
		strcpy(tempString, m_ObjectFileDir);
		char *pTemp = tempString + strlen(tempString) - 1;

		while (*pTemp == '/' || *pTemp == '\\')
		{
			*pTemp = 0;
			pTemp--;
		}

		fprintf(pBatchFile, "@mkdir \"%s%s\"  > NUL 2>&1\n", m_ProjectPath, tempString);
	}

	char BigCommandLine[TOKENIZER_MAX_STRING_LENGTH * 3];

	char fileNameWithoutExtension[MAX_PATH];
	sprintf(fileNameWithoutExtension, pFileName);
	TruncateStringAtLastOccurrence(fileNameWithoutExtension, '.');

	sprintf(BigCommandLine, "@cl /FI \"memcheck.h\" /nologo \"/Fo%s%s.obj\" /c /Z7 /WX ", m_ObjectFileDir, fileNameWithoutExtension);
//	sprintf(BigCommandLine, "@cl /FI \"memcheck.h\" /nologo \"/Fo%s%s.obj\" /c /ZI \"/Fd%svc80.pdb\" ", m_ObjectFileDir, fileNameWithoutExtension, m_ObjectFileDir);


	PutThingsIntoCommandLine(BigCommandLine, m_AdditionalIncludeDirs, "/I", true);
	PutThingsIntoCommandLine(BigCommandLine, m_PreprocessorDefines, "/D", false);

	sprintf(BigCommandLine + strlen(BigCommandLine), " \"%s%s\\%s", m_ProjectPath, pRelativePath, pFileName);

	if (bIsCPPFile && strcmp(BigCommandLine + strlen(BigCommandLine) - 4, ".cpp") != 0)
	{
		sprintf(BigCommandLine + strlen(BigCommandLine), ".cpp");
	}
	
	if (!bIsCPPFile && strcmp(BigCommandLine + strlen(BigCommandLine) - 2, ".c") != 0)
	{
		sprintf(BigCommandLine + strlen(BigCommandLine), ".c");
	}
	sprintf(BigCommandLine + strlen(BigCommandLine),"\"");


	fprintf(pBatchFile, "%s\n", BigCommandLine);

	fclose(pBatchFile);



	//if we are being called recursively by another sourceparser, ie by structParserStub's, then we don't actually want to
	//compile our C files. They have to all be compiled at the end, so that the crazy-ass exprCode stuff will work. Therefore, we
	//don't compile our .bat file, and instead store it in our parent's recurse context, so our parent can do all the compiling
	if (m_pRecursionContext)
	{
		STATICASSERTF(m_pRecursionContext->iNumCompileBatchFiles < MAX_COMPILE_BATCH_FILES, "Too many batch files to compile... must be too many projects in one solution. Tell Alex!");

		strcpy(m_pRecursionContext->compileBatchFileNameKeyFiles[m_pRecursionContext->iNumCompileBatchFiles], m_IdentifierDictionary.GetDictionaryFileName());
		strcpy(m_pRecursionContext->compileBatchFileNames[m_pRecursionContext->iNumCompileBatchFiles++], batName);
	}
	else
	{

		char systemString[1024];

		sprintf(systemString, "%s > NUL", batName);

		int retVal = system(systemString);

		if (retVal)
		{
			//KLUDGE: if we get a compile error, run again without NUL redirection so that error message is visible
			system(batName);

			printf("ERROR::: Something went wrong while compiling %s\n", pFileName);
		
			fflush(stdout);
			BreakIfInDebugger();

			Sleep(100);

			exit(1);
		}
	}*/

}
		

		

void ReplaceMacroInPlace(char *pString, char *pMacroToFind, char *pReplaceString)
{
	int iMacroLength = (int)strlen(pMacroToFind);
	int iStringLength = (int)strlen(pString);
	int iReplaceLength = (int)strlen(pReplaceString);

	if (iStringLength < iMacroLength)
	{
		return;
	}

	int i;

	for (i=0; i <= iStringLength - iMacroLength; i++)
	{
		if (StringBeginsWith(pString+i, pMacroToFind, true))
		{
			memmove(pString + i + iReplaceLength, pString + i + iMacroLength, iStringLength - (i + iMacroLength) + 1);
			memcpy(pString + i, pReplaceString, iReplaceLength);

			iStringLength += iReplaceLength - iMacroLength;
			i += iReplaceLength - 1;

		}
	}
}

void ReplaceMacrosInPlace(char *pString, char *pMacros[][2])
{
	int i;

	for (i=0; pMacros[i][0]; i++)
	{
		ReplaceMacroInPlace(pString, pMacros[i][0], pMacros[i][1]);
	}
}



//loads in all the dependencies that are stored in the FileListLoader. If one of the two dependent files doesn't exist, sets the other file
//to update. If both exist, store the dependency. Then all the stored dependencies will be processed
void SourceParser::LoadSavedDependenciesAndRemoveObsoleteFiles(void)
{
	int iNumSavedFiles = m_pFileListLoader->GetNumFiles();
	int iSavedFileNum;

	int iSavedFileNumToIndexArray[MAX_FILES_IN_PROJECT];


	for (iSavedFileNum=0; iSavedFileNum < iNumSavedFiles; iSavedFileNum++)
	{
		char *pSavedFileName = m_pFileListLoader->GetNthFileName(iSavedFileNum);

		iSavedFileNumToIndexArray[iSavedFileNum] = FindProjectFileIndex(pSavedFileName);

		if (iSavedFileNumToIndexArray[iSavedFileNum] == -1)
		{
			//this file no longer exists in the project
			m_IdentifierDictionary.DeleteAllFromFile(pSavedFileName);

			int i;

			for (i=0; i < m_iNumSourceParsers; i++)
			{
				m_pSourceParsers[i]->ResetSourceFile(pSavedFileName);
			}
		}
		else
		{
			m_iExtraDataPerFile[iSavedFileNumToIndexArray[iSavedFileNum]] = m_pFileListLoader->GetExtraData(iSavedFileNum);
		}
	}

	//now the iSavedFileNumToIndexArray is properly seeded

	for (iSavedFileNum=0; iSavedFileNum < iNumSavedFiles; iSavedFileNum++)
	{
		int iNumDependencies = m_pFileListLoader->GetNumDependencies(iSavedFileNum);

		int i;

		for (i=0; i < iNumDependencies; i++)
		{
			int iOtherFileNum = m_pFileListLoader->GetNthDependency(iSavedFileNum, i);

			//only process dependencies once, so only if otherFileNum > fileNum
			if (iOtherFileNum > iSavedFileNum)
			{
				int projFileIndex1 = iSavedFileNumToIndexArray[iSavedFileNum];
				int projFileIndex2 = iSavedFileNumToIndexArray[iOtherFileNum];

				if (projFileIndex1 == -1)
				{
					if (projFileIndex2 != -1)
					{
						m_bFilesNeedToBeUpdated[projFileIndex2] = true;
					}
				}
				else if (projFileIndex2 == -1)
				{
					m_bFilesNeedToBeUpdated[projFileIndex1] = true;
				}
				else
				{
					AddDependency(projFileIndex1, projFileIndex2);
				}
			}
		}
	}
}

//returns true if at least one file was set to udpate that was previously not set to update
//
//find all need-to-update files which have dependencies, and set all the other
//files they are dependent on to be need-to-update, and recurse. 
bool SourceParser::ProcessAllLoadedDependencies()
{
	bool bAtLeastOneSetToTrue = false;
	bool bNeedAnotherPass = true;

	while (bNeedAnotherPass)
	{
		bNeedAnotherPass = false;
		int iFileNum;

		for (iFileNum = 0; iFileNum < m_iNumProjectFiles; iFileNum++)
		{
			if (m_bFilesNeedToBeUpdated[iFileNum])
			{
				int i;

				for (i=0; i < m_iNumDependencies[iFileNum]; i++)
				{
					int iOtherFileNum = m_iDependencies[iFileNum][i];

					if (!m_bFilesNeedToBeUpdated[iOtherFileNum])
					{
						bAtLeastOneSetToTrue = true;
						if (iOtherFileNum < iFileNum)
						{
							bNeedAnotherPass = true;
						}

						m_bFilesNeedToBeUpdated[iOtherFileNum] = true;
					}
				}
			}
		}
	}

	return bAtLeastOneSetToTrue;
}

void SourceParser::ClearAllDependenciesForUpdatingFiles(void)
{
	int iFileNum;

	for (iFileNum = 0; iFileNum < m_iNumProjectFiles; iFileNum++)
	{
		if (m_bFilesNeedToBeUpdated[iFileNum])
		{
			m_iNumDependencies[iFileNum] = 0;
		}
	}
}

void SourceParser::AddDependency(int iFile1, int iFile2)
{
	int i;
	
	STATICASSERT(iFile1 != iFile2, "File can't depend on itself");
	
	bool bFound = false;
	for (i=0; i < m_iNumDependencies[iFile1]; i++)
	{
		if (m_iDependencies[iFile1][i] == iFile2)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		STATICASSERT(m_iNumDependencies[iFile1] < MAX_DEPENDENCIES_SINGLE_FILE, "Too many dependencies");

		m_iDependencies[iFile1][m_iNumDependencies[iFile1]++] = iFile2;
	}

	bFound = false;
	for (i=0; i < m_iNumDependencies[iFile2]; i++)
	{
		if (m_iDependencies[iFile2][i] == iFile1)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		STATICASSERT(m_iNumDependencies[iFile2] < MAX_DEPENDENCIES_SINGLE_FILE, "Too many dependencies");

		m_iDependencies[iFile2][m_iNumDependencies[iFile2]++] = iFile1;
	}
}


void SourceParser::ProcessAllFiles_ReadAll()
{
	ClearAllDependenciesForUpdatingFiles();
	
	int iFileNum;

	for (iFileNum=0; iFileNum < m_iNumProjectFiles; iFileNum++)
	{
		m_IdentifierDictionary.DeleteAllFromFile(m_ProjectFiles[iFileNum]);

		int i;

		for (i=0; i < m_iNumSourceParsers; i++)
		{
			m_pSourceParsers[i]->ResetSourceFile(m_ProjectFiles[iFileNum]);
		}

		m_iExtraDataPerFile[iFileNum] = 0;
	}

	for (iFileNum=0; iFileNum < m_iNumProjectFiles; iFileNum++)
	{
		ScanSourceFile(m_ProjectFiles[iFileNum]);
	}

	for (iFileNum=0; iFileNum < m_iNumProjectFiles; iFileNum++)
	{
		int i;

		for (i=0; i < m_iNumSourceParsers; i++)
		{
			char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE];

			int iNumDepenedencies = m_pSourceParsers[i]->ProcessDataSingleFile(m_ProjectFiles[iFileNum], pDependencies);

			int j;

			for (j=0; j < iNumDepenedencies; j++)
			{
				int iOtherFileNum = FindProjectFileIndex(pDependencies[j]);
				char errorString[1024];
				sprintf(errorString, "Dependency file <<%s>> not found (depended on by %s)", pDependencies[j], m_ProjectFiles[iFileNum]);
				
				STATICASSERT(iOtherFileNum != -1 && iOtherFileNum != iFileNum, errorString);

				AddDependency(iFileNum, iOtherFileNum);
			}
		}
	}
}

void SourceParser::ProcessAllFiles()
{

	ProcessAllLoadedDependencies();

	do
	{
		ClearAllDependenciesForUpdatingFiles();
		int iFileNum;


		for (iFileNum=0; iFileNum < m_iNumProjectFiles; iFileNum++)
		{
			if (m_bFilesNeedToBeUpdated[iFileNum])
			{
				m_IdentifierDictionary.DeleteAllFromFile(m_ProjectFiles[iFileNum]);

				int i;

				for (i=0; i < m_iNumSourceParsers; i++)
				{
					m_pSourceParsers[i]->ResetSourceFile(m_ProjectFiles[iFileNum]);
				}

				m_iExtraDataPerFile[iFileNum] = 0;

			}
		}

		for (iFileNum=0; iFileNum < m_iNumProjectFiles; iFileNum++)
		{
			if (m_bFilesNeedToBeUpdated[iFileNum])
			{
				ScanSourceFile(m_ProjectFiles[iFileNum]);
			}
		}

		for (iFileNum=0; iFileNum < m_iNumProjectFiles; iFileNum++)
		{
			if (m_bFilesNeedToBeUpdated[iFileNum])
			{
				int i;

				for (i=0; i < m_iNumSourceParsers; i++)
				{
					char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE];

					int iNumDependencies = m_pSourceParsers[i]->ProcessDataSingleFile(m_ProjectFiles[iFileNum], pDependencies);

					int j;

					for (j=0; j < iNumDependencies; j++)
					{
						int iOtherFileNum = FindProjectFileIndex(pDependencies[j]);
						char errorString[1024];
						sprintf(errorString, "Dependency file <<%s>> not found", pDependencies[j]);

						STATICASSERT(iOtherFileNum != -1 && iOtherFileNum != iFileNum, errorString);

						AddDependency(iFileNum, iOtherFileNum);
					}
				}
			}
		}
	}
	while (ProcessAllLoadedDependencies());
}


int SourceParser::FindProjectFileIndex(char *pFileName)
{
	int i;

	for (i=0; i < m_iNumProjectFiles; i++)
	{
		if (AreFilenamesEqual(pFileName, m_ProjectFiles[i]))
		{
			return i;
		}
	}

	return -1;
}


void SourceParser::SetExtraDataFlagForFile(char *pFileName, int iFlag)
{
	int iIndex = FindProjectFileIndex(pFileName);

	STATICASSERT(iIndex != -1, "Trying to set extra data flag for nonexistent file");

	m_iExtraDataPerFile[iIndex] |= iFlag;
}

void SourceParser::CreateWikiDirectory()
{
	char systemString[1024];
	sprintf(systemString, "md \"%swiki\" > NUL 2>&1", GetProjectPath());

	system(systemString);
}


bool SourceParser::ProjectIsClientOrClientOnlyLib(void)
{
	if (strstr(m_PreprocessorDefines, "GAMECLIENT"))
	{
		return true;
	}

	if (strstr(m_PreprocessorDefines, "CLIENTLIBMAKEWRAPPERS"))
	{
		return true;
	}

	return false;
}

	//returns true if the projet is the game server, or a lib that is linked only into the game server
bool SourceParser::ProjectIsGameServerOrGameServerOnlyLib(void)
{
	if (strstr(m_PreprocessorDefines, "GAMESERVER"))
	{
		return true;
	}

	if (strstr(m_PreprocessorDefines, "SERVERLIBMAKEWRAPPERS"))
	{
		return true;
	}

	return false;
}



bool SourceParser::MakeSpecialAutoRunFunction(void)
{
/*
	if (_stricmp(m_ShortenedProjectFileName, "UtilitiesLib") == 0)
	{
		return true;
	}

	for (i=0; i < m_iNumDependentLibraries; i++)
	{
		if (_stricmp(m_DependentLibraryNames[i], "UtilitiesLib") == 0)
		{
			return true;
		}
	}
*/
	return true;
}




void SourceParser::AddProjectForMasterWikiCreation(char *pFullPath, char *pProjectName)
{
	int i;

	for (i=0; i < m_iNumMasterWikiProjects; i++)
	{
		if (_stricmp(m_WikiProjectNames[i], pProjectName) == 0)
		{
			return;
		}
	}

	STATICASSERT(m_iNumMasterWikiProjects < MAX_WIKI_PROJECTS, "Too many wiki projects");

	strcpy(m_WikiProjectPaths[m_iNumMasterWikiProjects], pFullPath);
	strcpy(m_WikiProjectNames[m_iNumMasterWikiProjects], pProjectName);

	TruncateStringAtLastOccurrence(m_WikiProjectNames[m_iNumMasterWikiProjects], '.');

	m_iNumMasterWikiProjects++;
}

#define MAX_WIKI_CATEGORIES 256

typedef struct SingleCommandStruct
{
	char *pCommandName;
	char *pCommandDescription;
	struct SingleCommandStruct *pNext;
} SingleCommandStruct;

class MasterWikiCommandCategory
{
public:
	MasterWikiCommandCategory(char *pCategoryName);
	~MasterWikiCommandCategory();

	void LoadCommandsFromFile(char *pFileName);
	void SortCommands(void);

	void WriteCommands(FILE *pOutFile);

	char *GetCategoryName();
	bool IsHidden() { return m_bIsHidden; }
	bool m_bProjectsWhichHaveIt[MAX_WIKI_PROJECTS];


private:
	bool m_bIsHidden;
	char m_CategoryName[256];
	SingleCommandStruct *m_pFirstCommand;


};


void SourceParser::DoMasterWikiCreation(bool bExpressionCommands)
{
	MasterWikiCommandCategory *pCategories[MAX_WIKI_CATEGORIES];
	int iProjectNum;
	int iNumCategories = 0;
	int iCategoryNum;

	char *pWikiTypeHelperString = bExpressionCommands ? "Expr" : "";

	for (iProjectNum = 0; iProjectNum < m_iNumMasterWikiProjects; iProjectNum++)
	{
		char systemString[1024];

		sprintf(systemString, "dir /b \"%swiki\\*_auto%scommands.wiki\" > c:\\temp\\wikifiles.txt", m_WikiProjectPaths[iProjectNum],
			pWikiTypeHelperString);

		system(systemString);

		Tokenizer tokenizer;
		tokenizer.SetExtraCharsAllowedInIdentifiers(".");

		if (tokenizer.LoadFromFile("c:\\temp\\wikifiles.txt"))
		{
			enumTokenType eType;
			Token token;

			do
			{
				eType = tokenizer.GetNextToken(&token);

				if (eType != TOKEN_IDENTIFIER)
				{
					break;
				}

				if (!StringEndsWith(token.sVal, ".wiki"))
				{
					break;
				}

				TruncateStringAtLastOccurrence(token.sVal, '_');
				char *pCategoryName = token.sVal + strlen(m_WikiProjectNames[iProjectNum]) + 1;

				if (strcmp(pCategoryName, "autocommands.wiki") == 0)
				{
					pCategoryName = "Uncategorized";
				}

				int i;
				bool bCategoryAlreadyExists = false;

				for (i=0; i < iNumCategories; i++)
				{
					if (_stricmp(pCategoryName, pCategories[i]->GetCategoryName()) == 0)
					{
						pCategories[i]->m_bProjectsWhichHaveIt[iProjectNum] = true;
						bCategoryAlreadyExists = true;
						break;
					}
				}

				if (!bCategoryAlreadyExists)
				{
					STATICASSERT(iNumCategories < MAX_WIKI_CATEGORIES, "Too many wiki categories");
					pCategories[iNumCategories] = new MasterWikiCommandCategory(pCategoryName);
		
					pCategories[iNumCategories]->m_bProjectsWhichHaveIt[iProjectNum] = true;
					iNumCategories++;
				}
			} while (1);
		}


		system("erase c:\\temp\\wikifiles.txt");

	}
	
	char masterWikiFullFileName[MAX_PATH];
	char hiddenWikiFullFileName[MAX_PATH];
	
	strcpy(masterWikiFullFileName, m_SolutionPath);

	TruncateStringAtLastOccurrence(masterWikiFullFileName, '\\');
	strcpy(hiddenWikiFullFileName, masterWikiFullFileName);

	if (bExpressionCommands)
	{
		sprintf(masterWikiFullFileName + strlen(masterWikiFullFileName), "\\AllExprCommands.wiki");
		sprintf(hiddenWikiFullFileName + strlen(hiddenWikiFullFileName), "\\HiddenExprCommands.wiki");
	}
	else
	{
		sprintf(masterWikiFullFileName + strlen(masterWikiFullFileName), "\\AllCommands.wiki");
		sprintf(hiddenWikiFullFileName + strlen(hiddenWikiFullFileName), "\\HiddenCommands.wiki");
	}

	FILE *pMasterFile = fopen(masterWikiFullFileName, "wb");
	FILE *pHiddenFile = fopen(hiddenWikiFullFileName, "wb");

	

	fprintf(pMasterFile, "h2. All commands by category ");

	SYSTEMTIME sysTime;
	GetLocalTime(&sysTime);
	fprintf(pMasterFile, "(%u/%u/%u)\n", sysTime.wMonth, sysTime.wDay, sysTime.wYear % 100);



	for (iCategoryNum = 0; iCategoryNum < iNumCategories; iCategoryNum++)
	{
		fprintf(pCategories[iCategoryNum]->IsHidden() ? pHiddenFile : pMasterFile, "*%s* [#%s_commands]\n", pCategories[iCategoryNum]->GetCategoryName(), pCategories[iCategoryNum]->GetCategoryName());
	}


	for (iCategoryNum = 0; iCategoryNum < iNumCategories; iCategoryNum++)
	{
	
		fprintf(pCategories[iCategoryNum]->IsHidden() ? pHiddenFile : pMasterFile, "----\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n\\\\\n----\n");
		fprintf(pCategories[iCategoryNum]->IsHidden() ? pHiddenFile : pMasterFile, "{anchor:%s_commands}\nh3. Command Category: %s\n", pCategories[iCategoryNum]->GetCategoryName(), pCategories[iCategoryNum]->GetCategoryName());

		for (iProjectNum = 0; iProjectNum < m_iNumMasterWikiProjects; iProjectNum++)
		{
			if (pCategories[iCategoryNum]->m_bProjectsWhichHaveIt[iProjectNum])
			{
				char fileName[MAX_PATH];
				if (strcmp(pCategories[iCategoryNum]->GetCategoryName(), "Uncategorized") == 0)
				{
					sprintf(fileName, "%swiki\\%s_autocommands.wiki",
						m_WikiProjectPaths[iProjectNum], 
						m_WikiProjectNames[iProjectNum]);
				}
				else
				{
					sprintf(fileName, "%swiki\\%s_%s_auto%scommands.wiki",
						m_WikiProjectPaths[iProjectNum], 
						m_WikiProjectNames[iProjectNum],
						pCategories[iCategoryNum]->GetCategoryName(),
						pWikiTypeHelperString
						);
				}
				pCategories[iCategoryNum]->LoadCommandsFromFile(fileName);
			}
		}

		pCategories[iCategoryNum]->SortCommands();
		pCategories[iCategoryNum]->WriteCommands(pCategories[iCategoryNum]->IsHidden() ? pHiddenFile : pMasterFile);

		delete pCategories[iCategoryNum];
		
	}

	fclose(pHiddenFile);
	fclose(pMasterFile);
}


MasterWikiCommandCategory::MasterWikiCommandCategory(char *pCategoryName)
{
	strcpy(m_CategoryName, pCategoryName);

	m_bIsHidden = (_stricmp(pCategoryName, "hidden") == 0);

	m_pFirstCommand = NULL;
}

MasterWikiCommandCategory::~MasterWikiCommandCategory()
{
	while (m_pFirstCommand)
	{
		SingleCommandStruct *pNext = m_pFirstCommand->pNext;
		delete m_pFirstCommand->pCommandDescription;
		delete m_pFirstCommand->pCommandName;
		delete m_pFirstCommand;
		m_pFirstCommand = pNext;
	}
}

void MasterWikiCommandCategory::LoadCommandsFromFile(char *pFileName)
{
	Tokenizer tokenizer;

	Token token;
	enumTokenType eType;

	tokenizer.SetIgnoreQuotes(true);

	if (!tokenizer.LoadFromFile(pFileName))
	{
		return;
	}
	
	do
	{
		int iOffsetAtBeginningOfCommand;
		int iLineNum;
		char *pReadHeadAtBeginningOfCommand = tokenizer.GetReadHead();

		iOffsetAtBeginningOfCommand = tokenizer.GetOffset(&iLineNum);

		eType = tokenizer.GetNextToken(&token);

		if (eType == TOKEN_NONE)
		{
			return;
		}

		SingleCommandStruct *pNewCommand = new SingleCommandStruct;



		ASSERT(&tokenizer,eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "h4") == 0, "Expected h4");
		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_DOT, "Expected . after h4");
		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected command name after h4.");

		pNewCommand->pCommandName = new char[token.iVal + 1];
		strcpy(pNewCommand->pCommandName, token.sVal);

		do
		{
			eType = tokenizer.CheckNextToken(&token);

			if (eType == TOKEN_NONE || eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "h4") == 0)
			{
				break;
			}
			else
			{
				eType = tokenizer.GetNextToken(&token);
			}
		}
		while (1);

		int iOffsetAtEndOfCommand;

		iOffsetAtEndOfCommand = tokenizer.GetOffset(&iLineNum);

		pNewCommand->pCommandDescription = new char[iOffsetAtEndOfCommand - iOffsetAtBeginningOfCommand + 1];
		memcpy(pNewCommand->pCommandDescription, pReadHeadAtBeginningOfCommand, iOffsetAtEndOfCommand - iOffsetAtBeginningOfCommand);
		pNewCommand->pCommandDescription[iOffsetAtEndOfCommand - iOffsetAtBeginningOfCommand] = 0;

		pNewCommand->pNext = m_pFirstCommand;
		m_pFirstCommand = pNewCommand;

		NormalizeNewlinesInString(pNewCommand->pCommandDescription);
	} while (1);
}

void MergeSortCommands(SingleCommandStruct **ppList, int iListLen)
{
	int iListLen1;
	int iListLen2;
	SingleCommandStruct *pList1;
	SingleCommandStruct *pList2;
	SingleCommandStruct *pNext;

	int i;

	SingleCommandStruct *pMasterListHead;
	SingleCommandStruct *pMasterListTail;


	if (iListLen < 2)
	{
		return;
	}

	iListLen1 = iListLen / 2;
	iListLen2 = iListLen - iListLen1;

	pList2 = pList1 = *ppList;

	for (i=0; i < iListLen1 - 1; i++)
	{
		pList2 = pList2->pNext;
	}

	pNext = pList2->pNext;
	pList2->pNext = NULL;
	pList2 = pNext;

	//now pList1 and pList2 point to totally separate lists
	MergeSortCommands(&pList1, iListLen1);
	MergeSortCommands(&pList2, iListLen2);

	if (StringComesAlphabeticallyBefore(pList1->pCommandName, pList2->pCommandName))
	{
		pMasterListHead = pMasterListTail = pList1;
		pList1 = pList1->pNext;
		pMasterListHead->pNext = NULL;
	}
	else
	{
		pMasterListHead = pMasterListTail = pList2;
		pList2 = pList2->pNext;
		pMasterListHead->pNext = NULL;
	}

	while (pList1 && pList2)
	{
		if (StringComesAlphabeticallyBefore(pList1->pCommandName, pList2->pCommandName))
		{
			pMasterListTail->pNext = pList1;
			pList1 = pList1->pNext;
			pMasterListTail->pNext->pNext = NULL;
			pMasterListTail = pMasterListTail->pNext;
		}
		else
		{
			pMasterListTail->pNext = pList2;
			pList2 = pList2->pNext;
			pMasterListTail->pNext->pNext = NULL;
			pMasterListTail = pMasterListTail->pNext;
		}
	}

	if (pList1)
	{
		pMasterListTail->pNext = pList1;
	}
	else
	{
		pMasterListTail->pNext = pList2;
	}

	*ppList = pMasterListHead;
}








	


void MasterWikiCommandCategory::SortCommands(void)
{
	int iCount = 0;
	SingleCommandStruct *pCounter = m_pFirstCommand;

	while (pCounter)
	{
		iCount++;
		pCounter = pCounter->pNext;
	}

	MergeSortCommands(&m_pFirstCommand, iCount);

}

void MasterWikiCommandCategory::WriteCommands(FILE *pOutFile)
{
	SingleCommandStruct *pCounter = m_pFirstCommand;

	while (pCounter)
	{
		fprintf(pOutFile, "%s\n", pCounter->pCommandDescription);

		pCounter = pCounter->pNext;
	}
}

char *MasterWikiCommandCategory::GetCategoryName()
{
	return m_CategoryName;
}



bool SourceParser::DoesVariableHaveValue(char *pVarName, char *pValue, bool bCheckFinalValueOnly)
{
	SourceParserVar *pVar = m_pFirstVar;


	while (pVar)
	{
		if (_stricmp(pVar->pVarName, pVarName) == 0)
		{
			if (bCheckFinalValueOnly)
			{
				return StringBeginsWith(pVar->pValue + 1, pValue, true);
			}

			char tempBuffer[256];
			sprintf(tempBuffer, " %s ", pValue);
			if (strstri(pVar->pValue, tempBuffer))
			{
				return true;
			}
			else
			{
				return false;
			}
		}

		pVar = pVar->pNext;
	}

	return false;
}

bool SourceParser::VariableIsSuffix(char *pVarName, char *pValue)
{
	SourceParserVar *pVar = m_pFirstVar;

	while (pVar)
	{
		if (_stricmp(pVar->pVarName, pVarName) == 0)
		{
			//what this loop is doing is going through " foo bar wakka " (space separated, with trailing and leading spaces), 
			//finding foo, then, bar, then wakka, checking if pValue ends in bar, etc. If at any point
			//it finds a match, it returns true
			char *pReadHead = pVar->pValue;
			while (1)
			{
				while (*pReadHead == ' ')
				{
					pReadHead++;
				}

				if (!*pReadHead)
				{
					break;
				}

				char *pBeginningOfWord = pReadHead;

				while (*pReadHead != ' ')
				{
					pReadHead++;
				}

				char *pEndOfWord = pReadHead;

				*pEndOfWord = 0;
				bool bFound = StringEndsWith(pValue, pBeginningOfWord);
				*pEndOfWord = ' ';

				if (bFound)
				{
					return true;
				}
			}
		}

		pVar = pVar->pNext;
	}

	return false;
}


StringTree *SourceParser::GetStringTreeWithAllVariableValues(char *pVarName)
{

	SourceParserVar *pVar = m_pFirstVar;


	while (pVar)
	{
		if (_stricmp(pVar->pVarName, pVarName) == 0)
		{
			return StringTree_CreateStrTokStyle(pVar->pValue, " ");
		}

		pVar = pVar->pNext;

	}

	return NULL;
	
}


void SourceParser::AddVariableValue(char *pVarName, char *pValue)
{
	SourceParserVar *pVar = m_pFirstVar;


	while (pVar)
	{
		if (_stricmp(pVar->pVarName, pVarName) == 0)
		{
			int iCurLen = (int)strlen(pVar->pValue);
			int iAddLen = (int)strlen(pValue);
			char *pNewBuf = new char[iCurLen + iAddLen + 2];
			sprintf(pNewBuf, " %s%s", pValue, pVar->pValue);
			delete pVar->pValue;
			pVar->pValue = pNewBuf;
			return;
		}
		
		pVar = pVar->pNext;
	}

	pVar = new SourceParserVar;

	pVar->pNext = m_pFirstVar;
	m_pFirstVar = pVar;

	pVar->pVarName = STRDUP(pVarName);
	int iCurLen = (int)strlen(pValue);
	pVar->pValue = new char[iCurLen + 3];
	sprintf(pVar->pValue, " %s ", pValue);
}

void SourceParser::SetVariablesFromTokenizer(Tokenizer *pTokenizer, char *pStartingDirectory)
{
	enumTokenType eType;
	Token token;
	char varName[256];

	while (1)
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_NONE)
		{
			return;
		}

		ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER, "Expected identifier name to set");
		ASSERT(pTokenizer,token.iVal < 255, "Var name overflow");

		if (_stricmp(token.sVal, "#include") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string after #include");
			char fullIncludeName[4096];
			if (StringBeginsWith(token.sVal, "..", true))
			{
				sprintf(fullIncludeName, "%s%s", pStartingDirectory, token.sVal);
			}
			else
			{
				strcpy(fullIncludeName, token.sVal);
			}
			
			Tokenizer *pIncludeTokenizer = new Tokenizer;
			pIncludeTokenizer->SetExtraCharsAllowedInIdentifiers("#");
			if (!pIncludeTokenizer->LoadFromFile(fullIncludeName))
			{
				ASSERTF(pTokenizer,0, "Couldn't load include file %s", fullIncludeName);
			}


			char *pLastBackslash = strrchr(fullIncludeName, '\\');
			if (pLastBackslash)
			{
				*(pLastBackslash + 1) = 0;
			}

			SetVariablesFromTokenizer(pIncludeTokenizer, fullIncludeName);
		}
		else
		{
			strcpy(varName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Expected = after var name");
			
			do
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "expected identifier for var value");
				AddVariableValue(varName, token.sVal);

				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected , or ;");
			}
			while (token.iVal != RW_SEMICOLON);
		}
	} 
}

void SourceParser::FindVariablesFileAndLoadVariables(void)
{
	char directoryToTry[MAX_PATH];
	char fileToTry[MAX_PATH];

	strcpy(directoryToTry, m_ProjectPath);
	int iDirectoryStrLen = (int)strlen(directoryToTry);

	while (iDirectoryStrLen)
	{
		sprintf(fileToTry, "%sStructParserVars.txt", directoryToTry);
		Tokenizer *pTokenizer = new Tokenizer;
		pTokenizer->SetExtraCharsAllowedInIdentifiers("#");
		bool bSuccess = pTokenizer->LoadFromFile(fileToTry);

		if (bSuccess)
		{
			SetVariablesFromTokenizer(pTokenizer, directoryToTry);
			delete pTokenizer;
			return;
		}

		delete pTokenizer;
		directoryToTry[--iDirectoryStrLen] = 0;

		while (iDirectoryStrLen && directoryToTry[iDirectoryStrLen - 1] != '\\')
		{
			directoryToTry[--iDirectoryStrLen] = 0;
		}
	}
}


bool SourceParser::ProjectGoesIntoMasterWiki(char *pProjectName)
{
	char temp[256];
	strcpy(temp, pProjectName);
	MakeStringUpcase(temp);
	return StringEndsWith(temp, "GAMECLIENT.VCPROJ") || StringEndsWith(temp, "GAMESERVER.VCPROJ") || StringEndsWith(temp, "GAMECLIENT.VCXPROJ") || StringEndsWith(temp, "GAMESERVER.VCXPROJ");
}

void SourceParser::AddDependentProjectsForMasterWikiCreation(SourceParser *pMasterSourceParser)
{
	int i;

	for (i=0; i < m_iNumDependentLibraries; i++)
	{
		char fullPath[MAX_PATH];

		strcpy(fullPath, m_DependentLibraryAbsolutePaths[i]);
		TruncateStringAfterLastOccurrence(fullPath, '\\');

		pMasterSourceParser->AddProjectForMasterWikiCreation(fullPath, GetFileNameWithoutDirectoriesOrSlashes(m_DependentLibraryAbsolutePaths[i]));
	}
}

bool SourceParser::SourceParserChangedInAnyDependentLibrary(eSourceParserIndex eIndex)
{
	int i, j;

	if (m_pSourceParsers[eIndex] && m_pSourceParsers[eIndex]->m_bSomethingChanged)
	{
		return true;
	}

	if (m_pRecursionContext)
	{
		for (i=0; i < m_iNumDependentLibraries; i++)
		{
			for (j=0; j < m_pRecursionContext->iNumThingsChangedTrackers; j++)
			{
				if (_stricmp(m_DependentLibraryNames[i], m_pRecursionContext->thingsChangedTrackers[j].projName) == 0)
				{
					if (m_pRecursionContext->thingsChangedTrackers[j].changeBits & (1 << eIndex))
					{
						return true;
					}
				}
			}
		}
	}




	return false;
}




#undef FILE
#undef fopen
#undef fclose
#undef fprintf
#undef fscanf


void SourceParser::WriteRecursionMarkerFile(void)
{
	char fname[MAX_PATH];
	char shortName[MAX_PATH];
	FILE *pFile;
	
	sprintf(shortName, "%s", m_ShortenedProjectFileName);
	MakeStringAllAlphaNum(shortName);
	sprintf(fname, "%s\\%s.RecurseMarker", m_ProjectPath, shortName);

	pFile = fopen(fname, "wt");
	if (pFile)
	{
		__int64 iTime;
		fprintf(pFile, "%s ", m_SolutionPath);
		GetSystemTimeAsFileTime((LPFILETIME)&iTime);
		fprintf(pFile, "%I64d", iTime);
		fclose(pFile);
	}
}


void SourceParser::CheckRecursionMarkerFile(void)
{
	char fname[MAX_PATH];
	char shortName[MAX_PATH];
	FILE *pFile;


	sprintf(shortName, "%s_%s", m_ShortenedProjectFileName);
	MakeStringAllAlphaNum(shortName);
	sprintf(fname, "%s\\%s.RecurseMarker", m_ProjectPath, shortName);

	pFile = fopen(fname, "rt");

	if (!pFile)
	{
		return;
	}

	TRACE("Found recursion marker file");
	char readSolutionName[MAX_PATH] = "";
	__int64 readTime = 0;
	__int64 curTime;

	fscanf(pFile, "%s %I64d", readSolutionName, &readTime);
	fclose(pFile);
	GetSystemTimeAsFileTime((LPFILETIME)&curTime);

	if (AreFilenamesEqual(m_SolutionPath, readSolutionName) && (curTime - readTime < (__int64)30 * 60 * 10000000))
	{
		TRACE("Recently called recursively with same solution... quitting");
		exit(0);
	}

}

//don't put anything here, the file stuff is turned off