#include "stdio.h"
#include "string.h"
#include "windows.h"
#include "strutils.h"
#include "tokenizer.h"
#include "estring.h"
#include "utils.h"
#include "earray.h"

bool StringBeginsWith(const char *pMain, const char *pPrefix, bool bCaseSensitive)
{
	if (bCaseSensitive)
	{
		while (*pMain && *pPrefix)
		{
			if (*pMain != *pPrefix)
			{
				return false;
			}

			pMain++;
			pPrefix++;
		}
	}
	else
	{
		while (*pMain && *pPrefix)
		{
			if (toupper(*pMain) != toupper(*pPrefix))
			{
				return false;
			}

			pMain++;
			pPrefix++;
		}
	}

	if (*pPrefix)
	{
		return false;
	}

	return true;
}

char *NoConst(char *pInString)
{
	if (StringBeginsWith(pInString, "const ", true))
	{
		return pInString + 6;
	}

	return pInString;
}

void TruncateStringAtLastOccurrence(char *pString, char cTrunc)
{
	if (!pString)
	{
		return;
	}

	int iLen = (int)strlen(pString);

	char *pTemp = pString + iLen - 1;

	do
	{
		if (!(*pTemp))
		{
			return;
		}

		if (*pTemp == cTrunc)
		{
			*pTemp = 0;
			return;
		}

		pTemp--;
	}
	while (pTemp > pString);
}

void TruncateStringAfterLastOccurrence(char *pString, char cTrunc)
{
	if (!pString)
	{
		return;
	}

	int iLen = (int)strlen(pString);

	char *pTemp = pString + iLen - 1;

	if (*pTemp == cTrunc)
	{
		return;
	}

	pTemp--;

	do
	{
		if (!(*pTemp))
		{
			return;
		}

		if (*pTemp == cTrunc)
		{
			*(pTemp+1) = 0;
			return;
		}

		pTemp--;
	}
	while (pTemp > pString);
}





bool StringIsInList(char *pString, char *pList[])
{
	int i = 0;

	while (pList[i])
	{
		if (strcmp(pString, pList[i]) == 0)
		{
			return true;
		}

		i++;
	}

	return false;
}
void PutSlashAtEndOfString(char *pString)

{
	int len = (int)strlen(pString);

	if (pString[len-1] == '\\' || pString[len-1] == '/')
	{
		return;
	}

	pString[len] = '\\';
	pString[len+1] = 0;
}




int SubDivideStringAndRemoveWhiteSpace(char **ppSubStrings, char *pInString, char separator, int iMaxToFind)
{
	int i;

	int iNumFound = 0;
	bool bEndOfString = false;

	while (iNumFound < iMaxToFind && !bEndOfString)
	{
		while (IsWhiteSpace(*pInString) || (*pInString == separator && IsWhiteSpace(*(pInString + 1))))
		{
			pInString++;
		}

		ppSubStrings[iNumFound] = pInString;

		do
		{
			pInString++;
		}
		while ( *pInString && !(*pInString == ' ' && *(pInString + 1) == separator && *(pInString + 2) == ' '));

		iNumFound++;

		if (*pInString)
		{
			*pInString = 0;
			pInString++;
			*pInString = 0;
			pInString++;
			*pInString = 0;
			pInString++;
		}
		else
		{
			bEndOfString = true;
		}
	}


	//trim leading and trailing whitespace from all strings found
	for (i = 0; i < iNumFound; i++)
	{
		while (IsWhiteSpace(*(ppSubStrings[i])))
		{
			ppSubStrings[i]++;
		}

		int iLen = (int)strlen(ppSubStrings[i]);

		while (IsWhiteSpace(*(ppSubStrings[i] + iLen - 1)))
		{
			*(ppSubStrings[i] + iLen - 1) = 0;
			iLen--;
		}
	}

	return iNumFound;
}

void FixupBackslashedQuotes(char *pSourceString)
{
	int iLen = (int)strlen(pSourceString);
	int i = 0;

	do
	{
		if (i >= iLen)
		{
			return;
		}
		else if (pSourceString[i] == '\\')
		{
			if (pSourceString[i+1] == 0)
			{
				return;
			}

			if (pSourceString[i+1] == '"')
			{
				memmove(pSourceString + i, pSourceString + i + 1, iLen - i);
				i++;
				iLen--;
			}
			else
			{
				i += 2;
			}
		}
		else
		{
			i++;
		}
	}
	while (1);
}








char *GetFileNameWithoutDirectories(char *pSourceName)
{
	char *pOutString = pSourceName + strlen(pSourceName);

	while (pOutString > pSourceName && !(*pOutString == '\\' || *pOutString == '/'))
	{
		pOutString--;
	}

	return pOutString;
}


char *GetFileNameWithoutDirectoriesOrSlashes(char *pSourceName)
{
	char *pRetString = GetFileNameWithoutDirectories(pSourceName);
	if (pRetString[0] == '/' || pRetString[0] == '\\')
	{
		return pRetString + 1;
	}

	return pRetString;
}

void MakeStringAllAlphaNumAndUppercase(char *pString)
{
	MakeStringUpcase(pString);

	while (*pString)
	{
		if (!IsAlphaNum(*pString))
		{
			*pString = '_';
		}

		pString++;
	}
}

void MakeStringAllAlphaNum(char *pString)
{
	while (*pString)
	{
		if (!IsAlphaNum(*pString))
		{
			*pString = '_';
		}

		pString++;
	}
}
void MakeRepeatedCharacterString(char *pString, int iNumOfChar, int iMaxOfChar, char c)
{
	if (iNumOfChar < 0)
	{
		iNumOfChar = 0;
	}
	else
	{	
		if (iNumOfChar > iMaxOfChar)
		{
			iNumOfChar = iMaxOfChar;
		}

		memset(pString, c, iNumOfChar);
	}
	pString[iNumOfChar] = 0;
}

static __forceinline bool FilenameCharsAreEqual(char c1, char c2)
{
	if (MakeCharUpcase(c1) == MakeCharUpcase(c2))
	{
		return true;
	}

	if ((c1 == '\\' || c1 == '/') && (c2 == '\\' || c2 == '/'))
	{
		return true;
	}
	
	return false;
}

bool AreFilenamesEqual(char *pName1, char *pName2)
{
	do
	{
		if (!FilenameCharsAreEqual(*pName1, *pName2))
		{
			return false;
		}

		if (!(*pName1))
		{
			return true;
		}

		pName1++;
		pName2++;
	} while (1);
}

void RemoveTrailingWhiteSpace(char *pString)
{
	int iLen = (int)strlen(pString);

	char *pChar = pString + iLen - 1;

	while (pChar >= pString && IsWhiteSpace(*pChar))
	{
		*pChar = 0;
		pChar--;
	}
}

void RemoveLeadingWhiteSpace(char *pString)
{
	int iLen = (int)strlen(pString);
	int iWhiteSpaceCount = 0;
	
	int i = 0;

	while (pString[i] && IsWhiteSpace(pString[i]))
	{
		iWhiteSpaceCount++;
		i++;
	}

	if (iWhiteSpaceCount)
	{
		memmove(pString, pString + iWhiteSpaceCount, iLen - iWhiteSpaceCount + 1);
	}
}



void RemoveSuffixIfThere(char *pMainString, char *pSuffix)
{
	int iSuffixLen = (int)strlen(pSuffix);

	if (_stricmp(pMainString + strlen(pMainString) - iSuffixLen, pSuffix) == 0)
	{
		pMainString[strlen(pMainString) - iSuffixLen] = 0;
	}
}

char escapeSeqDefs[][2] =
{
	{ 'a', '\a' },
	{ 'b', '\b' },
	{ 'f', '\f' },
	{ 'n', '\n' },
	{ 'r', '\r' },
	{ 't', '\t' },
	{ 'v', '\v' },
	{ '\\', '\\' },
	{ '\'', '\'' },
	{ '"', '"' },
};

void RemoveCStyleEscaping(char *pOutString, char *pInString)
{
	while (*pInString)
	{
		if (*pInString == '\\')
		{
			int i;

			pInString++;

			for (i=0; i < sizeof(escapeSeqDefs) / sizeof(escapeSeqDefs[0]); i++)
			{
				if (*pInString == escapeSeqDefs[i][0])
				{
					*pOutString = escapeSeqDefs[i][1];
					pOutString++;
					pInString++;
					break;
				}
			}

			//if we didn't find it, just dump literally and hope for the best
			if (i == sizeof(escapeSeqDefs) / sizeof(escapeSeqDefs[0]))
			{
				*pOutString = '\\';
				pOutString++;
				*pOutString = *pInString;
				pOutString++;
				pInString++;
			}
		}
		else
		{
			*pOutString = *pInString;
			pOutString++;
			pInString++;
		}
	}

	*pOutString = 0;
}

void AddCStyleEscaping(char *pOutString, char *pInString, int iMaxSize)
{
	char *pMaxEnd = pOutString + iMaxSize;

	while (*pInString)
	{
		int i;
		bool bFound = false;

		if (pOutString >= pMaxEnd - 2)
		{
			*pOutString = 0;
			return;
		}

		for (i=0; i < sizeof(escapeSeqDefs) / sizeof(escapeSeqDefs[0]); i++)
		{
			if (*pInString == escapeSeqDefs[i][1])
			{
				*pOutString = '\\';
				pOutString++;

				*pOutString = escapeSeqDefs[i][0];
				pOutString++;

				pInString++;

				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			*pOutString = *pInString;
			pOutString++;
			pInString++;
		}
	}

	*pOutString = 0;
}


void assembleFilePath(char *pOutPath, char *pDir, char *pOffsetFile)
{
	char dir[MAX_PATH];
	char offsetFile[MAX_PATH];
	strcpy(dir, pDir);
	strcpy(offsetFile, pOffsetFile);

	while (StringBeginsWith(offsetFile, "..\\", true) || StringBeginsWith(offsetFile, "../", true))
	{
		memmove(offsetFile, offsetFile + 3, strlen(offsetFile) - 2);
		
		int iDirLen = (int)strlen(dir);

		if (iDirLen > 4)
		{
			do
			{
				iDirLen --;
				dir[iDirLen] = 0;
			}
			while (dir[iDirLen - 1] != '\\' && dir[iDirLen - 1] != '/');
		}
	}

	sprintf(pOutPath, "%s%s", dir, offsetFile);



}



void ClipStrings(char *pMainString, int iNumStringsToClip, char **ppStringsToClip)
{
	int i;
	char *pSubString;

	for (i=0; i < iNumStringsToClip; i++)
	{
		while ((pSubString = strstr(pMainString, ppStringsToClip[i])))
		{
			memmove(pSubString, pSubString + strlen(ppStringsToClip[i]), strlen(pMainString) - strlen(ppStringsToClip[i]) - (pSubString - pMainString) + 1);
		}
	}
}

//only supported wildcard right now is WILDCARD_STRING as a suffix
bool StringContainsWildcards(char *pString)
{
	int iLen = (int) strlen(pString);
	
	return (iLen > WILDCARD_STRING_LENGTH && strcmp(pString + iLen - WILDCARD_STRING_LENGTH, WILDCARD_STRING) == 0);
}
	
bool DoesStringMatchWildcard(char *pString, char *pWildcard)
{
	int iLen = (int) strlen(pWildcard) - WILDCARD_STRING_LENGTH;

	return (strncmp(pString, pWildcard, iLen) == 0);
}


//includes all the contents of filename in the file at the current location, with some C-style comments
void ForceIncludeFile(FILE *pOuterFile, char *pFileNameToInclude, char *pTruncateLine)
{
	int iTruncateLineLen = 0;
	if (pTruncateLine)
	{
		iTruncateLineLen = (int)strlen(pTruncateLine);
	}



	fprintf(pOuterFile, "//\n//\n//Beginning forced include of all contents of file %s\n//\n//\n//\n", pFileNameToInclude);

	FILE *pInnerFile = fopen(pFileNameToInclude, "rt");

	if (!pInnerFile)
	{
		fprintf(pOuterFile, "// (included file is empty or nonexistant)\n");
	}
	else
	{
		char c;
		int iCurTruncateLineIndex = 0;

		do
		{
			c = getc(pInnerFile);




			if (c != EOF)
			{
				//if we might be inside our truncate string at the beginning of a line
				if (iTruncateLineLen && iCurTruncateLineIndex >= 0)
				{
					if (c == pTruncateLine[iCurTruncateLineIndex])
					{
						iCurTruncateLineIndex++;
						if (iCurTruncateLineIndex == iTruncateLineLen)
						{
							//found our truncate string... we've written enough
							break;
						}
					}
					else
					{
						//now we know that we aren't in our truncate line... but we've read in iCurTruncateLineIndex chars
						//that matched
				
						int i;
						for (i=0; i < iCurTruncateLineIndex; i++)
						{
							putc(pTruncateLine[i], pOuterFile);
						}

						putc(c, pOuterFile);
						

						iCurTruncateLineIndex = -1;
					}
				}
				else
				{
					putc(c, pOuterFile);
				}

				if (c == '\n')
				{
					iCurTruncateLineIndex = 0;
				}

			}
			else
			{
				break;
			}
		} while (1);

		fclose(pInnerFile);
	}

	fprintf(pOuterFile, "//\n//\n//Ending forced include of all contents of file %s\n//\n//\n//\n", pFileNameToInclude);
}

void ReplaceCharWithChar(char *pString, char before, char after)
{
	char *pTemp = pString;

	while ((pTemp = strchr(pTemp, before)))
	{
		*pTemp = after;
		pTemp++;
	}
}

char *STRDUP(const char *pSource)
{
	char *pRetVal;

	if (!pSource)
	{
		return NULL;
	}

	int iLen = (int)strlen(pSource);

	pRetVal = new char[iLen + 1];
	memcpy(pRetVal, pSource, iLen+1);

	return pRetVal;
}

char *strdup(const char *pSource)
{
	char *pRetVal;

	if (!pSource)
	{
		return NULL;
	}

	int iLen = (int)strlen(pSource);

	pRetVal = (char*)malloc(iLen + 1);
	memcpy(pRetVal, pSource, iLen+1);

	return pRetVal;
}



void RemoveNewLinesAfterBackSlashes(char *pString)
{
	int iLen = (int)strlen(pString);
	int iOffset = 0;

	while (iOffset < iLen)
	{
		if (pString[iOffset] == '\\')
		{
			int i = iOffset+1;

			do
			{
				if (pString[i] == 0)
				{
					return;
				}
				else if (!IsWhiteSpace(pString[i]))
				{
					iOffset = i;
					break;
				}
				else if (pString[i] == '\n')
				{
					memmove(pString + iOffset, pString + i + 1, iLen - i);
					iLen -= i + 1 - iOffset;
					iOffset += 1;
					break;
				}
				i++;
			} while (1);
		}
		else
		{
			iOffset++;
		}
	}
}

bool StringEndsWith(char *pString, char *pSuffix)
{
	int iLen = (int)strlen(pString);
	int iSuffixLen = (int)strlen(pSuffix);

	if (iSuffixLen > iLen)
	{
		return false;
	}

	return (strcmp(pString + (iLen - iSuffixLen), pSuffix) == 0);
}

void FowardSlashes(char *pString)
{
	while (*pString)
	{
		if (*pString == '\\')
		{
			*pString = '/';
		}

		pString++;
	}
}


bool StringComesAlphabeticallyBefore(char *pString1, char *pString2)
{
	do
	{
		char c1 = MakeCharUpcase(*pString1);
		char c2 = MakeCharUpcase(*pString2);
		if (!c1)
		{
			return true;
		}

		if (!c2)
		{
			return false;
		}

		if (c1 < c2)
		{
			return true;
		}

		if (c1 > c2)
		{
			return false;
		}

		pString1++;
		pString2++;
	} while (1);
}

void NormalizeNewlinesInString(char *pString)
{
	int iLen = (int)strlen(pString);
	int i, j;

	for (i=0; i < iLen; i++)
	{
		if (pString[i] == '\n' || pString[i] == '\r')
		{
			pString[i] = '\n';
			int iCount = 0;
			j = i+1;

			while (pString[j] == '\n' || pString[j] == '\r')
			{
				j++;
				iCount++;
			}

			if (iCount)
			{
				memmove(pString + i + 1, pString + i + 1 + iCount, iLen - i - iCount);
				iLen -= iCount;
			}
		}
	}
}

void TruncateStringAtSuffixIfPresent(char *pString, char *pSuffix)
{
	if (!pString || !pSuffix || !pString[0] || !pSuffix[0])
	{
		return;
	}

	int iStrLen = (int)strlen(pString);
	int iSuffixLen = (int)strlen(pSuffix);

	if (iSuffixLen > iStrLen)
	{
		return;
	}

	if (strcmp(pSuffix, pString + iStrLen - iSuffixLen) == 0)
	{
		pString[iStrLen - iSuffixLen] = 0;
	}
}





// stristr /////////////////////////////////////////////////////////
//
// performs a case-insensitive lookup of a string within another
// (see C run-time strstr)
//
// str1 : buffer
// str2 : string to search for in the buffer
//
// example char* s = stristr("Make my day","DAY");
//
// S.Rodriguez, Jan 11, 2004
//
char* strstri(const char* str1, const char* str2)
{
	__asm
	{
		mov ah, 'A'
		mov dh, 'Z'

		mov esi, str1
		mov ecx, str2
		mov dl, [ecx]
		test dl,dl ; NULL?
		jz short str2empty_label

outerloop_label:
		mov ebx, esi ; save esi
		inc ebx
innerloop_label:
		mov al, [esi]
		inc esi
		test al,al
		je short str2notfound_label ; not found!

        cmp     dl,ah           ; 'A'
        jb      short skip1
        cmp     dl,dh           ; 'Z'
        ja      short skip1
        add     dl,'a' - 'A'    ; make lowercase the current character in str2
skip1:		

        cmp     al,ah           ; 'A'
        jb      short skip2
        cmp     al,dh           ; 'Z'
        ja      short skip2
        add     al,'a' - 'A'    ; make lowercase the current character in str1
skip2:		

		cmp al,dl
		je short onecharfound_label
		mov esi, ebx ; restore esi value, +1
		mov ecx, str2 ; restore ecx value as well
		mov dl, [ecx]
		jmp short outerloop_label ; search from start of str2 again
onecharfound_label:
		inc ecx
		mov dl,[ecx]
		test dl,dl
		jnz short innerloop_label
		jmp short str2found_label ; found!
str2empty_label:
		mov eax, esi // empty str2 ==> return str1
		jmp short ret_label
str2found_label:
		dec ebx
		mov eax, ebx // str2 found ==> return occurence within str1
		jmp short ret_label
str2notfound_label:
		xor eax, eax // str2 nt found ==> return NULL
		jmp short ret_label
ret_label:
	}
}


bool isNonWholeNumberFloatLiteral(char *pString)
{
	while (IsWhiteSpace(*pString))
	{
		pString++;
	}

	while (*pString == '-')
	{
		pString++;
	}

	if (!(isdigit(*pString)))
	{
		return false;
	}

	while (isdigit(*pString))
	{
		pString++;
	}

	if (*pString != '.')
	{
		return false;
	}

	pString++;

	while (isdigit(*pString))
	{
		if (*pString != '0')
		{
			return true;
		}

		pString++;
	}

	return false;
}



void ConcatOntoNewedString(char **ppTargetString, char *pWhatToConcat)
{
	int iNewLen;
	char *pNewString;

	if (!(*ppTargetString))
	{
		*ppTargetString = STRDUP(pWhatToConcat);
		return;
	}

	iNewLen = (int)(strlen(*ppTargetString) + strlen(pWhatToConcat));

	pNewString = new char[iNewLen + 1];
	
	sprintf(pNewString, "%s%s", *ppTargetString, pWhatToConcat);

	delete(*ppTargetString);
	*ppTargetString = pNewString;
}


bool isInt(char *pString)
{
	while (*pString == '-')
	{
		pString++;
	}

	if (!IsDigit(*pString))
	{
		return false;
	}

	while (IsDigit(*pString))
	{
		pString++;
	}

	if (*pString)
	{
		return false;
	}

	return true;
}


//copie dfrom hashFunctions.c in utilitiesLib
#define DEFAULT_HASH_SEED 0xfaceface
U32 burtlehash3( const char *k, U32  length, U32  initval);



/*
--------------------------------------------------------------------
This is identical to hash() on little-endian machines (like Intel 
x86s or VAXen).  It gives nondeterministic results on big-endian
machines.  It is faster than hash(), but a little slower than 
hash2(), and it requires
-- that all your machines be little-endian
--------------------------------------------------------------------
*/

#define mix(a,b,c) \
{ \
	a -= b; a -= c; a ^= (c>>13); \
	b -= c; b -= a; b ^= (a<<8); \
	c -= a; c -= b; c ^= (b>>13); \
	a -= b; a -= c; a ^= (c>>12);  \
	b -= c; b -= a; b ^= (a<<16); \
	c -= a; c -= b; c ^= (b>>5); \
	a -= b; a -= c; a ^= (c>>3);  \
	b -= c; b -= a; b ^= (a<<10); \
	c -= a; c -= b; c ^= (b>>15); \
}

U32 burtlehash3( const char *k, U32  length, U32  initval)
//register U8 *k;        /* the key */
//register U32  length;   /* the length of the key */
//register U32  initval;  /* the previous hash, or an arbitrary value */
{
	register U32 a,b,c,len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;    /* the golden ratio; an arbitrary value */
	c = initval;           /* the previous hash value */

	/*---------------------------------------- handle most of the key */
/*	if (((uintptr_t)k)&3)
	{
		while (len >= 12)    // unaligned
		{
			a += (k[0] +((U32)k[1]<<8) +((U32)k[2]<<16) +((U32)k[3]<<24));
			b += (k[4] +((U32)k[5]<<8) +((U32)k[6]<<16) +((U32)k[7]<<24));
			c += (k[8] +((U32)k[9]<<8) +((U32)k[10]<<16)+((U32)k[11]<<24));
			mix(a,b,c);
			k += 12; len -= 12;
		}
	}
	else*/
	// The aligned code works equally well on non-aligned addresses
	// But there may be some performance issues.
	// The above code does NOT work on bit endian machines -BZ
	{
		while (len >= 12)    /* aligned */
		{
			a += *(U32 *)(k+0);
			b += *(U32 *)(k+4);
			c += *(U32 *)(k+8);
			mix(a,b,c);
			k += 12; len -= 12;
		}
	}

	/*------------------------------------- handle the last 11 bytes */
	c += length;
	switch(len)              /* all the case statements fall through */
	{
	case 11: c+=((U32)k[10]<<24);
	case 10: c+=((U32)k[9]<<16);
	case 9 : c+=((U32)k[8]<<8);
		/* the first byte of c is reserved for the length */
	case 8 : b+=((U32)k[7]<<24);
	case 7 : b+=((U32)k[6]<<16);
	case 6 : b+=((U32)k[5]<<8);
	case 5 : b+=k[4];
	case 4 : a+=((U32)k[3]<<24);
	case 3 : a+=((U32)k[2]<<16);
	case 2 : a+=((U32)k[1]<<8);
	case 1 : a+=k[0];
		/* case 0: nothing left to add */
	}
	mix(a,b,c);
	/*-------------------------------------------- report the result */
	return c;
}



U32 hashString( const char *pcToHash )
{
	return burtlehash3(pcToHash, (U32)strlen(pcToHash), DEFAULT_HASH_SEED);
}

#define IsSlash(c) ((c) == '/' || (c) == '\\')

void GetNextPathSegment(char outPathSegment[MAX_PATH], char *pInPath)
{
	char *pWriteHead = outPathSegment;

	while (IsSlash(*pInPath))
	{
		pInPath++;
	}

	while ((*pInPath) && !IsSlash(*pInPath))
	{
		*pWriteHead = *pInPath;
		pWriteHead++;
		pInPath++;
	}

	*pWriteHead = 0;
}


void MakeAbsolutePathRelative(char outRelativePath[MAX_PATH], char *pInAbsolutePath, char *pRelativeToWhat)
{
	char temp1[MAX_PATH];
	char temp2[MAX_PATH];
	int i;

	//count of path levels in pRelativeToWhat after it diverges... in other words, the number
	//of copies of "..\" we want at the beginning of our out path
	int iCount = 0;

	//make sure they're on the same drive
	if (pInAbsolutePath[1] != ':' || pRelativeToWhat[1] != ':' || MakeCharUpcase(pInAbsolutePath[0]) != MakeCharUpcase(pRelativeToWhat[0]))
	{
		return;
	}

	pInAbsolutePath += 2;
	pRelativeToWhat += 2;

	while (1)
	{
		GetNextPathSegment(temp1, pInAbsolutePath);
		GetNextPathSegment(temp2, pRelativeToWhat);

		if (temp1[0] == 0 || temp2[0] == 0 || strcmp(temp1, temp2) != 0)
		{
			break;
		}

		pInAbsolutePath += strlen(temp1) + 1;
		pRelativeToWhat += strlen(temp2) + 1;
	}

	while (1)
	{
		GetNextPathSegment(temp2, pRelativeToWhat);
		if (temp2[0])
		{
			iCount++;

			pRelativeToWhat += strlen(temp2) + 1;
		}
		else
		{
			break;
		}
	}

	outRelativePath[0] = 0;

	for (i=0; i < iCount; i++)
	{
		strcat(outRelativePath, "..\\");
	}

	strcat(outRelativePath, pInAbsolutePath + 1);
}










void MakeFilenameRelativeToOnePathRelativeToAnotherPath(char outRelativePath[MAX_PATH], char *pInPath, char *pFirstPath, char *pSecondPath)
{
	char firstPath[MAX_PATH];
	char secondPath[MAX_PATH];

	char absolutePath[MAX_PATH] = "";

	strcpy(firstPath, pFirstPath);

	TruncateStringAfterLastOccurrence(firstPath, '\\');

	ReplaceAllOccurrenceOfSubString(firstPath, "\\\\", "\\");



	if (pSecondPath)
	{
		strcpy(secondPath, pSecondPath);

		TruncateStringAfterLastOccurrence(secondPath, '\\');
		ReplaceAllOccurrenceOfSubString(secondPath, "\\\\", "\\");

		assembleFilePath(absolutePath, firstPath, pInPath);

		MakeAbsolutePathRelative(outRelativePath, absolutePath, pSecondPath);
	}
	else
	{
		assembleFilePath(outRelativePath, firstPath, pInPath);
	}
}



bool ReplaceFirstOccurrenceOfSubString(char *pOutString, char *pSubString, char *pReplaceString)
{
	size_t iSubStringLen = strlen(pSubString);
	size_t iReplaceStringLen = strlen(pReplaceString);
	size_t iOutStringLen = strlen(pOutString);

	char *pFound = strstri(pOutString, pSubString);
	if (!pFound)
	{
		return false;
	}
	size_t iOffset = pFound - pOutString;

	if (iOffset + iSubStringLen == iOutStringLen)
	{
		//found string is at the very end... just copy
		memcpy(pOutString + iOffset, pReplaceString, iReplaceStringLen + 1);
	}
	else
	{
		memmove(pOutString + iOffset + iReplaceStringLen, pOutString + iOffset + iSubStringLen, iOutStringLen - iOffset + iSubStringLen + 1);
		memcpy(pOutString + iOffset, pReplaceString, iReplaceStringLen + 1);
	}

	return true;
}


int ReplaceAllOccurrenceOfSubString(char *pOutString, char *pSubString, char *pReplaceString)
{
	int iCount = 0;

	while (ReplaceFirstOccurrenceOfSubString(pOutString, pSubString, pReplaceString))
	{
		iCount++;
	}

	return iCount;
}


		






typedef struct StringTree 
{
	StringTree *pChildren[256];
	int iWordID;
	int iPrefixID;
	int iWordCount;
	int iMaxID;
} StringTree;


StringTree *StringTree_Create(void)
{
	return (StringTree*)calloc(sizeof(StringTree), 1);
}

void StringTree_AddWord(StringTree *pTree, char *pWord, int iID)
{
	if (!iID)
	{
		iID = pTree->iMaxID + 1;
	}
	
	pTree->iWordCount++;

	if (!pWord[0])
	{
		STATICASSERT(pTree->iWordID == 0, "Adding identical word to tree");
		pTree->iWordID = iID;
		return;
	}

	if (iID > pTree->iMaxID)
	{
		pTree->iMaxID = iID;
	}

	if (!pTree->pChildren[pWord[0]])
	{
		pTree->pChildren[pWord[0]] = StringTree_Create();
	}

	StringTree_AddWord(pTree->pChildren[pWord[0]], pWord + 1, iID);
}	

void StringTree_AddWordWithLength(StringTree *pTree, char *pWord, int iLen, int iWordID)
{
	char *pTemp = (char*)malloc(iLen + 1);
	memcpy(pTemp, pWord, iLen);
	pTemp[iLen] = 0;
	StringTree_AddWord(pTree, pTemp, iWordID);
	free(pTemp);
}




void StringTree_AddPrefix(StringTree *pTree, char *pWord, int iID)
{
	STATICASSERT(iID != 0, "zero is not a valid StringTree ID");
	pTree->iWordCount++;

	if (!pWord[0])
	{
		pTree->iPrefixID = iID;
		return;
	}

	if (!pTree->pChildren[pWord[0]])
	{
		pTree->pChildren[pWord[0]] = StringTree_Create();
	}

	StringTree_AddPrefix(pTree->pChildren[pWord[0]], pWord + 1, iID);
}	

//returns true if tree is now empty
int StringTree_RemoveWord(StringTree *pTree, char *pWord)
{
	int iFoundID;

	if (pWord[0])
	{

		if (!pTree->pChildren[pWord[0]])
		{
			return 0;
		}

		iFoundID  = StringTree_RemoveWord(pTree->pChildren[pWord[0]], pWord + 1);

		if (iFoundID)
		{
			if (pTree->pChildren[pWord[0]]->iWordCount == 0)
			{
				StringTree_Destroy(&pTree->pChildren[pWord[0]]);
			}

			pTree->iWordCount--;
			return iFoundID;
		}

		return 0;
	}

	if ((iFoundID = pTree->iWordID) || (iFoundID = pTree->iPrefixID))
	{
		pTree->iWordID = 0;
		pTree->iWordCount--;
	}

	return iFoundID;
}






int StringTree_CheckWord(StringTree *pTree, char *pWord)
{
	int iFoundPrefix = 0;
	STATICASSERT(pTree && pWord, "NULL tree or word passed in to checkWord");

	//recurse in one loop so this is as fast as possible
	while (1)
	{
		if (pTree->iPrefixID)
		{
			iFoundPrefix = pTree->iPrefixID;
		}

		if (!pWord[0])
		{
			return pTree->iWordID ? pTree->iWordID : iFoundPrefix;
		}

		if (!pTree->pChildren[pWord[0]])
		{
			return iFoundPrefix;
		}

		pTree = pTree->pChildren[pWord[0]];
		pWord++;
	}
}

int StringTree_CheckWord_IgnorePrefixes(StringTree *pTree, char *pWord)
{
	STATICASSERT(pTree && pWord, "NULL tree or word passed in to checkWord");

	//recurse in one loop so this is as fast as possible
	while (1)
	{

		if (!pWord[0])
		{
			return pTree->iWordID;
		}

		if (!pTree->pChildren[pWord[0]])
		{
			return 0;
		}

		pTree = pTree->pChildren[pWord[0]];
		pWord++;
	}
}

void StringTree_Destroy(StringTree **ppStringTree)
{
	if (!ppStringTree)
	{
		return;
	}

	if (!(*ppStringTree))
	{
		return;
	}

	int i;

	for (i=0; i < 256; i++)
	{
		StringTree_Destroy(&((*ppStringTree)->pChildren[i]));
	}

	free(*ppStringTree);
	*ppStringTree = NULL;
}


StringTree *StringTree_CreateFromList(char **ppWords, int iFirstID, char *pMagicPrefixString)
{
	StringTree *pTree = StringTree_Create();
	int i;
	int iMagicStringLen = pMagicPrefixString ? (int)strlen(pMagicPrefixString) : 0;

	for (i=0; ppWords[i]; i++)
	{
		if (pMagicPrefixString && StringEndsWith(ppWords[i], pMagicPrefixString))
		{
			char *pTemp = _strdup(ppWords[i]);
			pTemp[strlen(pTemp) - iMagicStringLen] = 0;
			StringTree_AddPrefix(pTree, pTemp, iFirstID + i);
			free(pTemp);
		}
		else
		{
			StringTree_AddWord(pTree, ppWords[i], iFirstID + i);
		}
	}

	return pTree;
}


void StringTree_AddAllWordsFromList(StringTree *pTree, char *pWords[])
{
	int i;

	for (i=0; pWords[i]; i++)
	{
		StringTree_AddWord(pTree, pWords[i], 0);
	}
}


#define STRINGTREE_ITERATE_MAXLENGTH 1024

void StringTree_IterateInternal(StringTree *pTree, StringTree_IterateCB pCB, void *pUserData1, void *pUserData2, char *pString, int iStrLen)
{
	int i;

	if (pTree->iWordID)
	{
		pCB(pString, pTree->iWordID, pUserData1, pUserData2);
	}

	for (i=0; i < 256; i++)
	{
		if (pTree->pChildren[i])
		{
			STATICASSERTF(iStrLen < STRINGTREE_ITERATE_MAXLENGTH - 1, "String in search tree beginning %s too long for iteration", pString);
			pString[iStrLen] = i;

			StringTree_IterateInternal(pTree->pChildren[i], pCB, pUserData1, pUserData2, pString, iStrLen + 1);
		}
	}

	pString[iStrLen] = 0;
}

void StringTree_Iterate(StringTree *pTree, StringTree_IterateCB pCB, void *pUserData1, void *pUserData2)
{
	char temp[STRINGTREE_ITERATE_MAXLENGTH] = "";

	StringTree_IterateInternal(pTree, pCB, pUserData1, pUserData2, temp, 0);

}




void StringTree_ExportListCB(char *pStr, int iStrID, void *pUserData1, void *pUserData2)
{
	char **ppList = (char**)pUserData1;
	int *pCount = (int*)pUserData2;
	ppList[*pCount] = _strdup(pStr);
	(*pCount)++;
}

//returns an alloced NULL-terminated list of alloced strings
char **StringTree_ExportList(StringTree *pTree)
{
	char **ppOutList = (char**)calloc(sizeof(char*) * (pTree->iWordCount + 1), 1);
	int i = 0;

	StringTree_Iterate(pTree, StringTree_ExportListCB, ppOutList, &i);

	return ppOutList;
}



void StringTree_CreateEscapedStringCB(char *pStr, int iStrID, void *pUserData1, void *pUserData2)
{
	char **ppEString = (char**)pUserData2;

	if (estrLength(ppEString) > 0)
	{
		estrConcatf(ppEString, ",");
	}

	while (*pStr)
	{
	switch (*pStr)
	{
	case '\r':
		estrConcatf(ppEString, "\\r");
		break;
	case '\n':
		estrConcatf(ppEString, "\\n");
		break;
	case '\"':
		estrConcatf(ppEString, "\\q");
		break;
	case ',':
		estrConcatf(ppEString, "\\c");
		break;
	case '\\':
		estrConcatf(ppEString, "\\\\");
		break;
	default:
		estrConcatf(ppEString, "%c", *pStr);
	}
	pStr++;
	}
}


char *StringTree_CreateEscapedString(StringTree *pTree)
{
	char *pEString = NULL;
	char *pRetVal;
	StringTree_Iterate(pTree, StringTree_CreateEscapedStringCB, &pEString, NULL);
	pRetVal = _strdup(pEString);
	estrDestroy(&pEString);
	return pRetVal;
}


//returns true on success, false on failure
bool StringTree_ReadFromEscapedString(StringTree *pTree, char *pStr)
{
	char temp[STRINGTREE_ITERATE_MAXLENGTH] = "";
	int iCurLen = 0;

	while (IsWhiteSpace(*pStr))
	{
		pStr++;
	}

	while (1)
	{
		char c = *(pStr++);

		if (c == 0)
		{
			StringTree_AddWord(pTree, temp, 0);
			return true;
		}

		if (c == ',')
		{
			StringTree_AddWord(pTree, temp, 0);
			memset(temp, 0, STRINGTREE_ITERATE_MAXLENGTH);
			iCurLen = 0;
			continue;
		}

		STATICASSERTF(iCurLen < STRINGTREE_ITERATE_MAXLENGTH - 1, "Trying to load too long string for stringtree: %s", temp);

		if (c == '\\')
		{
			c = *(pStr++);
			switch (c)
			{
			case '\\':
				temp[iCurLen] = '\\';
				break;
			case 'q':
				temp[iCurLen] = '"';
				break;
			case 'r':
				temp[iCurLen] = '\r';
				break;
			case 'n':
				temp[iCurLen] = '\n';
				break;
			case 'c':
				temp[iCurLen] = ',';
				break;
			default:
				STATICASSERTF(0, "Unknown stringtree escape character %c", c); 
			}


			


		}
		else
		{
			temp[iCurLen] = c;
		}

		iCurLen++;
	}
}







void StringTree_PrintCB(char *pStr, int iStrID, void *pUserData1, void *pUserData2)
{
	printf("%d: %s\n", iStrID, pStr);
}

StringTree *StringTree_CreateStrTokStyle(char *pInString, char *pSeparators)
{
	StringTree *pTree = StringTree_Create();
	char *pWordStart;
	int iNextID = 1;

	while (1)
	{
		while (*pInString && strchr(pSeparators, *pInString))
		{
			pInString++;
		}

		if (!*pInString)
		{
			return pTree;
		}

		pWordStart = pInString;

		while (*pInString && !strchr(pSeparators, *pInString))
		{
			pInString++;
		}

		if (!*pInString)
		{
			StringTree_AddWord(pTree, pWordStart, iNextID++);
			return pTree;
		}

		StringTree_AddWordWithLength(pTree, pWordStart, (int)(pInString - pWordStart), iNextID++);
	
	}

}

void VerifyFileAbsoluteAndMakeUnique(char *pFileName, char *pErrorMessage)
{
	char *pTemp;
	size_t iLen;
	char origName[MAX_PATH];
	strcpy(origName, pFileName);

	if (!pFileName || !pFileName[0])
	{
		Tokenizer::StaticAssertFailedf("Invalid string while: %s", pErrorMessage);
	}

	if (pFileName[1] != ':')
	{
		Tokenizer::StaticAssertFailedf("String \"%s\" not an absolute filename while: %s", pFileName, pErrorMessage);
	}

	//first pass... lowercase and backslashes
	pTemp = pFileName;

	while (*pTemp)
	{
		if (*pTemp == '/')
		{
			*pTemp = '\\';
		}

		*pTemp = tolower(*pTemp);

		pTemp++;
	}

	//second pass... remove all double slashes
	iLen = strlen(pFileName);
	pTemp = pFileName;

	while (*pTemp)
	{
		if (*pTemp == '\\' && *(pTemp + 1) == '\\')
		{
			memmove(pTemp, pTemp + 1, iLen - (pTemp - pFileName) + 1);
			iLen--;
		}
		else
		{
			pTemp++;
		}
	}

	//third pass... remove all \.\ 
	iLen = strlen(pFileName);
	pTemp = pFileName;


	while (*pTemp)
	{
		if (*pTemp == '\\' && *(pTemp + 1) == '.' && *(pTemp + 2) == '\\')
		{
			memmove(pTemp, pTemp + 2, iLen - (pTemp - pFileName));
			iLen -= 2;
		}
		else
		{
			pTemp++;
		}
	}

	//fourth pass... fix \..\ 
	int iLastSlashIndex = 0;
	int iCurSlashIndex = 0;
	iLen = strlen(pFileName);
	pTemp = pFileName;

	while (*pTemp)
	{
		if (*pTemp == '\\')
		{
			iLastSlashIndex = iCurSlashIndex;
			iCurSlashIndex = pTemp - pFileName;

			if (*(pTemp + 1) == '.' && *(pTemp + 2) == '.' && *(pTemp + 3) == '\\')
			{
				if (!iLastSlashIndex)
				{
					Tokenizer::StaticAssertFailedf("Filename %s had bad .. while: %s", origName, pErrorMessage);
				}

				memmove(pFileName + iLastSlashIndex, pTemp + 3, iLen - (pTemp - pFileName) - 2);
				
				pTemp = pFileName;
				iLastSlashIndex = 0;
				iCurSlashIndex = 0;
				iLen = strlen(pFileName);
			}
			else
			{
				pTemp ++;
			}
		}
		else
		{
			pTemp++;
		}
	}
}

char escapeChars[][2] =
{
	{ '\n', 'n', }, 
	{ '\"', 'q', }, 
	{ '\r', 'r', }, 
	{ '\\', 's', },
	{ '%', 'p', },
};

//returns static string, strdup it if you need to 
char *EscapeString_Underscore(char *pStr)
{
	static char *pRetVal = NULL;
	estrClear(&pRetVal);
	while (*pStr)
	{
		if (*pStr == '_')
		{
			estrConcatf(&pRetVal, "__");

		}
		else
		{
			int i;
			bool bSpecial = false;

			for (i = 0; i < ARRAY_SIZE(escapeChars); i++)
			{
				if (*pStr == escapeChars[i][0])
				{
					estrConcatf(&pRetVal, "_%c", escapeChars[i][1]);
					bSpecial = true;
					break;
				}
				
			}

			if (!bSpecial)
			{
				estrConcatf(&pRetVal, "%c", *pStr);
			}
		}

		pStr++;
	}

	return pRetVal;
}


//in place
void UnEscapeString_Underscore(char *pStr)
{
	int iLen = strlen(pStr);

	if (!iLen)
	{
		return;
	}

	while (*pStr)
	{
		if (*pStr != '_')
		{
			pStr++;
			iLen--;
		}
		else
		{
			if (pStr[1] == '_')
			{
				memmove(pStr, pStr + 1, iLen);
				pStr++;
				iLen -= 2;
			}
			else
			{
				bool bSpecial = false;
				int i;

				for (i = 0; i < ARRAY_SIZE(escapeChars); i++)
				{
					if (pStr[1] == escapeChars[i][1])
					{
						memmove(pStr, pStr + 1, iLen);
						pStr[0] = escapeChars[i][0];
						pStr++;
						iLen -= 2;
						bSpecial = true;
						break;
					}
				}

				if (!bSpecial)
				{
					pStr++;
					iLen--;
				}
			}
		}
	}
}
			

void WriteEarrayOfIdentifiersToFile(FILE *pOutFile, char ***pppEarray)
{
	int i;
	fprintf(pOutFile, " %d ", eaSize(pppEarray));
	for (i = 0; i < eaSize(pppEarray); i++)
	{
		fprintf(pOutFile, "%s ", (*pppEarray)[i]);
	}
}

void ReadEarrayOfIdentifiersFromFile(Tokenizer *pTokenizer, char ***pppEarray)
{
	int iSize;
	Token token;
	int i;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Couldn't read number of items for an earray");

	iSize = token.iVal;
	if (iSize == 0)
	{
		return;
	}

	for (i = 0; i < iSize; i++)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Couldn't read identifier for an earray");
		eaPush(pppEarray, _strdup(token.sVal));
	}
}