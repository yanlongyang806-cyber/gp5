#pragma once

typedef struct Checksum
{
	S64		size;
	U32		values[4];
} Checksum;

// May restart process, fills in current EXE's checksum info
void selfPatchStartup(SA_PRE_NN_FREE SA_POST_NN_VALID Checksum *checksum);

// Replace the running EXE with the new one, and restart (restarts twice)
void selfPatch(const void *new_exe_data, int new_exe_data_size);
