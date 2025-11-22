#pragma once

#define MAX_IMAGE_RES 2048 // Use -1 for no max - this only effects scaling up to powers of 2

typedef enum ImageFormat
{
	IF_RGB,
	IF_RGBA,
	IF_ALPHA,
	ImageFormat_COUNT,
} ImageFormat;

typedef struct Image
{
	unsigned char *data;
	int size[2];
	int bytesPerPixel;
	ImageFormat format;
} Image;


// Checks if an image is of a valid type (via extension)
Image *getImage(const char *fn, double *aspect, ImageFormat *format);

bool saveImage(const char *fn, Image *image);


