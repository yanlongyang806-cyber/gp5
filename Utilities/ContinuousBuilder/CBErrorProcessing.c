#include "CBErrorProcessing.h"
#include "ETCommon/ETCommonStructs.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETWebCommon.h"
#include "AutoGen/ETCommonStructs_h_ast.h"

#include "StructInit.h"
#include "GlobalTypes.h"
#include "GlobalComm.h"
#include "objContainer.h"
#include "net.h"
#include "timing.h"
#include "objContainerIO.h"
#include "utils.h"
#include "Organization.h"

extern ParseTable parse_TriviaData[];
#define TYPE_parse_TriviaData TriviaData
extern ErrorTrackerContext *gpCurrentContext;
extern ErrorTrackerSettings gErrorTrackerSettings;
ErrorTrackerContext *gpLastContext;

static bool sbDisableAllErrorProcessing = false;

void CBErrorProcessing_SetDisableAll(bool bSet)
{
	sbDisableAllErrorProcessing = bSet;
}

ErrorTrackerContext *CB_GetCurrentContext(void)
{
	return gpCurrentContext;
}
ErrorTrackerEntryList *CB_GetCurrentEntryList(void)
{
	return &gpCurrentContext->entryList;
}
ErrorTrackerContext *CB_GetLastContext(void)
{
	return gpLastContext;
}
ErrorTrackerEntryList *CB_GetLastEntryList(void)
{
	return &gpLastContext->entryList;
}

void CB_InitializeErrorTrackerContexts(void)
{
	gpCurrentContext = StructCreate(parse_ErrorTrackerContext);
	gpCurrentContext->entryList.eContainerType = GLOBALTYPE_ERRORTRACKERENTRY;

	gpLastContext = StructCreate(parse_ErrorTrackerContext);
	gpLastContext->entryList.eContainerType = GLOBALTYPE_ERRORTRACKERENTRY_LAST;
}

void CB_SwitchErrorTrackerContexts(void)
{
	ErrorTrackerContext *pContext = gpLastContext;
	objRemoveAllContainersWithType(gpLastContext->entryList.eContainerType);
	gpLastContext->entryList.bSomethingHasChanged = true;
	gpLastContext = gpCurrentContext;
	gpCurrentContext = pContext;
}

static U32 getNextID(void)
{
	if(!gpCurrentContext->entryList.uNextID) 
		gpCurrentContext->entryList.uNextID++; // We never want ID #0
	do 
	{
		gpCurrentContext->entryList.uNextID++;
	} while (objGetContainer(gpCurrentContext->entryList.eContainerType, gpCurrentContext->entryList.uNextID));
	return gpCurrentContext->entryList.uNextID;
}

extern void CB_NewErrorCallback(ErrorEntry *pNewEntry, ErrorEntry *pMergedEntry);
void CB_ProcessErrorData(NetLink *link, ErrorData *pErrorData)
{
	NOCONST(ErrorEntry) *pEntry = createErrorEntryFromErrorData(pErrorData, 0, NULL);
	NOCONST(ErrorEntry) *pMergedEntry;
	int iETIndex;

	if (!pEntry)
	{
		sendFailureResponse(link);
		return;
	}
	errorTrackerSendStatusUpdate(link, STATE_ERRORTRACKER_MERGE);
	pMergedEntry = CONTAINER_NOCONST(ErrorEntry, findErrorTrackerEntryFromNewEntry(pEntry));
	if (pMergedEntry == NULL) // New entry
	{
		Container * con = NULL;
		pEntry->uID = getNextID();
		iETIndex = 1;
		con = objAddExistingContainerToRepository(errorTrackerLibGetCurrentType(), pEntry->uID, pEntry);
		pMergedEntry = pEntry;
	}
	else // Merge entry
	{
		U32 uTime = timeSecondsSince2000();
		pEntry->uID = pMergedEntry->uID;
		iETIndex = pMergedEntry->iTotalCount+1;

		mergeErrorEntry_Part1(pMergedEntry, CONST_ENTRY(pEntry), uTime);
		mergeErrorEntry_Part2(pMergedEntry, CONST_ENTRY(pEntry), uTime);
		objContainerMarkModified(objGetContainer(errorTrackerLibGetCurrentType(), pMergedEntry->uID));
	}

	if (pMergedEntry->eType == ERRORDATATYPE_ERROR && !pMergedEntry->ppStackTraceLines && pEntry->ppStackTraceLines)
	{
		pMergedEntry->ppStackTraceLines = pEntry->ppStackTraceLines;
		pEntry->ppStackTraceLines = NULL;
	}
	CB_NewErrorCallback(CONST_ENTRY(pEntry), CONST_ENTRY(pMergedEntry));

	if (linkConnected(link) && pEntry && pMergedEntry)
	{
		SendDumpFlags(link, DUMPFLAGS_UNIQUEID, 0, pMergedEntry->uID, 0);
	}
	if (pMergedEntry != pEntry) // pEntry is not Container Object
		StructDestroyNoConst(parse_ErrorEntry, pEntry);
	/*else if (pEntry->uID) // Double checking that pEntry has an ID key
	{
		eaDestroyStruct(&pEntry->ppTriviaData, parse_TriviaData);
		objContainerMarkModified(objGetContainer(errorTrackerLibGetCurrentType(), pEntry->uID));
	}*/
}

void CB_IncomingErrorData(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	if (sbDisableAllErrorProcessing)
	{
		sendFailureResponse(link);
	}
	else
	{
		NOCONST(ErrorEntry) *pNewEntry = NULL;
		errorTrackerSendStatusUpdate(link, STATE_ERRORTRACKER_PARSEENTRY);
		CB_ProcessErrorData(link, pIncomingData->pErrorData);
	}
}



// Initialization

#define NUM_INCOMING_DATATYPES 8
static IncomingDataType sIncomingDataTypes[NUM_INCOMING_DATATYPES] = {
	INCOMINGDATATYPE_ERRORDATA,
	INCOMINGDATATYPE_LINK_DROPPED,
	INCOMINGDATATYPE_MINIDUMP_RECEIVED,
	INCOMINGDATATYPE_FULLDUMP_RECEIVED,
	INCOMINGDATATYPE_MEMORYDUMP_RECEIVED,
	INCOMINGDATATYPE_DUMP_DESCRIPTION_RECEIVED,
	INCOMINGDATATYPE_DUMP_CANCELLED,
	INCOMINGDATATYPE_DUMP_DESCRIPTION_UPDATE,
};
static IncomingDataHandler sIncomingDataHandlers[NUM_INCOMING_DATATYPES] = {
	CB_IncomingErrorData,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

AUTO_RUN;
void CB_InitializeIncomingDataFunctions(void)
{
	int i;
	for (i=0; i<NUM_INCOMING_DATATYPES; i++)
	{
		ETIncoming_SetDataTypeHandler(sIncomingDataTypes[i], sIncomingDataHandlers[i]);
	}
}

// ---------------------------------------------------------

void CB_ErrorTrackerInit(U32 uOptions, U32 uWebOptions)
{
	errorTrackerLibSetOptions(uOptions);
	objSetContainerSourceToHogFile(STACK_SPRINTF("%serrortracker.hogg", errorTrackerGetDatabaseDir()),0,NULL,NULL);
	objRegisterNativeSchema(GLOBALTYPE_ERRORTRACKERENTRY, parse_ErrorEntry, 
		NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_ERRORTRACKERENTRY_LAST, parse_ErrorEntry, 
		NULL, NULL, NULL, NULL, NULL);

	ETWeb_InitWebRootDirs();
	ETShared_SetFQDN(ORGANIZATION_DOMAIN);
	ETWeb_SetSourceDir("c:\\Night\\tools\\ContinuousBuilder\\webroot\\custom");
	ETWeb_SetDefaultPage("/ContinuousBuilder");

	if(gErrorTrackerSettings.iWebInterfacePort == 0)
		gErrorTrackerSettings.iWebInterfacePort = 80;
	if(gErrorTrackerSettings.pDumpDir == NULL)
		estrPrintf(&gErrorTrackerSettings.pDumpDir, "%sdumps", ETWeb_GetDataDir());

	CB_InitializeErrorTrackerContexts();
	ETCommonInit();
	wiSetFlags(uWebOptions);
	printf("\nErrorTracker Web Address: http://%s/\n", getMachineAddress());
}
