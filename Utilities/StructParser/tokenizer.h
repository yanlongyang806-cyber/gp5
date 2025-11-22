#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

#include "assert.h"
#include "windows.h"
#include "strutils.h"

#define MAX_IFDEF_LENGTH 256




//when a tokenizer is processing #ifdefs and stuff, it fills in and updates this struct
typedef struct 
{
	char defineName[MAX_IFDEF_LENGTH];
	bool bNot;
	bool bDefine; //if true, then this is an #ifdef or #if define. Otherwise this is an #if
	bool bPopThisWhenParentPops; //if true, then this was the first half of an #elif, and will get ended with the same #endif

} SingleIfDef;

#define MAX_IFDEF_DEPTH 32

typedef struct
{
	int iNumIfDefs;
	SingleIfDef ifDefs[MAX_IFDEF_DEPTH];
	bool bIncludes0; //if true, then "#if 0" or "if !1" is in the current stack. Should always be up to date
} IfDefStack;





#define TOKENIZER_MAX_STRING_LENGTH 16384

typedef enum
{
	TOKEN_NONE,
	TOKEN_INT,
	TOKEN_RESERVEDWORD,
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
} enumTokenType;

typedef enum
{
	RW_NONE,

	//first come two-character punctuation
	RW_ARROW,

	//then one-character punctuation
	RW_LEFTBRACE,
	RW_RIGHTBRACE,
	RW_LEFTPARENS,
	RW_RIGHTPARENS,
	RW_LEFTBRACKET,
	RW_RIGHTBRACKET,
	RW_COMMA,
	RW_COLON,
	RW_MINUS,
	RW_SEMICOLON,
	RW_PLUS,
	RW_SLASH,
	RW_NOT,
	RW_AMPERSAND,
	RW_ASTERISK,
	RW_CARET,
	RW_EQUALS,
	RW_DOT,
	RW_GT,
	RW_LT,
	RW_QM,
	RW_TILDE,
	RW_OR,

	//then words
	RW_STRUCT,
	RW_TYPEDEF,
	RW_VOID,
	RW_ENUM,
	RW_IF,

	RW_COUNT,
} enumReservedWordType;

#define FIRST_SINGLECHAR_RW RW_LEFTBRACE
#define FIRST_MULTICHAR_RW RW_STRUCT

#define ASSERT(pTokenizer, condition, expression) { if (!(condition)) (pTokenizer)->AssertFailed(expression); }
#define ASSERTF(pTokenizer, condition, expression, ...) { if (!(condition)) (pTokenizer)->AssertFailedf(expression, __VA_ARGS__); }

#define STATICASSERT(condition, expression) { if (!(condition)) Tokenizer::StaticAssertFailed(expression); }
#define STATICASSERTF(condition, expression, ...) { if (!(condition)) Tokenizer::StaticAssertFailedf(expression, __VA_ARGS__); }


typedef struct
{
	enumTokenType eType;
	int iVal;
	char sVal[TOKENIZER_MAX_STRING_LENGTH + 1];
} Token;

class Tokenizer
{
public:
	Tokenizer();
	~Tokenizer();

	enumTokenType GetNextToken(Token *pToken);
	enumTokenType CheckNextToken(Token *pToken);

	//like GetNextToken, but asserts on TOKEN_NONE
	enumTokenType MustGetNextToken(Token *pToken, char *pErrorString);

	//iAuxType is the reserved word to find. It's also the maximum string length for string/identifier types (strlen must be < )
	void AssertNextTokenTypeAndGet(Token *pToken, enumTokenType eType, int iAuxType, char *pErrorString);

	enumTokenType Assert2NextTokenTypesAndGet(Token *pToken, enumTokenType eType1, int iAuxType1, enumTokenType eType2, int iAuxType2, char *pErrorString);

	void AssertfNextTokenTypeAndGet(Token *pToken, enumTokenType eType, int iAuxType, char *pErrorString, ...);

	enumTokenType Assertf2NextTokenTypesAndGet(Token *pToken, enumTokenType eType1, int iAuxType1, enumTokenType eType2, int iAuxType2, char *pErrorString, ...);

	//eat all the tokens that start with an open bracket and end with a close bracket. Bracketing chars are passed in
	//(presumably [ ] ( ) or { }
	void AssertGetBracketBalancedBlock(enumReservedWordType leftRW, enumReservedWordType rightRW, char *pErrorString, char *pDestBuf, int iDestBufSize);

	//scans until it finds the first char. Then scans until it finds the second char. Copies from the first through the last, inclusive, into destbuf. Updates readHead.
	void GetSimpleBracketedString(char beginChar, char endChar, char *pDestBuf, int iDestBufSize);

	void AssertGetIdentifier(char *pIdentToGet);


	void LoadFromBuffer(char *pBuffer, int iSize, char *pFileName, int iStartingLineNum);

	void DumpToken(Token *pToken);

	void SetLookForControlCodeInComments(bool bSet) { m_bLookForControlCodeInComments = bSet; }

	bool IsInsideCommentControlCode() { return m_bInsideCommentControlCode; }

	void AssertFailed(char *pErrorString);
	void AssertFailedf(char *pErrorString, ...);
	static void StaticAssertFailed(char *pErrorString);
	static void StaticAssertFailedf(char *pErrorString, ...);

	void SaveLocation() { m_pSavedReadHead = m_pReadHead; m_iSavedLineNum = m_iCurLineNum;}
	void RestoreLocation() { m_pReadHead = m_pSavedReadHead; m_iCurLineNum = m_iSavedLineNum; }

	//sets the tree the first time it's called, which should be passed back in in the future for best performance
	void SetExtraReservedWords(char **ppWords, StringTree **pTree);

	bool LoadFromFile(char *pFileName);

	void SetCSourceStyleStrings(bool bSet) { m_bCSourceStyleStrings = bSet; }

	bool IsStringAtVeryEndOfBuffer(char *pString);

	int GetLastStringLength() { return m_LastStringLength; }; //returns the length of the last string token found (to avoid wasted time)

	int GetOffset(int *pLineNum) { if (pLineNum) { *pLineNum = m_iCurLineNum; } return (int)(m_pReadHead - m_pBufferStart); }
	void SetOffset(int iOffset, int iLineNum) { m_pReadHead = m_pBufferStart + iOffset; m_iCurLineNum = iLineNum; }

	//keeps getting tokens until it has gotten the specified reserved word. Asserts on EOF.
	void GetTokensUntilReservedWord(enumReservedWordType eReservedWord);

	//if the token doesn't already have an sval, sets one (via sprintfing the ival into it, or whatever is type-appropriate)
	void StringifyToken(Token *pToken);

	char *GetCurFileName() { return m_CurFileName; }
	char *GetCurFileName_NoDirs() { return GetFileNameWithoutDirectories(m_CurFileName) + 1; }
	int GetCurLineNum() { return m_iCurLineNum; }

//finds the next unmatched right parentheses (using normal token logic). Then returns every character from the current read head up until that
//character as a string, even if there are no quotes or anything
	void GetSpecialStringTokenWithParenthesesMatching(Token *pToken);

	void SetNoNewlinesInStrings(bool bSet) { m_bNoNewlinesInStrings = bSet; }

	void GetSurroundingSlashedCommentBlock(Token *pToken, bool bAllowOnePrecedingLineOfNonComments);

	void SetDontParseInts(bool bSet) { m_bDontParseInts = bSet; }

	//searches the entire loaded buffer for the given string, if it finds it, sets the read head
	//right after it and returns true. Otherwise returns false. 
	bool SetReadHeadAfterString(char *pString);

	//gets a pointer to the current read head. useful if you need to do totally custom parsing of some sort
	char *GetReadHead() { return m_pReadHead; }

	//if true, the tokenizer skips entirely over #define and the rest of that line
	void SetSkipDefines(bool bSet) { m_bSkipDefines = bSet; }

	//if true, the tokenizer tracks what #ifdefs it is inside of
	void SetUsesIfDefStack(bool bSet) { m_bUseIfDefStack = bSet; }

	//sets characters which temporarily count as OK for identifiers (so, for instance,
	//you can say that for a certainly application, this:is:one:identifier would in fact be one identifier
	//
	//NULL or "" to clear
	void SetExtraCharsAllowedInIdentifiers(char *pStringOfChars);

	//backs the read head up to the beginning of the current line of text
	void BackUpToBeginningOfLine();

	//returns true if the character we are about to read is the first character of a new line
	//this skips whitespace at the end of the line until it gets to non-whitespace
	bool IsBeginningOfLine();

	//returns true if the character we are about to read is the first non-whitespace character of a new line
	bool IsFirstNonWhitespaceCharacterInLine();

	//counts the { and } from the beginning of the file, returns the current depth
	int GetCurBraceDepth();

	void SetIgnoreQuotes(bool bSet) { m_bIgnoreQuotes = bSet; }

	//gets a literal int and returns its value. Asserts otherwise
	int AssertGetInt();

	//gets a literal string and puts it into the provided buffer. Asserts otherwise, or if the string is too big
	void AssertGetString(char *pBuf, int iBufSize);

	void AdvanceToBeginningOfLine();

	//invisible tokens are the various annotation things like __OPT_PTR_GOOD and stuff. Files which aren't
	//source files should not bother checking for them, for a minor performance boost
	void SetCheckForInvisibleTokens(bool bSet) { m_bCheckForInvisibleTokens = bSet; }

	//turns on the mode where it's illegal to have ifdefs of anything other than ints with the specified error string,
	//NULL to turn it back off
	void SetNonIntIfdefErrorString(char *pString);

	//add additional invis tokens to be skipped - used by GetNextToken internally
	void SetAdditionalSimpleInvisibleTokens(char **pAdditionalSimpleInvisibleTokens) { m_pAdditionalSimpleInvisibleTokens = pAdditionalSimpleInvisibleTokens; }

	//expects something like (foo, bar, wakka) with at least one item in it... allocs them and puts them in an earray
	void AssertGetParenthesizedCommaSeparatedList(char *pComment, char ***pppOutEarray);

private:
	bool m_bLookForControlCodeInComments;
	char *m_pBufferStart;
	char *m_pBufferEnd;
	char *m_pReadHead;
	
	bool m_bInsideCommentControlCode;

	char *m_pSavedReadHead;


	bool m_bOwnsBuffer;

	bool m_bCSourceStyleStrings;

	int m_LastStringLength;

	char m_CurFileName[MAX_PATH];
	int m_iCurLineNum;
	int m_iSavedLineNum;
	int m_iStartingLineNum;

	bool m_bNoNewlinesInStrings;

	bool m_bDontParseInts;

	bool m_bSkipDefines;


	char *m_pExtraIdentifierChars;

	//if true, " and ' will be skipped over, result in no TOKEN_STRINGs ever being returned
	bool m_bIgnoreQuotes;

	IfDefStack m_ifDefStack;
	bool m_bUseIfDefStack;

	bool m_bCheckForInvisibleTokens;

	static StringTree *m_pReservedWordTree;

	StringTree *m_pExtraReservedWordTree;
	char **m_ppExtraReservedWords;

	//if set, then encountering a non-int #if or #ifdef is a fatal error with this string
	char *m_pNonIntIfDefError;
	char **m_pAdditionalSimpleInvisibleTokens;

private: //funcs
	bool FoundInt();
	void Reset();

	//if this function is called, we just hit a \, and want to return true if there is nothing but optional whitespace then a newline
	bool CheckForMultiLineCString();
	bool DoesLineBeginWithComment();
	void ResetLineNumberCounter(void);
	enumTokenType GetNextToken_internal(Token *pToken);
	bool CheckIfTokenIsInvisibleAndSkipRemainder(enumTokenType eType, Token *pToken);
	bool SimpleFind(char *pWord);
	bool SimpleFind(char *pWord, char *pListOfDividers);

	//reads in the #ifdef or #endif or #else or #if that might start a line. If one does, then
	//ERASES THAT LINE FROM THE BUFFER (to avoid corruption when re-reading a token, for instance)
	//and eats the line. There is some potential for this to cause bad behavior if you're reading a file multiple times
	bool UpdateIfDefStack(void);

	bool Tokenizer::IsOKForIdent(char c)
	{
		return IsAlphaNum(c) || c == '_' 
			|| (m_pExtraIdentifierChars && strchr(m_pExtraIdentifierChars, c))
			|| m_bIgnoreQuotes && (c == '"' || c == '\'');
	}

	bool Tokenizer::IsOKForIdentStart(char c)
	{
		return IsAlpha(c) || c == '_' 
			|| (m_pExtraIdentifierChars && strchr(m_pExtraIdentifierChars, c))
			|| m_bIgnoreQuotes && (c == '"' || c == '\'');;
	}

	public:
		IfDefStack *GetIfDefStack(void) { return &m_ifDefStack; }



};


//IfDefStack functions
IfDefStack *CopyIfDefStack(IfDefStack *pInStack);
void DestroyIfDefStack(IfDefStack *pStack);
IfDefStack *ReadIfDefStackFromFile(Tokenizer *pTokenizer);
void WriteIfDefStackToFile(FILE *pFile, IfDefStack *pStack);
void RecalcIfStackIncludes0(IfDefStack *pStack);

/*to actually write the idefs into a text file, use
	void WriteRelevantIfsToFile(FILE *pFile, IfDefStack *pStack);
	void WriteRelevantEndIfsToFile(FILE *pFile, IfDefStack *pStack);
*/

#endif





