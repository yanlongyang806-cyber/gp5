#pragma once

typedef enum enumCBResult enumCBResult;

typedef struct NameValuePair NameValuePair;

AUTO_STRUCT;
typedef struct SingleBuildInfo
{
	char *pPresumedSVNBranch; AST(ESTRING)
	char *pPresumedGimmeProductAndBranch; AST(ESTRING)
	char *pCurState; AST(ESTRING)
	char *pLogs; AST(ESTRING FORMATSTRING(HTML=1))
	char *pBuilderPlexedLogs; AST(ESTRING FORMATSTRING(HTML=1))

	NameValuePair **ppVariables;

	enumCBResult eResult;
	U32 iStartTime;  AST(FORMATSTRING(HTML_SECS = 1))
	U32 iEndTime;  AST(FORMATSTRING(HTML_SECS = 1))
	int iNumErrors;
} SingleBuildInfo;

AUTO_STRUCT;
typedef struct BuilderInfo
{
	char *pMachineName; AST(KEY)
	bool bConnected; AST(NO_TEXT_SAVE)
	char *pBuilderComment; AST(ESTRING) //the comment that producers can set through the window dialog to describe the builder
	U32 iLastContactTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
	char *pMostRecentBuildSummary; AST(ESTRING)
	char *pMostRecentSuccessfulBuildSummary; AST(ESTRING)
	char **ppBuilderPlexedLogDirs; AST(NO_TEXT_SAVE FORMATSTRING(HTML_SKIP=1))
	SingleBuildInfo *pCurBuild;

	//oldest = [0]
	SingleBuildInfo **ppPreviousBuilds; AST(FORMATSTRING(HTML_REVERSE_ARRAY = 1))

	char *pDowntimeComment; AST(ESTRING)

	AST_COMMAND("Remove", "RemoveBuilder $FIELD(MachineName) $STRING(Type yes to remove this builder)")
	AST_COMMAND("Set Downtime Comment", "SetDowntimeComment $FIELD(MachineName) $STRING(Set this comment, empty to clear)")
	
} BuilderInfo;

AUTO_STRUCT;
typedef struct BuilderOverview
{
	char *pLink; AST(ESTRING FORMATSTRING(HTML=1))
		char *pConnected; AST(ESTRING, FORMATSTRING(HTML_CLASS_IFEXPR = "\\q$\\q = \\qYes\\q ; divGreen ; StrContains(\\q$\\q, \\qDowntime\\q) ; divPurple ; 1 ; divRed"))
		char *pSucceeded; AST(ESTRING, FORMATSTRING(HTML_CLASS_IFEXPR = "\\q$\\q = \\qYes\\q ; divGreen ; \\q$\\q = \\qNo\\q ; divRed"))
		char *pVNC; AST(ESTRING FORMATSTRING(HTML=1))
		char *pType; AST(ESTRING)
		char *pCurState; AST(ESTRING)
		char *pSVNRevision; AST(ESTRING)
		char *pGimmeTime; AST(ESTRING)
		char *pPatchVersion; AST(ESTRING)
		char *pSuccessfulBuild; AST(ESTRING)

} BuilderOverview;

AUTO_STRUCT;
typedef struct BuilderCategoryOverview
{
	char *pCategoryName; AST(KEY, FORMATSTRING(HTML_NO_HEADER = 1))
		BuilderOverview **ppBuilders;
} BuilderCategoryOverview;

AUTO_STRUCT;
typedef struct CBMonitorOverview
{
	char *pLinks; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER = 1))
		BuilderCategoryOverview **ppCategories;  AST(FORMATSTRING(HTML_NO_HEADER = 1))
} CBMonitorOverview;