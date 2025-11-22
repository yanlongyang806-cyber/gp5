#pragma once

#include "GlobalTypeEnum.h"
#include "luaScriptLib.h"

bool luaWalkSnapshot(GlobalType type, char *luaScriptFilename, char *snapshotFilename, char *offlineFilename, int numThreads);
