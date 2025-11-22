#if NV_MESH_MENDER
#define RT_ALLOW_VBO
#include "NVMeshMender.h"
#include "model.h"
#include "rt_model_cache.h"

#include "mathutil.h"
#include <stdio.h>
#include "GenericMesh.h"
#ifndef CLIENT
#include "tree.h"
#endif

#undef printf
#undef realloc

#pragma comment(lib, "C:/dxsdk/lib/d3dx9.lib")

extern "C" {

float a=0.11;

bool MeshMenderMend(Model *model)
{
	MeshMender mender;

	std::vector< MeshMender::Vertex > theVerts;
	std::vector< unsigned int > theIndices;
	std::vector< unsigned int > mappingNewToOld;

	//fill up the vectors with your mesh's data
	for(int i = 0; i < model->vert_count; ++i)
	{
		MeshMender::Vertex v;
		v.pos.x = model->vbo->verts[i][0];
		v.pos.y = model->vbo->verts[i][1];
		v.pos.z = model->vbo->verts[i][2];
		v.s = model->vbo->sts[i][0];
		v.t = model->vbo->sts[i][1];
		v.normal.x = model->vbo->norms[i][0];
		v.normal.y = model->vbo->norms[i][1];
		v.normal.z = model->vbo->norms[i][2];
		if (v.normal.x ==0 && v.normal.y == 0 && v.normal.z == 0)
			return false;
		//meshmender will computer normals, tangents, and binormals, no need to fill those in.
		//however, if you do not have meshmender compute the normals, you _must_ pass in valid
		//normals to meshmender
		theVerts.push_back(v);
	}

	for(int  i= 0 ; i< model->tri_count; ++i)
	{
		theIndices.push_back(model->vbo->tris[i*3+0]);
		theIndices.push_back(model->vbo->tris[i*3+1]);
		theIndices.push_back(model->vbo->tris[i*3+2]);
	}

	//pass it in to the mender to do it's stuff
	bool ret = mender.Mend( theVerts,  theIndices, mappingNewToOld,
		a,
		a,
		a,
		1.0f,
		MeshMender::DONT_CALCULATE_NORMALS,
		MeshMender::DONT_RESPECT_SPLITS,
		MeshMender::DONT_FIX_CYLINDRICAL); // try FIX_CYLINDRICAL

	// just drop some verts if there were new ones
	int numverts = MIN(model->vert_count, (int)theVerts.size());
	for (int i=0; i<numverts; i++) 
	{
		MeshMender::Vertex v = theVerts[i];
		model->vbo->verts[i][0] = v.pos.x;
		model->vbo->verts[i][1] = v.pos.y;
		model->vbo->verts[i][2] = v.pos.z;
		model->vbo->sts[i][0] = v.s;
		model->vbo->sts[i][1] = v.t;
		model->vbo->norms[i][0] = v.normal.x;
		model->vbo->norms[i][1] = v.normal.y;
		model->vbo->norms[i][2] = v.normal.z;
		model->vbo->binormals[i][0] = v.binormal.x;
		model->vbo->binormals[i][1] = v.binormal.y;
		model->vbo->binormals[i][2] = v.binormal.z;
		model->vbo->tangents[i][0] = v.tangent.x;
		model->vbo->tangents[i][1] = v.tangent.y;
		model->vbo->tangents[i][2] = v.tangent.z;
	}

	assert(model->tri_count*3 == (int)theIndices.size());
	int numinds = MIN(model->tri_count*3, (int)theIndices.size());
	for (int i=0; i<numinds; i++) 
	{
		model->vbo->tris[i] = MIN((int)theIndices[i], numverts-1);
	}

	printf("Added %d verts and %d tris (%d orig verts) Model: %s\n", (theVerts.size() - model->vert_count), (theIndices.size()/3 - model->tri_count), model->vert_count, model->name);

	return ret;
}

#ifndef CLIENT

bool MeshMenderMendGMesh(GMesh *mesh, char *name)
{

	MeshMender mender;
	int orig_vert_count = mesh->vert_count;
	int orig_tri_count = mesh->tri_count;

	std::vector< MeshMender::Vertex > theVerts;
	std::vector< unsigned int > theIndices;
	std::vector< unsigned int > mappingNewToOld;

	//fill up the vectors with your mesh's data
	for(int i = 0; i < mesh->vert_count; ++i)
	{
		MeshMender::Vertex v;
		v.pos.x = mesh->positions[i][0];
		v.pos.y = mesh->positions[i][1];
		v.pos.z = mesh->positions[i][2];
		v.s = mesh->tex1s[i][0];
		v.t = mesh->tex1s[i][1];
		v.normal.x = mesh->normals[i][0];
		v.normal.y = mesh->normals[i][1];
		v.normal.z = mesh->normals[i][2];
		if (v.normal.x ==0 && v.normal.y == 0 && v.normal.z == 0)
			return false;
		//meshmender will computer normals, tangents, and binormals, no need to fill those in.
		//however, if you do not have meshmender compute the normals, you _must_ pass in valid
		//normals to meshmender
		theVerts.push_back(v);
	}

	for(int  i= 0 ; i< mesh->tri_count; ++i)
	{
		theIndices.push_back(mesh->tris[i].idx[0]);
		theIndices.push_back(mesh->tris[i].idx[1]);
		theIndices.push_back(mesh->tris[i].idx[2]);
	}

	//pass it in to the mender to do it's stuff
	bool ret = mender.Mend( theVerts,  theIndices, mappingNewToOld,
		a,
		a,
		a,
		1.0f,
		MeshMender::DONT_CALCULATE_NORMALS,
		MeshMender::DONT_RESPECT_SPLITS,
		MeshMender::DONT_FIX_CYLINDRICAL); // try FIX_CYLINDRICAL

	// Do something with duplicated verts
	// this should really be added into MeshMender's logic if we wanted to use this
	// Also, the logic of how to make basises symetrical needs to be smarter
	if (1)
	{
		for (int i=0; i<mesh->vert_count; i++) 
		{
			MeshMender::Vertex &v = theVerts[i];
			for (int j=mesh->vert_count; j<(int)theVerts.size(); j++) if (i!=j) {
				if (mappingNewToOld[j]==i) {
					// split into two, if this appears to be a texture mirror, make basises symmetrical
					MeshMender::Vertex &v2 = theVerts[j];
					if (D3DXVec3Dot(&v.tangent, &v2.tangent) < 0.5) {
						v2.tangent = -v.tangent;
						v2.binormal = -v.binormal;
					} else if (D3DXVec3Dot(&v.tangent, &v2.tangent) > 0.5) {
						// Same vert!  re-merge
						v2.tangent = v.tangent;
						v2.binormal = v.binormal;
					}
				}
			}
		}
//		theVerts[104].tangent = theVerts[9].tangent = theVerts[101].binormal;
//		theVerts[104].binormal = theVerts[9].binormal = theVerts[16].tangent;
	}

//	if ((int)theVerts.size() == orig_vert_count)
//		return false;

	gmeshSetVertCount(mesh, theVerts.size());

//	FILE *fout = fopen("c:\\bump.txt", "wb");
	for (int i=0; i<mesh->vert_count; i++) 
	{
		MeshMender::Vertex v = theVerts[i];
		mesh->positions[i][0] = v.pos.x;
		mesh->positions[i][1] = v.pos.y;
		mesh->positions[i][2] = v.pos.z;
		mesh->tex1s[i][0] = v.s;
		mesh->tex1s[i][1] = v.t;
		mesh->normals[i][0] = v.normal.x;
		mesh->normals[i][1] = v.normal.y;
		mesh->normals[i][2] = v.normal.z;
		if (i<orig_vert_count)
			assert(mappingNewToOld[i]==i);

// Not saved in GetVrml! // add these as debug data to verify we're recreating them the same
//		model->vbo->binormals[i][0] = v.binormal.x;
//		model->vbo->binormals[i][1] = v.binormal.y;
//		model->vbo->binormals[i][2] = v.binormal.z;
//		model->vbo->tangents[i][0] = v.tangent.x;
//		model->vbo->tangents[i][1] = v.tangent.y;
//		model->vbo->tangents[i][2] = v.tangent.z;

//		fwrite(&v.binormal.x, sizeof(v.binormal.x), 1, fout);
//		fwrite(&v.binormal.y, sizeof(v.binormal.y), 1, fout);
//		fwrite(&v.binormal.z, sizeof(v.binormal.z), 1, fout);
//		fwrite(&v.tangent.x, sizeof(v.tangent.x), 1, fout);
//		fwrite(&v.tangent.y, sizeof(v.tangent.y), 1, fout);
//		fwrite(&v.tangent.z, sizeof(v.tangent.z), 1, fout);
//		fwrite(&v.normal.x, sizeof(v.normal.x), 1, fout);
//		printf("bin[%d] = {%1.6f,%1.6f,%1.6f}\n", i, v.binormal.x, v.binormal.y, v.binormal.z);
//		printf("tan[%d] = {%1.6f,%1.6f,%1.6f}\n", i, v.tangent.x, v.tangent.y, v.tangent.z);
	}
//	fclose(fout);

	assert(mesh->tri_count*3 == (int)theIndices.size());
	int numinds = MIN(mesh->tri_count*3, (int)theIndices.size());
	for (int i=0; i<numinds/3; i++) 
	{
		for (int j=0; j<3; j++) {
			mesh->tris[i].idx[j] = (int)theIndices[i*3+j];
			assert(mesh->tris[i].idx[j] < mesh->vert_count);
			// tex_id stays the same (this is the same triangle, just posibly new instances of verts)
		}
	}

	printf("MeshMender: Added %d verts and %d tris (%d orig verts) Model: %s\n", (theVerts.size() - orig_vert_count), (theIndices.size()/3 - orig_tri_count), orig_vert_count, name);

	return ret;
}


#endif

};
#endif
