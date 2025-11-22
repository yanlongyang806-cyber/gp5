#ifndef _TREE_H
#define _TREE_H

#include "GenericMesh.h"
#include "wlModelEnums.h"
#include "wlModel.h"

typedef struct BoneData
{
	const char** boneNames;
} BoneData;

typedef struct PosKeys
{
	U32 uiCount;
	Vec3* pvPos;
	F32* pfTimes; 
} PosKeys;

typedef struct RotKeys
{
	U32 uiCount;
	Vec4* pvAxisAngle;
	F32* pfTimes; 
} RotKeys;

// Temporary holding struct for alt pivot info
AUTO_STRUCT;
typedef struct AltPivotInfo
{
	const char* modelname; AST(KEY POOL_STRING)
	const char* filename; AST(CURRENTFILE)
	AltPivot** altpivot;
} AltPivotInfo;

typedef struct Node Node;

typedef struct Node
{
	char		name[200];
	Mat4		mat;
	Vec3		dynPos;
	Vec3		child_translate;

	Node		*next,*child;
	PosKeys		poskeys;
	RotKeys		rotkeys;
	
	GMesh		mesh;
	BoneData	bones;

	F32			radius;
	Vec3		min, max;
	F32			lightmap_size;

	Node 		*parent;	// these are accessed rarely, try to keep them
	Node 		*prev;		// in a separate cache line
	Node 		*nodeptr;	// for list alloc / dealloc not tree

	AltPivotInfo api;

	GMeshReductions	*reductions;
	F32			lod_distances[3];

	char		**mesh_names;
	Vec3		applied_transform;

	bool		no_lods;
	bool		no_tri_optimization;
	bool		high_precision_texcoord;
	bool		wind, trunk_wind;
	bool		high_detail_high_lod;
	bool		alpha_tri_sort;
	bool		vert_color_sort;

	U32			seed;
	char		**properties; // { name, value, name, value, etc... }

	int			id_override;

	const char	*attachment_bone;
} Node;



void checkit(Node *node);
void check2(Node *node);
void checknode(Node *node);
void checktree();
void treeDelete(Node *node, Node ***nodelist);
Node *newNode();
void freeNode(Node *node, Node ***nodelist);
Node *treeInsert(Node *parent);
void treeArray(Node *parent,Node ***nodelist);
void treeFree();
Node *treeFindRecurse(char *name,Node *node);
Node *treeFindNode(char *name);
Node * getTreeRoot();
Node **treeArrayOfAnimNodes(Node *parent,int *count);
void treeMove(Node *node, Node *newparent);
void treeMoveChildren(Node *source, Node *dest);

#endif
