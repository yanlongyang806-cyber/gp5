// General Patch Server HTTP serving

#ifndef _PATCHHTTP_H
#define _PATCHHTTP_H

typedef struct NetLink NetLink;
typedef struct PatchFile PatchFile;

// Initialize HTTP serving code.
void patchHttpInit(void);

// Process HTTP events.
void patchHttpTick(void);

void patchserverLoadForHttp(NetLink *link, PatchFile *patch, bool uncompressed);

#endif