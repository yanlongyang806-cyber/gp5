#include "POFileUtils.h"
#include "EString.h"
#include "earray.h"
#include "POFileUtils.h"
#include "POFileUtils_h_ast.h"
#include "file.h"
#include "StringUtil.h"

void RemoveCStyleEscaping(char **ppEStr)
{
	U32 iIndex = 0;
	while (iIndex + 1 < estrLength(ppEStr)) //no point in examining the very last character in the string
	{
		char c = (*ppEStr)[iIndex];
		if (c == '\\')
		{
			switch ((*ppEStr)[iIndex+1])
			{
			case 'r':
				(*ppEStr)[iIndex] = '\r';
				estrRemove(ppEStr, iIndex + 1, 1);
				break;
			case 'n':
				(*ppEStr)[iIndex] = '\n';
				estrRemove(ppEStr, iIndex + 1, 1);
				break;
			default:
				estrRemove(ppEStr, iIndex, 1);
				break;
			}
		}

		iIndex++;
	}
}



bool AddHashLineToBlock(char *pCurLine, POBlockRaw *pBlock, char **ppErrorString)
{
	switch (pCurLine[1])
	{
	case '.':
		if (strStartsWith(pCurLine, "#. description="))
		{
			if (pBlock->pDescription)
			{
				estrPrintf(ppErrorString, "Found duplicate description= lines");
				return false;
			}
			pBlock->pDescription = estrDupAndTrim(pCurLine + 15);
			RemoveCStyleEscaping(&pBlock->pDescription);
			return true;
		}

		if (strStartsWith(pCurLine, "#. scope="))
		{
			eaPush(&pBlock->ppScopes, estrDupAndTrim(pCurLine + 9));
			return true;
		}

		if (strStartsWith(pCurLine, "#. key="))
		{
			eaPush(&pBlock->ppKeys, estrDupAndTrim(pCurLine + 7));
			return true;
		}

		if (strStartsWith(pCurLine, "#. alternateTrans="))
		{
			eaPush(&pBlock->ppAlternateTrans, estrDupAndTrim(pCurLine + 18));
			return true;
		}

		estrPrintf(ppErrorString, "Unknown # line: %s", pCurLine);
		return false;
	
	case ':':
		{
			char *pTemp = estrDupAndTrim(pCurLine + 2);
			estrTruncateAtLastOccurrence(&pTemp, ':');
			eaPush(&pBlock->ppFiles, pTemp);
		}
		return true;
	}

	if (strStartsWith(pCurLine, "# Wordcount: "))
	{
		return true;
	}

	estrPrintf(ppErrorString, "Unknown # line: %s", pCurLine);
	return false;
}

bool ReadPotentiallyMultiLineString(char **ppCurLine, char ***pppMoreLines, char **ppOutEString, char **ppErrorString)
{
	if (estrLength(ppOutEString))
	{
		estrPrintf(ppErrorString, "Destination string not empty... was something duplicated?");
		return false;
	}

	//can loop over many lines
	while (*ppCurLine)
	{
		char *pCurReadHead = *ppCurLine;

		while (*pCurReadHead && *pCurReadHead != '"')
		{
			pCurReadHead++;
		}

		if (!*pCurReadHead)
		{
			estrPrintf(ppErrorString, "Found no quotes in line <<%s>>", *ppCurLine);
			return false;
		}

		pCurReadHead++;

		while (*pCurReadHead && *pCurReadHead != '"')
		{
			if (*pCurReadHead == '\\' && pCurReadHead[1])
			{
				switch (pCurReadHead[1])
				{
				case '\\':
					estrConcatChar(ppOutEString, '\\');
					break;
				case 'r':
					estrConcatChar(ppOutEString, '\r');
					break;
				case 'n':
					estrConcatChar(ppOutEString, '\n');
					break;
				default:
					estrConcatChar(ppOutEString, pCurReadHead[1]);
					break;
				}

				pCurReadHead += 2;
			}
			else
			{
				estrConcatChar(ppOutEString, *pCurReadHead);
				pCurReadHead++;
			}
		}

		if (!(*pCurReadHead))
		{
			estrPrintf(ppErrorString, "Found no close quotes in line <<%s>>", *ppCurLine);
			return false;
		}

		//*pCurReadHead must be "
		pCurReadHead++;
		if (!StringIsAllWhiteSpace(pCurReadHead))
		{
			estrPrintf(ppErrorString, "Found characters after the end of the string... possible quote escaping problem? <<%s>>", *ppCurLine);
			return false;
		}

		if (eaSize(pppMoreLines) && ((*pppMoreLines)[0])[0] == '"')
		{
			free(*ppCurLine);
			*ppCurLine = eaRemove(pppMoreLines, 0);
		}
		else
		{
			return true;
		}
	}

	//how did we get here?
	return true; 
}

static void printFailure(FORMAT_STR const char *pFmt, ...)
{
	char *pStr = NULL;
	estrGetVarArgs(&pStr, pFmt);
	consolePushColor();
	consoleSetColor(COLOR_RED | COLOR_BRIGHT, 0);
	printf("ERROR! ERROR! ERROR!\n");
	printf("%s", pStr);
	printf("\n\n");
	consolePopColor();
	estrDestroy(&pStr);
}


#define READ_RAW_BLOCK_FAIL(pFmt, ...) { printFailure(pFmt, __VA_ARGS__); SAFE_FREE(pCurLine); eaDestroyEx(pppInLines, NULL); StructDestroySafe(parse_POBlockRaw, &pBlock); estrDestroy(&pErrorString); return NULL; }

POBlockRaw *ReadRawBlockFromLines(char ***pppInLines, char *pInFileName, int iLineNum)
{
	POBlockRaw *pBlock;
	char *pErrorString = NULL;

	if (!eaSize(pppInLines))
	{
		return NULL;
	}
	
	pBlock = StructCreate(parse_POBlockRaw);

	pBlock->pReadFileName = strdup(pInFileName);
	pBlock->iLineNum = iLineNum;

	while (1)
	{
		char *pCurLine = eaRemove(pppInLines, 0);

		if (!pCurLine)
		{
			//maybe do some verification here?
			return pBlock;
		}

		if (pCurLine[0] == '#')
		{
			if (!AddHashLineToBlock(pCurLine, pBlock, &pErrorString))
			{
				READ_RAW_BLOCK_FAIL("near %s(%d), got error while parsing # line <<%s>: %s\n", 
					pInFileName, iLineNum, pCurLine, pErrorString);
			}
		}
		else if (strStartsWith(pCurLine, "msgctxt"))
		{
			if (!ReadPotentiallyMultiLineString(&pCurLine, pppInLines, &pBlock->pCtxt, &pErrorString))
			{
				READ_RAW_BLOCK_FAIL("near %s(%d), got error while parsing msgctxt string: %s\n", 
					pInFileName, iLineNum, pErrorString);
			}
		}
		else if (strStartsWith(pCurLine, "msgid"))
		{
			if (!ReadPotentiallyMultiLineString(&pCurLine, pppInLines, &pBlock->pID, &pErrorString))
			{
				READ_RAW_BLOCK_FAIL("near %s(%d), got error while parsing msgid string: %s\n", 
					pInFileName, iLineNum, pErrorString);
			}
		}
		else if (strStartsWith(pCurLine, "msgstr"))
		{
			if (!ReadPotentiallyMultiLineString(&pCurLine, pppInLines, &pBlock->pStr, &pErrorString))
			{
				READ_RAW_BLOCK_FAIL("near %s(%d), got error while parsing msgstr string: %s\n", 
					pInFileName, iLineNum, pErrorString);
			}
		}
		else
		{
			READ_RAW_BLOCK_FAIL("near %s(%d), could not parse line <<%s>>\n", 
				pInFileName, iLineNum, pCurLine);
		}

		SAFE_FREE(pCurLine);
	}

	eaDestroyEx(pppInLines, NULL);
	return pBlock;
}

void ReadTranslateBlocksFromFile(char *pFileName, POBlockRaw ***pppBlocks)
{
	FILE *pInFile = fopen(pFileName, "rt");
	char *pCurLine = NULL; //ESTRING
	char **ppCurLines = NULL; //NOT ESTRINGS
	char c;
	POBlockRaw *pCurBlock;
	int iCurLineNum = 0;
	int iStartingLineNumOfBlock = 0;
	bool bHeader = true;

	assertmsgf(pInFile, "Couldn't open %s for reading", pFileName);

	while (1)
	{
		c = getc(pInFile);

		if (c == 0 || c == EOF)
		{
			estrTrimLeadingAndTrailingWhitespace(&pCurLine);
			if (estrLength(&pCurLine) && !strStartsWith(pCurLine, "# Wordcount: "))
			{
				eaPush(&ppCurLines, strdup(pCurLine));
			}
			estrDestroy(&pCurLine);
	
			if (bHeader)
			{
				eaDestroyEx(&ppCurLines, NULL);
			}
			else
			{
				pCurBlock = ReadRawBlockFromLines(&ppCurLines, pFileName, iStartingLineNumOfBlock);
				if (pCurBlock)
				{
					eaPush(pppBlocks, pCurBlock);
				}
			}

			fclose(pInFile);
			return;
		}

		if (c == '\n')
		{
			iCurLineNum++;
			estrTrimLeadingAndTrailingWhitespace(&pCurLine);
			if (estrLength(&pCurLine) && !strStartsWith(pCurLine, "# Wordcount: "))
			{
				eaPush(&ppCurLines, strdup(pCurLine));
				estrClear(&pCurLine);
			}
			else
			{
				if (bHeader)
				{
					eaDestroyEx(&ppCurLines, NULL);
					bHeader = false;
					iStartingLineNumOfBlock = iCurLineNum;
				}
				else
				{
					pCurBlock = ReadRawBlockFromLines(&ppCurLines, pFileName, iStartingLineNumOfBlock);
					if (pCurBlock)
					{
						eaPush(pppBlocks, pCurBlock);
					}

					iStartingLineNumOfBlock = iCurLineNum;
				}
			}
		}
		else
		{
			estrConcatChar(&pCurLine, c);
		}
	}
}

#include "POFileUtils_h_ast.c"
