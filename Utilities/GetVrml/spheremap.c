#include "spheremap.h"
#include "file.h"
#include "tga.h"
#include "error.h"
#include "mathutil.h"

typedef struct {
	U16 face;
	F32 x, y;
} Point;
static struct {
	char fnames[6][MAX_PATH];
	U8 *data[6]; // RGBA data
	int w, h;
	U8 *spheremap; // RGBA (but alpha ignored)
	Point *pixelmap; // Map from dest pixel to cubemap face
} cubemapdata;

static Point vectorToPoint(Vec3 v)
{
	Point p;
	int biggest=0;
	if (ABS(v[1]) > ABS(v[0]))
		biggest = 1;
	if (ABS(v[2]) > ABS(v[biggest]))
		biggest = 2;
	p.face = biggest*2;
	if (v[biggest] < 0)
		p.face++;
	if (biggest == 0)
	{
		F32 t = 0.5f / v[0];
		p.x = (-t*v[2] + 0.5f) * cubemapdata.w;
		p.y = (((p.face==1)?1:-1)*t*v[1] + 0.5f) * cubemapdata.h;
	} else if (biggest == 1)
	{
		F32 t = 0.5f / v[1];
		p.x = (((p.face==3)?-1:1)*t*v[0] + 0.5f) * cubemapdata.w;
		p.y = (((p.face==3)?1:1)*t*v[2] + 0.5f) * cubemapdata.h;
	} else {
		F32 t = 0.5f / v[2];
		p.x = (t*v[0] + 0.5f) * cubemapdata.w;
		p.y = (((p.face==5)?1:-1)*t*v[1] + 0.5f) * cubemapdata.h;
	}
	return p;
}

static Point findPixel(int x, int y)
{
	Point r = {0};
	F32 xnorm = -1*(2*x / (F32)(cubemapdata.w - 1) - 1); // -1 .. 1
	// theta is 0 to 2*PI
	F32 theta = PI + 2 * asin(xnorm);
	//F32 theta = xnorm*xnorm*SIGN(xnorm)*PI + PI;
	// phi is -PI/2 to PI/2
	F32 ynorm = 2*y / (F32)(cubemapdata.h - 1) - 1; // -1..1
	//F32 phi = HALFPI * ynorm;
	F32 phi = asin(ynorm);
	F32 cosphi, sinphi;
	F32 costheta, sintheta;
	Vec3 v;
	sincosf(theta, &sintheta, &costheta);
	sincosf(phi, &sinphi, &cosphi);
	setVec3(v, cosphi * sintheta, -sinphi, cosphi * costheta);
	r = vectorToPoint(v);
	return r;
}

static void findPixels(void)
{
	int x, y;
	for (x=0; x<cubemapdata.w; x++)
	{
		for (y=0; y<cubemapdata.h; y++)
		{
			cubemapdata.pixelmap[x + y*cubemapdata.w] = findPixel(x, y);
		}
	}
}

static void sampleNearest(void)
{
	int x, y;
	for (x=0; x<cubemapdata.w; x++)
	{
		for (y=0; y<cubemapdata.h; y++)
		{
			Point p = cubemapdata.pixelmap[x + y*cubemapdata.w];
			int srcx = CLAMP((int)p.x, 0, cubemapdata.w-1);
			int srcy = CLAMP((int)p.y, 0, cubemapdata.h-1);
			U8 *src = &cubemapdata.data[p.face][(srcx + srcy*cubemapdata.w)*4];
			U8 *dst = &cubemapdata.spheremap[(x + y*cubemapdata.w)*4];
			*(U32*)dst = *(U32*)src;
		}
	}
}

static void blurSpheremap(void)
{
	int kernel[] = {1, 4, 6, 4, 1};
	int sum = 1+4+6+4+1;
	F32 invsum = 1.f/sum;
	int x, y, i, j;
	U8 *newbuffer = calloc(cubemapdata.w*cubemapdata.h, 4);
	for (x=0; x<cubemapdata.w; x++)
	{
		for (y=0; y<cubemapdata.h; y++)
		{
			int s[3] = {0,0,0};
			for (i=0; i<ARRAY_SIZE(kernel); i++)
			{
				int newx = x + i-(ARRAY_SIZE(kernel)/2);
				if (newx<0)
					newx+=cubemapdata.w;
				if (newx>=cubemapdata.w)
					newx-=cubemapdata.w;
				for (j=0; j<3; j++)
					s[j]+=cubemapdata.spheremap[(newx+y*cubemapdata.w)*4+j]*kernel[i];
			}
			for (j=0; j<3; j++)
			{
				int v = (int)round(s[j]*invsum);
				newbuffer[(x+y*cubemapdata.w)*4+j] = CLAMP(v, 0, 255);
			}
		}
	}
	for (x=0; x<cubemapdata.w; x++)
	{
		for (y=0; y<cubemapdata.h; y++)
		{
			int s[3] = {0,0,0};
			for (i=0; i<ARRAY_SIZE(kernel); i++)
			{
				int newy = y + i-(ARRAY_SIZE(kernel)/2);
				if (newy<0)
					newy+=cubemapdata.h;
				if (newy>=cubemapdata.h)
					newy-=cubemapdata.h;
				for (j=0; j<3; j++)
					s[j]+=newbuffer[(x+newy*cubemapdata.w)*4+j]*kernel[i];
			}
			for (j=0; j<3; j++)
			{
				int v = (int)round(s[j]*invsum);
				cubemapdata.spheremap[(x+y*cubemapdata.w)*4+j] = CLAMP(v, 0, 255);
			}
		}
	}
}

bool convertCubemapToSpheremap(const char *cubemapName, const char *spheremapName)
{
	bool bRet = true;
	int i;
	ZeroStruct(&cubemapdata);

	// Load data and validate
	{
		char *ext[] = {
			"_posx", "_negx",
			"_posy", "_negy",
			"_posz", "_negz",
		};
		int offset = strlen(cubemapName)-strlen("_negx.tga");
		char fname_base[MAX_PATH];
		strcpy(fname_base, cubemapName);
		assert(fname_base[offset] == '_');
		fname_base[offset] = '\0';
		for (i=0; i<ARRAY_SIZE(ext); i++) {
			int w, h;
			sprintf(cubemapdata.fnames[i], "%s%s.tga", fname_base, ext[i]);
			cubemapdata.data[i] = tgaLoadFromFname(cubemapdata.fnames[i], &w, &h);
			if (!cubemapdata.data[i])
			{
				ErrorFilenamef(cubemapdata.fnames[i], "Unable to open %s when converting a cubemap to spheremap.", cubemapdata.fnames[i]);
				bRet = false;
				goto cleanup;
			}
			if (i==0)
			{
				if (w != h)
				{
					ErrorFilenamef(cubemapdata.fnames[i], "Non-square cubemap %d x %d", w, h);
					bRet = false;
					goto cleanup;
				}
				cubemapdata.w = w;
				cubemapdata.h = h;
			} else {
				if (cubemapdata.w != w || cubemapdata.h != h)
				{
					ErrorFilenamef(cubemapdata.fnames[i], "One cubemap face does not have the same dimensions as the other.  Found %d x %d.  Expected %d x %d", w, h, cubemapdata.w, cubemapdata.h);
					bRet = false;
					goto cleanup;
				}
			}
		}
	}

	cubemapdata.spheremap = calloc(cubemapdata.w * cubemapdata.h, 4);
	cubemapdata.pixelmap = calloc(cubemapdata.w * cubemapdata.h, sizeof(Point));
	findPixels();
	// Sample from source pixels to dest pixels
	sampleNearest();
	// TODO later if needed: sample from area based on neighboring pixels/du/dv/etc
	blurSpheremap();

	tgaSave(spheremapName, cubemapdata.spheremap, cubemapdata.w, cubemapdata.h, 3);

cleanup:
	for (i=0; i<ARRAY_SIZE(cubemapdata.data); i++)
		SAFE_FREE(cubemapdata.data[i]);
	SAFE_FREE(cubemapdata.spheremap);
	SAFE_FREE(cubemapdata.pixelmap);
	return bRet;
}
