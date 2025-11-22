#pragma once

#include "pcl_typedefs.h"

typedef struct PatcherFileHistory PatcherFileHistory;

// Returns != 0 if they did something and we desire to be called again
int patchmeStatShow(const char *fname, bool graphical, PatcherFileHistory *history);
