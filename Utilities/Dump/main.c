#include "utilitiesLib.h"
#include "cmdparse.h"
#include "dump.h"
#include "heap.h"
#include "earray.h"
#include "structDefines.h"
#include "OSList_h_ast.h"

static char gpDumpFileName[CRYPTIC_MAX_PATH] = {0};
AUTO_CMD_STRING(gpDumpFileName, Dump) ACMD_CMDLINE;

// Analyze the heap.  Will only work for Windows Server 2003 32-bit processes.
static U64 guAnalyzeHeap = 0;
AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(AnalyzeHeap);
void SetHeapAddress(const char * pAddress)
{
	sscanf(pAddress, "%llx", &guAnalyzeHeap);
}

// Similar to "s -[1]d 0 0fffffff ptr" in WinDbg, but finds more results for some reason
static U64 guFindPointer = 0;
AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(FindPointer);
void FindPointer(const char * pAddress)
{
	sscanf(pAddress, "%llx", &guFindPointer);
}

static bool DumpHeapAccessFunction(HeapAddress uHeapAddr, void * pOut, HeapSize uSize, void * pUserData)
{
	return dumpGetData(pUserData, pOut, uHeapAddr, uSize);
}

int main(int argc, char * argv[])
{
	MiniDump * pDump = NULL;

	setCavemanMode();

	DO_AUTO_RUNS;

	cmdParseCommandLine(argc, argv);

	if (!gpDumpFileName[0])
	{
		printf("Usage: %s -Dump [dumpfile]\n", argv[0]);
		return EXIT_FAILURE;
	}

	// Load the dump
	printf("Loading %s... ", gpDumpFileName);
	pDump = dumpLoad(gpDumpFileName);
	if (!pDump)
	{
		printf("\nCould not load dump file: %s\n", argv[0]);
		return EXIT_FAILURE;
	}
	printf("Done.\n");

	// Print interesting things about the dump
	printf("Dump information:\n");
	printf("\tWindows Version: %s\n",
		StaticDefineIntRevLookupNonNull(WindowsVersionEnum, dumpWindowsVersion(pDump)));
	printf("\tApplication architecture: %s\n",
		dump64bit(pDump) ? "64bit" : "32bit");

	// Look for a pointer
	if (guFindPointer)
	{
		U64 * eaAddr = NULL;

		printf("Looking for ");
		dumpPrintPointer(pDump, guFindPointer);
		printf("... ");
		
		eaAddr = dumpFindData(pDump, &guFindPointer, dump64bit(pDump) ? 8 : 4);
		printf("Done.\n");

		if (eaAddr)
		{
			unsigned int uCurAddr;

			printf("Results:\n");
			
			for (uCurAddr = 0; uCurAddr < eai64USize(&eaAddr); uCurAddr++)
			{
				dumpPrintPointer(pDump, eaAddr[uCurAddr]);
				printf("\n");
			}
		}
		else
		{
			printf("No results found.\n");
		}

		eai64Destroy(&eaAddr);
	}

	// Analyze the heap
	if (guAnalyzeHeap)
	{
		HeapFlags flags = 0;
		if (dump64bit(pDump)) flags |= HF_64_BIT;

		printf("Verifying heap...\n");
		HeapVerify(guAnalyzeHeap, dumpWindowsVersion(pDump), flags, DumpHeapAccessFunction, pDump);
		printf("Done.\n");
	}

	dumpDestroy(pDump);
	_getch();

	return EXIT_SUCCESS;
}