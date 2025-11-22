#pragma once

//writing this all in C not C++ because that's what working at Cryptic for 4 years does to you

typedef struct XMLNode XMLNode;


typedef struct XMLNodeAttrib
{
	char *pName;
	char *pValue;
} XMLNodeAttrib;


typedef struct XMLNodeList
{
	XMLNode **ppNodes;
	int iCapacity;
	int iSize;
} XMLNodeList;

XMLNode *XMLNode_ParseFromBuffer(char *pBuffer, int iBufSize);
void XMLNode_Destroy(XMLNode *pNode);
char *XMLNode_GetName(XMLNode *pNode);
char *XMLNode_GetCData(XMLNode *pNode);

int XMLNode_GetNumAttribs(XMLNode *pNode);
XMLNodeAttrib *XMLNode_GetNthAttrib(XMLNode *pNode, int i);
XMLNodeAttrib *XMLNode_FindAttrib(XMLNode *pNode, char *pName);
void XMLNode_RemoveAttrib(XMLNode *pNode, int i);

int XMLNode_GetNumChildren(XMLNode *pNode);
XMLNode *XMLNode_GetNthChild(XMLNode *pNode, int i);
XMLNode *XMLNode_FindChild(XMLNode *pNode, char *pName);
void XMLNode_RemoveChild(XMLNode *pNode, int i);

XMLNode *XMLNode_GetParent(XMLNode *pNode);

//when querying an XML node tree, you often get back potentially multiple
//nodes, so the requests are always done using a list. When you are done with it,
//call XMLNodeList_Clear, with bDestroyChildren FALSE
void XMLNodeList_Clear(XMLNodeList *pList, bool bDestroyChildren);

//main searching function. Takes something like "property.itemgroup.clcompile" and returns a list of
//all nodes that match that "path"
//
//Each step of the key can be a simple string, in which case it's just the name of the node,
//but it can also include a parenthesized name-value pair for attribs which must be met,
//like this: property.ImportGroup(Label=PropertySheets).Import
void XMLNode_SearchWithPath(XMLNodeList *pOutList, XMLNode *pNode, char *pPath);

//returns true if this node and all children should be pruned
typedef bool (*XMLNodePruneCB)(XMLNode *pNode, void *pUserData);

//note that it is impossible to prune the root itself
void XMLNode_RecursivelyPruneWithCallBack(XMLNode *pNode, XMLNodePruneCB callback, void *pUserData);