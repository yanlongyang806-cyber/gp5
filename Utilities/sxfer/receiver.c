// Receive a file from remote peer (that we connect to)

#include "endian.h"
#include "error.h"
#include "file.h"
#include "fileutil.h"
#include "mathutil.h"
#include "net.h"
#include "sxfer.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "utils.h"
#include "wininclude.h"

// Number of data links to start with.
#define SXFER_INITIAL_DATA_LINKS 1

// The xfer will stop if the write buffer would exceed this size.
#define SXFER_MAXIMUM_WRITE_BUFFER_SIZE 512*1024*1024  // 1/2 GB

// Remote host to connect to.
static const char *s_remote_host;

// Remote port to connect to.
static U16 s_remote_port;

// Output filename.
static char *s_filename;

// Final size of output file to be written.
static U64 s_file_size;

// Modified time of file to write.
static U32 s_file_modified = 0;

// Output file write position
static U64 s_file_position;

// Control link
static NetLink *s_receive_link;

// If true, we're done.
static bool s_file_receive_done = false;

// Buffer to be written to disk.
static char *s_write_buffer = NULL;

// Chunk index of s_write_buffer.
static char *s_write_buffer_bitmap = NULL;

// Output file
static FILE *s_outfile;

// Number of active data links.
static int s_data_links = 0;

// If true, the main download has started.
static bool s_download_started = false;

// Calculate our current download progress.
static void calculateProgress()
{
	static int timer = 0;  // Display timer
	static U32 ticks = 0;
	F32 duration = 0;

	// Don't do progress calculations until download proper has started.
	if (!s_download_started)
		return;

	if (ticks++ % 0xff == 0)
	{
		if (!timer || (duration = timerElapsed(timer)) > 10)
		{
			static U64 s_starting_position;
			static int start_timer;  // Total download process timer
			static U64 s_last_position;
			S64 rate;
			F32 rec_num, tot_num, act_num, rate_num;
			char *rec_units, *tot_units, *act_units, *rate_units;
			U32 rec_prec, tot_prec, act_prec, rate_prec;
			int elapsed;
			int eta;
	
			PERFINFO_AUTO_START_FUNC();
	
			// Reset timer.
			if (timer)
				timerStart(timer);
			else
			{
				timer = timerAlloc();
				start_timer = timerAlloc();
				s_starting_position = s_file_position;
				s_last_position = s_starting_position;
			}
	
			// Print progress.
			humanBytes(s_file_position, &rec_num, &rec_units, &rec_prec);
			humanBytes(s_file_size, &tot_num, &tot_units, &tot_prec);
			humanBytes(s_file_position - s_starting_position, &act_num, &act_units, &act_prec);
			if (duration)
				rate = round64((s_file_position - s_last_position)/duration);
			else
				rate = 0;
			s_last_position = s_file_position;
			humanBytes(rate, &rate_num, &rate_units, &rate_prec);
			elapsed = timerElapsed(start_timer);
			if (rate)
				eta = (s_file_size - s_file_position)/rate;
			else
				eta = 0;
			printf("%u%%"
				" %.*f%s"
				"/%.*f%s"
				", %.*f%s xferred"
				", %d:%02d elapsed"
				", %.*f%s/s"
				", %d:%02d remaining"
				"       \r",
				(unsigned)(s_file_size ? s_file_position*100/s_file_size : 0),
				rec_prec, rec_num, rec_units,
				tot_prec, tot_num, tot_units,
				act_prec, act_num, act_units,
				elapsed/60, elapsed%60,
				rate_prec, rate_num, rate_units,
				eta/60, eta%60);

			PERFINFO_AUTO_STOP_FUNC();
		}
	}
}

// Individual data link state.
typedef struct dataReceiverState
{
	char *data;
} dataReceiverState;

// Get the file position of a chunk.
static U64 getChunkPosition(const char *data)
{
	U64 position;
	memcpy(&position, data, sizeof(position));
	position = endianSwapU64(position);
	return position;
}

// Flush the write buffer, if possible.
static void flushBuffer()
{
	PERFINFO_AUTO_START_FUNC();

	while (estrLength(&s_write_buffer_bitmap) && s_write_buffer_bitmap[0])
	{
		U64 to_write;
		U64 written;
		bool last_chunk = false;

		// Write a chunk.
		to_write = SXFER_PROTOCOL_CHUNK_SIZE;
		if (to_write + s_file_position > s_file_size)
		{
			to_write = s_file_size - s_file_position;
			last_chunk = true;
		}
		written = fwrite(s_write_buffer, 1, to_write, s_outfile);
		s_file_position += written;

		// Check for write error.
		if (written != to_write)
		{
			FatalErrorf("Write error");
			return;
		}

		// Update buffers and bitmap.
		estrRemove(&s_write_buffer_bitmap, 0, 1);
		estrRemove(&s_write_buffer, 0, to_write);

		// If we're done, finish.
		if (last_chunk)
		{
			fclose(s_outfile);
			fileSetTimestamp(s_filename, s_file_modified);
			s_file_receive_done = true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Receive a complete chunk of data.
static void receiveChunk(const char *data)
{
	U64 index;
	U64 chunk_position;
	U64 buffer_offset;
	U64 next_chunk_offset;

	PERFINFO_AUTO_START_FUNC();

	// Get the chunk offset and validate it.
	chunk_position = getChunkPosition(data);
	if (chunk_position >= s_file_size)  // || chunk_position % SXFER_PROTOCOL_CHUNK_SIZE)  this is allowed by resumes
		FatalErrorf("Invalid offset received.");
	if (chunk_position > s_file_position + SXFER_MAXIMUM_WRITE_BUFFER_SIZE)
		FatalErrorf("Write buffer maximum size exceeded.");

	//verbose_printf("\nreceived %"FORM_LL"u\n", chunk_position);

	// Update the index.
	buffer_offset = chunk_position - s_file_position;
	index = buffer_offset/SXFER_PROTOCOL_CHUNK_SIZE;
	estrSetSize(&s_write_buffer_bitmap, MAX(estrLength(&s_write_buffer_bitmap), index + 1));
	s_write_buffer_bitmap[index] = true;

	// Calculate size of this chunk.
	next_chunk_offset = buffer_offset + SXFER_PROTOCOL_CHUNK_SIZE;
	if (next_chunk_offset > s_file_size - s_file_position)
		next_chunk_offset = s_file_size - s_file_position;

	// Resize buffer to accommodate this chunk.
	estrForceSize(&s_write_buffer, MAX(estrLength(&s_write_buffer), next_chunk_offset));

	// Write this chunk to the buffer.
	memcpy(s_write_buffer + buffer_offset, data + sizeof(U64), next_chunk_offset - buffer_offset);

	// Flush the buffer.
	flushBuffer();

	// Update progress meter.
	calculateProgress();

	PERFINFO_AUTO_STOP_FUNC();
}

// Is this the final chunk, with a size smaller than SXFER_PROTOCOL_CHUNK_SIZE?
static bool chunkIsRunt(const char *data)
{
	U64 position = getChunkPosition(data);
	return position + SXFER_PROTOCOL_CHUNK_SIZE > s_file_size;
}

// Return the size of this runt packet
static U64 runtSize(const char *data)
{
	U64 position = getChunkPosition(data);
	return s_file_size - position;
}

void openDataLink(void);

// We have connected to the file sender, with a data link.
static void receiverDataConnect(NetLink *link, void *user_data)
{
	linkSetMaxRecvSize(link, SXFER_PROTOCOL_CHUNK_SIZE * 4);
}

// The file sender has disconnected from us, with a data link.
static void receiverDataDisconnect(NetLink *link, void *user_data)
{
	char *disconnect_reason = NULL;

	// Report error.
	linkGetDisconnectReason(link, &disconnect_reason);
	printf("Data link disconnect error: %s\n", disconnect_reason);
	estrDestroy(&disconnect_reason);

	// FIXME: Figure out how to re-request blocks that will be missing now.
	FatalErrorf("Can't recover");

	// Reconnect.
	openDataLink();
}

// The file sender has sent a packet to us, the receiver, with a data link.
static void receiverDataPacket(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	dataReceiverState *state = user_data;
	char *data = pktGetStringRaw(pkt);
	U32 len = pktGetSize(pkt);

	// Append packet data to chunk.
	estrConcat(&state->data, data, len);

	//verbose_printf("  %d", estrLength(&state->data));

	// Check if we have a full chunk yet, and if so, flush it from the data link buffer.
	for (;;)
	{
		bool complete_chunk;  // True if this chunk is complete
		bool runt = false;
		U64 chunk_size_with_header;

		// Check if the data we've received so far is big enough to be full chunk.
		complete_chunk = estrLength(&state->data) >= SXFER_PROTOCOL_CHUNK_SIZE + sizeof(U64);

		// If it's not, see if it's a runt.
		if (!complete_chunk && estrLength(&state->data) > sizeof(U64))
		{
			runt = chunkIsRunt(state->data);
			complete_chunk = runt && estrLength(&state->data) >= runtSize(state->data) + sizeof(U64);
		}

		// If the chunk isn't complete, wait.
		if (!complete_chunk)
			break;																				// Exit.

		// Calculate chunk size.
		if (estrLength(&state->data) > sizeof(U64) + s_file_size - s_file_position)  // FIXME: This isn't right for multiple chunks.
			FatalErrorf("Too much data received");
		if (runt)
			chunk_size_with_header = runtSize(state->data) + sizeof(U64);
		else
			chunk_size_with_header = SXFER_PROTOCOL_CHUNK_SIZE + sizeof(U64);

		// Process the chunk.
		receiveChunk(state->data);

		// Remove chunk from buffer.
		estrRemove(&state->data, 0, chunk_size_with_header);
	}
}

// The number of active data download links.
int receiverDataLinkCount()
{
	return s_data_links;
}

// Open a single data link to the remote host.
AUTO_COMMAND;
void openDataLink(void)
{
	NetLink *link = commConnect(commDefault(),
		LINKTYPE_UNSPEC,
		LINK_RAW|LINK_FORCE_FLUSH,
		s_remote_host,
		s_remote_port,
		receiverDataPacket,
		receiverDataConnect,
		receiverDataDisconnect,
		sizeof(dataReceiverState *));
	if (!link)
	{
		FatalErrorf("Failed to connect a data link");
		return;
	}

	// Incremental data link count.
	++s_data_links;
}

// Initiate download.
static void startDownload()
{
	int i;

	// Start download.
	loadend_printf("Getting file metadata...done");
	loadstart_printf("Starting download...\n");
	s_download_started = true;
	
	// Open several links.
	for (i = 0; i != SXFER_INITIAL_DATA_LINKS; ++i)
		openDataLink();
}

// Open a file, and verify its checksum.
static FILE *openFileAndVerify(const char *filename, U64 size, U32 modified)
{
	FILE *outfile;

	// If the file already exists, verify the checksum so that we can resume.
	if (fileExists(filename))
	{
		U64 existing_size = fileSize64(filename);
		Packet *pkt;
		U64 local_size;

		// Check file size.
		if (existing_size > size)
		{
			FatalErrorf("File on disk is too large: %"FORM_LL"u bytes.", existing_size);
			return NULL;
		}
		if (existing_size == size && fileLastChanged(filename) == modified)
			printf("File seems up to date; verifying checksum...\n");
		else
			printf("File exists: attempting resume...\n");

		// Open file.
		outfile = fopen(filename, "r+b");
		if (!outfile)
		{
			FatalErrorf("Unable to open output file.");
			return NULL;
		}

		// Request checksum.
		pkt = pktCreate(s_receive_link, SXFER_TOSENDER_REQ_CHECKSUM);
		local_size = fileSize64(filename);
		pktSendU64(pkt, local_size);
		s_file_position = local_size;
		pktSend(&pkt);
		return outfile;
	}

	// Open new output file.
	outfile = fopen(filename, "wb");
	if (!outfile)
	{
		FatalErrorf("Unable to open output file.");
		return NULL;
	}

	// Start receiving.
	startDownload();

	return outfile;
}

// The file sender has disconnected from us.
static void receiverControlDisconnect(NetLink *link, void *user_data)
{
	FatalErrorf("Control link disconnected");
}

// The file sender has sent a packet to us, the receiver.
static void receiverControlPacket(Packet *pkt, int cmd, NetLink *link, void *user_data)
{

	switch (cmd)
	{
		case SXFER_TORECEIVER_SET_FILEINFO:
			{
				U32 version = pktGetU32(pkt);
				s_filename = pktMallocString(pkt);
				s_file_size = pktGetU64(pkt);
				s_file_modified = pktGetU32(pkt);

				printf(  "version %d\n  filename %s\n  size %"FORM_LL"u\n  modified %s\n\n",
					version,
					s_filename,
					s_file_size,
					timeGetLocalDateStringFromSecondsSince2000(timeGetSecondsSince2000FromWindowsTime32(s_file_modified)));

				assert(version == SXFER_PROTOCOL_VERSION);

				s_outfile = openFileAndVerify(s_filename, s_file_size, s_file_modified);
			}
			break;

		case SXFER_TORECEIVER_SET_CHECKSUM:
			{
				U32 original_checksum = pktGetU32(pkt);
				U32 computed_checksum;

				// Compute checksum.
				loadstart_printf("\nComputing checksum of local file...");
				computed_checksum = computeChecksum(s_outfile, s_file_size);
				loadend_printf("done.");

				// Check checksum.
				if (original_checksum != computed_checksum)
				{
					FatalErrorf("Unable to resume: file contents does not match.");
					return;
				}

				// If we have all of the file, we just need to update the timestamp.
				if (s_file_size == fileGetSize64(s_outfile))
				{
					fclose(s_outfile);
					fileSetTimestamp(s_filename, s_file_modified);
					s_file_receive_done = true;
					return;
				}

				// Start receiving.
				startDownload();
			}
			
			break;
	}
}

// Connect to remote host, then receive file.
void receiveFile(const char *from_host, U16 port)
{
	bool success;
	int timer = timerAlloc();
	F32 step;

	// Connect to remote host.
	loadstart_printf("Connecting to \"%s:%d\"...", from_host, port);
	s_receive_link = commConnect(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, from_host, port, receiverControlPacket, NULL, receiverControlDisconnect, 0);
	s_remote_host = from_host;
	s_remote_port = port;
	success = linkConnectWait(&s_receive_link, 0);
	if (!success)
	{
		FatalErrorf("Unable to connect.");
		return;
	}
	loadend_printf("connected.");

	// Make the console larger so it can see the full status.
	consoleSetSize(120, 9999, 60);

	// Negotiate, resume, and receive file data.
	loadstart_printf("Getting file metadata...\n");
	while (!s_file_receive_done)
	{
		autoTimerThreadFrameBegin("receiving");
		commMonitor(commDefault());
		step = timerElapsedAndStart(timer);
		utilitiesLibOncePerFrame(0, 1);
		calculateProgress();
		autoTimerThreadFrameEnd();
	}

	loadend_printf("\nFile transferred.");
}
