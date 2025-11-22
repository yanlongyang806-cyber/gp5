 // sxfer - Striped transfer: File transfer for long and fat pipes

#include "cmdparse.h"
#include "ConsoleDebug.h"
#include "crypt.h"
#include "CrypticPorts.h"
#include "earray.h"
#include "file.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "logging.h"
#include "MemoryMonitor.h"
#include "net.h"
#include "sxfer.h"
#include "sysutil.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "windefinclude.h"

// If true, use public port.
static bool s_public = false;
AUTO_CMD_INT(s_public, public);

// Receive file from this host.
static char *s_from_host = NULL;
AUTO_CMD_ESTRING(s_from_host, from);

// File to send.
static char *s_file = NULL;
AUTO_CMD_ESTRING(s_file, file);

// Port to use.
static U32 s_port = 0;
AUTO_CMD_INT(s_port, port);

// Compute the checksum of a file, up to a size.
U32 computeChecksum(FILE *fp, U64 size)
{
	char buf[1024];
	U64 len;
	U64 read_so_far = 0;
	U32 checksum;

	PERFINFO_AUTO_START_FUNC();

	// Loop, updating the checksum, until we're at the specified size.
	cryptAdler32Init();
	while (read_so_far < size && (len = fread(buf, 1, MIN(sizeof(buf), size - read_so_far), fp)))
	{
		read_so_far += len;
		cryptAdler32Update(buf, len);
	}

	checksum = cryptAdler32Final();
	PERFINFO_AUTO_STOP_FUNC();
	return checksum;
}

// Called on fatal errors.
static void fatalErrorCallback(ErrorMessage *errMsg, void *userdata)
{
	const char *type = userdata;
	printf("\n\n%s aborted: %s", type, errMsg->estrMsg);
	exit(1);
}

// Add a data link.
static void debugAddDataLink(void)
{
	openDataLink();
	printf("\nNow using %d data links.\n", receiverDataLinkCount());
}

// xfer debug console
static ConsoleDebugMenu debug_options[] = {
	{'+', "Add data link", debugAddDataLink},
	{0}
};


// sxfer entry point
int main(int argc, char *argv[])
{
	WAIT_FOR_DEBUGGER
	EXCEPTION_HANDLER_BEGIN

	DO_AUTO_RUNS
	setDefaultAssertMode();

	// Starting banner
	loadstart_printf(
		"sxfer - Striped Transfer\n"
		"  Cryptic experimental large file transfer for long and fat pipes\n"
		"  IPv4 TCP, protocol version %d, branch %s, rev %d\n\n"
		"Initializing...\n", SXFER_PROTOCOL_VERSION, gBuildBranch, gBuildVersion);

	// Initialize everything.
	memMonitorInit();
	gimmeDLLDisable(1);
	fileDisableAutoDataDir();  // FolderCacheChooseMode();
	fileAllPathsAbsolute(true);
	utilitiesLibStartup();
	logSetDir("sxfer");
	autoTimerInit();
	cmdParseCommandLine(argc, argv);

	// Validate command line.
	if (!s_from_host && !s_file || s_from_host && s_file)
	{
		printf("\nxfer help\n");
		printf("On the machine that has the file to be downloaded, run the following:\n");
		printf("  sxfer -file <filename>\n");
		printf("On the machine that will be doing the downloading, run the following\n");
		printf("  sxfer -from <hostname>\n\n");
		printf("Other useful parameters:\n");
		printf("  -public   : use a public port rather than a Cryptic private port\n");
		printf("  -port <n> : use specific port number\n");
		printf("  All standard Cryptic command-line parameters work too.\n");
		return 2;
	}

	// Capture fatal errors.
	FatalErrorfSetCallback(fatalErrorCallback, s_file ? "Send" : "Receive");

	// Set port.
	if (!s_port)
	{
		if (s_public)
			s_port = SXFER_PUBLIC_PORT;
		else
			s_port = SXFER_PRIVATE_PORT;
	}
	loadend_printf("Initialization done.");

	// Set up debug console.
	eaPush(GetDefaultConsoleDebugMenu(), debug_options);

	// Initiate main operation, sending or receiving.
	if (s_from_host)
		receiveFile(s_from_host, s_port);
	else
		sendFile(s_port, s_file);

	EXCEPTION_HANDLER_END

	return 0;
}
