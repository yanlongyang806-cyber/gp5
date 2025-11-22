#pragma once

typedef struct CBTimingStep CBTimingStep;

AUTO_STRUCT;
typedef struct CBTimingStep
{
	char *pName;
	int iLevel;
	U64 iBeginTime; //mSecsSince2000
	S64 iEndTime; //0 means not complete
	char *pFullTime; AST(ESTRING)
	CBTimingStep **ppChildSteps;
	bool bFailed;
	bool bComment; //this step can not have children and is displayed somewhat differently


	bool bParentOfSubTree; //this step is a specially named parent of a "named subtree", and other steps are going to be added
		//specifically below it by name. (This is used for child scripting contexts). This works differently
		//in two ways: (a) steps can be added to it by name, and (b) it isn't automatically closed by things
		//that happen later on in this step's parent steps

	bool bDetachedTree; //if true, then this step is executing "detached", so the main timing continues without it
} CBTimingStep;

AUTO_STRUCT;
typedef struct CBTiming
{
	U32 iBuildStartTime; //used as identifier, ss2000
	U32 iBuildEndTime; //0 means not complete
	char *pFullTime; AST(ESTRING)
	bool bFailed;

	CBTimingStep **ppSteps;
} CBTiming;





void CBTiming_Begin(U32 iBuildStartTime);
void CBTiming_StepEx(char *pName, char *pSubTreeName, int iLevel, bool bParentOfNamedSubtree, bool bDetachedTree);
static __forceinline void CBTiming_Step(char *pName, int iLevel)
{
	CBTiming_StepEx(pName, NULL, iLevel, false, false);
}

void CBTiming_EndSubTree(char *pName, bool bFail);

void CBTiming_End(bool bFail);
void CBTiming_CommentEx(char *pString, char *pSubTreeName);
static __forceinline void CBTiming_Comment(char *pString)
{
	CBTiming_CommentEx(pString, NULL);
}

void CBTiming_Fail(void);


//stepIndexstring is something like "5+3+2+11" where each one is an index into the earray of steps
void CBTiming_ToHTMLString(char **ppOutString, U32 iBuildStartTime, char *pStepIndexString);

bool CBTiming_Active(void);
