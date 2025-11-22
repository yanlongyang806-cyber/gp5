#include "MagicCommandManager.h"
#include "strutils.h"
#include "sourceparser.h"
#include "autorunmanager.h"
#include "earray.h"
#include "estring.h"

#define CONTROLLER_AUTO_SETTING_PREFIX "__CAS_"

static char *sSIntNames[] =
{
	"int",
	"short",
	"S8",
	"S16",
	"S32",
	"GlobalType",
	NULL
};

static char *sUIntNames[] = 
{
	"U8",
	"U16",
	"U32",
	"EntityRef",
	"ContainerID",
	NULL
};

static char *sFloatNames[] = 
{
	"float",
	"F32",
	NULL
};


static char *sSInt64Names[] =
{
	"S64",
	NULL
};

static char *sUInt64Names[] = 
{
	"U64",
	NULL
};

static char *sFloat64Names[] = 
{
	"double",
	"F64",
	NULL
};

static char *sStringNames[] = 
{
	"char",
	"const char",
	NULL
};

static char *sSentenceNames[] = 
{
	"ACMD_SENTENCE",
	"const ACMD_SENTENCE",
	NULL
};

static char *sVec3Names[] = 
{
	"Vec3",
	"const Vec3",
	NULL
};

static char *sVec4Names[] = 
{
	"Vec4",
	"const Vec4",
	NULL
};

static char *sMat4Names[] = 
{
	"Mat4",
	"const Mat4",
	NULL
};

static char *sQuatNames[] = 
{
	"Quat",
	"const Quat",
	NULL
};

enum 
{
	MAGICWORD_AUTO_COMMAND,
	MAGICWORD_AUTO_COMMAND_REMOTE,
	MAGICWORD_AUTO_COMMAND_REMOTE_SLOW,
	MAGICWORD_AUTO_COMMAND_QUEUED,
	MAGICWORD_AUTO_INT,
	MAGICWORD_AUTO_FLOAT,
	MAGICWORD_AUTO_STRING,
	MAGICWORD_AUTO_ESTRING,
	MAGICWORD_AUTO_SENTENCE,
	MAGICWORD_AUTO_ESENTENCE,
	MAGICWORD_EXPR_FUNC,
	MAGICWORD_EXPR_FUNC_STATIC_CHECK,
};

void MagicCommandManager::CommandAssert(MAGIC_COMMAND_STRUCT *pField, bool bCondition, char *pErrorMessage)
{
	if (!bCondition)
	{
		printf("%s(%d) : error S0000 : (StructParser) %s\n", pField->sourceFileName, pField->iLineNum, pErrorMessage);
		fflush(stdout);
		BreakIfInDebugger();
		Sleep(100);
		exit(1);
	}
}


void MagicCommandManager::CommandAssertf(MAGIC_COMMAND_STRUCT *pCommand, bool bExpression, char *pErrorString, ...)
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

	CommandAssert(pCommand, bExpression, buf);
}


char *MagicCommandManager::GetMagicWord(int iWhichMagicWord)
{
	switch (iWhichMagicWord)
	{
	case MAGICWORD_AUTO_COMMAND_REMOTE:
		return "AUTO_COMMAND_REMOTE";
	case MAGICWORD_AUTO_COMMAND_REMOTE_SLOW:
		return "AUTO_COMMAND_REMOTE_SLOW";
	case MAGICWORD_AUTO_COMMAND_QUEUED:
		return "AUTO_COMMAND_QUEUED";
	case MAGICWORD_AUTO_INT:
		return "AUTO_CMD_INT";
	case MAGICWORD_AUTO_FLOAT:
		return "AUTO_CMD_FLOAT";
	case MAGICWORD_AUTO_STRING:
		return "AUTO_CMD_STRING";
	case MAGICWORD_AUTO_ESTRING:
		return "AUTO_CMD_ESTRING";
	case MAGICWORD_AUTO_SENTENCE:
		return "AUTO_CMD_SENTENCE";
	case MAGICWORD_AUTO_ESENTENCE:
		return "AUTO_CMD_ESENTENCE";
	case MAGICWORD_EXPR_FUNC:
		return "AUTO_EXPR_FUNC";
	case MAGICWORD_EXPR_FUNC_STATIC_CHECK:
		return "AUTO_EXPR_FUNC_STATIC_CHECK";
	}

	return "AUTO_COMMAND";
	
}

MagicCommandManager::MagicCommandManager()
{
	m_bSomethingChanged = false;

	m_ppMagicCommands = NULL;
	m_ppMagicCommandVars = NULL;

	m_MagicCommandFileName[0] = 0;
	m_ShortMagicCommandFileName[0] = 0;
	m_MagicCommandExecutableOnlyFileName[0] = 0;

	m_iNumCategoriesWritten = 0;


	memset(m_AllDefines, 0, sizeof(m_AllDefines));

	static char *pAdditionalSimpleInvisibleTokens[] =
	{
		NULL
	};

	m_pAdditionalSimpleInvisibleTokens = pAdditionalSimpleInvisibleTokens;
}

MagicCommandManager::~MagicCommandManager()
{
	eaDestroyEx(&m_ppMagicCommands, (EArrayItemCallback)DestroyCommand);
	eaDestroyEx(&m_ppMagicCommandVars, (EArrayItemCallback)DestroyCommandVar);
}

typedef enum
{
	RW_PARSABLE = RW_COUNT,
};

static char *sMagicCommandReservedWords[] =
{
	"PARSABLE",
	NULL
};
StringTree *spMagicCommandReservedWordTree = NULL;

void MagicCommandManager::SetProjectPathAndName(char *pProjectPath, char *pProjectName)
{
	strcpy(m_ProjectName, pProjectName);

	sprintf(m_ShortMagicCommandFileName, "%s_commands_autogen", pProjectName);
	sprintf(m_MagicCommandFileName, "%s\\AutoGen\\%s.c", pProjectPath, m_ShortMagicCommandFileName);
	sprintf(m_MagicCommandExecutableOnlyFileName, "%s\\AutoGen\\%s_ExprFuncs.c", pProjectPath, m_ShortMagicCommandFileName);
	sprintf(m_MagicCommandExprCodeHeaderFileName, "%s\\AutoGen\\%s_ExprCodes_autogen.h", pProjectPath, pProjectName);
	sprintf(m_TestClientFunctionsFileName, "%s\\AutoGen\\%s_CommandFuncs.c", pProjectPath, m_ShortMagicCommandFileName);
	sprintf(m_TestClientFunctionsHeaderName, "%s\\AutoGen\\%s_CommandFuncs.h", pProjectPath, m_ShortMagicCommandFileName);
	sprintf(m_RemoteFunctionsFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_RemoteFuncs.c", pProjectPath, pProjectName);
	sprintf(m_RemoteFunctionsHeaderName, "%s\\..\\Common\\AutoGen\\%s_autogen_RemoteFuncs.h", pProjectPath, pProjectName);
	sprintf(m_SlowFunctionsFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_SlowFuncs.c", pProjectPath, pProjectName);
	sprintf(m_SlowFunctionsHeaderName, "%s\\..\\Common\\AutoGen\\%s_autogen_SlowFuncs.h", pProjectPath, pProjectName);
	sprintf(m_QueuedFunctionsFileName, "%s\\AutoGen\\%s_autogen_QueuedFuncs.c", pProjectPath, pProjectName);
	sprintf(m_QueuedFunctionsHeaderName, "%s\\AutoGen\\%s_autogen_QueuedFuncs.h", pProjectPath, pProjectName);

	sprintf(m_ServerWrappersFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_ServerCmdWrappers.c", pProjectPath, pProjectName);
	sprintf(m_ServerWrappersHeaderFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_ServerCmdWrappers.h", pProjectPath, pProjectName);
	
	sprintf(m_ClientWrappersFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_ClientCmdWrappers.c", pProjectPath, pProjectName);
	sprintf(m_ClientWrappersHeaderFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_ClientCmdWrappers.h", pProjectPath, pProjectName);

	sprintf(m_GServerWrappersFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_GenericServerCmdWrappers.c", pProjectPath, pProjectName);
	sprintf(m_GServerWrappersHeaderFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_GenericServerCmdWrappers.h", pProjectPath, pProjectName);

	sprintf(m_GClientWrappersFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_GenericClientCmdWrappers.c", pProjectPath, pProjectName);
	sprintf(m_GClientWrappersHeaderFileName, "%s\\..\\Common\\AutoGen\\%s_autogen_GenericClientCmdWrappers.h", pProjectPath, pProjectName);

	sprintf(m_ClientToTestClientWrappersFileName, "%s\\AutoGen\\%s_autogen_TestClientCmds.c", pProjectPath, pProjectName);
	sprintf(m_ClientToTestClientWrappersHeaderFileName, "%s\\AutoGen\\%s_autogen_TestClientCmds.h", pProjectPath, pProjectName);
}

bool MagicCommandManager::DoesFileNeedUpdating(char *pFileName)
{
	return false;
}


bool MagicCommandManager::LoadStoredData(bool bForceReset)
{
	if (bForceReset)
	{
		m_bSomethingChanged = true;
		return false;
	}

	Tokenizer tokenizer;

	if (!tokenizer.LoadFromFile(m_MagicCommandFileName))
	{
		m_bSomethingChanged = true;
		return false;
	}

	if (!tokenizer.IsStringAtVeryEndOfBuffer("#endif"))
	{
		m_bSomethingChanged = true;
		return false;
	}

	tokenizer.SetExtraReservedWords(sMagicCommandReservedWords, &spMagicCommandReservedWordTree);
	tokenizer.SetCSourceStyleStrings(true);

	Token token;
	enumTokenType eType;

	do
	{
		eType = tokenizer.GetNextToken(&token);
		STATICASSERT(eType != TOKEN_NONE, "AUTOCOMMAND file corruption");
	} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_PARSABLE));

	tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of magic commands");

	int iNumMagicCommands = token.iVal;

	int iCommandNum;
	int i;

	for (iCommandNum = 0; iCommandNum < iNumMagicCommands; iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = (MAGIC_COMMAND_STRUCT*)calloc(sizeof(MAGIC_COMMAND_STRUCT), 1);
		eaPush(&m_ppMagicCommands, pCommand);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command flags");
		pCommand->iCommandFlags = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command flags2");
		pCommand->iCommandFlags2 = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command function name");
		strcpy(pCommand->functionName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command command name");
		strcpy(pCommand->commandName, token.sVal);

		strcpy(pCommand->safeCommandName, token.sVal);
		MakeStringAllAlphaNum(pCommand->safeCommandName);

		for (i=0; i < MAX_COMMAND_ALIASES; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command alias");
			strcpy(pCommand->commandAliases[i], token.sVal);
		}


		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command access level");
		pCommand->iAccessLevel = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 64, "Didn't find serverSpecificAccessLevel ser vername");
		strcpy(pCommand->serverSpecificAccessLevel_ServerName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find serverSpecificAccessLevel");
		pCommand->iServerSpecificAccessLevel = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Didn't find magic command source file");
		strcpy(pCommand->sourceFileName, token.sVal);
	
		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command line num");
		pCommand->iLineNum = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, TOKENIZER_MAX_STRING_LENGTH, "Didn't find magic command comment");
		RemoveCStyleEscaping(pCommand->comment, token.sVal);

		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command set name");
			strcpy(pCommand->commandSets[i], token.sVal);
		}

		for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command category name");
			strcpy(pCommand->commandCategories[i], token.sVal);
		}
	
		for (i=0; i < MAX_ERROR_FUNCTIONS_PER_COMMAND; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find who I'm the error function for");
			strcpy(pCommand->commandsWhichThisIsTheErrorFunctionFor[i], token.sVal);
		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find return arg type");
		pCommand->eReturnType = (enumMagicCommandArgType)token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH, "Didn't find return type name");
		strcpy(pCommand->returnTypeName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH, "Didn't find return static check type");
		strcpy(pCommand->returnStaticCheckParamType, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find expression return value category");
		pCommand->iReturnStaticCheckCategory = (enumExprArgCategory)token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find queue name");
		strcpy(pCommand->queueName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of defines");

		pCommand->iNumDefines = token.iVal;
		int i;
		for (i=0; i < pCommand->iNumDefines; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find command IFDEF");
			strcpy(pCommand->defines[i], token.sVal);
		}



		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of arguments");
		
		int iNumArgs = token.iVal;

		for (i=0; i < iNumArgs; i++)
		{
			MAGIC_COMMAND_ARG *pArg = (MAGIC_COMMAND_ARG*)calloc(sizeof(MAGIC_COMMAND_ARG), 1);
			eaPush(&pCommand->ppArgs, pArg);

			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find int for arg type");
			pArg->argType = (enumMagicCommandArgType)(token.iVal);

			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH, "Didn't find arg type name");
			strcpy(pArg->argTypeName, token.sVal);

			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMAND_ARGNAME_LENGTH, "Didn't find arg name");
			strcpy(pArg->argName, token.sVal);
	
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find namelist type");
			strcpy(pArg->argNameListTypeName, token.sVal);

			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find namelist data pointer");
			strcpy(pArg->argNameListDataPointerName, token.sVal);
	
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find namelist data pointer-was-string");
			pArg->argNameListDataPointerWasString = (bool)(token.iVal == 1);
	
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find arg flags");
			pArg->argFlags = token.iVal;

			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find bHasIntDefault");
			pArg->bHasDefaultInt = !!token.iVal;
		
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find int default");
			pArg->iDefaultInt = token.iVal;
		
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find bHasStringDefault");
			pArg->bHasDefaultString = !!token.iVal;
		
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMAND_ARGDEFAULT_LENGTH - 1, "Didn't find string default");
			strcpy(pArg->defaultString, token.sVal);

		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of expression tags");
		
		pCommand->iNumExpressionTags = token.iVal;

		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find expression tag");
			strcpy(pCommand->expressionTag[i], token.sVal);
		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find expression static checking function");
		strcpy(pCommand->expressionStaticCheckFunc, token.sVal);

		for (i=0; i < iNumArgs; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find string for static check type");
			strcpy(pCommand->ppArgs[i]->expressionStaticCheckParamType, token.sVal);
		}

		for (i=0; i < iNumArgs; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find expression argument flags");
			pCommand->ppArgs[i]->iExpressionArgFlags = token.iVal;
		}

		for (i=0; i < iNumArgs; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find expression argument categories");
			pCommand->ppArgs[i]->iExpressionArgCategory = (enumExprArgCategory)token.iVal;
		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find the expression function cost");
		pCommand->iExpressionCost = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find the bIsExprCodeExample");
		pCommand->bIsExprCodeExample = (token.iVal != 0);

		pCommand->pIfDefStack = ReadIfDefStackFromFile(&tokenizer);

		ReadEarrayOfIdentifiersFromFile(&tokenizer, &pCommand->ppProducts);
	}
	
	tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of magic command variables");
	int iNumMagicCommandVars = token.iVal;

	int iVarNum;
	for (iVarNum = 0; iVarNum < iNumMagicCommandVars; iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = (MAGIC_COMMANDVAR_STRUCT*)calloc(sizeof(MAGIC_COMMANDVAR_STRUCT), 1);
		eaPush(&m_ppMagicCommandVars, pCommandVar);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command var flags");
		pCommandVar->iCommandFlags = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command var flags2");
		pCommandVar->iCommandFlags2 = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command var name");
		strcpy(pCommandVar->varCommandName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Didn't find magic command var source file name");
		strcpy(pCommandVar->sourceFileName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command var source file line num");
		pCommandVar->iLineNum = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command var type");
		pCommandVar->eVarType = (enumMagicCommandArgType)token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command var access level");
		pCommandVar->iAccessLevel = (enumMagicCommandArgType)token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 64, "Didn't find serverSpecificAccessLevel ser vername");
		strcpy(pCommandVar->serverSpecificAccessLevel_ServerName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find serverSpecificAccessLevel");
		pCommandVar->iServerSpecificAccessLevel = token.iVal;

		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command var set");
			strcpy(pCommandVar->commandSets[i], token.sVal);
		}

		for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
		{
			tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command var category");
			strcpy(pCommandVar->commandCategories[i], token.sVal);
		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, TOKENIZER_MAX_STRING_LENGTH, "Didn't find magic command var comment");
		RemoveCStyleEscaping(pCommandVar->comment, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command callback func");
		strcpy(pCommandVar->callbackFunc, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find magic command maxvalue");
		pCommandVar->iMaxValue = (enumMagicCommandArgType)token.iVal;

		pCommandVar->pIfDefStack = ReadIfDefStackFromFile(&tokenizer);

		ReadEarrayOfIdentifiersFromFile(&tokenizer, &pCommandVar->ppProducts);

		ReadGlobalTypes_Parsable(&tokenizer, &pCommandVar->autoSettingGlobalTypes);


	}





	m_bSomethingChanged = false;

	return true;
}

void MagicCommandManager::DestroyCommand(MAGIC_COMMAND_STRUCT *pCommand)
{
	if (pCommand->pIfDefStack)
	{
		DestroyIfDefStack(pCommand->pIfDefStack);
		pCommand->pIfDefStack = NULL;
	}

	if (pCommand->pExprCode)
	{
		delete pCommand->pExprCode;
		pCommand->pExprCode = NULL;
	}

	eaDestroyEx(&pCommand->ppArgs, NULL);

	eaDestroyEx(&pCommand->ppProducts, NULL);

	free(pCommand);
}

void MagicCommandManager::DestroyCommandVar(MAGIC_COMMANDVAR_STRUCT *pCommandVar)
{
	if (pCommandVar->pIfDefStack)
	{
		DestroyIfDefStack(pCommandVar->pIfDefStack);
		pCommandVar->pIfDefStack = NULL;
	}

	eaDestroyEx(&pCommandVar->ppProducts, NULL);

	free(pCommandVar);
}


void MagicCommandManager::ResetSourceFile(char *pSourceFileName)
{
	int i = 0;

	while (i < eaSize(&m_ppMagicCommands))
	{
		if (AreFilenamesEqual(m_ppMagicCommands[i]->sourceFileName, pSourceFileName))
		{
			DestroyCommand(m_ppMagicCommands[i]);
			eaRemove(&m_ppMagicCommands, i);

			m_bSomethingChanged = true;
		}
		else
		{
			i++;
		}
	}

	i = 0;
	while (i < eaSize(&m_ppMagicCommandVars))
	{
		if (AreFilenamesEqual(m_ppMagicCommandVars[i]->sourceFileName, pSourceFileName))
		{
			DestroyCommandVar(m_ppMagicCommandVars[i]);
			eaRemove(&m_ppMagicCommandVars, i);

			m_bSomethingChanged = true;
		}
		else
		{
			i++;
		}
	}
}

char *MagicCommandManager::GetReturnValueMultiValTypeName(enumMagicCommandArgType eArgType)
{
	switch (eArgType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_UINT:
	case ARGTYPE_SINT64:
	case ARGTYPE_UINT64:
	case ARGTYPE_ENUM:
	case ARGTYPE_BOOL:
		return "MULTI_INT";
	case ARGTYPE_FLOAT:
	case ARGTYPE_FLOAT64:
		return "MULTI_FLOAT";
	case ARGTYPE_STRING:
	case ARGTYPE_SENTENCE:
	case ARGTYPE_ESCAPEDSTRING:
		return "MULTI_STRING";
	case ARGTYPE_STRUCT:
		return "MULTI_NP_POINTER";
	default:
		return "MULTI_NONE";
	}
}

char *MagicCommandManager::GetErrorFunctionName(char *pCommandName)
{
	static char returnBuffer[MAX_MAGICCOMMANDNAMELENGTH];

	int iCommandNum, i;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pErrorCommand = m_ppMagicCommands[iCommandNum];

		for (i=0; i < MAX_ERROR_FUNCTIONS_PER_COMMAND; i++)
		{
			if (!pErrorCommand->commandsWhichThisIsTheErrorFunctionFor[i])
			{
				break;
			}

			if (strcmp(pErrorCommand->commandsWhichThisIsTheErrorFunctionFor[i], pCommandName) == 0)
			{
				sprintf(returnBuffer, "%s_MAGICCOMMANDWRAPPER", pErrorCommand->functionName);
				return returnBuffer;
			}
		}
	}

	sprintf(returnBuffer, "NULL");
	return returnBuffer;
}

bool MagicCommandManager::ArgTypeIsExpressionOnly(enumMagicCommandArgType eArgType)
{
	return (eArgType >= ARGTYPE_EXPR_FIRST && eArgType <= ARGTYPE_EXPR_LAST);
}

bool MagicCommandManager::CommandHasExpressionOnlyArgumentsOrReturnVals(MAGIC_COMMAND_STRUCT *pCommand)
{
	if (ArgTypeIsExpressionOnly(pCommand->eReturnType))
	{
		return true;
	}

	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		if (ArgTypeIsExpressionOnly(pCommand->ppArgs[i]->argType))
		{
			return true;
		}
	}

	return false;
}

bool MagicCommandManager::CommandGetsWrittenOutInCommandSets(MAGIC_COMMAND_STRUCT *pCommand)
{
	return (!pCommand->commandsWhichThisIsTheErrorFunctionFor[0][0] 
		&& !(pCommand->iCommandFlags & COMMAND_FLAG_QUEUED) 
		&& !CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand)
		&& 	!(pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER && !(pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL) && !(pCommand->commandSets[0][0]))
		&& !CommandHasArgOfType(pCommand, ARGTYPE_PACKET));
}

void MagicCommandManager::WriteCommandSetData(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		if (pCommand->ppArgs[i]->argType == ARGTYPE_STRUCT)
		{
			fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->ppArgs[i]->argTypeName);
			fprintf(pOutFile, "#define TYPE_parse_%s %s\n", pCommand->ppArgs[i]->argTypeName, pCommand->ppArgs[i]->argTypeName);
		}
	}

	if (pCommand->eReturnType == ARGTYPE_STRUCT)
	{
		fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->returnTypeName);
		fprintf(pOutFile, "#define TYPE_parse_%s %s\n", pCommand->returnTypeName, pCommand->returnTypeName);
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
	{
		fprintf(pOutFile, "%sCmd cmdSetData_SlowRemoteCommand_%s[] = {\n", pCommand->iCommandFlags & COMMAND_FLAG_NONSTATICINTERNALCMD ? "" : "static ", pCommand->functionName);
	}
	else if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
	{
		fprintf(pOutFile, "%sCmd cmdSetData_RemoteCommand_%s[] = {\n", pCommand->iCommandFlags & COMMAND_FLAG_NONSTATICINTERNALCMD ? "" : "static ", pCommand->functionName);
	}
	else if (pCommand->commandSets[0][0] && !(pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL))
	{
		fprintf(pOutFile, "%sCmd cmdSetData_%s_%s[] = {\n", pCommand->iCommandFlags & COMMAND_FLAG_NONSTATICINTERNALCMD ? "" : "static ", pCommand->commandSets[0], pCommand->functionName);
	}
	else
	{
		fprintf(pOutFile, "%sCmd cmdSetData_%s[] = {\n", pCommand->iCommandFlags & COMMAND_FLAG_NONSTATICINTERNALCMD ? "" : "static ", pCommand->functionName);
	}
	

	int iAliasNum;

	char categoryString[2048] = "\" ";
	bool bAtLeastOneCategory = false;

	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommand->commandCategories[i][0])
		{
			strcat(categoryString, pCommand->commandCategories[i]);
			strcat(categoryString, " ");
			bAtLeastOneCategory = true;
		}
	}

	if (bAtLeastOneCategory)
	{
		strcat(categoryString, "\"");
	}
	else
	{
		sprintf(categoryString, "NULL");
	}

	//AliasNum -1 means use commandName
	for (iAliasNum = -1; iAliasNum < MAX_COMMAND_ALIASES; iAliasNum++)
	{
		if (iAliasNum == -1 || pCommand->commandAliases[iAliasNum][0])
		{
			char escapedFileName[MAX_PATH];
			AddCStyleEscaping(escapedFileName, GetFileNameWithoutDirectories(pCommand->sourceFileName) + 1, MAX_PATH);


			fprintf(pOutFile, "\t{ %d, \"%s\", \"%s\", %d, %s, s%sName, %s, {", pCommand->iAccessLevel, 
				iAliasNum == -1 ? pCommand->commandName : pCommand->commandAliases[iAliasNum],
				escapedFileName, pCommand->iLineNum,
				GetProductString(&pCommand->ppProducts),
				m_pParent->GetShortProjectName(),
				categoryString);

			int iNumNormalArgs = GetNumNormalArgs(pCommand);

			if (iNumNormalArgs == 0)
			{
				fprintf(pOutFile, "{0}");
			}
			else
			{
				int i;
				int iNumArgsFound = 0;

				for (i=0; i < eaSize(&pCommand->ppArgs); i++)
				{
					MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

					if (pArg->argType < ARGTYPE_FIRST_SPECIAL)
					{
						fprintf(pOutFile, GetArgDescriptionBlock(pArg->argType, 
							pArg->argName, pArg->argTypeName, pArg->argNameListDataPointerName, pArg->argNameListTypeName, 
							pArg->argNameListDataPointerWasString, pArg->bHasDefaultInt, pArg->iDefaultInt,
							pArg->bHasDefaultString ? pArg->defaultString : NULL));

						iNumArgsFound++;

						if (iNumArgsFound < iNumNormalArgs)
						{
							fprintf(pOutFile, ", ");
						}
					}
				}
			}

			char tempString1[TOKENIZER_MAX_STRING_LENGTH];
			char tempString2[TOKENIZER_MAX_STRING_LENGTH];
			strcpy(tempString1, pCommand->comment);
			char *pFirstNewLine = strchr(tempString1, '\n');
			char *pErrorFuncName = "NULL";
			char returnTypeTPI[256] = "NULL";

/*			if (pFirstNewLine)
			{
				*pFirstNewLine = 0;
			}*/

			AddCStyleEscaping(tempString2, tempString1, TOKENIZER_MAX_STRING_LENGTH);
			
			if (CommandCanHaveErrorFunction(pCommand))
			{
				pErrorFuncName = GetErrorFunctionName(pCommand->commandName);
			}

			if (pCommand->eReturnType == ARGTYPE_STRUCT)
			{
				sprintf(returnTypeTPI, "parse_%s", pCommand->returnTypeName);
			}

			fprintf(pOutFile, "},%s%s%s%s%s%s%s%s%s,\n\t\t\"%s\", %s_MAGICCOMMANDWRAPPER,{NULL,%s,%s,0,0}, %s}%s\n", 
				(pCommand->iCommandFlags & COMMAND_FLAG_HIDE) ? "CMDF_HIDEPRINT" : "0",
				(pCommand->iCommandFlags & COMMAND_FLAG_COMMANDLINE) ? " | CMDF_COMMANDLINE" : "", 
				(pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE) ? " | CMDF_EARLYCOMMANDLINE" : "", 
				(pCommand->iCommandFlags & COMMAND_FLAG_COMMANDLINE_ONLY) ? " | CMDF_COMMANDLINEONLY" : "", 
				(pCommand->iCommandFlags & COMMAND_FLAG_PASSENTITY) ? " | CMDF_PASSENTITY" : "",
				(pCommand->iCommandFlags & COMMAND_FLAG_IGNOREPARSEERRORS) ? " | CMDF_IGNOREPARSEERRORS" : "",
				(pCommand->iCommandFlags & COMMAND_FLAG_CACHE_AUTOCOMPLETE) ? " | CMDF_CACHE_AUTOCOMPLETE" : "",
				(pCommand->iCommandFlags2 & COMMAND_FLAG2_ALLOW_JSONRPC) ? " | CMDF_ALLOW_JSONRPC" : "",
				(eaSize(&pCommand->ppProducts) == 1 && stricmp(pCommand->ppProducts[0], "all") == 0)  ? " | CMDF_ALL_PRODUCTS" : "",
	
				tempString2, pCommand->functionName,
				GetReturnValueMultiValTypeName(pCommand->eReturnType), 
				returnTypeTPI,
				pErrorFuncName,
				pCommand->commandAliases[0][0] ? "," : "");
		}
	}

	fprintf(pOutFile, "\t%s\n};\n",
		pCommand->commandAliases[0][0] ? "{0}": "");
}


void MagicCommandManager::WriteCommandVarSetData(FILE *pOutFile, MAGIC_COMMANDVAR_STRUCT *pCommandVar)
{
	if (pCommandVar->callbackFunc[0])
	{
		fprintf(pOutFile, "extern void %s(CMDARGS);\n", pCommandVar->callbackFunc);
	}

	if (pCommandVar->commandSets[0][0] && !(pCommandVar->iCommandFlags & COMMAND_FLAG_GLOBAL))
	{
		fprintf(pOutFile, "%sCmd cmdVarSetData_%s_%s =\n", pCommandVar->iCommandFlags & COMMAND_FLAG_NONSTATICINTERNALCMD ? "" : "static ", pCommandVar->commandSets[0],pCommandVar->varCommandName);
	}
	else
	{
		fprintf(pOutFile, "%sCmd cmdVarSetData_%s =\n", pCommandVar->iCommandFlags & COMMAND_FLAG_NONSTATICINTERNALCMD ? "" : "static ", pCommandVar->varCommandName);
	}

	char tempString1[TOKENIZER_MAX_STRING_LENGTH];
	char tempString2[TOKENIZER_MAX_STRING_LENGTH];
	strcpy(tempString1, pCommandVar->comment);
	char *pFirstNewLine = strchr(tempString1, '\n');

/*	if (pFirstNewLine)
	{
		*pFirstNewLine = 0;
	}*/

	AddCStyleEscaping(tempString2, tempString1, TOKENIZER_MAX_STRING_LENGTH);

	char categoryString[2048] = "\" ";
	bool bAtLeastOneCategory = false;
	int i;

	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommandVar->commandCategories[i][0])
		{
			strcat(categoryString, pCommandVar->commandCategories[i]);
			strcat(categoryString, " ");
			bAtLeastOneCategory = true;
		}
	}

	if (bAtLeastOneCategory)
	{
		strcat(categoryString, "\"");
	}
	else
	{
		sprintf(categoryString, "NULL");
	}

	char escapedFileName[MAX_PATH];
	AddCStyleEscaping(escapedFileName, GetFileNameWithoutDirectories(pCommandVar->sourceFileName) + 1, MAX_PATH);

	fprintf(pOutFile, "{\n\t%d, \"%s\", \"%s\", %d, %s, s%sName,  %s, {{ NULL, %s, NULL, 0, %s, %d }},CMDF_PRINTVARS %s%s%s%s%s%s%s%s,\n\t\t\"%s\", %s, {NULL, MULTI_NONE,0,0,0}, %s\n};\n\n",
		pCommandVar->iAccessLevel, pCommandVar->varCommandName, 
		escapedFileName, pCommandVar->iLineNum,
		GetProductString(&pCommandVar->ppProducts),
		m_pParent->GetShortProjectName(),
		categoryString,
		GetReturnValueMultiValTypeName(pCommandVar->eVarType),
		pCommandVar->eVarType == ARGTYPE_SENTENCE ? "CMDAF_SENTENCE" : "0", 
		pCommandVar->iMaxValue,
		(pCommandVar->iCommandFlags & COMMAND_FLAG_HIDE) ? " | CMDF_HIDEPRINT" : "",
		(pCommandVar->iCommandFlags & COMMAND_FLAG_COMMANDLINE) ? " | CMDF_COMMANDLINE" : "",
		(pCommandVar->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE) ? " | CMDF_EARLYCOMMANDLINE" : "",
		(pCommandVar->iCommandFlags & COMMAND_FLAG_COMMANDLINE_ONLY) ? " | CMDF_COMMANDLINEONLY" : "", 
		(pCommandVar->iCommandFlags & COMMAND_FLAG_PASSENTITY) ? " | CMDF_PASSENTITY" : "",
		(pCommandVar->iCommandFlags & COMMAND_FLAG_IGNOREPARSEERRORS) ? " | CMDF_IGNOREPARSEERRORS" : "",
		(pCommandVar->iCommandFlags2 & COMMAND_FLAG2_ALLOW_JSONRPC) ? " | CMDF_ALLOW_JSONRPC" : "",
		(eaSize(&pCommandVar->ppProducts) == 1 && stricmp(pCommandVar->ppProducts[0], "all") == 0)  ? " | CMDF_ALL_PRODUCTS" : "",
		tempString2,
		pCommandVar->callbackFunc[0] ? pCommandVar->callbackFunc : "NULL",
		GetErrorFunctionName(pCommandVar->varCommandName));
	pCommandVar->bWritten = true;
}

/*
int MagicCommandManager::WriteCommandSet(FILE *pOutFile, char *pSetName, int iFlagToMatch)
{
	int iCommandNum;
	int iIndex = 0;
	int iVarNum;
	
	char tableName[MAX_MAGICCOMMANDNAMELENGTH];
	
	if (pSetName[0])
	{
		char tempName[MAX_MAGICCOMMANDNAMELENGTH];
		strcpy(tempName, pSetName);
		MakeStringAllAlphaNum(tempName);
		sprintf(tableName, "Auto_Cmds_%s_%s", m_ProjectName, tempName);


		STATICASSERT(m_iNumSetsWritten < MAX_OVERALL_SETS, "Too many total command sets");
		strcpy(m_SetsWritten[m_iNumSetsWritten++], pSetName);

	}
	else
	{
		sprintf(tableName, "Auto_Cmds_%s", m_ProjectName);
	}

	//func prototypes for callback funcs for variable commands
	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

		if (CommandVarIsInSet(pCommandVar, pSetName, iFlagToMatch))
		{
			if (pCommandVar->callbackFunc[0])
			{
				fprintf(pOutFile, "extern void %s(CMDARGS);\n", pCommandVar->callbackFunc);
			}
		}
	}

	//parse_x prototypes for struct args and return values
	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandIsInSet(pCommand, pSetName, iFlagToMatch))
		{
			if (CommandGetsWrittenOutInCommandSets(pCommand))
			{
				int i;

				for (i=0; i < eaSize(&pCommand->ppArgs); i++)
				{
					if (pCommand->argTypes[i] == ARGTYPE_STRUCT)
					{
						fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->argTypeNames[i]);
					}
				}

				if (pCommand->eReturnType == ARGTYPE_STRUCT)
				{
					fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->returnTypeName);
				}
			}
		}
	}



	fprintf(pOutFile, "\nCmd %s[] =\n{\n", tableName);


	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandIsInSet(pCommand, pSetName, iFlagToMatch))
		{
			if (CommandGetsWrittenOutInCommandSets(pCommand))
			{
				pCommand->bWritten = true;

				int iAliasNum;

				//AliasNum -1 means use commandName
				for (iAliasNum = -1; iAliasNum < MAX_COMMAND_ALIASES; iAliasNum++)
				{
					if (iAliasNum == -1 || pCommand->commandAliases[iAliasNum][0])
					{

						fprintf(pOutFile, "\t{ %d, \"%s\", {", pCommand->iAccessLevel, 
							iAliasNum == -1 ? pCommand->commandName : pCommand->commandAliases[iAliasNum]);

						int iNumNormalArgs = GetNumNormalArgs(pCommand);

						if (iNumNormalArgs == 0)
						{
							fprintf(pOutFile, "{0}");
						}
						else
						{
							int i;
							int iNumArgsFound = 0;

							for (i=0; i < eaSize(&pCommand->ppArgs); i++)
							{
								if (pCommand->argTypes[i] < ARGTYPE_FIRST_SPECIAL)
								{
									fprintf(pOutFile, GetInCodeCommandArgTypeNameFromArgType(pCommand->argTypes[i], pCommand->argTypeNames[i]));

									iNumArgsFound++;

									if (iNumArgsFound < iNumNormalArgs)
									{
										fprintf(pOutFile, ", ");
									}
								}
							}
						}

						char tempString1[TOKENIZER_MAX_STRING_LENGTH];
						char tempString2[TOKENIZER_MAX_STRING_LENGTH];
						strcpy(tempString1, pCommand->comment);
						char *pFirstNewLine = strchr(tempString1, '\n');

						if (pFirstNewLine)
						{
							*pFirstNewLine = 0;
						}

						AddCStyleEscaping(tempString2, tempString1, TOKENIZER_MAX_STRING_LENGTH);
					
						fprintf(pOutFile, "},%s,\n\t\t\"%s\", %s_MAGICCOMMANDWRAPPER,{%s,0,0,0}, %s},\n", 
							(pCommand->iCommandFlags & COMMAND_FLAG_HIDE) ? "CMDF_HIDEPRINT" : "0", 
							tempString2, pCommand->functionName,
							GetReturnValueMultiValTypeName(pCommand->eReturnType), 
							GetErrorFunctionName(pCommand->commandName));

						iIndex++;
					}
				}
			}
		}
	}

	
	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

		if (CommandVarIsInSet(pCommandVar, pSetName, iFlagToMatch))
		{

			char tempString1[TOKENIZER_MAX_STRING_LENGTH];
			char tempString2[TOKENIZER_MAX_STRING_LENGTH];
			strcpy(tempString1, pCommandVar->comment);
			char *pFirstNewLine = strchr(tempString1, '\n');

			if (pFirstNewLine)
			{
				*pFirstNewLine = 0;
			}

			AddCStyleEscaping(tempString2, tempString1, TOKENIZER_MAX_STRING_LENGTH);



			fprintf(pOutFile, "\t{ %d, \"%s\", {{ %s, NULL, 0, %s, %d }},CMDF_PRINTVARS %s,\n\t\t\"%s\", %s, {MULTI_NONE,0,0,0}, %s},\n",
				pCommandVar->iAccessLevel, pCommandVar->varCommandName, GetReturnValueMultiValTypeName(pCommandVar->eVarType),
				pCommandVar->eVarType == ARGTYPE_SENTENCE ? "CMDAF_SENTENCE" : "0", 
				pCommandVar->iMaxValue,
				pCommandVar->bFoundHide ? " | CMDF_HIDEPRINT" : "",
				tempString2,
				pCommandVar->callbackFunc[0] ? pCommandVar->callbackFunc : "NULL",
				GetErrorFunctionName(pCommandVar->varCommandName));
			pCommandVar->bWritten = true;

			strcpy(pCommandVar->tableName, tableName);
			pCommandVar->iIndexInTable = iIndex;

			iIndex++;
		}
	}

	fprintf(pOutFile, "\t{0}\n};\n\n");

	return iIndex;
}
*/

char *MagicCommandManager::GetFixedUpArgTypeNameForFuncPrototype(MAGIC_COMMAND_STRUCT *pCommand, int iArgNum)
{
	static char sBuf[MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH + 64];
	MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

	sprintf(sBuf, "%s%s", pArg->argTypeName, (IsPointerType(pArg->argType) || pArg->argType == ARGTYPE_STRUCT) ? "*" : "");

	return sBuf;
}

char *MagicCommandManager::GetFixedUpArgTypeNameForReadArgs(MAGIC_COMMAND_STRUCT *pCommand, int iArgNum)
{
	static char sBuf[MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH + 64];
	MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];


	sprintf(sBuf, "%s%s", 
        (pArg->argType == ARGTYPE_ENUM ? "int" : pArg->argTypeName),
        (IsPointerType(pArg->argType) 
		|| IsDirectVersionOfPointerType(pArg->argType) || pArg->argType == ARGTYPE_STRUCT) ? "*" : "");

	return NoConst(sBuf);
}

char *MagicCommandManager::GetReturnTypeName(MAGIC_COMMAND_STRUCT *pCommand)
{
	static char sBuf[MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH + 64];

	sprintf(sBuf, "%s%s", pCommand->returnTypeName, pCommand->eReturnType ==  ARGTYPE_STRUCT ? "*" : "");

	return sBuf;
}

void MagicCommandManager::VerifyCommandValidityPreWriteOut(MAGIC_COMMAND_STRUCT *pCommand)
{
	int i;

	if (pCommand->iCommandFlags & COMMAND_FLAG_STATIC_RETURN_STRUCT)
	{
		CommandAssert(pCommand, pCommand->eReturnType == ARGTYPE_STRUCT, "Non-struct-returning AUTO_COMMAND can't have ACMD_STATIC_RETURN");
	}

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_STRUCT)
		{
			if (strstr(pArg->argTypeName, "const "))
			{
				char temp[MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH];
				strcpy(temp, pArg->argTypeName);
				strcpy(pArg->argTypeName, strstr(temp, "const ") + 6);
			}

			char errorString[1024];
			sprintf(errorString, "Unrecognized struct type name %s", pArg->argTypeName);
			CommandAssert(pCommand,  m_pParent->GetDictionary()->FindIdentifier(pArg->argTypeName) == IDENTIFIER_STRUCT 
				|| m_pParent->GetDictionary()->FindIdentifier(pArg->argTypeName) == IDENTIFIER_STRUCT_CONTAINER
				|| m_pParent->GetDictionary()->FindIdentifier(pArg->argTypeName) == IDENTIFIER_NONE,
				errorString);
		}

		if (pArg->bHasDefaultInt)
		{
			switch (pArg->argType)
			{
			case ARGTYPE_STRUCT:
				CommandAssert(pCommand, pArg->iDefaultInt == 0, "Structs can have default NULL only");
				break;

			case ARGTYPE_STRING:
				CommandAssert(pCommand, pArg->iDefaultInt == 0, "Strings can only have string or NULL defaults");
				break;

			case ARGTYPE_SINT:
			case ARGTYPE_UINT:
			case ARGTYPE_SINT64:
			case ARGTYPE_UINT64:
			case ARGTYPE_BOOL:
			case ARGTYPE_ENUM:
				//happy
				break;
			default:
				CommandAssert(pCommand, 0, "Default int only allowed for int fields");
			}
		}
		else if (pArg->bHasDefaultString)
		{
			CommandAssert(pCommand, pArg->argType == ARGTYPE_STRING, "Default string only allowed for string fields");
		}
	}

	if (pCommand->eReturnType == ARGTYPE_STRUCT)
	{
		char errorString[1024];
		sprintf(errorString, "Unrecognized struct type name %s", pCommand->returnTypeName);
		CommandAssert(pCommand,  m_pParent->GetDictionary()->FindIdentifier(pCommand->returnTypeName) == IDENTIFIER_STRUCT
			|| (m_pParent->GetDictionary()->FindIdentifier(pCommand->returnTypeName) == IDENTIFIER_NONE/* && !(pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER) */)
			|| m_pParent->GetDictionary()->FindIdentifier(pCommand->returnTypeName) == IDENTIFIER_STRUCT_CONTAINER,
			errorString);
	}




	for (i=0; i < MAX_ERROR_FUNCTIONS_PER_COMMAND; i++)
	{
		if (pCommand->commandsWhichThisIsTheErrorFunctionFor[i][0])
		{
			CommandAssert(pCommand, strcmp(pCommand->commandName, pCommand->functionName) == 0, "Error functions can't have command names");
			CommandAssert(pCommand, pCommand->commandSets[0][0] == 0 && pCommand->commandCategories[0][0] == 0, "Error functions can't have categories or sets");
			CommandAssert(pCommand, GetNumNormalArgs(pCommand) == 0, "Error functions can't have arguments");


			bool bFound = false;
			MAGIC_COMMAND_STRUCT *pFoundCommand = NULL;
			MAGIC_COMMANDVAR_STRUCT *pFoundCommandVar = NULL;


			int iCommandNum;

			for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
			{
				MAGIC_COMMAND_STRUCT *pOtherCommand = m_ppMagicCommands[iCommandNum];
				
				if (CommandCanHaveErrorFunction(pOtherCommand) && pOtherCommand != pCommand)
				{
					int j;
	
					if (strcmp(pOtherCommand->commandName, pCommand->commandsWhichThisIsTheErrorFunctionFor[i]) == 0)
					{
						if (bFound)
						{
							char errorString[2048];
							sprintf(errorString, "Command %s is the error function for command %s. But there are two commands with that name, one in %s and one in %s",
								pCommand->commandName, pCommand->commandsWhichThisIsTheErrorFunctionFor[i], 
								pFoundCommand->sourceFileName, pOtherCommand->sourceFileName);

							CommandAssert(pCommand, 0, errorString);
						}


						bFound = true;
						pFoundCommand = pOtherCommand;
					}

					for (j=0; j < MAX_ERROR_FUNCTIONS_PER_COMMAND; j++)
					{
						if (strcmp(pCommand->commandsWhichThisIsTheErrorFunctionFor[i], pOtherCommand->commandsWhichThisIsTheErrorFunctionFor[j]) == 0)
						{
							CommandAssert(pCommand, 0, "Found two command trying to be error functions for the same command");
						}
					}
				}
			}

			if (pFoundCommand)
			{
				CommandAssert(pCommand, pFoundCommand->eReturnType == pCommand->eReturnType, "An error function must have the same return type as its command");
			}
				
			
			int iVarNum;
			for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
			{
				MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

				if (strcmp(pCommandVar->varCommandName, pCommand->commandsWhichThisIsTheErrorFunctionFor[i]) == 0)
				{
					if (pFoundCommand)
					{
						char errorString[1024];
						sprintf(errorString, "Command %s appears to be the error function for both a command in %s and a command var in %s",
							pCommand->commandName, pFoundCommand->sourceFileName, pCommandVar->sourceFileName);
						CommandAssert(pCommand, 0, errorString);
					}

					if (pFoundCommandVar)
					{
						char errorString[1024];
						sprintf(errorString, "Command %s appears to be the error function for two command vars in %s and %s",
							pCommand->commandName, pFoundCommandVar->sourceFileName, pCommandVar->sourceFileName);
						CommandAssert(pCommand, 0, errorString);
					}

					CommandAssert(pCommand, pCommand->eReturnType == ARGTYPE_NONE, "An error func for a command variable must have no return type");

					bFound = true;
					pFoundCommandVar = pCommandVar;
				}
			}
			

			CommandAssertf(pCommand, bFound, "Command is trying to be the error function for an unknown command %s", pCommand->commandsWhichThisIsTheErrorFunctionFor[i]);
		}
	}

	if (CommandHasArgOfType(pCommand, ARGTYPE_PACKET))
	{
		CommandAssertf(pCommand, CommandShouldGetRemotePacketWrapper(pCommand), "Packets can only be args to non-returning remote commands");
	}

	if (eaSize(&pCommand->ppProducts))
	{
		if (pCommand->iCommandFlags & (COMMAND_FLAG_SLOW_REMOTE | COMMAND_FLAG_REMOTE | COMMAND_FLAG_QUEUED | COMMAND_FLAG_EARLYCOMMANDLINE))
		{
			CommandAssertf(pCommand, false, "Command can't have ACMD_PRODUCTS and be remote or queued or early");
		}

		CommandAssert(pCommand, pCommand->commandSets[0][0] == 0, "Commands can't have both ACMD_PRODUCTS and ACMD_LIST");
	}

}

bool MagicCommandManager::CommandNeedsNormalWrapper(MAGIC_COMMAND_STRUCT *pCommand)
{
	if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
	{
		return false;
	}

	if (CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand))
	{
		return false;
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER && !(pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL) && !(pCommand->commandSets[0][0]))
	{
		return false;
	}

	if (CommandHasArgOfType(pCommand, ARGTYPE_PACKET))
	{
		return false;
	}

	return true;
}

int MagicCommandManager::MagicCommandComparator(const void *p1, const void *p2)
{
	MAGIC_COMMAND_STRUCT *pCommand1 = *((MAGIC_COMMAND_STRUCT**)p1);
	MAGIC_COMMAND_STRUCT *pCommand2 = *((MAGIC_COMMAND_STRUCT**)p2);

	return strcmp(pCommand1->functionName, pCommand2->functionName);
}

int MagicCommandManager::MagicCommandVarComparator(const void *p1, const void *p2)
{
	MAGIC_COMMANDVAR_STRUCT *pCommandVar1 = *((MAGIC_COMMANDVAR_STRUCT**)p1);
	MAGIC_COMMANDVAR_STRUCT *pCommandVar2 = *((MAGIC_COMMANDVAR_STRUCT**)p2);

	return strcmp(pCommandVar1->varCommandName, pCommandVar2->varCommandName);
}

bool MagicCommandManager::CommandShouldGetRemotePacketWrapper(MAGIC_COMMAND_STRUCT *pCommand)
{
	return (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE) && !(pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE) &&
		(pCommand->eReturnType == ARGTYPE_NONE) && (GetNumNormalArgs(pCommand) == eaSize(&pCommand->ppArgs));
}

bool MagicCommandManager::WriteOutData(void)
{
	int iCommandNum;
	int iVarNum;
	bool foundEnt = false;
	bool foundTran = false;
	int i;

	bool bAtLeastOneSlowCommand = false;
	bool bAtLeastOneExpressionListCommand = false;
	bool bAtLeastOnePacketCommand = false;

	if (!m_bSomethingChanged)
	{
		return false;
	}

	qsort(m_ppMagicCommands, eaSize(&m_ppMagicCommands), sizeof(void*), MagicCommandComparator);
	qsort(m_ppMagicCommandVars, eaSize(&m_ppMagicCommandVars), sizeof(void*), MagicCommandVarComparator);


	m_pParent->GetAutoRunManager()->ResetSourceFile("autogen_magiccommands");


	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		pCommand->bWritten = false;
		pCommand->bAlreadyWroteOutComment = false;

		VerifyCommandValidityPreWriteOut(pCommand);

		if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
		{
			bAtLeastOneSlowCommand = true;
		}

		if (pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER)
		{
			bAtLeastOneExpressionListCommand = true;
		}

		if (CommandShouldGetRemotePacketWrapper(pCommand))
		{
			bAtLeastOnePacketCommand = true;
		}

	}

	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];
		pCommandVar->bWritten = false;
		pCommandVar->bAlreadyWroteOutComment = false;
	}


	WriteOutRemoteCommands();



	FILE *pOutFile = fopen_nofail(m_MagicCommandFileName, "wt");
	FILE *pExprCodeHeaderFile = fopen_nofail(m_MagicCommandExprCodeHeaderFileName, "wt");
	FILE *pExecutableOnlyFile = NULL;

	if (m_pParent->ProjectIsExecutable())
	{
		pExecutableOnlyFile = fopen_nofail(m_MagicCommandExecutableOnlyFileName, "wt");
	}

	fprintf(pOutFile, "//This file contains data and prototypes for magic commands. It is autogenerated by StructParser\n"
        "\n//autogenerated" "nocheckin\n#include \"cmdparse.h\"\n#include \"textparser.h\"\n#include \"globaltypes.h\"\n#include \"net/net.h\"\n#include \"net/netpacketutil.h\"\n#include \"structnet.h\"\n#include \"structInternals.h\"\n");


	fprintf(pOutFile, "static char s%sName[] = \"%s\";\n",
		m_pParent->GetShortProjectName(),
		m_pParent->GetShortProjectName());


	if (bAtLeastOneSlowCommand)
	{
		ForceIncludeFile(pOutFile, m_SlowFunctionsFileName, NULL);
	}

//	if (bAtLeastOneExpressionListCommand)
//	{
		fprintf(pOutFile, "//if one of these includes fails, you're trying to use expression list commands somewhere illegal... talk to Raoul\n");
		fprintf(pOutFile, "#include \"ExpressionMinimal.h\"\n");
		fprintf(pOutFile, "#include \"ExpressionFunc.h\"\n");
		fprintf(pOutFile, "#include \"ScratchStack.h\"\n");
		fprintf(pOutFile, "#include \"mathutil.h\"\n");
		fprintf(pOutFile, "#include \"timing.h\"\n");
		fprintf(pOutFile, "typedef struct Entity Entity;\n");
//	}

	if (bAtLeastOnePacketCommand)
	{
		fprintf(pOutFile, "//if this include fails, you have a remote command in an illegal project\n#include \"localtransactionmanager.h\"\n");
	}

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandHasArgOfType(pCommand, ARGTYPE_ENTITY) && !foundEnt)
		{
			fprintf(pOutFile, "\n//About to make prototype for entExternGetCommandEntity and Entity.\n//If entExterGetCommandEntity fails to link, it's because you're in a project that doesn't\n//support entities. Talk to Ben or Alex\n");
			fprintf(pOutFile, "typedef struct Entity Entity;\nextern Entity *entExternGetCommandEntity(CmdContext *context);\n");
			foundEnt = true;
		}

		if (CommandHasArgOfType(pCommand, ARGTYPE_TRANSACTIONCOMMAND) && !foundTran)
		{
			fprintf(pOutFile, "\n//About to try to include objTransactions.h because one of the AUTO_COMMANDS in\n//This file has a TransactionCommand arg.\n#include \"objTransactions.h\"\n");
			foundTran = true;
		}
	}


	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandNeedsNormalWrapper(pCommand) || CommandShouldGetRemotePacketWrapper(pCommand))
		{

			WriteRelevantIfsToFile(pOutFile, pCommand->pIfDefStack);
			WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, true);


			//create the extern function prototype
			fprintf(pOutFile, "extern %s %s(", (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE) ? "void" : GetReturnTypeName(pCommand), pCommand->functionName);

			WriteOutGenericArgListForCommand(pOutFile, pCommand, true, false, true);

			fprintf(pOutFile, "); // function defined in %s\n", pCommand->sourceFileName);


			if (CommandNeedsNormalWrapper(pCommand))
			{
				fprintf(pOutFile, "static void %s_MAGICCOMMANDWRAPPER(CMDARGS) { ", pCommand->functionName);

				if (pCommand->eReturnType != ARGTYPE_NONE && !(pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE))
				{
					fprintf(pOutFile, "\t%s retVal;\n", GetReturnTypeName(pCommand));
				}
			
				int iNumNormalArgs = GetNumNormalArgs(pCommand);

				if (iNumNormalArgs)
				{
					int iNumArgsFound = 0;

					fprintf(pOutFile, "\tREADARGS%d(", iNumNormalArgs);

					for (i=0; i < eaSize(&pCommand->ppArgs); i++)
					{
						MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

						if (pArg->argType < ARGTYPE_FIRST_SPECIAL)
						{
							iNumArgsFound++;
				
							fprintf(pOutFile, "%s, arg%d", GetFixedUpArgTypeNameForReadArgs(pCommand, i), i);

							if (iNumArgsFound < iNumNormalArgs)
							{
								fprintf(pOutFile, ", ");
							}
						}
					}

					fprintf(pOutFile, ");\n");
				}


				if (CommandHasArgOfType(pCommand, ARGTYPE_TRANSACTIONCOMMAND))
				{
					fprintf(pOutFile, "\tif (cmd_context->data) ((TransactionCommand *)cmd_context->data)->parseCommand = cmd;\n");
				}

				for (i=0; i < eaSize(&pCommand->ppArgs); i++)
				{
					MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

					switch(pArg->argType)
					{
					case ARGTYPE_ENTITY:
						fprintf(pOutFile, "\tCHECK_ENTITY_PTR_VALIDITY(%s);\n", pArg->argName);
						break;
					}
				}


				if (pCommand->eReturnType != ARGTYPE_NONE && !(pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE))
				{
					fprintf(pOutFile, "\tretVal = %s(", pCommand->functionName);	
				}
				else
				{
					fprintf(pOutFile, "\t%s(", pCommand->functionName);
				}


				for (i=0; i < eaSize(&pCommand->ppArgs); i++)
				{
					MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

					switch(pArg->argType)
					{
					case ARGTYPE_ENTITY:
						fprintf(pOutFile, "(%s)entExternGetCommandEntity(cmd_context)", pArg->argTypeName);
						break;
					case ARGTYPE_GENERICSERVERCMDDATA:
						fprintf(pOutFile, "cmd_context->data");
						break;
					case ARGTYPE_CMD:
						fprintf(pOutFile, "cmd");
						break;
					case ARGTYPE_CMDCONTEXT:
						fprintf(pOutFile, "cmd_context");
						break;
					case ARGTYPE_SLOWCOMMANDID:
						fprintf(pOutFile, "cmd_context->iSlowCommandID");
						break;
					case ARGTYPE_TRANSACTIONCOMMAND:
						fprintf(pOutFile, "(TransactionCommand *)cmd_context->data");
						break;
					case ARGTYPE_IGNORE:
						fprintf(pOutFile, "NULL");
						break;

					default:
						fprintf(pOutFile, "%sarg%d", IsDirectVersionOfPointerType(pArg->argType) ? "*" :
							((pArg->argFlags & ARGFLAG_OWNABLE) ? "&" : ""), i);
						break;
					}
					if (i < eaSize(&pCommand->ppArgs) - 1)
					{
						fprintf(pOutFile, ", ");
					}
				}

				fprintf(pOutFile, ");\n");

				for (i=0; i < eaSize(&pCommand->ppArgs); i++)
				{
					MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

					switch(pArg->argType)
					{
					case ARGTYPE_ENTITY:
						fprintf(pOutFile, "\tCHECK_ENTITY_PTR_VALIDITY(%s);\n", pArg->argName);
						break;
					}
				}

				if (!(pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE))
				{
					switch (pCommand->eReturnType)
					{
					case ARGTYPE_ENUM:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%d\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.intval = (S64)retVal;\n");
						break;
					case ARGTYPE_SINT:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%d\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.intval = (S64)retVal;\n");
						break;
					case ARGTYPE_UINT:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%u\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.intval = (S64)retVal;\n");
						break;
					case ARGTYPE_BOOL:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%u\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.intval = (S64)retVal;\n");
						break;
					
					case ARGTYPE_FLOAT:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%f\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.floatval = (F64)retVal;\n");
						break;
					case ARGTYPE_SINT64:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%\"FORM_LL\"d\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.intval = (S64)retVal;\n");
						break;
					case ARGTYPE_UINT64:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%\"FORM_LL\"u\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.intval = (S64)retVal;\n");
						break;
					case ARGTYPE_FLOAT64:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%lf\", retVal);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.floatval = (F64)retVal;\n");
						break;
					case ARGTYPE_STRING:
						fprintf(pOutFile, "\testrCopy2_dbg(cmd_context->output_msg, retVal, \"autogen\", 0);\n");
						fprintf(pOutFile, "\tcmd_context->return_val.ptr = retVal;\n");
						break;
					case ARGTYPE_STRUCT:
						fprintf(pOutFile, "\tif (retVal)\n\t{\n"
							"\t\tif (cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)\n"
							"\t\t\tParserWriteXMLEx(cmd_context->output_msg, parse_%s, retVal, TPXML_FORMAT_XMLRPC|TPXML_NO_PRETTY);\n"
							"\t\telse if (cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_JSONRPC)\n"
							"\t\t\tParserWriteJSON(cmd_context->output_msg, parse_%s, retVal, 0, 0, 0);\n"
							"\t\telse\n"
							"\t\t\tParserWriteTextEscaped(cmd_context->output_msg, parse_%s, retVal, %s, 0, 0);\n",
							pCommand->returnTypeName,pCommand->returnTypeName, pCommand->returnTypeName,
							(pCommand->iCommandFlags & COMMAND_FLAG_FORCEWRITECURFILEINSTRUCTS) ? "WRITETEXTFLAG_FORCEWRITECURRENTFILE" : "0");
						if (!(pCommand->iCommandFlags & COMMAND_FLAG_STATIC_RETURN_STRUCT))
						{
							fprintf(pOutFile, "\t\tStructDestroy(parse_%s, retVal);\n", pCommand->returnTypeName);
						}
						fprintf(pOutFile, "\t}\n\telse\n\t{\n\testrCopy2(cmd_context->output_msg, \"\");\n\t}\n");
						break;
					case ARGTYPE_VEC3_POINTER:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%f %%f %%f\", (*retVal)[0], (*retVal)[1], (*retVal)[2]);\n");
						break;
					case ARGTYPE_VEC4_POINTER:
						fprintf(pOutFile, "\testrPrintf_dbg(cmd_context->output_msg, \"autogen\", 0, \"%%f %%f %%f %%f\", (*retVal)[0], (*retVal)[1], (*retVal)[2], (*retVal)[3]);\n");
						break;
					}
				}
				
				fprintf(pOutFile, "}\n");
			}

			if (CommandShouldGetRemotePacketWrapper(pCommand))
			{
				fprintf(pOutFile, "static void MAGICCOMMANDWRAPPER_FROMPACKET_%s(Packet *_pPacket_Structparser__INTERNAL) { ",
					pCommand->commandName);

				WriteOutGenericPrototypeToCallFunctionFromPacket(pOutFile, pCommand, "_pPacket_Structparser__INTERNAL");

				fprintf(pOutFile, "}\n");
			}

			WriteRelevantEndIfsToFile(pOutFile, pCommand->pIfDefStack);
		}
	}

	
	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandGetsWrittenOutInCommandSets(pCommand))
		{
			WriteRelevantIfsToFile(pOutFile, pCommand->pIfDefStack);
			WriteCommandSetData(pOutFile, pCommand);
			WriteRelevantEndIfsToFile(pOutFile, pCommand->pIfDefStack);
		}
	}

	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

		WriteRelevantIfsToFile(pOutFile, pCommandVar->pIfDefStack);
		WriteCommandVarSetData(pOutFile, pCommandVar);
		WriteRelevantEndIfsToFile(pOutFile, pCommandVar->pIfDefStack);
	}

	fprintf(pOutFile, "\n");


	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

		WriteRelevantIfsToFile(pOutFile, pCommandVar->pIfDefStack);
		fprintf(pOutFile, "void AutoGen_RegisterAutoCmd_%s(void *pAddress, int iSize)\n", pCommandVar->varCommandName);
		fprintf(pOutFile, "{\n\tcmdVarSetData_%s.data[0].ptr = pAddress;\n\tcmdVarSetData_%s.data[0].data_size = iSize;\n}\n",
			pCommandVar->varCommandName, pCommandVar->varCommandName);

		char autoRunName[MAX_MAGICCOMMANDNAMELENGTH];

		sprintf(autoRunName, "AutoGen_AutoRun_RegisterAutoCmd_%s", pCommandVar->varCommandName);
		m_pParent->GetAutoRunManager()->AddAutoRunWithIfDefs(autoRunName, "autogen_magiccommands", pCommandVar->pIfDefStack);
		WriteRelevantEndIfsToFile(pOutFile, pCommandVar->pIfDefStack);
	}

	fprintf(pOutFile, "\n");

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		int i;

		WriteRelevantIfsToFile(pOutFile, pCommand->pIfDefStack);


		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			if (pCommand->commandSets[i][0])
			{
				fprintf(pOutFile, "extern CmdList %s;\n", pCommand->commandSets[i]);
			}
		}

		WriteRelevantEndIfsToFile(pOutFile, pCommand->pIfDefStack);

	}
				
	fprintf(pOutFile, "extern CmdList gRemoteCmdList;\nextern CmdList gSlowRemoteCmdList;\n");
	fprintf(pOutFile, "extern ParseTable parse_Entity[];");
	fprintf(pOutFile, "\n");



	char autoFuncName[256];
	sprintf(autoFuncName, "Add_Auto_Cmds_%s", m_ProjectName);
	fprintf(pOutFile, "void %s(void)\n{\n", autoFuncName);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		WriteRelevantIfsToFile(pOutFile, pCommand->pIfDefStack);


		if (!CommandGetsWrittenOutInCommandSets(pCommand))
		{


		}
		else if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
		{
			fprintf(pOutFile, "\tcmdAddSingleCmdToList(&gSlowRemoteCmdList, &cmdSetData_SlowRemoteCommand_%s[0]);\n",
				pCommand->functionName);
		}
		else if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
		{
			fprintf(pOutFile, "\tcmdAddSingleCmdToList(&gRemoteCmdList, &cmdSetData_RemoteCommand_%s[0]);\n",
				pCommand->functionName);
		}
		else if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
		{
		}
		else
		{
			bool bClientOrServerOnly = false;

			if (pCommand->serverSpecificAccessLevel_ServerName[0])
			{
				if (pCommand->commandAliases[0][0])
				{
					fprintf(pOutFile, "\tif (GetAppGlobalType() == %s)\n\t{\n",
						pCommand->serverSpecificAccessLevel_ServerName);
					fprintf(pOutFile, "\t\tint iAliasNum;\n\t\tfor (iAliasNum = 0; cmdSetData_%s[iAliasNum].name; iAliasNum++)\n\t\t{\n\t\t\tcmdSetData_%s[iAliasNum].access_level = %d;\n\t\t}\n\t}\n",
						pCommand->functionName, pCommand->functionName, pCommand->iServerSpecificAccessLevel);
				}
				else
				{
					fprintf(pOutFile, "\tif (GetAppGlobalType() == %s)\n\t{\n\t\tcmdSetData_%s[0].access_level = %d;\n\t}\n",
						pCommand->serverSpecificAccessLevel_ServerName, pCommand->functionName, pCommand->iServerSpecificAccessLevel);
				}
			}

			if (pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_ONLY)
			{
				bClientOrServerOnly = true;
				fprintf(pOutFile, "\tif (GetAppGlobalType() == GLOBALTYPE_CLIENT)\n\t{\n");
			}
			else if (pCommand->iCommandFlags & COMMAND_FLAG_SERVER_ONLY)
			{
				bClientOrServerOnly = true;
				fprintf(pOutFile, "\tif (IsGameServerBasedType())\n\t{\n");
			}

			if (pCommand->iCommandFlags & COMMAND_FLAG_PRIVATE)
			{
				if (pCommand->commandAliases[0][0])
				{
					fprintf(pOutFile, "\t%scmdAddCmdArrayToList(&gPrivateCmdList, cmdSetData_%s);\n",
						bClientOrServerOnly ? "\t" : "",
						pCommand->functionName);
				}
				else
				{
					fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&gPrivateCmdList, &cmdSetData_%s[0]);\n",
						bClientOrServerOnly ? "\t" : "",
						pCommand->functionName);
				}
			}
			else 
			{
				if (pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
				{
					if (pCommand->commandAliases[0][0])
					{
						fprintf(pOutFile, "\t%scmdAddCmdArrayToList(&gEarlyCmdList, cmdSetData_%s);\n",
							bClientOrServerOnly ? "\t" : "",
							pCommand->functionName);
					}
					else
					{
						fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&gEarlyCmdList, &cmdSetData_%s[0]);\n",
							bClientOrServerOnly ? "\t" : "",
							pCommand->functionName);
					}
				}
			
				if (pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL || !(pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE) && pCommand->commandSets[0][0] == 0)
				{
					if (pCommand->commandAliases[0][0])
					{
						fprintf(pOutFile, "\t%scmdAddCmdArrayToList(&gGlobalCmdList, cmdSetData_%s);\n",
							bClientOrServerOnly ? "\t" : "",
							pCommand->functionName);
					}
					else
					{
						fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&gGlobalCmdList, &cmdSetData_%s[0]);\n",
							bClientOrServerOnly ? "\t" : "",
							pCommand->functionName);
					}
				}
			}

			int i;

			for (i=0; i < MAX_COMMAND_SETS; i++)
			{
				if (pCommand->commandSets[i][0])
				{
					if (pCommand->commandAliases[0][0])
					{
						if (pCommand->commandSets[0][0] && !(pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL))
						{
							fprintf(pOutFile, "\t%scmdAddCmdArrayToList(&%s, cmdSetData_%s_%s);\n",
								bClientOrServerOnly ? "\t" : "",
								pCommand->commandSets[i],
								pCommand->commandSets[0],pCommand->functionName);
						}
						else
						{						
							fprintf(pOutFile, "\t%scmdAddCmdArrayToList(&%s, cmdSetData_%s);\n",
								bClientOrServerOnly ? "\t" : "",
								pCommand->commandSets[i],
								pCommand->functionName);
						}
					}
					else
					{
						if (pCommand->commandSets[0][0] && !(pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL))
						{
							fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&%s, &cmdSetData_%s_%s[0]);\n",
								bClientOrServerOnly ? "\t" : "",
								pCommand->commandSets[i],
								pCommand->commandSets[0],pCommand->functionName);
						}
						else
						{						
							fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&%s, &cmdSetData_%s[0]);\n",
								bClientOrServerOnly ? "\t" : "",
								pCommand->commandSets[i],
								pCommand->functionName);
						}
					}
				}
			}

			if (bClientOrServerOnly)
			{
				fprintf(pOutFile, "\t}\n");
			}
		}

		WriteRelevantEndIfsToFile(pOutFile, pCommand->pIfDefStack);

	}


	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

		bool bClientOrServerOnly = false;

		WriteRelevantIfsToFile(pOutFile, pCommandVar->pIfDefStack);

		if (pCommandVar->serverSpecificAccessLevel_ServerName[0])
		{
			fprintf(pOutFile, "\tif (GetAppGlobalType() == %s)\n\t{\n\t\tcmdVarSetData_%s.access_level = %d;\n\t}\n",
				pCommandVar->serverSpecificAccessLevel_ServerName, pCommandVar->varCommandName, pCommandVar->iServerSpecificAccessLevel);
		}

		if (pCommandVar->iCommandFlags & COMMAND_FLAG_CLIENT_ONLY)
		{
			bClientOrServerOnly = true;
			fprintf(pOutFile, "\tif (GetAppGlobalType() == GLOBALTYPE_CLIENT)\n\t{\n");
		}
		else if (pCommandVar->iCommandFlags & COMMAND_FLAG_SERVER_ONLY)
		{
			bClientOrServerOnly = true;
			fprintf(pOutFile, "\tif (GetAppGlobalType() == GLOBALTYPE_GAMESERVER)\n\t{\n");
		}

		if (pCommandVar->iCommandFlags & COMMAND_FLAG_PRIVATE)
		{
	
			fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&gPrivateCmdList, &cmdVarSetData_%s);\n",
				bClientOrServerOnly ? "\t" : "",
				pCommandVar->varCommandName);
			
		}
		else if (pCommandVar->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
		{
	
			fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&gEarlyCmdList, &cmdVarSetData_%s);\n",
				bClientOrServerOnly ? "\t" : "",
				pCommandVar->varCommandName);
			
		}
		else if (pCommandVar->commandSets[0][0] == 0 || pCommandVar->iCommandFlags & COMMAND_FLAG_GLOBAL)
		{
			fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&gGlobalCmdList, &cmdVarSetData_%s);\n",
				bClientOrServerOnly ? "\t" : "",
				pCommandVar->varCommandName);
		}

		int i;

		for (i=0 ; i < pCommandVar->autoSettingGlobalTypes.iNumGlobalTypes; i++)
		{
			fprintf(pOutFile, "\tcmdAddAutoSettingGlobalType(&cmdVarSetData_%s, GLOBALTYPE_%s);\n", 
				pCommandVar->varCommandName, pCommandVar->autoSettingGlobalTypes.GlobalTypeNames[i]);
		}

		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			if (pCommandVar->commandSets[i][0])
			{
				if (pCommandVar->commandSets[0][0] && !(pCommandVar->iCommandFlags & COMMAND_FLAG_GLOBAL))
				{
					fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&%s, &cmdVarSetData_%s_%s);\n",
						bClientOrServerOnly ? "\t" : "",
						pCommandVar->commandSets[i],
						pCommandVar->commandSets[0],pCommandVar->varCommandName);
				}
				else
				{				
					fprintf(pOutFile, "\t%scmdAddSingleCmdToList(&%s, &cmdVarSetData_%s);\n",
						bClientOrServerOnly ? "\t" : "",
						pCommandVar->commandSets[i],
						pCommandVar->varCommandName);
				}
				
			}
		}

		if (bClientOrServerOnly)
		{
			fprintf(pOutFile, "\t}\n");
		}
	
		WriteRelevantEndIfsToFile(pOutFile, pCommandVar->pIfDefStack);


	}




	fprintf(pOutFile, "};\n");

	m_pParent->GetAutoRunManager()->AddAutoRun(autoFuncName, "autogen_magiccommands");



	sprintf(autoFuncName, "Add_Auto_Cmds_%s_Later", m_ProjectName);
	fprintf(pOutFile, "void %s(void)\n{\n", autoFuncName);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandShouldGetRemotePacketWrapper(pCommand))
		{	
			WriteRelevantIfsToFile(pOutFile, pCommand->pIfDefStack);
			fprintf(pOutFile, "\tRegisterSimplePacketRemoteCommandFunc(\"%s\", MAGICCOMMANDWRAPPER_FROMPACKET_%s);\n",
				pCommand->commandName, pCommand->commandName);
			WriteRelevantEndIfsToFile(pOutFile, pCommand->pIfDefStack);
		}
	}


	fprintf(pOutFile, "};\n");

	m_pParent->GetAutoRunManager()->AddAutoRunWithBody(autoFuncName, "autogen_magiccommands", NULL, NULL, AUTORUN_ORDER_EARLY, NULL);







	WriteOutExpressionListStuff(pOutFile, pExprCodeHeaderFile, pExecutableOnlyFile);


	fprintf(pOutFile, "\n#ifdef THIS_SYMBOL_IS_NOT_DEFINED\nPARSABLE\n");

	fprintf(pOutFile, "%d\n", eaSize(&m_ppMagicCommands));

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];
		
		Token commentToken;

		AddCStyleEscaping(commentToken.sVal, pCommand->comment, TOKENIZER_MAX_STRING_LENGTH);

		fprintf(pOutFile, " %d %d ", pCommand->iCommandFlags, pCommand->iCommandFlags2);

		fprintf(pOutFile, "\"%s\" \"%s\" ", 
			pCommand->functionName, pCommand->commandName);	
		
		for (i=0; i < MAX_COMMAND_ALIASES; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommand->commandAliases[i]);
		}

		fprintf(pOutFile, "%d \"%s\" %d \"%s\" %d \"%s\" ",
			pCommand->iAccessLevel, pCommand->serverSpecificAccessLevel_ServerName, pCommand->iServerSpecificAccessLevel,
			pCommand->sourceFileName, pCommand->iLineNum, commentToken.sVal);

		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommand->commandSets[i]);
		}

		for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommand->commandCategories[i]);
		}

		for (i=0; i < MAX_ERROR_FUNCTIONS_PER_COMMAND; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommand->commandsWhichThisIsTheErrorFunctionFor[i]);
		}


		fprintf(pOutFile, " %d \"%s\" \"%s\" %d \"%s\" %d ",
			pCommand->eReturnType, pCommand->returnTypeName, pCommand->returnStaticCheckParamType, pCommand->iReturnStaticCheckCategory, pCommand->queueName, pCommand->iNumDefines);

		
		for (i=0; i < pCommand->iNumDefines; i++)
		{
			fprintf(pOutFile, "\"%s\" ", pCommand->defines[i]);
		}

		fprintf(pOutFile, "%d ", eaSize(&pCommand->ppArgs));

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

			fprintf(pOutFile, "%d \"%s\" \"%s\" \"%s\" \"%s\" %d %d %d %d %d \"%s\" ", 
				pArg->argType,
				pArg->argTypeName, 
				pArg->argName,
				pArg->argNameListTypeName, 
				pArg->argNameListDataPointerName, 
				pArg->argNameListDataPointerWasString, 
				pArg->argFlags, 
				pArg->bHasDefaultInt, pArg->iDefaultInt,
				pArg->bHasDefaultString, pArg->defaultString);
		}

		fprintf(pOutFile, "%d ", pCommand->iNumExpressionTags);
		
		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommand->expressionTag[i]);
		}

		fprintf(pOutFile, " \"%s\" ", pCommand->expressionStaticCheckFunc);

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			fprintf(pOutFile, "\"%s\" ", pCommand->ppArgs[i]->expressionStaticCheckParamType);
		}

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			fprintf(pOutFile, "%d ", pCommand->ppArgs[i]->iExpressionArgFlags);
		}

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			fprintf(pOutFile, "%d ", pCommand->ppArgs[i]->iExpressionArgCategory);
		}

		fprintf(pOutFile, "%d %d ", pCommand->iExpressionCost, pCommand->bIsExprCodeExample);

		WriteIfDefStackToFile(pOutFile, pCommand->pIfDefStack);

		WriteEarrayOfIdentifiersToFile(pOutFile, &pCommand->ppProducts);

		fprintf(pOutFile, "\n");
	}

	fprintf(pOutFile, "%d\n", eaSize(&m_ppMagicCommandVars));

	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

		Token commentToken;

		fprintf(pOutFile, " %d %d ", pCommandVar->iCommandFlags, pCommandVar->iCommandFlags2);


		AddCStyleEscaping(commentToken.sVal, pCommandVar->comment, TOKENIZER_MAX_STRING_LENGTH);

		fprintf(pOutFile, "\"%s\" \"%s\" %d %d %d \"%s\" %d ",
			pCommandVar->varCommandName, pCommandVar->sourceFileName, pCommandVar->iLineNum,
			pCommandVar->eVarType, pCommandVar->iAccessLevel, pCommandVar->serverSpecificAccessLevel_ServerName, pCommandVar->iServerSpecificAccessLevel);
		
		for (i=0; i < MAX_COMMAND_SETS; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommandVar->commandSets[i]);
		}

		for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
		{
			fprintf(pOutFile, " \"%s\" ", pCommandVar->commandCategories[i]);
		}

		fprintf(pOutFile, " \"%s\" \"%s\" %d ", commentToken.sVal,
			pCommandVar->callbackFunc,pCommandVar->iMaxValue);

		WriteIfDefStackToFile(pOutFile, pCommandVar->pIfDefStack);
		WriteEarrayOfIdentifiersToFile(pOutFile, &pCommandVar->ppProducts);

		WriteGlobalTypes_Parsable(pOutFile, &pCommandVar->autoSettingGlobalTypes);


		fprintf(pOutFile, "\n");
	}


	fprintf(pOutFile, "#endif\n");

	fclose(pOutFile);
	fclose(pExprCodeHeaderFile);
	if (pExecutableOnlyFile)
	{
		fclose(pExecutableOnlyFile);
	}

	m_pParent->CreateWikiDirectory();

	char systemString[1024];
	
	sprintf(systemString, "erase \"%s\\wiki\\*_autocommands.wiki\" 2>nul", m_pParent->GetProjectPath(), m_pParent->GetShortProjectName());
	system(systemString);

	sprintf(systemString, "erase \"%s\\wiki\\*_autoexprcommands.wiki\" 2>nul", m_pParent->GetProjectPath(), m_pParent->GetShortProjectName());
	system(systemString);

	WriteOutWikiFileForCategory("", false);
	WriteOutWikiFileForCategory("all", false);
	WriteOutWikiFileForCategory("all", true);
	WriteOutWikiFileForCategory("hidden", false);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandShouldBeWrittenToWiki(pCommand))
		{
			for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
			{
				if (!CommandCategoryWritten(pCommand->commandCategories[i]))
				{
					WriteOutWikiFileForCategory(pCommand->commandCategories[i], false);
					WriteOutWikiFileForCategory(pCommand->commandCategories[i], true);
				}
			}
		}
	}

	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];
	
		for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
		{
			if (!CommandCategoryWritten(pCommandVar->commandCategories[i]))
			{
				WriteOutWikiFileForCategory(pCommandVar->commandCategories[i], false);
			}
		}
	}


	WriteOutFilesForTestClient();
	WriteOutQueuedCommands();

	if (AtLeastOneCommandHasFlag(COMMAND_FLAG_SERVER_WRAPPER))
	{
		WriteOutServerWrappers();
	}

	if (AtLeastOneCommandHasFlag(COMMAND_FLAG_CLIENT_WRAPPER))
	{
		WriteOutClientWrappers();
	}

	if (AtLeastOneCommandHasFlag(COMMAND_FLAG_GENERICCLIENT_WRAPPER))
	{
		WriteOutGenericClientWrappers();
	}

	if (AtLeastOneCommandHasFlag(COMMAND_FLAG_GENERICSERVER_WRAPPER))
	{
		WriteOutGenericServerWrappers();
	}

	if (CurrentProjectIsTestClient())
	{
		WriteOutClientToTestClientWrappers();
	}

	return true;
}


void MagicCommandManager::WriteExprCodeIncludesForLibraries(FILE *pOutFile, char *pPrefix)
{

	for (int i = 0; i < m_pParent->GetNumLibraries(); i++)
	{
		char libNameUpcase[MAX_PATH];
		char libPath[MAX_PATH];

		strcpy(libPath, m_pParent->GetNthLibraryRelativePath(i));
		TruncateStringAtLastOccurrence(libPath, '\\');
			
		strcpy(libNameUpcase, m_pParent->GetNthLibraryName(i));
		MakeStringUpcase(libNameUpcase);

		fprintf(pOutFile, "#define %s%s 1\n#include \"%s\\autogen\\%s_ExprCodes_autogen.h\"\n#undef %s%s\n",
			pPrefix, libNameUpcase, libPath, m_pParent->GetNthLibraryName(i), pPrefix, libNameUpcase);
	}	
}


void MagicCommandManager::WriteOutExpressionListStuff(FILE *pOutFile, FILE *pExprCodeHeaderFile, FILE *pExecutableOnlyFile)
{
	bool bFoundAtLeastOneExpressionFunc = false;
	int iCommandNum;
	
	char exprCodes[MAX_EXPR_CODES][2048];
	unsigned int exprCodeHashes[MAX_EXPR_CODES];
	int iNumExprCodes = 0;

	fprintf(pOutFile, "#pragma warning (disable:4054) // casting function pointers as data pointers\n");

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER)
		{
			bool bHasErrString = false;
			bool bErrEString = false;
			char *pExprCode;
			unsigned int exprCodeHash;
			bool bFoundExprCode = false;
			int i;


			if (pCommand->iNumExpressionTags)
				bFoundAtLeastOneExpressionFunc = true;
			
			pExprCode = GetFullExprCode(pCommand);
			exprCodeHash = hashString(pExprCode);

			for (i=0; i < iNumExprCodes; i++)
			{
				if (exprCodeHash == exprCodeHashes[i] && strcmp(pExprCode, exprCodes[i]) == 0)
				{
					bFoundExprCode = true;
					break;
				}
			}

			if (!bFoundExprCode)
			{
				CommandAssert(pCommand, iNumExprCodes < MAX_EXPR_CODES, "Too many unique expression codes");

				exprCodeHashes[iNumExprCodes] = exprCodeHash;
				strcpy(exprCodes[iNumExprCodes++], pExprCode);

				pCommand->bIsExprCodeExample = true;
			}

			// first, predeclare any enums or structs (and ParseTable for struct)
			int iArgNum;
			for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
			{
				MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

				if (pArg->argType == ARGTYPE_STRUCT)
				{
					fprintf(pOutFile, "typedef struct %s %s;\nextern ParseTable parse_%s[];\n", pArg->argTypeName, pArg->argTypeName, pArg->argTypeName);
				}
				else if (pArg->argType == ARGTYPE_ENUM)
				{
					fprintf(pOutFile, "typedef enum %s %s;\n", pArg->argTypeName, pArg->argTypeName);
				}
			}

			// second, write out the function prototype
			if (pCommand->eReturnType == ARGTYPE_STRUCT)
			{
				fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->returnTypeName);
				fprintf(pOutFile, "typedef struct %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
				fprintf(pOutFile, "extern %s *%s(", pCommand->returnTypeName, pCommand->functionName);
			}
			else if (pCommand->eReturnType == ARGTYPE_ENUM)
			{
				fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->returnTypeName);
				fprintf(pOutFile, "typedef enum %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
				fprintf(pOutFile, "extern %s %s(", pCommand->returnTypeName, pCommand->functionName);
			}
			else
			{
				fprintf(pOutFile, "extern %s %s(", 	pCommand->returnTypeName, pCommand->functionName);
			}

			// ... including function args
			for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
			{
				MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

				if (pArg->argType == ARGTYPE_STRUCT)
					fprintf(pOutFile, "%s* ", pArg->argTypeName);
				else
					fprintf(pOutFile, "%s ", pArg->argTypeName);

				fprintf(pOutFile, "%s%s", pArg->argName,
					iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			}

			// .... end the function prototype
			fprintf(pOutFile, ");\n");
		}
	}

	if (bFoundAtLeastOneExpressionFunc)
	{
		fprintf(pOutFile, "ExprFuncDesc AutoCmd_%s_ExprList[] = \n{\n", m_ProjectName);

		for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
		{
			MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

			char tempString1[TOKENIZER_MAX_STRING_LENGTH];
			char tempString2[TOKENIZER_MAX_STRING_LENGTH];
			strcpy(tempString1, pCommand->comment);
		

			AddCStyleEscaping(tempString2, tempString1, TOKENIZER_MAX_STRING_LENGTH);


			if (pCommand->iNumExpressionTags)
			{
				int iAliasNum;

				//aliasnum -1 means main name...
				for (iAliasNum = -1; iAliasNum < 0 || iAliasNum < MAX_COMMAND_ALIASES && pCommand->commandAliases[iAliasNum][0]; iAliasNum++)
				{

					fprintf(pOutFile, "\t{ 0, 0, (void**)%s, NULL, \"%s\", \"%s\", \"%s\", %d, {\n",
						pCommand->functionName, iAliasNum == -1 ? pCommand->commandName : pCommand->commandAliases[iAliasNum],
						tempString2,
						GetFileNameWithoutDirectoriesOrSlashes(pCommand->sourceFileName), pCommand->iLineNum);
						
					char retTypeTPIString[256] = "NULL";
					char *pRetTypeString = "MULTI_NONE";
					char retTypeStaticCheckType[MAX_MAGICCOMMANDNAMELENGTH] = "NULL";
					char retTypeStaticCheckCategory[256] = "ExprStaticCheckCat_None";
					char argTypeStaticCheckTypes[MAX_MAGICCOMMAND_ARGS * 64];

					argTypeStaticCheckTypes[0] = '\0';

					switch (pCommand->eReturnType)
					{
					case ARGTYPE_SINT:
					case ARGTYPE_UINT:
					case ARGTYPE_SINT64:
					case ARGTYPE_UINT64:
					case ARGTYPE_BOOL:
						pRetTypeString = "MULTI_INT";
						break;
					case ARGTYPE_FLOAT:
					case ARGTYPE_FLOAT64:
						pRetTypeString = "MULTI_FLOAT";
						break;
					case ARGTYPE_STRING:
						pRetTypeString = "MULTI_STRING";
						break;
					case ARGTYPE_EXPR_CMULTIVAL:
						pRetTypeString = "MULTI_CMULTI";
					case ARGTYPE_STRUCT:
						CommandAssert(pCommand,
							(pCommand->iCommandFlags & COMMAND_FLAG_RETVAL_ALLOW_NULL) ||
							(pCommand->iCommandFlags & COMMAND_FLAG_RETVAL_DONT_ALLOW_NULL),
							"Pointer return value must be annotated with SA_RET_OP* or SA_ORET_OP* (to specify you "
							"might return NULL) or SA_RET_NN* or SA_ORET_NN* (to guarantee you return a non-NULL pointer)");
						pRetTypeString = "MULTI_NP_POINTER";
						sprintf(retTypeTPIString, "parse_%s", pCommand->returnTypeName);
						break;
					}

					if (pCommand->returnStaticCheckParamType[0])
					{
						sprintf(retTypeStaticCheckType, "\"%s\"", pCommand->returnStaticCheckParamType);
					}

					switch (pCommand->iReturnStaticCheckCategory)
					{
					case EXPARGCAT_NONE:
						strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_None");
						break;
					case EXPARGCAT_RESOURCE:
						strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Resource");
						break;
					case EXPARGCAT_REFERENCE:
						strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Reference");
						break;
					case EXPARGCAT_ENUM:
						strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Enum");
						break;
					case EXPARGCAT_CUSTOM:
						strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Custom");
						break;

					}

					int iArgNum;
					bool bHasSelfPtr = false;
					bool bHasPartition = false;

					for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
					{
						MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

						int validExprFuncArg = false;
						int copyToReturn = false;
						switch (pArg->argType)
						{
						case ARGTYPE_SINT:
						case ARGTYPE_UINT:
						case ARGTYPE_BOOL:
						case ARGTYPE_SINT64:
						case ARGTYPE_UINT64:
							fprintf(pOutFile, "\t\t{ MULTI_INT, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_ENUM:
							fprintf(pOutFile, "\t\t{ MULTI_INT, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_FLOAT:
						case ARGTYPE_FLOAT64:
							fprintf(pOutFile, "\t\t{ MULTI_FLOAT, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_STRING:
							fprintf(pOutFile, "\t\t{ MULTI_STRING, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_EXPR_CMULTIVAL:
							fprintf(pOutFile, "\t\t{ MULTI_CMULTI, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_ENTITY:
							if(pArg->iExpressionArgFlags & COMMAND_EXPR_FLAG_SELF_PTR)
							{
								bHasSelfPtr = true;
								break;
							}
							// fall through and treat as a struct
						case ARGTYPE_STRUCT:
							fprintf(pOutFile, "\t\t{ MULTI_NP_POINTER, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_EXPR_SUBEXPR_IN:
							fprintf(pOutFile, "\t\t{ MULTIOP_NP_STACKPTR, \"%s\", NULL },", pArg->argName);
							fprintf(pOutFile, "\t\t{ MULTI_INT, \"subExprSize\", NULL },");
							// avoid rest of processing
							//validExprFuncArg = true;
							break;
						case ARGTYPE_EXPR_LOC_MAT4_IN:
							fprintf(pOutFile, "\t\t{ MULTIOP_LOC_MAT4, ");
							validExprFuncArg = true;
							break;
						case ARGTYPE_EXPR_ENTARRAY_IN:
							fprintf(pOutFile, "\t\t{ MULTI_NP_ENTITYARRAY, ");
							validExprFuncArg = true;
							break;

						case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
							fprintf(pOutFile, "\t\t{ MULTI_NP_ENTITYARRAY, ");
							validExprFuncArg = true;
							pRetTypeString = "MULTI_NP_ENTITYARRAY";
							break;
						
						case ARGTYPE_EXPR_ENTARRAY_OUT:
							copyToReturn = true;
							pRetTypeString = "MULTI_NP_ENTITYARRAY";
							break;
						case ARGTYPE_EXPR_INT_OUT:
							copyToReturn = true;
							pRetTypeString = "MULTI_INT";
							break;
						case ARGTYPE_EXPR_FLOAT_OUT:
							copyToReturn = true;
							pRetTypeString = "MULTI_FLOAT";
							break;
						case ARGTYPE_EXPR_STRING_OUT:
							copyToReturn = true;
							pRetTypeString = "MULTI_STRING";
							break;
						case ARGTYPE_EXPR_LOC_MAT4_OUT:
							copyToReturn = true;
							pRetTypeString = "MULTIOP_LOC_MAT4";
							break;
						case ARGTYPE_EXPR_VEC4_OUT:
							copyToReturn = true;
							pRetTypeString = "MULTI_VEC4";
							break;
						case ARGTYPE_EXPR_PARTITION:
							bHasPartition = true;
							break;
						case ARGTYPE_EXPR_ERRSTRING:
						case ARGTYPE_EXPR_ERRSTRING_STATIC:
						case ARGTYPE_EXPR_EXPRCONTEXT:
							break;
						default:
							CommandAssert(pCommand, 0, "Unsupported function argument type");
						}

						if(validExprFuncArg)
						{
							fprintf(pOutFile, "\"%s\", ", pArg->argName);

							if(pArg->expressionStaticCheckParamType[0])
								fprintf(pOutFile, "\"%s\", ", pArg->expressionStaticCheckParamType);
							else
								fprintf(pOutFile, "NULL, ");

							if(pArg->argType == ARGTYPE_STRUCT)
								fprintf(pOutFile, "parse_%s, ", pArg->argTypeName);
							else if(pArg->argType == ARGTYPE_ENTITY)
								fprintf(pOutFile, "parse_Entity, ");
							else
								fprintf(pOutFile, "NULL, ");

							if(pArg->iExpressionArgFlags & COMMAND_EXPR_FLAG_SA_PRE_OP)
								fprintf(pOutFile, "true, ");
							else
								fprintf(pOutFile, "false, ");
							
							switch (pArg->iExpressionArgCategory)
							{
							case EXPARGCAT_NONE:
								fprintf(pOutFile, "ExprStaticCheckCat_None ");
								break;
							case EXPARGCAT_RESOURCE:
								fprintf(pOutFile, "ExprStaticCheckCat_Resource ");
								break;
							case EXPARGCAT_REFERENCE:
								fprintf(pOutFile, "ExprStaticCheckCat_Reference ");
								break;
							case EXPARGCAT_ENUM:
								fprintf(pOutFile, "ExprStaticCheckCat_Enum ");
								break;
							case EXPARGCAT_CUSTOM:
								fprintf(pOutFile, "ExprStaticCheckCat_Custom ");
								break;

							}

							fprintf(pOutFile, "}%s ", iArgNum == MAX_MAGICCOMMAND_ARGS - 1 ? "" : ",");
						}
						else if (copyToReturn)
						{
							if (pArg->expressionStaticCheckParamType[0])
							{
								sprintf(retTypeStaticCheckType, "\"%s\"", pArg->expressionStaticCheckParamType);
							}

							switch (pArg->iExpressionArgCategory)
							{
							case EXPARGCAT_NONE:
								strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_None");
								break;
							case EXPARGCAT_RESOURCE:
								strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Resource");
								break;
							case EXPARGCAT_REFERENCE:
								strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Reference");
								break;
							case EXPARGCAT_ENUM:
								strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Enum");
								break;
							case EXPARGCAT_CUSTOM:
								strcpy(retTypeStaticCheckCategory, "ExprStaticCheckCat_Custom");
								break;

							}
						}
					}

					if (eaSize(&pCommand->ppArgs) == MAX_MAGICCOMMAND_ARGS)
					{
						fprintf(pOutFile, "},\n");
					}
					else
					{
						fprintf(pOutFile, "\t\t{ MULTI_NONE, NULL, NULL, NULL, false, ExprStaticCheckCat_None}},\n");
					}
					
					if (strcmp(pRetTypeString,"MULTI_NONE") != 0)
					{
						fprintf(pOutFile, "\t\t {%s, NULL, %s, %s, %s, %s},\n\t\t{ ", pRetTypeString, retTypeStaticCheckType, retTypeTPIString, (pCommand->iCommandFlags & COMMAND_FLAG_RETVAL_ALLOW_NULL) ? "true" : "false", retTypeStaticCheckCategory);
					}
					else
					{
						fprintf(pOutFile, "\t\t {%s},\n\t\t{ ", pRetTypeString);
					}

					int iTagNum;

					for (iTagNum=0; iTagNum < pCommand->iNumExpressionTags; iTagNum++)
					{
						fprintf(pOutFile, "{\"%s\"}, ", pCommand->expressionTag[iTagNum]);
					}

					fprintf(pOutFile, "{NULL} }, ");

					fprintf(pOutFile, " %d, \"%s\", " , pCommand->iExpressionCost, GetFullExprCode(pCommand));

					if (pCommand->expressionStaticCheckFunc[0])
					{
						MAGIC_COMMAND_STRUCT *pStaticCheckCommand = FindCommandByFuncName(pCommand->expressionStaticCheckFunc);


						CommandAssertf(pCommand, pStaticCheckCommand != NULL, "Unknown static check command func %s", pCommand->expressionStaticCheckFunc);
						CommandAssertf(pCommand, strcmp(GetFullExprCode(pStaticCheckCommand), GetFullExprCode(pCommand)) == 0, "Expr func %s has static check func %s, which has different prototype, which is bad juju", 
							pCommand->functionName, pStaticCheckCommand->functionName);

						
						fprintf(pOutFile, "(void*)%s", pCommand->expressionStaticCheckFunc);
					}
					else
					{
						fprintf(pOutFile, "NULL");
					}

					if(bHasSelfPtr || bHasPartition)
					{
						fprintf(pOutFile, ", ");
						if(bHasSelfPtr)
						{
							fprintf(pOutFile, "EXPR_FUNC_RQ_SELFPTR %s", bHasPartition ? " | " : "");
						}

						if(bHasPartition)
						{
							fprintf(pOutFile, "EXPR_FUNC_RQ_PARTITION");
						}
					}

					fprintf(pOutFile, "},\n");
				}
			}
		}

		fprintf(pOutFile, "};\n");

		char autoRunName[256];

		sprintf(autoRunName, "AutoCmds_%s_RegisterExprLists", m_ProjectName);
		fprintf(pOutFile, "void %s(void)\n{\n", autoRunName);
		
		fprintf(pOutFile, "\texprRegisterFunctionTable(AutoCmd_%s_ExprList, sizeof(AutoCmd_%s_ExprList)/sizeof(AutoCmd_%s_ExprList[0]), false);\n",
			m_ProjectName, m_ProjectName, m_ProjectName);

		fprintf(pOutFile, "}\n");

		m_pParent->GetAutoRunManager()->AddAutoRun(autoRunName, "autogen_magiccommands");
	}

	char allCapsProjectName[MAX_PATH];

	strcpy(allCapsProjectName, m_pParent->GetShortProjectName());
	MakeStringUpcase(allCapsProjectName);

	fprintf(pExprCodeHeaderFile, "//autogenerated" "nocheckin  this file is autogenerated by structparser\n#pragma warning(default:4242)\n"
        "#ifdef WANT_EXPRCODE_ENUMS_%s\n", allCapsProjectName);

	if (m_pParent->GetNumLibraries())
	{
		WriteExprCodeIncludesForLibraries(pExprCodeHeaderFile, "WANT_EXPRCODE_ENUMS_");
	}



	for (int i=0; i < iNumExprCodes; i++)
	{
		fprintf(pExprCodeHeaderFile, "#ifndef DEFINED_EXPRCODE_ENUM_%s\n#define DEFINED_EXPRCODE_ENUM_%s\n\tEXPRCODE_ENUM_%s,\n#endif\n",
			exprCodes[i], exprCodes[i], exprCodes[i]);
	}
	fprintf(pExprCodeHeaderFile, "#endif //WANT_EXPRCODE_ENUMS_%s\n", allCapsProjectName);

	fprintf(pExprCodeHeaderFile, "#ifdef WANT_EXPRCODE_STATICDEFINE_%s\n", allCapsProjectName);

	if (m_pParent->GetNumLibraries())
	{
		WriteExprCodeIncludesForLibraries(pExprCodeHeaderFile, "WANT_EXPRCODE_STATICDEFINE_");
	}

	for (int i=0; i < iNumExprCodes; i++)
	{
		fprintf(pExprCodeHeaderFile, "#ifndef DEFINED_EXPRCODE_STATICDEFINE_%s\n#define DEFINED_EXPRCODE_STATICDEFINE_%s\n\t{ \"%s\", EXPRCODE_ENUM_%s },\n#endif\n",
			exprCodes[i], exprCodes[i], exprCodes[i], exprCodes[i]);
	}
	fprintf(pExprCodeHeaderFile, "#endif //WANT_EXPRCODE_STATICDEFINE_%s\n", allCapsProjectName);



	fprintf(pExprCodeHeaderFile, "#ifdef WANT_EXPRCODE_PROTOTYPES_%s\n", allCapsProjectName);
	if (m_pParent->GetNumLibraries())
	{
		WriteExprCodeIncludesForLibraries(pExprCodeHeaderFile, "WANT_EXPRCODE_PROTOTYPES_");
	}

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->bIsExprCodeExample)
		{
			char *pExprCode = GetFullExprCode(pCommand);
			char *pReturnTypeString;

			switch (pCommand->eReturnType)
			{
				case ARGTYPE_UINT:
					pReturnTypeString = "U32";
					break;
				case ARGTYPE_STRUCT:
					pReturnTypeString = "void*";
					break;
				case ARGTYPE_ENUM:
					pReturnTypeString = "int";
					break;
				default:
					pReturnTypeString = pCommand->returnTypeName;
			}
            fprintf(pExprCodeHeaderFile, "#ifndef DEFINED_EXPRCODE_PROTOTYPE_%s\n#define DEFINED_EXPRCODE_PROTOTYPE_%s\n"
                "typedef %s EXPRCODE_PROTOTYPE_%s(", pExprCode, pExprCode, pReturnTypeString, pExprCode);

			int iArgNum;

			if (eaSize(&pCommand->ppArgs) == 0)
			{
				fprintf(pExprCodeHeaderFile, "void");
			}
			else for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
			{
				MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

				if(pArg->argType == ARGTYPE_STRUCT)
				{
					fprintf(pExprCodeHeaderFile, "void* "); // yes, we want void* here - reduces number of Types we need EXPRCODE_PROTOTYPE's for
				}
				else if(pArg->argType == ARGTYPE_EXPR_CMULTIVAL)
				{
					fprintf(pExprCodeHeaderFile, "CMultiVal* ");
				}
				else if (pArg->argType == ARGTYPE_UINT)
				{
					fprintf(pExprCodeHeaderFile, "U32 ");
				}
				else if (pArg->argType == ARGTYPE_ENUM)
				{
					fprintf(pExprCodeHeaderFile, "int "); // yes, we want int here - reduces number of Types we need EXPRCODE_PROTOTYPE's for
				}
				else
				{
					fprintf(pExprCodeHeaderFile, "%s ", pArg->argTypeName);
				}

				fprintf(pExprCodeHeaderFile, "arg%d%s", iArgNum,
					iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			}

            fprintf(pExprCodeHeaderFile, "); //for example, %s\n#endif\n", pCommand->functionName);
		}
	}

	fprintf(pExprCodeHeaderFile, "#endif // WANT_EXPRCODE_PROTOTYPES_%s\n", allCapsProjectName);

	fprintf(pExprCodeHeaderFile, "#ifdef WANT_EXPRCODE_SWITCH_CASES_%s\n", allCapsProjectName);
	if (m_pParent->GetNumLibraries())
	{
		WriteExprCodeIncludesForLibraries(pExprCodeHeaderFile, "WANT_EXPRCODE_SWITCH_CASES_");
	}

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->bIsExprCodeExample)
		{
			char *pExprCode = GetFullExprCode(pCommand);
			fprintf(pExprCodeHeaderFile, "#ifndef DEFINE_EXPR_SWITCH_CASE_%s\n#define DEFINE_EXPR_SWITCH_CASE_%s\nxcase EXPRCODE_ENUM_%s:\n{\n",
				pExprCode, pExprCode, pExprCode);


			WriteOutExprCodeCase(pExprCodeHeaderFile, pCommand);





			fprintf(pExprCodeHeaderFile, "};\n#endif\n");
		}
	}


	fprintf(pExprCodeHeaderFile, "#endif // WANT_EXPRCODE_SWITCH_CASES_%s\n", allCapsProjectName);

	if (m_pParent->ProjectIsExecutable())
	{
		char defineName[1024];

		fprintf(pExecutableOnlyFile, "//Because this is an executable project, here is where all the crazy web of include\n//stuff for exprCodes goes\n");
		fprintf(pExecutableOnlyFile, "#include \"globaltypes.h\"\n#include \"expressionfunc.h\"\n#include \"ExpressionMinimal.h\"\n#include \"structDefines.h\"\n#include \"mathutil.h\"\n");

		sprintf(defineName, "WANT_EXPRCODE_ENUMS_%s", m_pParent->GetShortProjectName());
		MakeStringUpcase(defineName);
		fprintf(pExecutableOnlyFile, "#define %s\ntypedef enum exprCodeEnum_AutoGen\n{\nEXPRCODE_ENUM_PLACEHOLDERZERO,\n#include \"autogen\\%s_exprCodes_autogen.h\"\n} exprCodeEnum_AutoGen;\n#undef %s\n", defineName, m_pParent->GetShortProjectName(), defineName);
		
		sprintf(defineName, "WANT_EXPRCODE_STATICDEFINE_%s", m_pParent->GetShortProjectName());
		MakeStringUpcase(defineName);
		fprintf(pExecutableOnlyFile, "#define %s\nStaticDefineInt enumExprCodeEnum_Autogen[]=\n{\n\tDEFINE_INT\n#include \"autogen\\%s_exprCodes_autogen.h\"\nDEFINE_END\n};\n#undef %s\n", defineName, m_pParent->GetShortProjectName(), defineName);

		sprintf(defineName, "WANT_EXPRCODE_PROTOTYPES_%s", m_pParent->GetShortProjectName());
		MakeStringUpcase(defineName);
		fprintf(pExecutableOnlyFile, "#define %s\n#include \"autogen\\%s_exprCodes_autogen.h\"\n#undef %s\n", defineName, m_pParent->GetShortProjectName(), defineName);

		sprintf(defineName, "WANT_EXPRCODE_SWITCH_CASES_%s", m_pParent->GetShortProjectName());
		MakeStringUpcase(defineName);
		fprintf(pExecutableOnlyFile, "#define %s\nExprFuncReturnVal exprCodeEvaluate_Autogen(MultiVal** args, MultiVal* retval, ExprContext* context, char** errEString, ExprFuncDesc *pFuncDesc, void *pFuncPtr)\n{\n switch (pFuncDesc->eExprCodeEnum) {\n", defineName);
		fprintf(pExecutableOnlyFile, "#include \"autogen\\%s_exprCodes_autogen.h\"\n}\nassertmsgf(0, \"Unhandled exprCode case %%s\", pFuncDesc->pExprCodeName); return 0;\n}\n#undef %s\n", m_pParent->GetShortProjectName(), defineName);


		fprintf(pExecutableOnlyFile, "#include \"ExpressionEvaluateForAutogenIncluding.c\"\n");
	}

}

void MagicCommandManager::WriteOutExprCodeCase(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	char *pExprCode = GetFullExprCode(pCommand);
	int iArgNum;
	bool bHasErrString = false;
	bool bErrEString = false;

	//declare all local variables
	for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];


		switch (pArg->argType)
		{
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_BOOL:
		case ARGTYPE_FLOAT:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
		case ARGTYPE_FLOAT64:
		case ARGTYPE_STRING:
			fprintf(pOutFile, "\t%s arg%d; ", 
				pArg->argTypeName, iArgNum);
			break;

		case ARGTYPE_ENUM:
			fprintf(pOutFile, "\tint arg%d; ", 
				iArgNum);
			break;

		case ARGTYPE_ENTITY:
			if(pArg->iExpressionArgFlags & COMMAND_EXPR_FLAG_SELF_PTR)
			{
				fprintf(pOutFile, "\tEntity *arg%d = NULL; ", iArgNum);
				break;
			}
			// fall through and treat as a struct
		case ARGTYPE_STRUCT:
			CommandAssert(pCommand,
				(pArg->iExpressionArgFlags & COMMAND_EXPR_FLAG_SA_PRE_NN) ||
				(pArg->iExpressionArgFlags & COMMAND_EXPR_FLAG_SA_PRE_OP),
				"Pointer argument must be annotated with SA_PRE_OP* or SA_PARAM_OP* (to allow NULLs "
				"passed in) or SA_PRE_NN* or SA_PARAM_NN* (to disallow NULLs)");

			fprintf(pOutFile, "\tconst ExprVarEntry* arg%dvar; ", iArgNum);
			fprintf(pOutFile, "\tvoid* arg%d; ", iArgNum);
			break;

		case ARGTYPE_EXPR_SUBEXPR_IN:
			fprintf(pOutFile, "\tAcmdType_ExprSubExpr arg%d; ", iArgNum);
			break;

		case ARGTYPE_EXPR_ENTARRAY_IN:
		case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
		case ARGTYPE_EXPR_ENTARRAY_OUT:
			fprintf(pOutFile, "\tEntity ***arg%d = NULL; ", iArgNum);
			break;

		case ARGTYPE_EXPR_LOC_MAT4_IN:
		case ARGTYPE_EXPR_LOC_MAT4_OUT:
			fprintf(pOutFile, "\tVec3* arg%d = NULL; ", iArgNum);
			break;

		case ARGTYPE_EXPR_VEC4_OUT:
			fprintf(pOutFile, "\tF32* arg%d = NULL; ", iArgNum);
			break;
		case ARGTYPE_EXPR_INT_OUT:
			fprintf(pOutFile, "\tint arg%d = 0; ", iArgNum);
			break;
		case ARGTYPE_EXPR_FLOAT_OUT:
			fprintf(pOutFile, "\tfloat arg%d = 0.0f; ", iArgNum);
			break;
		case ARGTYPE_EXPR_STRING_OUT:
			fprintf(pOutFile, "\tconst char *arg%d = \"\"; ", iArgNum);
			break;

		case ARGTYPE_EXPR_ERRSTRING:
			fprintf(pOutFile, "\tchar **err = NULL; ", iArgNum);
			bHasErrString = true;
			bErrEString = true;
			break;

		case ARGTYPE_EXPR_ERRSTRING_STATIC:
			fprintf(pOutFile, "\tchar *err = NULL; ", iArgNum);
			bHasErrString = true;
			break;

		case ARGTYPE_EXPR_EXPRCONTEXT:
		case ARGTYPE_EXPR_CMULTIVAL:
		case ARGTYPE_EXPR_PARTITION:
			break;

		default:
			CommandAssert(pCommand, 0, "Unsupported argument type for expression list command");
			break;

		}
	}

	if (eaSize(&pCommand->ppArgs))
	{
		fprintf(pOutFile, "\n");
	}

	//declare variable for retval
	if (pCommand->eReturnType != ARGTYPE_NONE)
	{
		switch(pCommand->eReturnType)
		{
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_BOOL:
		case ARGTYPE_FLOAT:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
		case ARGTYPE_FLOAT64:
		case ARGTYPE_STRING:
		case ARGTYPE_EXPR_FUNCRETURNVAL:
			fprintf(pOutFile, "\n\t%s localRetVal;\n", pCommand->returnTypeName);
			break;

		case ARGTYPE_ENUM:
			fprintf(pOutFile, "\n\tint localRetVal;\n");
			break;

		case ARGTYPE_STRUCT:
			fprintf(pOutFile, "\n\tvoid *localRetVal;\n");
			break;

		default:
			CommandAssert(pCommand, 0, "Unsupported return type for expression list command");
			break;
		}
	}

//	fprintf(pOutFile, "\tPERFINFO_AUTO_START(\"%s\", 1);\n", pCommand->commandName);


	int iInputArgNum = 0;

	//now initialize all local variables
	for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

		switch (pArg->argType)
		{
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_BOOL:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
			fprintf(pOutFile, "\targ%d = (%s)args[%d]->intval;\n", iArgNum, pArg->argTypeName, iInputArgNum++);
			break;
		case ARGTYPE_ENUM:
			fprintf(pOutFile, "\targ%d = (int)args[%d]->intval;\n", iArgNum, iInputArgNum++);
			break;
		case ARGTYPE_FLOAT:
		case ARGTYPE_FLOAT64:
			fprintf(pOutFile, "\targ%d = QuickGetFloat(args[%d]);\n", iArgNum, iInputArgNum++);
			break;
		case ARGTYPE_STRING:
			fprintf(pOutFile, "\targ%d = (char*)(args[%d]->str);\n", iArgNum, iInputArgNum++);
			break;
		case ARGTYPE_EXPR_SUBEXPR_IN:
			fprintf(pOutFile, "\targ%d.exprPtr = args[%d]->ptr;\n", iArgNum, iInputArgNum++);
			fprintf(pOutFile, "\targ%d.exprSize = (int)(args[%d]->intval);\n", iArgNum, iInputArgNum++);
			break;
		case ARGTYPE_EXPR_ENTARRAY_IN:
		case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
			fprintf(pOutFile, "\targ%d = args[%d]->entarray;\n", iArgNum, iInputArgNum++);
			break;

		case ARGTYPE_EXPR_ENTARRAY_OUT:
			fprintf(pOutFile, "\targ%d = exprContextGetNewEntArray(context);\n", iArgNum);
			break;

		case ARGTYPE_EXPR_LOC_MAT4_IN:
			fprintf(pOutFile, "\targ%d = args[%d]->vecptr;\n", iArgNum, iInputArgNum++);
			break;

		case ARGTYPE_EXPR_LOC_MAT4_OUT:
			fprintf(pOutFile, "\targ%d = exprContextAllocScratchMemory(context, sizeof(Mat4));\n", iArgNum);
			fprintf(pOutFile, "\tcopyMat4(unitmat,arg%d);\n", iArgNum);
			break;

		case ARGTYPE_EXPR_VEC4_OUT:
			fprintf(pOutFile, "\targ%d = exprContextAllocScratchMemory(context, sizeof(Vec4));\n", iArgNum);
			fprintf(pOutFile, "\tzeroVec4(arg%d);\n", iArgNum);
			break;

		case ARGTYPE_ENTITY:
			if(pArg->iExpressionArgFlags & COMMAND_EXPR_FLAG_SELF_PTR)
			{
				fprintf(pOutFile, "\targ%d = exprContextGetSelfPtr(context);\n", iArgNum);
				fprintf(pOutFile, "\tif(!arg%d)\n", iArgNum);
				fprintf(pOutFile, "\t{\n");
				fprintf(pOutFile, "\t\tretval->type = MULTI_INVALID;\n");
				fprintf(pOutFile, "\t\tretval->str = \"Context does not have a self ptr\";\n");
//				fprintf(pOutFile, "\t\tPERFINFO_AUTO_STOP();\n");
				fprintf(pOutFile, "\t\treturn ExprFuncReturnError;\n");
				fprintf(pOutFile, "\t}\n");
				break;
			}
			// fall through and treat as a struct
		case ARGTYPE_STRUCT:
			fprintf(pOutFile, "\targ%dvar = args[%d]->ptr;\n", iArgNum, iInputArgNum++);
			fprintf(pOutFile, "\targ%d = arg%dvar->inptr;\n", iArgNum, iArgNum);
			break;
		case ARGTYPE_EXPR_ERRSTRING:
			fprintf(pOutFile, "\terr = errEString;\n");
			break;

		case ARGTYPE_EXPR_ERRSTRING_STATIC:
			fprintf(pOutFile, "\terr = NULL;\n");
			break;
		}
	}

	//now call the function
	fprintf(pOutFile, "\t%s((EXPRCODE_PROTOTYPE_%s*)(pFuncPtr))(", 
		pCommand->eReturnType == ARGTYPE_NONE ? "" : "localRetVal = ",
		pExprCode);

	for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

		switch (pArg->argType)
		{
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_FLOAT:
		case ARGTYPE_BOOL:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
		case ARGTYPE_FLOAT64:
		case ARGTYPE_STRING:
		case ARGTYPE_ENTITY:
		case ARGTYPE_STRUCT:
		case ARGTYPE_ENUM:
		case ARGTYPE_EXPR_ENTARRAY_IN:
		case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
		case ARGTYPE_EXPR_ENTARRAY_OUT:
		case ARGTYPE_EXPR_LOC_MAT4_IN:
		case ARGTYPE_EXPR_LOC_MAT4_OUT:
		case ARGTYPE_EXPR_VEC4_OUT:
				fprintf(pOutFile, "arg%d%s", iArgNum,
				iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;

		case ARGTYPE_EXPR_CMULTIVAL:
			fprintf(pOutFile, "args[%d]%s", iArgNum - 1,
				iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;

		case ARGTYPE_EXPR_SUBEXPR_IN:
		case ARGTYPE_EXPR_INT_OUT:
		case ARGTYPE_EXPR_FLOAT_OUT:
		case ARGTYPE_EXPR_STRING_OUT:
			fprintf(pOutFile, "&arg%d%s", iArgNum,
				iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;

		case ARGTYPE_EXPR_ERRSTRING:
			fprintf(pOutFile, "err%s",
				iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;

		case ARGTYPE_EXPR_ERRSTRING_STATIC:
			fprintf(pOutFile, "&err%s",
				iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;

		case ARGTYPE_EXPR_EXPRCONTEXT:
			fprintf(pOutFile, "context%s", iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;

		case ARGTYPE_EXPR_PARTITION:
			fprintf(pOutFile, "exprContextGetPartition(context)%s", iArgNum < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
			break;
		}
	}

	fprintf(pOutFile, ");\n");

	//called the function, now do something with the return val

	if (pCommand->eReturnType == ARGTYPE_EXPR_FUNCRETURNVAL)
	{
		fprintf(pOutFile, "\tif (localRetVal == ExprFuncReturnError)\n\t{\n");

		if(bHasErrString)
		{
			fprintf(pOutFile, "\t\tretval->type = MULTI_INVALID;\n\t\tretval->str = ");
			if(bErrEString)
				fprintf(pOutFile, "*err");
			else
				fprintf(pOutFile, "err");
			fprintf(pOutFile, ";\n");
		}
			
//		fprintf(pOutFile, "\t\tPERFINFO_AUTO_STOP();\n");
		fprintf(pOutFile, "\t\treturn ExprFuncReturnError;\n\t}\n");
	}

	switch (pCommand->eReturnType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_UINT:
	case ARGTYPE_BOOL:
	case ARGTYPE_SINT64:
	case ARGTYPE_UINT64:
	case ARGTYPE_ENUM:
		fprintf(pOutFile, "\tretval->type = MULTI_INT;\n\tretval->intval = localRetVal;\n");
		break;

	case ARGTYPE_FLOAT:
	case ARGTYPE_FLOAT64:
		fprintf(pOutFile, "\tretval->type = MULTI_FLOAT;\n\tretval->floatval = localRetVal;\n");
		break;

	case ARGTYPE_STRING:
		fprintf(pOutFile, "\tretval->type = MULTI_STRING;\n\tretval->str = localRetVal ? localRetVal : \"\";\n");
		break;

	case ARGTYPE_STRUCT:
		fprintf(pOutFile, "\tretval->type = MULTI_NP_POINTER;\n\tretval->ptr_noconst = exprContextAllocScratchMemory(context, sizeof(ExprVarEntry));\n");
		fprintf(pOutFile, "\tmemset(retval->ptr_noconst, 0, sizeof(ExprVarEntry));\n");
		fprintf(pOutFile, "\t((ExprVarEntry*)retval->ptr_noconst)->table = pFuncDesc->returnType.ptrType;\n");
		fprintf(pOutFile, "\t((ExprVarEntry*)retval->ptr_noconst)->inptr = localRetVal;\n");
		fprintf(pOutFile, "\t((ExprVarEntry*)retval->ptr_noconst)->allowObjPath = true;\n");
		fprintf(pOutFile, "\t((ExprVarEntry*)retval->ptr_noconst)->allowVarAccess = true;\n");
		fprintf(pOutFile, "\t((ExprVarEntry*)retval->ptr_noconst)->name = \"FuncReturnVal\";\n");
		break;
	}

	for (iArgNum=0; iArgNum < eaSize(&pCommand->ppArgs); iArgNum++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[iArgNum];

		switch (pArg->argType)
		{
		case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
		case ARGTYPE_EXPR_ENTARRAY_OUT:
			fprintf(pOutFile, "\tretval->type = MULTI_NP_ENTITYARRAY;\n\tretval->entarray = arg%d;\n", iArgNum);
			break;

		case ARGTYPE_EXPR_ENTARRAY_IN:
			fprintf(pOutFile, "\texprContextClearEntArray(context, arg%d);\n", iArgNum);
			break;
		case ARGTYPE_EXPR_LOC_MAT4_IN:
			fprintf(pOutFile, "\texprContextFreeScratchMemory(context, arg%d);\n", iArgNum);
			break;

		case ARGTYPE_EXPR_INT_OUT:
			fprintf(pOutFile, "\tretval->type = MULTI_INT;\n\tretval->intval = arg%d;\n", iArgNum);
			break;
		case ARGTYPE_EXPR_FLOAT_OUT:
			fprintf(pOutFile, "\tretval->type = MULTI_FLOAT;\n\tretval->floatval = arg%d;\n", iArgNum);
			break;
		case ARGTYPE_EXPR_STRING_OUT:
			fprintf(pOutFile, "\tretval->type = MULTI_STRING;\n\tretval->str = arg%d;\n", iArgNum);
			break;
		case ARGTYPE_EXPR_VEC4_OUT:
			fprintf(pOutFile, "\tretval->type = MULTI_VEC4;\n\tretval->ptr = arg%d;\n", iArgNum);
			break;
		case ARGTYPE_EXPR_LOC_MAT4_OUT:
			fprintf(pOutFile, "\tretval->type = MULTIOP_LOC_MAT4;\n\tretval->ptr = arg%d;\n", iArgNum);
			break;
		}
	}

//	fprintf(pOutFile, "\tPERFINFO_AUTO_STOP();\n");

	if (pCommand->eReturnType == ARGTYPE_EXPR_FUNCRETURNVAL)
	{
		fprintf(pOutFile, "\treturn localRetVal;\n");
	}
	else
	{
		fprintf(pOutFile, "\treturn ExprFuncReturnFinished;\n");
	}
}


bool MagicCommandManager::CommandShouldBeWrittenToWiki(MAGIC_COMMAND_STRUCT *pCommand)
{

	if (pCommand->commandsWhichThisIsTheErrorFunctionFor[0][0] || (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
		|| (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
		|| (pCommand->iCommandFlags & COMMAND_FLAG_PRIVATE)
		|| pCommand->commandSets[0][0])
	{
		return false;
	}

	return true;
}

char *MagicCommandManager::GetWikiNameForArgType(enumMagicCommandArgType eArgType, char *pArgTypeName)
{
	static char temp[1024];

	switch (eArgType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_UINT:
		return "int";

	case ARGTYPE_ENUM:
		return "enum";

	case ARGTYPE_BOOL:
		return "bool";
	
	case ARGTYPE_FLOAT:
		return "float";

	case ARGTYPE_SINT64:
		return "S64";
	
	case ARGTYPE_UINT64:
		return "U64";

	case ARGTYPE_FLOAT64:
		return "F64";
	
	case ARGTYPE_STRING:
	case ARGTYPE_ESCAPEDSTRING:
		return "string";

	case ARGTYPE_SENTENCE:
		return "sentence";

	case ARGTYPE_VEC3_POINTER:
	case ARGTYPE_VEC3_DIRECT:
		return "xyz vector";
		
	case ARGTYPE_VEC4_POINTER:
	case ARGTYPE_VEC4_DIRECT:
		return "xyzw vector";
		
	case ARGTYPE_MAT4_POINTER:
	case ARGTYPE_MAT4_DIRECT:
		return "4x4 matrix";

	case ARGTYPE_QUAT_POINTER:
	case ARGTYPE_QUAT_DIRECT:
		return "quaternion";

	case ARGTYPE_EXPR_SUBEXPR_IN:
		return "SubExpression";
	
	case ARGTYPE_EXPR_ENTARRAY_IN:
		return "Entity Array";

	case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
		return "Entity Array (modified)";

	case ARGTYPE_EXPR_LOC_MAT4_IN:
		return "Location";

	case ARGTYPE_STRUCT:
		sprintf(temp, "%s struct", pArgTypeName);
		return temp;


	}

	return NULL;
}


char *MagicCommandManager::GetWikiNameForReturnType(enumMagicCommandArgType eArgType, char *pArgTypeName)
{
	switch (eArgType)
	{
	case ARGTYPE_EXPR_INT_OUT:
		return "int";
	case ARGTYPE_EXPR_FLOAT_OUT:
		return "float";
	case ARGTYPE_EXPR_STRING_OUT:
		return "string";
	case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
	case ARGTYPE_EXPR_ENTARRAY_OUT:
		return "Entity Array";
	case ARGTYPE_EXPR_LOC_MAT4_OUT:
		return "Location";
	case ARGTYPE_EXPR_VEC4_OUT:
		return "Vec4";
	default:
		return GetWikiNameForArgType(eArgType, pArgTypeName);
	}
}

bool MagicCommandManager::CommandHasCorrectExpressionNessForWiki(MAGIC_COMMAND_STRUCT *pCommand,
	bool bWantExpressionCommands)
{
	if (bWantExpressionCommands)
	{
		return pCommand->iNumExpressionTags != 0;
	}

	return !CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand);
}

void MagicCommandManager::WriteOutWikiFileForCategory(char *pCategoryName, bool bExpressionCommands)
{
	char fileName[MAX_PATH];
	FILE *pOutFile;
	int iCommandNum;
	bool bWroteAtLeastOne = false;



	if (pCategoryName[0])
	{
		sprintf(fileName, "%s\\wiki\\%s_%s_auto%scommands.wiki", m_pParent->GetProjectPath(), m_pParent->GetShortProjectName(), pCategoryName, bExpressionCommands ? "expr" : "");

		STATICASSERT(m_iNumCategoriesWritten < MAX_OVERALL_CATEGORIES, "Too many total command categories");
		if (!bExpressionCommands)
		{
			strcpy(m_CategoriesWritten[m_iNumCategoriesWritten++], pCategoryName);
		}
	}
	else
	{
		sprintf(fileName, "%s\\wiki\\%s_auto%scommands.wiki", m_pParent->GetProjectPath(), m_pParent->GetShortProjectName(), bExpressionCommands ? "expr" : "");
	}

	pOutFile = fopen_nofail(fileName, "wt");

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandShouldBeWrittenToWiki(pCommand) && (CommandIsInCategory(pCommand, pCategoryName)
			&& CommandHasCorrectExpressionNessForWiki(pCommand, bExpressionCommands)))
		{
			int i;

			bWroteAtLeastOne = true;

			pCommand->bAlreadyWroteOutComment = true;

			fprintf(pOutFile, "h4.%s ", pCommand->commandName);

			for (i = 0; i < MAX_COMMAND_ALIASES; i++)
			{
				if (pCommand->commandAliases[i][0])
				{
					if (i == 0)
					{
						fprintf(pOutFile, "(AKA %s",pCommand->commandAliases[i]);
					}
					else
					{
						fprintf(pOutFile, ", %s",pCommand->commandAliases[i]);
					}
				}
				else
				{
					break;
				}
			}
			if (i != 0)
			{
				fprintf(pOutFile, ") ");
			}

			fprintf(pOutFile,"(AccessLevel %d) [Notes|Command_%s_%s]\n", pCommand->iAccessLevel, pCommand->commandName, m_pParent->GetShortProjectName());
			
			//only expression autocommands get their return values written
			if (pCommand->iNumExpressionTags)
			{
				if (pCommand->eReturnType)
				{
					char *pReturnTypeName = GetWikiNameForReturnType(pCommand->eReturnType, pCommand->returnTypeName);

					if (pReturnTypeName)
					{
						fprintf(pOutFile, "*Returns: %s*\n", pReturnTypeName);
					}
				}
				else
				{
					fprintf(pOutFile, "*No Return*\n");
				}
			}

			int iArgNum = 1;
			for (i=0; i < eaSize(&pCommand->ppArgs); i++)
			{
				MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

				char *pWikiArgName = GetWikiNameForArgType(pArg->argType, pArg->argTypeName);

				if (pWikiArgName)
				{
					fprintf(pOutFile, "*Parameter %d: %s (%s)*\n", iArgNum++, pArg->argName, pWikiArgName);
				}
			}
			fprintf(pOutFile, "%s\n\n", pCommand->comment);
		}
	}

	if (!bExpressionCommands)
	{
		int iVarNum;
		for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
		{
			MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

			if (CommandVarIsInCategory(pCommandVar, pCategoryName))
			{
				bWroteAtLeastOne = true;
				pCommandVar->bAlreadyWroteOutComment = true;

				fprintf(pOutFile, "h4.%s (%s) - (AccessLevel %d) [Notes|Command_%s_%s]\n", pCommandVar->varCommandName, GetWikiNameForArgType(pCommandVar->eVarType, "You should never see this - talk to Alex"), pCommandVar->iAccessLevel, pCommandVar->varCommandName, m_pParent->GetShortProjectName());

			
				fprintf(pOutFile, "%s\n\n", pCommandVar->comment);
			}
		}
	}



	fclose(pOutFile);

	if (!bWroteAtLeastOne)
	{
		char systemString[1024];
		sprintf(systemString, "erase \"%s\" 2>nul", fileName);
		system(systemString);
	}


}


/*
	#define ARGMAT4 {TYPE_MAT4}
#define ARGVEC3 {TYPE_VEC3}
#define ARGF32 {TYPE_F32}
#define ARGU32 {TYPE_INT}
#define ARGS32 {TYPE_INT}
#define ARGSTR(size) {TYPE_STR, 0, (size)}
#define ARGSENTENCE(size) {TYPE_SENTENCE, 0, (size)}
#define ARGSTRUCT(str,def) {TYPE_TEXTPARSER, def, sizeof(str)}




Cmd game_private_cmds[] = 
{
	{ 9, "privategamecmd",		0, { ARGSTR(100)},0,
		"Test the new command parse system",testCommand},
	{ 0, "bug_internal", 0, {ARGS32, ARGSENTENCE(10000)}, 0,
		"Internal command used to process /bug commands",bugInternal },
	{ 9, "AStarRecording",		0, {{0}},0,
		"Starts AStarRecording"},
	{ 9, "ShowBeaconDebugInfo",		0, {{0}},0,
		"Gets beacon debug information"},
	{0}
};

static void bugInternal(CMDARGS)
{
	READARGS2(int,mode,char*,desc );
	BugReportInternal(desc, mode);
	if(mode)		// the /csrbug command will set tmp_int == 0, so no message is displayed
		conPrintf(textStd("BugLogged"));
}*/

void MagicCommandManager::ReadGlobalTypes(Tokenizer *pTokenizer, AUTO_SETTING_GLOBAL_TYPES *pGlobalTypes)
{
	Token token;

	while (1)
	{
		if (pGlobalTypes->iNumGlobalTypes == MAX_GLOBALTYPES_PER_AUTO_SETTING)
		{
			pTokenizer->AssertFailedf("Too many global types, max is %d", MAX_GLOBALTYPES_PER_AUTO_SETTING);
		}

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_GLOBALTYPES_NAME_LEN - 1, "Expected identifier in list of global types");

		strcpy(pGlobalTypes->GlobalTypeNames[pGlobalTypes->iNumGlobalTypes++], token.sVal);

		pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) or , after global type in list");

		if (token.iVal == RW_RIGHTPARENS)
		{
			break;
		}
	}
}

void MagicCommandManager::FoundMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, int iWhichMagicWord, char *pMagicWordString)
{
	SourceParserBaseClass::FoundMagicWord(pSourceFileName, pTokenizer, iWhichMagicWord, pMagicWordString);
	
	if (iWhichMagicWord ==  MAGICWORD_BEGINNING_OF_FILE || iWhichMagicWord == MAGICWORD_END_OF_FILE)
	{
		return;
	}

	switch (iWhichMagicWord)
	{
	case MAGICWORD_AUTO_COMMAND:
	case MAGICWORD_AUTO_COMMAND_REMOTE:
	case MAGICWORD_AUTO_COMMAND_REMOTE_SLOW:
	case MAGICWORD_AUTO_COMMAND_QUEUED:
	case MAGICWORD_EXPR_FUNC:
	case MAGICWORD_EXPR_FUNC_STATIC_CHECK:
		FoundCommandMagicWord(pSourceFileName, pTokenizer, 
			iWhichMagicWord == MAGICWORD_AUTO_COMMAND_REMOTE || iWhichMagicWord == MAGICWORD_AUTO_COMMAND_REMOTE_SLOW, 
			iWhichMagicWord == MAGICWORD_AUTO_COMMAND_REMOTE_SLOW,
			iWhichMagicWord == MAGICWORD_AUTO_COMMAND_QUEUED,
			iWhichMagicWord == MAGICWORD_EXPR_FUNC,
			iWhichMagicWord == MAGICWORD_EXPR_FUNC_STATIC_CHECK);
		break;

	case MAGICWORD_AUTO_INT:
		FoundCommandVarMagicWord(pSourceFileName, pTokenizer, ARGTYPE_SINT);
		break;
	case MAGICWORD_AUTO_FLOAT:
		FoundCommandVarMagicWord(pSourceFileName, pTokenizer, ARGTYPE_FLOAT);
		break;
	case MAGICWORD_AUTO_STRING:
	case MAGICWORD_AUTO_ESTRING:
		FoundCommandVarMagicWord(pSourceFileName, pTokenizer, ARGTYPE_STRING);
		break;
	case MAGICWORD_AUTO_SENTENCE:
	case MAGICWORD_AUTO_ESENTENCE:
		FoundCommandVarMagicWord(pSourceFileName, pTokenizer, ARGTYPE_SENTENCE);
		break;
	}
}


void MagicCommandManager::FoundCommandVarMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, enumMagicCommandArgType eCommandVarType)
{
	Tokenizer tokenizer;

	Token token;
	enumTokenType eType;

	MAGIC_COMMANDVAR_STRUCT *pVar = (MAGIC_COMMANDVAR_STRUCT*)calloc(sizeof(MAGIC_COMMANDVAR_STRUCT), 1);
	eaPush(&m_ppMagicCommandVars, pVar);

	//initialize the new command
	memset(pVar, 0, sizeof(MAGIC_COMMANDVAR_STRUCT));
	pVar->iAccessLevel = 9;
	pVar->eVarType = eCommandVarType;
	strcpy(pVar->sourceFileName, pSourceFileName);
	pVar->iLineNum = pTokenizer->GetCurLineNum();
	
	pVar->pIfDefStack = CopyIfDefStack(pTokenizer->GetIfDefStack());


	m_bSomethingChanged = true;

/*
these often look like this:

//comment here
int foo;
AUTO_CMD_INT(foo, foo);

so we need to skip one line before AUTO_CMD_INT before looking for the comment
*/
	pTokenizer->GetSurroundingSlashedCommentBlock(&token, true);
	if (token.sVal[0])
	{
		strcpy(pVar->comment, token.sVal);
	}
	else
	{
		sprintf(pVar->comment, "No comment provided");
	}


	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AUTO_CMD_XXX");
	
	//skip tokens until we find a comma, as the variable name may be multiple tokens, having potential . and -> in it
	do
	{
		eType = pTokenizer->MustGetNextToken(&token, "EOF in the middle of AUTO_CMD_XXX");
	} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA));

	pTokenizer->Assert2NextTokenTypesAndGet(&token, 
		TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH - 1, 
		TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH - 1, 
		"Expected identifier or string after AUTO_CMD_XXX(varname,");

	strcpy(pVar->varCommandName, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AUTO_CMD_XXX(varname, commandname");

	//check for ACMD_ commands
	while (1)
	{
		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_ACCESSLEVEL") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_ACCESSLEVEL");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after ACMD_ACCESSLEVEL(");

			pVar->iAccessLevel = token.iVal;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_ACCESSLEVEL(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_APPSPECIFICACCESSLEVEL") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_APPSPECIFICACCESSLEVEL");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 64, "Expected GLOBALTYPE_XXX after ACMD_APPSPECIFICACCESSLEVEL(");
			strcpy(pVar->serverSpecificAccessLevel_ServerName, token.sVal);
			
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after ACMD_APPSPECIFICACCESSLEVEL(xxx");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after ACMD_APPSPECIFICACCESSLEVEL(xxx,");

			pVar->iServerSpecificAccessLevel = token.iVal;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_APPSPECIFICACCESSLEVEL(xxx, x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CONTROLLER_AUTO_SETTING") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_CONTROLLER_AUTO_SETTINGATEGORY");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_CONTROLLER_AUTO_SETTING(");

			AddCommandVarToCategory_ControllerAutoSetting(pVar, token.sVal, pTokenizer);
		
			pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_CATEGORY(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_AUTO_SETTING") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_AUTO_SETTING");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_CONTROLLER_AUTO_SETTING(");

			AddCommandVarToCategory_ControllerAutoSetting(pVar, token.sVal, pTokenizer);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after ACMD_AUTO_SETTING(category");
	
			ReadGlobalTypes(pTokenizer, &pVar->autoSettingGlobalTypes);

		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CATEGORY") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_CATEGORY");
			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_CATEGORY(");
			
				AddCommandVarToCategory(pVar, token.sVal, pTokenizer);

				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_CATEGORY(x");
			} while (token.iVal == RW_COMMA);		
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_LIST") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_LIST");
			
			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_LIST(");
			
				AddCommandVarToSet(pVar, token.sVal, pTokenizer);

				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_LIST(x");
			} while (token.iVal == RW_COMMA);
		}		
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_PRIVATE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_PRIVATE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EARLYCOMMANDLINE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_EARLYCOMMANDLINE | COMMAND_FLAG_COMMANDLINE | COMMAND_FLAG_COMMANDLINE_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_ALLOW_JSONRPC") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags2 |= COMMAND_FLAG2_ALLOW_JSONRPC;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_FORCEWRITECURFILE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_FORCEWRITECURFILEINSTRUCTS;
		}
		else if (eType == TOKEN_IDENTIFIER && (strcmp(token.sVal, "ACMD_COMMANDLINE") == 0 || strcmp(token.sVal, "ACMD_CMDLINE") == 0))
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_COMMANDLINE | COMMAND_FLAG_COMMANDLINE_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CMDLINEORPUBLIC") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pVar->iCommandFlags |= COMMAND_FLAG_COMMANDLINE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_GLOBAL") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_GLOBAL;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_SERVERONLY") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_SERVER_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CLIENTONLY") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_CLIENT_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_NONSTATICINTERNALCMD") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pVar->iCommandFlags |= COMMAND_FLAG_NONSTATICINTERNALCMD;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_HIDE") == 0)
		{
			pTokenizer->GetNextToken(&token);
			
			pVar->iCommandFlags |= COMMAND_FLAG_HIDE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_IGNOREPARSEERRORS") == 0)
		{
			pTokenizer->GetNextToken(&token);
			
			pVar->iCommandFlags |= COMMAND_FLAG_IGNOREPARSEERRORS;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CALLBACK") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_CALLBACK");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_CALLBACK(");
	
			strcpy(pVar->callbackFunc, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_CALLBACK(x");

		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_MAXVALUE") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_MAXVALUE");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after ACMD_MAXVALUE(");

			pVar->iMaxValue = token.iVal;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_MAXVALUE(x");
		}		
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_PRODUCTS") == 0)
		{
			int i;
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertGetParenthesizedCommaSeparatedList("getting ACMD_PRODUCTS", &pVar->ppProducts);

			if (eaSize(&pVar->ppProducts) == 1 && stricmp(pVar->ppProducts[0], "all") == 0)
			{
				//do nothing, ALL is legal
			}
			else
			{
				for (i = 0; i < eaSize(&pVar->ppProducts); i++)
				{
					if (!m_pParent->DoesVariableHaveValue("Products", pVar->ppProducts[i], false))
					{
						pTokenizer->AssertFailedf("Found unknown product %s in ACMD_PRODUCTS. Legal products are defined in src/core/StructParserVars.txt", pVar->ppProducts[i]);
					}
				}
			}
		}
		else
		{
			break;
		}
	}

	if (pVar->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
	{
		ASSERT(pTokenizer,pVar->iAccessLevel == 0 && pVar->serverSpecificAccessLevel_ServerName[0] == 0, "EARLYCOMMANDLINE commands must have access level 0");
	}

	if (eaSize(&pVar->ppProducts))
	{
		if (pVar->iCommandFlags & (COMMAND_FLAG_SLOW_REMOTE | COMMAND_FLAG_REMOTE | COMMAND_FLAG_QUEUED | COMMAND_FLAG_EARLYCOMMANDLINE))
		{
			pTokenizer->AssertFailedf("Command can't have ACMD_PRODUCTS and be early");
		}

		if (!pVar->commandSets[0][0] == 0)
		{
			pTokenizer->AssertFailedf("Commands can't have both ACMD_PRODUCTS and ACMD_SET");
		}
	}

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ; after AUTO_CMD_XXX(varname, commandname)");
}

bool MagicCommandManager::CommandHasArgOfType(MAGIC_COMMAND_STRUCT *pCommand, enumMagicCommandArgType eType)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		if (pCommand->ppArgs[i]->argType == eType)
		{
			return true;
		}
	}

	return false;
}

int MagicCommandManager::CountArgsOfType(MAGIC_COMMAND_STRUCT *pCommand, enumMagicCommandArgType eType)
{
	int i;
	int iCount = 0;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		if (pCommand->ppArgs[i]->argType == eType)
		{
			iCount++;
		}
	}

	return iCount;
}
int MagicCommandManager::GetNumNormalArgs(MAGIC_COMMAND_STRUCT *pCommand)
{
	int iRetVal = 0;
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		if (pCommand->ppArgs[i]->argType < ARGTYPE_FIRST_SPECIAL)
		{
			iRetVal++;
		}
	}

	return iRetVal;
}





void MagicCommandManager::FoundCommandMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, 
	bool bIsRemoteCommand, bool bIsSlowCommand, bool bIsQueuedCommand, bool bIsExprFunc, bool bIsExprFuncStaticCheck)
{
	Tokenizer tokenizer;

	Token token;
	enumTokenType eType;



	//for slow commands, we read the "return value" early, and save it here
	char slowCommandReturnTypeName[MAX_MAGICCOMMAND_ARGNAME_LENGTH];
	int slowCommandReturnTypeNumAsterisks = 0;

	MAGIC_COMMAND_STRUCT *pCommand = (MAGIC_COMMAND_STRUCT*)calloc(sizeof(MAGIC_COMMAND_STRUCT), 1);
	eaPush(&m_ppMagicCommands, pCommand);

	//initialize the new command
	memset(pCommand, 0, sizeof(MAGIC_COMMAND_STRUCT));
	pCommand->iAccessLevel = 9;
	
	
	IfDefStack *pIfDefStack = pTokenizer->GetIfDefStack();
	if (pIfDefStack && pIfDefStack->iNumIfDefs)
	{
		pCommand->pIfDefStack = CopyIfDefStack(pIfDefStack);
	}

	if (bIsRemoteCommand)
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_REMOTE;
	}	
	if (bIsSlowCommand)
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_REMOTE | COMMAND_FLAG_SLOW_REMOTE;
	}	
	if (bIsQueuedCommand)
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_QUEUED;
	}

	m_bSomethingChanged = true;

	strcpy(pCommand->sourceFileName, pSourceFileName);
	pCommand->iLineNum = pTokenizer->GetCurLineNum();

	pTokenizer->GetSurroundingSlashedCommentBlock(&token, false);
	if (token.sVal[0])
	{
		strcpy(pCommand->comment, token.sVal);
	}
	else
	{
		sprintf(pCommand->comment, "No comment provided");
	}

	//if this is an AUTO_EXPR_FUNC, get the tags
	if (bIsExprFunc)
	{

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AUTO_EXPR_FUNC");

		pCommand->iCommandFlags |= COMMAND_FLAG_EXPR_WRAPPER; // generate is implicit in ACMD_EXPR_LIST

		do 
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after AUTO_EXPR_FUNC(");
			strcpy(pCommand->expressionTag[pCommand->iNumExpressionTags++], token.sVal);

			ASSERT(pTokenizer,pCommand->iNumExpressionTags <= MAX_COMMAND_SETS, "Too many ACMD_EXPR_TAGs specified");

			pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after AUTO_EXPR_FUNC(x");
		} while(token.iVal == RW_COMMA);
	}

	if (bIsExprFuncStaticCheck)
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_EXPR_WRAPPER;
	}

	//if this is a slow command, get the slow command arg name
	if (bIsSlowCommand)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AUTO_COMMAND_REMOTE_SLOW )");
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMAND_ARGNAME_LENGTH, "Expected typename after AUTO_COMMAND_REMOTE_SLOW(");

		strcpy(slowCommandReturnTypeName, token.sVal);

		while ( (eType = pTokenizer->CheckNextToken(&token)) == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
		{
			slowCommandReturnTypeNumAsterisks++;
			pTokenizer->GetNextToken(&token);
		}

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AUTO_COMMAND_REMOTE(x");

	}

	//if this is a queued command, get the queue name
	if (bIsQueuedCommand)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AUTO_COMMAND_QUEUED )");
		
		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_IDENTIFIER)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected queue name after AUTO_COMMAND_QUEUED(");

			strcpy(pCommand->queueName, token.sVal);
		}
		else
		{
			pCommand->queueName[0] = 0;
		}

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AUTO_COMMAND_QUEUED(x");

	}

	


	

	//check for ACMD_ commands
	while (1)
	{
		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CACHE_AUTOCOMPLETE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_CACHE_AUTOCOMPLETE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_I_AM_THE_ERROR_FUNCTION_FOR") == 0)
		{
			int i;

			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_I_AM_THE_ERROR_FUNCTION_FOR");
			pTokenizer->Assert2NextTokenTypesAndGet(&token, 
				TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH - 1, 
				TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH - 1, 
				"Expected identifier or string after ACMD_I_AM_THE_ERROR_FUNCTION_FOR(");

			for (i=0; i < MAX_ERROR_FUNCTIONS_PER_COMMAND; i++)
			{
				if (!pCommand->commandsWhichThisIsTheErrorFunctionFor[i][0])
				{
					strcpy(pCommand->commandsWhichThisIsTheErrorFunctionFor[i], token.sVal);
					break;
				}
			}

			ASSERT(pTokenizer,i < MAX_ERROR_FUNCTIONS_PER_COMMAND, "Too many ACMD_I_AM_THE_ERROR_FUNCTION_FORs");

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_I_AM_THE_ERROR_FUNCTION_FOR(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_NAME") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_NAME");
			pTokenizer->Assert2NextTokenTypesAndGet(&token, 
				TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH - 1, 
				TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH - 1, 
				"Expected identifier or string after ACMD_NAME(");

			strcpy(pCommand->commandName, token.sVal);

			strcpy(pCommand->safeCommandName,token.sVal);
			MakeStringAllAlphaNum(pCommand->safeCommandName);


			int iNumAliases = 0;


			while (	(eType = pTokenizer->CheckNextToken(&token)) == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
			{
				pTokenizer->GetNextToken(&token);
				pTokenizer->Assert2NextTokenTypesAndGet(&token, 
					TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH - 1, 
					TOKEN_STRING, MAX_MAGICCOMMANDNAMELENGTH - 1, 
					"Expected identifier or string after ACMD_NAME(x,");

				ASSERT(pTokenizer,iNumAliases < MAX_COMMAND_ALIASES, "Too many aliases");

				strcpy(pCommand->commandAliases[iNumAliases++], token.sVal);
			}


			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_NAME(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_ACCESSLEVEL") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_ACCESSLEVEL");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after ACMD_ACCESSLEVEL(");

			pCommand->iAccessLevel = token.iVal;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_ACCESSLEVEL(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_APPSPECIFICACCESSLEVEL") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_APPSPECIFICACCESSLEVEL");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 64, "Expected GLOBALTYPE_XXX after ACMD_APPSPECIFICACCESSLEVEL(");
			strcpy(pCommand->serverSpecificAccessLevel_ServerName, token.sVal);
			
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after ACMD_APPSPECIFICACCESSLEVEL(xxx");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after ACMD_APPSPECIFICACCESSLEVEL(xxx,");

			pCommand->iServerSpecificAccessLevel = token.iVal;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_APPSPECIFICACCESSLEVEL(xxx, x");
		}		
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CATEGORY") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_CATEGORY");
		
			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_CATEGORY(");
			
				AddCommandToCategory(pCommand, token.sVal, pTokenizer);

				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_CATEGORY(x");
			} while (token.iVal == RW_COMMA);		
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_LIST") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_LIST");
			
			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_LIST(");
			
				AddCommandToSet(pCommand, token.sVal, pTokenizer);

				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_LIST(x");
			} while (token.iVal == RW_COMMA);
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_PRIVATE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_PRIVATE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_FORCEWRITECURFILE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_FORCEWRITECURFILEINSTRUCTS;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_ALLOW_JSONRPC") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags2 |= COMMAND_FLAG2_ALLOW_JSONRPC;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EARLYCOMMANDLINE") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_EARLYCOMMANDLINE | COMMAND_FLAG_COMMANDLINE | COMMAND_FLAG_COMMANDLINE_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && (strcmp(token.sVal, "ACMD_COMMANDLINE") == 0 || strcmp(token.sVal, "ACMD_CMDLINE") == 0))
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_COMMANDLINE | COMMAND_FLAG_COMMANDLINE_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && (strcmp(token.sVal, "ACMD_CMDLINEORPUBLIC") == 0 || strcmp(token.sVal, "ACMD_CMDLINE") == 0))
		{
			pTokenizer->GetNextToken(&token);

			pCommand->iCommandFlags |= COMMAND_FLAG_COMMANDLINE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_GLOBAL") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_GLOBAL;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CLIENTONLY") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_CLIENT_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_SERVERONLY") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_SERVER_ONLY;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_MULTIPLE_RECIPIENTS") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_MULTIPLE_RECIPIENTS;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_NONSTATICINTERNALCMD") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_NONSTATICINTERNALCMD;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_TESTCLIENT") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_TESTCLIENT;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_NOTESTCLIENT") == 0)
		{
			pTokenizer->GetNextToken(&token);
		
			pCommand->iCommandFlags |= COMMAND_FLAG_NOTESTCLIENT;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_HIDE") == 0)
		{
			pTokenizer->GetNextToken(&token);
			
			pCommand->iCommandFlags |= COMMAND_FLAG_HIDE;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_IGNOREPARSEERRORS") == 0)
		{
			pTokenizer->GetNextToken(&token);
			
			pCommand->iCommandFlags |= COMMAND_FLAG_IGNOREPARSEERRORS;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CLIENTCMD") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pCommand->iCommandFlags |= COMMAND_FLAG_CLIENT_WRAPPER;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_CLIENTCMDFAST") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pCommand->iCommandFlags |= COMMAND_FLAG_CLIENT_WRAPPER | COMMAND_FLAG_CLIENT_WRAPPER_FAST;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_SERVERCMD") == 0)
		{
			pTokenizer->GetNextToken(&token);
			
			pCommand->iCommandFlags |= COMMAND_FLAG_SERVER_WRAPPER;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_GENERICSERVERCMD") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pCommand->iCommandFlags |= COMMAND_FLAG_GENERICSERVER_WRAPPER;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_GENERICCLIENTCMD") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pCommand->iCommandFlags |= COMMAND_FLAG_GENERICCLIENT_WRAPPER;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_IFDEF") == 0)
		{
			pTokenizer->GetNextToken(&token);
			ASSERT(pTokenizer,pCommand->iNumDefines < MAX_MAGICCOMMAND_DEFINES, "Too many ACMD_IFDEFs");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_IFDEF");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_IFDEF(");
			strcpy(pCommand->defines[pCommand->iNumDefines++], token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_IFDEF(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_TAG") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_TAG");

			pCommand->iCommandFlags |= COMMAND_FLAG_EXPR_WRAPPER; // generate is implicit in ACMD_EXPR_LIST

			do 
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_EXPR_TAG(");
				strcpy(pCommand->expressionTag[pCommand->iNumExpressionTags++], token.sVal);

				ASSERT(pTokenizer,pCommand->iNumExpressionTags <= MAX_COMMAND_SETS, "Too many ACMD_EXPR_TAGs specified");

				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_EXPR_TAG(x");
			} while(token.iVal == RW_COMMA);
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_STATIC_CHECK") == 0)
		{
			pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,pCommand->expressionStaticCheckFunc[0] == 0, "Can only have one ACMD_EXPR_STATIC_CHECK");

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_LIST");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_EXPR_STATIC_CHECK(");

			strcpy(pCommand->expressionStaticCheckFunc, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_LIST(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_FUNC_COST") == 0)
		{
			pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,pCommand->iExpressionCost == 0, "Can only specify cost for something once");

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected integer cost. Alex doesn't like floats");
			pCommand->iExpressionCost = token.iVal;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_LIST(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_FUNC_COST_MOVEMENT") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pCommand->iExpressionCost = 3;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_GENERATE_WRAPPER_ONLY") == 0)
		{
			pTokenizer->GetNextToken(&token);
			
			pCommand->iCommandFlags |= COMMAND_FLAG_EXPR_WRAPPER;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_PACKETERRORCALLBACK") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pCommand->iCommandFlags |= COMMAND_FLAG_PACKETERRORCALLBACK;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_PRODUCTS") == 0)
		{
			int i;
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertGetParenthesizedCommaSeparatedList("getting ACMD_PRODUCTS", &pCommand->ppProducts);

			if (eaSize(&pCommand->ppProducts) == 1 && stricmp(pCommand->ppProducts[0], "all") == 0)
			{
				//do nothing, ALL is legal
			}
			else for (i = 0; i < eaSize(&pCommand->ppProducts); i++)
			{
				if (!m_pParent->DoesVariableHaveValue("Products", pCommand->ppProducts[i], false))
				{
					pTokenizer->AssertFailedf("Found unknown product %s in ACMD_PRODUCTS. Legal products are defined in src/core/StructParserVars.txt", pCommand->ppProducts[i]);
				}
			}
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_INTERSHARD") == 0)
		{
			pTokenizer->GetNextToken(&token);

			pCommand->iCommandFlags |= COMMAND_FLAG_INTERSHARD;
		}

		/*
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_TAG") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_TAG");
			
			pCommand->iCommandFlags |= COMMAND_FLAG_ADD_TO_GLOBAL_EXPR_TABLE;
			pCommand->iCommandFlags |= COMMAND_FLAG_EXPR_WRAPPER;

			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Expected identifier after ACMD_EXPR_TAG(");

				int i;

				for(i = 0; i < ARRAYSIZE(pCommand->expressionTags); i++)
				{
					if(!pCommand->expressionTags[i][0])
						strcpy(pCommand->expressionTags[i], token.sVal);
				}

				ASSERT(pTokenizer,i < ARRAYSIZE(pCommand->expressionTags), "Too many sets for one command");
			
				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after ACMD_EXPR_TAG(x");
			} while (token.iVal == RW_COMMA);
		}
		*/
		else
		{
			break;
		}
	}

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Didn't find ; after AUTO_COMMAND");
	int iNumAsterisks = 0;
	
	eType = pTokenizer->CheckNextToken(&token);
	
	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_STATIC_RETURN") == 0)
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_STATIC_RETURN_STRUCT;
		pTokenizer->GetNextToken(&token);
	}
	
	if (eType == TOKEN_IDENTIFIER && (StringBeginsWith(token.sVal, "SA_RET_OP", true) || StringBeginsWith(token.sVal, "SA_ORET_OP", true)))
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_RETVAL_ALLOW_NULL;
		pTokenizer->GetNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && (StringBeginsWith(token.sVal, "SA_RET_NN", true) || StringBeginsWith(token.sVal, "SA_ORET_NN", true)))
	{
		pCommand->iCommandFlags |= COMMAND_FLAG_RETVAL_DONT_ALLOW_NULL;
		pTokenizer->GetNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_SC_TYPE") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_SC_TYPE");

		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
			"Expected identifier or string after ACMD_EXPR_SC_TYPE(");

		strcpy(pCommand->returnStaticCheckParamType, token.sVal);
		pCommand->iReturnStaticCheckCategory = EXPARGCAT_CUSTOM;

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_SC_TYPE(x");

		pTokenizer->GetNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_DICT") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_DICT");

		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
			"Expected identifier or string after ACMD_EXPR_DICT(");

		strcpy(pCommand->returnStaticCheckParamType, token.sVal);
		pCommand->iReturnStaticCheckCategory = EXPARGCAT_REFERENCE;

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_DICT(x");

		pTokenizer->GetNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_RES_DICT") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_RES_DICT");

		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
			"Expected identifier or string after ACMD_EXPR_RES_DICT(");

		strcpy(pCommand->returnStaticCheckParamType, token.sVal);
		pCommand->iReturnStaticCheckCategory = EXPARGCAT_RESOURCE;

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_RES_DICT(x");

		pTokenizer->GetNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_ENUM") == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_ENUM");

		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
			"Expected identifier or string after ACMD_EXPR_ENUM(");

		strcpy(pCommand->returnStaticCheckParamType, token.sVal);
		pCommand->iReturnStaticCheckCategory = EXPARGCAT_ENUM;

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_ENUM(x");

		pTokenizer->GetNextToken(&token);
	}

	//for slow commands, we lie and claim that the string we extracted from inside AUTO_COMMAND_REMOTE_SLOW is the 
	//return type name string, and also verify that the actual return type is void
	if (bIsSlowCommand)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_VOID, "AUTO_COMMAND_REMOTE_SLOW can only be applied to void functions");

		strcpy(pCommand->returnTypeName, slowCommandReturnTypeName);
		iNumAsterisks = slowCommandReturnTypeNumAsterisks;
	}
	else
	{
		while (eType == TOKEN_IDENTIFIER && StringBeginsWith(token.sVal, "SA_P", true)) // skip SA_PRE, SA_POST, SA_PRM, but not SA_RET/SA_ORET
		{
			eType = pTokenizer->GetNextToken(&token);
		}

		//skip over [const][struct]function type
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
		{
			eType = pTokenizer->GetNextToken(&token);
		}
		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_STRUCT)
		{
			eType = pTokenizer->GetNextToken(&token);
		}

		pTokenizer->StringifyToken(&token);
		sprintf_s(pCommand->returnTypeName, sizeof(pCommand->returnTypeName), "%s", token.sVal);

		//skip over any number of *s after function type
		eType = pTokenizer->CheckNextToken(&token);
		while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
		{
			eType = pTokenizer->GetNextToken(&token);
			eType = pTokenizer->CheckNextToken(&token);
			iNumAsterisks++;
		}
	}

	//check if the return value type is one we can deal with
	if (StringIsInList(pCommand->returnTypeName, sSIntNames) && iNumAsterisks == 0)
	{
		pCommand->eReturnType = ARGTYPE_SINT;
	}
	else if (StringIsInList(pCommand->returnTypeName, sUIntNames) && iNumAsterisks == 0)
	{
		pCommand->eReturnType = ARGTYPE_UINT;
	}
	else if (_stricmp(pCommand->returnTypeName, "bool") == 0)
	{
		pCommand->eReturnType = ARGTYPE_BOOL;
	}
	else if (StringIsInList(pCommand->returnTypeName, sFloatNames) && iNumAsterisks == 0)
	{
		pCommand->eReturnType = ARGTYPE_FLOAT;
	}
	else if (StringIsInList(pCommand->returnTypeName, sSInt64Names) && iNumAsterisks == 0)
	{
		pCommand->eReturnType = ARGTYPE_SINT64;
	}
	else if (StringIsInList(pCommand->returnTypeName, sUInt64Names) && iNumAsterisks == 0)
	{
		pCommand->eReturnType = ARGTYPE_UINT64;
	}
	else if (StringIsInList(pCommand->returnTypeName, sFloat64Names) && iNumAsterisks == 0)
	{
		pCommand->eReturnType = ARGTYPE_FLOAT64;
	}
	else if (strcmp(pCommand->returnTypeName, "char") == 0 && iNumAsterisks == 1)
	{
		pCommand->eReturnType = ARGTYPE_STRING;
		sprintf(pCommand->returnTypeName, "char*");
	}
	else if (strcmp(pCommand->returnTypeName, "Vec3") == 0 && iNumAsterisks == 1)
	{
		pCommand->eReturnType = ARGTYPE_VEC3_POINTER;
		sprintf(pCommand->returnTypeName, "Vec3*");
	}
	else if (strcmp(pCommand->returnTypeName, "Vec4") == 0 && iNumAsterisks == 1)
	{
		pCommand->eReturnType = ARGTYPE_VEC4_POINTER;
		sprintf(pCommand->returnTypeName, "Vec4*");
	}
	else if (iNumAsterisks == 1)
	{
		pCommand->eReturnType = ARGTYPE_STRUCT;
	}
	else if (strcmp(pCommand->returnTypeName, "void") == 0)
	{
		pCommand->eReturnType = ARGTYPE_NONE;
	}
	else if (strcmp(pCommand->returnTypeName, "ExprFuncReturnVal") == 0)
	{
		pCommand->eReturnType = ARGTYPE_EXPR_FUNCRETURNVAL;
	}
	else
	{
		pCommand->eReturnType = ARGTYPE_ENUM;
	}	

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH, "Didn't find magic command name after void");

	strcpy(pCommand->functionName, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after magic command name");

	do
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_VOID)
		{
			ASSERT(pTokenizer,eaSize(&pCommand->ppArgs) == 0, "void found in wrong place");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after void");
			break;
		}


		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
		{
			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
		{
			break;
		}

		ASSERT(pTokenizer,eaSize(&pCommand->ppArgs) < MAX_MAGICCOMMAND_ARGS, "Too many args to a magic command");

		MAGIC_COMMAND_ARG *pNewArg = (MAGIC_COMMAND_ARG*)calloc(sizeof(MAGIC_COMMAND_ARG), 1);

		// skip all SA_P* TOKEN_IDENTIFIER's, but detect/make note of any SA_PRE* or SA_PRM* tokens
		// (this code exists in two places, as we have code that has the SA_ tags before and after the autocmd stuff below)
		while (eType == TOKEN_IDENTIFIER && StringBeginsWith(token.sVal, "SA_P", true))
		{
			if (StringBeginsWith(token.sVal, "SA_PRE_NN", true) || StringBeginsWith(token.sVal, "SA_PARAM_NN", true))
			{
				pNewArg->iExpressionArgFlags |= COMMAND_EXPR_FLAG_SA_PRE_NN;
			}
			else if (StringBeginsWith(token.sVal, "SA_PRE_OP", true) || StringBeginsWith(token.sVal, "SA_PARAM_OP", true))
			{
				pNewArg->iExpressionArgFlags |= COMMAND_EXPR_FLAG_SA_PRE_OP;
			}
			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && (strcmp(token.sVal, "ACMD_DEFAULT") == 0 || strcmp(token.sVal, "ACMD_DEF") == 0))
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_DEFAULT");

			eType = pTokenizer->CheckNextToken(&token);
			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NULL") == 0)
			{
				pTokenizer->GetNextToken(&token);
				pNewArg->bHasDefaultInt = true;
				pNewArg->iDefaultInt = 0;
			}
			else
			{
				pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_INT, 0, TOKEN_STRING, MAX_MAGICCOMMAND_ARGDEFAULT_LENGTH-1, "Expected int or string or NULL after ACMD_DEFAULT(");
			

				if (token.eType == TOKEN_INT)
				{
					pNewArg->bHasDefaultInt = true;
					pNewArg->iDefaultInt = token.iVal;
				}
				else
				{
					pNewArg->bHasDefaultString = true;
					strcpy(pNewArg->defaultString, token.sVal);
				}
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_DEFAULT(x");

			eType = pTokenizer->GetNextToken(&token);
		}

		//this check occurs again below
		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_NAMELIST") == 0)
		{
			ASSERTF(pTokenizer, !pNewArg->bHasDefaultInt && !pNewArg->bHasDefaultString, "Arg can't have ACMD_NAMELIST and ACMD_DEF");

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_NAMELIST");
			
			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
				"Expected identifier or string after ACMD_NAMELIST(");

			strcpy(pNewArg->argNameListDataPointerName, token.sVal);

			pNewArg->argNameListDataPointerWasString = (eType == TOKEN_STRING);


			pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected , or ) after ACMD_NAMELIST(x");

			if (token.iVal == RW_RIGHTPARENS)
			{
				strcpy(pNewArg->argNameListTypeName, "NAMELISTTYPE_PREEXISTING");
			}
			else
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH - 20, "Expected identifier after ACMD_NAMELIST(x,");
				sprintf(pNewArg->argNameListTypeName, "NAMELISTTYPE_%s", token.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_NAMELIST(x, y");
			}

			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_SC_TYPE") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_SC_TYPE");

			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
				"Expected identifier or string after ACMD_EXPR_SC_TYPE(");

			strcpy(pNewArg->expressionStaticCheckParamType, token.sVal);
			pNewArg->iExpressionArgCategory = EXPARGCAT_CUSTOM;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_SC_TYPE(x");

			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_DICT") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_DICT");

			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
				"Expected identifier or string after ACMD_EXPR_DICT(");

			strcpy(pNewArg->expressionStaticCheckParamType, token.sVal);
			pNewArg->iExpressionArgCategory = EXPARGCAT_REFERENCE;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_DICT(x");

			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_RES_DICT") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_RES_DICT");

			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
				"Expected identifier or string after ACMD_EXPR_RES_DICT(");

			strcpy(pNewArg->expressionStaticCheckParamType, token.sVal);
			pNewArg->iExpressionArgCategory = EXPARGCAT_RESOURCE;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_RES_DICT(x");

			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_ENUM") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_EXPR_ENUM");

			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
				"Expected identifier or string after ACMD_EXPR_ENUM(");

			strcpy(pNewArg->expressionStaticCheckParamType, token.sVal);
			pNewArg->iExpressionArgCategory = EXPARGCAT_ENUM;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_EXPR_ENUM(x");

			eType = pTokenizer->GetNextToken(&token);
		}

		// skip all SA_P* TOKEN_IDENTIFIER's, but detect/make note of any SA_PRE* or SA_PRM* tokens
		// (this code exists in two places, as we have code that has the SA_ tags before and after the autocmd stuff below)
		while (eType == TOKEN_IDENTIFIER && StringBeginsWith(token.sVal, "SA_P", true))
		{
			if (StringBeginsWith(token.sVal, "SA_PRE_NN", true) || StringBeginsWith(token.sVal, "SA_PARAM_NN", true))
			{
				pNewArg->iExpressionArgFlags |= COMMAND_EXPR_FLAG_SA_PRE_NN;
			}
			else if (StringBeginsWith(token.sVal, "SA_PRE_OP", true) || StringBeginsWith(token.sVal, "SA_PARAM_OP", true))
			{
				pNewArg->iExpressionArgFlags |= COMMAND_EXPR_FLAG_SA_PRE_OP;
			}
			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_EXPR_SELF") == 0)
		{
			ASSERT(pTokenizer, eaSize(&pCommand->ppArgs) == 0, "Expression self pointers have to be the first parameter");
			pNewArg->iExpressionArgFlags |= COMMAND_EXPR_FLAG_SELF_PTR;
			eType = pTokenizer->GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_IGNORE") == 0)
		{
			pNewArg->argType = ARGTYPE_IGNORE;
			sprintf(pNewArg->argName, "ignore%d", eaSize(&pCommand->ppArgs));
			sprintf(pNewArg->argTypeName, "void*");
			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				if (eType == TOKEN_NONE || eType == TOKEN_RESERVEDWORD && (token.iVal == RW_COMMA || token.iVal == RW_RIGHTPARENS))
				{
					break;
				}

				pTokenizer->GetNextToken(&token);
			}
			while (1);
		}
		else

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_POINTER") == 0)
		{
			bool bFoundConst = false;
			bool bFoundNoConst = false;

			pNewArg->argType = ARGTYPE_VOIDSTAR;

			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
			{
				bFoundConst = true;
				pTokenizer->GetNextToken(&token);
			}

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "NOCONST") == 0)
			{
				bFoundNoConst = true;
				pTokenizer->GetNextToken(&token);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NO_CONST");
			}
	

			pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMAND_ARGTYPE_NAME_LENGTH - 5,
				TOKEN_RESERVEDWORD, RW_VOID,
				"Expected argument type or void after ACMD_POINTER");

			pTokenizer->StringifyToken(&token);

			sprintf(pNewArg->argTypeName, "%s%s", bFoundConst ? "const " : "", token.sVal);

			if (bFoundNoConst)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NO_CONST(x");
			}


			eType = pTokenizer->GetNextToken(&token);

			while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
			{
				strcat(pNewArg->argTypeName, "*");
				eType = pTokenizer->GetNextToken(&token);
			}

			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER && token.iVal < MAX_MAGICCOMMAND_ARGNAME_LENGTH, "Expected argument name after ACMD_POINTER argType");

			strcpy(pNewArg->argName, token.sVal);
		}
		else
		{
			char forceTypeName[256] = "";

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_FORCETYPE") == 0)
			{
				Token tempToken;
				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_FORCETYPE");
				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_IDENTIFIER, sizeof(forceTypeName), "Expected identifier after ACMD_FORCETYPE(");
				strcpy(forceTypeName, tempToken.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_FORCETYPE(x");
				eType = pTokenizer->GetNextToken(&token);
			}

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_OWNABLE") == 0)
			{
				Token tempToken;
				
				CommandAssert(pCommand, bIsRemoteCommand && pCommand->eReturnType == ARGTYPE_NONE, "ACMD_OWNABLE only allowed for non-returning remote commands");

				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_OWNABLE");
				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_IDENTIFIER, sizeof(forceTypeName), "Expected identifier after ACMD_FORCETYPE(");
				strcpy(forceTypeName, tempToken.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_OWNABLE(x");
				pNewArg->argFlags |= ARGFLAG_OWNABLE;
			}
			else
			{
				ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER, "Expected magic command arg type");
			}

			if (strcmp(token.sVal, "const") == 0)
			{
				Token tempToken;
				pTokenizer->AssertNextTokenTypeAndGet(&tempToken, TOKEN_IDENTIFIER, 0, "Expected magic command arg type after const");

				sprintf(token.sVal, "const %s", tempToken.sVal);
			}
				

			pNewArg->argType = GetArgTypeFromArgTypeName(forceTypeName[0] ? forceTypeName : token.sVal);
			sprintf_s(pNewArg->argTypeName, sizeof(pNewArg->argTypeName),
				"%s", forceTypeName[0] ? forceTypeName : token.sVal);

			ASSERT(pTokenizer,pNewArg->argType != ARGTYPE_NONE, "Unknown arg type for magic command");

			if (pNewArg->argType == ARGTYPE_SLOWCOMMANDID)
			{
				sprintf(pNewArg->argTypeName, "%s", token.sVal);
			}
			else if (pNewArg->argType >= ARGTYPE_FIRST_SPECIAL)
			{
				sprintf(pNewArg->argTypeName, "%s*", token.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "expected * after Entity/Cmd/CmdContext");
				ASSERT(pTokenizer,pCommand->expressionTag[0][0] || !CommandHasArgOfType(pCommand, pNewArg->argType), "Only one each Entity/Cmd/Context arg allowed per AUTO_COMMAND");
				if (pNewArg->argType == ARGTYPE_ENTITY)
				{
					pCommand->iCommandFlags |= COMMAND_FLAG_PASSENTITY;
				}
			}
			else if (pNewArg->argType == ARGTYPE_PACKET)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "expected * after Packet");
			}
			else if (pNewArg->argType == ARGTYPE_EXPR_CMULTIVAL)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "expected * after CMultiVal");
			}
			else if (pNewArg->argType == ARGTYPE_STRING)
			{
				Token nextToken;

				eType = pTokenizer->CheckNextToken(&nextToken);

				if (eType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_ASTERISK)
				{
					sprintf(pNewArg->argTypeName, "%s*", token.sVal);
					pTokenizer->GetNextToken(&token);
				}
				else
				{
					sprintf(pNewArg->argTypeName, token.sVal);
					pNewArg->argType = ARGTYPE_SINT;
				}
			}
			else if (pNewArg->argType == ARGTYPE_VEC3_DIRECT)
			{
				Token nextToken;
				enumTokenType eType;
				eType = pTokenizer->CheckNextToken(&nextToken);

				if (eType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_ASTERISK)
				{
					pNewArg->argType = ARGTYPE_VEC3_POINTER;
					pTokenizer->GetNextToken(&nextToken);
				}
			
				sprintf(pNewArg->argTypeName, "%s", token.sVal);
			}
			else if (pNewArg->argType == ARGTYPE_VEC4_DIRECT)
			{
				Token nextToken;
				enumTokenType eType;
				eType = pTokenizer->CheckNextToken(&nextToken);

				if (eType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_ASTERISK)
				{
					pNewArg->argType = ARGTYPE_VEC4_POINTER;
					pTokenizer->GetNextToken(&nextToken);
				}
			
				sprintf(pNewArg->argTypeName, "%s", token.sVal);
			}
			else if (pNewArg->argType == ARGTYPE_MAT4_DIRECT)
			{
				Token nextToken;
				enumTokenType eType;
				eType = pTokenizer->CheckNextToken(&nextToken);

				if (eType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_ASTERISK)
				{
					pNewArg->argType = ARGTYPE_MAT4_POINTER;
					pTokenizer->GetNextToken(&nextToken);
				}
			
				sprintf(pNewArg->argTypeName, "%s", token.sVal);
			}
			else if (pNewArg->argType == ARGTYPE_QUAT_DIRECT)
			{
				Token nextToken;
				enumTokenType eType;
				eType = pTokenizer->CheckNextToken(&nextToken);

				if (eType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_ASTERISK)
				{
					pNewArg->argType = ARGTYPE_QUAT_POINTER;
					pTokenizer->GetNextToken(&nextToken);
				}
			
				sprintf(pNewArg->argTypeName, "%s", token.sVal);
			}
			else if (pNewArg->argType == ARGTYPE_STRUCT && !(pNewArg->argFlags & ARGFLAG_OWNABLE))
			{
				strcpy(pNewArg->argTypeName, token.sVal);

				eType = pTokenizer->CheckNextToken(&token);

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK)
				{
					pTokenizer->GetNextToken(&token);
				}
				else
				{
					pNewArg->argType = ARGTYPE_ENUM;
				}
			}
			else if (pNewArg->argType == ARGTYPE_EXPR_EXPRCONTEXT)
			{
				for(int i = 0; i < eaSize(&pCommand->ppArgs); i++)
				{
					ASSERT(pTokenizer,pCommand->ppArgs[i]->argType == ARGTYPE_ENTITY 
						&& pCommand->ppArgs[i]->iExpressionArgFlags & COMMAND_EXPR_FLAG_SELF_PTR,
						"Only allowed to have a self pointer before an Expression Context for expression functions");
				}
				strcpy(pNewArg->argTypeName, "ExprContext*");
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "Expected * after ExprContext");
			}
			else if (pNewArg->argType == ARGTYPE_EXPR_PARTITION)
			{
				for(int i = 0; i < eaSize(&pCommand->ppArgs); i++)
				{
					ASSERT(pTokenizer,pCommand->ppArgs[i]->argType == ARGTYPE_ENTITY && pCommand->ppArgs[i]->iExpressionArgFlags & COMMAND_EXPR_FLAG_SELF_PTR ||
						pCommand->ppArgs[i]->argType == ARGTYPE_EXPR_EXPRCONTEXT,
						"Only allowed to have self pointers and expression contexts before a partition for expression functions");
				}
				strcpy(pNewArg->argTypeName, "ACMD_EXPR_PARTITION");
			}
			else if (pNewArg->argType >= ARGTYPE_EXPR_OUT_FIRST && pNewArg->argType <= ARGTYPE_EXPR_LAST)
			{
				for(int i = 0; i < eaSize(&pCommand->ppArgs); i++)
				{
					ASSERT(pTokenizer,(pCommand->ppArgs[i]->argType == ARGTYPE_ENTITY && pCommand->ppArgs[i]->iExpressionArgFlags & COMMAND_EXPR_FLAG_SELF_PTR) ||
						pCommand->ppArgs[i]->argType == ARGTYPE_EXPR_EXPRCONTEXT ||
						pCommand->ppArgs[i]->argType == ARGTYPE_EXPR_PARTITION ||
						pCommand->ppArgs[i]->argType >= ARGTYPE_EXPR_OUT_FIRST && pCommand->ppArgs[i]->argType <= ARGTYPE_EXPR_LAST,
						"Only allowed to have self pointers, partitions and Expression Contexts before function return values for expression functions");
				}
			}

			if(pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER &&
				pNewArg->argType != ARGTYPE_EXPR_ERRSTRING &&
				pNewArg->argType != ARGTYPE_EXPR_ERRSTRING_STATIC)
			{
				for(int i = 0; i < eaSize(&pCommand->ppArgs); i++)
				{
					ASSERT(pTokenizer,pCommand->ppArgs[i]->argType != ARGTYPE_EXPR_ERRSTRING && pCommand->ppArgs[i]->argType != ARGTYPE_EXPR_ERRSTRING_STATIC,
						"Error strings have to be the last parameter of an expression function");
				}
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMAND_ARGNAME_LENGTH, "Expected arg name after arg type");
			strcpy(pNewArg->argName, token.sVal);
		}
	
		//ACMD_NAMELIST is legal after the argument in addition to before it (if you change this, change the 
		//earlier check as well)
		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ACMD_NAMELIST") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ACMD_NAMELIST");
			
			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,(eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && token.iVal < MAX_MAGICCOMMANDNAMELENGTH,
				"Expected identifier or string after ACMD_NAMELIST(");

			strcpy(pNewArg->argNameListDataPointerName, token.sVal);

			pNewArg->argNameListDataPointerWasString = (eType == TOKEN_STRING);


			pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected , or ) after ACMD_NAMELIST(x");

			if (token.iVal == RW_RIGHTPARENS)
			{
				strcpy(pNewArg->argNameListTypeName, "NAMELISTTYPE_PREEXISTING");
			}
			else
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_MAGICCOMMANDNAMELENGTH - 20, "Expected identifier after ACMD_NAMELIST(x,");
				sprintf(pNewArg->argNameListTypeName, "NAMELISTTYPE_%s", token.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after ACMD_NAMELIST(x, y");
			}
		}

		eaPush(&pCommand->ppArgs, pNewArg);
	}
	while (1);

	if (pCommand->commandName[0] == 0)
	{
		strcpy(pCommand->commandName, pCommand->functionName);
		strcpy(pCommand->safeCommandName,pCommand->functionName);
	}

	if (pCommand->commandCategories[0][0] == 0 && pCommand->iNumExpressionTags)
	{
		int i;
		for (i=0; i < pCommand->iNumExpressionTags; i++)
			AddCommandToCategory(pCommand, pCommand->expressionTag[i], pTokenizer);
	}

	if ( bIsExprFunc )
	{
		CommandAssert(pCommand, pCommand->iNumExpressionTags > 0, "AUTO_EXPR_FUNC used for a function with no EXPR tags");
	}

	FixupCommandTypes(pTokenizer, pCommand);
	VerifyCommandValidity(pTokenizer, pCommand);

}

void MagicCommandManager::FixupCommandTypes(Tokenizer *pTokenizer, MAGIC_COMMAND_STRUCT *pCommand)
{
	if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
	{
		int i;

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			if (pCommand->ppArgs[i]->argType == ARGTYPE_STRING || pCommand->ppArgs[i]->argType == ARGTYPE_SENTENCE)
			{
				pCommand->ppArgs[i]->argType = ARGTYPE_ESCAPEDSTRING;
			}
		}
	}
}


void MagicCommandManager::VerifyCommandValidity(Tokenizer *pTokenizer, MAGIC_COMMAND_STRUCT *pCommand)
{
	int i;

	bool bFoundASentence = false;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argFlags & ARGFLAG_OWNABLE)
		{
			CommandAssert(pCommand, pArg->argType == ARGTYPE_STRUCT, "ACMD_OWNABLE allowed only for structs");
		}

		if (bFoundASentence)
		{
			if (pArg->argType < ARGTYPE_FIRST_SPECIAL)
			{
				ASSERT(pTokenizer,0, "An AUTO_COMMAND can only have one SENTENCE, and only as its last normal argument");
			}
		}
		else
		{
			if (pArg->argType == ARGTYPE_SENTENCE)
			{
				bFoundASentence = true;
			}
		}
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
	{
		ASSERT(pTokenizer,CountArgsOfType(pCommand, ARGTYPE_SLOWCOMMANDID) == 1, "Slow commands must have one SlowRemoteCommandID arg");
	}
	else
	{
		ASSERT(pTokenizer,CountArgsOfType(pCommand, ARGTYPE_SLOWCOMMANDID) == 0, "non-slow commands can not have a SlowRemoteCommandID arg");
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
	{
		ASSERT(pTokenizer,pCommand->commandSets[0][0] == 0, "Remote commands can not have a command set");

//		strcpy(pCommand->commandSets[0], CountArgsOfType(pCommand, ARGTYPE_SLOWCOMMANDID) == 1 ? "SlowRemote" : "Remote");

/*		for (i = ARGTYPE_FIRST_SPECIAL; i < ARGTYPE_LAST; i++)
		{
			if (i != ARGTYPE_SLOWCOMMANDID)
			{
				ASSERT(pTokenizer,CountArgsOfType(pCommand, (enumMagicCommandArgType)i) == 0, "remote commands can not have \"special\" args");
			}
		}*/
	}
	else
	{
		ASSERT(pTokenizer,CountArgsOfType(pCommand, ARGTYPE_SLOWCOMMANDID) == 0, "non-Remote commands can not have a SlowRemoteCommandID arg");

	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
	{
		int i;

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

			ASSERT(pTokenizer,pArg->argType < ARGTYPE_FIRST_SPECIAL, "Queued commands can't have special args");
			ASSERT(pTokenizer,pArg->argType != ARGTYPE_STRUCT, "Queued commands can't have struct args");
		}
			
		ASSERT(pTokenizer,pCommand->eReturnType == ARGTYPE_NONE, "Queued commands can't have return types (for now)");
		ASSERT(pTokenizer,pCommand->commandSets[0][0] == 0 && pCommand->commandCategories[0][0] == 0, "Queued commands can't have sets or categories");
		ASSERT(pTokenizer,strcmp(pCommand->commandName, pCommand->functionName) == 0, "Queued commands can't have names");
		ASSERT(pTokenizer,pCommand->commandsWhichThisIsTheErrorFunctionFor[0][0] == 0, "Queued commands can't be error functions");
		
	}
	else
	{
		ASSERT(pTokenizer,CountArgsOfType(pCommand, ARGTYPE_VOIDSTAR) == 0 || pCommand->iNumExpressionTags || (pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER), "NonQueued commands can't have ACMD_POINTER");
//		ASSERT(pTokenizer,CountArgsOfType(pCommand, ARGTYPE_QUAT_POINTER) == 0, "NonQueued commands can't have quat args");
//		ASSERT(pTokenizer,CountArgsOfType(pCommand, ARGTYPE_QUAT_DIRECT) == 0, "NonQueued commands can't have quat args");
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER)
	{
		int iNumOutArgs = 0;

		switch (pCommand->eReturnType)
		{
		case ARGTYPE_NONE:
		case ARGTYPE_EXPR_FUNCRETURNVAL:
			//fine, do nothing
			break;

		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_BOOL:
		case ARGTYPE_FLOAT:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
		case ARGTYPE_FLOAT64:
		case ARGTYPE_STRING:
		case ARGTYPE_STRUCT:
		case ARGTYPE_ENUM:
			//count as a return value
			iNumOutArgs++;
			break;

		default:
			ASSERT(pTokenizer,0, "Invalid return val type from expression list command");
			break;
		}

		int i;

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			if (pCommand->ppArgs[i]->argType >= ARGTYPE_EXPR_OUT_FIRST && pCommand->ppArgs[i]->argType <= ARGTYPE_EXPR_LAST)
			{
				iNumOutArgs++;
			}
		}

		ASSERT(pTokenizer,iNumOutArgs <= 1, "Too many _OUT args");

	}
	else
	{
		ASSERT(pTokenizer,!CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand), "non-expression-list commands can not have expression-specific arg or return types");
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
	{
		ASSERT(pTokenizer,pCommand->iAccessLevel == 0 && pCommand->serverSpecificAccessLevel_ServerName[0] == 0, "EARLYCOMMANDLINE functions must be access level 0");
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_PACKETERRORCALLBACK)
	{
		ASSERT(pTokenizer, (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE) 
			&& pCommand->eReturnType == ARGTYPE_NONE
			&& !(pCommand->iCommandFlags & COMMAND_FLAG_MULTIPLE_RECIPIENTS), "PACKETERRORCALLBACK only legal on no-return-val remote commands without multiple recipients");
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_INTERSHARD)
	{
		ASSERT(pTokenizer, (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE) 
			&& !(pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
			&& pCommand->eReturnType == ARGTYPE_NONE
			&& !(pCommand->iCommandFlags & COMMAND_FLAG_MULTIPLE_RECIPIENTS), "INTERSHARD only legal on no-return-val remote commands without multiple recipients");
	}
}











char *MagicCommandManager::GetArgDescriptionBlock(enumMagicCommandArgType eArgType, 
		char *pArgName, char *pTypeName,
		char *pNameListDataPointer, char *pNameListType, bool bDataPointerWasString,
		bool bHasDefaultInt, int iDefaultInt, char *pDefaultString)
{
	char tempString1[1024];
	static char tempString2[1024];

	char *pDefaultFlagSnippet = (bHasDefaultInt || pDefaultString) ? " | CMDAF_HAS_DEFAULT" : "";

	switch(eArgType)
	{
	case ARGTYPE_ENUM:
	case ARGTYPE_SINT:
		sprintf(tempString1, "{\"%s\", MULTI_INT,0, sizeof(S32), CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_UINT:
	case ARGTYPE_BOOL:
		sprintf(tempString1, "{\"%s\", MULTI_INT,0, sizeof(U32), CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_FLOAT:
		sprintf(tempString1, "{\"%s\", MULTI_FLOAT, 0, sizeof(F32), CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_SINT64:
		sprintf(tempString1, "{\"%s\", MULTI_INT,0, sizeof(S64), CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_UINT64:
		sprintf(tempString1, "{\"%s\", MULTI_INT,0, sizeof(U64), CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_FLOAT64:
		sprintf(tempString1, "{\"%s\", MULTI_FLOAT, 0, sizeof(F64), CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;


	case ARGTYPE_STRING:
		sprintf(tempString1, "{\"%s\", MULTI_STRING, 0, 0, CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_SENTENCE:
		sprintf(tempString1, "{\"%s\", MULTI_STRING, 0, 0, CMDAF_SENTENCE|CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_ESCAPEDSTRING:
		sprintf(tempString1, "{\"%s\", MULTI_STRING, 0, 0, CMDAF_ESCAPEDSTRING|CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_VEC3_POINTER:
	case ARGTYPE_VEC3_DIRECT:
		sprintf(tempString1, "{\"%s\", MULTI_VEC3,0,0,CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_VEC4_POINTER:
	case ARGTYPE_VEC4_DIRECT:
		sprintf(tempString1, "{\"%s\", MULTI_VEC4,0,0,CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_MAT4_POINTER:
	case ARGTYPE_MAT4_DIRECT:
		sprintf(tempString1, "{\"%s\", MULTI_MAT4,0,0,CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_QUAT_POINTER:
	case ARGTYPE_QUAT_DIRECT:
		sprintf(tempString1, "{\"%s\", MULTI_QUAT,0,0,CMDAF_ALLOCATED%s, 0, ", pArgName, pDefaultFlagSnippet);
		break;

	case ARGTYPE_STRUCT:
		sprintf(tempString1, "{\"%s\", MULTI_NP_POINTER, parse_%s, 0, CMDAF_ALLOCATED|CMDAF_TEXTPARSER%s, 0, ", pArgName, pTypeName, pDefaultFlagSnippet);
		break;

	default:
		return "WHAT THE? THIS SHOULD NOT BE!!!";
	
	}

	char nameListDataPointerString[256];

	if (pNameListDataPointer && pNameListDataPointer[0])
	{
		if (bDataPointerWasString)
		{
			sprintf(nameListDataPointerString, "(void**)\"%s\"", pNameListDataPointer);
		}
		else if (strcmp(pNameListType, "NAMELISTTYPE_COMMANDLIST") == 0 || strcmp(pNameListType, "NAMELISTTYPE_PREEXISTING") == 0)
		{
			sprintf(nameListDataPointerString, "(void**)&%s", pNameListDataPointer);
		}
		else	
		{
			sprintf(nameListDataPointerString, "&%s", pNameListDataPointer);
		}
	}
	else if (bHasDefaultInt)
	{
		sprintf(nameListDataPointerString, "(void**)((intptr_t)%d)", iDefaultInt);
	}
	else if (pDefaultString)
	{
		sprintf(nameListDataPointerString, "(void**)(\"%s\")", pDefaultString);
	}
	else
	{
		sprintf(nameListDataPointerString, "NULL");
	}

	sprintf(tempString2, "%s %s, %s }",
		tempString1, 
		pNameListType && pNameListType[0] ? pNameListType : "NAMELISTTYPE_NONE",
		nameListDataPointerString);

	return tempString2;


}




MagicCommandManager::enumMagicCommandArgType MagicCommandManager::GetArgTypeFromArgTypeName(char *pArgTypeName)
{
	if (StringIsInList(pArgTypeName, sSentenceNames))
	{
		return ARGTYPE_SENTENCE;
	}

	if (StringIsInList(pArgTypeName, sStringNames))
	{
		return ARGTYPE_STRING;
	}

	if (StringIsInList(pArgTypeName, sSIntNames))
	{
		return ARGTYPE_SINT;
	}

	if (StringIsInList(pArgTypeName, sUIntNames))
	{
		return ARGTYPE_UINT;
	}

	if (_stricmp(pArgTypeName, "bool") == 0)
	{
		return ARGTYPE_BOOL;
	}

	if (StringIsInList(pArgTypeName, sFloatNames))
	{
		return ARGTYPE_FLOAT;
	}

	if (StringIsInList(pArgTypeName, sSInt64Names))
	{
		return ARGTYPE_SINT64;
	}

	if (StringIsInList(pArgTypeName, sUInt64Names))
	{
		return ARGTYPE_UINT64;
	}

	if (StringIsInList(pArgTypeName, sFloat64Names))
	{
		return ARGTYPE_FLOAT64;
	}

	if (StringIsInList(pArgTypeName, sVec3Names))
	{
		return ARGTYPE_VEC3_DIRECT;
	}

	if (StringIsInList(pArgTypeName, sVec4Names))
	{
		return ARGTYPE_VEC4_DIRECT;
	}

	if (StringIsInList(pArgTypeName, sMat4Names))
	{
		return ARGTYPE_MAT4_DIRECT;
	}

	if (StringIsInList(pArgTypeName, sQuatNames))
	{
		return ARGTYPE_QUAT_DIRECT;
	}

	if (strcmp(pArgTypeName, "Entity") == 0)
	{
		return ARGTYPE_ENTITY;
	}

	if (strcmp(pArgTypeName, "GenericServerCmdData") == 0)
	{
		return ARGTYPE_GENERICSERVERCMDDATA;
	}

	if (strcmp(pArgTypeName, "TransactionCommand") == 0)
	{
		return ARGTYPE_TRANSACTIONCOMMAND;
	}

	if (strcmp(pArgTypeName, "Cmd") == 0)
	{
		return ARGTYPE_CMD;
	}

	if (strcmp(pArgTypeName, "CmdContext") == 0)
	{
		return ARGTYPE_CMDCONTEXT;
	}

	if (strcmp(pArgTypeName, "SlowRemoteCommandID") == 0)
	{
		return ARGTYPE_SLOWCOMMANDID;
	}

	if (strcmp(pArgTypeName, "ExprFuncReturnVal") == 0)
	{
		return ARGTYPE_EXPR_FUNCRETURNVAL;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_SUBEXPR_IN") == 0)
	{
		return ARGTYPE_EXPR_SUBEXPR_IN;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_ENTARRAY_IN") == 0)
	{
		return ARGTYPE_EXPR_ENTARRAY_IN;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_INT_OUT") == 0)
	{
		return ARGTYPE_EXPR_INT_OUT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_FLOAT_OUT") == 0)
	{
		return ARGTYPE_EXPR_FLOAT_OUT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_STRING_OUT") == 0)
	{
		return ARGTYPE_EXPR_STRING_OUT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_ERRSTRING") == 0)
	{
		return ARGTYPE_EXPR_ERRSTRING;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_ERRSTRING_STATIC") == 0)
	{
		return ARGTYPE_EXPR_ERRSTRING_STATIC;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_ENTARRAY_IN_OUT") == 0)
	{
		return ARGTYPE_EXPR_ENTARRAY_IN_OUT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_ENTARRAY_OUT") == 0)
	{
		return ARGTYPE_EXPR_ENTARRAY_OUT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_LOC_MAT4_IN") == 0)
	{
		return ARGTYPE_EXPR_LOC_MAT4_IN;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_LOC_MAT4_OUT") == 0)
	{
		return ARGTYPE_EXPR_LOC_MAT4_OUT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_VEC4_OUT") == 0)
	{
		return ARGTYPE_EXPR_VEC4_OUT;
	}

	if (strcmp(pArgTypeName, "ExprContext") == 0)
	{
		return ARGTYPE_EXPR_EXPRCONTEXT;
	}

	if (strcmp(pArgTypeName, "ACMD_EXPR_PARTITION") == 0)
	{
		return ARGTYPE_EXPR_PARTITION;
	}

	if (strcmp(pArgTypeName, "CMultiVal") == 0)
	{
		return ARGTYPE_EXPR_CMULTIVAL;
	}

	if (strcmp(pArgTypeName, "Packet") == 0)
	{
		return ARGTYPE_PACKET;
	}

	return ARGTYPE_STRUCT;
}



//returns number of dependencies found
int MagicCommandManager::ProcessDataSingleFile(char *pSourceFileName, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE])
{
	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (strcmp(pCommand->sourceFileName, pSourceFileName) == 0)
		{
			int i;

			if (pCommand->eReturnType == ARGTYPE_ENUM)
			{
				if (m_pParent->GetDictionary()->FindIdentifier(pCommand->returnTypeName) == IDENTIFIER_ENUM)
				{
					
				}
				else
				{
					CommandAssert(pCommand, 0, "Presumed enum type name not recognized");
				}			
			}


			for (i=0; i < eaSize(&pCommand->ppArgs); i++)
			{
				if (pCommand->ppArgs[i]->argType == ARGTYPE_ENUM)
				{
					if (m_pParent->GetDictionary()->FindIdentifier(pCommand->ppArgs[i]->argTypeName) == IDENTIFIER_ENUM)
					{
						
					}
					else
					{
						CommandAssert(pCommand, 0, "Presumed enum type name not recognized");
					}
				}
			}
		}
	}



	return 0;
}


//used for set_ and get_ function prototypes for AUTO_CMD_INTs and such, for which the
//actual type name is not available
char *MagicCommandManager::GetGenericTypeNameFromType(enumMagicCommandArgType eType)
{
	switch (eType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_UINT:
	case ARGTYPE_SINT64:
	case ARGTYPE_UINT64:
	case ARGTYPE_BOOL:
		return "__int64";

	case ARGTYPE_FLOAT:
	case ARGTYPE_FLOAT64:
		return "F64";

	case ARGTYPE_STRING:
	case ARGTYPE_SENTENCE:
		return "char*";
	}

	return "UNKNOWN TYPE HELP!!!!";
}



void MagicCommandManager::WriteCompleteTestClientPrototype(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	//fprintf(pOutFile, "//Wrapper function for %s\n", pCommand->commandName);
	
	fprintf(pOutFile, "CMD_DECLSPEC U32 cmd_%s(", pCommand->safeCommandName);

	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false, false);

	fprintf(pOutFile, ")");
}

// DEPRECATED - VAS 02/17/10
// void MagicCommandManager::WriteCompleteTestClientPrototype_Blocking(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which blocks until the command returns\n",
// 		pCommand->commandName);
// 	
// 	fprintf(pOutFile, "CMD_DECLSPEC enumTestClientCmdOutcome cmd_block_%s(char **ppCmdRetString", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, true);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_Struct(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which puts the return value into a struct which\n//can be polled and must be released via TestClientReleaseResults\n",
// 		pCommand->commandName);
// 	
// 	fprintf(pOutFile, "CMD_DECLSPEC TestClientCmdHandle cmd_retStruct_%s(", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_Callback(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which calls a callback when the command returns\n",
// 			pCommand->commandName);
// 		
// 	fprintf(pOutFile, "CMD_DECLSPEC TestClientCmdHandle cmd_retCB_%s(TestClientCallback *pCallBack, void *pUserData", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, true);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_UserBuff(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which puts the return value into a user-supplied\n//buffer which can be polled\n",
// 		pCommand->commandName);
// 	
// 	fprintf(pOutFile, "CMD_DECLSPEC void cmd_retUserStruct_%s(TestClientCmdResultStruct *pUserResultStruct", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, true);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_IntReturn(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which returns an int\n",
// 		pCommand->commandName);
// 
// 	fprintf(pOutFile, "CMD_DECLSPEC %s cmd_%s(", pCommand->returnTypeName, pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_Vec3Return(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which returns an vec3*\n",
// 		pCommand->commandName);
// 
// 	fprintf(pOutFile, "CMD_DECLSPEC Vec3 *cmd_%s(", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_Vec4Return(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which returns an vec4*\n",
// 		pCommand->commandName);
// 
// 	fprintf(pOutFile, "CMD_DECLSPEC Vec4 *cmd_%s(", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_FloatReturn(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which returns a float\n",
// 		pCommand->commandName);
// 
// 	fprintf(pOutFile, "CMD_DECLSPEC float cmd_%s(", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_StructReturn(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which returns a %s pointer\n",
// 		pCommand->commandName, pCommand->returnTypeName);
// 
// 	fprintf(pOutFile, "CMD_DECLSPEC %s *cmd_%s(", pCommand->returnTypeName, pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }
// 
// void MagicCommandManager::WriteCompleteTestClientPrototype_StringReturn(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
// {
// 	fprintf(pOutFile, "//Wrapper function for %s which returns a string\n",
// 		pCommand->commandName);
// 
// 	fprintf(pOutFile, "CMD_DECLSPEC char *cmd_%s(", pCommand->safeCommandName);
// 
// 	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false);
// 
// 	fprintf(pOutFile, ")");
// }

void MagicCommandManager::WriteOutPrototypesForTestClient(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, int iPrefixLen)
{

	fprintf(pOutFile, "\n\n//////-----------------  PROTOTYPE FOR COMMAND %s\n", pCommand->commandName);

	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	WriteCompleteTestClientPrototype(pOutFile, pCommand);
	fprintf(pOutFile, ";\n");

//	DEPRECATED - VAS 02/17/10
// 	if (pCommand->eReturnType != ARGTYPE_NONE)
// 	{
// 		WriteCompleteTestClientPrototype_Blocking(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 		WriteCompleteTestClientPrototype_Struct(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 		WriteCompleteTestClientPrototype_Callback(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 		WriteCompleteTestClientPrototype_UserBuff(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_SINT || pCommand->eReturnType == ARGTYPE_UINT
// 		|| pCommand->eReturnType == ARGTYPE_SINT64 || pCommand->eReturnType == ARGTYPE_UINT64
// 		|| pCommand->eReturnType == ARGTYPE_BOOL)
// 	{
// 		WriteCompleteTestClientPrototype_IntReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_FLOAT || pCommand->eReturnType == ARGTYPE_FLOAT64)
// 	{
// 		WriteCompleteTestClientPrototype_FloatReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_VEC3_POINTER)
// 	{
// 		WriteCompleteTestClientPrototype_Vec3Return(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_VEC4_POINTER)
// 	{
// 		WriteCompleteTestClientPrototype_Vec4Return(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_STRUCT)
// 	{
// 		WriteCompleteTestClientPrototype_StructReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_STRING)
// 	{
// 		WriteCompleteTestClientPrototype_StringReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, ";\n");
// 	}
}




void MagicCommandManager::WriteOutSharedFunctionBody(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, int iPrefixLen)
{

	fprintf(pOutFile, "\tchar *pAUTOGENWorkString = NULL;\n\testrStackCreate(&pAUTOGENWorkString);\n\testrCopy2(&pAUTOGENWorkString, \"%s \");\n", pCommand->commandName + iPrefixLen);

	WriteOutGenericCodeToPutArgumentsIntoEString(pOutFile, pCommand, "&pAUTOGENWorkString", false, false);
}

void MagicCommandManager::WriteOutFunctionBodiesForTestClient(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, int iPrefixLen)
{
	char commandDefineName[MAX_MAGICCOMMANDNAMELENGTH + 30];
	sprintf(commandDefineName, "_DEFINED_%s", pCommand->safeCommandName);
	MakeStringUpcase(commandDefineName);

	fprintf(pOutFile, "\n//////-----------------  WRAPPER FUNCTION FOR COMMAND %s (from %s(%d))\n", pCommand->commandName,
		pCommand->sourceFileName, pCommand->iLineNum);

	fprintf(pOutFile, "#ifndef %s\n#define %s\n",
		commandDefineName, commandDefineName);
	
	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	WriteCompleteTestClientPrototype(pOutFile, pCommand);
	fprintf(pOutFile, "\n{\n\tU32 iAUTOGENReturnId;\n");
	WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
	fprintf(pOutFile, "\tiAUTOGENReturnId = ClientController_SendCommandToClient(pAUTOGENWorkString);\n\testrDestroy(&pAUTOGENWorkString);\n\treturn iAUTOGENReturnId;\n}\n");

//	DEPRECATED - VAS 02/17/10
// 	if (pCommand->eReturnType != ARGTYPE_NONE)
// 	{
// 		WriteCompleteTestClientPrototype_Blocking(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\tenumTestClientCmdOutcome eRetVal;\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teRetVal = SendCommandToClient_Blocking(pAUTOGENWorkString, ppCmdRetString);\n\testrDestroy(&pAUTOGENWorkString);\n\treturn eRetVal;\n}\n");
// 
// 		WriteCompleteTestClientPrototype_Struct(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\tTestClientCmdHandle retVal;\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\tSendCommandToClient_NonBlocking(pAUTOGENWorkString, TESTCLIENTCMDTYPE_BUFFER, &retVal, NULL, NULL, NULL);\n\testrDestroy(&pAUTOGENWorkString);\n\treturn retVal;\n}\n");
// 
// 		WriteCompleteTestClientPrototype_Callback(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\tTestClientCmdHandle retVal;\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\tSendCommandToClient_NonBlocking(pAUTOGENWorkString, TESTCLIENTCMDTYPE_CALLBACK, &retVal, NULL, pCallBack, pUserData);\n\testrDestroy(&pAUTOGENWorkString);\n\treturn retVal;\n}\n");
// 
// 		WriteCompleteTestClientPrototype_UserBuff(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\tSendCommandToClient_NonBlocking(pAUTOGENWorkString, TESTCLIENTCMDTYPE_USERBUFFER, NULL, pUserResultStruct, NULL, NULL);\n\testrDestroy(&pAUTOGENWorkString);\n}\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_SINT || pCommand->eReturnType == ARGTYPE_UINT
// 		|| pCommand->eReturnType == ARGTYPE_SINT64 || pCommand->eReturnType == ARGTYPE_UINT64
// 		|| pCommand->eReturnType == ARGTYPE_BOOL)
// 	{
// 		WriteCompleteTestClientPrototype_IntReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\t__int64 iRetVal;\n\tenumTestClientCmdOutcome eOutCome;\n\tchar returnBuf[128];\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teOutCome = SendCommandToClient_Blocking_FixedString(pAUTOGENWorkString, returnBuf, 128);\n");
// 		fprintf(pOutFile, "\tassert(eOutCome == TESTCLIENT_CMD_SUCCEEDED);\n");
// 		fprintf(pOutFile, "\tsscanf(returnBuf, \"%%\"FORM_LL\"d\", &iRetVal);\n");
// 		fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\treturn iRetVal;\n}\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_VEC3_POINTER)
// 	{
// 		WriteCompleteTestClientPrototype_Vec3Return(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\tstatic Vec3 vRetVal;\n\tenumTestClientCmdOutcome eOutCome;\n\tchar returnBuf[128];\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teOutCome = SendCommandToClient_Blocking_FixedString(pAUTOGENWorkString, returnBuf, 128);\n");
// 		fprintf(pOutFile, "\tassert(eOutCome == TESTCLIENT_CMD_SUCCEEDED);\n");
// 		fprintf(pOutFile, "\tsscanf(returnBuf, \"%%f %%f %%f\", &vRetVal[0], &vRetVal[1], &vRetVal[2]);\n");
// 		fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\treturn &vRetVal;\n}\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_VEC4_POINTER)
// 	{
// 		WriteCompleteTestClientPrototype_Vec4Return(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\tstatic Vec4 vRetVal;\n\tenumTestClientCmdOutcome eOutCome;\n\tchar returnBuf[128];\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teOutCome = SendCommandToClient_Blocking_FixedString(pAUTOGENWorkString, returnBuf, 128);\n");
// 		fprintf(pOutFile, "\tassert(eOutCome == TESTCLIENT_CMD_SUCCEEDED);\n");
// 		fprintf(pOutFile, "\tsscanf(returnBuf, \"%%f %%f %%f %%f\", &vRetVal[0], &vRetVal[1], &vRetVal[2], &vRetVal[3]);\n");
// 		fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\treturn &vRetVal;\n}\n");
// 	}
// 
// 
// 
// 	if (pCommand->eReturnType == ARGTYPE_FLOAT || pCommand->eReturnType == ARGTYPE_FLOAT64)
// 	{
// 		WriteCompleteTestClientPrototype_FloatReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\t F64 fRetVal;\n\tenumTestClientCmdOutcome eOutCome;\n\tchar returnBuf[128];\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teOutCome = SendCommandToClient_Blocking_FixedString(pAUTOGENWorkString, returnBuf, 128);\n");
// 		fprintf(pOutFile, "\tassert(eOutCome == TESTCLIENT_CMD_SUCCEEDED);\n");
// 		fprintf(pOutFile, "\tsscanf(returnBuf, \"%%lf\", &fRetVal);\n");
// 		fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\treturn fRetVal;\n}\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_STRUCT)
// 	{
// 		WriteCompleteTestClientPrototype_StructReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\t %s *pRetVal;\n\tenumTestClientCmdOutcome eOutCome;\n\tchar *pOriginalRetString, *pRetString = NULL;\n", pCommand->returnTypeName);
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teOutCome = SendCommandToClient_Blocking(pAUTOGENWorkString, &pRetString);\n\tpOriginalRetString = pRetString;\n");
// 		fprintf(pOutFile, "\tassert(eOutCome == TESTCLIENT_CMD_SUCCEEDED);\n");
// 		fprintf(pOutFile, "\tpRetVal = StructCreate(parse_%s);\n", pCommand->returnTypeName);
// 		fprintf(pOutFile, "\tParserReadTextEscaped(&pRetString, parse_%s, pRetVal, 0);\n", pCommand->returnTypeName);
// 		fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\tfree(pOriginalRetString);\n\treturn pRetVal;\n}\n");
// 	}
// 
// 	if (pCommand->eReturnType == ARGTYPE_STRING)
// 	{
// 		WriteCompleteTestClientPrototype_StringReturn(pOutFile, pCommand);
// 		fprintf(pOutFile, "\n{\n\tenumTestClientCmdOutcome eOutCome;\n\tchar *pRetString = NULL;\n");
// 		WriteOutSharedFunctionBody(pOutFile, pCommand, iPrefixLen);
// 		fprintf(pOutFile, "\teOutCome = SendCommandToClient_Blocking(pAUTOGENWorkString, &pRetString);\n");
// 		fprintf(pOutFile, "\tassert(eOutCome == TESTCLIENT_CMD_SUCCEEDED);\n");
// 		fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\treturn pRetString;\n}\n");
// 	}

	fprintf(pOutFile, "\n#endif\n");
}

int MagicCommandManager::CopyCommandVarIntoCommandForSetting(MAGIC_COMMAND_STRUCT *pCommand, MAGIC_COMMANDVAR_STRUCT *pCommandVar)
{
	char *pPrefixString = "Set";
	MAGIC_COMMAND_ARG *pArg = (MAGIC_COMMAND_ARG*)calloc(sizeof(MAGIC_COMMAND_ARG), 1);

	memset(pCommand, 0, sizeof(MAGIC_COMMAND_STRUCT));

	eaPush(&pCommand->ppArgs, pArg);

	pArg->argType = pCommandVar->eVarType;
	pCommand->eReturnType = ARGTYPE_NONE;
	strcpy(pArg->argTypeName,GetGenericTypeNameFromType(pCommandVar->eVarType));
	sprintf(pArg->argName, "valToSet");
	sprintf(pCommand->commandName, "%s%s", pPrefixString, pCommandVar->varCommandName);
	strcpy(pCommand->safeCommandName,pCommand->commandName);
	MakeStringAllAlphaNum(pCommand->safeCommandName);

	strcpy(pCommand->sourceFileName, pCommandVar->sourceFileName);

	return (int)strlen(pPrefixString);
}

int MagicCommandManager::CopyCommandVarIntoCommandForGetting(MAGIC_COMMAND_STRUCT *pCommand, MAGIC_COMMANDVAR_STRUCT *pCommandVar)
{
	char *pPrefixString = "Get";
	memset(pCommand, 0, sizeof(MAGIC_COMMAND_STRUCT));

	pCommand->eReturnType = pCommandVar->eVarType;
	strcpy(pCommand->returnTypeName, GetGenericTypeNameFromType(pCommandVar->eVarType));
	sprintf(pCommand->commandName, "%s%s", pPrefixString, pCommandVar->varCommandName);
	strcpy(pCommand->safeCommandName,pCommand->commandName);
	MakeStringAllAlphaNum(pCommand->safeCommandName);
	strcpy(pCommand->sourceFileName, pCommandVar->sourceFileName);

	return (int)strlen(pPrefixString);
}

bool MagicCommandManager::CommandGetsWrittenOutForTestClients(MAGIC_COMMAND_STRUCT *pCommand)
{
	return 
	!pCommand->commandsWhichThisIsTheErrorFunctionFor[0][0] && !(pCommand->iCommandFlags & COMMAND_FLAG_NOTESTCLIENT)
	&&  !(pCommand->iCommandFlags & COMMAND_FLAG_QUEUED) && !(pCommand->iCommandFlags & COMMAND_FLAG_REMOTE) && !(pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
	&& !CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand) 
	&& ((pCommand->iCommandFlags & COMMAND_FLAG_TESTCLIENT) || !CommandHasArgOfType(pCommand, ARGTYPE_STRUCT)
	&& !((pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER) && !(pCommand->iCommandFlags & COMMAND_FLAG_GLOBAL)));
}

void MagicCommandManager::WriteOutFakeIncludesForTestClient(FILE *pOutFile)
{
	if (SlowSafeDependencyMode())
	{
		fprintf(pOutFile, "//#ifed-out includes to fool incredibuild dependency generation\n#if 0\n");
		int iCommandNum;

		for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
		{
			MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

			if (CommandGetsWrittenOutForTestClients(pCommand))
			{
				fprintf(pOutFile, "#include \"%s\"\n", pCommand->sourceFileName);
			}
		}

		int iVarNum;
		for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
		{
			MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];

			fprintf(pOutFile, "#include \"%s\"\n", pCommandVar->sourceFileName);
		}

		fprintf(pOutFile, "#endif\n");
	}
}

void MagicCommandManager::WriteOutFilesForTestClient(void)
{
	int iCommandNum, iVarNum;
	FILE *pOutFile = fopen_nofail(m_TestClientFunctionsFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. It contains functions to call all AUTO_COMMANDS\n//from the %s project. autogenerated""nocheckin\n#include \"ClientControllerLib.h\"\n#ifndef CMD_DECLSPEC\n#define CMD_DECLSPEC\n#endif\n", m_ProjectName);
	WriteOutFakeIncludesForTestClient(pOutFile);
	
	
	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandGetsWrittenOutForTestClients(pCommand))
		{
			WriteOutFunctionBodiesForTestClient(pOutFile, pCommand, 0);
		}
	}

	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];
		MAGIC_COMMAND_STRUCT command = {0};

		int iPrefixLen = CopyCommandVarIntoCommandForSetting(&command, pCommandVar);

		WriteOutFunctionBodiesForTestClient(pOutFile, &command, iPrefixLen);

		iPrefixLen = CopyCommandVarIntoCommandForGetting(&command, pCommandVar);

		WriteOutFunctionBodiesForTestClient(pOutFile, &command, iPrefixLen);


	}

	fclose(pOutFile);

	pOutFile = fopen_nofail(m_TestClientFunctionsHeaderName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. It contains function prototypes for all AUTO_COMMANDS\n//from the %s project. autogenerated""nocheckin\n#ifndef CMD_DECLSPEC\n#define CMD_DECLSPEC\n#endif\n", m_ProjectName);
	WriteOutFakeIncludesForTestClient(pOutFile);




	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (CommandGetsWrittenOutForTestClients(pCommand))
		{
			WriteOutPrototypesForTestClient(pOutFile, pCommand, 0);
		}
	}

	for (iVarNum = 0; iVarNum < eaSize(&m_ppMagicCommandVars); iVarNum++)
	{
		MAGIC_COMMANDVAR_STRUCT *pCommandVar = m_ppMagicCommandVars[iVarNum];
		MAGIC_COMMAND_STRUCT command;
		int iPrefixLen = CopyCommandVarIntoCommandForSetting(&command, pCommandVar);

		WriteOutPrototypesForTestClient(pOutFile, &command, iPrefixLen);

		iPrefixLen = CopyCommandVarIntoCommandForGetting(&command, pCommandVar);

		WriteOutPrototypesForTestClient(pOutFile, &command, iPrefixLen);
	}

	fclose(pOutFile);
}
	


void MagicCommandManager::WriteOutMainPrototypeForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	fprintf(pFile, "\n//Autogenerated wrapper for function %s, source file: %s\n", pCommand->commandName,
		pCommand->sourceFileName);


	WriteOutGenericExternsAndPrototypesForCommand(pFile, pCommand, false);

	

	fprintf(pFile, "void RemoteCommand_%s(%s GlobalType gServerType, ContainerID gServerID",
		pCommand->safeCommandName, pCommand->eReturnType == ARGTYPE_NONE ? "" : " TransactionReturnVal *pReturnValStruct,");

	WriteOutGenericArgListForCommand(pFile, pCommand, false, true, false);

	if (pCommand->iCommandFlags & COMMAND_FLAG_PACKETERRORCALLBACK)
	{
		fprintf(pFile, ", TransServerPacketFailureCB *pFailureCB, void *pUserData1, void *pUserData2");
	}
	fprintf(pFile, ")");
}


void MagicCommandManager::WriteOutInterShardPrototypeForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	fprintf(pFile, "\n//Autogenerated wrapper for function %s, source file: %s\n", pCommand->commandName,
		pCommand->sourceFileName);


	WriteOutGenericExternsAndPrototypesForCommand(pFile, pCommand, false);

	fprintf(pFile, "void RemoteCommand_Intershard_%s(const char *pShardName, GlobalType gServerType, ContainerID gServerID",
		pCommand->safeCommandName);

	WriteOutGenericArgListForCommand(pFile, pCommand, false, true, false);

	if (pCommand->iCommandFlags & COMMAND_FLAG_PACKETERRORCALLBACK)
	{
		fprintf(pFile, ", TransServerPacketFailureCB *pFailureCB, void *pUserData1, void *pUserData2");
	}
	fprintf(pFile, ")");
}


void MagicCommandManager::WriteOutQueuedPrototypeForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	fprintf(pFile, "\n//Autogenerated queued wrapper for function %s, source file: %s\n", pCommand->commandName,
		pCommand->sourceFileName);

	fprintf(pFile, "void QueueRemoteCommand_%s(char **_SP_pCommandString, GlobalType gServerType, ContainerID gServerID",
		pCommand->safeCommandName);

	WriteOutGenericArgListForCommand(pFile, pCommand, false, true, false);

	fprintf(pFile, ")");
}

void MagicCommandManager::WriteOutReturnPrototypeForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	fprintf(pFile, "\n//Autogenerated function return function %s, source file: %s\n", pCommand->commandName,
		pCommand->sourceFileName);

	if (pCommand->eReturnType == ARGTYPE_STRUCT)
	{
	
		fprintf(pFile, "//externs and typedefs for return struct from %s:\n", pCommand->commandName);
	
		fprintf(pFile, "typedef struct %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
		fprintf(pFile, "extern ParseTable parse_%s[];\n", pCommand->returnTypeName);
		fprintf(pFile, "#define TYPE_parse_%s %s\n", pCommand->returnTypeName, pCommand->returnTypeName);
	}
	else if (pCommand->eReturnType == ARGTYPE_ENUM)
	{
		fprintf(pFile, "typedef enum %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
	}

	fprintf(pFile, "enumTransactionOutcome RemoteCommandCheck_%s(TransactionReturnVal *pTransReturnStruct, %s%s* pRetVal)",
		pCommand->safeCommandName, pCommand->returnTypeName, pCommand->eReturnType == ARGTYPE_STRUCT ? "*" : "");
}


bool MagicCommandManager::CommandHasDefine(MAGIC_COMMAND_STRUCT *pCommand, char *pDefine)
{
	int i;
	for (i=0; i < pCommand->iNumDefines; i++)
	{
		if (strcmp(pCommand->defines[i], pDefine) == 0)
		{
			return true;
		}
	}

	return false;
}



void MagicCommandManager::WriteOutIfdefsForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	if (pCommand->iNumDefines)
	{
		int i;

		fprintf(pFile, "#if ");

		for (i=0; i < pCommand->iNumDefines; i++)
		{
			fprintf(pFile, "%sdefined(%s)", 
				i > 0 ? "|| " : "", pCommand->defines[i]);
		}

		fprintf(pFile, "\n");
	}
	else
	{
		fprintf(pFile, "#if !defined(PROJ_SPECIFIC_COMMANDS_ONLY)\n");
	}



}

void MagicCommandManager::WriteOutMultiRecipientPrototypeForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	fprintf(pFile, "\n//Autogenerated wrapper for function %s, source file: %s\n", pCommand->commandName,
		pCommand->sourceFileName);


	WriteOutGenericExternsAndPrototypesForCommand(pFile, pCommand, false);

	

	fprintf(pFile, "void RemoteCommand_MultipleRecipients_%s(ContainerRef ***pppRecipients",
		pCommand->safeCommandName, pCommand->eReturnType == ARGTYPE_NONE ? "" : " TransactionReturnVal *pReturnValStruct,");

	WriteOutGenericArgListForCommand(pFile, pCommand, false, true, false);

	fprintf(pFile, ")");
}

void MagicCommandManager::WriteOutEndifsForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{

	fprintf(pFile, "#endif\n");
}

void MagicCommandManager::WriteOutPrototypesForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutIfdefsForRemoteCommand(pFile, pCommand);
	
	if (pCommand->iCommandFlags & COMMAND_FLAG_INTERSHARD)
	{
		WriteOutInterShardPrototypeForRemoteCommand(pFile, pCommand);
		fprintf(pFile, ";\n");
	}

	WriteOutMainPrototypeForRemoteCommand(pFile, pCommand);
	fprintf(pFile, ";\n");

	if (pCommand->eReturnType != ARGTYPE_NONE)
	{
		WriteOutReturnPrototypeForRemoteCommand(pFile, pCommand);
		fprintf(pFile, ";\n");
	}
	else
	{
		WriteOutQueuedPrototypeForRemoteCommand(pFile, pCommand);
		fprintf(pFile, ";\n");
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_MULTIPLE_RECIPIENTS)
	{
		CommandAssert(pCommand, CommandShouldGetRemotePacketWrapper(pCommand), 
			"ACMD_MULTIPLE_RECIPIENTS only legal for remote commands with no return values or special arguments");

		WriteOutMultiRecipientPrototypeForRemoteCommand(pFile, pCommand);
		fprintf(pFile, ";\n");
	}

	WriteOutEndifsForRemoteCommand(pFile, pCommand);

}


void MagicCommandManager::WriteOutFunctionBodiesForRemoteCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutIfdefsForRemoteCommand(pFile, pCommand);
	
	if (pCommand->iCommandFlags & COMMAND_FLAG_INTERSHARD)
	{
		WriteOutInterShardPrototypeForRemoteCommand(pFile, pCommand);
		fprintf(pFile, "\n{\n\tPacket *_pPacket_StructParser__INTERNAL;\n\tstatic PacketTracker *__pTracker;\n\tLocalTransactionManager *pManager = objLocalManager();\n\tif (!pManager) return;\n\tONCE(__pTracker = PacketTrackerFind(\"RemoteCommand\", 0, \"%s\"));\n\t_pPacket_StructParser__INTERNAL = GetPacketToSendThroughTransactionServerToOtherShard(pManager, __pTracker, pShardName, gServerType, gServerID, TRANSPACKETCMD_REMOTECOMMAND, \"%s\", %s);\n", pCommand->commandName, pCommand->commandName,
			(pCommand->iCommandFlags & COMMAND_FLAG_PACKETERRORCALLBACK) ? "pFailureCB, pUserData1, pUserData2" : "NULL, NULL, NULL");
		WriteOutGenericCodeToPutArgumentsIntoPacket(pFile, pCommand, "_pPacket_StructParser__INTERNAL");

		fprintf(pFile, "\tSendPacketThroughTransactionServer(pManager, &_pPacket_StructParser__INTERNAL);\n}\n");
	}


	WriteOutMainPrototypeForRemoteCommand(pFile, pCommand);

	if (CommandShouldGetRemotePacketWrapper(pCommand))
	{
		fprintf(pFile, "\n{\n\tPacket *_pPacket_StructParser__INTERNAL;\n\tstatic PacketTracker *__pTracker;\n\t\n\tLocalTransactionManager *pManager = objLocalManager();\n\tif (!pManager) return;\n\tONCE(__pTracker = PacketTrackerFind(\"RemoteCommand\", 0, \"%s\"));\n\t_pPacket_StructParser__INTERNAL = GetPacketToSendThroughTransactionServer(pManager, __pTracker, gServerType, gServerID, TRANSPACKETCMD_REMOTECOMMAND, \"%s\", %s);\n", pCommand->commandName, pCommand->commandName,
			(pCommand->iCommandFlags & COMMAND_FLAG_PACKETERRORCALLBACK) ? "pFailureCB, pUserData1, pUserData2" : "NULL, NULL, NULL");

		WriteOutGenericCodeToPutArgumentsIntoPacket(pFile, pCommand, "_pPacket_StructParser__INTERNAL");

		fprintf(pFile, "\tSendPacketThroughTransactionServer(pManager, &_pPacket_StructParser__INTERNAL);\n}\n");

		if (pCommand->iCommandFlags & COMMAND_FLAG_MULTIPLE_RECIPIENTS)
		{
			WriteOutMultiRecipientPrototypeForRemoteCommand(pFile, pCommand);
			fprintf(pFile, "\n{\n\tPacket *_pPacket_StructParser__INTERNAL; \n\tstatic PacketTracker *__pTracker;\n\tLocalTransactionManager *pManager = objLocalManager();\n\tif (!pManager) return;\n\tONCE(__pTracker = PacketTrackerFind(\"RemoteCommandMultiple\", 0, \"%s\"));\n\t_pPacket_StructParser__INTERNAL = GetPacketToSendThroughTransactionServer_MultipleRecipients(pManager, __pTracker, pppRecipients, TRANSPACKETCMD_REMOTECOMMAND, \"%s\");\n", pCommand->commandName, pCommand->commandName);

			WriteOutGenericCodeToPutArgumentsIntoPacket(pFile, pCommand, "_pPacket_StructParser__INTERNAL");

			fprintf(pFile, "\tSendPacketThroughTransactionServer(pManager, &_pPacket_StructParser__INTERNAL);\n}\n");
		}

	}
	else
	{



		fprintf(pFile, "\n{\n\tchar *_SP_pCommandString = NULL;\n\tBaseTransaction baseTransaction;\n\tBaseTransaction **ppBaseTransactions = NULL;\n");

		fprintf(pFile, "\testrStackCreateSize(&_SP_pCommandString, 4096);\n\testrConcatf(&_SP_pCommandString, \"");
		fprintf(pFile, "%s", CommandHasArgOfType(pCommand, ARGTYPE_SLOWCOMMANDID) ? "slowremotecommand " : "remotecommand ");
		fprintf(pFile, "%s \");\n", pCommand->commandName);
		

		WriteOutGenericCodeToPutArgumentsIntoEString(pFile, pCommand, "&_SP_pCommandString", false, false);


		fprintf(pFile, "\tbaseTransaction.pData = _SP_pCommandString;\n\tbaseTransaction.recipient.containerID = gServerID;\n\tbaseTransaction.recipient.containerType = gServerType;\n\tbaseTransaction.pRequestedTransVariableNames = NULL;\n");

		fprintf(pFile, "\teaPush(&ppBaseTransactions, &baseTransaction);\n");

		if (pCommand->eReturnType != ARGTYPE_NONE)
		{
			fprintf(pFile, "\tif (pReturnValStruct)\n\t{\n");
			fprintf(pFile, "\t\tRequestNewTransaction( objLocalManager(), \"%s\", ppBaseTransactions, TRANS_TYPE_SEQUENTIAL_ATOMIC, %s, 0 );\n",
				pCommand->commandName, pCommand->eReturnType == ARGTYPE_NONE ? "NULL" : "pReturnValStruct");
			fprintf(pFile, "\t}\n\telse\n\t{\n");
			fprintf(pFile, "\t\tRequestNewTransaction( objLocalManager(), \"%s\", ppBaseTransactions, TRANS_TYPE_SEQUENTIAL_ATOMIC, NULL, 0 );\n\t}\n", pCommand->commandName);
		}
		else
		{
			fprintf(pFile, "\tRequestNewTransaction( objLocalManager(), \"%s\", ppBaseTransactions, TRANS_TYPE_SEQUENTIAL_ATOMIC, NULL, 0 );\n", pCommand->commandName);
		}

		fprintf(pFile, "\teaDestroy(&ppBaseTransactions);\n");
		fprintf(pFile , "\testrDestroy(&_SP_pCommandString);\n");

		fprintf(pFile, "}\n");
	}

	if (pCommand->eReturnType == ARGTYPE_NONE )
	{
		//if there's no return prototype, make a queued prototype, unless it has a packet arg
		if (!CommandHasArgOfType(pCommand, ARGTYPE_PACKET))
		{
			WriteOutQueuedPrototypeForRemoteCommand(pFile, pCommand);

			fprintf(pFile, "\n{\tif (!_SP_pCommandString) return;\n\tPERFINFO_AUTO_START_FUNC();\n\tif (*_SP_pCommandString) estrConcatStatic(_SP_pCommandString, \"\\n\");\n");
			fprintf(pFile, "\testrConcatf(_SP_pCommandString, \"remotecommand %%d %%d %s \", (int) gServerType, gServerID);\n", pCommand->commandName);

			WriteOutGenericCodeToPutArgumentsIntoEString(pFile, pCommand, "_SP_pCommandString", false, false);

			fprintf(pFile, "\tPERFINFO_AUTO_STOP();\n");
			fprintf(pFile, "}\n");
		}
		
		WriteOutEndifsForRemoteCommand(pFile, pCommand);

		return;
	}


	WriteOutReturnPrototypeForRemoteCommand(pFile, pCommand);

	fprintf(pFile, "\n{\n\tswitch (pTransReturnStruct->eOutcome)\n\t{\n");
	fprintf(pFile, "\tcase TRANSACTION_OUTCOME_FAILURE:\n\t\treturn TRANSACTION_OUTCOME_FAILURE;\n");
	fprintf(pFile, "\tcase TRANSACTION_OUTCOME_SUCCESS:\n");

	switch (pCommand->eReturnType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_SINT64:
	case ARGTYPE_BOOL:
	case ARGTYPE_ENUM:
		fprintf(pFile, "\t\tStringToInt_Multisize_AssumeGoodInput(pTransReturnStruct->pBaseReturnVals[0].returnString, pRetVal);\n");
		fprintf(pFile, "\t\treturn TRANSACTION_OUTCOME_SUCCESS;\n");
		break;

	case ARGTYPE_UINT:
	case ARGTYPE_UINT64:
		fprintf(pFile, "\t\tStringToUint_Multisize_AssumeGoodInput(pTransReturnStruct->pBaseReturnVals[0].returnString, pRetVal);\n");
		fprintf(pFile, "\t\treturn TRANSACTION_OUTCOME_SUCCESS;\n");
		break;


	case ARGTYPE_FLOAT:
	case ARGTYPE_FLOAT64:
		fprintf(pFile, "\t\tsscanf(pTransReturnStruct->pBaseReturnVals[0].returnString, SCANF_CODE_FOR_FLOAT(*pRetVal), pRetVal);\n");
		fprintf(pFile, "\t\treturn TRANSACTION_OUTCOME_SUCCESS;\n");
		break;


	case ARGTYPE_STRUCT:
		fprintf(pFile, "\t\tif (!pTransReturnStruct->pBaseReturnVals[0].returnString || pTransReturnStruct->pBaseReturnVals[0].returnString[0] == 0)\n");
		fprintf(pFile, "\t\t{\n\t\t\t*pRetVal = NULL;\n\t\t}\n\t\telse\n");
		fprintf(pFile, "\t\t{\n\t\t\tchar *pTempString;\n");
		fprintf(pFile, "\t\t\t*pRetVal = StructCreateVoid(parse_%s);\n", pCommand->returnTypeName);
		fprintf(pFile, "\t\t\tpTempString = pTransReturnStruct->pBaseReturnVals[0].returnString;\n");
		fprintf(pFile, "\t\t\tParserReadTextEscaped(&pTempString, parse_%s, *pRetVal, 0);\n",
			pCommand->returnTypeName);
		fprintf(pFile, "\t\t}\n\t\treturn TRANSACTION_OUTCOME_SUCCESS;\n");
		break;

	case ARGTYPE_STRING:
	case ARGTYPE_SENTENCE:
		fprintf(pFile, "\t\testrCopy2(pRetVal, pTransReturnStruct->pBaseReturnVals[0].returnString);\n");
		fprintf(pFile, "\t\treturn TRANSACTION_OUTCOME_SUCCESS;\n");
		break;

	default:
		fprintf(pFile, "ACK NO SUPPORT YET\n");
		break;
	}

	fprintf(pFile, "\t}\n\treturn TRANSACTION_OUTCOME_NONE;\n}\n");

	WriteOutEndifsForRemoteCommand(pFile, pCommand);


}



void MagicCommandManager::WriteOutFunctionPrototypeForSlowCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	
	
	fprintf(pFile, "\n//autogenerated function used to return the value from slow command %s\n//defined in file %s\n",
		pCommand->commandName, pCommand->sourceFileName);

	if (pCommand->eReturnType == ARGTYPE_STRUCT)
	{
		fprintf(pFile, "typedef struct %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
		fprintf(pFile, "extern ParseTable parse_%s[];\n",  pCommand->returnTypeName);
	}
	else if (pCommand->eReturnType == ARGTYPE_ENUM)
	{
		fprintf(pFile, "typedef enum %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
	}

	fprintf(pFile, "void SlowRemoteCommandReturn_%s(SlowRemoteCommandID iCmdID, %s%s retVal)",
		pCommand->safeCommandName, pCommand->returnTypeName, pCommand->eReturnType == ARGTYPE_STRUCT ? "*" : "");
}


void MagicCommandManager::WriteOutFunctionBodyForSlowCommand(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutFunctionPrototypeForSlowCommand(pFile, pCommand);
	if (pCommand->eReturnType == ARGTYPE_STRING || pCommand->eReturnType == ARGTYPE_SENTENCE)
	{
		//want exact string here with no extra spaces or anything... just return it
		fprintf(pFile, "\n{\n\tReturnSlowCommand(iCmdID, true, retVal);\n}\n");
	}
	else
	{
		fprintf(pFile, "\n{\n\tchar *pRetString = NULL;\n\testrCreate(&pRetString);\n");
		
		WriteOutGenericCodeToPutSingleArgumentIntoEString(pFile, pCommand->eReturnType, 
			"retVal", pCommand->returnTypeName, "&pRetString", false, false, pCommand);

		fprintf(pFile, "\tReturnSlowCommand(iCmdID, true, pRetString);\n\testrDestroy(&pRetString);\n}\n");
	}
}

	
void MagicCommandManager::WriteOutFakeIncludesForRemoteCommands(FILE *pOutFile)
{
	if (SlowSafeDependencyMode())
	{

		fprintf(pOutFile, "//#ifed-out includes to fool incredibuild dependency generation\n#if 0\n");
		int iCommandNum;

		for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
		{
			MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

			if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
			{
				fprintf(pOutFile, "#include \"%s\"\n", pCommand->sourceFileName);
			}
		}
		fprintf(pOutFile, "#endif\n");
	}
}

void MagicCommandManager::WriteOutFakeIncludesForSlowCommands(FILE *pOutFile)
{
	if (SlowSafeDependencyMode())
	{

		fprintf(pOutFile, "//#ifed-out includes to fool incredibuild dependency generation\n#if 0\n");
		int iCommandNum;

		for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
		{
			MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

			if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
			{
				fprintf(pOutFile, "#include \"%s\"\n", pCommand->sourceFileName);
			}
		}
		fprintf(pOutFile, "#endif\n");
	}
}

void MagicCommandManager::FindAllDefines(void)
{
	int iCommandNum;
	int i, j;
	int iNumAllDefines = 0;

	memset(m_AllDefines, 0, sizeof(m_AllDefines));

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
		{
			for (i=0; i < pCommand->iNumDefines; i++)
			{
				for (j=0; j < iNumAllDefines; j++)
				{
					if (strcmp(m_AllDefines[j], pCommand->defines[i]) == 0)
					{
						break;
					}
				}

				if ( j == iNumAllDefines)
				{
					CommandAssert(pCommand, iNumAllDefines < MAX_UNIQUE_DEFINES, "Too many unique defines");
				
					strcpy(m_AllDefines[iNumAllDefines++], pCommand->defines[i]);
				}
			}
		}
	}
}


void MagicCommandManager::WriteOutRemoteCommands(void)
{
	FILE *pOutFile = fopen_nofail(m_RemoteFunctionsFileName, "wt");

	FindAllDefines();

	fprintf(pOutFile, "//For more info on remote commands, look here: http://crypticwiki:8081/display/Core/AUTO_COMMAND_REMOTE+and+AUTO_COMMAND_REMOTE_SLOW\n");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"RemoteAutoCommandSupport.h\"\n#include \"TextParser.h\"\n#include \"ObjTransactions.h\"\n#include \"LocalTransactionManager.h\"\n#include \"StructNet.h\"\n#include \"netpacketutil.h\"\n#include \"StringUtil.h\"\n");

	WriteOutFakeIncludesForRemoteCommands(pOutFile);

	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
		{
			WriteOutFunctionBodiesForRemoteCommand(pOutFile, pCommand);
		}
	}

	fclose(pOutFile);


	pOutFile = fopen_nofail(m_RemoteFunctionsHeaderName, "wt");

	fprintf(pOutFile, "//For more info on remote commands, look here: http://crypticwiki:8081/display/Core/AUTO_COMMAND_REMOTE+and+AUTO_COMMAND_REMOTE_SLOW\n");
	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n#include \"RemoteAutoCommandSupport.h\"\n");
	
	WriteOutFakeIncludesForRemoteCommands(pOutFile);


	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
		{
			WriteOutPrototypesForRemoteCommand(pOutFile, pCommand);
		}
	}

	fclose(pOutFile);



	pOutFile = fopen_nofail(m_SlowFunctionsFileName, "wt");

	fprintf(pOutFile, "//For more info on remote commands, look here: http://crypticwiki:8081/display/Core/AUTO_COMMAND_REMOTE+and+AUTO_COMMAND_REMOTE_SLOW\n");
	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"RemoteAutoCommandSupport.h\"\n#include \"objtransactions.h\"\n#include \"textparser.h\"\n");
	
	WriteOutFakeIncludesForSlowCommands(pOutFile);


	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
		{
			WriteOutFunctionBodyForSlowCommand(pOutFile, pCommand);
		}
	}

	fclose(pOutFile);

	pOutFile = fopen_nofail(m_SlowFunctionsHeaderName, "wt");

	fprintf(pOutFile, "//For more info on remote commands, look here: http://crypticwiki:8081/display/Core/AUTO_COMMAND_REMOTE+and+AUTO_COMMAND_REMOTE_SLOW\n");
	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n#include \"RemoteAutoCommandSupport.h\"\n#include \"objtransactions.h\"\n#include \"textparser.h\"\n");
	
	WriteOutFakeIncludesForSlowCommands(pOutFile);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_SLOW_REMOTE)
		{
			WriteOutFunctionPrototypeForSlowCommand(pOutFile, pCommand);
			fprintf(pOutFile, ";\n");
		}
	}

	fclose(pOutFile);


}
	
void MagicCommandManager::WriteOutMainQueuedCommandPrototype(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_VOIDSTAR)
		{
			char typeName[256];
			char *pAsterisk;
			strcpy(typeName, pArg->argTypeName);
			pAsterisk = strchr(typeName, '*');
			if (pAsterisk)
			{
				*pAsterisk = 0;
			}

			if (strcmp(NoConst(typeName), "void") != 0 && strcmp(NoConst(typeName), "char") != 0)
			{
				fprintf(pOutFile, "typedef struct %s %s;\n",
					NoConst(typeName), NoConst(typeName));
			}
		}
	}

	fprintf(pOutFile, "\nvoid QueuedCommand_%s(", pCommand->safeCommandName);

	if (eaSize(&pCommand->ppArgs) == 0 && pCommand->queueName[0])
	{
		fprintf(pOutFile, " void )");
	}
	else
	{
		if (pCommand->queueName[0] == 0)
		{
			fprintf(pOutFile, "CommandQueue *pQueue%s", eaSize(&pCommand->ppArgs) ? ", " : "");
		}

		int i;

		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

			fprintf(pOutFile, "%s %s%s%s", pArg->argTypeName, 
				IsPointerType(pArg->argType) ? "*" : "",
				pArg->argName, i < eaSize(&pCommand->ppArgs) - 1 ? "," : "");
		}

		fprintf(pOutFile, ")");
	}
}

void MagicCommandManager::WriteOutWrapperQueuedCommandPrototype(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	fprintf(pOutFile, "void %s_QUEUEDWRAPPER(CommandQueue *pQueue)", pCommand->safeCommandName);
}

void MagicCommandManager::WriteOutPrototypesForQueuedCommand(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutMainQueuedCommandPrototype(pOutFile, pCommand);
	fprintf(pOutFile, ";\n");

	WriteOutWrapperQueuedCommandPrototype(pOutFile, pCommand);
	fprintf(pOutFile, ";\n");
}

bool MagicCommandManager::QueuedCommandTypeMightBeNull(enumMagicCommandArgType eType)
{
	switch (eType)
	{
	case ARGTYPE_VEC3_POINTER:
	case ARGTYPE_VEC3_DIRECT:
	case ARGTYPE_VEC4_POINTER:
	case ARGTYPE_VEC4_DIRECT:
	case ARGTYPE_MAT4_POINTER:
	case ARGTYPE_MAT4_DIRECT:
	case ARGTYPE_QUAT_POINTER:
	case ARGTYPE_QUAT_DIRECT:
		return true;
	}

	return false;
}

void MagicCommandManager::WriteOutBodiesForQueuedCommand(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	int i;

	//first, prototype of source function
	fprintf(pOutFile, "void %s(", pCommand->safeCommandName);

	if (eaSize(&pCommand->ppArgs) == 0)
	{
		fprintf(pOutFile, " void );\n");
	}
	else
	{
		for (i=0; i < eaSize(&pCommand->ppArgs); i++)
		{
			MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

			fprintf(pOutFile, "%s %s%s%s", 
				pArg->argTypeName, 
				IsPointerType(pArg->argType) ? "*" : "", 
				pArg->argName, 
				i < eaSize(&pCommand->ppArgs) - 1 ? "," : "");
		}

		fprintf(pOutFile, ");\n");
	}

	//extern declare of queue
	if (pCommand->queueName[0])
	{
		fprintf(pOutFile, "extern CommandQueue *%s;\n", pCommand->queueName);
	}

	WriteOutWrapperQueuedCommandPrototype(pOutFile, pCommand);
	fprintf(pOutFile, "\n{\n");

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_STRING)
		{
			fprintf(pOutFile, "\t%s %s = NULL;\n", NoConst(pArg->argTypeName), pArg->argName);
		}
		else
		{
			fprintf(pOutFile, "\t%s %s;\n", NoConst(pArg->argTypeName), pArg->argName);
		}
	
		if (QueuedCommandTypeMightBeNull(pArg->argType))
		{
			fprintf(pOutFile, "\tbool bRead_%s = false;\n", pArg->argName);	
		}
	}

	fprintf(pOutFile, "\n\tCommandQueue_EnterCriticalSection(pQueue);\n");

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{	
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_STRING || pArg->argType == ARGTYPE_SENTENCE)
		{
			fprintf(pOutFile, "\testrStackCreate(&%s);\n", pArg->argName);
		}
	}

	fprintf(pOutFile, "\n");

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		switch (pArg->argType)
		{
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_FLOAT:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
		case ARGTYPE_FLOAT64:
		case ARGTYPE_BOOL:
		case ARGTYPE_ENUM:
			fprintf(pOutFile, "\tCommandQueue_Read(pQueue, &%s, sizeof(%s));\n", 
				pArg->argName, NoConst(pArg->argTypeName));
			break;

		case ARGTYPE_VEC3_POINTER:
		case ARGTYPE_VEC3_DIRECT:
		case ARGTYPE_VEC4_POINTER:
		case ARGTYPE_VEC4_DIRECT:
		case ARGTYPE_MAT4_POINTER:
		case ARGTYPE_MAT4_DIRECT:
		case ARGTYPE_QUAT_POINTER:
		case ARGTYPE_QUAT_DIRECT:
			fprintf(pOutFile, "\tif (CommandQueue_ReadByte(pQueue))\n\t{\n\t\tbRead_%s=true;\n\t\tCommandQueue_Read(pQueue, %s, sizeof(%s));\n\t}\n", 
				pArg->argName, pArg->argName, NoConst(pArg->argTypeName));
			break;

		case ARGTYPE_STRING:
		case ARGTYPE_SENTENCE:
			fprintf(pOutFile, "\tCommandQueue_ReadString(pQueue, &%s);\n", pArg->argName);
			break;

		case ARGTYPE_VOIDSTAR:
			fprintf(pOutFile, "\tCommandQueue_Read(pQueue, &%s, sizeof(void*));\n", 
				pArg->argName);
			break;
		}
	}

	fprintf(pOutFile, "\n\t%s(", pCommand->safeCommandName);

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (QueuedCommandTypeMightBeNull(pArg->argType))
		{
			fprintf(pOutFile, "bRead_%s ? %s%s : NULL%s", pArg->argName, IsPointerType(pArg->argType) ? "&" : "", pArg->argName, i < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
		}
		else
		{
			fprintf(pOutFile, "%s%s%s", IsPointerType(pArg->argType) ? "&" : "", pArg->argName, i < eaSize(&pCommand->ppArgs) - 1 ? ", " : "");
		}
	}

	fprintf(pOutFile, ");\n");

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_STRING || pArg->argType == ARGTYPE_SENTENCE)
		{
			fprintf(pOutFile, "\testrDestroy(&%s);\n", pArg->argName);
		}
	}

	fprintf(pOutFile, "\n\tCommandQueue_LeaveCriticalSection(pQueue);\n");

	fprintf(pOutFile, "}\n");

	WriteOutMainQueuedCommandPrototype(pOutFile, pCommand);

	char *pQueueNameToUse = pCommand->queueName[0] ? pCommand->queueName : "pQueue";


	fprintf(pOutFile, "\n{\n\tvoid *pFunc = %s_QUEUEDWRAPPER;\n\tCommandQueue_EnterCriticalSection(%s);\n\tCommandQueue_Write(%s, &pFunc, sizeof(void*));\n", pCommand->commandName, pQueueNameToUse, pQueueNameToUse);

	for (i=0;i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		switch (pArg->argType)
		{
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_FLOAT:
		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
		case ARGTYPE_FLOAT64:
		case ARGTYPE_BOOL:
		case ARGTYPE_ENUM:
			fprintf(pOutFile, "\tCommandQueue_Write(%s, &%s, sizeof(%s));\n", 
				pQueueNameToUse, pArg->argName, pArg->argTypeName);
			break;

		case ARGTYPE_VEC3_POINTER:
		case ARGTYPE_VEC3_DIRECT:
		case ARGTYPE_VEC4_POINTER:
		case ARGTYPE_VEC4_DIRECT:
		case ARGTYPE_MAT4_POINTER:
		case ARGTYPE_MAT4_DIRECT:
		case ARGTYPE_QUAT_POINTER:
		case ARGTYPE_QUAT_DIRECT:
			fprintf(pOutFile, "\tif (%s)\n\t{\n\t\tCommandQueue_WriteByte(%s, 1);\n\t\tCommandQueue_Write(%s, %s, sizeof(%s));\n\t}\n\telse\n\t{\n\t\tCommandQueue_WriteByte(%s, 0);\n\t}\n",
				pArg->argName, pQueueNameToUse, pQueueNameToUse, pArg->argName, pArg->argTypeName, pQueueNameToUse);
			break;

		case ARGTYPE_STRING:
		case ARGTYPE_SENTENCE:
			fprintf(pOutFile, "\tCommandQueue_WriteString(%s, %s);\n", pQueueNameToUse, pArg->argName);
			break;

		case ARGTYPE_VOIDSTAR:
			fprintf(pOutFile, "\tCommandQueue_Write(%s, &%s, sizeof(void*));\n", 
				pQueueNameToUse, pArg->argName);
			break;

		}
	}
	fprintf(pOutFile, "\n\tCommandQueue_LeaveCriticalSection(%s);\n", pQueueNameToUse);

	fprintf(pOutFile, "}\n");
}


void MagicCommandManager::WriteOutFakeIncludesForQueuedCommands(FILE *pOutFile)
{
	if (SlowSafeDependencyMode())
	{

		fprintf(pOutFile, "//#ifed-out includes to fool incredibuild dependency generation\n#if 0\n");
		int iCommandNum;

		for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
		{
			MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

			if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
			{
				fprintf(pOutFile, "#include \"%s\"\n", pCommand->sourceFileName);
			}
		}
		fprintf(pOutFile, "#endif\n");
	}
}


void MagicCommandManager::WriteOutQueuedCommands(void)
{
	FILE *pOutFile = fopen_nofail(m_QueuedFunctionsFileName, "wt");

	fprintf(pOutFile, "//For more info on queued commands, look here: http://crypticwiki:8081/display/Core/AUTO_COMMAND_QUEUED\n");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"commandqueue.h\"\n\n");

	WriteOutFakeIncludesForQueuedCommands(pOutFile);


	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
		{
			WriteOutBodiesForQueuedCommand(pOutFile, pCommand);
		}
	}

	fclose(pOutFile);


	pOutFile = fopen_nofail(m_QueuedFunctionsHeaderName, "wt");

	fprintf(pOutFile, "//For more info on queued commands, look here: http://crypticwiki:8081/display/Core/AUTO_COMMAND_QUEUED\n");
	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n#include \"commandqueue.h\"\n#include \"estring.h\"\n");
	
	WriteOutFakeIncludesForQueuedCommands(pOutFile);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
		{
			WriteOutPrototypesForQueuedCommand(pOutFile, pCommand);
		}
	}

	fclose(pOutFile);



}
	

bool MagicCommandManager::DoesArgTypeNeedConstInWrapperPrototype(enumMagicCommandArgType eType, char *pTypeName)
{
	if (StringBeginsWith(pTypeName, "const ", true) || strstr(pTypeName, " const "))
	{
		return false;
	}

	switch (eType)
	{
	case ARGTYPE_STRUCT:
	case ARGTYPE_STRING:
	case ARGTYPE_SENTENCE:
	case ARGTYPE_ESCAPEDSTRING:
		return true;
	}

	return false;
}


void MagicCommandManager::WriteOutGenericArgListForCommand(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, 
   bool bIncludeSpecialArgs, bool bOtherArgsAlreadyWritten, bool bDoubleStarForOwnable)
{
	int i;
	bool bWroteAtLeastOne = false;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (bIncludeSpecialArgs || pArg->argType < ARGTYPE_FIRST_SPECIAL)
		{
			fprintf(pOutFile, "%s%s%s%s%s %s", 
				bWroteAtLeastOne || bOtherArgsAlreadyWritten ? ", " : "",
				DoesArgTypeNeedConstInWrapperPrototype(pArg->argType, pArg->argTypeName) ? "const " : "",
				//(pArg->argType == ARGTYPE_ENUM ? "int" : pArg->argTypeName),
                pArg->argTypeName,
				(IsPointerType(pArg->argType) || pArg->argType == ARGTYPE_STRUCT || pArg->argType == ARGTYPE_PACKET) ? "*" : "",
				bDoubleStarForOwnable && (pArg->argFlags & ARGFLAG_OWNABLE) ? "*" : "",
				pArg->argName);

			bWroteAtLeastOne = true;
		}
	}

	if (!bWroteAtLeastOne && !bOtherArgsAlreadyWritten)
	{
		fprintf(pOutFile, " void ");
	}
}

void MagicCommandManager::WriteOutGenericExternsAndPrototypesForCommand(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, bool bWriteNameListStuff)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_STRUCT)
		{
			fprintf(pOutFile, "typedef struct %s %s;\n", pArg->argTypeName, pArg->argTypeName);
			fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pArg->argTypeName);
			fprintf(pOutFile, "#define TYPE_parse_%s %s\n", pArg->argTypeName, pArg->argTypeName);
		}
		else if (pArg->argType == ARGTYPE_ENUM)
		{
			fprintf(pOutFile, "typedef enum %s %s;\n", pArg->argTypeName, pArg->argTypeName);
		}
		if (bWriteNameListStuff)
		{
			if (pArg->argNameListDataPointerName[0] && !pArg->argNameListDataPointerWasString)
			{
				if (strcmp(pArg->argNameListTypeName, "NAMELISTTYPE_COMMANDLIST") == 0)
				{
					fprintf(pOutFile, "extern CmdList %s;\n", pArg->argNameListDataPointerName);
				}
				else if (strcmp(pArg->argNameListTypeName, "NAMELISTTYPE_PREEXISTING") == 0)
				{
					fprintf(pOutFile, "extern NameList *%s;\n", pArg->argNameListDataPointerName);
				}
				else
				{
					fprintf(pOutFile, "extern void *%s;\n", pArg->argNameListDataPointerName);
				}
			}
		}
	}

	if (pCommand->eReturnType == ARGTYPE_STRUCT)
	{
		fprintf(pOutFile, "typedef struct %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
		fprintf(pOutFile, "extern ParseTable parse_%s[];\n", pCommand->returnTypeName);
		fprintf(pOutFile, "#define TYPE_parse_%s %s\n", pCommand->returnTypeName, pCommand->returnTypeName);
	}
	else if (pCommand->eReturnType == ARGTYPE_ENUM)
	{
		fprintf(pOutFile, "typedef enum %s %s;\n", pCommand->returnTypeName, pCommand->returnTypeName);
	}
	
}

void MagicCommandManager::WriteOutGenericCodeToPutArgumentsIntoEString(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, 
   char *pEStringName, bool bEscapeAllStrings, bool bPutStructsIntoCmdParseStructList)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		WriteOutGenericCodeToPutSingleArgumentIntoEString(pOutFile, pCommand->ppArgs[i]->argType, pCommand->ppArgs[i]->argName, 
			pCommand->ppArgs[i]->argTypeName,
			pEStringName, bEscapeAllStrings, bPutStructsIntoCmdParseStructList, pCommand);
	}
}



void MagicCommandManager::WriteOutGenericCodeToPutSingleArgumentIntoEString(FILE *pOutFile, enumMagicCommandArgType eArgType,
	char *pArgName, char *pArgTypeName, char *pEStringName, bool bEscapeAllStrings, bool bPutStructsIntoCmdParseStructList, MAGIC_COMMAND_STRUCT *pCommand)
{

	switch (eArgType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_UINT:
	case ARGTYPE_SINT64:
	case ARGTYPE_UINT64:
	case ARGTYPE_ENUM:
	case ARGTYPE_BOOL:
		fprintf(pOutFile, "\testrConcatf(%s, PRINTF_CODE_FOR_INT(%s), %s);\n",
			pEStringName, pArgName, pArgName);
		break;

	case ARGTYPE_FLOAT:
	case ARGTYPE_FLOAT64:
		fprintf(pOutFile, "\testrConcatf(%s, PRINTF_CODE_FOR_FLOAT(%s), %s);\n", 
			pEStringName, pArgName, pArgName);
		break;

	case ARGTYPE_STRING:
		if (bEscapeAllStrings)
		{
			fprintf(pOutFile, "\testrAppend2(%s, \" \\\"\");\n", pEStringName);
			fprintf(pOutFile, "\tif (%s) estrAppendEscaped(%s, %s);\n", pArgName, pEStringName, pArgName);
			fprintf(pOutFile, "\testrAppend2(%s, \"\\\" \");\n", pEStringName);
		}
		else
		{
			fprintf(pOutFile, "\testrConcatf(%s, \" \\\"%%s\\\" \", %s ? %s : \"\");\n", pEStringName, pArgName, pArgName);
		}
		break;

	case ARGTYPE_SENTENCE:
		if (bEscapeAllStrings)
		{
			fprintf(pOutFile, "\testrAppend2(%s, \" \\\"\");\n", pEStringName);
			fprintf(pOutFile, "\tif (%s) estrAppendEscaped(%s, %s);\n", pArgName, pEStringName, pArgName);
			fprintf(pOutFile, "\testrAppend2(%s, \"\\\" \");\n", pEStringName);
		}
		else
		{
			fprintf(pOutFile, "\testrConcatf(%s, \" %%s\", %s ? %s : \"\");\n", pEStringName, pArgName, pArgName);
		}
		break;

	case ARGTYPE_ESCAPEDSTRING:
		fprintf(pOutFile, "\testrAppend2(%s, \" \\\"\");\n", pEStringName);
		fprintf(pOutFile, "\tif (%s) estrAppendEscaped(%s, %s);\n", pArgName, pEStringName, pArgName);
		fprintf(pOutFile, "\testrAppend2(%s, \"\\\" \");\n", pEStringName);
		break;

	case ARGTYPE_VEC3_POINTER:
		fprintf(pOutFile, "\tif (%s)\n\t{\n", pArgName);
		fprintf(pOutFile, "\t\testrConcatf(%s, \" %%f %%f %%f \", (*%s)[0], (*%s)[1], (*%s)[2]);\n",
			pEStringName, pArgName, pArgName, pArgName);
		fprintf(pOutFile, "\t}\n\telse\n\t{\n\t\t\testrConcatf(%s, \" N N N \");\n\t}\n",
			pEStringName);
		break;

	case ARGTYPE_VEC3_DIRECT:
		fprintf(pOutFile, "\tif (%s)\n\t{\n", pArgName);
		fprintf(pOutFile, "\t\testrConcatf(%s, \" %%f %%f %%f \", (%s)[0], (%s)[1], (%s)[2]);\n",
			pEStringName, pArgName, pArgName, pArgName);
		fprintf(pOutFile, "\t}\n\telse\n\t{\n\t\t\testrConcatf(%s, \" N N N \");\n\t}\n",
			pEStringName);
		break;

	case ARGTYPE_VEC4_POINTER:
		fprintf(pOutFile, "\tif (%s)\n\t{\n", pArgName);
		fprintf(pOutFile, "\t\testrConcatf(%s, \" %%f %%f %%f %%f \", (*%s)[0], (*%s)[1], (*%s)[2], (*%s)[3]);\n",
			pEStringName, pArgName, pArgName, pArgName, pArgName);
		fprintf(pOutFile, "\t}\n\telse\n\t{\n\t\t\testrConcatf(%s, \" N N N N \");\n\t}\n",
			pEStringName);
		break;

	case ARGTYPE_VEC4_DIRECT:
		fprintf(pOutFile, "\tif (%s)\n\t{\n", pArgName);
		fprintf(pOutFile, "\t\testrConcatf(%s, \" %%f %%f %%f \", (%s)[0], (%s)[1], (%s)[2], (%s)[3]);\n",
			pEStringName, pArgName, pArgName, pArgName, pArgName);
		fprintf(pOutFile, "\t}\n\telse\n\t{\n\t\t\testrConcatf(%s, \" N N N N \");\n\t}\n",
			pEStringName);
		break;

	case ARGTYPE_MAT4_POINTER:
		fprintf(pOutFile, "\testrConcatf(%s, \" %%f %%f %%f %%f %%f %%f %%f %%f %%f %%f %%f %%f \", (*%s)[3][0], (*%s)[3][1], (*%s)[3][2], (*%s)[0][0], (*%s)[0][1], (*%s)[0][2], (*%s)[1][0], (*%s)[1][1], (*%s)[1][2], (*%s)[2][0], (*%s)[2][1], (*%s)[2][2]);\n", pEStringName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName);
		break;

	case ARGTYPE_MAT4_DIRECT:
		fprintf(pOutFile, "\testrConcatf(%s, \" %%f %%f %%f %%f %%f %%f %%f %%f %%f %%f %%f %%f \", %s[3][0], %s[3][1], %s[3][2], %s[0][0], %s[0][1], %s[0][2], %s[1][0], %s[1][1], %s[1][2], %s[2][0], %s[2][1], %s[2][2]);\n", pEStringName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName, pArgName);
		break;

	case ARGTYPE_QUAT_POINTER:
		fprintf(pOutFile, "\testrConcatf(%s, \" %%f %%f %%f %%f \", (*%s)[0], (*%s)[1], (*%s)[2], (*%s)[3]);\n",
			pEStringName, pArgName, pArgName, pArgName, pArgName);
		break;

	case ARGTYPE_QUAT_DIRECT:
		fprintf(pOutFile, "\testrConcatf(%s, \" %%f %%f %%f %%f\", %s[0], %s[1], %s[2], %s[3]);\n",
			pEStringName, pArgName, pArgName, pArgName, pArgName);
		break;

	case ARGTYPE_STRUCT:
		if (bPutStructsIntoCmdParseStructList)
		{
			fprintf(pOutFile, "\testrConcatf(%s, \" STRUCT(%%d)\", _iStructCount);\n", pEStringName);
			fprintf(pOutFile, "\tcmdAddToUnownedStructList(&_structList, parse_%s, %s);\n", pArgTypeName, pArgName);
			fprintf(pOutFile, "\t_iStructCount++;\n");
		}
		else
		{
			fprintf(pOutFile, "\testrConcatf(%s, \" \");\n", pEStringName);
			fprintf(pOutFile, "\tif (%s)\n\t\tParserWriteTextEscaped(%s, parse_%s, %s, %s, 0, 0);\n\telse\n\t\testrConcatf(%s, \"<& __NULL__ &>\");\n",
				pArgName, pEStringName, pArgTypeName, pArgName, 
				(pCommand->iCommandFlags & COMMAND_FLAG_FORCEWRITECURFILEINSTRUCTS) ? "WRITETEXTFLAG_FORCEWRITECURRENTFILE" : "0",
				pEStringName);
		}
		break;

	default:
		{
			if (eArgType < ARGTYPE_EXPR_FIRST)
			{
				char errorString[1024];
				sprintf(errorString, "Unknown data type %s", pArgTypeName);
				if (pCommand)
				{
					CommandAssert(pCommand, 0, errorString);
				}
				else
				{
					STATICASSERT(0, errorString);
				}
			}
		}
		break;
	}
		
	
}


void MagicCommandManager::WriteOutGenericCodeToPutArgumentsIntoPacket(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, 
   char *pPacketName)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		WriteOutGenericCodeToPutSingleArgumentIntoPacket(pOutFile, pCommand->ppArgs[i]->argType, pCommand->ppArgs[i]->argName, 
			pCommand->ppArgs[i]->argTypeName,
			pPacketName, pCommand);
	}
}


void MagicCommandManager::WriteOutGenericCodeToPutSingleArgumentIntoPacket(FILE *pOutFile, enumMagicCommandArgType eArgType,
	char *pArgName, char *pArgTypeName, char *pPacketName, MAGIC_COMMAND_STRUCT *pCommand)
{

	switch (eArgType)
	{
	case ARGTYPE_SINT:
	case ARGTYPE_UINT:
	case ARGTYPE_BOOL:
	case ARGTYPE_ENUM:
		fprintf(pOutFile, "\tpktSendBits(%s, 32, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_SINT64:
	case ARGTYPE_UINT64:
		fprintf(pOutFile, "\tpktSendBits64(%s, 64, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_FLOAT64:
		fprintf(pOutFile, "\tpktSendF64(%s, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_FLOAT:
		fprintf(pOutFile, "\tpktSendFloat(%s, %s);\n",
			pPacketName, pArgName);
		break;


	case ARGTYPE_STRING:
	case ARGTYPE_SENTENCE:
	case ARGTYPE_ESCAPEDSTRING:
		fprintf(pOutFile, "\tpktSendString(%s, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_VEC3_POINTER:
		fprintf(pOutFile, "\tpktSendVec3(%s, *%s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_VEC3_DIRECT:
		fprintf(pOutFile, "\tpktSendVec3(%s, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_VEC4_POINTER:
		fprintf(pOutFile, "\tpktSendVec4(%s, *%s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_VEC4_DIRECT:
		fprintf(pOutFile, "\tpktSendVec4(%s, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_MAT4_POINTER:
		fprintf(pOutFile, "\tpktSendMat4(%s, *%s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_MAT4_DIRECT:
		fprintf(pOutFile, "\tpktSendMat4(%s, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_QUAT_DIRECT:
		fprintf(pOutFile, "\tpktSendQuat(%s, %s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_QUAT_POINTER:
		fprintf(pOutFile, "\tpktSendQuat(%s, *%s);\n",
			pPacketName, pArgName);
		break;

	case ARGTYPE_STRUCT:
		fprintf(pOutFile, "\tif (%s)\n\t{\n\t\tpktSendBits(%s, 1, 1);\n\t\tParserSendStruct(parse_%s, %s, %s);\n\t}\n\telse\n\t{\n\t\tpktSendBits(%s, 1, 0);\n\t}\n",
			pArgName, pPacketName, pArgTypeName, pPacketName, pArgName, pPacketName);
		break;

	case ARGTYPE_PACKET:
		fprintf(pOutFile, "\tpktSendEntireTempPacket(%s, %s);\n", pPacketName, pArgName);
		break;

	default:
		{
			if (eArgType < ARGTYPE_EXPR_FIRST)
			{
				char errorString[1024];
				sprintf(errorString, "Unknown data type %s", pArgTypeName);
				if (pCommand)
				{
					CommandAssert(pCommand, 0, errorString);
				}
				else
				{
					STATICASSERT(0, errorString);
				}
			}
		}
		break;
	}
}

void MagicCommandManager::WriteOutGenericPrototypeToCallFunctionFromPacket(FILE *pFile, MAGIC_COMMAND_STRUCT *pCommand, char *pPacketName)
{
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		switch (pArg->argType)
		{

		case ARGTYPE_STRUCT:
			fprintf(pFile, "\t%s *%s=NULL;\n", pArg->argTypeName, pArg->argName);
			break;


		case ARGTYPE_STRING:
		case ARGTYPE_SENTENCE:
		case ARGTYPE_ESCAPEDSTRING:
			fprintf(pFile, "\tchar *%s;\n", pArg->argName);
			break;

		case ARGTYPE_ENUM:
			fprintf(pFile, "\tint %s;\n", pArg->argName);
            break;

		case ARGTYPE_PACKET:
			fprintf(pFile, "\tPacket *%s;\n", pArg->argName);
			break;

		default:
			fprintf(pFile, "\t%s %s;\n", 
				pArg->argTypeName, pArg->argName);
			break;
		}
	}

	fprintf(pFile, "\tPERFINFO_AUTO_START_FUNC();\n");

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];
	
		switch (pArg->argType)
		{

		case ARGTYPE_ENUM:
			fprintf(pFile, "\t%s = (int)pktGetBits(%s, 32);\n",
				pArg->argName, pPacketName);
			break;
		case ARGTYPE_SINT:
		case ARGTYPE_UINT:
		case ARGTYPE_BOOL:
			fprintf(pFile, "\t%s = (%s)pktGetBits(%s, 32);\n",
				pArg->argName, pArg->argTypeName, pPacketName);
			break;

		case ARGTYPE_SINT64:
		case ARGTYPE_UINT64:
			fprintf(pFile, "\t%s = pktGetBits64(%s, 64);\n",
				pArg->argName, pPacketName);
			break;

		case ARGTYPE_FLOAT64:
			fprintf(pFile, "\t%s = pktGetF64(%s, %s);\n",
				pArg->argName, pPacketName);
			break;

		case ARGTYPE_FLOAT:
			fprintf(pFile, "\t%s = pktGetFloat(%s);\n",
				pArg->argName, pPacketName);
			break;


		case ARGTYPE_STRING:
		case ARGTYPE_SENTENCE:
		case ARGTYPE_ESCAPEDSTRING:
			fprintf(pFile, "\t%s = pktGetStringTemp(%s);\n",
				pArg->argName, pPacketName);
			break;

		case ARGTYPE_VEC3_POINTER:
		case ARGTYPE_VEC3_DIRECT:
			fprintf(pFile, "\tpktGetVec3(%s, %s);\n",
				pPacketName, pArg->argName);
			break;

	
		case ARGTYPE_VEC4_POINTER:
		case ARGTYPE_VEC4_DIRECT:
			fprintf(pFile, "\tpktGetVec4(%s, %s);\n",
				pPacketName, pArg->argName);
			break;

		case ARGTYPE_MAT4_POINTER:
		case ARGTYPE_MAT4_DIRECT:
			fprintf(pFile, "\tpktGetMat4(%s, %s);\n",
				pPacketName, pArg->argName);
			break;

		case ARGTYPE_QUAT_DIRECT:
		case ARGTYPE_QUAT_POINTER:
			fprintf(pFile, "\tpktGetQuat(%s, %s);\n",
				pPacketName, pArg->argName);
			break;

		case ARGTYPE_STRUCT:
			fprintf(pFile, "\tif (pktGetBits(%s, 1))\n\t{\n\t\t%s = StructCreateVoid(parse_%s);\n\t\tParserRecv(parse_%s, %s, %s, 0);\n\t}\n",
				pPacketName, pArg->argName, pArg->argTypeName, pArg->argTypeName, pPacketName, pArg->argName);
			break;

		case ARGTYPE_PACKET:
			fprintf(pFile, "\t%s = pktCreateAndGetEntireTempPacket(%s);\n", pArg->argName, pPacketName);
			break;
		}
	}

	fprintf(pFile, "\n\t%s(", pCommand->functionName);
	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];
	
		if (IsPointerType(pArg->argType) || pArg->argFlags & ARGFLAG_OWNABLE)
		{
			fprintf(pFile, "&");
		}
		fprintf(pFile, "%s%s", pArg->argName, i == eaSize(&pCommand->ppArgs) - 1 ? "" : ", ");
	}
	fprintf(pFile, ");\n");

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];

		if (pArg->argType == ARGTYPE_STRUCT)
		{
	
			fprintf(pFile, "\tif (%s)\n\t{\n\t\tStructDestroy(parse_%s, %s);\n\t}\n",
				pArg->argName, pArg->argTypeName, pArg->argName);
		}
		else if (pArg->argType == ARGTYPE_PACKET)
		{
			fprintf(pFile, "\tpktFree(&%s);\n", pArg->argName);
		}
	}

	fprintf(pFile, "\tPERFINFO_AUTO_STOP();\n");


}

bool MagicCommandManager::CommandGetsWrittenOutForClientOrServerWrapper(MAGIC_COMMAND_STRUCT *pCommand)
{
	if (CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand))
	{
		return false;
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
	{
		return false;
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE)
	{
		return false;
	}

	if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
	{
		return false;
	}

	if (pCommand->commandsWhichThisIsTheErrorFunctionFor[0][0])
	{
		return false;
	}


	return true;
}

void MagicCommandManager::WriteOutPrototypeForServerWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	fprintf(pOutFile, "void ServerCmd_%s(", pCommand->safeCommandName);
	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false, false);

	fprintf(pOutFile, ")");
}

void MagicCommandManager::WriteOutBodyForServerWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	bool bHasStructs = CommandHasArgOfType(pCommand, ARGTYPE_STRUCT);
	if (pCommand->iNumDefines)
	{
		int i;

		fprintf(pOutFile, "#if ");

		for (i=0; i < pCommand->iNumDefines; i++)
		{
			fprintf(pOutFile, "%sdefined(%s)", 
				i > 0 ? "|| " : "", pCommand->defines[i]);
		}

		fprintf(pOutFile, "\n");
	}
	else
	{
		fprintf(pOutFile, "#ifdef GAMECLIENT\n");
	}

	WriteOutPrototypeForServerWrapper(pOutFile, pCommand);

	fprintf(pOutFile, "\n{\n\tchar *pAUTOGENWorkString = NULL;\n");

	if (bHasStructs)
	{
		fprintf(pOutFile, "\tint _iStructCount = 0;\n");
		fprintf(pOutFile, "\tCmdParseStructList _structList = {0};\n");
	}


	fprintf(pOutFile, "\testrStackCreate(&pAUTOGENWorkString);\n");
	fprintf(pOutFile, "\testrCopy2(&pAUTOGENWorkString, \"%s \");\n", pCommand->commandName);

	WriteOutGenericCodeToPutArgumentsIntoEString(pOutFile, pCommand, "&pAUTOGENWorkString", true, true);

	fprintf(pOutFile, "\tcmdSendCmdClientToServer(pAUTOGENWorkString, %s, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_SERVERWRAPPER, %s);\n", 
		pCommand->iCommandFlags & COMMAND_FLAG_PRIVATE ? "true" : "false", 
		bHasStructs ? "&_structList" : "NULL");

	if (bHasStructs)
	{
		fprintf(pOutFile, "\tcmdDestroyUnownedStructList(&_structList);\n");
	}

	fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n}\n");



	fprintf(pOutFile, "#endif\n");
}
	



void MagicCommandManager::WriteOutFakeIncludes(FILE *pOutFile, int iFlagToMatch)
{
	if (SlowSafeDependencyMode())
	{

		fprintf(pOutFile, "//#ifed-out includes to fool incredibuild dependency generation\n#if 0\n");
		int iCommandNum;

		for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
		{
			MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

			if (pCommand->iCommandFlags & iFlagToMatch || !iFlagToMatch)
			{
				fprintf(pOutFile, "#include \"%s\"\n", pCommand->sourceFileName);
			}
		}
		fprintf(pOutFile, "#endif\n");
	}
}

void MagicCommandManager::WriteOutClientWrappers(void)
{
	FILE *pOutFile = fopen_nofail(m_ClientWrappersFileName, "wt");


	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"cmdparse.h\"\n#include \"textparser.h\"\n\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_CLIENT_WRAPPER);

	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_WRAPPER)
		{
			CommandAssert(pCommand, CommandGetsWrittenOutForClientOrServerWrapper(pCommand), "Invalid CLIENTCMD request");

			WriteOutBodyForClientWrapper(pOutFile, pCommand, !!(pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_WRAPPER_FAST));
		}
	}

	fclose(pOutFile);


	pOutFile = fopen_nofail(m_ClientWrappersHeaderFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_CLIENT_WRAPPER);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];
	if (pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_WRAPPER)
		{
			WriteOutPrototypeForClientWrapper(pOutFile, pCommand, !!(pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_WRAPPER_FAST));
			fprintf(pOutFile, ";\n");
		}
	}

	fclose(pOutFile);
}



void MagicCommandManager::WriteOutPrototypeForClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, bool bFast)
{
	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	fprintf(pOutFile, "void ClientCmd_%s(Entity *pEntity", pCommand->safeCommandName);
	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, true, false);

	fprintf(pOutFile, ")");
}

void MagicCommandManager::WriteOutBodyForClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand, bool bFast)
{
	bool bHasStructs = CommandHasArgOfType(pCommand, ARGTYPE_STRUCT);


	if (pCommand->iNumDefines)
	{
		int i;

		fprintf(pOutFile, "#if ");

		for (i=0; i < pCommand->iNumDefines; i++)
		{
			fprintf(pOutFile, "%sdefined(%s)", 
				i > 0 ? "|| " : "", pCommand->defines[i]);
		}

		fprintf(pOutFile, "\n");
	}
	else
	{
		fprintf(pOutFile, "#ifdef GAMESERVER\n");
	}

	WriteOutPrototypeForClientWrapper(pOutFile, pCommand,  !!(pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_WRAPPER_FAST));

	fprintf(pOutFile, "\n{\n");
	fprintf(pOutFile, "\tchar *pAUTOGENWorkString = NULL;\n");
	if (bHasStructs)
	{
		fprintf(pOutFile, "\tint _iStructCount = 0;\n");
		fprintf(pOutFile, "\tCmdParseStructList _structList = {0};\n");
	}

	fprintf(pOutFile, "\tif(!pEntity || !entGetPlayer(pEntity)) return;\n");

	fprintf(pOutFile, "\tPERFINFO_AUTO_START_FUNC();\n");

	fprintf(pOutFile, "\testrStackCreate(&pAUTOGENWorkString);\n");
	fprintf(pOutFile, "\testrCopy2(&pAUTOGENWorkString, \"%s \");\n", pCommand->commandName);
	WriteOutGenericCodeToPutArgumentsIntoEString(pOutFile, pCommand, "&pAUTOGENWorkString",  true, true);

	fprintf(pOutFile, "\tcmdSendCmdServerToClient(pEntity, pAUTOGENWorkString, %s, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_CLIENTWRAPPER, %s, %s);\n", 
		pCommand->iCommandFlags & COMMAND_FLAG_PRIVATE ? "true" : "false", 
		pCommand->iCommandFlags & COMMAND_FLAG_CLIENT_WRAPPER_FAST ? "true" : "false",
		bHasStructs ? "&_structList" : "NULL");

	if (bHasStructs)
	{
		fprintf(pOutFile, "\tcmdDestroyUnownedStructList(&_structList);\n");
	}

	fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n\tPERFINFO_AUTO_STOP();\n}\n");

	fprintf(pOutFile, "#endif\n");
}




void MagicCommandManager::WriteOutServerWrappers(void)
{
	FILE *pOutFile = fopen_nofail(m_ServerWrappersFileName, "wt");


	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"textparser.h\"\n\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_SERVER_WRAPPER);
	
	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_SERVER_WRAPPER)
		{
			CommandAssert(pCommand, CommandGetsWrittenOutForClientOrServerWrapper(pCommand), "Invalid CLIENTCMD request");

			WriteOutBodyForServerWrapper(pOutFile, pCommand);
		}	
	}

	fclose(pOutFile);


	pOutFile = fopen_nofail(m_ServerWrappersHeaderFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_SERVER_WRAPPER);
	
	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_SERVER_WRAPPER)
		{
			WriteOutPrototypeForServerWrapper(pOutFile, pCommand);
			fprintf(pOutFile, ";\n");
		}
	}

	fclose(pOutFile);
}


void MagicCommandManager::WriteOutClientToTestClientWrappers(void)
{
	FILE *pOutFile = fopen_nofail(m_ClientToTestClientWrappersFileName, "wt");
	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"textparser.h\"\n#include \"GameClientLib.h\"\n#include \"net.h\"\n#include \"testclient_comm.h\"\n\n");

	WriteOutFakeIncludes(pOutFile, 0);

	fprintf(pOutFile, "#ifdef CLIENT_TO_TESTCLIENT_COMMANDS\n");

	
	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		AssertCommandIsOKForClientToTestClient(pCommand);

		WriteOutBodyForClientToTestClientWrapper(pOutFile, pCommand);			
	}

	fprintf(pOutFile, "\n#endif\n");

	fclose(pOutFile);


	pOutFile = fopen_nofail(m_ClientToTestClientWrappersHeaderFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n");
	WriteOutFakeIncludes(pOutFile, 0);
	
	fprintf(pOutFile, "#ifdef CLIENT_TO_TESTCLIENT_COMMANDS\n");

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		WriteOutPrototypeForClientToTestClientWrapper(pOutFile, pCommand);
		fprintf(pOutFile, ";\n");
	}

	fprintf(pOutFile, "\n#else\n");

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		WriteOutIfDefForIgnoringClientToTestClientWrapper(pOutFile, pCommand);
	}

	fprintf(pOutFile, "\n#endif\n");
	

	fclose(pOutFile);
}



bool MagicCommandManager::AtLeastOneCommandHasFlag(int iFlag)
{
	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & iFlag)
		{
			return true;
		}
	}

	return false;

}


bool MagicCommandManager::CommandIsInCategory(MAGIC_COMMAND_STRUCT *pCommand, char *pCategoryName)
{
	int i;

	if (_stricmp(pCategoryName, "all") == 0)
	{
		return (pCommand->iCommandFlags & COMMAND_FLAG_HIDE) == 0;
	}

	if (_stricmp(pCategoryName, "hidden") == 0)
	{
		return (pCommand->iCommandFlags & COMMAND_FLAG_HIDE) != 0;
	}

	if (pCategoryName[0] == 0)
	{
		return pCommand->commandCategories[0][0] == 0 && (pCommand->iCommandFlags & COMMAND_FLAG_HIDE) == 0;;
	}


	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommand->commandCategories[i][0] == 0)
		{
			return false;
		}

		if (strcmp(pCommand->commandCategories[i], pCategoryName) == 0)
		{
			return true;
		}
	}

	return false;
}

bool MagicCommandManager::CommandVarIsInCategory(MAGIC_COMMANDVAR_STRUCT *pCommandVar, char *pCategoryName)
{
	int i;

	if (_stricmp(pCategoryName, "all") == 0)
	{
		return (pCommandVar->iCommandFlags & COMMAND_FLAG_HIDE) == 0;
	}

	if (_stricmp(pCategoryName, "hidden") == 0)
	{
		return (pCommandVar->iCommandFlags & COMMAND_FLAG_HIDE) != 0;
	}

	if (pCategoryName[0] == 0)
	{
		return pCommandVar->commandCategories[0][0] == 0;
	}


	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommandVar->commandCategories[i][0] == 0)
		{
			return false;
		}

		if (strcmp(pCommandVar->commandCategories[i], pCategoryName) == 0)
		{
			return true;
		}
	}

	return false;
}




bool MagicCommandManager::CommandCategoryWritten(char *pCategoryName)
{
	int i;

	if (pCategoryName[0] == 0)
	{
		return true;
	}

	for (i=0; i < m_iNumCategoriesWritten; i++)
	{
		if (strcmp(m_CategoriesWritten[i], pCategoryName) == 0)
		{
			return true;
		}
	}

	return false;
}



void MagicCommandManager::AddCommandToSet(MAGIC_COMMAND_STRUCT *pCommand, char *pSetName, Tokenizer *pTokenizer)
{
	int i;

	for (i=0; i < MAX_COMMAND_SETS; i++)
	{
		if (pCommand->commandSets[i][0] == 0)
		{
			strcpy(pCommand->commandSets[i], pSetName);
			break;
		}
	}

	ASSERT(pTokenizer,i < MAX_COMMAND_SETS, "Too many sets for one command");

	if (pCommand->commandCategories[0][0] == 0)
	{
		AddCommandToCategory(pCommand, pSetName, pTokenizer);
	}
}


void MagicCommandManager::AddCommandToCategory(MAGIC_COMMAND_STRUCT *pCommand, char *pCategoryName, Tokenizer *pTokenizer)
{
	int i;

	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommand->commandCategories[i][0] == 0)
		{
			strcpy(pCommand->commandCategories[i], pCategoryName);
			MakeStringLowercase(pCommand->commandCategories[i]);
			break;
		}
	}

	ASSERT(pTokenizer,i < MAX_COMMAND_CATEGORIES, "Too many categories for one command");

}


void MagicCommandManager::AddCommandVarToSet(MAGIC_COMMANDVAR_STRUCT *pCommandVar, char *pSetName, Tokenizer *pTokenizer)
{
	int i;

	for (i=0; i < MAX_COMMAND_SETS; i++)
	{
		if (pCommandVar->commandSets[i][0] == 0)
		{
			strcpy(pCommandVar->commandSets[i], pSetName);
			break;
		}
	}

	ASSERT(pTokenizer,i < MAX_COMMAND_SETS, "Too many sets for one command");

	if (pCommandVar->commandCategories[0][0] == 0)
	{
		AddCommandVarToCategory(pCommandVar, pSetName, pTokenizer);
	}
}


void MagicCommandManager::AddCommandVarToCategory(MAGIC_COMMANDVAR_STRUCT *pCommandVar, char *pCategoryName, Tokenizer *pTokenizer)
{
	int i;

	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommandVar->commandCategories[i][0] == 0)
		{
			ASSERT(pTokenizer, !StringBeginsWith(pCommandVar->commandCategories[i], CONTROLLER_AUTO_SETTING_PREFIX, true), "Command can't have ACMD_CONTROLLER_AUTO_SETTING and ACMD_CATEGORY");
		}
	}


	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		if (pCommandVar->commandCategories[i][0] == 0)
		{
			strcpy(pCommandVar->commandCategories[i], pCategoryName);
			MakeStringLowercase(pCommandVar->commandCategories[i]);
			break;
		}
	}

	ASSERT(pTokenizer,i < MAX_COMMAND_CATEGORIES, "Too many categories for one command");

}

void MagicCommandManager::AddCommandVarToCategory_ControllerAutoSetting(MAGIC_COMMANDVAR_STRUCT *pCommandVar, char *pCategoryName, Tokenizer *pTokenizer)
{
	int i;

	for (i=0; i < MAX_COMMAND_CATEGORIES; i++)
	{
		ASSERT(pTokenizer, pCommandVar->commandCategories[i][0] == 0, "Command can't have ACMD_CONTROLLER_AUTO_SETTING and ACMD_CATEGORY");
	}

	sprintf(pCommandVar->commandCategories[0], "%s%s", CONTROLLER_AUTO_SETTING_PREFIX, pCategoryName);
}


bool MagicCommandManager::CurrentProjectIsTestClient(void)
{
	if (strstr(m_ProjectName, "TestClient"))
	{
		return true;
	}

	return false;
}

void MagicCommandManager::AssertCommandIsOKForClientToTestClient(MAGIC_COMMAND_STRUCT *pCommand)
{
	CommandAssert(pCommand, pCommand->eReturnType == ARGTYPE_NONE, "Functions with non-void return can not be AUTO_COMMANDS on the TestClient");

	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		CommandAssert(pCommand, pCommand->ppArgs[i]->argType < ARGTYPE_EXPR_FIRST, "TestClient AUTO_COMMANDs can't have special arg types");
	}
}

void MagicCommandManager::WriteOutIfDefForIgnoringClientToTestClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	fprintf(pOutFile, "#define TestClientCmd_%s(", pCommand->commandName);
	int i;

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		fprintf(pOutFile, "%s%s", i == 0 ? "" : ", ", pCommand->ppArgs[i]->argName);
	}

	fprintf(pOutFile, ")\n");
}


	
void MagicCommandManager::WriteOutPrototypeForClientToTestClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	fprintf(pOutFile, "void TestClientCmd_%s(", pCommand->safeCommandName);
	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, false, false);

	fprintf(pOutFile, ")");
}

void MagicCommandManager::WriteOutBodyForClientToTestClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutPrototypeForClientToTestClientWrapper(pOutFile, pCommand);

	fprintf(pOutFile, "\n{\n\tNetLink *pTestClientLink = gclGetLinkToTestClient();\n\tif (pTestClientLink)\n\t{\n\t\tPacket *pPak = pktCreate(pTestClientLink, TO_TESTCLIENT_CMD_COMMAND);\n");
	fprintf(pOutFile, "\t\tchar *pAUTOGENWorkString = NULL;\n\t\testrStackCreate(&pAUTOGENWorkString);\n");
	fprintf(pOutFile, "\t\testrCopy2(&pAUTOGENWorkString, \"%s \");\n", pCommand->commandName);
	
	WriteOutGenericCodeToPutArgumentsIntoEString(pOutFile, pCommand, "&pAUTOGENWorkString", false, false);
	
	fprintf(pOutFile, "\t\tpktSendString(pPak, pAUTOGENWorkString);\n\t\tpktSend(&pPak);\n");

	fprintf(pOutFile, "\t\testrDestroy(&pAUTOGENWorkString);\n\t}\n}\n");
}

char *MagicCommandManager::GetShortCodeForExprArgType(MAGIC_COMMAND_STRUCT *pCommand, enumMagicCommandArgType eType, int iFlags)
{
	switch(eType)
	{
	case ARGTYPE_NONE:
		return "VOID";
	case ARGTYPE_SINT:
		return "S32";
	case ARGTYPE_UINT:
		return "U32";
	case ARGTYPE_BOOL:
		return "BOOL";
	case ARGTYPE_FLOAT:
		return "F32";
	case ARGTYPE_SINT64:
		return "S64";
	case ARGTYPE_UINT64:
		return "U64";
	case ARGTYPE_FLOAT64:
		return "F64";
	case ARGTYPE_STRING:
		return "STR";
	case ARGTYPE_SENTENCE:
		return "SENT";
	case ARGTYPE_ESCAPEDSTRING:
		return "ESCSTR";
	case ARGTYPE_VEC3_POINTER:
		return "VEC3P";
	case ARGTYPE_VEC3_DIRECT:
		return "VEC3D";
	case ARGTYPE_VEC4_POINTER:
		return "VEC4P";
	case ARGTYPE_VEC4_DIRECT:
		return "VEC4D";
	case ARGTYPE_MAT4_POINTER:
		return "MAT4P";
	case ARGTYPE_MAT4_DIRECT:
		return "MAT4D";
	case ARGTYPE_QUAT_POINTER:
		return "QUATP";
	case ARGTYPE_QUAT_DIRECT:
		return "QUATD";
	case ARGTYPE_STRUCT:
		return "STRUCT";
	case ARGTYPE_ENUM :
		return "ENUM";
	case ARGTYPE_EXPR_SUBEXPR_IN:
		return "SUBEXI";
	case ARGTYPE_EXPR_ENTARRAY_IN:
		return "ENTAI";
	case ARGTYPE_EXPR_LOC_MAT4_IN:
		return "LMAT4I";
	case ARGTYPE_EXPR_EXPRCONTEXT:
		return "EXPRCX";
	case ARGTYPE_EXPR_PARTITION:
		return "EXPRPRT";
	case ARGTYPE_EXPR_ERRSTRING:
		return "ERRSTR";
	case ARGTYPE_EXPR_ERRSTRING_STATIC:
		return "ERRSST";
	case ARGTYPE_EXPR_INT_OUT:
		return "INTOUT";
	case ARGTYPE_EXPR_FLOAT_OUT:
		return "FLTOUT";
	case ARGTYPE_EXPR_STRING_OUT:
		return "STROUT";
	case ARGTYPE_EXPR_LOC_MAT4_OUT:
		return "LMAT4O";
	case ARGTYPE_EXPR_VEC4_OUT:
		return "VEC4OUT";
	case ARGTYPE_EXPR_ENTARRAY_IN_OUT:
		return "ENTAIO";
	case ARGTYPE_EXPR_ENTARRAY_OUT:
		return "ENTAO";
	case ARGTYPE_EXPR_FUNCRETURNVAL:
		return "FRETV";
	case ARGTYPE_EXPR_CMULTIVAL:
		return "CMULTIVAL";
	case ARGTYPE_ENTITY:
		if (iFlags & COMMAND_EXPR_FLAG_SELF_PTR)
		{
			return "SELF";
		}
		else
		{
			return "ENT";
		}
	}

	CommandAssert(pCommand, 0,  "Unsupported arg type for expression prototype coding");

	return NULL;
}

char *MagicCommandManager::GetFullExprCode(MAGIC_COMMAND_STRUCT *pCommand)
{
	char retVal[2048]; //must be at least 7 * numargs + slop bytes, 2048 should be plenty
	retVal[0] = 0;
	int i;

	if (pCommand->pExprCode)
	{
		return pCommand->pExprCode;
	}

	sprintf(retVal, "%s", GetShortCodeForExprArgType(pCommand, pCommand->eReturnType, 0));

	for (i=0; i < eaSize(&pCommand->ppArgs); i++)
	{
		MAGIC_COMMAND_ARG *pArg = pCommand->ppArgs[i];
		strcat(retVal, "_");
		strcat(retVal, GetShortCodeForExprArgType(pCommand, pArg->argType, pArg->iExpressionArgFlags));
	}

	pCommand->pExprCode = STRDUP(retVal);

	return pCommand->pExprCode;
}


MagicCommandManager::MAGIC_COMMAND_STRUCT *MagicCommandManager::FindCommandByFuncName(char *pFuncName)
{
	int i;

	for (i=0; i < eaSize(&m_ppMagicCommands); i++)
	{
		if (strcmp(m_ppMagicCommands[i]->functionName, pFuncName) == 0)
		{
			return m_ppMagicCommands[i];
		}
	}

	return NULL;
}

bool MagicCommandManager::CommandCanHaveErrorFunction(MAGIC_COMMAND_STRUCT *pCommand)
{
	if (pCommand->iCommandFlags & COMMAND_FLAG_EXPR_WRAPPER)
	{
		return false;
	}

	return true;
}

void MagicCommandManager::WriteOutGenericClientWrappers(void)
{
	FILE *pOutFile = fopen_nofail(m_GClientWrappersFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"cmdparse.h\"\n#include \"textparser.h\"\n\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_GENERICCLIENT_WRAPPER);

	int iCommandNum;
	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_GENERICCLIENT_WRAPPER)
		{
			// Reuse of ClientOrServer check
			CommandAssert(pCommand, CommandGetsWrittenOutForClientOrServerWrapper(pCommand), "Invalid GENERICCLIENTCMD request");
			WriteOutBodyForGenericClientWrapper(pOutFile, pCommand);
		}
	}
	fclose(pOutFile);

	pOutFile = fopen_nofail(m_GClientWrappersHeaderFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_GENERICCLIENT_WRAPPER);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];
		if (pCommand->iCommandFlags & COMMAND_FLAG_GENERICCLIENT_WRAPPER)
		{
			WriteOutPrototypeForGenericClientWrapper(pOutFile, pCommand);
			fprintf(pOutFile, ";\n");
		}
	}
	fclose(pOutFile);
}

void MagicCommandManager::WriteOutPrototypeForGenericClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	fprintf(pOutFile, "void GClientCmd_%s(U32 uID", pCommand->safeCommandName);
	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, true, false);

	fprintf(pOutFile, ")");
}

void MagicCommandManager::WriteOutBodyForGenericClientWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	bool bHasStructs = CommandHasArgOfType(pCommand, ARGTYPE_STRUCT);

	if (pCommand->iNumDefines)
	{
		int i;
		fprintf(pOutFile, "#if ");

		for (i=0; i < pCommand->iNumDefines; i++)
		{
			fprintf(pOutFile, "%sdefined(%s)", 
				i > 0 ? "|| " : "", pCommand->defines[i]);
		}
		fprintf(pOutFile, "\n");
	}

	WriteOutPrototypeForGenericClientWrapper(pOutFile, pCommand);

	fprintf(pOutFile, "\n{\n");
	fprintf(pOutFile, "\tchar *pAUTOGENWorkString = NULL;\n");
	if (bHasStructs)
	{
		fprintf(pOutFile, "\tint _iStructCount = 0;\n");
		fprintf(pOutFile, "\tCmdParseStructList _structList = {0};\n");
	}
	fprintf(pOutFile, "\testrStackCreate(&pAUTOGENWorkString);\n");
	fprintf(pOutFile, "\testrCopy2(&pAUTOGENWorkString, \"%s \");\n", pCommand->commandName);
	WriteOutGenericCodeToPutArgumentsIntoEString(pOutFile, pCommand, "&pAUTOGENWorkString",  true, false);

	fprintf(pOutFile, "\tcmdSendCmdGenericServerToClient(uID, pAUTOGENWorkString, %s, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_CLIENTWRAPPER, %s);\n", 
		pCommand->iCommandFlags & COMMAND_FLAG_PRIVATE ? "true" : "false", 
		bHasStructs ? "&_structList" : "NULL");

	if (bHasStructs)
	{
		fprintf(pOutFile, "\tcmdDestroyUnownedStructList(&_structList);\n");
	}

	fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n}\n");
	if (pCommand->iNumDefines)
		fprintf(pOutFile, "#endif\n");
}


bool MagicCommandManager::CommandGetsWrittenOutForGenericServerWrapper(MAGIC_COMMAND_STRUCT *pCommand)
{
	if (CommandHasExpressionOnlyArgumentsOrReturnVals(pCommand))
	{
		return false;
	}
	if (pCommand->iCommandFlags & COMMAND_FLAG_EARLYCOMMANDLINE)
	{
		return false;
	}
	// This works for REMOTE_COMMAND with NO return value
	if (pCommand->iCommandFlags & COMMAND_FLAG_REMOTE && pCommand->eReturnType != ARGTYPE_NONE)
	{
		return false;
	}
	if (pCommand->iCommandFlags & COMMAND_FLAG_QUEUED)
	{
		return false;
	}
	if (pCommand->commandsWhichThisIsTheErrorFunctionFor[0][0])
	{
		return false;
	}
	return true;
}

void MagicCommandManager::WriteOutGenericServerWrappers(void)
{
	FILE *pOutFile = fopen_nofail(m_GServerWrappersFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#include \"cmdparse.h\"\n#include \"GlobalTypes.h\"\n#include \"textparser.h\"\n\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_GENERICSERVER_WRAPPER);

	int iCommandNum;

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_GENERICSERVER_WRAPPER)
		{
			CommandAssert(pCommand, CommandGetsWrittenOutForGenericServerWrapper(pCommand), "Invalid GENERICSERVERCMD request");

			WriteOutBodyForGenericServerWrapper(pOutFile, pCommand);
		}	
	}
	fclose(pOutFile);

	pOutFile = fopen_nofail(m_GServerWrappersHeaderFileName, "wt");

	fprintf(pOutFile, "//This file is autogenerated. autogenerated""nocheckin\n#pragma once\nGCC_SYSTEM\n");
	WriteOutFakeIncludes(pOutFile, COMMAND_FLAG_GENERICSERVER_WRAPPER);

	for (iCommandNum = 0; iCommandNum < eaSize(&m_ppMagicCommands); iCommandNum++)
	{
		MAGIC_COMMAND_STRUCT *pCommand = m_ppMagicCommands[iCommandNum];

		if (pCommand->iCommandFlags & COMMAND_FLAG_GENERICSERVER_WRAPPER)
		{
			WriteOutPrototypeForGenericServerWrapper(pOutFile, pCommand);
			fprintf(pOutFile, ";\n");
		}
	}
	fclose(pOutFile);
}

void MagicCommandManager::WriteOutPrototypeForGenericServerWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	WriteOutGenericExternsAndPrototypesForCommand(pOutFile, pCommand, false);

	fprintf(pOutFile, "void GServerCmd_%s(GlobalType eServerType", pCommand->safeCommandName);
	WriteOutGenericArgListForCommand(pOutFile, pCommand, false, true, false);

	fprintf(pOutFile, ")");
}

void MagicCommandManager::WriteOutBodyForGenericServerWrapper(FILE *pOutFile, MAGIC_COMMAND_STRUCT *pCommand)
{
	bool bHasStructs = CommandHasArgOfType(pCommand, ARGTYPE_STRUCT);
	if (pCommand->iNumDefines)
	{
		int i;

		fprintf(pOutFile, "#if ");

		for (i=0; i < pCommand->iNumDefines; i++)
		{
			fprintf(pOutFile, "%sdefined(%s)", 
				i > 0 ? "|| " : "", pCommand->defines[i]);
		}

		fprintf(pOutFile, "\n");
	}

	WriteOutPrototypeForGenericServerWrapper(pOutFile, pCommand);

	fprintf(pOutFile, "\n{\n\tchar *pAUTOGENWorkString = NULL;\n");
	if (bHasStructs)
	{
		fprintf(pOutFile, "\tint _iStructCount = 0;\n");
		fprintf(pOutFile, "\tCmdParseStructList _structList = {0};\n");
	}
	fprintf(pOutFile, "\testrStackCreate(&pAUTOGENWorkString);\n");
	fprintf(pOutFile, "\testrCopy2(&pAUTOGENWorkString, \"%s \");\n", pCommand->commandName);

	WriteOutGenericCodeToPutArgumentsIntoEString(pOutFile, pCommand, "&pAUTOGENWorkString", true, true);

	fprintf(pOutFile, "\tcmdSendCmdGenericToServer(eServerType, pAUTOGENWorkString, %s, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_SERVERWRAPPER, %s);\n", 
		pCommand->iCommandFlags & COMMAND_FLAG_PRIVATE ? "true" : "false",
		bHasStructs ? "&_structList" : "NULL");

	if (bHasStructs)
	{
		fprintf(pOutFile, "\tcmdDestroyUnownedStructList(&_structList);\n");
	}
	fprintf(pOutFile, "\testrDestroy(&pAUTOGENWorkString);\n}\n");
	if (pCommand->iNumDefines)
		fprintf(pOutFile, "#endif\n");
}

void MagicCommandManager::ReadGlobalTypes_Parsable(Tokenizer *pTokenizer, AUTO_SETTING_GLOBAL_TYPES *pGlobalTypes)
{
	pGlobalTypes->iNumGlobalTypes = pTokenizer->AssertGetInt();
	int i;

	for (i = 0; i < pGlobalTypes->iNumGlobalTypes; i++)
	{
		pTokenizer->AssertGetString(pGlobalTypes->GlobalTypeNames[i], MAX_GLOBALTYPES_NAME_LEN);
	}

}

void MagicCommandManager::WriteGlobalTypes_Parsable(FILE *pFile, AUTO_SETTING_GLOBAL_TYPES *pGlobalTypes)
{
	int i;
	fprintf(pFile, " %d ", pGlobalTypes->iNumGlobalTypes);

	for (i = 0; i < pGlobalTypes->iNumGlobalTypes; i++)
	{
		fprintf(pFile, "\"%s\" ", pGlobalTypes->GlobalTypeNames[i]);
	}
}

char *MagicCommandManager::GetProductString(char ***pppProductEarray)
{
	static char *pRetString = NULL;
	int iSize = eaSize(pppProductEarray);
	int i;

	if (!iSize)
	{
		return "NULL";
	}

	if (iSize == 0 && stricmp((*pppProductEarray)[0], "all") == 0)
	{
		return "NULL";
	}

	estrClear(&pRetString);
	estrConcatf(&pRetString, "\"");
	for (i = 0; i < eaSize(pppProductEarray); i++)
	{
		estrConcatf(&pRetString, "%s%s", i == 0 ? "" : ", ", (*pppProductEarray)[i]);
	}
	estrConcatf(&pRetString, "\"");

	return pRetString;
}
