#include "sourceparserbaseclass.h"
#include "SourceParser.h"
#include "tokenizer.h"

SourceParserBaseClass::SourceParserBaseClass() :
	m_pAdditionalSimpleInvisibleTokens(NULL)
{}

SourceParserBaseClass::~SourceParserBaseClass()
{}

void SourceParserBaseClass::FoundMagicWord(char *, Tokenizer *pTokenizer, int, char *)
{
	pTokenizer->SetAdditionalSimpleInvisibleTokens(m_pAdditionalSimpleInvisibleTokens);
}

char *SourceParserBaseClass::GetAutoGenCFileName1(void)
{
	return NULL;
}
	
char *SourceParserBaseClass::GetAutoGenCFileName2(void)
{
	return NULL;
}


bool SourceParserBaseClass::HasRelevantIfDefs(IfDefStack *pStack)
{
	int i;

	if (!pStack || pStack->iNumIfDefs == 0)
	{
		return false;
	}

	for (i=0; i < pStack->iNumIfDefs; i++)
	{
		if (m_pParent->DoesVariableHaveValue("Relevant_Ifdefs", pStack->ifDefs[i].defineName, false))
		{
			return true;
		}
	}

	return false;
}
void SourceParserBaseClass::WriteRelevantIfsToFile(FILE *pFile, IfDefStack *pStack)
{
	int i;
	if (!pFile)
	{
		return;
	}
	if (!pStack || pStack->iNumIfDefs == 0)
	{
		return;
	}

	for (i=0; i < pStack->iNumIfDefs; i++)
	{
		if (m_pParent->DoesVariableHaveValue("Relevant_Ifdefs", pStack->ifDefs[i].defineName, false))
		{
			if (pStack->ifDefs[i].bDefine)
			{
				if (pStack->ifDefs[i].bNot)
				{
					fprintf(pFile, "#ifndef ");
				}
				else
				{
					fprintf(pFile, "#ifdef ");
				}
			}
			else
			{
				if (pStack->ifDefs[i].bNot)
				{
					fprintf(pFile, "#if !");
				}
				else
				{
					fprintf(pFile, "#if ");
				}
			}

			fprintf(pFile, "%s\n", pStack->ifDefs[i].defineName);
		}
	}
}

void SourceParserBaseClass::WriteRelevantEndIfsToFile(FILE *pFile, IfDefStack *pStack)
{
	int i;
	if (!pFile)
	{
		return;
	}

	if (!pStack || pStack->iNumIfDefs == 0)
	{
		return;
	}

	for (i=pStack->iNumIfDefs; i-->0;)
	{
		if (m_pParent->DoesVariableHaveValue("Relevant_Ifdefs", pStack->ifDefs[i].defineName, false))
		{
			fprintf(pFile, "#endif //%s\n", pStack->ifDefs[i].defineName);
		}
	}
}