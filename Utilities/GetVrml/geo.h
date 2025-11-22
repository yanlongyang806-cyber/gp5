#ifndef _GEO_H
#define _GEO_H

#include "GenericMesh.h"
#include "tree.h"

typedef struct DynBaseSkeleton DynBaseSkeleton;


typedef enum GetVrmlLibraryType
{
	LIB_UNSPECIFIED,
	LIB_OBJ,
	LIB_CHAR,
	LIB_ANIM,
	LIB_TEX,
} GetVrmlLibraryType;

#define DONT_POOL_VERTS				0
#define POOL_VERTS					1
#define POOL_VERTS_NOSTS			2
#define POOL_VERTS_NOSTS_NONORMS	3
#define POOL_VERTS_NONORMS			4

#define SHALLOW_COPY	0
#define DEEP_COPY		1


extern const char **tex_names;

typedef struct ModelTexName
{
	const char *modelName; // string pooled
	const char *texname; // Pointer into tex_names
} ModelTexName;

extern ModelTexName **model_tex_names;

void texNameClear(int addwhite);
int texNameAdd(const char *pcName, const char *modelUsingIt);

void geoAddFile(const char *name, const char **vrml_fnames, const char *geo_fname, const char *group_fname, 
				GetVrmlLibraryType targetlibrary, bool do_unwrapping, bool is_core, 
				/*const char *big_fname, */const char *deps_fname, const char *root_path, const DynBaseSkeleton *base_skeleton);
void nodeUvunwrap(Node *node);

char** enumObjLibFiles( const char* name, const char* src_fname, const char *dst_fname, const char* root_path );
char** enumCharLibFiles( const char* name, const char* src_fname, const char *dst_fname, const char* root_path );

#endif
