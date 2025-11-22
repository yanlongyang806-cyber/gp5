extern "C" {
#include "stdtypes.h"
#include "mathutil.h"
#include "file.h"
};

#include "wininclude.h"

#include "../../3rdparty/Tootle/include/tootlelib.h"

#pragma comment(lib, "../../3rdparty/Tootle/lib/TootleDLL.lib") // Linking to the debug version because the release version crashes on some model inputs, unable to reproduce outside of our app, so probably the debug CRT conflicting with the release one
#pragma comment(lib, "../../3rdparty/DirectX/lib/d3dx9.lib")
#pragma comment(lib, "../../3rdparty/DirectX/lib/d3d9.lib")

extern "C" {

static Vec3 viewpoints[26];

static bool displayTootleError(TootleResult result)
{
	switch (result)
	{
		case TOOTLE_INVALID_ARGS:
			printf("Invalid arguments passed to Tootle.\n");
			break;
		case TOOTLE_OUT_OF_MEMORY:
			printf("Tootle ran out of memory.\n");
			break;
		case TOOTLE_3D_API_ERROR:
			printf("Tootle could not initialize Direct3D.\n");
			break;
		case TOOTLE_INTERNAL_ERROR:
			printf("Tootle had an internal error.\n");
			break;
		case TOOTLE_NOT_INITIALIZED:
			printf("Tootle was not initialized.\n");
			break;
		default:
			return true;
	}

	return false;
}

static void sphereCoord(Vec3 coord, F32 radius, F32 a, F32 b)
{
	F32 sina, cosa;
	F32 sinb, cosb;
	sincosf(a, &sina, &cosa);
	sincosf(b, &sinb, &cosb);
	setVec3(coord, radius * cosa * cosb, radius * sinb, radius * sina * cosb);
}

void tootleOptimize(U32 *indices, int tri_count, F32 *vertices, U32 vert_count)
{
	static bool initialized = false;
	TootleResult result;
	F32 cache_in, avg_overdraw_in, max_overdraw_in;
	F32 cache_out, avg_overdraw_out, max_overdraw_out;
	U32 *clusters;

	if (!initialized)
	{
		int i;

		HANDLE hDLL;
		hDLL = LoadLibrary(L"Tootle_d.dll");
		if (!hDLL)
			hDLL = LoadLibrary(L"../../3rdparty/Tootle/bin/Tootle.dll");
		assertmsg(hDLL, "Error finding and loading Tootle.dll");
		result = TootleInit();
		if (!displayTootleError(result))
			return;
		initialized = true;
		setVec3(viewpoints[0], 0, 1, 0);
		setVec3(viewpoints[1], 0, -1, 0);
		for (i = 0; i < 8; ++i)
			sphereCoord(viewpoints[2+i], 1, RAD(i * 45.f), RAD(45.f));
		for (i = 0; i < 8; ++i)
			sphereCoord(viewpoints[10+i], 1, RAD(i * 45.f), RAD(0.f));
		for (i = 0; i < 8; ++i)
			sphereCoord(viewpoints[18+i], 1, RAD(i * 45.f), RAD(-45.f));
	}

	if (0)
	{
		FILE *f;
		int i;

		f = (FILE*)fopen("C:/tootle_indices.txt", "wt");
		if (f)
		{
			for (i = 0; i < tri_count; ++i)
				fprintf(f, "%d, %d, %d,\n", indices[i*3+0], indices[i*3+1], indices[i*3+2]);
			fclose(f);
		}

		f = (FILE*)fopen("C:/tootle_vertices.txt", "wt");
		if (f)
		{
			for (i = 0; i < (int)vert_count; ++i)
				fprintf(f, "%f, %f, %f,\n", vertices[i*3+0], vertices[i*3+1], vertices[i*3+2]);
			fclose(f);
		}

		f = (FILE*)fopen("C:/tootle.obj", "wt");
		if (f)
		{
			for (i = 0; i < (int)vert_count; ++i)
				fprintf(f, "v %f %f %f\n", vertices[i*3+0], vertices[i*3+1], vertices[i*3+2]);
			for (i = 0; i < tri_count; ++i)
				fprintf(f, "f %d %d %d\n", 1+indices[i*3+0], 1+indices[i*3+1], 1+indices[i*3+2]);
			fclose(f);
		}

	}

	clusters = (U32*)calloc(tri_count+1, sizeof(U32));

	// measure pre-optimization vertex cache efficiency
	result = TootleMeasureCacheEfficiency(indices, tri_count, TOOTLE_DEFAULT_VCACHE_SIZE, &cache_in);
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

	// measure pre-optimization overdraw
	result = TootleMeasureOverdraw(	vertices, indices, vert_count, tri_count, sizeof(Vec3), 
									NULL, 0, TOOTLE_CCW, &avg_overdraw_in, &max_overdraw_in);
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

	// cluster triangles (rearranges indices)
	result = TootleClusterMesh(	vertices, indices, vert_count, tri_count, sizeof(Vec3), 
								0, indices, clusters, NULL );
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

	// optimize each cluster for the vertex cache (rearranges indices)
	result = TootleVCacheClusters(	indices, tri_count, vert_count, TOOTLE_DEFAULT_VCACHE_SIZE, 
									clusters, indices, NULL );
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

	// optimize overdraw
	result = TootleOptimizeOverdraw(vertices, indices, vert_count, tri_count, sizeof(Vec3), 
									&viewpoints[0][0], ARRAY_SIZE(viewpoints), TOOTLE_CCW, clusters, indices, NULL);
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

	// measure post-optimization vertex cache efficiency
	result = TootleMeasureCacheEfficiency(indices, tri_count, TOOTLE_DEFAULT_VCACHE_SIZE, &cache_out);
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

	// measure post-optimization overdraw
	result = TootleMeasureOverdraw(	vertices, indices, vert_count, tri_count, sizeof(Vec3), 
									NULL, 0, TOOTLE_CCW, &avg_overdraw_out, &max_overdraw_out);
	if (!displayTootleError(result))
	{
		free(clusters);
		return;
	}

// 	printf("\nCache coherency in:  %f   Overdraw in:  %f, %f\n", cache_in, avg_overdraw_in, max_overdraw_in);
// 	printf("Cache coherency out: %f   Overdraw out: %f, %f\n", cache_out, avg_overdraw_out, max_overdraw_out);

	free(clusters);
}

};
