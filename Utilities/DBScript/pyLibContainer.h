#pragma once

#include "objContainerIO.h"
#include "tokenstore.h"
#include "file.h"
#include "timing.h"

void pyInitDBScriptModule(const char *pScriptFilename, const char *pSnapshotFilename);

void pyBegin();
void pyEnd();

bool pyLibContainerInit(PyObject *pMainModule);
void pyLibContainerShutdown();
bool pyProcessContainer(Container *con, U32 uContainerModifiedTime);
