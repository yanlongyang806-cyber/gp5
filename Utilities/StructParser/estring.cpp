#include "estring.h"
#include "utils.h"


#define estrToStr(estr)		((estr)->str)
#define estrFromStr(str)	((EString*)((str) - EStrHeaderSize))
#define cestrFromStr(str)	((const EString*)((str) - EStrHeaderSize))

#define VA_START(va, format)	{va_list va, *__vaTemp__ = &va;va_start(va, format)
#define VA_END()				va_end(*__vaTemp__);}

void estrClear(char** str)
{
	EString* estr;

	if(!str || !*str)
		return;

	estr = estrFromStr(*str);
	estr->stringLength = 0;
	estrTerminateString(estr);
}

void estrHeapCreate_dbg(char** str, unsigned int initSize, const char *caller_fname, int line)
{
	EString* estr;

	if(!str)
		return;

	
	if(0 == initSize)
		initSize = ESTR_DEFAULT_SIZE;

	estr = (EString*)malloc(EStrHeaderSize + EStrTerminatorSize + initSize);
	memset(estr,0,sizeof(EString));
	memcpy(estr->header, "ESTR", 4);
	estr->bufferLength = initSize;
	estr->stringLength = 0;
	*str = estrToStr(estr);

}

unsigned int estrReserveCapacity_dbg(char** str, unsigned int reserveSize, const char *caller_fname, int line)
{
	int index;
	int newObjSize;
	int newBufferSize;
	EString* estr;

	if(!str)
		return 0;
	if(!*str){
		estr = NULL;
	}else{
		estr = estrFromStr(*str);

		if(estr->bufferLength >= reserveSize + EStrTerminatorSize){
			return estr->bufferLength;
		}
	}

	// If the capacity is already larger than the specified reserve size,
	// the operation is already complete.

	index = highBitIndex(reserveSize + EStrHeaderSize + EStrTerminatorSize) + 1;
	if(32 == index)
	{
		newObjSize = 0xffffffff;	
		newBufferSize = newObjSize - EStrHeaderSize - EStrTerminatorSize;
		// I hope this never happens.  =)
		// Allocating 4gb of memory is always a bad idea.
	}
	else
	{
		newBufferSize = (1 << index) - (int)ESTR_SHRINK_AMOUNT; // Make actual allocation a power of 2
		newObjSize = newBufferSize + EStrHeaderSize + EStrTerminatorSize;
	}

	if (!estr)
	{
		estrHeapCreate_dbg(str, newBufferSize, caller_fname, line);
		estr = estrFromStr(*str);
	}
	else 
	{
		estr = (EString*)realloc(estr,newObjSize);

	}
	
	estr->bufferLength = newBufferSize;
	*str = estrToStr(estr);
	return newBufferSize;
}


void estrDestroy(char** str)
{
	EString* estr;

	if(!str || !*str)
		return;

	estr = estrFromStr(*str);

	free(estr);
			
	*str = NULL;
}


void estrInsert_dbg(char** dst, unsigned int insertByteIndex, const char* buffer, unsigned int byteCount, const char *caller_fname, int line)
{
	EString* estr;
	int verifiedInsertByteIndex;
	U32 dstNewLen;

	if(!dst || !buffer)
		return;
	dstNewLen = byteCount;
	if(!*dst){
		verifiedInsertByteIndex = 0;
	}else{
		estr = estrFromStr(*dst);
		verifiedInsertByteIndex = MINMAX(insertByteIndex, 0, estr->stringLength);
		dstNewLen += estr->stringLength;
	}
	estrReserveCapacity_dbg(dst, dstNewLen, caller_fname, line);

	estr = estrFromStr(*dst);	// String might have been reallocated.
	memmove(estr->str + verifiedInsertByteIndex+byteCount, estr->str + verifiedInsertByteIndex, estr->stringLength + EStrTerminatorSize - verifiedInsertByteIndex);
	memcpy(estr->str + verifiedInsertByteIndex, buffer, byteCount);
	estr->stringLength += byteCount;
}

void estrRemove(char** dst, unsigned int removeByteIndex, unsigned int byteCount)
{
	EString* estr;
	int verifiedRemoveByteIndex;

	if(!dst || !*dst)
		return;

	estr = estrFromStr(*dst);
	verifiedRemoveByteIndex = MINMAX(removeByteIndex, 0, estr->stringLength);

	// make sure the bytes to remove is valid. take a guess to the end of the string
	if( !verify( (verifiedRemoveByteIndex + byteCount) <= estr->stringLength ))
	{
		byteCount = estr->stringLength - verifiedRemoveByteIndex;
	}

	memmove(estr->str + verifiedRemoveByteIndex, estr->str + verifiedRemoveByteIndex + byteCount, estr->stringLength + EStrTerminatorSize - (verifiedRemoveByteIndex + byteCount));
	estr->stringLength -= byteCount;
	estrTerminateString(estr);

}

void estrConcat_dbg(char** dst, const char* src, unsigned int srcLength, const char *caller_fname, int line)
{
	estrConcat_dbg_inline(dst,src,srcLength,caller_fname,line);

}

unsigned int estrLength(const char* const* str)
{
	if(!str || !*str)
		return 0;

	return (cestrFromStr(*str))->stringLength;
}


void estrCreate_dbg(char** str, const char *caller_fname, int line)
{
	// By default, allocate the string from the heap.
	// Specify a 0 size string to have estrCreate() use the default string size.
	estrHeapCreate_dbg(str, 0, caller_fname, line);
}


unsigned int estrConcatfv_dbg(char** str, const char *caller_fname, int line, const char* format, va_list args)
{
	EString* estr;
	int printCount;

	if(!str)
		return 0;


	if(!*str)
		estrCreate_dbg(str, caller_fname, line);

	estr = estrFromStr(*str);

	// Try to print the string.
	printCount = _vsnprintf(estr->str + estr->stringLength, estr->bufferLength - estr->stringLength,
		format, args);
	if(printCount >= 0) {
		// Good!  It fit.
	} else {
		
		printCount = _vscprintf((char*)format, args);
		estrReserveCapacity_dbg(str, estr->stringLength + printCount + 1, caller_fname, line);
		estr = estrFromStr(*str);

		printCount = _vsnprintf(estr->str + estr->stringLength, estr->bufferLength - estr->stringLength,
			format, args);
	}

	estr->stringLength += printCount;

	estrTerminateString(estr);

	return printCount;
}

unsigned int estrPrintf_dbg(char** str, const char *caller_fname, int line, const char* format, ...)
{
	unsigned int count;
	VA_START(args, format);
	estrClear(str);
	count = estrConcatfv_dbg(str, caller_fname, line, format, args);
	VA_END();
	return count;
}

unsigned int estrConcatf_dbg(char** str, const char *caller_fname, int line, const char* format, ...)
{
	unsigned int count;
	VA_START(args, format);
	count = estrConcatfv_dbg(str, caller_fname, line, format, args);
	VA_END();
	return count;
}

void estrCopy2_dbg(char** dst, const char* src, const char *caller_fname, int line)
{
	if (!dst || src == *dst)
		return; // No work if source and dest are the same
	estrClear(dst);
	estrAppend2_dbg_inline(dst, src, caller_fname, line);
}