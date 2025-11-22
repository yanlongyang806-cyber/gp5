#pragma once

#include "stdio.h"
#include "windows.h"

#define StructCalloc(structType) ((structType*)calloc(sizeof(structType), 1))

typedef unsigned char U8;
typedef signed char S8;
typedef volatile signed char VS8;
typedef volatile unsigned char VU8;

typedef unsigned short U16;
typedef short S16;
typedef volatile unsigned short VU16;
typedef volatile short VS16;

typedef int S32;
typedef unsigned int U32;
typedef volatile int VS32;
typedef volatile unsigned int VU32;

typedef unsigned __int64 U64;
typedef __int64 S64;
typedef volatile __int64 VS64;
typedef volatile unsigned __int64 VU64;

typedef float F32;
typedef volatile float VF32;

typedef U16 F16;
typedef VU16 VF16;

typedef U32 EntityRef;
typedef U8 DirtyBit;

FILE *fopen_nofail(const char *pFileName, const char *pModes);

bool FileExists(const char *pFileName);

void BreakIfInDebugger();

int ProcessCount(char * procName);

int dirExists(const char *dirname);

//not for serious use
//will break around the first of each month
int timeApproxDifInMinutes(SYSTEMTIME *pTime1, SYSTEMTIME *pTime2);

//malloc/free
char *fileAlloc(const char *pFileName, int *piOutSize);



__forceinline static U32 highBitIndex(U32 value)
{
	U32 mask = 1 << (sizeof(U32) * 8 - 1);
	U32 count = 0;

	value = ~value;
	
	while(mask & value)
	{
		mask >>= 1;
		count++;
	}

	return 31 - count;
}


#define ARRAY_SIZE(n) (sizeof(n) / sizeof((n)[0]))

bool IsContinuousBuilder(void);

//bTrimToDir means instead of running this on c:\src\libs\SomeLib\file.txt, just run it on c:\src\libs\SomeLib
int GetSVNVersionAndBranch(char *pFileName, char *pOutBranch, bool bTrimToDir);