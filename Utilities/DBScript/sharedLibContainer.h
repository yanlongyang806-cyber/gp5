#pragma once

#include "objContainerIO.h"
#include "tokenstore.h"

typedef enum VarType
{
	VARTYPE_NORMAL = 0,
	VARTYPE_ARRAY,
	VARTYPE_STRUCT,
	VARTYPE_UNKNOWN
} VarType;

typedef struct XPathLookup
{
	ParseTable *tpi;
	void *ptr;
	int column;
	int index;
} XPathLookup;

bool xlookup(const char *xpath, Container *con, XPathLookup *p, bool requireStruct);
int xcount(const char *xpath, Container *con);
VarType xtype(const char *xpath, Container *con, StructTokenType *structTypeOut);
