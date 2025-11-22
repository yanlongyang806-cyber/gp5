#include "AutoTransactionManager.h"
#include "strutils.h"
#include "sourceparser.h"
#include "autorunmanager.h"
#include "estring.h"
#include "earray.h"


//strings which we ignore when they come in any order before a func arg
static char *sAutoTransArgWordsToIgnore[] = 
{
	"const",
//	"NN_PTR_GOOD",
	NULL
};


//"safe simple" functions are ones that we trust we can pass container args to
static char *sAutoTransSafeSimpleFunctionNames[] = 
{
	NULL
};

AutoTransactionManager::AutoTransactionManager()
{
	m_bSomethingChanged = false;
	m_ppFuncs = NULL;

	m_ShortAutoTransactionFileName[0] = 0;
	m_AutoTransactionFileName[0] = 0;
	m_AutoTransactionWrapperHeaderFileName[0] = 0;
	m_AutoTransactionWrapperSourceFileName[0] = 0;
	m_pWhiteListTree = m_pBlackListTree = m_pSuperWhiteListTree = m_pSafeFuncNamesTree = NULL;

	// AutoTrans needs to skip return values - can't skip at the higher level because the other managers may need them.
	static char *pAdditionalSimpleInvisibleTokens[] =
	{
		"SA_PRE_GOOD",
		"SA_PRE_VALID",
		"SA_PRE_NN_STR",
		"SA_PRE_OP_STR",
		"SA_PRE_OP_VALID",
		"SA_PRE_OP_FREE",
		"SA_PRE_OP_NULL",
		"SA_PRE_NN_VALID",
		"SA_PRE_NN_GOOD",
		"SA_PRE_NN_FREE",
		"SA_PRE_NN_NULL",
		"SA_PRE_NN_NN_VALID",
		"SA_PRE_NN_OP_VALID",
		"SA_PRE_OP_OP_VALID",
		"SA_PRE_OP_OP_STR",
		"SA_PRE_NN_NN_STR",
		"SA_PRE_NN_OP_STR",
		"SA_PARAM_GOOD",
		"SA_PARAM_VALID",
		"SA_PARAM_NN_STR",
		"SA_PARAM_OP_STR",
		"SA_PARAM_NN_VALID",
		"SA_PARAM_OP_VALID",
		"SA_PARAM_NN_NN_VALID",
		"SA_PARAM_NN_OP_VALID",
		"SA_PARAM_OP_OP_VALID",
		"SA_PARAM_OP_OP_STR",
		"SA_ORET_GOOD",
		"SA_ORET_VALID",
		"SA_RET_GOOD",
		"SA_RET_VALID",
		"SA_ORET_NN_STR",
		"SA_ORET_OP_STR",
		"SA_RET_NN_STR",
		"SA_RET_OP_STR",
		"SA_ORET_OP_VALID",
		"SA_ORET_NN_VALID",
		"SA_ORET_NN_GOOD",
		"SA_ORET_NN_FREE",
		"SA_ORET_NN_NULL",
		"SA_RET_OP_VALID",
		"SA_RET_NN_VALID",
		"SA_RET_NN_GOOD",
		"SA_RET_NN_FREE",
		"SA_RET_NN_NULL",
		"SA_RET_NN_NN_VALID",
		NULL
	};

	m_pAdditionalSimpleInvisibleTokens = pAdditionalSimpleInvisibleTokens;
}

char *AutoTransactionManager::GetMagicWord(int iWhichMagicWord) 
{	
	switch (iWhichMagicWord)
	{
	case MAGICWORD_AUTOTRANSACTION:
		return "AUTO_TRANSACTION";
	case MAGICWORD_AUTOTRANSHELPER:
		return "AUTO_TRANS_HELPER";
	case MAGICWORD_AUTOTRANSHELPERSIMPLE:
		return "AUTO_TRANS_HELPER_SIMPLE";
	default:
		return "x x";
	}
}


AutoTransactionManager::~AutoTransactionManager()
{

	eaDestroyEx(&m_ppFuncs, (EArrayItemCallback)FreeAutoTransactionFunc);

	StringTree_Destroy(&m_pWhiteListTree);
	StringTree_Destroy(&m_pSuperWhiteListTree);
	StringTree_Destroy(&m_pBlackListTree);
	StringTree_Destroy(&m_pSafeFuncNamesTree);


}



bool AutoTransactionManager::DoesFileNeedUpdating(char *pFileName)
{
	return false;
}
void AutoTransactionManager::FreeAutoTransactionFunc(AutoTransactionFunc *pFunc)
{
	eaDestroyEx(&pFunc->ppArgs, (EArrayItemCallback)DestroyArgStruct);
	eaDestroyEx(&pFunc->simpleArgList.ppSimpleArgs, (EArrayItemCallback)DestroySimpleArg);
	eaDestroyEx(&pFunc->containerArgList.ppContainerArgs, (EArrayItemCallback)DestroyContainerArg);
	eaDestroyEx(&pFunc->earrayUseList.ppEarrayUses, (EArrayItemCallback)DestroyEarrayUse);
	eaDestroyEx(&pFunc->funcCallList.ppFunctionCalls, (EArrayItemCallback)DestroyRecursingFunctionCall);
	eaDestroyEx(&pFunc->expectedLockList.ppExpectedLocks, (EArrayItemCallback)DestroyExpectedLock);
		

	StringTree_Destroy(&pFunc->pCalledFunctionsNameTree);
	free(pFunc);
}


typedef enum
{
	RW_PARSABLE = RW_COUNT,
};

static char *sAutoTransactionReservedWords[] =
{
	"PARSABLE",
	NULL
};

StringTree *spAutoTransactionReservedWordTree = NULL;


void AutoTransactionManager::SetProjectPathAndName(char *pProjectPath, char *pProjectName)
{
	strcpy(m_ProjectName, pProjectName);

	sprintf(m_ShortAutoTransactionFileName, "%s_autotransactions_autogen", pProjectName);
	sprintf(m_AutoTransactionFileName, "%s\\AutoGen\\%s.c", pProjectPath, m_ShortAutoTransactionFileName);
	sprintf(m_AutoTransactionWrapperHeaderFileName, "%s\\..\\Common\\AutoGen\\%s_wrappers.h", pProjectPath, m_ShortAutoTransactionFileName);
	sprintf(m_AutoTransactionWrapperSourceFileName, "%s\\..\\Common\\AutoGen\\%s_wrappers.c", pProjectPath, m_ShortAutoTransactionFileName);


}


/*
void AutoTransactionManager::AddFieldToLockToArg(ArgStruct *pArg, char *pFieldName, 
	enumAutoTransFieldToLockType eFieldType, 
	enumAutoTransLockType eLockType,
	int iIndexNum, char *pIndexString, char *pFileName, int iLineNum)
{
	FieldToLock *pField = pArg->pFirstFieldToLock;

	while (pField)
	{
		if (strcmp(pField->fieldName, pFieldName) == 0)
		{
			if (pField->eFieldType == eFieldType)
			{			
				if (pField->eLockType == eLockType)
				{
					if (eFieldType == ATR_FTL_NORMAL
						|| eFieldType == ATR_FTL_INDEXED_LITERAL_STRING && strcmp(pField->indexString, pIndexString) == 0
						|| pField->iIndexNum == iIndexNum)
					{
						return;
					}
				}
				else if (pField->eFieldType == ATR_FTL_NORMAL && pField->eLockType == ATR_LOCK_ARRAY_OPS)
				{
					if (eLockType == ATR_LOCK_NORMAL)
					{
						pField->eLockType = ATR_LOCK_NORMAL;
						return;
					}
				}
				else if (pField->eFieldType == ATR_FTL_NORMAL && pField->eLockType == ATR_LOCK_NORMAL)
				{
					if (eLockType == ATR_LOCK_ARRAY_OPS)
					{
						// Normal takes priority over array ops
						return;
					}
				}
			}
		}

		pField = pField->pNext;
	}

	pField = new FieldToLock;

	memset(pField, 0, sizeof(FieldToLock));

	strcpy(pField->fieldName, pFieldName);
	pField->eFieldType = eFieldType;
	pField->eLockType = eLockType;
	if (pField->eFieldType == ATR_FTL_INDEXED_LITERAL_STRING)
	{
		strcpy(pField->indexString, pIndexString);
	}
	else
	{
		pField->iIndexNum = iIndexNum;
	}
	
	strcpy(pField->fileName, pFileName);
	pField->iLineNum = iLineNum;

	
	pField->pNext = pArg->pFirstFieldToLock;
	pArg->pFirstFieldToLock = pField;
	
}*/



bool AutoTransactionManager::IsFullContainerFunction(char *pFuncName, int iArgNum)
{
	if ((strcmp(pFuncName, "StructCopyFields") == 0 || strcmp(pFuncName, "StructCopyAll") == 0) &&
		(iArgNum == 1 || iArgNum == 2))
	{
		return true;
	}

	if (strcmp(pFuncName, "StructReset") == 0 && iArgNum == 1)
	{
		return true;
	}

	return false;
}


static char *spNeverHasSideEffectFuncs[] = 
{
	"ISNULL",
	"NONNULL",
	NULL
};


bool AutoTransactionManager::FunctionNeverHasSideEffects(char *pFuncName)
{
	if (StringIsInList(pFuncName, spNeverHasSideEffectFuncs))
	{
		return true;
	}

	return false;
}


static char *spArrayOpsFuncs[] = 
{
	"eaIndexedAdd",
	"eaRemove",
	"eaFindAndRemove",
	NULL
};


bool AutoTransactionManager::FunctionIsArrayOperation(char *pFuncName)
{
	if (StringIsInList(pFuncName, spArrayOpsFuncs))
	{
		return true;
	}

	return false;
}

static char *spSpecialArrayOpsFuncs[] = 
{
	"eaSize",
	NULL
};

//certain earray operations are special in that, when applied to an earray of containers
//at the root level of an ATR func, or that earray passed down into a helper, they have
//no side effects at all, but when called on foo.ppArray they become an ARRAYOPS lock
bool AutoTransactionManager::FunctionIsSpecialArrayOperation(char *pFuncName)
{
	if (StringIsInList(pFuncName, spSpecialArrayOpsFuncs))
	{
		return true;
	}

	return false;
}

static char *spSimpleDerefFuncs[] = 
{
	"SAFE_MEMBER",
	"SAFE_MEMBER2",
	"SAFE_MEMBER3",
	"SAFE_MEMBER4",
	"SAFE_GET_REF",
	"SAFE_GET_REF2",
	NULL
};


//returns true for FOO where FOO(struct,x,y,z) should just be treated as struct.x.y.z
bool AutoTransactionManager::IsSimpleDereferenceFunction(char *pFuncName)
{	
	if (StringIsInList(pFuncName, spSimpleDerefFuncs))
	{
		return true;
	}

	return false;
}
/*
void AutoTransactionManager::AddRecurseFunctionToArg(ArgStruct *pArg, int iArgNum, char *pFuncName, bool bIsATRRecurse, char *pFieldString, char *pFileName, int iLineNum)
{
	
	
	RecursingFunction *pFunc = pArg->pFirstRecursingFunction;

	while (pFunc)
	{
		if (strcmp(pFunc->functionName, pFuncName) == 0 
			&& pFunc->iArgNum == iArgNum
			&& pFunc->bIsRecursingATRFunction == bIsATRRecurse
			&& strcmp(pFunc->fieldString, pFieldString) == 0)
		{
			return;
		}

		pFunc = pFunc->pNext;
	}

	pFunc = new RecursingFunction;

	strcpy(pFunc->functionName, pFuncName);
	pFunc->iArgNum = iArgNum;
	pFunc->bIsRecursingATRFunction = bIsATRRecurse;
	strcpy(pFunc->fieldString, pFieldString ? pFieldString : "");

	strcpy(pFunc->fileName, pFileName);
	pFunc->iLineNum = iLineNum;

	pFunc->pNext = pArg->pFirstRecursingFunction;
	pArg->pFirstRecursingFunction = pFunc;
	
}
*/


bool AutoTransactionManager::LoadStoredData(bool bForceReset)
{
	if (bForceReset)
	{
		m_bSomethingChanged = true;
		return false;
	}

	Tokenizer tokenizer;

	if (!tokenizer.LoadFromFile(m_AutoTransactionFileName))
	{
		m_bSomethingChanged = true;
		return false;
	}

	if (!tokenizer.IsStringAtVeryEndOfBuffer("#endif"))
	{
		m_bSomethingChanged = true;
		return false;
	}

	tokenizer.SetExtraReservedWords(sAutoTransactionReservedWords, &spAutoTransactionReservedWordTree);

	Token token;
	enumTokenType eType;

	do
	{
		eType = tokenizer.GetNextToken(&token);
		STATICASSERT(eType != TOKEN_NONE, "AUTOTRANS file corruption");
	} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_PARSABLE));

	tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of auto transactions");

	int iNumFuncs = token.iVal;

	int iFuncNum;

	for (iFuncNum = 0; iFuncNum < iNumFuncs; iFuncNum++)
	{
		AutoTransactionFunc *pFunc = StructCalloc(AutoTransactionFunc);


		eaPush(&m_ppFuncs, pFunc);
		char escapedString[TOKENIZER_MAX_STRING_LENGTH];

		pFunc->eFlags = tokenizer.AssertGetInt();
		tokenizer.AssertGetString(pFunc->functionName, sizeof(pFunc->functionName));
		tokenizer.AssertGetString(pFunc->sourceFileName, sizeof(pFunc->sourceFileName));
		pFunc->iSourceFileLineNum = tokenizer.AssertGetInt();
		tokenizer.AssertGetString(escapedString, sizeof(escapedString));

		pFunc->pCalledFunctionsNameTree = StringTree_Create();
		if (!StringTree_ReadFromEscapedString(pFunc->pCalledFunctionsNameTree, escapedString))
		{
			tokenizer.AssertFailedf("Invalid string tree escaped string");
		}		

		int iNumArgs = tokenizer.AssertGetInt();

		int iArgNum;

		for (iArgNum=0; iArgNum < iNumArgs; iArgNum++)
		{
			ArgStruct *pArg = StructCalloc(ArgStruct);
			eaPush(&pFunc->ppArgs, pArg);

			pArg->eArgType = (enumAutoTransArgType)tokenizer.AssertGetInt();
			pArg->eArgFlags = (enumArgFlags)tokenizer.AssertGetInt();
			tokenizer.AssertGetString(pArg->argTypeName, sizeof(pArg->argTypeName));
			tokenizer.AssertGetString(pArg->argName, sizeof(pArg->argName));
		}

		ReadSimpleArgListFromTokenizer(&tokenizer, &pFunc->simpleArgList);
		ReadContainerArgListFromTokenizer(&tokenizer, &pFunc->containerArgList);
		ReadEarrayUseListFromTokenizer(&tokenizer, &pFunc->earrayUseList);
		ReadFuncCallListFromTokenizer(&tokenizer, &pFunc->funcCallList);

	}



	m_bSomethingChanged = false;

	return true;
}

void AutoTransactionManager::ResetSourceFile(char *pSourceFileName)
{
	int i = 0;

	while (i < eaSize(&m_ppFuncs))
	{
		if (AreFilenamesEqual(m_ppFuncs[i]->sourceFileName, pSourceFileName))
		{
			FreeAutoTransactionFunc(m_ppFuncs[i]);
			eaRemove(&m_ppFuncs, i);

			m_bSomethingChanged = true;
		}
		else
		{
			i++;
		}
	}

	i = 0;
}

void AutoTransactionManager::FuncAssert(AutoTransactionFunc *pFunc, bool bExpression, char *pErrorString)
{
	if (!bExpression)
	{
		printf("%s(%d) : error S0000 : (StructParser) %s\n", pFunc->sourceFileName, pFunc->iSourceFileLineNum, pErrorString);
		fflush(stdout);
		BreakIfInDebugger();
		Sleep(100);
		exit(1);
	}
}


void AutoTransactionManager::FuncAssertf(AutoTransactionFunc *pFunc, bool bExpression, char *pErrorString, ...)
{
	char buf[1000] = "";
	va_list ap;

	if (bExpression)
	{
		return;
	}

	va_start(ap, pErrorString);
	if (pErrorString)
	{
		vsprintf(buf, pErrorString, ap);
	}
	va_end(ap);

	FuncAssert(pFunc, bExpression, buf);
}



char *AutoTransactionManager::GetTargetCodeArgTypeMacro(AutoTransactionFunc *pFunc, ArgStruct *pArg)
{
	switch (pArg->eArgType)
	{
	case ATR_ARGTYPE_INT:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "ATR_ARG_INT_PTR";
		}
		return "ATR_ARG_INT";

	case ATR_ARGTYPE_INT64:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "ATR_ARG_INT64_PTR";
		}
		return "ATR_ARG_INT64";

	case ATR_ARGTYPE_FLOAT:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "ATR_ARG_FLOAT_PTR";
		}
		return "ATR_ARG_FLOAT";

	case ATR_ARGTYPE_STRING:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "ATR_ARG_STRING";
		}
		FuncAssert(pFunc, 0, "Invalid function arg type");

	case ATR_ARGTYPE_CONTAINER:
		if (pArg->eArgFlags & ARGFLAG_ISEARRAY)
		{
			return "ATR_ARG_CONTAINER_EARRAY";
		}
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "ATR_ARG_CONTAINER";
		}
		FuncAssert(pFunc, 0, "Invalid function arg type");

	case ATR_ARGTYPE_STRUCT:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			if (pArg->eArgFlags & ARGFLAG_FOUNDCONST)
			{
				return "ATR_ARG_CONST_STRUCT";
			}
			else
			{
				return "ATR_ARG_STRUCT";
			}
		}
		FuncAssert(pFunc, 0, "Invalid function arg type");
	}

	return NULL;
}


char *AutoTransactionManager::GetTargetCodeArgGettingFunctionName(ArgStruct *pArg)
{
	switch (pArg->eArgType)
	{
	case ATR_ARGTYPE_INT:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "GetATRArg_IntPtr";
		}
		return "GetATRArg_Int";

	case ATR_ARGTYPE_INT64:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "GetATRArg_Int64Ptr";
		}
		return "GetATRArg_Int64";

	case ATR_ARGTYPE_FLOAT:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return "GetATRArg_FloatPtr";
		}
		return "GetATRArg_Float";

	case ATR_ARGTYPE_STRING:
		return "GetATRArg_String";
		

	case ATR_ARGTYPE_CONTAINER:
		if (pArg->eArgFlags & ARGFLAG_ISEARRAY)
		{
			return "GetATRArg_ContainerEArray";
		}
		else
		{
			return "GetATRArg_Container";
		}
		break;

	case ATR_ARGTYPE_STRUCT:
		return "GetATRArg_Struct";
	}	

	return NULL;
}

void AutoTransactionManager::WriteOutAutoGenAutoTransactionPrototype(FILE *pFile, AutoTransactionFunc *pFunc, bool bAppendVersion, bool bDeferredVersion)
{
	int iArgNum;
	//first, write out prototypes
	for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
	{
		ArgStruct *pArg = pFunc->ppArgs[iArgNum];
		
		switch (pArg->eArgType)
		{
		case ATR_ARGTYPE_STRUCT:
			fprintf(pFile, "typedef struct %s %s;\nextern ParseTable parse_%s[];\n",
				pArg->argTypeName, pArg->argTypeName, pArg->argTypeName);
		}
	}
	if (bAppendVersion)
	{
		fprintf(pFile, "int AutoTransAppend_%s(TransactionRequest *pTransactionRequest_In, GlobalType eServerTypeToRunOn", pFunc->functionName);
	}
	else if (bDeferredVersion)
	{
		fprintf(pFile, "int AutoTransDeferred_%s(TransactionReturnVal *pReturnVal, GlobalType eServerTypeToRunOn", pFunc->functionName);
	}
	else
	{
		fprintf(pFile, "int AutoTrans_%s(TransactionReturnVal *pReturnVal, GlobalType eServerTypeToRunOn", pFunc->functionName);
	}

	for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
	{
		ArgStruct *pArg = pFunc->ppArgs[iArgNum];
		
		switch (pArg->eArgType)
		{
		case ATR_ARGTYPE_INT:
			fprintf(pFile, ", int %s", pArg->argName);
			break;

		case ATR_ARGTYPE_INT64:
			fprintf(pFile, ", S64 %s", pArg->argName);
			break;

		case ATR_ARGTYPE_FLOAT:
			fprintf(pFile, ", float %s", pArg->argName);
			break;

		case ATR_ARGTYPE_STRING:
			fprintf(pFile, ", const char *%s", pArg->argName);
			break;

		case ATR_ARGTYPE_CONTAINER:
			if (pArg->eArgFlags & ARGFLAG_ISEARRAY)
			{
				fprintf(pFile, ", GlobalType %s_type, const U32 *const*%s_IDs", pArg->argName, pArg->argName);
			}
			else
			{
				fprintf(pFile, ", GlobalType %s_type, U32 %s_ID", pArg->argName, pArg->argName);
			}
			break;

		case ATR_ARGTYPE_STRUCT:
			fprintf(pFile, ", const %s *%s", pArg->argTypeName, pArg->argName);
			break;
		}
	}

	fprintf(pFile, ")");

}

void AutoTransactionManager::WriteOutAutoGenAutoTransactionBody(FILE *pOutFile, AutoTransactionFunc *pFunc, bool bAppendVersion, bool bDeferredVersion)
{
	WriteOutAutoGenAutoTransactionPrototype(pOutFile, pFunc, bAppendVersion, bDeferredVersion);
	fprintf(pOutFile, "\n{\n\tchar *pATRString = NULL;\n\tint iRetVal;\n\tContainerID iServerIDToRunOn;\t\nPERFINFO_AUTO_START_FUNC();\n\testrStackCreate(&pATRString);\n");
	
	fprintf(pOutFile, "\tif (eServerTypeToRunOn == GetAppGlobalType()) iServerIDToRunOn = GetAppGlobalID(); else iServerIDToRunOn = SPECIAL_CONTAINERID_FIND_BEST_FOR_TRANSACTION;\n");

	fprintf(pOutFile, "\testrConcatf(&pATRString, \"%s \");\n", pFunc->functionName);



	int iArgNum;

	for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
	{
		ArgStruct *pArg = pFunc->ppArgs[iArgNum];

		switch (pArg->eArgType)
		{
		case ATR_ARGTYPE_INT:
			fprintf(pOutFile, "\testrConcatf(&pATRString, \"%%d \", %s);\n", pArg->argName);
			break;

		case ATR_ARGTYPE_INT64:
			fprintf(pOutFile, "\testrConcatf(&pATRString, \"%%\"FORM_LL\"d \", %s);\n\n", pArg->argName);
			break;

		case ATR_ARGTYPE_FLOAT:
			fprintf(pOutFile, "\testrConcatf(&pATRString, \"%%f \", %s);\n", pArg->argName);
			break;

		case ATR_ARGTYPE_STRING:
			fprintf(pOutFile, "\testrConcatf(&pATRString, \"\\\"\");\n");
			fprintf(pOutFile, "\testrAppendEscaped(&pATRString, %s);\n", pArg->argName);
			fprintf(pOutFile, "\testrConcatf(&pATRString, \"\\\" \");\n");
			break;

		case ATR_ARGTYPE_CONTAINER:
			fprintf(pOutFile, "\tdevassert(%s_type == GLOBALTYPE_NONE || GlobalTypeSchemaType(%s_type) == SCHEMATYPE_PERSISTED);\n", pArg->argName, pArg->argName);

			if (pArg->eArgFlags & ARGFLAG_ISEARRAY)
			{
				fprintf(pOutFile, "\t{\n\t\tint i;\n\t\testrConcatf(&pATRString, \"%%s[\", GlobalTypeToName(%s_type));\n\t\tfor (i=0; i < ea32Size(%s_IDs); i++)\n\t\t{\n", pArg->argName, pArg->argName);
				fprintf(pOutFile, "\t\t\testrConcatf(&pATRString, \"%%s%%u\", i > 0 ? \",\" : \"\", (*%s_IDs)[i]);\n\t\t}\n", pArg->argName);
				fprintf(pOutFile, "\testrConcatf(&pATRString, \"] \");\n\t}\n");
			}
			else
			{
				fprintf(pOutFile, "\tif (%s_ID == 0)\n\t\testrConcatf(&pATRString, \"NULL \");\n\telse\n", pArg->argName);
				fprintf(pOutFile, "\t\testrConcatf(&pATRString, \"%%s[%%u] \", GlobalTypeToName(%s_type), %s_ID);\n", pArg->argName, pArg->argName);
			}
			break;

		case ATR_ARGTYPE_STRUCT:
			fprintf(pOutFile, "\tif (%s)\n\t{\n\t\tAutoTrans_WriteLocalStructString(parse_%s, %s, &pATRString);\n\t}\n",
				pArg->argName, pArg->argTypeName, pArg->argName);
			fprintf(pOutFile, "\telse\n\t{\n\t\testrConcatf(&pATRString, \"NULL\");\n\t}\n");
			fprintf(pOutFile, "\testrConcatf(&pATRString, \" \");\n");
			break;
		}
	}

	if (!bAppendVersion)
	{
		fprintf(pOutFile, "\n\tif (isDevelopmentMode()) AutoTrans_VerifyReturnLoggingCompatibility(\"%s\", eServerTypeToRunOn, pReturnVal);\n", pFunc->functionName);
	}


	if (bAppendVersion)
		fprintf(pOutFile, "\n\tiRetVal = objRequestAutoTransaction(pTransactionRequest_In, NULL, eServerTypeToRunOn, iServerIDToRunOn, pATRString);\n");
	else if (bDeferredVersion)
		fprintf(pOutFile, "\n\tiRetVal = objRequestAutoTransactionDeferred(pReturnVal, eServerTypeToRunOn, iServerIDToRunOn, pATRString);\n");
	else
		fprintf(pOutFile, "\n\tiRetVal = objRequestAutoTransaction(NULL, pReturnVal, eServerTypeToRunOn, iServerIDToRunOn, pATRString);\n");
	fprintf(pOutFile, "\testrDestroy(&pATRString);\n\tPERFINFO_AUTO_STOP_FUNC();\n\treturn iRetVal;\n}\n");
}


int AutoTransactionManager::AutoTransactionComparator(const void *p1, const void *p2)
{
	return strcmp((*((AutoTransactionFunc**)p1))->functionName, (*((AutoTransactionFunc**)p1))->functionName);
}

char *DerefLockTypeNames[] = 
{
	"ATR_LOCK_NORMAL", //a non-indexed-lock
	"ATR_LOCK_ARRAY_OPS", //Only lock the keys, because we're adding/subtracting something
	"ATR_LOCK_ARRAY_OPS_SPECIAL", //if this is the root of a list of containers passed into an ATR, do nothing. Otherwise, convert to ATR_LOCK_ARRAY_OPS
};

char *AutoTransactionManager::GetEarrayLockTypeName(enumAutoTransLockType eLockType)
{
	switch (eLockType)
	{
	case ATR_LOCK_NORMAL:
		return "ATR_LOCK_NORMAL";
	case ATR_LOCK_ARRAY_OPS:
		return "ATR_LOCK_ARRAY_OPS";
	case ATR_LOCK_INDEXED_NULLISOK:
		return "ATR_LOCK_INDEXED_NULLISOK";
	case ATR_LOCK_INDEXED_FAILONNULL:
		return "ATR_LOCK_INDEXED_FAILONNULL";
	}

	STATICASSERT(0, "Unknown earray lock type");
	return NULL;
}

char *AutoTransactionManager::GetEarrayIndexTypeName(enumRecurseArgType eArgType)
{
	switch (eArgType)
	{
	//identifier types are fixed up at auto_run time, so as far as the data tables are concerned,
	//they are the same as literals
	case RECURSEARGTYPE_LITERAL_INT:
	case RECURSEARGTYPE_IDENTIFIER_INT:
		return "ATR_INDEX_LITERAL_INT";

	case RECURSEARGTYPE_LITERAL_STRING:
	case RECURSEARGTYPE_IDENTIFIER_STRING:
		return "ATR_INDEX_LITERAL_STRING";
	
	case RECURSEARGTYPE_PARENT_SIMPLE_ARG:
		return "ATR_INDEX_SIMPLE_ARG";
	}
	STATICASSERT(0, "Unknown earray index type");

	return NULL;
}




bool AutoTransactionManager::WriteOutData(void)
{
	int iFuncNum /*, iHelperFuncNum, i*/;

	if (!m_bSomethingChanged)
	{
		return false;
	}

	FILE *pOutFile = fopen_nofail(m_AutoTransactionFileName, "wt");

	fprintf(pOutFile, "#ifndef GAMECLIENT\n//This file contains data and prototypes for auto transactions. It is autogenerated by StructParser\n\n//autogenerated" "nocheckin\n");



	qsort(m_ppFuncs, eaSize(&m_ppFuncs), sizeof(void*), AutoTransactionComparator);


	m_pParent->GetAutoRunManager()->ResetSourceFile("autogen_autotransactions");


	if (eaSize(&m_ppFuncs))
	{
		fprintf(pOutFile, "#include \"autotransdefs.h\"\n");
	}

	if (AtLeastOneNonHelperFunc())
	{
		fprintf(pOutFile, "#include \"objtransactions.h\"\n");
	}

	m_pSafeFuncNamesTree = StringTree_Create();


	for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
	{
		StringTree_AddWord(m_pSafeFuncNamesTree, m_ppFuncs[iFuncNum]->functionName, 0);
	}
	


	for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
	{
		AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

		if (!(pFunc->eFlags & ATRFLAG_IS_HELPER_SIMPLE))
		{
			fprintf(pOutFile, "\n//----------------Stuff relating to %s func %s -----------------------\n", 
				IsHelper(pFunc) ? "HELPER" : "AUTO_TRANS", pFunc->functionName); 

			WriteTimeVerifyAutoTransValidity(pFunc);
			
			if (eaSize(&pFunc->simpleArgList.ppSimpleArgs))
			{
				int i;

				fprintf(pOutFile, "static ATRSimpleArgDef ATR_%s_SimpleArgDefs[] = {\n",
					pFunc->functionName);

				for (i=0 ; i < eaSize(&pFunc->simpleArgList.ppSimpleArgs); i++)
				{
					fprintf(pOutFile, "\t{ \"%s\", %d },\n",
						pFunc->simpleArgList.ppSimpleArgs[i]->argName, pFunc->simpleArgList.ppSimpleArgs[i]->iArgIndex);
				}

				fprintf(pOutFile, "\t{NULL}\n};\n");
			}

			if (eaSize(&pFunc->containerArgList.ppContainerArgs))
			{
				int i;

				for (i=0 ; i < eaSize(&pFunc->containerArgList.ppContainerArgs); i++)
				{
					ContainerArg *pArg = pFunc->containerArgList.ppContainerArgs[i];

					if (eaSize(&pArg->dereferenceList.ppDereferences))
					{
						int j;

						fprintf(pOutFile, "static ATRStaticDereference ATR_%s_%s_StaticDerefs[] = {\n",
							pFunc->functionName, pArg->argName);

						for (j=0; j < eaSize(&pArg->dereferenceList.ppDereferences); j++)
						{
							Dereference *pDereference = pArg->dereferenceList.ppDereferences[j];
							fprintf(pOutFile, "\t{ \"%s\", %s, %d },\n", 
								pDereference->derefString, DerefLockTypeNames[pDereference->eType], pDereference->iLineNum);
						}

						fprintf(pOutFile, "\t{NULL}\n};\n");
					}
				}
						
				for (i=0 ; i < eaSize(&pFunc->containerArgList.ppContainerArgs); i++)
				{
					fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pFunc->containerArgList.ppContainerArgs[i]->argTypeName);
				}

				fprintf(pOutFile, "static ATRContainerArgDef ATR_%s_ContainerArgDefs[] = {\n",
					pFunc->functionName);

				for (i=0 ; i < eaSize(&pFunc->containerArgList.ppContainerArgs); i++)
				{
					fprintf(pOutFile, "\t{ \"%s\", %d, %d, ",
						pFunc->containerArgList.ppContainerArgs[i]->argName, pFunc->containerArgList.ppContainerArgs[i]->iArgIndex,
						(pFunc->containerArgList.ppContainerArgs[i]->eArgFlags & ARGFLAG_ALLOWFULLLOCK) ? 1 : 0 );

					if (pFunc->containerArgList.ppContainerArgs[i]->expectedLocks[0])
					{
						fprintf(pOutFile, "\"%s\", ", pFunc->containerArgList.ppContainerArgs[i]->expectedLocks);
					}
					else
					{
						fprintf(pOutFile, "NULL, ");
					}

					if (eaSize(&(pFunc->containerArgList.ppContainerArgs[i]->dereferenceList.ppDereferences)))
					{
						fprintf(pOutFile, "ATR_%s_%s_StaticDerefs, ",
							pFunc->functionName, pFunc->containerArgList.ppContainerArgs[i]->argName);
					}
					else
					{
						fprintf(pOutFile, "NULL, ");
					}


					fprintf(pOutFile, "false, NULL, NULL, parse_%s},\n", pFunc->containerArgList.ppContainerArgs[i]->argTypeName);
				}

				fprintf(pOutFile, "\t{NULL}\n};\n");
			}


			if (eaSize(&pFunc->earrayUseList.ppEarrayUses))
			{
				int i;

				fprintf(pOutFile, "static ATREarrayUseDef ATR_%s_EarrayUseDefs[] = {\n",
					pFunc->functionName);

				for (i=0; i < eaSize(&pFunc->earrayUseList.ppEarrayUses); i++)
				{
					EarrayUse *pUse = pFunc->earrayUseList.ppEarrayUses[i];

					fprintf(pOutFile, "\t{ %d, \"%s\", %s, %s, ",
						pUse->iContainerArgIndex, pUse->containerArgDerefString,
						GetEarrayLockTypeName(pUse->eLockType),
						GetEarrayIndexTypeName(pUse->eIndexType));

					switch (pUse->eIndexType)
					{
					case RECURSEARGTYPE_LITERAL_INT:
						fprintf(pOutFile, "%d, NULL, ",
							pUse->iVal);
						break;

					case RECURSEARGTYPE_LITERAL_STRING:
						fprintf(pOutFile, "0, \"%s\", ",
							pUse->sVal);
						break;

					case RECURSEARGTYPE_PARENT_SIMPLE_ARG:
						fprintf(pOutFile, "%d, NULL, ",
							pUse->iVal);
						break;

					case RECURSEARGTYPE_IDENTIFIER_INT:
						fprintf(pOutFile, "0 /*this will get filled in with the numeric value of %s at AUTO_RUN time*/, NULL, ",
							pUse->sVal);
						break;
					case RECURSEARGTYPE_IDENTIFIER_STRING:
						fprintf(pOutFile, "0, NULL /*this will get filled in with the string value of %s at AUTO_RUN time*/, ",
							pUse->sVal);
						break;
					}

					fprintf(pOutFile, "%d },\n",
						pUse->iLineNum);

				}
				fprintf(pOutFile, "\t{-1}\n};\n");
			}

			if (eaSize(&pFunc->funcCallList.ppFunctionCalls))
			{
				int i;

				for (i=0; i < eaSize(&pFunc->funcCallList.ppFunctionCalls); i++)
				{
					RecursingFunctionCall *pCall = pFunc->funcCallList.ppFunctionCalls[i];

					if (eaSize(&pCall->recursingContainerArgList.ppRecursingArgs))
					{
						int j;

						fprintf(pOutFile, "static ATRFuncCallContainerArg ATR_%s_%d_FuncCallContainerArgs[] = {\n",
							pFunc->functionName, i);

						for (j=0; j < eaSize(&pCall->recursingContainerArgList.ppRecursingArgs); j++)
						{
							fprintf(pOutFile, "\t{ %d, %d, \"%s\" },\n",
								pCall->recursingContainerArgList.ppRecursingArgs[j]->iParentArgIndex,
								pCall->recursingContainerArgList.ppRecursingArgs[j]->iRecursingArgIndex,
								pCall->recursingContainerArgList.ppRecursingArgs[j]->derefString);
						}

						fprintf(pOutFile, "\t{ -1 }\n};\n");
					}

					if (eaSize(&pCall->recursingSimpleArgList.ppRecursingArgs))
					{
						int j;

						fprintf(pOutFile, "static ATRFuncCallSimpleArg ATR_%s_%d_FuncCallSimpleArgs[] = {\n",
							pFunc->functionName, i);

						for (j=0; j < eaSize(&pCall->recursingSimpleArgList.ppRecursingArgs); j++)
						{
							RecursingFunctionCallSimpleArg *pSimpleArg = pCall->recursingSimpleArgList.ppRecursingArgs[j];

							fprintf(pOutFile, "\t{ %d, %s, ",
								pSimpleArg->iArgIndex, GetEarrayIndexTypeName(pSimpleArg->eType));

							switch (pSimpleArg->eType)
							{
							case RECURSEARGTYPE_LITERAL_INT:
							case RECURSEARGTYPE_PARENT_SIMPLE_ARG:
								fprintf(pOutFile, "%d, NULL},\n", 
									pSimpleArg->iVal);
								break;
							case RECURSEARGTYPE_LITERAL_STRING:
								fprintf(pOutFile, "0, \"%s\"},\n",
									pSimpleArg->sVal);
								break;
							default:
								FuncAssertf(pFunc, 0, "Simple arg to recursing function has invalid type");
								break;
							}
						}
						
						fprintf(pOutFile, "\t{ -1 }\n};\n");
					}
				}

				fprintf(pOutFile, "static ATRFunctionCallDef ATR_%s_FuncCalls[] ={\n",
					pFunc->functionName);

				for (i=0; i < eaSize(&pFunc->funcCallList.ppFunctionCalls); i++)
				{
					RecursingFunctionCall *pCall = pFunc->funcCallList.ppFunctionCalls[i];

					fprintf(pOutFile, "\t{ \"%s\", %d, ",
						pCall->funcName, pCall->iLineNum);

					if (eaSize(&pCall->recursingSimpleArgList.ppRecursingArgs))
					{
						fprintf(pOutFile, "ATR_%s_%d_FuncCallSimpleArgs, ",
							pFunc->functionName, i);
					}
					else
					{
						fprintf(pOutFile, "NULL, ");
					}

					if (eaSize(&pCall->recursingContainerArgList.ppRecursingArgs))
					{
						fprintf(pOutFile, "ATR_%s_%d_FuncCallContainerArgs },\n",
							pFunc->functionName, i);
					}
					else
					{
						fprintf(pOutFile, "NULL },\n");
					}

				}

				fprintf(pOutFile, "\t{NULL}\n};\n");
			}


			if (!IsHelper(pFunc))
			{


				int iArgNum;


				for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
				{
					ArgStruct *pArg = pFunc->ppArgs[iArgNum];

					if (pArg->eArgType == ATR_ARGTYPE_CONTAINER || pArg->eArgType == ATR_ARGTYPE_STRUCT)
					{
						fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pArg->argTypeName);
					}
				}


				fprintf(pOutFile, "static ATRArgDef ATR_%s_ArgDefs[] ={\n", pFunc->functionName);
				
				for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
				{
					ArgStruct *pArg = pFunc->ppArgs[iArgNum];

					fprintf(pOutFile, "\t{ %s, ", GetTargetCodeArgTypeMacro(pFunc, pArg));

					if (pArg->eArgType == ATR_ARGTYPE_CONTAINER || pArg->eArgType == ATR_ARGTYPE_STRUCT)
					{
						fprintf(pOutFile, "parse_%s, \"%s\" },\n", pArg->argTypeName, pArg->argName);
					}
					else
					{
						fprintf(pOutFile, "NULL, \"%s\" },\n", pArg->argName);
					}
				}

				fprintf(pOutFile, "\t{ ATR_ARG_NONE, NULL }\n};\n");


				fprintf(pOutFile, "enumTransactionOutcome %s(ATR_ARGS, ", pFunc->functionName);

				for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
				{
					ArgStruct *pArg = pFunc->ppArgs[iArgNum];


					fprintf(pOutFile, "%s%s %s%s%s", 
						(pArg->eArgType == ATR_ARGTYPE_CONTAINER || pArg->eArgType == ATR_ARGTYPE_STRUCT) ? "struct " : "",
						pArg->argTypeName, 
						(pArg->eArgFlags & ARGFLAG_ISEARRAY) ? "**" : ((pArg->eArgFlags & ARGFLAG_ISPOINTER) ? "*" : ""),
						pArg->argName,
						iArgNum == eaSize(&pFunc->ppArgs) - 1 ? "" : ", ");
				}
				fprintf(pOutFile, ");\n");

				fprintf(pOutFile, "static enumTransactionOutcome ATR_Wrapper_%s(char **pestrSuccess, char **pestrFail){\n", pFunc->functionName);
				for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
				{
					ArgStruct *pArg = pFunc->ppArgs[iArgNum];


					fprintf(pOutFile, "\t%s%s %sarg%d;\n", 
						(pArg->eArgType == ATR_ARGTYPE_CONTAINER || pArg->eArgType == ATR_ARGTYPE_STRUCT) ? "struct " : "",
						pArg->argTypeName, (pArg->eArgFlags & ARGFLAG_ISEARRAY) ? "**" : ((pArg->eArgFlags & ARGFLAG_ISPOINTER) ? "*" : ""), iArgNum);
				}
				
				fprintf(pOutFile, "\tenumTransactionOutcome eRetVal;\n\tPERFINFO_AUTO_START_FUNC();\n");

				for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
				{
					ArgStruct *pArg = pFunc->ppArgs[iArgNum];
			
					if (pArg->eArgType == ATR_ARGTYPE_INT)
					{
						fprintf(pOutFile, "\targ%d = (%s)%s(%d);\n", iArgNum, pArg->argTypeName, GetTargetCodeArgGettingFunctionName(pArg), iArgNum);
					}
					else
					{
						fprintf(pOutFile, "\targ%d = %s(%d);\n", iArgNum, GetTargetCodeArgGettingFunctionName(pArg), iArgNum);
					}
				}


				fprintf(pOutFile, "\n\teRetVal = %s(pestrSuccess, pestrFail, ", pFunc->functionName);

				for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
				{
					fprintf(pOutFile, "arg%d%s", iArgNum, iArgNum == eaSize(&pFunc->ppArgs) -1 ? "" : ", ");
				}

				fprintf(pOutFile, ");\n\tPERFINFO_AUTO_STOP_FUNC(); return eRetVal;\n};\n\n");
			}

			fprintf(pOutFile, "static ATR_FuncDef ATRFuncDef_%s = { \"%s\", \"%s\", \"%s\", ",
				pFunc->functionName, pFunc->functionName, GetFileNameWithoutDirectoriesOrSlashes(pFunc->sourceFileName),
				GetSuspicousFunctionCallNames(pFunc->pCalledFunctionsNameTree));

				

			if (IsHelper(pFunc))
			{
				fprintf(pOutFile, "NULL, NULL, ");
			}
			else
			{
				fprintf(pOutFile, "ATR_Wrapper_%s, ATR_%s_ArgDefs, ",
					pFunc->functionName, pFunc->functionName);
			}

			if (eaSize(&pFunc->simpleArgList.ppSimpleArgs))
			{
				fprintf(pOutFile, "ATR_%s_SimpleArgDefs, ", pFunc->functionName);
			}
			else
			{
				fprintf(pOutFile, "NULL, ");
			}

			if (eaSize(&pFunc->containerArgList.ppContainerArgs))
			{
				fprintf(pOutFile, "ATR_%s_ContainerArgDefs, ", pFunc->functionName);
			}
			else
			{
				fprintf(pOutFile, "NULL, ");
			}

			if (eaSize(&pFunc->earrayUseList.ppEarrayUses))
			{
				fprintf(pOutFile, "ATR_%s_EarrayUseDefs, ", pFunc->functionName);
			}
			else
			{
				fprintf(pOutFile, "NULL, ");
			}

			if (eaSize(&pFunc->funcCallList.ppFunctionCalls))
			{
				fprintf(pOutFile, "ATR_%s_FuncCalls, %d };\n", pFunc->functionName, !!(pFunc->eFlags & ATRFLAG_DOES_RETURN_LOGGING));
			}
			else
			{
				fprintf(pOutFile, " NULL, %d };\n", !!(pFunc->eFlags & ATRFLAG_DOES_RETURN_LOGGING));
			}

		}
	}

	bool bFoundAnIdentifier = false;
	char identifierFixupFuncName[300];

	for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
	{
		AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

		if (eaSize(&pFunc->earrayUseList.ppEarrayUses))
		{
			int i;

			for (i=0; i < eaSize(&pFunc->earrayUseList.ppEarrayUses); i++)
			{
				EarrayUse *pUse = pFunc->earrayUseList.ppEarrayUses[i];

				switch (pUse->eIndexType)
				{
				case RECURSEARGTYPE_IDENTIFIER_INT:
				case RECURSEARGTYPE_IDENTIFIER_STRING:
					if (!bFoundAnIdentifier)
					{
						bFoundAnIdentifier = true;

						sprintf(identifierFixupFuncName, "ATR_%s_IdentifierFixupFunc", m_pParent->GetShortProjectName());

						fprintf(pOutFile, "//Here is where includes for ATR_GLOBAL_SYMBOLs go\n");
					}


					fprintf(pOutFile, "\n#include \"%s\"\n", pUse->globalIncludeFile);
				}
			}
		}
	}

	if (bFoundAnIdentifier)
	{
		fprintf(pOutFile, "void %s(void)\n{\n", identifierFixupFuncName);

		for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
		{
			AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

			if (eaSize(&pFunc->earrayUseList.ppEarrayUses))
			{
				int i;

				for (i=0; i < eaSize(&pFunc->earrayUseList.ppEarrayUses); i++)
				{
					EarrayUse *pUse = pFunc->earrayUseList.ppEarrayUses[i];

					switch (pUse->eIndexType)
					{
					case RECURSEARGTYPE_IDENTIFIER_INT:
						fprintf(pOutFile, "//%s(%d)\n", GetFileNameWithoutDirectoriesOrSlashes(pUse->sourceFileName), pUse->iLineNum);
						fprintf(pOutFile, "\tATR_%s_EarrayUseDefs[%d].iVal = %s;\n",
							pFunc->functionName, i, pUse->sVal);
						break;
					case RECURSEARGTYPE_IDENTIFIER_STRING:
						fprintf(pOutFile, "//%s(%d)\n", GetFileNameWithoutDirectoriesOrSlashes(pUse->sourceFileName), pUse->iLineNum);
						fprintf(pOutFile, "\tATR_%s_EarrayUseDefs[%d].pSVal = strdup(%s);\n",
							pFunc->functionName, i, pUse->sVal);
						break;
					}
				}
			}
		}

		fprintf(pOutFile, "}\n");
	}


	if (eaSize(&m_ppFuncs))
	{
		char autoRunName[256];
		sprintf(autoRunName, "ATR_%s_RegisterAllFuncs_AutoRun", m_ProjectName);

		fprintf(pOutFile, "void %s(void)\n{\n", autoRunName);

	
		for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
		{
			AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

			if (pFunc->eFlags & ATRFLAG_IS_HELPER_SIMPLE)
			{
				fprintf(pOutFile, "\tRegisterSimpleATRHelper(\"%s\", \"%s\");\n", 
					pFunc->functionName, GetSuspicousFunctionCallNames(pFunc->pCalledFunctionsNameTree));
			}
			else
			{
				fprintf(pOutFile, "\tRegisterATRFuncDef(&ATRFuncDef_%s);\n", 
					pFunc->functionName);
			}
		}

		if (bFoundAnIdentifier)
		{
			fprintf(pOutFile, "\tRegisterATRIdentifierFixupFunc(&%s);\n", identifierFixupFuncName);
		}

		fprintf(pOutFile, "};\n");

	
		m_pParent->GetAutoRunManager()->AddAutoRun(autoRunName, "autogen_autotransactions");
	}



	//must be last
	if (eaSize(&m_ppFuncs))
	{
		char autoRunName[256];
		sprintf(autoRunName, "ATR_%s_RegisterAllFuncs_AutoRun", m_ProjectName);

		fprintf(pOutFile, "#else\nvoid %s(void){}\n", autoRunName);
	}
	fprintf(pOutFile, "\n#endif\n#ifdef THIS_SYMBOL_IS_NOT_DEFINED\nPARSABLE\n");

	fprintf(pOutFile, "%d\n", eaSize(&m_ppFuncs));

	for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
	{
		AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];
		
		char *pFuncTreeString = StringTree_CreateEscapedString(pFunc->pCalledFunctionsNameTree);

		fprintf(pOutFile, "%u \"%s\" \"%s\" %d \"%s\" %d ", 
			pFunc->eFlags, pFunc->functionName, pFunc->sourceFileName, pFunc->iSourceFileLineNum, pFuncTreeString, eaSize(&pFunc->ppArgs));

		free(pFuncTreeString);

		int iArgNum;

		for (iArgNum=0; iArgNum < eaSize(&pFunc->ppArgs); iArgNum++)
		{
			ArgStruct *pArg = pFunc->ppArgs[iArgNum];

			fprintf(pOutFile, "%d %d \"%s\" \"%s\" ",
				pArg->eArgType, pArg->eArgFlags, pArg->argTypeName, pArg->argName);
		}
		
		WriteSimpleArgListToFile(pOutFile, &pFunc->simpleArgList);
		WriteContainerArgListToFile(pOutFile, &pFunc->containerArgList);
		WriteEarrayUseListToFile(pOutFile, &pFunc->earrayUseList);
		WriteFuncCallListToFile(pOutFile, &pFunc->funcCallList);
		fprintf(pOutFile, "\n");
	}

	fprintf(pOutFile, "#endif\n");

	fclose(pOutFile);


	if (eaSize(&m_ppFuncs))
	{
		pOutFile = fopen_nofail(m_AutoTransactionWrapperHeaderFileName, "wt");

		fprintf(pOutFile, "#pragma once\nGCC_SYSTEM\n//This autogenerated file contains prototypes for wrapper functions\n//for calling AUTO_TRANSACTIONs from project %s\n//autogenerated" "nocheckin\n#include \"globaltypes.h\"\n#include \"localtransactionmanager.h\"\n", m_ProjectName);

		for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
		{
			AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

			if (!IsHelper(pFunc))
			{
				WriteOutAutoGenAutoTransactionPrototype(pOutFile, pFunc, false, false);
				fprintf(pOutFile, ";\n");
				if (pFunc->eFlags & ATRFLAG_MAKE_APPEND_VERSION)
				{
					WriteOutAutoGenAutoTransactionPrototype(pOutFile, pFunc, true, false);
					fprintf(pOutFile, ";\n");
				}

				if (pFunc->eFlags & ATRFLAG_MAKE_DEFERRED_VERSION)
				{
					WriteOutAutoGenAutoTransactionPrototype(pOutFile, pFunc, false, true);
					fprintf(pOutFile, ";\n");
				}

			}
		}

		fclose(pOutFile);
			

		pOutFile = fopen_nofail(m_AutoTransactionWrapperSourceFileName, "wt");

	//now write out autogenerated wrapper functions for autotransactions
		fprintf(pOutFile, "\n//Autogenerated wrapper functions for calling auto transactions\n#include \"estring.h\"\n#include \"textparser.h\"\n#include \"objtransactions.h\"\n#include \"earray.h\"\n#include \"AutoTransSupport.h\"\n#include \"file.h\"\n");

		for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
		{
			AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

			if (!IsHelper(pFunc))
			{
				WriteOutAutoGenAutoTransIFDEF(pOutFile, pFunc);
				WriteOutAutoGenAutoTransactionBody(pOutFile, pFunc, false, false);
				if (pFunc->eFlags & ATRFLAG_MAKE_APPEND_VERSION)
				{
					WriteOutAutoGenAutoTransactionBody(pOutFile, pFunc, true, false);
				}
				if (pFunc->eFlags & ATRFLAG_MAKE_DEFERRED_VERSION)
				{
					WriteOutAutoGenAutoTransactionBody(pOutFile, pFunc, false, true);
				}
				WriteOutAutoGenAutoTransENDIF(pOutFile, pFunc);
				
			}
		}
		fclose(pOutFile);
	}




	return true;
}

void AutoTransactionManager::WriteOutAutoGenAutoTransIFDEF(FILE *pOutFile, AutoTransactionFunc *pFunc)
{
	char temp[1024];
	strcpy(temp, GetFileNameWithoutDirectoriesOrSlashes(pFunc->sourceFileName));
	MakeStringAllAlphaNumAndUppercase(temp);

	fprintf(pOutFile, "#if !defined(_EXCLUDE_%s_AUTOTRANS_WRAPPERS_) && !(defined(_ATR_EXCLUDE_UNSPECIFIED_) && !defined(_INCLUDE_%s_AUTOTRANS_WRAPPERS_))\n", temp, temp);
}

void AutoTransactionManager::WriteOutAutoGenAutoTransENDIF(FILE *pOutFile, AutoTransactionFunc *pFunc)
{
	fprintf(pOutFile, "#endif\n");
}

int AutoTransactionManager::FindFuncByName(char *pString)
{
	int i;

	for (i=0; i < eaSize(&m_ppFuncs); i++)
	{
		if (strcmp(pString, m_ppFuncs[i]->functionName) == 0)
		{
			return i;
		}
	}

	return -1;
}
	
void AutoTransactionManager::ProcessArgNameInsideFunc(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc, int iContainerArgIndex)
{
	Token token;
	enumTokenType eType;
	
	bool bFoundBrackets = false;


	pTokenizer->SaveLocation();

	eType = pTokenizer->GetNextToken(&token);

	if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACKET)
	{
		int iBracketDepth = 1;

		bFoundBrackets = true;

		do
		{
			eType = pTokenizer->GetNextToken(&token);

			if (eType == TOKEN_NONE)
			{
				ASSERT(pTokenizer,0, "Found EOF in the middle of brackets for EARRAY AUTO_TRANS arg");
			}
			else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACKET)
			{
				iBracketDepth++;
			}
			else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACKET)
			{
				iBracketDepth--;
			}
		}
		while (iBracketDepth > 0);

		eType = pTokenizer->GetNextToken(&token);
	}


	if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ARROW)
	{
		char outString[MAX_NAME_LENGTH];
		int iOutStringLength = 0;

		if (bFoundBrackets)
		{
			sprintf(outString, "[]");
			iOutStringLength = 2;
		}

		do
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - iOutStringLength, "Expected identifier after arg->");
			strcpy(outString + iOutStringLength, token.sVal);
			iOutStringLength += token.iVal;

			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_RESERVEDWORD && (token.iVal == RW_ARROW || token.iVal == RW_DOT))
			{
				ASSERT(pTokenizer,iOutStringLength < MAX_NAME_LENGTH - 3, "String of dereferences has too many characters");
				pTokenizer->StringifyToken(&token);
				int iLen = (int)strlen(token.sVal);
				strcpy(outString + iOutStringLength, token.sVal);
				iOutStringLength += iLen;

				pTokenizer->GetNextToken(&token);
			}
			else
			{
				//if we are inside a function and are not about to encounter a [, and if the function we're inside
				//is not an ATR_RECURSE, we might be in a helper function

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACKET)
				{
					AddDereference(pFunc, iContainerArgIndex, outString, DEREFTYPE_NORMAL, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
					pTokenizer->RestoreLocation();
					return;
				}
				else
				{
					char funcName[MAX_NAME_LENGTH];
					int iArgNum;
					int iTempLineNum;
					RecursingFunctionCall recursingFunctionCall = {0};

					if (FindRecurseFunctionCallContainingPoint(pTokenizer, pFunc->iStartingTokenizerOffset, pFunc->iSourceFileLineNum, 
						pTokenizer->GetOffset(&iTempLineNum), true, &pFunc->simpleArgList, funcName, &iArgNum, &recursingFunctionCall))
					{
						if (FunctionNeverHasSideEffects(funcName))
						{
							pTokenizer->RestoreLocation();
							return;
						}

						if (FunctionIsArrayOperation(funcName) 
							//if a special array operation func (like eaSize) is called on a container with a deref, then it
							//can't possibly be on the root of an ATR earray of containers, so it is treated like a normal
							//ARRAYOPS dereferencing
							|| FunctionIsSpecialArrayOperation(funcName) )
						{
							AddDereference(pFunc, iContainerArgIndex, outString, DEREFTYPE_ARRAYOPS, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
							pTokenizer->RestoreLocation();
							return;
						}
						else 
						{		
							AddRecursingFunctionCallIfUniqueOrJustAddOneContainerArg(pTokenizer, &pFunc->funcCallList, &recursingFunctionCall,
								iContainerArgIndex, iArgNum, outString);
							pTokenizer->RestoreLocation();
							return;
						}
					}
					else
					{
						AddDereference(pFunc, iContainerArgIndex, outString, DEREFTYPE_NORMAL, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
						pTokenizer->RestoreLocation();
						return;
					}
			

				}
			}
		}
		while (1);
	}


	//if we get here, it's because our container name was NOT immediately followed by an arrow. Thus it
	//has no dereference string

	char funcName[MAX_NAME_LENGTH];
	int iArgNum;
	int iTempLineNum;
	RecursingFunctionCall recursingFunctionCall = {0};

	pTokenizer->RestoreLocation();

	if (FindRecurseFunctionCallContainingPoint(pTokenizer, pFunc->iStartingTokenizerOffset, pFunc->iSourceFileLineNum, 
		pTokenizer->GetOffset(&iTempLineNum), false, &pFunc->simpleArgList, funcName, &iArgNum, &recursingFunctionCall))
	{
		ContainerArg *pArg = FindContainerArgFromContainerArgListByIndex(&pFunc->containerArgList, iContainerArgIndex);
		
	
		if (FunctionIsSpecialArrayOperation(funcName))
		{
			if (pArg->eArgFlags & ARGFLAG_ISEARRAY)
			{
				//do nothing
			}
			else
			{
				AddDereference(pFunc, iContainerArgIndex, "", DEREFTYPE_ARRAYOPS_SPECIAL, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
			}
		}
		else if (FunctionNeverHasSideEffects(funcName))
		{
			//do nothing
		}
		else if (IsFullContainerFunction(funcName, iArgNum))
		{
			AddDereference(pFunc, iContainerArgIndex, "", DEREFTYPE_NORMAL, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
		}
		else if (IsSimpleDereferenceFunction(funcName))
		{
			if (iArgNum == 0 && eaSize(&recursingFunctionCall.simpleIdentifierArgList.ppIdentifierArgs) > 0)
			{
				char fullDerefString[1024] = "";
				int j;
				for (j=0; j < eaSize(&recursingFunctionCall.simpleIdentifierArgList.ppIdentifierArgs); j++)
				{
					if (j != 0)
					{
						strcat(fullDerefString, ".");
					}
					strcat(fullDerefString, recursingFunctionCall.simpleIdentifierArgList.ppIdentifierArgs[j]->sVal);
				}

				AddDereference(pFunc, iContainerArgIndex, fullDerefString, 
					DEREFTYPE_NORMAL, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
			}
			else
			{
				ASSERTF(pTokenizer, 0, "Invalid syntax in %s", funcName);
			}
		}
		else
		{
			AddRecursingFunctionCallIfUniqueOrJustAddOneContainerArg(pTokenizer, &pFunc->funcCallList, &recursingFunctionCall,
				iContainerArgIndex, iArgNum, "");
		}
		

		pTokenizer->RestoreLocation();
		return;
	}

	AddDereference(pFunc, iContainerArgIndex, "", DEREFTYPE_NORMAL, pTokenizer->GetCurFileName_NoDirs(), pTokenizer->GetCurLineNum());
}


//skips over a function call that has open parens, then optional ampersand, then a single identifier that may be a container 
//arg name, then close parens. If any of the above are not found, skip back out and keep going normaly
void AutoTransactionManager::SkipSafeSimpleFunction(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc)
{
	pTokenizer->SaveLocation();

	Token token;
	enumTokenType eType;

	eType = pTokenizer->GetNextToken(&token);

	if (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS))
	{
		pTokenizer->RestoreLocation();
		return;
	}

	eType = pTokenizer->GetNextToken(&token);

	if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_AMPERSAND)
	{
		eType = pTokenizer->GetNextToken(&token);
	}

	if (eType != TOKEN_IDENTIFIER)
	{
		pTokenizer->RestoreLocation();
		return;
	}
	eType = pTokenizer->GetNextToken(&token);

	if (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS))
	{
		pTokenizer->RestoreLocation();
		return;
	}	
}

	
void AutoTransactionManager::FoundMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, int iWhichMagicWord, char *pMagicWordString)
{
	SourceParserBaseClass::FoundMagicWord(pSourceFileName, pTokenizer, iWhichMagicWord, pMagicWordString);

	LazyInitStringTrees();

	switch (iWhichMagicWord)
	{
	case MAGICWORD_AUTOTRANSACTION:
	case MAGICWORD_AUTOTRANSHELPER:
	case MAGICWORD_AUTOTRANSHELPERSIMPLE:
		FoundAutoTransMagicWord(pSourceFileName, pTokenizer, (AutoTransMagicWord)iWhichMagicWord);
		break;
	}

}

void AutoTransactionManager::DumpSimpleArgList(SimpleArgList *pSimpleArgList)
{
	int i;

	for (i=0; i < eaSize(&pSimpleArgList->ppSimpleArgs); i++)
	{
		printf("%d: Index %d name %s\n",
			i, pSimpleArgList->ppSimpleArgs[i]->iArgIndex, pSimpleArgList->ppSimpleArgs[i]->argName);
	}
}

#if 0
void AutoTransactionManager::FoundAutoTransHelperMagicWord(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;
	enumTokenType eType;
	bool bFoundNoConst = false;

	ASSERT(pTokenizer,m_iNumHelperFuncs < MAX_AUTO_TRANSACTION_HELPER_FUNCS, "too many auto trans helper funcs");
	HelperFunc *pHelperFunc = &m_HelperFuncs[m_iNumHelperFuncs++];
	memset(pHelperFunc, 0, sizeof(HelperFunc));

	strcpy(pHelperFunc->sourceFileName, pSourceFileName);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ;  after AUTO_TRANS_HELPER");

	eType = pTokenizer->CheckNextToken(&token);
	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NOCONST") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NOCONST");
		bFoundNoConst = true;
	}
	else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
	{
		pTokenizer->GetNextToken(&token);
	}

	pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_VOID, TOKEN_IDENTIFIER, 0, "Expected return type after AUTO_TRANS_HELPER;");

	if (bFoundNoConst)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NOCONST(x");
	}


	eType = pTokenizer->GetNextToken(&token);

	while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
	{
		eType = pTokenizer->GetNextToken(&token);
	}


	ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER && token.iVal < MAX_NAME_LENGTH - 1, "Expected AUTO_TRANS_HELPER function name");

	strcpy(pHelperFunc->functionName, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after function name");

	int iCommaCount = 0;
	int iParenDepth = 1;

	while (1)
	{
		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,eType != TOKEN_NONE, "Unexpected end of file");

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
		{
			iParenDepth++;
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
		{
			iParenDepth--;
			if (iParenDepth == 0)
			{
				ASSERT(pTokenizer,pHelperFunc->iNumMagicArgs>0, "Helper func appears to have no ATH_ARGs... what is the point? What is the damn point?");
				break;
			}
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
		{
			if (iParenDepth == 1)
			{
				iCommaCount++;
			}
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ATH_ARG") == 0)
		{
			eType = pTokenizer->GetNextToken(&token);

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
			{
				eType = pTokenizer->GetNextToken(&token);
			}

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NOCONST") == 0)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NOCONST");
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected container type name after NOCONST(");
				/*strcpy(pArg->argTypeName, token.sVal);
				pArg->eArgType = ATR_ARGTYPE_CONTAINER;*/
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NOCONST");

			}
			else
			{
				ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_VOID || eType == TOKEN_IDENTIFIER, "Expected arg type after ATH_ARG");
			}

			eType = pTokenizer->GetNextToken(&token);

			while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
			{
				eType = pTokenizer->GetNextToken(&token);
			}

			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER && token.iVal < MAX_NAME_LENGTH - 1, "Expected arg name");

			ASSERT(pTokenizer,pHelperFunc->iNumMagicArgs < MAX_MAGIC_ARGS_PER_HELPER_FUNC, "Too many ATH_ARGs for one func");

			HelperFuncArg *pMagicArg = pHelperFunc->pMagicArgs[pHelperFunc->iNumMagicArgs++] = new HelperFuncArg;
			memset(pMagicArg, 0, sizeof(HelperFuncArg));


			strcpy(pMagicArg->argName, token.sVal);
			pMagicArg->iArgIndex = iCommaCount;
		}
		else if (iParenDepth == 1 && eType == TOKEN_IDENTIFIER)
		{
			//potential simple arg
			ASSERT(pTokenizer,pHelperFunc->simpleArgList.iNumSimpleArgs < MAX_SIMPLE_ARGS_PER_FUNC, "Too many simple args");
			SimpleArg *pArg = &pHelperFunc->simpleArgList.simpleArgs[pHelperFunc->simpleArgList.iNumSimpleArgs];

			pArg->iArgIndex = iCommaCount;

			if (strcmp(token.sVal, "const") == 0)
			{
				eType = pTokenizer->CheckNextToken(&token);
				if (eType != TOKEN_IDENTIFIER)
				{
					goto notASimpleArg;
				}
				pTokenizer->GetNextToken(&token);
			}

			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
			{
				pTokenizer->GetNextToken(&token);
				eType = pTokenizer->CheckNextToken(&token);
			}

			if (eType != TOKEN_IDENTIFIER)
			{
				goto notASimpleArg;
			}

			pTokenizer->GetNextToken(&token);

			ASSERTF(pTokenizer,token.iVal < MAX_NAME_LENGTH, "%s is too long to be an argument name", 
				token.sVal);

			strcpy(pArg->argName, token.sVal);

			eType = pTokenizer->CheckNextToken(&token);

			if (eType != TOKEN_RESERVEDWORD || (token.iVal != RW_COMMA && token.iVal != RW_RIGHTPARENS))
			{
				goto notASimpleArg;
			}

			pHelperFunc->simpleArgList.iNumSimpleArgs++;

notASimpleArg:;
		}


	}


	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTBRACE, "Expected { after ATH_ARGs");

	//first, go through and find the end brace
	int iOffsetAtBeginningOfFunctionBody;
	int iLineNumAtBeginningOfFunctionBody;
	int iOffsetAtEndOfFunctionBody;
	int iLineNumAtEndOfFunctionBody;

	iOffsetAtBeginningOfFunctionBody = pTokenizer->GetOffset(&iLineNumAtBeginningOfFunctionBody);

	int iBraceDepth = 1;

	while (1)
	{
		eType = pTokenizer->GetNextToken(&token);

		pTokenizer->Assert (eType != TOKEN_NONE, "Unexpected EOF in helper function");

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE)
		{
			iBraceDepth++;
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
		{
			iBraceDepth--;

			if (iBraceDepth == 0)
			{
				iOffsetAtEndOfFunctionBody = pTokenizer->GetOffset(&iLineNumAtEndOfFunctionBody);
				break;
			}
		}
	}

	pTokenizer->SetOffset(iOffsetAtBeginningOfFunctionBody, iLineNumAtBeginningOfFunctionBody);

	while (pTokenizer->GetOffset(NULL) < iOffsetAtEndOfFunctionBody)
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_IDENTIFIER)
		{
			int iArgNum;

			if (strcmp(token.sVal, "eaIndexedGetUsingInt") == 0)
			{
				ProcessEArrayGet(pTokenizer, pHelperFunc->sourceFileName, &pHelperFunc->simpleArgList, &pHelperFunc->containerArgList, false, ATR_LOCK_INDEXED_NULLISOK,
					&pHelperFunc->earrayUseList);
			}
			else if (strcmp(token.sVal, "eaIndexedGetUsingString") == 0)
			{
				ProcessEArrayGet(pTokenizer, pHelperFunc->sourceFileName, &pHelperFunc->simpleArgList, &pHelperFunc->containerArgList, true, ATR_LOCK_INDEXED_NULLISOK,
					&pHelperFunc->earrayUseList);
			}			
			else if (strcmp(token.sVal, "eaIndexedGetUsingInt_FailOnNULL") == 0)
			{
				ProcessEArrayGet(pTokenizer, pHelperFunc->sourceFileName, &pHelperFunc->simpleArgList, &pHelperFunc->containerArgList, false, ATR_LOCK_INDEXED_FAILONNULL,
					&pHelperFunc->earrayUseList);
			}
			else if (strcmp(token.sVal, "eaIndexedGetUsingString_FailOnNULL") == 0)
			{
				ProcessEArrayGet(pTokenizer, pHelperFunc->sourceFileName, &pHelperFunc->simpleArgList, &pHelperFunc->containerArgList, true, ATR_LOCK_INDEXED_FAILONNULL,
					&pHelperFunc->earrayUseList);
			} 
			else for (iArgNum=0; iArgNum < pHelperFunc->iNumMagicArgs; iArgNum++)
			{
				if (strcmp(token.sVal, pHelperFunc->pMagicArgs[iArgNum]->argName) == 0)
				{
					HelperFuncArg *pMagicArg = pHelperFunc->pMagicArgs[iArgNum];
					
					//put all .x->foo.bar stuff in fieldString
					char fieldString[512] = "";

					int iOffsetAfterArgTokenAndFields;
					int iLineNumAfterArgTokenAndFields;

					iOffsetAfterArgTokenAndFields = pTokenizer->GetOffset(&iLineNumAfterArgTokenAndFields);

					while (1)
					{
						eType = pTokenizer->GetNextToken(&token);

						if (!(eType == TOKEN_RESERVEDWORD && (token.iVal == RW_DOT || token.iVal == RW_ARROW)))
						{
							break;
						}

						pTokenizer->StringifyToken(&token);
						strcat(fieldString, token.sVal);

						pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected identifier after . or ->");

						strcat(fieldString, token.sVal);
	
						iOffsetAfterArgTokenAndFields = pTokenizer->GetOffset(&iLineNumAfterArgTokenAndFields);
					}

					//if the next token is [, then there's no point in looking for function calls, because we can't do
					//anything fancy past a [

					if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACKET)
					{
						AddFieldReferenceToHelperFuncMagicArg(pTokenizer, pMagicArg, fieldString);
					}
					else
					{
						char foundFuncName[256];
						int iFoundArgNum;
						RecursingFunctionCall recursingFunctionCall = {0};

						if (FindRecurseFunctionCallContainingPoint(pTokenizer, 
							iOffsetAtBeginningOfFunctionBody, iLineNumAtBeginningOfFunctionBody, 
								iOffsetAfterArgTokenAndFields, true,
								&pHelperFunc->simpleArgList,
								
								foundFuncName, &iFoundArgNum, &recursingFunctionCall))
						{
							if (FunctionNeverHasSideEffects(foundFuncName))
							{

							}
							else
							{
								AddPotentialHelperFuncRecurseToHelperFuncMagicArg(pTokenizer, pMagicArg, foundFuncName, iFoundArgNum, fieldString);
							}
						}
						else
						{
							AddFieldReferenceToHelperFuncMagicArg(pTokenizer, pMagicArg, fieldString);
						}	
					}
					pTokenizer->SetOffset(iOffsetAfterArgTokenAndFields, iLineNumAfterArgTokenAndFields);
					break;
				}
			}
		}
	}

	m_bSomethingChanged = true;
}
#endif



void AutoTransactionManager::GetFuncHeaderAndArgsForHelperFunc(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc)
{
	enumTokenType eType;
	Token token;
	bool bFoundNoConst = false;
	char argTypeName[MAX_NAME_LENGTH];


	while (pTokenizer->CheckNextToken(&token) == TOKEN_IDENTIFIER)
	{
		if (strcmp(token.sVal, "ATR_LOCKS") == 0)
		{
			pTokenizer->GetNextToken(&token);
			ReadExpectedLocks(pTokenizer, pFunc);
		}
		else
		{
			pTokenizer->AssertFailed("Unknown token after AUTO_TRANS_HELPER");
		}
	}

	pFunc->iStartingTokenizerOffset = pTokenizer->GetOffset(&pFunc->iSourceFileLineNum);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ;  after AUTO_TRANS_HELPER");

	eType = pTokenizer->CheckNextToken(&token);
	while (eType == TOKEN_IDENTIFIER && (strcmp(token.sVal, "__forceinline") == 0 || strcmp(token.sVal, "static") == 0))
	{
		pTokenizer->GetNextToken(&token);
		eType = pTokenizer->CheckNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NOCONST") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NOCONST");
		bFoundNoConst = true;
	}
	else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
	{
		pTokenizer->GetNextToken(&token);
	}

	pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_VOID, TOKEN_IDENTIFIER, 0, "Expected return type after AUTO_TRANS_HELPER;");

	if (bFoundNoConst)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NOCONST(x");
	}


	eType = pTokenizer->GetNextToken(&token);

	while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
	{
		eType = pTokenizer->GetNextToken(&token);
	}


	ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER && token.iVal < MAX_NAME_LENGTH - 1, "Expected AUTO_TRANS_HELPER function name");

	strcpy(pFunc->functionName, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after function name");

	int iCommaCount = 0;
	int iParenDepth = 1;

	//allow ATR_ARGS for hepler funcs, make sure to not count it towards comma count
	eType = pTokenizer->CheckNextToken(&token);
	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ATR_ARGS") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after ATR_ARGS");
	}

	while (1)
	{
		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,eType != TOKEN_NONE, "Unexpected end of file");

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
		{
			iParenDepth++;
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
		{
			iParenDepth--;
			if (iParenDepth == 0)
			{
				ASSERT(pTokenizer,eaSize(&pFunc->containerArgList.ppContainerArgs) > 0, "Helper func appears to have no ATH_ARGs... what is the point? What is the damn point?");
				break;
			}
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
		{
			if (iParenDepth == 1)
			{
				iCommaCount++;
			}
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ATH_ARG") == 0)
		{
			bool bFoundConstEarrayOf = false;
			bool bFoundAllowFullLock = false;

			eType = pTokenizer->GetNextToken(&token);
	
			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ATR_ALLOW_FULL_LOCK") == 0)
			{
				eType = pTokenizer->GetNextToken(&token);
				bFoundAllowFullLock = true;
			}


			
			while (eType == TOKEN_IDENTIFIER && StringIsInList(token.sVal, sAutoTransArgWordsToIgnore))
			{
				eType = pTokenizer->GetNextToken(&token);
			}

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "CONST_EARRAY_OF") == 0)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after CONST_EARRAY_OF");
				eType = pTokenizer->GetNextToken(&token);
				bFoundConstEarrayOf = true;
			}


			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NOCONST") == 0)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NOCONST");
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected container type name after NOCONST(");
				strcpy(argTypeName, token.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NOCONST");

			}
			else
			{
				ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_VOID || eType == TOKEN_IDENTIFIER, "Expected arg type after ATH_ARG");
				pTokenizer->StringifyToken(&token);
				strcpy(argTypeName, token.sVal);

			}

			if (bFoundConstEarrayOf)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after CONST_EARRAY_OF");
			}

			eType = pTokenizer->GetNextToken(&token);

			while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
			{
				eType = pTokenizer->GetNextToken(&token);
			}

			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER && token.iVal < MAX_NAME_LENGTH - 1, "Expected arg name");

			AddContainerArgToContainerArgList(pTokenizer, &pFunc->containerArgList, iCommaCount, 
				(enumArgFlags)((bFoundAllowFullLock ? ARGFLAG_ALLOWFULLLOCK : 0) | (bFoundConstEarrayOf ? ARGFLAG_ISEARRAY : 0)),
				token.sVal, argTypeName);
		}
		else if (iParenDepth == 1 && eType == TOKEN_IDENTIFIER)
		{
			//potential simple arg
			SimpleArg *pArg = StructCalloc(SimpleArg);
			bool bPushed = false;

			pArg->iArgIndex = iCommaCount;

			if (strcmp(token.sVal, "const") == 0)
			{
				eType = pTokenizer->CheckNextToken(&token);
				if (eType != TOKEN_IDENTIFIER)
				{
					goto notASimpleArg;
				}
				pTokenizer->GetNextToken(&token);
			}

			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
			{
				pTokenizer->GetNextToken(&token);
				eType = pTokenizer->CheckNextToken(&token);
			}

			if (eType != TOKEN_IDENTIFIER)
			{
				goto notASimpleArg;
			}

			pTokenizer->GetNextToken(&token);

			ASSERTF(pTokenizer,token.iVal < MAX_NAME_LENGTH, "%s is too long to be an argument name", 
				token.sVal);

			strcpy(pArg->argName, token.sVal);

			eType = pTokenizer->CheckNextToken(&token);

			if (eType != TOKEN_RESERVEDWORD || (token.iVal != RW_COMMA && token.iVal != RW_RIGHTPARENS))
			{
				goto notASimpleArg;
			}

			bPushed = true;
			eaPush(&pFunc->simpleArgList.ppSimpleArgs, pArg);

notASimpleArg:;

			if (!bPushed)
			{
				free(pArg);
			}
		}


	}

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTBRACE, "Expected { after ATH_ARGs");

	FixupExpectedLocksPostHeaderRead(pTokenizer, pFunc);
}


char *pIllegalStringsForSimpleHelperHeader[] = 
{
	"ATH_ARG",
	"ATR_ARGS",
	NULL,
};

void AutoTransactionManager::GetFuncHeaderAndArgsForSimpleHelperFunc(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc)
{
	enumTokenType eType;
	Token token;

	enumTokenType eLastType = TOKEN_NONE;
	Token lastToken = {TOKEN_NONE};
	int iParensDepth = 0;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ;  after AUTO_TRANS_HELPER_SIMPLE");
	pFunc->iStartingTokenizerOffset = pTokenizer->GetOffset(&pFunc->iSourceFileLineNum);

	do
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_IDENTIFIER)
		{
			if (StringIsInList(token.sVal, pIllegalStringsForSimpleHelperHeader))
			{
				pTokenizer->AssertFailedf("Found %s, illegal for AUTO_TRANS_HELPER_SIMPLE", token.sVal);
			}
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS && eLastType == TOKEN_IDENTIFIER)
		{
			if (iParensDepth == 0)
			{
				//random NOCONST() macros make things a bit confusing, but I'm pretty sure that the last
				//0-paren-depth identifier before the { must be the function name
				ASSERTF(pTokenizer, lastToken.iVal < MAX_NAME_LENGTH - 1, "AUTO_TRANS_HELPER_SIMPLE %s has name that is too long",
					lastToken.sVal);

				strcpy(pFunc->functionName, lastToken.sVal);
			}

			iParensDepth++;
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS && eLastType == TOKEN_IDENTIFIER)
		{
			iParensDepth--;
		}

	
		eLastType = eType;
		lastToken = token;
	} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE));
}





AutoTransactionManager::ExpectedLock *AutoTransactionManager::FindExpectedLock(ExpectedLockList *pList, char *pName)
{
	int i;

	for (i=0; i < eaSize(&pList->ppExpectedLocks); i++)
	{
		if (stricmp(pName, pList->ppExpectedLocks[i]->containerArgName) == 0)
		{
			return pList->ppExpectedLocks[i];
		}
	}

	return NULL;
}

void AutoTransactionManager::ReadExpectedLocks(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc)
{
//	enumTokenType eType;
	Token token;

	char argName[MAX_NAME_LENGTH];	 

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ATR_LOCKS");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected container arg name after ATR_LOCKS(");
	strcpy(argName, token.sVal);

	if (FindExpectedLock(&pFunc->expectedLockList, argName))
	{
		pTokenizer->AssertFailedf("Duplicate ATR_LOCKS for arg %s", argName);
	}

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after ATR_LOCKS(name");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string containing comma-separated locks after ATR_LOCKS(name:");

	if (strlen(token.sVal) >= MAX_EXPECTED_LOCKS_STR)
	{
		pTokenizer->AssertFailed("ATR_LOCKS string too long");
	}

	ExpectedLock *pLock = StructCalloc(ExpectedLock);
	eaPush(&pFunc->expectedLockList.ppExpectedLocks, pLock);
	strcpy(pLock->containerArgName, argName);
	strcpy(pLock->expectedLocks, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ATR_LOCKS string");
}

void AutoTransactionManager::FixupExpectedLocksPostHeaderRead(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc)
{
	if (eaSize(&pFunc->expectedLockList.ppExpectedLocks))
	{
		int i;

		for (i=0; i < eaSize(&pFunc->expectedLockList.ppExpectedLocks); i++)
		{
			ExpectedLock *pLock = pFunc->expectedLockList.ppExpectedLocks[i];
			int iIndex = FindContainerArgIndexFromList(&pFunc->containerArgList, pLock->containerArgName);
			if (iIndex == -1)
			{
				pTokenizer->AssertFailedf("Func %s has ATR_LOCKS for unknown container arg %s", 
					pFunc->functionName, pLock->containerArgName);
			}

			ContainerArg *pArg = FindContainerArgFromContainerArgListByIndex(&pFunc->containerArgList, iIndex);

			//handle "" specially
			if (pLock->expectedLocks[0] == 0)
			{
				strcpy(pArg->expectedLocks, "(none)");
			}
			else
			{
				strcpy(pArg->expectedLocks, pLock->expectedLocks);
			}
		}
	}

}

void AutoTransactionManager::GetFuncHeaderAndArgsForATRFunc(Tokenizer *pTokenizer, AutoTransactionFunc *pFunc)
{
	enumTokenType eType;
	Token token;

	while (pTokenizer->CheckNextToken(&token) == TOKEN_IDENTIFIER)
	{
		if (strcmp(token.sVal, "ATR_MAKE_APPEND") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pFunc->eFlags |= ATRFLAG_MAKE_APPEND_VERSION;
		}
		else if (strcmp(token.sVal, "ATR_MAKE_DEFERRED") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pFunc->eFlags |= ATRFLAG_MAKE_DEFERRED_VERSION;
		}
		else if (strcmp(token.sVal, "ATR_LOCKS") == 0)
		{
			pTokenizer->GetNextToken(&token);
			ReadExpectedLocks(pTokenizer, pFunc);







		}
		else
		{
			pTokenizer->AssertFailed("Unknown token after AUTO_TRANSACTION");
		}
	}

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ; after AUTO_TRANSACTION");

	pTokenizer->AssertGetIdentifier("enumTransactionOutcome");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected func name after enumTransactionOutcome");

	ASSERT(pTokenizer,FindFuncByName(token.sVal) == -1, "Func name already used for AutoTransaction");

	
	pFunc->iStartingTokenizerOffset = pTokenizer->GetOffset(&pFunc->iSourceFileLineNum);

	strcpy(pFunc->functionName, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after func name");
	pTokenizer->AssertGetIdentifier("ATR_ARGS");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after ATR_ARGS");

	do
	{
		bool bFoundNonContainer = false;
		bool bFoundAllowFullLock = false;
		bool bFoundConst = false;

		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
		{
			break;
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ATR_ALLOW_FULL_LOCK") == 0)
		{
			bFoundAllowFullLock = true;
			eType = pTokenizer->GetNextToken(&token);
		}


		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NON_CONTAINER") == 0)
		{
			bFoundNonContainer = true;
			eType = pTokenizer->GetNextToken(&token);
		}



		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
		{
			bFoundConst = true;
			eType = pTokenizer->GetNextToken(&token);
		}


		ArgStruct *pArg = StructCalloc(ArgStruct);
		eaPush(&pFunc->ppArgs, pArg);
	
		pArg->eArgFlags = (enumArgFlags)0;
		if (bFoundNonContainer)
		{
			pArg->eArgFlags |= ARGFLAG_FOUNDNONCONTAINER;
		}
		if (bFoundAllowFullLock)
		{
			pArg->eArgFlags |= ARGFLAG_ALLOWFULLLOCK;
		}

		if (bFoundConst)
		{
			pArg->eArgFlags |= ARGFLAG_FOUNDCONST;
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NOCONST") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NOCONST");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected container type name after NOCONST(");
			strcpy(pArg->argTypeName, token.sVal);
			pArg->eArgType = ATR_ARGTYPE_CONTAINER;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NOCONST");

		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "CONST_EARRAY_OF") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after CONST_EARRAY_OF");
			pTokenizer->AssertGetIdentifier("NOCONST");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after CONST_EARRAY_OF(NOCONST");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected container type name after CONST_EARRAY_OF(NOCONST(");
			strcpy(pArg->argTypeName, token.sVal);
			pArg->eArgType = ATR_ARGTYPE_CONTAINER;
			pArg->eArgFlags |= ARGFLAG_ISEARRAY;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after CONST_EARRAY_OF(NOCONST(x");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after CONST_EARRAY_OF(NOCONST(x)");
		}
		else
		{

			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER, "Expected argument type name");
			ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "Argument type name too long");
	
			pArg->eArgType = GetArgTypeFromString(token.sVal);

			if (pArg->eArgType == ATR_ARGTYPE_CONTAINER)
			{
				pArg->eArgType = ATR_ARGTYPE_STRUCT;
			}

			strcpy(pArg->argTypeName, token.sVal);
		}

		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
		{
			pArg->eArgFlags |= ARGFLAG_ISPOINTER;
			eType = pTokenizer->GetNextToken(&token);
		}

		ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER, "Expected argument name");
		ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "Argument name too long");
		
		strcpy(pArg->argName, token.sVal);

		CheckArgTypeValidity(pArg, pTokenizer);

		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
		{
			eType = pTokenizer->GetNextToken(&token);
		}
	}
	while (1);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTBRACE, "Expected { after arguments");

	int i;

	//now take our args and copy them into our simple and container arg lists (the format that is shared
	//with help funcs)
	for (i=0; i < eaSize(&pFunc->ppArgs); i++)
	{
		ArgStruct *pArg = pFunc->ppArgs[i];

		switch (pArg->eArgType)
		{
		case ATR_ARGTYPE_INT:
		case ATR_ARGTYPE_INT64:
		case ATR_ARGTYPE_STRING:
			FuncAssert(pFunc, !(pArg->eArgFlags & ARGFLAG_ALLOWFULLLOCK), "Cant allow full lock of non-container arg");
			AddSimpleArgToSimpleArgList(pTokenizer, &pFunc->simpleArgList, i, pArg->argName);
			break;
		case ATR_ARGTYPE_CONTAINER:
			AddContainerArgToContainerArgList(pTokenizer, &pFunc->containerArgList, i, (enumArgFlags)(pArg->eArgFlags), pArg->argName, pArg->argTypeName);
			break;
		}
	}

	FixupExpectedLocksPostHeaderRead(pTokenizer, pFunc);


}

void AutoTransactionManager::AddFunctionNameInsideTransaction(Tokenizer *pTokenizer, char *pAutoTransName, StringTree *pAutoTransFuncNameTree, char *pFuncName)
{
	//special case, ignore all QueuedRemoteCommand_
	if (StringBeginsWith(pFuncName, "QueueRemoteCommand_", true))
	{
		return;
	}

	if (StringTree_CheckWord(m_pBlackListTree, pFuncName))
	{
		pTokenizer->AssertFailedf("AUTO_TRANS func %s contains call to blacklisted function %s", pAutoTransName, pFuncName);
	}

	if (StringTree_CheckWord(m_pSuperWhiteListTree, pFuncName))
	{
		return;
	}

	if (StringTree_CheckWord(pAutoTransFuncNameTree, pFuncName))
	{
		return;
	}

	StringTree_AddWord(pAutoTransFuncNameTree, pFuncName, 0);

}

static char *spReturnLoggingFuncNames[] = 
{
	"TRANSACTION_RETURN_LOG_FAILURE",
	"TRANSACTION_RETURN_LOG_SUCCESS",
	"TRANSACTION_APPEND_LOG_FAILURE",
	"TRANSACTION_APPEND_LOG_SUCCESS",
	"TRANSACTION_APPEND_LOG_TO_CATEGORY_FAILURE",
	"TRANSACTION_APPEND_LOG_TO_CATEGORY_SUCCESS",
	"TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_FAILURE",
	"TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS",
};



bool AutoTransactionManager::TokenIsReturnLoggingFuncName(char *pStr)
{
	static StringTree *spTree = NULL;

	if (!spTree)
	{
		int i;
		spTree = StringTree_Create();

		for (i = 0; i < ARRAY_SIZE(spReturnLoggingFuncNames); i++)
		{
			StringTree_AddWord(spTree, spReturnLoggingFuncNames[i], 0);
		}
	}

	return !!StringTree_CheckWord(spTree, pStr);
}
		

void AutoTransactionManager::FoundAutoTransMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, AutoTransMagicWord eMagicWord)
{
	Tokenizer tokenizer;

	Token token;
	enumTokenType eType;

	Token prevToken = {TOKEN_NONE};
	enumTokenType ePrevType = TOKEN_NONE;

	pTokenizer->SetNonIntIfdefErrorString("Non-int #ifs and #ifdefs not allowed inside AUTO_TRANSACTIONS, too dangerous (currently non-fatal)");
	
	AutoTransactionFunc *pFunc = StructCalloc(AutoTransactionFunc);
	eaPush(&m_ppFuncs, pFunc);

	pFunc->pCalledFunctionsNameTree = StringTree_Create();

	switch (eMagicWord)
	{
	case MAGICWORD_AUTOTRANSHELPER:
		pFunc->eFlags |= ATRFLAG_IS_HELPER;
		break;

	case MAGICWORD_AUTOTRANSHELPERSIMPLE:
		pFunc->eFlags |= ATRFLAG_IS_HELPER_SIMPLE;
		break;
	}

	m_bSomethingChanged = true;
	strcpy(pFunc->sourceFileName, pSourceFileName);

	switch (eMagicWord)
	{
	case MAGICWORD_AUTOTRANSHELPER:
		GetFuncHeaderAndArgsForHelperFunc(pTokenizer, pFunc);
		break;

	case MAGICWORD_AUTOTRANSHELPERSIMPLE:
		GetFuncHeaderAndArgsForSimpleHelperFunc(pTokenizer, pFunc);
		break;

	default:
		GetFuncHeaderAndArgsForATRFunc(pTokenizer, pFunc);
		break;
	}
	int i;

	for (i = 0; i < eaSize(&m_ppFuncs) - 1; i++)
	{
		if (stricmp(m_ppFuncs[i]->functionName, pFunc->functionName) == 0)
		{
			pTokenizer->AssertFailedf("Found two AUTO_TRANS functions or helpers named %s... one %s(%d), one %s(%d)",
				pFunc->functionName, pFunc->sourceFileName, pFunc->iSourceFileLineNum,
				m_ppFuncs[i]->sourceFileName, m_ppFuncs[i]->iSourceFileLineNum);
		}
	}

	int iBraceDepth = 1;

	do
	{
		eType = pTokenizer->GetNextToken(&token);


		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS && ePrevType == TOKEN_IDENTIFIER)
		{
			AddFunctionNameInsideTransaction(pTokenizer, pFunc->functionName, pFunc->pCalledFunctionsNameTree, prevToken.sVal);
		}

		if (eType == TOKEN_NONE)
		{
			ASSERTF(pTokenizer,0, "End of file in the middle of auto trans function %s", 
				pFunc->functionName);
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE)
		{
			iBraceDepth++;
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
		{
			iBraceDepth--;
			
			if (iBraceDepth == 0)
			{
				break;
			}
		}
		else if (eType == TOKEN_IDENTIFIER)
		{
			if (TokenIsReturnLoggingFuncName(token.sVal))
			{
				pFunc->eFlags |= ATRFLAG_DOES_RETURN_LOGGING;
			}


			if (strcmp(token.sVal, "eaIndexedGetUsingInt") == 0 || strcmp(token.sVal, "eaIndexedRemoveUsingInt") == 0 || strcmp(token.sVal, "eaIndexedPushUsingIntIfPossible") == 0)
			{
				ProcessEArrayGet(pTokenizer, pFunc->sourceFileName, &pFunc->simpleArgList, &pFunc->containerArgList, false, ATR_LOCK_INDEXED_NULLISOK,
					&pFunc->earrayUseList);
			}
			else if (strcmp(token.sVal, "eaIndexedGetUsingString") == 0 || strcmp(token.sVal, "eaIndexedRemoveUsingString") == 0 || strcmp(token.sVal, "eaIndexedPushUsingStringIfPossible") == 0)
			{
				ProcessEArrayGet(pTokenizer, pFunc->sourceFileName, &pFunc->simpleArgList, &pFunc->containerArgList, true, ATR_LOCK_INDEXED_NULLISOK,
					&pFunc->earrayUseList);
			}			
			else if (strcmp(token.sVal, "eaIndexedGetUsingInt_FailOnNULL") == 0 || strcmp(token.sVal, "eaIndexedRemoveUsingInt_FailOnNULL") == 0)
			{
				ProcessEArrayGet(pTokenizer, pFunc->sourceFileName, &pFunc->simpleArgList, &pFunc->containerArgList, false, ATR_LOCK_INDEXED_FAILONNULL,
					&pFunc->earrayUseList);
			}
			else if (strcmp(token.sVal, "eaIndexedGetUsingString_FailOnNULL") == 0 || strcmp(token.sVal, "eaIndexedRemoveUsingString_FailOnNULL") == 0)
			{
				ProcessEArrayGet(pTokenizer, pFunc->sourceFileName, &pFunc->simpleArgList, &pFunc->containerArgList, true, ATR_LOCK_INDEXED_FAILONNULL,
					&pFunc->earrayUseList);
			}
			else if (StringIsInList(token.sVal, sAutoTransSafeSimpleFunctionNames))
			{
				SkipSafeSimpleFunction(pTokenizer, pFunc);
			}
			else
			{
				int iContainerArgIndex = FindContainerArgIndexFromList(&pFunc->containerArgList, token.sVal);
				if (iContainerArgIndex != -1)
				{
					if (ePrevType == TOKEN_RESERVEDWORD && (prevToken.iVal == RW_DOT || prevToken.iVal == RW_ARROW))
					{
						int iBrk = 0;
						//do nothing... this isn't actually a container arg, just a deref inside something else
						//with an unfortunately matching deref name
					}
					else
					{
						ProcessArgNameInsideFunc(pTokenizer, pFunc, iContainerArgIndex);
					}
				}
			}
		}

		ePrevType = eType;
		prevToken = token;

	} while (1);

/*
	printf("*******************************\n%s function %s (%s:%d)\n**************************\n",
		pFunc->bIsHelper ? "HELPER" : "ATR", pFunc->functionName, pFunc->sourceFileName, pFunc->iSourceFileLineNum);
	printf("ContainerArgs:\n");
	DumpContainerArgList(&pFunc->containerArgList);
	printf("\nSimple args:\n");
	DumpSimpleArgList(&pFunc->simpleArgList);
	printf("\nRecurseFuncCalls:\n");
	DumpRecursingFunctionCallList(&pFunc->funcCallList, &pFunc->simpleArgList, &pFunc->containerArgList);
	printf("\nEarray Uses:\n");
	DumpEarrayUseList(&pFunc->earrayUseList, &pFunc->containerArgList, &pFunc->simpleArgList);*/

	pTokenizer->SetNonIntIfdefErrorString(NULL);


}



//if this function finds a legal eaIndex use, it eats all the tokens. Otherwise, leaves them all there.
//This is called immediately after the eaIndexedWhatever has been eaten
void AutoTransactionManager::ProcessEArrayGet(Tokenizer *pTokenizer, char *pSourceFileName, SimpleArgList *pParentSimpleArgList,
	ContainerArgList *pParentContainerArgList,
	bool bUseString, enumAutoTransLockType eLockType,
	EarrayUseList *pOutEarrayUseList)
{
	Token token;
	enumTokenType eType;
	EarrayUse *pEarrayUse;


	pTokenizer->SaveLocation();

	eType = pTokenizer->GetNextToken(&token);

	if (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS))
	{
		pTokenizer->RestoreLocation();
		return;
	}

	eType = pTokenizer->GetNextToken(&token);

	if (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_AMPERSAND))
	{
		pTokenizer->RestoreLocation();
		return;
	}

	eType = pTokenizer->GetNextToken(&token);

	if (eType != TOKEN_IDENTIFIER)
	{
		pTokenizer->RestoreLocation();
		return;
	}

	pEarrayUse = StructCalloc(EarrayUse);

	pEarrayUse->iContainerArgIndex = FindContainerArgIndexFromList(pParentContainerArgList, token.sVal);
	pEarrayUse->eLockType = eLockType;
	strcpy(pEarrayUse->sourceFileName, GetFileNameWithoutDirectoriesOrSlashes(pSourceFileName));
	pEarrayUse->iLineNum = pTokenizer->GetCurLineNum();

	if (pEarrayUse->iContainerArgIndex == -1)
	{
		free(pEarrayUse);
		pTokenizer->RestoreLocation();
		return;
	}


	//if there is array indexing of any sort, then just prepend [] to the out arg list

	int iOutStringLength = 0;

	eType = pTokenizer->CheckNextToken(&token);
	if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACKET)
	{
		sprintf(pEarrayUse->containerArgDerefString, "[]");
		iOutStringLength += 2;
		pTokenizer->AssertGetBracketBalancedBlock(RW_LEFTBRACKET, RW_RIGHTBRACKET, "Expected [x] after eaIndexedGet(&containerName", NULL, 0);
	}
	
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ARROW, "Expected -> after eaIndexedGet(&containerName");

	do
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - iOutStringLength, "Expected identifier after eaIndexedGet(&containerName->");
		strcpy(pEarrayUse->containerArgDerefString + iOutStringLength, token.sVal);
		iOutStringLength += token.iVal;

		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && (token.iVal == RW_ARROW || token.iVal == RW_DOT))
		{
			ASSERT(pTokenizer,iOutStringLength < MAX_NAME_LENGTH - 3, "String of dereferences has too many characters");
			pTokenizer->StringifyToken(&token);
			int iLen = (int)strlen(token.sVal);
			strcpy(pEarrayUse->containerArgDerefString + iOutStringLength, token.sVal);
			iOutStringLength += iLen;

			pTokenizer->GetNextToken(&token);
		}
		else
		{
			break;
		}
	}
	while (1);

	

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after eaIndexedGet(&containerName->x");
	eType = pTokenizer->GetNextToken(&token);
	
	pEarrayUse->eIndexType = RECURSEARGTYPE_BAD;

	if (bUseString)
	{
		if (eType == TOKEN_STRING)
		{
			pEarrayUse->eIndexType = RECURSEARGTYPE_LITERAL_STRING;
			strcpy(pEarrayUse->sVal, token.sVal);			
		}
		else if (eType == TOKEN_IDENTIFIER)
		{
			pEarrayUse->iVal = FindSimpleArgIndexFromList(pParentSimpleArgList, token.sVal);
			if (pEarrayUse->iVal != -1)
			{
				pEarrayUse->eIndexType = RECURSEARGTYPE_PARENT_SIMPLE_ARG;

			}
			else if (strcmp(token.sVal, "ATR_GLOBAL_SYMBOL") == 0)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ATR_GLOBAL_SYMBOL");
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Expected quoted filename after ATR_GLOBAL_SYMBOL(");

				strcpy(pEarrayUse->globalIncludeFile, token.sVal);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) after ATR_GLOBAL_SYMBOL(x");
				pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

				ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "ATR_GLOBAL_SYMBOL string too long");

				strcpy(pEarrayUse->sVal, token.sVal);

				pEarrayUse->eIndexType = RECURSEARGTYPE_IDENTIFIER_STRING;

			}
		}
	}
	else
	{
		if (eType == TOKEN_INT)
		{
			pEarrayUse->eIndexType = RECURSEARGTYPE_LITERAL_INT;
			pEarrayUse->iVal = token.iVal;
		}
		else if (eType == TOKEN_IDENTIFIER)
		{
			pEarrayUse->iVal = FindSimpleArgIndexFromList(pParentSimpleArgList, token.sVal);
			if (pEarrayUse->iVal != -1)
			{
				pEarrayUse->eIndexType = RECURSEARGTYPE_PARENT_SIMPLE_ARG;
	
			}
			else if (strcmp(token.sVal, "ATR_GLOBAL_SYMBOL") == 0)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ATR_GLOBAL_SYMBOL");
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Expected quoted filename after ATR_GLOBAL_SYMBOL(");

				strcpy(pEarrayUse->globalIncludeFile, token.sVal);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) after ATR_GLOBAL_SYMBOL(x");
				pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

				ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "ATR_GLOBAL_SYMBOL string too long");

				strcpy(pEarrayUse->sVal, token.sVal);

				pEarrayUse->eIndexType = RECURSEARGTYPE_IDENTIFIER_INT;
			}		
		}
	}

	//in any of these cases, if the next token is not either right parens or comma, then something is wrong 
	//(we don't care which is which, the actual compiler can sort that out)
	eType = pTokenizer->GetNextToken(&token);
	if (!(eType == TOKEN_RESERVEDWORD && (token.iVal == RW_RIGHTPARENS || token.iVal == RW_COMMA)))
	{
		pEarrayUse->eIndexType = RECURSEARGTYPE_BAD;
	}


	if (pEarrayUse->eIndexType == RECURSEARGTYPE_BAD)
	{
		free(pEarrayUse);
		pTokenizer->RestoreLocation();
	}
	else
	{
		eaPush(&pOutEarrayUseList->ppEarrayUses, pEarrayUse);
	}
}

char *sNotFunctionNames[] = 
{
	"if",
	"for",
	"while",
	"do"
};

bool AutoTransactionManager::IsNeverAFunctionName(char *pString)
{
	return StringIsInList(pString, sNotFunctionNames);
}


bool AutoTransactionManager::FindRecurseFunctionCallContainingPoint(Tokenizer *pTokenizer, 
	int iStartingOffset, int iStartingLineNum, int iOffsetToFind, bool bOKIfArgIsDereferenced, 
	SimpleArgList *pParentFuncSimpleArgList,
	char *pFuncName, int *pOutArgNum, RecursingFunctionCall *pOutRecursingFunctionCall)
{
	Token token;
	enumTokenType eType;
	int iDummyLineNum;
	char funcName[MAX_NAME_LENGTH];


	pTokenizer->SetOffset(iStartingOffset, iStartingLineNum);
	
	do
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_NONE || pTokenizer->GetOffset(&iDummyLineNum) > iOffsetToFind )
		{
			return false;
		}

		if (eType == TOKEN_IDENTIFIER && strlen(token.sVal) < MAX_NAME_LENGTH && !IsNeverAFunctionName(token.sVal))
		{
	
			strcpy(funcName, token.sVal);


			int iAfterIdentifierOffset, iAfterIdentifierLineNum;
			iAfterIdentifierOffset = pTokenizer->GetOffset(&iAfterIdentifierLineNum);

			eType = pTokenizer->GetNextToken(&token);

			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
			{
				bool bFoundATRRecurseOrSimilar = false;
				pOutRecursingFunctionCall->UID = pTokenizer->GetOffset(&pOutRecursingFunctionCall->iLineNum);
				
				strcpy(pOutRecursingFunctionCall->funcName, funcName);

				eType = pTokenizer->CheckNextToken(&token);

				if (eType == TOKEN_IDENTIFIER && 
					(strcmp(token.sVal, "ATR_RECURSE") == 0 || strcmp(token.sVal, "ATR_PASS_ARGS") == 0 || strcmp(token.sVal, "ATR_EMPTY_ARGS") == 0))
				{
					bFoundATRRecurseOrSimilar = true;
					pTokenizer->GetNextToken(&token);
				}

				int iAfterRecurseOffset, iAfterRecurseLineNum;

				iAfterRecurseOffset = pTokenizer->GetOffset(&iAfterRecurseLineNum);

				pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

				//check if we are inside the argument list for this function
				if (pTokenizer->GetOffset(&iDummyLineNum) >= iOffsetToFind) 
				{
					//we've found a function that our desired offset is inside. Remember, however, there
					//could be nested functions, ie, if(myfunc(x)), so don't give up if we never hit the desired offset
					//in this function

					pTokenizer->SetOffset(iAfterRecurseOffset, iAfterRecurseLineNum);

					int iCommaCount = bFoundATRRecurseOrSimilar ? -1 : 0;
					bool bFirstArg = true;
					bool bFoundIt = false;
					bool bFoundOnlyIdentifiersArrowsAndDots;

					do
					{
						int iParensDepth = 0;

						//the first time through, if we found ATR_RECURSE, we already need to find a comma or )
						//
						//otherwise, we skip over that step the first time
						if (bFirstArg && !bFoundATRRecurseOrSimilar)
						{
							bFirstArg = false;
						}
						else
						{

							eType = pTokenizer->GetNextToken(&token);

							ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && (token.iVal == RW_COMMA || token.iVal == RW_RIGHTPARENS), "Expected , or ) after function argument");

							if (token.iVal == RW_RIGHTPARENS)
							{
								if (bFoundIt)
								{
									return true;
								}

								break;
							}

							iCommaCount++;
						}
						

						bFoundOnlyIdentifiersArrowsAndDots = true;

						//here is where we are about to read something in that might be a function argument... so 
						//we want to skip any number of !'s we see in order to make the if (!foo) case work, and
						//any number of &s so that eaSize(&foo) still counts as a function call
						do
						{
							eType = pTokenizer->GetNextToken(&token);
						}
						while (eType == TOKEN_RESERVEDWORD && (token.iVal == RW_NOT || token.iVal == RW_AMPERSAND));

						if (pTokenizer->GetOffset(&iDummyLineNum) == iOffsetToFind)
						{
							eType = pTokenizer->CheckNextToken(&token);

							//note that the offset to look for is at the end of the string of potential dereferences.
							//so, if "foo->x" is an argument to the inner function, we won't find that offset
							//until we have eaten the x. That means that this check here will only be hit
							//if the very first token in the argument is the name we are looking for with no
							//dereferences or anything, just the raw name
								
							if (eType == TOKEN_RESERVEDWORD && (token.iVal == RW_COMMA || token.iVal == RW_RIGHTPARENS || token.iVal == RW_LEFTBRACKET))
							{
								strcpy(pFuncName, funcName);
								*pOutArgNum = iCommaCount;
								bFoundIt = true;
							}
							else
							{
								return false;
							}
						}
						else
						{
							//at this point, we have eaten a token that is the first token from an argument. We
							//want to check if this is a token which matches a simple arg name, or is a literal int
							//or string. If it's any of those, we will add it to pOutRecursingFunctionCall
							enumTokenType eNextType;
							Token NextToken;

							eNextType = pTokenizer->CheckNextToken(&NextToken);

							if (eNextType == TOKEN_RESERVEDWORD 
								&& (NextToken.iVal == RW_COMMA || NextToken.iVal == RW_RIGHTPARENS))
							{
								if (eType == TOKEN_INT)
								{
									AddIntArgToRecursingFunctionCall(pTokenizer, pOutRecursingFunctionCall, iCommaCount, 
										token.iVal);
								}
								else if (eType == TOKEN_STRING)
								{
									AddStringArgToRecursingFunctionCall(pTokenizer, pOutRecursingFunctionCall, iCommaCount,
										token.sVal);
								}
								else if (eType == TOKEN_IDENTIFIER)
								{
									int iParentSimpleArgIndex = FindSimpleArgIndexFromList(pParentFuncSimpleArgList, token.sVal);

									if (iParentSimpleArgIndex != -1)
									{
										AddParentSimpleArgToRecursingFunctionCall(pTokenizer, pOutRecursingFunctionCall, iCommaCount,
											iParentSimpleArgIndex);
									}
									else
									{
										AddSimpleIdentifierArgToRecursingFunctionCall(pTokenizer, pOutRecursingFunctionCall, iCommaCount, token.sVal);
									}
								}
							}
						}

						if (!(eType == TOKEN_IDENTIFIER || eType == TOKEN_RESERVEDWORD && (token.iVal == RW_ARROW || token.iVal == RW_DOT)))
						{
							bFoundOnlyIdentifiersArrowsAndDots = false;
						}

						if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
						{
							iParensDepth++;
						}

						do
						{
							eType = pTokenizer->CheckNextToken(&token);

							if (eType == TOKEN_NONE)
							{
								ASSERTF(pTokenizer,0, "Ran past end of file while processing recursing function call at line %d",
									iAfterIdentifierLineNum);
							}

							if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
							{
								if (iParensDepth == 0)
								{
									break;
								}
								else
								{
									iParensDepth--;
								}
							}
							else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA && iParensDepth == 0)
							{
								break;
							}
							else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
							{
								iParensDepth++;
							}

							eType = pTokenizer->GetNextToken(&token);
							if (!(eType == TOKEN_IDENTIFIER || eType == TOKEN_RESERVEDWORD && (token.iVal == RW_ARROW || token.iVal == RW_DOT)))
							{
								bFoundOnlyIdentifiersArrowsAndDots = false;
							}
						}
						while (1);

						//here we are about to hit another comma or right parens. At this point, if we 
						//hit our offset, that must be because it had some dereferences hanging off of it
						//
						//note that if we hit our offset but bFoundOnlyIdentifiersArrowsAndDots is false, then
						//there must be something like funcCall(x + container.foo) as opposed to 
						//funcCall(container.foo). This does NOT count as a recursing func call,
						//rather it's just a situation in which we want to lock container.foo
						if (pTokenizer->GetOffset(&iDummyLineNum) == iOffsetToFind && bOKIfArgIsDereferenced && bFoundOnlyIdentifiersArrowsAndDots)
						{
							strcpy(pFuncName, funcName);
							*pOutArgNum = iCommaCount;
							bFoundIt = true;
						}
					}
					while (1);
				}
			}

			pTokenizer->SetOffset(iAfterIdentifierOffset, iAfterIdentifierLineNum);
		}
	}
	while (1);

}



















static char *sIntNames[] =
{
	"int",
	"short",
	"S8",
	"S16",
	"S32",
	"U8",
	"U16",
	"U32",
	"ContainerID",
	NULL
};

static char *sInt64Names[] =
{
	"S64",
	"U64",
	NULL
};


static char *sFloatNames[] = 
{
	"float",
	"F32",
	NULL
};

static char *sStringNames[] = 
{
	"char",
	NULL
};



AutoTransactionManager::enumAutoTransArgType AutoTransactionManager::GetArgTypeFromString(char *pArgTypeName)
{
	if (StringIsInList(pArgTypeName, sIntNames))
	{
		return ATR_ARGTYPE_INT;
	}

	if (StringIsInList(pArgTypeName, sInt64Names))
	{
		return ATR_ARGTYPE_INT64;
	}

	if (StringIsInList(pArgTypeName, sFloatNames))
	{
		return ATR_ARGTYPE_FLOAT;
	}

	if (StringIsInList(pArgTypeName, sStringNames))
	{
		return ATR_ARGTYPE_STRING;
	}


	return ATR_ARGTYPE_CONTAINER;
	
}

void AutoTransactionManager::CheckArgTypeValidity(ArgStruct *pArg, Tokenizer *pTokenizer)
{
	switch (pArg->eArgType)
	{
	case ATR_ARGTYPE_INT:
	case ATR_ARGTYPE_INT64:
		return;
	case ATR_ARGTYPE_FLOAT:
		return;
	case ATR_ARGTYPE_STRING:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return;
		}
		pArg->eArgType = ATR_ARGTYPE_INT;
		return;


	case ATR_ARGTYPE_STRUCT:
		if (pArg->eArgFlags & ARGFLAG_ISPOINTER)
		{
			return;
		}
		break;
	case ATR_ARGTYPE_CONTAINER:
		if (pArg->eArgFlags & (ARGFLAG_ISPOINTER | ARGFLAG_ISEARRAY))
		{
			return;
		}
		break;
	}

	char errorString[1024];
	sprintf(errorString, "Unrecognized, or improperly used, arg type %s", pArg->argTypeName);
	ASSERT(pTokenizer,0, errorString);
}

//returns number of dependencies found
int AutoTransactionManager::ProcessDataSingleFile(char *pSourceFileName, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE])
{
	return 0;
}




/*
void AutoTransactionManager::AddPotentialHelperFuncRecurseToHelperFuncMagicArg(Tokenizer *pTokenizer, HelperFuncArg *pArg,
	char *pFuncName, int iArgNum, char *pFieldString)
{
	int i;


	for (i=0; i < pArg->iNumPotentialHelperRecursions; i++)
	{
		if (strcmp(pArg->pPotentialRecursions[i]->pFuncName, pFuncName) == 0
			&& pArg->pPotentialRecursions[i]->iArgNum == iArgNum
			&& strcmp(pArg->pPotentialRecursions[i]->pFieldString, pFieldString) == 0)
		{
			return;
		}
	}

	ASSERT(pTokenizer,pArg->iNumPotentialHelperRecursions < MAX_POTENTIAL_HELPER_FUNC_RECURSIONS_PER_ARG, "Too many helper function recursions");

	PotentialHelperFuncRecursion *pRecursion = new PotentialHelperFuncRecursion;
	pRecursion->iArgNum = iArgNum;
	pRecursion->pFieldString = STRDUP(pFieldString);
	pRecursion->pFuncName = STRDUP(pFuncName);

	pRecursion->iLineNum = pTokenizer->GetCurLineNum();
	strcpy(pRecursion->fileName, pTokenizer->GetCurFileName_NoDirs());

	pArg->pPotentialRecursions[pArg->iNumPotentialHelperRecursions++] = pRecursion;
}

*/

/*
void AutoTransactionManager::AddFieldReferenceToHelperFuncMagicArg(Tokenizer *pTokenizer, HelperFuncArg *pArg, char *pFieldString)
{
	int i;

	if (!pFieldString[0])
	{
		pFieldString = ".*";
	}

	for (i=0; i < pArg->iNumFieldReferences; i++)
	{
		if (strcmp(pArg->fieldReferences[i].pFieldReferenceString, pFieldString) == 0)
		{
			return;
		}
	}

	ASSERT(pTokenizer,pArg->iNumFieldReferences < MAX_FIELD_REFERENCES_PER_HELPER_FUNC_ARG, "Too many field references");



	pArg->fieldReferences[pArg->iNumFieldReferences].pFieldReferenceString = STRDUP(pFieldString);
	pArg->fieldReferences[pArg->iNumFieldReferences].iLineNum = pTokenizer->GetCurLineNum();
	strcpy(pArg->fieldReferences[pArg->iNumFieldReferences].fileName, pTokenizer->GetCurFileName_NoDirs());

	
	pArg->iNumFieldReferences++;
}
*/


void AutoTransactionManager::AddSimpleArgToSimpleArgList(Tokenizer *pTokenizer, SimpleArgList *pList, int iArgIndex, char *pArgName)
{
	SimpleArg *pArg = StructCalloc(SimpleArg);

	pArg->iArgIndex = iArgIndex;
	strcpy(pArg->argName, pArgName);
	eaPush(&pList->ppSimpleArgs, pArg);
}

void AutoTransactionManager::AddContainerArgToContainerArgList(Tokenizer *pTokenizer, ContainerArgList *pList, int iArgIndex, enumArgFlags eFlags, char *pArgName, char *pArgTypeName)
{
	ContainerArg *pArg = StructCalloc(ContainerArg);

	pArg->iArgIndex = iArgIndex;
	strcpy(pArg->argName, pArgName);
	pArg->eArgFlags = eFlags;
	strcpy(pArg->argTypeName, pArgTypeName);



	eaPush(&pList->ppContainerArgs, pArg);
}


AutoTransactionManager::RecursingFunctionCallSimpleArg *AutoTransactionManager::GetNextSimpleArgForRecursingFunctionCall(Tokenizer *pTokenizer, RecursingFunctionCall *pCall)
{
	RecursingFunctionCallSimpleArg *pArg = StructCalloc(RecursingFunctionCallSimpleArg);
	eaPush(&pCall->recursingSimpleArgList.ppRecursingArgs, pArg);
	return pArg;
}



void AutoTransactionManager::AddIntArgToRecursingFunctionCall(Tokenizer *pTokenizer, RecursingFunctionCall *pCall, int iArgIndex, int iVal)
{
	RecursingFunctionCallSimpleArg *pArg = GetNextSimpleArgForRecursingFunctionCall(pTokenizer, pCall);

	pArg->eType = RECURSEARGTYPE_LITERAL_INT;
	pArg->iVal = iVal;
	pArg->iArgIndex = iArgIndex;
}

void AutoTransactionManager::AddStringArgToRecursingFunctionCall(Tokenizer *pTokenizer, RecursingFunctionCall *pCall, int iArgIndex, char *pVal)
{
	RecursingFunctionCallSimpleArg *pArg = GetNextSimpleArgForRecursingFunctionCall(pTokenizer, pCall);

	pArg->eType = RECURSEARGTYPE_LITERAL_STRING;
	if (strlen(pVal) >= sizeof(pArg->sVal))
	{
		pTokenizer->AssertFailedf("Literal string %s exceeds max length of args for recursing functions (%d)",
			pVal, sizeof(pArg->sVal));
	}

	strcpy(pArg->sVal, pVal);
	pArg->iArgIndex = iArgIndex;
}

void AutoTransactionManager::AddParentSimpleArgToRecursingFunctionCall(Tokenizer *pTokenizer, RecursingFunctionCall *pCall, int iArgIndex, int iParentSimpleArgIndex)
{
	RecursingFunctionCallSimpleArg *pArg = GetNextSimpleArgForRecursingFunctionCall(pTokenizer, pCall);

	pArg->eType = RECURSEARGTYPE_PARENT_SIMPLE_ARG;
	pArg->iVal = iParentSimpleArgIndex;
	pArg->iArgIndex = iArgIndex;
}

void AutoTransactionManager::AddSimpleIdentifierArgToRecursingFunctionCall(Tokenizer *pTokenizer, RecursingFunctionCall *pCall, int iArgNum, char *pString)
{
	ASSERTF(pTokenizer, strlen(pString) < MAX_NAME_LENGTH - 1, "String <<%s>> is to long to be simple identifier arg", pString);

	RecursingFunctionCallSimpleIdentifierArg *pArg = StructCalloc(RecursingFunctionCallSimpleIdentifierArg);
		
	pArg->iArgIndex = iArgNum;
	strcpy(pArg->sVal, pString);

	eaPush(&pCall->simpleIdentifierArgList.ppIdentifierArgs, pArg);
}


int AutoTransactionManager::FindSimpleArgIndexFromList(SimpleArgList *pList, char *pArgNameToFind)
{
	int i;

	for (i=0; i < eaSize(&pList->ppSimpleArgs); i++)
	{
		if (strcmp(pArgNameToFind, pList->ppSimpleArgs[i]->argName) == 0)
		{
			return pList->ppSimpleArgs[i]->iArgIndex;
		}
	}

	return -1;
}

int AutoTransactionManager::FindContainerArgIndexFromList(ContainerArgList *pList, char *pArgNameToFind)
{
	int i;

	for (i=0; i < eaSize(&pList->ppContainerArgs); i++)
	{
		if (strcmp(pArgNameToFind, pList->ppContainerArgs[i]->argName) == 0)
		{
			return pList->ppContainerArgs[i]->iArgIndex;
		}
	}

	return -1;
}

AutoTransactionManager::SimpleArg *AutoTransactionManager::FindSimpleArgFromSimpleArgListByIndex(SimpleArgList *pList, int iIndex)
{
	int i;

	for (i=0; i < eaSize(&pList->ppSimpleArgs); i++)
	{
		if (pList->ppSimpleArgs[i]->iArgIndex == iIndex)
		{
			return pList->ppSimpleArgs[i];
		}
	}

	STATICASSERT(0, "SimpleArg corruption");
	return NULL;
}


AutoTransactionManager::ContainerArg *AutoTransactionManager::FindContainerArgFromContainerArgListByIndex(ContainerArgList *pList, int iIndex)
{
	int i;

	for (i=0; i < eaSize(&pList->ppContainerArgs); i++)
	{
		if (pList->ppContainerArgs[i]->iArgIndex == iIndex)
		{
			return pList->ppContainerArgs[i];
		}
	}

	STATICASSERT(0, "ContainerArg corruption");
	return NULL;
}

void AutoTransactionManager::DumpRecursingFunctionCall(RecursingFunctionCall *pCall, SimpleArgList *pParentSimpleArgs,
	ContainerArgList *pParentContainerArgs)
{
	int i;
	printf("On line %d is a call to %s with %d simple args\n", 
		pCall->iLineNum, pCall->funcName, eaSize(&pCall->recursingSimpleArgList.ppRecursingArgs));


	for (i=0;i < eaSize(&pCall->recursingSimpleArgList.ppRecursingArgs); i++)
	{
		RecursingFunctionCallSimpleArg *pArg = pCall->recursingSimpleArgList.ppRecursingArgs[i];

		switch (pArg->eType)
		{
		case RECURSEARGTYPE_LITERAL_INT:
			printf("Arg %d is LITERAL INT %d\n",
				pArg->iArgIndex, pArg->iVal);
			break;
		case RECURSEARGTYPE_LITERAL_STRING:
			printf("Arg %d is LITERAL STRING %s\n",
				pArg->iArgIndex, pArg->sVal);
			break;
		case RECURSEARGTYPE_PARENT_SIMPLE_ARG:
			printf("Arg %d is parent's simple arg %d(%s)\n",
				pArg->iArgIndex, pArg->iVal, FindSimpleArgFromSimpleArgListByIndex(pParentSimpleArgs, pArg->iVal)->argName);
			break;
		}
	}

	printf("And %d container args\n",
		eaSize(&pCall->recursingContainerArgList.ppRecursingArgs));

	for (i=0; i < eaSize(&pCall->recursingContainerArgList.ppRecursingArgs); i++)
	{
		RecursingFunctionCallContainerArg *pArg = pCall->recursingContainerArgList.ppRecursingArgs[i];
		printf("Arg %d is container arg %s, with deref string \"%s\"\n",
			pArg->iRecursingArgIndex, FindContainerArgFromContainerArgListByIndex(pParentContainerArgs, pArg->iParentArgIndex),
			pArg->derefString);
	}

	printf("\n");

			

}



void AutoTransactionManager::DumpRecursingFunctionCallList(RecursingFunctionCallList *pList, SimpleArgList *pParentSimpleArgs, ContainerArgList *pParentContainerArgs)
{
	int i;
	
	for (i=0; i < eaSize(&pList->ppFunctionCalls); i++)
	{
		DumpRecursingFunctionCall(pList->ppFunctionCalls[i], pParentSimpleArgs, pParentContainerArgs);
	}
}



void AutoTransactionManager::DumpEarrayUseList(EarrayUseList *pEarrayUseList, ContainerArgList *pContainerArgs, SimpleArgList *pSimpleArgs)
{
	int i;

	for (i=0; i < eaSize(&pEarrayUseList->ppEarrayUses); i++)
	{
		EarrayUse *pEarrayUse = pEarrayUseList->ppEarrayUses[i];

		printf("line %d: %s(%s)",
			pEarrayUse->iLineNum, FindContainerArgFromContainerArgListByIndex(pContainerArgs, pEarrayUse->iContainerArgIndex)->argName,
			pEarrayUse->containerArgDerefString);

		switch(pEarrayUse->eIndexType)
		{
		case RECURSEARGTYPE_LITERAL_INT:
			printf(" [LITERAL INT %d]\n", pEarrayUse->iVal);
			break;
		case RECURSEARGTYPE_LITERAL_STRING:
			printf(" [LITERAL STRING %s]\n", pEarrayUse->sVal);
			break;
		case RECURSEARGTYPE_PARENT_SIMPLE_ARG:
			printf(" [PARENT ARG %s]\n", FindSimpleArgFromSimpleArgListByIndex(pSimpleArgs, pEarrayUse->iVal)->argName);
			break;
		case RECURSEARGTYPE_IDENTIFIER_INT:
			printf(" [INT IDENTIFIER %s]\n", pEarrayUse->sVal);
			break;
		case RECURSEARGTYPE_IDENTIFIER_STRING:
			printf(" [STRING IDENTIFIER %s]\n", pEarrayUse->sVal);
			break;
		}
	}
}


bool AutoTransactionManager::FirstDereferenceContainsSecondDereference(Dereference *pDeref1, Dereference *pDeref2)
{
	if (pDeref1->derefString[0] == 0 && pDeref1->eType == DEREFTYPE_NORMAL)
	{
		return true;
	}

	if (strcmp(pDeref1->derefString, pDeref2->derefString) == 0)
	{
		if (pDeref1->eType == pDeref2->eType)
		{
			return true;
		}

		if (pDeref1->eType == DEREFTYPE_NORMAL)
		{
			return true;
		}

		return false;
	}

	size_t len = strlen(pDeref1->derefString);
	if (pDeref1->eType == DEREFTYPE_NORMAL && pDeref2->eType == DEREFTYPE_NORMAL && strncmp(pDeref1->derefString, pDeref2->derefString, len) == 0
		&& !isalnum(pDeref2->derefString[len]) && pDeref2->derefString[len] != '_')
	{
		return true;
	}

	return false;
}


void AutoTransactionManager::AddDereference(AutoTransactionFunc *pFunc, int iContainerArgIndex, char *pString, enumDerefType eType, char *pFileName, int iLineNum)
{
	ContainerArg *pArg = FindContainerArgFromContainerArgListByIndex(&pFunc->containerArgList, iContainerArgIndex);
	Dereference *pNewDereference;
	int i;



	pNewDereference = StructCalloc(Dereference);

	strcpy(pNewDereference->derefString, pString);
	pNewDereference->iLineNum = iLineNum;
	pNewDereference->eType = eType;

	for (i = 0; i < eaSize(&pArg->dereferenceList.ppDereferences); i++)
	{
		if (FirstDereferenceContainsSecondDereference(pArg->dereferenceList.ppDereferences[i], pNewDereference))
		{
			free(pNewDereference);
			return; //do not increment pArg->dereferences.iNumDereferences, so our new dereference is forgotten
		}
	}

	//now go through entire list looking for dereferences rendered obsolete by the new one
	
	eaPush(&pArg->dereferenceList.ppDereferences, pNewDereference);

	for (i=eaSize(&pArg->dereferenceList.ppDereferences)-2; i >= 0; i--)
	{
		if (FirstDereferenceContainsSecondDereference(pNewDereference,
			pArg->dereferenceList.ppDereferences[i]))
		{
			eaRemove(&pArg->dereferenceList.ppDereferences, i);
		}
	}

}




void AutoTransactionManager::AddRecursingFunctionCallIfUniqueOrJustAddOneContainerArg(Tokenizer *pTokenizer, RecursingFunctionCallList *pList, RecursingFunctionCall *pCall,
	int iContainerArgIndex, int iIndexInFunctionCall, char *pDerefString)
{
	int i;

	for (i=0; i < eaSize(&pList->ppFunctionCalls); i++)
	{
		if (pList->ppFunctionCalls[i]->UID == pCall->UID)
		{
			pCall = pList->ppFunctionCalls[i];
			break;
		}
	}

	if (i == eaSize(&pList->ppFunctionCalls))
	{
		RecursingFunctionCall *pNewCall = StructCalloc(RecursingFunctionCall);
		eaPush(&pList->ppFunctionCalls, pNewCall);
		memcpy(pNewCall, pCall, sizeof(RecursingFunctionCall));
		pCall = pNewCall;

//		memcpy(&pList->functionCalls[pList->iNumRecursingFunctionCalls], pCall, sizeof(RecursingFunctionCall));
//		pCall = &pList->functionCalls[pList->iNumRecursingFunctionCalls++];
	}

	RecursingFunctionCallContainerArg *pArg = StructCalloc(RecursingFunctionCallContainerArg);
	eaPush(&pCall->recursingContainerArgList.ppRecursingArgs, pArg);

	pArg->iParentArgIndex = iContainerArgIndex;
	pArg->iRecursingArgIndex = iIndexInFunctionCall;
	strcpy(pArg->derefString, pDerefString);
}


void AutoTransactionManager::DumpContainerArgList(ContainerArgList *pList)
{
	int i;
	int j;
	for (i=0; i < eaSize(&pList->ppContainerArgs); i++)
	{
		ContainerArg *pArg = pList->ppContainerArgs[i];

		printf("Index %d arg %s\n",
			pArg->iArgIndex, pArg->argName);

		for (j=0; j < eaSize(&pArg->dereferenceList.ppDereferences); j++)
		{
			Dereference *pDereference = pArg->dereferenceList.ppDereferences[j];

			printf("\tLine %d has dereference \"%s\"%s\n",
				pDereference->iLineNum, pDereference->derefString, pDereference->eType == DEREFTYPE_ARRAYOPS ? " (ARRAYOPS)" : "");
		}
	}
}


void AutoTransactionManager::WriteSimpleArgListToFile(FILE *pOutFile, SimpleArgList *pList)
{
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppSimpleArgs));

	for (i=0; i < eaSize(&pList->ppSimpleArgs); i++)
	{
		SimpleArg *pArg = pList->ppSimpleArgs[i];

		fprintf(pOutFile, "\"%s\" %d ",
			pArg->argName, pArg->iArgIndex);
	}
}

void AutoTransactionManager::ReadSimpleArgListFromTokenizer(Tokenizer *pTokenizer, SimpleArgList *pList)
{
	int i;

	int iNumSimpleArgs = pTokenizer->AssertGetInt();

	for (i=0; i < iNumSimpleArgs; i++)
	{
		SimpleArg *pArg = StructCalloc(SimpleArg); 

		pTokenizer->AssertGetString(pArg->argName, sizeof(pArg->argName));
		pArg->iArgIndex = pTokenizer->AssertGetInt();

		eaPush(&pList->ppSimpleArgs, pArg);
	}
}

void AutoTransactionManager::WriteContainerArgListToFile(FILE *pOutFile, ContainerArgList *pList)
{	
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppContainerArgs));

	for (i=0; i < eaSize(&pList->ppContainerArgs); i++)
	{
		ContainerArg *pArg = pList->ppContainerArgs[i];
		fprintf(pOutFile, "\"%s\" \"%s\" %d %d ",
			pArg->argName, pArg->argTypeName, pArg->iArgIndex, pArg->eArgFlags);
		
		if (pArg->expectedLocks[0])
		{
			fprintf(pOutFile, "\"%s\" ", pArg->expectedLocks);
		}
		else
		{
			fprintf(pOutFile, "\"\" ");
		}

		WriteDereferenceListToFile(pOutFile, &pArg->dereferenceList);
	}
}


void AutoTransactionManager::ReadContainerArgListFromTokenizer(Tokenizer *pTokenizer, ContainerArgList *pList)
{
	int i;

	int iNumContainerArgs = pTokenizer->AssertGetInt();


	for (i=0; i < iNumContainerArgs; i++)
	{
		Token token;

		ContainerArg *pArg = StructCalloc(ContainerArg);
		eaPush(&pList->ppContainerArgs, pArg);


		pTokenizer->AssertGetString(pArg->argName, sizeof(pArg->argName));
		pTokenizer->AssertGetString(pArg->argTypeName, sizeof(pArg->argTypeName));
		pArg->iArgIndex = pTokenizer->AssertGetInt();
		pArg->eArgFlags = pTokenizer->AssertGetInt();

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_EXPECTED_LOCKS_STR, "Couldn't read expected lock string");

		if (token.sVal[0])
		{
			strcpy(pArg->expectedLocks, token.sVal);
		}
	
		ReadDereferenceListFromTokenizer(pTokenizer, &pArg->dereferenceList);
	}
}

void AutoTransactionManager::WriteEarrayUseListToFile(FILE *pOutFile, EarrayUseList *pList)
{
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppEarrayUses));

	for (i=0; i < eaSize(&pList->ppEarrayUses); i++)
	{
		EarrayUse *pEarrayUse = pList->ppEarrayUses[i];

		fprintf(pOutFile, "%d \"%s\" %d %d \"%s\" %d \"%s\" %d \"%s\" ",
			pEarrayUse->iContainerArgIndex, pEarrayUse->containerArgDerefString, pEarrayUse->eIndexType,
			pEarrayUse->iVal, pEarrayUse->sVal, pEarrayUse->eLockType, pEarrayUse->sourceFileName, 
			pEarrayUse->iLineNum, pEarrayUse->globalIncludeFile);
	}
}

void AutoTransactionManager::ReadEarrayUseListFromTokenizer(Tokenizer *pTokenizer, EarrayUseList *pList)
{
	int i;

	int iNumEarrayUses = pTokenizer->AssertGetInt();
	for (i=0; i < iNumEarrayUses; i++)
	{
		EarrayUse *pEarrayUse = StructCalloc(EarrayUse);
		eaPush(&pList->ppEarrayUses, pEarrayUse);

		pEarrayUse->iContainerArgIndex = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pEarrayUse->containerArgDerefString, sizeof(pEarrayUse->containerArgDerefString));
		pEarrayUse->eIndexType = (enumRecurseArgType)pTokenizer->AssertGetInt();
		pEarrayUse->iVal = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pEarrayUse->sVal, sizeof(pEarrayUse->sVal));
		pEarrayUse->eLockType = (enumAutoTransLockType)pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pEarrayUse->sourceFileName, sizeof(pEarrayUse->sourceFileName));
		pEarrayUse->iLineNum = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pEarrayUse->globalIncludeFile, sizeof(pEarrayUse->globalIncludeFile));
	}
}





void AutoTransactionManager::WriteFuncCallListToFile(FILE *pOutFile, RecursingFunctionCallList *pList)
{
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppFunctionCalls));
	
	for (i=0; i < eaSize(&pList->ppFunctionCalls); i++)
	{
		RecursingFunctionCall *pCall = pList->ppFunctionCalls[i];
		fprintf(pOutFile, "%d %d \"%s\" ",
			pCall->UID, pCall->iLineNum, pCall->funcName);

		WriteSimpleRecursingArgListToFile(pOutFile, &pCall->recursingSimpleArgList);
		WriteContainerRecursingArgListToFile(pOutFile, &pCall->recursingContainerArgList);
	}
}

void AutoTransactionManager::ReadFuncCallListFromTokenizer(Tokenizer *pTokenizer, RecursingFunctionCallList *pList)
{
	int i;

	int iNumRecursingFunctionCalls = pTokenizer->AssertGetInt();

	for (i=0; i < iNumRecursingFunctionCalls; i++)
	{
		RecursingFunctionCall *pCall = StructCalloc(RecursingFunctionCall);
		eaPush(&pList->ppFunctionCalls, pCall);

		pCall->UID = pTokenizer->AssertGetInt();
		pCall->iLineNum = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pCall->funcName, sizeof(pCall->funcName));

		ReadSimpleRecursingArgListFromTokenizer(pTokenizer, &pCall->recursingSimpleArgList);
		ReadContainerRecursingArgListFromTokenizer(pTokenizer, &pCall->recursingContainerArgList);
	}
}

void AutoTransactionManager::WriteDereferenceListToFile(FILE *pOutFile, DereferenceList *pList)
{
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppDereferences));
	
	for (i=0; i < eaSize(&pList->ppDereferences); i++)
	{
		Dereference *pDereference = pList->ppDereferences[i];

		fprintf(pOutFile, "%d \"%s\" %d ",
			pDereference->iLineNum, pDereference->derefString, pDereference->eType);
	}
}


void AutoTransactionManager::ReadDereferenceListFromTokenizer(Tokenizer *pTokenizer, DereferenceList *pList)
{
	int i;

	int iNumDereferences = pTokenizer->AssertGetInt();

	
	for (i=0; i < iNumDereferences; i++)
	{
		Dereference *pDereference = StructCalloc(Dereference);
		eaPush(&pList->ppDereferences, pDereference);

		pDereference->iLineNum = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pDereference->derefString, sizeof(pDereference->derefString));
		pDereference->eType = (enumDerefType)pTokenizer->AssertGetInt();
	}
}

void AutoTransactionManager::WriteSimpleRecursingArgListToFile(FILE *pOutFile, RecursingFunctionCallSimpleArgList *pList)
{
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppRecursingArgs));

	for (i=0; i < eaSize(&pList->ppRecursingArgs); i++)
	{
		RecursingFunctionCallSimpleArg *pArg = pList->ppRecursingArgs[i];

		fprintf(pOutFile, "%d %d \"%s\" %d ",
			pArg->eType, pArg->iVal, EscapeString_Underscore(pArg->sVal), pArg->iArgIndex);
	}
}


void AutoTransactionManager::ReadSimpleRecursingArgListFromTokenizer(Tokenizer *pTokenizer, RecursingFunctionCallSimpleArgList *pList)
{
	int i;
	int iNumRecursingArgs = pTokenizer->AssertGetInt();

	for (i=0; i < iNumRecursingArgs; i++)
	{
		RecursingFunctionCallSimpleArg *pArg = StructCalloc(RecursingFunctionCallSimpleArg);
		eaPush(&pList->ppRecursingArgs, pArg);

		pArg->eType = (enumRecurseArgType)pTokenizer->AssertGetInt();
		pArg->iVal = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pArg->sVal, sizeof(pArg->sVal));
		UnEscapeString_Underscore(pArg->sVal);
		pArg->iArgIndex = pTokenizer->AssertGetInt();
	}
}


void AutoTransactionManager::WriteContainerRecursingArgListToFile(FILE *pOutFile, RecursingFunctionCallContainerArgList *pList)
{
	int i;
	fprintf(pOutFile, "%d ", eaSize(&pList->ppRecursingArgs));
	
	for (i=0; i < eaSize(&pList->ppRecursingArgs); i++)
	{
		RecursingFunctionCallContainerArg *pArg = pList->ppRecursingArgs[i];

		fprintf(pOutFile, "%d %d \"%s\" ",
			pArg->iParentArgIndex, pArg->iRecursingArgIndex, pArg->derefString);
	}
}

void AutoTransactionManager::ReadContainerRecursingArgListFromTokenizer(Tokenizer *pTokenizer, RecursingFunctionCallContainerArgList *pList)
{
	int i;

	int iNumRecursingArgs = pTokenizer->AssertGetInt();

	for (i=0; i < iNumRecursingArgs; i++)
	{
		RecursingFunctionCallContainerArg *pArg = StructCalloc(RecursingFunctionCallContainerArg);
		eaPush(&pList->ppRecursingArgs, pArg);

		pArg->iParentArgIndex = pTokenizer->AssertGetInt();
		pArg->iRecursingArgIndex = pTokenizer->AssertGetInt();
		pTokenizer->AssertGetString(pArg->derefString, sizeof(pArg->derefString));
	}
}

bool AutoTransactionManager::AtLeastOneNonHelperFunc(void)
{
	int iFuncNum;

	for (iFuncNum = 0; iFuncNum < eaSize(&m_ppFuncs); iFuncNum++)
	{
		AutoTransactionFunc *pFunc = m_ppFuncs[iFuncNum];

		if (!IsHelper(pFunc))
		{
			return true;
		}
	}

	return false;
}

void AutoTransactionManager::LazyInitStringTrees(void)
{
	if (!m_pWhiteListTree)
	{
		m_pWhiteListTree = m_pParent->GetStringTreeWithAllVariableValues("AutoTransWhiteList");
		m_pBlackListTree = m_pParent->GetStringTreeWithAllVariableValues("AutoTransBlackList");
		m_pSuperWhiteListTree = m_pParent->GetStringTreeWithAllVariableValues("AutoTransSuperWhiteList");

		//fine if these aren't set, some small solutions might not share the settings in c:\src\core properly
		if (!m_pWhiteListTree)
		{
			m_pWhiteListTree = StringTree_Create();
		}
		if (!m_pBlackListTree)
		{
			m_pBlackListTree = StringTree_Create();
		}
		if (!m_pSuperWhiteListTree)
		{
			m_pSuperWhiteListTree = StringTree_Create();
		}

		StringTree_AddAllWordsFromList(m_pSuperWhiteListTree, spNeverHasSideEffectFuncs);
		StringTree_AddAllWordsFromList(m_pSuperWhiteListTree, spArrayOpsFuncs);
		StringTree_AddAllWordsFromList(m_pSuperWhiteListTree, spSpecialArrayOpsFuncs);
		StringTree_AddAllWordsFromList(m_pSuperWhiteListTree, spSimpleDerefFuncs);
		StringTree_AddAllWordsFromList(m_pSuperWhiteListTree, sAutoTransSafeSimpleFunctionNames);

	}
}

void AutoTransactionManager::GetSuspicousFunctionCallNamesCB(char *pStr, int iWordID, void *pUserData1, void *pUserData2)
{
	AutoTransactionManager *pThis = (AutoTransactionManager*)pUserData1;
	char **ppEString = (char**)pUserData2;

	if (StringTree_CheckWord(pThis->m_pWhiteListTree, pStr))
	{
		return;
	}

	if (StringTree_CheckWord(pThis->m_pSafeFuncNamesTree, pStr))
	{
		return;
	}
	
	estrConcatf(ppEString, "%s%s", estrLength(ppEString) == 0 ? "" : ",", pStr);
	
}

char *AutoTransactionManager::GetSuspicousFunctionCallNames(StringTree *pFuncNameTree)
{
	static char *pEString = NULL;

	if (pEString)
	{
		estrClear(&pEString);
	}

	StringTree_Iterate(pFuncNameTree, GetSuspicousFunctionCallNamesCB, this, &pEString);


	return pEString;
}





void AutoTransactionManager::WriteTimeVerifyAutoTransValidity(AutoTransactionFunc *pFunc)
{
	int i;

	for (i=0; i < eaSize(&pFunc->ppArgs); i++)
	{
		if (pFunc->ppArgs[i]->eArgFlags & ARGFLAG_FOUNDNONCONTAINER)
		{
			FuncAssert(pFunc, pFunc->ppArgs[i]->eArgType == ATR_ARGTYPE_STRUCT, "Found NON_CONTAINER in bad place");
		}

		if (pFunc->ppArgs[i]->eArgType == ATR_ARGTYPE_CONTAINER)
		{
			FuncAssertf(pFunc, m_pParent->GetDictionary()->FindIdentifier(pFunc->ppArgs[i]->argTypeName) == IDENTIFIER_STRUCT_CONTAINER,
				"Invalid or unrecognized container name '%s'... containers to AUTO_TRANSACTION must be AUTO_STRUCT AST_CONTAINER", pFunc->ppArgs[i]->argTypeName);
		}
		else if (pFunc->ppArgs[i]->eArgType == ATR_ARGTYPE_STRUCT)
		{
			enumIdentifierType eType = m_pParent->GetDictionary()->FindIdentifier(pFunc->ppArgs[i]->argTypeName);
			FuncAssertf(pFunc, (eType == IDENTIFIER_STRUCT) || ((pFunc->ppArgs[i]->eArgFlags & ARGFLAG_FOUNDNONCONTAINER) && (eType == IDENTIFIER_STRUCT_CONTAINER)),
				"Invalid or unrecognized struct name '%s'...", pFunc->ppArgs[i]->argTypeName);
		}

		if (pFunc->ppArgs[i]->eArgFlags & ARGFLAG_ISEARRAY)
		{
			FuncAssert(pFunc, pFunc->ppArgs[i]->eArgType == ATR_ARGTYPE_CONTAINER, "Only containers can be in EArrays in AUTOTRANS func defs");
		}
	}

#undef FILE
#undef fopen
#undef fprintf
#undef fclose

	for (i = 0; i < eaSize(&pFunc->containerArgList.ppContainerArgs); i++)
	{	
		ContainerArg *pArg = pFunc->containerArgList.ppContainerArgs[i];

		if (!(pArg->eArgFlags & ARGFLAG_ALLOWFULLLOCK))
		{
			int j;

			for (j = 0; j < eaSize(&pArg->dereferenceList.ppDereferences); j++)
			{
				Dereference *pDeref = pArg->dereferenceList.ppDereferences[j];

				if (stricmp(pDeref->derefString, "") == 0 || stricmp(pDeref->derefString, ".") == 0
					|| stricmp(pDeref->derefString, ".*") == 0)
				{
					static bool bFirst = true;
					FILE *pFile;

					if (bFirst)
					{
						pFile = fopen("c:\\StructParserFullLocks.txt", "wt");
						bFirst = false;
					}
					else
					{
						pFile = fopen("c:\\StructParserFullLocks.txt", "at");
					}
					if (pFile)
					{
						fprintf(pFile, "Func %s arg %s fully locked (%s)%d\n",
							pFunc->functionName, pArg->argName, pFunc->sourceFileName, pDeref->iLineNum);

						fclose(pFile);
					}
				}
			}
		}
	}
}

void AutoTransactionManager::DestroyArgStruct(ArgStruct *pArg)
{
	free(pArg);
}
void AutoTransactionManager::DestroySimpleArg(SimpleArg *pArg)
{
	free(pArg);
}
void AutoTransactionManager::DestroyContainerArg(ContainerArg *pArg)
{
	eaDestroyEx(&pArg->dereferenceList.ppDereferences, (EArrayItemCallback)DestroyDereference);
	free(pArg);
}
void AutoTransactionManager::DestroyEarrayUse(EarrayUse *pEarrayUse)
{
	free(pEarrayUse);
}
void AutoTransactionManager::DestroyRecursingFunctionCall(RecursingFunctionCall *pFunctionCall)
{
	eaDestroyEx(&pFunctionCall->recursingSimpleArgList.ppRecursingArgs, (EArrayItemCallback)DestroyRecursingFunctionCallSimpleArg);
	eaDestroyEx(&pFunctionCall->recursingContainerArgList.ppRecursingArgs, (EArrayItemCallback)DestroyRecursingFunctionCallContainerArg);
	eaDestroyEx(&pFunctionCall->simpleIdentifierArgList.ppIdentifierArgs, (EArrayItemCallback)DestroyRecursingFunctionCallSimpleIdentifierArg);
	free(pFunctionCall);
}
void AutoTransactionManager:: DestroyExpectedLock(ExpectedLock *pLock)
{
	free(pLock);
}

void AutoTransactionManager::DestroyDereference(Dereference *pDereference)
{
	free(pDereference);
}
void AutoTransactionManager::DestroyRecursingFunctionCallSimpleArg(RecursingFunctionCallSimpleArg *pArg)
{
	free(pArg);
}
void AutoTransactionManager::DestroyRecursingFunctionCallContainerArg(RecursingFunctionCallContainerArg *pArg)
{
	free(pArg);
}
void AutoTransactionManager::DestroyRecursingFunctionCallSimpleIdentifierArg(RecursingFunctionCallSimpleIdentifierArg *pArg)
{
	free(pArg);
}





//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
//STOP STOP STOP DO NOT PUT ANYTHING AFTER THIS
