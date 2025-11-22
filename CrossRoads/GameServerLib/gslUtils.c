#include "file.h"
#include "earray.h"
#include "utils.h"
#include "utilitiesLib.h"
#include "time.h"
#include "wininclude.h"
#include "rand.h"
#include "logging.h"
#include "textParser.h"
#include "GslUtils_c_ast.h"
#include "stringUtil.h"
#include "GslUtils.h"
#include "Entity.h"
#include "ExprHttp.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "ExpressionFunc.h"
#include "ContinuousBuilderSupport.h"
#include "TimedCallback.h"
#include "GlobalTypes.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"


#define CATEGORY_DISTRIBUTION_FILE "server/CategoryDistributionForLogStressTest.txt"

//log server stress test mode... gameservers load in a bunch of actual log lines which they use as
//a template to randomly generate log lines at a tunable rate, to stress test the log server

static char **sppLoadedTemplateLogLines = NULL;
static int siLogsPerSecond = 0;
static float sfLogsPerMillisecond = 0.0f;

//munge this percentage of alphanum/digit characters in order to preserve entropy
static float sfMungePercent = 0.15f;


static U32 *spCategoryRandomizationTable = NULL;

char randomAlpha(void)
{
	int iInt = randomIntRange(0, 51);
	if (iInt > 25)
	{
		return 'a' - 26 + iInt;
	}

	return 'A' + iInt;
}

char randomDigit(void)
{
	return '0' + randomIntRange(0,9);
}

enumLogCategory GetRandomLogCategory(void)
{
	if (spCategoryRandomizationTable)
	{
		int iIndex = randomIntRange(0, ea32Size(&spCategoryRandomizationTable) - 1);
		return spCategoryRandomizationTable[iIndex];
	}

	return randomIntRange(0, LOG_LAST - 1);
}

static void DoALog(void)
{
	static char *spTestLine = NULL;
	int iIndex = randomIntRange(0, eaSize(&sppLoadedTemplateLogLines) - 1);
	int i;
	int iLen;
	char *pRawLine = sppLoadedTemplateLogLines[iIndex] + 24;
	char *pColon = strchr(pRawLine, ':');
	assertmsgf(pColon, "Template log line for stress test badly formatted: %s", pRawLine);
	estrCopy2(&spTestLine, pColon + 2);
	iLen = strlen(spTestLine);

	for (i = 0; i < iLen; i++)
	{
		char c = spTestLine[i];
		if (isalpha(c) && randomPositiveF32() < sfMungePercent)
		{
			spTestLine[i] = randomAlpha();
		}
		else if (isdigit(c) && randomPositiveF32() < sfMungePercent)
		{
			spTestLine[i] = randomDigit();
		}
	}

	log_printf(GetRandomLogCategory(), "STRESSTEST: %s", spTestLine);

}

static void LogServerStressTestModeTickFunction(void)
{
	static S64 siLastTime = 0;
	S64 iTimePassed;
	S64 iCurTime;

	float fLogsPerThisTimePeriod;
	double fProbabilityOfALog;
	U32 iProbabilityOfALog_Billions;
	int iCounter = 0;

	if (!siLastTime)
	{
		siLastTime = timeGetTime();
		return;
	}

	iCurTime = timeGetTime();
	iTimePassed = iCurTime - siLastTime;
	siLastTime = iCurTime;

	fLogsPerThisTimePeriod = sfLogsPerMillisecond * iTimePassed;

	//if fLogsPerThisTimePeriod > 5 then abandon the fancy random math and do less fancy random math
	//due to the RNG breaking down 
	if (fLogsPerThisTimePeriod > 5.0f)
	{
		int i;
		int iNumToDo = fLogsPerThisTimePeriod * (randomPositiveF32() * 0.4f + 0.8f);

		for (i = 0; i < iNumToDo; i++)
		{
			DoALog();
		}
	}
	else
	{



		fProbabilityOfALog = ((double)fLogsPerThisTimePeriod) / (double)(1.0f + fLogsPerThisTimePeriod);

		//some clever math here... suppose we want an average of 1 log per millisecond. The probability will be 0.5 (1 / 1+1). But
		//there's always a chance of more logs, as we check the probability over and over again. So half the time there will be zero logs,
		//half the time there will be at least one log. But half of THAT time there will be two logs, and half of THAT time there
		//will be three logs, etc.

		iProbabilityOfALog_Billions = 1000000000 * fProbabilityOfALog;

		//cap at 100 per tick just to be safe
		while ((U32)randomIntRange(0, 1000000000) < iProbabilityOfALog_Billions && iCounter++ < 100)
		{
			DoALog();
		}
	}
}

AUTO_STRUCT;
typedef struct CategoryDistributionNode
{
	enumLogCategory eCategory;
	int iCount;
	int iRatio; 
} CategoryDistributionNode;


static void LoadCategoryDistributions(void)
{
	char *pBuf = fileAlloc(CATEGORY_DISTRIBUTION_FILE, NULL);
	char **ppLines = NULL;
	S64 iTotal = 0;
	S64 iTotalRatio = 0;
	
	CategoryDistributionNode **ppNodes = NULL;

	DivideString(pBuf, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	
	FOR_EACH_IN_EARRAY(ppLines, char, pLine)
	{
		char **ppWords = NULL;
		CategoryDistributionNode *pNode;
		DivideString(pLine, " ", &ppWords, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		assertmsgf(eaSize(&ppWords) == 2, "Unable to parse line %s from %s", pLine, CATEGORY_DISTRIBUTION_FILE);
		
		pNode = StructCreate(parse_CategoryDistributionNode);
		pNode->eCategory = StaticDefineInt_FastStringToInt(enumLogCategoryEnum, ppWords[0], -1);

		assertmsgf(pNode->eCategory != -1, "Unrecognized category %s from %s", ppWords[0], CATEGORY_DISTRIBUTION_FILE);

		if (!StringToInt_Paranoid(ppWords[1], &pNode->iCount))
		{
			assertmsgf(0, "Unable to parse line %s from %s", pLine, CATEGORY_DISTRIBUTION_FILE);
		}

		eaPush(&ppNodes, pNode);

		iTotal += pNode->iCount;

		eaDestroyEx(&ppWords, NULL);
	}
	FOR_EACH_END;

	eaDestroyEx(&ppLines, NULL);

	FOR_EACH_IN_EARRAY(ppNodes, CategoryDistributionNode, pNode)
	{
		int i;

		pNode->iRatio = ((S64)(pNode->iCount)) * 1000 / iTotal;
		if (pNode->iRatio == 0)
		{
			pNode->iRatio = 1;
		}

		for (i = 0; i < pNode->iRatio; i++)
		{
			ea32Push(&spCategoryRandomizationTable, pNode->eCategory);
		}
	
	}
	FOR_EACH_END;

	eaDestroyStruct(&ppNodes, parse_CategoryDistributionNode);
}
AUTO_COMMAND_REMOTE;
void BeginLogServerStressTestMode(int iLogsPerSecond)
{
	if (!sppLoadedTemplateLogLines)
	{
		char *pBuf = fileAlloc("server/SampleLogsForStressTest.txt", NULL);
		assertmsgf(pBuf, "Couldn't load server/SampleLogsForStressTest.txt... can't do log server stress test");
		DivideString(pBuf, "\n", &sppLoadedTemplateLogLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		free(pBuf);

		UtilitiesLib_AddExtraTickFunction(LogServerStressTestModeTickFunction);

		if (fileExists(CATEGORY_DISTRIBUTION_FILE))
		{
			LoadCategoryDistributions();
		}
	}

	siLogsPerSecond = iLogsPerSecond;
	sfLogsPerMillisecond = ((float)siLogsPerSecond) / 1000.0f;
}


static bool sbDoingTransServerStressTest = false;
static int siTransPerSecond = 0;
static int siBytesPerTrans = 0;

static F32 randomFloatInRange(F32 min, F32 max)
{
	float mag = max - min;
	float randFloat = min + (randomPositiveF32() * mag);

	return randFloat;
}

//n-1 out of will be short, this is n
#define SHORT_STRING_RATIO 1000

#define SHORT_STRING 6
#define LONG_STRING ((siBytesPerTrans * SHORT_STRING_RATIO) - (SHORT_STRING_RATIO - 1) * SHORT_STRING)

static char *TransServerStressTestString(void)
{
	static char *spRetVal = NULL;
	int iLen; 
	int i;
	static U32 siCounter = SHORT_STRING_RATIO;

	siCounter--;

	if (siCounter == 0)
	{
		iLen = LONG_STRING;
		siCounter = SHORT_STRING_RATIO * randomFloatInRange(0.5f, 1.5f);
	}
	else
	{
		iLen = SHORT_STRING;
	}

	iLen *= randomFloatInRange(0.7f, 1.3f);

	estrSetSize(&spRetVal, iLen);
	for (i = 0; i < iLen; i++)
	{
		spRetVal[i] = randomIntRange('a', 'z');
	}

	return spRetVal;
}


//average gap of .1 seconds between callbacks
static float GetTransServerStressTestInterval(void)
{
	return randomFloatInRange(0.05f, 0.15f);
}

AUTO_COMMAND_REMOTE;
void DoNothingWithThisString(char *pStr)
{

}

AUTO_COMMAND_REMOTE;
char *ReturnThisString(char *pStr)
{
	return pStr;
}


#include "autogen\GameServerLib_autotransactions_autogen_wrappers.h"
#include "Entity.h"
#include "Player.h"
#include "entity_h_ast.h"
#include "Player_h_ast.h"
#include "rewardCommon.h"
#include "RewardCommon_h_ast.h"

typedef struct TransactionReturnVal TransactionReturnVal;

static void TransServerStressTest_ReturnCB(TransactionReturnVal *returnVal, void *pUserData)
{
	int iBrk = 0;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt1, "pPlayer.eaRewardMods[]")
ATR_LOCKS(pEnt2, "pPlayer.eaRewardMods[]");
enumTransactionOutcome atr_StressTestRewardModifierTrans(ATR_ARGS, NOCONST(Entity) *pEnt1, NOCONST(Entity) *pEnt2,
	char *pIndex1, char *pIndex2, float fFactor)
{
	NOCONST(RewardModifier) *pReward1 = eaIndexedGetUsingString(&pEnt1->pPlayer->eaRewardMods, pIndex1);
	NOCONST(RewardModifier) *pReward2 = eaIndexedGetUsingString(&pEnt2->pPlayer->eaRewardMods, pIndex2);

	if (pReward1 && pReward2)
	{
		float fTemp = pReward1->fFactor;
		pReward1->fFactor = pReward2->fFactor;
		pReward2->fFactor = fTemp;

		TRANSACTION_RETURN_SUCCESS("Swapped the two values");
	}

	if (!pReward2)
	{
		pReward2 = (NOCONST(RewardModifier)*)StructCreate(parse_RewardModifier);
		pReward2->pchNumeric = allocAddString(pIndex2);
		pReward2->fFactor = fFactor * 2;
		eaIndexedPushUsingStringIfPossible(&pEnt2->pPlayer->eaRewardMods, pIndex2, pReward2);
	}

	if (!pReward1)
	{
		pReward1 = (NOCONST(RewardModifier)*)StructCreate(parse_RewardModifier);
		pReward1->pchNumeric = allocAddString(pIndex1);
		pReward1->fFactor = fFactor;
		eaIndexedPushUsingStringIfPossible(&pEnt1->pPlayer->eaRewardMods, pIndex1, pReward1);
	}

	TRANSACTION_RETURN_SUCCESS("Added one or more value");	
}
	

void DoRewardModifierTestTrans(void)
{
	int iID1 = randomIntRange(1,1000);
	int iID2 = (iID1 + randomIntRange(1,999));
	char key1[16], key2[16];
	int iVal;

	sprintf(key1, "%c%c", randomIntRange('a', 'z'), randomIntRange('a', 'z'));
	sprintf(key2, "%c%c", randomIntRange('a', 'z'), randomIntRange('a', 'z'));
	
	iVal = randomIntRange(1,100);

	if (iID2 > 1000)
	{
		iID2 -= 1000;
	}

	AutoTrans_atr_StressTestRewardModifierTrans(objCreateManagedReturnVal(TransServerStressTest_ReturnCB, NULL),
		GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, iID1, GLOBALTYPE_ENTITYPLAYER, iID2, key1, key2, iVal);
}




static void TransServerStressTestCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int iNumToDoThisFrame = siTransPerSecond / 10;
	int i;

	TimedCallback_Run(TransServerStressTestCB, NULL, GetTransServerStressTestInterval());

	iNumToDoThisFrame *= randomFloatInRange(0.5f, 1.5f);

	for (i = 0; i < iNumToDoThisFrame; i++)
	{
		DoRewardModifierTestTrans();
		/*
		char *pStr = TransServerStressTestString();


		if (randomIntRange(1,100) < 30)
		{
			RemoteCommand_DoNothingWithThisString(GLOBALTYPE_GAMESERVER, GetAppGlobalID(), pStr);
		}
		else
		{
			RemoteCommand_ReturnThisString(objCreateManagedReturnVal(TransServerStressTest_ReturnCB, NULL),
				GLOBALTYPE_GAMESERVER, GetAppGlobalID(), pStr);
		}*/

		
	}
}

AUTO_COMMAND;
void LongStringTest(void)
{
	char *pStr = NULL;
	int i;

	EnableLocalTransactions(false);

	for (i = 0; i < 1024 * 1024; i++)
	{
		estrConcatChar(&pStr, randomIntRange('a', 'z'));
	}

	RemoteCommand_DoNothingWithThisString(GLOBALTYPE_GAMESERVER, GetAppGlobalID(), pStr);
	
	RemoteCommand_ReturnThisString(objCreateManagedReturnVal(TransServerStressTest_ReturnCB, NULL),
		GLOBALTYPE_GAMESERVER, GetAppGlobalID(), pStr);

	estrDestroy(&pStr);
}

AUTO_COMMAND_REMOTE;
void BeginTransServerStressTestMode(char *pSettingString)
{
	sscanf(pSettingString, "%d %d", &siTransPerSecond, &siBytesPerTrans);

	EnableLocalTransactions(false);

	if (!sbDoingTransServerStressTest)
	{
		printfColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT, "Beginning trans server stress test: %d, %d\n", 
			siTransPerSecond, siBytesPerTrans);
		TimedCallback_Run(TransServerStressTestCB, NULL, GetTransServerStressTestInterval());
	}
	else
	{
		printfColor(COLOR_RED | COLOR_BLUE , "Changing variables for trans server stress test: %d, %d\n", 
			siTransPerSecond, siBytesPerTrans);
	}


	sbDoingTransServerStressTest = true;
}


bool GslIsDoingLoginServerOrTransServerStressTest(void)
{
	return !!sppLoadedTemplateLogLines || sbDoingTransServerStressTest;
}


//for the code in exprHttp.c/.h which sets up serverMonitoring for Expression functions
void OVERRIDE_LATELINK_GetLocationNameForExpressionFuncServerMonitoring(char **ppOutString)
{
	estrCopy2(ppOutString, "Ser");
}

AUTO_COMMAND ACMD_SERVERCMD;
void RequestExprFuncsForServerMonitoring(Entity *pEnt)
{
	ExpressionFuncForServerMonitorList *pList = StructCreate(parse_ExpressionFuncForServerMonitorList);
	BeginExpressionServerMonitoring();

	FOR_EACH_IN_STASHTABLE(sFuncTablesByName, ExprFuncTable, pFuncTable)
	{
		eaPush(&pList->ppFuncTableNames, pFuncTable->pName);
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(sAllExpressionsByName, ExpressionFuncForServerMonitor, pFunc)
	{
		eaPush(&pList->ppFuncs, pFunc);
	}
	FOR_EACH_END;

	ClientCmd_HereAreExprFuncs(pEnt, pList);

	eaDestroy(&pList->ppFuncs);
	StructDestroy(parse_ExpressionFuncForServerMonitorList, pList);
}

void OVERRIDE_LATELINK_ControllerScript_TemporaryPauseInternal(int iNumSeconds, char *pReason)
{
	//in makebins mode, talk directly to CB.exe... in non-makebins mode no action is necessary
	if(gbMakeBinsAndExit)
	{
		CBSupport_PauseTimeout(iNumSeconds);
	}
}


#include "GslUtils_c_ast.c"
