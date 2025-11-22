#include "AppServerLib.h"
#include "aslQueue.h"
#include "AutoTransDefs.h"
#include "Entity.h"
#include "fileutil.h"
#include "logging.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "partition_enums.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "rand.h"
#include "serverlib.h"
#include "StringCache.h"
#include "Team.h"
#include "PvPGameCommon.h"
#include "ChoiceTable.h"
#include "GlobalTypes.h"
#include "HttpXPathSupport.h"

#include "AutoGen/aslQueueServerMonitor_c_ast.h"


AUTO_STRUCT;
typedef struct QueueServerMonitorDatum
{
	// Name and Val are used to determine the identity of the Datum. 
	const char* Name; AST(POOL_STRING)
	int Val;

	// Total members in the instance, number in InQueue state, and an average of the current wait times for people InQueue
	U32 Tot;
	U32 InQueue;
	U32 AvgWaitingTime;
	
} QueueServerMonitorDatum;

AUTO_STRUCT;
typedef struct QueueServerMonitorInstance
{
	U32 uID;	// Container ID of the instance in the Queue Container
	
	U32 Maps;	// Number of currently running maps for the instance (in any state)
	
	// Total members in the instance, number in InQueue state, and an average of the current wait times for people InQueue
	U32 Members;
	U32 InQueue;
	U32 AvgWaitingTime;

	QueueServerMonitorDatum** RoleData;			// Earray for Role statistics
	QueueServerMonitorDatum** ClassData;		// Earray for Class statistics
	QueueServerMonitorDatum** AffiliationData;	// Earray for Affiliation statistics
	
} QueueServerMonitorInstance;


AUTO_STRUCT;
typedef struct QueueServerMonitorQueue
{
	char* NameLink; AST(ESTRING, FORMATSTRING(HTML=1))
	QueueServerMonitorInstance** eaInstances;	// Aray of queue information
} QueueServerMonitorQueue;

AUTO_STRUCT;
typedef struct QueueServerMonitorInfo
{
	QueueServerMonitorQueue** pQueues;	// Aray of queues
} QueueServerMonitorInfo;

static QueueServerMonitorInfo gQueueServerInfo = {0};

//////////////////////////////////////////

AUTO_STRUCT;
typedef struct QueueServerOverview 
{
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))
	QueueServerMonitorInfo *pCurInfo;
} QueueServerOverview;


//////////////////////////////////////////////////////////////////////////////////////////////////

// Tally that we found a member for the given data array. Keep track of total members, inQueue members, and total the waiting times so we can later compute an average
void QueueServerMonitor_AddDatum(QueueServerMonitorDatum*** peaDataArray, const char* pDatumName, int iDatumValue, bool bInQueue, int iWaitingTime)
{
	int i;
	QueueServerMonitorDatum* pDatum=NULL;

	for (i=0;i<eaSize(peaDataArray);i++)
	{
		QueueServerMonitorDatum* pTestDatum = (*peaDataArray)[i];
		if (pTestDatum->Name==pDatumName && pTestDatum->Val==iDatumValue)
		{
			pDatum = pTestDatum;
			break;
		}
	}

	if (pDatum==NULL)
	{
		// Didn't have this one yet. Make a new one
		pDatum = StructCreate(parse_QueueServerMonitorDatum);
		pDatum->Name=pDatumName;
		pDatum->Val=iDatumValue;
		eaPush(peaDataArray, pDatum);
	}

	pDatum->Tot++;
	if (bInQueue)
	{
		pDatum->InQueue++;
		pDatum->AvgWaitingTime+=iWaitingTime; // (We'll divide this by InQueue when we're done gathering)
	}
}

// Calculate the waiting average. This isn't really right. It's just based on the average of the times that people have currently been waiting.
// We have no waiting time history so it would be difficult to do a more complicated calculation at this point on the existing data.
void QueueServerMonitor_CalcDataWaitAverage(QueueServerMonitorDatum*** peaDataArray)
{
	int i;
	QueueServerMonitorDatum* pDatum=NULL;
	
	for (i=0;i<eaSize(peaDataArray);i++)
	{
		pDatum = (*peaDataArray)[i];

		if (pDatum->InQueue>0)
		{
			pDatum->AvgWaitingTime = 1+(int)((float)(pDatum->AvgWaitingTime) / (60.0f*(float)pDatum->InQueue));
		}
	}
}

// These two functions are NNO specific for now. STO won't need the info from them anyway since they are not using roles/classes.
// Refactor all of this if we make roles/classes into real things with defs, etc.
static const char* _NNO_RoleNames(int iRole)
{
	switch (iRole)
	{
		case 1: return(allocAddString("Tank"));
		case 2: return(allocAddString("Heal"));
		case 3: return(allocAddString("DPS"));
	}
   return(allocAddString("Other"));
}
static const char* _NNO_ClassNames(int iClass)
{
	switch (iClass)
	{
		case 1: return(allocAddString("Guardian"));
		case 2: return(allocAddString("Cleric"));
		case 3: return(allocAddString("GWFight"));
		case 4: return(allocAddString("Wizard"));
		case 5: return(allocAddString("Rogue"));
		case 6: return(allocAddString("Ranger"));
		case 7: return(allocAddString("Warlock"));
	}
   return(allocAddString("Other"));
}

// Update all the instances within a given queue
void QueueServerMonitor_UpdateInstances(QueueInfo *pQueueInfo, QueueServerMonitorQueue* pQueueMonitorInfo)
{
	int iInstanceIdx;
	U32 iCurrentTime = timeSecondsSince2000();

	for (iInstanceIdx = 0; iInstanceIdx < eaSize(&pQueueInfo->eaInstances); iInstanceIdx++)
	{
		QueueInstance* pInstance = pQueueInfo->eaInstances[iInstanceIdx];
		QueueServerMonitorInstance* pQueueMonitorInstanceInfo = StructCreate(parse_QueueServerMonitorInstance);
		S32 iMemberIdx, iMemberCount = eaSize(&pInstance->eaUnorderedMembers);

		eaPush(&pQueueMonitorInfo->eaInstances, pQueueMonitorInstanceInfo);

		pQueueMonitorInstanceInfo->uID = pInstance->uiID;
		pQueueMonitorInstanceInfo->Maps = eaSize(&pInstance->eaMaps);
		pQueueMonitorInstanceInfo->Members = eaSize(&pInstance->eaUnorderedMembers);

		// Tally each member. First overall stats, and then per role/class/affiliation
		for (iMemberIdx = 0; iMemberIdx < iMemberCount; iMemberIdx++)
		{
			QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			bool bInQueue=false;
			S32 iWaitingTime = 0;

			if (pMember->eState==PlayerQueueState_InQueue)
			{
				iWaitingTime = iCurrentTime - pMember->iStateEnteredTime;
				bInQueue=true;

				pQueueMonitorInstanceInfo->InQueue++;
				pQueueMonitorInstanceInfo->AvgWaitingTime += iWaitingTime;
			}

			QueueServerMonitor_AddDatum(&pQueueMonitorInstanceInfo->RoleData, _NNO_RoleNames(pMember->iGroupRole), pMember->iGroupRole, bInQueue, iWaitingTime);
			QueueServerMonitor_AddDatum(&pQueueMonitorInstanceInfo->ClassData, _NNO_ClassNames(pMember->iGroupClass), pMember->iGroupClass, bInQueue, iWaitingTime);
			QueueServerMonitor_AddDatum(&pQueueMonitorInstanceInfo->AffiliationData, pMember->pchAffiliation, 0, bInQueue, iWaitingTime);
		}

		// Done tallying. Now calculate the waiting average. This isn't really right. It's just based on the average of the times that people have been waiting.
		if (pQueueMonitorInstanceInfo->InQueue>0)
		{
			// Average and convert to minutes
			pQueueMonitorInstanceInfo->AvgWaitingTime = 1+(int)((float)(pQueueMonitorInstanceInfo->AvgWaitingTime) / (60.0f*(float)pQueueMonitorInstanceInfo->InQueue));
		}
		QueueServerMonitor_CalcDataWaitAverage(&pQueueMonitorInstanceInfo->RoleData);
		QueueServerMonitor_CalcDataWaitAverage(&pQueueMonitorInstanceInfo->ClassData);
		QueueServerMonitor_CalcDataWaitAverage(&pQueueMonitorInstanceInfo->AffiliationData);
	}
}

// Update the displayed data from each queue container.
void QueueServerMonitor_UpdateData()
{
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;

	StructReset(parse_QueueServerMonitorInfo, &gQueueServerInfo);

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);

	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueServerMonitorQueue* pQueueMonitorInfo = StructCreate(parse_QueueServerMonitorQueue);
		eaPush(&gQueueServerInfo.pQueues, pQueueMonitorInfo);

		// The name of the queue set up as a link to the generic QueueInfo page.
		estrPrintf(&pQueueMonitorInfo->NameLink, "<a href=\"%s%sQueueinfo[%d]\">%s</a>",
						LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME,
						pQueue->iContainerID,  pQueue->pchName);

		QueueServerMonitor_UpdateInstances(pQueue, pQueueMonitorInfo);
	}
	objClearContainerIterator(&queueIter);
}


// Hooked up in AppServerLib.c
void QueueServer_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static QueueServerOverview overview = {0};

	// Set the info to the static QueueServerInfo
	overview.pCurInfo = &gQueueServerInfo;

	// Reset QueueServerInfo and refill it based on the current data in the queue containers
	QueueServerMonitor_UpdateData();

	// Make sure we have a link back to the generic queue page
	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	*ppTPI = parse_QueueServerOverview;
	*ppStruct = &overview;
}

#include "AutoGen/aslQueueServerMonitor_c_ast.c"

