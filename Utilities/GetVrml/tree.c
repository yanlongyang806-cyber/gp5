#include <string.h>
#include "stdtypes.h"
#include "stdio.h"
#include "tree.h"
#include "stdlib.h"
#include "utils.h"
#include "error.h"

#include "MemoryPool.h"
#include "earray.h"

#include "tree_h_ast.h"

#define MAX_NODES 40000

MP_DEFINE(Node);

Node	*tree_root = NULL, *tree_root_tail = NULL;

Node	*node_array[MAX_NODES];
int		node_array_count;

#if 0
#define checkit(x)
#else
void checkit(Node *node)
{
	if (tree_root && tree_root->prev)
		FatalErrorf("gfxtree");
	if (tree_root_tail && tree_root_tail->next)
		FatalErrorf("gfxtree");
	if (node->child == node || node->parent == node || node->next == node || node->prev == node)
		FatalErrorf("gfxtree");
	if ((int)node->child == -1 || (int)node->parent == -1 || (int)node->next == -1 || (int)node->prev == -1)
		FatalErrorf("gfxtree");
}
#endif

void check2(Node *node)
{
int		a=1,b=0;

	if (!node)
		return;
		if (((int)node->parent < 0)
		|| ((int)node->prev < 0)
		|| ((int)node->nodeptr < 0))
			a = a / b;
}

void checknode(Node *node)
{

	for(;node;node=node->next)
	{
		check2(node);
		check2(node->parent);
		check2(node->prev);
		check2(node->nodeptr);
		checknode(node->child);
	}
}

void checktree()
{
	checknode(tree_root);
}

Node * getTreeRoot()
{
	checktree();
	return tree_root;
}

///######################## Delete Nodes ######################################
static void freePosKeys(PosKeys *keys)
{
	if (keys->pfTimes)
		free(keys->pfTimes);
	if (keys->pvPos)
		free(keys->pvPos);
}

static void freeRotKeys(RotKeys *keys)
{
	if (keys->pfTimes)
		free(keys->pfTimes);
	if (keys->pvAxisAngle)
		free(keys->pvAxisAngle);
}

void freeNode(Node *node, Node ***nodelist)
{
	eaDestroyEx(&node->api.altpivot, NULL);
	freePosKeys(&node->poskeys);
	freeRotKeys(&node->rotkeys);
#if GETVRML
	if (node->reductions)
	{
		freeGMeshReductions(node->reductions);
		node->reductions = 0;
	}
#endif
	eaDestroyEx(&node->mesh_names, NULL);
	eaDestroyEx(&node->properties, NULL);
	if (nodelist)
		eaFindAndRemove(nodelist, node);
	gmeshFreeData(&node->mesh);
	MP_FREE(Node, node);
}

static void freeTreeNodes(Node *node, Node ***nodelist)
{
	Node *next;

	for(;node;node = next)
	{
		checkit(node);
		if (node->child)
			freeTreeNodes(node->child, nodelist);
		next = node->next;
		freeNode(node, nodelist);
	}
}

void treeDelete(Node *node, Node ***nodelist)
{
	if (!node)
		return;
	checkit(node);

	freeTreeNodes(node->child, nodelist);
	node->child = 0;

	if (node == tree_root_tail)
		tree_root_tail = node->prev;

	if (node->prev)
	{
		node->prev->next = node->next;
		if (node->next)
			node->next->prev = node->prev;
	}
	else if (node->parent)
	{
		node->parent->child = node->next;
		if (node->next)
		{
			node->next->parent = node->parent;
			node->next->prev = 0;
		}
	}
	else
	{
		if (node->next)
			tree_root = node->next;
		else
			tree_root = node->child;
		if (tree_root)
		{
			tree_root->parent = 0;
			tree_root->prev	  = 0;
		}
	}
	checkit(node);

	freeNode(node, nodelist);

	if(tree_root)
		checkit(tree_root);
	if(tree_root_tail)
		checkit(tree_root_tail);
}

////////##################### Insert New Node ##################################
Node *newNode()
{
	MP_CREATE(Node, 256);
	return MP_ALLOC(Node);
}


Node *treeInsert(Node *parent)
{
Node	*curr;

	curr = newNode();
	curr->parent = parent;

	if (!parent)
	{
		if (!tree_root )
		{
			tree_root = curr;
			tree_root_tail = curr;
		}
		else
		{
			{
				assert(!tree_root_tail->next);	
				tree_root_tail->next = curr;
				curr->prev = tree_root_tail;
				tree_root_tail = curr;
			}
		}
	}
	else
	{
		curr->next = parent->child;
		parent->child = curr;
	}
	if (curr->next)
		curr->next->prev = curr;
	checkit(curr);
	return curr;
}

void treeMove(Node *node, Node *newparent)
{
	assert(node && node->parent && newparent && node->parent != newparent);
	if (node == tree_root_tail)
		tree_root_tail = node->prev;

	if (node->prev)
	{
		node->prev->next = node->next;
		if (node->next)
			node->next->prev = node->prev;
	}
	else if (node->parent)
	{
		node->parent->child = node->next;
		if (node->next)
		{
			node->next->parent = node->parent;
			node->next->prev = 0;
		}
	}

	node->parent = newparent;
	node->next = newparent->child;
	newparent->child = node;

	if (node->next)
		node->next->prev = node;

	checkit(node);
}

void treeMoveChildren(Node *source, Node *dest)
{
	assert(source && dest);
	while (source->child)
	{
		treeMove(source->child,dest);
	}
}

///############# End Insert ######################


static void treeArrayNode(Node *node,Node ***nodelist)
{
	for(;node;node = node->next)
	{
		if (eaFind(nodelist,node) >= 0)
			continue;
		eaPush(nodelist, node);
		if (node->child)
			treeArrayNode(node->child,nodelist);
	}
}

void treeArray(Node *parent,Node ***nodelist)
{
	treeArrayNode(parent, nodelist);
}

void treeFree()
{
	while (tree_root)
		treeDelete(tree_root, NULL);
	assert(tree_root == 0);
	assert(tree_root_tail == 0);
}

Node *treeFindRecurse(char *name,Node *node)
{
	Node	*found=0;

	for(;node;node = node->next)
	{
		if (stricmp(name,node->name)==0)
			return node;
		if (node->child)
			found = treeFindRecurse(name,node->child);
		if (found)
			return found;
	}
	return 0;
}


Node *treeFindNode(char *name)
{
	return treeFindRecurse(name,tree_root);
}

#include "tree_h_ast.c"