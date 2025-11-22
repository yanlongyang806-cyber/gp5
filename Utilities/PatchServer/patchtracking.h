#ifndef CRYPTIC_PATCHTRACKING_H
#define CRYPTIC_PATCHTRACKING_H

typedef struct NetLink NetLink;
typedef struct Packet Packet;
typedef struct PatchClientLink PatchClientLink;
typedef struct PatchProject PatchProject;

// Initialize patch tracking.
void patchTrackingInit(void);

// Add a server to be tracked.
void patchTrackingAdd(const char *name, const char *category, const char *parent);

// Remove a server from tracking.
void patchTrackingRemove(const char *name);

// Handle status updates from children.
void patchTrackingUpdate(PatchClientLink *client, const char *name, const char *status);

// Scan our databases for any updates to be sent to the master.
void patchTrackingScanForUpdates(bool do_scan, unsigned duration);

// Return true if a path is completely updated by all child servers.
bool patchTrackingIsCompletelyUpdatedPath(PatchProject *project, const char *path,
										  const char **include_categories, const char **exclude_categories);

#endif  // CRYPTIC_PATCHTRACKING_H
