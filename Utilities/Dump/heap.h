#pragma once

#include "OS/OSList.h"

// Change this to U64 for 64-bit dumps
typedef U64 HeapAddress;
typedef U64 HeapSize;

typedef bool (*HeapAccessFunction)(HeapAddress uHeapAddr, void * pOut, HeapSize uSize, void * pUserData);

typedef enum {
	HF_64_BIT = BIT(0)
} HeapFlags;

void HeapVerify(HeapAddress uHeapAddr, WindowsVersion eVersion, HeapFlags flags, HeapAccessFunction pAccessor, void * pAccessorUserData);