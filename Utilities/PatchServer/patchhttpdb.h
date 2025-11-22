// Direct, high-performance HTTP access to files in the PatchDB

#ifndef CRYPTIC_PATCHHTTPDB_H
#define CRYPTIC_PATCHHTTPDATABASE_H

// Root path of HTTP database access
#define PATCHHTTPDATABASE_ROOT "/database"

typedef struct HttpRequest HttpRequest;
typedef struct NetLink NetLink;

// Handle a request for "/database"
void patchHttpDbHandleRequestIndex(HttpRequest *request);

// Handle a request for "/database/*"
void patchHttpDbHandleRequest(HttpRequest *request);

// Process pending HTTP requests.
void patchHttpDbTick(void);

// Notify patchHttpDb code that a NetLink has disconnected.
void patchHttpDbDisconnect(NetLink *link, void *pLinkUserData);

// Return true if this HTTP NetLink is being processed by patchHttpDb.
bool patchHttpDbIsOurNetLink(NetLink *link);

// Total number of bytes sent
U64 patchHttpDbBytesSent(void);

// Total number of header bytes sent (total - payload)
U64 patchHttpDbBytesSentOverhead(void);

// Number of HTTP request bytes received from clients
U64 patchHttpDbBytesReceived(void);

#endif  // CRYPTIC_PATCHHTTPDATABASE_H
