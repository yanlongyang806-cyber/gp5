#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mathutil.h"
#include "Quat.h"

#include "file.h"
#include "logging.h"
#include "error.h"
#include "earray.h"
#include "crypt.h"
#include "qsortG.h"
#include "utils.h"
#include "textparser.h"
#include "MemoryPool.h"
#include "StringCache.h"

#include "main.h"
#include "NVMeshMenderWrapper.h"
#include "manifold.h"
#include "uvunwrap.h"
#include "tree.h"
#include "geo.h"
#include "vrml.h"
#include "output.h"

#include "wlModel.h"
#include "wlAutoLOD.h"
#include "ObjectLibrary.h"
#include "wlEditorIncludes.h"
#include "dynSkeleton.h"

//////////////////////////////////////////////////////////////////////////


// Tex names are actually just any packed name we are using for this format, right now textures and bones
const char **tex_names;

MP_DEFINE(ModelTexName);
ModelTexName **model_tex_names;

void texNameClear(int addwhite)
{
	MP_CREATE(ModelTexName, 128);
	mpFreeAll(MP_NAME(ModelTexName));
	eaClear(&model_tex_names);
	eaClear(&tex_names);

	if (addwhite)
		texNameAdd("white", NULL); // Default texture value
}

static void getBoneNameInternal(char *pcBoneName, int pcBoneName_size, const char* pcBoneNameOffset, int num_underscores)
{
	char *s;
	int u_count = 0;
	strcpy_s(SAFESTR2(pcBoneName), pcBoneNameOffset);
	s = strchr(pcBoneName, '_');
	while (s && u_count < num_underscores)
	{
		s = strchr(s+1, '_');
		if (s)
			u_count++;
	}

	if (s)
		*s = 0;
}

static const char* findBoneNameForGeometry(const char *pcName, const DynBaseSkeleton *pBaseSkeleton)
{
	char pcBoneName[256];
	const char* pcBoneNameOffset = pcName;
	if ( strnicmp(pcName, "GEO_", 4) == 0 )
		pcBoneNameOffset = pcName + 4;
	else if ( strnicmp(pcName, "MEO_", 4) == 0 ) // yes, there are some misspellings out there...
		pcBoneNameOffset = pcName + 4;

	if (pBaseSkeleton)
	{
		int num_underscores;
		for (num_underscores = 2; num_underscores >= 0; --num_underscores)
		{
			getBoneNameInternal(SAFESTR(pcBoneName), pcBoneNameOffset, num_underscores);
			if (dynBaseSkeletonFindNode(pBaseSkeleton, pcBoneName))
				break;
		}
	}
	else
	{
		getBoneNameInternal(SAFESTR(pcBoneName), pcBoneNameOffset, 0);
	}

	// fixup known broken bones names:
	if (stricmp(pcBoneName, "Hiplayer")==0)
		strcpy(pcBoneName, "Hips");
	else if (stricmp(pcBoneName, "Eyes")==0)
		strcpy(pcBoneName, "Eye");
	else if (stricmp(pcBoneName, "Earrings")==0)
		strcpy(pcBoneName, "Earings");
	else if (stricmp(pcBoneName, "Earring")==0)
		strcpy(pcBoneName, "Earings");
	else if (stricmp(pcBoneName, "Earing")==0)
		strcpy(pcBoneName, "Earings");
	else if (stricmp(pcBoneName, "EarAttach")==0)
		strcpy(pcBoneName, "Earings");
	else if (stricmp(pcBoneName, "EmblemF")==0)
		strcpy(pcBoneName, "Emblem");
	else if (stricmp(pcBoneName, "HelmetAttach")==0)
		strcpy(pcBoneName, "Temple");
	else if (stricmp(pcBoneName, "Cape")==0)
		strcpy(pcBoneName, "Back");
	else if (stricmp(pcBoneName, "CapeMantle")==0)
		strcpy(pcBoneName, "Back");
	else if (stricmp(pcBoneName, "CapeHipsMantle")==0)
		strcpy(pcBoneName, "Hips");
	else if (stricmp(pcBoneName, "CapeHips")==0)
		strcpy(pcBoneName, "Hips");
	else if (stricmp(pcBoneName, "ULegL")==0)
		strcpy(pcBoneName, "Hips");
	else if (stricmp(pcBoneName, "ULegR")==0)
		strcpy(pcBoneName, "Hips");
	else if (stricmp(pcBoneName, "Hip")==0)
		strcpy(pcBoneName, "Hips");
	else if (stricmp(pcBoneName, "CapeLArmL")==0)
		strcpy(pcBoneName, "LArmL");
	else if (stricmp(pcBoneName, "Tail")==0)
		strcpy(pcBoneName, "Tail1");
	else if (stricmp(pcBoneName, "CapeLArmR")==0)
		strcpy(pcBoneName, "LArmR");
	else if (stricmp(pcBoneName, "CapeWritsL")==0)
		strcpy(pcBoneName, "LArmL");
	else if (stricmp(pcBoneName, "CapeWristR")==0)
		strcpy(pcBoneName, "LArmR");
	else if (stricmp(pcBoneName, "ChestAttachL")==0)
		strcpy(pcBoneName, "ChestAttach_L");
	else if (stricmp(pcBoneName, "ChestAttachR")==0)
		strcpy(pcBoneName, "ChestAttach_R");

	return allocAddFilename(pcBoneName);
}

int texNameAdd(const char *pcName, const char *modelUsingIt)
{
	int		i;
	char	nameBuf[1024];
	int return_index=-1;

	modelUsingIt = allocAddCaseSensitiveString(modelUsingIt);

	getFileNameNoExt(nameBuf, pcName);
	if (strStartsWith(nameBuf, "x_")) {
		strcpy(nameBuf, nameBuf+2);
	} else if (strStartsWith(nameBuf, "!")) {
		strcpy(nameBuf, nameBuf+1);
	}

	if (!nameBuf[0])
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "\n   WARNING: model %s references material with no name (\"%s\"), using white instead.\n", modelUsingIt, pcName);
		strcpy(nameBuf, "white");
	}

	pcName = allocAddCaseSensitiveString(nameBuf);
	for(i=0;i<eaSize(&tex_names);i++)
	{
		if (tex_names[i] == pcName) {
			return_index= i;
			break;
		}
	}
	if (return_index == -1)
	{
		return_index = eaPush(&tex_names, pcName);
	}
	if (modelUsingIt)
	{
		bool bFound=false;
		for (i=0; i<eaSize(&model_tex_names); i++)
		{
			if (model_tex_names[i]->texname == pcName)
			{
				if (model_tex_names[i]->modelName == modelUsingIt)
				{
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			ModelTexName *mtn;
			MP_CREATE(ModelTexName, 128);
			mtn = MP_ALLOC(ModelTexName);
			mtn->modelName = modelUsingIt;
			mtn->texname = pcName;
			eaPush(&model_tex_names, mtn);
		}
	}
	return return_index;
}

/////########################## Bone Stuff ######################################

static int isAnimNode(Node *node)
{
	if (node->rotkeys.uiCount || node->poskeys.uiCount)
		return 1;
	if (strnicmp(node->name,"geo_",4)==0)
		return 1;
	return 0;
}

#define GEO_ID -2
#define NOT_ANIM -4

////################# end handle bones ######################################################

///##########################Normal Trick ###################################################
/*This is a hack to handle the fact that 3ds Max doesn't allow you to individually tweak normals
*/
#define SAME 0

static int isNormalObject(Node * node)
{
	int len;

	len = strlen(node->name);

	if( stricmp(node->name+len-2, "$N") == SAME || stricmp(node->name+len-3, "$SN") == SAME )
		return 1;
	
	return 0;
}

/*go through tree and find nodes with the same name as the normals node (minus prefix), 
replace every normal in the matching node with the first normal in the normals node
*/
static void replaceAllNormals(Node ***nodelist, Node * normals)
{
	int len, i, j;

	len = strlen(normals->name);
	len -= strlen("$SN");

	for(j = 0; j < eaSize(nodelist); ++j)
	{	
		Node *node = (*nodelist)[j];
		if( !isNormalObject(node) && _strnicmp(normals->name, node->name, len) == SAME )
		{
			for(i = 0 ; i < node->mesh.vert_count ; i++)
			{
				copyVec3(normals->mesh.normals[0], node->mesh.normals[i] );
			}
			printf("Replaced all %d normals from %s with the first normals from %s\n", 
				node->mesh.vert_count, node->name, normals->name );
		}
	}
}

/*used by replaceCloseNormals
*/
static int doCloseNormalReplacement(GMesh * s, GMesh * n)
{	
	int swapped = 0;
	int i, j, hit;
	for(i = 0 ; i < n->vert_count ; i++)
	{
		hit = 0;
		for(j = 0 ; j < s->vert_count ; j++)
		{
			if( nearSameVec3Tol(n->positions[i], s->positions[j], 0.01 ) )
			{
				hit++;
				copyVec3( n->normals[i], s->normals[j] );
				swapped++;
			}
		}
	}
	return swapped;
}

/*go through tree and find nodes with the same name as the normals node (minus prefix), 
in them, replace normals from the matching node with corresponding normals from the normals node.
(Corresponding means they have the same relative position.) 
*/
static void replaceCloseNormals(Node ***nodelist, Node * normals)
{
	int i, len, swapped_cnt;

	len = strlen(normals->name);
	len -= strlen("$N");

	for(i = 0; i < eaSize(nodelist); ++i)
	{	
		Node *node = (*nodelist)[i];
		if( !isNormalObject(node) && _strnicmp(normals->name, node->name, len) == SAME )
		{
			swapped_cnt = doCloseNormalReplacement(&node->mesh, &normals->mesh);
			printf("Replaced %d of %d normals from %s with normals from %s\n", 
				swapped_cnt, node->mesh.vert_count, node->name, normals->name );
		}
	}
}

/*Fills results with all nodes in the tree with this suffix.
*/
static int getNodesWithThisSuffix( Node ***nodelist, char * suffix, Node * results[] )
{
	int i, count = 0;
	char * c;

	for(i = 0; i < eaSize(nodelist); ++i)
	{
		Node *node = (*nodelist)[i];

		c  = node->name + strlen(node->name) - strlen(suffix);
		if ( c && _stricmp( c, suffix ) == SAME )
			results[count++] = node;
	}
	return count;
}

/*See my thing in n:docs for how this works
*/
static void crazyNormalTrick(Node ***nodelist)
{
	Node * normals[1000];
	int count = 0, i;

	count = getNodesWithThisSuffix(nodelist, "$N", normals );
	assert(count < 1000);

	for(i = 0 ; i < count ; i++)
	{
		replaceCloseNormals(nodelist, normals[i]);
		treeDelete(normals[i], nodelist);
	}

	count = getNodesWithThisSuffix(nodelist, "$SN", normals );
	assert(count < 1000);

	for(i = 0 ; i < count ; i++)
	{
		replaceAllNormals(nodelist, normals[i]);
		treeDelete(normals[i], nodelist);
	}
}
///#######End crazy normals trick ##########################################

static int isMorphTarget(Node * node)
{
	int len;

	len = strlen(node->name);

	if( stricmp(node->name+len-6, "_MORPH") == 0 )
		return 1;

	return 0;
}

static void addSpecificGeomorphTarget(Node ***nodelist, Node * morphtarget)
{
	int i, len;

	len = strlen(morphtarget->name);
	len -= strlen("_MORPH");

	for(i = 0; i < eaSize(nodelist); ++i)
	{	
		Node *node = (*nodelist)[i];
		if( !isMorphTarget(node) && _strnicmp(morphtarget->name, node->name, len) == 0 )
		{
			if (morphtarget->mesh.vert_count != node->mesh.vert_count)
			{
				printf("Found geomorph target for %s but the vertex counts did not match!\n", node->name);
			}
			else
			{
				gmeshAddPositions2(&node->mesh, &morphtarget->mesh);
				gmeshAddNormals2(&node->mesh, &morphtarget->mesh);

				printf("Added geomorph target to %s\n", node->name);
			}
		}
	}
}

static void addGeomorphTargets(Node ***nodelist)
{
	Node * morphs[1000];
	int count = 0, i;

	count = getNodesWithThisSuffix(nodelist, "_MORPH", morphs);
	assert(count < 1000);

	for(i = 0 ; i < count ; i++)
	{
		addSpecificGeomorphTarget(nodelist, morphs[i]);
		treeDelete(morphs[i], nodelist);
	}
}

static int isUV2(Node * node)
{
	int len;

	len = strlen(node->name);

	if( stricmp(node->name+len-4, "_UV2") == 0 )
		return 1;

	return 0;
}

static void addSpecificUV2s(Node ***nodelist, Node *uv2)
{
	int i, len;

	len = strlen(uv2->name);
	len -= strlen("_UV2");

	for(i = 0; i < eaSize(nodelist); ++i)
	{	
		Node *node = (*nodelist)[i];
		if( !isUV2(node) && _strnicmp(uv2->name, node->name, len) == 0 )
		{
			if (uv2->mesh.vert_count != node->mesh.vert_count)
			{
				printf("Found uv2 target for %s but the vertex counts did not match!\n", node->name);
			}
			else
			{
				gmeshAddTex2s(&node->mesh, &uv2->mesh);

				printf("Added uv2 target to %s\n", node->name);
			}
		}
	}
}

static void addTexCoord2s(Node ***nodelist)
{
	Node *uv2s[1000];
	int count = 0, i;

	count = getNodesWithThisSuffix(nodelist, "_UV2", uv2s);
	assert(count < 1000);

	for(i = 0 ; i < count ; i++)
	{
		addSpecificUV2s(nodelist, uv2s[i]);
		treeDelete(uv2s[i], nodelist);
	}
}

//##########Crazy Alternate Pivots thingy ################################

static void assignAltPivot(Node * node, U32 uiNumPrefixChars)
{
	if(node->parent)
	{
		AltPivot* ap = malloc(sizeof(*ap));
		char cFixedName[128];
		char* pc;
		eaPush(&node->parent->api.altpivot, ap);
		copyMat4(node->mat, ap->mat);
		extractScale(node->mat, ap->scale);
		strcpy(cFixedName, node->name);
		assert(strlen(cFixedName) > uiNumPrefixChars);
		pc = &cFixedName[uiNumPrefixChars];
		pc = strchr(pc, '_');
		if (pc)
			*pc = 0;
		ap->name = strdup(cFixedName);
	}
}

static void getAltPivots(Node * root)
{
	
	Node * node;
	
	for( node = root ; node ; node = node->next)
	{	
		if(!_strnicmp("AltPiv_", node->name, 7))
			assignAltPivot(node, 7);	
		else if(!_strnicmp("MountPoint_", node->name, 11))
			assignAltPivot(node, 11);	
		else if(!_strnicmp("FxPoint_", node->name, 8))
			assignAltPivot(node, 8);	
		getAltPivots(node->child);
	}
}

extern ParseTable parse_AltPivotInfo[];
#define TYPE_parse_AltPivotInfo AltPivotInfo

static Node *cleanAltPivots(Node *root, AltPivotInfo*** apis)
{
	Node * node;
	Node * next;

	for( node = root ; node ; )
	{
		next = node->next;
		if (eaSize(&node->api.altpivot)>0)
		{
			AltPivotInfo* api = StructAlloc(parse_AltPivotInfo);
			node->api.modelname = node->name;
			StructCopyAll(parse_AltPivotInfo, &node->api, api);
			eaPush(apis, api);
		}
		if(!_strnicmp("AltPiv_", node->name, 7) || !_strnicmp("MountPoint_", node->name, 11) || !_strnicmp("FxPoint_", node->name, 8) )
		{
			if (node == root)
				root = next;
			treeDelete(node, NULL);
		}
		else
		{
			node->child = cleanAltPivots(node->child, apis);
		}
		node = next;
	}
	return root;
}

static Node *doAltPivots(Node *root, AltPivotInfo*** apis)
{
	getAltPivots(root);
	return cleanAltPivots(root, apis);
}
//#######End crazy Alternate Pivot Thingy ##################################


typedef struct _TriSet
{	
	int *bones;
	int *tri_idx;
} TriSet;

static TriSet ** trisets;
static int num_trisets=0;

static void optimizeBoneReset(void)
{
	num_trisets=0;
}

static TriSet *optimizeBoneNewTriSet(void)
{
	TriSet *ret;
	int r2;
	if (num_trisets < eaSize(&trisets)) {
		assert(trisets);
		ret = trisets[num_trisets++];
	} else {
		num_trisets++;
		ret = calloc(sizeof(TriSet),1);
		r2 = eaPush(&trisets, ret);
		assert(num_trisets - 1 == r2);
	}
	eaiSetSize(&ret->bones, 0);
	eaiSetSize(&ret->tri_idx, 0);
	return ret;
}

static void destroyTriSet(TriSet *triset)
{
	eaiDestroy(&triset->bones);
	eaiDestroy(&triset->tri_idx);
}

static void optimizeBoneCleanup(void)
{
	eaClearEx(&trisets, destroyTriSet);
	eaDestroy(&trisets);
}

typedef struct BoneSection
{
	int bones[MAX_OBJBONES];
	int num_bones;
	int start_idx;
	int run_length;
} BoneSection;

typedef struct BoneSectionTriSet
{
	int *triset;
} BoneSectionTriSet;

static void freeTriset(BoneSectionTriSet *triset)
{
	eaiDestroy(&triset->triset);
	free(triset);
}

static void optimizeBone(Node *node, int tri_index_start, int tri_index_stop)
{
	GMesh *mesh = &node->mesh;
	BoneData *bones = &node->bones;
	int i, j, k ,l;
	int bones_in_tri[MAX_OBJBONES];
	int num_bones_in_tri;
	int match;
	int vert;
	int bone;
	int done;
	int a, b, temp;
	int tri_offset;
	GTriIdx *temp_tris;

	BoneSectionTriSet **bonesection_trisets = NULL; // bookkeeping, don't x-fer  Array of EArrayInts
	int *final_tri_list = NULL;
	BoneSection	**bonesections = NULL;

	if (!eaSize(&bones->boneNames))
		return;

	optimizeBoneReset();

	//1. *****Build a Catalog (trisets) of all combinations of bones tris represent.

	//for each tri
	for (i = tri_index_start; i < tri_index_stop; i++)
	{
		//A. Get a list of all the bones in that tri
		num_bones_in_tri = 0;
		for (j = 0; j < 3; j++)
		{
			vert = mesh->tris[i].idx[j];
			//get a list of each bone that has any weight in that tri. 
			for (k = 0; k < 4; k++)
			{
				bone = mesh->bonemats[vert][k] / 3;
				if (mesh->boneweights[vert][k] != 0.0)
				{
					for (l = 0; l < num_bones_in_tri; l++)
					{
						if (bone == bones_in_tri[l])
							break;
					}
					if (l == num_bones_in_tri)
					{
						bones_in_tri[num_bones_in_tri] = bone;
						num_bones_in_tri++;
						assert(num_bones_in_tri < MAX_OBJBONES);
					}
				}
			}
		}
	
		//sort the bones_in_tri for ease
		for (;;)
		{
			done = 1;
			for (a = 0, b = 1; b < num_bones_in_tri; a++, b++)
			{
				if (bones_in_tri[a] > bones_in_tri[b])
				{
					temp = bones_in_tri[b];
					bones_in_tri[b] = bones_in_tri[a];
					bones_in_tri[a] = temp;
					done = 0;
				}
			}
			if (done == 1)
				break;
		}
			
		//tri_idx (i) bones_in_tri[] and num_bones in tri 
		//B. Find the TriSet that matches this triangle, or add a new triset.
		match = -1;
		for (j = 0; j < num_trisets && match == -1; j++)
		{
			//match = checkForMatch(trisets[j], bones_in_tri, num_bones)
			if (num_bones_in_tri == eaiSize(&trisets[j]->bones))
			{
				match = j;
				for (k = 0; k < num_bones_in_tri; k++)
				{
					if (bones_in_tri[k] != trisets[j]->bones[k])
						match = -1;
				}
			}
		}
		if (match == -1)
		{
			TriSet *triset = optimizeBoneNewTriSet();
			for (j = 0; j < num_bones_in_tri; j++)
			{
				eaiPush(&triset->bones, bones_in_tri[j]);
			}
			assert(eaiSize(&triset->tri_idx)==0);
			match = num_trisets-1;
		}
	
		//C. Add this tri to it's new triset
		eaiPush(&trisets[match]->tri_idx, i);
	}


	//sort the tri_sets for ease
	for (;;)
	{
		done = 1;
		for (a = 0, b = 1; b < num_trisets; a++, b++)
		{
			if (eaiSize(&trisets[a]->bones) > eaiSize(&trisets[b]->bones))
			{
				TriSet *temp_triset = trisets[b];
				trisets[b] = trisets[a];
				trisets[a] = temp_triset;
				done = 0;
			}
		}
		if (done == 1)
			break;
	}

	//*********************************************************
	//2. Now that we have the whole object cataloged 
	//Divide it up in to two bone groups and a final, all the rest bone group.
	
	//A. Init stuff
	tri_offset = tri_index_start;

	//B. For each triset, identify which BoneSection it should be a part of
	//   and add it to the BoneSection bookkeeping list:
	//			int *bonesection_trisets[MAX_BONE_SECTIONS];
	//   This is where to later change the way the list built.
	//   For the future:  Combine orphaned one boners?  Add a minimum number of tris?

	for (i = num_trisets - 1; i >= 0; i--)
	{
		//If 3 or more add to the first, software set
		if (eaiSize(&trisets[i]->bones) >= 3)
		{
			if (!eaSize(&bonesection_trisets))
			{
				BoneSectionTriSet *set = calloc(1, sizeof(BoneSectionTriSet));
				eaPush(&bonesection_trisets, set);
			}
			eaiPush(&bonesection_trisets[0]->triset, i);
		}
		//If two, add to it's own hardware set
		else if (eaiSize(&trisets[i]->bones) == 2)
		{	
			BoneSectionTriSet *set = calloc(1, sizeof(BoneSectionTriSet));
			eaPush(&bonesection_trisets, set);
			eaiPush(&set->triset, i);
		}

		//If one, Find a twoer already in use, and add it to that: if that fails, make it it's own
		
		else if (eaiSize(&trisets[i]->bones) == 1)
		{
			match = -1;
			for (j = 0; j < eaSize(&bonesection_trisets) ; j++)
			{
				for (l = 0; l < eaiSize(&bonesection_trisets[j]->triset); l++)
				{
					if (eaiSize(&trisets[bonesection_trisets[j]->triset[l]]->bones) == 2)
					{
						for (k = 0; k < 2; k++)
						{
							if (trisets[bonesection_trisets[j]->triset[l]]->bones[k] == trisets[i]->bones[0])
							{
								match = j;
								break;
							}
						}
					}
					if (match != -1)
						break;
				}
				if (match != -1)
					break;
			}
			if (match == -1)
			{
				BoneSectionTriSet *set = calloc(1, sizeof(BoneSectionTriSet));
				match = eaPush(&bonesection_trisets, set);
			}

			eaiPush(&bonesection_trisets[match]->triset, i);
		}
		else if (eaiSize(&trisets[i]->bones) == 0)
		{
			BoneSectionTriSet *set = calloc(1, sizeof(BoneSectionTriSet));
			eaPush(&bonesection_trisets, set);
			eaiPush(&set->triset, i);
		}
		else 
		{
			assert(0);
		}
	}

						
	//C.  Take the Resulting list of
	//		int bonesection_trisets[MAX_BONE_SECTIONS][20]
	//		int num_bonesection_trisets; 
	//	  And use it to write to the shape's bonesection array and to the 
	//	  Array of tri indexes.

	for (i = 0; i < eaSize(&bonesection_trisets); i++)
	{
		BoneSection *bs = calloc(1, sizeof(BoneSection));
		eaPush(&bonesections, bs);
		bs->start_idx  = tri_offset;
	
		assert(eaiSize(&bonesection_trisets[i]->triset));
		for (l = 0; l < eaiSize(&bonesection_trisets[i]->triset); l++)
		{
			TriSet *triset = trisets[bonesection_trisets[i]->triset[l]];
			//For each bone in the triset
			//Compare it to each bone in the bonesection
			//If the bone isn't in the bonesection yet, add it
			//assert(triset->num_bones);
			for (j = 0; j < eaiSize(&triset->bones); j++)
			{	
				match = 0;
				for (k = 0; k < bs->num_bones; k++)
				{
					if (bs->bones[k] == triset->bones[j])
					{
						match = 1;
					}
				}
				if (!match)
				{
					bs->bones[bs->num_bones] = triset->bones[j];
					bs->num_bones++;
				}	
			}
			
			//Then update the tri_set and write this triset's tris to the new tri_idx	
			bs->run_length += eaiSize(&triset->tri_idx);
			
			for (j = 0; j < eaiSize(&triset->tri_idx); j++)
			{
				if (eaiSize(&final_tri_list) <= tri_offset)
					eaiSetSize(&final_tri_list, (tri_offset<=0)?2:tri_offset*2);
				eaiSet(&final_tri_list, triset->tri_idx[j], tri_offset);
				tri_offset++;
			}
		}	
	}
	assert(tri_index_stop == tri_offset);

	//D. Reorder the tris in the order final_tri_list specifies
	temp_tris = calloc(sizeof(GTriIdx),mesh->tri_count);
	for (i = tri_index_start; i < tri_index_stop; i++)
		temp_tris[i] = mesh->tris[i];
	for (i = tri_index_start; i < tri_index_stop; i++)
		mesh->tris[i] = temp_tris[final_tri_list[i]];
	free(temp_tris);

	eaiDestroy(&final_tri_list);
	eaDestroyEx(&bonesections, NULL);
	eaDestroyEx(&bonesection_trisets, freeTriset);
}

static void optimizeBones(Node **nodelist)
{
	int i;

	for(i = 0; i < eaSize(&nodelist); ++i)
	{
		Node *node = nodelist[i];
		int tri_start_index=0;
		while (tri_start_index != node->mesh.tri_count)
		{
			int texid = node->mesh.tris[tri_start_index].tex_id;
			int tri_stop_index=0;
			for (tri_stop_index = tri_start_index; tri_stop_index < node->mesh.tri_count && node->mesh.tris[tri_stop_index].tex_id == texid; tri_stop_index++);
			//if(isAnimNode(node))
			optimizeBone(node, tri_start_index, tri_stop_index);
			tri_start_index = tri_stop_index;
		} 
	}

	optimizeBoneCleanup();
}

static void reverseCharacter(Vec4 v) //mm
{
	Quat	quat;
	Mat3	rot_m;
	Mat3    y180;
	Mat3    result;

	copyMat3(unitmat, y180);
	yawMat3(RAD(180), y180);

	axisAngleToQuat(&v[0],v[3],quat);
	quatToMat(quat,rot_m);
	mulMat4(y180, rot_m, result);

	//now extract axis-angle rotation from result and set v[0-4] to that	
	return;
}

static void applyMatrix(Node *node, Mat4 parent_mat)
{
	Node	*next;
	Mat4	temp_mat;

	for(;node;node = next)
	{
		next = node->next;
		mulMat4(parent_mat, node->mat, temp_mat);
		copyMat4(temp_mat, node->mat);
	}
}

static Node *applyTransforms(Node *node, const DynBaseSkeleton *base_skeleton, bool is_root)
{
	Node	*next;

	for(;node;node = next)
	{
		bool deleted = false;
		bool is_anim_node = isAnimNode(node);

		next = node->next;

		if (is_anim_node)
			node->attachment_bone = findBoneNameForGeometry(node->name, base_skeleton);

		// recurse
		if (node->child)
			applyTransforms(node->child, base_skeleton, false);

		// apply scale and child_translate to leaves
		if (!is_anim_node && !node->child)
		{
			if (node->mesh.vert_count)
			{
				Mat4 child_mat;
				if (is_root)
				{
					copyMat3(node->mat, child_mat);
					copyMat4(unitmat, node->mat);
					copyVec3(node->child_translate, child_mat[3]);
				}
				else
				{
					Mat4 temp_mat, scale_mat;
					Vec3 scale;

					copyMat3(unitmat, temp_mat);
					copyVec3(node->child_translate, temp_mat[3]);

					extractScale(node->mat, scale); // normalizes the matrix
					createScaleMat(scale_mat, scale);
					copyVec3(zerovec3, scale_mat[3]);
					mulMat4(scale_mat, temp_mat, child_mat);
				}

				gmeshTransform(&node->mesh, child_mat);
				copyVec3(node->child_translate, node->applied_transform);
				setVec3same(node->child_translate, 0);
			}
			else
			{
				treeDelete(node, NULL);
				deleted = true;
			}
		}
		else if (!node->child && node->mesh.vert_count && base_skeleton && node->attachment_bone)
		{
			const DynNode *bone = dynBaseSkeletonFindNode(base_skeleton, node->attachment_bone);
			if (bone)
			{
				Mat4 child_mat;
				dynNodeGetWorldSpaceMat(bone, child_mat, true);
				gmeshTransform(&node->mesh, child_mat);
			}
			else
			{
				printf("Unable to find bone %s for node %s in skeleton %s!\n", node->attachment_bone, node->name, base_skeleton->pcName);
			}
		}

		if (!deleted && is_root)
		{
			// discard root level translation
			setVec3same(node->mat[3], 0);

			// apply root level matrix down one level
			if (node->child)
			{
				Mat4 temp_mat;
				copyMat3(node->mat, temp_mat);
				copyVec3(node->child_translate, temp_mat[3]);
				applyMatrix(node->child,temp_mat);
				setVec3same(node->child_translate, 0);
				copyMat4(unitmat, node->mat);
			}
		}
	}

	return getTreeRoot(); //(kindof a cheat)
}

//////////////////////////////////////////////////////////////////////////

static SimpleGroupDef **all_group_nodes;

MP_DEFINE(SimpleGroupChild);
MP_DEFINE(SimpleGroupDef);

__forceinline bool nodeNameOk(const char *name)
{
	if (strEndsWith(name, "_MORPH"))
		return false;
	if (strEndsWith(name, "_UV2"))
		return false;
	if (strEndsWith(name, "$N"))
		return false;
	if (strEndsWith(name, "$SN"))
		return false;
	return true;
}

static int cmpSimpleGroupEnt(const SimpleGroupChild **entp1, const SimpleGroupChild **entp2)
{
	const SimpleGroupChild *ent1 = *entp1;
	const SimpleGroupChild *ent2 = *entp2;
	int hash1, hash2;

	if (ent1->def && ent2->def)
	{
		int ret = stricmp(ent1->def->name_str, ent2->def->name_str);
		if (ret != 0)
			return ret;
	}

	hash1 = (int)cryptAdler32((U8 *)ent1->mat, sizeof(Mat4));
	hash2 = (int)cryptAdler32((U8 *)ent2->mat, sizeof(Mat4));

	return hash1 - hash2;
}

static SimpleGroupDef *createSimpleGroupDef(Node *orig_node);

static SimpleGroupChild *createSimpleGroupEnt(Node *orig_node, Node *parent_node)
{
	SimpleGroupChild *gchild;
	Mat4 transmat;
	MP_CREATE(SimpleGroupChild, 512);
	gchild = MP_ALLOC(SimpleGroupChild);

	copyMat3(unitmat, transmat);
	copyVec3(parent_node->child_translate, transmat[3]);
	mulMat4(transmat, orig_node->mat, gchild->mat);
	gchild->seed = orig_node->seed;
	gchild->def = createSimpleGroupDef(orig_node);

	return gchild;
}

static SimpleGroupDef *createSimpleGroupDef(Node *orig_node)
{
	SimpleGroupDef *gnode;
	Node *node;

	MP_CREATE(SimpleGroupDef, 256);
	gnode = MP_ALLOC(SimpleGroupDef);
	gnode->is_root = !orig_node->parent;
	gnode->name_uid = orig_node->id_override;

	strcpy(gnode->name_str, orig_node->name);

	if (orig_node->mesh.tri_count)
		strcpy(gnode->modelname, orig_node->name);

	gnode->user_data = orig_node;

	for (node = orig_node->child; node; node = node->next)
	{
		if (nodeNameOk(node->name))
			eaPush(&gnode->children, createSimpleGroupEnt(node, orig_node));
	}

	eaQSortG(gnode->children, cmpSimpleGroupEnt);

	eaPush(&all_group_nodes, gnode);
	return gnode;
}

// note: not recursive
static void freeSimpleGroupEnt(SimpleGroupChild *gchild)
{
	if (!gchild)
		return;
	MP_FREE(SimpleGroupChild, gchild);
}

// note: not recursive
// note: does not remove from all list
static void freeSimpleGroupDef(SimpleGroupDef *gnode)
{
	if (!gnode)
		return;
	eaDestroyEx(&gnode->children, freeSimpleGroupEnt);

	MP_FREE(SimpleGroupDef, gnode);
}

static bool cmpNodeMeshes(Node *node1, Node *node2)
{
	int i;
	bool match = true;

	if (!nearSameVec3(node1->applied_transform, node2->applied_transform))
		return false;

	if (eaSize(&node1->mesh_names) != eaSize(&node2->mesh_names))
		match = false;

	for (i = 0; i < eaSize(&node1->mesh_names); ++i)
	{
		if (i >= eaSize(&node2->mesh_names))
		{
			match = false;
			break;
		}
		if (stricmp(node1->mesh_names[i], node2->mesh_names[i])!=0)
			match = false;
	}

	if (!match && !gmeshCompare(&node1->mesh, &node2->mesh))
		return false;

	return true;
}

static bool cmpSimpleGroupDefs(SimpleGroupDef *gnode1, SimpleGroupDef *gnode2)
{
	int i;

	if (eaSize(&gnode1->children) != eaSize(&gnode2->children))
		return false;

	for (i = 0; i < eaSize(&gnode1->children); ++i)
	{
		SimpleGroupChild *child1 = gnode1->children[i];
		SimpleGroupChild *child2 = gnode2->children[i];

		if (child1->def != child2->def)
			return false;
		if (!nearSameVec3(child1->mat[0], child2->mat[0]))
			return false;
		if (!nearSameVec3(child1->mat[1], child2->mat[1]))
			return false;
		if (!nearSameVec3(child1->mat[2], child2->mat[2]))
			return false;
		if (!nearSameVec3(child1->mat[3], child2->mat[3]))
			return false;
	}

	if (!cmpNodeMeshes(gnode1->user_data, gnode2->user_data))
		return false;

	return true;
}

static int cmpSimpleGroupDefNames(const SimpleGroupDef **defp1, const SimpleGroupDef **defp2)
{
	const SimpleGroupDef *def1 = *defp1;
	const SimpleGroupDef *def2 = *defp2;

	if (!def1 || !def2)
		return 0;

	return stricmp(def1->name_str, def2->name_str);
}

static void replaceSimpleGroupDef(SimpleGroupDef *src, SimpleGroupDef *dst)
{
	int i, j;
	for (i = 0; i < eaSize(&all_group_nodes); ++i)
	{
		SimpleGroupDef *gnode = all_group_nodes[i];
		bool changed = false;

		for (j = 0; j < eaSize(&gnode->children); ++j)
		{
			if (gnode->children[j]->def == src)
			{
				gnode->children[j]->def = dst;
				changed = true;
			}
		}

		if (changed)
			eaQSortG(gnode->children, cmpSimpleGroupEnt);
	}
}

static void createGroupNodes(Node *node)
{
	bool founddup = true;

	for (; node; node = node->next)
	{
		if (nodeNameOk(node->name))
			createSimpleGroupDef(node);
	}
	eaQSortG(all_group_nodes, cmpSimpleGroupDefNames);

	// find duplicate groups
	// (there is probably a clever way of doing this, but time is not critical here)
	while (founddup)
	{
		int i, j;
		founddup = false;
		for (i = 0; i < eaSize(&all_group_nodes); ++i)
		{
			SimpleGroupDef *gnode1 = all_group_nodes[i];
			for (j = i + 1; j < eaSize(&all_group_nodes); ++j)
			{
				SimpleGroupDef *gnode2 = all_group_nodes[j];
				assert(gnode1 != gnode2);
				if (cmpSimpleGroupDefs(gnode1, gnode2))
				{
					SimpleGroupDef *gnode_delete = gnode2, *gnode_keep = gnode1;
					int delete_index = j;

					if (gnode_delete->is_root)
					{
						gnode_delete = gnode1;
						gnode_keep = gnode2;
						delete_index = i;
					}

					if (!gnode_delete->is_root)
					{
						treeMoveChildren(gnode_delete->user_data,gnode_keep->user_data);
						treeDelete(gnode_delete->user_data, NULL);
						eaRemove(&all_group_nodes, delete_index);
						freeSimpleGroupDef(gnode_delete);
						replaceSimpleGroupDef(gnode_delete, gnode_keep); // doesn't actually dereference gnode2, so it can be after the free
						founddup = true;
						break;
					}
				}
			}
		}
	}
}

static void freeAllGroupNodes(void)
{
	eaDestroyEx(&all_group_nodes, freeSimpleGroupDef);
}

//////////////////////////////////////////////////////////////////////////

static Node * mergeNodes(Node *node,GMesh *parent_mesh)
{
	Node	*next;

	for(;node;node = next)
	{
		next = node->next;

		if (node->child)
			mergeNodes(node->child, &node->mesh);

		if (!isAnimNode(node) && !node->child)
		{
			Mat4 transmat, temp_mat;
			copyMat3(unitmat, transmat);
			copyVec3(node->child_translate, transmat[3]);
			mulMat4(node->mat, transmat, temp_mat);
			gmeshTransform(&node->mesh, temp_mat);
			setVec3same(node->child_translate, 0);

			if (!node->mesh.vert_count)
			{
				treeDelete(node, NULL);
			}
			else if (parent_mesh)
			{
				gmeshMerge(parent_mesh, &node->mesh, 0, 0, node->wind, true);
				treeDelete(node, NULL);
			}
		}
	}
	return getTreeRoot(); //(kindof a cheat)
}

static F32 checkrad(Vec3 *verts,int count,Vec3 mid)
{
	int		i;
	F32		rad = 0;
	Vec3	dv;

	for(i=0;i<count;i++)
	{
		subVec3(verts[i],mid,dv);
		if (lengthVec3(dv) > rad)
			rad = lengthVec3(dv);
	}
	return rad;
}

static void findBounds(Node **nodelist)
{
	int		i, j;

	for (j = 0; j < eaSize(&nodelist); ++j)
	{
		Node *node = nodelist[j];

		node->radius = 0;

		if (node->mesh.vert_count)
		{
			Vec3 mid;

			// find bounding box
			copyVec3(node->mesh.positions[0], node->min);
			copyVec3(node->mesh.positions[0], node->max);
			for (i=1;i<node->mesh.vert_count;i++)
				vec3RunningMinMax(node->mesh.positions[i], node->min, node->max);

			// find bounding sphere
			centerVec3(node->min, node->max, mid);
			for (i=0;i<node->mesh.vert_count;i++)
			{
				F32 dv = distance3(node->mesh.positions[i], mid);
				MAX1(node->radius, dv);
			}
		}
		else
		{
			zeroVec3(node->min);
			zeroVec3(node->max);
		}
	}
}

static void poolVerts(Node **nodelist)
{
	int i;
	for(i = 0; i < eaSize(&nodelist); ++i)
	{
		Node *node = nodelist[i];
		gmeshSetUsageBits(&node->mesh, node->mesh.usagebits | USE_NORMALS);
		gmeshPool(&node->mesh, true, node->wind, true);
	}
}

static void ditchUnneededStuff(Node *root, Node ***nodelist, GetVrmlLibraryType targetlibrary )
{
	int i;

	for(i = 0; i < eaSize(nodelist); ++i)
	{
		Node *node = (*nodelist)[i];

		if (targetlibrary == LIB_CHAR)
		{
			if(strnicmp(node->name, "GEO_", 4))
			{
				treeDelete(node, nodelist);
				--i;
			}
			else
			{
				if(node->rotkeys.uiCount || node->poskeys.uiCount)
					printf("");
				node->rotkeys.uiCount = 0;
				node->poskeys.uiCount = 0;
				node->mesh.grid.cell = 0;
			}
		}
	}
}

static void reorderTrisByTex(Node **nodelist)
{
	int i;
	for(i = 0; i < eaSize(&nodelist); ++i)
	{	
		Node *node = nodelist[i];
		gmeshSortTrisByTexID(&node->mesh, NULL);
		if (node->vert_color_sort)
			gmeshSortTrisByVertColor(&node->mesh);
	}
}

void tootleOptimize(U32 *indices, int tri_count, F32 *vertices, U32 vert_count);

static void optimizeMesh(Node **nodelist, bool invertOrderOfAlphaTriSort)
{
	int i, j;

	for (i = 0; i < eaSize(&nodelist); ++i)
	{
		Node *node = nodelist[i];
		GMesh *mesh = &node->mesh;
		U32 tri_count;
		int next_tex_id, tex_id, index_start, next_index_start;
		U32 *indices;

		if (node->vert_color_sort)
			continue;

		if (!mesh->tri_count || node->no_tri_optimization)
			continue;

		if (invertOrderOfAlphaTriSort && !node->alpha_tri_sort)
			continue; // Only care about the ones that need to be sorted

		loadstart_printf("optimizing mesh overdraw.. ");

		indices = calloc(mesh->tri_count * 3, sizeof(U32));

		next_tex_id = mesh->tris[0].tex_id;
		next_index_start = 0;

		while (next_tex_id >= 0) 
		{
			tex_id = next_tex_id;
			next_tex_id = -1;
			index_start = next_index_start;
			next_index_start = mesh->tri_count;
			tri_count = 0;

			// fill per-material index buffer
			for (j = index_start; j < next_index_start; ++j)
			{
				if (mesh->tris[j].tex_id != tex_id)
				{
					next_tex_id = mesh->tris[j].tex_id;
					next_index_start = j;
					break;
				}

				assert(mesh->tris[j].idx[0] >= 0 && mesh->tris[j].idx[0] < mesh->vert_count);
				assert(mesh->tris[j].idx[1] >= 0 && mesh->tris[j].idx[1] < mesh->vert_count);
				assert(mesh->tris[j].idx[2] >= 0 && mesh->tris[j].idx[2] < mesh->vert_count);

				indices[tri_count*3+0] = mesh->tris[j].idx[0];
				indices[tri_count*3+1] = mesh->tris[j].idx[1];
				indices[tri_count*3+2] = mesh->tris[j].idx[2];
				++tri_count;
			}

			// optimize
			waitForGetVrmlRenderLock(true);
			tootleOptimize(indices, tri_count, &mesh->positions[0][0], mesh->vert_count);
			releaseGetVrmlRenderLock();

			// back fill per-material index buffer
			tri_count = 0;
			for (j = index_start; j < next_index_start; ++j)
			{
				int outindex = invertOrderOfAlphaTriSort?(next_index_start - 1 - tri_count):(index_start + tri_count);
				mesh->tris[outindex].idx[0] = indices[tri_count*3+0];
				mesh->tris[outindex].idx[1] = indices[tri_count*3+1];
				mesh->tris[outindex].idx[2] = indices[tri_count*3+2];
				++tri_count;
			}

		}

		free(indices);

		loadend_printf("done");
	}
}

static void ditchGrid(Node * node)
{
	polyGridFree(&node->mesh.grid);
}

static void meshMender(Node *node, char *name)
{
	MeshMenderMendGMesh(&node->mesh, name);
}

static void meshMenderRecurse(Node * node)
{
	// We are not using MeshMender because we determined it still creates
	// seems, and per-pixel calculations create much better results, and
	// our old, hacky method produces possibly better results than MeshMender
	// (assuming we use the hacky method of normalizing), *and* nvdxt produces
	// not quite right normals, so that on a texture mirror it would create
	// a seam *even if* our tangent/binormal generation was perfect.
	for(; node; node = node->next)
	{
		//if(isAnimNode(node))
		meshMender(node, node->name);
		if(node->child)
			meshMenderRecurse(node->child);
	}
}



/*cut out path and .wrl to get name of this anim
//cut header name down to just the name without folder path or '.wrl'
should really be in some util-type file
*/
void parseAnimNameFromFileName(char * fname, char * animname, size_t animname_size)
{
	char basename[MAX_PATH];
	char * bs, * fs, * s;

	fs = strrchr(fname, '/'); 
	bs = strrchr(fname, '\\');
	s = fs > bs ? fs : bs;
	if(s)
		strcpy(basename, s + 1);
	else
		strcpy(basename, fname);
	s = strrchr(basename,'.'); 
	if (s)
		*s = 0;
	strcpy_s(animname, animname_size, basename);
}

void nodeUvunwrap(Node *node)
{
	int i;
	Prim **primitives = NULL;
	GMesh temp_mesh={0};
	float size;
	Vec2 zerovec2={0};

	if (!node->mesh.tri_count)
		return;

	for (i = 0; i < node->mesh.tri_count; i++)
	{
		Prim *prim;
		int i0, i1, i2;

		i0 = node->mesh.tris[i].idx[0];
		i1 = node->mesh.tris[i].idx[1];
		i2 = node->mesh.tris[i].idx[2];

		if (node->mesh.tex1s)
			prim = primCreate(node->mesh.positions[i0], node->mesh.positions[i1], node->mesh.positions[i2], node->mesh.tex1s[i0], node->mesh.tex1s[i1], node->mesh.tex1s[i2]);
		else
			prim = primCreate(node->mesh.positions[i0], node->mesh.positions[i1], node->mesh.positions[i2], zerovec2, zerovec2, zerovec2);

		eaPush(&primitives, prim);
	}

	size = uvunwrap(&primitives);

	gmeshCopy(&temp_mesh, &node->mesh, 0);
	gmeshFreeData(&node->mesh);

	gmeshSetUsageBits(&node->mesh, temp_mesh.usagebits | USE_TEX2S);
	for (i = 0; i < temp_mesh.tri_count; i++)
	{
		int idx0, idx1, idx2;
		Vec2 st3s[3];
		primGetTexCoords(primitives[i], st3s[0], st3s[1], st3s[2]);

		idx0 = temp_mesh.tris[i].idx[0];
		idx1 = temp_mesh.tris[i].idx[1];
		idx2 = temp_mesh.tris[i].idx[2];

		idx0 = gmeshAddVert(&node->mesh,
			temp_mesh.positions?temp_mesh.positions[idx0]:0,
			0,
			temp_mesh.normals?temp_mesh.normals[idx0]:0,
			0,
			0,
			0,
			temp_mesh.tex1s?temp_mesh.tex1s[idx0]:0,
			st3s[0],
			0,
			0,
			temp_mesh.boneweights?temp_mesh.boneweights[idx0]:0,
			temp_mesh.bonemats?temp_mesh.bonemats[idx0]:0,
			0, false, false);
		idx1 = gmeshAddVert(&node->mesh,
			temp_mesh.positions?temp_mesh.positions[idx1]:0,
			0,
			temp_mesh.normals?temp_mesh.normals[idx1]:0,
			0,
			0,
			0,
			temp_mesh.tex1s?temp_mesh.tex1s[idx1]:0,
			st3s[1],
			0,
			0,
			temp_mesh.boneweights?temp_mesh.boneweights[idx1]:0,
			temp_mesh.bonemats?temp_mesh.bonemats[idx1]:0,
			0, false, false);
		idx2 = gmeshAddVert(&node->mesh,
			temp_mesh.positions?temp_mesh.positions[idx2]:0,
			0,
			temp_mesh.normals?temp_mesh.normals[idx2]:0,
			0,
			0,
			0,
			temp_mesh.tex1s?temp_mesh.tex1s[idx2]:0,
			st3s[2],
			0,
			0,
			temp_mesh.boneweights?temp_mesh.boneweights[idx2]:0,
			temp_mesh.bonemats?temp_mesh.bonemats[idx2]:0,
			0, false, false);
		gmeshAddTri(&node->mesh, idx0, idx1, idx2, temp_mesh.tris[i].tex_id, 0);
	}

	gmeshPool(&node->mesh, false, false, false);

	eaDestroyEx(&primitives, primDestroy);

	assertmsg(!node->reductions, "uvunwrapping of meshes with LOD instructions not yet implemented!");

	gmeshFreeData(&temp_mesh);

	node->lightmap_size = size;
}

static void makePrivateGroup(const char *base_def_name, int *counter, SimpleGroupDef *def)
{
	int i;
	if (!def || def->modelname[0] || strEndsWith(def->name_str, "&&") || def->fixedup)
		return;
	++(*counter);
	sprintf(def->name_str, "%s_%d&", base_def_name, *counter);
	def->fixedup = true;
	for (i = 0; i < eaSize(&def->children); ++i)
		makePrivateGroup(base_def_name, counter, def->children[i]->def);
}

static void removeEndingAmpersands(char *str)
{
	int l = strlen(str) - 1;
	while (l >= 0 && str[l] == '&')
	{
		str[l] = 0;
		--l;
	}
}

char** enumObjLibFiles( const char* name, const char* src_fname, const char *dst_fname, const char* root_path )
{
	char buffer[MAX_PATH];
	char **filelist = NULL;

	changeFileExt(dst_fname, ".MaterialDeps", buffer);
	eaPush(&filelist, strdup(buffer));

	changeFileExt(dst_fname, ".MaterialDeps2", buffer);
	eaPush(&filelist, strdup(buffer));

	changeFileExt(dst_fname, MODELHEADER_EXTENSION, buffer);
	eaPush(&filelist, strdup(buffer));

	changeFileExt(dst_fname, ".modelnames", buffer);
	eaPush(&filelist, strdup(buffer));

	return filelist;
}

char** enumCharLibFiles( const char* name, const char* src_fname, const char *dst_fname, const char* root_path )
{
	char buffer[MAX_PATH];
	char **filelist = NULL;

	changeFileExt(dst_fname, ".MaterialDeps", buffer);
	eaPush(&filelist, strdup(buffer));

	changeFileExt(dst_fname, ".MaterialDeps2", buffer);
	eaPush(&filelist, strdup(buffer));

	changeFileExt(dst_fname, MODELHEADER_EXTENSION, buffer);
	eaPush(&filelist, strdup(buffer));

	return filelist;
}


/*read one VRML file into a node tree and massage it into shape for the game.
addFile is the only entry point into geo.c (except a few processing functions used by vrml.c 
(which is in turn only called by this function and parseAnimName.. which really should be in 
some util type file) because it's a bit easier to do those bits of node 
processing as nodes are read in.) (Note that some processing of bone info is being done in 
packSkin in output.c which should really be done here.)

Returns a node list of LODs, if generated.
*/
void geoAddFile(const char *name, const char **vrml_fnames, const char *geo_fname, const char *group_fname, 
				GetVrmlLibraryType targetlibrary, bool do_unwrapping, bool is_core, 
				/*const char *big_fname, */const char *deps_fname, const char *root_path, const DynBaseSkeleton *base_skeleton)
{
	int i;
	int return_val = 1;
	Node	*root;
	Node	**list = NULL;
	Node	**meshlist = NULL;
	AltPivotInfo** eaAPIs = NULL;

	texNameClear(1);

	loadstart_printf("processing %s",vrml_fnames[0]);

	loadstart_printf("loading VRML.. ");
	root = readVrmlFiles(vrml_fnames);
	if(!root)
	{
		loadend_printf("can't open or parse %s!\nIf GetVRML couldn't open the file "
			"a message to that effect will proceed this error. Otherwise, parsing failed for some reason.",vrml_fnames[0]);
		goto cleanup;
	}
	loadend_printf("done");
	loadstart_printf("Processing.. ");
	
	root = doAltPivots(root, &eaAPIs);	//must come before merge nodes
	if (!root)
	{
		Errorf("The file %s contains no geometry (only Alt-pivots)", vrml_fnames[0]);
		loadend_printf("done");
		goto cleanup;
	}

	root = applyTransforms(root, base_skeleton, true);
	if (targetlibrary == LIB_OBJ)
	{
		createGroupNodes(root);
	}
	else if (targetlibrary == LIB_CHAR)
	{
		// Using 'applyTransforms() instead, as mergeNodes doesn't seem to do anything most of the time, and looks wrong anyway
		//root = mergeNodes(root,0);
	}

	treeArray(root,&list);

	findBounds(list);

	crazyNormalTrick(&list);  //does both "$N" trick and "$SN" trick
	addGeomorphTargets(&list);
	addTexCoord2s(&list);

	reorderTrisByTex(list); // JE: reorders triangles

	if (targetlibrary != LIB_OBJ)
		ditchUnneededStuff(root, &list, targetlibrary);

	poolVerts(list);

	// Any triangle optimizations done in here get undone by our model binning code, unless appropriate flags are set

	// JE: Only runs this code on nodes flagged with alpha_tri_sort currently
	optimizeMesh(list, true); // CD: reorders triangles within subobjects

	for (i = 0; i < eaSize(&list); ++i)
	{
		if (list[i]->mesh.vert_count >= 65536)
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "\nThe object %s has more than 65536 vertices, not processing.\n", list[i]->name);
			loadend_printf("done");
			goto cleanup;
		}
		if (list[i]->mesh.tri_count >= 65536)
		{
			// Technically our requirement is, I think, that a material/sub-object can contain only 64k tris, might technically be fine for an object to have more tris
			printfColor(COLOR_RED|COLOR_BRIGHT, "\nThe object %s has more than 65536 triangles, not processing.\n", list[i]->name);
			loadend_printf("done");
			goto cleanup;
		}
	}

#if 0
	// This appears to do basically random things when skinned to more than 2 bones
	// Definitely messes up tootle optimization done above (for ordering to increase/decrease overdraw)
	optimizeBones(list); // JE: reorders triangles, within each set with the same texture
#endif

	// remove degenerate tris so the AutoLODer doesn't freak out
	if (targetlibrary == LIB_OBJ)
	{
		for (i = 0; i < eaSize(&list); i++)
		{
			if (gmeshMarkDegenerateTris(&list[i]->mesh))
				gmeshPool(&list[i]->mesh, false, false, false);
		}
	}

	loadend_printf("done");

	if (!g_quick_process && 0) // JE: It appears we never read these precalced reductions anymore
	{
		loadstart_printf("creating reduce instructions.. ");
		for (i = 0; i < eaSize(&list); i++)
		{
			if (!list[i]->no_lods)
				list[i]->reductions = gmeshCalculateReductions(&list[i]->mesh, list[i]->lod_distances, true, true, targetlibrary != LIB_OBJ, false);
		}
		loadend_printf("done");
	}

	// do this again to make sure the LODs don't have degenerate tris before being uvunwrapped
	if (targetlibrary == LIB_OBJ)
	{
		loadstart_printf("Removing degenerate triangles.. ");
		for (i = 0; i < eaSize(&list); i++)
		{
			if (gmeshMarkDegenerateTris(&list[i]->mesh))
				gmeshPool(&list[i]->mesh, false, false, false);
		}
		loadend_printf("done.");
	}

	if (targetlibrary == LIB_OBJ && do_unwrapping)
	{
		loadstart_printf("uvunwrapping.. ");
		for (i = 0; i < eaSize(&list); i++)
			nodeUvunwrap(list[i]);
		loadend_printf("done");
	}

	if (targetlibrary == LIB_OBJ || targetlibrary == LIB_CHAR)
	{
		loadstart_printf("Adding tangent space.. ");
		for (i = 0; i < eaSize(&list); ++i)
			gmeshAddTangentSpace(&list[i]->mesh, list[i]->wind, targetlibrary == LIB_OBJ ? group_fname : geo_fname, LOG_GETVRML);
		loadend_printf("done");
	}

	// make second list of meshes
	for (i = 0; i < eaSize(&list); ++i)
	{
		if (list[i]->mesh.tri_count)
			eaPush(&meshlist, list[i]);
	}

	if (targetlibrary == LIB_OBJ)
	{

		// add private references
		for (i = 0; i < eaSize(&all_group_nodes); ++i)
		{
			if (strEndsWith(all_group_nodes[i]->name_str, "&&"))
			{
				char base_def_name[1024];
				int j, counter = 0;

				strcpy(base_def_name, all_group_nodes[i]->name_str);
				removeEndingAmpersands(base_def_name);

				for (j = 0; j < eaSize(&all_group_nodes[i]->children); ++j)
					makePrivateGroup(base_def_name, &counter, all_group_nodes[i]->children[j]->def);
			}
		}
		for (i = 0; i < eaSize(&all_group_nodes); ++i)
		{
			if (strEndsWith(all_group_nodes[i]->name_str, "&&"))
				removeEndingAmpersands(all_group_nodes[i]->name_str);
		}

		// remove private reference notation from models
		for (i = 0; i < eaSize(&meshlist); ++i)
		{
			int len = strlen(meshlist[i]->name);
			if (meshlist[i]->name[len-1] == '&')
			{
				int j;
				for (j = 0; j < eaSize(&all_group_nodes); ++j)
				{
					if (stricmp(all_group_nodes[j]->name_str, meshlist[i]->name)==0)
					{
						removeEndingAmpersands(all_group_nodes[i]->name_str);
						removeEndingAmpersands(all_group_nodes[i]->modelname);
					}
				}

				meshlist[i]->name[len-1] = 0;
			}
		}

		if (!g_export_vrml)
			return_val = objectLibraryWriteModelnamesToFile(group_fname, all_group_nodes, g_no_checkout, is_core);

		/*
		if (eaSize(&eaAPIs)>0)
		{
			return_val = (objectLibraryWriteAltPivotInfoToFile(group_fname, &eaAPIs, g_no_checkout, is_core) && return_val);
		}
		*/
	}
	if (!g_export_vrml && return_val)
	{
		if (targetlibrary == LIB_OBJ)
		{
			return_val = writeModelHeaderToFile(group_fname, &eaAPIs, &meshlist, g_no_checkout, is_core, false);
		}
		if (targetlibrary == LIB_CHAR)
		{
			return_val = writeModelHeaderToFile(geo_fname, &eaAPIs, &meshlist, g_no_checkout, is_core, true);
		}
	}

	if (return_val)
	{
		if (g_export_vrml)
		{
			char output_vrml_name[MAX_PATH];
			changeFileExt(vrml_fnames[0], ".processed.wrl", output_vrml_name);
			outputToVrml(output_vrml_name, meshlist, eaSize(&meshlist));
		}
		else
		{
			outputPackAllNodes(geo_fname/*, big_fname*/, deps_fname, name, meshlist, eaSize(&meshlist));
		}
	}
	else
	{
	}

	// free data
cleanup:
	texNameClear(1);
	eaDestroyStruct(&eaAPIs, parse_AltPivotInfo);
	eaDestroy(&meshlist);
	eaDestroy(&list);
	freeAllGroupNodes();
	treeFree();

	loadend_printf("");
}



