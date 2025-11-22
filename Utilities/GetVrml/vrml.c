#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "stdtypes.h"
#include "utils.h"
#include "mathutil.h"
#include "error.h"
#include "file.h"
#include "mathutil.h"
#include "StringCache.h"

#include "main.h"
#include "vrml.h"
#include "tree.h"
#include "geo.h"
#include "Quat.h"
#include "qsortG.h"
#include "earray.h"
#include "stringutil.h"
#include "Color.h"

#define VERSION_TEXT "Cryptic Version "
#define VERSION_MIN 4
#define VERSION_CUR 15
#define VERSION_MAX 16 // 16 is optional upgrade just to export secondary texture coordinates

static int baddata, colorsize;

typedef struct
{
	int		vert_idxs[3];
	int		norm_idxs[3];
	int		st_idxs[3];
	int		st2_idxs[3];
	int		color_idxs[3];
	int		ao_idxs[3];
	int		tex_idx;
} TriIdx;

typedef struct BoneWeight
{
	int bone_idx;
	int weight;
} BoneWeight;

typedef struct
{
	Vec3		*verts;
	Vec3		*norms;
	Vec2		*sts;
	Vec2		*sts2;

	Vec3		*color3;
	Vec4		*color4;
	F32			*ao;

	TriIdx		*tris;

	int			st_count;
	int			st2_count;
	int			vert_count;
	int			norm_count;
	int			color_count;
	int			ao_count;
	int			tri_count;
	int			tex_idx;

	int			numbones;
	char		bonelist[MAX_OBJBONES][32];  //array of all bones affecting this Shape (second index - 1 = max number of allowed chars per bone name)
	F32			*bone_idx[MAX_OBJBONES];

	char		**mesh_names;

} VrmlShape;

static void orientPos(Vec3 v)
{
	v[0] = -v[0];
}

static void orientAngle(Vec4 v)
{
	v[0] = -v[0];
}

static int addVert(const Vec3 v, const Vec2 st, const Vec2 st2, const Vec3 norm, const Vec4 color, F32 ao, VrmlShape *shape, F32 weight[], int numbones)
{
	int i,j;

	i = shape->vert_count;
	if (!st)
		st = zerovec3;
	if (!st2)
		st2 = zerovec3;
	if (!norm)
		norm = zerovec3;
	if (!v)
		v = zerovec3;
	if (!color)
		color = unitvec4;

	if (shape->verts)
		copyVec3(v,shape->verts[i]);
	if (shape->sts)
		copyVec2(st,shape->sts[i]);
	if (shape->sts2)
		copyVec2(st2,shape->sts2[i]);
	if (shape->norms)
		copyVec3(norm,shape->norms[i]);
	if (shape->color3)
		copyVec3(color,shape->color3[i]);
	else if (shape->color4)
		copyVec4(color,shape->color4[i]);
	if (shape->ao)
		shape->ao[i] = ao;
	for (j = 0; j < numbones ; j++)
	{
		if (shape->bone_idx[j])
			(shape->bone_idx[j])[i] = weight[j];  
	}

	shape->vert_count++;
	shape->st_count = shape->vert_count;
	shape->st2_count = shape->vert_count;
	shape->norm_count = shape->vert_count;
	shape->color_count = (shape->color3||shape->color4)?shape->vert_count:0;
	shape->ao_count = shape->ao?shape->vert_count:0;

	return i;
}

static void simplifyMergeVrmlShapes(VrmlShape *shape,VrmlShape *simple)
{
	int		i,j,maxverts,tc;
	F32		*v=0,*n=0;
	F32		*st=0;
	F32		*st2=0;
	F32		*c=0;
	F32		ao = 1;
	F32     weight[MAX_OBJBONES];
	int     k;

	tc = simple->tri_count;
	maxverts = simple->vert_count + shape->tri_count * 3;
	if (!maxverts)
		return;
	if (shape->norms || simple->norms)
		simple->norms = realloc(simple->norms,maxverts * sizeof(Vec3));
	if (shape->verts || simple->verts)
		simple->verts = realloc(simple->verts,maxverts * sizeof(Vec3));
	if (shape->color3 || simple->color3)
		simple->color3 = realloc(simple->color3,maxverts * sizeof(Vec3));
	if (shape->color4 || simple->color4)
		simple->color4 = realloc(simple->color4,maxverts * sizeof(Vec4));
	if (shape->ao || simple->ao)
		simple->ao = realloc(simple->ao,maxverts * sizeof(F32));

	if (shape->sts || simple->sts)
	{
		st = calloc(maxverts, sizeof(Vec2));
		if (simple->sts)
		{
			memcpy(st,simple->sts,simple->st_count * sizeof(Vec2));
			free(simple->sts);
		}
		simple->sts = (void *)st;
	}

	if (shape->sts2 || simple->sts2)
	{
		st2 = calloc(maxverts, sizeof(Vec2));
		if (simple->sts2)
		{
			memcpy(st2,simple->sts2,simple->st2_count * sizeof(Vec2));
			free(simple->sts2);
		}
		simple->sts2 = (void *)st2;
	}

	simple->tris = realloc(simple->tris,(tc + shape->tri_count) * sizeof(TriIdx));

	for (i = 0 ; i < shape->numbones ; i++)
		simple->bone_idx[i] = realloc(simple->bone_idx[i] , maxverts * sizeof(F32));

	for (i=0;i<shape->tri_count;i++)
	{
		for (j=0;j<3;j++)
		{
			if (shape->verts)
				v =	shape->verts[shape->tris[i].vert_idxs[j]];
			if (shape->norms)
				n =	shape->norms[shape->tris[i].norm_idxs[j]];
			if (shape->sts)
				st = shape->sts[shape->tris[i].st_idxs[j]];
			if (shape->sts2)
				st2 = shape->sts2[shape->tris[i].st2_idxs[j]];
			if (shape->color3)
				c = shape->color3[shape->tris[i].color_idxs[j]];
			else if (shape->color4)
				c = shape->color4[shape->tris[i].color_idxs[j]];
			if (shape->ao)
				ao = shape->ao[shape->tris[i].ao_idxs[j]];

			for (k = 0; k < shape->numbones; k++)
				weight[k] = (shape->bone_idx[k])[shape->tris[i].vert_idxs[j]];

			simple->tris[tc + i].vert_idxs[j]	= addVert(v,st,st2,n,c,ao,simple,weight,shape->numbones); 
			simple->tris[tc + i].st_idxs[j]		= simple->tris[tc + i].vert_idxs[j];
			simple->tris[tc + i].st2_idxs[j]	= simple->tris[tc + i].vert_idxs[j];
			simple->tris[tc + i].norm_idxs[j]	= simple->tris[tc + i].vert_idxs[j];
			simple->tris[tc + i].color_idxs[j]	= simple->tris[tc + i].vert_idxs[j];
			simple->tris[tc + i].ao_idxs[j]		= simple->tris[tc + i].vert_idxs[j];
		}
		simple->tris[tc + i].tex_idx = shape->tris[i].tex_idx;
	}

	simple->tri_count	+= shape->tri_count;
	assert(simple->tri_count < 1000000 && simple->tri_count >= 0);
	simple->tex_idx		= shape->tex_idx;

	simple->numbones = shape->numbones; 
	for (i = 0; i < shape->numbones; i++)
		strcpy(simple->bonelist[i], shape->bonelist[i]);

	for (i = 0; i < eaSize(&shape->mesh_names); ++i)
	{
		eaPush(&simple->mesh_names, strdup(shape->mesh_names[i]));
	}

	eaQSortG(simple->mesh_names, strCmp);
}

static const char * const *source_wrl_files = NULL;

static void setVRMLFiles(const char * const *names)
{
	source_wrl_files = names;
}

// Attributes an error to every source WRL file, since parser cannot distinguish original source file.
void ErrorVrmlFilesfInternal(const char *file, int line, char const *fmt, ...)
{
	va_list ap;
	int i;

	va_start(ap, fmt);
	for (i = 0; i < eaSize(&source_wrl_files); ++i)
		ErrorFilenamevInternal(file, line, source_wrl_files[i], fmt, ap);
	va_end(ap);
}

#define ErrorVrmlFilesf(fmt, ...)  ErrorVrmlFilesfInternal(__FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

#define MAX_VERT_POS (32767.0f)
#define MAX_NORMAL (255.0f)
#define MAX_ST (32767.0f)

static __forceinline int vec2Valid(Vec2 pos, F32 limit)
{
	Vec2 pos_orig;
	copyVec2(pos, pos_orig);
	CLAMPVEC2(pos, -limit, limit);
	return sameVec2(pos, pos_orig);
}

static __forceinline int vec3Valid(Vec3 pos, F32 limit)
{
	Vec3 pos_orig;
	copyVec3(pos, pos_orig);
	CLAMPVEC3(pos, -limit, limit);
	return sameVec3(pos, pos_orig);
}

static __forceinline int color3Valid(Vec3 color)
{
	Vec3 color_orig;
	copyVec3(color, color_orig);
	CLAMPVEC3(color, 0.0f, 1.0f);
	return sameVec3(color, color_orig);
}

static __forceinline int color4Valid(Vec4 color)
{
	Vec4 color_orig;
	copyVec4(color, color_orig);
	CLAMPVEC4(color, 0.0f, 1.0f);
	return sameVec4(color, color_orig);
}

static int validateVrmlShape(const VrmlShape * vrml_shape)
{
	int i, m;
	int geometryValidationError = 0;
	for (i = 0, m = vrml_shape->vert_count; i < m; ++i)
		if (!vec3Valid(vrml_shape->verts[i], MAX_VERT_POS))
			geometryValidationError |= USE_POSITIONS;
	for (i = 0, m = vrml_shape->norm_count; i < m; ++i)
		if (!vec3Valid(vrml_shape->norms[i], MAX_NORMAL))
			geometryValidationError |= USE_NORMALS;
	for (i = 0, m = vrml_shape->st_count; i < m; ++i)
		if (!vec2Valid(vrml_shape->sts[i], MAX_ST))
			geometryValidationError |= USE_TEX1S;
	for (i = 0, m = vrml_shape->st2_count; i < m; ++i)
		if (!vec2Valid(vrml_shape->sts2[i], MAX_ST))
			geometryValidationError |= USE_TEX2S;
	for (i = 0, m = vrml_shape->numbones; i < m; ++i)
	{
		int k;
		for (k = 0 ; k < vrml_shape->vert_count; k++)
		{
			F32 orig_weight = (vrml_shape->bone_idx[i])[k];
			F32 weight = CLAMPF32(orig_weight, 0.0f, 1.0f);
			(vrml_shape->bone_idx[i])[k] = weight;
			if (weight != orig_weight)
				geometryValidationError |= USE_BONEWEIGHTS;
		}
	}

	if (vrml_shape->color3)
	{
		for (i = 0, m = vrml_shape->color_count; i < m; ++i)
			if (!color3Valid(vrml_shape->color3[i]))
				geometryValidationError |= USE_COLORS;
	}
	if (vrml_shape->color4)
	{
		for (i = 0, m = vrml_shape->color_count; i < m; ++i)
			if (!color4Valid(vrml_shape->color4[i]))
				geometryValidationError |= USE_COLORS;
	}

	return geometryValidationError;
}


static void freeVrmlShape(VrmlShape *shape)
{
	int i;
	SAFE_FREE(shape->norms);
	SAFE_FREE(shape->verts);
	SAFE_FREE(shape->sts);
	SAFE_FREE(shape->sts2);
	SAFE_FREE(shape->color3);
	SAFE_FREE(shape->color4);
	SAFE_FREE(shape->ao);
	SAFE_FREE(shape->tris);

	for (i = 0; i < shape->numbones; i++)
	{
		SAFE_FREE(shape->bone_idx[i]);
	}

	eaDestroyEx(&shape->mesh_names, NULL);

	shape->numbones = 0;

	shape->norm_count = 0;
	shape->vert_count = 0;
	shape->st_count = 0;
	shape->st2_count = 0;
	shape->color_count = 0;
	shape->ao_count = 0;
	shape->tri_count = 0;
}

static int cmpBoneWeight(const BoneWeight *b1, const BoneWeight *b2)
{
	int t = b2->weight - b1->weight;
	if (!t)
		t = b1->bone_idx - b2->bone_idx;
	return t;
}

static int cmpBoneIdx(const BoneWeight *b1, const BoneWeight *b2)
{
	return b1->bone_idx - b2->bone_idx;
}

static void convertVrmlShapeToGMesh(BoneData *bones, GMesh *mesh, const VrmlShape *shape, bool has_wind)
{
	Vec4 *bone_weights = 0;
	GBoneMats *bone_matidxs = 0;
	int i, j, usage = 0;

	gmeshFreeData(mesh);

	if (shape->vert_count)
		usage |= USE_POSITIONS;
	if (shape->norm_count)
		usage |= USE_NORMALS;
	if (shape->st_count)
		usage |= USE_TEX1S;
	if (shape->sts2)
		usage |= USE_TEX2S;
	if (shape->color_count || shape->ao_count)
		usage |= USE_COLORS;
	if (shape->numbones)
		usage |= USE_BONEWEIGHTS;
	gmeshSetUsageBits(mesh, usage);

	// calculate bone weights and matrix indices
	if (shape->numbones)
	{
		BoneWeight bone_weight_infos[MAX_OBJBONES];

		bone_weights = calloc(shape->vert_count, sizeof(*bone_weights));
		bone_matidxs = calloc(shape->vert_count, sizeof(*bone_matidxs));
		for (i = 0; i < shape->vert_count; i++)
		{
			int total = 0;

			memset(bone_weight_infos, 0, MAX_OBJBONES * sizeof(BoneWeight));
			for (j = 0; j < MAX_OBJBONES; j++)
			{
				bone_weight_infos[j].bone_idx = j*3;
				if (j < shape->numbones)
					bone_weight_infos[j].weight = round(255 * MAX(0, shape->bone_idx[j][i]));
			}

			qsort(bone_weight_infos, MAX_OBJBONES, sizeof(BoneWeight), cmpBoneWeight);

			for (j = 0; j < 4; ++j)
			{
				if (total >= 255)
				{
					bone_weight_infos[j].weight = 0;
				}
				else if (total + bone_weight_infos[j].weight >= 255)
				{
					bone_weight_infos[j].weight = MAX(0, 255 - total);
					total = 255;
				}
				else
				{
					total += bone_weight_infos[j].weight;
				}
			}

			if (total < 255)
			{
				bone_weight_infos[0].weight += 255 - total;
			}

			qsort(bone_weight_infos, 4, sizeof(BoneWeight), cmpBoneIdx);

			for (j = 0; j < 4; ++j)
			{
				bone_matidxs[i][j] = bone_weight_infos[j].bone_idx;
				if (!bone_weight_infos[j].weight && j > 0)
					bone_matidxs[i][j] = bone_matidxs[i][j-1];
				bone_weights[i][j] = bone_weight_infos[j].weight * (1/255.f);
			}

		}
	}

	// add tris and verts to generic mesh
	for (i = 0; i < shape->tri_count; i++)
	{
		TriIdx *tri = &shape->tris[i];
		int idx0, idx1, idx2;
		F32 *vert1 = shape->verts?shape->verts[tri->vert_idxs[0]]:0;
		F32 *vert2 = shape->verts?shape->verts[tri->vert_idxs[1]]:0;
		F32 *vert3 = shape->verts?shape->verts[tri->vert_idxs[2]]:0;
		F32 *norm1 = shape->norms?shape->norms[tri->norm_idxs[0]]:0;
		F32 *norm2 = shape->norms?shape->norms[tri->norm_idxs[1]]:0;
		F32 *norm3 = shape->norms?shape->norms[tri->norm_idxs[2]]:0;
		Vec4 facenorm;
		Color c;

		if (vert1 && vert2 && vert3)
		{
			makePlane(vert1, vert2, vert3, facenorm);
			if (norm1 && lengthVec3Squared(norm1) < 0.0001f)
				norm1 = facenorm;
			if (norm2 && lengthVec3Squared(norm2) < 0.0001f)
				norm2 = facenorm;
			if (norm3 && lengthVec3Squared(norm3) < 0.0001f)
				norm3 = facenorm;
		}

		if (colorsize==3)
		{
			Vec4 colorvec;

			if (shape->color3)
				copyVec3(shape->color3[tri->color_idxs[0]], colorvec);
			else
				setVec3same(colorvec, 1);
			if (shape->ao)
				colorvec[3] = shape->ao[tri->ao_idxs[0]];
			else
				colorvec[3] = 1;
			idx0 = gmeshAddVertSimple2(mesh, 
				vert1,
				norm1,
				shape->sts?shape->sts[tri->st_idxs[0]]:0,
				shape->sts2?shape->sts2[tri->st2_idxs[0]]:0,
				(shape->color3||shape->ao)?vec4ToColor(&c, colorvec):0,
				shape->numbones?bone_weights[tri->vert_idxs[0]]:0,
				shape->numbones?bone_matidxs[tri->vert_idxs[0]]:0,
				0, has_wind, true);

			if (shape->color3)
				copyVec3(shape->color3[tri->color_idxs[1]], colorvec);
			else
				setVec3same(colorvec, 1);
			if (shape->ao)
				colorvec[3] = shape->ao[tri->ao_idxs[1]];
			else
				colorvec[3] = 1;
			idx1 = gmeshAddVertSimple2(mesh, 
				vert2,
				norm2,
				shape->sts?shape->sts[tri->st_idxs[1]]:0,
				shape->sts2?shape->sts2[tri->st2_idxs[1]]:0,
				(shape->color3||shape->ao)?vec4ToColor(&c, colorvec):0,
				shape->numbones?bone_weights[tri->vert_idxs[1]]:0,
				shape->numbones?bone_matidxs[tri->vert_idxs[1]]:0,
				0, has_wind, true);

			if (shape->color3)
				copyVec3(shape->color3[tri->color_idxs[2]], colorvec);
			else
				setVec3same(colorvec, 1);
			if (shape->ao)
				colorvec[3] = shape->ao[tri->ao_idxs[2]];
			else
				colorvec[3] = 1;
			idx2 = gmeshAddVertSimple2(mesh, 
				vert3,
				norm3,
				shape->sts?shape->sts[tri->st_idxs[2]]:0,
				shape->sts2?shape->sts2[tri->st2_idxs[2]]:0,
				(shape->color3||shape->ao)?vec4ToColor(&c, colorvec):0,
				shape->numbones?bone_weights[tri->vert_idxs[2]]:0,
				shape->numbones?bone_matidxs[tri->vert_idxs[2]]:0,
				0, has_wind, true);
		}
		else
		{
			idx0 = gmeshAddVertSimple2(mesh, 
				vert1,
				norm1,
				shape->sts?shape->sts[tri->st_idxs[0]]:0,
				shape->sts2?shape->sts2[tri->st2_idxs[0]]:0,
				shape->color4?vec4ToColor(&c, shape->color4[tri->color_idxs[0]]):0,
				shape->numbones?bone_weights[tri->vert_idxs[0]]:0,
				shape->numbones?bone_matidxs[tri->vert_idxs[0]]:0,
				0, has_wind, true);
			idx1 = gmeshAddVertSimple2(mesh, 
				vert2,
				norm2,
				shape->sts?shape->sts[tri->st_idxs[1]]:0,
				shape->sts2?shape->sts2[tri->st2_idxs[1]]:0,
				shape->color4?vec4ToColor(&c, shape->color4[tri->color_idxs[1]]):0,
				shape->numbones?bone_weights[tri->vert_idxs[1]]:0,
				shape->numbones?bone_matidxs[tri->vert_idxs[1]]:0,
				0, has_wind, true);
			idx2 = gmeshAddVertSimple2(mesh, 
				vert3,
				norm3,
				shape->sts?shape->sts[tri->st_idxs[2]]:0,
				shape->sts2?shape->sts2[tri->st2_idxs[2]]:0,
				shape->color4?vec4ToColor(&c, shape->color4[tri->color_idxs[2]]):0,
				shape->numbones?bone_weights[tri->vert_idxs[2]]:0,
				shape->numbones?bone_matidxs[tri->vert_idxs[2]]:0,
				0, has_wind, true);
		}
		gmeshAddTri(mesh, idx0, idx1, idx2, tri->tex_idx, 0);
	}

	// fill bone data
	for(i = 0; i < shape->numbones; i++)
		eaPush(&bones->boneNames, allocAddString(shape->bonelist[i]));

	// cleanup
	SAFE_FREE(bone_weights);
	SAFE_FREE(bone_matidxs);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#define MEMCHUNK (2 << 20)

typedef struct
{
	char *name;
	int value;
} VrmlToken;

enum
{
	VRML_DEF = 1,
	VRML_USE,
	VRML_TRANSFORM,
	VRML_TRANSLATION,
	VRML_PIVOT,
	VRML_ROTATION,
	VRML_SCALE,
	VRML_SCALEORIENTATION,
	VRML_CENTER,
	VRML_CHILDREN,
	VRML_TIMESENSOR,
	VRML_POSINTERP,
	VRML_KEY,
	VRML_KEYVALUE,
	VRML_ROTINTERP,
	VRML_SHAPE,
	VRML_APPEARANCE,
	VRML_MATERIAL,
	VRML_TEXTURE,
	VRML_URL,
	VRML_DIFFUSECOLOR,
	VRML_GEOMETRY,
	VRML_INDEXEDFACESET,
	VRML_CCW,
	VRML_SOLID,
	VRML_CONVEX,
	VRML_COLORPERVERTEX,
	VRML_COLORINDEX,
	VRML_AMBIENTOCCLUSION,
	VRML_AMBIENTOCCLUSIONINDEX,
	VRML_COORD,
	VRML_COORDINDEX,
	VRML_TEXCOORD,
	VRML_TEXCOORDINDEX,
	VRML_TEX2COORD,
	VRML_TEX2COORDINDEX,
	VRML_NODESTART,
	VRML_NODEEND,
	VRML_ARRAYSTART,
	VRML_ARRAYEND,
	VRML_POINTLIGHT,
	VRML_SPOTLIGHT,
	VRML_INTENSITY,
	VRML_COLOR,
	VRML_LOCATION,
	VRML_RADIUS,
	VRML_ATTENUATION,
	VRML_ON,
	VRML_FALSE,
	VRML_TRUE,
	VRML_SKIN, 
	VRML_NORMAL, 
	VRML_NORMALINDEX,
	VRML_NORMALPERVERTEX, 
	VRML_TEXTURETRANSFORM,
};	
	
VrmlToken vrml_tokens[] =
{
	{ "DEF",					VRML_DEF },
	{ "USE",					VRML_USE },
	{ "Transform",				VRML_TRANSFORM },
	{ "translation",			VRML_TRANSLATION },
	{ "pivot",					VRML_PIVOT },
	{ "rotation",				VRML_ROTATION },
	{ "scale",					VRML_SCALE },
	{ "scaleOrientation",		VRML_SCALEORIENTATION },
	{ "center",					VRML_CENTER },
	{ "children",				VRML_CHILDREN },
	{ "TimeSensor",				VRML_TIMESENSOR },
	{ "PositionInterpolator",	VRML_POSINTERP },
	{ "key",					VRML_KEY },
	{ "keyvalue",				VRML_KEYVALUE },
	{ "OrientationInterpolator",VRML_ROTINTERP },
	{ "Shape",					VRML_SHAPE },
	{ "appearance",				VRML_APPEARANCE },
	{ "material",				VRML_MATERIAL },
	{ "texture",				VRML_TEXTURE },
	{ "url",					VRML_URL },
	{ "DiffuseColor",			VRML_DIFFUSECOLOR },
	{ "geometry",				VRML_GEOMETRY },
	{ "ccw",					VRML_CCW },
	{ "convex",					VRML_CONVEX },
	{ "IndexedFaceSet",			VRML_INDEXEDFACESET },
	{ "solid",					VRML_SOLID },
	{ "coord",					VRML_COORD },
	{ "coordindex",				VRML_COORDINDEX },
	{ "texcoord",				VRML_TEXCOORD },
	{ "texcoordindex",			VRML_TEXCOORDINDEX },
	{ "tex2Coord",				VRML_TEX2COORD },
	{ "tex2CoordIndex",			VRML_TEX2COORDINDEX },
	{ "PointLight",				VRML_POINTLIGHT },
	{ "SpotLight",				VRML_SPOTLIGHT },
	{ "intensity",				VRML_INTENSITY },
	{ "color",					VRML_COLOR },
	{ "ColorPerVertex",			VRML_COLORPERVERTEX },
	{ "ColorIndex",				VRML_COLORINDEX },
	{ "AmbientOcclusion",		VRML_AMBIENTOCCLUSION },
	{ "AmbientOcclusionIndex",	VRML_AMBIENTOCCLUSIONINDEX },
	{ "location",				VRML_LOCATION },
	{ "radius",					VRML_RADIUS },
	{ "attenuation",			VRML_ATTENUATION },
	{ "on",						VRML_ON },
	{ "true",					VRML_TRUE },
	{ "false",					VRML_FALSE },
	{ "{",						VRML_NODESTART },
	{ "}",						VRML_NODEEND },
	{ "[",						VRML_ARRAYSTART },
	{ "]",						VRML_ARRAYEND },
	{ "skin",                   VRML_SKIN }, 
	{ "normal",                 VRML_NORMAL },
	{ "normalIndex",            VRML_NORMALINDEX },
	{ "normalPerVertex",        VRML_NORMALPERVERTEX }, 
	{ "textureTransform",		VRML_TEXTURETRANSFORM },
	{ 0,						0 },
};

char last_tokname[1000];

typedef struct
{
	char	*text;
	int		idx;
	char	token[1024];
	int		cmd;
} TokenInfo;

TokenInfo vrml_token_info;

static void setVrmlText(char *text)
{
	memset(&vrml_token_info,0,sizeof(vrml_token_info));
	vrml_token_info.text = text;
}

SA_ORET_OP_STR static char *getTok()
{
char		*s;
int			idx,quote=0;
TokenInfo	*tok;

	tok = &vrml_token_info;
	s = tok->text + tok->idx;
	for(;;)
	{
		if (*s == 0)
			return 0;
		if (*s == '#')
		{
			// skip both newline & CR characters
			idx = strcspn(s,"\n\r");
			s += idx;
			idx = strspn(s,"\n\r");
			s += idx;
			continue;
		}
		else
		{
			// skip blank lines
			int i, isblankline = 0;
			for (i = 0; s[i] && isspace((unsigned char)s[i]); ++i)
			{
				if (s[i] == '\n' || s[i] == '\r')
				{
					isblankline = 1;
					break;
				}
			}

			if (isblankline)
			{
				// skip both newline & CR characters
				s += i;
				idx = strspn(s,"\n\r");
				s += idx;
				continue;
			}
		}

		idx = strspn(s,"\n\r \t,");
		s += idx;
		if (*s == '[' || *s == ']' || *s == '{' || *s == '}')
			idx = 1;
		else if (*s == '"')
		{
			s++;
			idx = strcspn(s,"\n\r\"");
			quote = 1;
		}
		else
			idx = strcspn(s,"\n\r \t,]}[{");
		strncpy(tok->token,s,idx);
		tok->token[idx] = 0;
		if (quote)
			idx += strcspn(s + idx,"\n\r \t,]}[{");
		tok->idx = s - tok->text + idx;
		if (tok->token[0] == 0)
			return 0;
		return tok->token;
	}
}

static int tokNameCmp (const VrmlToken *cmd1, const VrmlToken *cmd2)
{
	if (!cmd1 || !cmd2)
		return -1;
	return stricmp(cmd1->name,cmd2->name);
}

static int cmdSort(VrmlToken *tokens)
{
int		count;

	for(count=0;tokens[count].name;count++)
		;
	qsort(tokens,count,sizeof(VrmlToken),
		  (int (*) (const void *, const void *)) tokNameCmp);
	return count;
}

static int cmdNum(SA_PARAM_OP_STR char *s)
{
VrmlToken	search,*match;
static	int vrml_token_count;

	if (!vrml_token_count)
		vrml_token_count = cmdSort(vrml_tokens);
	if (!s)
		return 0;
	strcpy(last_tokname,s);

	search.name = s;

	match = bsearch(&search, vrml_tokens, vrml_token_count,
				sizeof(VrmlToken),(int (*) (const void *, const void *))tokNameCmp);

	if (match)
		return match->value;
	return -1;
#if 0
int		i;

	for(i=0;vrml_tokens[i].name;i++)
	{
		if (stricmp(s,vrml_tokens[i].name) == 0)
			return vrml_tokens[i].value;
	}
	return -1;
#endif
}

static int getCmd()
{
	return cmdNum(getTok());
}

static void skipBlock()
{
int		depth = 0,cmd;

	for(;;)
	{
		cmd = getCmd();
		if (!cmd)
			FatalErrorf("Got lost in skipblock, depth = %d\n",depth);
		
		if (cmd == VRML_ARRAYSTART || cmd == VRML_NODESTART)
			depth++;
		if (cmd == VRML_ARRAYEND || cmd == VRML_NODEEND)
			depth--;
		if (!depth && (cmd == VRML_NODEEND || cmd == VRML_ARRAYEND))
			return;
	}
	printf("");
}

static F32 getF32()
{
	return atof(getTok());
}

static void getVec3(Vec3 v)
{
int		i;

	for(i=0;i<3;i++)
		v[i] = atof(getTok());
}

static void getVec4(Vec4 v)
{
int		i;

	for(i=0;i<4;i++)
		v[i] = atof(getTok());
}

#define getF32s(count) getF32s_dbg(count, __FILE__, __LINE__)
static F32 *getF32s_dbg(int *count, const char *caller_fname, int line)
{
char	*s;
static F32	*list;
static int list_len;
F32		*ptr;
int		i;

	getCmd();
	for(i=0;;i++)
	{
		s = getTok();
		if (*s == ']')
			break;
		if (i >= list_len)
		{
			list_len+=1000;
			list = srealloc(list,sizeof(list[0]) * list_len);
		}
		list[i] = atof(s);
	}
	ptr = smalloc(i * sizeof(F32));
	memcpy(ptr,list,i * sizeof(F32));
	//free(list);
	*count = i;
	return ptr;
}

static S32 *getS32s(int *count)
{
char	*s;
static S32	*list;
static int list_len;
S32		*ptr;
int		i;

	getCmd();
	for(i=0;;i++)
	{
		s = getTok();
		if (*s == ']')
			break;
		if (i >= list_len)
		{
			list_len+=1000;
			list = realloc(list,sizeof(list[0]) * list_len);
		}
		list[i] = atoi(s);
	}
	ptr = malloc(i * sizeof(S32));
	memcpy(ptr,list,i * sizeof(S32));
	//free(list);
	*count = i;
	return ptr;
}

static void getPosKeys(PosKeys *keys)
{
int		found = 0,cmd,spam;

	while(found != 2)
	{
		cmd = getCmd();
		if (cmd == VRML_KEY)
		{
			keys->pfTimes = getF32s(&keys->uiCount);
			found++;
		}
		if (cmd == VRML_KEYVALUE)
		{
			keys->pvPos = (Vec3*)getF32s(&spam);
			found++;
		}
	}
	getCmd();
}

static void getRotKeys(RotKeys *keys)
{
	int		found = 0,cmd,spam;

	while(found != 2)
	{
		cmd = getCmd();
		if (cmd == VRML_KEY)
		{
			keys->pfTimes = getF32s(&keys->uiCount);
			found++;
		}
		if (cmd == VRML_KEYVALUE)
		{
			keys->pvAxisAngle = (Vec4*)getF32s(&spam);
			found++;
		}
	}
	getCmd();
}

static void getAppearance(VrmlShape *shape, const char *name)
{
int		cmd;
char	*s;

	for(;;)
	{
		cmd = getCmd();
		if (cmd == VRML_MATERIAL || cmd == VRML_TEXTURETRANSFORM)
			skipBlock();
		else if (cmd == VRML_TEXTURE)
		{
			getTok();
			getCmd();
			getTok();
			s = getTok();
#ifdef GETVRML
			shape->tex_idx = texNameAdd(s, name); //to do, extricate this from vrml file reading
#else
			shape->tex_idx = 0; 
#endif
			getTok();
		}
		else if (cmd == VRML_NODEEND)
			break;
	
	}
}

static void getLight(char *name)
{
int		depth = 0;
Vec3	garbage;
F32		junk;

	for(;;)
	{
		switch(getCmd())
		{
			case VRML_INTENSITY:
				junk = getF32();
			xcase VRML_COLOR:
				getVec3(garbage);
			xcase VRML_LOCATION:
				getVec3(garbage);
			xcase VRML_RADIUS:
				junk = getF32();
			xcase VRML_ATTENUATION:
				getVec3(garbage);
			xcase VRML_ON:
				getTok(); // should check it, but what the hey
			xcase VRML_NODESTART:
				depth++;
			xcase VRML_NODEEND:
				if (--depth <= 0)
					return;
		}
	}
}

static F32 *getCoords(int *count)
{
	F32		*coords;
	const char *tok;

	while (strcmp(tok = getTok(), "point")!=0)
		assertmsg(tok, "Reached EOF looking for 'point' to start coords");
	coords = getF32s(count);
	getTok();
	return coords;
}

static F32 *getColors(int *count)
{
	F32		*coords;

	getTok();
	getTok();
	getTok();
	coords = getF32s(count);
	getTok();
	return coords;
}

static F32 *getAOs(int *count)
{
	F32		*coords;

	getTok();
	getTok();
	getTok();
	coords = getF32s(count);
	getTok();
	return coords;
}

static F32 *getNormals(int *count)
{
F32		*coords;

	getTok();
	getTok();
	getTok();
	coords = getF32s(count);
	getTok();
	return coords;
}

//mm Assumes that the vertex coordinate listing came before the bone listing in the VRML file.  
#pragma warning(disable:6262) // Using more than 16k stack (used 23k)
static void loadBoneWeights(VrmlShape * vrml_shape, int numverts, const char *name)
{
	char Token[1024];
	char bone[1024];
	char extrabones[20][1024];
	float weight;
	int anothervertex;
	int anotherbone;
	int vertexindex;
	int numbones;
	int numextrabones;
	int position;
	int idx;

	anothervertex = 1;
	vertexindex   = 0;	
	numbones = 0;
	numextrabones = 0;

	strcpy(Token, getTok()); //get first "["

	while (anothervertex) 
	{
		if(vertexindex > numverts)
		{
			ErrorDetailsf("%s",
				"This error might be due to a MAX object history bug; see below, and contact Ben Henderson, Matt Highison, or a member of the graphics team about this fix.\n\n"
				"The Problem:\n"
				"Exporting skinned geo to a .WRL file produces one extra set of skin weights then there are actual vertices. This should never be the case, "
				"obviously, and will trigger this GetVrml fatal error.\n\n"
				"The Solution:\n"
				"Create a sphere (or any simple piece of geo for that matter) and convert it into a \"editable poly\". Select the problem piece of geo, cut the skin, "
				"attach the sphere to the geo, remove the attached piece of geo, and paste the skin back on. The phantom vertex should now be gone.\n"
				"The problem seems to stem from Max maintaining a history on the object that may be corrupted. The above solution cleans up the history on the object.\n"
				);
			FatalErrorf("There are more bones in this shape than vertices. See error details for potential fix & more info.");
		}
		
		strcpy(Token, getTok()); //get rid of first "("
		if(strcmp(Token, "]")) //if not end of list, process the next vertex
		{
			anotherbone = 1;
			while (anotherbone)
			{
				strcpy(bone, getTok());
				if(strcmp(bone, ")"))
				{ 
					strcpy(Token, getTok());
					weight = atof(Token);
					
					//now I have a bone and a weight and a vertex index.
					position = -1;
					for(idx = 0 ; idx < numbones ; idx++)
					{
						if(!strcmp(bone, vrml_shape->bonelist[idx]))
						{
							position = idx;
							break;
						}
					}
					if(position == -1)
					{	
						if (numbones < MAX_OBJBONES)
						{
							vrml_shape->bone_idx[numbones] = malloc(numverts * sizeof(F32));
							memset(vrml_shape->bone_idx[numbones], 0, numverts * sizeof(F32));
							
							strcpy(vrml_shape->bonelist[numbones], bone);
							
							position = numbones; 
							numbones++;
						}
						else
						{
							char bonelistfile[1024] = "c:/bonelist_";
							FILE *f;
							int i;
							int b;
							float bestweight;

							strcat(bonelistfile, name);
							strcat(bonelistfile, ".txt");
							f = fopen(bonelistfile, "wt");

							bestweight = 0.0;
							
							// Is this a new bad bone?
							for(b = 0; b < numextrabones; ++b)
							{
								if(!strcmp(bone, extrabones[b]))
								{
									position = b;
									break;
								}
							}
							// if it's a new bad bone, store it in our overflow array
							if(position == -1)
							{
								if (numextrabones < 20)
									strcpy(extrabones[numextrabones], bone);
								numextrabones++;
							}

							if (f)
							{
								for (i = 0; i < MAX_OBJBONES; ++i)
									fprintf(f, "%s\n", vrml_shape->bonelist[i]);
								for (i = 0; i < numextrabones; ++i)
									fprintf(f, "%s\n", extrabones[i]);
								fclose(f);
							}
							Errorf("Shape \"%s\" has too many bones!(%d) Current bone (%s)\n", name, (numbones + numextrabones), bone);
							printfColor(COLOR_RED|COLOR_BRIGHT, "Bad skin data, skinning will look wrong! Bone list saved to c:/bonelist_%s.txt\n", name);
							// add weight to last valid bone so it doesn't break... don't forget to add it instead of replace if there's already weight
							position = MAX_OBJBONES - 1;
							// find the best place to assign this weight too...
							// not needed now that we're not writing if it's bad
							/*for(b = 0; b < numbones; ++b)
							{						
								if(bestweight > (vrml_shape->bone_idx[b])[vertexindex])
								{			
									bestweight = (vrml_shape->bone_idx[b])[vertexindex];
									position = b;
								}
							}*/
							
							// after all that... tell readVrmlFiles that we have bad data so it doesn't export...
							baddata = 1;
						}
					}
					(vrml_shape->bone_idx[position])[vertexindex] = (vrml_shape->bone_idx[position])[vertexindex] + weight;
				}
				else
				{
					anotherbone = 0;
				}
			}
			vertexindex++;
		}
		else{ anothervertex = 0;}
	}
	vrml_shape->numbones = numbones;

	if(vertexindex != numverts)
		FatalErrorf("vertex count != boneweight count.  Might fixed by reset xform.");

	//mm Order the bones here.  When I get around to it.  Heirarchical?
	//You now have numbones bone_idx  * [numverts]
	//  		   numbones bone_list 
}
//end mm*/

static TriIdx *setTriIdxs(TriIdx *tris,int *idxs,int count,int tex_idx,int type,int ccw)
{
int		i,j,idx;

	count /= 4;
	if (!tris)
		tris = calloc(count,sizeof(TriIdx));
	for(i=0;i<count;i++)
	{
		if (idxs[i*4+3] != -1)
		{
			FatalErrorf("ERROR! Non-triangle polys in file! Giving up.\n");
			exit(-1);
		}
		for(j=0;j<3;j++)
		{
			idx = j;
			if (ccw)
				idx = 2-idx;
			if (type == VRML_COORDINDEX)
				tris[i].vert_idxs[idx] = idxs[i*4+j];
			else if (type == VRML_TEXCOORDINDEX)
				tris[i].st_idxs[idx] = idxs[i*4+j];
			else if (type == VRML_TEX2COORDINDEX)
				tris[i].st2_idxs[idx] = idxs[i*4+j];
			else if (type == VRML_NORMALINDEX)
				tris[i].norm_idxs[idx] = idxs[i*4+j]; 
			else if (type == VRML_COLORINDEX)
				tris[i].color_idxs[idx] = idxs[i*4+j]; 
			else if (type == VRML_AMBIENTOCCLUSIONINDEX)
				tris[i].ao_idxs[idx] = idxs[i*4+j]; 
		}
		tris[i].tex_idx = tex_idx;
	}
	return tris;
}

static const char **shapes_without_tris;
static const char **shapes_with_tris;
static void printShapeWarnings(void)
{
	FOR_EACH_IN_EARRAY_FORWARDS(shapes_without_tris, const char, name)
	{
		if (-1==eaFind(&shapes_with_tris, name))
		{
			printfColor(COLOR_RED|COLOR_GREEN,
				"\nWarning: Shape (%s) with vertices but no triangles.\n"
				"         Possibly caused by a bad material id.\n", name);
		}
	}
	FOR_EACH_END;

	eaDestroy(&shapes_with_tris);
	eaDestroy(&shapes_without_tris);
}

typedef struct VRML_getGeometryResult
{
	GMeshAttributeUsage attributes_with_errors;
} VRML_getGeometryResult;

static VRML_getGeometryResult getGeometry(VrmlShape *out_shape, char * name) 
{
	int			cmd,count,*idxs,i,ccw=1;
	VrmlShape	vrml_shape;
	char		*tok;
	VRML_getGeometryResult result = { USE_EMPTY };

	memset(&vrml_shape,0,sizeof(vrml_shape));
	vrml_shape.tex_idx = out_shape->tex_idx;

	tok = getTok();
	cmd = cmdNum(tok);
	if (cmd!=VRML_DEF && cmd!=VRML_USE) {
		printfColor(COLOR_RED|COLOR_BRIGHT, "can only read geometry DEFs, found '%s' instead - skipping (object %s)\n", tok, name);
		skipBlock();
	} else {
		eaPush(&vrml_shape.mesh_names, strdup(getTok()));
		cmd = getCmd();
		if (cmd != VRML_INDEXEDFACESET)
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "can only read indexedfaceset - skipping (object %s)\n", name);
			skipBlock();
		}
		else
		{
			getCmd();
			for(;;)
			{
				cmd = getCmd();
				switch(cmd)
				{
					case VRML_SOLID:
						getTok();
					xcase VRML_CCW:
						ccw = getCmd() == VRML_TRUE;
					xcase VRML_CONVEX:
						getCmd();
					xcase VRML_COLORPERVERTEX:
						getCmd();
					xcase VRML_COLOR:
						assert(!vrml_shape.color3 && !vrml_shape.color4);
						if (colorsize == 3)
							vrml_shape.color3 = (void *)getColors(&count);
						else
							vrml_shape.color4 = (void *)getColors(&count);
						vrml_shape.color_count = count/colorsize;
					xcase VRML_COLORINDEX:
						idxs = getS32s(&count);
						if(count/4 == vrml_shape.tri_count) //if it's not screwy, get the colors...
						{
							vrml_shape.tris = setTriIdxs(vrml_shape.tris,idxs,count,vrml_shape.tex_idx,VRML_COLORINDEX,ccw);
						}
						else
						{
							printfColor(COLOR_RED|COLOR_BRIGHT, "Geometry %s colors != tris*4. (%d colors, %d triangles) Skipping...\n", name, count, vrml_shape.tri_count);
							SAFE_FREE(vrml_shape.color3);
							SAFE_FREE(vrml_shape.color4);
							vrml_shape.color_count = 0;
						}
						SAFE_FREE(idxs);
					xcase VRML_AMBIENTOCCLUSION:
						assert(colorsize == 3);
						assert(!vrml_shape.ao);
						vrml_shape.ao = (void *)getAOs(&count);
						vrml_shape.ao_count = count;
					xcase VRML_AMBIENTOCCLUSIONINDEX:
						idxs = getS32s(&count);
						if(count/4 == vrml_shape.tri_count) //if it's not screwy, get the colors...
						{
							vrml_shape.tris = setTriIdxs(vrml_shape.tris,idxs,count,vrml_shape.tex_idx,VRML_AMBIENTOCCLUSIONINDEX,ccw);
						}
						else
						{
							printfColor(COLOR_RED|COLOR_BRIGHT, "Geometry %s ambient occlusion != tris*3. (%d ambient occlusion values, %d triangles) Skipping...\n", name, count, vrml_shape.tri_count);
							SAFE_FREE(vrml_shape.ao);
							vrml_shape.ao_count = 0;
						}
						SAFE_FREE(idxs);
					xcase VRML_COORD:
						assert(!vrml_shape.verts);
						vrml_shape.verts = (void *)getCoords(&count);
						vrml_shape.vert_count = count/3;
						for(i=0;i<vrml_shape.vert_count;i++)
						{
							orientPos(vrml_shape.verts[i]);
						}
					xcase VRML_SKIN:
						loadBoneWeights(&vrml_shape, vrml_shape.vert_count, name);
					xcase VRML_NORMAL: 
						vrml_shape.norms = (void *)getNormals(&count);
						vrml_shape.norm_count = count/3;
						for(i=0;i<vrml_shape.norm_count;i++)
						{
							orientPos(vrml_shape.norms[i]);
						}
					xcase VRML_NORMALINDEX:
						idxs = getS32s(&count);
						if(count/4 == vrml_shape.tri_count) //if it's not screwy, get the normals...
							vrml_shape.tris = setTriIdxs(vrml_shape.tris,idxs,count,vrml_shape.tex_idx,VRML_NORMALINDEX,ccw);
						else
						{
							printfColor(COLOR_RED|COLOR_BRIGHT, "Geometry %s normals != tris*3. (%d normals, %d triangles) Skipping...\n", name, count, vrml_shape.tri_count);
							free(vrml_shape.norms);
							vrml_shape.norms = 0;
							vrml_shape.norm_count = 0;
						}
						free(idxs);
						idxs = 0;
					xcase VRML_NORMALPERVERTEX:
						getCmd();
					xcase VRML_TEXCOORD:
						assert(!vrml_shape.sts);
						vrml_shape.sts = (void *)getCoords(&count);
						vrml_shape.st_count = count/2;
					xcase VRML_TEX2COORD:
						assert(!vrml_shape.sts2);
						vrml_shape.sts2 = (void *)getCoords(&count);
						vrml_shape.st2_count = count/2;
					xcase VRML_COORDINDEX:
						idxs = getS32s(&count);
						vrml_shape.tris = setTriIdxs(vrml_shape.tris,idxs,count,vrml_shape.tex_idx,VRML_COORDINDEX,ccw);
						vrml_shape.tri_count = count / 4;
						assert(vrml_shape.tri_count < 1000000 && vrml_shape.tri_count >= 0);
						free(idxs);
					xcase VRML_TEXCOORDINDEX:
					case VRML_TEX2COORDINDEX:
						idxs = getS32s(&count);
						vrml_shape.tris = setTriIdxs(vrml_shape.tris,idxs,count,vrml_shape.tex_idx,cmd,ccw);
						free(idxs);
					xcase VRML_NODEEND:
					{
						const char *cname = allocAddCaseSensitiveString(name);
						int vrmlValidAndInRange = 0;

						if (!vrml_shape.tris || !vrml_shape.tri_count) 
						{
							if (vrml_shape.vert_count)
							{
								if (-1==eaFind(&shapes_with_tris, cname))
									eaPushUnique(&shapes_without_tris, cname);
							}
							SAFE_FREE(vrml_shape.verts);
							vrml_shape.vert_count = vrml_shape.tri_count = 0;
						} else {
							eaPushUnique(&shapes_with_tris, cname);
						}
						result.attributes_with_errors = validateVrmlShape(&vrml_shape);
						simplifyMergeVrmlShapes(&vrml_shape,out_shape); 
						freeVrmlShape(&vrml_shape);
						return result;
					}
					xcase 0:
					case -1:
						printfColor(COLOR_RED|COLOR_BRIGHT,
							"\nWarning: possible bad .wrl file!  Unhandled token: %s\n"
							"Press any key to continue...\n", vrml_token_info.token);
						_getch();
						freeVrmlShape(&vrml_shape);
						return result;
				}
			}
		}
	}

	freeVrmlShape(&vrml_shape);
	return result;
}

static void getShape(VrmlShape *shape, char * name)
{
	int		cmd;

	cmd = getCmd();
	for(;;)
	{
		char *tok = getTok();
		cmd = cmdNum(tok);
		if (cmd == VRML_APPEARANCE)
			getAppearance(shape, name);
		else if (cmd == VRML_GEOMETRY)
		{
			VRML_getGeometryResult geometryProcessResults = getGeometry(shape, name);
			if (geometryProcessResults.attributes_with_errors)
			{
				GMeshAttributeUsage geometryProcessErrorFlags = geometryProcessResults.attributes_with_errors;
				printfColor(COLOR_RED|COLOR_BRIGHT, "Shape \"%s\" has out-of-range attributes like positions or texture coordinates. Check error details.\n", name);
				ErrorDetailsf("Attributes with errors include: %s%s%s%s%s%s%s%s%s%s%s",
					geometryProcessErrorFlags & USE_POSITIONS ? "Position outside interval -32767 to 32767. " : "",
					geometryProcessErrorFlags & USE_POSITIONS2 ? "Position2 outside interval -32767 to 32767. " : "",
					geometryProcessErrorFlags & USE_NORMALS ? "Normals outside interval -255 to 255. " : "",
					geometryProcessErrorFlags & USE_NORMALS2 ? "Normals2 outside interval -255 to 255. " : "",
					geometryProcessErrorFlags & USE_BINORMALS ? "Binormals outside interval -255 to 255. " : "",
					geometryProcessErrorFlags & USE_TANGENTS ? "Tangents outside interval -255 to 255. " : "",
					geometryProcessErrorFlags & USE_TEX1S ? "TexCoord Channel 1 outside interval -32767 to 32767. " : "",
					geometryProcessErrorFlags & USE_TEX2S ? "TexCoord Channel 2 outside interval -32767 to 32767. " : "",
					geometryProcessErrorFlags & USE_BONEWEIGHTS ? "Bone weights outside interval 0 to 1. " : "",
					geometryProcessErrorFlags & USE_COLORS ? "Colors outside interval 0 to 1. " : "",
					geometryProcessErrorFlags & USE_VARCOLORS ? "Material Weight Colors (?) outside interval 0 to 1. " : ""
					);
				ErrorVrmlFilesf("Shape \"%s\" has out-of-range attributes like positions or texture coordinates. Check error details.\n", name);
			}
		}
		else if (cmd == VRML_NODEEND)
			break;
		else
			printfColor(COLOR_RED|COLOR_BRIGHT, "Skipping unknown block: %s (%d)\n", tok, cmd);
	}
}

static void fixName(char *name)
{
char	*s;

	s = strstr(name,"-ROOT");
	if (s)
		*s = 0;
}

static void setNodeMat(Node *node, Vec3 translate, Vec3 pivot, Vec4 rotate, Vec3 scale, Vec4 scaleOrient)
{
	Quat	quat;
	Mat3	rot_m,scale_m;

	if (!node)
		return;

	copyMat3(unitmat,scale_m); 
	copyMat3(unitmat,rot_m); 

	// calculate local pivot
	subVec3(translate, pivot, node->child_translate);

	// rotation
	axisAngleToQuat(&rotate[0],rotate[3],quat);
	quatToMat(quat,rot_m);

	// scale
	//	ignore scaleOrientation for now
	//	axisAngleToQuat(&scaleOrient[0],scaleOrient[3],quat);
	//	quatToMat(quat,scale_m);
	scaleMat3Vec3(scale_m,scale);

	// final matrix
	mulMat3(rot_m,scale_m,node->mat);
	copyVec3(pivot,node->mat[3]);
}


static Node *getNodes(Node *node)
{
	static int node_depth; //not used

	int			cmd, len;
	char		*s, name[1000] = {0};
	Node		*child=0;
	VrmlShape	vrml_shape = {0};

	Vec3 scale = {1,1,1};
	Vec3 translate = {0,0,0}, pivot = {0,0,0}, center = {0,0,0};
	Vec4 rotate;
	Vec4 scaleOrient;

	unitQuat(rotate);
	orientAngle(rotate);

	unitQuat(scaleOrient);
	orientAngle(scaleOrient);

	setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);

	for(;;)
	{
		cmd = getCmd();
		if (!cmd)
			return getTreeRoot();

		switch(cmd)
		{
			case VRML_DEF:
			case VRML_USE:
				strcpy(name,getTok());
			xcase VRML_TRANSFORM:
			{
				bool no_lods = false, no_triopt = false, high_precision_texcoord = false, wind = false, trunk_wind = false, high_detail_high_lod = false, alpha_tri_sort = false, vert_color_sort = false;
				int id_override = 0;

				if (!name[0])
				{
					printfColor(COLOR_BRIGHT|COLOR_RED, "Malformed VRML file detected (within %s)!  Possibly you are trying to export a MAX primitive\n instead of an editable mesh?\n", node?node->name:"root");
					skipBlock();
					continue;
				}

				if(strStartsWith(name, "!!")) {
					printfColor(COLOR_BRIGHT|COLOR_RED, "Malformed name detected (within %s)! Name starts with \"!!\" and will be reduced to zero length.\n", node?node->name:"root");
					skipBlock();
					continue;
				}

				len = strlen(name);
				s = name;
				while (s = strstr(s, "!!"))
				{
					*s = 0;
					s += 2;
					if (s - name >= len)
						break;
					if (strStartsWith(s, "devonly"))
					{
						no_lods = true;
						no_triopt = true;
					}
					else if (strStartsWith(s, "nolods"))
					{
						no_lods = true;
					}
					else if (strStartsWith(s, "notriopt"))
					{
						no_triopt = true;
					}
					else if (strStartsWith(s, "HighPrecisionTexcoord"))
					{
						high_precision_texcoord = true;
					}
					else if (strStartsWith(s, "trunkwind"))
					{
						trunk_wind = true;
					}
					else if (strStartsWith(s, "wind"))
					{
						wind = true;
					}
					else if (strStartsWith(s, "HighDetailLOD"))
					{
						high_detail_high_lod = true;
					}
					else if (strStartsWith(s, "AlphaTriSort"))
					{
						alpha_tri_sort = true;
					}
					else if (strStartsWith(s, "id="))
					{
						sscanf(s, "id=%d", &id_override);
					}
					else if (strStartsWith(s, "VertColorSort"))
					{
						vert_color_sort = true;
					}
				}
				if (treeFindNode(name))
				{
					printfColor(COLOR_RED|COLOR_BRIGHT, "Skipping duplicate: %s (child of %s)\n",name,node?node->name:"root");
					skipBlock();
					continue;
				}
				else
				{
					//Insert new node and init 
					child = treeInsert(node);
					assert(child);
					ZeroStruct(&child->mesh);
					strcpy(child->name,name);
					fixName(child->name);
					copyMat4(unitmat, child->mat);
					child->no_lods = no_lods;
					child->no_tri_optimization = no_triopt;
					child->high_precision_texcoord = high_precision_texcoord;
					child->wind = wind;
					child->trunk_wind = trunk_wind;
					child->high_detail_high_lod = high_detail_high_lod;
					child->alpha_tri_sort = alpha_tri_sort;
					child->vert_color_sort = vert_color_sort;
					child->id_override = id_override;
				}
			}
			xcase VRML_POINTLIGHT:
			case VRML_SPOTLIGHT:
				getLight(name);
			xcase VRML_TRANSLATION:
				getVec3(translate);
				orientPos(translate);
				copyVec3(translate,pivot);
				if (node)
					copyVec3(translate,node->dynPos);
				setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);
			xcase VRML_PIVOT: // Not real VRML, but who cares?
				getVec3(pivot);
				orientPos(pivot);
				setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);
			xcase VRML_ROTATION:
				getVec4(rotate);
				orientAngle(rotate);
				setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);
			xcase VRML_CENTER:
				getVec3(center);
				orientPos(center);
				setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);
			xcase VRML_SCALE:
				getVec3(scale);
				setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);
			xcase VRML_SCALEORIENTATION:
				getVec4(scaleOrient);
				orientAngle(scaleOrient);
				setNodeMat(node, translate, pivot, rotate, scale, scaleOrient);
			xcase VRML_CHILDREN:
				getTok();
			xcase VRML_TIMESENSOR:
				skipBlock();
			xcase VRML_POSINTERP:
				if (strstr(name,"-SCALE-") || !node)
					skipBlock();
				else
					getPosKeys(&node->poskeys);
			xcase VRML_ROTINTERP:
				if (strstr(name,"-SCALE-") || !node)
					skipBlock();
				else
					getRotKeys(&node->rotkeys);
			xcase VRML_SHAPE:
				if (!node)
					skipBlock();
				else
				{
					getShape(&vrml_shape, node->name);
					convertVrmlShapeToGMesh(&node->bones, &node->mesh, &vrml_shape, node->wind);
					eaCopy(&node->mesh_names, &vrml_shape.mesh_names);
					eaDestroy(&vrml_shape.mesh_names);
				}
			xcase VRML_NODESTART:
				node_depth++;
				getNodes(child);
			xcase VRML_NODEEND:
				node_depth--;
				freeVrmlShape(&vrml_shape);
				return getTreeRoot();
			break;
		}
	}

	freeVrmlShape(&vrml_shape);
	return getTreeRoot();
}
	

static char *expandVrml()
{
	// JE: Allocating 1gb here, but only a malloc, which does not bring it into the working set,
	//   so should be the same impact as a small alloc, but much, much cheaper than little reallocs
	int		depth,cmd,last_cmd=-1,idx,maxbytes=500000000,len,blocklen;
	char	*mem=0,*s;
	int		max_mem = 0,i;
	int		*def_idxs = 0;
	int		def_count = 0;
	char	*s2;

	if (maxbytes)
		mem = malloc(maxbytes);
	idx = 0;
	for(;;)
	{
		s = getTok();
		// since this routine removes quotes, we need to fixup the words with spaces so the parser won't get confused.
		// fixing up the words will cause errors later on, but they should at least be understandable
		// don't use spaces in your texture names!
		if (s)
		{
			for(s2=s;*s2;s2++)
				if (*s2 == ' ')
					*s2 = '_';
		}
		cmd = cmdNum(s);
		if (!cmd)
		{
			free(def_idxs);
			def_count = 0;
			return mem;
		}
		ANALYSIS_ASSUME(s);
		len = strlen(s);
		if (idx + len + 2 >= maxbytes)
		{
			maxbytes += MEMCHUNK;
			mem = realloc(mem,maxbytes);
		}
		strcpy_s(mem + idx, maxbytes - idx, s);
		idx += strlen(s);
		mem[idx++] = ' ';
		mem[idx] = 0;
		if (cmd == VRML_DEF)
		{
			def_idxs = realloc(def_idxs,sizeof(def_idxs[0]) * (def_count+1));
			def_idxs[def_count] = idx;
			def_count++;
		}
		if (last_cmd == VRML_USE)
		{
			len = strlen(s);
			for(i=0;i<def_count;i++)
				if (strncmp(mem + def_idxs[i],s,len)==0)
					break;
			if (i >= def_count)
			{
				FatalErrorf("Unmatched USE: %s\n",s);
			}
			depth = 0;
			for(s = mem + def_idxs[i];;s++)
			{
				if (*s == '[' || *s == '{')
					depth++;
				if (*s == ']' || *s == '}')
				{
					depth--;
					if (!depth)
						break;
				}
			}
			blocklen = s - (mem + def_idxs[i]) + 1 - len;
			if (idx + blocklen + 2 >= maxbytes)
			{
				maxbytes += MAX(MEMCHUNK, blocklen);
				mem = realloc(mem,maxbytes);
			}
			memcpy(mem + idx,mem + def_idxs[i] + len,blocklen);
			idx += blocklen;
			mem[idx] = 0;
		}
		last_cmd = cmd;
	}
}

static char *readSingleFile(const char *name, int *version_num, int *filelen)
{
	char *vrml_text = fileAlloc(name, filelen);
	const char *s;
	
	*version_num = 0;

	if (!vrml_text)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "\n\nUnable to read vrml file \"%s\"", name);
		*filelen = 0;
		return NULL;
	}

	s = strstr(vrml_text, VERSION_TEXT);
	if (s)
	{
		s += strlen(VERSION_TEXT);
		*version_num = atoi(s);
	}

	if (*version_num < VERSION_MIN || *version_num > VERSION_MAX)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "\n\nUnable to read vrml file \"%s\"", name);
		printf("\n File appears to be exported with the wrong version of the VRML exporter.  Looking for version %d through %d, found version %d.", VERSION_MIN, VERSION_MAX, *version_num);
		printf("\n Please install the latest exporter: C:\\Night\\tools\\art\\3DSMax\\InstallVrmlExporterMax2009.bat");
		printf("\n Make sure you close 3D Studio Max first!\n");
		free(vrml_text);
		*filelen = 0;
		return NULL;
	}

	if (*version_num < VERSION_CUR && !g_test && !g_force_rebuild)
	{
		printfColor(COLOR_BRIGHT|COLOR_RED, "\n\nA new version of the vrml exporter is available!");
		printf("\n Please install the latest exporter: C:\\Night\\tools\\art\\3DSMax\\InstallVrmlExporterMax2009.bat");
		printf("\n Make sure you close 3D Studio Max first!\n");
	}

	return vrml_text;
}

/*Only entry point for vrml.c 
Reads in a Vrml file, expands it, and converts it into a tree of Nodes, each
of which corresponds to a "DEF whatever Transform{}, Shape{}" in the VRML file
(Note: the tree and lights are global.) Returns the 
root of the tree.
*/
Node *readVrmlFiles(const char **names)
{
	Node *root;
	char *vrml_text = NULL, *vrml_exp_text;
	int i, base_version_num = 0;
	int version_num = 0;
	int vrml_text_len=1;
	const char utf8Sig[3] = { 0xef, 0xbb, 0xbf };

	// so far no errors, so clear baddata...
	baddata = 0;

	for (i = 0; i < eaSize(&names); ++i)
	{
		char *file_text;
		int file_len;
		int fileOffset = 0;

		file_text = readSingleFile(names[i], &version_num, &file_len);
		if (!file_text)
		{
			estrDestroy(&vrml_text);
			return NULL;
		}
		if (base_version_num && base_version_num != version_num)
		{
			printfColor(COLOR_BRIGHT|COLOR_RED, "\n\nUnable to append vrml file \"%s\"", names[i]);
			printf("\n File to be appended is a different version (%d) than the base file (%d).\n", version_num, base_version_num);
			printf(" You must re-export whichever file(s) are older.\n");
			estrDestroy(&vrml_text);
			return NULL;
		}

		if (strncmp(file_text,utf8Sig,3) == 0) {
			fileOffset = 3;
			if (!vrml_text) {
				vrml_text = malloc(sizeof(char));
				vrml_text[0] = '\0';
				vrml_text_len = 1;
			}
		}

		if (!vrml_text)
		{	// This is only run if vrml_text hasn't been set which means the first file being processed cannot be a utf8 file.
			vrml_text = file_text;
			vrml_text_len = file_len + 1; // fileAlloc includes a null character at the end
		} else {
			vrml_text = realloc(vrml_text, vrml_text_len + file_len - fileOffset);
			memcpy(vrml_text + vrml_text_len - 1, file_text + fileOffset, file_len - fileOffset);
			vrml_text_len += file_len - fileOffset;
			vrml_text[vrml_text_len-1] = '\0';
			free(file_text);
		}
		base_version_num = version_num;
	}

	colorsize = (version_num >= 12) ? 3 : 4;

	setVrmlText(vrml_text);
	vrml_exp_text = expandVrml();
	free(vrml_text);
	setVrmlText(vrml_exp_text);

	treeFree();
	setVRMLFiles(names);
	root = getNodes(0);
	setVRMLFiles(NULL);
	printShapeWarnings();
	free(vrml_exp_text);

	if (baddata)
		return NULL;

	return root;
}

