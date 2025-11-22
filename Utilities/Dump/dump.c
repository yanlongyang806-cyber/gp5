#include "dump.h"

#include <windows.h>
#include <dbghelp.h>
#include "file.h"

#include "dump_c_ast.h"

AUTO_STRUCT;
typedef struct MiniDumpMemoryInfo
{
	U64 uSize;
	U64 uStartAddress;
} MiniDumpMemoryInfo;

AUTO_STRUCT;
typedef struct MiniDump
{
	U32 uExceptionCode;
	bool b64bit;
	U32 uMajorVersion;
	U32 uMinorVersion;
	U64 uTotalMemorySize;
	U64 uMemoryStartAddr;
	EARRAY_OF(MiniDumpMemoryInfo) eaMemoryInfo;
	FILE * pFile; NO_AST
} MiniDump;

bool dumpValid(MINIDUMP_HEADER *pHeader)
{
	return !strncmp("MDMP", (char*)&pHeader->Signature, 4);
}

MiniDump * dumpLoad(const char * pDumpFileName)
{
	MINIDUMP_HEADER header;
	unsigned int iCurStream = 0;
	MiniDump * pDump = NULL;

	pDump = StructCreate(parse_MiniDump);

	pDump->pFile = fopen(pDumpFileName, "rb");
	if (!pDump->pFile)
	{
		dumpDestroy(pDump);
		return NULL;
	}

	fread(&header, sizeof(header), 1, pDump->pFile);

	if (!dumpValid(&header))
	{
		dumpDestroy(pDump);
		return NULL;
	}

	for (iCurStream = 0; iCurStream < header.NumberOfStreams; iCurStream++)
	{
		MINIDUMP_DIRECTORY dir;

		fseek(pDump->pFile, header.StreamDirectoryRva + iCurStream * sizeof(MINIDUMP_DIRECTORY), SEEK_SET);
		fread(&dir, sizeof(dir), 1, pDump->pFile);
		fseek(pDump->pFile, dir.Location.Rva, SEEK_SET);
		switch (dir.StreamType)
		{
		case Memory64ListStream:
			{
				MINIDUMP_MEMORY64_LIST list;
				unsigned int iCurListItem;

				fread(&list, sizeof(list), 1, pDump->pFile);

				assertmsg(!pDump->uMemoryStartAddr, "Multiple memory streams found; not handled."); // not sure if this is possible?
				pDump->uMemoryStartAddr = list.BaseRva;

				for (iCurListItem = 0; iCurListItem < list.NumberOfMemoryRanges; iCurListItem++) {
					MINIDUMP_MEMORY_DESCRIPTOR64 desc;
					MiniDumpMemoryInfo * pDumpMemoryInfo = StructCreate(parse_MiniDumpMemoryInfo);

					fread(&desc, sizeof(desc), 1, pDump->pFile);

					pDumpMemoryInfo->uSize = desc.DataSize;
					pDumpMemoryInfo->uStartAddress = desc.StartOfMemoryRange;

					eaPush(&pDump->eaMemoryInfo, pDumpMemoryInfo);
					pDump->uTotalMemorySize += pDumpMemoryInfo->uSize;
				}
			}
			break;
		case ExceptionStream:
			{
				MINIDUMP_EXCEPTION_STREAM except;
				fread(&except, sizeof(except), 1, pDump->pFile);
				pDump->uExceptionCode = except.ExceptionRecord.ExceptionCode;
			}
			break;
		case SystemInfoStream:
			{
				MINIDUMP_SYSTEM_INFO info;
				fread(&info, sizeof(info), 1, pDump->pFile);
				pDump->b64bit = info.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
				pDump->uMajorVersion = info.MajorVersion;
				pDump->uMinorVersion = info.MinorVersion;
			}
			break;
		}
	}

	return pDump;
}

void dumpDestroy(MiniDump * pDump)
{
	if (pDump->pFile)
	{
		fclose(pDump->pFile);
		pDump->pFile = NULL;
	}
	StructDestroy(parse_MiniDump, pDump);
}

bool dump64bit(MiniDump * pDump)
{
	return pDump->b64bit;
}

WindowsVersion dumpWindowsVersion(MiniDump * pDump)
{
	switch (pDump->uMajorVersion)
	{
	case 6:
		switch (pDump->uMinorVersion)
		{
		case 1: return Version_Windows7;
		case 0: return Version_WindowsServer2008;
		}
		break;
	case 5:
		switch (pDump->uMinorVersion)
		{
		case 2: return Version_WindowsServer2003;
		case 1: return Version_WindowsXP;
		case 0: return Version_Windows2000;
		}
		break;
	}
	return Version_WindowsUnknown;
}

bool dumpGetData(MiniDump * pDump, void * pData, U64 uAddress, size_t uSize)
{
	static unsigned int uLastFoundIndex = 0;
	static U64 uLastOffset = 0;

	U64 uOffset = uLastOffset;
	unsigned int uNumRanges = eaSize(&pDump->eaMemoryInfo);
	unsigned int uCurRange = MIN(uLastFoundIndex, uNumRanges - 1);

	if (!uNumRanges) return false;

	do
	{
		MiniDumpMemoryInfo * pRange = pDump->eaMemoryInfo[uCurRange];

		if (uAddress >= pRange->uStartAddress && uAddress + uSize <= pRange->uStartAddress + pRange->uSize)
		{
			uLastFoundIndex = uCurRange;
			uLastOffset = uOffset;
			uOffset += uAddress - pRange->uStartAddress;
			fseek(pDump->pFile, pDump->uMemoryStartAddr + uOffset, SEEK_SET);
			fread(pData, uSize, 1, pDump->pFile);
			return true;
		}

		uOffset += pRange->uSize;
		uCurRange++;
		if (uCurRange >= uNumRanges)
		{
			uCurRange = 0;
			uOffset = 0;
		}
	} while (uCurRange != uLastFoundIndex);

	return false;
}

U64 * dumpFindData(MiniDump * pDump, void * pData, size_t uDataLen)
{
	U64 * eaAddr = NULL;
	U64 uAddr = 0;
	
	fseek(pDump->pFile, pDump->uMemoryStartAddr, SEEK_SET);

	EARRAY_CONST_FOREACH_BEGIN(pDump->eaMemoryInfo, iCurRange, iNumRanges);
	{
		MiniDumpMemoryInfo * pRange = pDump->eaMemoryInfo[iCurRange];
		U8 * pMemory = malloc(pRange->uSize);
		U8 * pCurPtr = pMemory;
		U8 * pEndPtr = pMemory + pRange->uSize - uDataLen;

		if (pEndPtr < pCurPtr)
		{
			free(pMemory);
			continue; // The range is too small
		}

		fread(pMemory, pRange->uSize, 1, pDump->pFile);

		while (pCurPtr != pEndPtr)
		{
			if (!memcmp(pCurPtr, pData, uDataLen))
			{
				eai64Push(&eaAddr, pCurPtr - pMemory + pRange->uStartAddress);
			}

			pCurPtr++;
		}

		free(pMemory);
	}
	EARRAY_FOREACH_END;

	return eaAddr;
}

void dumpPrintPointer(MiniDump * pDump, U64 uAddress)
{
	if (dump64bit(pDump)) printf("%016llx", uAddress);
	else printf("%08x", uAddress);
}

#include "dump_c_ast.c"