#include "stdio.h"
#include "tokenizer.h"
#include "assert.h"
#include "string.h"
#include "windows.h"
#include "strutils.h"
#include "Utils.h"
#include "earray.h"





#undef ASSERT
#undef ASSERTF
#define ASSERT(condition, expression) { if (!(condition)) AssertFailed(expression); }
#define ASSERTF(condition, expression, ...) { if (!(condition)) AssertFailedf(expression, __VA_ARGS__); }


static char sCommentControlCode[] = {"##"};
#define COMMENT_CONTROL_CODE_LENGTH 2

static char sSingleCharReservedWordIndex[256] = { 0 };

static char *sReservedWords[] = 
{
	"",
	"->",
	"{",
	"}",
	"(",
	")",
	"[",
	"]",
	",",
	":",
	"-",
	";",
	"+",
	"/",
	"!",
	"&",
	"*",
	"^",
	"=",
	".",
	">",
	"<",
	"?",
	"~",
	"|",


	"struct",
	"typedef",
	"void",
	"enum",
	"if",
};

StringTree *Tokenizer::m_pReservedWordTree = NULL;


Tokenizer::Tokenizer()
{
	static bool bFirst = true;

	if (bFirst)
	{
		int i;
		bFirst = false;

		for (i = FIRST_SINGLECHAR_RW; i < FIRST_MULTICHAR_RW; i++)
		{
			sSingleCharReservedWordIndex[sReservedWords[i][0]] = i;
		}
	}

	m_bLookForControlCodeInComments = false;
	m_bInsideCommentControlCode = false;
	m_pExtraReservedWordTree = NULL;
	m_bOwnsBuffer = false;
	m_bCSourceStyleStrings = false;
	m_LastStringLength = 0;
	m_bNoNewlinesInStrings = false;
	m_bSkipDefines = false;
	m_pExtraIdentifierChars = NULL;
	m_bIgnoreQuotes = false;
	m_bUseIfDefStack = false;
	m_pNonIntIfDefError = NULL;
	memset(&m_ifDefStack, 0, sizeof(IfDefStack));

	
	if (!m_pReservedWordTree)
	{
		m_pReservedWordTree = StringTree_Create();

		int i;
		for (i = FIRST_MULTICHAR_RW; i < RW_COUNT; i++)
		{
			StringTree_AddWord(m_pReservedWordTree, sReservedWords[i], i);
		}
	}
}

Tokenizer::~Tokenizer()
{
	Reset();
}

void Tokenizer::SetExtraCharsAllowedInIdentifiers(char *pString)
{
	if (m_pExtraIdentifierChars)
	{
		delete m_pExtraIdentifierChars;
		m_pExtraIdentifierChars = NULL;
	}
	
	if (pString && pString[0])
	{
		m_pExtraIdentifierChars = STRDUP(pString);
	}
}

void Tokenizer::Reset()
{
	if (m_bOwnsBuffer)
	{
		delete [] m_pBufferStart;
		m_bOwnsBuffer = false;
	}
	m_bLookForControlCodeInComments = false;
	m_bInsideCommentControlCode = false;
	m_pExtraReservedWordTree = NULL;
	m_bCSourceStyleStrings = false;
	m_bDontParseInts = false;
	m_bCheckForInvisibleTokens = false;

}


void Tokenizer::LoadFromBuffer(char *pBuffer, int iSize, char *pFileName, int iStartingLineNum)
{
	

	Reset();

	STATICASSERT(pBuffer != NULL, "Can't load from NULL buffer");
	STATICASSERT(iSize > 0, "Can't load from zero-size buffer");

	m_pBufferStart = m_pReadHead = pBuffer;
	m_pBufferEnd = pBuffer + iSize;

	m_bInsideCommentControlCode = false;

	strcpy(m_CurFileName, pFileName);
	m_iStartingLineNum = iStartingLineNum;
	m_iCurLineNum = iStartingLineNum;

}

bool Tokenizer::LoadFromFile(char *pFileName)
{
	FILE *pInFile;

	Reset();

	pInFile = fopen(pFileName, "rb");
	
	if (!pInFile)
	{
		Sleep(100);

		pInFile = fopen(pFileName, "rb");
		{
			if (!pInFile)
			{
				return false;
			}
		}
	}
	

	fseek(pInFile, 0, SEEK_END);

	int iFileSize = ftell(pInFile);

	fseek(pInFile, 0, SEEK_SET);

	m_pBufferStart = new char[iFileSize + 1];

	STATICASSERT(m_pBufferStart != NULL, "new failed");

	fread(m_pBufferStart, iFileSize, 1, pInFile);

	fclose(pInFile);

	m_pBufferStart[iFileSize] = 0;

	//remove UTF-8 header if any
	if ((U8)m_pBufferStart[0] == 0xEF && (U8)m_pBufferStart[1] == 0xBB && (U8)m_pBufferStart[2] == 0xBF)
	{
		iFileSize -= 3;
		memmove(m_pBufferStart, m_pBufferStart + 3, iFileSize + 1);
	}

	m_pReadHead = m_pBufferStart;
	m_pBufferEnd = m_pBufferStart + iFileSize;

	m_bOwnsBuffer = true;


	strcpy(m_CurFileName, pFileName);
	m_iCurLineNum = 1;
	m_iStartingLineNum = 1;


	return true;

}

//if this function is called, we just hit a \, and want to return true if there is nothing but optional whitespace then a newline
bool Tokenizer::CheckForMultiLineCString()
{
	char *pStartingReadHead = m_pReadHead;

	//skip the backslash
	m_pReadHead++;

	//skip non-newline whitespace
	while ((*m_pReadHead != '\n') && (IsWhiteSpace(*m_pReadHead) || *m_pReadHead == '\r'))
	{
		m_pReadHead++;
	}

	if (*m_pReadHead == '\n')
	{
		m_iCurLineNum++;
		m_pReadHead++;
		return true;
	}

	m_pReadHead = pStartingReadHead;
	return false;
}

void Tokenizer::AdvanceToBeginningOfLine()
{
	do
	{
		m_pReadHead++;
	}
	while (*m_pReadHead && *m_pReadHead != '\n');

	m_pReadHead++;
}
void Tokenizer::BackUpToBeginningOfLine()
{
	m_pReadHead--;

	do
	{
		m_pReadHead--;
	}
	while (m_pReadHead >= m_pBufferStart && *m_pReadHead != '\n');

	m_pReadHead++;
}

bool Tokenizer::IsBeginningOfLine()
{
	char *pReadHead = m_pReadHead;

	if (pReadHead == m_pBufferStart)
	{
		return true;
	}

	while (IsWhiteSpace(*pReadHead))
		pReadHead++;

	if (*(pReadHead-1) == '\n')
	{
		return true;
	}

	return false;
}

bool Tokenizer::IsFirstNonWhitespaceCharacterInLine()
{
	char *pTemp = m_pReadHead;

	while (pTemp > m_pBufferStart && IsNonLineBreakWhiteSpace(*(pTemp-1)))
	{
		pTemp--;
	}

	if (pTemp == m_pBufferStart)
	{
		return true;
	}

	if (*(pTemp-1) == '\n')
	{
		return true;
	}

	return false;
}

bool Tokenizer::DoesLineBeginWithComment()
{
	char *pTempReadHead = m_pReadHead;

	while (IsNonLineBreakWhiteSpace(*pTempReadHead))
	{
		pTempReadHead++;
	}

	return (*pTempReadHead == '/' && *(pTempReadHead+1) == '/');
}


void Tokenizer::GetSurroundingSlashedCommentBlock(Token *pToken, bool bAllowOnePrecedingLineOfNonComments)
{
	int iLineNum, iOffset;
	int iCount = 0;
	bool bKeepGoing;

	iOffset = GetOffset(&iLineNum);

	BackUpToBeginningOfLine();

	char *pEndOfBlock = m_pReadHead;

	do
	{
		BackUpToBeginningOfLine();
		iCount++; 
		bKeepGoing = false;

		if (DoesLineBeginWithComment())
		{
			bKeepGoing = true;
		}
		else if (iCount == 1 && bAllowOnePrecedingLineOfNonComments)
		{
			bKeepGoing = true;
			pEndOfBlock = m_pReadHead;
		}
	} while (bKeepGoing);

	AdvanceToBeginningOfLine();

	int iOutLength = 0;
	bool bInsideSlashes = true;

	while (m_pReadHead < pEndOfBlock && iOutLength < TOKENIZER_MAX_STRING_LENGTH)
	{
		while (bInsideSlashes && (*m_pReadHead == '/' || IsNonLineBreakWhiteSpace(*m_pReadHead)))
		{
			m_pReadHead++;
		}

		bInsideSlashes = false;

		if (*m_pReadHead == '\n')
		{
			pToken->sVal[iOutLength] = '\n';
			iOutLength++;
			m_pReadHead++;
			bInsideSlashes = true;
		}
		else if (*m_pReadHead == '\r')
		{
			m_pReadHead++;
		}
		else
		{
			pToken->sVal[iOutLength] = *m_pReadHead;
			iOutLength++;
			m_pReadHead++;
		}
	}

	//got all preceding comments.
	AdvanceToBeginningOfLine();

	char *pBeginningOfSucceedingLines = m_pReadHead;

	while (DoesLineBeginWithComment())
	{
		AdvanceToBeginningOfLine();
	}

	char *pEndOfSucceedingLines = m_pReadHead;

	m_pReadHead = pBeginningOfSucceedingLines;

	bInsideSlashes = true;

	while (m_pReadHead < pEndOfSucceedingLines && iOutLength < TOKENIZER_MAX_STRING_LENGTH)
	{
		while (bInsideSlashes && (*m_pReadHead == '/' || IsNonLineBreakWhiteSpace(*m_pReadHead)))
		{
			m_pReadHead++;
		}

		bInsideSlashes = false;

		if (*m_pReadHead == '\n')
		{
			pToken->sVal[iOutLength] = '\n';
			iOutLength++;
			m_pReadHead++;
			bInsideSlashes = true;
		}
		else if (*m_pReadHead == '\r')
		{
			m_pReadHead++;
		}
		else
		{
			pToken->sVal[iOutLength] = *m_pReadHead;
			iOutLength++;
			m_pReadHead++;
		}
	}

	pToken->sVal[iOutLength] = 0;

	RemoveNewLinesAfterBackSlashes(pToken->sVal);
	pToken->iVal = (int)strlen(pToken->sVal);

	SetOffset(iOffset, iLineNum);
}



enumTokenType Tokenizer::MustGetNextToken(Token *pToken, char *pErrorString)
{
	enumTokenType eType = GetNextToken(pToken);

	ASSERT(eType != TOKEN_NONE, pErrorString);

	return eType;
}

char *pSimpleInvisibleTokens[] =
{
	"SA_POST_GOOD",
	"SA_POST_VALID",
	"SA_POST_FREE",
	"SA_POST_NULL",
	"SA_POST_OP_STR",
	"SA_POST_NN_STR",
	"SA_POST_OP_VALID",
	"SA_POST_OP_FREE",
	"SA_POST_OP_NULL",
	"SA_POST_NN_VALID",
	"SA_POST_NN_GOOD",
	"SA_POST_NN_FREE",
	"SA_POST_NN_NULL",
	"SA_POST_P_FREE",
	"SA_POST_P_NULL",
	"SA_POST_NN_NN_VALID",
	"SA_POST_NN_OP_VALID",
	"SA_POST_OP_OP_VALID",
	"SA_POST_OP_OP_STR",
	"SA_POST_NN_NN_STR",
	"SA_POST_NN_OP_STR",
	"FORMAT_STR",			
	NULL
};

char *pInvisibleTokensWithParensAndInt[] =
{
	"OPT_PTR_FREE_COUNT", 		
	"OPT_PTR_FREE_BYTES", 		
	"NN_PTR_FREE_COUNT",  		
	"NN_PTR_FREE_BYTES",  		
	"OPT_PTR_GOOD_COUNT",  		
	"OPT_PTR_GOOD_BYTES",  		
	"NN_PTR_GOOD_COUNT",  		
	"NN_PTR_GOOD_BYTES",  		
	"OPT_PTR_MAKE_COUNT", 		
	"OPT_PTR_MAKE_BYTES", 
	"NN_PTR_MAKE_COUNT",  	
	"NN_PTR_MAKE_BYTES",  	
	"PTR_POST_VALID_COUNT",		
	"PTR_POST_VALID_BYTES",		
	"PTR_PRE_VALID_COUNT",
	"PTR_PRE_VALID_BYTES",	
	NULL
};






//given a token, returns true, and skips any "dangling" invisible tokens, if it's invisible
bool Tokenizer::CheckIfTokenIsInvisibleAndSkipRemainder(enumTokenType eType, Token *pToken)
{

	if (eType != TOKEN_IDENTIFIER)
	{
		return false;
	}

	if (StringIsInList(pToken->sVal, pSimpleInvisibleTokens))
	{
		return true;
	}

	if (m_pAdditionalSimpleInvisibleTokens && StringIsInList(pToken->sVal, m_pAdditionalSimpleInvisibleTokens))
	{
		return true;
	}

	if (StringIsInList(pToken->sVal, pInvisibleTokensWithParensAndInt))
	{
		enumTokenType eTempType;
		Token tempToken;
		eTempType = GetNextToken_internal(&tempToken);
		ASSERT(eTempType == TOKEN_RESERVEDWORD && tempToken.iVal == RW_LEFTPARENS, "Expected (");
		GetSpecialStringTokenWithParenthesesMatching(&tempToken);
		

		return true;
	}
	return false;
}

//wrapper around GetNextToken_internal which skips over any "invisible" tokens
enumTokenType Tokenizer::GetNextToken(Token *pToken)
{
	enumTokenType eType;

	while (1)
	{
		eType = GetNextToken_internal(pToken);

		if  (!m_bCheckForInvisibleTokens || !CheckIfTokenIsInvisibleAndSkipRemainder(eType, pToken))
		{
			return eType;
		}
	}
}


enumTokenType Tokenizer::GetNextToken_internal(Token *pToken)
{
	while (1)
	{
		if (m_pReadHead >= m_pBufferEnd)
		{
			return TOKEN_NONE;
		}

		//skip white space
		if (IsWhiteSpace(*m_pReadHead))
		{
			if (*m_pReadHead == '\n')
			{
				m_bInsideCommentControlCode = false;
				m_iCurLineNum++;

			}
			m_pReadHead++;
			continue;
		}

		//skip /* */ comments
		if (*m_pReadHead == '/' && *(m_pReadHead + 1) == '*')
		{
			m_pReadHead += 2;

			while (m_pReadHead < m_pBufferEnd && !(*m_pReadHead == '*' && *(m_pReadHead + 1) == '/'))
			{
				if (*m_pReadHead == '\n')
				{
					m_iCurLineNum++;
				}

				m_pReadHead++;
			}
			m_pReadHead += 2;

			continue;
		}


		//check for #if type things
		if (m_bUseIfDefStack && IsFirstNonWhitespaceCharacterInLine() && *m_pReadHead == '#')
		{
			if (UpdateIfDefStack())
			{
				continue;
			}
		}

		//if we are inside an #if 0, zero out the rest of the line
		if (m_bUseIfDefStack && m_ifDefStack.bIncludes0)
		{
			while (m_pReadHead < m_pBufferEnd && *m_pReadHead != '\n')
			{
				*m_pReadHead = ' ';
				m_pReadHead++;
			}

			continue;
		}

		//skip // comments and #defines (if requested)
		if (*m_pReadHead == '/' && *(m_pReadHead + 1) == '/' || (m_bSkipDefines && StringBeginsWith(m_pReadHead, "#define ", true)))
		{
			m_pReadHead += 2;

			while (m_pReadHead < m_pBufferEnd && !(*m_pReadHead == '\n'))
			{
				if (*m_pReadHead == '\n')
				{
					m_iCurLineNum++;
				}

				if (m_bLookForControlCodeInComments && strncmp(m_pReadHead, sCommentControlCode, COMMENT_CONTROL_CODE_LENGTH) == 0)
				{
					m_pReadHead += COMMENT_CONTROL_CODE_LENGTH;
					m_bInsideCommentControlCode = true;
					break;
				}

				m_pReadHead++;
			}

			continue;
		}

		//read strings
		if ((*m_pReadHead == '"' || *m_pReadHead == '\'') && !m_bIgnoreQuotes)
		{
			if (m_bInsideCommentControlCode)
			{
				m_pReadHead++;
				continue;
			}

			char cQuoteType = *m_pReadHead;
			m_pReadHead++;
			int iStrLen = 0;

			while (*m_pReadHead != cQuoteType)
			{

				if (*m_pReadHead == '\n')
				{
					ASSERT(!m_bNoNewlinesInStrings, "Illegal linebreak found in string");
					m_iCurLineNum++;
				}
			
				ASSERT(iStrLen < TOKENIZER_MAX_STRING_LENGTH, "String overflow");
				ASSERT(m_pReadHead < m_pBufferEnd, "unterminated string");

				if (m_bCSourceStyleStrings)
				{
					if (*m_pReadHead == '\\')
					{	
						if (CheckForMultiLineCString())
						{
							continue;
						}
						else
						{
							pToken->sVal[iStrLen++] = *(m_pReadHead++);
						}
					}
				}
				pToken->sVal[iStrLen++] = *(m_pReadHead++);
			}

			pToken->sVal[iStrLen] = 0;

			pToken->eType = TOKEN_STRING;

			m_pReadHead++;

			m_LastStringLength = pToken->iVal = iStrLen;
			
//			printf("Found string <<%s>>\n", pToken->sVal);

			return TOKEN_STRING;
		}


		//read ints
		if (!m_bDontParseInts && FoundInt())
		{
			if (m_bInsideCommentControlCode)
			{
				m_pReadHead++;
				continue;
			}


			bool bNeg = false;
			int iVal = 0;

			while (*m_pReadHead == '-')
			{
				bNeg = !bNeg;
				m_pReadHead++;
			}

			ASSERT(IsDigit(*m_pReadHead) != 0, "Didn't find digit after minus sign");

			while (IsDigit(*m_pReadHead))
			{
				iVal *= 10;
				iVal += *m_pReadHead - '0';
				m_pReadHead++;
			}

			pToken->iVal = bNeg ? -iVal : iVal;
			pToken->eType = TOKEN_INT;
			return TOKEN_INT;
		}

		//check for two-digit reserved word
		int i;
		for (i=RW_NONE + 1; i < FIRST_SINGLECHAR_RW; i++)
		{
			if (*m_pReadHead == sReservedWords[i][0] && *(m_pReadHead + 1) == sReservedWords[i][1])
			{
				m_pReadHead++;
				m_pReadHead++;
				pToken->iVal = i;
				pToken->eType = TOKEN_RESERVEDWORD;
				return TOKEN_RESERVEDWORD;
			}
		}

		if ((i = sSingleCharReservedWordIndex[(U8)(*m_pReadHead)]))
		{
			m_pReadHead++;
			pToken->iVal = i;
			pToken->eType = TOKEN_RESERVEDWORD;
			return TOKEN_RESERVEDWORD;
		}

#if 0
		//check for single-digit reserved word
		for (i=FIRST_SINGLECHAR_RW; i < FIRST_MULTICHAR_RW; i++)
		{
			if (*m_pReadHead == sReservedWords[i][0])
			{
				m_pReadHead++;
				pToken->iVal = i;
				pToken->eType = TOKEN_RESERVEDWORD;
				return TOKEN_RESERVEDWORD;
			}
		}
#endif

		//read "normal" token
		if (IsOKForIdentStart(*m_pReadHead) || m_bDontParseInts && IsDigit(*m_pReadHead))
		{
			int iIdentifierLength = 0;
			int iReservedID;

			while (IsOKForIdent(*m_pReadHead))
			{
				ASSERT(iIdentifierLength < TOKENIZER_MAX_STRING_LENGTH, "Identifier too long");

				pToken->sVal[iIdentifierLength++] = *(m_pReadHead++);
			}
			
			pToken->sVal[iIdentifierLength] = 0;

			if ((iReservedID = StringTree_CheckWord(m_pReservedWordTree, pToken->sVal)))
			{
				pToken->iVal = iReservedID;
				pToken->eType = TOKEN_RESERVEDWORD;
				return TOKEN_RESERVEDWORD;
			}

			if (m_pExtraReservedWordTree)
			{
				if ((iReservedID = StringTree_CheckWord(m_pExtraReservedWordTree, pToken->sVal)))
				{
					pToken->iVal = iReservedID;
					pToken->eType = TOKEN_RESERVEDWORD;
					return TOKEN_RESERVEDWORD;
				}
			}

			m_LastStringLength = iIdentifierLength;


			pToken->eType = TOKEN_IDENTIFIER;
			pToken->iVal = iIdentifierLength;
			return TOKEN_IDENTIFIER;
		}

		//ignore unrecognized characters
		m_pReadHead++;
	}
}

enumTokenType Tokenizer::CheckNextToken(Token *pToken)
{
	char *pReadHead = m_pReadHead;
	int iLineNum = m_iCurLineNum;

	enumTokenType eType = GetNextToken(pToken);

	m_iCurLineNum = iLineNum;
	m_pReadHead = pReadHead;

	return eType;
}

void Tokenizer::AssertNextTokenTypeAndGet(Token *pToken, enumTokenType eExpectedType, int iExpectedAuxType, char *pErrorString)
{
	enumTokenType eType;

	eType = GetNextToken(pToken);

	ASSERT(eType == eExpectedType && (eType != TOKEN_RESERVEDWORD || iExpectedAuxType == pToken->iVal), pErrorString);

	if ((eType == TOKEN_STRING || eType == TOKEN_IDENTIFIER) && iExpectedAuxType)
	{
		ASSERT(GetLastStringLength() < iExpectedAuxType - 1, pErrorString);
	}
}

void Tokenizer::AssertfNextTokenTypeAndGet(Token *pToken, enumTokenType eType, int iAuxType, char *pErrorString, ...)
{
	char buf[1000] = "";
	va_list ap;

	va_start(ap, pErrorString);
	if (pErrorString)
	{
		vsprintf(buf, pErrorString, ap);
	}
	va_end(ap);

	AssertNextTokenTypeAndGet(pToken, eType, iAuxType, buf);
}

enumTokenType Tokenizer::Assertf2NextTokenTypesAndGet(Token *pToken, enumTokenType eType1, int iAuxType1, enumTokenType eType2, int iAuxType2, char *pErrorString, ...)
{
	char buf[1000] = "";
	va_list ap;

	va_start(ap, pErrorString);
	if (pErrorString)
	{
		vsprintf(buf, pErrorString, ap);
	}
	va_end(ap);

	return Assert2NextTokenTypesAndGet(pToken, eType1, iAuxType1, eType2, iAuxType2, buf);
}


enumTokenType Tokenizer::Assert2NextTokenTypesAndGet(Token *pToken, enumTokenType eExpectedType1, int iExpectedAuxType1, enumTokenType eExpectedType2, int iExpectedAuxType2, char *pErrorString)
{
	enumTokenType eType;

	eType = GetNextToken(pToken);

	ASSERT(eType == eExpectedType1 && (eType != TOKEN_RESERVEDWORD || iExpectedAuxType1 == pToken->iVal)
		|| eType == eExpectedType2 && (eType != TOKEN_RESERVEDWORD || iExpectedAuxType2 == pToken->iVal), pErrorString);

	if ((eType == TOKEN_STRING || eType == TOKEN_IDENTIFIER) && eType == eExpectedType1 && iExpectedAuxType1)
	{
		ASSERT(GetLastStringLength() < iExpectedAuxType1, pErrorString);
	}
	
	if ((eType == TOKEN_STRING || eType == TOKEN_IDENTIFIER) && eType == eExpectedType2 && iExpectedAuxType2)
	{
		ASSERT(GetLastStringLength() < iExpectedAuxType2, pErrorString);
	}

	return eType;
}
void Tokenizer::AssertGetIdentifier(char *pIdentToGet)
{
	enumTokenType eType;
	Token token;
	char errorString[TOKENIZER_MAX_STRING_LENGTH];

	sprintf(errorString, "Expected %s", pIdentToGet);

	eType = GetNextToken(&token);

	ASSERT(eType == TOKEN_IDENTIFIER, errorString);
	ASSERT(strcmp(pIdentToGet, token.sVal) == 0, errorString);
}



bool Tokenizer::IsStringAtVeryEndOfBuffer(char *pString)
{
	int len = (int)strlen(pString);

	char *pTemp = m_pBufferEnd;

	while (!IsAlphaNum(*pTemp))
	{
		pTemp--;
	}

	return StringBeginsWith(pTemp - len + 1, pString, true);
}


void Tokenizer::AssertFailedf(char *pErrorString, ...)
{
	char buf[1000] = "";
	va_list ap;

	va_start(ap, pErrorString);
	if (pErrorString)
	{
		vsprintf(buf, pErrorString, ap);
	}
	va_end(ap);

	AssertFailed(buf);
}

#define NUM_ASSERT_CHARACTERS_TO_DUMP 25
void Tokenizer::AssertFailed(char *pErrorString)
{

	printf("%s(%d) : error S0000 : (StructParser) %s\n", m_CurFileName, m_iCurLineNum, pErrorString);
	printf("String Before Error:<<<");


	char *pTemp;

	for (pTemp = m_pReadHead - NUM_ASSERT_CHARACTERS_TO_DUMP; pTemp < m_pReadHead;  pTemp++)
	{
		if (pTemp >= m_pBufferStart)
		{
			printf("%c", *pTemp);
		}
	}

	printf(">>>\n\n\n");

	fflush(stdout);
	BreakIfInDebugger();

	Sleep(100);

	exit(1);

}

void Tokenizer::StaticAssertFailed(char *pErrorString)
{

	printf("ERROR:%s\n", pErrorString);


	fflush(stdout);
	BreakIfInDebugger();

	Sleep(100);

	exit(1);
	
}

void Tokenizer::StaticAssertFailedf(char *pErrorString, ...)
{
	char buf[1000] = "";
	va_list ap;

	va_start(ap, pErrorString);
	if (pErrorString)
	{
		vsprintf(buf, pErrorString, ap);
	}
	va_end(ap);

	StaticAssertFailed(buf);
}




void Tokenizer::DumpToken(Token *pToken)
{
	switch(pToken->eType)
	{
	case TOKEN_INT:
		printf("INT %d\n", pToken->iVal);
		break;

	case TOKEN_RESERVEDWORD:
		printf("RW %s\n", sReservedWords[pToken->iVal]);
		break;

	case TOKEN_IDENTIFIER:
		printf("IDENTIFIER %s\n", pToken->sVal);
		break;
	}
}

bool Tokenizer::FoundInt()
{
	char *pTemp = m_pReadHead;

	while (*pTemp == '-')
	{
		pTemp++;
	}

	if (IsDigit(*pTemp))
	{
		return true;
	}

	return false;
}

void Tokenizer::GetTokensUntilReservedWord(enumReservedWordType eReservedWord)
{
	Token token;
	enumTokenType eType;

	do
	{
		eType = GetNextToken(&token);

		ASSERT(eType != TOKEN_NONE, "Found EOF while looking for reserved word");
	} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == eReservedWord));
}

void Tokenizer::StringifyToken(Token *pToken)
{
	switch (pToken->eType)
	{
	case TOKEN_INT:
		sprintf(pToken->sVal, "%d", pToken->iVal);
		break;

	case TOKEN_RESERVEDWORD:
		if (pToken->iVal >= RW_COUNT)
		{
			strcpy(pToken->sVal, m_ppExtraReservedWords[pToken->iVal - RW_COUNT]);
		}
		else
		{
			strcpy(pToken->sVal, sReservedWords[pToken->iVal]);
		}
		break;
	}
}

//finds the next unmatched right parentheses (using normal token logic). Then returns every character from the current read head up until that
//character as a string, even if there are no quotes or anything
void Tokenizer::GetSpecialStringTokenWithParenthesesMatching(Token *pToken)
{
	char *pStartingReadHead = m_pReadHead;
	int iCurDepth = 0;
	
	Token token;
	enumTokenType eType;

	do
	{
		eType = GetNextToken(&token);

		ASSERT(eType != TOKEN_NONE, "Never found matching ) during special token search");

		if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_RIGHTPARENS)
		{
			if (iCurDepth == 0)
			{
				pToken->eType = TOKEN_STRING;
				int iLen = (int)((m_pReadHead - pStartingReadHead) - 1);

				ASSERT(iLen < TOKENIZER_MAX_STRING_LENGTH, "Never found matching ) during special token search, or buffer overflowed");
				memcpy(pToken->sVal, pStartingReadHead, iLen);
				pToken->sVal[iLen] = 0;

				pToken->iVal = iLen;

				return;
			}
			else
			{
				iCurDepth--;
			}
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_LEFTPARENS)
		{
			iCurDepth++;
		}
	} while (1);
}

void Tokenizer::ResetLineNumberCounter(void)
{
	m_iCurLineNum = m_iStartingLineNum;
	char *pCounter;

	for (pCounter = m_pBufferStart; pCounter < m_pReadHead; pCounter++)
	{
		if (*pCounter == '\n')
		{
			m_iCurLineNum++;
		}
	}
}


bool Tokenizer::SetReadHeadAfterString(char *pString)
{
	char *pFoundString = strstr(m_pBufferStart, pString);

	if (pFoundString)
	{
		m_pReadHead = pFoundString + strlen(pString);
		ResetLineNumberCounter();
		return true;
	}
	else
	{
		return false;
	}
}

void Tokenizer::GetSimpleBracketedString(char beginChar, char endChar, char *pDestBuf, int iDestBufSize)
{
	char *pBegin; 

	while (*m_pReadHead && *m_pReadHead != beginChar)
	{
		m_pReadHead++;
	}

	ASSERT(*m_pReadHead, "Unexpected end of file during GetSimpleBracketedString");

	pBegin = m_pReadHead;

	while (*m_pReadHead && *m_pReadHead != endChar)
	{
		m_pReadHead++;
	}

	ASSERT(*m_pReadHead, "Unexpected end of file during GetSimpleBracketedString");


	m_pReadHead++;

	strncpy_s(pDestBuf, iDestBufSize, pBegin, m_pReadHead - pBegin);
}

void Tokenizer::AssertGetBracketBalancedBlock(enumReservedWordType leftRW, enumReservedWordType rightRW, char *pErrorString, char *pOutString, int iOutStringSize)
{
	Token token;
	enumTokenType eType;
	int iDepth = 1;
	char *pStartingReadHead = m_pReadHead;

	AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, leftRW, pErrorString);

	do
	{
		eType = GetNextToken(&token);

		ASSERT(eType != TOKEN_NONE, pErrorString);

		if (eType == TOKEN_RESERVEDWORD && token.iVal == leftRW)
		{
			iDepth++;
		}
		else if (eType == TOKEN_RESERVEDWORD && token.iVal == rightRW)
		{
			iDepth--;
		}
	}
	while (iDepth > 0);

	if (pOutString)
	{
		char *pNewReadHead = m_pReadHead;

		while (IsWhiteSpace(*pStartingReadHead))
		{
			pStartingReadHead++;
		}

		pNewReadHead--;
		while (IsWhiteSpace(*pNewReadHead))
		{
			pNewReadHead--;
		}


		

		strncpy_s(pOutString, iOutStringSize, pStartingReadHead, pNewReadHead - pStartingReadHead + 1);


	}
}

int Tokenizer::GetCurBraceDepth(void)
{
	int iCurOffset;
	int iCurLineNum;

	int iDepth = 0;

	enumTokenType eType;
	Token token;

	iCurOffset = GetOffset(&iCurLineNum);

	char *pStartingReadHead = m_pReadHead;

	m_pReadHead = m_pBufferStart;

	while (m_pReadHead  < pStartingReadHead)
	{
		eType = GetNextToken(&token);

		if (eType == TOKEN_RESERVEDWORD)
		{
			if (token.iVal == RW_RIGHTBRACE)
			{
				iDepth--;
			} 
			else if (token.iVal == RW_LEFTBRACE)
			{
				iDepth++;
			}
		}
	}

	SetOffset(iCurOffset, iCurLineNum);

	return iDepth;
}

bool Tokenizer::SimpleFind(char *pWord)
{
	return SimpleFind(pWord, "");
}

bool Tokenizer::SimpleFind(char *pWord, char *pListOfDividers)
{
	int iLen = (int)strlen(pWord);

	if (StringBeginsWith(m_pReadHead, pWord, true) && (IsWhiteSpace(m_pReadHead[iLen]) || strchr(pListOfDividers, m_pReadHead[iLen])))
	{
//		printf("Found %s, line %d\n", pWord, m_iCurLineNum);
		m_pReadHead += iLen;
		return true;
	}

	return false;
}


#define EAT_LINE_AND_RETURN_TRUE RecalcIfStackIncludes0(&m_ifDefStack); while (*m_pReadHead && (*m_pReadHead) != '\n') m_pReadHead++; memset(pStartingReadHead, ' ', m_pReadHead - pStartingReadHead); if (*m_pReadHead) m_pReadHead++; m_iCurLineNum++; return true;


//called when the current readhead points to a line beginning with #, returns true if a recognized
//preprocessor macro was called, updates the define stack, eats the line
bool Tokenizer::UpdateIfDefStack(void)
{
	Token token, nextToken;
	enumTokenType eType, eNextType;

	char *pStartingReadHead = m_pReadHead;

	if (SimpleFind("#endif"))
	{
		ASSERT(m_ifDefStack.iNumIfDefs > 0, "Non-matching #endif found");
		m_ifDefStack.iNumIfDefs--;

		while (m_ifDefStack.iNumIfDefs > 0 && m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs-1].bPopThisWhenParentPops)
		{
			m_ifDefStack.iNumIfDefs--;
		}


		EAT_LINE_AND_RETURN_TRUE;
	}

	if (SimpleFind("#if", "(!"))
	{
		ASSERT(m_ifDefStack.iNumIfDefs < MAX_IFDEF_DEPTH, "#if nesting depth overflow");
		SingleIfDef *pIfDef = &m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs++];
		memset(pIfDef, 0, sizeof(SingleIfDef));

		SetUsesIfDefStack(false);

		while (1)
		{
			eType = GetNextToken(&token);

			while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_NOT)
			{
				pIfDef->bNot = !pIfDef->bNot;
				eType = GetNextToken(&token);
			}

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "defined") == 0)
			{
				pIfDef->bDefine = true;
				eType = GetNextToken(&token);
			}

			switch (eType)
			{
			case TOKEN_NONE:
				ASSERT(0, "End of file in the middle of #if");
			case TOKEN_INT:
				StringifyToken(&token);
				strcpy(pIfDef->defineName, token.sVal);
				SetUsesIfDefStack(true);
				EAT_LINE_AND_RETURN_TRUE;

			case TOKEN_IDENTIFIER:
				if (m_pNonIntIfDefError)
				{
					printf("BAD_IFDEF(%s) %s(%d)\n", m_pNonIntIfDefError, m_CurFileName, m_iCurLineNum);
				}	

				ASSERT(token.iVal < MAX_IFDEF_LENGTH, "#if string too long");
				strcpy(pIfDef->defineName, token.sVal);

				//if we have 2 ampersands next, then repeat the loop
				eNextType = CheckNextToken(&nextToken);
				if (eNextType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_AMPERSAND)
				{
					eNextType = GetNextToken(&nextToken);
					eNextType = CheckNextToken(&nextToken);
					if (eNextType == TOKEN_RESERVEDWORD && nextToken.iVal == RW_AMPERSAND)
					{
						pIfDef->bPopThisWhenParentPops = true;
						eNextType = GetNextToken(&nextToken);
						ASSERT(m_ifDefStack.iNumIfDefs < MAX_IFDEF_DEPTH, "#if nesting depth overflow");
						pIfDef = &m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs++];
						memset(pIfDef, 0, sizeof(SingleIfDef));
						break; // out of switch statement, continue with WHILE loop
					}
				}

			
				SetUsesIfDefStack(true);
				EAT_LINE_AND_RETURN_TRUE;	

			default:
				sprintf(pIfDef->defineName, "Non-parsable compound ifdef");
				SetUsesIfDefStack(true);
				EAT_LINE_AND_RETURN_TRUE;
			}
		}
	}
			
	if (SimpleFind("#ifdef", "("))
	{
		ASSERT(m_ifDefStack.iNumIfDefs < MAX_IFDEF_DEPTH, "#if nesting depth overflow");
		SingleIfDef *pIfDef = &m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs++];
		memset(pIfDef, 0, sizeof(SingleIfDef));
		pIfDef->bDefine = true;

	
		SetUsesIfDefStack(false);
		eType = GetNextToken(&token);
		SetUsesIfDefStack(true);

		switch (eType)
		{
		case TOKEN_NONE:
			ASSERT(0, "End of file in the middle of #ifdef");

		case TOKEN_IDENTIFIER:
			if (m_pNonIntIfDefError)
			{
				printf("BAD_IFDEF(%s) %s(%d)\n",m_pNonIntIfDefError, m_CurFileName, m_iCurLineNum);
			}	
		
			ASSERT(token.iVal < MAX_IFDEF_LENGTH, "#ifdef string too long");
			strcpy(pIfDef->defineName, token.sVal);
			EAT_LINE_AND_RETURN_TRUE;

		default:
			ASSERT(0, "Unexpected token type found after #ifdef");
		}
	}
			
	if (SimpleFind("#ifndef", "("))
	{
		ASSERT(m_ifDefStack.iNumIfDefs < MAX_IFDEF_DEPTH, "#if nesting depth overflow");
		SingleIfDef *pIfDef = &m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs++];
		memset(pIfDef, 0, sizeof(SingleIfDef));
		pIfDef->bDefine = true;
		pIfDef->bNot = true;

		SetUsesIfDefStack(false);
		eType = GetNextToken(&token);
		SetUsesIfDefStack(true);

		switch (eType)
		{
		case TOKEN_NONE:
			ASSERT(0, "End of file in the middle of #ifdef");

		case TOKEN_IDENTIFIER:
			if (m_pNonIntIfDefError)
			{
				printf("BAD_IFDEF(%s) %s(%d)\n", m_pNonIntIfDefError, m_CurFileName, m_iCurLineNum);
			}				
			ASSERT(token.iVal < MAX_IFDEF_LENGTH, "#ifdef string too long");
			strcpy(pIfDef->defineName, token.sVal);
			EAT_LINE_AND_RETURN_TRUE;

		default:
			ASSERT(0, "Unexpected token type found after #ifdef");
		}
	}

	if (SimpleFind("#else"))
	{
		ASSERT(m_ifDefStack.iNumIfDefs > 0, "Non-matching #else found");
		m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs-1].bNot = !m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs-1].bNot;
		EAT_LINE_AND_RETURN_TRUE;
	
	}

	if (SimpleFind("#elif", "!("))
	{
		ASSERT(m_ifDefStack.iNumIfDefs > 0 && m_ifDefStack.iNumIfDefs <  MAX_IFDEF_DEPTH, "Non-matching or overdeep #elif found");
		m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs-1].bNot = !m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs-1].bNot;
		m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs-1].bPopThisWhenParentPops = true;

		SingleIfDef *pIfDef = &m_ifDefStack.ifDefs[m_ifDefStack.iNumIfDefs++];
		memset(pIfDef, 0, sizeof(SingleIfDef));

		SetUsesIfDefStack(false);
		eType = GetNextToken(&token);

		while (eType == TOKEN_RESERVEDWORD && token.iVal == RW_NOT)
		{
			pIfDef->bNot = !pIfDef->bNot;
			eType = GetNextToken(&token);
		}

		if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "define") == 0)
		{
			if (m_pNonIntIfDefError)
			{
				printf("(%s) %s(%d)\n", m_pNonIntIfDefError, m_CurFileName, m_iCurLineNum);
			}		
			pIfDef->bDefine = true;
			eType = GetNextToken(&token);
		}
		SetUsesIfDefStack(true);

		switch (eType)
		{
		case TOKEN_NONE:
			ASSERT(0, "End of file in the middle of #elif");
		case TOKEN_INT:
			StringifyToken(&token);
			strcpy(pIfDef->defineName, token.sVal);
			EAT_LINE_AND_RETURN_TRUE;

		case TOKEN_IDENTIFIER:
			ASSERT(token.iVal < MAX_IFDEF_LENGTH, "#elif string too long");
			strcpy(pIfDef->defineName, token.sVal);
			EAT_LINE_AND_RETURN_TRUE;

		default:
			ASSERT(0, "Unexpected token type found after #elif");
		}
	}


			


	return false;

}


void DestroyIfDefStack(IfDefStack *pStack)
{
	delete(pStack);
}

IfDefStack *CopyIfDefStack(IfDefStack *pInStack)
{
	if (!pInStack || pInStack->iNumIfDefs == 0)
	{
		return NULL;
	}

	IfDefStack *pRetVal = new IfDefStack;
	memcpy(pRetVal, pInStack, sizeof(IfDefStack));

	return pRetVal;
}


IfDefStack *ReadIfDefStackFromFile(Tokenizer *pTokenizer)
{
	Token token;
	int i;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Couldn't read number of items in ifdef stack");
	if (token.iVal == 0)
	{
		return NULL;
	}

	IfDefStack *pStack = new IfDefStack;
	memset(pStack, 0, sizeof(IfDefStack));

	pStack->iNumIfDefs = token.iVal;

	for (i=0; i < pStack->iNumIfDefs; i++)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Couldn't read ifdef bDefines");
		pStack->ifDefs[i].bDefine = !!token.iVal;

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Couldn't read ifdef bNot");
		pStack->ifDefs[i].bNot = !!token.iVal;

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_IFDEF_LENGTH, "Couldn't read ifdef name");
		strcpy(pStack->ifDefs[i].defineName, token.sVal);
	}

	RecalcIfStackIncludes0(pStack);

	return pStack;
}



void WriteIfDefStackToFile(FILE *pFile, IfDefStack *pStack)
{
	int i;
	
	if (!pStack)
	{
		fprintf(pFile, " 0 ");
		return;
	}

	fprintf(pFile, " %d ", pStack->iNumIfDefs);

	for (i=0; i < pStack->iNumIfDefs; i++)
	{
		fprintf(pFile, "%d %d \"%s\" ", pStack->ifDefs[i].bDefine, pStack->ifDefs[i].bNot, pStack->ifDefs[i].defineName);
	}

}

void RecalcIfStackIncludes0(IfDefStack *pStack)
{
	int i;

	for (i=0; i < pStack->iNumIfDefs; i++)
	{
		if (!pStack->ifDefs[i].bDefine && !pStack->ifDefs[i].bNot && strcmp(pStack->ifDefs[i].defineName, "0") == 0)
		{
			pStack->bIncludes0 = true;
			return;
		}

		if (!pStack->ifDefs[i].bDefine && pStack->ifDefs[i].bNot && strcmp(pStack->ifDefs[i].defineName, "0") != 0 && isInt(pStack->ifDefs[i].defineName))
		{
			pStack->bIncludes0 = true;
			return;
		}
	}

	pStack->bIncludes0 = false;
}

int Tokenizer::AssertGetInt(void)
{
	Token token;
	AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Expected an int");
	return token.iVal;
}

void Tokenizer::AssertGetString(char *pBuf, int iBufSize)
{
	Token token;
	AssertNextTokenTypeAndGet(&token, TOKEN_STRING, iBufSize, "Expected a string");
	strcpy(pBuf, token.sVal);
}

void Tokenizer::SetExtraReservedWords(char **ppWords, StringTree **ppTree)
{
	if (!ppWords)
	{
		m_pExtraReservedWordTree = NULL;
		m_ppExtraReservedWords = NULL;
		return;
	}



	if (!*ppTree)
	{
		*ppTree = StringTree_CreateFromList(ppWords, RW_COUNT, NULL);
	}

	m_pExtraReservedWordTree = *ppTree;
	m_ppExtraReservedWords = ppWords;
}

void Tokenizer::SetNonIntIfdefErrorString(char *pString)
{
	if (m_pNonIntIfDefError)
	{
		free(m_pNonIntIfDefError);
	}

	if (pString)
	{
		m_pNonIntIfDefError = _strdup(pString);
	}
	else
	{
		m_pNonIntIfDefError = NULL;
	}
}

void Tokenizer::AssertGetParenthesizedCommaSeparatedList(char *pComment, char ***pppOutEarray)
{
	Token token;
	AssertfNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "While %s, didn't find (", pComment);
	AssertfNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "While %s, didn't find identifier", pComment);

	while (1)
	{
		eaPush(pppOutEarray, _strdup(token.sVal));
		Assertf2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "While %s, expected ) or , after identifier", pComment);
		if (token.iVal == RW_RIGHTPARENS)
		{
			return;
		}
		AssertfNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "While %s, didn't find identifier", pComment);
	}
}


	
