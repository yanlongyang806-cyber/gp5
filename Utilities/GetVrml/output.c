#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <io.h>
#include "windefinclude.h"
#include "zlib.h"
#include "stdtypes.h"
#include "mathutil.h"
#include "utils.h"

#include "error.h"
#include "earray.h"
#include "endian.h"
#include "gridpoly.h"
#include "file.h"
#include "EString.h"
#include "gimmeDllWrapper.h"
#include "StringCache.h"
#include "timing.h"

#include "textparser.h"
#include "output.h" 
#include "geo.h"
#include "tree.h"
#include "StashTable.h"
#include "referencesystem.h"

#include "wlModel.h"
#include "../wlModelLoad.h"
#include "../wlModelBinning.h"

#include "AutoGen/output_c_ast.h"

extern ParseTable parse_ModelHeader[];
#define TYPE_parse_ModelHeader ModelHeader
extern ParseTable parse_AltPivot[];
#define TYPE_parse_AltPivot AltPivot

#define NUMCELLS_CUBE		8

typedef struct PackDataDisk
{
	int		packsize;
	U32		unpacksize;
	U32		data_offset;
} PackDataDisk;

static void outputData(const char *little_fname, const char *fname_deps);
static void zipDeltas(PackDataDisk *pack_little, void *data, int stride, int vert_count, F32 float_scale);

//////////////////////////////////////////////////////////////////////////

typedef struct
{
	char	*name;
	U8		*data;
	int		used;
	int		allocCount;
} MemBlock;

enum
{
	MEM_STRUCTDATA = 0,
	MEM_TEXNAMES,
	MEM_TEXIDX,
	MEM_MODELS,
	MEM_MODELHEADERS,
	MEM_OBJNAMES,
	MEM_PACKED,
	MEM_SCRATCH,
	MEM_SCRATCH2,
	MEM_HEADER,
};

static int		anim_header_count;

MemBlock mem_blocks[] =
{
	// little endian
	{ "STRUCTDATA"},
	{ "TEXNAMES"},
	{ "TEXIDX"},
	{ "MODELS"},
	{ "MODELHEADERS"},
	{ "OBJNAMES"},
	{ "PACKED"},
	{ "SCRATCH"},
	{ "SCRATCH2"},
	{ "HEADER"},
};

__forceinline static int getNextBlockMemRef(int block_num)
{
	return mem_blocks[block_num].used;
}

static void *dereferenceBlockMem(int mem_block, int offset)
{
	return mem_blocks[mem_block].data + offset;
}

/// Incrementally allocate AMOUNT bytes of memory from the block named
/// by BLOCK_NUM, but do not initialize it to anything.
///
/// SEE ALSO: allocBlockMem
static void *mallocBlockMem(int block_num,int amount)
{
	void	*ptr;
	MemBlock	*mblock;

	mblock = &mem_blocks[block_num];
	if (mblock->data) {
		mblock->data = realloc(mblock->data, mblock->used+amount+4);
		memset(mblock->data + mblock->used, 0, amount+4);
	} else {
		assert(!mblock->allocCount && !mblock->used);
		mblock->data = calloc(amount+4,1);
	}
	ptr = &mblock->data[mblock->used];
	mblock->used += amount;
	mblock->allocCount++;
	return ptr;
}

/// Incrementally allocate AMOUNT bytes of memory from the block named
/// by BLOCK_NUM, and initialize that block to 0.
static void *allocBlockMem(int block_num,int amount)
{
	void	*ptr;

	ptr = mallocBlockMem(block_num,amount);
	memset(ptr,0,amount);
	return ptr;
}

__forceinline static void writeFloatBlockMem(int block_num, float f)
{
	float *fmem = allocBlockMem(block_num, sizeof(f));
	*fmem = f;
}

__forceinline static void writeIntBlockMem(int block_num, int i)
{
	int *imem = allocBlockMem(block_num, sizeof(i));
	*imem = i;
}

__forceinline static void writeIntBlockMem2(int block_num, int i_little/*, int i_big*/)
{
	int *imem = allocBlockMem(block_num, sizeof(i_little));
	*imem = i_little;
}

__forceinline static void writeU16BlockMem(int block_num, U16 i)
{
	U16 *imem = allocBlockMem(block_num, sizeof(i));
	*imem = i;
}

__forceinline static void writeStringBlockMem(int block_num, char *str)
{
	if (str)
	{
		int slen = strlen(str) + 1;
		char *smem = allocBlockMem(block_num, slen);
		memcpy(smem, str, slen);
	}
	else
	{
		char *smem = allocBlockMem(block_num, 1);
		*smem = 0;
	}
}

__forceinline static void appendBlockToHeader(int blocknum)
{
	void	*ptr;

	ptr	= allocBlockMem(MEM_HEADER,mem_blocks[blocknum].used);
	memcpy(ptr,mem_blocks[blocknum].data,mem_blocks[blocknum].used);
}

//############### End Manage Memeory Blocks ############################################

//############### pack All Nodes and helper functions (make own file?) #####################

static int pack_hist[4];

static F32 quantF32(F32 val,F32 float_scale,F32 inv_float_scale)
{
	int		ival;
	F32		outval;

	ival = val * float_scale;
	ival &= ~1;
	outval = ival * inv_float_scale;
	return outval;
}

static U8 *compressDeltas(void *data,int *length,int stride,int count,PackType pack_type,F32 float_scale)
{
	static U32	*bits;
	static U8	*bytes;
	static int	max_bits,max_bytes;
	static int	byte_count[] = {0,1,2,4};
	int		i,j,k,t,val8,val16,val32,iDelta,val,code,cur_byte=0,cur_bit=0,bit_bytes;
	int		*iPtr = data;
	U16		*siPtr = data;
	F32		*fPtr = data,fDelta=0,inv_float_scale = 1;
	Vec4	fLast = {0,0,0,0};
	int		iLast[4] = {0,0,0,0};
	U8		*packed;

	*length = 0;
	if (!data || !count)
		return 0;
	if (float_scale)
		inv_float_scale = 1.f / float_scale;
	else
	{
		assert(pack_type == PACK_U32);
		float_scale = 1.0f;
	}
	bits = calloc((2 * count * stride + 7)/8,1);
	bytes = calloc(count * stride * 4 + 1,1); // Add 1 for the float_scale!

	bytes[cur_byte++] = log2((int)float_scale);
	for(i=0;i<count;i++)
	{
		for(j=0;j<stride;j++)
		{
			if (pack_type == PACK_F32)
			{
				fDelta = quantF32(*fPtr++,float_scale,inv_float_scale) - fLast[j];
				val8 = fDelta * float_scale + 0x7f;
				val16 = fDelta * float_scale + 0x7fff;
				val32 = *((int *)&fDelta);
			}
			else
			{
				if (pack_type == PACK_U32)
					t = *iPtr++;
				else
					t = *siPtr++;
				iDelta = t - iLast[j] - 1;
				iLast[j] = t;
				val8 = iDelta + 0x7f;
				val16 = iDelta + 0x7fff;
				val32 = iDelta;
			}
			if (val8 == 0x7f)
			{
				code	= 0;
			}
			else if ((val8 & ~0xff) == 0)
			{
				code	= 1;
				val		= val8;
				fLast[j]= (val8 - 0x7f) * 1.f/float_scale + fLast[j];
			}
			else if ((val16 & ~0xffff) == 0)
			{
				code	= 2;
				val		= val16;
				fLast[j]= (val16 - 0x7fff) * 1.f/float_scale + fLast[j];
			}
			else
			{
				code	= 3;
				val		= val32;
				fLast[j]= fDelta + fLast[j];
			}
			bits[cur_bit >> 5] |= code << (cur_bit & 31);
			for(k=0;k<byte_count[code];k++)
				bytes[cur_byte++] = (val >> k*8) & 255;
			cur_bit+=2;
			pack_hist[code]++;
		}
	}

	bit_bytes = (cur_bit+7)/8;
	packed = malloc(bit_bytes + cur_byte);
	memcpy(packed,bits,bit_bytes);
	memcpy(packed+bit_bytes,bytes,cur_byte);
	free(bits);
	free(bytes);
	*length = bit_bytes + cur_byte;
	//uncompressDeltas(data,packed,stride,count,pack_type,float_scale);
	return packed;
}

static void zipBlock(PackDataDisk *pack_little,void *data,int len)
{
	int		ziplen;
	U8		*zip_buf;

	ziplen	= len*2 + 128;
	zip_buf	= malloc(ziplen);
	compress((U8 *)zip_buf,&ziplen,data,len);
	
	if (pack_little)
		pack_little->data_offset		= getNextBlockMemRef(MEM_PACKED);

	if (ziplen*5 <= len*4) // ziplen <= len * 0.8
	{
		if (pack_little)
		{
			U8 *packed = allocBlockMem(MEM_PACKED,ziplen);
			memcpy(packed,zip_buf,ziplen);
		}
	}
	else
	{
		if (pack_little)
		{
			U8 *packed = allocBlockMem(MEM_PACKED,len);
			memcpy(packed,data,len);
		}

		ziplen = 0;
	}
	free(zip_buf);
	free(data);

	if (pack_little)
	{
		pack_little->packsize	= ziplen;
		pack_little->unpacksize	= len;
	}
}

static void packTexIdxs(int *texoffset_little, GTriIdx *tris, int count, int *tex_count_little)
{
	int		last_id=-1,i,numtex=0,last_i=0;
	int		old_count = count;

	if (!tris)
	{
		*texoffset_little = 0;
		return;
	}

	if (!count)
	{
		// Pack single dummy texidx to stop game from crashing
		numtex=1;
		count=1;
	} else {
		// Count unique IDs
		for(i=0;i<count;i++)
		{
			if (tris[i].tex_id != last_id)
			{
				last_id = tris[i].tex_id;
				numtex++;
			}
		}
	}

	// Alloc and fill in data
	last_id = -1;
	last_i = 0;
	numtex=0;
	*texoffset_little = getNextBlockMemRef(MEM_TEXIDX);
	for(i=0;i<old_count;i++)
	{
		if (tris[i].tex_id != last_id)
		{
			if (numtex)
				writeU16BlockMem(MEM_TEXIDX, i - last_i); // TexID.count
			writeU16BlockMem(MEM_TEXIDX, tris[i].tex_id); // TexID.id

			last_i = i;
			last_id = tris[i].tex_id;
			numtex++;
		}
	}

	if (!old_count)
	{
		writeU16BlockMem(MEM_TEXIDX, 0); // TexID.id
		writeU16BlockMem(MEM_TEXIDX, 0); // TexID.count
		numtex = 1;
	}
	else
	{
		writeU16BlockMem(MEM_TEXIDX, i - last_i); // TexID.count
	}

	*tex_count_little = numtex;
}

static void packSkin(PackDataDisk *pack_weights_little, PackDataDisk *pack_matidxs_little/*, PackDataDisk *pack_weights_big, PackDataDisk *pack_matidxs_big*/, GMesh *mesh, BoneData *bones)
{ 
	if (eaSize(&bones->boneNames) > 0)
	{
		int i;
		U8		*u8Weights,*u8Matidxs;

		u8Weights = malloc(mesh->vert_count*4);
		u8Matidxs = malloc(mesh->vert_count*4);

		for(i=0;i<mesh->vert_count;i++)
		{
            assert( 0 <= mesh->boneweights[i][0] && mesh->boneweights[i][0] <= 1
                    && 0 <= mesh->boneweights[i][1] && mesh->boneweights[i][1] <= 1
                    && 0 <= mesh->boneweights[i][2] && mesh->boneweights[i][2] <= 1
                    && 0 <= mesh->boneweights[i][3] && mesh->boneweights[i][3] <= 1 );
            assert( 0 <= mesh->bonemats[i][0] && mesh->bonemats[i][0] < 256
                    && 0 <= mesh->bonemats[i][1] && mesh->bonemats[i][1] < 256
                    && 0 <= mesh->bonemats[i][2] && mesh->bonemats[i][2] < 256
                    && 0 <= mesh->bonemats[i][3] && mesh->bonemats[i][3] < 256 );
			assert(eaSize(&bones->boneNames) > 1 || mesh->boneweights[i][1] == 0);
			assert(eaSize(&bones->boneNames) > 2 || mesh->boneweights[i][2] == 0);
			assert(eaSize(&bones->boneNames) > 3 || mesh->boneweights[i][3] == 0);
            
			u8Weights[i*4+0] = mesh->boneweights[i][0] * 255;
			u8Weights[i*4+1] = mesh->boneweights[i][1] * 255;
			u8Weights[i*4+2] = mesh->boneweights[i][2] * 255;
			u8Weights[i*4+3] = mesh->boneweights[i][3] * 255;
			u8Matidxs[i*4+0] = mesh->bonemats[i][0];
			u8Matidxs[i*4+1] = mesh->bonemats[i][1];
			u8Matidxs[i*4+2] = mesh->bonemats[i][2];
			u8Matidxs[i*4+3] = mesh->bonemats[i][3];
		}
		zipBlock(pack_weights_little,u8Weights,mesh->vert_count*4);
		zipBlock(pack_matidxs_little,u8Matidxs,mesh->vert_count*4);
	}
}

static U8 * checkForDuplicates(U8 * newdata, int newdata_amt, U8 * olddata, int olddata_amt)
{
	int i;
	if(newdata_amt == 0 || olddata_amt == 0)
		return 0;

	//i+2 instead? (size of the short we use as compressed anim data?
	for(i = 0 ; i <= olddata_amt - newdata_amt ; i++)
	{
		if(memcmp(&(newdata[0]), &(olddata[i]), newdata_amt) == 0)
			return &(olddata[i]);
	}
	return 0;

}
	
static void packTris(PackDataDisk *pack_little, GTriIdx *tris, int tri_count)
{
	int		i,j;
	int		*mem,delta_tri_len;
	void	*delta_tris;

	if (!tris || !tri_count)
		return;
	mem = malloc(sizeof(int) * tri_count * 3);
	for (i = 0; i < tri_count; i++)
	{
		for (j = 0; j < 3; j++)
			mem[i*3 + j] = tris[i].idx[j];
	}
	delta_tris	= compressDeltas(mem,&delta_tri_len,3,tri_count,PACK_U32,0);
	zipBlock(pack_little,delta_tris,delta_tri_len);
	free(mem);
}

static void zipDeltas(PackDataDisk *pack_little, void *data, int stride, int vert_count, F32 float_scale)
{
	void	*deltas;
	int		delta_len;

	if (!data)
		return;

	deltas	= compressDeltas(data,&delta_len,stride,vert_count,PACK_F32,float_scale);
	zipBlock(pack_little,deltas,delta_len);
}

static void dynAddInts(void **data, int *count, int *max_count, int num_ints, void *ints, int endian_swap)
{
	int *ptr = dynArrayAdd(*data, 1, *count, *max_count, sizeof(int) * num_ints);
	memcpy(ptr, ints, sizeof(int) * num_ints);

	if (endian_swap)
	{
		int i;
		for (i = 0; i < num_ints; i++)
			ptr[i] = endianSwapU32(ptr[i]);
	}
}

static void dynAddData(void **data, int *count, int *max_count, int datasize, void *newdata)
{
	void *ptr = dynArrayAdd(*data, 1, *count, *max_count, datasize);
	memcpy(ptr, newdata, datasize);
}

static void packReductions(GMeshReductions *reductions, PackDataDisk *pack_little)
{
	void	*data_little=0;
	int		count_little=0, max_count_little=0;
	void	*deltas;
	int		delta_len;

#define dynAddIntsLittle(num_ints, ints) dynAddInts(&data_little, &count_little, &max_count_little, num_ints, ints, 0)
#define dynAddIntsBoth(num_ints, ints) dynAddIntsLittle(num_ints, ints)
#define dynAddDataLittle(datasize, newdata) dynAddData(&data_little, &count_little, &max_count_little, datasize, newdata)

	dynAddIntsBoth(1, &reductions->num_reductions);
	dynAddIntsBoth(reductions->num_reductions, reductions->num_tris_left);
	dynAddIntsBoth(reductions->num_reductions, reductions->error_values);
	dynAddIntsBoth(reductions->num_reductions, reductions->remaps_counts);
	dynAddIntsBoth(reductions->num_reductions, reductions->changes_counts);

	dynAddIntsBoth(1, &reductions->total_remaps);
	dynAddIntsBoth(reductions->total_remaps * 3, reductions->remaps);
	dynAddIntsBoth(1, &reductions->total_remap_tris);
	dynAddIntsBoth(reductions->total_remap_tris, reductions->remap_tris);

	dynAddIntsBoth(1, &reductions->total_changes);
	dynAddIntsBoth(reductions->total_changes, reductions->changes);

	deltas = compressDeltas(reductions->positions,&delta_len,3,reductions->total_changes,PACK_F32,32768.f);
	dynAddIntsBoth(1, &delta_len);
	dynAddDataLittle(delta_len, deltas);
	free(deltas);

	deltas = compressDeltas(reductions->tex1s,&delta_len,2,reductions->total_changes,PACK_F32,4096.f);
	dynAddIntsBoth(1, &delta_len);
	dynAddDataLittle(delta_len, deltas);
	free(deltas);

	zipBlock(pack_little, data_little, count_little);

#undef dynAddDataLittle
#undef dynAddIntsBoth
#undef dynAddIntsLittle
}


#define GEO_VERSION_NUM 17
typedef struct ModelFormatOnDisk_v15
{
	int				struct_size;
	F32				radius;
	int				tex_count;
	int				vert_count;
	int				tri_count;
	int				tex_idx_offset;
	int				name_offset;
	Vec3			min,max;

	struct
	{
		PackDataDisk		tris;
		PackDataDisk		verts;
		PackDataDisk		normals;
		PackDataDisk		binormals;
		PackDataDisk		tangents;
		PackDataDisk		sts;
		PackDataDisk		st3s;
		PackDataDisk		colors;
		PackDataDisk		weights;
		PackDataDisk		matidxs;
		PackDataDisk		reductions;
		PackDataDisk		verts2;
		PackDataDisk		normals2;
	} pack;

	ModelProcessTimeFlags	process_time_flags;
	F32				lightmap_size;
	F32				autolod_dists[3];

} ModelFormatOnDisk_v15;

/*Packs a node tree into an modelheader and its models
note that the name of the header is the full path of the .wrl file minus ".wrl"
*/
void outputPackAllNodes(const char *little_fname, const char *deps_fname, const char *name_UNUSED, Node **list, int count)
{
	int						i;
	Node					*node;
	ModelFormatOnDisk_v15	*model_little;
	U32						*header_little;
	F32						longest = 0;
	int namesize;


#define GROUPINFO 1

	anim_header_count++;


	// The header is just a U32 now.
	header_little		= allocBlockMem(MEM_MODELHEADERS, sizeOfVersion16GeoLoadDataHeader);
	model_little		= allocBlockMem(MEM_MODELS,count * sizeof(*model_little));

	*header_little = count;

	for (i = 0; i < count; i++, model_little++)
	{
		char *name;
		ModelProcessTimeFlags process_time_flags=0;

		node = list[i];

		if (!node->mesh.tri_count)
			printfColor(COLOR_RED|COLOR_BRIGHT, "   WARNING: model %s has no triangles!\n", node->name);

		model_little->struct_size = sizeof(*model_little);

		if (!node->no_tri_optimization)
			process_time_flags |= MODEL_PROCESSED_TRI_OPTIMIZATIONS;
		if (node->high_precision_texcoord)
			process_time_flags |= MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS;
		if (node->wind)
			process_time_flags |= MODEL_PROCESSED_HAS_WIND;
		if (node->trunk_wind)
			process_time_flags |= MODEL_PROCESSED_HAS_TRUNK_WIND;
		if (node->high_detail_high_lod)
			process_time_flags |= MODEL_PROCESSED_HIGH_DETAIL_HIGH_LOD;
		if (node->alpha_tri_sort)
			process_time_flags |= MODEL_PROCESSED_ALPHA_TRI_SORT;
		if (node->vert_color_sort)
			process_time_flags |= MODEL_PROCESSED_VERT_COLOR_SORT;

		model_little->process_time_flags = process_time_flags;

		model_little->name_offset = getNextBlockMemRef(MEM_OBJNAMES);
		namesize = strlen(node->name)+1;
		name = allocBlockMem(MEM_OBJNAMES, namesize);
		strcpy_s(name, namesize, node->name);

		packTris(&model_little->pack.tris,node->mesh.tris,node->mesh.tri_count);
		zipDeltas(&model_little->pack.verts,node->mesh.positions,3,node->mesh.vert_count,32768.f);
		zipDeltas(&model_little->pack.normals,node->mesh.normals,3,node->mesh.vert_count,256.f);
		zipDeltas(&model_little->pack.binormals,node->mesh.binormals,3,node->mesh.vert_count,256.f);
		zipDeltas(&model_little->pack.tangents,node->mesh.tangents,3,node->mesh.vert_count,256.f);
		zipDeltas(&model_little->pack.verts2,node->mesh.positions2,3,node->mesh.vert_count,32768.f);
		zipDeltas(&model_little->pack.normals2,node->mesh.normals2,3,node->mesh.vert_count,256.f);
		zipDeltas(&model_little->pack.sts,node->mesh.tex1s,2,node->mesh.vert_count,node->high_precision_texcoord?32768.f:4096.f);
		zipDeltas(&model_little->pack.st3s,node->mesh.tex2s,2,node->mesh.vert_count,node->high_precision_texcoord?32768.f:4096.f);

		if (node->mesh.colors)
		{
			U8 *colors = calloc(node->mesh.vert_count*4, sizeof(U8));
			int j;
			for (j = 0; j < node->mesh.vert_count; ++j)
			{
				colors[j*4+0] = node->mesh.colors[j].r;
				colors[j*4+1] = node->mesh.colors[j].g;
				colors[j*4+2] = node->mesh.colors[j].b;
				colors[j*4+3] = node->mesh.colors[j].a;
			}
			zipBlock(&model_little->pack.colors,colors,node->mesh.vert_count*4*sizeof(U8));
			// free(colors); freed by zipBlock
		}
		else
		{
			ZeroStructForce(&model_little->pack.colors);
		}

		copyVec3(node->lod_distances, model_little->autolod_dists);

		if (node->reductions)
			packReductions(node->reductions,&model_little->pack.reductions);

		packTexIdxs(&model_little->tex_idx_offset, node->mesh.tris, node->mesh.tri_count, &model_little->tex_count);
		packSkin( &model_little->pack.weights, &model_little->pack.matidxs, &node->mesh, &node->bones);

		model_little->tri_count		= node->mesh.tri_count;
		model_little->vert_count	= node->mesh.vert_count;
		model_little->radius		= node->radius;

		copyVec3(node->min,model_little->min);
		copyVec3(node->max,model_little->max);
		model_little->lightmap_size = node->lightmap_size;
    }

	outputData(little_fname, deps_fname);
}

///################### End Pack Node ##########################################

static int writeLine(FileWrapper *fw, int indent, const char *format, ...)
{
	int i, ret = 0;

	for (i = 0; i < indent; ++i)
	{
		ret += 2;
		fputc(' ', fw);
		fputc(' ', fw);
	}

	VA_START(va, format);
	ret += x_vfprintf(fw, format, va);
	VA_END();

	fputc('\n', fw);
	ret++;

	return ret;
}

// these macros convert to vrml space
#define VEC3DS(v) -((v)[0]), ((v)[1]), ((v)[2])
#define IDX3DS(v) ((v)[2]), ((v)[1]), ((v)[0])

static void outputMeshToVrml(FILE *f, int indent, GMesh *mesh, const char *name)
{
	GMesh temp_mesh;
	int i, cur_tex_id, start_tri, end_tri;

	// separate mesh into one per material
	start_tri = 0;
	while (start_tri < mesh->tri_count)
	{
		ZeroStructForce(&temp_mesh);
		gmeshSetUsageBits(&temp_mesh, mesh->usagebits);

		cur_tex_id = mesh->tris[start_tri].tex_id;
		for (end_tri = start_tri; end_tri < mesh->tri_count; ++end_tri)
		{
			GTriIdx *tri = &mesh->tris[end_tri];
			int idx0, idx1, idx2;

			if (tri->tex_id != cur_tex_id)
				break;

			idx0 = gmeshAddVertSimple(&temp_mesh, 
										mesh->positions?mesh->positions[tri->idx[0]]:NULL,
										mesh->normals?mesh->normals[tri->idx[0]]:NULL,
										mesh->tex1s?mesh->tex1s[tri->idx[0]]:NULL,
										NULL,
										NULL,
										NULL,
										1, false, false);
			idx1 = gmeshAddVertSimple(&temp_mesh, 
										mesh->positions?mesh->positions[tri->idx[1]]:NULL,
										mesh->normals?mesh->normals[tri->idx[1]]:NULL,
										mesh->tex1s?mesh->tex1s[tri->idx[1]]:NULL,
										NULL,
										NULL,
										NULL,
										1, false, false);
			idx2 = gmeshAddVertSimple(&temp_mesh, 
										mesh->positions?mesh->positions[tri->idx[2]]:NULL,
										mesh->normals?mesh->normals[tri->idx[2]]:NULL,
										mesh->tex1s?mesh->tex1s[tri->idx[2]]:NULL,
										NULL,
										NULL,
										NULL,
										1, false, false);
			gmeshAddTri(&temp_mesh, idx0, idx1, idx2, tri->tex_id, 0);
		}

		gmeshMarkDegenerateTris(&temp_mesh);
		gmeshPool(&temp_mesh, true, false, false);

		if (temp_mesh.tri_count)
		{
			writeLine(f, indent, "Shape {");
			indent++;

			writeLine(f, indent, "appearance Appearance {");
			indent++;

			writeLine(f, indent, "material Material {");
			indent++;

			writeLine(f, indent, "diffuseColor 0.5 0.5 0.5");
			writeLine(f, indent, "ambientIntensity 1");
			writeLine(f, indent, "specularColor 0 0 0");
			writeLine(f, indent, "shininess 0");
			writeLine(f, indent, "transparency 0");

			indent--;
			writeLine(f, indent, "}"); // end material

			writeLine(f, indent, "texture ImageTexture {");
			indent++;

			writeLine(f, indent, "url \"../maps/%s\"", tex_names[cur_tex_id]);

			indent--;
			writeLine(f, indent, "}"); // end texture

			indent--;
			writeLine(f, indent, "}"); // end appearance

			writeLine(f, indent, "geometry DEF %s-FACES IndexedFaceSet {", name);
			indent++;

			writeLine(f, indent, "ccw TRUE");
			writeLine(f, indent, "solid TRUE");

			if (temp_mesh.positions)
			{
				writeLine(f, indent, "coord DEF %s%d-COORD Coordinate { point [", name, start_tri);
				indent++;

				for (i = 0; i < temp_mesh.vert_count; ++i)
					writeLine(f, indent, "%.8f %.8f %.8f%s", VEC3DS(temp_mesh.positions[i]), i==temp_mesh.vert_count-1 ? "]" : ",");

				indent--;
				writeLine(f, indent, "}"); // end coord
			}

			if (temp_mesh.normals)
			{
				writeLine(f, indent, "normal Normal { vector [", name, start_tri);
				indent++;

				for (i = 0; i < temp_mesh.vert_count; ++i)
					writeLine(f, indent, "%.8f %.8f %.8f%s", VEC3DS(temp_mesh.normals[i]), i==temp_mesh.vert_count-1 ? "]" : ",");

				indent--;
				writeLine(f, indent, "}"); // end normal

				writeLine(f, indent, "normalPerVertex TRUE");
			}

			if (temp_mesh.tex1s)
			{
				writeLine(f, indent, "texCoord DEF %s%d-TEXCOORD TextureCoordinate { point [", name, start_tri);
				indent++;

				for (i = 0; i < temp_mesh.vert_count; ++i)
					writeLine(f, indent, "%.8f %.8f%s", temp_mesh.tex1s[i][0], temp_mesh.tex1s[i][1], i==temp_mesh.vert_count-1 ? "]" : ",");

				indent--;
				writeLine(f, indent, "}"); // end texCoord
			}

			if (temp_mesh.positions)
			{
				writeLine(f, indent, "coordIndex [");
				indent++;

				for (i = 0; i < temp_mesh.tri_count; ++i)
					writeLine(f, indent, "%d, %d, %d, -1,", IDX3DS(temp_mesh.tris[i].idx));

				indent--;
				writeLine(f, indent, "]"); // end coordIndex
			}

			if (temp_mesh.normals)
			{
				writeLine(f, indent, "normalIndex [");
				indent++;

				for (i = 0; i < temp_mesh.tri_count; ++i)
					writeLine(f, indent, "%d, %d, %d, -1,", IDX3DS(temp_mesh.tris[i].idx));

				indent--;
				writeLine(f, indent, "]"); // end normalIndex
			}

			if (temp_mesh.tex1s)
			{
				writeLine(f, indent, "texCoordIndex [");
				indent++;

				for (i = 0; i < temp_mesh.tri_count; ++i)
					writeLine(f, indent, "%d, %d, %d, -1,", IDX3DS(temp_mesh.tris[i].idx));

				indent--;
				writeLine(f, indent, "]"); // end texCoordIndex
			}

			indent--;
			writeLine(f, indent, "}"); // end geometry

			indent--;
			writeLine(f, indent, "}"); // end Shape

		}

		start_tri = end_tri;
		gmeshFreeData(&temp_mesh);
	}
}

void outputToVrml(const char *output_filename, Node **list, int count)
{
	FILE *f;
	int i, indent = 0;

	f = fopen(output_filename, "wt");
	if (!f)
		return;

	writeLine(f, indent, "#VRML V2.0 utf8\n");

	for (i = 0; i < count; ++i)
	{
		Node *node = list[i];

		if (!node->mesh.tri_count)
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "   WARNING: model %s has no triangles!\n", node->name);
			continue;
		}

		writeLine(f, indent, "DEF %s Transform {", node->name);
		indent++;

		writeLine(f, indent, "children [");
		indent++;

		outputMeshToVrml(f, indent, &node->mesh, node->name);

		indent--;
		writeLine(f, indent, "]"); // end children

		indent--;
		assert(indent == 0);
		writeLine(f, indent, "}\n"); // end DEF
	}

	fclose(f);
}

/*fucked up a little bit*/
static int calcNameTableSize(char *names,int step,int count)
{
	int		i,bytes,idx_bytes;

	idx_bytes = (count + 1) * 4;
	bytes = idx_bytes;
	for(i=0;i<count;i++)
		bytes += strlen(&names[i * step]) + 1;
	bytes = (bytes + 7) & ~7;

	return bytes;
}

static void nameTablePack(int mblock, const char **names)
{
	int		i,bytes,idx_bytes,idx;
	char	*mem,*str;
	size_t str_size;
	int		*idxs;
	int count = eaSize(&names);

	idx_bytes = (count + 1) * 4;
	bytes = idx_bytes;
	for(i=0;i<count;i++)
		bytes += strlen(names[i]) + 1;
	bytes = (bytes + 7) & ~7;

	mem = allocBlockMem(mblock,bytes);
	idxs = (void *)mem;
	idxs[0] = count;
	idxs++;
	str = mem + idx_bytes;
	str_size = bytes - idx_bytes;
	for(idx=i=0;i<count;i++)
	{
		idxs[i] = idx;
		strcpy_s(str + idx, str_size - idx, names[i]);
		idx += strlen(str + idx) + 1;
	}
}

static char *fgets_nocr(char *buf,int len,FILE *file)
{
char	*ret;

	ret = fgets(buf,len,file);
	if (!ret)
		return 0;
	buf[strlen(buf)-1] = 0;
	return ret;
}

static void finishHeader(int data_size_little)
{
	assert(mem_blocks[MEM_HEADER].used == 0);

	writeIntBlockMem2(MEM_HEADER, data_size_little);
	writeIntBlockMem(MEM_HEADER, mem_blocks[MEM_TEXNAMES].used);
	writeIntBlockMem(MEM_HEADER, mem_blocks[MEM_OBJNAMES].used);
	writeIntBlockMem(MEM_HEADER, mem_blocks[MEM_TEXIDX].used);

	appendBlockToHeader(MEM_TEXNAMES);
	appendBlockToHeader(MEM_OBJNAMES);
	appendBlockToHeader(MEM_TEXIDX);
	appendBlockToHeader(MEM_MODELHEADERS);
	appendBlockToHeader(MEM_MODELS);
}

static void *compressHeader(int *ziplen,int *header_len)
{
	U8		*zipped;
	int		header_size;
	int		memblock = MEM_HEADER;

	header_size = mem_blocks[memblock].used;
	*ziplen = header_size*2 + 128;
	zipped = malloc(*ziplen);
	compress(zipped,ziplen,mem_blocks[memblock].data,header_size);
	*header_len = header_size;
	return zipped;
}

__forceinline static void fwrite_int(int ival, FILE *file)
{
	fwrite(&ival,4,1,file);
}

static void writeOutputData(const char *fname, U8 *compressed_header, int compressed_size, int uncompressed_size)
{
	int block_offset = 0;
	FILE *file;

	// open file
	file = fopen(fname,"wb");
	if (!file)
		FatalErrorf("Can't open %s for writing! (Look for Checkout errors above)\n",fname);

	{
		int version = GEO_VERSION_NUM;
		int zero = 0;
		int header_size = compressed_size;

		header_size += 8; // compensate for sizeof(header_len) + sizeof(version) + sizeof(new header) so pig system will cache everything
		fwrite_int(header_size, file);
		fwrite_int(version, file);
	}

	fwrite_int(uncompressed_size, file);
	fwrite(compressed_header, compressed_size, 1, file);
	fwrite(mem_blocks[block_offset+MEM_PACKED].data,1,mem_blocks[block_offset+MEM_PACKED].used,file);
	fwrite(mem_blocks[block_offset+MEM_STRUCTDATA].data,1,mem_blocks[block_offset+MEM_STRUCTDATA].used,file);
	fclose(file);

	loadstart_printf("Updating geo bins...");
	geo2UpdateBinsForGeo(fname);
	geoForceBackgroundLoaderToFinish();
	loadend_printf("Done.");
}

static void writeMaterialDeps(const char *fname, const char *fname_geofile)
{
	int i;
	bool didOne=false;
	char *buf=NULL;
	char relpath[CRYPTIC_MAX_PATH];
	estrStackCreate(&buf);

	estrConcatStatic(&buf, "MaterialDeps\n");
	fileRelativePath(fname_geofile, relpath);
	estrConcatf(&buf, "\tGeoFile \"%s\"\n", relpath);
	for (i=0; i<eaSize(&tex_names); i++) {
		if (stricmp(tex_names[i], "white")!=0) {
			estrConcatf(&buf, "\tMaterialDep \"%s\"\n", tex_names[i]);
			didOne = true;
		}
	}
// Too large
// 	for (i=0; i<eaSize(&model_tex_names); i++)
// 	{
// 		estrConcatf(&buf, "\tMaterialModelDep \"%s\" \"%s\"\n", model_tex_names[i]->modelName, model_tex_names[i]->texname);
// 	}
	estrConcatf(&buf, "EndMaterialDeps\n");

	if (didOne) {
		FILE *file;
		file = fopen(fname, "w");
		if (!file)
			FatalErrorf("Can't open %s for writing! (Look for Checkout errors above)\n",fname);
		fwrite(buf, 1, estrLength(&buf), file);
		fclose(file);
	} else {
		fileForceRemove(fname);
	}

	// Also includes Model -> Material deps
	estrClear(&buf);
	estrConcatStatic(&buf, "MaterialDeps\n");
	fileRelativePath(fname_geofile, relpath);
	estrConcatf(&buf, "\tGeoFile \"%s\"\n", relpath);
	for (i=0; i<eaSize(&tex_names); i++) {
		if (stricmp(tex_names[i], "white")!=0) {
			estrConcatf(&buf, "\tMaterialDep \"%s\"\n", tex_names[i]);
			didOne = true;
		}
	}
	for (i=0; i<eaSize(&model_tex_names); i++)
	{
		estrConcatf(&buf, "\tMaterialModelDep \"%s\" \"%s\"\n", model_tex_names[i]->modelName, model_tex_names[i]->texname);
	}
	estrConcatf(&buf, "EndMaterialDeps\n");

	if (didOne) {
		FILE *file;
		char fname2[MAX_PATH];
		changeFileExt(fname, ".MaterialDeps2", fname2);
		file = fopen(fname2, "w");
		if (!file)
			FatalErrorf("Can't open %s for writing! (Look for Checkout errors above)\n",fname2);
		fwrite(buf, 1, estrLength(&buf), file);
		fclose(file);
	} else {
		fileForceRemove(fname);
	}

	estrDestroy(&buf);
}

/*reset the globals (called after outputData is done) So this 
*/
static void outputResetVars(void)
{
	int		i;

	for(i=0;i<ARRAY_SIZE(mem_blocks);i++)
	{
		SAFE_FREE(mem_blocks[i].data);
		mem_blocks[i].used = 0;
		mem_blocks[i].allocCount = 0;
	}
	anim_header_count = 0;
	texNameClear(1);
}


static void outputData(const char *little_fname, const char *fname_deps)
{
	U8			*header;
	int			ziplen,header_len,i,count;
	int			datasize_little;
	ModelFormatOnDisk_v15	*model_little;


	nameTablePack(MEM_TEXNAMES, tex_names);
	mem_blocks[MEM_OBJNAMES].used = (mem_blocks[MEM_OBJNAMES].used + 3) & ~3;
	mem_blocks[MEM_PACKED].used = (mem_blocks[MEM_PACKED].used + 3) & ~3;

	model_little = (void *)mem_blocks[MEM_MODELS].data;
	count = mem_blocks[MEM_MODELS].used / sizeof(*model_little);

	for( i = 0 ; i < count ; i++, model_little++/*, model_big++*/ )
	{
		assert((int)model_little->name_offset < mem_blocks[MEM_OBJNAMES].used); // Should already be packed
		assert((int)model_little->pack.tris.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.verts.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.verts2.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.normals.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.normals2.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.binormals.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.tangents.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.sts.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.st3s.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.colors.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.weights.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.matidxs.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed
		assert((int)model_little->pack.reductions.data_offset < mem_blocks[MEM_PACKED].used); // Should already be packed

	}

	datasize_little = mem_blocks[MEM_PACKED].used + mem_blocks[MEM_STRUCTDATA].used;

	//pack MEM_TEXNAMES mem block [one int as tex_name count + (tex_name_count *(one ptr to the tex name in MEM_TEXNAMES * size of that texname string)]

	finishHeader(datasize_little/*, datasize_big*/);

	header = compressHeader(&ziplen, &header_len/*, 0*/);
	writeOutputData(little_fname/*, 0*/, header, ziplen, header_len);
	free(header);

	writeMaterialDeps(fname_deps, little_fname);
	{
		char timestampfile[MAX_PATH];
		FILE *f;
		changeFileExt(little_fname, ".timestamp", timestampfile);
		f = fopen(timestampfile, "w");
		if (f) {
			fprintf(f, "%"FORM_LL"d", time(NULL));
			fclose(f);
		}
	}

	outputResetVars();
}

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct ModelHeaderList
{
	ModelHeader **headers; AST(NAME(ModelHeader))
} ModelHeaderList;

int writeModelHeaderToFile(const char *fname, AltPivotInfo*** apis, Node*** meshlist, bool no_checkout, bool is_core, bool character_lib)
{
	char fname_out[MAX_PATH];
	int success;
	ModelHeaderList header_list = {0};

	changeFileExt(fname, MODELHEADER_EXTENSION, fname_out);

	if (no_checkout)
	{
		if (_chmod(fname_out, _S_IREAD | _S_IWRITE) != 0)
			verbose_printf("Unable to change permissions for %s\n", fname_out);
	}
	else
	{
		// File should have been checked out successfully by caller
		if (fileExists(fname_out) && fileIsReadOnly(fname_out))
		{
			ErrorFilenamef(fname_out, "Unable to checkout file!");
			return 0;
		}
	}

	FOR_EACH_IN_EARRAY((*meshlist), Node, model)
		ModelHeader* header = StructAlloc(parse_ModelHeader);
		char buf[MAX_PATH];
		fileRelativePath(fname_out, buf);
		header->filename = allocAddFilename(buf);
		if (character_lib)
		{
			header->modelname = wlCharacterModelKey(fname, model->name);
		}
		else
			header->modelname = allocAddString(model->name);
		header->attachment_bone = model->attachment_bone;
		eaCopy(&header->bone_names, &model->bones.boneNames);

        // Add alt pivots
		FOR_EACH_IN_EARRAY(*apis, AltPivotInfo, api)
			if (stricmp(api->modelname, model->name)==0)
			{
				int i;
				eaSetSize(&header->altpivot, eaSize(&api->altpivot));
				for (i=0; i<eaSize(&api->altpivot); i++) 
				{
					header->altpivot[i] = StructAlloc(parse_AltPivot);
					StructCopy(parse_AltPivot, api->altpivot[i], header->altpivot[i], 0, 0, 0);
				}
			}
		FOR_EACH_END

		copyVec3(model->min, header->min);
		copyVec3(model->max, header->max);
		header->radius = model->radius;
		header->tri_count = model->mesh.tri_count;
		header->has_verts2 = !!model->mesh.positions2;
		header->high_detail_high_lod = !!model->high_detail_high_lod;
		modelHeaderAddToSet(header);

		eaPush(&header_list.headers, header);

		if (RefSystem_ReferentFromString("ModelHeader", header->modelname))
		{
			// Leaks old ModelHeader - free it?
			RefSystem_RemoveReferent(RefSystem_ReferentFromString("ModelHeader", header->modelname), false);
		}
		RefSystem_AddReferent("ModelHeader", header->modelname, header);
	FOR_EACH_END;

	if (!eaSize(&header_list.headers)) {
		printfColor(COLOR_RED|COLOR_BRIGHT, "Error, no models defined in file?\n");
		return 0;
	}

	success = ParserWriteTextFile(fname_out, parse_ModelHeaderList, &header_list, 0, 0);
	if (!success) {
		printfColor(COLOR_RED|COLOR_BRIGHT, "Failed to write %s\n", fname_out);
	}
	eaDestroy(&header_list.headers);
	return success;
}

#include "AutoGen/output_c_ast.c"
