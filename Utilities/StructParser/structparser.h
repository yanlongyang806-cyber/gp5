#ifndef _STRUCTPARSER_H_
#define _STRUCTPARSER_H_

#include "stdio.h"
#include "tokenizer.h"
#include "windows.h"
#include "sourceparserbaseclass.h"


#define MAX_FIELDS 512
#define MAX_STRUCTS 4096

#define MAX_ENUMS 1024

#define MAX_REDUNDANT_NAMES_PER_FIELD 4

#define MAX_IGNORES 32

#define MAX_END_STRINGS 4

#define MAX_ENUM_EXTRA_NAMES 12

#define MAX_WIKI_COMMENTS 16

#define MAX_MACROS 128

#define MAX_USER_FLAGS 8

#define MAX_UNIONS 16

//both the maximum depth of a flatembed tree, and the most times a single
//struct can be flat embedded in any other struct tree
#define MAX_FLATEMBED_RECURSE_DEPTH 32

#define MAX_AUTO_TP_FUNC_OPTS 64

#define MAX_TP_FUNC_OPT_ARG_STRING 400

#define MAX_TP_FUNC_OPT_ARGS 32


typedef enum
{
	DATATYPE_NONE,
	DATATYPE_INT,
	DATATYPE_FLOAT,
	DATATYPE_CHAR,
	DATATYPE_STRUCT,
	DATATYPE_LINK,
	DATATYPE_ENUM,
	DATATYPE_VOID,
	DATATYPE_VEC2,
	DATATYPE_VEC3,
	DATATYPE_VEC4,
	DATATYPE_IVEC2,
	DATATYPE_IVEC3,
	DATATYPE_IVEC4,
	DATATYPE_MAT3,
	DATATYPE_MAT4,
	DATATYPE_QUAT,
	DATATYPE_MULTIVAL,

	DATATYPE_STASHTABLE,
	DATATYPE_TOKENIZERPARAMS,
	DATATYPE_TOKENIZERFUNCTIONCALL,

	DATATYPE_REFERENCE,


	DATATYPE_UNKNOWN,

	//special data types that are only set after processing various commands
	DATATYPE_FILENAME,
	DATATYPE_CURRENTFILE,
	DATATYPE_TIMESTAMP,
	DATATYPE_LINENUM,
	DATATYPE_BOOLFLAG,
	DATATYPE_RAW,
	DATATYPE_POINTER,
	DATATYPE_RGB,
	DATATYPE_RG,
	DATATYPE_RGBA,

	DATATYPE_STRUCT_POLY,

	DATATYPE_BIT,

	DATATYPE_MAT44,

	//special types... normally a mat4 or mat3 is written as a pyr, or a pyr + pos. But 
	//they can optionally be written as just 12 or 16 floats, but in that case we set
	//up a special multiline thing which redundantly reads the pyr form
	DATATYPE_MAT3_ASMATRIX,
	DATATYPE_MAT4_ASMATRIX,
} enumDataType;

#define DATATYPE_FIRST_SPECIAL DATATYPE_FILENAME

typedef enum
{
	STORAGETYPE_EMBEDDED,
	STORAGETYPE_ARRAY, //embedded array
	STORAGETYPE_EARRAY,
} enumDataStorageType;

typedef enum
{
	REFERENCETYPE_DIRECT,
	REFERENCETYPE_POINTER,
} enumDataReferenceType;

//formatting options are nice and simple in that no more than one of them is allowed, so we can just track
//which one is turned on

//NOTE NOTE NOTE must be kept in sync with the RW_FORMAT_foo
typedef enum
{
	FORMAT_NONE,
	FORMAT_IP,
	FORMAT_KBYTES,
	FORMAT_FRIENDLYDATE,
	FORMAT_FRIENDLYSS2000,
	FORMAT_FRIENDLYCPU,
	FORMAT_PERCENT,
	FORMAT_HSV,
	FORMAT_HSV_OFFSET,
	FORMAT_TEXTURE,
	FORMAT_COLOR,

	FORMAT_COUNT,
} enumFormatType;
//NOTE NOTE NOTE must be kept in sync with the RW_FORMAT_foo


//formatting flags can be on or off individually
//NOTE NOTE NOTE must be kept in sync with the RW_FORMAT_FLAG_foo
typedef enum
{
	FORMAT_FLAG_UI_LEFT,
	FORMAT_FLAG_UI_RIGHT,
	FORMAT_FLAG_UI_RESIZABLE,
	FORMAT_FLAG_UI_NOTRANSLATE_HEADER,
	FORMAT_FLAG_UI_NOHEADER,
	FORMAT_FLAG_UI_NODISPLAY,

	FORMAT_FLAG_COUNT,
} enumFormatFlag;
//NOTE NOTE NOTE must be kept in sync with the RW_FORMAT_FLAG_foo


typedef struct
{
	char indexString[MAX_NAME_LENGTH];
	char nameString[MAX_NAME_LENGTH];
} FIELD_INDEX;

#define MAX_REDUNDANT_STRUCTS 4

typedef struct
{
	char name[MAX_NAME_LENGTH];
	char subTable[MAX_NAME_LENGTH];
} REDUNDANT_STRUCT_INFO;

//a "STRUCT_COMMAND" is something that is not actually related to a field of the struct,
//but which will cause any auto-generated UI for that struct to contain a button which,
//when pressed, will generate a command. Each button has a name and a string
typedef struct STRUCT_COMMAND
{
	char *pCommandName;
	char *pCommandString;
	char *pCommandExpression;
	struct STRUCT_COMMAND *pNext;
} STRUCT_COMMAND;

typedef struct
{
	char baseStructFieldName[MAX_NAME_LENGTH];
	char curStructFieldName[MAX_NAME_LENGTH * 2 + 5];
	char userFieldName[MAX_NAME_LENGTH];
	char typeName[MAX_NAME_LENGTH];

	int iNumRedundantNames;
	char redundantNames[MAX_REDUNDANT_NAMES_PER_FIELD][MAX_NAME_LENGTH];

	int iNumRedundantStructInfos;
	REDUNDANT_STRUCT_INFO redundantStructs[MAX_REDUNDANT_STRUCTS];


	int iNumAsterisks;
	int iArrayDepth;
	bool bArray;
	bool bOwns;

	enumDataType eDataType;
	enumDataStorageType eStorageType;
	enumDataReferenceType eReferenceType;
	char arraySizeString[MAX_NAME_LENGTH];

	bool bBitField;

	enumFormatType eFormatType;
	int lvWidth;
	bool bFormatFlags[FORMAT_FLAG_COUNT];

	bool bFoundSpecialConstKeyword;
	bool bFoundConst;


	//when we find tokens that affect the actual basic data type, we store the tokens here and process them later
	bool bFoundFileNameToken;
	bool bFoundCurrentFileToken;
	bool bFoundTimeStampToken;
	bool bFoundLineNumToken;
	bool bFoundFlagsToken;
	bool bFoundBoolFlagToken;
	bool bFoundRawToken;
	bool bFoundPointerToken; 
	bool bFoundVec3Token;
	bool bFoundVec2Token;
	bool bFoundRGBToken;
	bool bFoundRGToken;
	bool bFoundRGBAToken;
	bool bFoundIntToken;

	//whether an included struct is a container, when it can't be auto-determined
	bool bFoundForceContainer;

	// Misc bits
	bool bFoundRedundantToken;
	bool bFoundStructParam;

	bool bFoundPersist;
	bool bFoundNoTransact;
	bool bFoundSometimesTransact;

	bool bFoundVolatileRef;
	bool bIsInheritanceStruct;

	bool bFoundRequestIndexDefine;

	bool bFoundLateBind;

	bool bFoundUsedField;

	bool bFoundSelfOnly;

	bool bFoundSubscribe;

	bool bFoundSimpleInheritance;

	bool bFoundBlockEArray;

	bool bFoundAsMatrixToken;

	int iMinBits;
	int iPrecision;
	int iFloatAccuracy;

	char subTableName[MAX_NAME_LENGTH];
	char structTpiName[MAX_NAME_LENGTH];

	//we save the file name and line number of this field for error reporting
	char fileName[MAX_PATH];
	int iLineNum;

	char defaultString[TOKENIZER_MAX_STRING_LENGTH + 1];
	char rawSizeString[TOKENIZER_MAX_STRING_LENGTH + 1];
	char pointerSizeString[TOKENIZER_MAX_STRING_LENGTH + 1];

	int iNumIndexes;
	FIELD_INDEX *pIndexes; //yes, this is misspelled, but it keeps it symmetrical with iNumIndexes

	int iNumWikiComments;
	char *pWikiComments[MAX_WIKI_COMMENTS];

	//used during dumping to make redundant structs work
	char *pCurOverrideTPIName;

	bool bFlatEmbedded;
	char flatEmbeddingPrefix[MAX_NAME_LENGTH];

	//source file where the 
	char *pStructSourceFileName;

	int iNumUserFlags;
	char userFlags[MAX_USER_FLAGS][MAX_NAME_LENGTH];

	char refDictionaryName[MAX_NAME_LENGTH];

	//at what index in the parse table was this field exported
	//
	//Has to be an array because a struct foo might have a struct
	//bar embedded in it twice. In that case, a field in bar has
	//more than one indexInParseTable
	int iIndicesInParseTable[MAX_FLATEMBED_RECURSE_DEPTH];
	int iCurIndexCount;

	//found AST(STRUCT(x, and need to force a particular type of struct
	bool bForceOptionalStruct;
	bool bForceEArrayOfStructs;

	//commands that should come immediately after this field
	STRUCT_COMMAND *pFirstCommand;

	STRUCT_COMMAND *pFirstBeforeCommand; //commands that should come immediately before this field

	//stuff for polymorphism
	bool bIAmPolyParentTypeField;
	bool bIAmPolyChildTypeField;
	char myPolymorphicType[MAX_NAME_LENGTH];

	//which "type flag" strings were found, indices into sTokTypeFlags
	unsigned int iTypeFlagsFound;

	//indices into sTokTypeFlags_NoRedundantRepeat
	unsigned int iTypeFlags_NoRedundantRepeatFound;

	//this field is a struct, but instead of embedding that struct's wiki inside this struct's wiki, put it
	//parallel on the page and set up a hotlink to it
	bool bDoWikiLink;

	//format string built up by repeated calls to FORMATSTRING(x = 16 , y = "hi there")
	char *pFormatString;
	
	//this field is a dirty bit for its parent
	bool bIsDirtyBit;

} STRUCT_FIELD_DESC;



typedef struct
{
	char *pIn;
	int iInLength;
	char *pOut;
	int iOutLength;
} AST_MACRO_STRUCT;

class StructParser : public SourceParserBaseClass
{
public:
	StructParser();
	virtual ~StructParser();

public:
	virtual void SetProjectPathAndName(char *pProjectPath, char *pProjectName);
	virtual bool LoadStoredData(bool bForceReset);

	virtual void ResetSourceFile(char *pSourceFileName);

	virtual bool WriteOutData(void);

	virtual char *GetMagicWord(int iWhichMagicWord);

	//note that iWhichMagicWord can be MAGICWORD_BEGINING_OF_FILE or MAGICWORD_END_OF_FILE
	virtual void FoundMagicWord(char *pSourceFileName, Tokenizer *pTokenizer, int iWhichMagicWord, char *pMagicWordString);

	//returns number of dependencies found
	virtual int ProcessDataSingleFile(char *pSourceFileName, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE]);

	virtual bool DoesFileNeedUpdating(char *pFileName);

private:
	typedef struct
	{
		int iFirstFieldNum;
		int iLastFieldNum;
		char name[MAX_NAME_LENGTH];
	} UnionStruct;

	typedef struct STRUCT_DEF
	{
		char sourceFileName[MAX_PATH];
		char structName[MAX_NAME_LENGTH];
		int iNumFields;
		STRUCT_FIELD_DESC *pStructFields[MAX_FIELDS];

		char creationCommentFieldName[MAX_NAME_LENGTH];


		int iNumIgnores;
		char ignores[MAX_IGNORES][MAX_NAME_LENGTH];
		bool bIgnoresAreStructParam[MAX_IGNORES];

		int iNumIgnoreStructs;
		char ignoreStructs[MAX_IGNORES][MAX_NAME_LENGTH];

		int iLongestUserFieldNameLength;

		bool bHasStartString;
		char startString[MAX_NAME_LENGTH];

		int iNumEndStrings;
		char endStrings[MAX_END_STRINGS][MAX_NAME_LENGTH];

		bool bStripUnderscores;

		char *pMainWikiComment;

		int iNumUnions;
		UnionStruct unions[MAX_UNIONS];
		bool bCurrentlyInsideUnion;

		bool bNoPrefixStripping;
		bool bForceUseActualFieldName;
		bool bAlwaysIncludeActualFieldNameAsRedundant;
		bool bIsContainer;
		bool bIsForceConst;

		bool bSingleThreadedMemPool;
		bool bThreadSafeMemPool;
		bool bNoMemTracking;

		bool bNoUnrecognized; 
		bool bRuntimeModified;
		bool bSaveOriginalCaseFieldNames;

		
		//precise starting location of this file, so that the container non-const copying code can return to that precise point
		int iPreciseStartingOffsetInFile;
		int iPreciseStartingLineNumInFile;

		//string set in AST_FORALL for this struct
		char forAllString[TOKENIZER_MAX_STRING_LENGTH];


		//stuff for polymorphism
		char structNameIInheritFrom[MAX_NAME_LENGTH];
		bool bIAmAPolymorphicParent;
		int iParentTypeExtraCount; //how many extra lines to allocate in the polytable


		//prefix and suffix to put around the non-const definition... needed for Sam's crazy macro force-private stuff
		char *pNonConstPrefixString;
		char *pNonConstSuffixString;

		//auto-fixup func name
		char fixupFuncName[MAX_NAME_LENGTH];


		//stuff for tracking recursive links while writing out wiki pages
		bool bLinkedTo;
		bool bWrittenOutAlready;

		char *pStructLevelFormatString;

		IfDefStack *pIfDefStack;


		struct STRUCT_DEF *pRecursionBlocker;
	} STRUCT_DEF;

	int m_iNumStructs;
	STRUCT_DEF *m_pStructs[MAX_STRUCTS];


//callback function called by RecurseOverAllFieldsAndFlatEmbeds
	typedef void FieldRecurseCB(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, 
		int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields,		
		void *pUserData);

	typedef struct
	{
		char inCodeName[MAX_NAME_LENGTH];
		int iNumExtraNames;
		char extraNames[MAX_ENUM_EXTRA_NAMES][MAX_NAME_LENGTH];
		char *pWikiComment;
	} ENUM_ENTRY;

	typedef struct
	{
		char enumName[MAX_NAME_LENGTH];
		char enumToAppendTo[MAX_NAME_LENGTH];
		char enumAppendOtherToMe[MAX_NAME_LENGTH];
		int iNumEntries;
		int iNumAllocatedEntries;
		ENUM_ENTRY *pEntries;
		char sourceFileName[MAX_PATH];
		bool bNoPrefixStripping;
		int iPadding;
		char *pMainWikiComment;
		char embeddedDynamicName[MAX_NAME_LENGTH];
		IfDefStack *pIfDefStack;
	} ENUM_DEF;

	typedef struct AUTO_TP_FUNC_OPT_STRUCT
	{
		char funcTypeName[MAX_NAME_LENGTH];
		char resultType[MAX_NAME_LENGTH];
		char fullArgString[MAX_TP_FUNC_OPT_ARG_STRING];
		char recurseArgString[MAX_TP_FUNC_OPT_ARG_STRING];
		char sourceFileName[MAX_PATH];
	} AUTO_TP_FUNC_OPT_STRUCT;




	int m_iNumEnums;
	ENUM_DEF *m_pEnums[MAX_ENUMS];


	int m_iNumMacros;
	AST_MACRO_STRUCT m_Macros[MAX_MACROS];

	
	int m_iNumAutoTPFuncOpts;
	AUTO_TP_FUNC_OPT_STRUCT m_AutoTpFuncOpts[MAX_AUTO_TP_FUNC_OPTS];

	char *m_pPrefix;
	char *m_pSuffix;

	
	char m_ProjectName[MAX_PATH];

private:
	void DumpExterns(FILE *pFile, STRUCT_DEF *pStruct);
	void DumpStruct(FILE *pFile, STRUCT_DEF *pStruct);
	void DumpStructPrototype(FILE *pFile, STRUCT_DEF *pStruct);

	bool ReadSingleField(Tokenizer *pTokenizer, STRUCT_FIELD_DESC *pField, STRUCT_DEF *pStruct);
	int DumpField(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName, int iLongestUserFieldNameLength, char *pStructFieldNamePrefix,
		char *pUserFieldNamePrefix);
	bool IsLinkName(char *pString);
	bool IsOutlawedTypeName(char *pString);

	void FixupFieldTypes(STRUCT_DEF *pStruct);
	void FixupFieldTypes_RightBeforeWritingData(STRUCT_DEF *pStruct);

	void TemplateFileNameFromSourceFileName(char *pTemplateName, char *pTemplateHeaderName, char *pSourceName);

	static bool IsFloatName(char *pString);
	static bool IsIntName(char *pString);
	static bool IsCharName(char *pString);

	void DumpFieldFormatting(FILE *pFile, STRUCT_FIELD_DESC *pField);

	int DumpFieldDirectEmbedded(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName, int iLongestFieldNameLength, int iIndexInMultiLineField, char *pUserFieldNamePrefix);
	int DumpFieldDirectArray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName);
	int DumpFieldDirectEarray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName);
	int DumpFieldPointerEmbedded(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName);
	int DumpFieldPointerArray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName);
	int DumpFieldPointerEarray(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName);

	void ProcessStructFieldCommandString(STRUCT_DEF *pStruct, STRUCT_FIELD_DESC *pField, char *pSourceString, 
		int iNumNotStrings, char **ppNotStrings, char *pFileName, int iLineNum,
		Tokenizer *pMainTokenizer);
	void AddExtraNameToField(Tokenizer *pTokenizer, STRUCT_FIELD_DESC *pField, char *pName, bool bKeepOriginalName);

	//pUserFieldNamePrefix has alreayd been prepended to pUserFieldName if necessary. It is included in case we need
	//to recurse
	int DumpFieldSpecifyUserFieldName(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pStructName, char *pUserFieldName, 
		bool bNameIsRedundant, int iLongestFieldNameLength, int iIndexInMultiLineField, char *pUserFieldNamePrefix);

	char *GetIntPrefixString(STRUCT_FIELD_DESC *pField);
	char *GetFloatPrefixString(STRUCT_FIELD_DESC *pField);

	char *GetFieldTpiName(STRUCT_FIELD_DESC *pField, bool bIgnoreLateBind);

	void PrintIgnore(FILE *pFile, char *pIgnoreString, int iLongestFieldNameLength, bool bIgnoreIsStructParam, bool bIgnoreIsStruct);

	int PrintStructStart(FILE *pFile, STRUCT_DEF *pStruct);
	int PrintStructEnd(FILE *pFile, STRUCT_DEF *pStruct);
	bool IsStructAllStructParams(STRUCT_DEF *pStruct);

	void FixupFieldName(STRUCT_FIELD_DESC *pField, bool bStripUnderscores, bool bNoPrefixStripping, bool bForceUseActualFieldName,
		bool bAlwaysIncludeActualFieldNameAsRedundant);

	void WriteHeaderFileStart(FILE *pFile, char *pSourceName);
	void WriteHeaderFileEnd(FILE *pFile, char *pSourceName);

	void CalcLongestUserFieldName(STRUCT_DEF *pStruct);

	void FoundStructMagicWord(char *pSourceFileName, Tokenizer *pTokenizer);
	void FoundEnumMagicWord(char *pSourceFileName, Tokenizer *pTokenizer);
	void FoundAutoTPFuncOptMagicWord(char *pSourceFileName, Tokenizer *pTokenizer);

	void DumpEnum(FILE *pFile, ENUM_DEF *pEnum);
	void DumpEnumPrototype(FILE *pFile, ENUM_DEF *pEnum);

	void DumpAutoTpFuncOpt(FILE *pFile, AUTO_TP_FUNC_OPT_STRUCT *pFunc);

	void WriteOutDataSingleFile(char *pFileName);
	void WikiFileNameFromSourceFileName(char *pWikiFileName, char *pSourceName);

	bool StructHasWikiComments(STRUCT_DEF *pStruct);
	void DumpStructToWikiFile(FILE *pFile, STRUCT_DEF *pStruct);
	void DumpStructFieldsToWikiFile(FILE *pFile, STRUCT_DEF *pStruct, int iDepth, bool bIncludeStartAndEndToks, STRUCT_DEF *pParent);
	bool StringRequiresWikiBackslash(char *pString);

	char *GetWikiTypeString(STRUCT_FIELD_DESC *pField, char *pIndexString);
	void DumpWikiFieldName(FILE *pFile, STRUCT_FIELD_DESC *pField, char *pPrefixString, char *pOverrideName);

	bool FieldDumpsItselfCompletely(STRUCT_FIELD_DESC *pField);

	void DeleteStruct(int iStructIndex);
	void DeleteEnum(int iStructIndex);

	void FindDependenciesInStruct(char *pSourceFileName, STRUCT_DEF *pStruct, int *piNumDependencies, char *pDependencies[MAX_DEPENDENCIES_SINGLE_FILE]);

	void ResetMacros(void);

	void FoundMacro(char *pSourceFileName, Tokenizer *pTokenizer);
	void FoundSuffix(char *pSourceFileName, Tokenizer *pTokenizer);
	void FoundPrefix(char *pSourceFileName, Tokenizer *pTokenizer);
	void FoundFixupFunc(char *pSourceFileName, Tokenizer *pTokenizer);

	void ReplaceMacrosInString(char *pString, int iOutStringLength);
	int ReplaceMacroInString(char *pString, AST_MACRO_STRUCT *pMacro, int iCurLength, int iMaxLength);

	char *GetAllUserFlags(STRUCT_FIELD_DESC *pField);

	STRUCT_DEF *FindNamedStruct(char *pStructName);

	ENUM_ENTRY *GetNewEntry(ENUM_DEF *pEnum);

	void AttemptToDeduceReferenceDictionaryName(STRUCT_FIELD_DESC *pField, char *pTypeName);

	char *GetFieldSpecificPrefixString(STRUCT_DEF *pStruct, int iFieldNum);

	void CheckOverallStructValidity(Tokenizer *pTokenizer, STRUCT_DEF *pStruct);
	void CheckOverallStructValidity_PostFixup(STRUCT_DEF *pStruct);
	void DumpStructInitFunc(FILE *pFile, STRUCT_DEF *pStruct);

	void DumpNonConstCopy(FILE *pFile, STRUCT_DEF *pStruct, bool bForceConstStruct);

	void DumpIndexDefines(FILE *pFile, STRUCT_DEF *pStruct, STRUCT_FIELD_DESC *pField, int iStartingOffset);


	void DumpCommand(	FILE *pFile, STRUCT_FIELD_DESC *pField, STRUCT_COMMAND *pCommand, int iLongestFieldNameLength);

	void DumpPolyTable(FILE *pFile, STRUCT_DEF *pStruct);
	
	bool StringIsContainerName(STRUCT_DEF *pStruct, char *pString);

	bool StringIsForceConstStructName(STRUCT_DEF *pStruct, char *pString);

	void RecurseOverAllFieldsAndFlatEmbeds(STRUCT_DEF *pParentStruct, STRUCT_DEF *pStruct, FieldRecurseCB *pCB, int iRecurseDepth, void *pUserData);
	static void AreThereLateBinds(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField,int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData);
	static void DumpLateBindFixups(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields,void *pUserData);

	int PrintStructTableInfoColumn(FILE *pFile, STRUCT_DEF *pStruct);

	int GetNumLinesFieldTakesUp(STRUCT_FIELD_DESC *pField);
	char *GetMultiLineFieldNamePrefix(STRUCT_FIELD_DESC *pField, int iIndex);

	static void AreThereBitFields(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields,void *pUserData);
	static void DumpBitFieldFixups(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields,void *pUserData);
	static void SetupFloatDefaults(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields, void *pUserData);
	ENUM_DEF *FindEnumByName(char *pName);
	void DumpEnumInWikiForField(ENUM_DEF *pEnum, char **ppOutEString);
	void DumpEnumToWikiFile(FILE *pOutFile, ENUM_DEF *pEnum);

	void AssertNameIsLegalForFormatStringString(Tokenizer *pTokenizer, char *pName);
	void AssertNameIsLegalForFormatStringInt(Tokenizer *pTokenizer, char *pName);

	void AddStringToFormatString(char **ppFormatString, char *pStringToAdd);
	void ReadTokensIntoFormatString(Tokenizer *pTokenizer, char **ppFormatString);

	void AssertFieldHasNoDefault(STRUCT_FIELD_DESC *pField, char *pErrorString);

	//goes over all structs and fields and sets the iCurIndexCount to 0, so that 
	//the indicesInParent stuff will properly re-count up each time we recurse over
	//latebinds
	void ResetAllStructFieldIndicesInStruct(STRUCT_DEF *pStruct);
	void ResetAllStructFieldIndices(void);

	bool FieldHasTypeFlag(STRUCT_FIELD_DESC *pField, char *pFlagName);
	void VerifyStructWithCountOfFieldsWritten(STRUCT_DEF *pStruct, int iCount);
	static void CheckUsedFieldSize(STRUCT_DEF *pParentStruct, STRUCT_FIELD_DESC *pField, int iRecurseDepth, STRUCT_FIELD_DESC **ppRecurse_fields,void *pUserData);

	void AddTokTypeFlagByString(Tokenizer *pTokenizer, STRUCT_FIELD_DESC *pField, char *pFlagString);


};

#endif