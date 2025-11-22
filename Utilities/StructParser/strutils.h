#ifndef _STRUTILS_H_ 
#define _STRUTILS_H_

#include "stdio.h"
#include "windef.h"

typedef unsigned int U32;
class Tokenizer;

char *NoConst(char *pInString);

static __forceinline char MakeCharUpcase(char c)
{
	if (c >= 'a' && c <= 'z')
	{
		c += 'A' - 'a';
	}

	return c;
}

static __forceinline char MakeCharLowercase(char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		c -= 'A' - 'a';
	}

	return c;
}


static __forceinline void MakeStringUpcase(char *pString)
{
	if (pString)
	{
		while (*pString)
		{
			*pString = MakeCharUpcase(*pString);
			
			pString++;
		}
	}
}

static __forceinline void MakeStringLowercase(char *pString)
{
	if (pString)
	{
		while (*pString)
		{
			*pString = MakeCharLowercase(*pString);
			
			pString++;
		}
	}
}

void TruncateStringAtLastOccurrence(char *pString, char cTrunc);
void TruncateStringAfterLastOccurrence(char *pString, char cTrunc);

bool StringIsInList(char *pString, char *pList[]);

void PutSlashAtEndOfString(char *pString);

int SubDivideStringAndRemoveWhiteSpace(char **ppSubStrings, char *pInString, char separator, int iMaxToFind);

static __forceinline bool IsWhiteSpace(char c)
{
	return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

static __forceinline bool IsNonLineBreakWhiteSpace(char c)
{
	return c == ' ' || c == '\t';
}


static __forceinline bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

static __forceinline bool IsAlpha(char c)
{
	return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z';
}

static __forceinline bool IsAlphaNum(char c)
{
	return IsDigit(c) || IsAlpha(c);
}


static __forceinline bool IsOKForIdent(char c)
{
	return IsAlphaNum(c) || c == '_';
}

static __forceinline bool IsOKForIdentStart(char c)
{
	return IsAlpha(c) || c == '_';
}

void ReplaceMacrosInPlace(char *pString, char *pMacros[][2]);

void FixupBackslashedQuotes(char *pSourceString);

char *GetFileNameWithoutDirectories(char *pSourceName);
char *GetFileNameWithoutDirectoriesOrSlashes(char *pSourceName);

void MakeStringAllAlphaNumAndUppercase(char *pString);
void MakeStringAllAlphaNum(char *pString);

void MakeRepeatedCharacterString(char *pString, int iNumOfChar, int iMaxOfChar, char c);

bool AreFilenamesEqual(char *pName1, char *pName2);

void RemoveTrailingWhiteSpace(char *pString);
void RemoveLeadingWhiteSpace(char *pString);

void RemoveSuffixIfThere(char *pMainString, char *pSuffix);

void RemoveCStyleEscaping(char *pOutString, char *pInString);
void AddCStyleEscaping(char *pOutString, char *pInString, int iMaxSize);

void assembleFilePath(char *pOutPath, char *pDir, char *pOffsetFile);

//goes into a source string, finds each occurrence of each of the substrings listed, and snips them out
void ClipStrings(char *pMainString, int iNumStringsToClip, char **ppStringsToClip);

#define WILDCARD_STRING "__XXAUTOSTRUCTWILDCARDXX__"
#define WILDCARD_STRING_LENGTH 26

bool StringContainsWildcards(char *pString);
bool DoesStringMatchWildcard(char *pString, char *pWildcard);

//includes all the contents of filename in the file at the current location, with some C-style comments
//
//include up to, but not including, the line beginning with pTruncateLine
void ForceIncludeFile(FILE *pOuterFile, char *pFileNameToInclude, char *pTruncateLine);

//replace all occurences in a string of one char with another
void ReplaceCharWithChar(char *pString, char before, char after);

//new/delete
char *STRDUP(const char *pSource);

//malloc/free
char *strdup(const char *pSource);

void RemoveNewLinesAfterBackSlashes(char *pString);

bool StringEndsWith(char *pString, char *pSuffix);

void FowardSlashes(char *pString);

bool StringComesAlphabeticallyBefore(char *pString1, char *pString2);

//turns all /r/n/r/n whatever businesses into a single /n
void NormalizeNewlinesInString(char *pString);

char* strstri(const char* str1, const char* str2);

void TruncateStringAtSuffixIfPresent(char *pString, char *pSuffix);


bool isNonWholeNumberFloatLiteral(char *pString);

bool isInt(char *pString);
//gimpy ass replacement for estrConcat
void ConcatOntoNewedString(char **ppTargetString, char *pWhatToConcat);

U32	hashString( const char* pcToHash );

//takes something like "..\foo.txt" that is relative to "c:\src\mastersolution\allProjectsMasterSolution.sln" and makes
//it relative to "c:\src\fightclub\mastersolution\mastersolution.sln", making it "..\..\foo.txt" instead.
//

//pFirstPath and pSecondPath can be paths or filenames
//
//if pSecondPath is NULL, make it global
void MakeFilenameRelativeToOnePathRelativeToAnotherPath(char outRelativeDir[MAX_PATH], char *pInPath, char *pFirstPath, char *pSecondPath);



typedef struct StringTree StringTree;

typedef void (*StringTree_IterateCB)(char *pStr, int iWordID, void *pUserData1, void *pUserData2);
StringTree *StringTree_Create(void);

//iWordID 0 means "pick the next highest one"
void StringTree_AddWord(StringTree *pTree, char *pWord, int iWordID);
void StringTree_AddWordWithLength(StringTree *pTree, char *pWord, int iLen, int iWordID);
void StringTree_AddPrefix(StringTree *pTree, char *pPrefix, int iPrefixID);

void StringTree_Iterate(StringTree *pTree, StringTree_IterateCB pCB, void *pUserData1, void *pUserData2);

//returns ID of removed word, or 0 if it was not found
int StringTree_RemoveWord(StringTree *pTree, char *pWord);
int StringTree_CheckWord(StringTree *pTree, char *pWord);
int StringTree_CheckWord_IgnorePrefixes(StringTree *pTree, char *pWord);

void StringTree_Destroy(StringTree **ppStringTree);

//if pMagicPrefixString is non-NULL, then any string ending with it is actually a prefix
StringTree *StringTree_CreateFromList(char **ppWords, int iFirstID, char *pMagicPrefixString);

void StringTree_AddAllWordsFromList(StringTree *pTree, char *pWords[]);

//for instance, input string could be "  foo     bar " with pSeparators being " ", this would
//create a string tree with "foo" and "bar" in it
StringTree *StringTree_CreateStrTokStyle(char *pInString, char *pSeparators);

//returns an alloced NULL-terminated list of alloced strings
char **StringTree_ExportList(StringTree *pTree);

//returns malloced string listing all elements (does NOT preserve IDs)
char *StringTree_CreateEscapedString(StringTree *pTree);

//returns true on success, false on failure
bool StringTree_ReadFromEscapedString(StringTree *pTree, char *pStr);

bool ReplaceFirstOccurrenceOfSubString(char *pOutString, char *pSubString, char *pReplaceString);
int ReplaceAllOccurrenceOfSubString(char *pOutString, char *pSubString, char *pReplaceString);

bool StringBeginsWith(const char *pMain, const char *pPrefix, bool bCaseSensitive);

//asserts if the string is not an absolute path, backslashes, removes double slashes, dots, and makes all lowercase
void VerifyFileAbsoluteAndMakeUnique(char *pFileName, char *pErrorMessage);

//special escaping which uses underscores instead of \ for escaping, makes it nice and unambiguous that it will
//never collide with C string escaping

//returns static string, strdup it if you need to 
char *EscapeString_Underscore(char *pStr);

//in place
void UnEscapeString_Underscore(char *pStr);

//assumes that all the strings don't contain whitespaces or unusual punctuation
void WriteEarrayOfIdentifiersToFile(FILE *pOutFile, char ***pppEarray);
void ReadEarrayOfIdentifiersFromFile(Tokenizer *pTokenizer, char ***pppEarray);

#endif