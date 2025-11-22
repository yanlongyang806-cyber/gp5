#ifndef _SOURCEPARSER_BASECLASS_H_
#define _SOURCEPARSER_BASECLASS_H_

#include "tokenizer.h"
#include "filelistloader.h"

#define MAGICWORD_BEGINNING_OF_FILE -1
#define MAGICWORD_END_OF_FILE -2

//generic useful max string length
#define MAX_NAME_LENGTH 128


class SourceParser;

class SourceParserBaseClass
{
public:
	SourceParserBaseClass();
	virtual ~SourceParserBaseClass();

	virtual void SetProjectPathAndName(char *pProjectPath, char *pProjectName) = 0;

	virtual bool LoadStoredData(bool bForceReset) = 0;

	virtual void ResetSourceFile(char *pSourceFileName) = 0;

	virtual bool WriteOutData(void) = 0;

	virtual char *GetMagicWord(int iWhichMagicWord) = 0;

	//note that iWhichMagicWord can be MAGICWORD_BEGINING_OF_FILE or MAGICWORD_END_OF_FILE
	virtual void FoundMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, int iWhichMagicWord, char *pMagicWordString);

	//returns number of dependencies found
	virtual int ProcessDataSingleFile(char *pSourceFileName, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE]) = 0;

	void SetParent(SourceParser *pParent, int iIndex) { m_pParent = pParent; m_iIndexInParent = iIndex;};

	virtual bool DoesFileNeedUpdating(char *pFileName) = 0;

	virtual char *GetAutoGenCFileName1(void);
	virtual char *GetAutoGenCFileName2(void);

	void WriteRelevantIfsToFile(FILE *pFile, IfDefStack *pStack);
	void WriteRelevantEndIfsToFile(FILE *pFile, IfDefStack *pStack);

	bool HasRelevantIfDefs(IfDefStack *pStack);



protected:
	SourceParser *m_pParent;
	int m_iIndexInParent;
	char **m_pAdditionalSimpleInvisibleTokens;

public:
	bool m_bSomethingChanged;


};

#endif