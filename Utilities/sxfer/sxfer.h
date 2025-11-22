// sxfer internal interface.

#ifndef CRYPTIC_SXFER_H
#define CRYPTIC_SXFER_H

#include "net.h"

// Protocol version
#define SXFER_PROTOCOL_VERSION 1

// Size of data chunks
#define SXFER_PROTOCOL_CHUNK_SIZE (1024*1024)

// Control packets.  Only add to the end to maintain compatibility.
enum {
	SXFER_TORECEIVER_SET_FILEINFO = COMM_MAX_CMD,
	SXFER_TOSENDER_REQ_CHECKSUM,
	SXFER_TORECEIVER_SET_CHECKSUM,
};

// Wait for someone to connect, then send them a file.
void sendFile(U16 port, const char *file);

// Connect to remote host, then receive file.
void receiveFile(const char *from_host, U16 port);

// Compute the checksum of a file, up to a size.
U32 computeChecksum(FILE *fp, U64 size);

// The number of active data download links.
int receiverDataLinkCount(void);

// Open another data link.
void openDataLink(void);

#endif  // CRYPTIC_SXFER_H
