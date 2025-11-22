// Send a file to a remote peer (that connects to us)

#include "endian.h"
#include "error.h"
#include "file.h"
#include "net.h"
#include "sxfer.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "ThreadManager.h"
#include "utilitiesLib.h"
#include "utils.h"

// File handle to send.
static FILE *s_infile;

// Size of file to send.
static U64 s_file_size;

// Current read position of infile.
static U64 s_infile_position;

// The NetListen object we're waiting on.
static NetListen *s_wait_listen;

// Control link
static NetLink *s_send_link;

// Mutex for reading from the file.
static CRITICAL_SECTION s_file_mutex;

// If this is set, it's time to exit.
static bool s_serving_done = false;

// Read a chunk from a file.
static U64 readChunk(char chunk[SXFER_PROTOCOL_CHUNK_SIZE], U64 *chunk_size)
{
	size_t bytes_read;
	U64 position;

	PERFINFO_AUTO_START_FUNC();

	// Read chunk from file.
	EnterCriticalSection(&s_file_mutex);
	bytes_read = fread(chunk, 1, SXFER_PROTOCOL_CHUNK_SIZE, s_infile);
	position = s_infile_position;
	s_infile_position += bytes_read;
	LeaveCriticalSection(&s_file_mutex);

	// Check if we're at the end of the file.
	if (!bytes_read)
	{
		if (s_infile_position != s_file_size)
			FatalErrorf("Unable to read whole file.");
		*chunk_size = 0;
		return 0;
	}

	// Return data.
	*chunk_size = bytes_read;
	PERFINFO_AUTO_STOP_FUNC();
	return position;
}

// The data thread reads a chunk then writes it to the data link.
static DWORD WINAPI dataThread(LPVOID lpParam)
{
	NetLink *link = lpParam;
	char *chunk;

	EXCEPTION_HANDLER_BEGIN

	chunk = malloc(SXFER_PROTOCOL_CHUNK_SIZE);

	for (;;)
	{
		U64 chunk_size;
		U64 chunk_position;
		Packet *pak;

		autoTimerThreadFrameBegin("dataThread");

		// Read chunk from file.
		chunk_position = readChunk(chunk, &chunk_size);

		// Check if we're done.
		if (!chunk_size)
			break;

		// Write to link.
		PERFINFO_AUTO_START("Write", 1);
		//verbose_printf("Sending pos %"FORM_LL"u - %"FORM_LL"u\n", chunk_position, chunk_size);
		pak = pktCreateRaw(link);
		chunk_position = endianSwapU64(chunk_position);
		pktSendBytesRaw(pak, &chunk_position, sizeof(chunk_position));
		pktSendBytesRaw(pak, chunk, chunk_size);
		pktSendRaw(&pak);
		PERFINFO_AUTO_STOP();

		autoTimerThreadFrameEnd();
	}

	free(chunk);

	EXCEPTION_HANDLER_END

	return 0;
}

// A receiver has connected to us, the file sender.
static void senderConnect(NetLink *link, void *user_data)
{
	static bool data_thread_connected = false;
	ManagedThread *t;

	// After the control link connects, switch to raw for data links.
	if (!s_send_link)
	{
		char ip[MAX_IP_STR];
		listenSetRequiredFlags(s_wait_listen, LINK_RAW|LINK_FORCE_FLUSH);
		s_send_link = link;
		loadend_printf("connection from %s", linkGetIpStr(link, ip, sizeof(ip)));
		return;
	}

	if (!data_thread_connected)
	{
		loadend_printf("done.");
		loadstart_printf("Serving file...");
		data_thread_connected = true;
	}

	// Create thread for each data link.
	t = tmCreateThread(dataThread, link);
}

// A receiver has disconnected from us, the file sender.
static void senderDisconnect(NetLink *link, void *user_data)
{
	if (link == s_send_link)
		s_serving_done = true;
}

// A receive has sent a packet to us, the file sender.
static void senderPacket(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	switch (cmd)
	{
		case SXFER_TOSENDER_REQ_CHECKSUM:
			{
				U64 size = pktGetU64(pkt);
				Packet *reply_pkt = pktCreate(link, SXFER_TORECEIVER_SET_CHECKSUM);
				loadstart_printf("\nPerforming checksum...");
				pktSendU32(reply_pkt, computeChecksum(s_infile, size));
				loadend_printf("done.");
				s_infile_position = size;
				pktSend(&reply_pkt);
			}
			break;
	}
}

// Wait for someone to connect, then send them a file.
void sendFile(U16 port, const char *file)
{
	U32 timer = timerAlloc();
	F32 step;
	U32 modified;
	Packet *pkt;

	// Initialize file mutex.
	InitializeCriticalSection(&s_file_mutex);

	// Open file.
	loadstart_printf("Opening \"%s\"...", file);
	s_infile = fopen(file, "rb");
	if (!s_infile)
	{
		FatalErrorf("Unable to open input file.");
		return;
	}
	s_file_size = fileGetSize64(s_infile);
	modified = fileLastChanged(file);

	loadend_printf("opened.");

	// Wait for incoming connection.
	loadstart_printf("Waiting for incoming connection...\n");
	s_wait_listen = commListen(commDefault(),
		LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_NO_ALERTS_ON_FULL_SEND_BUFFER | LINKTYPE_SIZE_20MEG,
		LINK_FORCE_FLUSH,
		port,
		senderPacket,
		senderConnect,
		senderDisconnect,
		0);
	if (!s_wait_listen)
		FatalErrorf("Unable to bind to port.");
	timerStart(timer);
	while (!s_send_link)
	{
		autoTimerThreadFrameBegin("control");
		commMonitor(commDefault());
		step = timerElapsedAndStart(timer);
		utilitiesLibOncePerFrame(0, 1);
		autoTimerThreadFrameEnd();
	}

	// Send file information.
	loadstart_printf("Negotiating with receiver...");
	pkt = pktCreate(s_send_link, SXFER_TORECEIVER_SET_FILEINFO);
	pktSendU32(pkt, SXFER_PROTOCOL_VERSION);
	pktSendString(pkt, getFileNameConst(file));
	pktSendU64(pkt, s_file_size);
	pktSendU32(pkt, modified);
	pktSend(&pkt);

	// Wait for the receiver to request the file.
	while (!s_serving_done)
	{
		autoTimerThreadFrameBegin("control");
		commMonitor(commDefault());
		step = timerElapsedAndStart(timer);
		utilitiesLibOncePerFrame(0, 1);
		autoTimerThreadFrameEnd();
	}

	loadend_printf("done.");
}
