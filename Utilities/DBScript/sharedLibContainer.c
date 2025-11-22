#include "sharedLibContainer.h"
#include "file.h"
#include "timing.h"

// ---------------------------------------------------------------------------------------
// XPath Helpers

bool xlookup(const char *xpath, Container *con, XPathLookup *p, bool requireStruct)
{
	bool found = true;
	PERFINFO_AUTO_START_FUNC();

	if(!strcmp(xpath, ".") || xpath[0] == 0)
	{
		p->tpi    = con->containerSchema->classParse;
		p->ptr    = con->containerData;
		p->column = 0;
		p->index  = 0;
	}
	else
	{
		found = objPathResolveField(xpath, con->containerSchema->classParse, con->containerData, &p->tpi, &p->column, &p->ptr, &p->index, OBJPATHFLAG_TRAVERSEUNOWNED);
		if(found && requireStruct)
		{
			if(TOK_HAS_SUBTABLE(p->tpi[p->column].type) && p->tpi[p->column].subtable)
			{
				p->tpi = p->tpi[p->column].subtable;
				p->column = 0;
			}
			else
			{
				found = false;
				p->tpi = NULL;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return found;
}

int xcount(const char *xpath, Container *con)
{
	int count = 0;
	XPathLookup l;
	PERFINFO_AUTO_START_FUNC();
	if(xlookup(xpath, con, &l, false))
		count = TokenStoreGetNumElems(l.tpi, l.column, l.ptr, NULL);
	PERFINFO_AUTO_STOP();
	return count;
}

VarType xtype(const char *xpath, Container *con, StructTokenType *structTypeOut)
{
	VarType varType = VARTYPE_UNKNOWN;
	XPathLookup l;

	PERFINFO_AUTO_START_FUNC();
	if(xlookup(xpath, con, &l, false))
	{
		varType = VARTYPE_NORMAL;
		if(structTypeOut) *structTypeOut = l.tpi[l.column].type;
		if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(l.tpi[l.column].type)) && (l.index == -1))
			varType = VARTYPE_ARRAY;
		else if(l.column == 0 || TOK_HAS_SUBTABLE(l.tpi[l.column].type))
			varType = VARTYPE_STRUCT;
	}

	PERFINFO_AUTO_STOP();
	return varType;
}
