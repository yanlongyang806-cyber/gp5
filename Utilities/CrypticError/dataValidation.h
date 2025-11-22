#pragma once

#include "wininclude.h"

bool appendDataValidationTrivia(HANDLE hProcess, const char *dataDir, const char *dataFilename, char **estrOutput, bool *bAppendFilename);
