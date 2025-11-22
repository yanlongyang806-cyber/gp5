#include "AutoRunManager.h"
#include "strutils.h"
#include "sourceparser.h"
#include "earray.h"

//when writing out the functions that all AUTO_RUNs, write this many func calls per line, to compact the file
#define AUTO_RUNS_PER_LINE 5


#define AUTORUN_WILDCARD_PREFIX "AUTO_RUN_"

AutoRunManager::AutoRunManager()
{
	m_bSomethingChanged = false;
	m_ppAutoRuns = NULL;
	m_AutoRunFileName[0] = 0;
	m_ShortAutoRunFileName[0] = 0;	

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
		NULL
	};

	m_pAdditionalSimpleInvisibleTokens = pAdditionalSimpleInvisibleTokens;
}

AutoRunManager::~AutoRunManager()
{
	eaDestroyEx(&m_ppAutoRuns, (EArrayItemCallback)DeleteAutoRun);
}

char *AutoRunManager::GetMagicWord(int iWhichMagicWord)
{
	switch (iWhichMagicWord)
	{
	case AUTORUN_ORDER_FIRST:
		return "AUTO_RUN_FIRST";
	case AUTORUN_ORDER_SECOND:
		return "AUTO_RUN_SECOND";
	case AUTORUN_ORDER_EARLY:
		return "AUTO_RUN_EARLY";
	case AUTORUN_ORDER_POSTINTERNAL:
		return "AUTO_RUN_POSTINTERNAL";
	case AUTORUN_ORDER_NORMAL:
		return "AUTO_RUN";
	case AUTORUN_ORDER_LATE:
		return "AUTO_RUN_LATE";
	case AUTORUN_ANON:
		return "AUTO_RUN_ANON";
	case AUTORUN_STARTUP:
		return "AUTO_STARTUP";
	case AUTORUN_ORDER_FILE:
		return "AUTO_RUN_FILE";
	case AUTORUN_WILDCARD:
		return AUTORUN_WILDCARD_PREFIX WILDCARD_STRING;
	}

	return "x x";
}


typedef enum
{
	RW_PARSABLE = RW_COUNT,
};

static char *sAutoRunReservedWords[] =
{
	"PARSABLE",
	NULL
};

static StringTree *spAutoRunReservedWordTree = NULL;

void AutoRunManager::SetProjectPathAndName(char *pProjectPath, char *pProjectName)
{
	strcpy(m_ProjectName, pProjectName);

	sprintf(m_ShortAutoRunFileName, "%s_autorun_autogen", pProjectName);
	sprintf(m_AutoRunFileName, "%s\\AutoGen\\%s.c", pProjectPath, m_ShortAutoRunFileName);


}

bool AutoRunManager::DoesFileNeedUpdating(char *pFileName)
{
	return false;
}


bool AutoRunManager::LoadStoredData(bool bForceReset)
{
	if (bForceReset)
	{
		m_bSomethingChanged = true;
		return false;
	}

	Tokenizer tokenizer;

	if (!tokenizer.LoadFromFile(m_AutoRunFileName))
	{
		m_bSomethingChanged = true;
		return false;
	}

	if (!tokenizer.IsStringAtVeryEndOfBuffer("#endif"))
	{
		m_bSomethingChanged = true;
		return false;
	}

	tokenizer.SetExtraReservedWords(sAutoRunReservedWords, &spAutoRunReservedWordTree);

	Token token;
	enumTokenType eType;

	do
	{
		eType = tokenizer.GetNextToken(&token);
		STATICASSERT(eType != TOKEN_NONE, "AUTORUN data corruption");
	} while (!(eType == TOKEN_RESERVEDWORD && token.iVal == RW_PARSABLE));

	tokenizer.SetCSourceStyleStrings(true);

	tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find number of autruns");

	int iNumAutoRuns = token.iVal;

	for (int iAutoRunNum = 0; iAutoRunNum < iNumAutoRuns; iAutoRunNum++)
	{
		AUTO_RUN_STRUCT *pAutoRun = (AUTO_RUN_STRUCT*)calloc(sizeof(AUTO_RUN_STRUCT), 1);

		memset(pAutoRun, 0, sizeof(AUTO_RUN_STRUCT));

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_AUTORUN_COMMAND_LENGTH, "Didn't find autorun function name");
		strcpy(pAutoRun->functionName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, MAX_PATH, "Didn't find autorun source file");
		strcpy(pAutoRun->sourceFileName, token.sVal);

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find autorun order");
		pAutoRun->iOrder = token.iVal;

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find autorun declarations");

		if (token.sVal[0])
		{
			char temp[TOKENIZER_MAX_STRING_LENGTH];
			RemoveCStyleEscaping(temp, token.sVal);
			pAutoRun->pDeclarations = STRDUP(temp);
		}
		else
		{
			pAutoRun->pDeclarations = NULL;
		}
	
		
		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find autorun code");

		if (token.sVal[0])
		{
			char temp[TOKENIZER_MAX_STRING_LENGTH];
			RemoveCStyleEscaping(temp, token.sVal);
			pAutoRun->pCode = STRDUP(temp);
		}
		else
		{
			pAutoRun->pCode = NULL;
		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find pAutoStartupName");
		if (token.sVal[0])
		{
			pAutoRun->pAutoStartupName = STRDUP(token.sVal);
		}

		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_STRING, 0, "Didn't find pAutoStartupDependencyString");
		if (token.sVal[0])
		{
			pAutoRun->pAutoStartupDependencyString = STRDUP(token.sVal);
		}
	
		tokenizer.AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find bAutoStartupStartsOn");
		pAutoRun->bAutoStartupStartsOn = token.iVal;

		pAutoRun->pIfDefStack = ReadIfDefStackFromFile(&tokenizer);

		eaPush(&m_ppAutoRuns, pAutoRun);
	}

	m_bSomethingChanged = false;

	return true;
}

void AutoRunManager::DeleteAutoRun(AUTO_RUN_STRUCT *pAutoRun)
{
	if (pAutoRun->pIfDefStack)
	{
		DestroyIfDefStack(pAutoRun->pIfDefStack);
		pAutoRun->pIfDefStack = NULL;
	}

	if (pAutoRun->pDeclarations)
	{
		delete(pAutoRun->pDeclarations);
	}

	if (pAutoRun->pCode)
	{
		delete(pAutoRun->pCode);
	}

	if (pAutoRun->pAutoStartupName)
	{
		delete(pAutoRun->pAutoStartupName);
	}

	if (pAutoRun->pAutoStartupDependencyString)
	{
		delete(pAutoRun->pAutoStartupDependencyString);
	}

	free(pAutoRun);
}


void AutoRunManager::ResetSourceFile(char *pSourceFileName)
{
	int i = 0;

	while (i < eaSize(&m_ppAutoRuns))
	{
		if (AreFilenamesEqual(m_ppAutoRuns[i]->sourceFileName, pSourceFileName))
		{
			DeleteAutoRun(m_ppAutoRuns[i]);
			eaRemove(&m_ppAutoRuns, i);

			m_bSomethingChanged = true;
		}
		else
		{
			i++;
		}
	}
}

int AutoRunComparator(const void *p1, const void *p2)
{
	AUTO_RUN_STRUCT *pAutoRun1 = *((AUTO_RUN_STRUCT**)p1);
	AUTO_RUN_STRUCT *pAutoRun2 = *((AUTO_RUN_STRUCT**)p2);

	return strcmp(pAutoRun1->functionName, pAutoRun2->functionName);
}

void AutoRunManager::WriteOutAnonAutoRunIncludes(FILE *pOutFile)
{
	//check if *_AnonAutoRunIncludes.h is included in the parent file
	int i;
	int iNumFiles = m_pParent->GetNumProjectFiles();

	for (i=0; i < iNumFiles; i++)
	{
		char *pFileName = m_pParent->GetNthProjectFile(i);

		if (strstri(pFileName, "AnonAutoRunIncludes.h"))
		{
			fprintf(pOutFile, "\n//including %s, so that ANON_AUTORUNs will get the header files they need\n#include \"%s\"\n", pFileName, pFileName);
		}
	}

}


bool AutoRunManager::WriteOutData(void)
{
	int iAutoRunNum;

	if (!m_bSomethingChanged)
	{
		return false;
	}

	qsort(m_ppAutoRuns, eaSize(&m_ppAutoRuns), sizeof(void*), AutoRunComparator);

	FILE *pOutFile = fopen_nofail(m_AutoRunFileName, "wt");

	fprintf(pOutFile, "//This file contains data and prototypes for autoruns. It is autogenerated by StructParser\n\n// autogenerated" "nocheckin\n");


	fprintf(pOutFile, "//This file contains data and prototypes for autoruns. It is autogenerated by StructParser\n\n// autogenerated" "nocheckin\n#include\"metatask.h\"\nextern int giCurAutoRunStep;\n");

	WriteOutAnonAutoRunIncludes(pOutFile);

	//print out function bodies for any autoruns that have internal bodies/declarations

	for (iAutoRunNum = 0; iAutoRunNum < eaSize(&m_ppAutoRuns); iAutoRunNum++)
	{
		AUTO_RUN_STRUCT *pAutoRun = m_ppAutoRuns[iAutoRunNum];
	
		WriteRelevantIfsToFile(pOutFile, pAutoRun->pIfDefStack);

		if (pAutoRun->pCode)
		{
			fprintf(pOutFile, "//declarations/code for internal autorun %s\n", pAutoRun->functionName);

			if (pAutoRun->pDeclarations)
			{
				//pDeclarations has a newline at the end already
				fprintf(pOutFile, "%s", pAutoRun->pDeclarations);
			}

			fprintf(pOutFile, "static void %s(void)\n{ %s }\n", pAutoRun->functionName, pAutoRun->pCode);
		}

		if (pAutoRun->pAutoStartupName)
		{
			fprintf(pOutFile, "extern void %s(void);\n", pAutoRun->functionName);
			fprintf(pOutFile, "static void AUTOFIX_STARTUP_%s(void)\n{\n", pAutoRun->functionName);
			fprintf(pOutFile, "\tMetaTask_AddTask(\"startup\", \"%s\", %s, \"%s\", true, %s, \"%s\", METATASK_BEHAVIOR_ONLY_ONCE);\n}\n",
				pAutoRun->pAutoStartupName, pAutoRun->functionName, pAutoRun->functionName, 
				pAutoRun->bAutoStartupStartsOn ? "true" : "false",
				pAutoRun->pAutoStartupDependencyString ? pAutoRun->pAutoStartupDependencyString : "");

		}

		WriteRelevantEndIfsToFile(pOutFile, pAutoRun->pIfDefStack);

	}


	int iNewlineCounter = 0;


	for (iAutoRunNum = 0; iAutoRunNum < eaSize(&m_ppAutoRuns); iAutoRunNum++)
	{
		AUTO_RUN_STRUCT *pAutoRun = m_ppAutoRuns[iAutoRunNum];

		if (HasRelevantIfDefs(pAutoRun->pIfDefStack))
		{
			if (iNewlineCounter % AUTO_RUNS_PER_LINE != 1)
			{
				fprintf(pOutFile, "\n");
			}
			iNewlineCounter = 0;
		}

		WriteRelevantIfsToFile(pOutFile, pAutoRun->pIfDefStack);
		fprintf(pOutFile, "extern void %s(void);%s", pAutoRun->functionName, (iNewlineCounter++) % AUTO_RUNS_PER_LINE == 0 ? "\n" : " ");
		WriteRelevantEndIfsToFile(pOutFile, pAutoRun->pIfDefStack);
	}

	int iLibNum, iOrderNum;

	for (iLibNum = 0; iLibNum < m_pParent->GetNumLibraries(); iLibNum++)
	{
		for (iOrderNum = 0; iOrderNum < AUTORUN_ORDER_COUNT; iOrderNum++)
		{
			fprintf(pOutFile, "void doAutoRuns_%s_%d(void);\n", m_pParent->GetNthLibraryName(iLibNum), iOrderNum);
		}
	}

	for (iOrderNum = 0; iOrderNum < AUTORUN_ORDER_COUNT; iOrderNum++)
	{
		iNewlineCounter = 0;
		bool bWroteNewlinesLastTime = true;


		fprintf(pOutFile, "\nvoid doAutoRuns_%s_%d(void)\n{\n", 
			m_pParent->GetShortProjectName(), iOrderNum);

		fprintf(pOutFile, "\tstatic int once = 0;\n\tif (once) return;\n\tonce = 1;\n");
		fprintf(pOutFile, "\tgiCurAutoRunStep = %d;\n", iOrderNum);


		for (iLibNum = 0; iLibNum < m_pParent->GetNumLibraries(); iLibNum++)
		{	
			if (m_pParent->IsNthLibraryXBoxExcluded(iLibNum))
			{
				fprintf(pOutFile, "\t#if !_XBOX // XBOX excluded\n");
			}
			if (m_pParent->IsNthLibraryPS3Excluded(iLibNum))
			{
				fprintf(pOutFile, "\t#if !_PS3 // PS3 excluded\n");
			}

			fprintf(pOutFile, "\tdoAutoRuns_%s_%d();\n", m_pParent->GetNthLibraryName(iLibNum), iOrderNum);		
	
			if (m_pParent->IsNthLibraryPS3Excluded(iLibNum))
			{
				fprintf(pOutFile, "\t#endif // PS3 excluded\n");
			}
			if (m_pParent->IsNthLibraryXBoxExcluded(iLibNum))
			{
				fprintf(pOutFile, "\t#endif // XBOX excluded\n");
			}
		}

		for (iAutoRunNum = 0; iAutoRunNum < eaSize(&m_ppAutoRuns); iAutoRunNum++)
		{
			AUTO_RUN_STRUCT *pAutoRun = m_ppAutoRuns[iAutoRunNum];


			if (pAutoRun->iOrder == iOrderNum)
			{
				if (HasRelevantIfDefs(pAutoRun->pIfDefStack))
				{
					if (iNewlineCounter % AUTO_RUNS_PER_LINE != 1)
					{
						fprintf(pOutFile, "\n");
					}
					iNewlineCounter = 0;
				}

				WriteRelevantIfsToFile(pOutFile, pAutoRun->pIfDefStack);
				fprintf(pOutFile, "\t%s();%s", pAutoRun->functionName, (iNewlineCounter++) % AUTO_RUNS_PER_LINE == 0 ? "\n" : " ");
				WriteRelevantEndIfsToFile(pOutFile, pAutoRun->pIfDefStack);

			}


		}

		fprintf(pOutFile, "}\n");
	}

	fprintf(pOutFile, "extern void utilitiesLibPreAutoRunStuff(void);\n");

	fprintf(pOutFile, "int MagicAutoRunFunc_%s(void)\n{\n", m_pParent->GetShortProjectName());


	fprintf(pOutFile, "\tutilitiesLibPreAutoRunStuff();\n");

	for (iOrderNum = 0; iOrderNum < AUTORUN_ORDER_COUNT; iOrderNum++)
	{
		if (iOrderNum != AUTORUN_ORDER_FILE)
		{
			fprintf(pOutFile, "\tdoAutoRuns_%s_%d();\n", 
				m_pParent->GetShortProjectName(), iOrderNum);
		}
	}

	fprintf(pOutFile, "\treturn 0;\n}\n");

	fprintf(pOutFile, "int MagicAutoRunFunc_File_%s(void)\n{\n", m_pParent->GetShortProjectName());

		fprintf(pOutFile, "\tdoAutoRuns_%s_%d();\n", 
			m_pParent->GetShortProjectName(), AUTORUN_ORDER_FILE);

	fprintf(pOutFile, "\treturn 0;\n}\n");



	if (m_pParent->ProjectIsExecutable())
	{
		fprintf(pOutFile, "void do_auto_runs(void)\n{\n\tMagicAutoRunFunc_%s();\n}\n",
			m_pParent->GetShortProjectName());
		fprintf(pOutFile, "void do_auto_runs_file(void)\n{\n\tMagicAutoRunFunc_File_%s();\n}\n",
			m_pParent->GetShortProjectName());

		for (iLibNum = 0; iLibNum < m_pParent->GetNumLibraries(); iLibNum++)
		{	
			fprintf(pOutFile, "extern void _%s_AutoRun_SPECIALINTERNAL(void);\n", m_pParent->GetNthLibraryName(iLibNum));
		}

		fprintf(pOutFile, "void do_special_internal_autoruns(void)\n{\n");
		fprintf(pOutFile, "\t_%s_AutoRun_SPECIALINTERNAL();\n", m_pParent->GetShortProjectName());
		for (iLibNum = 0; iLibNum < m_pParent->GetNumLibraries(); iLibNum++)
		{	
			if (m_pParent->IsNthLibraryXBoxExcluded(iLibNum))
			{
				fprintf(pOutFile, "\t#if !_XBOX // XBOX excluded\n");
			}
			if (m_pParent->IsNthLibraryPS3Excluded(iLibNum))
			{
				fprintf(pOutFile, "\t#if !_PS3 // PS3 excluded\n");
			}

			fprintf(pOutFile, "\t_%s_AutoRun_SPECIALINTERNAL();\n", m_pParent->GetNthLibraryName(iLibNum));		
	
			if (m_pParent->IsNthLibraryPS3Excluded(iLibNum))
			{
				fprintf(pOutFile, "\t#endif // PS3 excluded\n");
			}
			if (m_pParent->IsNthLibraryXBoxExcluded(iLibNum))
			{
				fprintf(pOutFile, "\t#endif // XBOX excluded\n");
			}
		}
		fprintf(pOutFile, "};\n");
	}

	fprintf(pOutFile, "\n\n#ifdef THIS_SYMBOL_IS_NOT_DEFINED\nPARSABLE\n");
	
	fprintf(pOutFile, "%d\n", eaSize(&m_ppAutoRuns));

	for (iAutoRunNum = 0; iAutoRunNum < eaSize(&m_ppAutoRuns); iAutoRunNum++)
	{
		AUTO_RUN_STRUCT *pAutoRun = m_ppAutoRuns[iAutoRunNum];
		
		fprintf(pOutFile, "\"%s\" \"%s\" %d ", pAutoRun->functionName, pAutoRun->sourceFileName, pAutoRun->iOrder);

		if (pAutoRun->pDeclarations)
		{
			char temp[TOKENIZER_MAX_STRING_LENGTH + 1000];

			AddCStyleEscaping(temp, pAutoRun->pDeclarations, TOKENIZER_MAX_STRING_LENGTH + 999);

			fprintf(pOutFile, "\"%s\" ", temp);
		}
		else
		{
			fprintf(pOutFile, "\"\" ");
		}

		if (pAutoRun->pCode)
		{
			char temp[TOKENIZER_MAX_STRING_LENGTH + 1000];

			AddCStyleEscaping(temp, pAutoRun->pCode, TOKENIZER_MAX_STRING_LENGTH + 999);

			fprintf(pOutFile, "\"%s\" ", temp);
		}
		else
		{
			fprintf(pOutFile, "\"\" ");
		}

		fprintf(pOutFile, " \"%s\" \"%s\" %d ",
			pAutoRun->pAutoStartupName ? pAutoRun->pAutoStartupName : "",
			pAutoRun->pAutoStartupDependencyString ? pAutoRun->pAutoStartupDependencyString : "",
			pAutoRun->bAutoStartupStartsOn
			);

		WriteIfDefStackToFile(pOutFile, pAutoRun->pIfDefStack);
		fprintf(pOutFile, "\n");
	}

	fprintf(pOutFile, "#endif\n");

	fclose(pOutFile);



	return true;
}
	






void AutoRunManager::FoundMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, int iWhichMagicWord, char *pMagicWordString)
{
	SourceParserBaseClass::FoundMagicWord(pSourceFileName, pTokenizer, iWhichMagicWord, pMagicWordString);

	Token token;
	enumTokenType eType;

	if (iWhichMagicWord ==  MAGICWORD_BEGINNING_OF_FILE || iWhichMagicWord == MAGICWORD_END_OF_FILE)
	{
		return;
	}


	if (iWhichMagicWord == AUTORUN_ANON)
	{
		Token token;
		char funcName[256];
		int iLineNum;

		//all this gibberish is to support replacing __FILE__ in the AUTO_RUN_ANON with the actual filename, properly
		//turned into a C-source-style escaped string
		char fileNameReplaceString1[MAX_PATH * 2 + 10];
		char fileNameReplaceString2[MAX_PATH * 2 + 10];

		char *macros[][2] =
		{
			{
				"__FILE__",
				fileNameReplaceString2,
			},
			{
				NULL,
				NULL
			}
		};

		AddCStyleEscaping(fileNameReplaceString1, pSourceFileName, MAX_PATH * 2 + 5);
		sprintf(fileNameReplaceString2, "\"%s\"", fileNameReplaceString1);


		sprintf(funcName, "%s_ANON_AUTORUN_%d", 
			GetFileNameWithoutDirectories(pSourceFileName), pTokenizer->GetOffset(&iLineNum));
		MakeStringAllAlphaNum(funcName);

		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after AUTO_RUN_WILDCARD");
		pTokenizer->GetSpecialStringTokenWithParenthesesMatching(&token);
		
		ReplaceMacrosInPlace(token.sVal, macros);

		
		AddAutoRunWithBody(funcName, pSourceFileName, "", token.sVal, AUTORUN_ORDER_NORMAL, pTokenizer->GetIfDefStack());
		return;
	}


	AUTO_RUN_STRUCT *pAutoRun = (AUTO_RUN_STRUCT*)calloc(sizeof(AUTO_RUN_STRUCT), 1);
	eaPush(&m_ppAutoRuns, pAutoRun);
	
	m_bSomethingChanged = true;

	strcpy(pAutoRun->sourceFileName, pSourceFileName);

	pAutoRun->pCode = pAutoRun->pDeclarations = NULL;

	pAutoRun->pIfDefStack = CopyIfDefStack(pTokenizer->GetIfDefStack());

	if (iWhichMagicWord == AUTORUN_STARTUP)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after AUTO_STARTUP");
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Didn't find identifier after AUTO_STARTUP");

		pAutoRun->pAutoStartupName = STRDUP(token.sVal);
		
		eType = pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_RESERVEDWORD, RW_COMMA,
			TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Expected , or ) after AUTO_STARTUP(x)");

		if (token.iVal == RW_COMMA)
		{
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_INT, 0, "Didn't find int after AUTO_STARTUP(x,");
			pAutoRun->bAutoStartupStartsOn = token.iVal;
			pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Didn't find ) after AUTO_STARTUP(x,n");
		}

		//check for ASTRT_ commands
		while (1)
		{
			eType = pTokenizer->CheckNextToken(&token);

			if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ASTRT_DEPS") == 0)
			{
				pTokenizer->GetNextToken(&token);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after ASTRT_DEPS");

				while (1)
				{
					eType = pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_IDENTIFIER, 0, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "expected identifier or ) inside ASTRT_DEPS");
					
					if (eType == TOKEN_IDENTIFIER)
					{
						char tempString[1024];
						sprintf(tempString, "%sAFTER %s", pAutoRun->pAutoStartupDependencyString ? ", " : "",
							token.sVal);

						ConcatOntoNewedString(&pAutoRun->pAutoStartupDependencyString, tempString);

						eType = pTokenizer->CheckNextToken(&token);
						if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
						{
							eType = pTokenizer->GetNextToken(&token);
						}
					}
					else
					{
						break;
					}
				};
			}
			else if (eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "ASTRT_CANCELLEDBY") == 0)
			{
				pTokenizer->GetNextToken(&token);
				pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after ASTRT_CANCELLEDBY");

				while (1)
				{
					eType = pTokenizer->Assert2NextTokenTypesAndGet(&token, TOKEN_IDENTIFIER, 0, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "expected identifier or ) inside ASTRT_CANCELLEDBY");
					
					if (eType == TOKEN_IDENTIFIER)
					{
						char tempString[1024];
						sprintf(tempString, "%sCANCELLEDBY %s", pAutoRun->pAutoStartupDependencyString ? ", " : "",
							token.sVal);

						ConcatOntoNewedString(&pAutoRun->pAutoStartupDependencyString, tempString);

						eType = pTokenizer->CheckNextToken(&token);
						if (eType == TOKEN_RESERVEDWORD && token.iVal == RW_COMMA)
						{
							eType = pTokenizer->GetNextToken(&token);
						}
					}
					else
					{
						break;
					}
				};
			}
			else
			{
				break;
			}
		}
		//now all ASTRTs have been gotten, so the next thing we get should be a semicolon
	}


	if (iWhichMagicWord == AUTORUN_WILDCARD)
	{
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after AUTO_RUN_WILDCARD");
		pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, 0, "Didn't find identifier after AUTO_RUN_WILDCARD");

		char autoGenFuncName[4096];

		sprintf(autoGenFuncName, "_AUTOGEN_%s%s", pMagicWordString + strlen(AUTORUN_WILDCARD_PREFIX), token.sVal);

		ASSERTF(pTokenizer, strlen(autoGenFuncName) < MAX_AUTORUN_COMMAND_LENGTH, "assumed AUTO_RUN function name %s too long", 
			autoGenFuncName);

		strcpy(pAutoRun->functionName, autoGenFuncName);

		pAutoRun->iOrder = AUTORUN_ORDER_NORMAL;
		return;

	}







	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_SEMICOLON, "Didn't find ; after AUTO_RUN");

	eType = pTokenizer->GetNextToken(&token);

	ASSERT(pTokenizer, eType == TOKEN_RESERVEDWORD && token.iVal == RW_VOID 
		|| eType == TOKEN_IDENTIFIER && strcmp(token.sVal, "int") == 0, "Expected int or void after AUTO_RUN;");

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_IDENTIFIER, MAX_AUTORUN_COMMAND_LENGTH, "Didn't find auto run name after int");

	strcpy(pAutoRun->functionName, token.sVal);
	pAutoRun->iOrder = iWhichMagicWord;

	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_LEFTPARENS, "Didn't find ( after auto run name");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_VOID, "Didn't find void after (");
	pTokenizer->AssertNextTokenTypeAndGet(&token, TOKEN_RESERVEDWORD, RW_RIGHTPARENS, "Didn't find ) after void");

	if (pAutoRun->pAutoStartupName)
	{
		char fixupStartupName[1024];
		sprintf(fixupStartupName, "AUTOFIX_STARTUP_%s", pAutoRun->functionName);
		AddAutoRunSpecial(fixupStartupName, pSourceFileName, false, AUTORUN_ORDER_EARLY);
	}
}

void AutoRunManager::AddAutoRun(char *pFuncName, char *pSourceFileName)
{
	AddAutoRunWithBody(pFuncName, pSourceFileName, NULL, NULL, AUTORUN_ORDER_INTERNAL, NULL);
}

void AutoRunManager::AddAutoRunWithIfDefs(char *pFuncName, char *pSourceFileName, IfDefStack *pIfDefs)
{
	AddAutoRunWithBody(pFuncName, pSourceFileName, NULL, NULL, AUTORUN_ORDER_INTERNAL, pIfDefs);
}

void AutoRunManager::AddAutoRunWithBody(char *pFuncName, char *pSourceFileName, char *pDeclarations, char *pCode, int iOrder, IfDefStack *pIfDefStack)
{


	AUTO_RUN_STRUCT *pAutoRun = (AUTO_RUN_STRUCT*)calloc(sizeof(AUTO_RUN_STRUCT), 1);
	eaPush(&m_ppAutoRuns, pAutoRun);

	STATICASSERTF(strlen(pFuncName) < MAX_AUTORUN_COMMAND_LENGTH, "AUTORUN name %s too long", pFuncName);

	strcpy(pAutoRun->functionName, pFuncName);
	strcpy(pAutoRun->sourceFileName, pSourceFileName);
	pAutoRun->iOrder = iOrder;

	pAutoRun->pIfDefStack = CopyIfDefStack(pIfDefStack);

	if (pCode)
	{
		pAutoRun->pCode = STRDUP(pCode);
		if (pDeclarations)
		{
			pAutoRun->pDeclarations = STRDUP(pDeclarations);
		}
		else
		{
			pAutoRun->pDeclarations = NULL;
		}
	}
	else
	{
		assert(!pDeclarations);
		pAutoRun->pDeclarations = pAutoRun->pCode = NULL;
	}

	m_bSomethingChanged = true;

}


void AutoRunManager::AddAutoRunSpecial(char *pFuncName, char *pSourceFileName, bool bCheckIfAlreadyExists, int iOrder)
{
	if (bCheckIfAlreadyExists)
	{
		int i;

		for (i=0; i < eaSize(&m_ppAutoRuns); i++)
		{
			if (strcmp(pFuncName, m_ppAutoRuns[i]->functionName) == 0)
			{
				return;
			}
		}
	}

	STATICASSERTF(strlen(pFuncName) < MAX_AUTORUN_COMMAND_LENGTH, "AUTORUN name %s too long", pFuncName);

	AUTO_RUN_STRUCT *pAutoRun = (AUTO_RUN_STRUCT*)calloc(sizeof(AUTO_RUN_STRUCT), 1);
	eaPush(&m_ppAutoRuns, pAutoRun);

	strcpy(pAutoRun->functionName, pFuncName);
	strcpy(pAutoRun->sourceFileName, pSourceFileName);
	pAutoRun->iOrder = iOrder;
	pAutoRun->pDeclarations = pAutoRun->pCode = NULL;

	m_bSomethingChanged = true;



}




//returns number of dependencies found
int AutoRunManager::ProcessDataSingleFile(char *pSourceFileName, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE])
{
	return 0;
}













