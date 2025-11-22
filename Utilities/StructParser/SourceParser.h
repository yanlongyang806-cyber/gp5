#ifndef _SOURCEPARSER_H_
#define _SOURCEPARSER_H_

#include "tokenizer.h"

#include "FileListLoader.h"
#include "FileListWriter.h"

#include "IdentifierDictionary.h"
#include "SourceParserBaseClass.h"

#include "utils.h"


#pragma warning( disable : 4996 )

typedef struct XMLNode XMLNode;

typedef enum eSourceParserIndex
{
	SOURCEPARSERINDEX_MAGICCOMMANDMANAGER,
	SOURCEPARSERINDEX_STRUCTPARSER,
	SOURCEPARSERINDEX_AUTOTRANSACTIONMANAGER,
	SOURCEPARSERINDEX_AUTOTESTMANAGER,
	SOURCEPARSERINDEX_LATELINKMANAGER,


	//needs to be last
	SOURCEPARSERINDEX_AUTORUNMANAGER,
} eSourceParserIndex;

#define NUM_BASE_SOURCE_PARSERS (SOURCEPARSERINDEX_AUTORUNMANAGER + 1)

#define MAX_MAGIC_WORDS_PER_BASE_SOURCE_PARSER 14

#define MAX_DEPENDENT_LIBRARIES 32

#define MAX_WIKI_PROJECTS 256
#define MAX_WIKI_CATEGORIES 256

#define MAX_PROJECTS_ONE_SOLUTION 256
#define MAX_DEPENDENCIES_ONE_PROJECT 64

#define MAX_PROPERTYSHEETS_PER_PROJ 8

typedef class AutoRunManager AutoRunManager;

#define MAX_COMPILE_BATCH_FILES 200
typedef struct SourceParserWhatThingsChangedTracker
{
	char projName[MAX_NAME_LENGTH];
	int changeBits;
} SourceParserWhatThingsChangedTracker;

typedef struct SourceParserRecursionContext
{
	int iNumCompileBatchFiles;
	char compileBatchFileNames[MAX_COMPILE_BATCH_FILES][MAX_PATH];

	//files which, if the batch file fails to compile, we should delete so that the project is forced to recompile
	char compileBatchFileNameKeyFiles[MAX_COMPILE_BATCH_FILES][MAX_PATH];

	int iNumThingsChangedTrackers;
	SourceParserWhatThingsChangedTracker thingsChangedTrackers[MAX_PROJECTS_ONE_SOLUTION];
} SourceParserRecursionContext;


class SourceParser
{
public:
	SourceParser();
	~SourceParser();

	void ParseSource(char *pProjectPath, char *pProjectFileName, char *pCurTarget, char *pCurConfiguration, char *pCurVCDir, char *pSolutionPath,
		char *pExecutable, SourceParserRecursionContext *pRecursionContext);

	void CompileCFile(char *pFileName, char *pRelativePath, bool bIsCPPFile);

	char *GetShortProjectName() { return m_ShortenedProjectFileName; }
	char *GetProjectPath() { return m_ProjectPath; }
	IdentifierDictionary *GetDictionary() { return &m_IdentifierDictionary; }

	void SetExtraDataFlagForFile(char *pFileName, int iFlag);

	int GetNumLibraries(void) { return m_iNumDependentLibraries; }
	char *GetNthLibraryName(int n) { return m_DependentLibraryNames[n]; }
	char *GetNthLibraryAbsolutePath(int n) { return m_DependentLibraryAbsolutePaths[n]; }
	char *GetNthLibraryRelativePath(int n) { return m_DependentLibraryRelativePaths[n]; }
	bool IsNthLibraryXBoxExcluded(int n) { return m_bExcludeLibrariesFromXBOX[n]; }
    bool IsNthLibraryPS3Excluded(int n) { return m_bExcludeLibrariesFromPS3[n]; }

	AutoRunManager *GetAutoRunManager() { return m_pAutoRunManager; }

	void CreateWikiDirectory();

	//returns true if the project is the game client, or a lib that is linked only into the game client
	bool ProjectIsClientOrClientOnlyLib(void);

	//returns true if the projet is the game server, or a lib that is linked only into the game server
	bool ProjectIsGameServerOrGameServerOnlyLib(void);

	//returns true if the project is an executable as opposed to a library
	bool ProjectIsExecutable(void) { return m_bIsAnExecutable; }

	int GetNumProjectFiles(void) { return m_iNumProjectFiles; }
	char *GetNthProjectFile(int n) { return m_ProjectFiles[n]; }

	bool DoesVariableHaveValue(char *pVarName, char *pValue, bool bCheckFinalValueOnly);

	void AddDependentProjectsForMasterWikiCreation(SourceParser *pMasterSourceParser);
	void AddProjectForMasterWikiCreation(char *pFullPath, char *pProjectName);

	//returns true if one of the values of the variable is a suffix of the string
	bool VariableIsSuffix(char *pVarName, char *pStrig);

	void TryToReadProjectDepenciesFromProjectFile(char *pProjectDependencyIDs[MAX_DEPENDENCIES_ONE_PROJECT], int *piNumDependencies, char *pProjFullPath);
	
	StringTree *GetStringTreeWithAllVariableValues(char *pVarName);


private://structs
	typedef struct SourceParserVar
	{
		char *pVarName;
		char *pValue;
		struct SourceParserVar *pNext;
	} SourceParserVar;


private:
	FileListLoader *m_pFileListLoader;
	FileListWriter *m_pFileListWriter;

	IdentifierDictionary m_IdentifierDictionary;

	int m_iNumSourceParsers;
	SourceParserBaseClass *m_pSourceParsers[NUM_BASE_SOURCE_PARSERS];
	AutoRunManager *m_pAutoRunManager;

	int m_iNumProjectFiles;
	char m_ProjectFiles[MAX_FILES_IN_PROJECT][MAX_PATH];
	bool m_bFilesNeedToBeUpdated[MAX_FILES_IN_PROJECT];

	int m_iNumDependencies[MAX_FILES_IN_PROJECT];
	int m_iDependencies[MAX_FILES_IN_PROJECT][MAX_DEPENDENCIES_SINGLE_FILE];

	int m_iExtraDataPerFile[MAX_FILES_IN_PROJECT];

	char m_FullProjectFileName[MAX_PATH];

	char m_Executable[MAX_PATH];

	char m_ProjectPath[MAX_PATH];
	char m_ShortenedProjectFileName[MAX_PATH];

	//includes filename
	char m_SolutionPath[MAX_PATH];
	//dir only. Ends with backslash
	char m_SolutionDir[MAX_PATH];

	int m_iNumDependentLibraries;
	char m_DependentLibraryNames[MAX_DEPENDENT_LIBRARIES][MAX_PATH];
	char m_DependentLibraryAbsolutePaths[MAX_DEPENDENT_LIBRARIES][MAX_PATH];
	char m_DependentLibraryRelativePaths[MAX_DEPENDENT_LIBRARIES][MAX_PATH]; //relative to the .vcproj file
	bool m_bExcludeLibrariesFromXBOX[MAX_DEPENDENT_LIBRARIES];
    bool m_bExcludeLibrariesFromPS3[MAX_DEPENDENT_LIBRARIES];


	//esnure that the project contains the two master autogen files
	bool m_FoundAutoGenFile1;
	bool m_FoundAutoGenFile2;

	char m_AutoGenFile1Name[MAX_PATH];
	char m_AutoGenFile2Name[MAX_PATH];

	char m_SpecialAutoRunFuncName[MAX_PATH];

	//whether the project we're working on is an executable vs. a libary
	bool m_bIsAnExecutable;



	//---------------stuff used to do command-line compilation of auto-generated C files

	//stuff passed in on the command line
	char *m_pCurTarget;
	char *m_pCurConfiguration;
	char *m_pCurVCDir;

	//stuff ripped out of vcproj file
	char m_AdditionalIncludeDirs[TOKENIZER_MAX_STRING_LENGTH];
	char m_PreprocessorDefines[TOKENIZER_MAX_STRING_LENGTH];
	char m_ObjectFileDir[MAX_PATH];

	//stuff used to check whether we need to C file compiling
	bool m_bCleanBuildHappened;
	bool m_bProjectFileChanged;

	//stuff for master-wiki-file-generation, done only by the structparserstub project
	int m_iNumMasterWikiProjects;
	char m_WikiProjectPaths[MAX_WIKI_PROJECTS][MAX_PATH];
	char m_WikiProjectNames[MAX_WIKI_PROJECTS][MAX_PATH];

	SourceParserVar *m_pFirstVar;

	SourceParserRecursionContext *m_pRecursionContext;

private:
	void ProcessProjectFile();
	void ProcessProjectFile_VS2010(void);
	bool NeedToUpdateFile(char *pFileName, int iExtraData,  bool bForceUpdateUnlessFileDoesntExist);
	void ScanSourceFile(char *pSourceFile);
	
	void LoadSavedDependenciesAndRemoveObsoleteFiles(void);

	//returns true if at least one file was set to udpate that was previously not set to update
	//
	//find all need-to-update files which have dependencies, and set all the other
	//files they are dependent on to be need-to-update, and recurse. 
	bool ProcessAllLoadedDependencies();
	void ClearAllDependenciesForUpdatingFiles(void);
	void AddDependency(int iFile1, int iFile2);
	void ProcessAllFiles_ReadAll();
	void ProcessAllFiles();
	int FindProjectFileIndex(char *pFileName);
	void PrepareMasterFiles(bool bBuildAll);
	void MakeAutoGenDirectory();
	void ProcessSolutionFile(bool bRecursivelyCallStructParser, bool bForceReadAll);
	void CheckForRequiredFiles(char *pFileName);
	bool IsLibraryXBoxExcluded(char *pLibName);
    bool IsLibraryPS3Excluded(char *pLibName);
	bool DoMasterFilesExist();
	void GetAdditionalStuffFromPropertySheets(char *pDirsAlreadyFound, char *pPropertySheetNames, char *pToolName, int iReservedWordToFind);
	bool DidCleanBuildJustHappen();
	void CleanOutAllAutoGenFiles(char *pProjectDir, char *pShortProjectFileName);
	bool IsQuickExitPossible();
	void CreateCleanBuildMarkerFile();
	void CreateParsers(void);
	
	bool MakeSpecialAutoRunFunction(void);

	void DoMasterWikiCreation(bool bExpressionCommands);

	void AddVariableValue(char *pVarName, char *pValue);
	void SetVariablesFromTokenizer(Tokenizer *pTokenizer, char *pStartingDirectory);
	void FindVariablesFileAndLoadVariables(void);

	bool ProjectGoesIntoMasterWiki(char *pProjectName);

	//pass in SOURCEPARSERINDEX_MAGICCOMMANDMANAGER, this tells you if 
	//the current MagicCommandManager, or the MagicCommandManager of any dependent library,
	//changed this run
	bool SourceParserChangedInAnyDependentLibrary(eSourceParserIndex eIndex);

	bool ShouldSkipProjectEntirely(char *pProjName);

	void WriteRecursionMarkerFile(void);
	void CheckRecursionMarkerFile(void);

	void ReplacePlatformAndConfigurationMacros_2010(char *pString);
	void GetVariableFromProjFileAndPropertySheets(char outString[TOKENIZER_MAX_STRING_LENGTH], XMLNode *pParentNode, XMLNode *pPropertySheetNodes[MAX_PROPERTYSHEETS_PER_PROJ], int iNumPropertySheets, char *pVariablePath, char *pVariableName);

};


//global TRACE for verbose stuff
extern int gVerbose;
#define TRACE(...)  {if (gVerbose) {printf(__VA_ARGS__); fflush(stdout);}}

bool SlowSafeDependencyMode(void);
extern int gDontCheckForBadIfsInCode;



#endif
