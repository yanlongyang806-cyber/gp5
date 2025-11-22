#ifndef CRYPTIC_PATCHUPDATE_H
#define CRYPTIC_PATCHUPDATE_H

#include "pcl_typedefs.h"

typedef struct NetComm NetComm;
typedef struct ServerConfig ServerConfig;

// Print some PCL xfer status information.
void cmd_showXferStats(void);

// Low volume update logging
#define patchupdateLog(fmt, ...) patchupdateLog_dbg(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void patchupdateLog_dbg(FORMAT_STR char const *fmt, ...);

// High volume update logging
#define patchupdateLogVerbose(fmt, ...) patchupdateLogVerbose_dbg(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void patchupdateLogVerbose_dbg(FORMAT_STR char const *fmt, ...);

void patchupdateSendUpdateStatus(const char *status);

// Start and maintain PCL connection with parent server.
// This should be called once per tick.
void patchupdateProcess(void);

// Return true if this server is not a master server.
bool patchupdateIsChildServer(void);

// Return true if this server is a child and is connected to a parent.
bool patchupdateIsConnectedToParent(void);

// Return true if this server is a child and is in the process on connecting or reconnecting to a parent.
bool patchupdateIsConnectingToParent(void);

// Disconnect from the parent server, if connected.
void patchupdateDisconnect(void);

// Initialize updating.
void patchupdateInit(ServerConfig *config);

// Notify the parent that a child is activating.
void patchupdateNotifyActivate(const char *name, const char *category, const char *parent);

// Notify the parent that a child is deactivating.
void patchupdateNotifyDeactivate(const char *name);

// Send updating status to the parent.
void patchupdateNotifyUpdateStatus(const char *name, const char *status);

// Notify the parent that a client has accessed a view.
void patchupdateNotifyView(const char * project, const char * name, U32 ip);

// Get PCL max_net_bytes.
U32 patchupdateGetMaxNetBytes(void);

// Set PCL max_net_bytes.
void patchupdateSetMaxNetBytes(U32 max_net_bytes);

// Set comm used for PCL connections.
void patchupdateSetUpdateComm(NetComm *comm);

// Report a PCL error.
void patchupdateError(PCL_ErrorCode error, const char *error_details);

// Get the current mirroring status.
const char *patchupdateUpdateStatus(void);

#endif  // CRYPTIC_PATCHUPDATE_H
