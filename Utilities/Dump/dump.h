#pragma once

#include "OS/OSList.h"

typedef struct MiniDump MiniDump;

// Loads a dump file and returns an opaque handle
MiniDump * dumpLoad(const char * pDumpFileName);

// Destroys the opaque handle returned from dumpLoad
void dumpDestroy(MiniDump * pDump);

// Returns true if the dump is for a 64-bit process
bool dump64bit(MiniDump * pDump);

// Gets the Windows version of the dump
WindowsVersion dumpWindowsVersion(MiniDump * pDump);

// Gets a pointer to data from the dump at an address and of the specified size.
// Returns false if it's not in the dump.
bool dumpGetData(MiniDump * pDump, void * pData, U64 uAddress, size_t uSize);

// Gets an eArray of addresses where the specified data is found in the dump's memory.
U64 * dumpFindData(MiniDump * pDump, void * pData, size_t uDataLen);

void dumpPrintPointer(MiniDump * pDump, U64 uAddress);