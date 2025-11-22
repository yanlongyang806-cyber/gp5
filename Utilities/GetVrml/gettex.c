#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <share.h>
#include <io.h>
#include <sys/types.h>
#include <time.h>
#include <conio.h>
//#include <omp.h>

#include "stdtypes.h"

#include "error.h"
#include "earray.h"
#include "wininclude.h"
#include "windefinclude.h"
#include "utils.h"
#include "mathutil.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "sysutil.h"
#include "winutil.h"
#include "process_util.h"
#include "file.h"
#include "tga.h"
#include "tiff.h"
#include "timing.h"
#include "gimmeDLLWrapper.h"
#include "ScratchStack.h"
#include "StringUtil.h"
#include "textparser.h"
#include "Color.h"
#include "MemRef.h"
#include "ImageUtil.h"

#include "tdither.h"
#include "mipmap.h"
#include "main.h"
#include "spheremap.h"
#include "DirectDrawTypes.h"
#include "CrypticDXT.h"
#include "RdrTexture.h"
#include "crunch.h"

#include "GraphicsLib.h"
#include "GfxTextureTools.h"
#include "GfxTexOpts.h"
#include "Materials.h"
#include "UTF8.h"

#define DXT_IF_LARGE_THRESHOLD 64
#define MAX_BRIGHTNESS_DST 4.f // must match with the multiplier in AtmosphericScatter_Space.phl and TextureRGBE.phl
#define MAX_BRIGHTNESS_SRC 5.f // must match with the multiplier in GfxAtmospherics.c

static char *cubemap_ext[] = {
	"_posx", "_negx",
	"_posy", "_negy",
	"_posz", "_negz",
};
enum {
	FACE_POSX,
	FACE_NEGX,
	FACE_POSY,
	FACE_NEGY,
	FACE_POSZ,
	FACE_NEGZ,
};

typedef struct TexOpt TexOpt;

typedef struct
{
	int		width,height;
	U8		*data;
} TgaInfo;

typedef struct tgaHeader{
	U8 IDLength;
	U8 CMapType;
	U8 ImgType;
	U8 CMapStartLo;
	U8 CMapStartHi;
	U8 CMapLengthLo;
	U8 CMapLengthHi;
	U8 CMapDepth;
	U8 XOffSetLo;
	U8 XOffSetHi;
	U8 YOffSetLo;
	U8 YOffSetHi;
	U8 WidthLo;
	U8 WidthHi;
	U8 HeightLo;
	U8 HeightHi;
	U8 PixelDepth;
	U8 ImageDescriptor;
} TgaHeader;

static bool checkForDuplicateNames(const char *texturefilename);
static bool checkForDuplicateNamesRemove(char *texturefilename);

static int tgaGetInfo(void *mem, bool *alpha,int *width,int *height, const char *filename)
{
	int			bpp;
	TgaHeader	*tga_header;

	tga_header = mem;
	bpp = (tga_header->PixelDepth + 1) >> 3;
	if (bpp == 2 || bpp == 1) {
		char error[1024];
		sprintf_s(SAFESTR(error), "GetTex Error: File (%s) is a 8 or 16-bit TGA, only 24-bit (opaque) and 32-bit (w/ alpha) TGA files are supported.", filename);
		ErrorFilenamef(filename, "%s", error);
		msgAlert(compatibleGetConsoleWindow(), error);
		*alpha = 0;
		*width = 9999;
		*height = 9999;
		return 0;
	}
	if (bpp == 4)
		*alpha = true;
	else
		*alpha = false;

	*width = tga_header->WidthLo + tga_header->WidthHi * 256;
	*height = tga_header->HeightLo + tga_header->HeightHi * 256;
	return 1;
}

static bool tgaGetRealAlpha(const char *fname)
{
	FILE		*file;
	U8 *data;
	U8 *src;
	bool alpha = false;
	int i;
	int width, height;
	// Exhaustively check the alpha value
	file = fopen(fname, "rb");
	data = tgaLoad(file, &width, &height);
	fclose(file);
	for(src = data,i=0; i<height * width && !alpha; i++,src+=4)
	{
		if (src[3]!=255)
			alpha = true;
	}
	SAFE_FREE(data);
	return alpha;
}

static F32 tgaGetAlphaThreshold(const char *fname, F32 threshold_weight)
{
	FILE		*file;
	F32 total=0;
	U8 *data;
	U8 *src;
	int i;
	int width, height;
	// Exhaustively check the alpha value
	file = fopen(fname, "rb");
	data = tgaLoad(file, &width, &height);
	fclose(file);
	for(src = data,i=0; i<height * width; i++,src+=4)
	{
		total += src[3]/255.f;
	}
	SAFE_FREE(data);
	assert(threshold_weight);
	return total / (height * width) * threshold_weight;
}


static int tgaGetInfoFromFilename(const char *fname, bool *alpha, int *width, int *height)
{
	FILE		*file;
	TgaHeader	header;

	file = fopen(fname,"rb");
	if (!file)
		return 0;
	if (sizeof(TgaHeader)==fread(&header,1,sizeof(TgaHeader),file)) {
		int ret = tgaGetInfo(&header,alpha,width,height, fname);
		fclose(file);
		if (*alpha) {
			*alpha = tgaGetRealAlpha(fname);
		}
		return ret;
	} else {
		fclose(file);
		return 0;
	}
}

static int ddsGetInfoFromFilename(const char *fname, bool *alpha, int *width, int *height)
{
	FILE		*file;
	DDSURFACEDESC2 ddsd;
	char header[5];

	file = fopen(fname,"rb");
	if (!file)
		return 0;
	if (4==fread(header,1,4,file))
	{
		if (strncmp(header, "DDS ", 4)!=0)
		{
			fclose(file);
			return 0;
		}
		if (sizeof(ddsd) == fread(&ddsd, 1, sizeof(ddsd), file))
		{
			fclose(file);
			*width = ddsd.dwWidth;
			*height = ddsd.dwHeight;
			*alpha = (ddsd.ddpfPixelFormat.dwFourCC == FOURCC_DXT1)?false:true;
			return 1;
		} else {
			fclose(file);
			return 0;
		}
	} else {
		fclose(file);
		return 0;
	}
}
static void tgaMakePow2(const char *fname,char *outname,int alpha)
{
	FILE	*file;
	TgaInfo	info = {0};
	int		i,j,size,components,w,h;
	U8		*data;

	file = fopen(fname,"rb");
	if (!file)
		return;
	info.data = tgaLoad(file,&info.width,&info.height);
	fclose(file);
	if (!info.data)
		return;
	size = info.width * info.height * 4;
	components = 3;
	for(i=3;i<size;i+=4)
	{
		if (info.data[i] != 255)
		{
			components = 4;
			break;
		}
	}

	if (alpha)
		components = 4;

	w = 1 << log2(info.width);
	h = 1 << log2(info.height);
	data = calloc(w*h,4);
	// copy old non-p2 image into new bigger p2 rect. Duplicate right and bottom edge to fill out the extra space
	for(i=0;i<info.height;i++)
	{
		memcpy(&data[i*w*4],&info.data[i*info.width*4],info.width*4);
		for(j=info.width;j<w;j++)
		{
			memcpy(&data[(i*w+j)*4],&info.data[(i*info.width+info.width-1)*4],4);
		}
	}
	for(i=info.height;i<h;i++)
	{
		memcpy(&data[i*w*4],&data[(info.height-1)*w*4],w*4);
	}
	tgaSave(outname,data,w,h,components);
	free(data);
	free(info.data);
}

static void findBand(TgaInfo *info, int x, int y, int dx, int dy, U16 *v)
{
	bool bIsInBlack=false;
	bool bFoundFirstBlack=false;
	int i=0;
	while (x<info->width && y<info->height)
	{
		U8 *px = &info->data[(y*info->width+x)*4];
		bool bBlack;
		if (px[3] < 127)
			bBlack = false;
		else if (px[0] > 127)
			bBlack = false;
		else
			bBlack = true;

		if (bBlack && !bIsInBlack)
		{
			bIsInBlack = true;
			if (!bFoundFirstBlack)
			{
				v[0] = i;
				bFoundFirstBlack = true;
			}
		} else if (!bBlack && bIsInBlack) {
			v[1] = i-1;
			bIsInBlack = false;
		}

		x+=dx;
		y+=dy;
		i++;
	}
	if (bIsInBlack)
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "NinePatch: black bar runs off the edge of the image, invalid!\n");
		v[1] = i-2;
	}
}

void tgaParseNinePatch(const char *src_tga_name, const char *out_tga_name, const char *out_wtex_name, bool alpha)
{
	FILE	*file;
	TgaInfo	info = {0};
	int		i,size,components,w,h;
	U8		*data;

	file = fopen(src_tga_name,"rb");
	if (!file)
		return;
	info.data = tgaLoad(file,&info.width,&info.height);
	fclose(file);
	if (!info.data)
		return;
	size = info.width * info.height * 4;
	components = 3;
	for(i=3;i<size;i+=4)
	{
		if (info.data[i] != 255)
		{
			components = 4;
			break;
		}
	}

	if (alpha)
		components = 4;

	// Determine and save NinePatch data
	{
		NinePatch np = {0};
		NinePatchList npl = {0};
		char filename[MAX_PATH];
		findBand(&info, 1, 0, 1, 0, np.stretchableX);
		findBand(&info, 0, 1, 0, 1, np.stretchableY);
		findBand(&info, 1, info.height-1, 1, 0, np.paddingX);
		findBand(&info, info.width-1, 1, 0, 1, np.paddingY);
		changeFileExt(out_wtex_name, ".NinePatch", filename);
		eaPush(&npl.ppNinePatches, &np);
		ParserWriteTextFile(filename, parse_NinePatchList, &npl, 0, 0);
		eaDestroy(&npl.ppNinePatches);
	}

	w = info.width - 2;
	h = info.height - 2;
	data = calloc(w*h,4);
	for(i=0;i<h;i++)
	{
		memcpy(&data[i*w*4],&info.data[((i+1)*info.width+1)*4],w*4);
	}
	tgaSave(out_tga_name,data,w,h,components);
	free(data);
	free(info.data);

}


#define expandU8(c) (F32)(((U8)(c))/255.f*2.f - 1.f)
// static F32 expandU8(U8 c)
// {
// 	return c/255.f*2.f - 1.f;
// }

#define compressF32(ff)  (U8)(((F32)(ff) + 1.f) * 0.5f * 255 + 0.5f)
// static U8 compressF32(F32 f)
// {
// 	return (U8)((f + 1.f) * 0.5 * 255 + 0.5f);
// }

// AUTO_RUN;
void testit_f32_u8_expand_compress(void)
{
	int i;
	for (i=0; i<255; i++) {
		F32 f = expandU8(i);
		U8 c = compressF32(f);
		assert(c == i);
	}
}

F32 lengthU8Vec3Squared(U8 vec[3])
{
	Vec3 v = {expandU8(vec[0]), expandU8(vec[1]), expandU8(vec[2])};
	return lengthVec3Squared(v);
}

bool do_normal_compress_search=true;

void normalizeU8(U8 vec[3])
{
	Vec3 v = {expandU8(vec[0]), expandU8(vec[1]), expandU8(vec[2])};
	normalVec3(v);
	if (!do_normal_compress_search) {
		vec[0] = compressF32(v[0]);
		vec[1] = compressF32(v[1]);
		vec[2] = compressF32(v[2]);
	} else {
		// Try +/-1 to see if we can get a more normalized vector
		int d[] = {-1, 0, 1};
		int i, j, k;
		int base[3];
		U8 test[3];
		bool foundOne=false;
		F32 besterr=99999;

		base[0] = compressF32(v[0]);
		base[1] = compressF32(v[1]);
		base[2] = compressF32(v[2]);
		for (i=0; i<ARRAY_SIZE(d); i++) {
			test[0] = base[0] + d[i];
			if (INRANGE(test[0], 0, 256)) {
				for (j=0; j<ARRAY_SIZE(d); j++) {
					test[1] = base[1] + d[j];
					if (INRANGE(test[1], 0, 256)) {
						for (k=0; k<ARRAY_SIZE(d); k++) {
							test[2] = base[2] + d[k];
							if (INRANGE(test[2], 0, 256)) {
								F32 thislen = lengthU8Vec3Squared(test);
								F32 thiserr = ABS(thislen - 1);
								if (thiserr < besterr) {
									besterr = thiserr;
									copyVec3(test, vec);
									foundOne = true;
								}
							}
						}
					}
				}
			}
		}
		assert(foundOne);
	}
}


static void tgaNormalize(const char *fname,char *outname,int alpha)
{
	FILE	*file;
	TgaInfo	info = {0};
	int		i,size,components;

	file = fopen(fname,"rb");
	if (!file)
		return;
	info.data = tgaLoad(file,&info.width,&info.height);
	fclose(file);
	if (!info.data)
		return;
	size = info.width * info.height * 4;
	components = 3;
	for(i=3;i<size;i+=4)
	{
		if (info.data[i] != 255)
		{
			components = 4;
			break;
		}
	}

	if (alpha)
		components = 4;
	#pragma omp parallel for
	for(i=0;i<info.height*info.width; i++)
	{
		normalizeU8(info.data + i*4);
	}
	tgaSave(outname,info.data,info.width,info.height,components);
	free(info.data);

}

static void tgaPrepareDXT5nm(const char *fname,char *outname)
{
	FILE	*file;
	TgaInfo	info = {0};
	int		i,size,components;

	file = fopen(fname,"rb");
	if (!file)
		return;
	info.data = tgaLoad(file,&info.width,&info.height);
	fclose(file);
	if (!info.data)
		return;
	size = info.width * info.height * 4;
	components = 3;
	for(i=3;i<size;i+=4)
	{
		if (info.data[i] != 255)
		{
			components = 4;
			break;
		}
	}

	assert(components == 3); // Should not have gotten here otherwise

	#pragma omp parallel for
	for(i=0;i<info.height*info.width; i++)
	{
		normalizeU8(info.data + i*4);
		// swizzle xyzw = 0y0x;
		info.data[i*4 + 3] = info.data[i*4 + 0];
		info.data[i*4 + 0] = 0;
		//info.data[i*4 + 1] = info.data[i*4 + 1];
		info.data[i*4 + 2] = 0;
	}
	tgaSave(outname,info.data,info.width,info.height,4);
	free(info.data);
}

static int isPow2(int x)
{
	int		t;
	t =  !(x & (x-1));
	return t;
}

static void savePartialTga(char *outputName, TgaInfo *src_info, int x0, int y0, int width, int height)
{
	char *mem;
	int i;
	width = MIN(width, src_info->width - x0);
	height = MIN(height, src_info->height - y0);
	assert(width && height);
	mem = malloc(width*height*4);
	for(i=0;i<height;i++)
	{
		memcpy(&mem[i*width*4],&src_info->data[((y0+i)*src_info->width + x0)*4],width*4);
	}
	tgaSave(outputName, mem, width, height, 4);
	free(mem);
}

// Returns 0_0, 0_1, 1_0, etc.  EArray of strdup'd strings.
// If given NULL for lowres_tganame, just return the list of endings
static bool texSplit(const char *tganame, const char *outputName, const char *hires_base, char ***endings, char ***options, int **widths, int **heights)
{
	FILE	*file;
	TgaInfo	info = {0};
	int		i,j,size;

	file = fopen(tganame,"rb");
	if (!file)
		return false;
	info.data = tgaLoad(file,&info.width,&info.height);
	fclose(file);
	if (!info.data)
		return false;

	// First save options for low-res version
#define LOWRES_SIZE 512
	eaPush(endings, strdup(""));
	eaPush(options, strdup(STACK_SPRINTF(" -prescale %d %d -RescaleMitchell ", LOWRES_SIZE, LOWRES_SIZE)));
	eaiPush(widths, LOWRES_SIZE);
	eaiPush(heights, LOWRES_SIZE);

	size = 256;

	for (i=0; i*size<info.width; i++) {
		for (j=0; j*size<info.height; j++) {
			char buf[64];
			char split_tga_name[MAX_PATH];
			sprintf(buf, "%d_%d", i, j);
			eaPush(endings, strdup(buf));

			sprintf(split_tga_name, "%s%s.tga", hires_base, buf);

			sprintf(buf, " "); // No options
			eaPush(options, strdup(buf));

			eaiPush(widths, MIN(size, info.width - i*size));
			eaiPush(heights, MIN(size, info.height - j*size));

			// Make .tga file
			savePartialTga(split_tga_name, &info, i*size, j*size, size, size);
		}
	}

	SAFE_FREE(info.data);

	{
		char outputInfo[MAX_PATH];
		char outputBaseName[MAX_PATH];
		changeFileExt(outputName, ".TexSplitInfo", outputInfo);
		changeFileExt(outputName, "", outputBaseName);
		if (GIMME_NO_ERROR == checkoutSingleFile(outputInfo, tganame))
		{
			FILE *f = fopen(outputInfo, "wb");
			fprintf(f, 
				"TexSplitInfo\r\n"
				"\tName %s\r\n"
				"\tOrigSize %d %d\r\n"
				"\tTileSize %d %d\r\n"
				"End\r\n",
				strrchr(outputBaseName, '/') + 1, info.width, info.height,
				size, size);
			fclose(f);
		}
	}

	return true;
}

static void removeSplitOutputs(const char *output_name, const char *fname)
{
	char output[MAX_PATH];
	int x=0, y=0, i;
	char **filenames=NULL;
	int skip=0;
	while (true) {
		char end[64];
		sprintf(end, "@%d_%d.wtex", x, y);
		changeFileExt(output_name, end, output);
		if (fileExists(output)) {
			eaPush(&filenames, strdup(output));
			x++;
			skip=0;
		} else {
			skip++;
			if (skip>=5) {
				if (x==0)
					break;
				x = 0;
				y++;
			} else {
				x++; // Recovery from a couple missing files
			}
		}
	}

	if (GIMME_NO_ERROR == checkoutFiles(filenames, fname)) {
		for (i=0; i<eaSize(&filenames); i++) 
			fileForceRemove(filenames[i]);
	}
	eaDestroyEx(&filenames, NULL);
}

static __forceinline int RoundUpPixelsToNextBlockSize( int pixels )
{
	return (pixels+3) & ~3;
}

static void checkCubemapOutputs(const char *fname, const char *tmpname)
{
	char spheremapName[MAX_PATH];
	char *s;
	strcpy(spheremapName, fname);
	s = strrchr(spheremapName, '_');
	assert(stricmp(s+5, ".tga")==0 || stricmp(s+5, ".dds")==0); // Should be _posx.tga or _negx.tga, etc
	strcpy_s(s, ARRAY_SIZE(spheremapName) - (s-spheremapName), "_spheremap.tga");
	if (!fileExists(spheremapName) ||
		fileNewer(spheremapName, fname))
	{
		if (GIMME_NO_ERROR == checkoutSingleFile(spheremapName, fname))
		{
			if (strEndsWith(fname, ".dds"))
			{
				// Output temporary face .tgas
				char temp[MAX_PATH];
				strcpy(temp, tmpname);
				s = strrchr(temp, '_');
				assert(s);

				// Load DDS
				{
					U8 *data;
					U32 len;
					DDSURFACEDESC2 *ddsd;
					U8 *pixeldata;
					int j;
					RdrTexFormat tex_format;
					U32 levels;

					data = fileAlloc(fname, &len);
					assert(data);
					ddsd = (DDSURFACEDESC2*)((char*)data + 4);
					tex_format = texFormatFromDDSD(ddsd);
					levels = (ddsd->dwFlags & DDSD_MIPMAPCOUNT) ? ddsd->dwMipMapCount : 1;
					assert(ddsd->ddsCaps.dwCaps2 == (DDSCAPS2_CUBEMAP_ALLFACES|DDSCAPS2_CUBEMAP));

					// uncompress faces of all mips
					pixeldata = (U8*)(ddsd+1);
					for (j=0; j<6; j++)
					{
						strcpy_s(s, ARRAY_SIZE(temp) - (s-temp), cubemap_ext[j]);
						strcat_s(s, ARRAY_SIZE(temp) - (s-temp), ".tga");

						if (tex_format == RTEX_DXT1 || tex_format == RTEX_DXT3 || tex_format == RTEX_DXT5)
						{
							int width = RoundUpPixelsToNextBlockSize(ddsd->dwWidth);
							int height = RoundUpPixelsToNextBlockSize(ddsd->dwHeight);
							int dest_size = width * height * 4;
							U8 *dest = malloc(dest_size);
							U8 *walk;
							int i;
							dxtDecompressDirect(pixeldata, dest, width, height, tex_format);
							// swap BGR->RGB
							walk = dest;
							for (i=0; i<width*height; i++, walk+=4)
							{
								U8 t = walk[0];
								walk[0] = walk[2];
								walk[2] = t;
							}
							tgaSave(temp, dest, width, height, 4);
							free(dest);
						}
						else
						{
							unsigned int i;
							// Uncompressed, just save it
							if (tex_format == RTEX_BGRA_U8)
							{
								// swap r&b
								for (i=0; i<ddsd->dwWidth*ddsd->dwHeight*4; i+=4)
								{
									U8 t = pixeldata[i];
									pixeldata[i] = pixeldata[i+2];
									pixeldata[i+2] = t;
								}
								tgaSave(temp, pixeldata, ddsd->dwWidth, ddsd->dwHeight, 4);
							} else if (tex_format == RTEX_BGR_U8) {
								// swap r&b
								for (i=0; i<ddsd->dwWidth*ddsd->dwHeight*3; i+=3)
								{
									U8 t = pixeldata[i];
									pixeldata[i] = pixeldata[i+2];
									pixeldata[i+2] = t;
								}
								tgaSave(temp, pixeldata, ddsd->dwWidth, ddsd->dwHeight, 3);
							} else {
								assert(0);
							}
						}

						// Skip face and mips
						pixeldata += imgByteCount(RTEX_2D, tex_format, ddsd->dwWidth, ddsd->dwHeight, 1, levels);
					}
					free(data);
				}

				fname = temp;
			}
			convertCubemapToSpheremap(fname, spheremapName);
		}
	}
}

static bool tifToTga(const char *tif_fname, const char *tga_fname)
{
	int width, height, bytesPerChannel, numChannels;
	bool isFloat;
	void *tiff_data = tiffLoadFromFilename(tif_fname, &width, &height, &bytesPerChannel, &numChannels, &isFloat);
	U8 *out_data, *dataptr;
	int i;

	if (!tiff_data || numChannels < 3)
	{
		free(tiff_data);
		return false;
	}

	out_data = dataptr = ScratchAlloc(width * height * sizeof(U8) * 4);
	for (i = 0; i < width * height; ++i)
	{
		F32 max_val, multiplier;
		Vec3 data;
		int val;

		if (isFloat && bytesPerChannel == sizeof(F32))
		{
			copyVec3(&((F32*)tiff_data)[i*numChannels], data);
		}
		else if (isFloat && bytesPerChannel == sizeof(F16))
		{
			data[0] = F16toF32(((F16*)tiff_data)[i*numChannels+0]);
			data[1] = F16toF32(((F16*)tiff_data)[i*numChannels+1]);
			data[2] = F16toF32(((F16*)tiff_data)[i*numChannels+2]);
		}
		else if (!isFloat && bytesPerChannel == sizeof(U32))
		{
			scaleVec3(&((U32*)tiff_data)[i*numChannels], MAX_BRIGHTNESS_SRC / ULONG_MAX, data);
		}
		else if (!isFloat && bytesPerChannel == sizeof(U16))
		{
			scaleVec3(&((U16*)tiff_data)[i*numChannels], MAX_BRIGHTNESS_SRC / USHRT_MAX, data);
		}
		else
		{
			assertmsg(0, "TIF files must be 16 or 32 bits per channel");
		}

		max_val = MAX(data[0], data[1]);
		MAX1(max_val, data[2]);

		if (max_val > MAX_BRIGHTNESS_DST)
			max_val = MAX_BRIGHTNESS_DST;

		if (max_val > 0)
		{
			max_val = ceil(max_val * 255.f / MAX_BRIGHTNESS_DST);
			multiplier = 255.f * 255.f / (MAX_BRIGHTNESS_DST * max_val);
		}
		else
		{ 
			max_val = 0;
			multiplier = 0;
		}

		val = round(multiplier * data[0]);
		*(dataptr++) = CLAMP(val, 0, 255); // R

		val = round(multiplier * data[1]);
		*(dataptr++) = CLAMP(val, 0, 255); // G

		val = round(multiplier * data[2]);
		*(dataptr++) = CLAMP(val, 0, 255); // B

		val = round(max_val);
		*(dataptr++) = CLAMP(val, 0, 255); // A
	}

	free(tiff_data);

	if (!tgaSave(tga_fname, out_data, width, height, 4))
	{
		ScratchFree(out_data);
		return false;
	}

	ScratchFree(out_data);
	return true;
}

static int processTextureSub(const char *fname, const char *true_fname, const char *tmpname, const char *output_name, TexOptFlags texopt_flags, int width, int height, int alpha, TexOpt *tex_opt, const char *nvdxt_options, bool is_tiff);

int processLightmap(
	const char *fname_const, const char *tmpname,
	const char *true_fname, const char *output_name,
	TexOpt *tex_opt, const char *nvdxt_opts,
	TexOptFlags texopt_flags, bool alpha) {

	// Combine three lightmaps into two textures.

#define NUM_LIGHTMAP_OUTPUTS 3

	const char *output_name_parts[3] = {
		"avg", "intensity", "lowend"
	};

	// Input files.
	char base_fname[MAX_PATH];
	char fnames[3][MAX_PATH];

	// Temporary DDS files.
	char tmp_base_fname[MAX_PATH];
	char tmp_fnames[NUM_LIGHTMAP_OUTPUTS][MAX_PATH];

	// Temporary TGA files.
	char out_fnames[NUM_LIGHTMAP_OUTPUTS][MAX_PATH];

	// Final output .wtex files.
	char out_wtex_base_fname[MAX_PATH];
	char out_wtex_fnames[NUM_LIGHTMAP_OUTPUTS][MAX_PATH];

	int i;
	bool bRet = true;

	// Get the base name for the image's source name, temporary name, and
	// output name. That is, the name without _base1.blah at the end, so
	// we can get the other image in the set easily and so we can output
	// correct names.

	changeFileExt(fname_const, "", base_fname);
	changeFileExt(tmpname,     "", tmp_base_fname);
	changeFileExt(output_name, "", out_wtex_base_fname);

#define CHECKBASE(x, y) \
	if(strEndsWith(y, x)) { \
		y[strlen(y) - strlen(x)] = 0; \
	}

	// Source name.
	CHECKBASE("_r", base_fname) else
	CHECKBASE("_g", base_fname) else
	CHECKBASE("_b", base_fname) else {
		printf("Error: Invalid lightmap input name: %s\n", base_fname);
		return false;
	}

	// Temporary name.
	CHECKBASE("_r", tmp_base_fname) else
	CHECKBASE("_g", tmp_base_fname) else
	CHECKBASE("_b", tmp_base_fname)

	// Output name.
	CHECKBASE("_r", out_wtex_base_fname) else
	CHECKBASE("_g", out_wtex_base_fname) else
	CHECKBASE("_b", out_wtex_base_fname)

	// Generate the real list of input files.
	for(i = 0; i < 3; i++) {
		sprintf(fnames[i], "%s_%c.tga", base_fname, 
			i == 0 ? 'r' : (i == 1 ? 'g' : 'b'));
	}

	// Generate the real list of output and temp file names.
	for(i = 0; i < NUM_LIGHTMAP_OUTPUTS; i++) {
		sprintf(tmp_fnames[i], "%s_%s.dds", tmp_base_fname, output_name_parts[i]);
		sprintf(out_fnames[i], "%s_%s.tga", tmp_base_fname, output_name_parts[i]);
		sprintf(out_wtex_fnames[i], "%s_%s.wtex", out_wtex_base_fname, output_name_parts[i]);
	}

	{
		// Input data.
		unsigned char *tgaData[3];
		int w;
		int h;
		int tmpw;
		int tmph;

		// Load all three inputs.
		for(i = 0; i < 3; i++) {

			tgaData[i] = tgaLoadFromFname(fnames[i], &tmpw, &tmph);

			if(i == 0) {
				w = tmpw;
				h = tmph;
			}

			if(!tgaData[i] || w != tmpw || h != tmph) {

				int j;

				printf("Error: Can't process lightmap. %s\n",
					tgaData[i] ?
						"Inputs are differing sizes." :
						"Could not load an input.");

				// Clean up all the inputs loaded to this point.
				for(j = 0; j <= i; j++) {
					if(tgaData[i]) free(tgaData[i]);
				}

				return false;
			}
		}

		{
			// Outputs are...
			//   Average color
			//   Intensities for each input as color channels

			unsigned char *avgColorOut  = malloc(4 * w * h);
			unsigned char *intensityOut = malloc(4 * w * h);
			unsigned char *lowEndOut    = malloc(4 * w * h);
			float *avgColorTmp = malloc(4 * w * h * sizeof(float));
			int x;
			int y;

			for(x = 0; x < w; x++) {
				for(y = 0; y < h; y++) {
					int channel;
					for(channel = 0; channel < 4; channel++) {

						int index_base = (x + y * w) * 4;
						int index = (x + y * w) * 4 + channel;

						// Average color. Scaled to 0.0 to 1.0 so we can sanely normalize
						// it later based on intensity.
						avgColorTmp[index] = (
							(float)(tgaData[0][index]) +
							(float)(tgaData[1][index]) +
							(float)(tgaData[2][index])) / (3.0 * 255.0);

						if(channel < 3 && channel >= 0) {

							// Calculate intensity.

							// For some reason, /analyze thinks channel
							// can be greater than three for each of
							// these.
#pragma warning(suppress:6201)
							int r = (int)(tgaData[channel][index_base]);
#pragma warning(suppress:6201)
							int g = (int)(tgaData[channel][index_base + 1]);
#pragma warning(suppress:6201)
							int b = (int)(tgaData[channel][index_base + 2]);

							int intensity = (MAX(MAX(r, g), b));
							intensity = CLAMP(intensity, 0, 255);
							intensityOut[index] = intensity;

						} else {

							// Intensity alpha channel is always opaque.
							intensityOut[index] = 255;
						}

						// Low-end output.
						lowEndOut[index] = (int)(avgColorTmp[index] * 255.0);
					}
				}
			}

			// Normalize average colors.
			for(x = 0; x < w; x++) {
				for(y = 0; y < h; y++) {

					int index_base = (x + y * w) * 4;
					int channel;

					float intensity =
						MAXF(
							avgColorTmp[index_base],
							MAXF(
								avgColorTmp[index_base + 1],
								avgColorTmp[index_base + 2]));

					float inverseIntensity =
						intensity ? (1.0 / intensity) : 255.0;

					for(channel = 0; channel < 3; channel++) {
						
						int avgColorScaled =
							(int)(avgColorTmp[index_base + channel] * inverseIntensity * 255.0);
						
						avgColorOut[index_base + channel] = CLAMP(avgColorScaled, 0, 255);
					}

					// The alpha channel on the average color image is
					// the average intensity for low-end cards.
					avgColorOut[index_base + 3] = CLAMP((int)(intensity * 255.0), 0, 255);
				}
			}

			// Save and process the average color and intensity images.
			for(i = 0; i < NUM_LIGHTMAP_OUTPUTS; i++) {
				tgaSave(out_fnames[i], i == 0 ? avgColorOut : (i == 1 ? intensityOut : lowEndOut), w, h, 4);

				if (GIMME_NO_ERROR == checkoutSingleFile(out_fnames[i], true_fname)) {
					bRet = bRet && processTextureSub(
						out_fnames[i], fname_const,
						tmp_fnames[i], out_wtex_fnames[i], texopt_flags,
						w, h, true, tex_opt, nvdxt_opts, false);
				}
			}

			free(avgColorTmp);
			free(avgColorOut);
			free(intensityOut);
			free(lowEndOut);
		}

		for(i = 0; i < 3; i++) {
			if(tgaData[i]) free(tgaData[i]);
		}
	}

	if(bRet) {
		// We didn't output something with the name of the input file,
		// but we do need a timestamp for it anyway.
		bRet = bRet && texWriteTimestamp(output_name, fname_const, tex_opt);
	}

	return bRet;
}

int processTexture(const char *fname_const, char *output_name, const char* nvdxt_opts)
{
	char	buf[10000],tmpname[1000],buf2[1000],fname[1000];
	const char *s;
	int		width,height;
	TexOpt	*tex_opt;
	TexOptFlags texopt_flags=0;
	bool alpha=false;
	bool bRet;
	bool bDDS;
	bool is_tiff = false;
	//F32 *fade = NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	printf("\n");

	strcpy(fname, fname_const);

	makefullpath(output_name,buf2);
	s = strstri(buf2,"texture_library/");
	sprintf_s(SAFESTR(tmpname),"%s/%s",fileTempDir(),s);
	if (strEndsWith(fname_const, ".9.tga"))
		changeFileExt(tmpname, ".9.dds", tmpname);
	else
		changeFileExt(tmpname, ".dds", tmpname);
	mkdirtree(tmpname);
	backSlashes(tmpname);
	backSlashes(fname);

	if (strEndsWith(fname, ".dds"))
	{
		bDDS = true;
		if (!ddsGetInfoFromFilename(fname, &alpha, &width, &height))
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "Not processing due to invalid DDS file\n");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}
	else
	{
		if (strEndsWith(fname, ".tif"))
		{
			// process TIF into RGBE intermediate TGA file
			char tmpname2[1000];
			changeFileExt(tmpname, ".tga", tmpname2);
			if (!tifToTga(fname, tmpname2))
			{
				printfColor(COLOR_RED|COLOR_BRIGHT, "Not processing due to invalid TIF file\n");
				PERFINFO_AUTO_STOP();
				return 0;
			}
			strcpy(fname, tmpname2);
			is_tiff = true;
		}

		bDDS = false;
		if (!tgaGetInfoFromFilename(fname, &alpha, &width, &height))
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "Not processing due to invalid TGA file\n");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	s = strstriConst(fname,"texture_library");
	if (s)
		s += strlen("texture_library/");
	else
		s = fname;
	tex_opt = texoptFromTextureName(fname_const, &texopt_flags);
	if (texopt_flags & TEXOPT_JPEG)
	{
		changeFileExt(tmpname, ".jpg", tmpname);
	}

	if (strEndsWith(fname, "_bump") || strEndsWith(fname, "_bump.tga"))
		assert(texopt_flags & TEXOPT_BUMPMAP);

	if (bDDS)
	{
		if (texopt_flags & (TEXOPT_SPLIT|TEXOPT_JPEG|TEXOPT_FIX_ALPHA_MIPS|TEXOPT_ALPHABORDER|TEXOPT_COLORBORDER|TEXOPT_NOMIP))
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "DDS files cannot have any process-time TexOpts set!\n");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	makeTexOptString(output_name,SAFESTR(buf2), tex_opt, texopt_flags);
	sprintf_s(SAFESTR(buf),"%4d x %4d %s %s",width,height,alpha ? "ALPHA" : "solid",s);

	if (buf2[0])
	{
		strcat(buf," # ");
		strcat(buf,buf2);
		if (((texopt_flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT) == COMPRESSION_DXT_IF_LARGE) {
			if (width >= DXT_IF_LARGE_THRESHOLD && height >= DXT_IF_LARGE_THRESHOLD) {
				strcat(buf, " (Large, doing DXT)");
			} else {
				strcat(buf, " (Not large, using Truecolor)");
			}
		}
	}
#undef fflush
	printf("\r*%s%78c\n", buf, ' ');
	fflush(stdout);

	if ((!isPower2(width) || !isPower2(height)) && !nvdxt_opts)
	{
		if (!(texopt_flags & TEXOPT_NOMIP))
		{
			printfColor(COLOR_RED|COLOR_BRIGHT,
				"ERROR: Texture is NOT a power of 2, this will be expanded to the next largest\n"
				"  power of 2, taking additional memory, not tiling correctly, and causing problems\n"
				"  on some cards.  For non-UI textures, you must use power of 2 sized textures.\n");
			return 0; // Skip processing it at all, it'll only cause problems
		}
	}

	if (texoptGetCompressionType(tex_opt, texopt_flags) == COMPRESSION_DXT5NM && alpha)
	{
		printfColor(COLOR_RED|COLOR_BRIGHT,
			"ERROR: DXT5nm does not support an alpha channel.  Not processing.\n");
		return 0; // Skip processing
	}

    {
        float texAspectRatio = (float)width / height;
        if( !(texopt_flags & TEXOPT_NOMIP) && (texAspectRatio > 8 || texAspectRatio < 1.0f / 8) ) {
            printfColor( COLOR_RED | COLOR_BRIGHT,
                         "Warning: Texture aspect ratio is greater than 8 -- the low\n"
                         "mips will likely not work on D3D.\n" );
        }
    }
    
	if (texopt_flags & TEXOPT_SPLIT) {
		// Process into multiple files
		char intermediate[MAX_PATH];
		char hires_base[MAX_PATH];
		char output[MAX_PATH];
		char **endings=NULL;
		char **options=NULL;
		int *widths=NULL;
		int *heights=NULL;
		int i;

		assert(!is_tiff);

		changeFileExt(tmpname, "@", hires_base); // c:/Project/tmp/file@

		// checkout and remove all matching files before processing
		removeSplitOutputs(output_name, fname);

		bRet = texSplit(fname, output_name, hires_base, &endings, &options, &widths, &heights); // Returns 0_0, 0_1, 1_0, etc.
		assert(endings[0][0]=='\0'); // First one is not a special ending, just options to NVDXT

		strcpy(intermediate, tmpname); // c:/Project/tmp/file.dds
		strcpy(output, output_name); // c:/Project/data/file.wtex
		bRet &= processTextureSub(fname, NULL, intermediate, output, texopt_flags, widths[0], heights[0], alpha, tex_opt,
                                  STACK_SPRINTF( "%s %s", nvdxt_opts ? nvdxt_opts : "", options[0]), is_tiff);
		for (i=1; i<eaSize(&endings); i++) {
			char hires_tga[MAX_PATH];
			char end[64];
			sprintf(intermediate, "%s%s.dds", hires_base, endings[i]);
			sprintf(hires_tga, "%s%s.tga", hires_base, endings[i]);
			//strcpy(hires_tga, fname);
			sprintf(end, "@%s.wtex", endings[i]);
			changeFileExt(output_name, end, output);
			if (GIMME_NO_ERROR == checkoutSingleFile(output, fname)) {
				bRet &= processTextureSub(hires_tga, NULL, intermediate, output, texopt_flags, widths[i], heights[i], alpha, tex_opt,
                                          STACK_SPRINTF( "%s %s", nvdxt_opts ? nvdxt_opts : "", options[i]), is_tiff);
			}
		}
		eaDestroyEx(&endings, NULL);
		eaDestroyEx(&options, NULL);
		eaiDestroy(&widths);
		eaiDestroy(&heights);

		if (!bRet)
			removeSplitOutputs(output_name, fname);
	
	} else if (texopt_flags & TEXOPT_LIGHTMAP) {

		// Combine three lightmaps into two textures.
		bRet = processLightmap(
			fname_const, tmpname,
			fname, output_name,
			tex_opt, nvdxt_opts,
			texopt_flags, alpha);

	} else {
		bRet = processTextureSub(fname, is_tiff ? fname_const : NULL, tmpname, output_name, texopt_flags, width, height, alpha, tex_opt, nvdxt_opts, is_tiff);
	}

	PERFINFO_AUTO_STOP();

	return bRet;
}

static void borderimageAndFixup(U8 *data, int bpp, int w, int h, Color c)
{
	int i, j;
	for (i=0; i<w; i++)
	{
		for (j=0; j<bpp; j++)
			data[(i + 0*w)*bpp + j] = c.rgba[j];
		for (j=1; j<h-1; j++)
		{
			U8 t = data[(i + j*w)*bpp+0];
			data[(i + j*w)*bpp+0] = data[(i + j*w)*bpp+2];
			data[(i + j*w)*bpp+2] = t;
		}
		for (j=0; j<bpp; j++)
			data[(i + (h-1)*w)*bpp + j] = c.rgba[j];
	}
	for (i=0; i<h; i++)
	{
		for (j=0; j<bpp; j++)
			data[(0 + i*w)*bpp + j] = c.rgba[j];
		for (j=0; j<bpp; j++)
			data[((w-1) + i*w)*bpp + j] = c.rgba[j];
	}
}

// After it's been DXT compressed, ensure that one of the lookup colors in each border block
//  is our border color
void colorBorderDXT(DDSURFACEDESC2 *ddsd, U8 *data, int w, int h, Color border_color, TexOptFlags flags)
{
	int x, y;
	int i;
	Color565 c565;
	int bytesPerBlock;
	c565.r = border_color.r >> 3;
	c565.g = border_color.g >> 2;
	c565.b = border_color.b >> 3;

	if (w<4)
		w = 4;
	if (h<4)
		h = 4;

	bytesPerBlock = 8;
	switch(ddsd->ddpfPixelFormat.dwFourCC)
	{
	case FOURCC_DXT5:
		bytesPerBlock = 16;
		// Intentional fall-through
	case FOURCC_DXT1:
	{
		int bytesPerRow = (w/4)*bytesPerBlock;
		for (y=0; y<h; y+=4)
		{
			for (x=0; x<w; x+=((y==0 || y==h-4)?4:w-4))
			{
				bool b3Color;
				U8 *block = data + (y/4)*bytesPerRow + (x/4)*bytesPerBlock;
				Color565 c[4];
				bool bGood;
				int best;
				int bestDiff;

				if (ddsd->ddpfPixelFormat.dwFourCC == FOURCC_DXT5)
				{
					// Alpha channel first
					U8 a[8];
					a[0] = block[0];
					a[1] = block[1];
					if (a[0] > a[1])
					{
						for (i=2; i<8; i++)
							a[i] = ((8-i)*a[0] + (i-1)*a[1])/7;
					} else {
						for (i=2; i<6; i++)
							a[i] = ((6-i)*a[0] + (i-1)*a[1])/5;
						a[6] = 0;
						a[7] = 255;
					}

					bGood = false;
					best=0;
					bestDiff=100000;
					for (i=0; i<8; i++)
					{
						if (a[i] == border_color.a)
						{
							bGood = true;
							best = i;
							break;
						} else {
							int diff = ABS(a[i] - border_color.a);
							if (diff < bestDiff)
							{
								bestDiff = diff;
								best = i;
							}
						}
					}
					if (!bGood)
					{
						if (best == 0)
							block[0] = border_color.a;
						else if (best == 1)
							block[1] = border_color.a;
						else {
							assert(best == 0 || best == 1); // If not, the logic gets much trickier
						}
					}
					// Set all border colors to be using the best index
					block+=2;
					{
						U32 *p = (U32*)block;
						int xx, yy, b;
						int bits[3] = {!!(best&4), !!(best&2), best&1};
						int bitindex=0;
						for (yy=0; yy<4; yy++)
						{
							for (xx=0; xx<4; xx++)
							{
								int border = (x==0 && xx==0 || y==0 && yy==0 ||
									x==w-4 && xx==3 || y==h-4 && yy==3);
								if (border)
								{
									for (b=0; b<3; b++)
									{
										if (bits[b])
											CLRB(p, bitindex);
										else
											SETB(p, bitindex);
										bitindex++;
									}
								} else {
									bitindex+=3;
								}
							}
						}
						assert(bitindex == 6*8);
					}

					// Advance to beginning of color channel
					block += 6;
				}

				c[0] = ((Color565*)block)[0];
				c[1] = ((Color565*)block)[1];
				if (ddsd->ddpfPixelFormat.dwFourCC == FOURCC_DXT1 && c[0].integer <= c[1].integer)
				{
					b3Color = true;
					c[2].r = (c[0].r + c[1].r) / 2;
					c[2].g = (c[0].g + c[1].g) / 2;
					c[2].b = (c[0].b + c[1].b) / 2;
					c[3].r = c[3].g = c[3].b = 0;
				} else {
					b3Color = false;
					c[2].r = (c[0].r*2 + c[1].r*1) / 3;
					c[2].g = (c[0].g*2 + c[1].g*1) / 3;
					c[2].b = (c[0].b*2 + c[1].b*1) / 3;
					c[3].r = (c[0].r*1 + c[1].r*2) / 3;
					c[3].g = (c[0].g*1 + c[1].g*2) / 3;
					c[3].b = (c[0].b*1 + c[1].b*2) / 3;
				}
				bGood = false;
				best=0;
				bestDiff=100000;
				for (i=0; i<4; i++)
				{
					if (c[i].integer == c565.integer && (i==0 || i==1))
					{
						bGood = true;
						best = i;
						break;
					} else {
						int diff = ABS(c[i].r - c565.r)
							+ ABS(c[i].g - c565.g)
							+ ABS(c[i].b - c565.b);
						if (diff < bestDiff)
						{
							bestDiff = diff;
							best = i;
						}
					}
				}
				if (!bGood)
				{
					if (!(flags & TEXOPT_COLORBORDER_LEGACY))
					{
						if (best == 2)
						{ // Either 4 color and this is near color 0, or 3 color and it's in the middle so we're screwed either way
							best = 0;
						} else if (best == 3) {
							if (b3Color) {
								// Nearest to black, but not exactly back, horrid things!
								best = 0;
							} else {
								// 4 color, nearest to color 1
								best = 1;
							}
						}
					}

					if (best == 0)
						((Color565*)block)[0] = c565;
					else if (best == 1)
						((Color565*)block)[1] = c565;
					else {
						// For now, letting it slide, we have a texture we can reproduce this with
						//assert(ddsd->ddpfPixelFormat.dwFourCC == FOURCC_DXT5 ||
						//	best == 0 || best == 1); // If not, the logic gets much trickier

						// But, for DXT5, we already got the alpha exactly right, so let's not care about a little off on the color channel

					}
				}
				// Set all border colors to be using the best index
				block+=4;
#define SET2B(u8, index, v) (u8) = (u8) & ~(3<<((index)<<1)) | (v<<((index)<<1))
				if (x==0)
				{
					SET2B(block[0], 0, best);
					SET2B(block[1], 0, best);
					SET2B(block[2], 0, best);
					SET2B(block[3], 0, best);
				}
				if (x==w-4)
				{
					SET2B(block[0], 3, best);
					SET2B(block[1], 3, best);
					SET2B(block[2], 3, best);
					SET2B(block[3], 3, best);
				}
				if (y==0)
				{
					SET2B(block[0], 0, best);
					SET2B(block[0], 1, best);
					SET2B(block[0], 2, best);
					SET2B(block[0], 3, best);
				}
				if (y==h-4)
				{
					SET2B(block[3], 0, best);
					SET2B(block[3], 1, best);
					SET2B(block[3], 2, best);
					SET2B(block[3], 3, best);
				}

				if (w==4) // Jumping by w-4 above, don't want to infinite loop!
					break;
			}
		}
		return;
	}
	case FOURCC_DXT3:
		assert(!"not supported");
		break;
	default:
		// Uncompressed, assume it's fine?  Maybe need to deal with 5551 here?
		return;
	}
}


static void doColorBorder(const char *outputfile, const char *tmpdir, TexOptFlags texopt_flags, TexOpt *tex_opt, const char *optionsbuf)
{
	U8 *data;
	U32 len;
	DDSURFACEDESC2 *ddsd;
	U8 *pixeldata;
	unsigned int i;
	int bpp;
	int w, h;
	char tmp_file_name[MAX_PATH];
	Color c = texoptGetBorderColor(tex_opt);
	U8 *finaldata = NULL;
	int finallen;
	int finaloffs=0;
	RdrTexFormat finalformat;
	unsigned int levels;

	// Convert border color to 565 and back
	Color c565;
	c565.r = round(c.r*31.f/255.f);
	c565.g = round(c.g*63.f/255.f);
	c565.b = round(c.b*31.f/255.f);
	c.r = round(c565.r*255.f/31.f);
	c.g = round(c565.g*255.f/63.f);
	c.b = round(c565.b*255.f/31.f);

	// Temporary .dds file was written out in truecolor
	// load & detach 
	data = fileAlloc(outputfile, &len);
	assert(data);
	ddsd = (DDSURFACEDESC2*)((char*)data + 4);
	assert(ddsd->ddpfPixelFormat.dwFlags == DDS_RGBA && ddsd->ddpfPixelFormat.dwRGBBitCount == 32 ||
		ddsd->ddpfPixelFormat.dwFlags == DDS_RGB && ddsd->ddpfPixelFormat.dwRGBBitCount == 24);
	bpp = ddsd->ddpfPixelFormat.dwRGBBitCount >> 3;
	pixeldata = (U8*)(ddsd+1);
	w = ddsd->dwWidth;
	h = ddsd->dwHeight;
	levels = (ddsd->dwFlags & DDSD_MIPMAPCOUNT) ? ddsd->dwMipMapCount : 1;

	sprintf(tmp_file_name, "%s/%s", tmpdir, getFileNameConst(outputfile));
	changeFileExt(tmp_file_name, ".tga", tmp_file_name);
	for (i=0; i<levels; i++)
	{
		char cmd[10000];
		char ddsname[MAX_PATH];
		int level_size;
		// border mips and swap r/b
		borderimageAndFixup(pixeldata, bpp, w, h, c);
		// save mips
		tgaSaveRGB(tmp_file_name, pixeldata, w, h, bpp);
		// nvdxt mips
		sprintf(cmd, "%s -file %s -outdir %s >> c:\\dxtex.txt", optionsbuf, tmp_file_name, tmpdir);
		if (system(cmd))
		{
			assert(0);
		}

		changeFileExt(tmp_file_name, ".dds", ddsname);


		if (i==0)
		{
			finaldata = fileAlloc(ddsname, &finallen);
			finaloffs = 4 + sizeof(DDSURFACEDESC2);
			finalformat = texFormatFromDDSD((DDSURFACEDESC2*)(finaldata + 4));
			level_size = imgByteCount(RTEX_2D, finalformat, w, h, 1, 1); 
		} else {
			// merge it into finaldata
			char *tmpdata;
			int tmplen;
			tmpdata = fileAlloc(ddsname, &tmplen);
			assert(0==memcmp(&((DDSURFACEDESC2*)(tmpdata+4))->ddpfPixelFormat,
				&((DDSURFACEDESC2*)(finaldata+4))->ddpfPixelFormat,
				sizeof(((DDSURFACEDESC2*)(finaldata+4))->ddpfPixelFormat)));
			level_size = imgByteCount(RTEX_2D, finalformat, w, h, 1, 1); 
			assert(finaloffs + level_size <= finallen);
			memcpy(finaldata + finaloffs, tmpdata + 4 + sizeof(DDSURFACEDESC2), level_size);
			fileFree(tmpdata);
		}

		// Go through DXT block if compressed and poke new color values into border lookup tables
		colorBorderDXT((DDSURFACEDESC2*)(finaldata+4), finaldata + finaloffs, w, h, c, texopt_flags);

		finaloffs += level_size;

		pixeldata += w*h*bpp;
		w>>=1;
		if (!w)
			w = 1;
		h>>=1;
		if (!h)
			h = 1;
	}
	assert(finaloffs == finallen);
	// save final data
	{
		FILE *fout = fopen(outputfile, "wb");
		assert(fout);
		fwrite(finaldata, 1, finallen, fout);
		fclose(fout);
	}
	fileFree(finaldata);
	fileFree(data);
}

static void applyEdge(U8 *data[6], int w, int h, int bpp,
					  int src_face, int src_x0, int src_y0, int src_x1, int src_y1,
					  int dst_face, int dst_x0, int dst_y0, int dst_x1, int dst_y1)
{
	int i, j;
	int src_dx = src_x1 - src_x0;
	int src_dy = src_y1 - src_y0;
	int dst_dx = dst_x1 - dst_x0;
	int dst_dy = dst_y1 - dst_y0;
	int src_x = src_x0?(w-1):0;
	int src_y = src_y0?(h-1):0;
	int dst_x = dst_x0?(w-1):0;
	int dst_y = dst_y0?(h-1):0;
	assert(w==h);
	for (i=0; i<w; i++)
	{
		U8 *s = &data[src_face][(src_x + src_y*w)*bpp];
		U8 *d = &data[dst_face][(dst_x + dst_y*w)*bpp];
		for (j=0; j<bpp; j++)
		{
			*d++ = *s++;
		}
		src_x += src_dx;
		src_y += src_dy;
		dst_x += dst_dx;
		dst_y += dst_dy;
	}
}


static void doCubeMapStitching(const char *outputfile, const char *tmpdir, TexOptFlags texopt_flags, TexOpt *tex_opt, const char *optionsbuf)
{
	U8 *data;
	U32 len;
	DDSURFACEDESC2 *ddsd;
	U8 *pixeldata;
	unsigned int i;
	int j;
	int bpp;
	int w, h;
	unsigned int levels;
	char tmp_file_name[MAX_PATH];
	U8 *face_data[20][6];

	// Temporary .dds file was written out in truecolor
	// load & detach 
	data = fileAlloc(outputfile, &len);
	assert(data);
	ddsd = (DDSURFACEDESC2*)((char*)data + 4);
	assert(ddsd->ddpfPixelFormat.dwFlags == DDS_RGBA && ddsd->ddpfPixelFormat.dwRGBBitCount == 32 ||
		ddsd->ddpfPixelFormat.dwFlags == DDS_RGB && ddsd->ddpfPixelFormat.dwRGBBitCount == 24);
	bpp = ddsd->ddpfPixelFormat.dwRGBBitCount >> 3;
	levels = (ddsd->dwFlags & DDSD_MIPMAPCOUNT) ? ddsd->dwMipMapCount : 1;
	assert(ddsd->ddsCaps.dwCaps2 == (DDSCAPS2_CUBEMAP_ALLFACES|DDSCAPS2_CUBEMAP));
	sprintf(tmp_file_name, "%s/%s", tmpdir, getFileNameConst(outputfile));
	changeFileExt(tmp_file_name, "", tmp_file_name);

	// Gather faces of all mips
	pixeldata = (U8*)(ddsd+1);
	for (j=0; j<6; j++)
	{
		w = ddsd->dwWidth;
		h = ddsd->dwHeight;
		for (i=0; i<levels; i++)
		{

			face_data[i][j] = pixeldata;

			pixeldata += w*h*bpp;
			w>>=1;
			if (!w)
				w = 1;
			h>>=1;
			if (!h)
				h = 1;
		}
	}

	// stitch borders, write temps, process temps
	w = ddsd->dwWidth;
	h = ddsd->dwHeight;
	for (i=0; i<levels; i++)
	{
		int level_size = w*h*bpp;

		// stitch
		// top down
		applyEdge(face_data[i], w, h, bpp, FACE_POSY, 0, 0, 0, 1, FACE_NEGX, 0, 0, 1, 0);
		applyEdge(face_data[i], w, h, bpp, FACE_POSY, 0, 1, 1, 1, FACE_POSZ, 0, 0, 1, 0);
		applyEdge(face_data[i], w, h, bpp, FACE_POSY, 1, 1, 1, 0, FACE_POSX, 0, 0, 1, 0);
		applyEdge(face_data[i], w, h, bpp, FACE_POSY, 1, 0, 0, 0, FACE_NEGZ, 0, 0, 1, 0);

		// sides around
		applyEdge(face_data[i], w, h, bpp, FACE_NEGX, 1, 0, 1, 1, FACE_POSZ, 0, 0, 0, 1);
		applyEdge(face_data[i], w, h, bpp, FACE_POSZ, 1, 0, 1, 1, FACE_POSX, 0, 0, 0, 1);
		applyEdge(face_data[i], w, h, bpp, FACE_POSX, 1, 0, 1, 1, FACE_NEGZ, 0, 0, 0, 1);
		applyEdge(face_data[i], w, h, bpp, FACE_NEGZ, 1, 0, 1, 1, FACE_NEGX, 0, 0, 0, 1);

		// bottom up
		applyEdge(face_data[i], w, h, bpp, FACE_NEGY, 0, 0, 0, 1, FACE_NEGX, 1, 1, 0, 1);
		applyEdge(face_data[i], w, h, bpp, FACE_NEGY, 0, 1, 1, 1, FACE_NEGZ, 1, 1, 0, 1);
		applyEdge(face_data[i], w, h, bpp, FACE_NEGY, 1, 1, 1, 0, FACE_POSX, 1, 1, 0, 1);
		applyEdge(face_data[i], w, h, bpp, FACE_NEGY, 1, 0, 0, 0, FACE_POSZ, 1, 1, 0, 1);

		// write and process
		for (j=0; j<6; j++)
		{
			char cmd[10000];
			char facename[MAX_PATH];
			int x;
			U8 *p;
			sprintf(facename, "%s_%d%s.tga", tmp_file_name, i, cubemap_ext[j]);

			// swap RB
			p = face_data[i][j];
			for (x=0; x<w*h; x++, p+=bpp)
			{
				U8 t = p[0];
				p[0] = p[2];
				p[2] = t;
			}

			tgaSaveRGB(facename, face_data[i][j], w, h, bpp);

			// nvdxt mips
			sprintf(cmd, "%s -file %s -outdir %s >> c:\\dxtex.txt", optionsbuf, facename, tmpdir);
			if (system(cmd))
			{
				assert(0);
			}
		}
		w>>=1;
		if (!w)
			w = 1;
		h>>=1;
		if (!h)
			h = 1;
	}

	{
		RdrTexFormat finalformat;
		U8 *finaldata = NULL;
		int finallen;
		int finaloffs;
#define STORE(ptr, x) { memcpy_s(finaldata + finaloffs, finallen - finaloffs, ptr, x); finaloffs += (x); assert(finaloffs <= finallen); }

		// Read processed, and pack into final DDS
		for (j=0; j<6; j++)
		{
			w = ddsd->dwWidth;
			h = ddsd->dwHeight;
			for (i=0; i<levels; i++)
			{
				char ddsname[MAX_PATH];
				char facename[MAX_PATH];
				char *tmpdata;
				int tmplen;
				int level_size;

				sprintf(facename, "%s_%d%s.tga", tmp_file_name, i, cubemap_ext[j]);
				changeFileExt(facename, ".dds", ddsname);

				tmpdata = fileAlloc(ddsname, &tmplen);

				if (i==0 && j==0)
				{
					// Initial allocation and header setup
					DDSURFACEDESC2 *ddsd2 = (DDSURFACEDESC2*)(tmpdata+4);
					assert(ddsd->dwMipMapCount == ddsd2->dwMipMapCount);
					ddsd2->ddsCaps = ddsd->ddsCaps; // Copy original caps

					finalformat = texFormatFromDDSD(ddsd2);
					finallen = imgByteCount(RTEX_2D, finalformat, w, h, 1, levels);
					finallen *= 6;
					finallen += 4 + sizeof(DDSURFACEDESC2);
					finaldata = malloc(finallen);
					finaloffs = 0;
					STORE(tmpdata, 4 + sizeof(DDSURFACEDESC2));
				}

				// merge it into finaldata
				assert(0==memcmp(&((DDSURFACEDESC2*)(tmpdata+4))->ddpfPixelFormat,
					&((DDSURFACEDESC2*)(finaldata+4))->ddpfPixelFormat,
					sizeof(((DDSURFACEDESC2*)(finaldata+4))->ddpfPixelFormat)));
				level_size = imgByteCount(RTEX_2D, finalformat, w, h, 1, 1); 
				STORE(tmpdata + 4 + sizeof(DDSURFACEDESC2), level_size);
				fileFree(tmpdata);

				w>>=1;
				if (!w)
					w = 1;
				h>>=1;
				if (!h)
					h = 1;
			}
		}
#undef STORE
		assert(finaloffs == finallen);
		// save final data
		{
			FILE *fout = fopen(outputfile, "wb");
			assert(fout);
			fwrite(finaldata, 1, finallen, fout);
			fclose(fout);
		}
		fileFree(finaldata);
	}
	fileFree(data);
}


static void autoMakeMaterial(const char *output_name)
{
	char buf[MAX_PATH];
	char material_name[MAX_PATH];
	char tex_name[MAX_PATH];
	FILE *fout;
	if (strstri(output_name, "character_library"))
		return;
	if (strstri(output_name, "costume"))
		return;
	if (strstri(output_name, "/FX/"))
		return;

	strcpy(buf, output_name);
	getFileNameNoExt(material_name, buf);
	getFileNameNoExt(tex_name, buf);
	if (!strEndsWith(material_name, "_D"))
		return;
	material_name[strlen(material_name)-2] = '\0';
	if (materialExists(material_name))
		return;

	strstriReplace(buf, "/texture_library/", "/materials/");
	assert(strEndsWith(buf, "_D.wtex"));
	buf[strlen(buf) - strlen("_D.wtex")] = '\0';
	strcat(buf, ".Material");

	if (!fileExists(buf))
	{
		fout = fileOpen(buf, "w");
		if (fout)
		{
			char *freeme=NULL;
			const char *fmt_string;
			printfColor(COLOR_RED|COLOR_GREEN|COLOR_BRIGHT, "Auto creating material: %s\n", buf);
			fmt_string = freeme = fileAlloc("materials/Templates/NewMaterialTemplate.txt", NULL);
			if (!fmt_string)
				fmt_string = "\n"
					"Material\n"
					"	Template SingleTexture\n"
					"\n"
					"	OperationValue\n"
					"		OpName DiffuseMap\n"
					"\n"
					"		SpecificValue Texture\n"
					"			SValue  %s\n"
					"		EndSpecificValue\n"
					"	EndOperationValue\n"
					"	DiffuseContribution 0.5\n"
					"	PhysicalProperties Default\n"
					"EndMaterial\n";
			fprintf(fout, FORMAT_OK(fmt_string), tex_name);
			SAFE_FREE(freeme);

			fileClose(fout);
		}
	}
}


static int processTextureSub(const char *fname, const char *true_fname, const char *tmpname, const char *output_name, TexOptFlags texopt_flags, int width, int height, int alpha, TexOpt *tex_opt, const char *nvdxt_options, bool is_tiff)
{
	U32		size=0;
	U8		*mem;
	bool bRet = true;
	char **tmp_files = NULL;

	// Make sure that non-DXT formats are not crunched. The crunch flag can be erroneously set
	// if hand-editing a texopt file.
	if (!texoptShouldCrunch(tex_opt, texopt_flags)) {
		texopt_flags &= ~TEXOPT_CRUNCH;
	}

	if (strEndsWith(fname, ".dds"))
	{
		if (texopt_flags & TEXOPT_CUBEMAP)
			checkCubemapOutputs(fname, tmpname);
		tmpname = fname;
	}
	else
	{
		char *buf = NULL;
		char optionsbuf[10000];
		char tga_fname[MAX_PATH], tif_fname[MAX_PATH];
		char tmp_tga_name[MAX_PATH]="";
		const char *texProcessorExe = NULL;
		int texProcessorErrorCode = 0;
		const char *outputfile = NULL;
		const char *s;
		char *s2;
		char tmpdir[MAX_PATH];
		TexOptMipFilterType mip_filter;
		TexOptMipSharpening mip_sharpening;
		TexOptQuality quality;
		TexOptCompressionType compression_type;
		F32 threshold_weight = texoptGetAlphaMipThreshold(tex_opt);

		strcpy(tmpdir,tmpname);
		s2 = strrchr(tmpdir,'\\');
		if (s2)
			*s2 = 0;

		mip_filter = texoptGetMipFilterType(tex_opt);
		mip_sharpening = texoptGetMipSharpening(tex_opt);
		quality = texoptGetQuality(tex_opt);
		compression_type = texoptGetCompressionType(tex_opt, texopt_flags);
		if (compression_type == COMPRESSION_1555)
		{
			// For DX11, we're storing the 16bpp textures as 32bpp on disk, and converting at load time on DX9
			compression_type = COMPRESSION_TRUECOLOR;
		}

		if (texopt_flags & TEXOPT_JPEG)
		{
			texProcessorExe = "cjpeg";
			estrPrintf(&buf,"cjpeg \"%s\"  \"%s\" >> c:\\dxtex.txt",fname,tmpname);
			outputfile = tmpname;
		}
		else
		{
			bool bDXT=false;
			bool bWasScaled=false;
			bool bForceTrueColorBecauseSmall=false;
			s = (const char*)getFileName((char*)fname);

			if (strEndsWith(s, ".9.tga"))
			{
				if (width < 3 || height < 3) {
                    printfColor(COLOR_RED|COLOR_BRIGHT, "Error: Nine patch file with dimensions %dx%d. Minimum is 3x3.", width, height);
                    return false;
				}

				// Parse out NinePatch data, save temp tga without borders
				sprintf_s(SAFESTR(tmp_tga_name),"c:\\%s",s);
				tgaParseNinePatch(fname, tmp_tga_name, output_name, alpha);
				width-=2;
				height-=2;
			}

			if ((!isPow2(width) || !isPow2(height)) && !nvdxt_options && !(texopt_flags & TEXOPT_NOMIP))
			{
				const char *src_name = tmp_tga_name[0]?tmp_tga_name:fname;
				bWasScaled = true;
				sprintf_s(SAFESTR(tmp_tga_name),"c:\\%s",s);
				tgaMakePow2(src_name,tmp_tga_name,alpha);
				if (texopt_flags & TEXOPT_NORMALMAP) {
					printfColor(COLOR_RED|COLOR_BRIGHT, "Error: non-power of 2 normal map!  Not supported!");
					return false;
				}
			}
			if (compression_type == COMPRESSION_DXT5NM)
			{
				sprintf_s(SAFESTR(tmp_tga_name),"c:\\%s",s);
				assert(!alpha);
				tgaPrepareDXT5nm(fname, tmp_tga_name);
				alpha = true;
			}
			else if (texopt_flags & TEXOPT_NORMALMAP)
			{
				assert(!(texopt_flags & TEXOPT_BUMPMAP));
				sprintf_s(SAFESTR(tmp_tga_name),"c:\\%s",s);
				tgaNormalize(fname, tmp_tga_name, alpha);
			}
			texProcessorExe = "nvdxt";
			estrConcat(&buf, nvdxt_path, strlen(nvdxt_path));
			strcpy(optionsbuf, nvdxt_path);
			if (texopt_flags & TEXOPT_CUBEMAP)
			{
				checkCubemapOutputs(fname, NULL);
				estrConcatf(&buf, " -cubeMap -output \"%s\"", tmpname);
			} else if (texopt_flags & TEXOPT_VOLUMEMAP) {
				estrConcatf(&buf, " -volumeMap -output \"%s\"", tmpname);
			} else {
				if (tmp_tga_name[0])
					eaPush(&tmp_files, strdup(tmp_tga_name));
				estrConcatf(&buf, " -file \"%s\"", tmp_tga_name[0] ? tmp_tga_name : fname);
				estrConcatf(&buf, " -output \"%s\"", tmpname);
			}

			outputfile = tmpname;
			if (texopt_flags & TEXOPT_NOMIP)
			{
				estrConcatf(&buf," -nomipmap");
				strcat(optionsbuf, " -nomipmap");
			}
			else if (texopt_flags & (TEXOPT_CLAMPS|TEXOPT_CLAMPT))
			{
				//JE: This mip filtering mode seems to handle clamped alpha edges much better than kaiser which assumes tiling
				estrConcatf(&buf," -cubic");
				strcat(optionsbuf," -cubic");
				// This is probably handled automatically now in the TexOptEditor
			} else {
				if (mip_filter == MIP_KAISER) {
					estrConcatf(&buf," -kaiser");
					strcat(optionsbuf," -kaiser");
				} else if (mip_filter == MIP_CUBIC) {
					estrConcatf(&buf," -cubic");
					strcat(optionsbuf," -cubic");
				} else if (mip_filter == MIP_MITCHELL) {
					estrConcatf(&buf," -Mitchell");
					strcat(optionsbuf," -Mitchell");
				} else { 
					// Assume box
				}

				if (mip_sharpening != SHARPEN_NONE && (texopt_flags & (TEXOPT_NORMALMAP | TEXOPT_BUMPMAP)) == 0) {
					const char *sharpen_type = texoptGetMipSharpeningString(tex_opt);

					estrConcatf(&buf, " -sharpenMethod %s", sharpen_type);
					strcat(optionsbuf, " -sharpenMethod ");
					strcat(optionsbuf, sharpen_type); 
				}
			}
			//estrConcatf(&buf," -dither");

			//if (texopt_flags & TEXOPT_FADE)
			//{
			//	// Fading is done below
			//	//sprintf_s(SAFESTR(buf2)," -u8888 -fade -fadeamount %d -fadecolor 808080 ",0);//(int)tex_opt->blend[0]);
			//	//estrAppend2(&buf,buf2);
			//	alpha = true;
			//	texopt_flags |= TEXOPT_TRUECOLOR;
			//}

			if (compression_type == COMPRESSION_DXT_IF_LARGE)
			{
				if (width >= DXT_IF_LARGE_THRESHOLD && height >= DXT_IF_LARGE_THRESHOLD)
				{
					// Leave compression_type as DXT_IF_LARGE, will skip TRUECOLOR check below
				} else {
					compression_type = COMPRESSION_AUTO;
					bForceTrueColorBecauseSmall = true;
				}
			}

			if (texopt_flags & TEXOPT_CRUNCH)
			{
				bDXT = true;
				if (alpha)
					estrConcatf(&buf, " -u8888");
				else
					estrConcatf(&buf, " -u888");
			} else {
				char compression_flag[10];
				if (compression_type == COMPRESSION_TRUECOLOR ||
					compression_type == COMPRESSION_HALFRES_TRUECOLOR ||
					bForceTrueColorBecauseSmall)
				{
					if (alpha)
						strcpy(compression_flag," -u8888");
					else
						strcpy(compression_flag," -u888");
				} else {
					estrConcatf(&buf, " -quality_production");
					strcat(optionsbuf, " -quality_production");

					switch (compression_type) {
						xcase COMPRESSION_AUTO:
						acase COMPRESSION_DXT_IF_LARGE:
							if (alpha)
								strcpy(compression_flag," -dxt5");
							else
								strcpy(compression_flag," -dxt1a");
							bDXT = true;
						xcase COMPRESSION_DXT1:
							strcpy(compression_flag," -dxt1a");
							bDXT = true;
						xcase COMPRESSION_DXT5:
							strcpy(compression_flag," -dxt5");
							bDXT = true;
						xcase COMPRESSION_1555:
							assert(0); // Shouldn't happen anymore, gets converted to truecolor
							strcpy(compression_flag, " -u1555");
						xcase COMPRESSION_U8:
							strcpy(compression_flag, " -a8");
						xcase COMPRESSION_DXT5NM:
							strcpy(compression_flag," -dxt5");
							bDXT = true;
					}
				}
				if (texopt_flags & (TEXOPT_COLORBORDER|TEXOPT_CUBEMAP))
				{
					if (alpha)
						estrConcatf(&buf, " -u8888");
					else
						estrConcatf(&buf, " -u888");
					strcat(optionsbuf, compression_flag);
				} else {
					estrAppend2(&buf, compression_flag);
					strcat(optionsbuf, compression_flag);
				}
			}

			if (bDXT && ((width % 4) || (height % 4)) && !bWasScaled) {
				width = (width + 3) / 4 * 4;
				height = (height + 3) / 4 * 4;
				estrConcatf(&buf, " -prescale %d %d", width, height);
				printfColor(COLOR_RED|COLOR_BRIGHT, "\nWarning: DXT compressing image which is not a multiple of 4, rescaling to %d x %d; may cause visual quality problems.\n", width, height);
			}

			if (compression_type == COMPRESSION_HALFRES_TRUECOLOR && width > 16 && height > 16) {
				estrConcatf(&buf, " -rel_scale 0.5 0.5 -RescaleMitchell");
			}

			if (texopt_flags & TEXOPT_BUMPMAP)
				estrConcatf(&buf," -n4 -rgb");

			if (nvdxt_options)
				estrAppend2(&buf, nvdxt_options);

			// Must be last!
			if (texopt_flags & TEXOPT_CUBEMAP)
			{
				int i;
				char fname_base[MAX_PATH];
				char cubemap_lst_name[MAX_PATH];
				FILE *cubemap_lst;
				int offset = strlen(fname)-strlen("_negx.tga");
				strcpy(fname_base, fname);
				assert(fname_base[offset] == '_');
				fname_base[offset] = '\0';

				sprintf(cubemap_lst_name, "%s/%s", fileTempDir(), "cubemap.lst");
				cubemap_lst = fopen(cubemap_lst_name, "w");
				for (i=0; i<ARRAY_SIZE(cubemap_ext); i++)
				{
					if (is_tiff)
					{
						sprintf(tif_fname, "%s%s.tif", fname_base, cubemap_ext[i]);
						changeFileExt(outputfile, cubemap_ext[i], tga_fname);
						strcat(tga_fname, ".tga");
						if (!tifToTga(tif_fname, tga_fname))
							bRet = false;
						fprintf(cubemap_lst, "%s\n", tga_fname);
					}
					else
					{
						fprintf(cubemap_lst, "%s%s.tga\n", fname_base, cubemap_ext[i]);
					}
				}
				fclose(cubemap_lst);

				backSlashes(cubemap_lst_name);
				estrConcatf(&buf, " -list %s", cubemap_lst_name);
			}
			else if (texopt_flags & TEXOPT_VOLUMEMAP)
			{
				int i;
				char fname_base[MAX_PATH];
				char volume_lst_name[MAX_PATH];
				FILE *volume_lst;
				int offset = strlen(true_fname)-strlen("_slice0.tif");

				if (width != height)
				{
					width = height = MIN(width, height);
					printfColor(COLOR_RED|COLOR_BRIGHT, "\nWarning: Processing volume image which is not square, rescaling to %d x %d.\n", width, width);
				}

				strcpy(fname_base, true_fname);
				assert(fname_base[offset] == '_');
				fname_base[offset] = '\0';

				sprintf(volume_lst_name, "%s/%s", fileTempDir(), "volumemap.lst");
				volume_lst = fopen(volume_lst_name, "w");
				for (i = 0; i < width; ++i)
				{
					if (is_tiff)
					{
						sprintf(tif_fname, "%s_slice%d.tif", fname_base, i);
						strcpy(tga_fname, outputfile);
						s2 = strrstr(tga_fname, "_voltex");
						if (s2)
						{
							*s2 = 0;
							strcatf(tga_fname, "_slice%d.tga", i);
						}
						else
						{
							changeFileExt(outputfile, "_slice", tga_fname);
							strcatf(tga_fname, "%d.tga", i);
						}
						if (!tifToTga(tif_fname, tga_fname))
							bRet = false;
						else
							eaPush(&tmp_files, strdup(tga_fname));
						fprintf(volume_lst, "%s\n", tga_fname);
					}
					else
					{
						fprintf(volume_lst, "%s_slice%d.tga\n", fname_base, i);
					}
				}
				fclose(volume_lst);

				backSlashes(volume_lst_name);
				estrConcatf(&buf, " -list %s", volume_lst_name);
			}

			if (texopt_flags & TEXOPT_FIX_ALPHA_MIPS) {
				F32 cutoff = tgaGetAlphaThreshold(fname, threshold_weight);
				if (cutoff > 0.85)
					printfColor(COLOR_RED|COLOR_BRIGHT, "FixAlphaMIPs being used, but probably not needed (alpha > 85%%)\n");
				estrConcatf(&buf, " -alpha_threshold %1.3f -binaryalpha", cutoff);
			}

			if (texopt_flags & TEXOPT_COLORBORDER)
			{
				// Temporary .dds file will be written out in truecolor, we'll detach it, load it, border mips, save mips, nvdxt mips, stitch it
			} 
			else if ((texopt_flags & TEXOPT_ALPHABORDER) == TEXOPT_ALPHABORDER)
			{
				estrConcatf(&buf, " -alphaborder");
			}
			else if (texopt_flags & TEXOPT_ALPHABORDER_LR)
			{
				estrConcatf(&buf, " -alphaborderLeft -alphaborderRight");
			}
			else if (texopt_flags & TEXOPT_ALPHABORDER_TB)
			{
				estrConcatf(&buf, " -alphaborderTop -alphaborderBottom");
			}

			estrConcatf(&buf," >> c:\\dxtex.txt");
		}

		if (outputfile)
			remove(outputfile); // Remove output file, since nvdxt just overwrites (doesn't truncate)

		texProcessorErrorCode = system(buf);
		if (texProcessorErrorCode)
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "\n%s returned an error (%d) processing this texture!  Probably a bad .TGA file. Check the end of c:\\dxtex.txt for more info.\n", texProcessorExe, texProcessorErrorCode);
			printfColor(COLOR_RED|COLOR_BRIGHT, "\nFull texture processing command:\n%s\n", buf);
		}
		if (!fileExists(outputfile))
			printfColor(COLOR_RED|COLOR_BRIGHT, "\nOutput file %s generation failed.\n", outputfile);


		if (texopt_flags & TEXOPT_COLORBORDER)
		{
			// Temporary .dds file was written out in truecolor, we'll detach it, load it, border mips, save mips, nvdxt mips, stitch it
			doColorBorder(outputfile, tmpdir, texopt_flags, tex_opt, optionsbuf);
		}
		if (texopt_flags & TEXOPT_CUBEMAP)
		{
			// Temporary .dds file was written out in truecolor, we'll detach it, load it, stitch faces on mips, save mips, nvdxt mips, stitch it
			doCubeMapStitching(outputfile, tmpdir, texopt_flags, tex_opt, optionsbuf);
		}

		estrDestroy(&buf);
	}

	eaForEach(&tmp_files, remove);
	eaDestroyEx(&tmp_files, NULL);

	if (!bRet)
		return false;

	if (g_dds_pause) {
		printf("\nPlease edit/replace %s, and then press any key.\n", tmpname);
		_getch();
	}

	mem = fileAlloc(tmpname,&size);
	if (mem) {
		//fade = texoptGetFade(tex_opt);
		//if (texopt_flags & TEXOPT_FADE) {
		//	// Do post-processing on .dds file for Fade stuff
		//	gluBuild2DMipmapsRGBFadeInMem(width, height, GL_RGBA, GL_UNSIGNED_BYTE, mem + 4 + sizeof_DDSURFACEDESC2, 256*fade[0], 256*fade[1]);
		//}

		if (texopt_flags & TEXOPT_CRUNCH) {
			char *crn_data = NULL;
			size_t crn_size;
			CrnQualityLevel crnQuality;
			CrnTargetFormat crnFormat;

			switch (texoptGetQuality(tex_opt)) {
			case QUALITY_LOWEST:
				crnQuality = CRN_QUALITY_LOWEST;
			xdefault:
				crnQuality = CRN_QUALITY_NORMAL;
			}

			switch (texoptGetCompressionType(tex_opt, texopt_flags)) {
			case COMPRESSION_DXT5:
			case COMPRESSION_DXT5NM:
				crnFormat = CRN_FORMAT_DXT5;
			xcase COMPRESSION_DXT1:
				crnFormat = CRN_FORMAT_DXT1;
			xdefault:
				crnFormat = alpha ? CRN_FORMAT_DXT5 : CRN_FORMAT_DXT1;
			}

			// make sure the reversed mips flag is set in the wtex file header
			// even if it somehow is not set in the texopts
			if ((texopt_flags & TEXOPT_NOMIP) == 0) {
				texopt_flags |= TEXOPT_REVERSED_MIPS;
			}

			crn_size = crnCompressDds(&crn_data, mem, size, CRN_FILE_TYPE_CRN, crnFormat, crnQuality);
			if (crn_size) {
				void *mipHeader = NULL;

				if ((texopt_flags & TEXOPT_NOMIP) == 0) {
					// decompress the low mips to a temp buffer for the cached mip header
					mipHeader = malloc(crnDdsSizeForLevels(crn_data, 6, 1));
					crnDecompressToDds(mipHeader, crn_data, NULL, 6);
				}

				bRet = texWriteData(output_name, crn_data, crn_size, mipHeader, width, height, alpha, texopt_flags, true_fname?true_fname:fname, tex_opt);
				
				free(mipHeader);
			} else {
				bRet = false;
			}

			crnFree(crn_data);
		} else {
			// Write file data
			bRet = texWriteData(output_name, mem, size, NULL, width, height, alpha, texopt_flags, true_fname?true_fname:fname, tex_opt);
		}

		//autoMakeMaterial(output_name);	// Disapling the autocreation of materials when processing textures.

		free(mem);
	} else {
		bRet = false;
	}
	return bRet;
}

static bool isSplitFile(const unsigned char *outputPath, unsigned char *baseName, size_t baseName_size)
{
	char *ext = strrchr(outputPath, '.');
	char *s = ext;
	if (!s)
		return false;
	s--;
	if (!isdigit(*s))
		return false;
	while (isdigit(*s) && s>=outputPath)
		s--;
	if (s < outputPath || *s!='_')
		return false;
	s--;
	if (!isdigit(*s))
		return false;
	while (isdigit(*s) && s>=outputPath)
		s--;
	if (s < outputPath || *s!='@')
		return false;
	// String ends in the form _###_###.***
	strcpy_s(SAFESTR2(baseName), outputPath);
	baseName[s - outputPath] = '\0';
	strcat_s(SAFESTR2(baseName), ext);
	return true;
}


static char tgaRootPath[MAX_PATH];

static FileScanAction deleteIfMissingTga(char *dir, struct _finddata32_t *data, void *pUserData)
{
	char wtexPath[MAX_PATH];
	char fullTexPath[MAX_PATH];
	char tgaPath[MAX_PATH];
	char ddsPath[MAX_PATH];
	char tifPath[MAX_PATH];
	char ninePatchPath[MAX_PATH];
	char *s;
	char *texPathAndName;
	TexOptFlags texopt_flags;

	PERFINFO_AUTO_START_FUNC();

	sprintf_s(SAFESTR(wtexPath), "%s/%s", dir, data->name);
	forwardSlashes(wtexPath);
	if (data->name[0]=='_') {
		PERFINFO_AUTO_STOP_FUNC();
		return FSA_NO_EXPLORE_DIRECTORY;
	}
	if (!strrchr(wtexPath, '.') || stricmp(strrchr(wtexPath, '.'), ".wtex")!=0) {
		// not a .wtex file
		PERFINFO_AUTO_STOP_FUNC();
		return FSA_EXPLORE_DIRECTORY;
	}
	assert(!(data->attrib & _S_IFDIR));

	texPathAndName = strstri(wtexPath,"texture_library");
	if (texPathAndName)
		texPathAndName += strlen("texture_library/");
	else
		assert(false);

	// Make path to source TGA
	strcpy(tgaPath, tgaRootPath);
	s = strstri(tgaPath, "texture_library");
	if (s) {
		*s = 0;
		strcat(tgaPath, "texture_library");
	}
	strcat(tgaPath, "/");
	strcat(tgaPath, texPathAndName);

	// Cubemap input files should not be in data!
	strstriReplace(tgaPath, "_posx.", "_CubeMap Output File That Shouldn't exist.");
	strstriReplace(tgaPath, "_posy.", "_CubeMap Output File That Shouldn't exist.");
	strstriReplace(tgaPath, "_posz.", "_CubeMap Output File That Shouldn't exist.");
	strstriReplace(tgaPath, "_negx.", "_CubeMap Output File That Shouldn't exist.");
	strstriReplace(tgaPath, "_negy.", "_CubeMap Output File That Shouldn't exist.");
	strstriReplace(tgaPath, "_negz.", "_CubeMap Output File That Shouldn't exist.");

	if (fileCoreSrcDir() && strStartsWith(tgaRootPath, fileCoreSrcDir()))
		sprintf(fullTexPath, "%s/%s", fileCoreDataDir(), wtexPath);
	else
		sprintf(fullTexPath, "%s/%s", fileDataDir(), wtexPath);

	if (!fileExists(fullTexPath))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FSA_EXPLORE_DIRECTORY;
	}

	if (strrchr(tgaPath, '.')) {
		changeFileExt(tgaPath, ".tga", tgaPath);
	} else {
		assert(false);
	}
	changeFileExt(tgaPath, ".dds", ddsPath);
	strstriReplace(tgaPath, "_cube.tga", "_posx.tga");
	strstriReplace(tgaPath, "_voltex.tga", "_slice0.tga");
	if (isSplitFile(tgaPath, SAFESTR(tgaPath))) {
		// It's a split file, and we just changed the tga path name
	}
	changeFileExt(tgaPath, ".tif", tifPath);
	changeFileExt(tgaPath, ".9.tga", ninePatchPath);

	texoptFromTextureName(wtexPath, &texopt_flags);

	if (!fileExists(tgaPath) && !fileExists(ddsPath) && !fileExists(tifPath) && !fileExists(ninePatchPath) || (texopt_flags & TEXOPT_EXCLUDE))
	{
		bool deleted=false;
		do { // Just using Continues as a Goto

			// Check for orphaned files we don't want to delete
			// This can happen all the time now, since people may not check their .tgas in right away
			//printf("Not deleting %s's orphaned file : %s\n", fullpath);

            if( strstr( fullTexPath, "/generated/" )) {
                continue;       //< autogenerated textures don't have a source TGA
            }

			if(texopt_flags & TEXOPT_LIGHTMAP) {

				// Check to see if any base file exists.
				char basePath[MAX_PATH];
				int i;
				bool foundBaseImage = false;
				strcpy(basePath, tgaPath);

				// Find the last underscore.
				for(i = strlen(basePath); i >= 0; i--) {
					if(basePath[i] == '_') {
						basePath[i] = 0;
						break;
					}
				}

				// Check all the possible input images.
				for(i = 0; i < 3; i++) {
					char realBaseImage[MAX_PATH];					
					sprintf(realBaseImage, "%s_%c.tga", basePath,
						i == 0 ? 'r' : (i == 1 ? 'g' : 'b'));
					if(fileExists(realBaseImage)) {
						foundBaseImage = true;
						break;
					}
				}

				if(foundBaseImage) {
					// Found one of the base images. Don't prune this.
					continue;
				}
			}


			if (!g_force_rebuild) {
				const char *lockee = gimmeDLLQueryIsFileLocked(tgaPath);
				if (lockee && stricmp(lockee, gimmeDLLQueryUserName())!=0) {
					continue; // .tga is checked out by someone else
				}
				if (!lockee) 
				{
					const char *lastauthor = gimmeDLLQueryLastAuthor(tgaPath);
					if (lastauthor && stricmp(lastauthor, gimmeDLLQueryUserName())!=0) {
						ANALYSIS_ASSUME(lastauthor);
						if (strstriConst(lastauthor, "Not in database")) {
							// Not in database, see if we own the .tga
							if (gimmeDLLQueryIsFileMine(wtexPath)) {
								// The .wtex file is mine!  Delete it!
							} else {
								continue;
							}
						} else {
							// Handle files that had their checkouts undone
							if (gimmeDLLQueryIsFileMine(wtexPath)) {
								// The .wtex file is mine!  Delete it!
							} else {
								continue;
							}
						}
					}
				}
			}

			if (g_no_checkout) {
				printf("Would delete : %s\n", fullTexPath);
				deleted = true;
				continue;
			}
			// It's orphaned, no other condition hit, delete it!
			s = fullTexPath;
			{
				char msg[1024];
				static bool doneOnce=false;
				if (!doneOnce) {
					printf("GetTex is prompting you about deleting a texture.  Click Show Console Window in the MCP to show the dialog if it is not visible.\n");
					doneOnce = true;
				}
				sprintf(msg, "Source TGA or DDS file (%s) has been removed, or this is in an excluded folder.\nOkay to delete?\n  %s\nIf you choose No, you must restore the appropriate TGA file.", 
					tgaPath, s);
				if (g_force_rebuild >= 2 || MessageBox_UTF8(compatibleGetConsoleWindow(), msg, "Delete Texture?", MB_YESNO)==IDYES)
				{
					static const char * const sibling_exts[] = { ".timestamp", ".htex" };
					int sib;

					printf(" Deleting : %s\n", s);
					gimmeDLLForceManifest(false);
					gimmeDLLDoOperation(s, GIMME_CHECKOUT, GIMME_QUIET);
					gimmeDLLForceManifest(true);
					fileForceRemove(s);

					// delete any sibling files
					for (sib = 0; sib < ARRAYSIZE(sibling_exts); ++sib) {
						char siblingfile[MAX_PATH];
						changeFileExt(s, sibling_exts[sib], siblingfile);
						if (fileExists(siblingfile))
						{
							printf(" Deleting : %s\n", siblingfile);
							gimmeDLLForceManifest(false);
							gimmeDLLDoOperation(siblingfile, GIMME_CHECKOUT, GIMME_QUIET);
							gimmeDLLForceManifest(true);
							fileForceRemove(siblingfile);
						}
					}
					deleted = true;
				}
			}
		} while (false);
	}
	PERFINFO_AUTO_STOP_FUNC();
	return FSA_EXPLORE_DIRECTORY;
}

void pruneTextures(const char *path)
{
	strcpy(tgaRootPath, path);

	// Delete .wtex files for which there are no corresponding .tga files
	gimmeDLLForceManifest(true);
	fileScanAllDataDirs("texture_library", deleteIfMissingTga, NULL);
	gimmeDLLForceManifest(false);
}
