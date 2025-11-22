#ifndef CRYPTIC_PATCHMIRRORING_H
#define CRYPTIC_PATCHMIRRORING_H

typedef struct PCL_Client PCL_Client;

// Get the current mirroring status.
const char *patchmirrorUpdateStatus(PCL_Client *client);

// Start and maintain sync process with parent server.
// This is called by patchupdate.  It returns true if calling this function again might yield additional progress.
// continuing is true if this function is being called a second time, so the timeout should not be reset
bool patchmirroringMirrorProcess(PCL_Client *client, bool continuing);

// Force the next update to be a full update.
void patchmirroringNextMirrorForceFull(void);

// Manually start mirroring.
void patchmirroringRequestMirror(void);

// Reset the mirroring status.
void patchmirroringResetConnection(void);

// Initialize updating.
void patchmirroringInit(void);

// Return false if mirroring is actually active, as opposed to just waiting for notification of changes.
bool patchmirroringIsMirroringIdle(void);

#endif  // CRYPTIC_PATCHMIRRORING_H
