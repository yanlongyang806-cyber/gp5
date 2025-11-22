#pragma once
#include "GlobalTypes.h"

AUTO_STRUCT;
typedef struct BuilderPlexerChoice
{
	char *pBuilderName; AST(KEY)
	char *pProductName;
	U32 iLastActivationTime;
	U32 iLastDeactivationTime;
	char *pLastRunComment; AST(ESTRING)
} BuilderPlexerChoice;

AUTO_STRUCT;
typedef struct BuilderPlexerState
{
	BuilderPlexerChoice **ppAllChoices;
	char *pCurActiveChoiceName; AST(ESTRING)//if not set or empty, there is no current build
} BuilderPlexerState;

AUTO_STRUCT;
typedef struct BuilderPlexerProduct
{
	char *pName; AST(STRUCTPARAM)
	char *pShortName; AST(STRUCTPARAM)

	char *pProductDescription;

} BuilderPlexerProduct;

AUTO_STRUCT;
typedef struct BuilderPlexerGlobalConfig
{
	char **ppRequiredDirNames; AST(NAME(RequiredDir))
	char **ppOptionalDirNames; AST(NAME(OptionalDir))

	BuilderPlexerProduct **ppProducts; AST(NAME(Product))
} BuilderPlexerGlobalConfig;




extern BuilderPlexerGlobalConfig gBPConfig;
extern BuilderPlexerState gBPState;

void FailWithMessage(char *pFmt, ...);
void LoadConfig(void);
BuilderPlexerChoice *GetCurActiveChoice(void);
BuilderPlexerChoice *FindChoiceByName(char *pName);
bool VerifyFoldersForActiveState(BuilderPlexerChoice *pChoice, char **ppErrorString);

//looks to see if c:/src exist, etc., implying that there is a newly created choice to add to the system
//returns -1 on error, 0 if no folders exist, 1 if they do
int CheckForFoldersForChoiceCreation(BuilderPlexerProduct **ppProduct, char **ppErrorString);

void SaveState();
void LoadState();



BuilderPlexerProduct *FindProductByName(char *pName);

char *GetCurSummaryString(void);
char *GetDescriptionOfBuild(BuilderPlexerChoice *pChoice);

void MakeRenamedDirName(char **ppOutDirName, char *pInDirName, BuilderPlexerChoice *pChoice);
void MakeNonRenamedDirName(char **ppOutDirName, char *pInDirName);
int ReplaceDirNameMacros(char **ppOutString, BuilderPlexerProduct *pProduct);

