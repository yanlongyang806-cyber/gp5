#pragma once

typedef struct NameValuePair NameValuePair;


AUTO_STRUCT;
typedef struct CBMonitorHeartbeatStruct
{
	//these two are different from the variables because they are
	//calculated in special fashions at different times
	char *pPresumedSVNBranch; AST(ESTRING)
	char *pPresumedGimmeProductAndBranch; AST(ESTRING)

	NameValuePair **ppVariables;
	char *pCurState; AST(ESTRING)
	int iNumErrors;
} CBMonitorHeartbeatStruct;


#define CBMONITOR_HEARTBEAT 60