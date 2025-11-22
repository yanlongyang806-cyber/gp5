#include "mathutil.h"
#include "jpeg.h"

static U32 *pixels;
static int		xsize = 500,ysize = 400;

static void plot(int x,int y)
{
	int		idx;

	y = ysize-y;
	if (y >= ysize)
		y = ysize-1;
	if (y < 0)
		y = 0;
	if (x < 0)
		x = 0;
	if (x >= xsize)
		x = xsize-1;
	idx = x + y * xsize;
	assert(idx >=0 && idx < xsize*ysize);
	pixels[idx] = 0xffffff;
}

static void line(int x0,int x1,int y0,int y1)
{
	#define swap(a,b) {int t=a;a=b;b=t;}

	int		x,deltax,deltay,ystep,y,steep;
	float	error,deltaerr;

	steep = ABS(y1 - y0) > ABS(x1 - x0);
	if (steep)
	{
		 swap(x0, y0)
		 swap(x1, y1)
	}
	if (x0 > x1)
	{
		 swap(x0, x1)
		 swap(y0, y1)
	}
	deltax = x1 - x0;
	deltay = ABS(y1 - y0);
	error = 0;
	if (deltax)
		deltaerr = (float)deltay / (float)deltax;
	else
		deltaerr = 1;
	y = y0;

	 if (y0 < y1)
		ystep = 1;
	else
		ystep = -1;
	for(x=x0;x<=x1;x++)
	{
		 if (steep)
			plot(y,x);
		else
			plot(x,y);
		error = error + deltaerr;
		if (error >= 0.5)
		{
			 y = y + ystep;
			 error = error - 1.0;
		}
	}
}

void graphValues(F64 *values,int count)
{
	int		i,x,y,oldx=0,oldy=0;
	F64		minval,maxval,range;
	char	fname[MAX_PATH];

	pixels = calloc(xsize * ysize * sizeof(U32),1);
	maxval = minval = values[0];
	for(i=0;i<count;i++)
	{
		minval = MIN(minval,values[i]);
		maxval = MAX(maxval,values[i]);
	}
	range = maxval - minval;
	if (!range)
		range = 1;
	range=100;
	for(i=0;i<count;i++)
	{
		x = i * xsize / (count-1);
		y = values[i] * ysize / range;
		line(oldx,x,oldy,y);
		oldx=x;
		oldy=y;
	}
	strcpy(fname,"c:/temp/test.jpg");
	//jpgSave(fname,(U8*)pixels,4,xsize,ysize);
	free(pixels);
}

