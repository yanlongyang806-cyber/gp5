C_DECLARATIONS_BEGIN
#include "stdtypes.h"
#include "tga.h"
#include "file.h"
#include "mathutil.h"
C_DECLARATIONS_END
#include "ImageHandler.h"
#include <Windows.h>
#include <stdlib.h>
#include <assert.h>


static bool WriteTGA(const char* in_sName, Image *image);
static Image *ReadTGA(const char* in_sName);

Image *getImage(const char *fn, double *aspect, ImageFormat *format)
{
	Image *ret;
	const char *ext=strrchr(fn, '.')+1;


	if (stricmp(ext, "tga")!=0) {
		OutputDebugString("Error: unsupported image extension");
		return NULL;
	}
	ret = ReadTGA(fn);
	if (!ret) {
		OutputDebugString("Error reading file\n");
		return NULL;
	}

	// resize image to fit screen
	*aspect = (double)ret->size[0]/(double)ret->size[1];

	int iPixelWidth;
	if ((ret->format == IF_RGBA || *format==IF_RGBA) && (*format!=IF_RGB && *format!=IF_ALPHA)) {
		// If autodetect or already set to alpha
		*format=IF_RGBA;
		iPixelWidth=4;
	} else if (*format==IF_ALPHA) {
		*format = IF_ALPHA;
		iPixelWidth=1;
	} else if (*format == 0 && ret->format == IF_ALPHA) {
		*format = IF_ALPHA;
		iPixelWidth=1;
	} else {
		// If either a) it's autodetect and there is no alpha channel, or b) it's forced to IF_RGB
		*format=IF_RGB;
		iPixelWidth=3;
	}

	// Output into data, free original
	if (ret->format != *format) {
		unsigned char *image_data = (unsigned char*)malloc(ret->size[0]*ret->size[1]*iPixelWidth);
		if (*format == IF_ALPHA) {
			for (int i=0; i<ret->size[0] * ret->size[1]; i++) {
				if (ret->format == IF_RGBA) {
					image_data[i] = ret->data[i*ret->bytesPerPixel + 3];
				} else if (ret->format == IF_RGB) {
					image_data[i] = ret->data[i*ret->bytesPerPixel];
				} else {
					assert(0);
				}
			}
		} else {
			assert(0);
		}
		free(ret->data);
		ret->data = image_data;
	}

	ret->format = *format;
	return ret;
}

bool saveImage(const char *fn, Image *image)
{
	const char *ext=strrchr(fn, '.')+1;
	
	if (stricmp(ext, "tga")!=0) {
		OutputDebugString("Error: unsupported image extension");
		return false;
	}

	return WriteTGA(fn, image);
}


static bool WriteTGA( const char* in_sName, Image *image )
{
	switch(image->format)
	{
		xcase IF_RGB:
		{
			tgaSaveRGB(in_sName, (char*)image->data, image->size[0], image->size[1], 3);
			return true;
		}
		xcase IF_RGBA:
		{
			return tgaSave(in_sName, (char*)image->data, image->size[0], image->size[1], 4);
		}
		xcase IF_ALPHA:
		{
			//expand it out to 32-bit color since our tga loader cant to alpha only
			unsigned char* tmpData = (unsigned char*)malloc(image->size[0]*image->size[1]*4);
			for (int i = 0; i < image->size[0]*image->size[1]; i++)
			{
				tmpData[i*4] = tmpData[i*4+1] = tmpData[i*4+2] = tmpData[i*4+3] = image->data[i];
			}
			bool result = tgaSave(in_sName, (char*)tmpData, image->size[0], image->size[1], 4);
			free(tmpData);
			return result;
		}
		xdefault:
		{
			OutputDebugString("Error: unsupported image format");
			return false;
		}
	}
}

static Image * ReadTGA( const char* in_sName )
{
	int w = 0, h = 0;
	char* data = tgaLoadFromFname(in_sName, &w, &h);
	
	if (data)
	{
		Image* ret = (Image*)calloc(1, sizeof(Image));
		ret->data = (unsigned char*)data;
		setVec2(ret->size, w, h);
		ret->format = IF_RGBA;
		return ret;
	}
	else
	{
		return NULL;
	}
}