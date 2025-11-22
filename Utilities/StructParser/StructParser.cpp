// StructParser.cpp : Defines the entry point for the console application.
//

#include "stdio.h"
#include "assert.h"
#include "tokenizer.h"
#include "structparser.h"
#include "windows.h"
#include "identifierdictionary.h"
#include "magiccommandmanager.h"
#include "strutils.h"
#include "filelistloader.h"
#include "sourceparser.h"
#include "autorunmanager.h"
#include "estring.h"

#define MAX_TABS 32
#define TAB_WIDTH 4

#define NUMTABS(len) (MAX_TABS - (((int)(len)) + 1) / TAB_WIDTH)

bool DumpBitFieldFixups_align;

#define AUTOSTRUCT_EXTRA_DATA "AST"
#define AUTOSTRUCT_EXCLUDE "NO_AST"

#define AUTOSTRUCT_IGNORE "AST_IGNORE"
#define AUTOSTRUCT_IGNORE_STRUCTPARAM "AST_IGNORE_STRUCTPARAM"
#define AUTOSTRUCT_IGNORE_STRUCT "AST_IGNORE_STRUCT"

#define AUTOSTRUCT_START "AST_START"
#define AUTOSTRUCT_STOP "AST_STOP"

#define AUTOSTRUCT_STARTTOK "AST_STARTTOK"
#define AUTOSTRUCT_ENDTOK "AST_ENDTOK"

#define AUTOSTRUCT_NO_UNRECOGNIZED "AST_NO_UNRECOGNIZED"
#define AUTOSTRUCT_STRIP_UNDERSCORES "AST_STRIP_UNDERSCORES"
#define AUTOSTRUCT_NO_PREFIX_STRIPPING "AST_NO_PREFIX_STRIP"
#define AUTOSTRUCT_FORCE_USE_ACTUAL_FIELD_NAME "AST_FORCE_USE_ACTUAL_FIELD_NAME"
#define AUTOSTRUCT_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT "AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT"
#define AUTOSTRUCT_CONTAINER "AST_CONTAINER"
#define AUTOSTRUCT_FORCE_CONST "AST_FORCE_CONST"

#define AUTOSTRUCT_WIKI_COMMENT "WIKI"

#define STRUCTPARSER_PREFIX "parse_"

#define AUTOSTRUCT_MACRO "AST_MACRO"
#define AUTOSTRUCT_PREFIX "AST_PREFIX"
#define AUTOSTRUCT_SUFFIX "AST_SUFFIX"

#define AUTOSTRUCT_FORMATSTRING "AST_FORMATSTRING"

#define AUTOSTRUCT_NONCONST_PREFIXSUFFIX "AST_NONCONST_PREFIXSUFFIX"

#define AUTOSTRUCT_NOT "AST_NOT"

#define AUTOSTRUCT_FOR_ALL "AST_FOR_ALL"

#define AUTOSTRUCT_COMMAND_BETWEEN_FIELDS "AST_COMMAND"

#define AUTO_STRUCT_FIXUP_FUNC "AST_FIXUPFUNC"

#define AUTOSTRUCT_SINGLETHREADED_MEMPOOL "AST_SINGLETHREADED_MEMPOOL"
#define AUTOSTRUCT_THREADSAFE_MEMPOOL "AST_THREADSAFE_MEMPOOL"
#define AUTOSTRUCT_NOMEMTRACKING "AST_NOMEMTRACKING"

#define AUTOSTRUCT_CREATION_COMMENT_FIELD "AST_CREATION_COMMENT_FIELD"

#define AUTOSTRUCT_RUNTIME_MODIFIED "AST_RUNTIME_MODIFIED"

#define AUTOSTRUCT_SAVE_ORIGINAL_CASE "AST_SAVE_ORIGINAL_CASE_OF_FIELD_NAMES"


#define MAX_NOT_STRINGS 8


char gFileEndString[] = "END_OF_FILE";

static char *sOutlawedTypeNameList[] =
{
	"F64",
	NULL
};


static char *sFloatNameList[] =
{
	"float",
	"F32",
	NULL
};

static char *sIntNameList[] =
{
	"int",
	"U32",
	"S32",
	"U16",
	"S16",
	"bool",
	"U64",
	"S64",
	"U8",
	"S8",
	"ContainerID",
	"DWORD",
	"size_t",
	"__time32_t",
	"DirtyBit",
	"long",
	"ptrdiff_t",
	"EntityRef",
	"SlowRemoteCommandID",
	NULL
};

enum
{
	STRUCT_MAGICWORD,
	ENUM_MAGICWORD,
	AST_MACRO_MAGICWORD,
	AST_PREFIX_MAGICWORD,
	AST_SUFFIX_MAGICWORD,
	FIXUPFUNC_MAGICWORD,
	AUTO_TP_FUNC_OPT_MAGICWORD,
};

typedef struct SpecialConstKeyword
{
	char *pStartingString;
	char *pNonConstString;
	enumDataType eDataType;
	enumDataStorageType eStorageType;
	enumDataReferenceType eReferenceType;
	bool bNeedToGetStructType;
	char *pTokTypeFlagToAdd;

} SpecialConstKeyword;

void FieldAssert(STRUCT_FIELD_DESC *pField, bool bCondition, char *pErrorMessage)
{
	if (!bCondition)
	{
		printf("%s(%d) : error S0000 : (StructParser) %s\n", pField->fileName, pField->iLineNum, pErrorMessage);
		fflush(stdout);
		BreakIfInDebugger();
		Sleep(100);
		exit(1);
	}
}

void FieldAssertf(STRUCT_FIELD_DESC *pField, bool bCondition, char *pErrorMessage, ...)
{
	char buf[1000] = "";
	va_list ap;

	if (bCondition)
	{
		return;
	}

	va_start(ap, pErrorMessage);
	if (pErrorMessage)
	{
		vsprintf(buf, pErrorMessage, ap);
	}
	va_end(ap);


	printf("%s(%d) : error S0000 : (StructParser) %s\n", pField->fileName, pField->iLineNum, buf);
	fflush(stdout);
	BreakIfInDebugger();
	Sleep(100);
	exit(1);
	
}
SpecialConstKeyword gSpecialConstKeywords[] = 
{
	{
		"CONST_EARRAY_OF",
		"EARRAY_OF",
		DATATYPE_STRUCT,
		STORAGETYPE_EARRAY,
		REFERENCETYPE_POINTER,
		true,
	},
	{
		"CONST_INT_EARRAY",
		"INT_EARRAY",
		DATATYPE_INT,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		false,
	},
	{
		"CONST_CONTAINERID_EARRAY",
		"CONTAINERID_EARRAY",
		DATATYPE_INT,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		false,
	},
	{
		"CONST_FLOAT_EARRAY",
		"FLOAT_EARRAY",
		DATATYPE_FLOAT,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		false,
	},
	{
		"CONST_OPTIONAL_STRUCT",
		"OPTIONAL_STRUCT",
		DATATYPE_STRUCT,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		true,
	},
	{
		"CONST_STRING_MODIFIABLE",
		"STRING_MODIFIABLE",
		DATATYPE_CHAR,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		false,
	},
	{
		"CONST_STRING_POOLED",
		"STRING_POOLED",
		DATATYPE_CHAR,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		false,
		"POOL_STRING",
	},
	{
		"CONST_STRING_EARRAY",
		"STRING_EARRAY",
		DATATYPE_CHAR,
		STORAGETYPE_EARRAY,
		REFERENCETYPE_POINTER,
		false,
	},
	{
		"CONST_UINT_EARRAY",
		"UINT_EARRAY",
		DATATYPE_INT,
		STORAGETYPE_EMBEDDED,
		REFERENCETYPE_POINTER,
		false,
	},
};













StructParser::StructParser()
{
	m_iNumStructs = 0;
	m_iNumEnums = 0;
	m_iNumMacros = 0;
	m_iNumAutoTPFuncOpts = 0;

	m_pPrefix = NULL;
	m_pSuffix = NULL;
}

bool StructParser::DoesFileNeedUpdating(char *pFileName)
{
	HANDLE hFile;

	char templateFileName[MAX_PATH];
	char templateHeaderFileName[MAX_PATH];

	TemplateFileNameFromSourceFileName(templateFileName, templateHeaderFileName, pFileName);


	hFile = CreateFile(templateFileName, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return true;
	}
	else 
	{
		CloseHandle(hFile);
	}

	hFile = CreateFile(templateHeaderFileName, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return true;
	}
	else 
	{
		CloseHandle(hFile);
	}


	return false;
}

void StructParser::DeleteStruct(int iIndex)
{
	int j;

	DestroyIfDefStack(m_pStructs[iIndex]->pIfDefStack);


	for (j=0; j < m_pStructs[iIndex]->iNumFields; j++)
	{
		if (m_pStructs[iIndex]->pStructFields[j]->iNumWikiComments)
		{
			int k;

			for (k=0; k < m_pStructs[iIndex]->pStructFields[j]->iNumWikiComments; k++)
			{
				delete m_pStructs[iIndex]->pStructFields[j]->pWikiComments[k];
			}
		}

		while (m_pStructs[iIndex]->pStructFields[j]->pFirstCommand)
		{
			STRUCT_COMMAND *pCommand = m_pStructs[iIndex]->pStructFields[j]->pFirstCommand;
			m_pStructs[iIndex]->pStructFields[j]->pFirstCommand = pCommand->pNext;

			delete pCommand->pCommandName;
			delete pCommand->pCommandString;
			if (pCommand->pCommandExpression)
			{
				delete pCommand->pCommandExpression;
			}
			delete pCommand;
		}

	
		while (m_pStructs[iIndex]->pStructFields[j]->pFirstBeforeCommand)
		{
			STRUCT_COMMAND *pCommand = m_pStructs[iIndex]->pStructFields[j]->pFirstBeforeCommand;
			m_pStructs[iIndex]->pStructFields[j]->pFirstBeforeCommand = pCommand->pNext;

			delete pCommand->pCommandName;
			delete pCommand->pCommandString;
			if (pCommand->pCommandExpression)
			{
				delete pCommand->pCommandExpression;
			}
			delete pCommand;
		}

		if (m_pStructs[iIndex]->pStructFields[j]->pIndexes)
		{
			delete m_pStructs[iIndex]->pStructFields[j]->pIndexes;
		}

		if (m_pStructs[iIndex]->pStructFields[j]->pFormatString)
		{
			delete m_pStructs[iIndex]->pStructFields[j]->pFormatString;
		}

		delete m_pStructs[iIndex]->pStructFields[j];

	}

	if (m_pStructs[iIndex]->pMainWikiComment)
	{
		delete m_pStructs[iIndex]->pMainWikiComment;
	}

	if (m_pStructs[iIndex]->pNonConstPrefixString)
	{
		delete m_pStructs[iIndex]->pNonConstPrefixString;
	}

	if (m_pStructs[iIndex]->pNonConstSuffixString)
	{
		delete m_pStructs[iIndex]->pNonConstSuffixString;
	}

	if (m_pStructs[iIndex]->pStructLevelFormatString)
	{
		delete m_pStructs[iIndex]->pStructLevelFormatString;
	}

	delete m_pStructs[iIndex];
}

void StructParser::DeleteEnum(int iIndex)
{
	ENUM_DEF *pEnum = m_pEnums[iIndex];
	if (pEnum->pIfDefStack)
	{
		DestroyIfDefStack(pEnum->pIfDefStack);
		pEnum->pIfDefStack = NULL;
	}

	if (pEnum->pMainWikiComment)
	{
		free(pEnum->pIfDefStack);
		pEnum->pIfDefStack = NULL;
	}

	int i;
	for (i=0; i < pEnum->iNumEntries; i++)
	{
		if (pEnum->pEntries[i].pWikiComment)
		{
			free(pEnum->pEntries[i].pWikiComment);
		}
	}

	if (pEnum->pEntries)
	{
		free(pEnum->pEntries);
	}

	free(pEnum);


}


StructParser::~StructParser()
{
	int i;

	for (i=0; i < m_iNumStructs; i++)
	{
		DeleteStruct(i);
		
	}

	for (i=0; i < m_iNumEnums; i++)
	{
		if (m_pEnums[i]->pEntries)
		{
			if (m_pEnums[i]->pEntries->pWikiComment)
			{
				delete m_pEnums[i]->pEntries->pWikiComment;
			}
			delete m_pEnums[i]->pEntries;
		}

		if (m_pEnums[i]->pMainWikiComment)
		{
			delete m_pEnums[i]->pMainWikiComment;
		}

		delete m_pEnums[i];
	}

	ResetMacros();
}

char *StructParser::GetMagicWord(int iWhichMagicWord)
{
	switch (iWhichMagicWord)
	{
	case STRUCT_MAGICWORD: 
		return "AUTO_STRUCT";
	case ENUM_MAGICWORD:
		return "AUTO_ENUM";
	case AST_MACRO_MAGICWORD:
		return AUTOSTRUCT_MACRO;
	case AST_PREFIX_MAGICWORD:
		return AUTOSTRUCT_PREFIX;
	case AST_SUFFIX_MAGICWORD:
		return AUTOSTRUCT_SUFFIX;
	case FIXUPFUNC_MAGICWORD:
		return "AUTO_FIXUPFUNC";
	case AUTO_TP_FUNC_OPT_MAGICWORD:
		return "AUTO_TP_FUNC_OPT";
	default:
		return "x x";
	}
}


void StructParser::FoundMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, int iWhichMagicWord, char *pMagicWordString)
{
	switch (iWhichMagicWord)
	{
	case MAGICWORD_BEGINNING_OF_FILE:
	case MAGICWORD_END_OF_FILE:
		ResetMacros();
		break;
		

	case STRUCT_MAGICWORD:
		FoundStructMagicWord(pSourceFileName, pTokenizer);
		break;

	case ENUM_MAGICWORD:
		FoundEnumMagicWord(pSourceFileName, pTokenizer);
		break;

	case AUTO_TP_FUNC_OPT_MAGICWORD:
		FoundAutoTPFuncOptMagicWord(pSourceFileName, pTokenizer);
		break;

	case AST_MACRO_MAGICWORD:
		FoundMacro(pSourceFileName, pTokenizer);
		break;

	case AST_PREFIX_MAGICWORD:
		FoundPrefix(pSourceFileName, pTokenizer);
		break;

	case AST_SUFFIX_MAGICWORD:
		FoundSuffix(pSourceFileName, pTokenizer);
		break;

	case FIXUPFUNC_MAGICWORD:
		FoundFixupFunc(pSourceFileName, pTokenizer);
		break;

	default:
		ASSERT(pTokenizer,0, "Got bad magic word somehow");
		break;
	}
}

void StructParser::FoundFixupFunc(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;

	char *pFuncName;
	char *pTypeName;

	char funcBody[TOKENIZER_MAX_STRING_LENGTH];
	char defines[TOKENIZER_MAX_STRING_LENGTH];
	char autoRunFuncName[256];

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ; after FIXUPFUNC");
	pTokenizer->AssertGetIdentifier("TextParserResult");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected func name after bool");
	pFuncName = STRDUP(token.sVal);
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after bool funcname");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected type name after funcname(");
	pTypeName = STRDUP(token.sVal);
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "Expected * after bool funcname(type");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected var name after funcname(type*");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after bool funcname(type*");
	pTokenizer->AssertGetIdentifier("enumTextParserFixupType");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected var name after funcname(type*, enumTextParserFixupType");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected ,  after funcname(type*, enumTextParserFixupType x");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_VOID, "Expected void  after funcname(type*, enumTextParserFixupType x,");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "Expected *  after funcname(type*, enumTextParserFixupType x, void");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected var name after funcname(type*, enumTextParserFixupType x, void*");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after bool funcname(type*, enumTextParserFixupType x, void *x");

	sprintf(funcBody, "\tParserSetTableFixupFunc(parse_%s, %s);\n", pTypeName, pFuncName);
	sprintf(defines, "extern ParseTable parse_%s[];\nTextParserResult %s(void *pStruct, enumTextParserFixupType eFixupType, void *pEtraData);\n", pTypeName, pFuncName);

	sprintf(autoRunFuncName, "_AUTORUN_RegisterFixupFunc%s", pTypeName);

	m_pParent->GetAutoRunManager()->AddAutoRunWithBody(autoRunFuncName, pSourceFileName, defines, funcBody, AUTORUN_ORDER_INTERNAL, pTokenizer->GetIfDefStack());

	delete(pFuncName);
	delete(pTypeName);
}



void StructParser::FoundPrefix(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;
	if (m_pPrefix)
	{
		delete m_pPrefix;
	}
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_PREFIX");

	pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

	m_pPrefix = new char[token.iVal + 1];
	strcpy(m_pPrefix, token.sVal);


}

void StructParser::FoundSuffix(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;
	if (m_pSuffix)
	{
		delete m_pSuffix;
	}
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_SUFFIX");

	pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

	m_pSuffix = new char[token.iVal + 1];
	strcpy(m_pSuffix, token.sVal);

}

void StructParser::FoundMacro(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;

	int i;


	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_MACRO");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected identifier after AST_MACRO((");

	for (i=0; i < m_iNumMacros; i++)
	{
		if (strcmp(m_Macros[i].pIn, token.sVal) == 0)
		{
			//this macro already exists. Replace the pOut

			delete m_Macros[i].pOut;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after AST_MACRO((x");

			pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

			m_Macros[i].pOut = new char[token.iVal + 1];
			strcpy(m_Macros[i].pOut, token.sVal);
			m_Macros[i].iOutLength = token.iVal;


			return;



		}
	}




	ASSERT(pTokenizer,m_iNumMacros < MAX_MACROS, "Too many macros");
	AST_MACRO_STRUCT *pCurMacro = &m_Macros[m_iNumMacros++];


	pCurMacro->pIn = new char[token.iVal + 1];
	strcpy(pCurMacro->pIn, token.sVal);
	pCurMacro->iInLength = token.iVal;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after AST_MACRO((x");

	pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

	pCurMacro->pOut = new char[token.iVal + 1];
	strcpy(pCurMacro->pOut, token.sVal);
	pCurMacro->iOutLength = token.iVal;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected )) after AST_MACRO((x,y");

}



//typedef void (*writetext_f)(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

void StructParser::FoundAutoTPFuncOptMagicWord(char *pSourceFileName, Tokenizer *pTokenizer)

{
	Token token;
//	enumTokenType eType;


	STATICASSERT(m_iNumAutoTPFuncOpts < MAX_AUTO_TP_FUNC_OPTS, "Too many AUTO_TP_FUNC_OPTs");

	AUTO_TP_FUNC_OPT_STRUCT *pFunc = &m_AutoTpFuncOpts[m_iNumAutoTPFuncOpts++];
	memset(pFunc, 0, sizeof(AUTO_TP_FUNC_OPT_STRUCT));

	strcpy(pFunc->sourceFileName, pSourceFileName);


	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Expected ; after AUTO_TP_FUNC_OPT");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_TYPEDEF, "Expected typedef after AUTO_TP_FUNC_OPT;");
	pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, TOKEN_RESERVEDWORD, RW_VOID, "Expected identifier after typedef in AUTO_TP_FUNC_OPT");
	strcpy(pFunc->resultType, token.sVal);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after return type in AUTO_TP_FUNC_OPT");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_ASTERISK, "Expected * after ( in AUTO_TP_FUNC_OPT");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected identifier after (*");
	strcpy(pFunc->funcTypeName, token.sVal);

	int iFuncTypeNameLen = (int)strlen(pFunc->funcTypeName);
	if (iFuncTypeNameLen < 3 || pFunc->funcTypeName[iFuncTypeNameLen-1] != 'f' || pFunc->funcTypeName[iFuncTypeNameLen-2] != '_')
	{
		pTokenizer->AssertFailedf("While processing AUTO_TP_FUNC_OPT, found ill-formed name %s. Names must be foo_f", pFunc->funcTypeName);
	}

	pFunc->funcTypeName[iFuncTypeNameLen-2] = 0;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after fnc name in AUTO_TP_FUNC_OPT");
	pTokenizer->AssertGetBracketBalancedBlock(RW_LEFTPARENS, RW_RIGHTPARENS, "Couldn't read arg list for AUTO_TP_FUNC_OPT", pFunc->fullArgString, MAX_TP_FUNC_OPT_ARG_STRING);

	char argListStringCopy[MAX_TP_FUNC_OPT_ARG_STRING];
	char *pArgSubStrings[MAX_TP_FUNC_OPT_ARGS] = {0};
	int iNumArgs;
	char *strTokKludgeMacro[][2] = 
	{
		{ ",", " # " },
		{ NULL, NULL }
	};

	strcpy(argListStringCopy, pFunc->fullArgString + 1);
	argListStringCopy[strlen(argListStringCopy) - 1] = 0;


	ReplaceMacrosInPlace(argListStringCopy, strTokKludgeMacro);

	//this is some kludge stroking here... too lazy to make it better
	iNumArgs = SubDivideStringAndRemoveWhiteSpace(pArgSubStrings, argListStringCopy, '#', MAX_TP_FUNC_OPT_ARGS);

	int i;

	for (i = 0; i < iNumArgs; i++)
	{
		int j = (int)strlen(pArgSubStrings[i]) - 1;
		while (j > 0 && !IsOKForIdent(pArgSubStrings[i][j]))
		{
			pArgSubStrings[i][j] = 0;
			j--;
		}

		while ( j >= 0 && IsOKForIdent(pArgSubStrings[i][j]))
		{
			j--;
		}

		strcat(pFunc->recurseArgString, pArgSubStrings[i] + j + 1);
		if (i < iNumArgs - 1)
		{
			strcat(pFunc->recurseArgString, ", ");
		}
	}
}

void StructParser::FoundEnumMagicWord(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;
	enumTokenType eType;


	STATICASSERT(m_iNumEnums < MAX_ENUMS, "Too many enums");

	ENUM_DEF *pEnum = m_pEnums[m_iNumEnums++] = new ENUM_DEF;
	memset(pEnum, 0, sizeof(ENUM_DEF));

	pEnum->pIfDefStack = CopyIfDefStack(pTokenizer->GetIfDefStack());


	do
	{
		eType = pTokenizer->GetNextToken(&token);
		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON)
		{
			break;
		}


		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AEN_PAD") == 0)
		{
			pEnum->iPadding++;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AEN_NO_PREFIX_STRIPPING") == 0)
		{
			pEnum->bNoPrefixStripping = true;
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AEN_APPEND_TO") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AEN_APPEND_TO");
			ASSERT(pTokenizer,pEnum->enumToAppendTo[0] == 0, "Can't have two AEN_APPEND_TOs in one ENUM");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after AEN_APPEND_TO(");
			strcpy(pEnum->enumToAppendTo, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AEN_APPEND_TO(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AEN_APPEND_OTHER_TO_ME") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AEN_APPEND_OTHER_TO_ME");
			ASSERT(pTokenizer,pEnum->enumAppendOtherToMe[0] == 0, "Can't have two AEN_APPEND_OTHER_TO_ME in one ENUM");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after AEN_APPEND_OTHER_TO_ME(");
			strcpy(pEnum->enumAppendOtherToMe, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after enumAppendOtherToMe(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AEN_EXTEND_WITH_DYNLIST") == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AEN_EXTEND_WITH_DYNLIST");
			ASSERT(pTokenizer,pEnum->embeddedDynamicName[0] == 0, "Can't have two AEN_EXTEND_WITH_DYNLIST in one ENUM");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after AEN_EXTEND_WITH_DYNLIST(");
			strcpy(pEnum->embeddedDynamicName, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AEN_EXTEND_WITH_DYNLIST(x");
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AEN_WIKI") == 0)
		{
			ASSERT(pTokenizer,pEnum->pMainWikiComment == NULL, "Can't have two AEN_WIKIs for one AUTO_ENUM");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AEN_WIKI");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string after AEN_WIKI(");

			if (token.sVal[0])
			{
				pEnum->pMainWikiComment = new char[token.iVal + 1];
				strcpy(pEnum->pMainWikiComment, token.sVal);
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AEN_WIKI(\"x\"");

		}
		else
		{
			ASSERT(pTokenizer,0, "Unrecognized command after AUTO_ENUM... missing semicolon?");
		}
	} while (1);


	
	strcpy(pEnum->sourceFileName, pSourceFileName);

	eType = pTokenizer->GetNextToken(&token);

	if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_TYPEDEF)
	{
		eType = pTokenizer->GetNextToken(&token);
	}

	ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_ENUM, "Expected [typedef] enum after AUTO_ENUM");

	eType = pTokenizer->GetNextToken(&token);

	if (eType == TOKEN_IDENTIFIER)
	{
		ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "enum name too long");

		ASSERTF(pTokenizer,FindEnumByName(token.sVal) == NULL, "Duplicate enum name %s", token.sVal);

		strcpy(pEnum->enumName, token.sVal);

		eType = pTokenizer->GetNextToken(&token);
	}

	ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE, "Expected { after enum");

	do
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
		{
			break;
		}


		ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER, "Expected identifier as enum entry name");
		ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "identifier entry name too long");

		if (strcmp(token.sVal, "EIGNORE") == 0 || strcmp(token.sVal, "ENAMES") == 0)
		{
			ASSERT(pTokenizer, 0, "Found EIGNORE/ENAMES not following a comma after an enum field");
		}



		ENUM_ENTRY *pEntry = GetNewEntry(pEnum);

		strcpy(pEntry->inCodeName, token.sVal);

		if (pEnum->pMainWikiComment)
		{
			pTokenizer->GetSurroundingSlashedCommentBlock(&token, false);

			if (token.sVal[0])
			{
				pEntry->pWikiComment = STRDUP(token.sVal);
			}
		}

		do
		{
			eType = pTokenizer->GetNextToken(&token);

			if (eType == TOKEN_IDENTIFIER)
			{
				if (strcmp(token.sVal, "EIGNORE") == 0 || strcmp(token.sVal, "ENAMES") == 0)
				{
					ASSERT(pTokenizer, 0, "Found EIGNORE/ENAMES not following a comma after an enum field");
				}
			}

		}
		while (!(eType == TOKEN_RESERVEDWORD && (token.iVal == RW_RIGHTBRACE || token.iVal == RW_COMMA)));

		if (token.iVal == RW_RIGHTBRACE)
		{
			break;
		}

		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "EIGNORE") == 0)
		{
			pEnum->iNumEntries--;
			pTokenizer->GetNextToken(&token);
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ENAMES") == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after ENAMES");

			//allow colons and periods in ENAMES
			pTokenizer->SetExtraCharsAllowedInIdentifiers(":.");

			do
			{
				eType = pTokenizer->GetNextToken(&token);

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
				{
					break;
				}

				ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected identifier inside ENAMES");

				ASSERT(pTokenizer,pEntry->iNumExtraNames < MAX_ENUM_EXTRA_NAMES, "Too many extra names");

				ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "extra name too long");

				strcpy(pEntry->extraNames[pEntry->iNumExtraNames++], token.sVal);

				eType = pTokenizer->CheckNextToken(&token);
				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_OR)
				{
					pTokenizer->GetNextToken(&token);
				}
			} while (1);

			pTokenizer->SetExtraCharsAllowedInIdentifiers(NULL);


		}
	}
	while (1);

	if (pEnum->enumName[0] == 0)

	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected identifier after enum { ... }");
		strcpy(pEnum->enumName, token.sVal);
	}
	else
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_IDENTIFIER)
		{
			ASSERT(pTokenizer,strcmp(token.sVal, pEnum->enumName) == 0, "enum names at beginning and end of enum must match precisely");
		}
	}

	m_pParent->GetDictionary()->AddIdentifier(pEnum->enumName, pSourceFileName, IDENTIFIER_ENUM);
}
	
#define NUM_ENTRIES_TO_ALLOCATE_AT_ONCE 8

StructParser::ENUM_ENTRY *StructParser::GetNewEntry(ENUM_DEF *pEnum)
{
	if (pEnum->iNumEntries == pEnum->iNumAllocatedEntries)
	{
		ENUM_ENTRY *pNewEntries = new ENUM_ENTRY[pEnum->iNumAllocatedEntries + NUM_ENTRIES_TO_ALLOCATE_AT_ONCE];
		STATICASSERT(pNewEntries != NULL, "new failed");

		memset(pNewEntries, 0, sizeof(ENUM_ENTRY) * (pEnum->iNumAllocatedEntries + NUM_ENTRIES_TO_ALLOCATE_AT_ONCE));

		if (pEnum->iNumAllocatedEntries)
		{
			memcpy(pNewEntries, pEnum->pEntries, sizeof(ENUM_ENTRY) * pEnum->iNumAllocatedEntries);
			delete(pEnum->pEntries);
		}

		pEnum->iNumAllocatedEntries += NUM_ENTRIES_TO_ALLOCATE_AT_ONCE;
	
		pEnum->pEntries = pNewEntries;
	}

	return pEnum->pEntries + (pEnum->iNumEntries++);
}



void StructParser::FoundStructMagicWord(char *pSourceFileName, Tokenizer *pTokenizer)
{
	Token token;
	enumTokenType eType;

	STRUCT_COMMAND *pCurBeforeCommands = NULL;

	STATICASSERTF(m_iNumStructs < MAX_STRUCTS, "Too many structs in %s",pSourceFileName);

	STRUCT_DEF *pStruct = m_pStructs[m_iNumStructs++] = new STRUCT_DEF;

	memset(pStruct, 0, sizeof(STRUCT_DEF));
	
	pStruct->pIfDefStack = CopyIfDefStack(pTokenizer->GetIfDefStack());

	eType = pTokenizer->GetNextToken(&token);

	while (eType == TOKEN_IDENTIFIER)
	{
		if (strcmp(token.sVal, AUTOSTRUCT_IGNORE) == 0 || strcmp(token.sVal, AUTOSTRUCT_IGNORE_STRUCTPARAM) == 0)
		{
			bool bStructParam = (strcmp(token.sVal, AUTOSTRUCT_IGNORE_STRUCTPARAM) == 0);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_IGNORE");
			eType = pTokenizer->GetNextToken(&token);
			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected string or identifier after AST_IGNORE(");
			ASSERT(pTokenizer,pStruct->iNumIgnores < MAX_IGNORES, "Too many ignores");
			ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "Ignore string too long");
			
			pStruct->bIgnoresAreStructParam[pStruct->iNumIgnores] = bStructParam;
			strcpy(pStruct->ignores[pStruct->iNumIgnores++], token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AST_IGNORE(x");
		}		
		else if (strcmp(token.sVal, AUTOSTRUCT_IGNORE_STRUCT) == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_IGNORE_STRUCT");
			eType = pTokenizer->GetNextToken(&token);
			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected string or identifier after AST_IGNORE_STRUCT(");
			ASSERT(pTokenizer,pStruct->iNumIgnoreStructs < MAX_IGNORES, "Too many ignores");
			ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "Ignore string too long");
			
			strcpy(pStruct->ignoreStructs[pStruct->iNumIgnoreStructs++], token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AST_IGNORE_STRUCT(x");
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_STARTTOK) == 0)
		{
			pStruct->bHasStartString = true;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_STARTTOK ");
			eType = pTokenizer->GetNextToken(&token);
			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected identifier or string after AST_STARTTOK(");
			ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH - 1, "AST_STARTTOK string too long");
			strcpy(pStruct->startString, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AST_STARTTOK(x ");

		}
		else if (strcmp(token.sVal, AUTOSTRUCT_ENDTOK) == 0)
		{
			ASSERT(pTokenizer,pStruct->iNumEndStrings < MAX_END_STRINGS-1, "Too many end strings");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_ENDTOK ");
			eType = pTokenizer->GetNextToken(&token);
			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected identifier or string after AST_ENDTOK(");
			ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH - 1, "AST_ENDTOK string too long");
			strcpy(pStruct->endStrings[pStruct->iNumEndStrings], token.sVal);
			pStruct->iNumEndStrings++;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AST_ENDTOK(x ");

		}
		else if (strcmp(token.sVal, AUTOSTRUCT_NO_UNRECOGNIZED) == 0)
		{
			pStruct->bNoUnrecognized = true;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_STRIP_UNDERSCORES) == 0)
		{
			pStruct->bStripUnderscores = true;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_NO_PREFIX_STRIPPING) == 0)
		{
			pStruct->bNoPrefixStripping = true;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_FORCE_USE_ACTUAL_FIELD_NAME) == 0)
		{
			pStruct->bForceUseActualFieldName = true;
			pStruct->bNoPrefixStripping = false;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT) == 0)
		{
			pStruct->bAlwaysIncludeActualFieldNameAsRedundant = false;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_SINGLETHREADED_MEMPOOL) == 0)
		{
			pStruct->bSingleThreadedMemPool = true;
		}		
		else if (strcmp(token.sVal, AUTOSTRUCT_THREADSAFE_MEMPOOL) == 0)
		{
			pStruct->bThreadSafeMemPool = true;
		}		
		else if (strcmp(token.sVal, AUTOSTRUCT_NOMEMTRACKING) == 0)
		{
			pStruct->bNoMemTracking= true;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_CONTAINER) == 0)
		{
			pStruct->bIsContainer = true;

			if (!pStruct->bForceUseActualFieldName)
			{
				pStruct->bNoPrefixStripping = true;
				pStruct->bAlwaysIncludeActualFieldNameAsRedundant = true;
			}
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_FORCE_CONST) == 0)
		{
			pStruct->bIsForceConst = true;
		}
		else if (strcmp(token.sVal, AUTO_STRUCT_FIXUP_FUNC) == 0)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_FIXUPFUNC");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH-1, "Expected string after AST_FIXUPFUNC");

			strcpy(pStruct->fixupFuncName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AST_FIXUPFUNC(x");
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_WIKI_COMMENT) == 0)
		{
			ASSERT(pTokenizer,pStruct->pMainWikiComment == NULL, "Can't have two WIKI comments for the same struct");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after WIKI");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string after WIKI(");

			pStruct->pMainWikiComment = new char[token.iVal + 1];
			strcpy(pStruct->pMainWikiComment, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after WIKI");


		}
		else if (strcmp(token.sVal, AUTOSTRUCT_FOR_ALL) == 0)
		{
			ASSERT(pTokenizer,pStruct->forAllString[0] == 0, "Can't have two AST_FOR_ALLs");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_FOR_ALL");
			pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);
			strcpy(pStruct->forAllString, token.sVal);
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_NONCONST_PREFIXSUFFIX) == 0)
		{
			ASSERT(pTokenizer,pStruct->pNonConstPrefixString == NULL, "can't have two AST_NONCONST_PREFIXSUFFIX");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_NONCONST_PREFIXSUFFIX");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string after AST_NONCONST_PREFIXSUFFIX(");
			pStruct->pNonConstPrefixString = new char[token.iVal + 1];
			RemoveCStyleEscaping(pStruct->pNonConstPrefixString, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after AST_NONCONST_PREFIXSUFFIX(x");
		
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string after AST_NONCONST_PREFIXSUFFIX(x,");
			pStruct->pNonConstSuffixString = new char[token.iVal + 1];
			RemoveCStyleEscaping(pStruct->pNonConstSuffixString, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AST_NONCONST_PREFIXSUFFIX(x,y");
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_FORMATSTRING) == 0)
		{
			ReadTokensIntoFormatString(pTokenizer, &pStruct->pStructLevelFormatString);
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_CREATION_COMMENT_FIELD) == 0)
		{
			ASSERT(pTokenizer, pStruct->creationCommentFieldName[0] == 0, "can't have two AST_CREATION_COMMENT_FIELD");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_CREATION_COMMENT_FIELD");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected identifier after AST_CREATION_COMMENT_FIELD(");
			strcpy(pStruct->creationCommentFieldName, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ( after AST_CREATION_COMMENT_FIELD");
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_RUNTIME_MODIFIED) == 0)
		{
			pStruct->bRuntimeModified = true;
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_SAVE_ORIGINAL_CASE) == 0)
		{
			pStruct->bSaveOriginalCaseFieldNames = true;
		}
		else
		{
			ASSERT(pTokenizer,0, "Expected AUTO_STRUCT [AST_IGNORE ...] [AST_IGNORE_STRUCT ...] [AST_START ...] [AST_END ...] [AST_STRIP_UNDERSCORES] [WIKI(\"...\")]; [typedef] struct");
		}


		eType = pTokenizer->GetNextToken(&token);

	}
	
	ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON, "Expected semicolon after AUTO_STRUCT and its commands");

	pStruct->iPreciseStartingOffsetInFile = pTokenizer->GetOffset(&pStruct->iPreciseStartingLineNumInFile);

	eType = pTokenizer->GetNextToken(&token);


	if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_TYPEDEF)
	{
		eType = pTokenizer->GetNextToken(&token);
	}

	ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_STRUCT, "Expected AUTO_STRUCT [typedef] struct");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected struct name after 'struct'");

	if (stricmp(token.sVal, "__ALIGN") == 0)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after __ALIGN");
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after __ALIGN(x");
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Expected struct name after 'struct'");
	}


	ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "struct name too long");

	strcpy(pStruct->structName, token.sVal);

	strcpy(pStruct->sourceFileName, pSourceFileName);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTBRACE, "Expected { after struct name");


	do
	{
		eType = pTokenizer->CheckNextToken(&token);

		if (eType == TOKEN_NONE)
		{
			ASSERT(pTokenizer, 0, "Unexpected end of file");
		}

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
		{
			if (pStruct->bCurrentlyInsideUnion)
			{
				UnionStruct *pCurUnion = &pStruct->unions[pStruct->iNumUnions - 1];

				pTokenizer->GetNextToken(&token);
				eType = pTokenizer->GetNextToken(&token);
				if (eType == TOKEN_IDENTIFIER)
				{
					ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "Union name too long");
					strcpy(pCurUnion->name, token.sVal);
					eType = pTokenizer->GetNextToken(&token);
				}

				ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON, "Expected ; after union");
				pStruct->bCurrentlyInsideUnion = false;
				pCurUnion->iLastFieldNum = pStruct->iNumFields - 1;

				//assert that this union has at least all-but-one of its fields redundant
				int iNumRedundantFields = 0;

				int i;

				for (i = pCurUnion->iFirstFieldNum; i <= pCurUnion->iLastFieldNum; i++)
				{
					if (pStruct->pStructFields[i]->bFoundRedundantToken)
					{
						iNumRedundantFields++;
					}
				}

				ASSERT(pTokenizer,pCurUnion->iLastFieldNum - pCurUnion->iFirstFieldNum + 1 - iNumRedundantFields <= 1, "Only one non-redundant field allowed in a union");
				continue;
			}

			break;
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "union") == 0)
		{
			//first, check if this union is NO_AST, which means looking past nested braces to the next ;, then seeing
			//if the very next token is NO_AST
			int iBraceDepth = 0;

			pTokenizer->SaveLocation();

			do
			{
				eType = pTokenizer->GetNextToken(&token);
			
				ASSERT(pTokenizer,eType != TOKEN_NONE, "EOF found inside union");

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE)
				{
					iBraceDepth++;
				}
				else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
				{
					iBraceDepth--;
					ASSERT(pTokenizer,iBraceDepth >= 0, "Mismatched braces found inside union");
				}
			}
			while (!( iBraceDepth == 0 && eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON));

			eType = pTokenizer->GetNextToken(&token);

			if (!(eType == TOKEN_IDENTIFIER && strcmp(token.sVal, AUTOSTRUCT_EXCLUDE) == 0))
			{
				pTokenizer->RestoreLocation();

				pTokenizer->GetNextToken(&token);

				ASSERT(pTokenizer,!pStruct->bCurrentlyInsideUnion, "StructParser doesn't support nested unions");
				ASSERT(pTokenizer,pStruct->iNumUnions < MAX_UNIONS, "Too many unions in one struct");

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTBRACE, "Expected { after union");

				pStruct->bCurrentlyInsideUnion = true;
				pStruct->unions[pStruct->iNumUnions].iFirstFieldNum = pStruct->iNumFields;
				pStruct->iNumUnions++;
			}
		}
		else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, AUTOSTRUCT_STOP) == 0)
		{
			int iBraceDepth = 0;
			do
			{
				eType = pTokenizer->CheckNextToken(&token);

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTBRACE)
				{
					iBraceDepth++;
				}
				else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACE)
				{
					if (iBraceDepth == 0)
					{
						break;
					}
					else
					{
						iBraceDepth--;
					}
				}

				pTokenizer->GetNextToken(&token);



			}
			while (!(eType == TOKEN_IDENTIFIER && strcmp(token.sVal, AUTOSTRUCT_START) == 0));
		}
		else if (eType == TOKEN_RESERVEDWORD && strcmp(token.sVal, AUTOSTRUCT_MACRO) == 0)
		{
			pTokenizer->GetNextToken(&token);
			FoundMacro(pSourceFileName, pTokenizer);
		}
		else if (eType == TOKEN_RESERVEDWORD && strcmp(token.sVal, AUTOSTRUCT_PREFIX) == 0)
		{
			pTokenizer->GetNextToken(&token);
			FoundPrefix(pSourceFileName, pTokenizer);
		}
		else if (eType == TOKEN_RESERVEDWORD && strcmp(token.sVal, AUTOSTRUCT_SUFFIX) == 0)
		{
			pTokenizer->GetNextToken(&token);
			FoundSuffix(pSourceFileName, pTokenizer);
		}
		else if (strcmp(token.sVal, AUTOSTRUCT_COMMAND_BETWEEN_FIELDS) == 0)
		{
			pTokenizer->GetNextToken(&token);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_COMMAND");

			STRUCT_COMMAND *pCommand = new STRUCT_COMMAND;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected command name after AST_COMMAND(");

			pCommand->pCommandName = new char[token.iVal + 1];
			strcpy(pCommand->pCommandName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after AST_COMMAND(name");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected command string after AST_COMMAND(name,");

			pCommand->pCommandString = new char[token.iVal + 1];
			strcpy(pCommand->pCommandString, token.sVal);

			pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, 
				TOKEN_RESERVEDWORD, RW_COMMA, "Expected ) or , after AST_COMMAND(name, command");

			if (token.iVal == RW_COMMA)
			{
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected command expression string after AST_COMMAND(name, command,");
				pCommand->pCommandExpression = STRDUP(token.sVal);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, 
					 "Expected ) after AST_COMMAND(name, command, expression");
			}
			else
			{
				pCommand->pCommandExpression = NULL;
			}


			if (pStruct->iNumFields)
			{
				STRUCT_FIELD_DESC *pLastField = pStruct->pStructFields[pStruct->iNumFields-1];

				pCommand->pNext = pLastField->pFirstCommand;
				pLastField->pFirstCommand = pCommand;
			}
			else
			{
				pCommand->pNext = pCurBeforeCommands;
				pCurBeforeCommands = pCommand;
			}
		}
		else
		{
			ASSERT(pTokenizer,pStruct->iNumFields < MAX_FIELDS, "too many fields in one struct");
			
			pStruct->pStructFields[pStruct->iNumFields] = new STRUCT_FIELD_DESC;
			memset(pStruct->pStructFields[pStruct->iNumFields], 0, sizeof(STRUCT_FIELD_DESC));

			if (ReadSingleField(pTokenizer, pStruct->pStructFields[pStruct->iNumFields], pStruct))
			{
				pStruct->iNumFields++;
			}
			else
			{
				delete(pStruct->pStructFields[pStruct->iNumFields]);
			}

			if (pStruct->iNumFields == 1 && pCurBeforeCommands)
			{
				pStruct->pStructFields[0]->pFirstBeforeCommand = pCurBeforeCommands;
				pCurBeforeCommands = NULL;
			}
		}

	} while (1);

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTBRACE, "Expected } after struct");
	eType = pTokenizer->CheckNextToken(&token);

	if (eType == TOKEN_IDENTIFIER)
	{
		ASSERT(pTokenizer,strcmp(token.sVal, pStruct->structName) == 0, "AUTO_STRUCT requires the same struct name after typedef struct and after the entire struct");
	}

	CheckOverallStructValidity(pTokenizer, pStruct);

	m_pParent->GetDictionary()->AddIdentifier(pStruct->structName, pSourceFileName, 
		pStruct->bIsContainer ? IDENTIFIER_STRUCT_CONTAINER : IDENTIFIER_STRUCT);
}

void StructParser::CheckOverallStructValidity(Tokenizer *pTokenizer, STRUCT_DEF *pStruct)
{
	int i;
	bool bFoundPersistedField = false;

	ASSERT(pTokenizer,pStruct->iNumFields > 0, "Structs must have at least one field");

	for (i=0; i < pStruct->iNumFields; i++)
	{
		STRUCT_FIELD_DESC *pField = pStruct->pStructFields[i];

		if (pField->bFoundPersist)
		{
			bFoundPersistedField = true;
		}
	}

	if (bFoundPersistedField)
	{
		ASSERT(pTokenizer,pStruct->bIsContainer, "Found a persisted field in a non-container struct");
	}
	else
	{
		ASSERT(pTokenizer,!pStruct->bIsContainer, "A container struct must have at least one persisted field");
	}
}

void StructParser::CheckOverallStructValidity_PostFixup(STRUCT_DEF *pStruct)
{
	int i;

	if (pStruct->structNameIInheritFrom[0])
	{
		FieldAssert(pStruct->pStructFields[0], pStruct->pStructFields[0]->eDataType == DATATYPE_STRUCT
			&& pStruct->pStructFields[0]->eStorageType == STORAGETYPE_EMBEDDED
			&& pStruct->pStructFields[0]->eReferenceType == REFERENCETYPE_DIRECT,
			"POLYCHILDTYPE only legal on an embedded struct");
	}

	for (i=0; i < pStruct->iNumFields; i++)
	{
		STRUCT_FIELD_DESC *pField = pStruct->pStructFields[i];

		if (pField->refDictionaryName[0])
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_CHAR || pField->eDataType == DATATYPE_REFERENCE, "Can't have RESOURCEDICT/REFDICT on that type");
		}

		if (pStruct->bIsForceConst)
		{
			if (!(pField->bFoundSpecialConstKeyword
					|| pField->bFoundConst 
						&& (pField->eDataType == DATATYPE_INT || pField->eDataType == DATATYPE_FLOAT
							|| pField->eDataType == DATATYPE_CHAR || pField->eDataType == DATATYPE_ENUM
					|| pField->eDataType == DATATYPE_BIT || pField->eDataType == DATATYPE_NONE
					|| pField->eDataType == DATATYPE_BOOLFLAG
					|| pField->eDataType == DATATYPE_MAT3 || pField->eDataType == DATATYPE_MAT4
					|| pField->eDataType == DATATYPE_VEC3 || pField->eDataType == DATATYPE_VEC4 
					|| pField->eDataType == DATATYPE_MAT44 
					|| pField->eDataType == DATATYPE_MAT4_ASMATRIX || pField->eDataType == DATATYPE_MAT3_ASMATRIX
					|| pField->eDataType == DATATYPE_QUAT)
						&& (pField->eStorageType == STORAGETYPE_EMBEDDED || pField->eStorageType == STORAGETYPE_ARRAY)
						&& pField->eReferenceType == REFERENCETYPE_DIRECT))
			{
				char errorString[1024];
				sprintf(errorString, "A field in a FORCE_CONST struct must be a simple const type, or one of the special CONST_ macros. Found a bad one of type %d, storage type %d ref type %d\n", pField->eDataType, pField->eStorageType, pField->eReferenceType);
				FieldAssert(pField, 0, errorString);	
			}
		}


		if (pField->bFoundPersist)
		{
			if (!pField->bFoundNoTransact && !pField->bFoundSometimesTransact)
			{			
				//a persisted field must have a const keyword, or be am embedded float, int, bool or enum
				if (!(pField->bFoundSpecialConstKeyword
					|| pField->bFoundConst 
						&& (pField->eDataType == DATATYPE_INT || pField->eDataType == DATATYPE_FLOAT
							|| pField->eDataType == DATATYPE_CHAR || pField->eDataType == DATATYPE_ENUM
					|| pField->eDataType == DATATYPE_BIT || pField->eDataType == DATATYPE_NONE
					|| pField->eDataType == DATATYPE_BOOLFLAG
					|| pField->eDataType == DATATYPE_MAT3 || pField->eDataType == DATATYPE_MAT4
					|| pField->eDataType == DATATYPE_VEC3 || pField->eDataType == DATATYPE_VEC4
					|| pField->eDataType == DATATYPE_MAT44
					|| pField->eDataType == DATATYPE_MAT4_ASMATRIX || pField->eDataType == DATATYPE_MAT3_ASMATRIX
					|| pField->eDataType == DATATYPE_QUAT)
						&& (pField->eStorageType == STORAGETYPE_EMBEDDED || pField->eStorageType == STORAGETYPE_ARRAY)
						&& pField->eReferenceType == REFERENCETYPE_DIRECT
					|| pField->eDataType == DATATYPE_STRUCT 
						&& pField->eStorageType == STORAGETYPE_EMBEDDED
						&& pField->eReferenceType == REFERENCETYPE_DIRECT
						&& m_pParent->GetDictionary()->FindIdentifier(pField->typeName) == IDENTIFIER_STRUCT_CONTAINER))
				{
					char errorString[1024];
					sprintf(errorString, "A persisted field must be a simple const type, or one of the special CONST_ macros, or an embedded container. Found a bad one of type %d, storage type %d ref type %d\n", pField->eDataType, pField->eStorageType, pField->eReferenceType);
					FieldAssert(pField, 0, errorString);
				}
			}

			if (pField->eDataType == DATATYPE_STRUCT)
			{
				FieldAssert(pField, m_pParent->GetDictionary()->FindIdentifier(pField->typeName) == IDENTIFIER_STRUCT_CONTAINER || m_pParent->GetDictionary()->FindIdentifier(pField->typeName) == IDENTIFIER_NONE && pField->bFoundForceContainer,
					"A persisted struct inside a container must be a container... use AST_FORCE_CONTAINER if it is from a different project");
			}

			
			FieldAssert(pField, !FieldHasTypeFlag(pField, "CLIENT_ONLY"), "A field can't have CLIENT_ONLY and PERSIST");

		}
		else
		{
			FieldAssert(pField, !(pField->bFoundNoTransact || pField->bFoundSometimesTransact), "A field can't be NO_TRANSACT or SOMETIMES_TRANSACT without being PERSIST");
			FieldAssert(pField, !pField->bFoundSubscribe, "A field can't be SUBSCRIBE without being PERSIST");
		}

		if (pField->bFoundSimpleInheritance)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_CHAR && pField->eStorageType == STORAGETYPE_EARRAY
				&& pField->eReferenceType == REFERENCETYPE_POINTER, "A SIMPLE_INHERITANCE field can only be an earray of strings");

		}

	}
}



bool StructParser::ReadSingleField(Tokenizer *pTokenizer, STRUCT_FIELD_DESC *pField, STRUCT_DEF *pStruct)
{
	Token token;
	enumTokenType eType;

	bool bExclude = false;

	//first, check for whether there is a NO_AST immediately after the next semicolon. If so, ignore this whole thing so we don't
	//have parse errors when things we don't understand are NO_ASTed.
	pTokenizer->SaveLocation();
	do
	{
		eType = pTokenizer->GetNextToken(&token);

	} while (eType != TOKEN_NONE && !(eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON));

	if (eType == TOKEN_NONE)
	{
		return false;
	}
	
	eType = pTokenizer->GetNextToken(&token);

	if (eType == TOKEN_NONE || eType == TOKEN_IDENTIFIER && strcmp(token.sVal, AUTOSTRUCT_EXCLUDE) == 0)
	{
		return false;
	}

	pTokenizer->RestoreLocation();

	eType = pTokenizer->CheckNextToken(&token);
	
	//skip "volatile" if present
	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "volatile") == 0)
	{
		eType = pTokenizer->GetNextToken(&token);
	}

	//check for initial "const"
	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
	{
		pField->bFoundConst = true;
		eType = pTokenizer->GetNextToken(&token);
	}


	//now check for one of the "special const" tokens
	if (eType == TOKEN_IDENTIFIER)
	{
		int i;

		for (i=0; i < sizeof(gSpecialConstKeywords) / sizeof(gSpecialConstKeywords[0]); i++)
		{
			if (strcmp(token.sVal, gSpecialConstKeywords[i].pStartingString) == 0 ||
				strcmp(token.sVal, gSpecialConstKeywords[i].pNonConstString) == 0)
			{
				pTokenizer->GetNextToken(&token);
				pField->bFoundSpecialConstKeyword = true;				
				pField->eDataType = gSpecialConstKeywords[i].eDataType;
				pField->eStorageType = gSpecialConstKeywords[i].eStorageType;
				pField->eReferenceType = gSpecialConstKeywords[i].eReferenceType;

				if (gSpecialConstKeywords[i].pTokTypeFlagToAdd)
				{
					AddTokTypeFlagByString(pTokenizer, pField, gSpecialConstKeywords[i].pTokTypeFlagToAdd);
				}

				if (gSpecialConstKeywords[i].bNeedToGetStructType)
				{
					pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after special const keyword");
					pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected const or identifier");
					if (strcmp(token.sVal, "const") == 0)
					{
						pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Expected identifier");
					}
					strcpy(pField->typeName, token.sVal);
					pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected )");
				}

				break;
			}
		}
	}


	if (!pField->bFoundSpecialConstKeyword)
	{

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_VOID)
		{
			pTokenizer->GetNextToken(&token);
			pField->eDataType = DATATYPE_VOID;
			sprintf(pField->typeName, "void");
		}
		else if (eType == TOKEN_IDENTIFIER && (strcmp(token.sVal, "REF_TO") == 0 || strcmp(token.sVal, "CONST_REF_TO") == 0))
		{
			pTokenizer->GetNextToken(&token);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after REF_TO");

			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
			{
				pTokenizer->GetNextToken(&token);
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "expected identifier after REF_TO(");
			
			AttemptToDeduceReferenceDictionaryName(pField, token.sVal);

			strcpy(pField->typeName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after REF_TO");

			pField->eDataType = DATATYPE_REFERENCE;

			if (strcmp(token.sVal, "CONST_REF_TO") )
			{
				pField->bFoundSpecialConstKeyword = true;
			}
		}
		else
		{
				
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH, "Couldn't find field name");

			strcpy(pField->typeName, token.sVal);

			if (strcmp(pField->typeName, "DirtyBit") == 0)
			{
				pField->bIsDirtyBit = true;
			}
		}
	}




	pField->iLineNum = pTokenizer->GetCurLineNum();
	strcpy(pField->fileName, pTokenizer->GetCurFileName());

	eType = pTokenizer->CheckNextToken(&token);

	

	while ((eType == TOKEN_RESERVEDWORD && token.iVal == RW_ASTERISK) ||
		eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "const") == 0)
	{
		if (eType == TOKEN_RESERVEDWORD)
		{
			pField->iNumAsterisks++;
			pTokenizer->GetNextToken(&token);
			eType = pTokenizer->CheckNextToken(&token);
		}
		else
		{
			pField->bFoundConst = true;
			pTokenizer->GetNextToken(&token);
			eType = pTokenizer->CheckNextToken(&token);
		}
	}

	ASSERT(pTokenizer,pField->iNumAsterisks < 3, "Don't know how to deal with more than two asterisks");

	if (pField->bFoundSpecialConstKeyword)
	{
		ASSERT(pTokenizer,pField->iNumAsterisks == 0 && !pField->bFoundConst, "After a special const token, * and const are illegal");
	}

	ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER, "Expected identifier for field name after field type");

	ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH, "field name too long");

	if (pStruct->creationCommentFieldName[0])
	{
		ASSERT(pTokenizer, stricmp(token.sVal, pStruct->creationCommentFieldName) != 0, "creationCommentField must be NO_AST");
	}

	strcpy(pField->baseStructFieldName, token.sVal);
	strcpy(pField->curStructFieldName, token.sVal);
	strcpy(pField->userFieldName, token.sVal);

	pTokenizer->GetNextToken(&token);

	do
	{
		eType = pTokenizer->GetNextToken(&token);

		ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && (token.iVal == RW_LEFTBRACKET || token.iVal == RW_SEMICOLON 
			|| token.iVal == RW_COLON), "Expected [, : or ; after field name");

		if (token.iVal == RW_COLON)
		{
			// Get the next token and ignore
			pTokenizer->GetNextToken(&token);
			pField->bBitField = true;
		} 
		else if (token.iVal == RW_LEFTBRACKET)
		{
			ASSERT(pTokenizer,!pField->bFoundSpecialConstKeyword, "Can't have [] after special const keyword");

			pField->iArrayDepth++;

			pTokenizer->SaveLocation();

			do
			{
				eType = pTokenizer->GetNextToken(&token);

				if (eType == TOKEN_NONE)
				{
					pTokenizer->RestoreLocation();
					ASSERT(pTokenizer,0, "Never found ]");
				}

				if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACKET)
				{
					break;
				}

				pTokenizer->StringifyToken(&token);

				char tempString[TOKENIZER_MAX_STRING_LENGTH + MAX_NAME_LENGTH + 1];

				sprintf(tempString, "%s %s", pField->arraySizeString, token.sVal);

				ASSERT(pTokenizer,strlen(tempString) < MAX_NAME_LENGTH, "tokens inside [] are too long");

				strcpy(pField->arraySizeString, tempString);
			} 
			while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTBRACKET));
		}
		else
		{
			break;
		}
	}
	while (1);


	int iNumNotStrings = 0;
	char *pNotStrings[MAX_NOT_STRINGS];
	int i;

	eType = pTokenizer->CheckNextToken(&token);

	while (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, AUTOSTRUCT_NOT) == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST_NOT");

		pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

		ASSERT(pTokenizer,iNumNotStrings < MAX_NOT_STRINGS, "Too many AST_NOTs");

		pNotStrings[iNumNotStrings] = new char[token.iVal + 1];
		strcpy(pNotStrings[iNumNotStrings++], token.sVal);

		eType = pTokenizer->CheckNextToken(&token);
	}

	if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, AUTOSTRUCT_EXTRA_DATA) == 0)
	{
		pTokenizer->GetNextToken(&token);
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AST");
		
		pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

		ProcessStructFieldCommandString(pStruct, pField, token.sVal, iNumNotStrings, pNotStrings, pTokenizer->GetCurFileName(), pTokenizer->GetCurLineNum(), pTokenizer);
	}
	else
	{
		ProcessStructFieldCommandString(pStruct, pField, " ", iNumNotStrings, pNotStrings, pTokenizer->GetCurFileName(), pTokenizer->GetCurLineNum(), pTokenizer);
	}

	for (i=0; i < iNumNotStrings; i++)
	{
		free(pNotStrings[i]);
	}

	FixupFieldName(pField, pStruct->bStripUnderscores, pStruct->bNoPrefixStripping, 
		pStruct->bForceUseActualFieldName, pStruct->bAlwaysIncludeActualFieldNameAsRedundant);

	return true;
}
	
enum
{
	RW_NAME = RW_COUNT,
	RW_ADDNAMES,

	//reserved words for formatting must all be sequential and in order

	//NOTE NOTE NOTE must be kept in sync with enumFormatType
	RW_FORMAT_IP,
	RW_FORMAT_KBYTES,
	RW_FORMAT_FRIENDLYDATE,
	RW_FORMAT_FRIENDLYSS2000,
	RW_FORMAT_DATESS2000,
	RW_FORMAT_FRIENDLYCPU,
	RW_FORMAT_PERCENT,
	RW_FORMAT_HSV,
	RW_FORMAT_HSV_OFFSET,
	RW_FORMAT_TEXTURE,
	RW_FORMAT_COLOR,
	//NOTE NOTE NOTE must be kept in sync with enumFormatType

	RW_LVWIDTH,

	//reserved words for format flags must all be sequential and in order
	
	//NOTE NOTE NOTE must be kept in sync with enumFormatFlag
	RW_FORMAT_FLAG_UI_LEFT,
	RW_FORMAT_FLAG_UI_RIGHT,
	RW_FORMAT_FLAG_UI_RESIZABLE,
	RW_FORMAT_FLAG_UI_NOTRANSLATE_HEADER,
	RW_FORMAT_FLAG_UI_NOHEADER,
	RW_FORMAT_FLAG_UI_NODISPLAY,
	//NOTE NOTE NOTE must be kept in sync with enumFormatFlag

	RW_FILENAME,
	RW_CURRENTFILE,
	RW_TIMESTAMP,
	RW_LINENUM,

	RW_MINBITS,
	RW_PRECISION,

	RW_FLOAT_HUNDREDTHS,
	RW_FLOAT_TENTHS,
	RW_FLOAT_ONES,
	RW_FLOAT_FIVES,
	RW_FLOAT_TENS,

	RW_DEF,
	RW_DEFAULT,

	RW_FLAGS,
	RW_BOOLFLAG,
	RW_USEDFIELD,

	RW_RAW,
	RW_POINTER,
	RW_INT,

	RW_VEC3,
	RW_VEC2,
	RW_RGB,
	RW_RGBA,
	RW_RG,

	RW_SUBTABLE,
	RW_ALLCAPSSTRUCT,

	RW_INDEX,
	RW_AUTO_INDEX,

	RW_WIKI,

	RW_REDUNDANT_STRUCT,

	RW_EMBEDDED_FLAT,

	RW_USERFLAG,

	RW_REFDICT,
	RW_RESOURCEDICT,
	RW_COPYDICT,

	RW_REDUNDANT,
	RW_STRUCTPARAM,
	

	RW_PERSIST,
	RW_NOTRANSACT,
	RW_SOMETIMESTRANSACT,
	// End of bit flags

	RW_REQUESTINDEXDEFINE,

	RW_COMMAND,

	RW_POLYCHILDTYPE,
	RW_POLYPARENTTYPE,

	RW_FORCECONTAINER,

	RW_LATEBIND,

	RW_WIKILINK,

	RW_FORMATSTRING,

	RW_SELF_ONLY,

	RW_SUBSCRIBE,

	RW_SIMPLE_INHERITANCE,

	RW_BLOCK_EARRAY,

	RW_AS_MATRIX,
};

#define FIRST_FORMAT_RW RW_FORMAT_IP
#define FIRST_FORMAT_FLAG_RW RW_FORMAT_FLAG_UI_LEFT

#define FIRST_FLOAT_ACCURACY_RW RW_FLOAT_HUNDREDTHS
#define LAST_FLOAT_ACCURACY_RW RW_FLOAT_TENS

char *sFieldCommandStringReservedWords[] =
{
	"NAME",
	"ADDNAMES",
	"FORMAT_IP",
	"FORMAT_KBYTES",
	"FORMAT_FRIENDLYDATE",
	"FORMAT_FRIENDLYSS2000",
	"FORMAT_DATESS2000",
	"FORMAT_FRIENDLYCPU",
	"FORMAT_PERCENT",
	"FORMAT_HSV",
	"FORMAT_HSV_OFFSET",
	"FORMAT_TEXTURE",
	"FORMAT_COLOR",

	"FORMAT_LVWIDTH",

	"FORMAT_UI_LEFT",
	"FORMAT_UI_RIGHT",
	"FORMAT_UI_RESIZABLE",
	"FORMAT_UI_NOTRANSLATE_HEADER",
	"FORMAT_UI_NOHEADER",
	"FORMAT_UI_NODISPLAY",

	"FILENAME",
	"CURRENTFILE",
	"TIMESTAMP",
	"LINENUM",

	"MINBITS",
	"PRECISION",
	
	"FLOAT_HUNDREDTHS",
	"FLOAT_TENTHS",
	"FLOAT_ONES",
	"FLOAT_FIVES",
	"FLOAT_TENS",

	"DEF",
	"DEFAULT",

	"FLAGS",
	"BOOLFLAG",
	"USEDFIELD",
	"RAW",
	"POINTER",
	"INT",
	
	"VEC3",
	"VEC2",
	"RGB",
	"RGBA",
	"RG",

	"SUBTABLE",
	"STRUCT",
	
	"INDEX",
	"AUTO_INDEX",

	"WIKI",

	"REDUNDANT_STRUCT",

	"EMBEDDED_FLAT",

	"USERFLAG",

	"REFDICT",
	"RESOURCEDICT",
	"COPYDICT",

	// Misc bit flags

	"REDUNDANTNAME",
	"STRUCTPARAM",

	"PERSIST",
	"NO_TRANSACT",
	"SOMETIMES_TRANSACT",

	"INDEX_DEFINE",

	"COMMAND",

	"POLYCHILDTYPE",
	"POLYPARENTTYPE",

	"FORCE_CONTAINER",

	"LATEBIND",

	"WIKILINK",

	"FORMATSTRING",

	"SELF_ONLY",
	"SELF_ONLY_SUBSCRIBE_OK",

	"SIMPLE_INHERITANCE",

	"BLOCK_EARRAY",

	"AS_MATRIX",

	NULL
};

struct StringTree *pFieldCommandStringTree = NULL;


//these strings are prepended to the type field, with TOK_ before them
char *sTokTypeFlags[] =
{
	"POOL_STRING",
	"POOL_STRING_DB",
	"ESTRING",
	"SERVER_ONLY",
	"CLIENT_ONLY",
	"SELF_AND_TEAM_ONLY",
	"STRUCT_NORECURSE",
	"CASE_SENSITIVE",
	"EDIT_ONLY",
	"NO_INDEX",
	"VOLATILE_REF",
	"ALWAYS_ALLOC",
	"NO_INDEXED_PREALLOC",
	"NON_NULL_REF",
	"VITAL_REF",
	"NO_WRITE",
	"NO_NETSEND",
	"NO_TEXT_SAVE",
	"NO_INHERIT",
	"UNOWNED",
	"NO_LOG",
	"NON_NULL_REF__ERROR_ONLY",
	"SUBSCRIBE",
	"LOGIN_SUBSCRIBE",
};

//these are the same as the ones above, except that for REDUNDANT_NAME fields, they
//are ignored
char *sTokTypeFlags_NoRedundantRepeat[] =
{
	"KEY",
	"REQUIRED",
};

bool StructParser::FieldHasTypeFlag(STRUCT_FIELD_DESC *pField, char *pFlagName)
{
	int i;

	for (i=0 ; i < sizeof(sTokTypeFlags) / sizeof(sTokTypeFlags[0]); i++)
	{
		if (strcmp(pFlagName, sTokTypeFlags[i]) == 0)
		{
			return !!(pField->iTypeFlagsFound & (1 << i));
		}
	}

	FieldAssert(pField, 0, "Unknown type flag");
	return false;
}


void StructParser::AddExtraNameToField(Tokenizer *pTokenizer, STRUCT_FIELD_DESC *pField, char *pName, bool bKeepOriginalName)
{
	if (!bKeepOriginalName)
	{
		if (strcmp(pField->userFieldName, pField->curStructFieldName) == 0)
		{
			strcpy(pField->userFieldName, pName);
			return;
		}
	}

	ASSERT(pTokenizer,pField->iNumRedundantNames < MAX_REDUNDANT_NAMES_PER_FIELD, "Too many redundant names");

	strcpy(pField->redundantNames[pField->iNumRedundantNames++], pName);
}

void StructParser::ProcessStructFieldCommandString(STRUCT_DEF *pStruct, STRUCT_FIELD_DESC *pField, char *pSourceString, 
	int iNumNotStrings, char **ppNotStrings, char *pFileName, int iLineNum, Tokenizer *pMainTokenizer)
{
	Tokenizer tokenizer;
	Tokenizer *pTokenizer = &tokenizer;

	char stringToUse[TOKENIZER_MAX_STRING_LENGTH * 3];

	sprintf(stringToUse, "%s %s %s %s", m_pPrefix ? m_pPrefix : " ", pStruct->forAllString[0] ? pStruct->forAllString : " ", pSourceString, m_pSuffix ? m_pSuffix : " ");

	ReplaceMacrosInString(stringToUse, TOKENIZER_MAX_STRING_LENGTH);

	ClipStrings(stringToUse, iNumNotStrings, ppNotStrings);

	pTokenizer->LoadFromBuffer(stringToUse, (int)strlen(stringToUse) + 1, pFileName, iLineNum);

	pTokenizer->SetExtraReservedWords(sFieldCommandStringReservedWords, &pFieldCommandStringTree);

	Token token;
	enumTokenType eType;

	do
	{
		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_NONE)
		{
			break;
		}

		if (eType == TOKEN_IDENTIFIER)
		{
			int i;
			bool bFound = false;

			for (i=0; i < sizeof(sTokTypeFlags) / sizeof(sTokTypeFlags[0]); i++)
			{
				STATICASSERT(i < 32, "too many sTokTypeFlags");

				if (strcmp(token.sVal, sTokTypeFlags[i]) == 0)
				{
					pField->iTypeFlagsFound |= (1 << i);
					bFound = true; 
					break;
				}
			}

			if (!bFound)
			{
				for (i=0; i < sizeof(sTokTypeFlags_NoRedundantRepeat) / sizeof(sTokTypeFlags_NoRedundantRepeat[0]); i++)
				{
					STATICASSERT(i < 32, "too many sTokTypeFlags_NoRedundantRepeat");

					if (strcmp(token.sVal, sTokTypeFlags_NoRedundantRepeat[i]) == 0)
					{
						pField->iTypeFlags_NoRedundantRepeatFound |= (1 << i);
						bFound = true; 
						break;
					}
				}
			}

			if (!bFound)
			{
				char errorString[256];
				sprintf(errorString, "Found unrecognized struct field command %s", token.sVal);
				ASSERT(pTokenizer,0, errorString);
			}

			continue;
		}


		if (eType != TOKEN_RESERVEDWORD)
		{
			pTokenizer->StringifyToken(&token);
			char errorString[256];
			sprintf(errorString, "Found unrecognized struct field command %s", token.sVal);
			ASSERT(pTokenizer,0, errorString);
		}

		if (token.iVal >= FIRST_FORMAT_RW && token.iVal - FIRST_FORMAT_RW <= FORMAT_COUNT - 1)
		{
			ASSERT(pTokenizer,pField->eFormatType == FORMAT_NONE, "Only one FORMAT_XXX allowed per field");
			pField->eFormatType = (enumFormatType)(token.iVal - FIRST_FORMAT_RW + 1);
		}
		else if (token.iVal >= FIRST_FORMAT_FLAG_RW && token.iVal - FIRST_FORMAT_FLAG_RW < FORMAT_FLAG_COUNT)
		{
			pField->bFormatFlags[token.iVal - FIRST_FORMAT_FLAG_RW] = true;
		}
		else if (token.iVal >= FIRST_FLOAT_ACCURACY_RW && token.iVal <= LAST_FLOAT_ACCURACY_RW)
		{
			ASSERT(pTokenizer,pField->iFloatAccuracy == 0, "Can't have two FLOAT_XXX commands for same field");
			pField->iFloatAccuracy = token.iVal - FIRST_FLOAT_ACCURACY_RW + 1;
		}
		else switch (token.iVal)
		{
		case RW_NAME:
		case RW_ADDNAMES:
			{
				bool bAddNames = (token.iVal == RW_ADDNAMES);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after NAME or ADDNAMES");
				
				do
				{
					eType = pTokenizer->GetNextToken(&token);
					ASSERT(pTokenizer, (eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING) && strlen(token.sVal) <  MAX_NAME_LENGTH, "Expected identifier or string after NAME(");

					AddExtraNameToField(pTokenizer, pField, token.sVal, bAddNames);

					eType = pTokenizer->CheckNextToken(&token);

					if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
					{
						pTokenizer->GetNextToken(&token);
					}
					else
					{
						break;
					}
				} while (1);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after NAME(x");
			}

			break;

		case RW_LVWIDTH:
			ASSERT(pTokenizer,pField->lvWidth == 0, "Found two FORMAT_LVWIDTH tokens");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after LVWIDTH");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected number after LVWIDTH(");
			ASSERT(pTokenizer,token.iVal > 0 && token.iVal <= 255, "LVWIDTH out of range");
			pField->lvWidth = token.iVal;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after LVWIDTH(x");
			break;

		case RW_FILENAME:
			pField->bFoundFileNameToken = true;
			break;

		case RW_CURRENTFILE:
			pField->bFoundCurrentFileToken = true;
			break;

		case RW_TIMESTAMP:
			pField->bFoundTimeStampToken = true;
			break;

		case RW_LINENUM:
			pField->bFoundLineNumToken = true;
			break;

		case RW_MINBITS:
			ASSERT(pTokenizer,pField->iMinBits == 0, "Found two MINBITS tokens");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after MINBITS");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected number after MINBITS(");
			ASSERT(pTokenizer,token.iVal > 0 && token.iVal <= 255, "MINBITS out of range");
			pField->iMinBits = token.iVal;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after MINBITS(x");
			break;

		case RW_PRECISION:
			ASSERT(pTokenizer,pField->iPrecision == 0, "Found two PRECISION tokens");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after PRECISION");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected number after PRECISION(");
			ASSERT(pTokenizer,token.iVal > 0 && token.iVal <= 255, "PRECISION out of range");
			pField->iPrecision = token.iVal;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after PRECISION(x");
			break;

		case RW_DEF:
		case RW_DEFAULT:
			ASSERT(pTokenizer,pField->defaultString[0] == 0, "Found two DEFAULTS");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after DEFAULT");

			pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

			ASSERT(pTokenizer,token.sVal[0] != 0, "Default seems to be empty");

			strcpy(pField->defaultString, token.sVal);
			break;

		case RW_FLAGS:
			pField->bFoundFlagsToken = true;
			break;

		case RW_BOOLFLAG:
			pField->bFoundBoolFlagToken = true;
			break;

		case RW_USEDFIELD:
			pField->bFoundUsedField = true;
			break;

		case RW_INT:
			pField->bFoundIntToken = true;
			break;

		case RW_RAW:
			ASSERT(pTokenizer,!pField->bFoundRawToken, "Found two RAW tokens");

			pField->bFoundRawToken = true;

			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
			{
				pTokenizer->GetNextToken(&token);
				pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

				ASSERT(pTokenizer,token.sVal[0] != 0, "RAW size appears to empty");

				strcpy(pField->rawSizeString, token.sVal);
			}
			break;

		case RW_POINTER:
			ASSERT(pTokenizer,!pField->bFoundPointerToken, "Found two RAW tokens");

			pField->bFoundPointerToken = true;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after POINTER");
	
			pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);

			ASSERT(pTokenizer,token.sVal[0] != 0, "POINTER size appears to empty");

			strcpy(pField->pointerSizeString, token.sVal);
			
			break;			

		case RW_VEC3:
			pField->bFoundVec3Token = true;
			break;

		case RW_VEC2:
			pField->bFoundVec2Token = true;
			break;

		case RW_RGB:
			pField->bFoundRGBToken = true;
			break;

		case RW_RGBA:
			pField->bFoundRGBAToken = true;
			break;

		case RW_RG:
			pField->bFoundRGToken = true;
			break;

		case RW_SUBTABLE:
			ASSERT(pTokenizer,pField->subTableName[0] == 0, "Found two SUBTABLE tokens");
			ASSERT(pTokenizer,pField->refDictionaryName[0] == 0, "SUBTABLE can't exist with RESOURCEDICT or REFDICT");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after SUBTABLE");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after SUBTABLE(");
			strcpy(pField->subTableName, token.sVal);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after SUBTABLE(x");
			break;



		case RW_ALLCAPSSTRUCT:
			ASSERT(pTokenizer,pField->structTpiName[0] == 0, "Found two STRUCT tokens");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after STRUCT");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after STRUCT(");
			

			
			strcpy(pField->structTpiName, token.sVal);
			
			eType = pTokenizer->CheckNextToken(&token);
			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
			{
				eType = pTokenizer->GetNextToken(&token);
				eType = pTokenizer->GetNextToken(&token);

				if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "OPTIONAL") == 0)
				{
					pField->bForceOptionalStruct = true;
				}
				else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "EARRAY")== 0)
				{
					pField->bForceEArrayOfStructs = true;
				}
				else 
				{
					ASSERT(pTokenizer,0, "Expected OPTIONAL or EARRAY after STRUCT(x,");
				}
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after STRUCT(x");
			break;

		case RW_INDEX:
			{
				//hacky fake realloc
				FIELD_INDEX *pNewIndexes = new FIELD_INDEX[pField->iNumIndexes + 1];
				if (pField->pIndexes)
				{
					memcpy(pNewIndexes, pField->pIndexes, sizeof(FIELD_INDEX) * pField->iNumIndexes);
					delete pField->pIndexes;
				}

				pField->pIndexes = pNewIndexes;

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after INDEX");
				eType = pTokenizer->GetNextToken(&token);

				ASSERT(pTokenizer,eType == TOKEN_INT || eType == TOKEN_IDENTIFIER, "expected int or identifer after INDEX");
				pTokenizer->StringifyToken(&token);
				ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH - 1, "INDEX identifier overflow");

				strcpy(pField->pIndexes[pField->iNumIndexes].indexString, token.sVal);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after INDEX(x");
			
				eType = pTokenizer->GetNextToken(&token);
				ASSERT(pTokenizer,eType == TOKEN_STRING || eType == TOKEN_IDENTIFIER, "expected string or identifier  after INDEX(x,");
				ASSERT(pTokenizer,strlen(token.sVal) < MAX_NAME_LENGTH - 1, "INDEX name overflow");

				strcpy(pField->pIndexes[pField->iNumIndexes++].nameString, token.sVal);

				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after INDEX(x,x");
			}
			break;

		case RW_AUTO_INDEX:
			{
				int iArraySize = atoi(pField->arraySizeString);
				char nameString[MAX_NAME_LENGTH];
				int i;
				
				ASSERT(pTokenizer,iArraySize > 0, "AUTO_INDEX requires a non-zero literal int for array size");
				ASSERT(pTokenizer,pField->pIndexes == NULL, "AUTO_INDEX conflicts with INDEX or another AUTO_INDEX");
				pField->iNumIndexes = iArraySize;
				pField->pIndexes = new FIELD_INDEX[iArraySize];
				
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after AUTO_INDEX");
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 5, "Expected identifier after AUTO_INDEX(");
				strcpy(nameString, token.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after AUTO_INDEX(x");

				for (i=0; i < iArraySize; i++)
				{
					sprintf(pField->pIndexes[i].indexString, "%d", i);
					sprintf(pField->pIndexes[i].nameString, "%s_%d", nameString, i);
				}
			}
			break;






		case RW_COMMA:
			break;

		case RW_WIKI:
			ASSERT(pTokenizer,pField->iNumWikiComments < MAX_WIKI_COMMENTS, "Two many WIKI comments for one field");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after WIKI");
			
			eType = pTokenizer->CheckNextToken(&token);
			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "AUTO") == 0)
			{
				pTokenizer->GetNextToken(&token);
				
				pMainTokenizer->GetSurroundingSlashedCommentBlock(&token, false);

				if (strlen(token.sVal) > 0)
				{
					pField->pWikiComments[pField->iNumWikiComments] = new char[token.iVal + 1];
					strcpy(pField->pWikiComments[pField->iNumWikiComments++] , token.sVal);
				}
			}
			else
			{
				
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected string after WIKI(");

				pField->pWikiComments[pField->iNumWikiComments] = new char[token.iVal + 1];
				strcpy(pField->pWikiComments[pField->iNumWikiComments++] , token.sVal);
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after WIKI");
			break;

		case RW_REDUNDANT_STRUCT:
			ASSERT(pTokenizer,pField->iNumRedundantStructInfos < MAX_REDUNDANT_STRUCTS, "Too many REDUNDANT_STRUCTs");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after REDUNDANT_STRUCT");
			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected string or identifier after REDUNDANT_STRUCT(");

			ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "Redundant struct name too long");

			strcpy(pField->redundantStructs[pField->iNumRedundantStructInfos].name, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after REDUNDANT_STRUCT(x");
			eType = pTokenizer->GetNextToken(&token);

			ASSERT(pTokenizer,eType == TOKEN_IDENTIFIER || eType == TOKEN_STRING, "Expected string or identifier after REDUNDANT_STRUCT(x,");

			ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "Redundant struct subtable name too long");

			strcpy(pField->redundantStructs[pField->iNumRedundantStructInfos++].subTable, token.sVal);
			
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ( after REDUNDANT_STRUCT");

			break;

		case RW_EMBEDDED_FLAT:
			pField->bFlatEmbedded = true;
			eType = pTokenizer->CheckNextToken(&token);
			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
			{
				pTokenizer->GetNextToken(&token);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after EMBEDDED_FLAT(");
				strcpy(pField->flatEmbeddingPrefix, token.sVal);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after EMBEDDED_FLAT(x");
			}

			break;

		case RW_USERFLAG:
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after USERFLAG");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after USERFLAG(");
	
			bool bAlreadyFound;

			int i;

			bAlreadyFound = false;
			for (i=0; i < pField->iNumUserFlags; i++)
			{
				if (strcmp(token.sVal, pField->userFlags[i]) == 0)
				{
					bAlreadyFound = true;
					break;
				}
			}

			if (!bAlreadyFound)
			{
				ASSERT(pTokenizer,pField->iNumUserFlags < MAX_USER_FLAGS, "Too many distinct USERFLAGs found");

				strcpy(pField->userFlags[pField->iNumUserFlags++], token.sVal);
			}

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after USERFLAG(x");
			break;

		case RW_REFDICT:
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after REFDICT");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after REFDICT(");

			ASSERT(pTokenizer,pField->eDataType == DATATYPE_REFERENCE, "Can't have REFDICT for non-reference");

			strcpy(pField->refDictionaryName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after REFDICT(x");
			break;

		case RW_RESOURCEDICT:
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after RESOURCEDICT");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after RESOURCEDICT(");

			ASSERT(pTokenizer,pField->subTableName[0] == 0, "RESOURCEDICT can't exist with SUBTABLE");

			strcpy(pField->refDictionaryName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after RESOURCEDICT(x");
			break;

		case RW_COPYDICT:
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after COPYDICT");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_NAME_LENGTH - 1, "Expected identifier after COPYDICT(");

			ASSERT(pTokenizer,pField->eDataType == DATATYPE_REFERENCE, "Can't have COPYDICT for non-reference");
			strcpy(pField->refDictionaryName, "CopyDict_");
			strcat(pField->refDictionaryName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after COPYDICT(x");
			break;

		case RW_REDUNDANT:
			pField->bFoundRedundantToken = true;
			break;

		case RW_STRUCTPARAM:
			pField->bFoundStructParam = true;
			break;

		case RW_PERSIST:
			pField->bFoundPersist = true;
			break;

		case RW_NOTRANSACT:
			pField->bFoundNoTransact = true;
			break;

		case RW_SOMETIMESTRANSACT:
			pField->bFoundSometimesTransact = true;
			break;

		case RW_REQUESTINDEXDEFINE:
			pField->bFoundRequestIndexDefine = true;
			break;

		case RW_COMMAND:
			STRUCT_COMMAND *pCommand;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after COMMAND");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected command name after COMMAND");
			pCommand = new STRUCT_COMMAND;
			pCommand->pCommandName = new char[token.iVal + 1];
			strcpy(pCommand->pCommandName, token.sVal);

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, "Expected , after COMMAND(name");
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Expected command string after COMMAND(name,");

			pCommand->pCommandString = new char[token.iVal + 1];
			strcpy(pCommand->pCommandString, token.sVal);

			pCommand->pNext = pField->pFirstCommand;
			pField->pFirstCommand = pCommand;

			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after COMMAND(name, string");
			break;

		case RW_POLYCHILDTYPE:
			ASSERT(pTokenizer,pField == pStruct->pStructFields[0], "POLYCHILDTYPE only legal on first field of struct");
			strcpy(pStruct->structNameIInheritFrom, pField->typeName);
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after POLYCHILDTYPE");
			pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);
			ASSERT(pTokenizer,token.iVal < MAX_NAME_LENGTH, "POLYCHILDTYPE string too long");
			strcpy(pField->myPolymorphicType, token.sVal);
			pField->bIAmPolyChildTypeField = true;
			break;

		case RW_POLYPARENTTYPE:
			ASSERT(pTokenizer,!pStruct->bIAmAPolymorphicParent && pStruct->structNameIInheritFrom[0] == 0, "Struct can't have two POLYXTYPEs");
			pStruct->bIAmAPolymorphicParent = true;
			pField->bIAmPolyParentTypeField = true;
			eType = pTokenizer->CheckNextToken(&token);
			if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
			{
				pTokenizer->GetNextToken(&token);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected int after POLYPARENTTYPE(");
				pStruct->iParentTypeExtraCount = token.iVal;
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected ) after POLYPARENTTYPE(x");
			}
			else
			{
				pStruct->iParentTypeExtraCount = 4;
			}

			break;

		case RW_FORCECONTAINER:
			ASSERT(pTokenizer,!pField->bFoundForceContainer, "Can't have two occurrences of FORCE_CONTAINER");
			pField->bFoundForceContainer = true;
			break;



		case RW_LATEBIND:
			pField->bFoundLateBind = true;
			break;

		case RW_WIKILINK:
			pField->bDoWikiLink = true;
			break;

		case RW_FORMATSTRING:
			ReadTokensIntoFormatString(pTokenizer, &pField->pFormatString);
			break;

		case RW_SELF_ONLY:
			pField->bFoundSelfOnly = true;
			break;


		case RW_SUBSCRIBE:
			pField->bFoundSubscribe = true;
			break;

		case RW_SIMPLE_INHERITANCE:
			pField->bFoundSimpleInheritance = true;
			break;

		case RW_BLOCK_EARRAY:
			pField->bFoundBlockEArray = true;
			break;

		case RW_AS_MATRIX:
			pField->bFoundAsMatrixToken = true;
			break;


		default:
			

			pTokenizer->StringifyToken(&token);
			char errorString[256];
			sprintf(errorString, "Found unrecognized struct field command %s", token.sVal);
			ASSERT(pTokenizer,0, errorString);
			break;
		}
	} while (1);
}

void StructParser::FixupFieldTypes_RightBeforeWritingData(STRUCT_DEF *pStruct)
{

}


void StructParser::ReadTokensIntoFormatString(Tokenizer *pTokenizer, char **ppFormatString)
{
	Token token;
	enumTokenType eType;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Expected ( after FORMATSTRING");
	
	while (1)
	{
		char name[256];
		char curString[4096];
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, sizeof(name)-1, "Expected FORMATSTRING name");

		strcpy(name, token.sVal);

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_EQUALS, "Expected = after FORMATSTRING x");

		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_INT)
		{
			AssertNameIsLegalForFormatStringInt(pTokenizer, name);
			sprintf(curString, "%s = %d", name, token.iVal);
			AddStringToFormatString(ppFormatString, curString);
		}
		else if (eType == TOKEN_STRING)
		{
			AssertNameIsLegalForFormatStringString(pTokenizer, name);
			ASSERT(pTokenizer,token.iVal + strlen(name) < sizeof(curString) - 10, "format string overflow");

			sprintf(curString, "%s = \\\"%s\\\"", name, token.sVal);
			AddStringToFormatString(ppFormatString, curString);
		}
		else
		{
			ASSERT(pTokenizer,0, "Expected int or string in format string, found neither");
		}

		eType = pTokenizer->GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
		{
			break;
		}

		ASSERT(pTokenizer,eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA, "Expected , or ) in FORMATSTRING");
	}
}


void StructParser::FixupFieldTypes(STRUCT_DEF *pStruct)
{
	int i;
	enumIdentifierType eIdentifierType;

	for (i=0; i < pStruct->iNumFields; i++)
	{
		STRUCT_FIELD_DESC *pField = pStruct->pStructFields[i];

		if (pField->iNumIndexes > 0)
		{
			FieldAssert(pField, pField->iArrayDepth > 0, "Can't have INDEX without an array");
			pField->iArrayDepth--;
		}

		if (!pField->bFoundSpecialConstKeyword)
		{

			if (pField->bOwns)
			{
				pField->eDataType = DATATYPE_STRUCT;
			}
			else if (pField->eDataType == DATATYPE_REFERENCE)
			{	
				FieldAssert(pField, pField->refDictionaryName[0] != 0, "Reference has no ref dictionary name, either implied from type or from REFDICT(x)");
			}
			else if ((eIdentifierType = m_pParent->GetDictionary()->FindIdentifier(pField->typeName)) != IDENTIFIER_NONE)
			{
				if (eIdentifierType == IDENTIFIER_ENUM)
				{
					pField->eDataType = DATATYPE_INT;

					if (pField->subTableName[0] == 0)
					{
						sprintf(pField->subTableName, "%sEnum", pField->typeName); 
					}
				}
				else if (eIdentifierType == IDENTIFIER_STRUCT || eIdentifierType == IDENTIFIER_STRUCT_CONTAINER)
				{
					pField->eDataType = DATATYPE_STRUCT;
					pField->pStructSourceFileName = m_pParent->GetDictionary()->GetSourceFileForIdentifier(pField->typeName);
				}

			}
			else if (IsOutlawedTypeName(pField->typeName))
			{
				char errorString[1024];
				sprintf(errorString, "Unsupported type: %s", pField->typeName);
				FieldAssert(pField, 0, errorString);
			}
			else if (IsLinkName(pField->typeName))
			{
				pField->eDataType = DATATYPE_LINK;
			}
			else if (IsFloatName(pField->typeName))
			{
				pField->eDataType = DATATYPE_FLOAT;
			}
			else if (IsIntName(pField->typeName) || pField->bFoundIntToken)
			{
				pField->eDataType = DATATYPE_INT;
			}
			else if (IsCharName(pField->typeName))
			{
				pField->eDataType = DATATYPE_CHAR;
			}
			else if (strcmp(pField->typeName, "Vec4") == 0)
			{
				pField->eDataType = DATATYPE_VEC4;
			}
			else if (strcmp(pField->typeName, "Vec3") == 0)
			{
				pField->eDataType = DATATYPE_VEC3;
			}
			else if (strcmp(pField->typeName, "Vec2") == 0)
			{
				pField->eDataType = DATATYPE_VEC2;
			}
			else if (strcmp(pField->typeName, "IVec4") == 0)
			{
				pField->eDataType = DATATYPE_IVEC4;
			}
			else if (strcmp(pField->typeName, "IVec3") == 0)
			{
				pField->eDataType = DATATYPE_IVEC3;
			}
			else if (strcmp(pField->typeName, "IVec2") == 0)
			{
				pField->eDataType = DATATYPE_IVEC2;
			}
			else if (strcmp(pField->typeName, "Mat3") == 0)
			{
				pField->eDataType = DATATYPE_MAT3;
			}
			else if (strcmp(pField->typeName, "Mat4") == 0)
			{
				pField->eDataType = DATATYPE_MAT4;
			}
			else if (strcmp(pField->typeName, "Mat44") == 0)
			{
				pField->eDataType = DATATYPE_MAT44;
			}
			else if (strcmp(pField->typeName, "Quat") == 0)
			{
				pField->eDataType = DATATYPE_QUAT;
			}
			else if (pField->structTpiName[0])
			{
				pField->eDataType = DATATYPE_STRUCT;
				
			}
			else if (strcmp(pField->typeName, "TokenizerParams") == 0)
			{
				pField->eDataType = DATATYPE_TOKENIZERPARAMS;
			}
			else if (strcmp(pField->typeName, "TokenizerFunctionCall") == 0)
			{
				pField->eDataType = DATATYPE_TOKENIZERFUNCTIONCALL;
			}
			else if (strcmp(pField->typeName, "StashTable") == 0)
			{
				pField->eDataType = DATATYPE_STASHTABLE;
			}
			else if (strcmp(pField->typeName, "MultiVal") == 0)
			{
				pField->eDataType = DATATYPE_MULTIVAL;
			}
			else if (pField->eDataType != DATATYPE_VOID) 
			{
				if (pStruct->bNoUnrecognized)
				{
					FieldAssertf(pField, 0, "AST_NO_UNRECOGNIZED set and found unrecognized data type %s. This is probably because your struct is shared across projects, but an enum or struct it includes is not shared.", pField->typeName);
				}
				else
				{
					if (pField->iNumAsterisks == 0)
					{
						if (pField->subTableName[0] == 0)
						{
							sprintf(pField->subTableName, "%sEnum", pField->typeName);
						}
					
						pField->eDataType = DATATYPE_INT;
						
					}
					else
					{
						pField->eDataType = DATATYPE_STRUCT;
						sprintf(pField->structTpiName, "parse_%s", pField->typeName);
					}
				}

			}
		}
		else if (pField->eDataType != DATATYPE_REFERENCE)
		{
			if	((eIdentifierType = m_pParent->GetDictionary()->FindIdentifier(pField->typeName)) != IDENTIFIER_NONE)
			{
				if (eIdentifierType == IDENTIFIER_ENUM)
				{
					pField->eDataType = DATATYPE_INT;

					if (pField->subTableName[0] == 0)
					{
						sprintf(pField->subTableName, "%sEnum", pField->typeName); 
					}
				}
				else if (eIdentifierType == IDENTIFIER_STRUCT || eIdentifierType == IDENTIFIER_STRUCT_CONTAINER)
				{

					pField->eDataType = DATATYPE_STRUCT;
					pField->pStructSourceFileName = m_pParent->GetDictionary()->GetSourceFileForIdentifier(pField->typeName);
					
				}

			}
		}


		if (pField->bFoundVec3Token)
		{
			if (pField->eDataType == DATATYPE_FLOAT && pField->iArrayDepth)
			{
				pField->iArrayDepth--;
				pField->eDataType = DATATYPE_VEC3;
			}
			else
			{
				FieldAssert(pField, pField->eDataType == DATATYPE_VEC3, "TOK_VEC3 only legal for data that is Vec3 or float[]");
			}
		}

		if (pField->bFoundVec2Token)
		{
			if (pField->eDataType == DATATYPE_FLOAT && pField->iArrayDepth)
			{
				pField->iArrayDepth--;
				pField->eDataType = DATATYPE_VEC2;
			}
			else
			{
				FieldAssert(pField, pField->eDataType == DATATYPE_VEC3 || pField->eDataType == DATATYPE_VEC2, 
					"TOK_VEC2 only legal for data that is Vec3 or Vec2 or float[]");
			}
		}

		if (pField->bFoundRGBAToken)
		{
			if (pField->eDataType == DATATYPE_INT && pField->iArrayDepth)
			{
				pField->iArrayDepth--;
				pField->eDataType = DATATYPE_RGBA;
			}
			else if (pField->eDataType == DATATYPE_INT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT && strcmp(pField->typeName, "Color") == 0)
			{
				pField->eDataType = DATATYPE_RGBA;
			}
			else
			{
				FieldAssert(pField, 0, "TOK_RGBA only legal for data that is U8[] or named \"Color\"");
			}
		}


		if (pField->bFoundRGBToken)
		{
			if (pField->eDataType == DATATYPE_INT && pField->iArrayDepth)
			{
				pField->iArrayDepth--;
				pField->eDataType = DATATYPE_RGB;
			}
			else
			{
				FieldAssert(pField, 0, "TOK_RGB only legal for data that is U8[]");
			}
		}

		if (pField->bFoundRGToken)
		{
			if (pField->eDataType == DATATYPE_INT && pField->iArrayDepth)
			{
				pField->iArrayDepth--;
				pField->eDataType = DATATYPE_RG;
			}
			else
			{
				FieldAssert(pField, 0, "TOK_RG only legal for data that is U8[]");
			}
		}

		if (pField->bFoundAsMatrixToken)
		{
			if (pField->eDataType == DATATYPE_MAT3)
			{
				pField->eDataType = DATATYPE_MAT3_ASMATRIX;
			}
			else if (pField->eDataType == DATATYPE_MAT4)
			{
				pField->eDataType = DATATYPE_MAT4_ASMATRIX;
			}
			else
			{
				FieldAssert(pField, 0, "TOK_AS_MATRIX only legal for Mat3 or Mat4");
			}
		}


		if (pField->bForceOptionalStruct || pField->bForceEArrayOfStructs)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_STRUCT, "Ambiguity from bForceOptionalStruct/bForceEArrayofStructs");
			FieldAssert(pField, pField->iArrayDepth == 0 && pField->iNumAsterisks == 0, "Can't force EARRAY or OPTIONAL structs with [] or *");

			if (pField->bForceOptionalStruct)
			{
				pField->eReferenceType = REFERENCETYPE_POINTER;
				pField->eStorageType = STORAGETYPE_EMBEDDED;
			}
			else
			{
				pField->eReferenceType = REFERENCETYPE_POINTER;
				pField->eStorageType = STORAGETYPE_EARRAY;
			}
		}
		else if (!pField->bFoundSpecialConstKeyword && pField->eDataType != DATATYPE_REFERENCE)
		{

			FieldAssert(pField, pField->iArrayDepth == 0 || pField->iArrayDepth == 1, "Can't parse multidimensional arrays");

			pField->bArray = (pField->iArrayDepth == 1);


			switch (pField->iNumAsterisks)
			{
			case 0:
				pField->eReferenceType = REFERENCETYPE_DIRECT;
				pField->eStorageType = pField->bArray ? STORAGETYPE_ARRAY : STORAGETYPE_EMBEDDED;
				break;

			case 1:
				FieldAssert(pField, !pField->bArray, "Arrays of pointers not supported"); 
				pField->eReferenceType = REFERENCETYPE_POINTER;
				pField->eStorageType = STORAGETYPE_EMBEDDED;
				break;

			case 2:
				FieldAssert(pField, !pField->bArray, "Arrays of EArrays not supported");
				pField->eReferenceType = REFERENCETYPE_POINTER;
				pField->eStorageType = STORAGETYPE_EARRAY;
				break;

			}
		}
		








	
		if (pField->bFoundCurrentFileToken)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_CHAR && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_POINTER,
				"CURRENTFILE only supported for char*");

			pField->eDataType = DATATYPE_CURRENTFILE;
		}

		if (pField->bFoundFileNameToken)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_CHAR /*&& pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_POINTER*/,
				"FILENAME only supported for char* or char**");

			pField->eDataType = DATATYPE_FILENAME;
		}

		if (pField->bFoundTimeStampToken)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT,
				"TIMESTAMP only supported for int");

			pField->eDataType = DATATYPE_TIMESTAMP;
		}

		if (pField->bFoundLineNumToken)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT,
				"LINENUM only supported for int");

			pField->eDataType = DATATYPE_LINENUM;
		}

		if (pField->bFoundFlagsToken)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT,
				"FLAGS only supported for direct single non-array int");
		}

		if (pField->bFoundBoolFlagToken)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT,
				"BOOLFLAG only supported for int");

			pField->eDataType = DATATYPE_BOOLFLAG;
		}

		if (pField->bFoundUsedField)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT && pField->eStorageType == STORAGETYPE_ARRAY && pField->eReferenceType == REFERENCETYPE_DIRECT,
				"USEDFIELD only supported for U32[]");

		}

		if (pField->bFoundRawToken)
		{
			FieldAssert(pField, pField->eDataType < DATATYPE_FIRST_SPECIAL, "Can't have RAW along with some other special type");
			pField->eDataType = DATATYPE_RAW;
			pField->eStorageType = STORAGETYPE_EMBEDDED;
			pField->eReferenceType = REFERENCETYPE_DIRECT;
		}

		if (pField->bFoundPointerToken)
		{
			FieldAssert(pField, pField->eDataType < DATATYPE_FIRST_SPECIAL, "Can't have POINTER along with some other special type");
			FieldAssert(pField, pField->eReferenceType == REFERENCETYPE_POINTER, "Can't have POINTER without having, well, a pointer");
			pField->eDataType = DATATYPE_POINTER;
			pField->eStorageType = STORAGETYPE_EMBEDDED;
		}

		if (pField->bBitField)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT && pField->eReferenceType == REFERENCETYPE_DIRECT
				&& pField->eStorageType == STORAGETYPE_EMBEDDED, "Bitfields only supported for single ints");
			pField->eDataType = DATATYPE_BIT;

			FieldAssert(pField, pField->defaultString[0] == 0 || isInt(pField->defaultString), "Bitfields can only have numeric values as default values");
		}



	

		if (pField->bFoundLateBind)
		{
			FieldAssert(pField, pField->eDataType == DATATYPE_INT || pField->eDataType == DATATYPE_STRUCT, "Found unexecpted LATEBIND");
			pField->eDataType = DATATYPE_STRUCT;
		}

		if (pField->eDataType == DATATYPE_STRUCT 
			&& strcmp(pField->typeName, "InheritanceData") == 0
			&& pField->eStorageType == STORAGETYPE_EMBEDDED
			&& pField->eReferenceType == REFERENCETYPE_POINTER)
		{
			pField->bIsInheritanceStruct = true;
		}

		if (pField->eDataType == DATATYPE_FLOAT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT)
		{
			//direct embedded floats are now allowed to have any default...
		}
		else
		{
			if (pField->defaultString[0])
			{
				FieldAssertf(pField, !isNonWholeNumberFloatLiteral(pField->defaultString), "default value %s appears to be a non-whole-number literal float. This magically works for direct-embedded floats, but nothing else", pField->defaultString);
			}
		}

		//fixed size strings and direct embedded bits get their default values in a special way
		if (pField->eDataType == DATATYPE_CHAR && pField->eStorageType == STORAGETYPE_ARRAY && pField->eReferenceType == REFERENCETYPE_DIRECT
			|| pField->eDataType == DATATYPE_BIT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT)
		{
			if (pField->defaultString[0])
			{
				char tempString[4096];
				sprintf(tempString, " SPECIAL_DEFAULT = \\\"%s\\\" ", pField->defaultString);

				AddStringToFormatString(&pField->pFormatString, tempString);
			}
		}
		else
		{
		//if we have no default string, use "0", to avoid having to check repeatedly in other places whether it even exists
			if (pField->defaultString[0] == 0)
			{
				pField->defaultString[0] = '0';
			}
		}


		if (pField->bFlatEmbedded)
		{
			if (pField->eDataType != DATATYPE_STRUCT)
			{
				FieldAssertf(pField, 0, "Didn't recognize %s as a struct, can't FLAT_EMBED it",
					pField->typeName);
			}

			FieldAssert(pField, pField->eStorageType == STORAGETYPE_EMBEDDED
				&& pField->eReferenceType == REFERENCETYPE_DIRECT,
				"A FLAT_EMBEDDED struct must be direct embedded, not optional");
		}
	}


	for (i=0; i < pStruct->iNumFields; i++)
	{
		STRUCT_FIELD_DESC *pField = pStruct->pStructFields[i];

		//check if what we think is a struct is actually polymorphic.
		//Adding AST(STRUCT()) overrides this.
		if (pField->eDataType == DATATYPE_STRUCT && !pField->bIAmPolyChildTypeField && !pField->bIAmPolyParentTypeField
			&& !pField->structTpiName[0])
		{
			STRUCT_DEF *pOtherStruct = FindNamedStruct(pField->typeName);

			if (pOtherStruct && pOtherStruct->bIAmAPolymorphicParent)
			{
				pField->eDataType = DATATYPE_STRUCT_POLY;
			}
		}

	}
}

bool StructParser::IsStructAllStructParams(STRUCT_DEF *pStruct)
{
	int i;

	for (i=0; i < pStruct->iNumFields; i++)
	{
		if (!pStruct->pStructFields[i]->bFoundStructParam && pStruct->pStructFields[i]->userFieldName[0] && !(pStruct->pStructFields[i]->eDataType == DATATYPE_CURRENTFILE ))
		{
			return false;
		}
	}

	return true;
}

void StructParser::CalcLongestUserFieldName(STRUCT_DEF *pStruct)
{
	pStruct->iLongestUserFieldNameLength = (int)strlen(pStruct->structName);

	int i;

	if (pStruct->bHasStartString)
	{
		int iLen = (int)strlen(pStruct->startString);

		if (iLen > pStruct->iLongestUserFieldNameLength)
		{
			pStruct->iLongestUserFieldNameLength = iLen;
		}
	}

	for (i=0; i<pStruct->iNumEndStrings; i++)
	{
		int iLen = (int)strlen(pStruct->endStrings[i]);

		if (iLen > pStruct->iLongestUserFieldNameLength)
		{
			pStruct->iLongestUserFieldNameLength = iLen;
		}
	}


	for (i=0; i < pStruct->iNumIgnores; i++)
	{
		int iLen = (int)strlen(pStruct->ignores[i]);

		if (iLen > pStruct->iLongestUserFieldNameLength)
		{
			pStruct->iLongestUserFieldNameLength = iLen;
		}
	}

	for (i=0; i < pStruct->iNumIgnoreStructs; i++)
	{
		int iLen = (int)strlen(pStruct->ignoreStructs[i]);

		if (iLen > pStruct->iLongestUserFieldNameLength)
		{
			pStruct->iLongestUserFieldNameLength = iLen;
		}
	}

	for (i=0 ; i < pStruct->iNumFields; i++)
	{
		int iLen = (int)strlen(pStruct->pStructFields[i]->userFieldName);

		if (iLen > pStruct->iLongestUserFieldNameLength)
		{
			pStruct->iLongestUserFieldNameLength = iLen;
		}

		int j;

		for (j=0; j < pStruct->pStructFields[i]->iNumRedundantNames; j++)
		{
			int iLen = (int)strlen(pStruct->pStructFields[i]->redundantNames[j]);
	
			if (iLen > pStruct->iLongestUserFieldNameLength)
			{
				pStruct->iLongestUserFieldNameLength = iLen;
			}
		}

		for (j=0; j < pStruct->pStructFields[i]->iNumIndexes; j++)
		{
			int iLen = (int)strlen(pStruct->pStructFields[i]->pIndexes[j].nameString);
	
			if (iLen > pStruct->iLongestUserFieldNameLength)
			{
				pStruct->iLongestUserFieldNameLength = iLen;
			}
		}

		STRUCT_COMMAND *pCommand = pStruct->pStructFields[i]->pFirstCommand;

		while (pCommand)
		{
			int iLen = (int)strlen(pCommand->pCommandName);

			if (iLen > pStruct->iLongestUserFieldNameLength)
			{
				pStruct->iLongestUserFieldNameLength = iLen;
			}

			pCommand = pCommand->pNext;
		}	
		
		pCommand = pStruct->pStructFields[i]->pFirstBeforeCommand;

		while (pCommand)
		{
			int iLen = (int)strlen(pCommand->pCommandName);

			if (iLen > pStruct->iLongestUserFieldNameLength)
			{
				pStruct->iLongestUserFieldNameLength = iLen;
			}

			pCommand = pCommand->pNext;
		}
	}
}


void StructParser::DumpStructInitFunc(FILE *pFile, STRUCT_DEF *pStruct)
{
	int iNumBits = 0;
	char pClassName[1024];
	int iNumLateBinds = 0;

	RecurseOverAllFieldsAndFlatEmbeds(pStruct, pStruct, AreThereBitFields, 0, &iNumBits);

	if (iNumBits)
	{
		fprintf(pFile, "void FindAutoStructBitField(char *pStruct, int iAllocedSizeInWords, ParseTable *pTPIColumn);\n");
	}
	
	if (pStruct->structNameIInheritFrom[0])
	{
		fprintf(pFile, "extern ParseTable polyTable_%s[];\n", pStruct->structNameIInheritFrom);
		fprintf(pFile, "void AddEntryToPolyTable(ParseTable *polyTable, char *pName, ParseTable *pTable, int iSize);\n");
	}

	if (pStruct->fixupFuncName[0])
	{
		fprintf(pFile, "TextParserResult %s(%s *pStruct, enumTextParserFixupType eFixupType, void *pExtraData);\n", pStruct->fixupFuncName, pStruct->structName);
	}

	if (pStruct->bIsContainer || pStruct->bIsForceConst)
	{
		sprintf(pClassName,"NOCONST(%s)",pStruct->structName);
	}
	else
	{
		strcpy(pClassName,pStruct->structName);
	}

	if (pStruct->bSingleThreadedMemPool)
	{
		fprintf(pFile, "typedef struct MemoryPoolImp *MemoryPool;\nextern MemoryPool memPool%s;\n",
			pStruct->structName);
	}
	else if (pStruct->bThreadSafeMemPool)
	{
		fprintf(pFile, "extern ThreadSafeMemoryPool tsmemPool%s;\n", pStruct->structName);
	}

	fprintf(pFile, "int autoStruct_fixup_%s()\n{\n", pStruct->structName);

	fprintf(pFile, "\tint iSize = sizeof(%s);\n", pStruct->structName);

	fprintf(pFile, "\tstatic char once = 0;\n\tif (once) return 0;\n\tonce = 1;\n");

	char *pFixedUpSourceFileName = GetFileNameWithoutDirectories(pStruct->sourceFileName);
	while (*pFixedUpSourceFileName == '\\' || *pFixedUpSourceFileName == '/')
	{
		pFixedUpSourceFileName++;
	}

	fprintf(pFile, "\tParserSetTableInfo(parse_%s, iSize, \"%s\", %s, \"%s\", 0 %s %s);\n", pStruct->structName, pStruct->structName,
		pStruct->fixupFuncName[0] ? pStruct->fixupFuncName : "NULL", pFixedUpSourceFileName, 
		pStruct->bRuntimeModified ?  "" : "| SETTABLEINFO_ALLOW_CRC_CACHING ",
		pStruct->bSaveOriginalCaseFieldNames ? "| SETTABLEINFO_SAVE_ORIGINAL_CASE_FIELD_NAMES " : "");

	if (pStruct->creationCommentFieldName[0])
	{
		fprintf(pFile, "\tassertmsgf(offsetof(%s, %s) != 0, \"Creation comment field for %s has offset 0. It can't be the first field in the struct\");\n",
			pStruct->structName, pStruct->creationCommentFieldName, pStruct->structName);

		fprintf(pFile, "\tSetCreationCommentOffsetInTPIInfoColumn(parse_%s, offsetof(%s, %s));\n",
			pStruct->structName, pStruct->structName, pStruct->creationCommentFieldName);
		fprintf(pFile, "\n//if compiling fails on the next line, it's because your creation comment field does\\n//not exist, or is not a char*\n");
		fprintf(pFile, "\tif (0) { ((%s*)0x0)->%s = \"\"; };\n\n\n", pStruct->structName, pStruct->creationCommentFieldName);
	}

	if (pStruct->structNameIInheritFrom[0])
	{
		fprintf(pFile, "\tAddEntryToPolyTable(polyTable_%s, \"%s\", parse_%s, iSize);\n",
			pStruct->structNameIInheritFrom, pStruct->structName, pStruct->structName);
	}

	if (pStruct->bSingleThreadedMemPool)
	{
		fprintf(pFile, "\tParserSetTPIUsesSingleThreadedMemPool(parse_%s, &memPool%s);\n",
			pStruct->structName, pStruct->structName);
	}
	else if (pStruct->bThreadSafeMemPool)
	{
		fprintf(pFile, "\tParserSetTPIUsesThreadSafeMemPool(parse_%s, &tsmemPool%s);\n",
			pStruct->structName, pStruct->structName);
	}
	
	if (pStruct->bNoMemTracking)
	{
		fprintf(pFile, "\tParserSetTPINoMemTracking(parse_%s);\n",
			pStruct->structName);
	}

	if (iNumBits)
	{
        DumpBitFieldFixups_align = 1;

		fprintf(pFile, "\t{\n\t\tint iSizeInWords = (sizeof(%s) + 7) / 4;\n\t\t%s *pTemp = alloca(iSizeInWords * 4);\n\t\tmemset(pTemp, 0, iSizeInWords * 4);\n",
			pClassName, pClassName);

		RecurseOverAllFieldsAndFlatEmbeds(pStruct, pStruct, DumpBitFieldFixups, 0, pFile);

		fprintf(pFile, "\t}\n");
	}
	RecurseOverAllFieldsAndFlatEmbeds(pStruct, pStruct, SetupFloatDefaults, 0, pFile);


	fprintf(pFile, "\treturn 0;\n};\n");

	char tempName[256];
	sprintf(tempName, "autoStruct_fixup_%s", pStruct->structName);



	m_pParent->GetAutoRunManager()->AddAutoRunWithIfDefs(tempName, pStruct->sourceFileName, pStruct->pIfDefStack);

	bool bHasLateBinds = false;

	RecurseOverAllFieldsAndFlatEmbeds(pStruct, pStruct, AreThereLateBinds, 0, &bHasLateBinds);

	if (bHasLateBinds)
	{
		fprintf(pFile, "void DoAutoStructLateBind(ParseTable *pTPI, int iColumnIndex, char *pOtherTPIName);\n");
		fprintf(pFile, "void autoStruct_lateFixup_%s(void)\n{\n", pStruct->structName);

		RecurseOverAllFieldsAndFlatEmbeds(pStruct, pStruct, DumpLateBindFixups, 0, pFile);

		fprintf(pFile, "}\n");


		sprintf(tempName, "autoStruct_lateFixup_%s", pStruct->structName);

		m_pParent->GetAutoRunManager()->AddAutoRunSpecial(tempName, pStruct->sourceFileName, false, AUTORUN_ORDER_LATE);

	}

}

static STRUCT_FIELD_DESC *spFlatEmbedRecurseFields[MAX_FLATEMBED_RECURSE_DEPTH];

void StructParser::RecurseOverAllFieldsAndFlatEmbeds(STRUCT_DEF *pParentStruct, STRUCT_DEF *pStruct, FieldRecurseCB *pCB, 
	int iRecurseDepth, void *pUserData)
{
	int i;

	if (iRecurseDepth == 0)
	{
		ResetAllStructFieldIndices();
	}


	for (i=0; i < pStruct->iNumFields; i++)
	{
		pCB(pParentStruct, pStruct->pStructFields[i], iRecurseDepth, spFlatEmbedRecurseFields, pUserData);
		
		if (pStruct->pStructFields[i]->eDataType == DATATYPE_STRUCT
			&& (pStruct->pStructFields[i]->bFlatEmbedded || pStruct->pStructFields[i]->bIAmPolyChildTypeField))
		{
			STATICASSERT(iRecurseDepth < MAX_FLATEMBED_RECURSE_DEPTH, "Exceeded flatembed recurse limit");

			STRUCT_DEF *pOtherStruct = FindNamedStruct(pStruct->pStructFields[i]->typeName);
			FieldAssert(pStruct->pStructFields[i], pOtherStruct != NULL, "Couldn't find flat embedded struct");
			
			spFlatEmbedRecurseFields[iRecurseDepth] = pStruct->pStructFields[i];

			RecurseOverAllFieldsAndFlatEmbeds(pParentStruct, pOtherStruct, pCB, iRecurseDepth + 1, pUserData);
		}
	}
}

void StructParser::AreThereLateBinds(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
	int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData)
{
	if (pField->bFoundLateBind)
	{
		*((bool*)pUserData) = true;
	}
}

void StructParser::DumpLateBindFixups(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
	int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData)
{
		int i;

	if (pField->bFoundLateBind)
	{
		if (pField->structTpiName[0])
		{
			FieldAssert(pField, StringBeginsWith(pField->structTpiName, "parse_", true), "LATEBIND fields with STRUCT() must have tpis named \"parse_x\"");
		}

		fprintf((FILE*)pUserData, "\tDoAutoStructLateBind(parse_%s, %d, \"%s\");\n", pParentStruct->structName, 
			pField->iIndicesInParseTable[pField->iCurIndexCount], pField->structTpiName[0] ? pField->structTpiName + 6 : pField->typeName);

		for (i=0; i < pField->iNumRedundantNames; i++)
		{
			fprintf((FILE*)pUserData, "\tDoAutoStructLateBind(parse_%s, %d, \"%s\");\n", pParentStruct->structName, 
				pField->iIndicesInParseTable[pField->iCurIndexCount] + i + 1, pField->structTpiName[0] ? pField->structTpiName + 6 : pField->typeName);
		}
		
		if (pField->iNumRedundantStructInfos)
		{
			for (i=0; i < pField->iNumRedundantStructInfos; i++)
			{
				FieldAssert(pField, StringBeginsWith(pField->redundantStructs[i].subTable, "parse_", true), 
					"For latebinded redundant structs, parse table name must start with \"parse_\"");
				fprintf((FILE*)pUserData, "\tDoAutoStructLateBind(parse_%s, %d, \"%s\");\n",
					pParentStruct->structName, pField->iIndicesInParseTable[pField->iCurIndexCount] + pField->iNumRedundantNames + i + 1, 
					pField->redundantStructs[i].subTable + 6);
			}
		}

		pField->iCurIndexCount++;


	}
}

void StructParser::AreThereBitFields(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
	int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData)
{
	if (pField->eDataType == DATATYPE_BIT)
	{
		(*((int*)pUserData))++;
	}
}

void StructParser::DumpBitFieldFixups(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
	int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData)
{
	if (pField->eDataType == DATATYPE_BIT)
	{
		char curFieldName[1024] = "";
		int i;

		for (i=0; i < iRecurseDepth ;i++)
		{
			sprintf(curFieldName + strlen(curFieldName), "%s.", ppRecurse_fields[i]->baseStructFieldName);
		}

		sprintf(curFieldName + strlen(curFieldName), "%s", pField->baseStructFieldName);

        if(DumpBitFieldFixups_align) {
            DumpBitFieldFixups_align = 0;

            fprintf((FILE*)pUserData, "\t\tpTemp->%s = ~0;\n\t\tFindAutoStructBitField((char*)pTemp, iSizeInWords, &parse_%s[%d]);\n\t\tpTemp->%s = 0;\n",
			    curFieldName,
			    pParentStruct->structName, pField->iIndicesInParseTable[pField->iCurIndexCount],
			    curFieldName);

        } else {
            fprintf((FILE*)pUserData, "\t\tpTemp->%s = ~0;\n\t\tFindAutoStructBitField((char*)pTemp, iSizeInWords, &parse_%s[%d]);\n\t\tpTemp->%s = 0;\n",
			    curFieldName,
			    pParentStruct->structName, pField->iIndicesInParseTable[pField->iCurIndexCount],
			    curFieldName);
        }

		for (i=0; i < pField->iNumRedundantNames; i++)
		{
			fprintf((FILE*)pUserData, "\t\tpTemp->%s = ~0;\n\t\tFindAutoStructBitField((char*)pTemp, iSizeInWords, &parse_%s[%d]);\n\t\tpTemp->%s = 0;\n",
					curFieldName,
					pParentStruct->structName, pField->iIndicesInParseTable[pField->iCurIndexCount] + i + 1,
					curFieldName);
		}

		pField->iCurIndexCount++;

	}
    else
    {
        DumpBitFieldFixups_align = 1;
    }
}

void StructParser::SetupFloatDefaults(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
	int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData)
{
	if (pField->eDataType == DATATYPE_FLOAT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT && (strcmp(pField->defaultString, "0") != 0))
	{
		int i;
		fprintf((FILE*)pUserData, "\tparse_%s[%d].param = GET_INTPTR_FROM_FLOAT((float)%s);\n", pParentStruct->structName, pField->iIndicesInParseTable[pField->iCurIndexCount], pField->defaultString);

		for (i=0 ; i < pField->iNumRedundantNames; i++)
		{
			fprintf((FILE*)pUserData, "\tparse_%s[%d].param = GET_INTPTR_FROM_FLOAT((float)%s);\n", pParentStruct->structName, pField->iIndicesInParseTable[pField->iCurIndexCount] + i + 1, pField->defaultString);
		}

		pField->iCurIndexCount++;
	}
}


void StructParser::DumpExterns(FILE *pFile, STRUCT_DEF *pStruct)
{
	int i;
	int iRecurseDepth = 0;
	//generate any necessary extern ParseTable declarations
	for (i=0; i < pStruct->iNumFields; i++)
	{
		STRUCT_FIELD_DESC *pField = pStruct->pStructFields[i];
		switch (pField->eDataType)
		{
		case DATATYPE_STRUCT_POLY:
			fprintf(pFile, "extern ParseTable polyTable_%s[];\n", 
				pField->typeName);
			break;
	/*	case DATATYPE_INT:
			if (pField->subTableName[0])
			{
				fprintf(pFile, "extern staticDefineInt %s[];\n", pField->subTableName);
			}
			else
			{
				fprintf(pFile, "extern staticDefineInt %sEnum[];\n", pField->typeName);
			}
			break;*/

		case DATATYPE_STRUCT:
			if (pField->bFlatEmbedded)
			{
				iRecurseDepth++;

				FieldAssert(pField, iRecurseDepth < 20, "Presumed infinite EMBEDDED_FLAT/POLYCHILDTYPE recursion found");

				STRUCT_DEF *pOtherStruct = FindNamedStruct(pField->typeName);

				FieldAssert(pField, pOtherStruct != NULL, "Couldn't find AUTO_STRUCT def for EMBEDDED_FLAT struct");

				DumpExterns(pFile,pOtherStruct);

				iRecurseDepth--;
			}
			else
			{	
				if (!pField->bFoundLateBind)
				{
					char *pTPIName = GetFieldTpiName(pField, false);
					if (strcmp(pTPIName, "NULL") != 0)
					{
						fprintf(pFile, "extern ParseTable %s[];\n", pTPIName);
						fprintf(pFile, "#define TYPE_%s %s\n", pTPIName, pTPIName + strlen(STRUCTPARSER_PREFIX));
					}
				}
			}
			break;
		}
	}
}

void StructParser::DumpStruct(FILE *pFile, STRUCT_DEF *pStruct)
{
	int iCount = 0;
		int i;

	ResetAllStructFieldIndices();

	CalcLongestUserFieldName(pStruct);

	if (pFile)
	{



		fprintf(pFile, "//autogenerated" "nocheckin\n");

		DumpExterns(pFile,pStruct);

		fprintf(pFile, "//Structparser.exe autogenerated ParseTable for struct %s\n", pStruct->structName);
		fprintf(pFile, "#define TYPE_%s%s %s\n", STRUCTPARSER_PREFIX, pStruct->structName, pStruct->structName);
		fprintf(pFile, "ParseTable %s%s[] =\n{\n", STRUCTPARSER_PREFIX, pStruct->structName);

		iCount += PrintStructTableInfoColumn(pFile, pStruct);

		iCount += PrintStructStart(pFile, pStruct);

		for (i=0; i < pStruct->iNumFields; i++)
		{
			FieldAssert(pStruct->pStructFields[i], pStruct->pStructFields[i]->iCurIndexCount < MAX_FLATEMBED_RECURSE_DEPTH - 1, "Too much flat embed recursion or duplication");
			pStruct->pStructFields[i]->iIndicesInParseTable[pStruct->pStructFields[i]->iCurIndexCount++] = iCount;
			iCount += DumpField(pFile, pStruct->pStructFields[i], pStruct->structName, pStruct->iLongestUserFieldNameLength, GetFieldSpecificPrefixString(pStruct, i), "");
		}

		for (i=0; i < pStruct->iNumIgnores; i++)
		{
			PrintIgnore(pFile, pStruct->ignores[i], pStruct->iLongestUserFieldNameLength, pStruct->bIgnoresAreStructParam[i], false);
		}

		for (i=0; i < pStruct->iNumIgnoreStructs; i++)
		{
			PrintIgnore(pFile, pStruct->ignoreStructs[i], pStruct->iLongestUserFieldNameLength, false, true);
		}

		iCount += PrintStructEnd(pFile, pStruct);

		VerifyStructWithCountOfFieldsWritten(pStruct, iCount);


		fprintf(pFile, "\t{ \"\", 0, 0 }\n};\n");

		for (i=0; i < pStruct->iNumFields; i++)
		{
			DumpIndexDefines(pFile, pStruct, pStruct->pStructFields[i], 0);

		}

		if (pStruct->bIAmAPolymorphicParent)
		{
			DumpPolyTable(pFile, pStruct);
		}

	}
}

void StructParser::VerifyStructWithCountOfFieldsWritten(STRUCT_DEF *pStruct, int iCount)
{
	RecurseOverAllFieldsAndFlatEmbeds(pStruct, pStruct, CheckUsedFieldSize, 0, &iCount);
}



void StructParser::CheckUsedFieldSize(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
	int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData)
{

	int *iCount = (int*)pUserData;

	if (pField->bFoundUsedField)
	{
		int iArraySize = atoi(pField->arraySizeString);

		if (iArraySize) //if the arraysize is not a literal int, then overflows will be caught at
						//runtime
		{		
			if (*iCount > iArraySize * 32)
			{
				char errorString[1024];
				
				sprintf(errorString, "USEDFIELD is too small... %d fields in struct %s require at least %d words to store",
					*iCount, pParentStruct->structName, (*iCount + 31)/32);


				FieldAssert(pField, 0, errorString);
			}
		}
	}
	
}



void StructParser::DumpPolyTable(FILE *pFile, STRUCT_DEF *pStruct)
{
	int i;

	fprintf(pFile, "//filled in by autogenerated fixup-time calls to AddEntryToPolyTable()\n");
	fprintf(pFile, "ParseTable polyTable_%s[] = \n{\n", pStruct->structName);

	for (i=0; i < m_iNumStructs; i++)
	{
		if (strcmp(m_pStructs[i]->structNameIInheritFrom, pStruct->structName) == 0)
		{
			fprintf(pFile, "\t{ \"\", 0, 0 },\n");
		}
	}

	for (i=0; i < pStruct->iParentTypeExtraCount; i++)
	{
		fprintf(pFile, "\t{ \"\", 0, 0 },\n");
	}

	fprintf(pFile, "\t{ \"\", 0, 1 }\n};\n");


}


//TODO: do the proper recurseThroughFlatEmbeds thing for this so that it works right with index defines
//of two flat embeds of the same thing embedded in one parent
void StructParser::DumpIndexDefines(FILE *pFile, STRUCT_DEF *pStruct, STRUCT_FIELD_DESC *pField, int iOffset)
{
	if (pField->bFoundRequestIndexDefine)
	{
		char tempString[4096];

		sprintf(tempString, "PARSE_%s_%s_INDEX", pStruct->structName, pField->userFieldName);
		MakeStringAllAlphaNumAndUppercase(tempString);
		fprintf(pFile, "#define %s %d\n", tempString, pField->iIndicesInParseTable[0] + iOffset);
	}
	
	if (pField->eReferenceType == REFERENCETYPE_DIRECT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->bFlatEmbedded)
	{
		STRUCT_DEF *pSubStruct = FindNamedStruct(pField->typeName);

		if(pSubStruct)
		{
			int i;

			for (i=0; i < pSubStruct->iNumFields; i++)
			{
				DumpIndexDefines(pFile, pStruct, pSubStruct->pStructFields[i], pField->iIndicesInParseTable[0] + iOffset);
			}
		}
	}
}

void StructParser::DumpStructPrototype(FILE *pFile, STRUCT_DEF *pStruct)
{
	if (pFile)
	{
		fprintf(pFile, "\nextern ParseTable %s%s[];\n", STRUCTPARSER_PREFIX, pStruct->structName);
		fprintf(pFile, "#define TYPE_%s%s %s\n", STRUCTPARSER_PREFIX, pStruct->structName, pStruct->structName);

		int i;

		for (i=0; i < pStruct->iNumFields; i++)
		{
			DumpIndexDefines(pFile, pStruct, pStruct->pStructFields[i], 0);

		}

		if (pStruct->bIAmAPolymorphicParent)
		{
			fprintf(pFile, "\nextern ParseTable polyTable_%s[];\n", pStruct->structName);
		}

	}
}


#define FIRST bFirst ? ", " : "|"

void StructParser::DumpFieldFormatting(FILE *pFile, STRUCT_FIELD_DESC *pField)
{
	bool bFirst = true;

	//RGB fields automatically get just TOK_FORMAT_COLOR for formatting
	if (pField->eDataType == DATATYPE_RG || pField->eDataType == DATATYPE_RGB || pField->eDataType == DATATYPE_RGBA)
	{
		bFirst = false;
		fprintf(pFile, " , TOK_FORMAT_COLOR");
	}
	else
	{

		if (pField->eFormatType != FORMAT_NONE)
		{
			fprintf(pFile, " %s TOK_%s", FIRST, sFieldCommandStringReservedWords[pField->eFormatType + FIRST_FORMAT_RW - RW_COUNT - 1]);
			bFirst = false;
		}

		if (pField->lvWidth)
		{
			fprintf(pFile, " %s TOK_FORMAT_LVWIDTH(%d)", FIRST, pField->lvWidth);
			bFirst = false;
		}

		int i;
	
		if (pField->bFoundFlagsToken)
		{
			fprintf(pFile, " %s TOK_FORMAT_FLAGS", FIRST);
			bFirst = false;
		}

		for (i=0; i < FORMAT_FLAG_COUNT; i++)
		{
			if (pField->bFormatFlags[i])
			{
				fprintf(pFile, " %s TOK_%s", FIRST, sFieldCommandStringReservedWords[FIRST_FORMAT_FLAG_RW - RW_COUNT + i]);
				bFirst = false;
			}
		}
	}

	if (pField->pFormatString)
	{
		if (bFirst)
		{
			fprintf(pFile, ", 0 ");
		}

		fprintf(pFile, ", \"%s\"", pField->pFormatString);
	}
}

char *StructParser::GetIntPrefixString(STRUCT_FIELD_DESC *pField)
{
	static char workString[256];

	if (pField->iMinBits)
	{
		sprintf(workString, "TOK_MINBITS(%d) | ", pField->iMinBits);
	}
	else if (pField->iPrecision)
	{
		sprintf(workString, "TOK_PRECISION(%d) | ", pField->iPrecision);
	}
	else
	{
		workString[0] = 0;
	}

	return workString;
}

char *StructParser::GetFloatPrefixString(STRUCT_FIELD_DESC *pField)
{
	static char workString[256];

	if (pField->iFloatAccuracy)
	{
		sprintf(workString, "TOK_FLOAT_ROUNDING(%s) | ", sFieldCommandStringReservedWords[pField->iFloatAccuracy + FIRST_FLOAT_ACCURACY_RW - RW_COUNT - 1]);
	}
	else if (pField->iPrecision)
	{
		sprintf(workString, "TOK_PRECISION(%d) | ", pField->iPrecision);
	}
	else
	{
		workString[0] = 0;
	}

	return workString;
}

char *StructParser::GetFieldTpiName(STRUCT_FIELD_DESC *pField, bool bIgnoreLateBind)
{
	static char workString[256];

	if (pField->bFoundLateBind && !bIgnoreLateBind)
	{
		return "NULL";
	}

	if (pField->pCurOverrideTPIName)
	{
		strcpy(workString, pField->pCurOverrideTPIName);
	}
	else if (pField->structTpiName[0])
	{
		strcpy(workString, pField->structTpiName);
	}
	else
	{
		sprintf(workString, "%s%s", STRUCTPARSER_PREFIX, pField->typeName);
	}

	return workString;
}

char *StructParser::GetFieldSpecificPrefixString(STRUCT_DEF *pStruct, int iFieldNum)
{
	static char sWorkString[MAX_NAME_LENGTH];

	int i;

	for (i=0; i < pStruct->iNumUnions; i++)
	{
		if (pStruct->unions[i].iFirstFieldNum <= iFieldNum && pStruct->unions[i].iLastFieldNum >= iFieldNum && pStruct->unions[i].name[0])
		{
			sprintf(sWorkString, "%s.", pStruct->unions[i].name);
			return sWorkString;
		}
	}

	sWorkString[0] = 0;
	return sWorkString;
}
void StructParser::AssertFieldHasNoDefault(STRUCT_FIELD_DESC *pField, char *pErrorString)
{
	FieldAssert(pField, pField->defaultString[0] == 0 || pField->defaultString[0] == '0' && pField->defaultString[1] == 0 || strcmp(pField->defaultString, "NULL") == 0, pErrorString);
}

int StructParser::DumpFieldDirectEmbedded(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName, int iLongestFieldName, int iIndexInMultiLineField, char *pUserFieldNamePrefix)
{
	int iCount = 0;
	static int iRecurseDepth = 0;

	switch (pField->eDataType)
	{
	case DATATYPE_INT:
		fprintf(pFile, "%sTOK_AUTOINT(%s, %s, %s), %s ", GetIntPrefixString(pField), pStructName, pField->curStructFieldName,
			pField->defaultString, pField->subTableName[0] ? pField->subTableName : "NULL");
		iCount++;
		break;

	case DATATYPE_FLOAT:
		fprintf(pFile, "%sTOK_F32(%s, %s, %s), NULL ", GetFloatPrefixString(pField), pStructName, pField->curStructFieldName,
			pField->defaultString);
		iCount++;
		break;

	case DATATYPE_STRUCT:
		AssertFieldHasNoDefault(pField, "Default values not allowed for embedded structs");
		if (pField->bFlatEmbedded)
		{
			iRecurseDepth++;

			FieldAssert(pField, iRecurseDepth < 20, "Presumed infinite EMBEDDED_FLAT/POLYCHILDTYPE recursion found");

			char prefixString[MAX_NAME_LENGTH];

			STRUCT_DEF *pOtherStruct = FindNamedStruct(pField->typeName);

			FieldAssert(pField, pOtherStruct != NULL, "Couldn't find AUTO_STRUCT def for EMBEDDED_FLAT struct");
			char tabs[MAX_TABS + 1];	

			MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pField->baseStructFieldName)) - NUMTABS(iLongestFieldName) + 1, MAX_TABS, '\t');

			fprintf(pFile, "\t{ \"%s\", %sTOK_IGNORE | TOK_FLATEMBED },\n", pField->userFieldName[0] ? pField->userFieldName : pField->baseStructFieldName, tabs);

			iCount++;

			sprintf(prefixString, "%s.", pField->curStructFieldName);

			int i;

			for (i=0; i < pOtherStruct->iNumFields; i++)
			{
				char finalPrefixString[MAX_NAME_LENGTH];
				char finalUserFieldNamePrefixString[MAX_NAME_LENGTH];

				sprintf(finalUserFieldNamePrefixString, "%s%s", pUserFieldNamePrefix, pField->flatEmbeddingPrefix);
				sprintf(finalPrefixString, "%s%s", prefixString, GetFieldSpecificPrefixString(pOtherStruct, i));

				FieldAssert(pOtherStruct->pStructFields[i], pOtherStruct->pStructFields[i]->iCurIndexCount < MAX_FLATEMBED_RECURSE_DEPTH - 1, "Too much flat embed recursion");
				pOtherStruct->pStructFields[i]->iIndicesInParseTable[pOtherStruct->pStructFields[i]->iCurIndexCount++] = pField->iIndicesInParseTable[pField->iCurIndexCount-1] + iCount;
				iCount += DumpField(pFile, pOtherStruct->pStructFields[i], pStructName, iLongestFieldName, finalPrefixString, finalUserFieldNamePrefixString);
			}

			iRecurseDepth--;
		}
		else if (pField->bIAmPolyChildTypeField)
		{
			iRecurseDepth++;

			FieldAssert(pField, iRecurseDepth < 20, "Presumed infinite EMBEDDED_FLAT/POLYCHILDTYPE recursion found");

			char prefixString[MAX_NAME_LENGTH];

			STRUCT_DEF *pOtherStruct = FindNamedStruct(pField->typeName);

			FieldAssert(pField, pOtherStruct != NULL && pOtherStruct->bIAmAPolymorphicParent, 
				"Couldn't find parent struct with POLYPARENTTYPE field");

			sprintf(prefixString, "%s.", pField->curStructFieldName);

			int i;

			for (i=0; i < pOtherStruct->iNumFields; i++)
			{
				char finalPrefixString[MAX_NAME_LENGTH];
				sprintf(finalPrefixString, "%s%s", prefixString, GetFieldSpecificPrefixString(pOtherStruct, i));

				FieldAssert(pOtherStruct->pStructFields[i], pOtherStruct->pStructFields[i]->iCurIndexCount < MAX_FLATEMBED_RECURSE_DEPTH - 1, "Too much flat embed recursion");
				pOtherStruct->pStructFields[i]->iIndicesInParseTable[pOtherStruct->pStructFields[i]->iCurIndexCount++] = pField->iIndicesInParseTable[pField->iCurIndexCount - 1] + iCount;
				if (pOtherStruct->pStructFields[i]->bIAmPolyParentTypeField)
				{
					FieldAssert(pOtherStruct->pStructFields[i], strcmp(pOtherStruct->pStructFields[i]->defaultString, "0") == 0, "POLYPARENTFIELD fields can not have default values");

					strcpy(pOtherStruct->pStructFields[i]->defaultString, pField->myPolymorphicType);
				}
				
				iCount += DumpField(pFile, pOtherStruct->pStructFields[i], pStructName, iLongestFieldName, finalPrefixString, pUserFieldNamePrefix);

				if (pOtherStruct->pStructFields[i]->bIAmPolyParentTypeField)
				{
					strcpy(pOtherStruct->pStructFields[i]->defaultString, "0");
				}
			}

			iRecurseDepth--;

		}
		else
		{
			fprintf(pFile, "TOK_EMBEDDEDSTRUCT(%s, %s, %s)", pStructName, pField->curStructFieldName, GetFieldTpiName(pField, false));
			iCount++;
		}
		break;

	case DATATYPE_STRUCT_POLY:
		AssertFieldHasNoDefault(pField, "Default values not allowed for embedded polys");
		fprintf(pFile, "TOK_EMBEDDEDPOLYMORPH(%s, %s, polyTable_%s)", pStructName, pField->curStructFieldName, pField->typeName);
		iCount++;
		break;


	case DATATYPE_TIMESTAMP:
		AssertFieldHasNoDefault(pField, "Default values not allowed for timestamps");
		fprintf(pFile, "%sTOK_TIMESTAMP(%s, %s), NULL ", GetIntPrefixString(pField), pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_LINENUM:
		AssertFieldHasNoDefault(pField, "Default values not allowed for linenums");
		fprintf(pFile, "%sTOK_LINENUM(%s, %s), NULL ", GetIntPrefixString(pField), pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_BOOLFLAG:
		fprintf(pFile, "%sTOK_BOOLFLAG(%s, %s, %s), %s ", GetIntPrefixString(pField), pStructName, pField->curStructFieldName,
			pField->defaultString, pField->subTableName[0] ? pField->subTableName : "NULL");
		iCount++;
		break;

	case DATATYPE_RAW:
		if (pField->rawSizeString[0])
		{
			fprintf(pFile, "TOK_RAW_S(%s, %s, %s), NULL ", pStructName, pField->curStructFieldName, pField->rawSizeString);
			iCount++;
		}
		else
		{
			fprintf(pFile, "TOK_RAW(%s, %s), NULL ", pStructName, pField->curStructFieldName);
			iCount++;
		}
		break;

	case DATATYPE_VEC4:
		AssertFieldHasNoDefault(pField, "Default values not allowed for vec4s");
		fprintf(pFile, "TOK_VEC4(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_VEC3:
		AssertFieldHasNoDefault(pField, "Default values not allowed for vec3s");
		fprintf(pFile, "TOK_VEC3(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_VEC2:
		AssertFieldHasNoDefault(pField, "Default values not allowed for vec2s");
		fprintf(pFile, "TOK_VEC2(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_IVEC4:
		AssertFieldHasNoDefault(pField, "Default values not allowed for ivec4s");
		fprintf(pFile, "TOK_IVEC4(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_IVEC3:
		AssertFieldHasNoDefault(pField, "Default values not allowed for ivec3s");
		fprintf(pFile, "TOK_IVEC3(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_IVEC2:
		AssertFieldHasNoDefault(pField, "Default values not allowed for ivec2s");
		fprintf(pFile, "TOK_IVEC2(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_RGB:
		AssertFieldHasNoDefault(pField, "Default values not allowed for rgbx");
		fprintf(pFile, "TOK_RGB(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_RGBA:
		AssertFieldHasNoDefault(pField, "Default values not allowed for rgbas");
		fprintf(pFile, "TOK_RGBA(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_RG:
		AssertFieldHasNoDefault(pField, "Default values not allowed for rgs");
		fprintf(pFile, "TOK_RG(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_MAT3:
		AssertFieldHasNoDefault(pField, "Default values not allowed for mat3s");
		fprintf(pFile, "TOK_MAT3PYR(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_MAT4:
		AssertFieldHasNoDefault(pField, "Default values not allowed for mat4s");
		if (iIndexInMultiLineField == 0)
		{
			fprintf(pFile, "TOK_MAT4PYR_ROT(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		}
		else
		{
			fprintf(pFile, "TOK_MAT4PYR_POS(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		}
		iCount++;
		break;

	case DATATYPE_MAT3_ASMATRIX:
		AssertFieldHasNoDefault(pField, "Default values not allowed for mat3s");
		if (iIndexInMultiLineField == 0)
		{
			fprintf(pFile, "TOK_FIXED_ARRAY | TOK_F32_X, offsetof(%s, %s), 9, NULL ", pStructName, pField->curStructFieldName);
		}
		else
		{
			fprintf(pFile, "TOK_REDUNDANTNAME | TOK_MAT3PYR(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		}
		iCount++;
		break;

	case DATATYPE_MAT4_ASMATRIX:
		AssertFieldHasNoDefault(pField, "Default values not allowed for mat4s");
		if (iIndexInMultiLineField == 0)
		{
			fprintf(pFile, "TOK_FIXED_ARRAY | TOK_F32_X, offsetof(%s, %s), 12, NULL ", pStructName, pField->curStructFieldName);
		}
		else if (iIndexInMultiLineField == 1)
		{
			fprintf(pFile, "TOK_REDUNDANTNAME | TOK_MAT4PYR_ROT(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		}
		else
		{
			fprintf(pFile, "TOK_REDUNDANTNAME | TOK_MAT4PYR_POS(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		}
		iCount++;
		break;



	case DATATYPE_MAT44:
		AssertFieldHasNoDefault(pField, "Default values not allowed for mat44s");
	
		fprintf(pFile, "TOK_FIXED_ARRAY | TOK_F32_X, offsetof(%s, %s),  16, NULL ", pStructName, pField->curStructFieldName);

		iCount++;
		break;


	case DATATYPE_QUAT:
		AssertFieldHasNoDefault(pField, "Default values not allowed for quats");
		fprintf(pFile, "TOK_QUATPYR(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_STASHTABLE:
		AssertFieldHasNoDefault(pField, "Default values not allowed for stashtables");
		fprintf(pFile, "TOK_STASHTABLE(%s, %s), NULL", pStructName, pField->curStructFieldName);
		iCount++;
		break;

	case DATATYPE_REFERENCE:
		fprintf(pFile, "TOK_REFERENCE(%s, %s, %s, \"%s\") ", pStructName, pField->curStructFieldName,
			pField->defaultString, pField->refDictionaryName);
		iCount++;
		break;

	case DATATYPE_BIT:
		if (pField->defaultString[0])
		{
			fprintf(pFile, "TOK_SPECIAL_DEFAULT | ");
		}
		fprintf(pFile, "TOK_BIT, 0, 8, %s", pField->subTableName[0] ? pField->subTableName : "NULL");
		iCount++;
		break;

	case DATATYPE_MULTIVAL:
		fprintf(pFile, "TOK_MULTIVAL(%s, %s), NULL", pStructName, pField->curStructFieldName);
		iCount++;
		break;


	default:
		FieldAssert(pField, 0, "Unknown or unsupported data type (Direct Embedded)");
		break;
	}

	DumpFieldFormatting(pFile, pField);

	assert(iCount > 0);

	return iCount;
}

int StructParser::DumpFieldDirectArray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName)
{
	if (pField->eDataType != DATATYPE_CHAR)
	{
		AssertFieldHasNoDefault(pField, "Default values not allowed for arrays or fixed-size strings");
	}
	switch (pField->eDataType)
	{
	case DATATYPE_INT:
		fprintf(pFile, "%s%sTOK_FIXED_ARRAY | TOK_AUTOINTARRAY(%s, %s), %s ", GetIntPrefixString(pField), 
			pField->bFoundUsedField ? "TOK_USEDFIELD | " : "",			
			pStructName, pField->curStructFieldName,
			pField->subTableName[0] ? pField->subTableName : "NULL");
		break;

	case DATATYPE_FLOAT:
		fprintf(pFile, "%sTOK_FIXED_ARRAY | TOK_F32_X, offsetof(%s, %s), %s, NULL ", GetFloatPrefixString(pField), pStructName, pField->curStructFieldName, pField->arraySizeString);
		break;

	case DATATYPE_CHAR:
		if (pField->defaultString[0])
		{
			fprintf(pFile, "TOK_SPECIAL_DEFAULT | ");
		}

		if (pField->refDictionaryName[0])
			fprintf(pFile, "TOK_FIXEDSTR(%s, %s), \"%s\" ", pStructName, pField->curStructFieldName, pField->refDictionaryName);
		else
			fprintf(pFile, "TOK_FIXEDSTR(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		break;

	case DATATYPE_MULTIVAL:
		fprintf(pFile, "TOK_MULTIARRAY(%s, %s), NULL", pStructName, pField->curStructFieldName);
		break;

	default:
		FieldAssert(pField, 0, "Unknown or unsupported data type (Direct Array)");
		break;
	}

	DumpFieldFormatting(pFile, pField);

	return 1;

}
int StructParser::DumpFieldDirectEarray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName)
{	
	AssertFieldHasNoDefault(pField, "Default values not allowed for earrays");
	
//	switch (pField->eDataType)
	{
//	default:
		FieldAssert(pField, 0, "Unknown or unsupported data type (Direct EArray)");
	}

	DumpFieldFormatting(pFile, pField);

	return 1;
}


int StructParser::DumpFieldPointerEmbedded(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName)
{
	switch (pField->eDataType)
	{
	case DATATYPE_INT:
		AssertFieldHasNoDefault(pField, "Default values not allowed for int arrays");
		fprintf(pFile, "%sTOK_INTARRAY(%s, %s),  %s", GetIntPrefixString(pField), pStructName, pField->curStructFieldName, 
			pField->subTableName[0] ? pField->subTableName : "NULL");
		break;

	case DATATYPE_FLOAT:
		AssertFieldHasNoDefault(pField, "Default values not allowed for float arrays");
		fprintf(pFile, "%sTOK_F32ARRAY(%s, %s), NULL ", GetFloatPrefixString(pField), pStructName, pField->curStructFieldName);
		break;


 	case DATATYPE_CHAR:
		if (pField->refDictionaryName[0])
			fprintf(pFile, "TOK_STRING(%s, %s, %s), \"%s\" ", pStructName, pField->curStructFieldName, pField->defaultString, pField->refDictionaryName);
		else
			fprintf(pFile, "TOK_STRING(%s, %s, %s), NULL ", pStructName, pField->curStructFieldName, pField->defaultString);
		break;

	case DATATYPE_STRUCT:
		AssertFieldHasNoDefault(pField, "Default values not allowed for structs");
		if (pField->bFoundBlockEArray)
		{
			if (pField->bFoundLateBind)
			{
				fprintf(pFile, "TOK_LATEBINDSTRUCTBLOCKEARRAY(%s, %s) ", pStructName, pField->curStructFieldName);
			}
			else
			{
				fprintf(pFile, "TOK_STRUCTBLOCKEARRAY(%s, %s, %s) ", pStructName, pField->curStructFieldName,
					GetFieldTpiName(pField, false));
			}
		}
		else
		{
			if (pField->bFoundLateBind)
			{
				fprintf(pFile, "TOK_OPTIONALLATEBINDSTRUCT(%s, %s) ", pStructName, pField->curStructFieldName);
			}
			else
			{
				fprintf(pFile, "TOK_OPTIONALSTRUCT(%s, %s, %s) ", pStructName, pField->curStructFieldName,
					GetFieldTpiName(pField, false));
			}
		}
		break;


	case DATATYPE_STRUCT_POLY:
		AssertFieldHasNoDefault(pField, "Default values not allowed for polymorphs");
		fprintf(pFile, "TOK_OPTIONALPOLYMORPH(%s, %s, polyTable_%s) ", pStructName, pField->curStructFieldName,
			pField->typeName);
		break;

	case DATATYPE_FILENAME:
		AssertFieldHasNoDefault(pField, "Default values not allowed for TOK_FILENAME");
		fprintf(pFile, "TOK_FILENAME(%s, %s, NULL), NULL", pStructName, pField->curStructFieldName);
		break;
	case DATATYPE_CURRENTFILE:
		AssertFieldHasNoDefault(pField, "Default values not allowed for TOK_CURRENTFILE");
		fprintf(pFile, "TOK_POOL_STRING | TOK_CURRENTFILE(%s, %s), NULL", pStructName, pField->curStructFieldName);
		break;

	case DATATYPE_POINTER:
		AssertFieldHasNoDefault(pField, "Default values not allowed for TOK_POINTER");
		fprintf(pFile, "TOK_POINTER(%s, %s, %s), NULL", pStructName, pField->curStructFieldName, pField->pointerSizeString);
		break;

	case DATATYPE_MULTIVAL:
		AssertFieldHasNoDefault(pField, "Default values not allowed for TOK_MULTIVAL");
		fprintf(pFile, "TOK_MULTIBLOCKARRAY(%s, %s), NULL", pStructName, pField->curStructFieldName);
		break;


	default:
		FieldAssert(pField, 0, "Unknown or unsupported data type (Pointer Embedded)");
		break;


	}

	DumpFieldFormatting(pFile, pField);

	return 1;
}
int StructParser::DumpFieldPointerArray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName)
{
		FieldAssert(pField, 0, "Unknown or unsupported data type (Pointer Array)");

		return 1;

}
int StructParser::DumpFieldPointerEarray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName)
{
		AssertFieldHasNoDefault(pField, "Default values not allowed for earrays");
	switch (pField->eDataType)
	{


	case DATATYPE_STRUCT:
		if (pField->bFoundLateBind)
		{
			fprintf(pFile, "TOK_LATEBINDSTRUCT(%s, %s) ", pStructName, pField->curStructFieldName);
		}
		else
		{
			fprintf(pFile, "TOK_STRUCT(%s, %s, %s) ", pStructName, pField->curStructFieldName, GetFieldTpiName(pField, false));
		}
		break;

	case DATATYPE_STRUCT_POLY:
		fprintf(pFile, "TOK_POLYMORPH(%s, %s, polyTable_%s) ", pStructName, pField->curStructFieldName, pField->typeName);
		break;

	case DATATYPE_TOKENIZERPARAMS:
		fprintf(pFile, "TOK_UNPARSED(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		break;

	case DATATYPE_TOKENIZERFUNCTIONCALL:
		fprintf(pFile, "TOK_FUNCTIONCALL(%s, %s, %s), NULL ", pStructName, pField->curStructFieldName, pField->defaultString);
		break;

	case DATATYPE_CHAR:
		if (pField->refDictionaryName[0])
			fprintf(pFile, "TOK_STRINGARRAY(%s, %s), \"%s\" ", pStructName, pField->curStructFieldName, pField->refDictionaryName);
		else
			fprintf(pFile, "TOK_STRINGARRAY(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		break;

	case DATATYPE_FILENAME:
		fprintf(pFile, "TOK_FILENAMEARRAY(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		break;

	case DATATYPE_MULTIVAL:
		fprintf(pFile, "TOK_MULTIEARRAY(%s, %s), NULL ", pStructName, pField->curStructFieldName);
		break;

	default:
		FieldAssert(pField, 0, "Unknown or unsupported data type (Pointer Earray)");
		break;
	}


	DumpFieldFormatting(pFile, pField);

	return 1;
}

//structFieldNamePrefix is the prefix of the name of the field in the C struct, ie "foo[4].". 
//
//UserFieldNamePrefix is a prefix to prepend to the name field that shows up in the TPI
int StructParser::DumpField(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName, int iLongestUserFieldNameLength, char *pStructFieldNamePrefix, char *pUserFieldNamePrefix)
{
	int iCount = 0;
	char curUserFieldName[MAX_NAME_LENGTH];

	STRUCT_COMMAND *pCommand = pField->pFirstBeforeCommand;
	while (pCommand)
	{
		DumpCommand(pFile, pField, pCommand, iLongestUserFieldNameLength);
		iCount++;
		pCommand = pCommand->pNext;
	}


//for special things like Mat4 which end up taking up more than one ParseTable column
	int iNumLines = GetNumLinesFieldTakesUp(pField);

	if (iNumLines > 1)
	{
		int i;

		for (i=0; i < iNumLines; i++)
		{
			char *pPrefix = GetMultiLineFieldNamePrefix(pField, i);
			sprintf(curUserFieldName, "%s%s%s%s",
				pUserFieldNamePrefix, GetMultiLineFieldNamePrefix(pField, i), pPrefix[0] ? "_" : "", pField->userFieldName);

			iCount += DumpFieldSpecifyUserFieldName(pFile, pField, pStructName, curUserFieldName, 
				false, iLongestUserFieldNameLength, i, pUserFieldNamePrefix);
		}

		
		return iCount;

	}

	if (pField->iNumIndexes)
	{
		FieldAssert(pField, pField->iNumRedundantNames == 0, "Can't have redundant names and INDEX in the same field");

		int i;

		for (i=0; i < pField->iNumIndexes; i++)
		{
			sprintf(curUserFieldName, "%s%s", pUserFieldNamePrefix, pField->pIndexes[i].nameString);

			sprintf(pField->curStructFieldName, "%s%s[%s]", pStructFieldNamePrefix, pField->baseStructFieldName, pField->pIndexes[i].indexString);

			iCount += DumpFieldSpecifyUserFieldName(pFile, pField, pStructName, curUserFieldName, pField->bFoundRedundantToken, 
				iLongestUserFieldNameLength, 0, pUserFieldNamePrefix);
		}


	
		return iCount;
	}
	

	sprintf(pField->curStructFieldName, "%s%s", pStructFieldNamePrefix, pField->baseStructFieldName);

	sprintf(curUserFieldName, "%s%s", pUserFieldNamePrefix, pField->userFieldName);
	iCount += DumpFieldSpecifyUserFieldName(pFile, pField, pStructName, curUserFieldName, pField->bFoundRedundantToken, iLongestUserFieldNameLength, 0, pUserFieldNamePrefix);

	int i;

	for (i=0; i < pField->iNumRedundantNames; i++)
	{
		sprintf(curUserFieldName, "%s%s", pUserFieldNamePrefix, pField->redundantNames[i]);
		iCount += DumpFieldSpecifyUserFieldName(pFile, pField, pStructName, curUserFieldName, true, iLongestUserFieldNameLength, 0, pUserFieldNamePrefix);
	}

	if (pField->iNumRedundantStructInfos)
	{
		FieldAssert(pField, pField->eDataType == DATATYPE_STRUCT, "REDUNDANT_STRUCT found for non-struct token");

		for (i=0; i < pField->iNumRedundantStructInfos; i++)
		{
			pField->pCurOverrideTPIName = pField->redundantStructs[i].subTable;
			sprintf(curUserFieldName, "%s%s", pUserFieldNamePrefix, pField->redundantStructs[i].name);
			iCount += DumpFieldSpecifyUserFieldName(pFile, pField, pStructName, curUserFieldName, true, iLongestUserFieldNameLength, 0, pUserFieldNamePrefix);
		}

		pField->pCurOverrideTPIName = NULL;
	}

	pCommand = pField->pFirstCommand;
	while (pCommand)
	{
		DumpCommand(pFile, pField, pCommand, iLongestUserFieldNameLength);
		iCount++;
		pCommand = pCommand->pNext;
	}

	return iCount;

}

//returns true if the field dumping will print out all the { }\n stuff. In particular, this is used for
//EMBEDDED_FLAT structs and the embedded struct used in polymorphism
bool StructParser::FieldDumpsItselfCompletely(STRUCT_FIELD_DESC *pField)
{
	return pField->eDataType == DATATYPE_STRUCT && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_DIRECT && pField->bFlatEmbedded
		|| pField->bIAmPolyChildTypeField;
}


char *StructParser::GetAllUserFlags(STRUCT_FIELD_DESC *pField)
{
	static char sString[MAX_USER_FLAGS * (MAX_NAME_LENGTH + 1)];

	sString[0] = 0;

	int i;

	for (i=0; i < pField->iNumUserFlags; i++)
	{
		sprintf(sString + strlen(sString), "%s | ", pField->userFlags[i]);
	}

	return sString;
}



void StructParser::DumpCommand(	FILE *pFile, STRUCT_FIELD_DESC *pField, STRUCT_COMMAND *pCommand, int iLongestFieldNameLength)
{
	char tabs[MAX_TABS + 1];	

	MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pCommand->pCommandName)) - NUMTABS(iLongestFieldNameLength) + 1, MAX_TABS, '\t');

	if (pCommand->pCommandExpression)
	{
		char escapedExression[1024];
		AddCStyleEscaping(escapedExression, pCommand->pCommandExpression, 1024);

		fprintf(pFile, "\t{ \"%s\", %sTOK_COMMAND, 0, (intptr_t)\"%s\", NULL, 0, \" commandExpr = \\\"%s\\\" \" },\n",
			pCommand->pCommandName, tabs, pCommand->pCommandString, escapedExression);
	}
	else
	{
		fprintf(pFile, "\t{ \"%s\", %sTOK_COMMAND, 0, (intptr_t)\"%s\" },\n",
			pCommand->pCommandName, tabs, pCommand->pCommandString);
	}
}


int StructParser::DumpFieldSpecifyUserFieldName(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName, char *pUserFieldName, 
	bool bNameIsRedundant, int iLongestFieldNameLength, int iIndexInMultiLineField, char *pUserFieldNamePrefix)
{
	int iCount = 0;

	char tabs[MAX_TABS + 1];	
	MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pUserFieldName)) - NUMTABS(iLongestFieldNameLength) + 1, MAX_TABS, '\t');

	if (!FieldDumpsItselfCompletely(pField))
	{
		fprintf(pFile, "\t{ \"%s\",%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			pUserFieldName, 
			tabs,

			pField->bIAmPolyParentTypeField ? "TOK_OBJECTTYPE | " : "",
			bNameIsRedundant ? "TOK_REDUNDANTNAME | " : "",
			pField->bFoundStructParam ? "TOK_STRUCTPARAM | " : "",
			GetAllUserFlags(pField),
			pField->bFoundPersist ? "TOK_PERSIST | " : "",
			pField->bFoundNoTransact ? "TOK_NO_TRANSACT | " : "",
			pField->bFoundSometimesTransact ? "TOK_SOMETIMES_TRANSACT | " : "",
			pField->bIsInheritanceStruct ? "TOK_INHERITANCE_STRUCT | " : "",
			pField->refDictionaryName[0] && pField->eDataType == DATATYPE_CHAR ? "TOK_GLOBAL_NAME | " : "",
			pField->bIsDirtyBit ? "TOK_DIRTY_BIT | ": "",
			pField->bFoundSelfOnly ? "TOK_SELF_ONLY | " : "",
			pField->bFoundSubscribe ? "SUBSCRIBE | " : "",
			pField->bFoundSimpleInheritance ? "TOK_INHERITANCE_STRUCT | " : ""
			
			);


		int i;

		for (i=0; i < sizeof(pField->iTypeFlagsFound) * 8; i++)
		{
			if (pField->iTypeFlagsFound & (1 << i))
			{
				fprintf(pFile, "TOK_%s | ", sTokTypeFlags[i]);
			}
		}

		if (!bNameIsRedundant)
		{
			int i;

			for (i=0; i < sizeof(pField->iTypeFlags_NoRedundantRepeatFound) * 8; i++)
			{
				if (pField->iTypeFlags_NoRedundantRepeatFound & (1 << i))
				{
					fprintf(pFile, "TOK_%s | ", sTokTypeFlags_NoRedundantRepeat[i]);
				}
			}
		}
	}

	switch(pField->eReferenceType)
	{
	case REFERENCETYPE_DIRECT:
		switch (pField->eStorageType)
		{
		case STORAGETYPE_EMBEDDED:
			iCount += DumpFieldDirectEmbedded(pFile, pField, pStructName, iLongestFieldNameLength, iIndexInMultiLineField, pUserFieldNamePrefix);
			break;

		case STORAGETYPE_ARRAY:
			iCount += DumpFieldDirectArray(pFile, pField, pStructName);
			break;

		case STORAGETYPE_EARRAY:
			iCount += DumpFieldDirectEarray(pFile, pField, pStructName);
			break;
		}
		break;

	case REFERENCETYPE_POINTER:
	switch (pField->eStorageType)
		{
		case STORAGETYPE_EMBEDDED:
			iCount += DumpFieldPointerEmbedded(pFile, pField, pStructName);
			break;

		case STORAGETYPE_ARRAY:
			iCount += DumpFieldPointerArray(pFile, pField, pStructName);
			break;

		case STORAGETYPE_EARRAY:
			iCount += DumpFieldPointerEarray(pFile, pField, pStructName);
			break;
		}
		break;
	}	
			
	if (!FieldDumpsItselfCompletely(pField))
	{
		fprintf(pFile, "},\n");
	}

	return iCount;
}

void StructParser::PrintIgnore(FILE *pFile, char *pIgnoreString, int iLongestFieldNameLength, bool bIgnoreIsStructParam, bool bIsStruct)
{
	char tabs[MAX_TABS + 1];
	MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pIgnoreString)) - NUMTABS(iLongestFieldNameLength) + 1, MAX_TABS, '\t');


	fprintf(pFile, "\t{ \"%s\",%sTOK_IGNORE%s%s, 0 },\n", pIgnoreString, tabs, bIgnoreIsStructParam ? " | TOK_STRUCTPARAM" : "",
		bIsStruct ? " | TOK_IGNORE_STRUCT " : "");
}




bool StructParser::IsLinkName(char *pString)
{
	return false;
}

bool StructParser::IsFloatName(char *pString)
{
	return StringIsInList(pString, sFloatNameList);
}
bool StructParser::IsOutlawedTypeName(char *pString)
{
	return StringIsInList(pString, sOutlawedTypeNameList);
}

bool StructParser::IsIntName(char *pString)
{
	return StringIsInList(pString, sIntNameList);
}

bool StructParser::IsCharName(char *pString)
{
	return (strcmp(pString, "char") == 0);
}



bool StructParser::LoadStoredData(bool bForceReset)
{
	return true;
}
void StructParser::SetProjectPathAndName(char *pProjectPath, char *pProjectName)
{
}


void StructParser::TemplateFileNameFromSourceFileName(char *pTemplateName, char *pTemplateHeaderName, char *pSourceName)
{
	char workName[MAX_PATH];
	strcpy(workName, GetFileNameWithoutDirectories(pSourceName));
	int iLen = (int)strlen(workName);

	int i;

	for (i=0; i < iLen; i++)
	{
		if (workName[i] == '.')
		{
			workName[i] = '_';
		}
	}

	sprintf(pTemplateName, "%sAutoGen%s_ast.c", m_pParent->GetProjectPath(), workName);
	sprintf(pTemplateHeaderName, "%sAutoGen%s_ast.h", m_pParent->GetProjectPath(), workName);
}

void StructParser::WikiFileNameFromSourceFileName(char *pWikiFileName, char *pSourceName)
{
	char shortWikiName[MAX_PATH];

	int iLen = (int)strlen(pSourceName);

	char *pTemp = pSourceName + iLen - 1;

	while (*pTemp && *pTemp != '\\' && *pTemp != '/')
	{
		pTemp--;
	}

	if (*pTemp)
	{
		pTemp++;
	}

	strcpy(shortWikiName, pTemp);

	pTemp = shortWikiName;

	while (*pTemp)
	{
		if (*pTemp == '.')
		{
			*pTemp = '_';
		}

		pTemp++;
	}

	sprintf(pWikiFileName, "%swiki\\%s.wiki", m_pParent->GetProjectPath(), shortWikiName);
}




void StructParser::ResetSourceFile(char *pSourceFileName)
{
	char templateFileName[MAX_PATH];
	char templateHeaderFileName[MAX_PATH];

	TemplateFileNameFromSourceFileName(templateFileName, templateHeaderFileName, pSourceFileName);

	MaybeDeleteFileLater(templateFileName);
	MaybeDeleteFileLater(templateHeaderFileName);

		int i = 0;

	while (i < m_iNumStructs)
	{
		if (AreFilenamesEqual(m_pStructs[i]->sourceFileName, pSourceFileName))
		{
			DeleteStruct(i);

			memmove(&m_pStructs[i], &m_pStructs[i + 1], (m_iNumStructs - i - 1) * sizeof(void*));
			m_iNumStructs--;

		}
		else
		{
			i++;
		}
	}

	i = 0;

	while (i < m_iNumEnums)
	{
		if (AreFilenamesEqual(m_pEnums[i]->sourceFileName, pSourceFileName))
		{
			DeleteEnum(i);

			memmove(&m_pEnums[i], &m_pEnums[i + 1], (m_iNumEnums - i - 1) * sizeof(void*));
			m_iNumEnums--;

		}
		else
		{
			i++;
		}
	}


}

bool StructParser::WriteOutData(void)
{
	int iNumFileNames = 0;
	char fileNames[MAX_FILES_IN_PROJECT][MAX_PATH];

	int i, j;

	


	for (i=0; i < m_iNumStructs; i++)
	{
		FixupFieldTypes_RightBeforeWritingData(m_pStructs[i]);

		bool bIsUnique = true;

		for (j = 0; j < iNumFileNames; j++)
		{
			if (strcmp(m_pStructs[i]->sourceFileName, fileNames[j]) == 0)
			{
				bIsUnique = false;
				break;
			}
		}

		if (bIsUnique)
		{
			strcpy(fileNames[iNumFileNames++], m_pStructs[i]->sourceFileName);
		}
	}

	for (i=0; i < m_iNumEnums; i++)
	{
		bool bIsUnique = true;

		for (j = 0; j < iNumFileNames; j++)
		{
			if (strcmp(m_pEnums[i]->sourceFileName, fileNames[j]) == 0)
			{
				bIsUnique = false;
				break;
			}
		}

		if (bIsUnique)
		{
			strcpy(fileNames[iNumFileNames++], m_pEnums[i]->sourceFileName);
		}
	}

	for (i=0; i < m_iNumAutoTPFuncOpts; i++)
	{
		bool bIsUnique = true;

		for (j = 0; j < iNumFileNames; j++)
		{
			if (strcmp(m_AutoTpFuncOpts[i].sourceFileName, fileNames[j]) == 0)
			{
				bIsUnique = false;
				break;
			}
		}

		if (bIsUnique)
		{
			strcpy(fileNames[iNumFileNames++], m_AutoTpFuncOpts[i].sourceFileName);
		}
	}

	for (i=0; i < iNumFileNames; i++)
	{
		m_pParent->SetExtraDataFlagForFile(fileNames[i], 1 << m_iIndexInParent);
		WriteOutDataSingleFile(fileNames[i]);
	}

	return false;
}

bool StructParser::StringIsContainerName(STRUCT_DEF *pStruct, char *pString)
{
	if (m_pParent->GetDictionary()->FindIdentifier(pString) == IDENTIFIER_STRUCT_CONTAINER)
	{
		return true;
	}

	int i;

	for (i=0; i < pStruct->iNumFields; i++)
	{
		if (pStruct->pStructFields[i]->eDataType == DATATYPE_STRUCT 
			&& pStruct->pStructFields[i]->bFoundForceContainer
			&& strcmp(pStruct->pStructFields[i]->typeName, pString) == 0)
		{
			return true;
		}
	}

	return false;
}


#define NONCONST_SUFFIX "_AutoGen_NoConst"

void StructParser::DumpNonConstCopy(FILE *pFile, STRUCT_DEF *pStruct, bool bForceConstStruct)
{
	char structsPrototyped[MAX_NAME_LENGTH][MAX_FIELDS];
	int iNumStructsPrototyped = 0;

	Tokenizer tokenizer;
	tokenizer.LoadFromFile(pStruct->sourceFileName);
	tokenizer.SetOffset(pStruct->iPreciseStartingOffsetInFile, pStruct->iPreciseStartingLineNumInFile);
	tokenizer.SetCheckForInvisibleTokens(true);

	Token token;
	enumTokenType eType;

	fprintf(pFile, "\n//This is the autogenerated non-const copy of %s\n//Here are some struct prototypes for %s\n", 
		pStruct->structName, pStruct->structName);

	if (pStruct->pNonConstPrefixString)
	{
		fprintf(pFile, "\n%s\n", pStruct->pNonConstPrefixString);
	}

	int i;

	for (i=0; i < pStruct->iNumFields; i++)
	{
		if (pStruct->pStructFields[i]->eDataType == DATATYPE_STRUCT)
		{
			char sourceFileName[MAX_PATH];

			strcpy(structsPrototyped[iNumStructsPrototyped++], pStruct->pStructFields[i]->typeName);

			if (m_pParent->GetDictionary()->FindIdentifierAndGetSourceFile(pStruct->pStructFields[i]->typeName, sourceFileName) == IDENTIFIER_STRUCT_CONTAINER)
			{
				//for embedded structs that come from .h files, include the header rather than
				//making a prototype
				if (strcmp(sourceFileName + strlen(sourceFileName) - 2, ".h") == 0
					&& pStruct->pStructFields[i]->eStorageType == STORAGETYPE_EMBEDDED
					&& pStruct->pStructFields[i]->eReferenceType == REFERENCETYPE_DIRECT)
				{
					char simpleSourceFileName[MAX_PATH];
					strcpy(simpleSourceFileName, GetFileNameWithoutDirectories(sourceFileName));
					TruncateStringAtLastOccurrence(simpleSourceFileName, '.');
					fprintf(pFile, "#include \"AutoGen\\%s_h_ast.h\"\n",
						simpleSourceFileName);
				}
				else
				{
					fprintf(pFile, "typedef struct %s%s %s%s;\n", pStruct->pStructFields[i]->typeName, 
						NONCONST_SUFFIX, pStruct->pStructFields[i]->typeName, NONCONST_SUFFIX);
				}
			}
			else if (pStruct->pStructFields[i]->bFoundForceContainer)
			{
				fprintf(pFile, "typedef struct %s%s %s%s;\n", pStruct->pStructFields[i]->typeName, 
						NONCONST_SUFFIX, pStruct->pStructFields[i]->typeName, NONCONST_SUFFIX);
			}
			else
			{
				fprintf(pFile, "typedef struct %s %s;\n", pStruct->pStructFields[i]->typeName, pStruct->pStructFields[i]->typeName);
			}

		}
	}

	fprintf(pFile, "\n");

	int iBraceDepth = 0;

	do
	{
		bool bFound = false;
	
		eType = tokenizer.GetNextToken(&token);

		ASSERT(&tokenizer,eType != TOKEN_NONE, "File corruption or something bad while making non-const version");

		if (eType == TOKEN_IDENTIFIER)
		{
			if (strcmp(token.sVal, "const") == 0)
			{
				bFound = true;
			}
			else if (strcmp(token.sVal, AUTOSTRUCT_EXCLUDE) == 0)
			{
				bFound = true;
			}
			else if (strcmp(token.sVal, AUTOSTRUCT_EXTRA_DATA) == 0
				|| strcmp(token.sVal, AUTOSTRUCT_MACRO) == 0
				|| strcmp(token.sVal, AUTOSTRUCT_PREFIX) == 0
				|| strcmp(token.sVal, AUTOSTRUCT_NOT) == 0
				|| strcmp(token.sVal, AUTOSTRUCT_SUFFIX) == 0)
			{
				bFound = true;
				tokenizer.GetNextToken(&token);
				tokenizer.GetSpecialStringTokenWithParenthesesMatching(&token);
			}
			else if (strcmp(token.sVal, pStruct->structName) == 0)
			{
				bFound = true;
				fprintf(pFile, "%s%s ", token.sVal, NONCONST_SUFFIX);
			}
			else if (StringIsContainerName(pStruct, token.sVal))
			{
				//only do suffixing on structs that are actively used in structparsed fields
				for (i=0; i < iNumStructsPrototyped; i++)
				{
					if (strcmp(token.sVal, structsPrototyped[i]) == 0)
					{
						bFound = true;
						fprintf(pFile, "%s%s ", token.sVal, NONCONST_SUFFIX);
						break;
					}
				}
			} 
			else if (strcmp(token.sVal, "CONST_REF_TO") == 0)
			{
				bFound = true;
				fprintf(pFile, "REF_TO ");
			}
			else
			{
				int i;
				for (i=0; i < sizeof(gSpecialConstKeywords) / sizeof(gSpecialConstKeywords[0]); i++)
				{
					if (strcmp(token.sVal, gSpecialConstKeywords[i].pStartingString) == 0)
					{
						fprintf(pFile, "%s ", gSpecialConstKeywords[i].pNonConstString);
						bFound = true;
						break;
					}
				}
			}

			//for FORCE_CONST structs, we always noconst-ify any token that we recognize as being the name
			//of an included struct
			if (!bFound && bForceConstStruct)
			{
				for (i=0; i < iNumStructsPrototyped; i++)
				{
					if (strcmp(token.sVal, structsPrototyped[i]) == 0)
					{
						bFound = true;
						fprintf(pFile, "%s%s ", token.sVal, NONCONST_SUFFIX);
						break;
					}
				}
			}

		}

		if (!bFound)
		{
			tokenizer.StringifyToken(&token);
			fprintf(pFile, "%s ", token.sVal);
		}

		if (eType == TOKEN_RESERVEDWORD)
		{
			if (token.iVal == RW_LEFTBRACE)
			{
				iBraceDepth++;
			}
			else if (token.iVal == RW_RIGHTBRACE)
			{
				iBraceDepth--;
			}
		}

		if (!bFound)
		{
			if (eType == TOKEN_RESERVEDWORD && (token.iVal == RW_LEFTBRACE || token.iVal == RW_RIGHTBRACE || token.iVal == RW_SEMICOLON))
			{
				fprintf(pFile, "\n");
				int i;

				for (i=0; i < iBraceDepth ; i++)
				{
					fprintf(pFile, "\t");
				}
			}
		}

	} while (!(iBraceDepth == 0 && eType == TOKEN_RESERVEDWORD && token.iVal == RW_SEMICOLON));


	fprintf(pFile, "\n\n");

	if (pStruct->pNonConstSuffixString)
	{
		fprintf(pFile, "\n%s\n", pStruct->pNonConstSuffixString);
	}

}



void StructParser::WriteOutDataSingleFile(char *pFileName)
{

	int i;

	char templateFileName[MAX_PATH];
	char templateHeaderFileName[MAX_PATH];

	bool bNeedToWriteWikiFile = false;
	bool bIncludedInlineFile = false;

	TemplateFileNameFromSourceFileName(templateFileName, templateHeaderFileName, pFileName);

	FILE *pMainFile = NULL;
	FILE *pHeaderFile = NULL;

	
	pMainFile = fopen_nofail(templateFileName, "wt");
	fprintf(pMainFile, "#include \"textparser.h\"\n");

	if (SlowSafeDependencyMode())
	{
		fprintf(pMainFile, "//#ifed-out include to fool incredibuild dependencies\n#if 0\n#include \"%s\"\n#endif\n", pFileName);
	}

	pHeaderFile = fopen_nofail(templateHeaderFileName, "wt");
	WriteHeaderFileStart(pHeaderFile, pFileName);

	for (i=0; i < m_iNumEnums; i++)
	{
		if (strcmp(m_pEnums[i]->sourceFileName, pFileName) == 0)
		{
			if (m_pEnums[i]->pMainWikiComment)
			{
				bNeedToWriteWikiFile = true;
			}

			DumpEnum(pMainFile, m_pEnums[i]);
			DumpEnumPrototype(pHeaderFile, m_pEnums[i]);
		}
	}
	
	for (i=0; i < m_iNumAutoTPFuncOpts; i++)
	{
		if (strcmp(m_AutoTpFuncOpts[i].sourceFileName, pFileName) == 0)
		{
			if (!bIncludedInlineFile)
			{
				bIncludedInlineFile = true;
				fprintf(pHeaderFile, "#include \"textParsercallbacks_inline.h\"\n");
			}
			DumpAutoTpFuncOpt(pHeaderFile, &m_AutoTpFuncOpts[i]);
		}
	}
	for (i=0; i < m_iNumStructs; i++)
	{
		if (strcmp(m_pStructs[i]->sourceFileName, pFileName) == 0)
		{
			WriteRelevantIfsToFile(pMainFile, m_pStructs[i]->pIfDefStack);
	
			DumpStruct(pMainFile, m_pStructs[i]);
			DumpStructPrototype(pHeaderFile, m_pStructs[i]);
			DumpStructInitFunc(pMainFile, m_pStructs[i]);
			if (m_pStructs[i]->pMainWikiComment)
			{
				bNeedToWriteWikiFile = true;
			}

			if (m_pStructs[i]->bIsContainer || m_pStructs[i]->bIsForceConst)
			{
				DumpNonConstCopy(pHeaderFile, m_pStructs[i], m_pStructs[i]->bIsForceConst);
			}

			WriteRelevantEndIfsToFile(pMainFile, m_pStructs[i]->pIfDefStack);

		}
	}



	

	fclose(pMainFile);

	WriteHeaderFileEnd(pHeaderFile, pFileName);
	fclose(pHeaderFile);
		
	if (bNeedToWriteWikiFile)
	{
		char wikiFileName[MAX_PATH];
		m_pParent->CreateWikiDirectory();

		WikiFileNameFromSourceFileName(wikiFileName, pFileName);

		FILE *pWikiFile = fopen_nofail(wikiFileName, "wt");

		for (i=0; i < m_iNumEnums; i++)
		{
			if (strcmp(m_pEnums[i]->sourceFileName, pFileName) == 0)
			{
				DumpEnumToWikiFile(pWikiFile, m_pEnums[i]);
			}
		}

		for (i=0; i < m_iNumStructs; i++)
		{
			m_pStructs[i]->bLinkedTo = false;
			m_pStructs[i]->bWrittenOutAlready = false;
		}


		for (i=0; i < m_iNumStructs; i++)
		{
			if (strcmp(m_pStructs[i]->sourceFileName, pFileName) == 0)
			{
				if (m_pStructs[i]->pMainWikiComment)
				{
					DumpStructToWikiFile(pWikiFile, m_pStructs[i]);
					m_pStructs[i]->bWrittenOutAlready = true;
				}
			}
		}

		bool bFoundOne;

		do
		{
			bFoundOne = false;

			for (i=0; i < m_iNumStructs; i++)
			{
				if (m_pStructs[i]->bLinkedTo && !m_pStructs[i]->bWrittenOutAlready)
				{
					bFoundOne = true;
					DumpStructToWikiFile(pWikiFile, m_pStructs[i]);
					m_pStructs[i]->bWrittenOutAlready = true;
				}
			}
		}
		while (bFoundOne);

		fclose(pWikiFile);
	}
}

bool StructParser::StructHasWikiComments(STRUCT_DEF *pStruct)
{
	int i;

	for (i=0; i < pStruct->iNumFields; i++)
	{
		if (pStruct->pStructFields[i]->iNumWikiComments)
		{
			return true;
		}
	}

	return false;
}

char *StructParser::GetWikiTypeString(STRUCT_FIELD_DESC *pField, char *pIndexString)
{
	static char *pReturnString = NULL;

	if (strcmp(pField->typeName, "bool") == 0)
	{
		if (pIndexString[0])
		{
			estrPrintf(&pReturnString, " < 0/1 (%s)>", pIndexString);
		}
		else
		{
			estrPrintf(&pReturnString, " < 0/1 >");
		}

		return pReturnString;
	}

	switch (pField->eDataType)
	{
	case DATATYPE_INT:
		{
			ENUM_DEF *pEnum;
			if (pField->subTableName && (pEnum = FindEnumByName(pField->typeName)))
			{
				estrClear(&pReturnString);
				DumpEnumInWikiForField(pEnum, &pReturnString);
			}
			else
			{
				estrPrintf(&pReturnString, " < int%s >", pIndexString);
			}
		}
		break;

	case DATATYPE_FLOAT:
		estrPrintf(&pReturnString, " < float%s >", pIndexString);
		break;
	case DATATYPE_CHAR:
		if (pField->refDictionaryName[0])
		{
			estrPrintf(&pReturnString, " < %sRef%s >", pField->refDictionaryName, pIndexString);
		}
		else
		{		
			estrPrintf(&pReturnString, " < string%s >", pIndexString);
		}
		break;
	case DATATYPE_REFERENCE:
		estrPrintf(&pReturnString, " < %sRef%s >", pField->refDictionaryName, pIndexString);
		break;
	case DATATYPE_FILENAME:
		estrPrintf(&pReturnString, " < filename >");
		break;
	case DATATYPE_RGB:
		estrPrintf(&pReturnString, " < r g b >");
		break;
	case DATATYPE_RGBA:
		estrPrintf(&pReturnString, " < r g b a >");
		break;
	case DATATYPE_RG:
		estrPrintf(&pReturnString, " < r g >");
		break;

	default:
	
		estrCopy2(&pReturnString, pIndexString);
		break;
	}

	return pReturnString;
}

bool StructParser::StringRequiresWikiBackslash(char *pString)
{
	if (pString[0] == '{' || pString[0] == '}' || pString[0] == '(' || pString[0] == ')' || pString[0] == '\\')
	{
		return true;
	}

	return false;
}

void StructParser::DumpWikiFieldName(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pPrefixString, char *pOverrideName)
{
	if ((pField->eDataType == DATATYPE_INT || pField->eDataType == DATATYPE_FLOAT) && pField->eStorageType == STORAGETYPE_EMBEDDED && pField->eReferenceType == REFERENCETYPE_POINTER)
	{
		fprintf(pFile, "%s*%s*%s\n", pPrefixString, pOverrideName ? pOverrideName : pField->userFieldName, GetWikiTypeString(pField, "1"));
		fprintf(pFile, "%s*%s*%s\n", pPrefixString, pOverrideName ? pOverrideName : pField->userFieldName, GetWikiTypeString(pField, "2"));
	}
	else if (pField->eStorageType == STORAGETYPE_EARRAY && pField->eReferenceType == REFERENCETYPE_POINTER && pField->eDataType == DATATYPE_CHAR)
	{
		fprintf(pFile, "%s*%s*%s\n", pPrefixString, pOverrideName ? pOverrideName : pField->userFieldName, GetWikiTypeString(pField, "1"));
		fprintf(pFile, "%s*%s*%s\n", pPrefixString, pOverrideName ? pOverrideName : pField->userFieldName, GetWikiTypeString(pField, "2"));
	}
	else if (pField->eStorageType == STORAGETYPE_EARRAY && pField->eReferenceType == REFERENCETYPE_POINTER && pField->eDataType == DATATYPE_STRUCT)
	{
		fprintf(pFile, "%s*%s*%s\n", pPrefixString, pOverrideName ? pOverrideName : pField->userFieldName, GetWikiTypeString(pField, " (multiple allowed)"));
	}
	else
	{
		fprintf(pFile, "%s*%s*%s\n", pPrefixString, pOverrideName ? pOverrideName : pField->userFieldName, GetWikiTypeString(pField, ""));
	}
}



void StructParser::DumpStructFieldsToWikiFile(FILE *pFile, STRUCT_DEF *pStruct, int iDepth, bool bIncludeStartAndEnd, STRUCT_DEF *pParent)
{
	int i;
	char pPrefix1[16];
	char pPrefix2[16];
	int iDepthTemp;

	if (pStruct->pRecursionBlocker == pParent)
	{
		return;
	}

	pStruct->pRecursionBlocker = pParent;


	STATICASSERT(iDepth < 14, "depth recursion overlow while writing wiki files");

	strcpy(pPrefix1, "");
	strcpy(pPrefix2, "-");
	for (iDepthTemp = iDepth; iDepthTemp>0; iDepthTemp--)
	{
		strcat(pPrefix1, "-");
		strcat(pPrefix2, "-");
	}
	if (iDepth)
		strcat(pPrefix1, "\t");
	strcat(pPrefix2, "\t");

	if (bIncludeStartAndEnd)
	{
		char startTok[MAX_NAME_LENGTH] = "{";

		if (pStruct->bHasStartString)
		{
			strcpy(startTok, pStruct->startString);
		}
		else if (IsStructAllStructParams(pStruct))
		{
			startTok[0] = 0;
		}

		if (startTok[0])
		{
			if (iDepth <= 1) // JE
				fprintf(pFile, "\n"); // JE
			else
				fprintf(pFile, "%s", pPrefix1+1); // JE
			if (StringRequiresWikiBackslash(startTok))
			{
				fprintf(pFile, "*\\%s*\n", startTok); // JE: Added bolden to match struct def
			}
			else
			{
				fprintf(pFile, "*%s*\n", startTok); // JE: Added bolden to match struct def
			}
		}
	}

	for (i=0; i < pStruct->iNumFields; i++)
	{
		int iIndexNum = 0;

		do
		{
			char *pOverrideName = NULL;
			if (pStruct->pStructFields[i]->iNumIndexes)
			{
				pOverrideName = pStruct->pStructFields[i]->pIndexes[iIndexNum].nameString;
			}

			if (pStruct->pStructFields[i]->iNumWikiComments)
			{
			

				if (iDepth == 0) // JE
					fprintf(pFile, "\n"); // JE
				DumpWikiFieldName(pFile, pStruct->pStructFields[i], pPrefix1, pOverrideName);
				
				int j;
				for (j=0; j < pStruct->pStructFields[i]->iNumWikiComments; j++)
				{
					RemoveTrailingWhiteSpace(pStruct->pStructFields[i]->pWikiComments[j]);

					fprintf(pFile, "%s", pPrefix2);

					char *pReadHead = pStruct->pStructFields[i]->pWikiComments[j];

					while (*pReadHead)
					{
						fprintf(pFile, "%c", *pReadHead);
						if (*pReadHead == '\n' && *(pReadHead + 1))
						{
							fprintf(pFile, "%s", pPrefix2);
						}
						pReadHead++;
					}
					fprintf(pFile, "\n");
				}
				//JE: fprintf(pFile, "\n");
			}

			if (pStruct->pStructFields[i]->eDataType == DATATYPE_STRUCT && !FieldHasTypeFlag(pStruct->pStructFields[i], "UNOWNED"))
			{
				enumIdentifierType eIdentifierType = m_pParent->GetDictionary()->FindIdentifier(pStruct->pStructFields[i]->typeName);

				if (eIdentifierType == IDENTIFIER_STRUCT || eIdentifierType == IDENTIFIER_STRUCT_CONTAINER)
				{
					STRUCT_DEF *pSubStruct = FindNamedStruct(pStruct->pStructFields[i]->typeName);

					if(pSubStruct)
					{

						if (StructHasWikiComments(pSubStruct))
						{
							if (pStruct->pStructFields[i]->eReferenceType == REFERENCETYPE_DIRECT && pStruct->pStructFields[i]->eStorageType == STORAGETYPE_EMBEDDED && pStruct->pStructFields[i]->bFlatEmbedded)
							{
								DumpStructFieldsToWikiFile(pFile, pSubStruct, iDepth, false, pParent);


							}
							else
							{
								if (pStruct->pStructFields[i]->bDoWikiLink)
								{
									fprintf(pFile, "[#%s]\n", pSubStruct->structName);
									pSubStruct->bLinkedTo = true;
								}
								else
								{
									if (!pStruct->pStructFields[i]->iNumWikiComments)
									{
										if (iDepth == 0) // JE
											fprintf(pFile, "\n"); // JE
										DumpWikiFieldName(pFile, pStruct->pStructFields[i], pPrefix1, pOverrideName);
									}

									DumpStructFieldsToWikiFile(pFile, pSubStruct, iDepth + 1, true, pParent);
								}
							}
						}
					}
				}
			}

			iIndexNum++;
		}
		while (iIndexNum < pStruct->pStructFields[i]->iNumIndexes);		
	}

	if (bIncludeStartAndEnd)
	{
		char endTok[MAX_NAME_LENGTH] = "}";

		if (pStruct->iNumEndStrings)
		{
			strcpy(endTok, pStruct->endStrings[0]);
		}
		else if (IsStructAllStructParams(pStruct))
		{
			sprintf(endTok, "\n");
		}

		if (endTok[0])
		{
			if (iDepth <= 1) // JE
				fprintf(pFile, "\n"); // JE
			else
				fprintf(pFile, "%s", pPrefix1+1); // JE
			if (StringRequiresWikiBackslash(endTok))
			{
				fprintf(pFile, "*\\%s*\n", endTok); // JE: Added bolden to match struct def
			}
			else
			{
				fprintf(pFile, "*%s*\n", endTok); // JE: Added bolden to match struct def
			}
		}
	}

}


void StructParser::DumpStructToWikiFile(FILE *pFile, STRUCT_DEF *pStruct)
{
	fprintf(pFile, "{anchor:%s}\n", pStruct->structName);
	fprintf(pFile, "\n_auto-generated from %s_\n", pStruct->sourceFileName);
	fprintf(pFile, "\nh3.%s\n", pStruct->pMainWikiComment);

	DumpStructFieldsToWikiFile(pFile, pStruct, 0, false, pStruct);
}


StructParser::STRUCT_DEF *StructParser::FindNamedStruct(char *pStructName)
{
	int i;

	for (i=0; i < m_iNumStructs; i++)
	{
		if (strcmp(m_pStructs[i]->structName, pStructName) == 0)
		{
			return m_pStructs[i];
		}
	}

	return NULL;
}


char *pPrefixes[] = 
{
	"p",
	"e",
	"i",
	"f",
	"pp",
	"pi",
	"pch",
	"b",
	"pc",
	"ea",
	"b",
	"v",
};

void StructParser::FixupFieldName(STRUCT_FIELD_DESC *pField, bool bStripUnderscores, bool bNoPrefixStripping, bool bForceUseActualFieldName,
	bool bAlwaysIncludeActualFieldNameAsRedundant)
{
	if (strcmp(pField->userFieldName, pField->baseStructFieldName) == 0)
	{
		int i;

		if (!bNoPrefixStripping)
		{
			for (i=0; i < sizeof(pPrefixes) / sizeof(pPrefixes[0]); i++)
			{
				int iPrefixLength = (int)strlen(pPrefixes[i]);

				if (StringBeginsWith(pField->userFieldName, pPrefixes[i], true))
				{
					if (pField->userFieldName[iPrefixLength] >= 'A' && pField->userFieldName[iPrefixLength] <= 'Z' || pField->userFieldName[iPrefixLength] == '_') 
					{
						int iNameLength = (int)strlen(pField->userFieldName);

						memmove(pField->userFieldName, pField->userFieldName + iPrefixLength, iNameLength - iPrefixLength + 1);
						
						break;
					}
				}
			}
		}

		if (bStripUnderscores)
		{
			int iLen = (int)strlen(pField->userFieldName);

			i = 0;

			while (i < iLen)
			{
				if (pField->userFieldName[i] == '_')
				{
					memmove(pField->userFieldName + i, pField->userFieldName + i + 1, iLen - i);
					iLen -= 1;
				}
				else
				{
					i++;
				}
			}
		}
	}

	if (bForceUseActualFieldName)
	{
		if (strcmp(pField->userFieldName, pField->baseStructFieldName) == 0)
		{
			return;
		}

		FieldAssert(pField, pField->iNumRedundantNames < MAX_REDUNDANT_NAMES_PER_FIELD, "Too many redundant names with FORCE_USE_ACTUAL_FIELD_NAME");
	
		memmove(pField->redundantNames[1], pField->redundantNames[0], pField->iNumRedundantNames * sizeof(pField->redundantNames[0]));
		pField->iNumRedundantNames++;
		strcpy(pField->redundantNames[0], pField->userFieldName);
		strcpy(pField->userFieldName, pField->baseStructFieldName);
	}
	else if (bAlwaysIncludeActualFieldNameAsRedundant)
	{
		if (strcmp(pField->userFieldName, pField->baseStructFieldName) == 0)
		{
			return;
		}

		FieldAssert(pField, pField->iNumRedundantNames < MAX_REDUNDANT_NAMES_PER_FIELD, "Too many redundant names with FORCE_USE_ACTUAL_FIELD_NAME");

		strcpy(pField->redundantNames[pField->iNumRedundantNames++], pField->baseStructFieldName);
	}

}


void StructParser::WriteHeaderFileStart(FILE *pFile, char *pSourceName)
{
	char *pShortenedFileName = GetFileNameWithoutDirectories(pSourceName);
	char macroName[MAX_PATH];
	sprintf(macroName, "_%s_AST_H_", pShortenedFileName);

	MakeStringAllAlphaNumAndUppercase(macroName);

	fprintf(pFile, "#ifndef %s\n#define %s\n#pragma once\nGCC_SYSTEM\n#include \"textparser.h\"\n//This file is autogenerated and contains TextParserInfo prototypes from %s\n//autogenerated" "nocheckin\n",
		macroName, macroName, 	
		pShortenedFileName);

	if (SlowSafeDependencyMode())
	{
		fprintf(pFile, "//#ifed-out include to fool incredibuild dependencies\n#if 0\n#include \"%s\"\n#endif\n", pSourceName);
	}
}
	

	
void StructParser::WriteHeaderFileEnd(FILE *pFile, char *pSourceName)
{
	fprintf(pFile, "\n#endif\n");
}


int StructParser::PrintStructTableInfoColumn(FILE *pFile, STRUCT_DEF *pStruct)
{
	char tabs[MAX_TABS + 1];
	MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pStruct->structName)) - NUMTABS(pStruct->iLongestUserFieldNameLength) + 1, MAX_TABS, '\t');
	fprintf(pFile, "\t{ \"%s\", %sTOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(%s), 0, NULL, 0, ", pStruct->structName, tabs, pStruct->structName);
	if (pStruct->pStructLevelFormatString)
	{
		fprintf(pFile, "\"%s\"},\n", pStruct->pStructLevelFormatString);
	}
	else
	{
		fprintf(pFile, "NULL },\n");
	}
	return 1;
}

int StructParser::PrintStructStart(FILE *pFile, STRUCT_DEF *pStruct)
{
	if (pStruct->bHasStartString)
	{
		if (pStruct->startString[0])
		{
			char tabs[MAX_TABS + 1];
			
			MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pStruct->startString)) - NUMTABS(pStruct->iLongestUserFieldNameLength) + 1, MAX_TABS, '\t');

			fprintf(pFile, "\t{ \"%s\",%sTOK_START, 0 },\n", pStruct->startString, tabs);
			return 1;
		}
	}
	else if (IsStructAllStructParams(pStruct))
	{
	}
	else
	{
		char tabs[MAX_TABS + 1];
		
		MakeRepeatedCharacterString(tabs, NUMTABS(1) - NUMTABS(pStruct->iLongestUserFieldNameLength) + 1, MAX_TABS, '\t');

		fprintf(pFile, "\t{ \"{\",%sTOK_START, 0 },\n", tabs);
		return 1;
	}

	return 0;
}

int StructParser::PrintStructEnd(FILE *pFile, STRUCT_DEF *pStruct)
{
	if (pStruct->iNumEndStrings)
	{
		int ret=0;
		for (int i=0; i<pStruct->iNumEndStrings; i++)
		{
			if (pStruct->endStrings[i][0])
			{
				char tabs[MAX_TABS + 1];
				
				MakeRepeatedCharacterString(tabs, NUMTABS(strlen(pStruct->endStrings[i])) - NUMTABS(pStruct->iLongestUserFieldNameLength) + 1, MAX_TABS, '\t');

				fprintf(pFile, "\t{ \"%s\",%sTOK_END, 0 },\n", pStruct->endStrings[i], tabs);
				ret = 1;
			}
		}
		return ret;

	}
	else if (IsStructAllStructParams(pStruct))
	{
		char tabs[MAX_TABS + 1];
		
		MakeRepeatedCharacterString(tabs, NUMTABS(2) - NUMTABS(pStruct->iLongestUserFieldNameLength) + 1, MAX_TABS, '\t');

		fprintf(pFile, "\t{ \"\\n\",%sTOK_END, 0 },\n", tabs);
		return 1;
	}
	else
	{
		char tabs[MAX_TABS + 1];
		
		MakeRepeatedCharacterString(tabs, NUMTABS(1) - NUMTABS(pStruct->iLongestUserFieldNameLength) + 1, MAX_TABS, '\t');

		fprintf(pFile, "\t{ \"}\",%sTOK_END, 0 },\n", tabs);
		return 1;
	}
}

/*
static __forceinline bool RECVDIFF(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	if (!(tpi[column].type & (TOK_EARRAY | TOK_FIXED_ARRAY)))
	{
		recvdiff_f recvdiffFunc = TYPE_INFO(tpi[column].type).recvdiff;
		if (recvdiffFunc)
			return recvdiffFunc(pak, tpi, column, structptr, index, eFlags);
		return false;
	}
	else if (tpi[column].type & TOK_EARRAY)
	{
		return earray_recvdiff(pak, tpi, column, structptr, index, eFlags);
	}
	else
	{
		return fixedarray_recvdiff(pak, tpi, column, structptr, index, eFlags);
	}
}
*/

void StructParser::DumpAutoTpFuncOpt(FILE *pFile, AUTO_TP_FUNC_OPT_STRUCT *pFunc)
{
	bool bVoidReturn = false;

	if (strcmp(pFunc->resultType, "void") == 0)
	{
		bVoidReturn = true;
	}



	fprintf(pFile, "static __forceinline %s %s_autogen%s\n",
		pFunc->resultType, pFunc->funcTypeName, pFunc->fullArgString);
	fprintf(pFile, "{\n\tStructTypeField field_type = tpi[column].type;");
	fprintf(pFile, "\n\tif (!(field_type & (TOK_EARRAY | TOK_FIXED_ARRAY)))\n\t{\n\t\t%snonarray_%s(%s);\n",
		bVoidReturn ? "" : "return ", pFunc->funcTypeName, pFunc->recurseArgString);
	fprintf(pFile, "\t}\n\telse if (field_type & TOK_EARRAY)\n\t{\n");
	fprintf(pFile, "\t\t%searray_%s(%s);\n\t}\n\telse\n\t{\n",
		bVoidReturn ? "" : "return ", pFunc->funcTypeName, pFunc->recurseArgString);
	fprintf(pFile, "\t\t%sfixedarray_%s(%s);\n\t}\n}\n\n",
		bVoidReturn ? "" : "return ", pFunc->funcTypeName, pFunc->recurseArgString);

}



/*
StaticDefineInt FieldTypeEnum[] =
{
	DEFINE_INT
	{ "Normal", FIELDTYPE_NORMAL},
	DEFINE_END
};
*/




void StructParser::DumpEnum(FILE *pFile, ENUM_DEF *pEnum)
{
	int iPrefixLength = 0;
	int iLastUnderscore = 0;
	int i, j;

	if (pEnum->iNumEntries > 1 && !pEnum->bNoPrefixStripping)
	{
		char prefixString[MAX_NAME_LENGTH];
		strcpy(prefixString, pEnum->pEntries[0].inCodeName);

		iPrefixLength = (int)strlen(prefixString);


		for (i=1; i < pEnum->iNumEntries; i++)
		{
			for (j=0; j < iPrefixLength; j++)
			{
				if (prefixString[j] != pEnum->pEntries[i].inCodeName[j])
				{
					iPrefixLength = j;
					break;
				}
			}
		}
	}

	for (i=0; i < iPrefixLength; i++)
	{
		if (pEnum->pEntries[0].inCodeName[i] == '_')
		{
			iLastUnderscore = i;
		}
	}

	if (iLastUnderscore)
	{
		iPrefixLength = iLastUnderscore + 1;
	}


	fprintf(pFile, "\n//auto-generated staticdefine for enum %s\n//autogenerated" "nocheckin\n", pEnum->enumName);

	WriteRelevantIfsToFile(pFile, pEnum->pIfDefStack);

	if (pEnum->embeddedDynamicName[0])
	{
		fprintf(pFile, "extern DefineContext *%s;\n", pEnum->embeddedDynamicName);
	}

	fprintf(pFile, "StaticDefineInt %sEnum[] =\n{\n\tDEFINE_INT\n", pEnum->enumName);

	for (i=0; i < pEnum->iNumEntries; i++)
	{
		fprintf(pFile, "\t{ \"%s\", %s},\n", 
			pEnum->pEntries[i].iNumExtraNames ? pEnum->pEntries[i].extraNames[0] : pEnum->pEntries[i].inCodeName + iPrefixLength, 
			pEnum->pEntries[i].inCodeName);

		for (j=1; j < pEnum->pEntries[i].iNumExtraNames; j++)
		{
			fprintf(pFile, "\t{ \"%s\", %s},\n", 
				pEnum->pEntries[i].extraNames[j], pEnum->pEntries[i].inCodeName);
		}


	}

	if (pEnum->embeddedDynamicName[0])
	{
		fprintf(pFile, "\tDEFINE_EMBEDDYNAMIC_INT(%s)\n", pEnum->embeddedDynamicName);
	}

	for (i = 0; i < pEnum->iPadding; i++)
	{
		fprintf(pFile, "\tDEFINE_END\n");
	}

	fprintf(pFile, "\tDEFINE_END\n};\n");

	
	char fixupFuncName[256];
	sprintf(fixupFuncName, "autoEnum_fixup_%s", pEnum->enumName);
	if (pEnum->enumToAppendTo[0])
	{
		fprintf(pFile, "extern StaticDefineInt %sEnum[];\n", pEnum->enumToAppendTo);
	}
	if (pEnum->enumAppendOtherToMe[0])
	{
		fprintf(pFile, "extern StaticDefineInt %sEnum[];\n", pEnum->enumAppendOtherToMe);
	}
	fprintf(pFile, "void %s(void)\n{\n", fixupFuncName);

	fprintf(pFile, "\tstatic bool bOnce = false; if (bOnce) return; bOnce = true;\n");

	fprintf(pFile, "\tRegisterNamedStaticDefine(%sEnum, \"%s\");\n", pEnum->enumName, pEnum->enumName);

	if (pEnum->enumToAppendTo[0])
	{
		fprintf(pFile, "\tStaticDefineIntAddTailList(%sEnum, %sEnum);\n", pEnum->enumToAppendTo, pEnum->enumName);
	}
	if (pEnum->enumAppendOtherToMe[0])
	{
		fprintf(pFile, "\tStaticDefineIntAddTailList(%sEnum, %sEnum);\n", pEnum->enumName, pEnum->enumAppendOtherToMe);
	}

	fprintf(pFile, "}\n");

	WriteRelevantEndIfsToFile(pFile, pEnum->pIfDefStack);


	m_pParent->GetAutoRunManager()->AddAutoRunWithIfDefs(fixupFuncName, pEnum->sourceFileName, pEnum->pIfDefStack);





}


void StructParser::DumpEnumPrototype(FILE *pFile, ENUM_DEF *pEnum)
{


	fprintf(pFile, "\n//auto-generated staticdefine for enum %s\n//autogenerated" "nocheckin\n", pEnum->enumName);
	fprintf(pFile, "extern StaticDefineInt %sEnum[];\n", pEnum->enumName);


}

//returns number of dependencies found
int StructParser::ProcessDataSingleFile(char *pSourceFileName, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE])
{
	int i;

	for (i=0; i < m_iNumStructs; i++)
	{
		if (AreFilenamesEqual(m_pStructs[i]->sourceFileName, pSourceFileName))
		{
			FixupFieldTypes(m_pStructs[i]);
			CheckOverallStructValidity_PostFixup(m_pStructs[i]);
		}
	}

	int iNumDependencies = 0;

	for (i=0; i < m_iNumStructs; i++)
	{
		if (AreFilenamesEqual(m_pStructs[i]->sourceFileName, pSourceFileName))
		{
			
			FindDependenciesInStruct(pSourceFileName, m_pStructs[i], &iNumDependencies, pDependencies);
			
		}
	}

	return iNumDependencies;
}

void StructParser::FindDependenciesInStruct(char *pSourceFileName, STRUCT_DEF *pStruct, int *piNumDependencies, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE])
{
	int i;
	

	if (pStruct->structNameIInheritFrom[0])
	{
		FieldAssert(pStruct->pStructFields[0], pStruct->pStructFields[0]->pStructSourceFileName != NULL,
			"Unrecognized struct name for POLYCHILDTYPE");
		if (!AreFilenamesEqual(pStruct->pStructFields[0]->pStructSourceFileName, pSourceFileName))
		{
			FieldAssert(pStruct->pStructFields[0], *piNumDependencies < MAX_DEPENDENCIES_SINGLE_FILE, "Too many dependencies");
			pDependencies[*piNumDependencies] = pStruct->pStructFields[0]->pStructSourceFileName;
			(*piNumDependencies)++;
		}
	}

	for (i=0; i < pStruct->iNumFields; i++)
	{
		STRUCT_FIELD_DESC *pField = pStruct->pStructFields[i];

		if (pField->eDataType == DATATYPE_INT && pField->subTableName[0] && (StructHasWikiComments(pStruct) || pStruct->pMainWikiComment))
		{
			char *pEnumFile;

			if (m_pParent->GetDictionary()->FindIdentifierAndGetSourceFilePointer(pField->typeName, &pEnumFile) == IDENTIFIER_ENUM)
			{
				if (!AreFilenamesEqual(pSourceFileName, pEnumFile))
				{
					FieldAssert(pField, *piNumDependencies < MAX_DEPENDENCIES_SINGLE_FILE, "Too many dependencies");
					pDependencies[*piNumDependencies] = pEnumFile;
					(*piNumDependencies)++;
				}
			}
		} 
		else if (pField->eDataType == DATATYPE_STRUCT && pField->bFlatEmbedded
			|| pField->eDataType == DATATYPE_STRUCT && (StructHasWikiComments(pStruct) || pStruct->pMainWikiComment)

//all of a container's structs cause dependencies in case they switch to/from being a container				
			|| pField->eDataType == DATATYPE_STRUCT && pStruct->bIsContainer)
		{

			//this is so that we can always tell whether the structs in a container are themselves containers
			if (pField->eDataType == DATATYPE_STRUCT && pStruct->bIsContainer && pField->bFoundPersist)
			{
				FieldAssert(pField, pField->pStructSourceFileName != NULL
					|| pField->bFoundForceContainer, "Persisted fiels for containers may only contain structs which are defined in this project, or have FORCE_CONTAINER");
			}

			if (pField->pStructSourceFileName)
			{
				if (!AreFilenamesEqual(pField->pStructSourceFileName, pSourceFileName))
				{
					FieldAssert(pField, *piNumDependencies < MAX_DEPENDENCIES_SINGLE_FILE, "Too many dependencies");
					pDependencies[*piNumDependencies] = pField->pStructSourceFileName;
					(*piNumDependencies)++;
				}
			}
		}
		else if (pField->eDataType == DATATYPE_STRUCT_POLY)
		{
			if (pField->pStructSourceFileName)
			{
				if (!AreFilenamesEqual(pField->pStructSourceFileName, pSourceFileName))
				{
					FieldAssert(pField, *piNumDependencies < MAX_DEPENDENCIES_SINGLE_FILE, "Too many dependencies");
					pDependencies[*piNumDependencies] = pField->pStructSourceFileName;
					(*piNumDependencies)++;
				}
			}
		}
	}
}

void StructParser::ResetMacros(void)
{
	int i;

	for (i=0; i < m_iNumMacros; i++)
	{
		delete m_Macros[i].pIn;
		delete m_Macros[i].pOut;
	}

	m_iNumMacros = 0;

	if (m_pPrefix)
	{
		delete m_pPrefix;
		m_pPrefix = NULL;
	}

	if (m_pSuffix)
	{
		delete m_pSuffix;
		m_pSuffix = NULL;
	}
}



void StructParser::ReplaceMacrosInString(char *pString, int iOutStringLength)
{
	int i;
	int iLen = (int)strlen(pString);

	for (i=0; i < m_iNumMacros; i++)
	{
		iLen += ReplaceMacroInString(pString, &m_Macros[i], iLen, iOutStringLength);
	}
}

int StructParser::ReplaceMacroInString(char *pString, AST_MACRO_STRUCT *pMacro, int iCurLength, int iMaxLength)
{
	int iRetVal = 0;
	int i;

	if (iCurLength < pMacro->iInLength)
	{
		return 0;
	}
	
	for (i=0; i <= iCurLength - pMacro->iInLength; i++)
	{
		if (StringBeginsWith(pString + i, pMacro->pIn, true))
		{
			if ((i == 0 || !IsOKForIdent(pString[i-1])) && !IsOKForIdent(pString[i + pMacro->iInLength]))
			{
				memmove(pString + i + pMacro->iOutLength, pString + i + pMacro->iInLength, iCurLength - (i + pMacro->iInLength) + 1);
				memcpy(pString + i, pMacro->pOut, pMacro->iOutLength);

				iCurLength += pMacro->iOutLength - pMacro->iInLength;
				iRetVal += pMacro->iOutLength - pMacro->iInLength;
				i += pMacro->iOutLength - 1;
			}
		}
	}

	return iRetVal;
}

void StructParser::AttemptToDeduceReferenceDictionaryName(STRUCT_FIELD_DESC *pField, char *pTypeName)
{
	strcpy(pField->refDictionaryName, pTypeName);
}

int StructParser::GetNumLinesFieldTakesUp(STRUCT_FIELD_DESC *pField)
{
	switch (pField->eDataType)
	{
	case DATATYPE_MAT4:
	case DATATYPE_MAT3_ASMATRIX:
		return 2;

	case DATATYPE_MAT4_ASMATRIX:
		return 3;
	}

	return 1;
}

char *StructParser::GetMultiLineFieldNamePrefix(STRUCT_FIELD_DESC *pField, int iIndex)
{
	switch (pField->eDataType)
	{
	case DATATYPE_MAT4:
		switch (iIndex)
		{
		case 0:
			return "rot";
		case 1:
			return "pos";
		}

	case DATATYPE_MAT3_ASMATRIX:
		switch (iIndex)
		{
		case 0:
			return "asmatrix";
		case 1:
			return "";
		}

	case DATATYPE_MAT4_ASMATRIX:
		switch (iIndex)
		{
		case 0:
			return "asmatrix";
		case 1:
			return "rot";
		case 2:
			return "pos";
		}
	}


	return "THIS STRING SHOULD NEVER BE SEEN! PANIC! RUN FOR THE HILLS!";
}


StructParser::ENUM_DEF *StructParser::FindEnumByName(char *pName)
{
	int i;

	for (i=0; i < m_iNumEnums; i++)
	{
		if (strcmp(pName, m_pEnums[i]->enumName) == 0)
		{
			return m_pEnums[i];
		}
	}

	return NULL;
}



void StructParser::DumpEnumToWikiFile(FILE *pOutFile, ENUM_DEF *pEnum)
{
	int iPrefixLength = 0;
	int i, j;

	fprintf(pOutFile, "{anchor:%s}\n", pEnum->enumName);

	fprintf(pOutFile, "_auto-generated from %s_\n", pEnum->sourceFileName);

	fprintf(pOutFile, "h3. Enumerated type *%s* - %s\n", pEnum->enumName, pEnum->pMainWikiComment);

	if (pEnum->iNumEntries > 1 && !pEnum->bNoPrefixStripping)
	{
		char prefixString[MAX_NAME_LENGTH];
		strcpy(prefixString, pEnum->pEntries[0].inCodeName);

		iPrefixLength = (int)strlen(prefixString);


		for (i=1; i < pEnum->iNumEntries; i++)
		{
			for (j=0; j < iPrefixLength; j++)
			{
				if (prefixString[j] != pEnum->pEntries[i].inCodeName[j])
				{
					iPrefixLength = j;
					break;
				}
			}
		}
	}

	for (i=0; i < pEnum->iNumEntries; i++)
	{
		fprintf(pOutFile, "*%s*\n",
			pEnum->pEntries[i].iNumExtraNames ? pEnum->pEntries[i].extraNames[0] : pEnum->pEntries[i].inCodeName + iPrefixLength);
		if (pEnum->pEntries[i].pWikiComment)
		{
			fprintf(pOutFile, "-\t%s\n", pEnum->pEntries[i].pWikiComment);
		}
	}

}

void StructParser::DumpEnumInWikiForField(ENUM_DEF *pEnum, char **ppOutEString)
{
	int iPrefixLength = 0;
	int i, j;

	if (pEnum->iNumEntries > 1 && !pEnum->bNoPrefixStripping)
	{
		char prefixString[MAX_NAME_LENGTH];
		strcpy(prefixString, pEnum->pEntries[0].inCodeName);

		iPrefixLength = (int)strlen(prefixString);


		for (i=1; i < pEnum->iNumEntries; i++)
		{
			for (j=0; j < iPrefixLength; j++)
			{
				if (prefixString[j] != pEnum->pEntries[i].inCodeName[j])
				{
					iPrefixLength = j;
					break;
				}
			}
		}
	}

	estrConcatf(ppOutEString, " < ");

	for (i=0; i < pEnum->iNumEntries; i++)
	{
		estrConcatf(ppOutEString, "%s%s",
			i > 0 ? " | " : "",
			pEnum->pEntries[i].iNumExtraNames ? pEnum->pEntries[i].extraNames[0] : pEnum->pEntries[i].inCodeName + iPrefixLength);
	}

	estrConcatf(ppOutEString, " > [#%s]\n", pEnum->enumName);


}


void StructParser::AssertNameIsLegalForFormatStringString(Tokenizer *pTokenizer, char *pName)
{
	if (!m_pParent->DoesVariableHaveValue("FormatString_Strings", pName, false))
	{
		ASSERTF(pTokenizer,0, "Found illegal or misused FormatString name %s... legal names are defined in the FormatString_Strings variable in StructParserVars.txt, found in c:\\src\\core, c:\\src\\fightclub, etc.", pName);
	}
}
void StructParser::AssertNameIsLegalForFormatStringInt(Tokenizer *pTokenizer, char *pName)
{
	if (!m_pParent->DoesVariableHaveValue("FormatString_Ints", pName, false))
	{
		ASSERTF(pTokenizer,0, "Found illegal or misused FormatString name %s... legal names are defined in the FormatString_Strings variable in StructParserVars.txt, found in c:\\src\\core, c:\\src\\fightclub, etc.", pName);
	}}

void StructParser::AddStringToFormatString(char **ppFormatString, char *pStringToAdd)
{
	if (!(*ppFormatString))
	{
		(*ppFormatString) = STRDUP(pStringToAdd);
		return;
	}

	int iCurLen = (int)strlen((*ppFormatString));
	int iAddLen = (int)strlen(pStringToAdd);
	char *pNewString = new char[iCurLen + iAddLen + 4];

	sprintf(pNewString, "%s , %s", (*ppFormatString), pStringToAdd);

	delete((*ppFormatString));
	(*ppFormatString) = pNewString;
}


void StructParser::ResetAllStructFieldIndicesInStruct(STRUCT_DEF *pStruct)
{
	int i;

	for (i=0; i < pStruct->iNumFields; i++)
	{
		pStruct->pStructFields[i]->iCurIndexCount = 0;
	}
}


void StructParser::ResetAllStructFieldIndices(void)
{
	int i;

	for (i=0; i < m_iNumStructs; i++)
	{
		ResetAllStructFieldIndicesInStruct(m_pStructs[i]);
	}
}

void StructParser::AddTokTypeFlagByString(Tokenizer *pTokenizer, STRUCT_FIELD_DESC *pField, char *pFlagString)
{
	int i;
	if (!pFlagString)
	{
		return;
	}

	for (i=0; i < sizeof(sTokTypeFlags) / sizeof(sTokTypeFlags[0]); i++)
	{
		STATICASSERT(i < 32, "too many sTokTypeFlags");

		if (strcmp(pFlagString, sTokTypeFlags[i]) == 0)
		{
			pField->iTypeFlagsFound |= (1 << i);
			return;
		}
	}

	pTokenizer->AssertFailedf("Unknown tok type flag %s", pFlagString);

}
