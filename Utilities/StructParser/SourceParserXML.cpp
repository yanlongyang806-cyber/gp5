#include "SourceParserXML.h"
#include "stdio.h"
#include "windows.h"
#include "tokenizer.h"
#include "utils.h"

#undef assert
#define assert STATICASSERT

#undef assertf
#define assertf STATICASSERTF

#pragma comment(lib, "libexpatMT.lib")

//Global macro for linking expat
#define XML_LARGE_SIZE 1
#define XML_STATIC 1
#include "../../3rdparty/expat-2.0.1/lib/expat.h"
#include "../../3rdparty/expat-2.0.1/xmlwf/xmlfile.h"

#define stricmp _stricmp
#define strdup _strdup

#define STARTING_LIST_SIZE 4
#define STARTING_CDATA_CAPACITY 64

typedef struct XMLNodeAttribList
{
	XMLNodeAttrib **ppAttribs;
	int iCapacity;
	int iSize;
} XMLNodeAttribList;


typedef struct XMLNode
{
	char *pName;
	char *pCData;
	int iCDataCapacity;
	int iCDataSize;

	XMLNodeAttribList attribs;
	XMLNodeList children;
	XMLNode *pParent;
} XMLNode;

void XMLNodeAttrib_Destroy(XMLNodeAttrib *pAttrib)
{
	if (pAttrib->pName)
	{
		free(pAttrib->pName);
	}
	if (pAttrib->pValue)
	{
		free(pAttrib->pValue);
	}
	free(pAttrib);
}

void XMLNodeAttribList_Clear(XMLNodeAttribList *pList)
{
	int i;

	for (i=0; i < pList->iSize; i++)
	{
		XMLNodeAttrib_Destroy(pList->ppAttribs[i]);
	}

	if (pList->ppAttribs)
	{
		free(pList->ppAttribs);
	}

	memset(pList, 0, sizeof(XMLNodeAttribList));
}

void XMLNodeAttribList_Push(XMLNodeAttribList *pList, XMLNodeAttrib *pAttrib)
{
	if (pList->iCapacity == 0)
	{
		pList->ppAttribs = (XMLNodeAttrib**)calloc(sizeof(void*) * STARTING_LIST_SIZE, 1);
		pList->iCapacity = STARTING_LIST_SIZE;
		pList->iSize = 1;
		pList->ppAttribs[0] = pAttrib;
		return;
	}

	if (pList->iCapacity == pList->iSize)
	{
		XMLNodeAttrib **ppNewAttribs = (XMLNodeAttrib**)calloc(sizeof(void*) * pList->iCapacity * 2, 1);
		memcpy(ppNewAttribs, pList->ppAttribs, sizeof(void*) * pList->iCapacity);
		free(pList->ppAttribs);
		pList->ppAttribs = ppNewAttribs;
		pList->iCapacity *= 2;
	}
	
	pList->ppAttribs[pList->iSize++] = pAttrib;
}

void XMLNodeAttribList_Remove(XMLNodeAttribList *pList, int i)
{
	assert(i >= 0 && i < pList->iSize, "Index out of range for AttribList_Remove");
	XMLNodeAttrib_Destroy(pList->ppAttribs[i]);
	pList->iSize--;
	if (i == pList->iSize)
	{
		return;
	}

	memmove(&pList->ppAttribs[i], &pList->ppAttribs[i+1], sizeof(void*) * (pList->iSize - i));
}

//debugging only
void XMLNodeAttribList_Dump(XMLNodeAttribList *pList, char *pPrefix)
{
	int i;
	
	printf("%s%d attribs\n", pPrefix, pList->iSize);
	for (i=0; i < pList->iSize; i++)
	{
		printf("%s%d: %s -- %s\n", pPrefix, i, pList->ppAttribs[i]->pName, pList->ppAttribs[i]->pValue ? pList->ppAttribs[i]->pValue : "(NULL)");
	}
	printf("\n\n");
}
/*
void XMLNoteAttribList_Test(void)
{
	XMLNodeAttribList list = {0};

	int i;

	for (i =0; i < 10; i++)
	{
		char temp[64];
		XMLNodeAttrib *pAttrib = (XMLNodeAttrib *)calloc(sizeof(XMLNodeAttrib), 1);
		sprintf(temp, "Name %d", i);
		pAttrib->pName = strdup(temp);
		sprintf(temp, "Val %d", i);
		pAttrib->pValue = strdup(temp);

		XMLNodeAttribList_Push(&list, pAttrib);
	}

	XMLNodeAttribList_Dump(&list);

	XMLNodeAttribList_Remove(&list, 1);
	XMLNodeAttribList_Remove(&list, 2);
	XMLNodeAttribList_Remove(&list, 3);

	XMLNodeAttribList_Dump(&list);

	XMLNodeAttribList_Clear(&list);

}
*/


void XMLNodeList_Clear(XMLNodeList *pList, bool bDestroyChildren)
{
	int i;

	if (bDestroyChildren)
	{
		for (i=0; i < pList->iSize; i++)
		{
			XMLNode_Destroy(pList->ppNodes[i]);
		}
	}

	if (pList->ppNodes)
	{
		free(pList->ppNodes);
	}

	memset(pList, 0, sizeof(XMLNodeList));
}


int XMLNodeList_Size(XMLNodeList *pList)
{
	return pList->iSize;
}
XMLNode *XMLNodeList_GetNode(XMLNodeList *pList, int iIndex)
{
	assert(iIndex >= 0 && iIndex < pList->iSize, "Invalid List_GetNode index");
	return pList->ppNodes[iIndex];
}

void XMLNodeList_Push(XMLNodeList *pList, XMLNode *pNode)
{
	if (pList->iCapacity == 0)
	{
		pList->ppNodes = (XMLNode**)calloc(sizeof(void*) * STARTING_LIST_SIZE, 1);
		pList->iCapacity = STARTING_LIST_SIZE;
		pList->iSize = 1;
		pList->ppNodes[0] = pNode;
		return;
	}

	if (pList->iCapacity == pList->iSize)
	{
		XMLNode **ppNewNodes = (XMLNode**)calloc(sizeof(void*) * pList->iCapacity * 2, 1);
		memcpy(ppNewNodes, pList->ppNodes, sizeof(void*) * pList->iCapacity);
		free(pList->ppNodes);
		pList->ppNodes = ppNewNodes;
		pList->iCapacity *= 2;
	}
	
	pList->ppNodes[pList->iSize++] = pNode;
}

void XMLNodeList_Remove(XMLNodeList *pList, int i)
{
	assert(i >= 0 && i < pList->iSize, "Index out of range for List_Remove");
	XMLNode_Destroy(pList->ppNodes[i]);
	pList->iSize--;
	if (i == pList->iSize)
	{
		return;
	}

	memmove(&pList->ppNodes[i], &pList->ppNodes[i+1], sizeof(void*) * (pList->iSize - i));
}

void XMLNode_Destroy(XMLNode *pNode)
{
	if (pNode->pName)
	{
		free(pNode->pName);
	}
	if (pNode->pCData)
	{
		free(pNode->pCData);
	}

	XMLNodeList_Clear(&pNode->children, true);
	XMLNodeAttribList_Clear(&pNode->attribs);
	free(pNode);
}

char *XMLNode_GetName(XMLNode *pNode)
{
	return pNode->pName;
}

char *XMLNode_GetCData(XMLNode *pNode)
{
	return pNode->pCData;
}

int XMLNode_GetNumAttribs(XMLNode *pNode)
{
	return pNode->attribs.iSize;
}

XMLNodeAttrib *XMLNode_GetNthAttrib(XMLNode *pNode, int i)
{
	assert(i >= 0 && i < pNode->attribs.iSize, "index out of range for " __FUNCTION__);
	return pNode->attribs.ppAttribs[i];
}

XMLNodeAttrib *XMLNode_FindAttrib(XMLNode *pNode, char *pName)
{
	int i;

	for (i=0; i < pNode->attribs.iSize; i++)
	{
		if (pNode->attribs.ppAttribs[i]->pName && stricmp(pNode->attribs.ppAttribs[i]->pName, pName) == 0)
		{
			return pNode->attribs.ppAttribs[i];
		}
	}

	return NULL;
}

void XMLNode_RemoveAttrib(XMLNode *pNode, int i)
{
	XMLNodeAttribList_Remove(&pNode->attribs, i);
}

int XMLNode_GetNumChildren(XMLNode *pNode)
{
	return pNode->children.iSize;
}

XMLNode *XMLNode_GetNthChild(XMLNode *pNode, int i)
{
	assert(i >= 0 && i < pNode->children.iSize, "index out of range for " __FUNCTION__);
	return pNode->children.ppNodes[i];
}

XMLNode *XMLNode_FindChild(XMLNode *pNode, char *pName)
{
	int i;

	for (i=0; i < pNode->children.iSize; i++)
	{
		if (pNode->children.ppNodes[i]->pName && stricmp(pNode->children.ppNodes[i]->pName, pName) == 0)
		{
			return pNode->children.ppNodes[i];
		}
	}

	return NULL;
}

void XMLNode_RemoveChild(XMLNode *pNode, int i)
{
	XMLNodeList_Remove(&pNode->children, i);
}

XMLNode *XMLNode_GetParent(XMLNode *pNode)
{
	return pNode->pParent;
}

XMLNode *XMLNode_Create(char *pName, char **ppAttribs)
{
	XMLNode *pRetVal = (XMLNode*)calloc(sizeof(XMLNode), 1);
	int i;

	pRetVal->pName = strdup(pName);

	if (ppAttribs)
	{
		i = 0;

		while (ppAttribs[i])
		{
			XMLNodeAttrib *pAttrib = (XMLNodeAttrib*)calloc(sizeof(XMLNodeAttrib), 1);
			pAttrib->pName = strdup(ppAttribs[i]);
			if (ppAttribs[i+1])
			{
				pAttrib->pValue = strdup(ppAttribs[i+1]);
			}

			XMLNodeAttribList_Push(&pRetVal->attribs, pAttrib);

			i += 2;
		}
	}

	return pRetVal;
}

XMLNode *gpRoot = NULL;
XMLNode *gpCurrent = NULL;

void XMLNode_Start(void *data, const XML_Char *el, const XML_Char **attr)
{
	if (!gpRoot)
	{
		gpRoot = XMLNode_Create((char*)el, (char**)attr);
		gpCurrent = gpRoot;
	}
	else
	{
		XMLNode *pNew = XMLNode_Create((char*)el, (char**)attr);
		pNew->pParent = gpCurrent;
		XMLNodeList_Push(&gpCurrent->children, pNew);
		gpCurrent = pNew;
	}
}

void XMLNode_CData(void *data, const XML_Char *s, int len)
{
	if (gpCurrent)
	{
		if (!gpCurrent->pCData)
		{
			gpCurrent->pCData = (char*)calloc(STARTING_CDATA_CAPACITY, 1);
			gpCurrent->iCDataCapacity = STARTING_CDATA_CAPACITY;
			gpCurrent->iCDataSize = 0;
		}
		
		int iNewLen = gpCurrent->iCDataSize + len;
		
		if (iNewLen >= gpCurrent->iCDataCapacity)
		{
			int iNewCapacity;
			iNewCapacity = gpCurrent->iCDataCapacity * 2;
			while (iNewCapacity <= iNewLen)
			{
				iNewCapacity *= 2;
			}

			char *pNewCDataBuf = (char*)calloc(iNewCapacity, 1);
			if (gpCurrent->iCDataSize)
			{
				memcpy(pNewCDataBuf, gpCurrent->pCData, gpCurrent->iCDataSize);
			}
			free(gpCurrent->pCData);
			gpCurrent->pCData = pNewCDataBuf;
			gpCurrent->iCDataCapacity = iNewCapacity;
		}
		

		memcpy(gpCurrent->pCData + gpCurrent->iCDataSize, s, len);
		gpCurrent->iCDataSize += len;
	}
}


void XMLNode_End(void *data, const XML_Char *el)
{
	assert(gpCurrent, "Got misplaced XMLNode_End");
	assert(stricmp(gpCurrent->pName, (char*)el) == 0, "Mismatched START/END");

	gpCurrent = gpCurrent->pParent;
}

XMLNode *XMLNode_ParseFromBuffer(char *pBuffer, int iBufSize)
{
	XMLNode *pRetVal;
	int i;

	for (i=0; i < iBufSize; i++)
	{
		if (pBuffer[i] == '\r')
		{
			pBuffer[i] = ' ';
		}
	}

	gpRoot = gpCurrent = NULL;

	assert(pBuffer && iBufSize, "Can't XMLNode_ParseFromBuffer an empty buffer");

	XML_Parser p = XML_ParserCreate(NULL);

	XML_SetElementHandler(p, XMLNode_Start, XMLNode_End);
	XML_SetCharacterDataHandler(p, XMLNode_CData);


	if (XML_Parse(p, pBuffer, iBufSize, 1) == XML_STATUS_ERROR)
	{
		assertf(0, "Parse error at line %" XML_FMT_INT_MOD "u:\n%s\n",
              XML_GetCurrentLineNumber(p),
              XML_ErrorString(XML_GetErrorCode(p)));
	}

	XML_ParserFree(p);

	pRetVal = gpRoot;
	gpRoot = NULL;
	gpCurrent = NULL;

	return pRetVal;

}

void XMLNode_Dump(XMLNode *pNode, int iIndent)
{
	char *pPrefix = (char*)malloc(iIndent + 1);
	int i;

	for (i=0; i < iIndent; i++)
	{
		pPrefix[i] = ' ';
	}

	pPrefix[iIndent] = 0;

	printf("%sNode %s\n", pPrefix, pNode->pName);

	if (pNode->iCDataSize)
	{
		printf("%sCData <<%s>>\n", pPrefix, pNode->pCData);
	}

	XMLNodeAttribList_Dump(&pNode->attribs, pPrefix);

	printf("%s%d children:\n", pPrefix, pNode->children.iSize);

	for (i=0; i < pNode->children.iSize; i++)
	{
		printf("%s(%s's child %d/%d)\n", pPrefix, pNode->pName, i, pNode->children.iSize);
		XMLNode_Dump(pNode->children.ppNodes[i], iIndent + 3);
	}


	free(pPrefix);
}
/*
void XML_Test(void)
{
	int iBufSize;
	char *pBuf = fileAlloc("c:\\temp\\GameServerLib.vcxproj", &iBufSize);
	int i;

	XMLNode *pParent = XMLNode_ParseFromBuffer(pBuf, iBufSize);
	XMLNodeList list = {0};


	XMLNode_SearchWithPath(&list, pParent, "Project.ItemGroup.ClCompile");

	printf("Found %d files\n", XMLNodeList_Size(&list));
	for (i=0; i < XMLNodeList_Size(&list); i++)
	{
		XMLNode_Dump(XMLNodeList_GetNode(&list, i), 0);
	}

	XMLNode_Destroy(pParent);
	free(pBuf);
}
*/

//condition string will be something like "foo=bar". commas and parentheses and equals are illegal
bool XMLNode_NodeMatchesAttribConditionString(XMLNode *pNode, char *pAttribString)
{
	char *pEquals = strchr(pAttribString, '=');
	XMLNodeAttrib *pAttrib;

	assertf(pEquals, "Badly formatted search string %s", pAttribString);

	*pEquals = 0;

	pAttrib = XMLNode_FindAttrib(pNode, pAttribString);

	if (!pAttrib || !pAttrib->pValue)
	{
		return false;
	}

	if (stricmp(pAttrib->pValue, pEquals + 1) == 0)
	{
		return true;
	}

	return false;
}


bool XMLNode_NodeMatchesSearchString(XMLNode *pNode, char *pStr, int iStrLen)
{
	char temp[1024];
	char *pFirstParen;

	assertf(iStrLen < 1024, "Search string %s is too long", pStr);
	memcpy(temp, pStr, iStrLen);
	temp[iStrLen] = 0;

	if ((pFirstParen = strchr(temp, '(')))
	{
		*pFirstParen = 0;
		if (stricmp(pNode->pName, temp) != 0)
		{
			return false;
		}

		char *pCloseParen = strchr(pFirstParen + 1, ')');
		assertf(pCloseParen, "Badly formatted search string %s", pStr);

		*pCloseParen = 0;

		return XMLNode_NodeMatchesAttribConditionString(pNode, pFirstParen + 1);
	}
	else
	{
		return stricmp(pNode->pName, temp) == 0;
	}
}



void XMLNode_SearchWithPath(XMLNodeList *pOutList, XMLNode *pNode, char *pPath)
{
	char *pFirstDot;

	if (!pPath || !pPath[0])
	{
		return;
	}


	pFirstDot = strchr(pPath, '.');
	if (pFirstDot)
	{
		if (XMLNode_NodeMatchesSearchString(pNode, pPath, (int)(pFirstDot - pPath)))
		{
			int i;

			for (i=0; i < pNode->children.iSize; i++)
			{
				XMLNode_SearchWithPath(pOutList, pNode->children.ppNodes[i], pFirstDot + 1);
			}
		}

		return;
		
	}
	else
	{
		if (XMLNode_NodeMatchesSearchString(pNode, pPath, (int)strlen(pPath)))
		{
			XMLNodeList_Push(pOutList, pNode);
		}
	}
}


void XMLNode_RecursivelyPruneWithCallBack(XMLNode *pNode, XMLNodePruneCB callback, void *pUserData)
{
	int i;

	for (i=pNode->children.iSize - 1; i >= 0; i--)
	{
		if (callback(pNode->children.ppNodes[i], pUserData))
		{
			XMLNodeList_Remove(&pNode->children, i);
		}
		else
		{
			XMLNode_RecursivelyPruneWithCallBack(pNode->children.ppNodes[i], callback, pUserData);
		}
	}
}