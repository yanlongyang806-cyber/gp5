#pragma once

#include "stdio.h"
#include "stdlib.h"
#include "memory.h"
#include "string.h"


#define offsetof(s,m)   (size_t)&reinterpret_cast<const volatile char&>((((s *)0)->m))
#define MINMAX(a,min,max) ((a) < (min) ? (min) : (a) > (max) ? (max) : (a))
#define verify(exp) (exp)

typedef struct EString
{
	char header[4];				// should contain "ESTR"
	unsigned int bufferLength;
	unsigned int stringLength;
	char str[1];
} EString;

#define EStrHeaderSize offsetof(EString, str)
#define EStrTerminatorSize 1



#define ESTR_SHRINK_AMOUNT (EStrHeaderSize - EStrTerminatorSize) // Fudge total allocation size to powers of 2
#define ESTR_DEFAULT_SIZE (64 - ESTR_SHRINK_AMOUNT)
#define ESTR_HEADER "ESTR"


unsigned int estrReserveCapacity_dbg(char** str, unsigned int reserveSize, const char *caller_fname, int line);


__forceinline static void estrTerminateString(EString* estr)
{
	estr->str[estr->stringLength] = 0;
}

__forceinline static EString* estrFromStr_sekret(char* str)
{
	return (EString*)(str - EStrHeaderSize);
}

static __forceinline void estrConcat_dbg_inline(char** dst, const char* src, unsigned int srcLength, const char *caller_fname, int line)
{
	EString* estr;
	if(!dst)
		return;
	if(*dst){
		estr = estrFromStr_sekret(*dst);
		if(estr->bufferLength < estr->stringLength + srcLength + EStrTerminatorSize)
		{
			estrReserveCapacity_dbg(dst, estr->stringLength + srcLength, caller_fname, line);
			estr = estrFromStr_sekret(*dst);
		}
	}else{
		estrReserveCapacity_dbg(dst, srcLength, caller_fname, line);
		estr = estrFromStr_sekret(*dst);
	}

	memcpy(estr->str + estr->stringLength, src, srcLength);
	estr->stringLength += srcLength;
	estrTerminateString(estr);
}


static __forceinline void estrAppend2_dbg_inline(char** dst, const char* src, const char *caller_fname, int line)
{
	if(!src)
		return;

	estrConcat_dbg_inline(dst, src, (int)strlen(src), caller_fname, line);
}


unsigned int estrPrintf_dbg(char** str, const char *caller_fname, int line,  const char* format, ...);
#define estrPrintf(str, fmt, ...) estrPrintf_dbg(str, __FILE__, __LINE__, fmt, __VA_ARGS__)

unsigned int estrConcatf_dbg(char** str, const char *caller_fname, int line, const char* format, ...);
#define estrConcatf(str, fmt, ...) estrConcatf_dbg(str, __FILE__, __LINE__, fmt, __VA_ARGS__)


void estrDestroy( char** str);
void estrClear( char** str);
unsigned int estrLength(const char* const * str);

void estrCopy2_dbg( char** dst,  const char* src, const char *caller_fname, int line);
#define estrCopy2(dst, src) estrCopy2_dbg(dst, src, __FILE__, __LINE__)

