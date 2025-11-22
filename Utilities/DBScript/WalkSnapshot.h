#pragma once

#include "objContainerIO.h"

void WalkSnapshot(GlobalType type, const char *snapshotFilename, const char *offlineFilename, int numThreads, DumpLoadedContainerCallback func);