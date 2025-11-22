#pragma once

//when reading/writing a .po file, all the information from a single message block is represented by one
//of these
AUTO_STRUCT;
typedef struct POBlockRaw
{
	char *pDescription; AST(ESTRING)
	char **ppKeys; AST(ESTRING)
	char **ppFiles; AST(ESTRING)
	char **ppScopes; AST(ESTRING)
	char *pCtxt; AST(ESTRING)
	char *pID; AST(ESTRING)
	char *pStr; AST(ESTRING)
	char **ppAlternateTrans; AST(ESTRING)

	char *pReadFileName;
	int iLineNum;
} POBlockRaw;

void ReadTranslateBlocksFromFile(char *pFileName, POBlockRaw ***pppBlocks);