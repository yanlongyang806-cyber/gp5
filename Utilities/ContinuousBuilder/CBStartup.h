#pragma once

AUTO_STRUCT;
typedef struct CBProduct
{
	char *pProductName; AST(STRUCTPARAM)
	char *pShortProductName; AST(STRUCTPARAM)
	char *pProductDescription;
} CBProduct;


AUTO_STRUCT;
typedef struct CBType
{
	char *pShortTypeName; AST(STRUCTPARAM) //short symbolic name, all caps, no spaces, used in all scripts
	char *pTypeName; //long human-readable name
	char *pTypeDescription;

	char **ppInheritsFrom; //ordered earray of typeNameShorts of other product types to load first

	bool bDontUpdateConfigFiles;
} CBType;

AUTO_STRUCT;
typedef struct CBProductAndTypeList
{
	CBProduct **ppProduct;
	CBType **ppType;

	char **ppConfigFileLocation; 
} CBProductAndTypeList;


void CB_DoStartup(void);

//CB_DoStartup guarantees that the following will be set:

extern CBProduct *gpCBProduct;
extern CBType *gpCBType;
extern CBProductAndTypeList gProductAndTypeList;

bool CBProductIsCore(void);

//returns true if this is a "continuous" CB, that is, one which does the hardwired
//getting and compiling and testing and stuff
//
//this is NOT necessarily the same as just checking the global type because there 
//could be multiple subtypes of CONT
bool CBTypeIsCONT(void); 


bool CBStartup_StringIsProductName(const char *pStr);
bool CBStartup_StringIsBuildTypeName(const char *pStr);