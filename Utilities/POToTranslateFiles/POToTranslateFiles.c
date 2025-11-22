#include "MemoryMonitor.h"
#include "FolderCache.h"
#include "sysUtil.h"
#include "UtilitiesLib.h"
#include "cmdParse.h"
#include "file.h"
#include "Estring.h"
#include "earray.h"
#include "StringCache.h"
#include "timing.h"
#include "textparser.h"
#include "POToTranslateFiles_c_ast.h"
#include "fileutil2.h"
#include "Regex.h"
#include "StringUtil.h"
#include "POFileUtils.h"
#include "Message.h"
#include "Message_h_ast.h"

char configFileName[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(configFileName, configFile);

static bool sbUnattendedMode = false;
AUTO_CMD_INT(sbUnattendedMode, UnattendedMode);

static MessageCategoryForLocalizationList *spCategoryList = NULL;

AUTO_STRUCT;
typedef struct POToTranslateConfig
{
	char POFileSourceDirectory[CRYPTIC_MAX_PATH];
	char OutClientFile[CRYPTIC_MAX_PATH];
	char OutServerFile[CRYPTIC_MAX_PATH];
	
	//if set, then will load in a PO Categories file (ie, c:/night/data/server/config/POExportCategories.txt), and will
	//ignore any filename in a categorized file that does not fit that category. This is to work around a bug that was
	//briefly present in the po file exporting which would write out all the filenames of messages in a message group, even
	//if some of them didnt' fit the current category
	char POCategoriesFile[CRYPTIC_MAX_PATH];
	
	char **ppClientMessageDirectories; //regexes, ".*/Defs/Items/.*". Any message whose file matches one of these
		//will be stuck into the client instead of the server

	bool bSpecialOtherLanguageBootstrapMode : 1; //if true, then we're doing the special bootstrap where we fill up
		//Night translations at the very beginning of the localization process by finding all the messages from 
		//StarTrek (which was already fully localized) with identical keys and identical english strings, and
		//then grabbing the StarTrek translation and inserting it into Night

	bool bNULLOriginalIsAnError : 1; //if true, CompareNonTextParts will error if the original string is missing (NULL).

	bool bMissingTranslationIsAnError : 1; //if true, CompareNonTextParts will error if the translated string is missing (NULL).

	bool bBraceCountMismatchIsAnError : 1; //if true, CompareNonTextParts will error if the number of several types of braces
		// is different in the original string vs the translated one. For now, it tests "{}<>".

	bool bMissingTokenIsAnError : 1; //if true, CompareNonTextParts will error if any token exists in the original string but
		// not in the translated one, and vice-versa. This is less strict check than bTokenOrderIsAnError, and is probably
		// what you want to do. This will MISS differences in HOW MANY TIMES a token is used in each string.

	bool bTokenOrderIsAnError : 1; //if true, CompareNonTextParts will error if the "{Value}" type tokens are in a different
		//order in the translation versus the original. This is probably too strict, since it's more grammatically correct
		//to reorder words in some languages. "{Value}'s Shirt" vs "Shirt de {Value}". Consider using bMissingTokenIsAnError instead.

	bool bBlankTranslationIsAnError : 1; //if true, CompareNonTextParts will error if the translated string is blank "". This
		// is probably because the translation for this language is incomplete so far.

	bool bBadFileEncodingIsAnError : 1; //if true, CompareNonTextParts will error if the .po file was saved in the wrong encoding 
		// somewhere along the way.
		// You can fix it in EditPlus by:
		//   1) Open the .po file
		//   2)	File->"Save As…" and choose the Encoding "ANSI".
		//   3)	Close the file.
		//   4)	File->Open and choose the Encoding "UTF-8".
		//   5)	File->"Save As..." and choose the Encoding "UTF-8".
		//   6) Re-run POToTranslate

	bool bIgnoreKValuesWhenMatching : 1; //if true, CompareNonTextParts will ignore "{k:blah}" in both strings, when doing all the comparisons.

	bool bIgnoreBRInBraceCount : 1; // if true, "<br>" won't count in either string for the triangle brace count.
	
	bool bIgnoreEntityGenderTokenCount : 1; // if true, "{Entity.Gender" won't count in either string for the token count.

	int iNumMessageGroupsRead; NO_AST
	int iNumMessagesRead; NO_AST
	int iNumNew; NO_AST
	int iNumChanged; NO_AST
	int iNumThatWereNull; NO_AST
	int iNumUnchanged; NO_AST
	int iNumBootStrapped; NO_AST

} POToTranslateConfig;

//What this writes out ends up getting read in as a TranslatedMessage, but this has all the fields
AUTO_STRUCT AST_IGNORE(CoreMsg) AST_IGNORE(Updated) AST_IGNORE(Duplicate);
typedef struct TranslateFileMessage
{
	char *pMessageKey; AST(NAME(MessageKey) KEY)
	char *pScope; AST(NAME(Scope))
	char *pDescription; AST(NAME(Description))
	char *pDefaultString; AST(NAME(DefaultString))
	char *pTranslatedString; AST(NAME(TranslatedString))
	char *pClientMsg; AST(NAME(ClientMsg))
	char *pMsgFileName; AST(NAME(MsgFileName))
	bool bReadFromPOs; NO_AST
	bool bNewThisRun; NO_AST
} TranslateFileMessage;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct TranslatedFileMessageList
{
	TranslateFileMessage **ppMessages; AST(NAME(Message))
} TranslatedFileMessageList;


TranslatedFileMessageList *spClientMessageList = NULL;
TranslatedFileMessageList *spServerMessageList = NULL;

AUTO_FIXUPFUNC;
TextParserResult fixupPOToTranslateConfig(POToTranslateConfig *pConfig, enumTextParserFixupType eType, void *pExtraData)
{
	int i;

	switch(eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		for (i = 0; i < eaSize(&pConfig->ppClientMessageDirectories); i++)
		{
			forwardSlashes(pConfig->ppClientMessageDirectories[i]);
		}
		break;
	}

	return 1;
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


void ProcessBlockFail(POBlockRaw *pBlock, FORMAT_STR const char *pFmt, ...)
{
	char *pErrorString = NULL;
	estrGetVarArgs(&pErrorString, pFmt);
	
	if(eaSize(&pBlock->ppKeys) > 0)
	{
		printFailure("While reading block from %s(%d):\nkey=%s\n%s\n", pBlock->pReadFileName, pBlock->iLineNum, pBlock->ppKeys[0], pErrorString);
	}
	else
	{
		printFailure("While reading block from %s(%d):\n%s\n", pBlock->pReadFileName, pBlock->iLineNum, pErrorString);
	}

	estrDestroy(&pErrorString);
}



#define PROCESSBLOCK_FAIL(pFmt, ...) { ProcessBlockFail(pBlock, pFmt, __VA_ARGS__); return; }

bool FileIsClientFile(POToTranslateConfig *pConfig, char *pFileName)
{
	int i;

	forwardSlashes(pFileName);

	for (i = 0;i < eaSize(&pConfig->ppClientMessageDirectories); i++)
	{
		if (RegExSimpleMatch(pFileName, pConfig->ppClientMessageDirectories[i]))
		{
			return true;
		}
	}

	return false;
}

MessageCategoryForLocalization *FindCategoryForFile(char *pFileName)
{
	FOR_EACH_IN_EARRAY_FORWARDS(spCategoryList->ppCategories, MessageCategoryForLocalization, pCategory)
	{
		int i;
		
		for (i = 0; i < eaSize(&pCategory->ppFileNameSubStrings); i++)
		{
			if (strstri(pFileName, pCategory->ppFileNameSubStrings[i]))
			{
				return pCategory;
			}
		}
	}
	FOR_EACH_END;


	return NULL;
}

void CompareNonTextParts(POToTranslateConfig *pConfig, POBlockRaw *pBlock, const char *pID_Original, const char *pStr_Original)
{
	const char *pID = pID_Original;
	const char *pStr = pStr_Original;
	char *estrID = NULL;
	char *estrStr = NULL;

	if( !pConfig )
		return;

	if( !pID && pConfig->bNULLOriginalIsAnError )
	{
		PROCESSBLOCK_FAIL("Original String is NULL. That's bad");
	}
	else if( !pID )
	{
		return;
	}

	if( !pStr && pConfig->bMissingTranslationIsAnError )
	{
		PROCESSBLOCK_FAIL("Translation missing for:\r\n Originally: \"%s\"", pID_Original);
	}
	else if( !pStr )
	{
		return;
	}

	if( !*pStr && pConfig->bBlankTranslationIsAnError )
	{
		PROCESSBLOCK_FAIL("Translation is blank for:\r\n Originally: \"%s\"", pID_Original);
	}

	// We've found that the badly encoded files have a 'Ã' ("U+00C3 Latin Capital Letter A With Tilde" maybe "U+00D0 Latin Capital Letter Eth") instead of the the right byte.
	// This will fail if we ever have a language that really uses this character.
	if( pConfig->bBadFileEncodingIsAnError && (*pStr == '\xc3') )
	{
		PROCESSBLOCK_FAIL("Translation has bad file encoding for:\r\n Originally: \"%s\"\r\n Translated: \"%s\"", pID_Original, pStr_Original);
	}

	// If requested, copy the Real strings to the locals, and strip out all "{k:blah}" bits
	if( pConfig->bIgnoreKValuesWhenMatching || pConfig->bIgnoreBRInBraceCount || pConfig->bIgnoreEntityGenderTokenCount )
	{
		const char *pC, *pCNext = NULL;
		char *pD;

		estrSetSize(&estrID, strlen(pID_Original));
		estrSetSize(&estrStr, strlen(pStr_Original));

		// Copy the strings to the estrings, but ignore anything like "{k:blah}"
		// NOTE: This method assumes that the k-strings will always be the innermost curly-braces.
		// If this isn't true, we'll have to do much more complex parsing here.
		pD = estrID;
		pC = pID_Original;
		while(*pC)
		{
			// If we've found a "{k:blah}"...
			if( (pConfig->bIgnoreKValuesWhenMatching && !strnicmp(pC, "{k:", 3)) || 
				(pConfig->bIgnoreEntityGenderTokenCount && !strnicmp(pC, "{Entity.Gender", 3)) )
			{
				// ... skip to the next '}'
				pCNext = strchr(pC, '}');
				if( !pCNext )
				{
					ProcessBlockFail(pBlock, "Missing '}' for \"%.10s\":\r\n Originally: \"%s\"", pC, pID_Original);
					break; // Something awful happened
				}
				pC = pCNext+1;
			}
			else if( pConfig->bIgnoreBRInBraceCount && !strnicmp(pC, "<br>", 4) )
			{
				pC += 4; // strlen "<br>"
			}
			else
			{
				// copy the character
				*(pD++) = *(pC++);
			}
		}
		*(pD) = '\0';
		estrSetSize(&estrID, strlen(estrID));

		pD = estrStr;
		pC = pStr_Original;
		while(*pC)
		{
			// If we've found a "{k:blah}"...
			if( (pConfig->bIgnoreKValuesWhenMatching && !strnicmp(pC, "{k:", 3)) || 
				(pConfig->bIgnoreEntityGenderTokenCount && !strnicmp(pC, "{Entity.Gender", 3)) )
			{
				// ... skip to the next '}'
				pCNext = strchr(pC, '}');
				if( !pCNext )
				{
					ProcessBlockFail(pBlock, "Missing '}' for \"%.10s\":\r\n Translated: \"%s\"", pC, pStr_Original);
					break; // Something awful happened
				}
				pC = pCNext+1;
			}
			else if( pConfig->bIgnoreBRInBraceCount && !strnicmp(pC, "<br>", 4) )
			{
				pC += 4; // strlen "<br>"
			}
			else
			{
				// copy the character
				*(pD++) = *(pC++);
			}
		}
		*(pD) = '\0';
		estrSetSize(&estrStr, strlen(estrStr));

		pID = estrID;
		pStr = estrStr;
	}

	// Make sure the braces counts match.
	if( pConfig->bBraceCountMismatchIsAnError )
	{
		const unsigned char charsToCount[] =
		{
			'{', '}',
			'<', '>',
			//'[', ']',
			//'(', ')',
		};
		const int CHARS = sizeof(charsToCount)/sizeof(charsToCount[0]);
		int i=0;

		for( i=0; i<CHARS; ++i )
		{
			int iCount1 = 0;
			int iCount2 = 0;
			const unsigned char *pC;
			pC = pID;
			while(pC = strchr(pC, charsToCount[i])) { ++iCount1; ++pC; }
			pC = pStr;
			while(pC = strchr(pC, charsToCount[i])) { ++iCount2; ++pC; }

			if( iCount1 != iCount2 )
			{
				PROCESSBLOCK_FAIL("Braces count mismatch for '%c':\r\n Originally: \"%s\"\r\n Translated: \"%s\"", charsToCount[i], pID_Original, pStr_Original);
			}
		}
	}

	if( pConfig->bMissingTokenIsAnError )
	{
		// Order-independent check of the tokens in each string. Every token that exists in the original MUST also be in the 
		// translated one, and vice-versa.
		const unsigned char *pChar;
		const unsigned char *pTok;
		unsigned char tok[1024];
		const ptrdiff_t maxTok = sizeof(tok)/sizeof(tok[0]);

		const unsigned char ** eaTokens1 = NULL;
		const unsigned char ** eaTokens2 = NULL;
		eaCreate(&eaTokens1);
		eaCreate(&eaTokens2);

		// Find all the tokens in pID
		pChar = pID;
		while( *pChar )
		{
			// Find the next '{' in pID
			while( *pChar && *pChar != '{') { ++pChar; }

			if( !*pChar ) break;

			++pChar;

			// Skip any whitespace
			while( *pChar && isspace(*pChar) ) { ++pChar; }

			// Store the start of the token
			pTok = pChar;

			while( *pChar && !isspace(*pChar) && *pChar != '}' ) { ++pChar; }

			assert(pChar-pTok < maxTok);
			strncpy(tok, pTok, pChar-pTok);
			tok[maxTok-1] = '\0';
			string_tolower(tok);

			eaPushUnique(&eaTokens1, allocAddString(tok));

			++pChar;
		}

		// Find all the tokens in pStr
		pChar = pStr;
		while( *pChar )
		{
			// Find the next '{' in pID
			while( *pChar && *pChar != '{') { ++pChar; }

			if( !*pChar ) break;

			++pChar;

			// Skip any whitespace
			while( *pChar && isspace(*pChar) ) { ++pChar; }

			// Store the start of the token
			pTok = pChar;

			while( *pChar && !isspace(*pChar) && *pChar != '}' ) { ++pChar; }

			assert(pChar-pTok < maxTok);
			strncpy(tok, pTok, pChar-pTok);
			tok[maxTok-1] = '\0';
			string_tolower(tok);

			eaPushUnique(&eaTokens2, allocAddString(tok));

			++pChar;
		}

		// Make sure there's a match in eaTokens1 for every token in eaTokens2.
		FOR_EACH_IN_EARRAY_FORWARDS(eaTokens1, const char, pTok1)
		{
			if( eaFind(&eaTokens2, pTok1) == -1 )
			{
				PROCESSBLOCK_FAIL("Token \"%s\" missing from translation:\r\n Originally: \"%s\"\r\n Translated: \"%s\"", pTok1, pID_Original, pStr_Original);
			}
		}
		FOR_EACH_END

		// Make sure there's a match in eaTokens2 for every token in eaTokens1.
		FOR_EACH_IN_EARRAY_FORWARDS(eaTokens2, const char, pTok2)
		{
			if( eaFind(&eaTokens1, pTok2) == -1 )
			{
				PROCESSBLOCK_FAIL("Token \"%s\" missing from translation:\r\n Originally: \"%s\"\r\n Translated: \"%s\"", pTok2, pID_Original, pStr_Original);
			}
		}
		FOR_EACH_END

		eaDestroy(&eaTokens1);
		eaDestroy(&eaTokens2);
	}

	if( pConfig->bTokenOrderIsAnError )
	{
		// For each "{Value}" type substring in the default string, make sure there's an exact match in the translated string.
		// TODO: The current method requires that the ORDER of the substrings matches exactly. This is probably too strict, since it 
		// might be grammatically correct to swap some values around in other languages. But this will get a reasonable approximation.
		const unsigned char *pChar1 = pID;
		const unsigned char *pChar2 = pStr;
		const unsigned char *pTok1, *pTok2;

		while( *pChar1 )
		{
			// Find the next '{' in pID
			while( *pChar1 && *pChar1 != '{') { ++pChar1; }

			// Find the next '{' in pStr
			while( *pChar2 && *pChar2 != '{') { ++pChar2; }

			if( !*pChar1 && !*pChar2 )
				break;

			if( *pChar1 == '{' && *pChar2 != '{' )
			{
				PROCESSBLOCK_FAIL("Mismatching '{':\r\n Originally: \"%s\"\r\n Translated: \"%s\"", pID_Original, pStr_Original);
			}

			++pChar1;
			++pChar2;

			// Skip any whitespace
			while( *pChar1 && isspace(*pChar1) ) { ++pChar1; }
			while( *pChar2 && isspace(*pChar2) ) { ++pChar2; }

			// Store the start of the token
			pTok1 = pChar1;
			pTok2 = pChar2;

			// The following non-space characters must match EXACTLY
			while( *pChar1 && *pChar2 && !isspace(*pChar1) && *pChar1 != '}' && tolower(*pChar1) == tolower(*pChar2) ) { ++pChar1; ++pChar2; }

			if( *pChar1 != *pChar2 )
			{
				PROCESSBLOCK_FAIL("Token order mismatch:\r\n Originally: \"%*s\"\r\n Translated: \"%*s\"", pChar1-pTok1, pTok1, pChar2-pTok2, pTok2);
			}

			++pChar1;
			++pChar2;
		}
	}

	estrDestroy(&estrID);
	estrDestroy(&estrStr);
}

void ProcessBlock(POToTranslateConfig *pConfig, POBlockRaw *pBlock)
{
	int i;
	TranslatedFileMessageList *pListToUse;
	TranslateFileMessage *pOldMessage;
	TranslateFileMessage *pNewMessage;
	char *pDescription = pBlock->pDescription;
	char *pCtxt = pBlock->pCtxt;
	char *pID = pBlock->pID;
	char *pStr = pBlock->pStr;


	//one of the following three will be set if spCategoryList was loaded
	bool bCategoryIsEverything = false;
	bool bCategoryIsUncategorized = false;
	MessageCategoryForLocalization *pCategory = NULL;

	if (!eaSize(&pBlock->ppKeys))
	{
		PROCESSBLOCK_FAIL("Found no keys... that is not legal");
	}

	//if we loaded in some PO file categories, then go in and remove all filenames that don't match the
	//category the POBLock's file is in)
	if (spCategoryList)
	{
		static char *pFileNameShort = NULL;
		estrClear(&pFileNameShort);
		estrGetDirAndFileNameAndExtension(pBlock->pReadFileName, NULL, &pFileNameShort, NULL);

		if (stricmp(pFileNameShort, "Everything") == 0)
		{
			bCategoryIsEverything = true;
		}
		else if (stricmp(pFileNameShort, "Uncategorized") == 0)
		{
			bCategoryIsUncategorized = true;
		}
		else
		{
			FOR_EACH_IN_EARRAY(spCategoryList->ppCategories, MessageCategoryForLocalization, pCategoryIter)
			{
				if (stricmp(pCategoryIter->pCategoryName, pFileNameShort) == 0)
				{
					pCategory = pCategoryIter;
					break;
				}
			}
			FOR_EACH_END;

			if (!pCategory)
			{
				PROCESSBLOCK_FAIL("Unable to find a category in %s named %s... but we read messages from %s",
					pConfig->POCategoriesFile, pFileNameShort, pBlock->pReadFileName);
			}
		}

		//if the category is everything, then we accept all filenames
		if (!bCategoryIsEverything)
		{
			for (i = eaSize(&pBlock->ppFiles) - 1; i >= 0; i--)
			{
				MessageCategoryForLocalization *pCategoryForFile = FindCategoryForFile(pBlock->ppFiles[i]);

				if (pCategoryForFile != pCategory)
				{
					//note that this counts both the NULL cases, so deals properly with "uncategorized"
					char *pTempFileName = eaRemove(&pBlock->ppFiles, i);
					printf("Note: Removing filename %s which we loaded from %s, because it seems to be categorized wrong\n",
						pTempFileName, pBlock->pReadFileName);
					estrDestroy(&pTempFileName);
				}
			}
		}
	}





	if (eaSize(&pBlock->ppKeys) != eaSize(&pBlock->ppFiles))
	{
		static char *pFailString = NULL;

		estrPrintf(&pFailString, "Number of keys (%d) does not match number of files (%d). Keys: ",
			eaSize(&pBlock->ppKeys), eaSize(&pBlock->ppFiles));

		for (i = 0; i < eaSize(&pBlock->ppKeys); i++)
		{
			estrConcatf(&pFailString, "%s%s", i == 0 ? "" : ", ", pBlock->ppKeys[i]);
		}

		estrConcatf(&pFailString, "  Files: ");


		for (i = 0; i < eaSize(&pBlock->ppFiles); i++)
		{
			estrConcatf(&pFailString, "%s%s", i == 0 ? "" : ", ", pBlock->ppFiles[i]);
		}

		PROCESSBLOCK_FAIL("%s", pFailString);
	}

	if (eaSize(&pBlock->ppScopes) != eaSize(&pBlock->ppKeys) && eaSize(&pBlock->ppScopes) != 0)
	{
		PROCESSBLOCK_FAIL("Number of scopes must be either zero or match number of files");
	}

	if (eaSize(&pBlock->ppAlternateTrans) > 0)
	{
		PROCESSBLOCK_FAIL("Importing a block with an alternateTrans will irreversibly damage the .translation file. Change the Description in the .ms file for some of these messagekeys until you no longer get an \"#. alternateTrans=\" line from the builder.");
	}

	pConfig->iNumMessagesRead += eaSize(&pBlock->ppKeys);

	for (i = 0; i < eaSize(&pBlock->ppKeys); i++)
	{
		char *pKey = pBlock->ppKeys[i];
		char *pFile = pBlock->ppFiles[i];
		char *pScope = eaSize(&pBlock->ppScopes) ? pBlock->ppScopes[i] : NULL;
		bool bClient = false;

		if (FileIsClientFile(pConfig, pFile))
		{
			pListToUse = spClientMessageList;
			bClient = true;
		}
		else
		{
			pListToUse = spServerMessageList;
		}

		// Complain violently if the non-text parts of the translated string don't match the default strings non-text parts.
		CompareNonTextParts(pConfig, pBlock, pID, pStr);

		pOldMessage = eaIndexedGetUsingString(&pListToUse->ppMessages, pKey);

		if (pOldMessage)
		{
			if (pConfig->bSpecialOtherLanguageBootstrapMode)
			{
				if (stricmp_safe(pOldMessage->pDefaultString, pID) == 0
					&& (!pOldMessage->pTranslatedString || pOldMessage->pTranslatedString[0] == 0))
				{
					//we're in bootstrap mode, the keys match, the english strings match, and there is no translated
					//string to begin with... do the bootstrapping
					pOldMessage->bReadFromPOs = true;

					SAFE_FREE(pOldMessage->pTranslatedString);
					pOldMessage->pTranslatedString = strdup(pStr);

					pConfig->iNumBootStrapped++;

					printf("Bootstrapping message %s:%s as:\n%s\n\n", 
						pKey, pOldMessage->pDefaultString, pOldMessage->pTranslatedString);
				}
			}
			else
			{
				pOldMessage->bReadFromPOs = true;

				if (stricmp_safe(pOldMessage->pTranslatedString, pStr) == 0)
				{
					pConfig->iNumUnchanged++;
				}
				else if (!pOldMessage->pTranslatedString || !pOldMessage->pTranslatedString[0])
				{
					pConfig->iNumThatWereNull++;
				}
				else
				{
					pConfig->iNumChanged++;
				}

				SAFE_FREE(pOldMessage->pScope);
				pOldMessage->pScope = strdup(pScope);
			
				SAFE_FREE(pOldMessage->pDescription);
				pOldMessage->pDescription = strdup(pDescription);

				SAFE_FREE(pOldMessage->pDefaultString);
				pOldMessage->pDefaultString = strdup(pID);

				SAFE_FREE(pOldMessage->pTranslatedString);
				pOldMessage->pTranslatedString = strdup(pStr);

				SAFE_FREE(pOldMessage->pClientMsg);
				pOldMessage->pClientMsg = strdup(bClient ? "1" : NULL);

				SAFE_FREE(pOldMessage->pMsgFileName);
				pOldMessage->pMsgFileName = strdup(pFile);
			}
		}
		else
		{
			//never create new messages in Bootstrap Mode
			if (pConfig->bSpecialOtherLanguageBootstrapMode)
			{
				continue;
			}

			pNewMessage = StructCreate(parse_TranslateFileMessage);
			pConfig->iNumNew++;

			pNewMessage->pMessageKey = strdup(pKey);
			pNewMessage->pScope = strdup(pScope);		
			pNewMessage->pDescription = strdup(pDescription);
			pNewMessage->pDefaultString = strdup(pID);
			pNewMessage->pTranslatedString = strdup(pStr);
			pNewMessage->pClientMsg = strdup(bClient ? "1" : NULL);
			pNewMessage->pMsgFileName = strdup(pFile);
			pNewMessage->bNewThisRun = true;

			eaPush(&pListToUse->ppMessages, pNewMessage);
		}
	}
}


void RunFromConfig(POToTranslateConfig *pConfig)
{
	POBlockRaw **ppRawBlocks = NULL;
	char **ppPoFiles = NULL;
	int i;
	int iCurSize = 0;
	int iUnreadClient = 0, iUnreadServer = 0, iNewClient = 0, iNewServer = 0;
	int iNumRemoved = 0;

	spClientMessageList = StructCreate(parse_TranslatedFileMessageList);
	spServerMessageList = StructCreate(parse_TranslatedFileMessageList);

	if (pConfig->POCategoriesFile[0])
	{
		if (!fileExists(pConfig->POCategoriesFile))
		{
			printf("Error: %s does not exist\n", pConfig->POCategoriesFile);
			return;
		}
		spCategoryList = StructCreate(parse_MessageCategoryForLocalizationList);

		if (!ParserReadTextFile(pConfig->POCategoriesFile, parse_MessageCategoryForLocalizationList, spCategoryList, 0))
		{
			loadend_printf("Errors while reading %s\n", pConfig->POCategoriesFile);
			return;
		}
	}

	if (!pConfig->POFileSourceDirectory[0] || !pConfig->OutClientFile[0] || !pConfig->OutServerFile[0])
	{
		printf("Error: your config file must include POFileSourceDirectory, OutClientFile and OutServerFile\n");
		return;
	}

	if (!fileExists(pConfig->OutClientFile))
	{
		char c;
		printf("Warning: %s does not exist. Do you wish it to be created?\n", pConfig->OutClientFile);
		c = _getch();
		if (c != 'y')
		{
			return;
		}
	}
	else
	{
		loadstart_printf("About to read from %s...", pConfig->OutClientFile);
		if (!ParserReadTextFile(pConfig->OutClientFile, parse_TranslatedFileMessageList, spClientMessageList, 0))
		{
			loadend_printf("Errors while reading %s\n", pConfig->OutClientFile);
			return;
		}
		else
		{
			loadend_printf("contained %d messages\n", eaSize(&spClientMessageList->ppMessages));
		}
	}

	if (!fileExists(pConfig->OutServerFile))
	{
		char c;
		printf("Warning: %s does not exist. Do you wish it to be created?\n", pConfig->OutServerFile);
		c = _getch();
		if (c != 'y')
		{
			return;
		}
	}
	else
	{
		loadstart_printf("About to read from %s...", pConfig->OutServerFile);
		if (!ParserReadTextFile(pConfig->OutServerFile, parse_TranslatedFileMessageList, spServerMessageList, 0))
		{
			loadend_printf("Errors while reading %s (if the error involved the file being empty, note that it either needs to exist and be full, or not exist at all... delete it and try again)\n", pConfig->OutServerFile);
			return;
		}
		else
		{
			loadend_printf("contained %d messages\n", eaSize(&spServerMessageList->ppMessages));
		}
	}

	ppPoFiles = fileScanDirNoSubdirRecurse(pConfig->POFileSourceDirectory);

	for (i = eaSize(&ppPoFiles) - 1; i >= 0; i--)
	{
		if (!strEndsWith(ppPoFiles[i], ".po"))
		{
			free(ppPoFiles[i]);
			eaRemove(&ppPoFiles, i);
		}
	}

	if (!eaSize(&ppPoFiles))
	{
		printf("No .po files found in %s...\n", pConfig->POFileSourceDirectory);
	}

	for (i = 0; i < eaSize(&ppPoFiles); i++)
	{
		loadstart_printf("About to read message groups from %s...", ppPoFiles[i]);
		ReadTranslateBlocksFromFile(ppPoFiles[i], &ppRawBlocks);
		loadend_printf("...contained %d message groups\n", eaSize(&ppRawBlocks) - iCurSize);
		iCurSize = eaSize(&ppRawBlocks);
	}

	pConfig->iNumMessageGroupsRead = eaSize(&ppRawBlocks);

	for (i = 0; i < eaSize(&ppRawBlocks); i++)
	{
		POBlockRaw *pBlock = ppRawBlocks[i];


		if (i % 1000 == 0)
		{
			printf("Processing message group %d/%d\n", i, eaSize(&ppRawBlocks));
		}

	
		ProcessBlock(pConfig, pBlock);
	}
	

	//stat counting
	FOR_EACH_IN_EARRAY(spClientMessageList->ppMessages, TranslateFileMessage, pMessage)
	{
		if (pMessage->bNewThisRun)
		{
			iNewClient++;
		}
		else if (!pMessage->bReadFromPOs)
		{
			iUnreadClient++;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(spServerMessageList->ppMessages, TranslateFileMessage, pMessage)
	{
		if (pMessage->bNewThisRun)
		{
			iNewServer++;
		}
		else if (!pMessage->bReadFromPOs)
		{
			iUnreadServer++;
		}
	}
	FOR_EACH_END;

	printf("Done processing messages... found %d total message groups, %d total messages:\n",
		pConfig->iNumMessageGroupsRead, pConfig->iNumMessagesRead);

	if (pConfig->bSpecialOtherLanguageBootstrapMode)
	{
		printf("%d bootstrapped\n", pConfig->iNumBootStrapped);
	}
	else
	{

		printf("%d new\n", pConfig->iNumNew);
		printf("%d unchanged\n", pConfig->iNumUnchanged);
		printf("%d changed\n", pConfig->iNumChanged);
		printf("%d that were previously empty\n", pConfig->iNumThatWereNull);

		printf("Writing out %d client messages... of these, %d are new, and %d were not found in the .po files at all\n",
			eaSize(&spClientMessageList->ppMessages), iNewClient, iUnreadClient);

		printf("Writing out %d Server messages... of these, %d are new, and %d were not found in the .po files at all\n",
			eaSize(&spServerMessageList->ppMessages), iNewServer, iUnreadServer);
	}


	printf("Removing empty client messages\n");
	for (i = eaSize(&spClientMessageList->ppMessages) - 1; i>= 0; i--)
	{
		TranslateFileMessage *pMessage = spClientMessageList->ppMessages[i];

		if (!pMessage->pTranslatedString || StringIsAllWhiteSpace(pMessage->pTranslatedString))
		{
			iNumRemoved++;
			StructDestroy(parse_TranslateFileMessage, pMessage);
			eaRemove(&spClientMessageList->ppMessages, i);
		}
	}

	printf("Removed %d\n\n", iNumRemoved);


	iNumRemoved = 0;
	printf("Removing empty server messages\n");
	for (i = eaSize(&spServerMessageList->ppMessages) - 1; i>= 0; i--)
	{
		TranslateFileMessage *pMessage = spServerMessageList->ppMessages[i];

		if (!pMessage->pTranslatedString || StringIsAllWhiteSpace(pMessage->pTranslatedString))
		{
			iNumRemoved++;
			StructDestroy(parse_TranslateFileMessage, pMessage);
			eaRemove(&spServerMessageList->ppMessages, i);
		}
	}

	printf("Removed %d\n\n", iNumRemoved);



	loadstart_printf("Going to try to write back out %s\n", pConfig->OutClientFile);
	ParserWriteTextFile(pConfig->OutClientFile, parse_TranslatedFileMessageList, spClientMessageList, 0, 0);
	loadend_printf("...Done\n");

	loadstart_printf("Going to try to write back out %s\n", pConfig->OutServerFile);
	ParserWriteTextFile(pConfig->OutServerFile, parse_TranslatedFileMessageList, spServerMessageList, 0, 0);
	loadend_printf("...Done\n");
	

}



int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	bool bNeedToConfigure = false;
	POToTranslateConfig *pConfig;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);



	srand((unsigned int)time(NULL));

	fileAllPathsAbsolute(true);

	if (!configFileName[0])
	{
		printf("Please specify config filename with -configFile\n");
		return;
	}

	if (!fileExists(configFileName))
	{
		printf("Config file %s seems to not exist\n", configFileName);
		return;
	}

	pConfig = StructCreate(parse_POToTranslateConfig);
	
	if (!ParserReadTextFile(configFileName, parse_POToTranslateConfig, pConfig, 0))
	{
		printf("Errors while reading %s, unable to proceed\n", configFileName);
		return;
	}

	RunFromConfig(pConfig);
	

	if( !sbUnattendedMode )
	{
		printf("Press a key to exit.\n");
		(void)_getch();
	}
//	consoleSetColor(0, COLOR_GREEN | COLOR_HIGHLIGHT);


	EXCEPTION_HANDLER_END

}


#include "POToTranslateFiles_c_ast.c"
