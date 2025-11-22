/*
 * The BRUTAL downloader
 */

//#include "BrutalDownload_c_ast.h"
#include "BlockEarray.h"
#include "estring.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "MemoryMonitor.h"
#include "ServerLib.h"
#include "sysutil.h"
#include "ThreadManager.h"
#include "timing.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "utilitieslib.h"
#include "utils.h"
#include "windefinclude.h"
#include "winutil.h"

#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

// State of a DownloadRequest
typedef enum DownloadState
{
	StateUnused = 0,
	StateQueued,
	StateDownloading,
	StateDone,
} DownloadState;

// Request to download a particular file
typedef struct DownloadRequest
{
	char path[256];
	char host[256];
	char filename[256];
	DownloadState state;
	U16 port;
} DownloadRequest;

// All files to be downloaded
static DownloadRequest *beaRequests = NULL;

// Total number of files requested so far.
static unsigned total_requests = 0;

// Mutex for beaRequests
static CRITICAL_SECTION mutex;

// Number of threads that there are.
static U32 thread_count = 0;

// Signaled when finished.
static HANDLE done_event;

static void RemoveDoubleSlashes(char *path)
{
	char *slashes;
	while ((slashes = strstr(path, "//")))
		memmove(slashes, slashes + 1, strlen(slashes));
}

static void CanonicalizePath(char *path)
{
	char *dotdot, *dot;
	RemoveDoubleSlashes(path);
	while ((dotdot = strstr(path + 1, "/..")))
	{
		char *parent;
		*dotdot = 0;
		parent = strrchr(path + 1, '/');
		*dotdot = '/';
		memmove(parent + 1, dotdot + 3, strlen(dotdot + 3) + 1);
		RemoveDoubleSlashes(path);
	}
	while ((dot = strstr(path + 1, "/./")))
		memmove(dot + 1, dot + 3, strlen(dot + 3) + 1);
}

static bool MakeDownloadRequest(DownloadRequest *request, const char *ref, const char *current_host, const char *current_path)
{
	const char http_prefix[] = "http://";

	PERFINFO_AUTO_START_FUNC();

	if (strStartsWith(ref, http_prefix))
	{
		const char *host = ref += sizeof(http_prefix) - 1;
		const char *port = strchr(host, ':');
		const char *path = strchr(host, '/');
		U16 portnum = 80;

		if (port)
		{
			++port;
			portnum = strtoul(port, NULL, 10);
		}
		request->port = portnum;

		if (!path)
		{
			PERFINFO_AUTO_START_FUNC();
			return false;
		}

		if (host && strnicmp(current_host, host, path - host))
		{
			Errorf("Ignoring cross-host link \"%s\"", ref);
			PERFINFO_AUTO_START_FUNC();
			return false;
		}
		strcpy(request->host, host);

		strcpy(request->path, path);
		devassert(request->path[0] == '/');
	}
	else
	{
		request->port = 80;
		assert(current_host);
		assert(current_path);
		strcpy(request->host, current_host);
		sprintf(request->path, "/%s/%s", current_path, ref);
	}

	CanonicalizePath(request->path);

	strcpy(request->filename, request->path);

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

static bool QueueDownloadRequest(const char *ref, const char *current_host, const char *current_path)
{
	int index;
	bool success;

	// Get the slot to use.
	EnterCriticalSection(&mutex);
	if (total_requests == (unsigned)beaSize(&beaRequests))
		beaSetSize(&beaRequests, beaSize(&beaRequests) * 4);
	index = total_requests;
	LeaveCriticalSection(&mutex);

	// Create the download request.
	success = MakeDownloadRequest(&beaRequests[index], ref, current_host, current_path);
	if (!success)
		return false;

	// Incremental request count.
	EnterCriticalSection(&mutex);
	++total_requests;
	LeaveCriticalSection(&mutex);

	return true;
}

static int FindStart()
{

}

static DWORD WINAPI Downloader(LPVOID lpParam)
{
	EnterCriticalSection(&mutex);
	++thread_count;
	LeaveCriticalSection(&mutex);


	// stuff

	return 0;
}

// Spawn a downloader connection.
static void SpawnThread()
{
	ManagedThread *mtServer = tmCreateThread(Downloader, NULL);
}

int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	bool success;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV

	WAIT_FOR_DEBUGGER

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	utilitiesLibStartup();
	autoTimerInit();

	if (argc != 2 || !strStartsWith(argv[1], "http://"))
	{
		printf("\n\nThe BRUTAL downloader\nVersion: %s\n\nUsage: %s http://example.com/something\n\nUse with care...\n", GetUsefulVersionString(), argv[0]);
		return 1;
	}

	// Initialize.
	InitializeCriticalSection(&mutex);
	beaSetSize(&beaRequests, 100000);
	done_event = CreateEvent(NULL, false, false, NULL);

	// Request first file.
	success = QueueDownloadRequest(argv[1], NULL, NULL);
	SpawnThread();

	// Wait for download completion.
	WaitForSingleObject(done_event, INFINITE);

	EXCEPTION_HANDLER_END

	return 0;
}

//#include "BrutalDownload_c_ast.c"
